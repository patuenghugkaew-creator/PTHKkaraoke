#ifndef TICKERBAR_H
#define TICKERBAR_H

// "ข้อความวิ่ง": a thin strip at the top (or bottom) edge of a screen with text scrolling right to
// left - the shop's messages and the next songs in the queue. It sits over everything in its host
// window, follows the host's size, and lets mouse clicks through. Header-only, no Q_OBJECT.

#include <QColor>
#include <QElapsedTimer>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QTimer>
#include <QWidget>
#include <cmath>
#include <functional>

class TickerBar : public QWidget
{
public:
    struct Style {
        int speed = 110;                // pixels per second at 1080 lines (scaled with the screen)
        int sizePct = 100;              // text size, % of the normal size
        QColor fg = QColor(255, 255, 255);
        QColor bg = QColor(0, 0, 0, 150);
        bool top = true;
    };

    // pixels to leave free at the right end when at the top (e.g. the clock in the corner), and
    // at the bottom when at the bottom (e.g. the control bar); may be empty
    std::function<int()> reserveRight;
    std::function<int()> reserveBottom;
    // true while the strip must step aside (e.g. the song search is open where it runs);
    // checked every frame, so it goes and comes back at once
    std::function<bool()> stepAside;

    explicit TickerBar(QWidget *host) : QWidget(host)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        host->installEventFilter(this);
        frame.setTimerType(Qt::PreciseTimer);
        frame.setInterval(16);
        QObject::connect(&frame, &QTimer::timeout, this, [this]() {
            const bool aside = stepAside && stepAside();
            if (aside && isVisible())
                hide();
            else if (!aside && !isVisible()) {
                relayout();
                show();
                raise();
            }
            if (!aside) {
                if (++ticks % 10 == 0)  // ~6 times a second: follow popups in the corner
                    relayout();
                update();
            }
        });
        hide();
    }

    void setStyle(const Style &s)
    {
        if (s.speed == st.speed && s.sizePct == st.sizePct && s.fg == st.fg && s.bg == st.bg && s.top == st.top && styled)
            return;
        st = s;
        styled = true;
        cache = QPixmap();
        relayout();
    }

    // the text to show; a change waits until the current pass has gone by (no jump mid-sentence)
    void setText(const QString &t)
    {
        if (t == pending)
            return;
        pending = t;
        if (shown.isEmpty() || !isVisible())
            startPass();
    }

    void setActive(bool on)
    {
        active = on;
        refreshVisible();
    }
    // the host's other widgets moved (e.g. the clock appeared): place the strip again
    void placeAgain()
    {
        if (isVisible() && parentWidget())
            relayout();
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == parentWidget() && (e->type() == QEvent::Resize || e->type() == QEvent::Show))
            relayout();
        return false;
    }

    void paintEvent(QPaintEvent *) override
    {
        if (shown.isEmpty())
            return;
        if (cache.isNull())
            render();
        const double pps = st.speed * (parentWidget()->height() / 1080.0);
        const double x = width() - clock.elapsed() / 1000.0 * std::max(20.0, pps);
        const double w = cache.width() / cache.devicePixelRatio();
        if (x + w < 0) {                // gone by: the next pass (with new text, if any)
            startPass();
            return;
        }
        // the text fades in / out at both ends of the strip instead of being cut off at an edge
        // (it looked cut by the clock in the corner)
        const qreal dpr = devicePixelRatioF();
        const QSize px(int(width() * dpr), int(height() * dpr));
        if (strip.size() != px) {
            strip = QImage(px, QImage::Format_ARGB32_Premultiplied);
            strip.setDevicePixelRatio(dpr);
        }
        strip.fill(Qt::transparent);
        {
            QPainter s(&strip);
            s.drawPixmap(QPointF(std::round(x), 0), cache);
            const double fade = std::min(width() / 4.0, height() * 1.2);
            QLinearGradient g(0, 0, width(), 0);
            g.setColorAt(0, QColor(0, 0, 0, 0));
            g.setColorAt(fade / width(), QColor(0, 0, 0, 255));
            g.setColorAt(1.0 - fade / width(), QColor(0, 0, 0, 255));
            g.setColorAt(1, QColor(0, 0, 0, 0));
            s.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            s.fillRect(QRectF(0, 0, width(), height()), g);
        }
        QPainter p(this);
        p.fillRect(rect(), st.bg);
        p.drawImage(QPointF(0, 0), strip);
    }

private:
    void relayout()
    {
        QWidget *host = parentWidget();
        if (!host)
            return;
        const int h = std::max(30, int(host->height() * 0.06 * st.sizePct / 100.0));   // readable on a TV
        const int right = (st.top && reserveRight) ? std::max(0, reserveRight()) : 0;
        const int bottom = (!st.top && reserveBottom) ? std::max(0, reserveBottom()) : 0;
        const int w = std::max(100, host->width() - right);
        const QRect g(0, st.top ? 0 : host->height() - bottom - h, w, h);
        if (g.height() != height())
            cache = QPixmap();          // the text is drawn for the strip's height
        if (g != geometry())
            setGeometry(g);
        raise();
    }

    void render()
    {
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(std::max(12, int(height() * 0.62)));
        const QFontMetrics fm(f);
        const qreal dpr = devicePixelRatioF();
        const int w = fm.horizontalAdvance(shown) + 4;
        cache = QPixmap(int(w * dpr), int(height() * dpr));
        cache.setDevicePixelRatio(dpr);
        cache.fill(Qt::transparent);
        QPainter p(&cache);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setFont(f);
        const int y = (height() - fm.height()) / 2 + fm.ascent();
        p.setPen(QColor(0, 0, 0, 170));                 // a soft shadow keeps it readable on any picture
        p.drawText(2, y + 2, shown);
        p.setPen(st.fg);
        p.drawText(0, y, shown);
    }

    void startPass()
    {
        shown = pending;
        cache = QPixmap();
        clock.restart();
        refreshVisible();
    }

    void refreshVisible()
    {
        const bool on = active && !shown.isEmpty();
        if (on && !isVisible() && !(stepAside && stepAside())) {
            relayout();
            show();
            raise();
        } else if (!on && isVisible()) {
            hide();
        }
        if (on && !frame.isActive())
            frame.start();
        else if (!on)
            frame.stop();
    }

    Style st;
    QString pending, shown;
    QPixmap cache;
    QImage strip;                       // one frame of the strip, for the faded ends
    QElapsedTimer clock;
    QTimer frame;
    bool active = false;
    bool styled = false;
    unsigned ticks = 0;
};

#endif // TICKERBAR_H
