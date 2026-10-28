#ifndef TITLEBAR_H
#define TITLEBAR_H

// Custom dark title bar for the frameless mixer window.
// Header-only and without Q_OBJECT on purpose (no moc step needed).

#include <QWidget>
#include <QString>
#include <QByteArray>
#include <QStringList>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QSysInfo>
#include <QSettings>
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include "Config.h"
#include <QLabel>
#include <QAbstractButton>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPolygon>
#include <QRegion>
#include <QResizeEvent>
#include <QLinearGradient>
#include <QPixmap>
#include <QFont>
#include <QFontMetrics>
#include <QTimer>
#include <QEvent>
#include <QMouseEvent>
#include <QWindow>
#include <QPalette>
#include <QOperatingSystemVersion>
#include <QLibrary>
#include <QVariant>
#include <QGraphicsDropShadowEffect>
#include <QVariantAnimation>

#include "Theme.h"

class TitleButton : public QAbstractButton
{
public:
    enum Kind { Minimize, Close };

    explicit TitleButton(Kind k, QWidget *parent = nullptr) : QAbstractButton(parent), kind(k)
    {
        setFixedSize(40, 26);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::ArrowCursor);
    }

protected:
    void enterEvent(QEvent *) override { hover = true; update(); }
    void leaveEvent(QEvent *) override { hover = false; update(); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QColor fg = QColor("#e8eaf0");
        QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);

        if (hover || isDown()) {
            QColor bg = (kind == Close) ? QColor("#e5484d") : QColor("#ffffff");
            bg.setAlpha(kind == Close ? (isDown() ? 255 : 230) : 60);
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(r, 6, 6);
            if (kind == Close)
                fg = QColor("#ffffff");
        }

        p.setPen(QPen(fg, 1.6, Qt::SolidLine, Qt::RoundCap));
        QPointF c = rect().center();
        if (kind == Minimize) {
            p.drawLine(QPointF(c.x() - 5, c.y() + 3), QPointF(c.x() + 5, c.y() + 3));
        } else {
            p.drawLine(QPointF(c.x() - 4.5, c.y() - 4.5), QPointF(c.x() + 4.5, c.y() + 4.5));
            p.drawLine(QPointF(c.x() - 4.5, c.y() + 4.5), QPointF(c.x() + 4.5, c.y() - 4.5));
        }
    }

private:
    Kind kind;
    bool hover = false;
};


class TitleBar : public QWidget
{
public:
    TitleButton *minimizeButton;
    TitleButton *closeButton;

    explicit TitleBar(const QString &title, QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(36);
        setAutoFillBackground(false);

        QHBoxLayout *lay = new QHBoxLayout(this);
        lay->setContentsMargins(12, 0, 6, 0);
        lay->setSpacing(8);

        iconLabel = new QLabel(this);
        iconLabel->setFixedSize(22, 22);
        iconLabel->setScaledContents(true);
        iconLabel->setPixmap(QPixmap(":/Icons/App/pthk.png"));
        iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        lay->addWidget(iconLabel);

        titleLabel = new QLabel(title, this);
        QFont f = titleLabel->font();
        f.setPointSize(12);
        f.setBold(true);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
        f.setStyleStrategy(QFont::PreferAntialias);
        titleLabel->setFont(f);
        titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        lay->addWidget(titleLabel);

        lay->addStretch(1);

        minimizeButton = new TitleButton(TitleButton::Minimize, this);
        closeButton = new TitleButton(TitleButton::Close, this);
        lay->addWidget(minimizeButton);
        lay->addWidget(closeButton);

        refresh();
    }

    void setTitle(const QString &t) { titleLabel->setText(t); QTimer::singleShot(0, this, [this]() { fitTitle(); }); }

    // shrink the title font until the whole text fits between the icon and the buttons
    void fitTitle()
    {
        QWidget *right = minimizeButton->isVisible() ? static_cast<QWidget *>(minimizeButton)
                                                     : static_cast<QWidget *>(closeButton);
        int avail = right->x() - titleLabel->x() - 8;
        if (avail < 40)
            return;
        QFont f = titleLabel->font();
        for (int pt = 12; pt >= 7; --pt)
        {
            f.setPointSize(pt);
            f.setLetterSpacing(QFont::AbsoluteSpacing, pt >= 11 ? 0.6 : 0.0);
            if (QFontMetrics(f).horizontalAdvance(titleLabel->text()) + 8 <= avail)
                break;
        }
        titleLabel->setFont(f);
    }

protected:
    void resizeEvent(QResizeEvent *e) override { QWidget::resizeEvent(e); fitTitle(); }
    bool isDarkTheme() const { return palette().color(QPalette::Window).lightness() < 128; }
    QColor neon() const { return palette().color(QPalette::Link); }

