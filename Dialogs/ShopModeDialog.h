// ShopModeDialog.h: Screen lock for karaoke shops ("ล็อกหน้าจอสำหรับร้าน") and
// automatic background music when the queue is empty ("เปิดเพลงคลอเมื่อคิวว่าง").
//
// Settings keys:
//   ShopLock/On        bool     false   whether screen lock is currently active
//   ShopLock/Strict    bool     false   also lock stop/skip/queue-edit/volume
//   ShopLock/Pin       QString  ""      optional unlock PIN (empty = no PIN)
//   IdleMusic/On       bool     false   automatically play background music when queue is empty
//   IdleMusic/Source   int      0       0 = Top/History, 1 = Favorites, 2 = Random library, 3 = Folder
//   IdleMusic/Folder   QString  ""      custom folder path for source 3
//   IdleMusic/DelaySec int      10      seconds to wait after queue becomes empty (3..120)
//   IdleMusic/VolumePct int     45      idle music volume relative to master volume (10..100%)
//   IdleMusic/KeepVocal bool    true    keep vocals on video/MP3 idle songs even when vocal cut is on

#ifndef SHOPMODEDIALOG_H
#define SHOPMODEDIALOG_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <functional>

class ShopPinDialog : public QDialog
{
public:
    explicit ShopPinDialog(QWidget *parent, const QString &expectedPin, const QString &actionName = QString())
        : QDialog(parent), pin(expectedPin)
    {
        setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
        setWindowTitle(QObject::tr("ปลดล็อกหน้าจอสำหรับร้าน"));
        setMinimumWidth(360);

        QVBoxLayout *root = new QVBoxLayout(this);
        root->setContentsMargins(18, 16, 18, 16);
        root->setSpacing(10);

        const QString msg = actionName.isEmpty()
            ? QObject::tr("กรุณาใส่รหัสผ่านร้านเพื่อปลดล็อกหน้าจอ")
            : QObject::tr("หน้าจอล็อกอยู่ — กรุณาใส่รหัสผ่านร้านเพื่อเปิด:\n\"%1\"").arg(actionName);
        QLabel *prompt = new QLabel(msg, this);
        prompt->setWordWrap(true);
        prompt->setStyleSheet("font-weight: 600; font-size: 14px;");
        root->addWidget(prompt);

        edit = new QLineEdit(this);
        edit->setEchoMode(QLineEdit::Password);
        edit->setPlaceholderText(QObject::tr("ใส่รหัสปลดล็อก..."));
        edit->setAlignment(Qt::AlignCenter);
        edit->setStyleSheet("font-size: 18px; padding: 6px 10px; letter-spacing: 4px;");
        root->addWidget(edit);

        // On-screen keypad for touch-screen / mouse-only karaoke kiosks
        QGridLayout *pad = new QGridLayout();
        pad->setSpacing(6);
        for (int i = 1; i <= 9; ++i) {
            QPushButton *b = new QPushButton(QString::number(i), this);
            b->setMinimumHeight(36);
            b->setFocusPolicy(Qt::NoFocus);
            connect(b, &QPushButton::clicked, this, [this, i]() {
                edit->insert(QString::number(i));
            });
            pad->addWidget(b, (i - 1) / 3, (i - 1) % 3);
        }
        QPushButton *bClear = new QPushButton(QObject::tr("ล้าง"), this);
        bClear->setMinimumHeight(36);
        bClear->setFocusPolicy(Qt::NoFocus);
        connect(bClear, &QPushButton::clicked, edit, &QLineEdit::clear);
        pad->addWidget(bClear, 3, 0);

        QPushButton *b0 = new QPushButton("0", this);
        b0->setMinimumHeight(36);
        b0->setFocusPolicy(Qt::NoFocus);
        connect(b0, &QPushButton::clicked, this, [this]() { edit->insert("0"); });
        pad->addWidget(b0, 3, 1);

        QPushButton *bBack = new QPushButton(QObject::tr("⌫ ลบ"), this);
        bBack->setMinimumHeight(36);
        bBack->setFocusPolicy(Qt::NoFocus);
        connect(bBack, &QPushButton::clicked, edit, &QLineEdit::backspace);
        pad->addWidget(bBack, 3, 2);
        root->addLayout(pad);

        errLabel = new QLabel(this);
        errLabel->setStyleSheet("color: #f87171; font-weight: 600;");
        errLabel->hide();
        root->addWidget(errLabel);

        QHBoxLayout *btns = new QHBoxLayout();
        btns->addStretch(1);
        QPushButton *btnOk = new QPushButton(QObject::tr("ปลดล็อก"), this);
        btnOk->setDefault(true);
        btnOk->setMinimumWidth(100);
        QPushButton *btnCancel = new QPushButton(QObject::tr("ยกเลิก"), this);
        btnCancel->setMinimumWidth(90);
        btns->addWidget(btnOk);
        btns->addWidget(btnCancel);
        root->addLayout(btns);

        auto tryAccept = [this]() {
            if (edit->text() == pin) {
                accept();
            } else {
                errLabel->setText(QObject::tr("รหัสไม่ถูกต้อง กรุณาลองใหม่"));
                errLabel->show();
                edit->clear();
                edit->setFocus();
            }
        };
        connect(btnOk, &QPushButton::clicked, this, tryAccept);
        connect(edit, &QLineEdit::returnPressed, this, tryAccept);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

        edit->setFocus();
    }

