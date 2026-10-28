#ifndef LOCKSOUNDFONTDIALOG_H
#define LOCKSOUNDFONTDIALOG_H

// "Lock Soundfont": pick a soundfont (one that is already loaded, or any .sf2 on disk), type the Hardware ID of the
// computer it is for (or the USB ID of a flash drive), press Lock and a protected copy (.kmsf, plays only in KaraMixer
// AND only on the computer with that Hardware ID / while that flash drive is plugged in) is saved in the Kmsf folder.
// The ID has to be typed again for every lock.
// Header-only, no Q_OBJECT (all connections are lambdas).

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QLineEdit>
#include <QClipboard>
#include <QApplication>
#include <QSettings>
#include <QComboBox>

#include "Config.h"
#include "Utils.h"
#include "Midi/MidiSynthesizer.h"
#include "PTHKCore.h"
#include "Widgets/DialogSkin.h"

class LockSoundfontDialog : public QDialog
{
public:
    LockSoundfontDialog(QWidget *parent, MidiSynthesizer *synth) : QDialog(parent), synth(synth)
    {
        setWindowTitle(QString::fromUtf8("Lock Soundfont"));
        setMinimumWidth(520);

        QVBoxLayout *v = new QVBoxLayout(this);
        v->setSpacing(8);

        QLabel *help = new QLabel(QString::fromUtf8(
            "เลือก Soundfont ที่จะล็อก เลือกว่าจะผูกกับ \"เครื่อง\" หรือ \"แฟลชไดร์ USB\" กรอก ID แล้วกด Lock\n"
            "ระบบจะสร้างไฟล์ .kmsf (เล่นได้เฉพาะใน PTHK Karaoke แก้ไขไม่ได้) บันทึกในโฟลเดอร์ Kmsf (อยู่ข้างโฟลเดอร์ Config)\n"
            "แบบเครื่อง: ใช้ได้เฉพาะเครื่องที่มี Hardware ID ตรงกัน  แบบ USB: ใช้ได้เมื่อเสียบแฟลชไดร์ตัวนั้นก่อนเปิดโปรแกรม"), this);
        help->setWordWrap(true);
        v->addWidget(help);

        list = new QListWidget(this);
        list->setMinimumHeight(160);
        v->addWidget(list, 1);

        // what the file is locked to: this computer's Hardware ID or the USB ID of a flash drive
        {
            modeBox = new QComboBox(this);
            modeBox->addItem(QString::fromUtf8("ผูกกับเครื่อง (Hardware ID)"), 0);
            modeBox->addItem(QString::fromUtf8("ผูกกับแฟลชไดร์ USB (USB ID)"), 1);
            usbBox = new QComboBox(this);
            QPushButton *usbRefresh = new QPushButton(QString::fromUtf8("ค้นหาไดร์ใหม่"), this);
            QHBoxLayout *r0 = new QHBoxLayout();
            r0->addWidget(modeBox);
            r0->addWidget(usbBox, 1);
            r0->addWidget(usbRefresh);
            v->addLayout(r0);
            connect(usbRefresh, &QPushButton::clicked, this, [this]() { fillUsb(true); });
            usbRefreshBtn = usbRefresh;

            mine = new QLabel(this);
            QFont mono(QString::fromLatin1("Consolas"));
            mono.setStyleHint(QFont::Monospace);
            mono.setPointSize(12);
            mono.setBold(true);
            mine->setFont(mono);
            mine->setTextInteractionFlags(Qt::TextSelectableByMouse);
            mine->setMinimumHeight(36);
            mine->setStyleSheet(QString::fromLatin1("QLabel { background-color: #161d26; color: #38bdf8; border: 1px solid #0a8aa6; border-radius: 6px; padding: 6px 14px; font-weight: bold; }"));
            QPushButton *copy = new QPushButton(QString::fromUtf8("คัดลอก"), this);
            copy->setObjectName(QString::fromLatin1("lockCopy"));
            QHBoxLayout *r1 = new QHBoxLayout();
            r1->addWidget(mine, 1);
            r1->addWidget(copy);
            v->addLayout(r1);
            connect(copy, &QPushButton::clicked, this, [this]() { QApplication::clipboard()->setText(shownId()); });

            idEdit = new QLineEdit(this);
            idEdit->setPlaceholderText(QString::fromUtf8("กรอก Hardware ID ของเครื่องปลายทาง (8 ตัว เช่น 556BAE3D)"));
            idEdit->setMaxLength(12);
            idEdit->setFont(mono);
            idEdit->setMinimumHeight(34);
            useMine = new QPushButton(QString::fromUtf8("ใช้ ID เครื่องนี้"), this);
            QHBoxLayout *r2 = new QHBoxLayout();
            r2->addWidget(idEdit, 1);
            r2->addWidget(useMine);
            v->addLayout(r2);
            connect(useMine, &QPushButton::clicked, this, [this]() { idEdit->setText(shownId()); });
            connect(modeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { updateMode(); });
            connect(usbBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { updateMode(); });
            fillUsb(true);
        }

        QHBoxLayout *h = new QHBoxLayout();
        QPushButton *browse = new QPushButton(QString::fromUtf8("เลือกไฟล์ .sf2 จากเครื่อง..."), this);
        lockBtn = new QPushButton(QString::fromUtf8("Lock"), this);
        QPushButton *close = new QPushButton(QString::fromUtf8("ปิด"), this);
        for (QPushButton *b : { browse, lockBtn, close }) {   // a little bigger (size only)
            QFont bf = b->font();
            bf.setPointSizeF(bf.pointSizeF() > 0 ? bf.pointSizeF() + 1.0 : 10.0);
            b->setFont(bf);
            b->setMinimumHeight(38);
        }
        h->addWidget(browse);
        h->addStretch(1);
        h->addWidget(lockBtn);
        h->addWidget(close);
        v->addLayout(h);

        status = new QLabel(this);
        status->setWordWrap(true);
        v->addWidget(status);

        refill();

        connect(browse, &QPushButton::clicked, this, [this]() {
            const QString f = QFileDialog::getOpenFileName(this, QString::fromUtf8("Soundfont"), Utils::LAST_OPEN_DIR,
                                                           QString::fromUtf8("SoundFont (*.sf2 *.SF2)"));
            if (f.isEmpty()) return;
            Utils::LAST_OPEN_DIR = QFileInfo(f).dir().absolutePath();
            QListWidgetItem *it = new QListWidgetItem(QFileInfo(f).fileName() + QString::fromUtf8("   (") + f + QString::fromUtf8(")"));
            it->setData(Qt::UserRole, f);
            list->addItem(it);
            list->setCurrentItem(it);
        });
        connect(close, &QPushButton::clicked, this, &QDialog::accept);
        connect(lockBtn, &QPushButton::clicked, this, [this]() { doLock(); });
    }

private:
    MidiSynthesizer *synth;
    QListWidget *list = nullptr;
    QPushButton *lockBtn = nullptr;
    QLabel *status = nullptr;
    QLineEdit *idEdit = nullptr;
    QComboBox *modeBox = nullptr;
    QComboBox *usbBox = nullptr;
    QPushButton *usbRefreshBtn = nullptr;
    QPushButton *useMine = nullptr;
    QLabel *mine = nullptr;