    void refresh()
    {
        // crisp white title text on the always-dark title bar (no glow)
        titleLabel->setStyleSheet("color: #ffffff; background: transparent;");
        update();
    }

    void changeEvent(QEvent *e) override
    {
        if (e->type() == QEvent::PaletteChange || e->type() == QEvent::ApplicationPaletteChange)
        {
            refresh();
        }
        QWidget::changeEvent(e);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && window() && window()->windowHandle()) {
            window()->windowHandle()->startSystemMove();
            e->accept();
            return;
        }
        QWidget::mousePressEvent(e);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRectF r = QRectF(rect());
        QLinearGradient bg(0, 0, 0, r.height());
        bg.setColorAt(0, QColor("#23262e"));
        bg.setColorAt(1, QColor("#171a20"));
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(r, 9, 9);
        p.drawRect(QRectF(0, r.height() / 2, r.width(), r.height() / 2));   // square bottom corners

        // hairline separator under the bar
        p.setPen(QPen(QColor("#3a3f4b"), 1));
        p.drawLine(QPointF(0, r.height() - 0.5), QPointF(r.width(), r.height() - 0.5));
    }

private:
    QLabel *iconLabel;
    QLabel *titleLabel;
};

// Rounded window corners.
// Windows 11: ask DWM for its native (anti-aliased) rounded corners - no mask, no transparency, so
// nothing can turn see-through. Windows 10 and older: plain square corners (also crisp, no mask).
namespace WinCorners
{
    inline bool isWin11()
    {
#ifdef Q_OS_WIN
        const QOperatingSystemVersion v = QOperatingSystemVersion::current();
        return v.type() == QOperatingSystemVersion::Windows && v.microVersion() >= 22000;
#else
        return false;
#endif
    }

    // call after the native window exists (e.g. on the first Show event); safe to call repeatedly
    inline void apply(QWidget *w)
    {
#ifdef Q_OS_WIN
        if (!w || !w->isWindow() || !isWin11() || w->property("_dwmCorners").toBool())
            return;
        typedef long (__stdcall *DwmSetWindowAttributeFn)(void *, unsigned long, const void *, unsigned long);
        static QLibrary dwm(QStringLiteral("dwmapi"));
        static DwmSetWindowAttributeFn fn = reinterpret_cast<DwmSetWindowAttributeFn>(dwm.resolve("DwmSetWindowAttribute"));
        if (!fn)
            return;
        void *hwnd = reinterpret_cast<void *>(w->winId());
        const int round = 2;                       // DWMWCP_ROUND
        fn(hwnd, 33, &round, sizeof(round));       // DWMWA_WINDOW_CORNER_PREFERENCE
        const unsigned long border = 0x0052453F;   // COLORREF 0x00BBGGRR = #3f4552
        fn(hwnd, 34, &border, sizeof(border));     // DWMWA_BORDER_COLOR
        w->setProperty("_dwmCorners", true);
#else
        Q_UNUSED(w);
#endif
    }

    inline double radius() { return isWin11() ? 8.0 : 0.0; }
}

// Transparent overlay on top of a frameless window: draws the plain 1px outline that follows the
// window corners (rounded on Windows 11, square elsewhere).
class FrameOverlay : public QWidget
{
public:
    explicit FrameOverlay(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        parent->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == parentWidget())
        {
            if (e->type() == QEvent::Resize)
            {
                setGeometry(parentWidget()->rect());
                raise();
            }
            else if (e->type() == QEvent::Show)
                WinCorners::apply(parentWidget());
        }
        return QWidget::eventFilter(o, e);
    }

    void paintEvent(QPaintEvent *) override
    {
        QWidget *w = parentWidget();
        if (w && (w->isMaximized() || w->isFullScreen()))
            return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#3f4552"), 1.0));
        p.setBrush(Qt::NoBrush);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const double rad = WinCorners::radius();
        if (rad > 0.0)
            p.drawRoundedRect(r, rad - 0.5, rad - 0.5);
        else
            p.drawRect(r);
    }
};

#endif // TITLEBAR_H
