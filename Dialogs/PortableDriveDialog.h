#ifndef PORTABLEDRIVEDIALOG_H
#define PORTABLEDRIVEDIALOG_H

// "สร้างไดรฟ์พกพา (USB / External HD)": copies this whole karaoke onto a drive, ready to sing on any
// computer - the program, every setting (mixer, sound fonts, effects, idle music, sound pads, event
// sounds), the song database and the lyrics index, and the song libraries - into
// "<drive>\PTHK Karaoke". The copy runs in portable mode there (it has its own Config and Data
// folders, see Config::initConfigDataPath) and its settings point into the drive; a new drive letter
// on another computer is handled by Config's rebasePortablePaths (portable-location.txt).
// Run again later: only new or changed files are copied. The songs are the shop's own copy for its
// own drive; nothing is sent anywhere else. Header-only, no Q_OBJECT.

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QElapsedTimer>
#include <QStyle>
#include <QMessageBox>
#include <QMutex>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <atomic>
#include <functional>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "Config.h"

class PortableDriveDialog : public QDialog
{
public:
    explicit PortableDriveDialog(QWidget *parent) : QDialog(parent)
    {
        setObjectName("PortableDrive");
        setWindowTitle(QString::fromUtf8("สร้างไดรฟ์พกพา (USB / External HD)"));
        setMinimumWidth(820);
        setStyleSheet(QStringLiteral(
            "#PortableDrive { background: #15171d; }"
            "#PortableDrive QLabel { color: #eceef3; background: transparent; }"
            "#PortableDrive QLabel#muted { color: #8b92a3; }"
            "#PortableDrive QLabel#warn { color: #ffb454; }"
            "#PortableDrive QLabel#bad { color: #ff7b7b; }"
            "#PortableDrive QLabel#good { color: #4ade80; }"
            "#PortableDrive QFrame#box { background: #1c1f27; border-radius: 12px; }"
            "#PortableDrive QCheckBox { color: #eceef3; spacing: 10px; }"
            "#PortableDrive QCheckBox:disabled { color: #b4b9c5; }"
            "#PortableDrive QComboBox { background: #23272f; color: #eceef3; border: 1px solid #353b48; border-radius: 8px;"
            "  padding: 6px 10px; }"
            "#PortableDrive QComboBox QAbstractItemView { background: #23272f; color: #eceef3; selection-background-color: #0a84ff; }"
            "#PortableDrive QProgressBar { background: #23272f; border: none; border-radius: 6px; height: 12px; color: transparent; }"
            "#PortableDrive QProgressBar::chunk { background: #0a84ff; border-radius: 6px; }"
            "#PortableDrive QPushButton#primary { background: #0a84ff; color: white; border: none; border-radius: 9px;"
            "  padding: 9px 22px; font-weight: bold; }"
            "#PortableDrive QPushButton#primary:hover { background: #3d9bff; }"
            "#PortableDrive QPushButton#primary:disabled { background: #2a2f3a; color: #6b7280; }"
            "#PortableDrive QPushButton#ghost { background: transparent; color: #d4d9e3; border: 1px solid #353b48;"
            "  border-radius: 9px; padding: 8px 16px; }"
            "#PortableDrive QPushButton#ghost:hover { background: #232731; }"));

        QVBoxLayout *main = new QVBoxLayout(this);
        main->setContentsMargins(22, 16, 22, 18);
        main->setSpacing(12);

        QLabel *t = new QLabel(QString::fromUtf8("สร้างไดรฟ์พกพา"), this);
        QFont tf = t->font();
        tf.setPixelSize(24);
        tf.setBold(true);
        t->setFont(tf);
        QLabel *st = new QLabel(QString::fromUtf8("คัดลอกโปรแกรม ค่าตั้งทั้งหมด และคลังเพลงลง USB / External HD "
                                                  "เสียบเครื่องไหนก็เปิดร้องได้ทันที ไม่ต้องตั้งค่าใหม่"), this);
        st->setObjectName("muted");
        st->setWordWrap(true);
        main->addWidget(t);
        main->addWidget(st);

        // ---- 1. the drive
        QFrame *b1 = box(main, QString::fromUtf8("1.  เลือกไดรฟ์ปลายทาง"));
        QVBoxLayout *l1 = static_cast<QVBoxLayout *>(b1->layout());
        QHBoxLayout *dr = new QHBoxLayout();
        drives = new QComboBox(b1);
        drives->setMinimumWidth(460);
        QPushButton *other = new QPushButton(QString::fromUtf8("เลือกโฟลเดอร์อื่น..."), b1);
        other->setObjectName("ghost");
        dr->addWidget(drives, 1);
        dr->addWidget(other);
        l1->addLayout(dr);
        targetLabel = new QLabel(b1);
        targetLabel->setObjectName("muted");
        targetLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        driveNote = new QLabel(b1);
        driveNote->setWordWrap(true);
        l1->addWidget(targetLabel);
        l1->addWidget(driveNote);

        // ---- 2. what goes onto it
        QFrame *b2 = box(main, QString::fromUtf8("2.  สิ่งที่จะคัดลอก"));
        QGridLayout *g = new QGridLayout();
        g->setHorizontalSpacing(16);
        g->setVerticalSpacing(6);
        static_cast<QVBoxLayout *>(b2->layout())->addLayout(g);
        collectParts();
        for (int i = 0; i < parts.size(); ++i) {
            Part &p = parts[i];
            p.check = new QCheckBox(p.title, b2);
            p.check->setChecked(p.on);
            if (p.required)
                p.check->setToolTip(QString::fromUtf8("ต้องคัดลอกเสมอ: ไม่มีส่วนนี้ไดรฟ์จะใช้ไม่ได้"));
            p.check->setEnabled(p.available);
            if (!p.available)
                p.check->setText(p.title + QString::fromUtf8("  (ไม่มีในเครื่องนี้)"));
            p.sizeLabel = new QLabel(QString::fromUtf8("กำลังนับ..."), b2);
            p.sizeLabel->setObjectName("muted");
            p.sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            g->addWidget(p.check, i, 0);
            g->addWidget(p.sizeLabel, i, 1);
            connect(p.check, &QCheckBox::toggled, this, [this, i](bool on) {
                if (parts[i].required && !on) {             // always copied: stays ticked
                    QSignalBlocker b(parts[i].check);
                    parts[i].check->setChecked(true);
                    return;
                }
                parts[i].on = on;
                updateSummary();
            });
        }
        g->setColumnStretch(0, 1);
        summary = new QLabel(b2);
        QFont sf = summary->font();
        sf.setBold(true);
        summary->setFont(sf);
        static_cast<QVBoxLayout *>(b2->layout())->addWidget(summary);

        // ---- 3. progress
        QFrame *b3 = box(main, QString::fromUtf8("3.  คัดลอก"));
        QVBoxLayout *l3 = static_cast<QVBoxLayout *>(b3->layout());
        bar = new QProgressBar(b3);
        bar->setRange(0, 1000);
        bar->setValue(0);
        bar->setFixedHeight(12);
        status = new QLabel(QString::fromUtf8("กด \"เริ่มคัดลอก\" เมื่อพร้อม  ·  ครั้งต่อไปคัดลอกเฉพาะไฟล์ใหม่หรือที่เปลี่ยน"), b3);
        status->setWordWrap(true);
        currentFile = new QLabel(b3);
        currentFile->setObjectName("muted");
        l3->addWidget(bar);
        l3->addWidget(status);
        l3->addWidget(currentFile);

        QHBoxLayout *bt = new QHBoxLayout();
        openBtn = new QPushButton(QString::fromUtf8("เปิดโฟลเดอร์บนไดรฟ์"), this);
        openBtn->setObjectName("ghost");
        openBtn->hide();
        startBtn = new QPushButton(QString::fromUtf8("เริ่มคัดลอก"), this);
        startBtn->setObjectName("primary");
        closeBtn = new QPushButton(QString::fromUtf8("ปิด"), this);
        closeBtn->setObjectName("ghost");
        bt->addWidget(openBtn);
        bt->addStretch(1);
        bt->addWidget(startBtn);
        bt->addWidget(closeBtn);
        main->addLayout(bt);

        fillDrives();
        connect(drives, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            target = drives->currentData().toString();
            updateTarget();
        });
        connect(other, &QPushButton::clicked, this, [this]() {
            const QString d = QFileDialog::getExistingDirectory(this, QString::fromUtf8("เลือกโฟลเดอร์ที่จะวาง \"PTHK Karaoke\""));
            if (d.isEmpty())
                return;
            drives->blockSignals(true);
            drives->addItem(QDir::toNativeSeparators(d), QDir::fromNativeSeparators(d));
            drives->setCurrentIndex(drives->count() - 1);
            drives->blockSignals(false);
            target = QDir::fromNativeSeparators(d);
            updateTarget();
        });
        connect(startBtn, &QPushButton::clicked, this, [this]() { start(); });
        connect(closeBtn, &QPushButton::clicked, this, [this]() { if (confirmStop()) reject(); });
        connect(openBtn, &QPushButton::clicked, this, [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(appRoot())); });

        timer = new QTimer(this);
        timer->setInterval(200);
        connect(timer, &QTimer::timeout, this, [this]() { tick(); });
        timer->start();

        // count the files of every part in the background (the song folders hold many)
        scanning = true;
        scanWorker = std::thread([this]() { scanAll(); });
    }
    ~PortableDriveDialog() override
    {
        cancel = true;
        if (scanWorker.joinable())
            scanWorker.join();
        if (copyWorker.joinable())
            copyWorker.join();
    }

