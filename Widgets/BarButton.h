// Buttons of the playback bar: modern glass pill buttons with duo-tone category-colored icons.
// Sizes follow the menu size (Theme::menuSize: normal / large / extra large).
#ifndef BARBUTTON_H
#define BARBUTTON_H

#include <QAbstractButton>
#include <QApplication>
#include <QCursor>
#include <QHash>
#include <QWidget>
#include <QFontMetrics>
#include <QLinearGradient>
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
    QPen p(c, qMax(1.5, r.width() / 12.5 * k));
    p.setCapStyle(Qt::RoundCap);
    p.setJoinStyle(Qt::RoundJoin);
    return p;
}

inline QColor duoFill(const QColor &c, int alpha = 72)
{
    return QColor(c.red(), c.green(), c.blue(), alpha);
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

inline void tri(QPainter &p, const QRectF &r, const QColor &c, double x0, double x1, bool right, int fillAlpha = 220)
{
    QPainterPath t;
    if (right) {
        t.moveTo(at(r, x0, 4.8)); t.lineTo(at(r, x1, 12)); t.lineTo(at(r, x0, 19.2));
    } else {
        t.moveTo(at(r, x1, 4.8)); t.lineTo(at(r, x0, 12)); t.lineTo(at(r, x1, 19.2));
    }
    t.closeSubpath();
    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0.0, c.lighter(120));
    g.setColorAt(1.0, QColor(c.red(), c.green(), c.blue(), fillAlpha));
    p.setPen(pen(r, c.lighter(115), 0.75));
    p.setBrush(g);
    p.drawPath(t);
}

inline void play(QPainter &p, const QRectF &r, const QColor &c)
{
    // Soft glow disc behind the play triangle
    p.setPen(Qt::NoPen);
    p.setBrush(duoFill(c, 42));
    p.drawEllipse(box(r, 2.5, 2.5, 19, 19));
    tri(p, r, c, 7.5, 18.5, true, 240);
}

inline void pause(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(duoFill(c, 42));
    p.drawEllipse(box(r, 2.5, 2.5, 19, 19));
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0.0, c.lighter(120));
    g.setColorAt(1.0, c);
    p.setPen(pen(r, c.lighter(115), 0.55));
    p.setBrush(g);
    p.drawRoundedRect(box(r, 6.2, 5.2, 4.2, 13.6), r.width() / 14, r.width() / 14);
    p.drawRoundedRect(box(r, 13.6, 5.2, 4.2, 13.6), r.width() / 14, r.width() / 14);
}

inline void stop(QPainter &p, const QRectF &r, const QColor &c)
{
    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0.0, c.lighter(118));
    g.setColorAt(1.0, duoFill(c, 195));
    p.setPen(pen(r, c.lighter(115), 0.75));
    p.setBrush(g);
    p.drawRoundedRect(box(r, 5.5, 5.5, 13, 13), r.width() / 8, r.width() / 8);
}

inline void prev(QPainter &p, const QRectF &r, const QColor &c)
{
    tri(p, r, c, 8.5, 19.5, false, 185);
    p.setPen(pen(r, c.lighter(115), 1.15));
    p.drawLine(at(r, 5.2, 5.2), at(r, 5.2, 18.8));
}

inline void next(QPainter &p, const QRectF &r, const QColor &c)
{
    tri(p, r, c, 4.5, 15.5, true, 185);
    p.setPen(pen(r, c.lighter(115), 1.15));
    p.drawLine(at(r, 18.8, 5.2), at(r, 18.8, 18.8));
}

// end the song: play mark and bars fading down
inline void endSong(QPainter &p, const QRectF &r, const QColor &c)
{
    tri(p, r, c, 2.8, 11.2, true, 195);
    p.setPen(pen(r, c.lighter(115), 1.05));
    p.drawLine(at(r, 14.2, 5.5), at(r, 14.2, 18.5));
    p.setPen(pen(r, QColor(c.red(), c.green(), c.blue(), 200), 1.05));
    p.drawLine(at(r, 17.6, 8.2), at(r, 17.6, 15.8));
    p.setPen(pen(r, QColor(c.red(), c.green(), c.blue(), 135), 1.05));
    p.drawLine(at(r, 21.0, 10.8), at(r, 21.0, 13.2));
}

