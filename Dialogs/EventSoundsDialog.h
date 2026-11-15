#ifndef EVENTSOUNDSDIALOG_H
#define EVENTSOUNDSDIALOG_H

// "เสียงพิธีการ / เอฟเฟกต์": one window for the sounds of an event - inviting the chairman, the
// national and royal anthems, applause, cheering, funny sounds (glass breaking ...).
// Each category is a plain folder with a Thai name under one root folder next to the program's data
// (".PTHKKaraoke\เสียงพิธีการ", portable: "<program>\เสียงพิธีการ"): a file put in a category folder is a
// card in that category. The sound banks the program already knows (PTHK's own, the shop's,
// other karaoke programs' FX folders) are listed below the categories, so older sounds are found too.
// Playing goes through SoundPads::preview (the main mixer: volume, limiter, F12 recording);
// a click on the playing card stops it. Dark like the playback bar. Header-only, no Q_OBJECT.

#include <QAbstractButton>
#include <QCollator>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFrame>
#include <QResizeEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QShowEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QSlider>
#include <QStyledItemDelegate>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>

#include "Config.h"
#include "Midi/SoundPads.h"

namespace EventSoundsUi {

// colours (the playback bar's dark look)
inline QColor bg()      { return QColor(0x15, 0x17, 0x1d); }
inline QColor panel()   { return QColor(0x1c, 0x1f, 0x27); }
inline QColor card()    { return QColor(0x23, 0x27, 0x31); }
inline QColor cardHi()  { return QColor(0x2b, 0x30, 0x3c); }
inline QColor line()    { return QColor(0x31, 0x36, 0x42); }
inline QColor text()    { return QColor(0xec, 0xee, 0xf3); }
inline QColor muted()   { return QColor(0x8b, 0x92, 0xa3); }
inline QColor accent()  { return QColor(0x0a, 0x84, 0xff); }

// one colour per category, so the list and the cards can be told apart at a glance
inline QColor tint(int i)
{
    static const QColor c[] = { QColor("#f5a524"), QColor("#ef4444"), QColor("#eab308"), QColor("#22c55e"),
                                QColor("#06b6d4"), QColor("#a855f7"), QColor("#ec4899"), QColor("#64748b") };
    return c[qMax(0, i) % 8];
}

inline QString duration(double s)
{
    if (s < 0)
        return QString();
    const int t = int(s + 0.5);
    return QString("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QChar('0'));
}

// a sound: a dark card with a round play button, its name, its length and type; while it plays the
// card lights up and a line along its bottom shows how far it is
class SoundCard : public QAbstractButton
{
public:
    SoundCard(const QString &path, const QColor &tintColor, QWidget *parent)
        : QAbstractButton(parent), file(path), tintC(tintColor)
    {
        setText(SoundPads::fileLabel(path));
        setToolTip(QDir::toNativeSeparators(path));
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::NoFocus);
        setFixedHeight(48);
        setMinimumWidth(170);
        sub = duration(SoundPads::fileSeconds(path));      // the length only: the type is in the tooltip
    }
    QString path() const { return file; }
    // -1 = not playing, else 0..1
    void setProgress(double p)
    {
        if (qAbs(p - prog) > 0.002 || (p < 0) != (prog < 0)) {
            prog = p;
            update();
        }
    }
    QSize sizeHint() const override { return QSize(200, 48); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool playing = prog >= 0;
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        QPainterPath body;
        body.addRoundedRect(r, 10, 10);
        p.fillPath(body, playing ? QColor(0x1d, 0x2c, 0x44) : (underMouse() ? cardHi() : card()));
        p.setPen(QPen(playing ? accent() : (underMouse() ? QColor(0x41, 0x48, 0x57) : line()), playing ? 1.6 : 1.0));
        p.drawPath(body);

        // the round button: play / stop
        const double d = 30;
        const QRectF c(r.left() + 10, r.center().y() - d / 2, d, d);
        p.setPen(Qt::NoPen);
        p.setBrush(playing ? accent() : tintC);
        p.drawEllipse(c);
        p.setBrush(Qt::white);
        if (playing) {
            p.drawRoundedRect(QRectF(c.center().x() - 5, c.center().y() - 5, 10, 10), 2, 2);
        } else {
            QPainterPath t;
            t.moveTo(c.center().x() - 3.5, c.center().y() - 6.5);
            t.lineTo(c.center().x() + 7, c.center().y());
            t.lineTo(c.center().x() - 3.5, c.center().y() + 6.5);
            t.closeSubpath();
            p.drawPath(t);
        }

        // one line: the name, its length at the right
        QFont s = font();
        s.setPixelSize(12);
        const QFontMetrics sfm(s);
        const QString right = playing ? QString::fromUtf8("หยุด") : sub;
        const double rw = sfm.horizontalAdvance(right) + 4;
        const double x = c.right() + 10;
        const double w = r.right() - 10 - rw - 8 - x;
        QFont f = font();
        f.setPixelSize(15);
        f.setBold(true);
        p.setFont(f);
        p.setPen(EventSoundsUi::text());
        const QFontMetrics fm(f);
        p.drawText(QRectF(x, r.top(), w, r.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(QAbstractButton::text(), Qt::ElideRight, int(w)));
        p.setFont(s);
        p.setPen(playing ? QColor(0x8c, 0xc4, 0xff) : muted());
        p.drawText(QRectF(r.right() - 10 - rw, r.top(), rw, r.height()), Qt::AlignRight | Qt::AlignVCenter, right);

        if (playing) {      // how far it is
            const QRectF track(r.left() + 10, r.bottom() - 4, r.width() - 20, 2.5);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 28));
            p.drawRoundedRect(track, 1.2, 1.2);
            p.setBrush(accent());
            p.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * prog, track.height()), 1.2, 1.2);
        }
    }

