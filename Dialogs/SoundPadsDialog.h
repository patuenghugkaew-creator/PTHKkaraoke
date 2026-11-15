#ifndef SOUNDPADSDIALOG_H
#define SOUNDPADSDIALOG_H

// Sound pads: the settings dialog (name, built-in sound or own file, level, try it) and the
// on-screen panel (9 buttons). Header-only, no Q_OBJECT.

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QListWidget>
#include <QMessageBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>
#include <QWidget>
#include <functional>

#include "Midi/SoundPads.h"

class SoundPadsDialog : public QDialog
{
public:
    SoundPadsDialog(QWidget *parent, SoundPads *pads) : QDialog(parent), sp(pads)
    {
        setWindowTitle(QString::fromUtf8("ตั้งค่าแป้นเสียงเชียร์"));
        QVBoxLayout *v = new QVBoxLayout(this);
        QLabel *help = new QLabel(QString::fromUtf8(
            "กด Alt + 1 ถึง 9 หรือกดปุ่มบนแถบแป้นเสียง • เลือกเสียงในตัว หรือใส่ไฟล์เสียงของร้านเอง (WAV / MP3)"), this);
        help->setWordWrap(true);
        v->addWidget(help);

        // ---- sound banks from folders (e.g. other karaoke programs' SoundFx folders)
        QGroupBox *banksBox = new QGroupBox(QString::fromUtf8("ชุดเสียงจากโฟลเดอร์ (สลับชุดด้วย Alt + 0 หรือบนแถบปุ่ม)"), this);
        QVBoxLayout *bv = new QVBoxLayout(banksBox);
        QLabel *bh = new QLabel(QString::fromUtf8(
            "โฟลเดอร์ย่อยแต่ละโฟลเดอร์ = 1 ชุด • แป้น 1-9 = ไฟล์เสียง 9 ไฟล์แรกของชุด ชื่อปุ่มตามชื่อไฟล์"), banksBox);
        bh->setWordWrap(true);
        bv->addWidget(bh);
        rootList = new QListWidget(banksBox);
        rootList->addItems(sp->bankRoots());
        rootList->setMaximumHeight(90);
        bv->addWidget(rootList);
        QHBoxLayout *bb1 = new QHBoxLayout;
        QPushButton *add = new QPushButton(QString::fromUtf8("เพิ่มโฟลเดอร์..."), banksBox);
        QPushButton *del = new QPushButton(QString::fromUtf8("เอาออก"), banksBox);
        QPushButton *find = new QPushButton(QString::fromUtf8("ค้นหาในเครื่อง"), banksBox);
        bankGain = new QDoubleSpinBox(banksBox);
        bankGain->setRange(-30.0, 12.0);
        bankGain->setDecimals(0);
        bankGain->setSuffix(" dB");
        bankGain->setValue(sp->bankGainDb());
        bb1->addWidget(add);
        bb1->addWidget(del);
        bb1->addWidget(find);
        bb1->addStretch();
        bb1->addWidget(new QLabel(QString::fromUtf8("ความดังชุดเสียง"), banksBox));
        bb1->addWidget(bankGain);
        bv->addLayout(bb1);
        v->addWidget(banksBox);
        connect(add, &QPushButton::clicked, this, [this]() {
            const QString d = QFileDialog::getExistingDirectory(this, QString::fromUtf8("โฟลเดอร์ชุดเสียง"));
            if (!d.isEmpty() && rootList->findItems(QDir::cleanPath(d), Qt::MatchFixedString).isEmpty())
                rootList->addItem(QDir::cleanPath(d));
        });
        connect(del, &QPushButton::clicked, this, [this]() { delete rootList->currentItem(); });
        connect(find, &QPushButton::clicked, this, [this, find]() {
            find->setEnabled(false);
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const QStringList f = SoundPads::findBankRoots();
            QApplication::restoreOverrideCursor();
            find->setEnabled(true);
            for (const QString &d : f)
                if (rootList->findItems(d, Qt::MatchFixedString).isEmpty())
                    rootList->addItem(d);
            if (f.isEmpty())
                QMessageBox::information(this, windowTitle(), QString::fromUtf8("ไม่พบโฟลเดอร์ชุดเสียงของโปรแกรมคาราโอเกะในเครื่องนี้"));
        });

        v->addWidget(new QLabel(QString::fromUtf8("<b>ชุด \"ตั้งเอง\"</b>"), this));
        QGridLayout *g = new QGridLayout;
        const char *heads[] = { "แป้น", "ชื่อบนปุ่ม", "เสียง", "ไฟล์เสียงของร้าน", "", "ความดัง", "" };
        for (int c = 0; c < 7; ++c)
            g->addWidget(new QLabel(QString::fromUtf8(heads[c]), this), 0, c);

        for (int i = 0; i < SoundPads::Count; ++i) {
            const SoundPads::Pad p = sp->pad(i);
            Row &r = rows[i];
            const int row = i + 1;
            g->addWidget(new QLabel(QString("Alt + %1").arg(i + 1), this), row, 0);

            r.name = new QLineEdit(p.name, this);
            r.name->setPlaceholderText(SoundPads::builtinName(p.builtin));
            g->addWidget(r.name, row, 1);

            r.sound = new QComboBox(this);
            for (int b = 0; b < SoundPads::BuiltinCount; ++b)
                r.sound->addItem(SoundPads::builtinName(b), b);
            r.sound->setCurrentIndex(r.sound->findData(p.builtin));
            g->addWidget(r.sound, row, 2);

            r.file = new QLineEdit(p.file, this);
            setFileHint(i, p.builtin);
            r.file->setClearButtonEnabled(true);
            r.file->setMinimumWidth(220);
            g->addWidget(r.file, row, 3);

            QPushButton *browse = new QPushButton(QString::fromUtf8("เลือก..."), this);
            g->addWidget(browse, row, 4);
            connect(browse, &QPushButton::clicked, this, [this, i]() {
                const QString f = QFileDialog::getOpenFileName(this, QString::fromUtf8("ไฟล์เสียงของแป้น %1").arg(i + 1),
                                                               QFileInfo(rows[i].file->text()).absolutePath(),
                                                               QString::fromUtf8("ไฟล์เสียง (*.wav *.mp3 *.ogg *.flac *.m4a *.wma)"));
                if (!f.isEmpty())
                    rows[i].file->setText(f);
            });

            r.gain = new QDoubleSpinBox(this);
            r.gain->setRange(-30.0, 12.0);
            r.gain->setSingleStep(1.0);
            r.gain->setDecimals(0);
            r.gain->setSuffix(" dB");
            r.gain->setValue(p.gainDb);
            g->addWidget(r.gain, row, 5);

            QPushButton *tryIt = new QPushButton(QString::fromUtf8("ลองฟัง"), this);
            g->addWidget(tryIt, row, 6);
            connect(tryIt, &QPushButton::clicked, this, [this, i]() {
                const SoundPads::Pad keep = sp->pad(i);
                const int keepBank = sp->currentBank();
                sp->setPad(i, collect(i));
                sp->setBankSilently(0);     // try the pad of "ตั้งเอง" whatever bank is chosen
                sp->trigger(i);
                sp->setBankSilently(keepBank);
                sp->setPad(i, keep);    // only kept with OK
            });
            // the name field shows what the chosen sound is called while it has no name of its own
            connect(r.sound, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, i]() {
                const int b = rows[i].sound->currentData().toInt();
                rows[i].name->setPlaceholderText(SoundPads::builtinName(b));
                setFileHint(i, b);
            });
        }
        v->addLayout(g);

        QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(bb, &QDialogButtonBox::accepted, this, [this]() {
            for (int i = 0; i < SoundPads::Count; ++i)
                sp->setPad(i, collect(i));
            QStringList roots;
            for (int r = 0; r < rootList->count(); ++r)
                roots << rootList->item(r)->text();
            sp->setBankRoots(roots);
            sp->setBankGainDb(bankGain->value());
            sp->save();
            accept();
        });
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        v->addWidget(bb);
    }

private:
    struct Row { QLineEdit *name = nullptr; QComboBox *sound = nullptr; QLineEdit *file = nullptr; QDoubleSpinBox *gain = nullptr; };

    // what an empty file field plays: a recorded sound from the banks, else the soundfont
    void setFileHint(int i, int builtin)
    {
        const QString real = sp->realFileFor(builtin);
        rows[i].file->setPlaceholderText(real.isEmpty() ? QString::fromUtf8("(ใช้เสียงในตัว)")
                                                        : QString::fromUtf8("(ใช้เสียงจริง: %1)").arg(SoundPads::fileLabel(real)));
        rows[i].file->setToolTip(real);
    }

    SoundPads::Pad collect(int i) const
    {
        SoundPads::Pad p;
        p.name = rows[i].name->text().trimmed();
        p.builtin = rows[i].sound->currentData().toInt();
        p.file = rows[i].file->text().trimmed();
        p.gainDb = rows[i].gain->value();
        return p;
    }

    SoundPads *sp;
    Row rows[SoundPads::Count];
    QListWidget *rootList = nullptr;
    QDoubleSpinBox *bankGain = nullptr;
};

