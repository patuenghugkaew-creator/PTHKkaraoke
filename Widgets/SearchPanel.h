#ifndef SEARCHPANEL_H
#define SEARCHPANEL_H

// The song search, easier to use:
//   SearchModeChips   - the search modes in a row at the top of the search bar, the current one lit;
//                       click one, or Tab / Shift+Tab
//   SearchResultsList - the songs found, several at once, under the search bar: the one Enter takes
//                       is marked, Up / Down move; a line at the bottom says more (e.g. lyrics found)
// Header-only, no Q_OBJECT.

#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <functional>

class SearchModeChips : public QWidget
{
public:
    std::function<void(int)> onPick;        // index into the names

    explicit SearchModeChips(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setCursor(Qt::PointingHandCursor);
    }
    void setModes(const QStringList &names) { modes = names; update(); }
    void setCurrent(int i) { if (i != cur) { cur = i; update(); } }
    int wantedWidth() const
    {
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(qMax(12, int(height() * 0.55)));
        const QFontMetrics fm(f);
        int x = 2;
        for (const QString &m : modes)
            x += fm.horizontalAdvance(m) + 22 + 6;
        f.setBold(false);
        f.setPixelSize(qMax(11, int(height() * 0.45)));
        const QFontMetrics hfm(f);
        return x + 6 + hfm.horizontalAdvance(QString::fromUtf8("Tab = โหมดถัดไป   Shift+Tab = ย้อนกลับ")) + 16;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(qMax(12, int(height() * 0.55)));
        p.setFont(f);
        const QFontMetrics fm(f);
        rects.clear();
        int x = 2;
        for (int i = 0; i < modes.size(); ++i) {
            const int w = fm.horizontalAdvance(modes[i]) + 22;
            const QRectF r(x, 2, w, height() - 4);
            rects.append(r.toRect());
            QPainterPath path;
            path.addRoundedRect(r, r.height() / 2, r.height() / 2);
            if (i == cur) {
                p.fillPath(path, QColor(0, 129, 194, 235));
                p.setPen(Qt::white);
            } else {
                p.fillPath(path, QColor(255, 255, 255, 40));
                p.setPen(QColor(230, 230, 230));
            }
            p.drawText(r, Qt::AlignCenter, modes[i]);
            x += w + 6;
        }
        p.setPen(QColor(200, 200, 200));
        f.setBold(false);
        f.setPixelSize(qMax(11, int(height() * 0.45)));
        p.setFont(f);
        p.drawText(QRect(x + 6, 0, width() - x - 6, height()), Qt::AlignVCenter | Qt::AlignLeft,
                   QString::fromUtf8("Tab = โหมดถัดไป   Shift+Tab = ย้อนกลับ"));
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        for (int i = 0; i < rects.size(); ++i)
            if (rects[i].contains(e->pos())) {
                if (onPick) onPick(i);
                return;
            }
    }

private:
    QStringList modes;
    QVector<QRect> rects;
    int cur = 0;
};

class SearchResultsList : public QWidget
{
public:
    struct Row { QString id, title, detail; };      // detail: tempo and file type

    // a click on a row (index in the rows shown); again = the row was already the marked one
    std::function<void(int row, bool again)> onPick;
    std::function<void(int step)> onWheel;          // +1 = next song, -1 = previous