private:
    QString file, sub;
    QColor tintC;
    double prog = -1.0;
};

// the category list: section titles, a coloured dot, the name and a count at the right
class CategoryDelegate : public QStyledItemDelegate
{
public:
    enum { KindRole = Qt::UserRole + 1, CountRole, TintRole };    // kind: 0 category, 1 section title
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &i) const override
    {
        return QSize(200, i.data(KindRole).toInt() == 1 ? 28 : 31);
    }
    void paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &i) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(o.rect).adjusted(4, 2, -4, -2);
        const QString name = i.data(Qt::DisplayRole).toString();
        if (i.data(KindRole).toInt() == 1) {            // section title
            QFont f = o.font;
            f.setPixelSize(12);
            f.setBold(true);
            p->setFont(f);
            p->setPen(muted());
            p->drawText(r.adjusted(10, 5, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, name);
            p->restore();
            return;
        }
        const bool sel = o.state & QStyle::State_Selected;
        const bool hov = o.state & QStyle::State_MouseOver;
        if (sel || hov) {
            QPainterPath b;
            b.addRoundedRect(r, 8, 8);
            p->fillPath(b, sel ? QColor(0x2a, 0x36, 0x4d) : QColor(0x24, 0x28, 0x32));
            if (sel) {
                p->setPen(Qt::NoPen);
                p->setBrush(accent());
                p->drawRoundedRect(QRectF(r.left(), r.top() + 8, 3, r.height() - 16), 1.5, 1.5);
            }
        }
        p->setPen(Qt::NoPen);
        p->setBrush(i.data(TintRole).value<QColor>());
        p->drawEllipse(QPointF(r.left() + 18, r.center().y()), 4.5, 4.5);

        const int n = i.data(CountRole).toInt();
        QFont cf = o.font;
        cf.setPixelSize(12);
        cf.setBold(true);
        const QString cnt = n > 0 ? QString::number(n) : QString::fromUtf8("ว่าง");
        const QFontMetrics cfm(cf);
        const double cw = cfm.horizontalAdvance(cnt) + 16;
        const QRectF pill(r.right() - cw - 8, r.center().y() - 9, cw, 18);

        QFont f = o.font;
        f.setPixelSize(14);
        f.setBold(sel);
        p->setFont(f);
        p->setPen(n > 0 || sel ? text() : QColor(0xb4, 0xb9, 0xc5));
        const QRectF tr(r.left() + 32, r.top(), pill.left() - r.left() - 40, r.height());
        p->drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(f).elidedText(name, Qt::ElideMiddle, int(tr.width())));

        p->setFont(cf);
        if (n > 0) {
            p->setPen(Qt::NoPen);
            p->setBrush(sel ? accent() : QColor(0x33, 0x39, 0x46));
            p->drawRoundedRect(pill, 10, 10);
            p->setPen(Qt::white);
        } else {
            p->setPen(muted());
        }
        p->drawText(pill, Qt::AlignCenter, cnt);
        p->restore();
    }
};

} // namespace EventSoundsUi

