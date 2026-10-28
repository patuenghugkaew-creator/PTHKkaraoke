#include "SecondMonitorDialog.h"
#include "ui_SecondMonitorDialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QScreen>
#include <QWindow>

SecondMonitorDialog::SecondMonitorDialog(QWidget *parent, LyricsWidget *lyr) :
    QDialog(parent),
    ui(new Ui::SecondMonitorDialog)
{
    bgWidget = new Background(this);
    videoView = new FrameVideoWidget(this);     // above the background, below the lyrics
    videoView->hide();
    videoView->setContextMenuPolicy(Qt::NoContextMenu);

    ui->setupUi(this);

    ui->lyr->setTextFont(lyr->textFont());
    ui->lyr->setTextColor(lyr->textColor());
    ui->lyr->setTextBorderColor(lyr->textBorderColor());
    ui->lyr->setTextBorderOutColor(lyr->textBorderOutColor());
    ui->lyr->setTextBorderWidth(lyr->textBorderWidth());
    ui->lyr->setTextBorderOutWidth(lyr->textBorderOutWidth());

    ui->lyr->setCurColor(lyr->curColor());
    ui->lyr->setCurBorderColor(lyr->curBorderColor());
    ui->lyr->setCurBorderOutColor(lyr->curBorderOutColor());
    ui->lyr->setCurBorderWidth(lyr->curBorderWidth());
    ui->lyr->setCurBorderOutWidth(lyr->curBorderOutWidth());

    ui->lyr->setLine1Position(lyr->line1Position());
    ui->lyr->setLine2Position(lyr->line2Position());
    ui->lyr->setLine1Y(lyr->line1Y());
    ui->lyr->setLine2Y(lyr->line2Y());
    ui->lyr->setAnimationTime(lyr->animationTime());
    ui->lyr->setAutoFontSize(lyr->isAutoFontSize());

    ui->lyr->setLyrics(lyr->lyrData(), lyr->curData());

    ui->lyr->hide();

    // the lyrics / background cover the whole window: their right-click goes to this window's menu
    ui->lyr->setContextMenuPolicy(Qt::NoContextMenu);
    bgWidget->setContextMenuPolicy(Qt::NoContextMenu);
}

SecondMonitorDialog::~SecondMonitorDialog()
{
    delete ui;

    delete bgWidget;
}

Background *SecondMonitorDialog::backgroundWidget()
{
    return bgWidget;
}

LyricsWidget *SecondMonitorDialog::lyrWidget()
{
    return ui->lyr;
}

void SecondMonitorDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    bgWidget->resize(size());
    videoView->resize(size());
}

void SecondMonitorDialog::setVideoVisible(bool on)
{
    if (!on)
        videoView->clear();
    videoView->resize(size());
    videoView->setVisible(on);
    bgWidget->setVideoPaused(on);       // the song video covers the background
}

void SecondMonitorDialog::showOnScreen(QScreen *screen)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setGeometry(screen->geometry());
    show();
    windowHandle()->setScreen(screen);
    showFullScreen();
}

void SecondMonitorDialog::showAsWindow()
{
    QScreen *screen = windowHandle() ? windowHandle()->screen() : qApp->primaryScreen();
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    QRect r = screen->availableGeometry();
    setGeometry(r.x() + r.width() / 8, r.y() + r.height() / 8, r.width() * 3 / 4, r.height() * 3 / 4);
    showNormal();
}

void SecondMonitorDialog::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *full = menu.addAction(isFullScreen() ? tr("แสดงเป็นหน้าต่าง") : tr("เต็มจอ (Full Screen)"));

    // one entry per monitor: put the lyrics screen on the TV / projector of choice
    QMenu *move = menu.addMenu(tr("ย้ายไปจอ"));
    const QList<QScreen *> screens = qApp->screens();
    for (int i = 0; i < screens.count(); i++) {
        QScreen *s = screens[i];
        QAction *a = move->addAction(tr("จอ %1  (%2 x %3)").arg(i + 1).arg(s->geometry().width()).arg(s->geometry().height()));
        a->setCheckable(true);
        a->setChecked(windowHandle() && windowHandle()->screen() == s);
        connect(a, &QAction::triggered, this, [this, s]() { showOnScreen(s); });
    }

    menu.addSeparator();
    QAction *close = menu.addAction(tr("ปิดจอที่ 2"));

    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == full) {
        if (isFullScreen())
            showAsWindow();
        else
            showOnScreen(windowHandle() ? windowHandle()->screen() : qApp->primaryScreen());
    } else if (chosen == close) {
        emit closeRequested();
    }
}

void SecondMonitorDialog::keyPressEvent(QKeyEvent *event)
{
    if (parentWidget())
        QCoreApplication::sendEvent(parentWidget(), event);
}

void SecondMonitorDialog::keyReleaseEvent(QKeyEvent *event)
{
    if (parentWidget())
        QCoreApplication::sendEvent(parentWidget(), event);
}

void SecondMonitorDialog::reject()
{
    // Esc is a player key (close the search box), not "close this window"
}

void SecondMonitorDialog::closeEvent(QCloseEvent *event)
{
    if (closingByPlayer) {
        event->accept();
        return;
    }
    event->ignore();
    emit closeRequested();
}
