// Buttons of the playback bar: a line icon drawn here (sharp at any size) over a short label.
// The bar is always dark (see frameControl), so the colors are fixed light ones; "on" buttons take
// the theme's accent color. Sizes follow the menu size (Theme::menuSize: normal / large / extra large).
#ifndef BARBUTTON_H
#define BARBUTTON_H

#include <QAbstractButton>
#include <QApplication>
#include <QCursor>
#include <QHash>
#include <QWidget>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QtMath>
#include <functional>

#include "../Theme.h"

namespace BarIcons {

using Fn = std::function<void(QPainter &, const QRectF &, const QColor &)>;

inline QPen pen(const QRectF &r, const QColor &c, double k = 1.0)
{
    QPen p(c, qMax(1.4, r.width() / 13.0 * k));
    p.setCapStyle(Qt::RoundCap);
    p.setJoinStyle(Qt::RoundJoin);
    return p;
}

// coordinates on a 24 x 24 grid
inline QPointF at(const QRectF &r, double x, double y)
{
    return QPointF(r.left() + r.width() * x / 24.0, r.top() + r.height() * y / 24.0);
}
inline QRectF box(const QRectF &r, double x, double y, double w, double h)
{
    return QRectF(at(r, x, y), at(r, x + w, y + h));
}

inline void tri(QPainter &p, const QRectF &r, const QColor &c, double x0, double x1, bool right)
{
    QPainterPath t;
    if (right) {
        t.moveTo(at(r, x0, 5)); t.lineTo(at(r, x1, 12)); t.lineTo(at(r, x0, 19));
    } else {
        t.moveTo(at(r, x1, 5)); t.lineTo(at(r, x0, 12)); t.lineTo(at(r, x1, 19));
    }
    t.closeSubpath();
    p.setPen(pen(r, c, 0.6));
    p.setBrush(c);
    p.drawPath(t);
}

inline void play(QPainter &p, const QRectF &r, const QColor &c) { tri(p, r, c, 6.5, 19, true); }
inline void pause(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(box(r, 6, 5, 4, 14), r.width() / 24, r.width() / 24);
    p.drawRoundedRect(box(r, 14, 5, 4, 14), r.width() / 24, r.width() / 24);
}
inline void stop(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(box(r, 6, 6, 12, 12), r.width() / 12, r.width() / 12);
}
inline void prev(QPainter &p, const QRectF &r, const QColor &c)
{
    tri(p, r, c, 8, 19, false);
    p.setPen(pen(r, c));
    p.drawLine(at(r, 5.5, 5.5), at(r, 5.5, 18.5));
}
inline void next(QPainter &p, const QRectF &r, const QColor &c)
{
    tri(p, r, c, 5, 16, true);
    p.setPen(pen(r, c));
    p.drawLine(at(r, 18.5, 5.5), at(r, 18.5, 18.5));
}
// end the song: play mark and bars fading down
inline void endSong(QPainter &p, const QRectF &r, const QColor &c)
{
    tri(p, r, c, 3, 11, true);
    p.setPen(pen(r, c));
    p.drawLine(at(r, 14, 6), at(r, 14, 18));
    p.drawLine(at(r, 17.5, 8.5), at(r, 17.5, 15.5));
    p.drawLine(at(r, 21, 11), at(r, 21, 13));
}
inline void mic(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(box(r, 9, 3, 6, 11), r.width() / 8, r.width() / 8);
    QPainterPath a;
    a.moveTo(at(r, 5.5, 11));
    a.cubicTo(at(r, 5.5, 19.5), at(r, 18.5, 19.5), at(r, 18.5, 11));
    p.drawPath(a);
    p.drawLine(at(r, 12, 17.5), at(r, 12, 21));
}
inline void micOff(QPainter &p, const QRectF &r, const QColor &c)
{
    mic(p, r, c);
    p.setPen(pen(r, c));
    p.drawLine(at(r, 4, 3.5), at(r, 20, 20.5));
}
inline void note(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c));
    p.drawLine(at(r, 15, 17), at(r, 15, 4));
    QPainterPath f;
    f.moveTo(at(r, 15, 4));
    f.cubicTo(at(r, 17, 7), at(r, 20, 7), at(r, 20, 10));
    p.drawPath(f);
    p.setBrush(c);
    p.drawEllipse(QRectF(at(r, 8.5, 14.5), at(r, 15, 19.5)));
}
inline void medley(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c));
    p.setBrush(Qt::NoBrush);
    QPainterPath a, b;
    a.moveTo(at(r, 3, 7)); a.cubicTo(at(r, 11, 7), at(r, 12, 17), at(r, 20, 17));
    b.moveTo(at(r, 3, 17)); b.cubicTo(at(r, 11, 17), at(r, 12, 7), at(r, 20, 7));
    p.drawPath(a);
    p.drawPath(b);
    p.drawLine(at(r, 17.5, 4.5), at(r, 20.5, 7));
    p.drawLine(at(r, 17.5, 9.5), at(r, 20.5, 7));
    p.drawLine(at(r, 17.5, 14.5), at(r, 20.5, 17));
    p.drawLine(at(r, 17.5, 19.5), at(r, 20.5, 17));
}
// snare drum with two sticks
inline void drum(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(at(r, 3, 10.5), at(r, 21, 16.5)));
    QPainterPath body;
    body.moveTo(at(r, 3, 13.5)); body.lineTo(at(r, 3, 18.5));
    body.cubicTo(at(r, 3, 22.5), at(r, 21, 22.5), at(r, 21, 18.5));
    body.lineTo(at(r, 21, 13.5));
    p.drawPath(body);
    p.setPen(pen(r, c, 0.6));
    p.drawLine(at(r, 8, 16.5), at(r, 8, 21));
    p.drawLine(at(r, 16, 16.5), at(r, 16, 21));
    p.setPen(pen(r, c));
    p.drawLine(at(r, 5, 2.5), at(r, 11, 11));
    p.drawLine(at(r, 19, 2.5), at(r, 13, 11));
}
inline void songs(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c));
    for (int i = 0; i < 3; ++i) {
        const double y = 6.5 + i * 5.5;
        p.drawLine(at(r, 9, y), at(r, 20, y));
        p.drawPoint(at(r, 4.5, y));
    }
}
inline void youtube(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(box(r, 2.5, 5, 19, 14), r.width() / 6, r.width() / 6);
    QPainterPath t;
    t.moveTo(at(r, 10, 8.5)); t.lineTo(at(r, 15.5, 12)); t.lineTo(at(r, 10, 15.5));
    t.closeSubpath();
    p.setBrush(c);
    p.setPen(pen(r, c, 0.5));
    p.drawPath(t);
}
inline void mixer(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c));
    const double xs[3] = { 6, 12, 18 }, ys[3] = { 14, 8, 16 };
    for (int i = 0; i < 3; ++i) {
        p.drawLine(at(r, xs[i], 3.5), at(r, xs[i], 20.5));
        p.setBrush(c);
        p.drawEllipse(at(r, xs[i], ys[i]), r.width() / 9.5, r.width() / 9.5);
    }
}
inline void more(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    for (int i = 0; i < 3; ++i)
        p.drawEllipse(at(r, 5 + i * 7, 12), r.width() / 13, r.width() / 13);
}

