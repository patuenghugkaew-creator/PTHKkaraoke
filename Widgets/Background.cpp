#include "Background.h"

#include <QPainter>
#include <QDirIterator>
#include <QMediaPlayer>
#include <algorithm>
#include <random>
#include "VideoFrameSurface.h"
#include <QVideoSurfaceFormat>
#include <functional>
#include <QImageReader>
#include <QPointer>
#include <QApplication>
#include <QResizeEvent>
#include <QCollator>
#include <thread>

Background::Background(QWidget *parent) :
    QWidget(parent),
    _color(81, 81, 81)
{
    _slideTimer.setSingleShot(true);
    connect(&_slideTimer, &QTimer::timeout, this, [this]() {
        if (!_slides.isEmpty())
            loadSlide((_slideIndex + 1) % _slides.count(), true);
    });
    _animTimer.setInterval(16);
    connect(&_animTimer, &QTimer::timeout, this, [this]() {
        if (_animClock.elapsed() >= _slideOpt.transitionMs) {
            _animTimer.stop();
            _slidePrev = QImage();      // free it
        }
        update();
    });
}

void Background::setBackgroundType(Background::BackgroundType t)
{
    _bgType = t;
    updateVideoPlayback();
    updateSlideshow();
    update();
}

void Background::setBackgroundColor(const QColor &color)
{
    _color = color;
    update();
}

void Background::setBackgroundColor(const QString &colorName)
{
    setBackgroundColor(QColor(colorName));
}

void Background::setBackgroundImage(const QString &imgFile)
{
    if (imgFile.isEmpty())
        return;

    QImage img(imgFile);
    if (img.isNull())
        return;

    _image = img;
    _imageName = imgFile;
    update();
}

int Background::setBackgroundVideoDir(const QString &dir)
{
    _videoDir = dir;

    if (_videoPlayer == nullptr) {
        _videoPlayer = new QMediaPlayer(this);
        _videoPlayer->setMuted(true);
        _surface = new VideoFrameSurface(this);
        // present() may run on the decoder's thread: hand the frame to the GUI thread
        _surface->onFrame = [this](const QImage &img) {
            QMetaObject::invokeMethod(this, [this, img]() {
                _frame = img;
                update();
            }, Qt::QueuedConnection);
        };
        _videoPlayer->setVideoOutput(_surface);

        // Own playlist instead of QMediaPlaylist::Loop: a file that cannot be played made the
        // playlist jump to the "next" (the same) file again and again until the stack overflowed.
        // Here the next file is started from the event loop, and a full round of failures stops it.
        connect(_videoPlayer, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
            if (s == QMediaPlayer::EndOfMedia) {
                _videoFails = 0;
                QMetaObject::invokeMethod(this, [this]() { playNextVideo(); }, Qt::QueuedConnection);
            } else if (s == QMediaPlayer::InvalidMedia) {
                if (++_videoFails < _videos.count())
                    QMetaObject::invokeMethod(this, [this]() { playNextVideo(); }, Qt::QueuedConnection);
            }
        });
    }

    _videoPlayer->stop();
    _videos.clear();
    _videoIndex = -1;
    _videoFails = 0;
    _frame = QImage();

    if (!dir.isEmpty()) {
        // (a named list: a range-for over "QStringList() << ..." would iterate a destroyed temporary)
        const QStringList exts = { "mp4", "mkv", "avi", "wmv", "mpg", "mpeg", "mov" };
        QStringList filters;
        for (const QString &ext : exts)
            filters << "*." + ext << "*." + ext.toUpper();
        QDirIterator it(dir, filters, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            _videos << it.next();
        std::shuffle(_videos.begin(), _videos.end(), std::mt19937(std::random_device{}()));
    }

    updateVideoPlayback();
    return _videos.count();
}

void Background::playNextVideo()
{
    if (_videos.isEmpty() || _bgType != Video || _videoPaused)
        return;
    _videoIndex = (_videoIndex + 1) % _videos.count();
    _videoPlayer->setMedia(QUrl::fromLocalFile(_videos[_videoIndex]));
    _videoPlayer->play();
}

void Background::setVideoPaused(bool paused)
{
    _videoPaused = paused;
    updateVideoPlayback();
    updateSlideshow();
}

// ---------------- slideshow ----------------

QStringList Background::imageExtensions()
{
    QStringList exts;
    for (const QByteArray &f : QImageReader::supportedImageFormats()) {
        const QString e = QString::fromLatin1(f).toLower();
        if (e != "pbm" && e != "pgm" && e != "ppm" && e != "xbm" && e != "xpm" && e != "cur" && e != "svgz")
            exts << e;
    }
    exts << "jpeg" << "jpg" << "png" << "bmp";
    exts.removeDuplicates();
    return exts;
}

