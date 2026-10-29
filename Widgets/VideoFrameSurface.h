#ifndef VIDEOFRAMESURFACE_H
#define VIDEOFRAMESURFACE_H

#include <QAbstractVideoSurface>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QVideoSurfaceFormat>
#include <QWidget>
#include <functional>

// Receives decoded frames and hands them on as QImages.
class VideoFrameSurface : public QAbstractVideoSurface
{
public:
    explicit VideoFrameSurface(QObject *parent) : QAbstractVideoSurface(parent) {}

    std::function<void(const QImage &)> onFrame;

    QList<QVideoFrame::PixelFormat> supportedPixelFormats(QAbstractVideoBuffer::HandleType type) const override
    {
        if (type != QAbstractVideoBuffer::NoHandle)
            return {};
        // decoders usually hand out YUV (NV12 for H.264 on Windows); QVideoFrame::image() converts
        return { QVideoFrame::Format_RGB32, QVideoFrame::Format_ARGB32, QVideoFrame::Format_RGB24,
                 QVideoFrame::Format_BGR32, QVideoFrame::Format_NV12, QVideoFrame::Format_YV12,
                 QVideoFrame::Format_YUV420P, QVideoFrame::Format_YUYV, QVideoFrame::Format_UYVY };
    }

    bool present(const QVideoFrame &frame) override
    {
        QImage img = frame.image();     // copies, any supported pixel format
        if (img.isNull())
            return true;
        if (surfaceFormat().scanLineDirection() == QVideoSurfaceFormat::BottomToTop)
            img = img.mirrored(false, true);
        if (onFrame)
            onFrame(img);
        return true;
    }
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
            QMetaObject::invokeMethod(this, [this, img]() {
                setFrame(img);
                if (onFrameShown)
                    onFrameShown(img);
            }, Qt::QueuedConnection);
        };
    }

    std::function<void(const QImage &)> onFrameShown;   // e.g. to mirror the video on the 2nd screen

    QAbstractVideoSurface *videoSurface() const { return surface; }
    void setFrame(const QImage &img) { frame = img; pixmap = QPixmap(); hasPixmap = false; update(); }
    void setPixmap(const QPixmap &pm) { pixmap = pm; frame = QImage(); hasPixmap = true; update(); }
    void clear() { frame = QImage(); pixmap = QPixmap(); hasPixmap = false; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QSize frameSize = hasPixmap ? pixmap.size() : frame.size();
        if (frameSize.isEmpty()) {
            p.fillRect(rect(), Qt::black);
            return;
        }

        QSize s = frameSize.scaled(size(), Qt::KeepAspectRatio);
        QRect r(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);

        // Fill black only in letterbox/pillarbox margins (avoid full-screen fill overhead)
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
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(r, frame);
        }
    }

private:
    VideoFrameSurface *surface;
    QImage frame;
    QPixmap pixmap;
    bool hasPixmap = false;
};

#endif // VIDEOFRAMESURFACE_H