// 9 buttons floating over the main window. It never takes the keyboard, so the main window's keys
// keep working while it is shown.
class SoundPadsPanel : public QWidget
{
public:
    std::function<void(int)> onPad;         // a pad was pressed
    std::function<void()> onSettings;
    std::function<void()> onStopAll;
    std::function<void()> onAddSounds;
    std::function<void()> onClosed;

    SoundPadsPanel(QWidget *parent, SoundPads *pads)
        : QWidget(parent, Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus), sp(pads)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setWindowTitle(QString::fromUtf8("แป้นเสียงเชียร์"));
        QVBoxLayout *v = new QVBoxLayout(this);
        v->setContentsMargins(6, 6, 6, 6);
        bankBox = new QComboBox(this);
        bankBox->setFocusPolicy(Qt::NoFocus);
        bankBox->setToolTip(QString::fromUtf8("ชุดเสียง (Alt + 0 = ชุดถัดไป)"));
        connect(bankBox, QOverload<int>::of(&QComboBox::activated), this, [this](int b) {
            sp->setBank(b);
            refresh();
        });
        v->addWidget(bankBox);
        QGridLayout *g = new QGridLayout;
        g->setSpacing(4);
        for (int i = 0; i < SoundPads::Count; ++i) {
            QPushButton *b = new QPushButton(this);
            b->setFocusPolicy(Qt::NoFocus);
            b->setMinimumSize(96, 52);
            connect(b, &QPushButton::clicked, this, [this, i]() { if (onPad) onPad(i); });
            buttons[i] = b;
            g->addWidget(b, i / 3, i % 3);
        }
        v->addLayout(g);

        // level of all the pads (the song is not touched) and stop
        QHBoxLayout *lv = new QHBoxLayout;
        lv->addWidget(new QLabel(QString::fromUtf8("ความดัง"), this));
        level = new QSlider(Qt::Horizontal, this);
        level->setFocusPolicy(Qt::NoFocus);
        level->setRange(-30, 12);
        level->setValue(int(std::lround(sp->padsGainDb())));
        level->setToolTip(QString::fromUtf8("ความดังของแป้นเสียงทุกแป้น (ไม่กระทบเสียงเพลง)"));
        levelText = new QLabel(this);
        levelText->setMinimumWidth(48);
        auto showLevel = [this](int db) { levelText->setText((db > 0 ? "+" : "") + QString::number(db) + " dB"); };
        showLevel(level->value());
        connect(level, &QSlider::valueChanged, this, [this, showLevel](int db) {
            showLevel(db);
            sp->setPadsGainDb(db);
        });
        lv->addWidget(level, 1);
        lv->addWidget(levelText);
        v->addLayout(lv);

        QHBoxLayout *bl = new QHBoxLayout;
        QPushButton *stop = new QPushButton(QString::fromUtf8("■ หยุดทั้งหมด"), this);
        stop->setFocusPolicy(Qt::NoFocus);
        stop->setToolTip(QString::fromUtf8("Alt + Backspace • กดแป้นเดิมซ้ำ = หยุดเสียงแป้นนั้น"));
        connect(stop, &QPushButton::clicked, this, [this]() { if (onStopAll) onStopAll(); });
        QPushButton *add = new QPushButton(QString::fromUtf8("＋ เพิ่มเสียง..."), this);
        add->setFocusPolicy(Qt::NoFocus);
        connect(add, &QPushButton::clicked, this, [this]() { if (onAddSounds) onAddSounds(); });
        QPushButton *set = new QPushButton(QString::fromUtf8("ตั้งค่าแป้น..."), this);
        set->setFocusPolicy(Qt::NoFocus);
        connect(set, &QPushButton::clicked, this, [this]() { if (onSettings) onSettings(); });
        bl->addWidget(stop);
        bl->addWidget(add);
        bl->addWidget(set);
        v->addLayout(bl);
        refresh();
    }

    void refresh()
    {
        bankBox->clear();
        for (int b = 0; b < sp->bankCount(); ++b)
            bankBox->addItem(sp->bankName(b));
        bankBox->setCurrentIndex(sp->currentBank());
        bankBox->setVisible(sp->bankCount() > 1);
        for (int i = 0; i < SoundPads::Count; ++i) {
            buttons[i]->setText(QString("%1\n%2").arg(i + 1).arg(sp->label(i)));
            buttons[i]->setEnabled(!sp->isEmpty(i));
        }
    }

protected:
    void closeEvent(QCloseEvent *e) override
    {
        QWidget::closeEvent(e);
        if (onClosed)
            onClosed();
    }

private:
    SoundPads *sp;
    QComboBox *bankBox = nullptr;
    QSlider *level = nullptr;
    QLabel *levelText = nullptr;
    QPushButton *buttons[SoundPads::Count] = {};
};

#endif // SOUNDPADSDIALOG_H