    static bool verify(QWidget *parent, QSettings *s, const QString &actionName = QString())
    {
        const QString pin = s->value("ShopLock/Pin", "").toString().trimmed();
        if (pin.isEmpty()) {
            if (actionName.isEmpty())
                return true;
            QMessageBox box(QMessageBox::Question,
                            QObject::tr("หน้าจอล็อกอยู่"),
                            QObject::tr("ฟีเจอร์ \"%1\" ถูกล็อกไว้สำหรับร้าน\nต้องการปลดล็อกหน้าจอเพื่อเปิดใช้งานหรือไม่?")
                                .arg(actionName),
                            QMessageBox::NoButton, parent);
            QPushButton *bUnlock = box.addButton(QObject::tr("ปลดล็อกหน้าจอ"), QMessageBox::AcceptRole);
            box.addButton(QObject::tr("ยกเลิก"), QMessageBox::RejectRole);
            box.exec();
            return box.clickedButton() == bUnlock;
        }
        ShopPinDialog dlg(parent, pin, actionName);
        return dlg.exec() == QDialog::Accepted;
    }

private:
    QString pin;
    QLineEdit *edit = nullptr;
    QLabel *errLabel = nullptr;
};

class ShopModeDialog : public QDialog
{
public:
    std::function<void()> onChanged;
    std::function<void()> onPlayIdleNow;