inline void mic(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c.lighter(112)));
    p.setBrush(duoFill(c, 95));
    p.drawRoundedRect(box(r, 8.5, 2.8, 7, 11.2), r.width() / 6.5, r.width() / 6.5);
    // subtle grille lines inside mic capsule
    p.setPen(pen(r, c.lighter(130), 0.55));
    p.drawLine(at(r, 10.2, 6.2), at(r, 13.8, 6.2));
    p.drawLine(at(r, 10.2, 8.8), at(r, 13.8, 8.8));
    p.setPen(pen(r, c.lighter(112)));
    p.setBrush(Qt::NoBrush);
    QPainterPath a;
    a.moveTo(at(r, 5.2, 10.8));
    a.cubicTo(at(r, 5.2, 19.5), at(r, 18.8, 19.5), at(r, 18.8, 10.8));
    p.drawPath(a);
    p.drawLine(at(r, 12, 17.5), at(r, 12, 21.2));
    p.drawLine(at(r, 8.5, 21.2), at(r, 15.5, 21.2));
}

inline void micOff(QPainter &p, const QRectF &r, const QColor &c)
{
    mic(p, r, c);
    p.setPen(pen(r, QColor(255, 90, 90), 1.25));
    p.drawLine(at(r, 4, 3.5), at(r, 20, 20.5));
}

inline void note(QPainter &p, const QRectF &r, const QColor &c)
{
    // Beamed eighth notes for a richer musical key icon
    p.setPen(pen(r, c.lighter(112)));
    p.setBrush(duoFill(c, 160));
    QPainterPath beam;
    beam.moveTo(at(r, 8.5, 5.5));
    beam.lineTo(at(r, 19.5, 3.5));
    beam.lineTo(at(r, 19.5, 6.8));
    beam.lineTo(at(r, 8.5, 8.8));
    beam.closeSubpath();
    p.drawPath(beam);
    p.drawLine(at(r, 8.5, 6.0), at(r, 8.5, 17.5));
    p.drawLine(at(r, 19.5, 4.0), at(r, 19.5, 15.5));
    p.setBrush(c.lighter(115));
    p.drawEllipse(box(r, 3.5, 14.8, 5.5, 4.6));
    p.drawEllipse(box(r, 14.5, 12.8, 5.5, 4.6));
}

inline void medley(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, duoFill(c, 130), 1.6));
    p.setBrush(Qt::NoBrush);
    QPainterPath a, b;
    a.moveTo(at(r, 3, 7)); a.cubicTo(at(r, 11, 7), at(r, 12, 17), at(r, 20, 17));
    b.moveTo(at(r, 3, 17)); b.cubicTo(at(r, 11, 17), at(r, 12, 7), at(r, 20, 7));
    p.drawPath(a);
    p.drawPath(b);
    p.setPen(pen(r, c.lighter(118), 1.05));
    p.drawPath(a);
    p.drawPath(b);
    p.drawLine(at(r, 17.2, 4.3), at(r, 20.6, 7));
    p.drawLine(at(r, 17.2, 9.7), at(r, 20.6, 7));
    p.drawLine(at(r, 17.2, 14.3), at(r, 20.6, 17));
    p.drawLine(at(r, 17.2, 19.7), at(r, 20.6, 17));
}

// snare drum with two sticks
inline void drum(QPainter &p, const QRectF &r, const QColor &c)
{
    QPainterPath body;
    body.moveTo(at(r, 3, 13.2)); body.lineTo(at(r, 3, 18.5));
    body.cubicTo(at(r, 3, 22.5), at(r, 21, 22.5), at(r, 21, 18.5));
    body.lineTo(at(r, 21, 13.2));
    body.closeSubpath();
    p.setPen(pen(r, c.lighter(112), 0.9));
    p.setBrush(duoFill(c, 95));
    p.drawPath(body);
    p.setBrush(duoFill(c.lighter(135), 140));
    p.drawEllipse(QRectF(at(r, 3, 10.2), at(r, 21, 16.2)));
    p.setPen(pen(r, c.lighter(120), 0.65));
    p.drawLine(at(r, 8, 16.2), at(r, 8, 20.8));
    p.drawLine(at(r, 12, 16.5), at(r, 12, 21.2));
    p.drawLine(at(r, 16, 16.2), at(r, 16, 20.8));
    p.setPen(pen(r, c.lighter(125), 1.05));
    p.drawLine(at(r, 4.5, 2.5), at(r, 10.8, 11.2));
    p.drawLine(at(r, 19.5, 2.5), at(r, 13.2, 11.2));
}

