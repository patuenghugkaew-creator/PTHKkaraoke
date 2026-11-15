#ifndef SOUNDFXMANAGER_H
#define SOUNDFXMANAGER_H

// "เพิ่มเสียงเอฟเฟกต์": the shop adds its own sound files. They are copied into SoundPads::ownRoot(),
// one folder per bank, named "01 - <name>.mp3" so the order is the pad order (pad 1-9 = the first 9).
// Files can also be dropped on the window. Header-only, no Q_OBJECT.

#include <QDesktopServices>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>

#include "Midi/SoundPads.h"

class SoundFxManager : public QDialog
{
public:
    std::function<void()> onChanged;    // banks / sounds were added, removed or moved

    SoundFxManager(QWidget *parent, SoundPads *pads, const QString &showBank = QString())
        : QDialog(parent), sp(pads)
    {
        setWindowTitle(QString::fromUtf8("เพิ่มเสียงเอฟเฟกต์"));
        setAcceptDrops(true);
        resize(760, 460);
        QDir().mkpath(SoundPads::ownRoot());

        QVBoxLayout *v = new QVBoxLayout(this);
        QLabel *help = new QLabel(QString::fromUtf8(
            "ใส่ไฟล์เสียงของร้านเอง (WAV / MP3 ...) แยกเป็นชุด • แป้น 1-9 ของชุด = 9 เสียงแรก "
            "• ลากไฟล์เสียงมาวางที่หน้านี้ได้ • ไฟล์ถูกคัดลอกมาเก็บไว้ ต้นฉบับย้ายหรือลบได้"), this);
        help->setWordWrap(true);
        v->addWidget(help);

        QHBoxLayout *h = new QHBoxLayout;
        // ---- banks
        QVBoxLayout *left = new QVBoxLayout;
        left->addWidget(new QLabel(QString::fromUtf8("<b>ชุดเสียงของร้าน</b>"), this));
        bankList = new QListWidget(this);
        left->addWidget(bankList, 1);
        QHBoxLayout *lb = new QHBoxLayout;
        QPushButton *newBank = new QPushButton(QString::fromUtf8("ชุดใหม่..."), this);
        QPushButton *renBank = new QPushButton(QString::fromUtf8("เปลี่ยนชื่อ..."), this);
        QPushButton *delBank = new QPushButton(QString::fromUtf8("ลบชุด"), this);
        lb->addWidget(newBank);
        lb->addWidget(renBank);
        lb->addWidget(delBank);
        left->addLayout(lb);
        h->addLayout(left, 2);

        // ---- the sounds of the chosen bank
        QVBoxLayout *right = new QVBoxLayout;
        right->addWidget(new QLabel(QString::fromUtf8("<b>เสียงในชุด</b> (ลำดับ = แป้น 1-9)"), this));
        fileList = new QListWidget(this);
        right->addWidget(fileList, 1);
        QHBoxLayout *rb = new QHBoxLayout;
        QPushButton *add = new QPushButton(QString::fromUtf8("เพิ่มไฟล์เสียง..."), this);
        QPushButton *play = new QPushButton(QString::fromUtf8("ลองฟัง"), this);
        QPushButton *up = new QPushButton(QString::fromUtf8("▲"), this);
        QPushButton *down = new QPushButton(QString::fromUtf8("▼"), this);
        QPushButton *del = new QPushButton(QString::fromUtf8("ลบเสียง"), this);
        up->setToolTip(QString::fromUtf8("ขึ้น (แป้นเลขน้อยลง)"));
        down->setToolTip(QString::fromUtf8("ลง (แป้นเลขมากขึ้น)"));
        rb->addWidget(add);
        rb->addWidget(play);
        rb->addStretch();
        rb->addWidget(up);
        rb->addWidget(down);
        rb->addWidget(del);
        right->addLayout(rb);
        h->addLayout(right, 3);
        v->addLayout(h, 1);

        QHBoxLayout *bottom = new QHBoxLayout;
        QPushButton *openDir = new QPushButton(QString::fromUtf8("เปิดโฟลเดอร์เสียง"), this);
        QPushButton *close = new QPushButton(QString::fromUtf8("ปิด"), this);
        bottom->addWidget(openDir);
        bottom->addStretch();
        bottom->addWidget(close);
        v->addLayout(bottom);

        connect(bankList, &QListWidget::currentRowChanged, this, [this]() { showFiles(); });
        connect(fileList, &QListWidget::itemDoubleClicked, this, [this]() { previewCurrent(); });
        connect(newBank, &QPushButton::clicked, this, [this]() { createBank(); });
        connect(renBank, &QPushButton::clicked, this, [this]() { renameBank(); });
        connect(delBank, &QPushButton::clicked, this, [this]() { deleteBank(); });
        connect(add, &QPushButton::clicked, this, [this]() {
            const QStringList f = QFileDialog::getOpenFileNames(this, QString::fromUtf8("เลือกไฟล์เสียง"), lastDir,
                QString::fromUtf8("ไฟล์เสียง (*.wav *.mp3 *.ogg *.flac *.m4a *.wma *.aif *.aiff)"));
            if (!f.isEmpty()) {
                lastDir = QFileInfo(f.first()).absolutePath();
                addFiles(f);
            }
        });
        connect(play, &QPushButton::clicked, this, [this]() { previewCurrent(); });
        connect(up, &QPushButton::clicked, this, [this]() { moveCurrent(-1); });
        connect(down, &QPushButton::clicked, this, [this]() { moveCurrent(+1); });
        connect(del, &QPushButton::clicked, this, [this]() { deleteCurrent(); });
        connect(openDir, &QPushButton::clicked, this, [this]() {
            const QString d = currentBankDir().isEmpty() ? SoundPads::ownRoot() : currentBankDir();
            QDesktopServices::openUrl(QUrl::fromLocalFile(d));
        });
        connect(close, &QPushButton::clicked, this, &QDialog::accept);

        showBanks(showBank);
    }
    ~SoundFxManager() override { sp->stopPreview(); }

