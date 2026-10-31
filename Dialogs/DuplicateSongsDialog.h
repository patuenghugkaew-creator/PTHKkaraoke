// "ค้นหาเพลงซ้ำ": songs with the same name, artist and tempo (optionally key) are grouped;
// the user ticks the copies to remove. Removal takes them out of the song list; optionally
// the files go to the Recycle Bin (recoverable). Header-only, no moc needed.
#ifndef DUPLICATESONGSDIALOG_H
#define DUPLICATESONGSDIALOG_H

#include "SongDatabase.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QTreeWidget>
#include <QVBoxLayout>

class DuplicateSongsDialog : public QDialog
{
public:
    DuplicateSongsDialog(SongDatabase *database, QWidget *parent) : QDialog(parent), db(database)
    {
        setWindowTitle(tr("ค้นหาเพลงซ้ำ"));
        setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        resize(980, 640);

        auto *lay = new QVBoxLayout(this);
        lblSummary = new QLabel(this);
        lblSummary->setWordWrap(true);
        lay->addWidget(lblSummary);

        auto *opts = new QHBoxLayout();
        edFilter = new QLineEdit(this);
        edFilter->setPlaceholderText(tr("กรองตามชื่อเพลง / นักร้อง"));
        opts->addWidget(edFilter, 1);
        chkKey = new QCheckBox(tr("คีย์ต้องตรงกันด้วย"), this);
        opts->addWidget(chkKey);
        cbScope = new QComboBox(this);
        cbScope->addItem(tr("ซ้ำทุกแบบ"), 0);
        cbScope->addItem(tr("เฉพาะชนิดไฟล์เดียวกันซ้ำกัน (เช่น NCN ซ้ำ NCN)"), 1);
        opts->addWidget(cbScope);
        auto *btnFind = new QPushButton(tr("ค้นหา"), this);
        opts->addWidget(btnFind);
        lay->addLayout(opts);

        tree = new QTreeWidget(this);
        tree->setColumnCount(5);
        tree->setHeaderLabels({ tr("รหัส / เพลง"), tr("ชนิด"), tr("คีย์"), tr("ความเร็ว"), tr("ไฟล์") });
        tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
        tree->setColumnWidth(0, 360);
        tree->setColumnWidth(1, 60);
        tree->setColumnWidth(2, 50);
        tree->setColumnWidth(3, 70);
        tree->setUniformRowHeights(true);
        lay->addWidget(tree, 1);

        auto *pager = new QHBoxLayout();
        auto *btnPrev = new QPushButton(tr("◀ ก่อนหน้า"), this);
        auto *btnNext = new QPushButton(tr("ถัดไป ▶"), this);
        lblPage = new QLabel(this);
        pager->addWidget(btnPrev);
        pager->addWidget(lblPage);
        pager->addWidget(btnNext);
        pager->addStretch(1);
        pager->addWidget(new QLabel(tr("ติ๊กอัตโนมัติ เก็บไว้กลุ่มละ 1 เพลง โดยเก็บ:"), this));
        cbKeep = new QComboBox(this);
        cbKeep->addItem("EMK", "EMK");
        cbKeep->addItem("NCN", "NCN");
        cbKeep->addItem("SIB", "SIB");
        cbKeep->addItem("HNK", "HNK");
        cbKeep->addItem(tr("ตัวแรกในกลุ่ม"), "");
        pager->addWidget(cbKeep);
        auto *btnAuto = new QPushButton(tr("ติ๊กอัตโนมัติ"), this);
        auto *btnClear = new QPushButton(tr("ล้างที่ติ๊ก"), this);
        pager->addWidget(btnAuto);
        pager->addWidget(btnClear);
        lay->addLayout(pager);

        auto *bottom = new QHBoxLayout();
        chkFiles = new QCheckBox(tr("ย้ายไฟล์ไปถังขยะด้วย (กู้คืนได้)"), this);
        chkFiles->setToolTip(tr("ไม่ติ๊ก: ลบออกจากรายการเพลงเท่านั้น ถ้าสั่งอัปเดตคลังเพลงทั้งหมด เพลงจะกลับมา"));
        bottom->addWidget(chkFiles);
        bottom->addStretch(1);
        lblChecked = new QLabel(this);
        bottom->addWidget(lblChecked);
        auto *btnDelete = new QPushButton(tr("ลบที่ติ๊ก"), this);
        btnDelete->setStyleSheet("QPushButton { color: #d32f2f; font-weight: bold; }");
        auto *btnClose = new QPushButton(tr("ปิด"), this);
        bottom->addWidget(btnDelete);
        bottom->addWidget(btnClose);
        lay->addLayout(bottom);

        connect(btnFind, &QPushButton::clicked, this, [this]() { rebuild(); });
        connect(edFilter, &QLineEdit::returnPressed, this, [this]() { applyFilter(); });
        connect(chkKey, &QCheckBox::toggled, this, [this]() { rebuild(); });
        connect(cbScope, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { rebuild(); });
        connect(btnPrev, &QPushButton::clicked, this, [this]() { if (page > 0) { --page; showPage(); } });
        connect(btnNext, &QPushButton::clicked, this, [this]() { if ((page + 1) * kPageSize < filtered.size()) { ++page; showPage(); } });
        connect(btnAuto, &QPushButton::clicked, this, [this]() { autoSelect(); });
        connect(btnClear, &QPushButton::clicked, this, [this]() {
            for (Entry &e : all) e.checked = false;
            showPage();
        });
        connect(btnDelete, &QPushButton::clicked, this, [this]() { deleteChecked(); });
        connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);
        connect(tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *it, int col) {
            if (building || col != 0) return;
            const int idx = it->data(0, Qt::UserRole).toInt();
            if (idx >= 0 && idx < all.size()) {
                all[idx].checked = it->checkState(0) == Qt::Checked;
                updateCount();
            }
        });

        load();
        rebuild();
    }

