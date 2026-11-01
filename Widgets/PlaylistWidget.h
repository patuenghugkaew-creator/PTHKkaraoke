#ifndef PLAYLISTWIDGET_H
#define PLAYLISTWIDGET_H

#include <QWidget>
#include <QColor>

#include "Song.h"

namespace Ui {
class PlaylistWidget;
}

class PlaylistWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PlaylistWidget(QWidget *parent = 0);
    ~PlaylistWidget();

    static QColor typeColor(const QString &type);

    void setPlaylist(const QList<Song*> &songlist);
    void addSong(Song *song);
    void updateDetail(int row, Song *song);

    int rowCount();
    int currentRow();
    void setCurrentRow(int row);

    void removeRow(int row);
    void swapRow(int row, int toRow);

signals:
    void rowClicked(int row);
    void rowDoubleClicked(int row);             // play this song

private:
    Ui::PlaylistWidget *ui;
};

#endif // PLAYLISTWIDGET_H