inline void queue(QPainter &p, const QRectF &r, const QColor &c)
{
    // Queue card + play badge + lines
    p.setPen(pen(r, c.lighter(112), 0.85));
    p.setBrush(duoFill(c, 65));
    p.drawRoundedRect(box(r, 2.5, 4.0, 19.0, 16.0), r.width() / 8, r.width() / 8);
    tri(p, box(r, 3.5, 5.2, 7.5, 7.5), c.lighter(120), 5, 19, true, 230);
    p.setPen(pen(r, c.lighter(120), 1.0));
    p.drawLine(at(r, 11.5, 8.8), at(r, 19.0, 8.8));
    p.drawLine(at(r, 5.5, 13.2), at(r, 19.0, 13.2));
    p.drawLine(at(r, 5.5, 16.8), at(r, 16.0, 16.8));
}

inline void songs(QPainter &p, const QRectF &r, const QColor &c)
{
    // Star + favorite/history list lines
    QPainterPath star;
    const QPointF center = at(r, 7.2, 8.5);
    const double ro = r.width() * 4.2 / 24.0;
    const double ri = ro * 0.45;
    for (int i = 0; i < 10; ++i) {
        const double ang = -M_PI / 2.0 + i * M_PI / 5.0;
        const double rad = (i % 2 == 0) ? ro : ri;
        const QPointF pt(center.x() + rad * qCos(ang), center.y() + rad * qSin(ang));
        if (i == 0) star.moveTo(pt); else star.lineTo(pt);
    }
    star.closeSubpath();
    p.setPen(pen(r, c.lighter(115), 0.7));
    p.setBrush(duoFill(c, 190));
    p.drawPath(star);
    p.setPen(pen(r, c.lighter(115), 1.05));
    p.drawLine(at(r, 13.2, 6.8), at(r, 20.5, 6.8));
    p.drawLine(at(r, 13.2, 11.5), at(r, 20.5, 11.5));
    p.drawLine(at(r, 4.5, 17.2), at(r, 20.5, 17.2));
}

inline void youtube(QPainter &p, const QRectF &r, const QColor &c)
{
    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0.0, c.lighter(115));
    g.setColorAt(1.0, c.darker(115));
    p.setPen(pen(r, c.lighter(125), 0.7));
    p.setBrush(g);
    p.drawRoundedRect(box(r, 2.2, 4.8, 19.6, 14.4), r.width() / 5.5, r.width() / 5.5);
    QPainterPath t;
    t.moveTo(at(r, 9.8, 8.3)); t.lineTo(at(r, 15.8, 12.0)); t.lineTo(at(r, 9.8, 15.7));
    t.closeSubpath();
    p.setBrush(QColor(255, 255, 255, 245));
    p.setPen(Qt::NoPen);
    p.drawPath(t);
}

inline void mixer(QPainter &p, const QRectF &r, const QColor &c)
{
    const double xs[3] = { 6, 12, 18 }, ys[3] = { 14.5, 8.5, 15.5 };
    for (int i = 0; i < 3; ++i) {
        p.setPen(pen(r, duoFill(c, 110), 0.95));
        p.drawLine(at(r, xs[i], 3.5), at(r, xs[i], ys[i]));
        p.setPen(pen(r, c.lighter(120), 1.15));
        p.drawLine(at(r, xs[i], ys[i]), at(r, xs[i], 20.5));
        p.setPen(pen(r, c.lighter(135), 0.7));
        p.setBrush(c.lighter(115));
        p.drawRoundedRect(box(r, xs[i] - 2.2, ys[i] - 1.6, 4.4, 3.2), r.width() / 18, r.width() / 18);
    }
}

