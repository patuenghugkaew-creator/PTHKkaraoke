#ifndef DIALOGSKIN_H
#define DIALOGSKIN_H

// Gives every application dialog (Settings, EQ, Reverb, PRO, Lock Soundfont, ...)
// the same themed neon title bar and automatically polishes all dialog controls
// (buttons, combo boxes, line edits, spin boxes, and layout margins) so that
// Thai labels never touch button borders or look cramped.
// Header-only, no Q_OBJECT (no moc step).

#include <QObject>
#include <QDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QAbstractButton>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QAbstractSpinBox>
#include <QLayout>
#include <QFontMetrics>
#include <QMenu>
#include <QApplication>
#include <QHash>
#include <QSet>
#include <QEvent>
#include <QMouseEvent>
#include <QWindow>
#include <QByteArray>

#include "Widgets/TitleBar.h"

class DialogSkin : public QObject
{
public:
    explicit DialogSkin(QObject *parent = nullptr) : QObject(parent) {}

    // Helper that can also be called manually after changing a button's text at runtime
    static void fitDialogButton(QPushButton *btn)
    {
        if (!btn)
            return;
        btn->setCursor(Qt::PointingHandCursor);
        const QString txt = btn->text().trimmed();
        const bool isCompact = (btn->maximumWidth() <= 60)
                               || txt == QLatin1String("...")
                               || txt == QLatin1String("+")
                               || txt == QLatin1String("-")
                               || txt == QString(QChar(0x2212));
        if (isCompact) {
            if (txt == QLatin1String("...") && btn->maximumHeight() >= 32) {
                btn->setMinimumSize(42, 32);
                btn->setMaximumWidth(46);
            }
            if (!btn->styleSheet().contains(QLatin1String("padding"))) {
                btn->setStyleSheet(btn->styleSheet()
                    + QStringLiteral(" QPushButton { padding: 2px 4px; font-weight: bold; }"));
            }
            return;
        }

        QFont bf = btn->font();
        if (bf.pointSizeF() > 0 && bf.pointSizeF() < 10.0)
            bf.setPointSizeF(10.0);
        bf.setBold(true);
        btn->setFont(bf);

        if (btn->maximumHeight() >= 36 && btn->minimumHeight() < 36)
            btn->setMinimumHeight(36);

        if (!btn->styleSheet().contains(QLatin1String("padding"))) {
            btn->setStyleSheet(btn->styleSheet()
                + QStringLiteral(" QPushButton { padding: 6px 18px; }"));
        }

        const int textW = QFontMetrics(btn->font()).horizontalAdvance(txt);
        const int iconW = btn->icon().isNull() ? 0 : (btn->iconSize().width() + 8);
        const int wantW = qMax(96, textW + iconW + 40);
        if (btn->maximumWidth() >= wantW)
            btn->setMinimumWidth(qMax(btn->minimumWidth(), wantW));
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        switch (e->type()) {
        case QEvent::Polish:
        case QEvent::Show:
        case QEvent::Resize:
        case QEvent::WindowTitleChange:
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
            break;
        default:
            return false;
        }

        if (!o->isWidgetType())
            return false;

        if (e->type() == QEvent::Polish) {
            if (QMenu *m = qobject_cast<QMenu *>(o)) {
                if (m->isWindow() && !m->isVisible() && !m->property("pthkRounded").toBool()) {
                    m->setProperty("pthkRounded", true);
                    m->setWindowFlag(Qt::FramelessWindowHint, true);
                    m->setWindowFlag(Qt::NoDropShadowWindowHint, true);
                    m->setAttribute(Qt::WA_TranslucentBackground, true);
                }
                return false;
            }
        }

        QDialog *dlg = qobject_cast<QDialog *>(o);
        if (!dlg)
            return false;

        if (e->type() == QEvent::Polish) {
            inheritStayOnTop(dlg);
            skin(dlg);
            return false;
        }

        auto it = skins.find(dlg);
        if (it == skins.end())
            return false;

        switch (e->type()) {
        case QEvent::Show:
            polishControls(dlg);
            it->bar->setGeometry(0, 0, dlg->width(), H);
            it->bar->raise();
            it->frame->setGeometry(dlg->rect());
            it->frame->raise();
            break;
        case QEvent::Resize:
            it->bar->setGeometry(0, 0, dlg->width(), H);
            it->bar->raise();
            it->frame->setGeometry(dlg->rect());
            it->frame->raise();
            break;
        case QEvent::WindowTitleChange:
            it->bar->setTitle(dlg->windowTitle());
            break;
        case QEvent::MouseMove: {
            Qt::Edges ed = edgesAt(dlg, static_cast<QMouseEvent *>(e)->pos());
            dlg->setCursor(edgeCursor(ed));
            break;
        }
        case QEvent::MouseButtonPress: {
            QMouseEvent *me = static_cast<QMouseEvent *>(e);
            if (me->button() == Qt::LeftButton) {
                Qt::Edges ed = edgesAt(dlg, me->pos());
                if (ed && dlg->windowHandle()) {
                    dlg->windowHandle()->startSystemResize(ed);
                    return true;
                }
            }
            break;
        }
        default:
            break;
        }
        return false;
    }

private:
    static const int H = 36;

