#ifndef MENUICONS_H
#define MENUICONS_H

// Modern, colorful vector badge icons for all menus and submenus (HiDPI-aware).

#include <QApplication>
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QScreen>
#include <QtMath>

namespace MenuIcons {

enum Kind {
    Settings, Channel, Bus, Speaker, Vst, Meter, Eq, Chorus, Reverb,
    Soundfont, Reset, Save, Open, Language, Theme, About, Exit, Phone,
    AddSong, YouTube, Star, MicRecord, Screen, Fullscreen, Video,
    Search, Book, Download, AiSparkle, Lock, DmxLight, Trophy,
    Transition, Help, QrCode
};

inline bool isDark()
{
    return qApp->palette().color(QPalette::Window).lightness() < 128;
}

inline QColor normalColor()
{
    return qApp->palette().color(QPalette::Text);
}

inline QColor dimColor()
{
    QColor c = qApp->palette().color(QPalette::Text);
    c.setAlpha(150);
    return c;
}

inline QColor onColor()
{
    return isDark() ? QColor("#39ff88") : QColor("#10b957");
}

inline QColor defaultAccent(Kind kind)
{
    switch (kind) {
    case AddSong:    return QColor("#10b981"); // Emerald green
    case YouTube:    return QColor("#ef4444"); // YouTube red
    case Star:       return QColor("#f59e0b"); // Amber gold
    case MicRecord:  return QColor("#f43f5e"); // Rose red
    case Screen:     return QColor("#0ea5e9"); // Sky blue
    case Fullscreen: return QColor("#3b82f6"); // Blue
    case Video:      return QColor("#8b5cf6"); // Violet
    case Open:       return QColor("#f59e0b"); // Amber folder
    case Search:     return QColor("#0284c7"); // Cyan blue
    case Book:       return QColor("#6366f1"); // Indigo
    case Download:   return QColor("#10b981"); // Green
    case AiSparkle:  return QColor("#a855f7"); // Purple AI
    case Eq:         return QColor("#0891b2"); // Teal cyan
    case Chorus:     return QColor("#06b6d4"); // Cyan
    case Reverb:     return QColor("#8b5cf6"); // Purple
    case Soundfont:  return QColor("#f97316"); // Orange
    case Lock:       return QColor("#eab308"); // Gold
    case Channel:    return QColor("#3b82f6"); // Blue
    case Bus:        return QColor("#6366f1"); // Indigo
    case Speaker:    return QColor("#10b981"); // Emerald
    case Vst:        return QColor("#9333ea"); // Purple
    case Meter:      return QColor("#22c55e"); // Green meter
    case Reset:      return QColor("#f59e0b"); // Amber
    case Save:       return QColor("#2563eb"); // Royal blue
    case Language:   return QColor("#0284c7"); // Globe blue
    case Theme:      return QColor("#7c3aed"); // Violet
    case Phone:      return QColor("#0891b2"); // Cyan
    case QrCode:     return QColor("#0d9488"); // Teal
    case DmxLight:   return QColor("#ec4899"); // Pink stage light
    case Trophy:     return QColor("#eab308"); // Gold trophy
    case Transition: return QColor("#14b8a6"); // Teal crossfade
    case Settings:   return QColor("#475569"); // Slate
    case Help:       return QColor("#0284c7"); // Help blue
    case About:      return QColor("#4f46e5"); // Indigo info
    case Exit:       return QColor("#ef4444"); // Power red
    }
    return normalColor();
}

inline QIcon make(Kind kind, const QColor &customColor)
{
    const int S = 20;
    qreal dpr = 1.0;
    if (QScreen *scr = QGuiApplication::primaryScreen())
        dpr = scr->devicePixelRatio();

    QPixmap pm(int(S * dpr), int(S * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    const QColor color = customColor.isValid() ? customColor : defaultAccent(kind);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Subtle soft rounded badge background so every icon sits in a uniform modern frame
    QColor bg = color;
    bg.setAlpha(isDark() ? 42 : 28);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(1.0, 1.0, 18.0, 18.0), 5.0, 5.0);

    QPen pen(color, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (kind) {
    case AddSong: {
        // Musical note + plus badge
        p.drawLine(QPointF(9.5, 5.0), QPointF(9.5, 13.0));
        p.drawLine(QPointF(9.5, 5.0), QPointF(14.0, 6.2));
        p.setBrush(color);
        p.drawEllipse(QPointF(7.6, 13.2), 2.1, 1.6);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(14.0, 11.5), QPointF(14.0, 15.5));
        p.drawLine(QPointF(12.0, 13.5), QPointF(16.0, 13.5));
        break;
    }
    case YouTube: {
        // Solid red rounded play screen with white play triangle
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(2.5, 4.2, 15.0, 11.6), 3.2, 3.2);
        p.setBrush(Qt::white);
        QPolygonF tri;
        tri << QPointF(8.3, 7.2) << QPointF(8.3, 12.8) << QPointF(13.2, 10.0);
        p.drawPolygon(tri);
        break;
    }
    case Star: {
        // 5-point gold star
        QPainterPath star;
        const QPointF c(10.0, 10.2);
        const qreal rOut = 6.2, rIn = 2.7;
        for (int i = 0; i < 10; ++i) {
            const qreal a = -M_PI / 2.0 + i * M_PI / 5.0;
            const qreal r = (i % 2 == 0) ? rOut : rIn;
            const QPointF pt(c.x() + r * qCos(a), c.y() + r * qSin(a));
            if (i == 0) star.moveTo(pt);
            else star.lineTo(pt);
        }
        star.closeSubpath();
        QColor fill = color;
        fill.setAlpha(90);
        p.setBrush(fill);
        p.drawPath(star);
        break;
    }
    case MicRecord: {
        // Studio microphone
        p.drawRoundedRect(QRectF(7.6, 3.5, 4.8, 7.5), 2.4, 2.4);
        p.drawArc(QRectF(5.5, 6.5, 9.0, 7.0), 0, -180 * 16);
        p.drawLine(QPointF(10.0, 13.5), QPointF(10.0, 16.2));
        p.drawLine(QPointF(7.2, 16.2), QPointF(12.8, 16.2));
        break;
    }
    case Screen: {
        // Dual monitor / TV display
        p.drawRoundedRect(QRectF(3.2, 4.0, 13.6, 9.2), 1.8, 1.8);
        p.drawLine(QPointF(10.0, 13.2), QPointF(10.0, 16.0));
        p.drawLine(QPointF(6.8, 16.0), QPointF(13.2, 16.0));
        break;
    }
    case Fullscreen: {
        // 4 corner expand arrows
        p.drawLine(QPointF(4.2, 7.5), QPointF(4.2, 4.2));
        p.drawLine(QPointF(4.2, 4.2), QPointF(7.5, 4.2));
        p.drawLine(QPointF(15.8, 7.5), QPointF(15.8, 4.2));
        p.drawLine(QPointF(15.8, 4.2), QPointF(12.5, 4.2));
        p.drawLine(QPointF(4.2, 12.5), QPointF(4.2, 15.8));
        p.drawLine(QPointF(4.2, 15.8), QPointF(7.5, 15.8));
        p.drawLine(QPointF(15.8, 12.5), QPointF(15.8, 15.8));
        p.drawLine(QPointF(15.8, 15.8), QPointF(12.5, 15.8));
        break;
    }
    case Video: {
        // Clapperboard / video frame with play triangle
        p.drawRoundedRect(QRectF(3.2, 4.5, 13.6, 11.0), 2.0, 2.0);
        QPolygonF tri;
        tri << QPointF(8.4, 7.6) << QPointF(8.4, 12.4) << QPointF(12.8, 10.0);
        p.setBrush(color);
        p.drawPolygon(tri);
        break;
    }
    case Search: {
        // Magnifying glass
        p.drawEllipse(QPointF(9.0, 9.0), 4.4, 4.4);
        p.drawLine(QPointF(12.3, 12.3), QPointF(15.8, 15.8));
        break;
    }
    case Book: {
        // Songbook pages
        p.drawRoundedRect(QRectF(4.0, 3.8, 12.0, 12.4), 1.8, 1.8);
        p.drawLine(QPointF(7.0, 3.8), QPointF(7.0, 16.2));
        p.drawLine(QPointF(9.5, 7.5), QPointF(13.5, 7.5));
        p.drawLine(QPointF(9.5, 10.5), QPointF(13.5, 10.5));
        break;
    }
    case Download: {
        // Down arrow into tray
        p.drawLine(QPointF(10.0, 4.0), QPointF(10.0, 12.5));
        p.drawLine(QPointF(6.8, 9.5), QPointF(10.0, 12.8));
        p.drawLine(QPointF(13.2, 9.5), QPointF(10.0, 12.8));
        p.drawLine(QPointF(4.5, 15.5), QPointF(15.5, 15.5));
        break;
    }
    case AiSparkle: {
        // 4-point AI star sparkle
        QPainterPath sp;
        sp.moveTo(10.0, 3.2);
        sp.quadTo(10.0, 10.0, 16.8, 10.0);
        sp.quadTo(10.0, 10.0, 10.0, 16.8);
        sp.quadTo(10.0, 10.0, 3.2, 10.0);
        sp.quadTo(10.0, 10.0, 10.0, 3.2);
        QColor fill = color;
        fill.setAlpha(95);
        p.setBrush(fill);
        p.drawPath(sp);
        break;
    }
    case Lock: {
        // Padlock
        p.drawRoundedRect(QRectF(4.8, 9.2, 10.4, 6.8), 1.8, 1.8);
        p.drawArc(QRectF(6.8, 4.2, 6.4, 8.0), 0, 180 * 16);
        p.setBrush(color);
        p.drawEllipse(QPointF(10.0, 12.5), 1.0, 1.0);
        break;
    }
    case DmxLight: {
        // Stage spotlight with rays
        p.drawEllipse(QPointF(10.0, 8.5), 4.0, 4.0);
        p.drawLine(QPointF(10.0, 2.5), QPointF(10.0, 3.8));
        p.drawLine(QPointF(4.5, 4.5), QPointF(5.6, 5.6));
        p.drawLine(QPointF(15.5, 4.5), QPointF(14.4, 5.6));
        p.drawLine(QPointF(7.5, 15.8), QPointF(12.5, 15.8));
        p.drawLine(QPointF(8.5, 12.5), QPointF(7.5, 15.8));
        p.drawLine(QPointF(11.5, 12.5), QPointF(12.5, 15.8));
        break;
    }
    case Trophy: {
        // Trophy cup
        p.drawRoundedRect(QRectF(6.2, 4.0, 7.6, 6.2), 2.0, 2.0);
        p.drawLine(QPointF(10.0, 10.2), QPointF(10.0, 14.8));
        p.drawLine(QPointF(6.8, 15.2), QPointF(13.2, 15.2));
        p.drawArc(QRectF(4.0, 4.8, 3.0, 4.0), 90 * 16, 180 * 16);
        p.drawArc(QRectF(13.0, 4.8, 3.0, 4.0), -90 * 16, 180 * 16);
        break;
    }
    case Transition: {
        // Crossfade / medley arrows
        p.drawLine(QPointF(3.8, 7.0), QPointF(15.5, 13.0));
        p.drawLine(QPointF(3.8, 13.0), QPointF(15.5, 7.0));
        p.drawLine(QPointF(13.0, 5.8), QPointF(15.8, 7.0));
        p.drawLine(QPointF(13.0, 14.2), QPointF(15.8, 13.0));
        break;
    }
    case QrCode: {
        // 3 QR finder squares + dots
        p.drawRect(QRectF(4.0, 4.0, 4.5, 4.5));
        p.drawRect(QRectF(11.5, 4.0, 4.5, 4.5));
        p.drawRect(QRectF(4.0, 11.5, 4.5, 4.5));
        p.setBrush(color);
        p.drawRect(QRectF(11.5, 11.5, 2.0, 2.0));
        p.drawRect(QRectF(14.0, 14.0, 2.0, 2.0));
        break;
    }
    case Help: {
        // Question mark in circle
        p.drawEllipse(QPointF(10.0, 10.0), 6.5, 6.5);
        p.drawArc(QRectF(7.8, 6.0, 4.4, 3.8), 30 * 16, 195 * 16);
        p.drawLine(QPointF(10.0, 9.8), QPointF(10.0, 11.5));
        p.setBrush(color);
        p.drawEllipse(QPointF(10.0, 13.8), 0.7, 0.7);
        break;
    }
    case Settings: {
        p.drawEllipse(QPointF(10, 10), 4.6, 4.6);
        p.drawEllipse(QPointF(10, 10), 1.8, 1.8);
        for (int i = 0; i < 8; i++) {
            qreal a = i * M_PI / 4;
            p.drawLine(QPointF(10 + 4.6 * qCos(a), 10 + 4.6 * qSin(a)),
                       QPointF(10 + 6.8 * qCos(a), 10 + 6.8 * qSin(a)));
        }
        break;
    }
    case Channel:
        p.drawLine(QPointF(3.5, 10), QPointF(8, 10));
        p.drawLine(QPointF(8, 10), QPointF(15.5, 4.8));
        p.drawLine(QPointF(8, 10), QPointF(16.2, 10));
        p.drawLine(QPointF(8, 10), QPointF(15.5, 15.2));
        break;
    case Bus:
        for (int y : { 5, 10, 15 }) {
            p.drawEllipse(QPointF(4.8, y), 1.1, 1.1);
            p.drawLine(QPointF(8, y), QPointF(15.8, y));
        }
        break;
    case Speaker:
        p.drawRoundedRect(QRectF(4.8, 2.8, 10.4, 14.4), 2, 2);
        p.drawEllipse(QPointF(10, 11.8), 2.6, 2.6);
        p.drawEllipse(QPointF(10, 6.0), 0.9, 0.9);
        break;
    case Vst:
        p.drawRoundedRect(QRectF(4.2, 7.5, 11.6, 7.2), 2, 2);
        p.drawLine(QPointF(7.5, 3.5), QPointF(7.5, 7.5));
        p.drawLine(QPointF(12.5, 3.5), QPointF(12.5, 7.5));
        p.drawLine(QPointF(10, 14.7), QPointF(10, 17.0));
        break;
    case Meter:
        p.drawLine(QPointF(4.5, 15.5), QPointF(4.5, 11.5));
        p.drawLine(QPointF(8.0, 15.5), QPointF(8.0, 7.8));
        p.drawLine(QPointF(11.5, 15.5), QPointF(11.5, 4.5));
        p.drawLine(QPointF(15.0, 15.5), QPointF(15.0, 9.5));
        break;
    case Eq: {
        const qreal xs[3] = { 5.5, 10.0, 14.5 };
        const qreal ks[3] = { 11.8, 6.2, 10.0 };
        for (int i = 0; i < 3; i++) {
            p.drawLine(QPointF(xs[i], 3.5), QPointF(xs[i], 16.5));
            p.setBrush(color);
            p.drawRoundedRect(QRectF(xs[i] - 2.0, ks[i] - 1.3, 4.0, 2.6), 1, 1);
            p.setBrush(Qt::NoBrush);
        }
        break;
    }
    case Chorus:
        for (int k = 0; k < 2; k++) {
            QPainterPath w;
            const qreal y = k == 0 ? 7.2 : 12.6;
            w.moveTo(3.0, y);
            w.cubicTo(5.2, y - 4.5, 7.2, y - 4.5, 10.0, y);
            w.cubicTo(12.8, y + 4.5, 14.8, y + 4.5, 17.0, y);
            QPen pp = pen;
            if (k == 1) { QColor c2 = color; c2.setAlphaF(0.6); pp.setColor(c2); }
            p.setPen(pp);
            p.drawPath(w);
        }
        break;
    case Reverb:
        p.setBrush(color);
        p.drawEllipse(QPointF(4.5, 10), 1.5, 1.5);
        p.setBrush(Qt::NoBrush);
        for (int r : { 5, 8, 11 }) {
            QRectF rc(4.5 - r, 10 - r, 2 * r, 2 * r);
            QPen pp = pen;
            QColor c2 = color; c2.setAlphaF(r == 5 ? 1.0 : (r == 8 ? 0.75 : 0.5)); pp.setColor(c2);
            p.setPen(pp);
            p.drawArc(rc, -42 * 16, 84 * 16);
        }
        break;
    case Soundfont:
        p.drawRoundedRect(QRectF(3.2, 3.8, 13.6, 12.4), 2, 2);
        p.drawLine(QPointF(3.2, 8.0), QPointF(16.8, 8.0));
        p.drawLine(QPointF(3.2, 12.0), QPointF(16.8, 12.0));
        p.drawLine(QPointF(8.0, 3.8), QPointF(8.0, 16.2));
        break;
    case Reset:
        p.drawArc(QRectF(4.0, 4.0, 12.0, 12.0), 40 * 16, 280 * 16);
        p.drawLine(QPointF(14.2, 3.5), QPointF(14.2, 6.8));
        p.drawLine(QPointF(14.2, 6.8), QPointF(10.8, 6.8));
        break;
    case Save:
        p.drawRoundedRect(QRectF(3.8, 3.8, 12.4, 12.4), 1.8, 1.8);
        p.drawRect(QRectF(6.5, 3.8, 6.5, 4.0));
        p.drawRect(QRectF(6.5, 10.8, 6.5, 5.4));
        break;
    case Open: {
        QPainterPath f;
        f.moveTo(3.2, 15.2); f.lineTo(3.2, 5.2); f.lineTo(7.8, 5.2); f.lineTo(9.3, 7.0); f.lineTo(16.2, 7.0);
        f.lineTo(16.2, 15.2); f.closeSubpath();
        QColor fill = color;
        fill.setAlpha(65);
        p.setBrush(fill);
        p.drawPath(f);
        break;
    }
    case Language:
        p.drawEllipse(QPointF(10, 10), 6.4, 6.4);
        p.drawEllipse(QPointF(10, 10), 2.8, 6.4);
        p.drawLine(QPointF(3.6, 10), QPointF(16.4, 10));
        break;
    case Theme:
        p.drawEllipse(QPointF(10, 10), 6.4, 6.4);
        {
            QPainterPath half;
            half.moveTo(10, 3.6); half.arcTo(QRectF(3.6, 3.6, 12.8, 12.8), 90, -180); half.closeSubpath();
            p.setBrush(color);
            p.drawPath(half);
            p.setBrush(Qt::NoBrush);
        }
        break;
    case About:
        p.drawEllipse(QPointF(10, 10), 6.4, 6.4);
        p.drawLine(QPointF(10, 9.0), QPointF(10, 13.5));
        p.setBrush(color);
        p.drawEllipse(QPointF(10, 6.5), 0.65, 0.65);
        break;
    case Exit:
        p.drawArc(QRectF(4.2, 4.6, 11.6, 11.6), 120 * 16, -300 * 16);
        p.drawLine(QPointF(10, 2.8), QPointF(10, 9.2));
        break;
    case Phone:
        p.drawRoundedRect(QRectF(5.5, 2.8, 9.0, 14.4), 2, 2);
        p.drawLine(QPointF(8.5, 14.8), QPointF(11.5, 14.8));
        p.drawLine(QPointF(8.5, 4.8), QPointF(11.5, 4.8));
        break;
    }
    p.end();
    return QIcon(pm);
}

inline QIcon make(Kind kind) { return make(kind, QColor()); }

} // namespace MenuIcons

#endif // MENUICONS_H