inline void more(QPainter &p, const QRectF &r, const QColor &c)
{
    // 2x2 modern app menu grid
    p.setPen(pen(r, c.lighter(115), 0.8));
    p.setBrush(duoFill(c, 95));
    const double coords[2] = { 4.2, 13.2 };
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            if (row == 0 && col == 1)
                p.setBrush(duoFill(c.lighter(125), 185));
            else
                p.setBrush(duoFill(c, 85));
            p.drawRoundedRect(box(r, coords[col], coords[row], 6.6, 6.6), r.width() / 12, r.width() / 12);
        }
    }
}

inline void restart(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c.lighter(115), 1.05));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(at(r, 3.8, 3.8), at(r, 20.2, 20.2)), 100 * 16, 290 * 16);
    p.drawLine(at(r, 9.2, 2.3), at(r, 12.8, 4.2));
    p.drawLine(at(r, 9.2, 6.7), at(r, 12.8, 4.2));
    tri(p, QRectF(at(r, 7.8, 7.8), at(r, 16.5, 16.5)), c, 6.5, 18.5, true, 210);
}

inline void addSong(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c.lighter(112), 0.95));
    p.setBrush(duoFill(c, 85));
    QPainterPath f;
    f.moveTo(at(r, 2.8, 7)); f.lineTo(at(r, 2.8, 19)); f.lineTo(at(r, 21.2, 19)); f.lineTo(at(r, 21.2, 8.8));
    f.lineTo(at(r, 12, 8.8)); f.lineTo(at(r, 9.8, 5.8)); f.lineTo(at(r, 3.2, 5.8));
    f.closeSubpath();
    p.drawPath(f);
    p.setPen(pen(r, c.lighter(135), 1.15));
    p.drawLine(at(r, 12, 11.2), at(r, 12, 16.8));
    p.drawLine(at(r, 9.2, 14), at(r, 14.8, 14));
}

inline void subtitles(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c.lighter(112), 0.95));
    p.setBrush(duoFill(c, 80));
    p.drawRoundedRect(box(r, 2.5, 4.8, 19, 14.4), r.width() / 7.5, r.width() / 7.5);
    p.setPen(pen(r, c.lighter(130), 1.05));
    p.drawLine(at(r, 6, 12.2), at(r, 10.5, 12.2));
    p.drawLine(at(r, 13, 12.2), at(r, 18, 12.2));
    p.drawLine(at(r, 6, 15.5), at(r, 13, 15.5));
    p.drawLine(at(r, 15.5, 15.5), at(r, 18, 15.5));
}

inline void screen2(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c.lighter(110), 0.9));
    p.setBrush(duoFill(c, 55));
    p.drawRoundedRect(box(r, 2, 3.8, 13, 9.5), r.width() / 14, r.width() / 14);
    p.setBrush(duoFill(c, 120));
    p.setPen(pen(r, c.lighter(120), 1.0));
    p.drawRoundedRect(box(r, 9, 8.8, 13, 9.5), r.width() / 14, r.width() / 14);
    p.drawLine(at(r, 12.5, 21), at(r, 18.5, 21));
}

inline void fullscreen(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(duoFill(c, 60));
    p.drawRoundedRect(box(r, 6.5, 6.5, 11, 11), r.width() / 14, r.width() / 14);
    p.setPen(pen(r, c.lighter(118), 1.1));
    p.setBrush(Qt::NoBrush);
    const double a = 3.8, b = 20.2, k = 5.2;
    p.drawPolyline(QPolygonF({ at(r, a, a + k), at(r, a, a), at(r, a + k, a) }));
    p.drawPolyline(QPolygonF({ at(r, b - k, a), at(r, b, a), at(r, b, a + k) }));
    p.drawPolyline(QPolygonF({ at(r, a, b - k), at(r, a, b), at(r, a + k, b) }));
    p.drawPolyline(QPolygonF({ at(r, b - k, b), at(r, b, b), at(r, b, b - k) }));
}

inline void lock(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c.lighter(115), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawArc(box(r, 7.8, 4.2, 8.4, 10.2), 0, 180 * 16);
    p.setBrush(duoFill(c, 115));
    p.drawRoundedRect(box(r, 4.8, 10.8, 14.4, 9.8), r.width() / 10, r.width() / 10);
    p.setPen(Qt::NoPen);
    p.setBrush(c.lighter(135));
    p.drawEllipse(at(r, 12, 15.5), r.width() / 13, r.width() / 13);
}

