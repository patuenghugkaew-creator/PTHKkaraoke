#include "SongListsDialog.h"

#include <QTabWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QKeyEvent>
#include <QMessageBox>

SongListsDialog::SongListsDialog(QWidget *parent, SongDatabase *db) :
    QDialog(parent), db(db)
{
    setWindowTitle(tr("รายการเพลง (เพลงโปรด / เล่นล่าสุด / เพลงฮิต)"));
    resize(660, 540);

    tabs = new QTabWidget(this);

    const QList<QPair<QString, SongDatabase::ListKind>> kinds = {
        { tr("เพลงโปรด"),          SongDatabase::ListKind::Favorites },
        { tr("เล่นล่าสุด"),         SongDatabase::ListKind::History },
        { tr("เพลงฮิต (เล่นบ่อย)"), SongDatabase::ListKind::Top },
    };

    for (const auto &k : kinds) {
        QListWidget *w = new QListWidget(tabs);
        QFont f = w->font();
        f.setPointSize(f.pointSize() + 3);
        w->setFont(f);
        tabKinds.append(k.second);
        lists.append(db->songList(k.second));
        fill(w, lists.last());
        connect(w, &QListWidget::itemActivated, this, [this, w]() { pick(w); });
        tabs->addTab(w, k.first);
    }

    QLabel *hint = new QLabel(tr("วิธีเพิ่มเพลงโปรด: กดปุ่ม F2 ขณะกำลังค้นหาเพลงหรือขณะกำลังเล่นเพลงนั้น   |   กด Delete เพื่อลบเพลงที่เลือก"), this);
    hint->setStyleSheet("color: #888; font-size: 12px;");

    QHBoxLayout *btnRow = new QHBoxLayout();
    QPushButton *btnRemove = new QPushButton(tr("ลบเพลงที่เลือก (Del)"), this);
    QPushButton *btnClear  = new QPushButton(tr("ล้างทั้งหมดในหน้านี้..."), this);
    QPushButton *btnAdd    = new QPushButton(tr("เพิ่มเข้าคิว"), this);
    QPushButton *btnClose  = new QPushButton(tr("ปิด"), this);
    btnAdd->setDefault(true);

    connect(btnRemove, &QPushButton::clicked, this, [this]() { removeCurrent(); });
    connect(btnClear,  &QPushButton::clicked, this, [this]() { clearCurrentTab(); });
    connect(btnAdd,    &QPushButton::clicked, this, [this]() {
        pick(qobject_cast<QListWidget *>(tabs->currentWidget()));
    });
    connect(btnClose,  &QPushButton::clicked, this, &QDialog::reject);

    btnRow->addWidget(btnRemove);
    btnRow->addWidget(btnClear);
    btnRow->addStretch();
    btnRow->addWidget(btnAdd);
    btnRow->addWidget(btnClose);

    QVBoxLayout *l = new QVBoxLayout(this);
    l->addWidget(tabs);
    l->addWidget(hint);
    l->addLayout(btnRow);

    // open on the favorites tab, or on "recently played" while there are no favorites yet
    tabs->setCurrentIndex(lists[0].isEmpty() && !lists[1].isEmpty() ? 1 : 0);
    tabs->currentWidget()->setFocus();
}

void SongListsDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete) {
        removeCurrent();
        return;
    }
    QDialog::keyPressEvent(event);
}

void SongListsDialog::refreshTab(int idx)
{
    if (idx < 0 || idx >= tabKinds.count())
        return;
    QListWidget *w = qobject_cast<QListWidget *>(tabs->widget(idx));
    if (!w)
        return;
    w->clear();
    lists[idx] = db->songList(tabKinds[idx]);
    fill(w, lists[idx]);
}

void SongListsDialog::removeCurrent()
{
    int idx = tabs->currentIndex();
    QListWidget *w = qobject_cast<QListWidget *>(tabs->currentWidget());
    if (!w || idx < 0 || idx >= lists.count())
        return;
    int row = w->currentRow();
    if (row < 0 || row >= lists[idx].count())
        return;

    Song s = lists[idx][row];
    db->removeFromList(tabKinds[idx], s);
    if (tabKinds[idx] == SongDatabase::ListKind::Favorites) {
        refreshTab(idx);
    } else {
        // History and Top share the history table
        refreshTab(1);
        refreshTab(2);
    }
}

void SongListsDialog::clearCurrentTab()
{
    int idx = tabs->currentIndex();
    if (idx < 0 || idx >= lists.count() || lists[idx].isEmpty())
        return;

    QString tabName = tabs->tabText(idx);
    if (QMessageBox::question(this, tr("ยืนยันการล้างรายการ"),
                              tr("ต้องการล้างรายการในหน้า \"%1\" ทั้งหมดใช่หรือไม่?").arg(tabName),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    db->clearList(tabKinds[idx]);
    if (tabKinds[idx] == SongDatabase::ListKind::Favorites) {
        refreshTab(idx);
    } else {
        refreshTab(1);
        refreshTab(2);
    }
}

void SongListsDialog::fill(QListWidget *w, const QList<Song> &songs)
{
    if (songs.isEmpty()) {
        QListWidgetItem *it = new QListWidgetItem(tr("(ยังไม่มีเพลง)"), w);
        it->setFlags(Qt::NoItemFlags);
        return;
    }
    for (Song s : songs) {
        QString text = s.id() + "   " + s.name();
        if (!s.artist().isEmpty())
            text += "  -  " + s.artist();
        text += "   [" + s.songType() + "]";
        new QListWidgetItem(text, w);
    }
    w->setCurrentRow(0);
}

void SongListsDialog::pick(QListWidget *w)
{
    if (w == nullptr)
        return;
    int list = tabs->indexOf(w);
    int row = w->currentRow();
    if (list < 0 || row < 0 || row >= lists[list].count())
        return;
    selected = lists[list][row];
    picked = true;
    accept();
}
