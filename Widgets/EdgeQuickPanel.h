#ifndef EDGEQUICKPANEL_H
#define EDGEQUICKPANEL_H

// Quick buttons at the right edge of the main window, like a phone's edge panel.
//   * a thin handle sits at the right edge (only while the program is the active window)
//   * mouse held at the right edge for a moment, or a click / tap on the handle -> a column of
//     buttons slides out (applause, cheer, event sounds, key up / down, vocal, play / pause, next)
//   * it closes when the mouse leaves it, or after a few seconds without use
// Both parts are frameless tool windows that never take the focus, so typing still goes to the
// song search and they show over video / YouTube too. Header-only, no Q_OBJECT.

#include <QWidget>
#include <QTimer>
#include <QCursor>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QApplication>
#include <QElapsedTimer>
#include <QVariantAnimation>
#include <QVector>
#include <functional>
#include <cmath>

#include "BarButton.h"

namespace EdgeIcons {
using namespace BarIcons;

// two clapping hands
inline void clap(QPainter &p, const QRectF &r, const QColor &c)
{
    p.save();
    p.setPen(pen(r, c.lighter(115), 0.95));
    p.setBrush(duoFill(c, 135));
    for (int s : { -1, 1 }) {
        p.save();
        p.translate(at(r, 12 + s * 2.6, 15));
        p.rotate(s * 22);
        const double w = r.width() * 5.2 / 24.0, h = r.height() * 11.0 / 24.0;
        p.drawRoundedRect(QRectF(-w / 2, -h * 0.75, w, h), w / 2.2, w / 2.2);
        p.restore();
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(pen(r, c.lighter(135), 0.9));
    p.drawLine(at(r, 12, 2.2), at(r, 12, 4.4));
    p.drawLine(at(r, 6.5, 3.6), at(r, 7.8, 5.4));
    p.drawLine(at(r, 17.5, 3.6), at(r, 16.2, 5.4));
    p.restore();
}

// a burst (cheer)
inline void cheer(QPainter &p, const QRectF &r, const QColor &c)
{
    QPainterPath star;
    for (int i = 0; i < 10; ++i) {
        const double a = -1.5707963267949 + i * 0.62831853071796;
        const double rad = (i % 2 == 0) ? 8.8 : 3.8;
        const QPointF pt = at(r, 12 + rad * std::cos(a), 12.5 + rad * std::sin(a));
        if (i == 0)
            star.moveTo(pt);
        else
            star.lineTo(pt);
    }
    star.closeSubpath();
    p.setPen(pen(r, c.lighter(115), 0.95));
    p.setBrush(duoFill(c, 135));
    p.drawPath(star);
}

// the note icon with a + or - beside it
inline Fn keyStep(bool up)
{
    return [up](QPainter &p, const QRectF &r, const QColor &c) {
        note(p, QRectF(r.left() - r.width() * 0.12, r.top(), r.width(), r.height()), c);
        p.setPen(pen(r, c.lighter(140), 1.15));
        const QPointF m = at(r, 20.0, 6.0);
        const double d = r.width() * 3.0 / 24.0;
        p.drawLine(QPointF(m.x() - d, m.y()), QPointF(m.x() + d, m.y()));
        if (up)
            p.drawLine(QPointF(m.x(), m.y() - d), QPointF(m.x(), m.y() + d));
    };
}
} // namespace EdgeIcons

class EdgeQuickPanel : public QWidget
{
public:
    struct Item {
        QString label;
        BarIcons::Fn icon;
        QColor color;
        bool lit = false;                       // shown as on (sound playing, vocal cut ...)
        std::function<void()> run;
        std::function<void(Item &)> refresh;    // label / icon / lit from the program's state
    };

