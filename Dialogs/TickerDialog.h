#ifndef TICKERDIALOG_H
#define TICKERDIALOG_H

// "ข้อความวิ่ง" settings: on / off, which screens, top or bottom, the shop's messages, the next songs,
// speed, size and colours. Every change is shown at once; Cancel puts the old settings back.
// Header-only, no Q_OBJECT. Settings group "Ticker/".

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <functional>

class TickerDialog : public QDialog
{
public:
    std::function<void()> onChanged;    // the settings changed: show them

    // the defaults, also used by the player
    static QStringList defaultMessages() { return { QString::fromUtf8("ยินดีต้อนรับ ขอให้สนุกกับการร้องเพลง") }; }

    TickerDialog(QWidget *parent, QSettings *s) : QDialog(parent), set(s)
    {
        setWindowTitle(QString::fromUtf8("ข้อความวิ่ง"));
        resize(560, 520);
        set->beginGroup("Ticker");
        for (const QString &k : set->childKeys())
            before[k] = set->value(k);
        set->endGroup();

        QVBoxLayout *v = new QVBoxLayout(this);
        on = new QCheckBox(QString::fromUtf8("แสดงข้อความวิ่ง"), this);
        on->setChecked(set->value("Ticker/On", false).toBool());
        v->addWidget(on);

        QFormLayout *f = new QFormLayout;
        QHBoxLayout *screens = new QHBoxLayout;
        onMain = new QCheckBox(QString::fromUtf8("จอหลัก"), this);
        onSecond = new QCheckBox(QString::fromUtf8("จอที่ 2"), this);
        onMain->setChecked(set->value("Ticker/Main", true).toBool());
        onSecond->setChecked(set->value("Ticker/Second", true).toBool());
        screens->addWidget(onMain);
        screens->addWidget(onSecond);
        screens->addStretch();
        f->addRow(QString::fromUtf8("แสดงที่"), screens);

        where = new QComboBox(this);
        where->addItem(QString::fromUtf8("ขอบบน"), true);
        where->addItem(QString::fromUtf8("ขอบล่าง"), false);
        where->setCurrentIndex(set->value("Ticker/Top", true).toBool() ? 0 : 1);
        f->addRow(QString::fromUtf8("ตำแหน่ง"), where);
        v->addLayout(f);

        v->addWidget(new QLabel(QString::fromUtf8("ข้อความของร้าน (บรรทัดละ 1 ข้อความ วนแสดงทุกข้อความ)"), this));
        msgs = new QPlainTextEdit(this);
        msgs->setPlainText(set->value("Ticker/Messages", defaultMessages()).toStringList().join('\n'));
        msgs->setPlaceholderText(QString::fromUtf8("เช่น ยินดีต้อนรับ / โปรโมชั่นเบียร์ 3 ขวด 199 บาท / สุขสันต์วันเกิดคุณ..."));
        v->addWidget(msgs, 1);

        QHBoxLayout *next = new QHBoxLayout;
        showNext = new QCheckBox(QString::fromUtf8("แสดงเพลงถัดไปในคิว"), this);
        showNext->setChecked(set->value("Ticker/ShowNext", true).toBool());
        nextCount = new QSpinBox(this);
        nextCount->setRange(1, 5);
        nextCount->setValue(set->value("Ticker/NextCount", 2).toInt());
        nextCount->setSuffix(QString::fromUtf8(" เพลง"));
        next->addWidget(showNext);
        next->addWidget(nextCount);
        next->addStretch();
        v->addLayout(next);

        QFormLayout *look = new QFormLayout;
        speed = slider(40, 300, set->value("Ticker/Speed", 110).toInt());
        size = slider(60, 200, set->value("Ticker/Size", 100).toInt());
        look->addRow(QString::fromUtf8("ความเร็ว"), speed);
        look->addRow(QString::fromUtf8("ขนาดตัวอักษร"), size);
        fg = set->value("Ticker/Fg", QColor(255, 255, 255)).value<QColor>();
        bg = set->value("Ticker/Bg", QColor(0, 0, 0)).value<QColor>();
        QHBoxLayout *colours = new QHBoxLayout;
        fgBtn = new QPushButton(QString::fromUtf8("สีตัวอักษร"), this);
        bgBtn = new QPushButton(QString::fromUtf8("สีพื้น"), this);
        colours->addWidget(fgBtn);
        colours->addWidget(bgBtn);
        colours->addStretch();
        look->addRow(QString::fromUtf8("สี"), colours);
        alpha = slider(0, 255, set->value("Ticker/BgAlpha", 150).toInt());
        look->addRow(QString::fromUtf8("ความทึบของพื้น"), alpha);
        v->addLayout(look);
        paintButtons();

        QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        v->addWidget(bb);
        connect(bb, &QDialogButtonBox::accepted, this, [this]() { save(); accept(); });
        connect(bb, &QDialogButtonBox::rejected, this, [this]() {
            set->remove("Ticker");                  // the settings as they were
            for (auto it = before.begin(); it != before.end(); ++it)
                set->setValue("Ticker/" + it.key(), it.value());
            if (onChanged) onChanged();
            reject();
        });

        // show every change at once
        auto live = [this]() { save(); };
        for (QCheckBox *c : { on, onMain, onSecond, showNext })
            connect(c, &QCheckBox::toggled, this, live);
        connect(where, QOverload<int>::of(&QComboBox::currentIndexChanged), this, live);
        connect(nextCount, QOverload<int>::of(&QSpinBox::valueChanged), this, live);
        for (QSlider *sl : { speed, size, alpha })
            connect(sl, &QSlider::valueChanged, this, live);
        connect(msgs, &QPlainTextEdit::textChanged, this, live);
        connect(fgBtn, &QPushButton::clicked, this, [this]() {
            const QColor c = QColorDialog::getColor(fg, this, QString::fromUtf8("สีตัวอักษร"));
            if (c.isValid()) { fg = c; paintButtons(); save(); }
        });
        connect(bgBtn, &QPushButton::clicked, this, [this]() {
            const QColor c = QColorDialog::getColor(bg, this, QString::fromUtf8("สีพื้น"));
            if (c.isValid()) { bg = c; paintButtons(); save(); }
        });
    }

private:
    QSlider *slider(int lo, int hi, int value)
    {
        QSlider *s = new QSlider(Qt::Horizontal, this);
        s->setRange(lo, hi);
        s->setValue(value);
        return s;
    }
    void paintButtons()
    {
        fgBtn->setStyleSheet(QString("QPushButton { color: %1; background: %2; }").arg(fg.name(), bg.name()));
        bgBtn->setStyleSheet(QString("QPushButton { color: %1; background: %2; }").arg(fg.name(), bg.name()));
    }
    void save()
    {
        QStringList m;
        for (const QString &l : msgs->toPlainText().split('\n'))
            if (!l.trimmed().isEmpty())
                m << l.trimmed();
        set->setValue("Ticker/On", on->isChecked());
        set->setValue("Ticker/Main", onMain->isChecked());
        set->setValue("Ticker/Second", onSecond->isChecked());
        set->setValue("Ticker/Top", where->currentData().toBool());
        set->setValue("Ticker/Messages", m);
        set->setValue("Ticker/ShowNext", showNext->isChecked());
        set->setValue("Ticker/NextCount", nextCount->value());
        set->setValue("Ticker/Speed", speed->value());
        set->setValue("Ticker/Size", size->value());
        set->setValue("Ticker/Fg", fg);
        set->setValue("Ticker/Bg", bg);
        set->setValue("Ticker/BgAlpha", alpha->value());
        if (onChanged)
            onChanged();
    }

    QSettings *set;
    QMap<QString, QVariant> before;
    QCheckBox *on, *onMain, *onSecond, *showNext;
    QComboBox *where;
    QPlainTextEdit *msgs;
    QSpinBox *nextCount;
    QSlider *speed, *size, *alpha;
    QPushButton *fgBtn, *bgBtn;
    QColor fg, bg;
};

#endif // TICKERDIALOG_H
