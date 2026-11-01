#include "PlaylistWidget.h"
#include "ui_PlaylistWidget.h"

PlaylistWidget::PlaylistWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PlaylistWidget)
{
    ui->setupUi(this);

    #ifdef __linux__
    { // UI Font in linux
        QString fontName = "TH Sarabun New Bold";
        QFont f = ui->lbPlaylist->font();
        f.setBold(true);
        f.setFamily(fontName);
        f.setPointSize(f.pointSize() - 10);

        ui->lbPlaylist->setFont(f);

        f.setPointSize(f.pointSize() - 2);
        ui->tableWidget->setFont(f);
    }
    #endif

    QHeaderView *header = ui->tableWidget->horizontalHeader();
    header->setMinimumSectionSize(40);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::Stretch);
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);

    connect(ui->tableWidget, &QTableWidget::cellClicked, this, [this](int row, int) { emit rowClicked(row); });
    connect(ui->tableWidget, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { emit rowDoubleClicked(row); });
}

void PlaylistWidget::applyOverlayStyle()
{
    ui->framePlaylist->setStyleSheet(
        "#framePlaylist { border: none; border-radius: 12px; background-color: rgba(16, 18, 24, 160); }");
    ui->frame->setStyleSheet(
        "#frame { border: none; border-radius: 8px; background-color: rgba(0, 129, 194, 190); }"
        "#frame QLabel { background: transparent; color: rgb(255, 244, 110); }");
    ui->tableWidget->setStyleSheet(
        "QTableView { color: rgb(240, 240, 240); border: none; background: transparent;"
        " gridline-color: rgba(255, 255, 255, 35); }"
        "QTableView::item { padding: 0 6px; background: transparent; }"
        "QTableView::item:selected { background-color: rgba(255, 255, 255, 210); color: rgb(40, 40, 40); }");
    ui->tableWidget->viewport()->setAutoFillBackground(false);
    ui->tableWidget->setAutoFillBackground(false);
}

int PlaylistWidget::fittingHeight(int maxHeight) const
{
    const QTableWidget *t = ui->tableWidget;
    int rows = 0;
    for (int r = 0; r < t->rowCount(); ++r)
        rows += t->rowHeight(r);
    if (t->rowCount() == 0)
        rows = t->verticalHeader()->defaultSectionSize();
    const QMargins m = ui->framePlaylist->layout() ? ui->framePlaylist->layout()->contentsMargins() : QMargins();
    const int spacing = ui->framePlaylist->layout() ? ui->framePlaylist->layout()->spacing() : 0;
    const int h = ui->frame->sizeHint().height() + spacing + rows + t->frameWidth() * 2 + m.top() + m.bottom() + 6;
    return qMin(h, maxHeight);
}

// one colour per kind of song, so the queue can be read at a glance
QColor PlaylistWidget::typeColor(const QString &type)
{
    if (type == "YOUTUBE") return QColor("#f87171");   // red
    if (type == "VIDEO")   return QColor("#c084fc");   // purple
    if (type == "MP3")     return QColor("#34d399");   // green
    if (type == "SIB")     return QColor("#fbbf24");   // amber
    if (type == "EMK")     return QColor("#fb923c");   // orange
    return QColor("#60a5fa");                          // MIDI (NCN, KAR, HNK...): blue
}

PlaylistWidget::~PlaylistWidget()
{
    delete ui;
}

void PlaylistWidget::setPlaylist(const QList<Song *> &songlist)
{
    for (int row=ui->tableWidget->rowCount()-1; row>=0; row--)
        ui->tableWidget->removeRow(row);

    for (Song* s : songlist)
        addSong(s);

    setCurrentRow(0);
}

void PlaylistWidget::addSong(Song *song)
{
    int rowCount = ui->tableWidget->rowCount();

    ui->tableWidget->insertRow(rowCount);
    ui->tableWidget->setItem(rowCount, 0, new QTableWidgetItem(QString::number(rowCount+1)));
    ui->tableWidget->setItem(rowCount, 1, new QTableWidgetItem(song->id()));
    ui->tableWidget->setItem(rowCount, 2, new QTableWidgetItem(song->detailWithoutIDType()));
    QTableWidgetItem *type = new QTableWidgetItem(song->songType());
    type->setTextAlignment(Qt::AlignCenter);
    type->setForeground(typeColor(song->songType()));
    ui->tableWidget->setItem(rowCount, 3, type);

    ui->tableWidget->item(rowCount, 0)->setTextAlignment(Qt::AlignHCenter|Qt::AlignVCenter);
}

void PlaylistWidget::updateDetail(int row, Song *song)
{
    ui->tableWidget->item(row, 2)->setText(song->detailWithoutIDType());
}

int PlaylistWidget::rowCount()
{
    return ui->tableWidget->rowCount();
}

int PlaylistWidget::currentRow()
{
    if (ui->tableWidget->rowCount() > 0)
        return ui->tableWidget->currentRow();
    else
        return -1;
}

void PlaylistWidget::setCurrentRow(int row)
{
    if (row >= ui->tableWidget->rowCount())
        return;

    ui->tableWidget->selectRow(row);
}

void PlaylistWidget::removeRow(int row)
{
    if (row >= ui->tableWidget->rowCount())
        return;

    ui->tableWidget->removeRow(row);

    for (int i=row; i<ui->tableWidget->rowCount(); i++)
    {
        ui->tableWidget->item(i, 0)->setText(QString::number(i+1));
    }

    if (row == 0)
        ui->tableWidget->selectRow(0);
    else if (row == ui->tableWidget->rowCount())
        ui->tableWidget->selectRow(row-1);
    else
        ui->tableWidget->selectRow(row);
}

void PlaylistWidget::swapRow(int row, int toRow)
{
    if (row >= ui->tableWidget->rowCount())
        return;

    if (toRow >= ui->tableWidget->rowCount())
        return;

    // row item
    QTableWidgetItem *item1 = ui->tableWidget->takeItem(row, 1);
    QTableWidgetItem *item2 = ui->tableWidget->takeItem(row, 2);
    QTableWidgetItem *item3 = ui->tableWidget->takeItem(row, 3);

    // set new row data
    ui->tableWidget->setItem(row, 1, ui->tableWidget->takeItem(toRow, 1));
    ui->tableWidget->setItem(row, 2, ui->tableWidget->takeItem(toRow, 2));
    ui->tableWidget->setItem(row, 3, ui->tableWidget->takeItem(toRow, 3));

    // set toRow item data
    ui->tableWidget->setItem(toRow, 1, item1);
    ui->tableWidget->setItem(toRow, 2, item2);
    ui->tableWidget->setItem(toRow, 3, item3);

    ui->tableWidget->selectRow(row);
}
