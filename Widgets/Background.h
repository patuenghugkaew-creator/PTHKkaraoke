#ifndef BACKGROUND_H
#define BACKGROUND_H

#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QStringList>

class QMediaPlayer;
class VideoFrameSurface;

class Background : public QWidget
{
    Q_OBJECT
public:
    enum BackgroundType {
        Color = 0,
        Image,
        Video,
        Slideshow
    };

    // Slideshow type: the pictures of a folder, one after another, with a transition
    struct SlideshowOptions {
        QString dir;
        int intervalSec = 10;       // time each picture stays
        int transition = 0;         // 0 fade, 1 slide, 2 zoom, 3 random, 4 none
        int transitionMs = 1200;    // length of the transition
        bool shuffle = true;
        bool fill = true;           // true: fill the screen (crop), false: whole picture (black bars)
    };

    explicit Background(QWidget *parent = nullptr);

    BackgroundType backgroundType() { return _bgType; }
    void setBackgroundType(BackgroundType t);

    QColor backgroundColor() { return _color; }
    void setBackgroundColor(const QColor &color);
    void setBackgroundColor(const QString &colorName);

    QString backgroundImage() { return _imageName; }
    void setBackgroundImage(const QString &imgFile);

    // Video type: every video file in the folder, shuffled and looped, without sound.
    // Frames are painted by this widget, so the lyrics above stay visible.
    QString backgroundVideoDir() { return _videoDir; }
    int setBackgroundVideoDir(const QString &dir);     // returns the number of videos found
    void setVideoPaused(bool paused);                  // e.g. while a karaoke video song plays

    int setSlideshow(const SlideshowOptions &o);       // returns the number of pictures found
    SlideshowOptions slideshow() const { return _slideOpt; }
    static QStringList slideshowImages(const QString &dir);
    static QStringList imageExtensions();              // jpg, png, webp... (what Qt can read here)

protected:
    void paintEvent(QPaintEvent *event);
    void resizeEvent(QResizeEvent *event);

private:
    void updateVideoPlayback();
    void playNextVideo();
    void updateSlideshow();
    void loadSlide(int index, bool withTransition);
    void slideLoaded(int generation, int index, const QImage &img, bool withTransition);
    void drawSlide(QPainter &p, const QImage &img, qreal opacity, qreal dx, qreal scale);

    BackgroundType _bgType = Color;
    QColor _color;
    QImage _image;
    QString _imageName;

    QString _videoDir;
    QMediaPlayer *_videoPlayer = nullptr;
    QStringList _videos;        // shuffled once per folder
    int _videoIndex = -1;
    int _videoFails = 0;        // files in a row that could not be played
    VideoFrameSurface *_surface = nullptr;
    QImage _frame;
    bool _videoPaused = false;

    SlideshowOptions _slideOpt;
    QStringList _slides;
    int _slideIndex = -1;
    int _slideGen = 0;          // results of older loads are ignored
    int _slideFails = 0;
    bool _slideLoading = false;
    QImage _slideCur, _slidePrev;
    int _effect = 0;
    QTimer _slideTimer;         // next picture
    QTimer _animTimer;          // repaints during a transition only
    QElapsedTimer _animClock;
};

#endif // BACKGROUND_H