    explicit SearchResultsList(QWidget *parent) : QWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        queueButton = new QPushButton(tr("+ เพิ่มเข้าคิว"), this);
        queueButton->setFocusPolicy(Qt::NoFocus);
        queueButton->setCursor(Qt::PointingHandCursor);
        queueButton->setStyleSheet("QPushButton { color: white; background: #176a48; border: 1px solid #6bd5a6; border-radius: 6px; padding: 0 10px; font-size: 16px; } QPushButton:hover { background: #208557; }");
        connect(queueButton, &QPushButton::clicked, this, [this]() {
            if (mark >= 0 && mark < rows.size() && onPick)
                onPick(mark, true);
        });
        hide();
    }

    // rows: the window shown; marked: which of them Enter takes (-1 none); footer: the line under it
    void setRows(const QVector<Row> &r, int marked, const QString &footerText)
    {
        rows = r;
        mark = marked;
        footer = footerText;
        positionQueueButton();
        update();
    }
    int rowHeight() const { return rowH; }
    void setRowHeight(int h) { rowH = h; }
    int wantedHeight() const { return (rows.size() + (footer.isEmpty() ? 0 : 1)) * rowH + 10; }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QWidget::resizeEvent(e);
        positionQueueButton();
    }
    void paintEvent(QPaintEvent *) override
    {
        if (rows.isEmpty() && footer.isEmpty())
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath bg;
        bg.addRoundedRect(QRectF(rect()), 12, 12);
        p.fillPath(bg, QColor(16, 18, 24, 205));
        QFont f = font();
        f.setPixelSize(qMax(13, int(rowH * 0.58)));
        f.setBold(true);
        const QFontMetrics fm(f);
        int y = 5;
        for (int i = 0; i < rows.size(); ++i) {
            const QRect r(6, y, width() - 12 - queueButton->sizeHint().width() - 12, rowH);
            if (i == mark) {
                QPainterPath hl;
                hl.addRoundedRect(QRectF(r), 8, 8);
                p.fillPath(hl, QColor(0, 129, 194, 215));
            }
            p.setFont(f);
            p.setPen(i == mark ? QColor(255, 255, 255) : QColor(170, 210, 255));
            const int idW = qMax(fm.horizontalAdvance("0000000000"), fm.horizontalAdvance(rows[i].id) + 12);
            p.drawText(QRect(r.x() + 10, r.y(), idW, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
                       (i == mark ? QString::fromUtf8("▶ ") : QString("   ")) + rows[i].id);
            const int detailW = fm.horizontalAdvance(rows[i].detail) + 16;
            p.setPen(i == mark ? QColor(255, 255, 255) : QColor(235, 235, 235));
            const QRect tr(r.x() + 10 + idW + 8, r.y(), r.width() - idW - detailW - 30, r.height());
            p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(rows[i].title, Qt::ElideRight, tr.width()));
            p.setPen(i == mark ? QColor(220, 240, 255) : QColor(160, 160, 160));
            p.drawText(QRect(r.right() - detailW - 6, r.y(), detailW, r.height()), Qt::AlignVCenter | Qt::AlignRight, rows[i].detail);
            y += rowH;
        }
        if (!footer.isEmpty()) {
            QFont ff = f;
            ff.setBold(false);
            ff.setPixelSize(qMax(12, int(rowH * 0.5)));
            p.setFont(ff);
            p.setPen(QColor(255, 230, 120));
            p.drawText(QRect(16, y, width() - 32, rowH), Qt::AlignVCenter | Qt::AlignLeft, footer);
        }
    }
    int rowAt(const QPoint &pos) const
    {
        const int i = (pos.y() - 5) / qMax(1, rowH);
        return (pos.y() >= 5 && i >= 0 && i < rows.size()) ? i : -1;
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        const int i = rowAt(e->pos());
        if (i >= 0 && onPick)
            onPick(i, i == mark);
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        // the first click marked it; the second one counts as "again" in mousePressEvent order
        const int i = rowAt(e->pos());
        if (e->button() == Qt::LeftButton && i >= 0 && onPick)
            onPick(i, true);
    }
    void wheelEvent(QWheelEvent *e) override
    {
        const int d = e->angleDelta().y();
        if (d != 0 && onWheel)
            onWheel(d < 0 ? 1 : -1);
    }

private:
    QPushButton *queueButton = nullptr;
    void positionQueueButton()
    {
        queueButton->setVisible(mark >= 0 && mark < rows.size());
        const int w = queueButton->sizeHint().width();
        queueButton->setGeometry(width() - w - 12, 5 + mark * rowH + 2, w, rowH - 4);
    }
    QVector<Row> rows;
    int mark = -1;
    QString footer;
    int rowH = 40;
};

#endif // SEARCHPANEL_H
