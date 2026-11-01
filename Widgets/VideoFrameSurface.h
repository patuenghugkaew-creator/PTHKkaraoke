#ifndef VIDEOFRAMESURFACE_H
#define VIDEOFRAMESURFACE_H

#include <QAbstractVideoSurface>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QVideoSurfaceFormat>
#include <QWidget>
#include <QElapsedTimer>
#include <atomic>
#include <functional>

// Receives decoded frames and hands them on as QImages.
class VideoFrameSurface : public QAbstractVideoSurface
{
public:
    explicit VideoFrameSurface(QObject *parent) : QAbstractVideoSurface(parent) {}

    std::function<void(const QImage &)> onFrame;
    std::atomic<qint64> lastPts{-1};            // media time (us) of the last frame handed on
    std::atomic<int> skippedLate{0};

    // The song's clock (ms), from the player's position: frames already behind it are skipped
    // instead of being converted and shown late. Without this, a picture that fell behind once
    // (busy CPU) never caught up: every late frame was still converted, at full cost.
    void setClock(qint64 ms)
    {
        if (!clock.isValid())
            clock.start();
        clockPos.store(ms);
        clockAt.store(clock.elapsed());
    }
    void resetClock() { clockPos.store(-1); }

    QList<QVideoFrame::PixelFormat> supportedPixelFormats(QAbstractVideoBuffer::HandleType type) const override
    {
        if (type != QAbstractVideoBuffer::NoHandle)
            return {};
        // Prioritize lightweight formats that don't need heavy CPU conversion
        return { QVideoFrame::Format_RGB32, QVideoFrame::Format_ARGB32,
                 QVideoFrame::Format_BGR32, QVideoFrame::Format_NV12,
                 QVideoFrame::Format_YUV420P, QVideoFrame::Format_YV12 };
    }

    bool present(const QVideoFrame &frame) override
    {
        if (!frame.isValid())
            return true;
        const qint64 pos = clockPos.load();
        if (pos >= 0 && frame.startTime() >= 0 && clock.isValid()) {
            const qint64 since = clock.elapsed() - clockAt.load();
            const qint64 now = pos + (since < 500 ? since : 0);     // paused: no running ahead
            if (frame.startTime() / 1000 < now - 60) {
                ++skippedLate;
                return true;
            }
        }
        // 60 frames a second: every other one (30 a second look smooth; half the work)
        const qint64 t = frame.startTime();
        if (t >= 0) {
            const qint64 prev = lastPts.load();
            if (prev >= 0 && t > prev && t - prev < 30000)
                return true;
        }
        QImage img = frame.image();     // copies, any supported pixel format
        if (img.isNull())
            return true;
        if (surfaceFormat().scanLineDirection() == QVideoSurfaceFormat::BottomToTop)
            img = img.mirrored(false, true);
        // แปลงเป็น RGB32 ให้เสร็จใน Background Thread เพื่อให้ QPainter วาดลงจอได้ทันทีโดยไม่ต้องแปลงสีซ้ำใน UI Thread
        if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32_Premultiplied)
            img = std::move(img).convertToFormat(QImage::Format_RGB32);
        lastPts.store(frame.startTime());
        if (onFrame)
            onFrame(img);
        return true;
    }

private:
    QElapsedTimer clock;
    std::atomic<qint64> clockPos{-1}, clockAt{0};
};

// Shows the karaoke video songs. Paints the frames itself instead of QVideoWidget, whose
// native Media Foundation video window stays black once Qt WebEngine is in the program.
class FrameVideoWidget : public QWidget
{
public:
    explicit FrameVideoWidget(QWidget *parent) : QWidget(parent), surface(new VideoFrameSurface(this))
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
        surface->onFrame = [this](const QImage &img) {
            // Drop frame if previous frame is still pending in UI thread to prevent queue congestion
            if (framePending.exchange(true)) {
                ++statDropped;
                return;
            }
            const qint64 pts = surface->lastPts.load();
            QMetaObject::invokeMethod(this, [this, img, pts]() {
                framePending.store(false);
                shownPts.store(pts);
                ++statShown;
                setFrame(img);
                if (onFrameShown)
                    onFrameShown(img);
            }, Qt::QueuedConnection);
        };
    }

    std::function<void(const QImage &)> onFrameShown;   // e.g. to mirror the video on the 2nd screen

    QAbstractVideoSurface *videoSurface() const { return surface; }
    VideoFrameSurface *frameSurface() const { return surface; }
    // diagnostics: media time (us) of the frame on screen; frames shown / dropped since the last read
    qint64 shownTimeUs() const { return shownPts.load(); }
    int takeShown() { return statShown.exchange(0); }
    int takeDropped() { return statDropped.exchange(0); }
    int takeSkippedLate() { return surface->skippedLate.exchange(0); }
    // a new song: nothing of the previous one is on screen any more (its frame time is forgotten)
    void newSong()
    {
        shownPts.store(-1);
        surface->lastPts.store(-1);
        surface->resetClock();
    }
    void setFrame(const QImage &img) { frame = img; pixmap = QPixmap(); hasPixmap = false; update(); }
    void setPixmap(const QPixmap &pm) { pixmap = pm; frame = QImage(); hasPixmap = true; update(); }
    void clear() { frame = QImage(); pixmap = QPixmap(); hasPixmap = false; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.setRenderHint(QPainter::Antialiasing, false);

        const QSize frameSize = hasPixmap ? pixmap.size() : frame.size();
        if (frameSize.isEmpty()) {
            p.fillRect(rect(), Qt::black);
            return;
        }

        QSize s = frameSize.scaled(size(), Qt::KeepAspectRatio);
        QRect r(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);

        // ระบายสีดำเฉพาะขอบที่เหลือ (ไม่ระบายทับพื้นที่วิดีโอตรงกลาง)
        if (r.top() > 0)
            p.fillRect(0, 0, width(), r.top(), Qt::black);
        if (r.bottom() < height() - 1)
            p.fillRect(0, r.bottom() + 1, width(), height() - r.bottom() - 1, Qt::black);
        if (r.left() > 0)
            p.fillRect(0, r.top(), r.left(), r.height(), Qt::black);
        if (r.right() < width() - 1)
            p.fillRect(r.right() + 1, r.top(), width() - r.right() - 1, r.height(), Qt::black);

        if (hasPixmap) {
            p.drawPixmap(r, pixmap);
        } else {
            p.drawImage(r, frame);
        }
    }

private:
    VideoFrameSurface *surface;
    QImage frame;
    QPixmap pixmap;
    bool hasPixmap = false;
    std::atomic<bool> framePending{false};
    std::atomic<qint64> shownPts{-1};
    std::atomic<int> statShown{0}, statDropped{0};
};

#endif // VIDEOFRAMESURFACE_H