inline void search(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c.lighter(115), 1.05));
    p.setBrush(duoFill(c, 85));
    p.drawEllipse(QRectF(at(r, 3.8, 3.8), at(r, 16.2, 16.2)));
    p.setPen(pen(r, c.lighter(130), 1.25));
    p.drawLine(at(r, 14.8, 14.8), at(r, 20.5, 20.5));
}

// metronome: tempo
inline void tempo(QPainter &p, const QRectF &r, const QColor &c)
{
    p.setPen(pen(r, c.lighter(112), 0.95));
    p.setBrush(duoFill(c, 85));
    QPainterPath b;
    b.moveTo(at(r, 9.2, 3.2)); b.lineTo(at(r, 14.8, 3.2)); b.lineTo(at(r, 19.2, 20.8));
    b.lineTo(at(r, 4.8, 20.8)); b.closeSubpath();
    p.drawPath(b);
    p.setPen(pen(r, c.lighter(135), 1.15));
    p.drawLine(at(r, 12, 17.2), at(r, 18.0, 5.8));
    p.setBrush(c.lighter(130));
    p.drawEllipse(at(r, 16.0, 9.6), r.width() / 15, r.width() / 15);
}

// event sounds / sound effects: speaker horn with sound waves
inline void fx(QPainter &p, const QRectF &r, const QColor &c)
{
    QPainterPath horn;
    horn.moveTo(at(r, 3.2, 9.5));
    horn.lineTo(at(r, 7.5, 9.5));
    horn.lineTo(at(r, 13.5, 4.8));
    horn.lineTo(at(r, 13.5, 19.2));
    horn.lineTo(at(r, 7.5, 14.5));
    horn.lineTo(at(r, 3.2, 14.5));
    horn.closeSubpath();
    p.setPen(pen(r, c.lighter(115), 0.95));
    p.setBrush(duoFill(c, 135));
    p.drawPath(horn);
    p.setBrush(Qt::NoBrush);
    p.setPen(pen(r, c.lighter(125), 1.05));
    p.drawArc(box(r, 12.2, 7.5, 5.5, 9.0), -55 * 16, 110 * 16);
    p.setPen(pen(r, c.lighter(135), 1.05));
    p.drawArc(box(r, 14.0, 4.5, 7.5, 15.0), -55 * 16, 110 * 16);
}

// signature color per icon name
inline QColor colorOf(const QString &n)
{
    static const QHash<QString, QColor> cols = {
        { "play",       QColor(46, 213, 115) },   // emerald green
        { "pause",      QColor(255, 177, 66) },   // warm amber
        { "stop",       QColor(255, 94, 87) },    // coral red
        { "prev",       QColor(72, 219, 251) },   // sky cyan
        { "next",       QColor(72, 219, 251) },   // sky cyan
        { "restart",    QColor(255, 159, 67) },   // sunny orange
        { "end",        QColor(255, 107, 129) },  // rose coral
        { "mic",        QColor(255, 107, 157) },  // electric pink
        { "micOff",     QColor(255, 107, 157) },  // electric pink
        { "drum",       QColor(254, 202, 87) },   // golden amber
        { "note",       QColor(29, 209, 161) },   // turquoise mint
        { "tempo",      QColor(84, 160, 255) },   // azure blue
        { "medley",     QColor(165, 94, 234) },   // neon violet
        { "fx",         QColor(255, 126, 95) },   // vibrant coral-orange
        { "search",     QColor(56, 189, 248) },   // bright sky blue
        { "queue",      QColor(52, 211, 153) },   // mint emerald
        { "songs",      QColor(251, 191, 36) },   // warm gold
        { "youtube",    QColor(255, 56, 56) },    // YouTube red
        { "addSong",    QColor(74, 222, 128) },   // fresh green
        { "mixer",      QColor(245, 158, 11) },   // audio amber
        { "subtitles",  QColor(129, 140, 248) },  // soft indigo
        { "screen2",    QColor(45, 212, 191) },   // teal cyan
        { "fullscreen", QColor(96, 165, 250) },   // ice blue
        { "lock",       QColor(251, 146, 60) },   // amber orange
        { "more",       QColor(167, 139, 250) },  // soft violet
    };
    return cols.value(n, QColor(148, 163, 184));
}