    struct Skin {
        TitleBar *bar;
        FrameOverlay *frame;
        bool shownOnce = false;
    };
    QHash<QObject *, Skin> skins;
    QSet<QObject *> busy;

    static Qt::Edges edgesAt(QDialog *dlg, const QPoint &pos)
    {
        Qt::Edges ed;
        const int m = 6;
        const bool canW = dlg->minimumWidth() != dlg->maximumWidth();
        const bool canH = dlg->minimumHeight() != dlg->maximumHeight();
        if (canW && pos.x() < m)
            ed |= Qt::LeftEdge;
        else if (canW && pos.x() >= dlg->width() - m)
            ed |= Qt::RightEdge;
        if (canH && pos.y() >= dlg->height() - m)
            ed |= Qt::BottomEdge;
        return ed;
    }

    static Qt::CursorShape edgeCursor(Qt::Edges ed)
    {
        const bool h = ed & (Qt::LeftEdge | Qt::RightEdge);
        const bool v = ed & Qt::BottomEdge;
        if (h && v)
            return (ed & Qt::LeftEdge) ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor;
        if (h)
            return Qt::SizeHorCursor;
        if (v)
            return Qt::SizeVerCursor;
        return Qt::ArrowCursor;
    }

    static void inheritStayOnTop(QDialog *dlg)
    {
        if (!dlg->isWindow() || dlg->isVisible())
            return;
        if (dlg->windowFlags() & Qt::WindowStaysOnTopHint)
            return;
        const QWidgetList tops = QApplication::topLevelWidgets();
        for (QWidget *w : tops) {
            if (w != dlg && w->isVisible()
                && ((w->windowFlags() & Qt::WindowStaysOnTopHint) || w->property("kmTopmost").toBool())) {
                dlg->setWindowFlag(Qt::WindowStaysOnTopHint, true);
                break;
            }
        }
    }