    bool usbMode() const { return modeBox && modeBox->currentData().toInt() == 1; }

    // the ID shown above the entry: this computer's Hardware ID, or the USB ID of the selected flash drive
    QString shownId() const
    {
        return usbMode() ? usbBox->currentData().toString() : PTHKCore::ownerMachineId();
    }

    void fillUsb(bool force)
    {
        usbBox->blockSignals(true);
        usbBox->clear();
        for (const PTHKCore::UsbDrive &d : PTHKCore::scanUsbDrives(force))
            usbBox->addItem(d.label(), d.id);
        usbBox->blockSignals(false);
        updateMode();
    }

    void updateMode()
    {
        const bool usb = usbMode();
        usbBox->setVisible(usb);
        usbRefreshBtn->setVisible(usb);
        useMine->setText(usb ? QString::fromUtf8("ใช้ ID ไดร์ที่เลือก") : QString::fromUtf8("ใช้ ID เครื่องนี้"));
        DialogSkin::fitDialogButton(useMine);
        idEdit->setPlaceholderText(usb ? QString::fromUtf8("กรอก USB ID ของแฟลชไดร์ (8 ตัว เช่น 1A2B3C4D)")
                                       : QString::fromUtf8("กรอก Hardware ID ของเครื่องปลายทาง (8 ตัว เช่น 556BAE3D)"));
        if (usb) {
            const QString id = shownId();
            mine->setText(id.isEmpty() ? QString::fromUtf8("ไม่พบแฟลชไดร์ USB (เสียบแล้วกด \"ค้นหาไดร์ใหม่\")")
                                       : QString::fromUtf8("USB ID ของไดร์ที่เลือก: ") + id);
        } else {
            mine->setText(QString::fromUtf8("Hardware ID ของเครื่องนี้: ") + PTHKCore::ownerMachineId());
        }
    }