    // the bank chosen when the window closes ("" = none)
    QString chosenBank() const { return bankList->currentItem() ? bankList->currentItem()->text() : QString(); }

protected:
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->mimeData()->hasUrls())
            e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override
    {
        QStringList f;
        for (const QUrl &u : e->mimeData()->urls())
            if (u.isLocalFile())
                f << u.toLocalFile();
        addFiles(f);
    }

private:
    QString currentBankDir() const
    {
        const QListWidgetItem *it = bankList->currentItem();
        return it ? QDir(SoundPads::ownRoot()).absoluteFilePath(it->text()) : QString();
    }

    void showBanks(const QString &select = QString())
    {
        const QString keep = select.isEmpty() && bankList->currentItem() ? bankList->currentItem()->text() : select;
        bankList->clear();
        const QFileInfoList dirs = QDir(SoundPads::ownRoot()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                                          QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &d : dirs)
            bankList->addItem(d.fileName());
        int row = 0;
        for (int r = 0; r < bankList->count(); ++r)
            if (bankList->item(r)->text() == keep)
                row = r;
        if (bankList->count() > 0)
            bankList->setCurrentRow(row);
        showFiles();
    }

    void showFiles(int select = -1)
    {
        fileList->clear();
        const QString dir = currentBankDir();
        if (dir.isEmpty()) {
            fileList->addItem(QString::fromUtf8("(ยังไม่มีชุดเสียง: กด \"เพิ่มไฟล์เสียง...\" หรือลากไฟล์มาวาง)"));
            fileList->item(0)->setFlags(Qt::NoItemFlags);
            return;
        }
        const QStringList f = SoundPads::soundFiles(dir);
        for (int i = 0; i < f.size(); ++i) {
            QListWidgetItem *it = new QListWidgetItem(i < SoundPads::Count
                ? QString::fromUtf8("แป้น %1   %2").arg(i + 1).arg(SoundPads::fileLabel(f[i]))
                : QString::fromUtf8("        %1   (เกิน 9 เสียง: ไม่มีแป้น)").arg(SoundPads::fileLabel(f[i])));
            it->setData(Qt::UserRole, f[i]);
            fileList->addItem(it);
        }
        if (fileList->count() > 0)
            fileList->setCurrentRow(qBound(0, select, fileList->count() - 1));
    }

    static bool validName(const QString &n)
    {
        static const QString bad = QStringLiteral("\\/:*?\"<>|");
        if (n.trimmed().isEmpty())
            return false;
        for (const QChar c : n)
            if (bad.contains(c))
                return false;
        return true;
    }
    QString askBankName(const QString &title, const QString &start)
    {
        bool ok = false;
        const QString n = QInputDialog::getText(this, title, QString::fromUtf8("ชื่อชุด:"), QLineEdit::Normal, start, &ok).trimmed();
        if (!ok || n.isEmpty())
            return QString();
        if (!validName(n)) {
            QMessageBox::warning(this, title, QString::fromUtf8("ชื่อชุดมีตัวอักษรที่ใช้ไม่ได้: \\ / : * ? \" < > |"));
            return QString();
        }
        if (QFileInfo(QDir(SoundPads::ownRoot()).absoluteFilePath(n)).exists()) {
            QMessageBox::warning(this, title, QString::fromUtf8("มีชุดชื่อนี้แล้ว"));
            return QString();
        }
        return n;
    }

    void createBank()
    {
        const QString n = askBankName(QString::fromUtf8("ชุดเสียงใหม่"), QString::fromUtf8("ชุดที่ %1").arg(bankList->count() + 1));
        if (n.isEmpty())
            return;
        QDir(SoundPads::ownRoot()).mkpath(n);
        showBanks(n);
    }
    void renameBank()
    {
        const QListWidgetItem *it = bankList->currentItem();
        if (!it)
            return;
        const QString old = it->text();
        const QString n = askBankName(QString::fromUtf8("เปลี่ยนชื่อชุด"), old);
        if (n.isEmpty() || n == old)
            return;
        sp->stopAll();                  // its files may be playing
        if (!QDir(SoundPads::ownRoot()).rename(old, n)) {
            QMessageBox::warning(this, windowTitle(), QString::fromUtf8("เปลี่ยนชื่อไม่ได้"));
            return;
        }
        showBanks(n);
        changed();
    }
    void deleteBank()
    {
        const QListWidgetItem *it = bankList->currentItem();
        if (!it)
            return;
        const QString name = it->text();
        if (QMessageBox::question(this, windowTitle(),
                QString::fromUtf8("ลบชุด \"%1\" และไฟล์เสียงทั้งหมดในชุด?\n(ไฟล์ต้นฉบับที่เลือกมาไม่ถูกลบ)").arg(name))
                != QMessageBox::Yes)
            return;
        sp->stopAll();
        QDir(QDir(SoundPads::ownRoot()).absoluteFilePath(name)).removeRecursively();
        showBanks();
        changed();
    }

    // copy sound files into the chosen bank (a new bank "ชุดที่ 1" when there is none yet)
    void addFiles(const QStringList &paths)
    {
        QStringList ok;
        for (const QString &p : paths)
            if (SoundPads::isSoundFile(p))
                ok << p;
        if (ok.isEmpty()) {
            if (!paths.isEmpty())
                QMessageBox::warning(this, windowTitle(), QString::fromUtf8("ไม่มีไฟล์เสียงที่ใช้ได้ (WAV, MP3, OGG, FLAC, M4A, WMA, AIF)"));
            return;
        }
        if (currentBankDir().isEmpty()) {
            const QString n = QString::fromUtf8("ชุดที่ 1");
            QDir(SoundPads::ownRoot()).mkpath(n);
            showBanks(n);
        }
        const QString dir = currentBankDir();
        QStringList order = SoundPads::soundFiles(dir);
        int failed = 0;
        for (const QString &src : ok) {
            const QFileInfo fi(src);
            // any number first; renumber() gives every file its place number
            const QString label = SoundPads::fileLabel(src);
            QString dst = QDir(dir).absoluteFilePath(QString("99 - %1.%2").arg(label).arg(fi.suffix().toLower()));
            for (int k = 2; QFileInfo::exists(dst); ++k)
                dst = QDir(dir).absoluteFilePath(QString("99 - %1 (%2).%3").arg(label).arg(k).arg(fi.suffix().toLower()));
            if (QFile::copy(src, dst))
                order << dst;
            else
                ++failed;
        }
        renumber(dir, order);
        showFiles(order.size() - 1);
        changed();
        if (failed > 0)
            QMessageBox::warning(this, windowTitle(), QString::fromUtf8("คัดลอกไม่ได้ %1 ไฟล์").arg(failed));
    }

    // names the files "01 - <name>.ext", "02 - ..." in this order (= pad 1, 2, ...)
    static void renumber(const QString &dir, const QStringList &order)
    {
        QStringList temp;
        for (int i = 0; i < order.size(); ++i) {           // out of the way first: no name clashes
            const QString t = QDir(dir).absoluteFilePath(QString("~renumber %1.%2").arg(i).arg(QFileInfo(order[i]).suffix()));
            temp << (QFile::rename(order[i], t) ? t : order[i]);
        }
        for (int i = 0; i < temp.size(); ++i) {
            const QString label = SoundPads::fileLabel(order[i]);
            QString name = QString("%1 - %2.%3").arg(i + 1, 2, 10, QChar('0')).arg(label).arg(QFileInfo(order[i]).suffix().toLower());
            QFile::rename(temp[i], QDir(dir).absoluteFilePath(name));
        }
    }

    void moveCurrent(int step)
    {
        const int r = fileList->currentRow();
        const QString dir = currentBankDir();
        QStringList order = SoundPads::soundFiles(dir);
        if (r < 0 || r >= order.size() || r + step < 0 || r + step >= order.size())
            return;
        sp->stopAll();
        order.swapItemsAt(r, r + step);
        renumber(dir, order);
        showFiles(r + step);
        changed();
    }
    void deleteCurrent()
    {
        const QListWidgetItem *it = fileList->currentItem();
        if (!it || it->data(Qt::UserRole).toString().isEmpty())
            return;
        const QString f = it->data(Qt::UserRole).toString();
        if (QMessageBox::question(this, windowTitle(), QString::fromUtf8("ลบเสียง \"%1\" ออกจากชุด?").arg(SoundPads::fileLabel(f)))
                != QMessageBox::Yes)
            return;
        const int r = fileList->currentRow();
        sp->stopAll();
        QFile::remove(f);
        renumber(currentBankDir(), SoundPads::soundFiles(currentBankDir()));
        showFiles(r);
        changed();
    }
    void previewCurrent()
    {
        const QListWidgetItem *it = fileList->currentItem();
        if (it && !it->data(Qt::UserRole).toString().isEmpty())
            sp->preview(it->data(Qt::UserRole).toString());
    }
    void changed()
    {
        sp->reload();
        if (onChanged)
            onChanged();
    }

    SoundPads *sp;
    QListWidget *bankList = nullptr;
    QListWidget *fileList = nullptr;
    QString lastDir;
};

#endif // SOUNDFXMANAGER_H