class EventSoundsDialog : public QDialog
{
public:
    // the root folder of the categories
    static QString root()
    {
        return QDir::cleanPath(QDir(Config::DATABASE_DIR_PATH).absoluteFilePath(QString::fromUtf8("../เสียงพิธีการ")));
    }
    // the categories a new installation starts with (numbered: that is their order)
    static QStringList defaultCategories()
    {
        return {
            QString::fromUtf8("1 เชิญประธาน เปิดงาน"),
            QString::fromUtf8("2 เพลงชาติไทย"),
            QString::fromUtf8("3 เพลงสรรเสริญพระบารมี"),
            QString::fromUtf8("4 ปรบมือ"),
            QString::fromUtf8("5 เสียงโห่ เชียร์"),
            QString::fromUtf8("6 เสียงตลก แก้วแตก"),
            QString::fromUtf8("7 เพลงวันเกิด อวยพร"),
            QString::fromUtf8("8 อื่น ๆ"),
        };
    }
    // the folders exist (made once; a folder the shop deleted on purpose is not made again)
    static void ensureFolders()
    {
        QDir r(root());
        if (r.exists())
            return;
        r.mkpath(".");
        for (const QString &c : defaultCategories())
            r.mkpath(c);
        QFile f(r.absoluteFilePath(QString::fromUtf8("อ่านก่อน.txt")));
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream t(&f);
            t.setCodec("UTF-8");
            t.setGenerateByteOrderMark(true);       // Notepad shows the Thai text
            t << QString::fromUtf8(
                "โฟลเดอร์นี้คือ \"เสียงพิธีการ / เอฟเฟกต์\" ของโปรแกรม PTHK Karaoke\r\n\r\n"
                "- 1 โฟลเดอร์ย่อย = 1 หมวดเสียงในหน้าต่าง \"เสียงพิธีการ / เอฟเฟกต์\" (ปุ่ม อื่น ๆ บนแถบล่าง)\r\n"
                "- ใส่ไฟล์ MP3 / WAV / FLAC / M4A / OGG / WMA ลงในโฟลเดอร์หมวดนั้น แล้วไฟล์จะขึ้นเป็นปุ่มให้กดเล่น\r\n"
                "- ชื่อปุ่ม = ชื่อไฟล์ (ตัวเลขหน้าชื่อใช้เรียงลำดับ เช่น \"01 เชิญประธาน.mp3\")\r\n"
                "- สร้างโฟลเดอร์ใหม่ = หมวดใหม่ ลบโฟลเดอร์ = ลบหมวด\r\n");
        }
    }

    EventSoundsDialog(QWidget *parent, SoundPads *pads) : QDialog(parent), sp(pads)
    {
        using namespace EventSoundsUi;
        ensureFolders();
        setObjectName("EventSounds");
        setWindowTitle(QString::fromUtf8("เสียงพิธีการ / เอฟเฟกต์"));
        setAcceptDrops(true);
        setMinimumSize(900, 560);
        {   // big: every sound of a group in sight without scrolling (most of the screen)
            QScreen *sc = parent ? parent->screen() : QGuiApplication::primaryScreen();
            const QRect av = sc ? sc->availableGeometry() : QRect(0, 0, 1366, 768);
            resize(qBound(900, int(av.width() * 0.86), 1500), qBound(560, int(av.height() * 0.86), 920));
        }
        setStyleSheet(QStringLiteral(
            "#EventSounds { background: #15171d; }"
            "#EventSounds QLabel { color: #eceef3; background: transparent; }"
            "#EventSounds QLabel#muted { color: #8b92a3; }"
            "#EventSounds QFrame#sidebar { background: #1c1f27; border-radius: 14px; }"
            "#EventSounds QFrame#divider { background: #262a34; border: none; }"
            "#EventSounds QListWidget { background: transparent; border: none; outline: 0; }"
            "#EventSounds QScrollArea, #EventSounds QScrollArea > QWidget > QWidget { background: transparent; border: none; }"
            "#EventSounds QPushButton#primary { background: #0a84ff; color: white; border: none; border-radius: 9px;"
            "  padding: 9px 18px; font-weight: bold; }"
            "#EventSounds QPushButton#primary:hover { background: #3d9bff; }"
            "#EventSounds QPushButton#ghost { background: transparent; color: #d4d9e3; border: 1px solid #353b48;"
            "  border-radius: 9px; padding: 8px 16px; }"
            "#EventSounds QPushButton#ghost:hover { background: #232731; border-color: #4a5160; }"
            "#EventSounds QPushButton#link { background: transparent; color: #6fb4ff; border: none; padding: 2px 4px; }"
            "#EventSounds QPushButton#link:hover { color: #9ccbff; text-decoration: underline; }"
            "#EventSounds QPushButton#danger { background: #2a1b1f; color: #ff7b7b; border: 1px solid #4d2a30;"
            "  border-radius: 9px; padding: 9px 18px; font-weight: bold; }"
            "#EventSounds QPushButton#danger:hover { background: #3a2026; }"
            "#EventSounds QPushButton#neutral { background: #2a2f3a; color: #eceef3; border: none; border-radius: 9px;"
            "  padding: 9px 22px; font-weight: bold; }"
            "#EventSounds QPushButton#neutral:hover { background: #343a47; }"
            "#EventSounds QSlider::groove:horizontal { height: 4px; background: #2f3542; border-radius: 2px; }"
            "#EventSounds QSlider::sub-page:horizontal { background: #0a84ff; border-radius: 2px; }"
            "#EventSounds QSlider::handle:horizontal { background: white; width: 16px; margin: -6px 0; border-radius: 8px; }"
            "#EventSounds QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }"
            "#EventSounds QScrollBar::handle:vertical { background: #3a404d; border-radius: 4px; min-height: 30px; }"
            "#EventSounds QScrollBar::add-line:vertical, #EventSounds QScrollBar::sub-line:vertical { height: 0; }"
            "#EventSounds QScrollBar::add-page:vertical, #EventSounds QScrollBar::sub-page:vertical { background: none; }"));

        QVBoxLayout *main = new QVBoxLayout(this);
        main->setContentsMargins(22, 16, 22, 18);
        main->setSpacing(14);

        // ---- header
        {
            QVBoxLayout *h = new QVBoxLayout();
            h->setSpacing(2);
            QLabel *t = new QLabel(QString::fromUtf8("เสียงพิธีการ / เอฟเฟกต์"), this);
            QFont f = t->font();
            f.setPixelSize(24);
            f.setBold(true);
            t->setFont(f);
            QLabel *s = new QLabel(QString::fromUtf8(
                "คลิกการ์ดเพื่อเล่น  ·  คลิกอีกครั้งเพื่อหยุด  ·  ลากไฟล์เสียงมาวางบนหน้าต่างนี้เพื่อเพิ่มเข้าหมวดที่เลือก"), this);
            s->setObjectName("muted");
            h->addWidget(t);
            h->addWidget(s);
            main->addLayout(h);
        }

        QHBoxLayout *body = new QHBoxLayout();
        body->setSpacing(18);
        main->addLayout(body, 1);

        // ---- sidebar: the categories, then the sound banks found on this computer
        QFrame *side = new QFrame(this);
        side->setObjectName("sidebar");
        side->setFixedWidth(330);
        QVBoxLayout *sl = new QVBoxLayout(side);
        sl->setContentsMargins(8, 8, 8, 10);
        sl->setSpacing(6);
        list = new QListWidget(side);
        list->setItemDelegate(new CategoryDelegate(list));
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setMouseTracking(true);
        list->viewport()->setAttribute(Qt::WA_Hover);
        list->setFocusPolicy(Qt::NoFocus);
        sl->addWidget(list, 1);
        QPushButton *btnNew = new QPushButton(QString::fromUtf8("+  สร้างหมวดใหม่"), side);
        btnNew->setObjectName("ghost");
        sl->addWidget(btnNew);
        body->addWidget(side);

        // ---- the sounds of the chosen category
        QVBoxLayout *right = new QVBoxLayout();
        right->setSpacing(10);
        body->addLayout(right, 1);
        {
            QHBoxLayout *top = new QHBoxLayout();
            top->setSpacing(10);
            QVBoxLayout *names = new QVBoxLayout();
            names->setSpacing(2);
            title = new QLabel(this);
            QFont f = title->font();
            f.setPixelSize(21);
            f.setBold(true);
            title->setFont(f);
            names->addWidget(title);
            QHBoxLayout *pathRow = new QHBoxLayout();
            pathRow->setSpacing(4);
            folderLabel = new QLabel(this);
            folderLabel->setObjectName("muted");
            folderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            pathRow->addWidget(folderLabel, 1);
            names->addLayout(pathRow);
            top->addLayout(names, 1);
            btnOpen = new QPushButton(QString::fromUtf8("เปิดโฟลเดอร์"), this);
            btnOpen->setObjectName("ghost");
            btnOpen->setToolTip(QString::fromUtf8("เปิดโฟลเดอร์ของหมวดนี้ใน Windows: ใส่ไฟล์เสียงลงไป แล้วการ์ดจะขึ้นเอง"));
            btnAdd = new QPushButton(QString::fromUtf8("+  เพิ่มไฟล์เสียง"), this);
            btnAdd->setObjectName("primary");
            btnAdd->setToolTip(QString::fromUtf8("เลือกไฟล์เสียง โปรแกรมคัดลอกไปเก็บในโฟลเดอร์ของหมวดนี้ให้"));
            top->addWidget(btnOpen, 0, Qt::AlignTop);
            top->addWidget(btnAdd, 0, Qt::AlignTop);
            right->addLayout(top);
        }
        QFrame *div = new QFrame(this);
        div->setObjectName("divider");
        div->setFixedHeight(1);
        right->addWidget(div);

        QScrollArea *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        grid = new QWidget(scroll);
        gridLayout = new QGridLayout(grid);
        gridLayout->setContentsMargins(0, 4, 6, 4);
        gridLayout->setAlignment(Qt::AlignTop);
        gridLayout->setHorizontalSpacing(8);
        gridLayout->setVerticalSpacing(8);
        scroll->setWidget(grid);
        right->addWidget(scroll, 1);

        // ---- footer: what plays, the level, stop, close
        QFrame *div2 = new QFrame(this);
        div2->setObjectName("divider");
        div2->setFixedHeight(1);
        main->addWidget(div2);
        QHBoxLayout *bottom = new QHBoxLayout();
        bottom->setSpacing(12);
        nowPlaying = new QLabel(this);
        bottom->addWidget(nowPlaying, 1);
        QLabel *volT = new QLabel(QString::fromUtf8("ความดัง"), this);
        volT->setObjectName("muted");
        bottom->addWidget(volT);
        QSlider *vol = new QSlider(Qt::Horizontal, this);
        vol->setRange(-30, 12);
        vol->setValue(int(sp ? sp->padsGainDb() : 0.0));
        vol->setFixedWidth(180);
        vol->setFocusPolicy(Qt::NoFocus);
        vol->setToolTip(QString::fromUtf8("ความดังของเสียงพิธีการและแป้นเสียงทั้งหมด"));
        bottom->addWidget(vol);
        QLabel *volV = new QLabel(this);
        volV->setObjectName("muted");
        volV->setFixedWidth(52);
        auto showDb = [volV](int db) { volV->setText(QString("%1%2 dB").arg(db > 0 ? "+" : "").arg(db)); };
        showDb(vol->value());
        bottom->addWidget(volV);
        bottom->addSpacing(8);
        QPushButton *stop = new QPushButton(QString::fromUtf8("■  หยุดเสียงทั้งหมด"), this);
        stop->setObjectName("danger");
        QPushButton *close = new QPushButton(QString::fromUtf8("ปิด"), this);
        close->setObjectName("neutral");
        bottom->addWidget(stop);
        bottom->addWidget(close);
        main->addLayout(bottom);

        connect(list, &QListWidget::currentRowChanged, this, [this](int) { showCategory(); });
        connect(btnAdd, &QPushButton::clicked, this, [this]() { addFiles(); });
        connect(btnOpen, &QPushButton::clicked, this, [this]() {
            const Cat c = current();
            if (!c.folder.isEmpty())
                QDesktopServices::openUrl(QUrl::fromLocalFile(c.folder));
        });
        connect(btnNew, &QPushButton::clicked, this, [this]() { newCategory(); });
        connect(vol, &QSlider::valueChanged, this, [this, showDb](int db) {
            showDb(db);
            if (sp) sp->setPadsGainDb(db);
        });
        connect(stop, &QPushButton::clicked, this, [this]() { if (sp) sp->stopAll(); refreshPlaying(); });
        connect(close, &QPushButton::clicked, this, &QDialog::accept);

        // the playing card stays lit and shows how far it is (also when it ends by itself)
        QTimer *t = new QTimer(this);
        t->setInterval(120);
        connect(t, &QTimer::timeout, this, [this]() { refreshPlaying(); });
        t->start();

        fillList();
    }

    // the category shown first (its name), e.g. the one used last time
    void selectCategory(const QString &name)
    {
        for (int i = 0; i < cats.size(); ++i)
            if (!cats[i].name.isEmpty() && cats[i].name == name)
                list->setCurrentRow(i);
    }
    QString currentCategoryName() const { return current().name; }

