#include "Background.h"

#include <QPainter>
#include <QDirIterator>
#include <QMediaPlayer>
#include <algorithm>
#include <random>
#include "VideoFrameSurface.h"
#include <QVideoSurfaceFormat>
#include <functional>

Background::Background(QWidget *parent) :
    QWidget(parent),
    _color(81, 81, 81)
{

}

void Background::setBackgroundType(Background::BackgroundType t)
{
    _bgType = t;
    updateVideoPlayback();
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