inline void restart(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(at(r, 4, 4), at(r, 20, 20)), 100 * 16, 290 * 16);
    p.drawLine(at(r, 9.5, 2.5), at(r, 12.5, 4.2));
    p.drawLine(at(r, 9.5, 6.5), at(r, 12.5, 4.2));
    tri(p, QRectF(at(r, 8, 8), at(r, 16, 16)), c, 7, 18, true);
}
inline void addSong(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c));
    p.setBrush(Qt::NoBrush);
    QPainterPath f;                             // folder
    f.moveTo(at(r, 3, 7)); f.lineTo(at(r, 3, 19)); f.lineTo(at(r, 21, 19)); f.lineTo(at(r, 21, 9));
    f.lineTo(at(r, 12, 9)); f.lineTo(at(r, 10, 6)); f.lineTo(at(r, 3.5, 6));
    p.drawPath(f);
    p.drawLine(at(r, 12, 11.5), at(r, 12, 16.5));
    p.drawLine(at(r, 9.5, 14), at(r, 14.5, 14));
}
inline void subtitles(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(box(r, 2.5, 5, 19, 14), r.width() / 8, r.width() / 8);
    p.drawLine(at(r, 6, 12.5), at(r, 10, 12.5));
    p.drawLine(at(r, 12.5, 12.5), at(r, 18, 12.5));
    p.drawLine(at(r, 6, 15.5), at(r, 13, 15.5));
    p.drawLine(at(r, 15.5, 15.5), at(r, 18, 15.5));
}
inline void screen2(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(box(r, 2, 4, 13, 9.5), r.width() / 16, r.width() / 16);
    p.drawRoundedRect(box(r, 9, 9, 13, 9.5), r.width() / 16, r.width() / 16);
    p.drawLine(at(r, 13, 21), at(r, 18, 21));
}
inline void fullscreen(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c));
    p.setBrush(Qt::NoBrush);
    const double a = 4, b = 20, k = 5;
    p.drawPolyline(QPolygonF({ at(r, a, a + k), at(r, a, a), at(r, a + k, a) }));
    p.drawPolyline(QPolygonF({ at(r, b - k, a), at(r, b, a), at(r, b, a + k) }));
    p.drawPolyline(QPolygonF({ at(r, a, b - k), at(r, a, b), at(r, a + k, b) }));
    p.drawPolyline(QPolygonF({ at(r, b - k, b), at(r, b, b), at(r, b, b - k) }));
}