private:
    struct Entry { QString id, name, artist, key, type, path; int tempo = 0; bool checked = false; bool gone = false; };
    static const int kPageSize = 200;

    static QString norm(const QString &s)
    {
        QString t = s.toLower();
        t.remove(QRegularExpression("\\s+"));
        return t;
    }

    void load()
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        all.clear();
        QSqlQuery q(*db->database());
        q.setForwardOnly(true);
        q.exec("SELECT id, name, artist, keyname, tempo, songtype, path FROM songs");
        while (q.next()) {
            Entry e;
            e.id = q.value(0).toString();
            e.name = q.value(1).toString();
            e.artist = q.value(2).toString();
            e.key = q.value(3).toString();
            e.tempo = q.value(4).toInt();
            e.type = q.value(5).toString();
            e.path = q.value(6).toString();
            all.append(e);
        }
        QApplication::restoreOverrideCursor();
    }

    void rebuild()
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        groups.clear();
        QHash<QString, int> byKey;
        const bool withKey = chkKey->isChecked();
        for (int i = 0; i < all.size(); ++i) {
            const Entry &e = all[i];
            if (e.gone || e.name.trimmed().isEmpty())
                continue;
            QString k = norm(e.name) + '\x1f' + norm(e.artist) + '\x1f' + QString::number(e.tempo);
            if (withKey) k += '\x1f' + norm(e.key);
            if (cbScope->currentData().toInt() == 1) k += '\x1f' + e.type;
            auto it = byKey.find(k);
            if (it == byKey.end()) {
                byKey.insert(k, groups.size());
                groups.append(QVector<int>{ i });
            } else {
                groups[it.value()].append(i);
            }
        }
        QVector<QVector<int>> dups;
        for (const QVector<int> &g : groups)
            if (g.size() > 1) dups.append(g);
        groups = dups;
        int extra = 0;
        for (const QVector<int> &g : groups) extra += g.size() - 1;
        lblSummary->setText(tr("พบเพลงซ้ำ %1 กลุ่ม (ชื่อเพลง + นักร้อง + ความเร็ว%2 ตรงกัน) รวมส่วนเกิน %3 เพลง จากทั้งหมด %4 เพลง")
                                .arg(groups.size()).arg(chkKey->isChecked() ? tr(" + คีย์") : QString())
                                .arg(extra).arg(all.size()));
        QApplication::restoreOverrideCursor();
        applyFilter();
    }

    void applyFilter()
    {
        filtered.clear();
        const QString f = norm(edFilter->text());
        for (int g = 0; g < groups.size(); ++g) {
            if (!f.isEmpty()) {
                const Entry &e = all[groups[g].first()];
                if (!norm(e.name).contains(f) && !norm(e.artist).contains(f))
                    continue;
            }
            filtered.append(g);
        }
        page = 0;
        showPage();
    }

    void showPage()
    {
        building = true;
        tree->clear();
        const int from = page * kPageSize, to = qMin(filtered.size(), from + kPageSize);
        for (int fi = from; fi < to; ++fi) {
            const QVector<int> &g = groups[filtered[fi]];
            const Entry &h = all[g.first()];
            auto *parent = new QTreeWidgetItem(tree);
            parent->setText(0, QString("%1 — %2  (%3 BPM)  × %4").arg(h.name, h.artist).arg(h.tempo).arg(g.size()));
            parent->setFirstColumnSpanned(true);
            QFont bf = parent->font(0); bf.setBold(true); parent->setFont(0, bf);
            for (int idx : g) {
                const Entry &e = all[idx];
                auto *it = new QTreeWidgetItem(parent);
                it->setText(0, e.id);
                it->setText(1, e.type);
                it->setText(2, e.key);
                it->setText(3, QString::number(e.tempo));
                it->setText(4, e.path);
                it->setData(0, Qt::UserRole, idx);
                it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
                it->setCheckState(0, e.checked ? Qt::Checked : Qt::Unchecked);
            }
            parent->setExpanded(true);
        }
        building = false;
        const int pages = qMax(1, (filtered.size() + kPageSize - 1) / kPageSize);
        lblPage->setText(tr("หน้า %1 / %2  (%3 กลุ่ม)").arg(page + 1).arg(pages).arg(filtered.size()));
        updateCount();
    }

    void updateCount()
    {
        int n = 0;
        for (const Entry &e : all) if (e.checked && !e.gone) ++n;
        lblChecked->setText(tr("ติ๊กไว้ %1 เพลง").arg(n));
    }

    // tick all but one per group (in the filtered groups), keeping the preferred file type
    void autoSelect()
    {
        const QString keep = cbKeep->currentData().toString();
        for (int gi : filtered) {
            const QVector<int> &g = groups[gi];
            int keepIdx = g.first();
            if (!keep.isEmpty())
                for (int idx : g)
                    if (all[idx].type == keep) { keepIdx = idx; break; }
            for (int idx : g)
                all[idx].checked = (idx != keepIdx);
        }
        showPage();
    }

    QString filePathOf(const Entry &e) const
    {
        const QString t = e.type;
        QStringList cands;
        if (t == "NCN") cands << db->ncnPath() + e.path;
        else if (t == "EMK") {
            cands << db->emkPath() + e.path << db->emkPath() + "/" + e.path
                  << QFileInfo(db->ncnPath()).dir().absoluteFilePath("EMK") + e.path;
        }
        else if (t == "SIB") cands << db->sibPath() + e.path << db->sibPath() + "/" + e.path;
        else if (t == "HNK") cands << db->hnkPath() + e.path << db->hnkPath() + "/" + e.path;
        else if (t == "KAR") cands << db->karPath() + e.path;
        else if (t == "VIDEO") cands << db->videoPath() + e.path;
        else if (t == "MP3") cands << db->mp3Path() + e.path;
        for (const QString &c : cands)
            if (QFile::exists(c)) return c;
        return QString();
    }

    void deleteChecked()
    {
        QVector<int> sel;
        for (int i = 0; i < all.size(); ++i)
            if (all[i].checked && !all[i].gone) sel.append(i);
        if (sel.isEmpty()) {
            QMessageBox::information(this, windowTitle(), tr("ยังไม่ได้ติ๊กเพลงที่จะลบ"));
            return;
        }
        // groups that would lose every copy
        int wholeGroups = 0;
        for (const QVector<int> &g : groups) {
            bool allChecked = true;
            for (int idx : g) if (!all[idx].checked) { allChecked = false; break; }
            if (allChecked) ++wholeGroups;
        }
        QString msg = tr("ลบ %1 เพลงออกจากรายการเพลง").arg(sel.size());
        if (chkFiles->isChecked()) msg += tr("\nและย้ายไฟล์ไปถังขยะ");
        if (wholeGroups > 0)
            msg += tr("\n\nคำเตือน: มี %1 กลุ่มที่ติ๊กครบทุกเพลง เพลงนั้นจะไม่เหลือเลย").arg(wholeGroups);
        if (QMessageBox::question(this, windowTitle(), msg + tr("\n\nยืนยัน?")) != QMessageBox::Yes)
            return;

        QProgressDialog prog(tr("กำลังลบ..."), tr("หยุด"), 0, sel.size(), this);
        prog.setWindowModality(Qt::WindowModal);
        prog.setMinimumDuration(300);
        QSqlDatabase *sdb = db->database();
        sdb->transaction();
        QSqlQuery q(*sdb);
        q.prepare("DELETE FROM songs WHERE id = ? AND songtype = ? AND path = ?");
        int done = 0, trashFailed = 0;
        for (int n = 0; n < sel.size(); ++n) {
            if (prog.wasCanceled()) break;
            Entry &e = all[sel[n]];
            if (chkFiles->isChecked()) {
                const QString f = filePathOf(e);
                if (!f.isEmpty()) {
                    if (!QFile::moveToTrash(f)) ++trashFailed;
                    if (e.type == "NCN") {          // its cursor / lyrics files too
                        const QString cur = SongDatabase::getCurFilePath(f), lyr = SongDatabase::getLyrFilePath(f);
                        if (!cur.isEmpty() && QFile::exists(cur)) QFile::moveToTrash(cur);
                        if (!lyr.isEmpty() && QFile::exists(lyr)) QFile::moveToTrash(lyr);
                    }
                }
            }
            q.bindValue(0, e.id);
            q.bindValue(1, e.type);
            q.bindValue(2, e.path);
            if (q.exec()) { e.gone = true; e.checked = false; ++done; }
            if ((n & 63) == 0) prog.setValue(n);
        }
        q.finish();
        sdb->commit();
        prog.setValue(sel.size());
        QString res = tr("ลบแล้ว %1 เพลง").arg(done);
        if (trashFailed) res += tr("\nย้ายไฟล์ไปถังขยะไม่ได้ %1 ไฟล์ (ไฟล์อาจถูกใช้งานอยู่)").arg(trashFailed);
        QMessageBox::information(this, windowTitle(), res);
        rebuild();
    }

    SongDatabase *db;
    QLabel *lblSummary, *lblPage, *lblChecked;
    QLineEdit *edFilter;
    QCheckBox *chkKey, *chkFiles;
    QComboBox *cbScope, *cbKeep;
    QTreeWidget *tree;
    QVector<Entry> all;
    QVector<QVector<int>> groups;
    QVector<int> filtered;
    int page = 0;
    bool building = false;
};

#endif // DUPLICATESONGSDIALOG_H