    void refill()
    {
        list->clear();
        for (const QString &f : synth->soundfontFiles()) {
            if (PTHKCore::isProtectedSoundfont(f))
                continue;   // already locked
            if (!f.endsWith(QLatin1String(".sf2"), Qt::CaseInsensitive))
                continue;   // only .sf2 can be locked
            QListWidgetItem *it = new QListWidgetItem(QFileInfo(f).fileName() + QString::fromUtf8("   (") + f + QString::fromUtf8(")"));
            it->setData(Qt::UserRole, f);
            list->addItem(it);
        }
    }

    void doLock()
    {
        QListWidgetItem *it = list->currentItem();
        if (!it) {
            status->setText(QString::fromUtf8("กรุณาเลือก Soundfont ก่อน"));
            return;
        }
        const QString src = it->data(Qt::UserRole).toString();

        // the Hardware ID of the computer the soundfont is for: typed in every time
        const QString id = PTHKCore::normalizeId(idEdit->text());
        if (id.isEmpty()) {
            status->setText(QString::fromUtf8("กรุณากรอก ID ให้ถูกต้อง (8 ตัว 0-9, A-F)"));
            idEdit->setFocus();
            return;
        }

        // <data folder>/Kmsf, next to the Config and Data folders; the ID is part of the name
        const QString outDir = QFileInfo(Config::CONFIG_DIR_PATH).dir().absoluteFilePath(QString::fromUtf8("Kmsf"));
        QDir().mkpath(outDir);
        const bool usb = usbMode();
        const QString dst = QDir(outDir).absoluteFilePath(QFileInfo(src).completeBaseName() + QString::fromUtf8(usb ? "-usb-" : "-") + id
                                                          + QString::fromUtf8(".kmsf"));
        if (QFileInfo::exists(dst)
            && QMessageBox::question(this, QString::fromUtf8("Lock Soundfont"),
                   QFileInfo(dst).fileName() + QString::fromUtf8(" มีอยู่แล้ว ต้องการเขียนทับหรือไม่?")) != QMessageBox::Yes)
            return;

        lockBtn->setEnabled(false);
        status->setText(QString::fromUtf8("กำลังล็อก ..."));
        QApplication::setOverrideCursor(Qt::WaitCursor);
        qApp->processEvents();
        QString err;
        const bool ok = PTHKCore::lockSoundfont(src, dst, id, usb, &err);
        QApplication::restoreOverrideCursor();
        lockBtn->setEnabled(true);

        if (!ok) {
            status->setText(QString::fromUtf8("ล็อกไม่สำเร็จ: ") + err);
            return;
        }

        idEdit->clear();     // the ID has to be typed again for the next lock

        const bool playsHere = usb ? PTHKCore::connectedUsbIds().contains(id) : (id == PTHKCore::ownerMachineId());
        if (!playsHere) {
            // made for another computer / another flash drive: it cannot play here, so it is not added to the list
            status->setText(QString::fromUtf8(usb ? "ล็อกแล้วสำหรับแฟลชไดร์ USB ID " : "ล็อกแล้วสำหรับเครื่อง ID ") + id
                            + QString::fromUtf8(" (ใช้ได้เฉพาะ") + QString::fromUtf8(usb ? "เมื่อเสียบไดร์นั้น" : "เครื่องนั้น")
                            + QString::fromUtf8("): ") + QDir::toNativeSeparators(dst));
            return;
        }

        // add the locked copy to the soundfont list (once)
        bool known = false;
        for (const QString &f : synth->soundfontFiles())
            if (QFileInfo(f) == QFileInfo(dst)) { known = true; break; }
        if (!known && synth->addSoundfont(dst)) {
            QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
            st.setValue("SynthSoundfonts", QVariant::fromValue(synth->soundfontFiles()));
        }
        status->setText(QString::fromUtf8(usb ? "ล็อกแล้วสำหรับแฟลชไดร์ USB ID " : "ล็อกแล้วสำหรับเครื่อง ID ") + id
                        + QString::fromUtf8(": ") + QDir::toNativeSeparators(dst));
    }
};

#endif // LOCKSOUNDFONTDIALOG_H