    void polishControls(QDialog *dlg)
    {
        const bool box = qobject_cast<QMessageBox *>(dlg) || qobject_cast<QInputDialog *>(dlg);

        // 1. Comfortable layout margins & spacing
        if (QLayout *lay = dlg->layout()) {
            if (!box) {
                int l, t, r, b;
                lay->getContentsMargins(&l, &t, &r, &b);
                if (l < 14 || r < 14 || b < 14)
                    lay->setContentsMargins(qMax(l, 16), qMax(t, 12), qMax(r, 16), qMax(b, 16));
                if (lay->spacing() >= 0 && lay->spacing() < 10)
                    lay->setSpacing(10);
            }
        }

        // 2. QDialogButtonBox buttons (OK / Cancel / Close / Yes / No)
        for (QDialogButtonBox *bb : dlg->findChildren<QDialogButtonBox *>()) {
            for (QAbstractButton *b : bb->buttons()) {
                b->setCursor(Qt::PointingHandCursor);
                QFont bf = b->font();
                bf.setPointSizeF(bf.pointSizeF() > 0 ? qMax(10.0, bf.pointSizeF()) : 10.0);
                bf.setBold(true);
                b->setFont(bf);
                const int textW = QFontMetrics(bf).horizontalAdvance(b->text().trimmed());
                const int minW = qMax(box ? 104 : 96, textW + 40);
                b->setMinimumSize(minW, box ? 42 : 38);
                if (!b->styleSheet().contains(QLatin1String("padding"))) {
                    b->setStyleSheet(b->styleSheet()
                        + QStringLiteral(" QPushButton { padding: 6px 18px; }"));
                }
            }
        }

        // 3. All direct QPushButtons in the dialog
        for (QPushButton *btn : dlg->findChildren<QPushButton *>()) {
            fitDialogButton(btn);
        }

        // 4. Align heights of QComboBox, QLineEdit, and QSpinBox / QDoubleSpinBox with buttons
        for (QComboBox *cb : dlg->findChildren<QComboBox *>()) {
            if (cb->maximumHeight() >= 34 && cb->minimumHeight() < 34)
                cb->setMinimumHeight(34);
        }
        for (QLineEdit *le : dlg->findChildren<QLineEdit *>()) {
            if (le->maximumHeight() >= 34 && le->minimumHeight() < 34)
                le->setMinimumHeight(34);
        }
        for (QAbstractSpinBox *sb : dlg->findChildren<QAbstractSpinBox *>()) {
            QFont sf = sb->font();
            if (sf.pointSizeF() > 0 && sf.pointSizeF() < 9.5) {
                sf.setPointSizeF(10.0);
                sb->setFont(sf);
            }
            if (sb->maximumHeight() >= 32 && sb->minimumHeight() < 32)
                sb->setMinimumHeight(32);
            if (!sb->styleSheet().contains(QLatin1String("font-size"))) {
                sb->setStyleSheet(sb->styleSheet()
                    + QStringLiteral(" QSpinBox, QDoubleSpinBox { font-size: 10pt; padding: 3px 10px 3px 6px; }"));
            }
        }

        // 5. On first show, expand dialog if needed so newly padded controls never clip
        auto it = skins.find(dlg);
        if (it != skins.end() && !it->shownOnce) {
            it->shownOnce = true;
            if (dlg->layout())
                dlg->layout()->activate();
            const QSize hint = dlg->sizeHint().expandedTo(dlg->minimumSizeHint());
            const int nw = qMax(dlg->width(), hint.width());
            const int nh = qMax(dlg->height(), hint.height());
            if (nw != dlg->width() || nh != dlg->height())
                dlg->resize(nw, nh);
        }
    }

    void skin(QDialog *dlg)
    {
        if (skins.contains(dlg) || busy.contains(dlg))
            return;
        if (!dlg->isWindow() || dlg->isVisible())
            return;
        if (dlg->property("pthkNoSkin").toBool())
            return;
        if (dlg->windowFlags() & Qt::FramelessWindowHint)
            return;

        busy.insert(dlg);

        Qt::WindowFlags f = dlg->windowFlags();
        f |= Qt::FramelessWindowHint;
        f &= ~Qt::WindowContextHelpButtonHint;
        dlg->setWindowFlags(f);

        // make room for the title bar (the layout respects contents margins)
        int l, t, r, b;
        dlg->getContentsMargins(&l, &t, &r, &b);
        dlg->setContentsMargins(l, t + H, r, b);

        QSize mn = dlg->minimumSize();
        QSize mx = dlg->maximumSize();
        if (mx.height() < QWIDGETSIZE_MAX)
            dlg->setMaximumHeight(mx.height() + H);
        if (mn.height() > 0)
            dlg->setMinimumHeight(mn.height() + H);
        dlg->resize(dlg->width(), dlg->height() + H);

        TitleBar *bar = new TitleBar(dlg->windowTitle(), dlg);
        bar->minimizeButton->hide();
        QObject::connect(bar->closeButton, &QAbstractButton::clicked, dlg, [dlg]() { dlg->reject(); });
        bar->setGeometry(0, 0, dlg->width(), H);
        bar->show();

        FrameOverlay *frame = new FrameOverlay(dlg);
        frame->setGeometry(dlg->rect());
        frame->show();
        frame->raise();

        dlg->setMouseTracking(true);

        skins.insert(dlg, Skin{ bar, frame, false });
        polishControls(dlg);
        busy.remove(dlg);
        QObject::connect(dlg, &QObject::destroyed, this, [this](QObject *obj) { skins.remove(obj); });
    }
};

#endif // DIALOGSKIN_H