protected:
    void reject() override
    {
        if (!confirmStop())
            return;
        QDialog::reject();
    }

private:
    struct Job { QString src, dst; qint64 size = 0; };
    struct Part {
        QString key, title;
        QStringList srcDirs;            // copied into dstRel (each dir's contents)
        QStringList srcFiles;           // single files: into dstRel, by name
        QString dstRel;                 // inside "<drive>/PTHK Karaoke"
        QStringList dirDst;             // optional: where each srcDirs entry goes (else dstRel)
        bool on = true, required = false, available = true;
        QVector<Job> jobs;              // filled by the scan
        qint64 bytes = 0;
        QCheckBox *check = nullptr;
        QLabel *sizeLabel = nullptr;
    };

    // ---------- what there is to copy ----------
    static QString appDir() { return QDir::fromNativeSeparators(QCoreApplication::applicationDirPath()); }
    QString appRoot() const { return target.isEmpty() ? QString() : QDir::cleanPath(target + "/PTHK Karaoke"); }

    void collectParts()
    {
        QSettings s(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        auto dirIf = [](const QString &d) { return (!d.isEmpty() && QFileInfo(d).isDir()) ? QStringList{ QDir::cleanPath(d) } : QStringList(); };
        Part prog;
        prog.key = "program";
        prog.title = QString::fromUtf8("ตัวโปรแกรม PTHK Karaoke (รวม AI แยกเสียง, VST, เสียงในตัว)");
        prog.srcDirs = QStringList() << appDir();
        prog.required = true;
        parts << prog;

        Part set;
        set.key = "settings";
        set.title = QString::fromUtf8("ค่าตั้งทั้งหมด + ฐานข้อมูลเพลง + ดัชนีเนื้อร้อง + เสียงพิธีการ / เสียงของร้าน");
        set.required = true;
        parts << set;

        const struct { const char *key, *title, *setting, *dst; } songs[] = {
            { "NCN",   "เพลง NCN",                "NCNPath",   "Songs/NCN" },
            { "EMK",   "เพลง EMK / HNK",          "HNKPath",   "Songs/HNK" },
            { "SIB",   "เพลง SIB",                "SIBPath",   "Songs/SIB" },
            { "KAR",   "เพลง KAR / MIDI",         "KARPath",   "Songs/KAR" },
            { "VIDEO", "วิดีโอคาราโอเกะ (รวมคลิป YouTube ที่โหลดไว้)", "VideoPath", "Songs/VIDEO" },
            { "MP3",   "เพลงเสียง MP3 / WAV ...",  "MP3Path",   "Songs/MP3" },
        };
        for (const auto &sg : songs) {
            Part p;
            p.key = sg.key;
            p.title = QString::fromUtf8(sg.title);
            p.srcDirs = dirIf(Config::relocatePath(s.value(sg.setting).toString()));
            p.dstRel = sg.dst;
            p.available = !p.srcDirs.isEmpty();
            p.on = p.available;
            parts << p;
        }

        Part sf;
        sf.key = "soundfonts";
        sf.title = QString::fromUtf8("ซาวด์ฟอนต์ที่ใช้อยู่");
        for (const QString &f : s.value("SynthSoundfonts").toStringList()) {
            const QString r = Config::relocatePath(f);
            if (QFileInfo(r).isFile())
                sf.srcFiles << QDir::cleanPath(r);
        }
        sf.dstRel = "SoundFonts";
        sf.available = !sf.srcFiles.isEmpty();
        sf.on = sf.available;
        parts << sf;

        Part fx;
        fx.key = "banks";
        fx.title = QString::fromUtf8("ชุดเสียงเอฟเฟกต์ / แป้นเสียง ที่อยู่นอกโปรแกรม");
        for (const QString &r : s.value("SoundPads/BankRoots").toStringList())
            fx.srcDirs << dirIf(Config::relocatePath(r));
        for (int i = 1; i <= 9; ++i) {
            const QString f = Config::relocatePath(s.value(QString("SoundPads/Pad%1/File").arg(i)).toString());
            if (QFileInfo(f).isFile())
                fx.srcFiles << QDir::cleanPath(f);
        }
        fx.dstRel = "SoundBanks";
        fx.available = !fx.srcDirs.isEmpty() || !fx.srcFiles.isEmpty();
        fx.on = fx.available;
        parts << fx;

        // backgrounds: the picture, the slideshow pictures, the slideshow / video folders
        Part bgp;
        bgp.key = "backgrounds";
        bgp.title = QString::fromUtf8("พื้นหลัง (รูปภาพ / สไลด์ภาพ / วิดีโอพื้นหลัง)");
        bgp.dstRel = "Backgrounds";
        const QString bg = Config::relocatePath(s.value("BackgroundImage").toString());
        if (!bg.startsWith(':') && QFileInfo(bg).isFile())     // (":/..." = the built-in picture)
            bgp.srcFiles << QDir::cleanPath(bg);
        for (const QString &f : s.value("Slideshow/Files").toStringList()) {
            const QString r = Config::relocatePath(f);
            if (QFileInfo(r).isFile())
                bgp.srcFiles << QDir::cleanPath(r);
        }
        const QStringList slideDir = dirIf(Config::relocatePath(s.value("Slideshow/Dir").toString()));
        if (!slideDir.isEmpty()) { bgp.srcDirs << slideDir; bgp.dirDst << "Backgrounds/Slides"; }
        const QStringList videoDir = dirIf(Config::relocatePath(s.value("BackgroundVideoDir").toString()));
        if (!videoDir.isEmpty()) { bgp.srcDirs << videoDir; bgp.dirDst << "Backgrounds/Video"; }
        bgp.srcFiles.removeDuplicates();
        bgp.available = !bgp.srcDirs.isEmpty() || !bgp.srcFiles.isEmpty();
        bgp.on = bgp.available;
        parts << bgp;
    }

    // every file of every part (worker thread)
    void scanAll()
    {
        const QString ownFx = QDir::cleanPath(QDir(Config::DATABASE_DIR_PATH).absoluteFilePath("../SoundFx"));
        const QString events = QDir::cleanPath(QDir(Config::DATABASE_DIR_PATH).absoluteFilePath(QString::fromUtf8("../เสียงพิธีการ")));
        for (int i = 0; i < parts.size() && !cancel; ++i) {
            Part &p = parts[i];
            QVector<Job> jobs;
            qint64 bytes = 0;
            auto addDir = [&](const QString &src, const QString &dst, const std::function<bool(const QString &rel)> &skip) {
                QDirIterator it(src, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
                while (it.hasNext() && !cancel) {
                    it.next();
                    const QString rel = it.filePath().mid(src.length() + 1);
                    if (skip && skip(rel))
                        continue;
                    const qint64 sz = it.fileInfo().size();
                    jobs.append({ it.filePath(), dst + "/" + rel, sz });
                    bytes += sz;
                }
            };
            if (p.key == "program") {
                addDir(appDir(), QString(), [](const QString &rel) {
                    const QString top = rel.section('/', 0, 0).toLower();
                    if (rel.contains('/') && (top == "config" || top == "data" || top == "songs" || top == "soundfx"
                                              || top == QString::fromUtf8("เสียงพิธีการ")))
                        return true;                    // the settings / songs parts bring their own
                    const QString l = rel.toLower();
                    return l.endsWith(".log") || l.endsWith(".pdb") || l == "debug-logs.txt" || l == "debug-offline.txt"
                           || l.endsWith(".old") || l.endsWith(".bak");
                });
            } else if (p.key == "settings") {
                addDir(Config::CONFIG_DIR_PATH, "Config", [](const QString &rel) {
                    return rel == "portable-location.txt" || rel == "vst-cache.json";   // made again on the drive
                });
                addDir(Config::DATABASE_DIR_PATH, "Data", [](const QString &rel) {
                    const QString l = rel.toLower();
                    return l.endsWith("-journal") || l.endsWith("-wal") || l.endsWith("-shm");
                });
                if (QFileInfo(ownFx).isDir())
                    addDir(ownFx, "SoundFx", nullptr);
                if (QFileInfo(events).isDir())
                    addDir(events, QString::fromUtf8("เสียงพิธีการ"), nullptr);
            } else {
                for (int d = 0; d < p.srcDirs.size(); ++d) {
                    // banks: one folder each (bankDst); song libraries: straight in
                    const QString dst = p.key == "banks" ? bankDst(p, d)
                                      : (d < p.dirDst.size() ? p.dirDst[d] : p.dstRel);
                    addDir(p.srcDirs[d], dst, nullptr);
                }
                QSet<QString> names;
                for (const QString &f : p.srcFiles) {
                    QString n = QFileInfo(f).fileName();
                    for (int k = 2; names.contains(n.toLower()); ++k)
                        n = QString("%1 (%2).%3").arg(QFileInfo(f).completeBaseName()).arg(k).arg(QFileInfo(f).suffix());
                    names.insert(n.toLower());
                    const qint64 sz = QFileInfo(f).size();
                    jobs.append({ f, (p.key == "banks" ? p.dstRel + "/Files/" : p.dstRel + "/") + n, sz });
                    bytes += sz;
                }
            }
            QMutexLocker lock(&mutex);
            p.jobs = jobs;
            p.bytes = bytes;
            scannedParts = i + 1;
        }
        scanning = false;
    }
    // "D:/eXtreme Karaoke 2026/SoundFx" -> "SoundBanks/eXtreme Karaoke 2026/SoundFx": the same last two
    // folder names, so the sound pads name the banks as before and the chosen bank stays chosen
    static QString bankDst(const Part &p, int index)
    {
        auto two = [](const QString &dir) {
            QDir d(dir);
            const QString last = d.dirName();
            return (d.cdUp() && !d.isRoot() && !d.dirName().isEmpty()) ? d.dirName() + "/" + last : last;
        };
        const QString want = two(p.srcDirs[index]);
        int same = 0;
        for (int i = 0; i < index; ++i)
            if (two(p.srcDirs[i]).compare(want, Qt::CaseInsensitive) == 0)
                ++same;
        return p.dstRel + "/" + (same ? QString("%1 (%2)").arg(want).arg(same + 1) : want);
    }
    // ---------- drives ----------
    void fillDrives()
    {
        const QString sys = QDir::fromNativeSeparators(qEnvironmentVariable("SystemDrive", "C:")).left(2).toUpper();
        int pick = -1, firstOther = -1;
        for (const QStorageInfo &v : QStorageInfo::mountedVolumes()) {
            if (!v.isValid() || !v.isReady() || v.isReadOnly())
                continue;
            const QString root = QDir::fromNativeSeparators(v.rootPath());
            const QString letter = root.left(2).toUpper();
            const UINT type = GetDriveTypeW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(root).utf16()));
            QString kind = type == DRIVE_REMOVABLE ? QString::fromUtf8("แฟลชไดรฟ์ USB")
                         : (letter == sys ? QString::fromUtf8("ไดรฟ์ระบบ") : QString::fromUtf8("ฮาร์ดดิสก์ / External HD"));
            const QString name = v.name().isEmpty() ? QString() : (" " + v.name());
            const QString text = QString::fromUtf8("%1%2  ·  %3  ·  ว่าง %4 จาก %5  ·  %6")
                                     .arg(letter, name, kind, gb(v.bytesAvailable()), gb(v.bytesTotal()),
                                          QString::fromLatin1(v.fileSystemType()));
            drives->addItem(text, root.endsWith('/') ? root.left(root.length() - 1) : root);
            if (type == DRIVE_REMOVABLE && pick < 0)
                pick = drives->count() - 1;
            if (letter != sys)
                firstOther = drives->count() - 1;
        }
        if (pick < 0)
            pick = firstOther;
        if (pick < 0)
            pick = 0;
        drives->setCurrentIndex(pick);
        target = drives->currentData().toString();
        updateTarget();
    }
    static QString gb(qint64 b)
    {
        if (b < 0)
            return "?";
        if (b < 1024LL * 1024 * 1024)
            return QString("%1 MB").arg(b / (1024.0 * 1024), 0, 'f', 0);
        return QString("%1 GB").arg(b / (1024.0 * 1024 * 1024), 0, 'f', 1);
    }
    void updateTarget()
    {
        targetLabel->setText(QString::fromUtf8("จะวางไว้ที่:  ") + QDir::toNativeSeparators(appRoot()));
        QStringList notes;
        QString style = "good";
        const QStorageInfo v(target);
        if (QString::fromLatin1(v.fileSystemType()).toUpper().startsWith("FAT")) {
            notes << QString::fromUtf8("ไดรฟ์นี้เป็น FAT32: คัดลอกเพลงจำนวนมากช้ามาก และไฟล์ใหญ่เกิน 4 GB ใส่ไม่ได้ "
                                       "แนะนำให้ฟอร์แมตเป็น exFAT หรือ NTFS ก่อน (ข้อมูลในไดรฟ์จะหาย)");
            style = "warn";
        }
        if (appRoot().compare(QDir::cleanPath(appDir()), Qt::CaseInsensitive) == 0
                || QDir::cleanPath(appDir()).startsWith(appRoot() + "/", Qt::CaseInsensitive)) {
            notes << QString::fromUtf8("นี่คือโฟลเดอร์ของโปรแกรมที่เปิดอยู่ เลือกไดรฟ์อื่น");
            style = "bad";
        }
        if (QFileInfo(appRoot() + "/PTHKKaraoke.exe").exists())
            notes << QString::fromUtf8("ไดรฟ์นี้มี PTHK Karaoke อยู่แล้ว: จะอัปเดตเฉพาะไฟล์ใหม่หรือที่เปลี่ยน");
        driveNote->setObjectName(style);
        driveNote->setText(notes.isEmpty() ? QString::fromUtf8("✓ พร้อมใช้") : notes.join("\n"));
        driveNote->style()->unpolish(driveNote);
        driveNote->style()->polish(driveNote);
        updateSummary();
    }
    void updateSummary()
    {
        qint64 total = 0;
        {
            QMutexLocker lock(&mutex);
            for (const Part &p : parts)
                if (p.on)
                    total += p.bytes;
        }
        const QStorageInfo v(target);
        summary->setText(QString::fromUtf8("รวมที่เลือก %1%2  ·  ไดรฟ์ว่าง %3")
                             .arg(gb(total), scanning ? QString::fromUtf8(" (ยังนับอยู่)") : QString(), gb(v.bytesAvailable())));
        startBtn->setEnabled(!copying && !scanning && !target.isEmpty()
                             && driveNote->objectName() != "bad");
    }

    // ---------- the copy (worker thread) ----------
    bool confirmStop()
    {
        if (!copying)
            return true;
        if (QMessageBox::question(this, windowTitle(), QString::fromUtf8("กำลังคัดลอกอยู่ หยุดตอนนี้ไหม?\n"
                "(คัดลอกต่อทีหลังได้ กดเริ่มใหม่จะข้ามไฟล์ที่คัดลอกแล้ว)")) != QMessageBox::Yes)
            return false;
        cancel = true;
        if (copyWorker.joinable())
            copyWorker.join();
        copying = false;
        return true;
    }

    void start()
    {
        if (copying || scanning || target.isEmpty())
            return;
        QVector<Part> chosen;
        for (const Part &p : parts)
            if (p.on && p.available)
                chosen << p;
        const QString root = appRoot();
        for (Part &p : parts)
            if (p.check)
                p.check->setEnabled(false);
        drives->setEnabled(false);
        startBtn->setEnabled(false);
        openBtn->hide();
        copying = true;
        cancel = false;
        done = 0;
        failed = 0;
        doneBytes = 0;
        totalBytes = 0;
        clock.start();
        if (copyWorker.joinable())
            copyWorker.join();
        copyWorker = std::thread([this, chosen, root]() { copyAll(chosen, root); });
    }

    void setStage(const QString &s) { QMutexLocker lock(&mutex); stage = s; }
    void setCurrent(const QString &s) { QMutexLocker lock(&mutex); current = s; }
    void addError(const QString &s) { QMutexLocker lock(&mutex); ++failed; if (errors.size() < 8) errors << s; }

    void copyAll(QVector<Part> chosen, const QString root)
    {
        // 1. what is not on the drive yet (same size and date = already there)
        setStage(QString::fromUtf8("กำลังดูว่าไดรฟ์มีไฟล์ไหนแล้วบ้าง..."));
        QVector<Job> todo;
        qint64 need = 0;
        for (const Part &p : chosen)
            for (const Job &j : p.jobs) {
                if (cancel) { finish(false, root); return; }
                const Job x{ j.src, root + (j.dst.isEmpty() || j.dst.startsWith('/') ? j.dst : "/" + j.dst), j.size };
                if (isDb(x.src))
                    continue;                           // the databases: a clean snapshot below
                const QFileInfo d(x.dst);
                if (d.exists() && d.size() == x.size
                        && qAbs(d.lastModified().msecsTo(QFileInfo(x.src).lastModified())) <= 2000)
                    continue;
                todo << x;
                need += x.size;
            }
        totalBytes = need;
        totalFiles = todo.size();
        const QStorageInfo v(root.left(3));
        if (v.isValid() && need > v.bytesAvailable() - 300LL * 1024 * 1024) {
            addError(QString::fromUtf8("พื้นที่ในไดรฟ์ไม่พอ: ต้องใช้อีก %1 แต่ว่าง %2").arg(gb(need), gb(v.bytesAvailable())));
            finish(false, root);
            return;
        }

        // 2. the files
        setStage(QString::fromUtf8("กำลังคัดลอก..."));
        QSet<QString> made;
        for (const Job &j : todo) {
            if (cancel) { finish(false, root); return; }
            const QString dir = QFileInfo(j.dst).absolutePath();
            if (!made.contains(dir)) {
                QDir().mkpath(dir);
                made.insert(dir);
            }
            setCurrent(j.src);
            // CopyFile keeps the file's date: the next run sees it as already there
            if (!CopyFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(j.src).utf16()),
                           reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(j.dst).utf16()), FALSE))
                addError(QString::fromUtf8("คัดลอกไม่ได้ (รหัส %1): %2").arg(GetLastError()).arg(QDir::toNativeSeparators(j.src)));
            doneBytes += j.size;
            ++done;
        }

        // 3. the song database and the lyrics index: a consistent copy while the program uses them
        setStage(QString::fromUtf8("กำลังคัดลอกฐานข้อมูลเพลงและดัชนีเนื้อร้อง..."));
        QDir().mkpath(root + "/Data");
        for (const QString &db : QDir(Config::DATABASE_DIR_PATH).entryList({ "*.db3" }, QDir::Files)) {
            if (cancel) { finish(false, root); return; }
            setCurrent(db);
            snapshotDb(Config::DATABASE_DIR_PATH + "/" + db, root + "/Data/" + db);
        }

        // 4. the settings on the drive point into the drive
        setStage(QString::fromUtf8("กำลังตั้งค่าให้ชี้เข้าไดรฟ์..."));
        rewriteSettings(chosen, root);
        finish(true, root);
    }
    static bool isDb(const QString &src)
    {
        return src.endsWith(".db3", Qt::CaseInsensitive)
               && QFileInfo(src).absolutePath().compare(QDir::cleanPath(Config::DATABASE_DIR_PATH), Qt::CaseInsensitive) == 0;
    }
    void snapshotDb(const QString &src, const QString &dst)
    {
        const QString tmp = dst + ".new";
        QFile::remove(tmp);
        bool ok = false;
        {
            const QString conn = "portable-" + QUuid::createUuid().toString();
            {
                QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
                db.setDatabaseName(src);
                db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000");
                if (db.open()) {
                    QSqlQuery q(db);
                    ok = q.exec(QString("VACUUM INTO '%1'").arg(QString(tmp).replace("'", "''")));
                    if (!ok)
                        addError(QString::fromUtf8("ฐานข้อมูล %1: %2").arg(QFileInfo(src).fileName(), q.lastError().text()));
                    q.finish();
                    db.close();
                }
            }
            QSqlDatabase::removeDatabase(conn);
        }
        if (!ok) {                                      // older SQLite: a plain copy
            ok = CopyFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(src).utf16()),
                           reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(tmp).utf16()), FALSE);
        }
        if (ok) {
            QFile::remove(dst);
            if (!QFile::rename(tmp, dst))
                addError(QString::fromUtf8("วางฐานข้อมูลไม่ได้: ") + QDir::toNativeSeparators(dst));
        }
    }

    // where a source folder / file landed on the drive
    QString mapped(const QVector<Part> &chosen, const QString &root, const QString &src) const
    {
        const QString s = QDir::cleanPath(Config::relocatePath(src));
        for (const Part &p : chosen)
            for (const Job &j : p.jobs)
                if (j.src.compare(s, Qt::CaseInsensitive) == 0)
                    return root + "/" + j.dst;
        return QString();
    }

    void rewriteSettings(const QVector<Part> &chosen, const QString &root)
    {
        {
            QSettings s(root + "/Config/PTHKKaraoke.conf", QSettings::IniFormat);
            const struct { const char *key, *part, *dst; } folders[] = {
                { "NCNPath", "NCN", "Songs/NCN" }, { "HNKPath", "EMK", "Songs/HNK" }, { "SIBPath", "SIB", "Songs/SIB" },
                { "KARPath", "KAR", "Songs/KAR" }, { "VideoPath", "VIDEO", "Songs/VIDEO" }, { "MP3Path", "MP3", "Songs/MP3" },
            };
            for (const auto &f : folders) {
                // (not copied: an empty folder on the drive, not a folder of this computer)
                s.setValue(f.key, root + "/" + f.dst);
                QDir().mkpath(root + "/" + f.dst);
            }
            QStringList sfs;
            for (const QString &f : s.value("SynthSoundfonts").toStringList()) {
                const QString m = mapped(chosen, root, f);
                sfs << (m.isEmpty() ? f : m);
            }
            s.setValue("SynthSoundfonts", sfs);
            // the sound banks: one folder each on the drive
            for (const Part &p : chosen)
                if (p.key == "banks") {
                    QStringList roots;
                    for (int d = 0; d < p.srcDirs.size(); ++d)
                        roots << root + "/" + bankDst(p, d);
                    s.setValue("SoundPads/BankRoots", roots);
                    for (int i = 1; i <= 9; ++i) {
                        const QString k = QString("SoundPads/Pad%1/File").arg(i);
                        const QString m = mapped(chosen, root, s.value(k).toString());
                        if (!m.isEmpty())
                            s.setValue(k, m);
                    }
                }
            for (const Part &p : chosen)
                if (p.key == "backgrounds") {
                    const QString m = mapped(chosen, root, s.value("BackgroundImage").toString());
                    if (!m.isEmpty())
                        s.setValue("BackgroundImage", m);
                    QStringList files;
                    for (const QString &f : s.value("Slideshow/Files").toStringList()) {
                        const QString mf = mapped(chosen, root, f);
                        files << (mf.isEmpty() ? f : mf);
                    }
                    if (!files.isEmpty())
                        s.setValue("Slideshow/Files", files);
                    for (int d = 0; d < p.srcDirs.size() && d < p.dirDst.size(); ++d) {
                        if (p.dirDst[d].endsWith("/Slides"))
                            s.setValue("Slideshow/Dir", root + "/" + p.dirDst[d]);
                        else if (p.dirDst[d].endsWith("/Video"))
                            s.setValue("BackgroundVideoDir", root + "/" + p.dirDst[d]);
                    }
                }
            s.sync();
        }
        {   // plugins outside the program's VST folder: copied in; the mixer finds them there
            const QString file = root + "/Config/SynthMixer.conf";
            QFile f(file);
            if (f.open(QIODevice::ReadOnly)) {
                QString text = QString::fromLatin1(f.readAll());
                f.close();
                static const QRegularExpression plug(QStringLiteral("([A-Za-z]:/[^=\\r\\n,\"]*?\\.(?:dll|vst3))"),
                                                     QRegularExpression::CaseInsensitiveOption);
                QSet<QString> done;
                auto it = plug.globalMatch(text);
                QStringList found;
                while (it.hasNext())
                    found << it.next().captured(1);
                for (const QString &p : found) {
                    if (done.contains(p) || p.startsWith(root, Qt::CaseInsensitive))
                        continue;
                    done.insert(p);
                    const QFileInfo fi(p);
                    const QString dst = root + "/VST/" + fi.fileName();
                    if (fi.isDir())
                        copyTree(p, dst);
                    else if (fi.isFile())
                        CopyFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(p).utf16()),
                                  reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(dst).utf16()), FALSE);
                    if (QFileInfo::exists(dst))
                        text.replace(p, dst, Qt::CaseInsensitive);
                }
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    f.write(text.toLatin1());
                    f.close();              // (QSettings below writes the same file)
                }
            }
            QSettings syn(file, QSettings::IniFormat);
            syn.setValue("VSTDirs", QStringList());     // <program>\VST is always looked at
            syn.sync();
        }
        // the drive's own place: on another computer (other letter) the paths follow it
        QFile m(root + "/Config/portable-location.txt");
        if (m.open(QIODevice::WriteOnly | QIODevice::Truncate))
            m.write(root.toUtf8());
        QFile p(root + "/Portable.txt");
        if (p.open(QIODevice::WriteOnly | QIODevice::Truncate))
            p.write(QString::fromUtf8("PTHK Karaoke แบบพกพา: ค่าตั้งและฐานข้อมูลเพลงอยู่ในโฟลเดอร์ Config และ Data ข้างโปรแกรม\r\n"
                                      "เปิด PTHKKaraoke.exe จากไดรฟ์นี้ได้ทุกเครื่อง\r\n").toUtf8());
    }
    void copyTree(const QString &src, const QString &dst)
    {
        QDirIterator it(src, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString d = dst + it.filePath().mid(src.length());
            QDir().mkpath(QFileInfo(d).absolutePath());
            CopyFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(it.filePath()).utf16()),
                      reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(d).utf16()), FALSE);
        }
    }
    void finish(bool ok, const QString &)
    {
        {
            QMutexLocker lock(&mutex);
            finished = true;
            finishedOk = ok && !cancel;
        }
        copying = false;
    }

    // ---------- the screen, 5 times a second ----------
    void tick()
    {
        if (scanning || scannedShown != scannedParts) {
            QMutexLocker lock(&mutex);
            for (int i = 0; i < parts.size(); ++i)
                if (i < scannedParts && parts[i].sizeLabel)
                    parts[i].sizeLabel->setText(QString::fromUtf8("%1 ไฟล์  ·  %2")
                                                    .arg(QLocale(QLocale::English).toString(parts[i].jobs.size()), gb(parts[i].bytes)));
            scannedShown = scannedParts;
            lock.unlock();
            updateSummary();
        }
        QString st, cur;
        {
            QMutexLocker lock(&mutex);
            st = stage;
            cur = current;
        }
        if (copying || finished) {
            const qint64 tb = totalBytes, db = doneBytes;
            bar->setValue(tb > 0 ? int(1000.0 * db / tb) : (finished ? 1000 : 0));
            const double sec = clock.isValid() ? clock.elapsed() / 1000.0 : 0;
            const double rate = sec > 1 ? db / sec : 0;
            QString eta;
            if (rate > 0 && tb > db) {
                const int left = int((tb - db) / rate);
                eta = QString::fromUtf8("  ·  เหลืออีกประมาณ %1 นาที").arg(qMax(1, left / 60));
            }
            if (!finished) status->setText(st + QString::fromUtf8("  %1 / %2 ไฟล์  ·  %3 / %4%5")
                                     .arg(QLocale(QLocale::English).toString(int(done)), QLocale(QLocale::English).toString(int(totalFiles)),
                                          gb(db), gb(tb), eta));
            currentFile->setText(currentFile->fontMetrics().elidedText(QDir::toNativeSeparators(cur), Qt::ElideMiddle,
                                                                       qMax(200, currentFile->width())));
        }
        bool fin = false;
        {
            QMutexLocker lock(&mutex);
            fin = finished;
            finished = false;
        }
        if (fin) {
            for (Part &p : parts)
                if (p.check)
                    p.check->setEnabled(p.available);
            drives->setEnabled(true);
            currentFile->clear();
            QStringList errs;
            {
                QMutexLocker lock(&mutex);
                errs = errors;
            }
            if (finishedOk) {
                bar->setValue(1000);
                status->setObjectName("good");
                status->setText(QString::fromUtf8("✓ เสร็จแล้ว  ·  เสียบไดรฟ์นี้ที่เครื่องไหนก็ได้ แล้วเปิด  %1  ร้องได้ทันที%2")
                                    .arg(QDir::toNativeSeparators(appRoot() + "/PTHKKaraoke.exe"),
                                         failed > 0 ? QString::fromUtf8("\n(คัดลอกไม่ได้ %1 ไฟล์: %2)").arg(int(failed)).arg(errs.join("\n")) : QString()));
                openBtn->show();
                startBtn->setText(QString::fromUtf8("อัปเดตอีกครั้ง"));
            } else {
                status->setObjectName(errs.isEmpty() ? "warn" : "bad");
                status->setText(errs.isEmpty() ? QString::fromUtf8("หยุดแล้ว  ·  กดเริ่มอีกครั้งเพื่อคัดลอกต่อ")
                                               : errs.join("\n"));
            }
            status->style()->unpolish(status);
            status->style()->polish(status);
            updateSummary();
        }
    }

    QFrame *box(QVBoxLayout *main, const QString &title)
    {
        QFrame *f = new QFrame(this);
        f->setObjectName("box");
        QVBoxLayout *l = new QVBoxLayout(f);
        l->setContentsMargins(16, 12, 16, 14);
        l->setSpacing(8);
        QLabel *t = new QLabel(title, f);
        QFont tf = t->font();
        tf.setPixelSize(15);
        tf.setBold(true);
        t->setFont(tf);
        l->addWidget(t);
        main->addWidget(f);
        return f;
    }

    QVector<Part> parts;
    QString target;
    QComboBox *drives = nullptr;
    QLabel *targetLabel = nullptr, *driveNote = nullptr, *summary = nullptr, *status = nullptr, *currentFile = nullptr;
    QProgressBar *bar = nullptr;
    QPushButton *startBtn = nullptr, *closeBtn = nullptr, *openBtn = nullptr;
    QTimer *timer = nullptr;
    QElapsedTimer clock;

    std::thread scanWorker, copyWorker;
    QMutex mutex;                       // parts' jobs / sizes, stage, current, errors
    std::atomic<bool> cancel { false }, scanning { false }, copying { false };
    std::atomic<int> scannedParts { 0 };
    int scannedShown = -1;
    std::atomic<qint64> done { 0 }, failed { 0 }, doneBytes { 0 }, totalBytes { 0 }, totalFiles { 0 };
    QString stage, current;
    QStringList errors;
    bool finished = false, finishedOk = false;     // (finished is set under the mutex)
};

#endif // PORTABLEDRIVEDIALOG_H