// the line icon of each bar function, by name (also used for the "more" menu)
inline Fn byName(const QString &n)
{
    static const QHash<QString, Fn> all = {
        { "play", play }, { "pause", pause }, { "stop", stop }, { "prev", prev }, { "next", next },
        { "end", endSong }, { "mic", mic }, { "micOff", micOff }, { "note", note }, { "medley", medley },
        { "drum", drum }, { "fx", fx }, { "queue", queue }, { "songs", songs }, { "youtube", youtube }, { "mixer", mixer }, { "more", more },
        { "restart", restart }, { "addSong", addSong }, { "subtitles", subtitles }, { "screen2", screen2 },
        { "fullscreen", fullscreen }, { "lock", lock }, { "search", search }, { "tempo", tempo },
    };
    return all.value(n);
}

} // namespace BarIcons

class BarButton : public QAbstractButton
{
public:
    BarButton(const QString &label, BarIcons::Fn icon, QWidget *parent = nullptr,
              const QColor &accentColor = QColor())
        : QAbstractButton(parent), drawIcon(std::move(icon)), customAccent(accentColor)
    {
        setText(label);
        setFocusPolicy(Qt::NoFocus);            // typing still goes to the song search
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }

    void setIconFn(BarIcons::Fn f, const QColor &accentColor = QColor())
    {
        drawIcon = std::move(f);
        if (accentColor.isValid())
            customAccent = accentColor;
        update();
    }
    void setAccentColor(const QColor &c) { customAccent = c; update(); }
    QColor buttonAccent() const { return customAccent.isValid() ? customAccent : accent(); }
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
    static QColor dim() { return QColor(220, 226, 238); }
    static QColor accent() { return qApp->palette().color(QPalette::Highlight); }
    static QColor accentText() { return qApp->palette().color(QPalette::HighlightedText); }
    static QFont labelFont(const QFont &base)
    {
        QFont f = base;
        f.setPointSizeF(8.8 * scale());
        return f;
    }