// the line icon of each bar function, by name (also used for the "more" menu)
inline Fn byName(const QString &n)
{
    static const QHash<QString, Fn> all = {
        { "play", play }, { "pause", pause }, { "stop", stop }, { "prev", prev }, { "next", next },
        { "end", endSong }, { "mic", mic }, { "micOff", micOff }, { "note", note }, { "medley", medley },
        { "drum", drum }, { "songs", songs }, { "youtube", youtube }, { "mixer", mixer }, { "more", more },
        { "restart", restart }, { "addSong", addSong }, { "subtitles", subtitles }, { "screen2", screen2 },
        { "fullscreen", fullscreen },
    };
    return all.value(n);
}

} // namespace BarIcons

class BarButton : public QAbstractButton
{
public:
    BarButton(const QString &label, BarIcons::Fn icon, QWidget *parent = nullptr)
        : QAbstractButton(parent), drawIcon(std::move(icon))
    {
        setText(label);
        setFocusPolicy(Qt::NoFocus);            // typing still goes to the song search
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }

    void setIconFn(BarIcons::Fn f) { drawIcon = std::move(f); update(); }
    void setBig(bool on) { big = on; updateGeometry(); update(); }
    // AI ring around the icon: -1 none, 0..99 preparing (with the number), 100 ready
    void setRing(int pct) { if (pct != ring) { ring = pct; update(); } }
    int ringValue() const { return ring; }
    // a small arrow at the right: a click there calls onArrow (e.g. medley settings)
    void setArrow(std::function<void()> f) { onArrow = std::move(f); updateGeometry(); update(); }

    static double scale()
    {
        static const double f[3] = { 1.0, 1.2, 1.4 };
        return f[qBound(0, Theme::menuSize(), 2)];
    }
    static QColor fg() { return QColor(245, 246, 250); }
    static QColor dim() { return QColor(190, 196, 208); }
    static QColor accent() { return qApp->palette().color(QPalette::Highlight); }
    static QColor accentText() { return qApp->palette().color(QPalette::HighlightedText); }
    static QFont labelFont(const QFont &base)
    {
        QFont f = base;
        f.setPointSizeF(9.0 * scale());
        return f;
    }

    QSize sizeHint() const override
    {
        const double s = scale();
        const int icon = int((big ? 30 : 24) * s);
        const QFontMetrics fm(labelFont(font()));
        const int w = qMax(fm.horizontalAdvance(text()) + int(16 * s), icon + int(22 * s))
                      + (onArrow ? int(14 * s) : 0);
        const int h = int(24 * s) + int(6 * s) + fm.height() + int(10 * s);
        return QSize(w, h);
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double s = scale();
        const QRectF all = QRectF(rect()).adjusted(1, 1, -1, -1);
        const bool hover = underMouse() && isEnabled();
        const bool on = isChecked();
        const double rad = 7 * s;
        if (on) {
            p.setPen(Qt::NoPen);
            p.setBrush(accent());
            p.drawRoundedRect(all, rad, rad);
        } else if (hover || isDown()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, isDown() ? 40 : 22));
            p.drawRoundedRect(all, rad, rad);
        }
        const QColor ink = !isEnabled() ? QColor(120, 126, 138) : on ? accentText() : fg();
        const QColor sub = !isEnabled() ? QColor(120, 126, 138) : on ? accentText() : dim();

        const QFontMetrics fm(labelFont(font()));
        const int arrowW = onArrow ? int(14 * s) : 0;
        const double iconSz = (big ? 30 : 24) * s;
        const double iconBox = 24 * s;
        const QRectF body(all.left(), all.top(), all.width() - arrowW, all.height());
        const double top = all.top() + 5 * s + (iconBox - qMin(iconSz, iconBox + 6 * s)) / 2;
        QRectF ir(body.center().x() - iconSz / 2, top - (big ? 3 * s : 0), iconSz, iconSz);

        if (ring >= 0) {
            // AI ring: thin track, the progress arc, full when ready; the icon sits inside
            const QRectF rr = ir.adjusted(-2 * s, -2 * s, 2 * s, 2 * s);
            QPen track(on ? QColor(255, 255, 255, 90) : QColor(255, 255, 255, 55), 2 * s);
            p.setPen(track);
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(rr);
            QPen arc(on ? accentText() : accent(), 2.2 * s);
            arc.setCapStyle(Qt::RoundCap);
            p.setPen(arc);
            if (ring >= 100)
                p.drawEllipse(rr);
            else if (ring > 0)
                p.drawArc(rr, 90 * 16, -int(ring * 360 * 16 / 100));
            ir = ir.adjusted(ir.width() * 0.2, ir.height() * 0.2, -ir.width() * 0.2, -ir.height() * 0.2);
            if (ring < 100) {
                // preparing: the percent in place of the icon
                QFont nf = labelFont(font());
                nf.setPointSizeF(nf.pointSizeF() * 0.95);
                nf.setBold(true);
                p.setFont(nf);
                p.setPen(ink);
                p.drawText(rr, Qt::AlignCenter, QString::number(ring));
                ir = QRectF();
            }
        }
        if (drawIcon && !ir.isNull()) {
            p.save();
            drawIcon(p, ir, ink);
            p.restore();
        }

        p.setFont(labelFont(font()));
        p.setPen(sub);
        const QRectF tr(body.left(), all.bottom() - fm.height() - 5 * s, body.width(), fm.height());
        p.drawText(tr, Qt::AlignCenter, text());

        if (onArrow) {
            const QRectF ar(all.right() - arrowW, all.top(), arrowW, all.height());
            p.setPen(QPen(on ? QColor(255, 255, 255, 90) : QColor(255, 255, 255, 40), 1));
            p.drawLine(QPointF(ar.left(), ar.top() + 8 * s), QPointF(ar.left(), ar.bottom() - 8 * s));
            QPainterPath t;
            const QPointF c = ar.center();
            t.moveTo(c + QPointF(-3.5 * s, -1.5 * s));
            t.lineTo(c + QPointF(3.5 * s, -1.5 * s));
            t.lineTo(c + QPointF(0, 2.5 * s));
            t.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(sub);
            p.drawPath(t);
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (onArrow && e->button() == Qt::LeftButton && e->pos().x() > width() - int(14 * scale())) {
            onArrow();
            e->accept();
            return;
        }
        QAbstractButton::mousePressEvent(e);
    }

