#ifndef TRANSITIONPREVIEWWIDGET_H
#define TRANSITIONPREVIEWWIDGET_H

#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFont>
#include <cmath>

class TransitionPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TransitionPreviewWidget(QWidget *parent = nullptr);
    ~TransitionPreviewWidget() override = default;

    void setMedleyParams(int waitBars,
                         bool quantizeBar,
                         bool skipIntro,
                         bool autoCutOutro,
                         int fillBars,
                         int fillPattern,
                         int fillVolume,
                         int tempoMode,
                         int medleyBpm,
                         double crossfadeSec,
                         int crossfadeCurve,
                         int activeTab);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int    _waitBars = 1;
    bool   _quantizeBar = true;
    bool   _skipIntro = true;
    bool   _autoCutOutro = true;
    int    _fillBars = 1;
    int    _fillPattern = 0;
    int    _fillVolume = 100;
    int    _tempoMode = 0;
    int    _medleyBpm = 120;
    double _crossfadeSec = 3.0;
    int    _crossfadeCurve = 0;
    int    _activeTab = 0;

    void drawMidiTransition(QPainter &p, const QRect &r);
    void drawAudioCrossfade(QPainter &p, const QRect &r);
    void drawStatusFooter(QPainter &p, const QRect &r);
};

#endif // TRANSITIONPREVIEWWIDGET_H