    QSize sizeHint() const override
    {
        const double s = scale();
        const int icon = int((big ? 28 : 22) * s);
        const QFontMetrics fm(labelFont(font()));
        const int w = qMax(fm.horizontalAdvance(text()) + int(14 * s), icon + int(18 * s))
                      + (onArrow ? int(13 * s) : 0);
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
        const QRectF all = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        const bool hover = underMouse() && isEnabled();
        const bool on = isChecked();
        const double rad = 8.0 * s;
        const QColor acc = buttonAccent();

        // Modern Glass Pill background & border
        QLinearGradient bg(all.topLeft(), all.bottomLeft());
        QPen borderPen(Qt::NoPen);
        if (!isEnabled()) {
            bg.setColorAt(0.0, QColor(32, 36, 46, 120));
            bg.setColorAt(1.0, QColor(20, 23, 30, 120));
            borderPen = QPen(QColor(255, 255, 255, 18), 1.0);
        } else if (on) {
            bg.setColorAt(0.0, acc.lighter(hover ? 122 : 112));
            bg.setColorAt(1.0, acc.darker(135));
            borderPen = QPen(acc.lighter(145), 1.2 * s);
        } else if (big) {
            bg.setColorAt(0.0, QColor(acc.red(), acc.green(), acc.blue(), isDown() ? 130 : (hover ? 95 : 58)));
            bg.setColorAt(1.0, QColor(22, 28, 38, 225));
            borderPen = QPen(QColor(acc.red(), acc.green(), acc.blue(), hover ? 210 : 140), 1.2 * s);
        } else if (isDown()) {
            bg.setColorAt(0.0, QColor(acc.red(), acc.green(), acc.blue(), 85));
            bg.setColorAt(1.0, QColor(20, 24, 34, 235));
            borderPen = QPen(QColor(acc.red(), acc.green(), acc.blue(), 190), 1.1 * s);
        } else if (hover) {
            bg.setColorAt(0.0, QColor(acc.red(), acc.green(), acc.blue(), 62));
            bg.setColorAt(0.55, QColor(38, 45, 62, 215));
            bg.setColorAt(1.0, QColor(24, 29, 40, 230));
            borderPen = QPen(QColor(acc.red(), acc.green(), acc.blue(), 165), 1.1 * s);
        } else {
            bg.setColorAt(0.0, QColor(46 + acc.red() / 16, 52 + acc.green() / 16, 68 + acc.blue() / 16, 185));
            bg.setColorAt(1.0, QColor(22, 26, 36, 215));
            borderPen = QPen(QColor(255, 255, 255, 34), 1.0);
        }

        p.setPen(borderPen);
        p.setBrush(bg);
        p.drawRoundedRect(all, rad, rad);

        // Subtle top glass rim highlight
        if (isEnabled() && !isDown()) {
            p.setPen(QPen(QColor(255, 255, 255, on ? 90 : (hover ? 65 : 32)), 1.0));
            p.drawLine(QPointF(all.left() + rad * 0.8, all.top() + 1.0),
                       QPointF(all.right() - rad * 0.8, all.top() + 1.0));
        }

        const QColor ink = !isEnabled() ? QColor(115, 122, 136)
                         : on           ? QColor(255, 255, 255)
                         : (hover ? acc.lighter(115) : acc);
        const QColor sub = !isEnabled() ? QColor(115, 122, 136)
                         : (on || hover || big) ? QColor(255, 255, 255)
                         : dim();

        QFont lf = labelFont(font());
        if (on || big)
            lf.setBold(true);
        const QFontMetrics fm(lf);
        const int arrowW = onArrow ? int(13 * s) : 0;
        const double iconSz = (big ? 28 : 23) * s;
        const double iconBox = 24 * s;
        const QRectF body(all.left(), all.top(), all.width() - arrowW, all.height());
        const double top = all.top() + 4.5 * s + (iconBox - qMin(iconSz, iconBox + 6 * s)) / 2;
        QRectF ir(body.center().x() - iconSz / 2, top - (big ? 2.5 * s : 0), iconSz, iconSz);

        if (ring >= 0) {
            // AI ring: thin track, the progress arc, full when ready; the icon sits inside
            const QRectF rr = ir.adjusted(-2 * s, -2 * s, 2 * s, 2 * s);
            QPen track(on ? QColor(255, 255, 255, 90) : QColor(255, 255, 255, 55), 2 * s);
            p.setPen(track);
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(rr);
            QPen arc(on ? QColor(255, 255, 255) : acc.lighter(120), 2.2 * s);
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
                p.setPen(QColor(255, 255, 255));
                p.drawText(rr, Qt::AlignCenter, QString::number(ring));
                ir = QRectF();
            }
        }
        if (drawIcon && !ir.isNull()) {
            p.save();
            drawIcon(p, ir, ink);
            p.restore();
        }

        p.setFont(lf);
        p.setPen(sub);
        const QRectF tr(body.left(), all.bottom() - fm.height() - 4.0 * s, body.width(), fm.height());
        p.drawText(tr, Qt::AlignCenter, text());

        if (onArrow) {
            const QRectF ar(all.right() - arrowW, all.top(), arrowW, all.height());
            p.setPen(QPen(on ? QColor(255, 255, 255, 95) : QColor(255, 255, 255, 42), 1));
            p.drawLine(QPointF(ar.left(), ar.top() + 7 * s), QPointF(ar.left(), ar.bottom() - 7 * s));
            QPainterPath t;
            const QPointF c = ar.center();
            t.moveTo(c + QPointF(-3.2 * s, -1.5 * s));
            t.lineTo(c + QPointF(3.2 * s, -1.5 * s));
            t.lineTo(c + QPointF(0, 2.5 * s));
            t.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(sub);
            p.drawPath(t);
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (onArrow && e->button() == Qt::LeftButton && e->pos().x() > width() - int(13 * scale())) {
            onArrow();
            e->accept();
            return;
        }
        QAbstractButton::mousePressEvent(e);
    }

private:
    BarIcons::Fn drawIcon;
    QColor customAccent;
    std::function<void()> onArrow;
    bool big = false;
    int ring = -1;
};