    ShopModeDialog(QWidget *parent, QSettings *s)
        : QDialog(parent), settings(s)
    {
        setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
        setWindowTitle(QObject::tr("ล็อกหน้าจอสำหรับร้าน และเพลงคลอเมื่อคิวว่าง"));
        setMinimumWidth(580);

        QVBoxLayout *root = new QVBoxLayout(this);
        root->setContentsMargins(16, 14, 16, 14);
        root->setSpacing(12);

        // ---- Group 1: Screen Lock for Shops ------------------------------------------------
        QGroupBox *gLock = new QGroupBox(QObject::tr("ล็อกหน้าจอสำหรับร้าน (ป้องกันลูกค้าเผลอกดตั้งค่า / ลบเพลง / ปิดโปรแกรม)"), this);
        QVBoxLayout *vLock = new QVBoxLayout(gLock);
        vLock->setSpacing(8);

        chkLock = new QCheckBox(QObject::tr("เปิดล็อกหน้าจอสำหรับร้านตอนนี้ (สลับเร็วด้วยปุ่มลัด Ctrl+L หรือปุ่ม \"ล็อก\" บนแถบล่าง)"), gLock);
        chkLock->setChecked(s->value("ShopLock/On", false).toBool());
        vLock->addWidget(chkLock);

        QLabel *lockInfo = new QLabel(
            QObject::tr("เมื่อล็อกอยู่: ลูกค้ายังค้นหาเพลง เพิ่มเข้าคิว เปิดคิว เปลี่ยนคีย์/จังหวะ ตัดเสียงร้อง และกดแป้นเสียงเชียร์ได้ตามปกติ\n"
                        "แต่จะกั้นการเปิดตั้งค่า มิกเซอร์ เพิ่มเพลง ลบเพลงออกจากคลัง เมนูคลิกขวาทั้งหมด และการปิดโปรแกรม"), gLock);
        lockInfo->setWordWrap(true);
        lockInfo->setStyleSheet("color: #9ca3af; font-size: 12px;");
        vLock->addWidget(lockInfo);

        chkStrict = new QCheckBox(
            QObject::tr("ล็อกเข้มงวด: ห้ามกดหยุด (F7) / ข้ามเพลง (F8-F9, End) / ลบหรือเลื่อนคิว / ปรับความดังหลัก"), gLock);
        chkStrict->setChecked(s->value("ShopLock/Strict", false).toBool());
        vLock->addWidget(chkStrict);

        QHBoxLayout *pinRow = new QHBoxLayout();
        pinRow->addWidget(new QLabel(QObject::tr("รหัสปลดล็อกร้าน:"), gLock));
        edPin = new QLineEdit(s->value("ShopLock/Pin", "").toString(), gLock);
        edPin->setEchoMode(QLineEdit::Password);
        edPin->setMaxLength(16);
        edPin->setPlaceholderText(QObject::tr("เว้นว่าง = ปลดล็อกได้โดยไม่ใช้รหัส"));
        edPin->setMaximumWidth(220);
        pinRow->addWidget(edPin);
        QCheckBox *chkShowPin = new QCheckBox(QObject::tr("แสดงรหัส"), gLock);
        connect(chkShowPin, &QCheckBox::toggled, this, [this](bool show) {
            edPin->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
        });
        pinRow->addWidget(chkShowPin);
        pinRow->addStretch(1);
        vLock->addLayout(pinRow);

        root->addWidget(gLock);

        // ---- Group 2: Idle / Background Music when Queue is Empty -------------------------
        QGroupBox *gIdle = new QGroupBox(QObject::tr("เปิดเพลงคลออัตโนมัติเมื่อคิวว่าง (ไม่ให้ร้านเงียบเมื่อไม่มีคนร้อง)"), this);
        QFormLayout *fIdle = new QFormLayout(gIdle);
        fIdle->setVerticalSpacing(8);

        chkIdle = new QCheckBox(QObject::tr("เปิดเพลงคลออัตโนมัติเมื่อไม่มีเพลงเล่นและคิวว่าง"), gIdle);
        chkIdle->setChecked(s->value("IdleMusic/On", false).toBool());
        fIdle->addRow(chkIdle);

        cmbSource = new QComboBox(gIdle);
        cmbSource->addItem(QObject::tr("เพลงฮิต / เล่นบ่อย (ถ้ายังไม่มีประวัติจะสุ่มจากคลังเพลง)"));
        cmbSource->addItem(QObject::tr("เพลงโปรด (ถ้ายังไม่มีเพลงโปรดจะสุ่มจากคลังเพลง)"));
        cmbSource->addItem(QObject::tr("สุ่มจากคลังเพลงทั้งหมด"));
        cmbSource->addItem(QObject::tr("โฟลเดอร์เพลงคลอที่เลือกเอง (MP3 / วิดีโอ)"));
        cmbSource->addItem(QObject::tr("ฉลาด: เลือกตามที่ลูกค้าร้อง + เพลงฮิตจาก YouTube (แนะนำ)"));
        cmbSource->setCurrentIndex(qBound(0, s->value("IdleMusic/Source", 4).toInt(), 4));
        fIdle->addRow(QObject::tr("แหล่งเพลงคลอ:"), cmbSource);

        // smart: the style (IdleSmart.h); "auto" follows tonight's customers
        cmbStyle = new QComboBox(gIdle);
        {
            const QStringList names = IdleSmart::styleNames(), keys = IdleSmart::styleKeys();
            for (int i = 0; i < names.size(); ++i)
                cmbStyle->addItem(names[i], keys[i]);
            for (const QString &g : YtHistory::genres())          // the genre buttons of the YouTube page
                if (!g.contains(QString::fromUtf8("ฮิตตอนนี้")))
                    cmbStyle->addItem(QObject::tr("แนว: ") + g, g);
            const int at = cmbStyle->findData(s->value("IdleMusic/Style", "auto").toString());
            cmbStyle->setCurrentIndex(at < 0 ? 0 : at);
        }
        fIdle->addRow(QObject::tr("แนวเพลงคลอ (แบบฉลาด):"), cmbStyle);
        QLabel *smartNote = new QLabel(
            QObject::tr("อัตโนมัติ: เมื่อลูกค้าร้องจบ เพลงคลอจะดูชนิดเพลง ความเร็ว (BPM) แนวเพลง และยุคของเพลงล่าสุด "
                        "แล้วเลือกเพลงที่เข้ากัน (เพลงช้าต่อเพลงช้า เพลงเร็วต่อเพลงเร็ว แนวและยุคเดียวกัน) ไม่เอาคลิปแสดงสด / Cover / รวมเพลง\n"
                        "เพลงที่มีในคลังจะเปิดจากไฟล์ในเครื่อง ที่ไม่มีจะเปิดจาก YouTube (ต้องต่ออินเทอร์เน็ต ถ้าเน็ตหลุดจะสุ่มจากเพลงฮิตของร้านแทน)"), gIdle);
        smartNote->setWordWrap(true);
        smartNote->setStyleSheet("color: #9ca3af; font-size: 12px;");
        fIdle->addRow(smartNote);

        QWidget *folderWrap = new QWidget(gIdle);
        QHBoxLayout *folderRow = new QHBoxLayout(folderWrap);
        folderRow->setContentsMargins(0, 0, 0, 0);
        folderRow->setSpacing(6);
        edFolder = new QLineEdit(s->value("IdleMusic/Folder", "").toString(), folderWrap);
        edFolder->setPlaceholderText(QObject::tr("เลือกโฟลเดอร์ที่มีไฟล์ .mp3 หรือ .mp4 สำหรับเปิดคลอ..."));
        QPushButton *btnBrowse = new QPushButton(QObject::tr("เลือกโฟลเดอร์..."), folderWrap);
        connect(btnBrowse, &QPushButton::clicked, this, [this]() {
            const QString d = QFileDialog::getExistingDirectory(
                this, QObject::tr("เลือกโฟลเดอร์เพลงคลอ"),
                edFolder->text().isEmpty() ? QDir::homePath() : edFolder->text());
            if (!d.isEmpty()) {
                edFolder->setText(d);
                save();
            }
        });
        folderRow->addWidget(edFolder, 1);
        folderRow->addWidget(btnBrowse);
        fIdle->addRow(QObject::tr("โฟลเดอร์เพลงคลอ:"), folderWrap);

        auto updateFolderEnabled = [this, folderWrap, smartNote]() {
            folderWrap->setEnabled(cmbSource->currentIndex() == 3);
            cmbStyle->setEnabled(cmbSource->currentIndex() == 4);
            smartNote->setEnabled(cmbSource->currentIndex() == 4);
        };
        updateFolderEnabled();

        QHBoxLayout *delayRow = new QHBoxLayout();
        spDelay = new QSpinBox(gIdle);
        spDelay->setRange(3, 120);
        spDelay->setSingleStep(5);
        spDelay->setSuffix(QObject::tr(" วินาที"));
        spDelay->setValue(qBound(3, s->value("IdleMusic/DelaySec", 10).toInt(), 120));
        spDelay->setMinimumWidth(110);
        delayRow->addWidget(spDelay);
        delayRow->addStretch(1);
        fIdle->addRow(QObject::tr("รอก่อนเริ่มเล่นเพลงคลอ:"), delayRow);

        QHBoxLayout *volRow = new QHBoxLayout();
        slVol = new QSlider(Qt::Horizontal, gIdle);
        slVol->setRange(10, 100);
        slVol->setValue(qBound(10, s->value("IdleMusic/VolumePct", 45).toInt(), 100));
        QLabel *lbVol = new QLabel(QString::number(slVol->value()) + "%", gIdle);
        lbVol->setMinimumWidth(44);
        volRow->addWidget(slVol, 1);
        volRow->addWidget(lbVol);
        fIdle->addRow(QObject::tr("ความดังเพลงคลอ (เทียบกับความดังหลัก):"), volRow);

        chkVocal = new QCheckBox(
            QObject::tr("เปิดเสียงร้องของเพลงคลอไว้ (สำหรับเพลงวิดีโอ / MP3 แม้เปิดปุ่มตัดเสียงร้องอยู่)"), gIdle);
        chkVocal->setChecked(s->value("IdleMusic/KeepVocal", true).toBool());
        fIdle->addRow(chkVocal);

        QLabel *idleNote = new QLabel(
            QObject::tr("เมื่อลูกค้าพิมพ์ค้นหาแล้วกดเพิ่มเพลงเข้าคิว เพลงคลอจะค่อย ๆ เบาลงแล้วตัดเข้าเพลงของลูกค้าอัตโนมัติทันที\n"
                        "(เพลงคลอจะไม่ถูกบันทึกปนในประวัติเพลงฮิต และไม่คิดคะแนนร้องเพลง)"), gIdle);
        idleNote->setWordWrap(true);
        idleNote->setStyleSheet("color: #9ca3af; font-size: 12px;");
        fIdle->addRow(idleNote);

        QHBoxLayout *testRow = new QHBoxLayout();
        QPushButton *btnTestIdle = new QPushButton(QObject::tr("▶ ลองเล่นเพลงคลอตอนนี้"), gIdle);
        connect(btnTestIdle, &QPushButton::clicked, this, [this]() {
            chkIdle->setChecked(true);
            save();
            if (onPlayIdleNow)
                onPlayIdleNow();
        });
        testRow->addWidget(btnTestIdle);
        testRow->addStretch(1);
        fIdle->addRow(QString(), testRow);

        root->addWidget(gIdle);

        QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
        bb->button(QDialogButtonBox::Close)->setText(QObject::tr("ปิด"));
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
        root->addWidget(bb);

        connect(chkLock, &QCheckBox::toggled, this, [this]() { save(); });
        connect(chkStrict, &QCheckBox::toggled, this, [this]() { save(); });
        connect(edPin, &QLineEdit::textChanged, this, [this]() { save(); });
        connect(chkIdle, &QCheckBox::toggled, this, [this]() { save(); });
        connect(cmbSource, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, updateFolderEnabled]() {
            updateFolderEnabled();
            save();
        });
        connect(cmbStyle, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { save(); });
        connect(edFolder, &QLineEdit::textChanged, this, [this]() { save(); });
        connect(spDelay, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { save(); });
        connect(slVol, &QSlider::valueChanged, this, [this, lbVol](int v) {
            lbVol->setText(QString::number(v) + "%");
            save();
        });
        connect(chkVocal, &QCheckBox::toggled, this, [this]() { save(); });
    }