    // `host`: the main window; `allowed`: false while the panel must not open (dialogs, locked ...)
    EdgeQuickPanel(QWidget *host, std::function<bool()> allowed = nullptr)
        : QWidget(host, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus),
          host(host), allowed(std::move(allowed)), handle(new Handle(host))
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        handle->onClick = [this]() { open(true); };
        poll.setInterval(60);
        QObject::connect(&poll, &QTimer::timeout, this, [this]() { tick(); });
        poll.start();
        anim.setDuration(140);
        anim.setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(&anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            const double t = v.toDouble();
            move(target.x() + int((1.0 - t) * 28), target.y());
            setWindowOpacity(t);
        });
        QObject::connect(&anim, &QVariantAnimation::finished, this, [this]() {
            if (anim.direction() == QAbstractAnimation::Backward)
                hide();
        });
    }
    ~EdgeQuickPanel() override { delete handle; }

    void setItems(const QVector<Item> &list) { items = list; layoutPanel(); }
    void setEnabledEdge(bool on)
    {
        enabledEdge = on;
        if (!on) {
            close(false);
            handle->hide();
        }
    }
    bool isEnabledEdge() const { return enabledEdge; }
    std::function<void(const QPoint &)> onContextMenu;     // right-click on the panel or the handle

    void open(bool byTap)
    {
        if (!enabledEdge || items.isEmpty())
            return;
        tapped = byTap;
        idle.restart();
        outside.invalidate();
        refreshAll();
        layoutPanel();
        if (isVisible() && anim.direction() == QAbstractAnimation::Forward)
            return;
        handle->hide();
        anim.stop();
        anim.setDirection(QAbstractAnimation::Forward);
        anim.setStartValue(0.0);
        anim.setEndValue(1.0);
        setWindowOpacity(0.0);
        move(target.x() + 28, target.y());
        show();
        raise();
        anim.start();
    }
    void close(bool animate = true)
    {
        if (!isVisible())
            return;
        hover = -1;
        if (!animate) {
            anim.stop();
            hide();
            return;
        }
        if (anim.state() == QAbstractAnimation::Running && anim.direction() == QAbstractAnimation::Backward)
            return;
        anim.stop();
        anim.setDirection(QAbstractAnimation::Backward);
        anim.setStartValue(0.0);
        anim.setEndValue(1.0);
        anim.setCurrentTime(anim.duration());
        anim.start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF bg = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(QColor(255, 255, 255, 40), 1));
        p.setBrush(QColor(22, 24, 34, 236));
        p.drawRoundedRect(bg, 16, 16);
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        for (int i = 0; i < items.size(); ++i) {
            const Item &it = items.at(i);
            const QRectF cell = cellRect(i);
            if (i == hover || it.lit) {
                p.setPen(Qt::NoPen);
                QColor h = it.color;
                h.setAlpha(it.lit ? 70 : 40);
                p.setBrush(h);
                p.drawRoundedRect(cell.adjusted(4, 2, -4, -2), 10, 10);
            }
            const double s = 28;
            const QRectF ic(cell.center().x() - s / 2, cell.top() + 7, s, s);
            if (it.icon)
                it.icon(p, ic, it.color);
            p.setPen(QColor(236, 238, 245));
            p.drawText(QRectF(cell.left(), ic.bottom() + 2, cell.width(), 18), Qt::AlignHCenter | Qt::AlignTop, it.label);
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        idle.restart();
        const int h = indexAt(e->pos());
        if (h != hover) {
            hover = h;
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        hover = -1;
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        idle.restart();
        if (e->button() == Qt::RightButton) {
            if (onContextMenu)
                onContextMenu(e->globalPos());
            return;
        }
        if (e->button() != Qt::LeftButton)
            return;
        const int i = indexAt(e->pos());
        if (i < 0)
            return;
        pressed = i;
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        idle.restart();
        const int i = indexAt(e->pos());
        const int was = pressed;
        pressed = -1;
        if (e->button() != Qt::LeftButton || i < 0 || i != was)
            return;
        if (items[i].run)
            items[i].run();
        // the state changes a moment later (sound starts, song pauses): show it then
        QTimer::singleShot(150, this, [this]() { refreshAll(); update(); });
    }

private:
    // the thin bar at the edge; a click / tap opens the panel
    class Handle : public QWidget
    {
    public:
        std::function<void()> onClick;
        std::function<void(const QPoint &)> onRight;
        explicit Handle(QWidget *host) : QWidget(host, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus)
        {
            setAttribute(Qt::WA_ShowWithoutActivating);
            setAttribute(Qt::WA_TranslucentBackground);
            setCursor(Qt::PointingHandCursor);
            setToolTip(QString::fromUtf8("ปุ่มด่วน: เลื่อนเมาส์มาชิดขอบขวา หรือคลิกตรงนี้"));
            resize(10, 72);
        }
    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(200, 205, 220, 120));
            p.drawRoundedRect(QRectF(3, 0, 5, height()), 2.5, 2.5);
        }
        void mousePressEvent(QMouseEvent *e) override
        {
            if (e->button() == Qt::RightButton && onRight)
                onRight(e->globalPos());
            else if (e->button() == Qt::LeftButton && onClick)
                onClick();
        }
    };

    static constexpr int cellW = 76, cellH = 58, pad = 6;

    QRectF cellRect(int i) const { return QRectF(pad, pad + i * cellH, cellW, cellH); }
    int indexAt(const QPoint &pt) const
    {
        for (int i = 0; i < items.size(); ++i)
            if (cellRect(i).contains(pt))
                return i;
        return -1;
    }
    QRect hostRect() const
    {
        return QRect(host->mapToGlobal(QPoint(0, 0)), host->size());
    }
    void layoutPanel()
    {
        const QRect r = hostRect();
        int h = pad * 2 + items.size() * cellH;
        h = qMin(h, r.height() - 16);
        const int w = cellW + pad * 2;
        resize(w, h);
        target = QPoint(r.right() + 1 - w - 8, r.top() + qMax(8, (r.height() - h) / 2));
        if (!isVisible() || anim.state() != QAbstractAnimation::Running)
            if (isVisible())
                move(target);
    }
    void refreshAll()
    {
        for (Item &it : items)
            if (it.refresh)
                it.refresh(it);
    }
    bool hostUsable() const
    {
        if (!enabledEdge || !host->isVisible() || host->isMinimized())
            return false;
        if (QApplication::activeModalWidget() || QApplication::activePopupWidget())
            return false;
        if (allowed && !allowed())
            return false;
        // the program is the one in use (the panel itself never becomes the active window)
        return host->isActiveWindow();
    }
    void tick()
    {
        const bool usable = hostUsable();
        const QRect r = hostRect();
        const QPoint c = QCursor::pos();

        // the handle
        if (usable && !isVisible()) {
            handle->move(r.right() + 1 - handle->width(), r.top() + (r.height() - handle->height()) / 2);
            if (!handle->isVisible())
                handle->show();
        } else if (handle->isVisible()) {
            handle->hide();
        }

        if (!isVisible()) {
            // mouse held at the right edge
            const bool atEdge = usable && c.x() >= r.right() - 5 && c.x() <= r.right() + 2
                    && c.y() >= r.top() + 40 && c.y() <= r.bottom() - 40
                    && !(QApplication::mouseButtons() & Qt::LeftButton);
            if (atEdge) {
                if (!dwell.isValid())
                    dwell.start();
                else if (dwell.elapsed() >= 250) {
                    dwell.invalidate();
                    open(false);
                }
            } else {
                dwell.invalidate();
            }
            return;
        }
        if (anim.state() == QAbstractAnimation::Running && anim.direction() == QAbstractAnimation::Backward)
            return;
        if (!usable && !geometry().contains(c)) {
            close(false);
            return;
        }
        // the mouse left it (a margin around it counts as inside), or nobody used it for a while
        const bool inside = geometry().adjusted(-28, -28, 28, 28).contains(c);
        if (inside) {
            outside.invalidate();
        } else if (!outside.isValid()) {
            outside.start();
        }
        if ((!tapped && outside.isValid() && outside.elapsed() > 600)
                || (tapped && outside.isValid() && outside.elapsed() > 1500)
                || idle.elapsed() > 5000) {
            close();
            return;
        }
        // the state of the buttons (sound ended, song paused from the keyboard ...)
        if (++refreshTick % 8 == 0) {
            refreshAll();
            update();
        }
    }

    QWidget *host;
    std::function<bool()> allowed;
    Handle *handle;
    QVector<Item> items;
    QTimer poll;
    QVariantAnimation anim;
    QElapsedTimer dwell, outside, idle;
    QPoint target;
    int hover = -1, pressed = -1, refreshTick = 0;
    bool tapped = false, enabledEdge = true;

public:
    // the handle's right-click goes to the same menu
    void wireHandleMenu() { handle->onRight = [this](const QPoint &g) { if (onContextMenu) onContextMenu(g); }; }
};

#endif // EDGEQUICKPANEL_H
