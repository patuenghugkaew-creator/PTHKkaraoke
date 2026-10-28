#ifndef SECONDMONITORDIALOG_H
#define SECONDMONITORDIALOG_H

#include "Widgets/Background.h"
#include "Widgets/LyricsWidget.h"
#include "Widgets/VideoFrameSurface.h"

#include <QDialog>

namespace Ui {
class SecondMonitorDialog;
}

class SecondMonitorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SecondMonitorDialog(QWidget *parent = 0, LyricsWidget *lyr = nullptr);
    ~SecondMonitorDialog();

    Background *backgroundWidget();

    LyricsWidget *lyrWidget();

    // full screen on a monitor (no frame) or a normal resizable window
    void showOnScreen(QScreen *screen);
    void showAsWindow();
    void setClosingByPlayer() { closingByPlayer = true; }

    // video songs / YouTube mirrored from the player's screen
    void setVideoFrame(const QImage &img) { videoView->setFrame(img); }
    void setVideoVisible(bool on);

signals:
    void closeRequested();      // "ปิดจอที่ 2" in the right-click menu: the player closes it

protected:
    void resizeEvent(QResizeEvent *event);
    void contextMenuEvent(QContextMenuEvent *event);
    // keys typed while this window has the focus (Home, End, song numbers...) go to the player
    void keyPressEvent(QKeyEvent *event);
    void keyReleaseEvent(QKeyEvent *event);
    void reject();              // Esc must not close the 2nd screen
    void closeEvent(QCloseEvent *event);    // window X: let the player close it properly

private:
    Ui::SecondMonitorDialog *ui;
    Background *bgWidget = nullptr;
    FrameVideoWidget *videoView = nullptr;
    bool closingByPlayer = false;
};

#endif // SECONDMONITORDIALOG_H
