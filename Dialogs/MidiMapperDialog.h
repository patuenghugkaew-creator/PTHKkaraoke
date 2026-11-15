// MidiMapperDialog.h: "แยกเสียงมิดี้ (MIDI Mapper)" - where every part of the music goes, in one window
// (like eXtreme's Midi Mapper):
//  - main MIDI out and MIDI in
//  - tab "เครื่องดนตรี" / "กลอง": each mixer strip to the SoundFont or to a MIDI port (loopMIDI ...),
//    "ส่งออกอย่างเดียว" or "ส่งออก + เสียงในโปรแกรม"; quick "whole drum kit to a port"
//  - tab "ช่อง MIDI 1-16": each channel follows the main out, plays on the SoundFont, or goes to a port
//  - save / open a set as a .pthkmap file (JSON, by port name) to switch setups in one click
// Everything is kept by port name: a port that is missing now (loopMIDI not started) is kept and
// connected when it shows up (MainWindow::applyMidiRouting, every 2 s). The MIDI-in port is never used
// as an output (it would loop). Changes apply at once.
#ifndef MIDIMAPPERDIALOG_H
#define MIDIMAPPERDIALOG_H

#include "MainWindow.h"
#include "Midi/MidiPlayer.h"
#include "Midi/MidiSynthesizer.h"
#include "Config.h"

#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

class MidiMapperDialog : public QDialog
{
public:
    MidiMapperDialog(MainWindow *mainWin, QWidget *parent = nullptr)
        : QDialog(parent), mw(mainWin), player(mainWin->midiPlayer()), syn(mainWin->midiPlayer()->midiSynthesizer())
    {
        setWindowTitle(tr("แยกเสียงมิดี้ (MIDI Mapper) - ส่งเสียงออก loopMIDI / โปรแกรมอื่น"));
        setMinimumSize(940, 720);
        // big, plain text: the shop reads this from a distance / a phone remote
        setStyleSheet("QDialog, QDialog QWidget { font-size: 17px; }"
                      "QTabBar::tab { font-size: 17px; padding: 8px 18px; }"
                      "QHeaderView::section { font-size: 16px; font-weight: 600; padding: 6px; }");
        QVBoxLayout *root = new QVBoxLayout(this);

        // ---- main out / in
        QHBoxLayout *top = new QHBoxLayout;
        top->addWidget(new QLabel(tr("มิดี้ขาออกหลัก:")));
        cbOut = new QComboBox;
        cbOut->setMinimumWidth(220);
        top->addWidget(cbOut, 1);
        top->addSpacing(16);
        top->addWidget(new QLabel(tr("มิดี้ขาเข้า:")));
        cbIn = new QComboBox;
        cbIn->setMinimumWidth(200);
        top->addWidget(cbIn, 1);
        root->addLayout(top);
        lbWarn = new QLabel;
        lbWarn->setStyleSheet("color: #b45309; font-weight: 600;");
        lbWarn->setWordWrap(true);
        root->addWidget(lbWarn);

        // ---- tabs
        QTabWidget *tabs = new QTabWidget;
        tInst = makeTable(tr("เครื่องดนตรี"));
        tDrum = makeTable(tr("ชิ้นกลอง"));
        tCh = makeTable(tr("ช่อง MIDI"));
        QWidget *drumPage = new QWidget;
        {
            QVBoxLayout *dl = new QVBoxLayout(drumPage);
            dl->setContentsMargins(0, 0, 0, 0);
            QHBoxLayout *quick = new QHBoxLayout;
            quick->addWidget(new QLabel(tr("ส่งกลองทั้งชุดไป:")));
            cbDrumAll = new QComboBox;
            cbDrumAll->setMinimumWidth(200);
            quick->addWidget(cbDrumAll);
            QPushButton *btnDrumAll = new QPushButton(tr("ใช้กับทุกชิ้น"));
            quick->addWidget(btnDrumAll);
            quick->addStretch(1);
            dl->addLayout(quick);
            dl->addWidget(tDrum);
            connect(btnDrumAll, &QPushButton::clicked, this, [this]() {
                const QString port = cbDrumAll->currentData().toString();
                syn->sendAllNotesOff(9);
                for (int t : drumTypes())
                    syn->setExtMidiOut(static_cast<InstrumentType>(t), port);
                fillTables();
            });
        }
        tabs->addTab(tInst, tr("เครื่องดนตรี"));
        tabs->addTab(drumPage, tr("กลอง (แยกชิ้น)"));
        tabs->addTab(tCh, tr("ช่อง MIDI 1-16"));
        root->addWidget(tabs, 1);

        QLabel *hint = new QLabel(tr("เครื่องดนตรีที่ส่งออกยังใช้ปุ่ม M / S เฟดเดอร์ และแถบมิเตอร์ในมิกเซอร์ได้ (เฟดเดอร์ = ความแรงโน้ตที่ส่งออก)\n"
                                     "ถ้ายังไม่ได้เปิด loopMIDI ค่าที่ตั้งไว้ไม่หาย โปรแกรมจะต่อให้เองเมื่อพอร์ตพร้อม"));
        hint->setWordWrap(true);
        root->addWidget(hint);

        // ---- bottom
        QHBoxLayout *bottom = new QHBoxLayout;
        QPushButton *btnSave = new QPushButton(tr("บันทึกชุด..."));
        QPushButton *btnOpen = new QPushButton(tr("เปิดชุด..."));
        QPushButton *btnReset = new QPushButton(tr("คืนเป็นเสียงในโปรแกรมทั้งหมด"));
        QPushButton *btnClose = new QPushButton(tr("ปิด"));
        bottom->addWidget(btnSave);
        bottom->addWidget(btnOpen);
        bottom->addWidget(btnReset);
        bottom->addStretch(1);
        bottom->addWidget(btnClose);
        root->addLayout(bottom);
        connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
        connect(btnSave, &QPushButton::clicked, this, [this]() { saveSet(); });
        connect(btnOpen, &QPushButton::clicked, this, [this]() { openSet(); });
        connect(btnReset, &QPushButton::clicked, this, [this]() {
            if (QMessageBox::question(this, windowTitle(), tr("ให้ทุกเครื่องดนตรี ทุกช่อง และมิดี้ขาออกหลัก กลับมาใช้เสียงในโปรแกรม (SoundFont)?"))
                    != QMessageBox::Yes)
                return;
            mw->resetMidiRoutingToSoundfont();
            fillAll();
        });

        connect(cbOut, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
            mw->setMidiOutWant(cbOut->currentData().toString());
            mw->applyMidiRouting();
            fillChannelTable();         // "ตามมิดี้ขาออกหลัก" rows change meaning
            refreshStatus();
        });
        connect(cbIn, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
            mw->stop();     // a new input mid-song leaves the synth with lost instruments
            mw->setMidiInWant(cbIn->currentData().toString());
            mw->applyMidiRouting();
            refreshStatus();
        });

        fillAll();
        poll.setInterval(250);
        connect(&poll, &QTimer::timeout, this, [this]() { refreshStatus(); });
        poll.start();
    }