private:
    QSettings *settings = nullptr;
    QCheckBox *chkLock = nullptr;
    QCheckBox *chkStrict = nullptr;
    QLineEdit *edPin = nullptr;
    QCheckBox *chkIdle = nullptr;
    QComboBox *cmbSource = nullptr;
    QComboBox *cmbStyle = nullptr;
    QLineEdit *edFolder = nullptr;
    QSpinBox *spDelay = nullptr;
    QSlider *slVol = nullptr;
    QCheckBox *chkVocal = nullptr;

    void save()
    {
        settings->setValue("ShopLock/On", chkLock->isChecked());
        settings->setValue("ShopLock/Strict", chkStrict->isChecked());
        settings->setValue("ShopLock/Pin", edPin->text().trimmed());
        settings->setValue("IdleMusic/On", chkIdle->isChecked());
        settings->setValue("IdleMusic/Source", cmbSource->currentIndex());
        settings->setValue("IdleMusic/Style", cmbStyle->currentData().toString());
        settings->setValue("IdleMusic/Folder", edFolder->text().trimmed());
        settings->setValue("IdleMusic/DelaySec", spDelay->value());
        settings->setValue("IdleMusic/VolumePct", slVol->value());
        settings->setValue("IdleMusic/KeepVocal", chkVocal->isChecked());
        if (onChanged)
            onChanged();
    }
};

#endif // SHOPMODEDIALOG_H
