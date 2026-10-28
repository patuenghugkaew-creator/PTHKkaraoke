#ifndef SONGLISTSDIALOG_H
#define SONGLISTSDIALOG_H

// Favorites / recently played / most played songs (F3). Enter or double-click picks a song.

#include <QDialog>
#include <QList>

#include "Song.h"
#include "SongDatabase.h"

class QTabWidget;
class QListWidget;

class SongListsDialog : public QDialog
{
    Q_OBJECT

public:
    SongListsDialog(QWidget *parent, SongDatabase *db);

    bool hasSelection() const { return picked; }
    Song selectedSong() const { return selected; }

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void fill(QListWidget *w, const QList<Song> &songs);
    void pick(QListWidget *w);
    void removeCurrent();
    void clearCurrentTab();
    void refreshTab(int idx);

    SongDatabase *db;
    QTabWidget *tabs;
    QList<SongDatabase::ListKind> tabKinds;
    QList<QList<Song>> lists;
    Song selected;
    bool picked = false;
};

#endif // SONGLISTSDIALOG_H
