#ifndef BACKGROUND_H
#define BACKGROUND_H

#include <QWidget>
#include <QImage>

class QMediaPlayer;
class VideoFrameSurface;

class Background : public QWidget
{
    Q_OBJECT
public:
    enum BackgroundType {
        Color = 0,
        Image,
        Video
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

protected:
    void paintEvent(QPaintEvent *event);

private:
    void updateVideoPlayback();
    void playNextVideo();

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
};

#endif // BACKGROUND_H