QStringList Background::slideshowImages(const QString &dir)
{
    QStringList files;
    if (dir.isEmpty())
        return files;
    QStringList filters;
    for (const QString &e : imageExtensions())
        filters << "*." + e << "*." + e.toUpper();
    QDirIterator it(dir, filters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        files << it.next();
    QCollator c;
    c.setNumericMode(true);         // pic2 before pic10
    std::sort(files.begin(), files.end(), [&c](const QString &x, const QString &y) { return c.compare(x, y) < 0; });
    return files;
}

int Background::setSlideshow(const SlideshowOptions &o)
{
    const bool sameDir = (o.dir == _slideOpt.dir) && o.shuffle == _slideOpt.shuffle && !_slides.isEmpty();
    const bool sameLook = o.fill == _slideOpt.fill;
    _slideOpt = o;
    _slideOpt.intervalSec = qBound(2, o.intervalSec, 3600);
    _slideOpt.transitionMs = qBound(0, o.transitionMs, 5000);
    if (!sameDir) {
        _slides = slideshowImages(o.dir);
        if (o.shuffle)
            std::shuffle(_slides.begin(), _slides.end(), std::mt19937(std::random_device{}()));
        _slideIndex = -1;
        _slideCur = QImage();
        _slidePrev = QImage();
        ++_slideGen;
        _slideLoading = false;
    } else if (!sameLook && _slideIndex >= 0) {
        loadSlide(_slideIndex, false);      // fill / fit changed: redraw the current picture
    }
    _slideTimer.stop();
    updateSlideshow();
    return _slides.count();
}

void Background::updateSlideshow()
{
    const bool run = _bgType == Slideshow && !_videoPaused && !_slides.isEmpty();
    if (!run) {
        _slideTimer.stop();
        _animTimer.stop();
        return;
    }
    if (_slideCur.isNull() && !_slideLoading)
        loadSlide(_slideIndex < 0 ? 0 : _slideIndex, false);
    else if (!_slideTimer.isActive() && !_slideLoading)
        _slideTimer.start(_slideOpt.intervalSec * 1000);
}

// Pictures are read and scaled on a worker thread (a big photo takes a moment), already at
// the screen's size, so the transitions only copy pixels.
void Background::loadSlide(int index, bool withTransition)
{
    if (_slides.isEmpty())
        return;
    index = qBound(0, index, _slides.count() - 1);
    _slideLoading = true;
    const int gen = ++_slideGen;
    const QString file = _slides[index];
    const qreal dpr = devicePixelRatioF();
    const QSize target = (QSizeF(qMax(width(), 640), qMax(height(), 360)) * dpr).toSize();
    const bool fill = _slideOpt.fill;
    QPointer<Background> self(this);
    std::thread([self, gen, index, file, target, fill, dpr, withTransition]() {
        QImageReader r(file);
        r.setAutoTransform(true);           // phone photos: EXIF rotation
        QSize src = r.size();
        if (src.isValid() && !r.transformation().testFlag(QImageIOHandler::TransformationRotate90) ) {
            // read big JPEGs already reduced (much faster)
            const QSize want = src.scaled(target, fill ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
            if (want.width() < src.width())
                r.setScaledSize(want);
        }
        QImage img = r.read();
        if (!img.isNull()) {
            QImage scaled = img.scaled(target, fill ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
            if (fill && scaled.size() != target)        // crop the overflow, keep the middle
                scaled = scaled.copy((scaled.width() - target.width()) / 2, (scaled.height() - target.height()) / 2,
                                     target.width(), target.height());
            img = scaled.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            img.setDevicePixelRatio(dpr);
        }
        QMetaObject::invokeMethod(qApp, [self, gen, index, img, withTransition]() {
            if (self)
                self->slideLoaded(gen, index, img, withTransition);
        }, Qt::QueuedConnection);
    }).detach();
}

void Background::slideLoaded(int generation, int index, const QImage &img, bool withTransition)
{
    if (generation != _slideGen)
        return;                             // an older request
    _slideLoading = false;
    _slideIndex = index;
    if (img.isNull()) {                     // unreadable file: try the next one
        if (++_slideFails < _slides.count())
            loadSlide((index + 1) % _slides.count(), withTransition);
        return;
    }
    _slideFails = 0;
    const bool animate = withTransition && !_slideCur.isNull() && _slideOpt.transitionMs > 0 && _slideOpt.transition != 4;
    _slidePrev = animate ? _slideCur : QImage();
    _slideCur = img;
    _effect = _slideOpt.transition == 3 ? int(std::random_device{}() % 3) : _slideOpt.transition;
    if (animate) {
        _animClock.start();
        _animTimer.start();
    }
    update();
    if (_bgType == Slideshow && !_videoPaused)
        _slideTimer.start(_slideOpt.intervalSec * 1000 + (animate ? _slideOpt.transitionMs : 0));
}

// draws a picture centred, with opacity, moved sideways by dx (pixels) and zoomed by scale
void Background::drawSlide(QPainter &p, const QImage &img, qreal opacity, qreal dx, qreal scale)
{
    if (img.isNull() || opacity <= 0.0)
        return;
    const QSizeF sz = QSizeF(img.size()) / img.devicePixelRatio();
    QRectF r(QPointF((width() - sz.width()) / 2.0 + dx, (height() - sz.height()) / 2.0), sz);
    p.setOpacity(opacity);
    if (qFuzzyCompare(scale, 1.0)) {
        p.drawImage(r.topLeft(), img);
    } else {
        const QPointF c = r.center();
        QSizeF s2 = sz * scale;
        p.drawImage(QRectF(QPointF(c.x() - s2.width() / 2, c.y() - s2.height() / 2), s2), img);
    }
    p.setOpacity(1.0);
}

void Background::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // full screen / window: read the current picture again at the new size
    if (_bgType == Slideshow && _slideIndex >= 0 && !_slideCur.isNull()) {
        const QSize was = event->oldSize();
        if (!was.isValid() || qAbs(was.width() - width()) > 8 || qAbs(was.height() - height()) > 8)
            loadSlide(_slideIndex, false);
    }
}

void Background::updateVideoPlayback()
{
    if (_videoPlayer == nullptr)
        return;

    bool run = _bgType == Video && !_videoPaused && !_videos.isEmpty();
    if (run && _videoPlayer->state() != QMediaPlayer::PlayingState) {
        if (_videoIndex < 0)
            playNextVideo();
        else
            _videoPlayer->play();
    } else if (!run && _videoPlayer->state() == QMediaPlayer::PlayingState) {
        _videoPlayer->pause();
    }
}

void Background::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);

    switch (_bgType) {
    case Color: {
        // soft vertical gradient of the chosen colour, darker towards the lyrics at the bottom
        QLinearGradient g(rect().topLeft(), rect().bottomLeft());
        g.setColorAt(0.0, _color.lighter(125));
        g.setColorAt(0.55, _color);
        g.setColorAt(1.0, _color.darker(220));
        painter.fillRect(rect(), g);
        break;
    }
    case Image:
        painter.drawImage(rect(), _image);
        break;
    case Slideshow: {
        painter.fillRect(rect(), Qt::black);
        if (_slideCur.isNull()) {
            // nothing yet (loading, or an empty folder): the default picture
            if (!_image.isNull())
                painter.drawImage(rect(), _image);
            break;
        }
        qreal t = 1.0;
        if (!_slidePrev.isNull() && _slideOpt.transitionMs > 0) {
            t = qBound(0.0, _animClock.elapsed() / qreal(_slideOpt.transitionMs), 1.0);
            t = t * t * (3.0 - 2.0 * t);        // ease in / out
        }
        if (_slidePrev.isNull() || t >= 1.0) {
            drawSlide(painter, _slideCur, 1.0, 0.0, 1.0);
        } else if (_effect == 1) {              // slide: the new picture pushes the old one out
            drawSlide(painter, _slidePrev, 1.0, -t * width(), 1.0);
            drawSlide(painter, _slideCur, 1.0, (1.0 - t) * width(), 1.0);
        } else if (_effect == 2) {              // zoom: the new picture settles in while it fades in
            drawSlide(painter, _slidePrev, 1.0, 0.0, 1.0);
            drawSlide(painter, _slideCur, t, 0.0, 1.12 - 0.12 * t);
        } else {                                // fade
            drawSlide(painter, _slidePrev, 1.0, 0.0, 1.0);
            drawSlide(painter, _slideCur, t, 0.0, 1.0);
        }
        break;
    }
    case Video:
        painter.fillRect(rect(), Qt::black);
        if (!_frame.isNull()) {
            // fill the whole area, keep the picture's proportions (crop the overflow)
            QSize s = _frame.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
            QRect target(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(target, _frame);
        }
        break;
    }
}