// "คีย์ −/+" (or "ความเร็ว −/+"): click the left half to step down, the right half to step up
class KeyControl : public QWidget
{
public:
    explicit KeyControl(QWidget *parent = nullptr, const QString &label = QString(),
                        BarIcons::Fn icon = BarIcons::note, const QString &tip = QString(),
                        const QColor &accentCol = QColor())
        : QWidget(parent),
          name(label.isEmpty() ? QObject::tr("คีย์") : label),
          drawIcon(std::move(icon)),
          acc(accentCol.isValid() ? accentCol : BarIcons::colorOf(label.isEmpty() ? QStringLiteral("note") : QStringLiteral("tempo")))
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setMouseTracking(true);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(tip.isEmpty() ? QObject::tr("เปลี่ยนคีย์เพลง: ซ้าย = ลดคีย์ (Delete), ขวา = เพิ่มคีย์ (Insert)") : tip);
    }
    std::function<void(int)> onStep;
    void setValue(int v) { if (v != value) { value = v; update(); } }

    QSize sizeHint() const override
    {
        const double s = BarButton::scale();
        const QFontMetrics fm(BarButton::labelFont(font()));
        return QSize(int(88 * s), int(24 * s) + int(6 * s) + fm.height() + int(10 * s));
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double s = BarButton::scale();
        const QRectF all = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        const double rad = 8.0 * s;
        const bool hover = underMouse();
        const bool active = (value != 0);

        // Glass pill body matching BarButton
        QLinearGradient bg(all.topLeft(), all.bottomLeft());
        if (active) {
            bg.setColorAt(0.0, QColor(acc.red(), acc.green(), acc.blue(), 75));
            bg.setColorAt(1.0, QColor(24, 30, 42, 225));
        } else if (hover) {
            bg.setColorAt(0.0, QColor(acc.red(), acc.green(), acc.blue(), 48));
            bg.setColorAt(1.0, QColor(24, 29, 40, 225));
        } else {
            bg.setColorAt(0.0, QColor(46 + acc.red() / 16, 52 + acc.green() / 16, 68 + acc.blue() / 16, 185));
            bg.setColorAt(1.0, QColor(22, 26, 36, 215));
        }
        p.setPen(QPen(active ? acc.lighter(120) : (hover ? QColor(acc.red(), acc.green(), acc.blue(), 150) : QColor(255, 255, 255, 34)),
                      (active || hover) ? 1.1 * s : 1.0));
        p.setBrush(bg);
        p.drawRoundedRect(all, rad, rad);

        const double half = all.width() / 2;
        if (hover) {
            const QPoint m = mapFromGlobal(QCursor::pos());
            const QRectF h = m.x() < width() / 2 ? QRectF(all.left(), all.top(), half, all.height())
                                                 : QRectF(all.left() + half, all.top(), half, all.height());
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(acc.red(), acc.green(), acc.blue(), 42));
            p.drawRoundedRect(h, rad, rad);
        }

        const double icon = 22 * s;
        const QRectF ir(all.center().x() - icon / 2, all.top() + 4.5 * s, icon, icon);
        drawIcon(p, ir, hover ? acc.lighter(118) : acc);

        // − and + badges at the sides
        const double y = ir.center().y();
        const double xl = all.left() + 12.5 * s, xr = all.right() - 12.5 * s, d = 4.2 * s;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 22));
        p.drawEllipse(QPointF(xl, y), 7.5 * s, 7.5 * s);
        p.drawEllipse(QPointF(xr, y), 7.5 * s, 7.5 * s);

        p.setPen(BarIcons::pen(ir, BarButton::fg(), 0.95));
        p.drawLine(QPointF(xl - d, y), QPointF(xl + d, y));
        p.drawLine(QPointF(xr - d, y), QPointF(xr + d, y));
        p.drawLine(QPointF(xr, y - d), QPointF(xr, y + d));

        QFont lf = BarButton::labelFont(font());
        if (active)
            lf.setBold(true);
        const QFontMetrics fm(lf);
        p.setFont(lf);
        p.setPen(active ? acc.lighter(145) : (hover ? QColor(255, 255, 255) : BarButton::dim()));
        const QString t = value == 0 ? name : QString("%1 %2%3").arg(name).arg(value > 0 ? "+" : "").arg(value);
        p.drawText(QRectF(all.left(), all.bottom() - fm.height() - 4.0 * s, all.width(), fm.height()), Qt::AlignCenter, t);
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
    QString name;
    BarIcons::Fn drawIcon;
    QColor acc;
};

#endif // BARBUTTON_H