private:
    BarIcons::Fn drawIcon;
    std::function<void()> onArrow;
    bool big = false;
    int ring = -1;
};

// "คีย์ −/+": click the left half to lower the key, the right half to raise it
class KeyControl : public QWidget
{
public:
    explicit KeyControl(QWidget *parent = nullptr) : QWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setMouseTracking(true);
        setToolTip(QObject::tr("เปลี่ยนคีย์เพลง: ซ้าย = ลดคีย์ (Delete), ขวา = เพิ่มคีย์ (Insert)"));
    }
    std::function<void(int)> onStep;
    void setValue(int v) { if (v != value) { value = v; update(); } }

    QSize sizeHint() const override
    {
        const double s = BarButton::scale();
        const QFontMetrics fm(BarButton::labelFont(font()));
        return QSize(int(96 * s), int(24 * s) + int(6 * s) + fm.height() + int(10 * s));
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double s = BarButton::scale();
        const QRectF all = QRectF(rect()).adjusted(1, 1, -1, -1);
        const QFontMetrics fm(BarButton::labelFont(font()));
        const double half = all.width() / 2;
        if (underMouse()) {
            const QPoint m = mapFromGlobal(QCursor::pos());
            const QRectF h = m.x() < width() / 2 ? QRectF(all.left(), all.top(), half, all.height())
                                                 : QRectF(all.left() + half, all.top(), half, all.height());
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 22));
            p.drawRoundedRect(h, 7 * s, 7 * s);
        }
        const double icon = 24 * s;
        const QRectF ir(all.center().x() - icon / 2, all.top() + 5 * s, icon, icon);
        BarIcons::note(p, ir, BarButton::fg());
        // − and + at the sides
        p.setPen(BarIcons::pen(ir, BarButton::fg()));
        const double y = ir.center().y();
        const double xl = all.left() + 12 * s, xr = all.right() - 12 * s, d = 5 * s;
        p.drawLine(QPointF(xl - d, y), QPointF(xl + d, y));
        p.drawLine(QPointF(xr - d, y), QPointF(xr + d, y));
        p.drawLine(QPointF(xr, y - d), QPointF(xr, y + d));
        p.setFont(BarButton::labelFont(font()));
        p.setPen(value != 0 ? BarButton::accent().lighter(150) : BarButton::dim());
        const QString t = value == 0 ? QObject::tr("คีย์") : QObject::tr("คีย์ %1%2").arg(value > 0 ? "+" : "").arg(value);
        p.drawText(QRectF(all.left(), all.bottom() - fm.height() - 5 * s, all.width(), fm.height()), Qt::AlignCenter, t);
    }
    void mouseMoveEvent(QMouseEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onStep)
            onStep(e->pos().x() < width() / 2 ? -1 : +1);
    }

private:
    int value = 0;
};

#endif // BARBUTTON_H