protected:
    void showEvent(QShowEvent *e) override
    {
        QDialog::showEvent(e);
        if (list->currentRow() < 0 && list->count() > 1)
            list->setCurrentRow(1);             // (row 0 is the section title)
        showCategory();
    }
    void resizeEvent(QResizeEvent *e) override
    {
        QDialog::resizeEvent(e);
        const int cols = columns();
        if (cols != lastCols && !cards.isEmpty())
            showCategory();
    }
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->mimeData()->hasUrls() && current().own)
            e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override
    {
        QStringList files;
        for (const QUrl &u : e->mimeData()->urls())
            if (u.isLocalFile())
                files << u.toLocalFile();
        copyIn(files);
    }

public:
    struct Cat {
        QString name;           // shown ("" = a section title)
        QString folder;         // on disk
        QStringList files;      // sound files, in order
        bool own = false;       // one of the categories (files can be added)
        QColor tint;
    };

    static QVector<Cat> allCategories(SoundPads *sp)
    {
        ensureFolders();
        QVector<Cat> out;
        QFileInfoList dirs = QDir(root()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCollator col;
        col.setNumericMode(true);
        std::sort(dirs.begin(), dirs.end(), [&col](const QFileInfo &a, const QFileInfo &b) {
            return col.compare(a.fileName(), b.fileName()) < 0;
        });
        int k = 0;
        for (const QFileInfo &d : dirs) {
            Cat c;
            c.name = shown(d.fileName());
            c.folder = d.absoluteFilePath();
            c.files = SoundPads::soundFiles(c.folder);
            c.own = true;
            c.tint = EventSoundsUi::tint(k++);
            out.append(c);
        }
        for (const Cat &c : foundGroupsOf(sp))
            out.append(c);
        return out;
    }

private:
    Cat current() const
    {
        const int r = list->currentRow();
        return (r >= 0 && r < cats.size()) ? cats[r] : Cat();
    }
    int columns() const { return qBound(3, (width() - 330 - 80) / 215, 5); }

    static QString shown(const QString &folderName)
    {
        return SoundPads::fileLabel(folderName + ".x");     // "3 เพลงสรรเสริญพระบารมี" -> without the number
    }

    // what a sound is, from its file and folder names (the first rule that fits)
    static int groupOf(const QString &file)
    {
        const QString name = SoundPads::fileLabel(file).toLower();
        const QString folder = QFileInfo(file).absoluteDir().dirName().toLower();
        auto has = [](const QString &t, const char *words) {
            for (const QString &w : QString::fromUtf8(words).split('|'))
                if (t.contains(w))
                    return true;
            return false;
        };
        if (has(name, "ปรบมือ|โห่|เชียร์|กรี๊ด|lets go|นกหวีด|กลองรัว|ตบมุก"))           return 0;
        if (has(name, "หัวเราะ|เสียงหัว|คราง|โอ้โน|แก้วแตก|ติ้ว|จิ๊ว|แตรเศร้า|พระเจ้า|เครียด|เก็บตก|รอนาน")) return 1;
        if (has(folder, "น้าค่อม|น้าคอม|แอนนา"))                                          return 2;
        if (has(name, "bird|นก|ช้าง|ม้า|กบ|ฝนตก|ฟ้าร้อง"))                                 return 3;
        if (has(name, "แคน|พิณ|หัวเคียว|กลอง|สะแนน|ลาย") || has(folder, "แคน|heno|ม่วน"))  return 4;
        if (has(name, "starchip|wing|บั้งไฟ|ฆ้อง|ไซเลน|เทียบเวลา|มวย|พุทธัง|ติ๊งต่อง|แตร")) return 5;
        if (has(folder, "ตลก"))                                                           return 1;
        return 6;
    }
    static QVector<Cat> foundGroupsOf(SoundPads *sp)
    {
        static const char *names[] = { "ปรบมือ เชียร์", "เสียงตลก ฮา", "คำพูดฮิต", "เสียงสัตว์ ธรรมชาติ",
                                       "ดนตรีอีสาน แคน พิณ กลอง", "เสียงประกอบ", "เสียงไม่มีชื่อ" };
        const int n = int(sizeof(names) / sizeof(names[0]));
        QVector<Cat> g(n);
        for (int i = 0; i < n; ++i) {
            g[i].name = QString::fromUtf8(names[i]);
            g[i].tint = EventSoundsUi::tint(i + 3);
        }
        if (!sp)
            return {};
        QSet<QString> folders, seen;
        for (int b = 1; b < sp->bankCount(); ++b) {
            const QStringList f = sp->bankFiles(b);
            if (!f.isEmpty())
                folders.insert(QFileInfo(f.first()).absolutePath());
        }
        QStringList sortedFolders = folders.values();
        std::sort(sortedFolders.begin(), sortedFolders.end());
        for (const QString &dir : sortedFolders)
            for (const QString &file : SoundPads::soundFiles(dir)) {      // every file, not only the pads' 9
                // the same sound in two programs' folders: once
                const QString key = SoundPads::fileLabel(file).toLower() + '|' + QString::number(QFileInfo(file).size());
                if (seen.contains(key))
                    continue;
                seen.insert(key);
                g[groupOf(file)].files << file;
            }
        QVector<Cat> out;
        for (const Cat &c : g)
            if (!c.files.isEmpty())
                out << c;
        return out;
    }
    QVector<Cat> foundGroups() const { return foundGroupsOf(sp); }

    void addSection(const QString &title)
    {
        QListWidgetItem *it = new QListWidgetItem(title, list);
        it->setFlags(Qt::NoItemFlags);
        it->setData(EventSoundsUi::CategoryDelegate::KindRole, 1);
        cats.append(Cat());
    }
    void addCat(const Cat &c)
    {
        QListWidgetItem *it = new QListWidgetItem(c.name, list);
        it->setData(EventSoundsUi::CategoryDelegate::KindRole, 0);
        it->setData(EventSoundsUi::CategoryDelegate::CountRole, c.files.size());
        it->setData(EventSoundsUi::CategoryDelegate::TintRole, c.tint);
        it->setToolTip(QDir::toNativeSeparators(c.folder));
        cats.append(c);
    }

    void fillList()
    {
        const QString keep = current().name;
        cats.clear();
        list->clear();
        addSection(QString::fromUtf8("หมวดของร้าน"));
        QFileInfoList dirs = QDir(root()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCollator col;
        col.setNumericMode(true);
        std::sort(dirs.begin(), dirs.end(), [&col](const QFileInfo &a, const QFileInfo &b) {
            return col.compare(a.fileName(), b.fileName()) < 0;
        });
        int k = 0;
        for (const QFileInfo &d : dirs) {
            Cat c;
            c.name = shown(d.fileName());
            c.folder = d.absoluteFilePath();
            c.files = SoundPads::soundFiles(c.folder);
            c.own = true;
            c.tint = EventSoundsUi::tint(k++);
            addCat(c);
        }
        // the sounds already on this computer (the sound pads' folders: PTHK's own, the shop's, other
        // karaoke programs'), all together and sorted by what they are - no program names
        const QVector<Cat> found = foundGroups();
        if (!found.isEmpty()) {
            addSection(QString::fromUtf8("เสียงเอฟเฟกต์ในเครื่อง"));
            for (const Cat &c : found)
                addCat(c);
        }
        int row = -1;
        for (int i = 0; i < cats.size(); ++i)
            if (!keep.isEmpty() && cats[i].name == keep)
                row = i;
        list->setCurrentRow(row >= 0 ? row : 1);
        showCategory();
    }

    void showCategory()
    {
        const Cat c = current();
        while (QLayoutItem *it = gridLayout->takeAt(0)) {
            delete it->widget();
            delete it;
        }
        cards.clear();
        title->setText(c.name);
        btnAdd->setVisible(c.own);
        btnOpen->setVisible(!c.folder.isEmpty());
        const QString where = QDir::toNativeSeparators(c.folder);
        if (c.own)
            folderLabel->setText(QString::fromUtf8("โฟลเดอร์:  ") + where);
        else if (!c.name.isEmpty())
            folderLabel->setText(QString::fromUtf8("เสียงที่มีอยู่แล้วในเครื่อง จัดกลุ่มให้ตามชนิดเสียง  ·  ชี้ที่การ์ดเพื่อดูว่าไฟล์อยู่ที่ไหน"));
        else
            folderLabel->clear();
        folderLabel->setToolTip(where);

        lastCols = columns();
        if (c.files.isEmpty()) {
            gridLayout->addWidget(emptyState(c), 0, 0, 1, lastCols);
            refreshPlaying();
            return;
        }
        for (int i = 0; i < c.files.size(); ++i) {
            EventSoundsUi::SoundCard *b = new EventSoundsUi::SoundCard(c.files[i], c.tint, grid);
            const QString path = c.files[i];
            connect(b, &QAbstractButton::clicked, this, [this, path]() {
                if (!sp || !sp->preview(path))
                    QMessageBox::warning(this, windowTitle(), QString::fromUtf8("เล่นไฟล์นี้ไม่ได้:\n") + QDir::toNativeSeparators(path));
                refreshPlaying();
            });
            gridLayout->addWidget(b, i / lastCols, i % lastCols);
            cards.append(b);
        }
        for (int col = 0; col < lastCols; ++col)
            gridLayout->setColumnStretch(col, 1);
        refreshPlaying();
    }

    // nothing in the category yet: what to do, in the middle
    QWidget *emptyState(const Cat &c)
    {
        QWidget *w = new QWidget(grid);
        w->setMinimumHeight(320);
        QVBoxLayout *l = new QVBoxLayout(w);
        l->setAlignment(Qt::AlignCenter);
        l->setSpacing(10);
        QLabel *icon = new QLabel(QString::fromUtf8("♪"), w);
        QFont f = icon->font();
        f.setPixelSize(54);
        icon->setFont(f);
        icon->setStyleSheet(QString("color: %1;").arg(c.tint.isValid() ? c.tint.name() : QString("#8b92a3")));
        icon->setAlignment(Qt::AlignCenter);
        QLabel *t = new QLabel(QString::fromUtf8("ยังไม่มีเสียงในหมวดนี้"), w);
        QFont tf = t->font();
        tf.setPixelSize(18);
        tf.setBold(true);
        t->setFont(tf);
        t->setAlignment(Qt::AlignCenter);
        QLabel *s = new QLabel(QString::fromUtf8("เพิ่มไฟล์ MP3 / WAV ได้ 3 ทาง: กดปุ่มด้านล่าง  ·  ลากไฟล์มาวางบนหน้าต่างนี้\n"
                                                 "·  หรือคัดลอกไฟล์ลงในโฟลเดอร์ของหมวด (ปุ่ม \"เปิดโฟลเดอร์\" ด้านบน)"), w);
        s->setObjectName("muted");
        s->setAlignment(Qt::AlignCenter);
        l->addWidget(icon);
        l->addWidget(t);
        l->addWidget(s);
        if (c.own) {
            QPushButton *add = new QPushButton(QString::fromUtf8("+  เพิ่มไฟล์เสียง"), w);
            add->setObjectName("primary");
            connect(add, &QPushButton::clicked, this, [this]() { addFiles(); });
            l->addSpacing(6);
            l->addWidget(add, 0, Qt::AlignCenter);
        }
        return w;
    }

    void refreshPlaying()
    {
        const QString now = sp ? sp->previewPlaying() : QString();
        const double prog = sp ? sp->previewProgress() : -1.0;
        for (EventSoundsUi::SoundCard *b : cards) {
            const bool on = !now.isEmpty() && QFileInfo(b->path()) == QFileInfo(now);
            b->setProgress(on ? qMax(0.0, prog) : -1.0);
        }
        nowPlaying->setText(now.isEmpty()
            ? QString::fromUtf8("<span style='color:#8b92a3'>ยังไม่มีเสียงเล่นอยู่</span>")
            : QString::fromUtf8("<span style='color:#0a84ff'>●</span>&nbsp; กำลังเล่น: <b>%1</b>")
                  .arg(SoundPads::fileLabel(now).toHtmlEscaped()));
    }

    void addFiles()
    {
        if (!current().own)
            return;
        const QStringList files = QFileDialog::getOpenFileNames(this, QString::fromUtf8("เลือกไฟล์เสียงสำหรับหมวด ") + current().name,
            QString(), QString::fromUtf8("ไฟล์เสียง (*.mp3 *.wav *.flac *.m4a *.ogg *.wma *.aif *.aiff)"));
        copyIn(files);
    }

    // into the selected category's folder (a file of the same name: a number is added)
    void copyIn(const QStringList &files)
    {
        const Cat c = current();
        if (!c.own || files.isEmpty())
            return;
        int copied = 0;
        QStringList bad;
        for (const QString &src : files) {
            if (!SoundPads::isSoundFile(src)) {
                bad << QFileInfo(src).fileName();
                continue;
            }
            const QFileInfo fi(src);
            QString dst = QDir(c.folder).absoluteFilePath(fi.fileName());
            for (int n = 2; QFile::exists(dst); ++n)
                dst = QDir(c.folder).absoluteFilePath(QString("%1 (%2).%3").arg(fi.completeBaseName()).arg(n).arg(fi.suffix()));
            if (QFile::copy(src, dst))
                ++copied;
            else
                bad << fi.fileName();
        }
        fillList();
        if (!bad.isEmpty())
            QMessageBox::information(this, windowTitle(),
                QString::fromUtf8("เพิ่มแล้ว %1 ไฟล์\nไม่ได้เพิ่ม (ไม่ใช่ไฟล์เสียง หรือคัดลอกไม่ได้):\n%2").arg(copied).arg(bad.join("\n")));
    }

    void newCategory()
    {
        bool ok = false;
        QString name = QInputDialog::getText(this, QString::fromUtf8("สร้างหมวดใหม่"),
            QString::fromUtf8("ชื่อหมวด (เช่น \"เพลงมาร์ชบริษัท\"):"), QLineEdit::Normal, QString(), &ok).trimmed();
        name.remove(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")));    // not allowed in folder names
        if (!ok || name.isEmpty())
            return;
        int maxNum = 0;
        for (const QFileInfo &d : QDir(root()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            maxNum = qMax(maxNum, d.fileName().section(' ', 0, 0).toInt());
        const QString folder = QString("%1 %2").arg(maxNum + 1).arg(name);
        if (!QDir(root()).mkpath(folder)) {
            QMessageBox::warning(this, windowTitle(), QString::fromUtf8("สร้างโฟลเดอร์ไม่ได้"));
            return;
        }
        fillList();
        selectCategory(shown(folder));
    }

    SoundPads *sp = nullptr;
    QVector<Cat> cats;
    QListWidget *list = nullptr;
    QLabel *title = nullptr, *folderLabel = nullptr, *nowPlaying = nullptr;
    QWidget *grid = nullptr;
    QGridLayout *gridLayout = nullptr;
    QPushButton *btnAdd = nullptr, *btnOpen = nullptr;
    QVector<EventSoundsUi::SoundCard *> cards;
    int lastCols = 0;
};

#endif // EVENTSOUNDSDIALOG_H
