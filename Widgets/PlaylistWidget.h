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

    // see-through and rounded over the video (like the song search bar)
    void applyOverlayStyle();
    // the height that shows every song (up to `maxHeight`)
    int fittingHeight(int maxHeight) const;

signals:
    void rowClicked(int row);
    void rowDoubleClicked(int row);             // play this song

private:
    Ui::PlaylistWidget *ui;
};

#endif // PLAYLISTWIDGET_H