private:
    MainWindow *mw;
    MidiPlayer *player;
    MidiSynthesizer *syn;
    QComboBox *cbOut = nullptr, *cbIn = nullptr, *cbDrumAll = nullptr;
    QLabel *lbWarn = nullptr;
    QTableWidget *tInst = nullptr, *tDrum = nullptr, *tCh = nullptr;
    QTimer poll;
    QStringList outs;
    QSet<QString> busy;         // ports that sent something in the last poll

    static QList<int> melodicTypes()
    {
        QList<int> l;
        for (int i = 0; i <= int(InstrumentType::SoundEffects); ++i)
            l << i;
        l << int(InstrumentType::Canthai);
        return l;
    }
    static QList<int> drumTypes()
    {
        QList<int> l;
        for (int i = int(InstrumentType::BassDrum); i <= int(InstrumentType::PercussionEtc); ++i)
            l << i;
        return l;
    }
    static QString typeName(int t)
    {
        static const char *n[] = {
            "Piano (เปียโน)", "Organ (ออร์แกน)", "Accordion (แอคคอร์เดียน)", "Chromatic Percussion (ระนาด / เบลล์)",
            "Percussive", "Bass (เบส)", "Guitar Nylon (กีตาร์โปร่งไนลอน)", "Guitar Steel (กีตาร์โปร่ง)",
            "Guitar Jazz (กีตาร์แจ๊ส)", "Guitar Clean (กีตาร์ไฟฟ้า)", "Overdriven Guitar", "Distortion Guitar",
            "Guitar Harmonics", "Trumpet (ทรัมเป็ต)", "Brass (เครื่องทองเหลือง)", "Synth Brass", "Saxophone (แซกโซโฟน)",
            "Reed (คลาริเน็ต / โอโบ)", "Pipe (ขลุ่ย / ฟลุต)", "Strings (เครื่องสาย)", "Ensemble (วงสาย / คอรัส)",
            "Synth Lead", "Synth Pad", "Synth Effects", "Ethnic (พิณ / แคน / ซึง)", "Sound Effects",
            "Kick (กลองกระเดื่อง)", "Snare (สแนร์)", "Side Stick", "High Tom (ทอมสูง)", "Mid Tom (ทอมกลาง)",
            "Low Tom (ทอมต่ำ)", "Hi-hat (ไฮแฮต)", "Cowbell", "Crash (แฉ)", "Ride", "Bongo (บองโก)", "Conga (คองก้า)",
            "Timbale", "ฉิ่ง", "ฉาบ", "เครื่องเคาะอื่น ๆ",
        };
        if (t >= 0 && t < int(sizeof(n) / sizeof(n[0])))
            return QString::fromUtf8(n[t]);
        if (t == int(InstrumentType::Canthai))
            return QString::fromUtf8("Canthai (แคน, ชุด 101 / 108 / 110)");
        return QString::number(t);
    }

    QTableWidget *makeTable(const QString &first)
    {
        QTableWidget *t = new QTableWidget(0, 4);
        t->setHorizontalHeaderLabels({ first, tr("ส่งไปที่"), tr("เสียงในโปรแกรม"), tr("สถานะ") });
        t->verticalHeader()->setVisible(false);
        t->setSelectionMode(QAbstractItemView::NoSelection);
        t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        t->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        t->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        t->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
        t->setColumnWidth(3, 210);
        t->verticalHeader()->setDefaultSectionSize(44);
        return t;
    }

    // a port combo: the given first items, the ports present, and a wanted port that is missing now
    void fillPortCombo(QComboBox *cb, const QList<QPair<QString, QString>> &first, const QString &want)
    {
        cb->clear();
        for (const auto &f : first)
            cb->addItem(f.first, f.second);
        for (const QString &n : outs)
            cb->addItem(n, n);
        if (!want.isEmpty() && cb->findData(want) < 0)
            cb->addItem(want + tr(" (ยังไม่พบ)"), want);
        cb->setCurrentIndex(qMax(0, cb->findData(want)));
    }

    void fillAll()
    {
        outs = MidiPlayer::midiDevices();
        const QStringList ins = MidiPlayer::midiInDevices();
        fillPortCombo(cbOut, { { tr("เสียงในโปรแกรม (SoundFont)"), QString() } }, mw->midiOutWant());
        cbIn->clear();
        cbIn->addItem(tr("ไม่ใช้"), QString());
        for (const QString &n : ins)
            cbIn->addItem(n, n);
        const QString inWant = mw->midiInWant();
        if (!inWant.isEmpty() && cbIn->findData(inWant) < 0)
            cbIn->addItem(inWant + tr(" (ยังไม่พบ)"), inWant);
        cbIn->setCurrentIndex(qMax(0, cbIn->findData(inWant)));
        fillPortCombo(cbDrumAll, { { tr("เสียงในโปรแกรม (SoundFont)"), QString() } }, QString());
        fillTables();
    }

    void fillTables()
    {
        fillInstTable(tInst, melodicTypes());
        fillInstTable(tDrum, drumTypes());
        fillChannelTable();
        refreshStatus();
    }

    QComboBox *modeCombo(bool dual, bool enabled)
    {
        QComboBox *m = new QComboBox;
        m->addItem(tr("ไม่ดัง (ส่งออกอย่างเดียว)"), false);
        m->addItem(tr("ดังด้วย (ทั้งสองที่)"), true);
        m->setCurrentIndex(dual ? 1 : 0);
        m->setEnabled(enabled);
        return m;
    }

    void fillInstTable(QTableWidget *t, const QList<int> &types)
    {
        t->setRowCount(types.size());
        for (int r = 0; r < types.size(); ++r) {
            const InstrumentType it = static_cast<InstrumentType>(types[r]);
            QTableWidgetItem *name = new QTableWidgetItem(typeName(types[r]));
            name->setData(Qt::UserRole, types[r]);
            name->setFlags(Qt::ItemIsEnabled);
            t->setItem(r, 0, name);
            const QString want = syn->extMidiOut(it);
            QComboBox *cb = new QComboBox;
            fillPortCombo(cb, { { tr("เสียงในโปรแกรม"), QString() } }, want);
            t->setCellWidget(r, 1, cb);
            QComboBox *mode = modeCombo(syn->isExtMidiDual(it), !want.isEmpty());
            t->setCellWidget(r, 2, mode);
            QTableWidgetItem *st = new QTableWidgetItem;
            st->setFlags(Qt::ItemIsEnabled);
            t->setItem(r, 3, st);
            connect(cb, QOverload<int>::of(&QComboBox::activated), this, [this, it, cb, mode]() {
                const QString port = cb->currentData().toString();
                syn->sendAllNotesOff();                 // no notes left hanging on the old way
                syn->setExtMidiOut(it, port);
                if (port.isEmpty())
                    mode->setCurrentIndex(0);
                mode->setEnabled(!port.isEmpty());
                refreshStatus();
            });
            connect(mode, QOverload<int>::of(&QComboBox::activated), this, [this, it, mode]() {
                syn->sendAllNotesOff();
                syn->setExtMidiDual(it, mode->currentData().toBool());
                refreshStatus();
            });
        }
    }

    void fillChannelTable()
    {
        tCh->setRowCount(16);
        for (int ch = 0; ch < 16; ++ch) {
            QTableWidgetItem *name = new QTableWidgetItem(ch == 9 ? tr("ช่อง 10 (กลอง)") : tr("ช่อง %1").arg(ch + 1));
            name->setFlags(Qt::ItemIsEnabled);
            tCh->setItem(ch, 0, name);
            const MainWindow::MidiRoute r = mw->midiChannelWant(ch);
            QComboBox *cb = new QComboBox;
            fillPortCombo(cb, { { tr("ตามมิดี้ขาออกหลัก"), QString() },
                                { tr("เสียงในโปรแกรม (SoundFont)"), MainWindow::midiSynthPortKey() } }, r.port);
            tCh->setCellWidget(ch, 1, cb);
            QComboBox *mode = modeCombo(r.dual, goesOut(r.port));
            tCh->setCellWidget(ch, 2, mode);
            QTableWidgetItem *st = new QTableWidgetItem;
            st->setFlags(Qt::ItemIsEnabled);
            tCh->setItem(ch, 3, st);
            auto apply = [this, ch, cb, mode]() {
                MainWindow::MidiRoute nr;
                nr.port = cb->currentData().toString();
                mode->setEnabled(goesOut(nr.port));
                if (!mode->isEnabled())
                    mode->setCurrentIndex(0);
                nr.dual = mode->currentData().toBool();
                mw->setMidiChannelWant(ch, nr);
                mw->applyMidiRouting();
                refreshStatus();
            };
            connect(cb, QOverload<int>::of(&QComboBox::activated), this, apply);
            connect(mode, QOverload<int>::of(&QComboBox::activated), this, apply);
        }
    }

    // a channel goes to a port: its own, or the main out when that is a port
    bool goesOut(const QString &port) const
    {
        if (port == MainWindow::midiSynthPortKey())
            return false;
        return !port.isEmpty() || !mw->midiOutWant().isEmpty();
    }

    // a status cell: what is really used now, with a light while notes go out
    void setStatus(QTableWidgetItem *it, const QString &want, bool connected, const QString &portNow)
    {
        if (!it)
            return;
        if (want.isEmpty() && portNow.isEmpty()) {
            it->setText(tr("เสียงในโปรแกรม"));
            it->setForeground(QColor(0x37, 0x41, 0x51));
        } else if (!connected) {
            it->setText(tr("รอพอร์ต (เสียงในโปรแกรมไปก่อน)"));
            it->setForeground(QColor(0xb4, 0x53, 0x09));
        } else if (busy.contains(portNow)) {
            it->setText(QString::fromUtf8("● ") + tr("กำลังส่ง"));
            it->setForeground(QColor(0x15, 0x80, 0x3d));
        } else {
            it->setText(QString::fromUtf8("○ ") + tr("ต่อแล้ว"));
            it->setForeground(QColor(0x1d, 0x4e, 0xd8));
        }
    }

    void refreshStatus()
    {
        // activity: messages sent per port since the last poll
        busy.clear();
        QSet<QString> names;
        for (const QString &n : outs)
            names.insert(n);
        for (const QString &n : names)
            if (MidiOut::takeCount(n) > 0)
                busy.insert(n);

        for (QTableWidget *t : { tInst, tDrum }) {
            for (int r = 0; r < t->rowCount(); ++r) {
                const InstrumentType it = static_cast<InstrumentType>(t->item(r, 0)->data(Qt::UserRole).toInt());
                const QString want = syn->extMidiOut(it);
                setStatus(t->item(r, 3), want, syn->isExtMidiConnected(it), want);
            }
        }
        for (int ch = 0; ch < 16; ++ch) {
            const QString now = player->channelPortName(ch);
            const MainWindow::MidiRoute r = mw->midiChannelWant(ch);
            QString want = r.port == MainWindow::midiSynthPortKey() ? QString() : r.port;
            if (r.port.isEmpty())
                want = mw->midiOutWant();
            setStatus(tCh->item(ch, 3), want, !now.isEmpty(), now);
        }

        // the in / out loop guard, said plainly
        const QString in = mw->midiInWant();
        bool loop = !in.isEmpty() && in == mw->midiOutWant();
        for (int ch = 0; ch < 16 && !loop; ++ch)
            loop = !in.isEmpty() && mw->midiChannelWant(ch).port == in;
        lbWarn->setText(loop ? tr("พอร์ตที่ใช้เป็นมิดี้ขาเข้า (%1) จะไม่ถูกใช้ส่งออก เพราะเสียงจะวนลูปจน loopMIDI ตัดพอร์ต "
                                  "ให้ใช้ขาเข้ากับขาออกคนละพอร์ต").arg(in)
                             : QString());
        lbWarn->setVisible(loop);
    }

    // ---- sets (.pthkmap)
    QJsonObject currentSet()
    {
        QJsonObject o;
        o["type"] = "PTHK MIDI Mapper";
        o["version"] = 1;
        o["midiOut"] = mw->midiOutWant();
        o["midiIn"] = mw->midiInWant();
        QJsonArray chs;
        for (int ch = 0; ch < 16; ++ch) {
            const MainWindow::MidiRoute r = mw->midiChannelWant(ch);
            QJsonObject c;
            c["port"] = r.port;
            c["dual"] = r.dual;
            chs.append(c);
        }
        o["channels"] = chs;
        QJsonObject inst;
        for (int t : melodicTypes() + drumTypes()) {
            const InstrumentType it = static_cast<InstrumentType>(t);
            const QString p = syn->extMidiOut(it);
            if (p.isEmpty())
                continue;
            QJsonObject i;
            i["port"] = p;
            i["dual"] = syn->isExtMidiDual(it);
            inst[QString::number(t)] = i;
        }
        o["instruments"] = inst;
        return o;
    }

    void applySet(const QJsonObject &o, bool withInOut = true)
    {
        mw->stop();
        if (withInOut) {
            mw->setMidiOutWant(o.value("midiOut").toString());
            mw->setMidiInWant(o.value("midiIn").toString());
        } else {
            mw->setMidiOutWant(QString());
        }
        const QJsonArray chs = o.value("channels").toArray();
        for (int ch = 0; ch < 16; ++ch) {
            MainWindow::MidiRoute r;
            if (ch < chs.size()) {
                r.port = chs[ch].toObject().value("port").toString();
                r.dual = chs[ch].toObject().value("dual").toBool();
            }
            mw->setMidiChannelWant(ch, r);
        }
        const QJsonObject inst = o.value("instruments").toObject();
        for (int t : melodicTypes() + drumTypes()) {
            const InstrumentType it = static_cast<InstrumentType>(t);
            const QJsonObject i = inst.value(QString::number(t)).toObject();
            syn->setExtMidiOut(it, i.value("port").toString());
            syn->setExtMidiDual(it, i.value("dual").toBool());
        }
        mw->applyMidiRouting();
        mw->saveSynthMixerSettings();
        fillAll();
    }

    static QString setsDir()
    {
        const QString d = Config::CONFIG_DIR_PATH + "/MidiMaps";
        QDir().mkpath(d);
        return d;
    }

    void saveSet()
    {
        const QString f = QFileDialog::getSaveFileName(this, tr("บันทึกชุดแยกเสียงมิดี้"), setsDir() + "/" + tr("ชุดใหม่") + ".pthkmap",
                                                       tr("ชุดแยกเสียงมิดี้ (*.pthkmap)"));
        if (f.isEmpty())
            return;
        QFile file(f);
        if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, windowTitle(), tr("บันทึกไฟล์ไม่สำเร็จ\n") + f);
            return;
        }
        file.write(QJsonDocument(currentSet()).toJson());
    }

    void openSet()
    {
        const QString f = QFileDialog::getOpenFileName(this, tr("เปิดชุดแยกเสียงมิดี้"), setsDir(), tr("ชุดแยกเสียงมิดี้ (*.pthkmap)"));
        if (f.isEmpty())
            return;
        QFile file(f);
        if (!file.open(QIODevice::ReadOnly))
            return;
        const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
        if (o.value("type").toString() != "PTHK MIDI Mapper") {
            QMessageBox::warning(this, windowTitle(), tr("ไฟล์นี้ไม่ใช่ชุดแยกเสียงมิดี้ของ PTHK Karaoke"));
            return;
        }
        applySet(o);
    }
};

#endif // MIDIMAPPERDIALOG_H
