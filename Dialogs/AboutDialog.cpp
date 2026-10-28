#include "AboutDialog.h"
#include "ui_AboutDialog.h"

#include "version.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFont>
#include <QTextBrowser>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QApplication>
#include <QSettings>
#include <QClipboard>
#include <QDialog>

AboutDialog::AboutDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutDialog)
{
    ui->setupUi(this);

    ui->lbVersion->setText(VER_FILEVERSION_STR);
    ui->lbQtVer->hide();
    ui->label_5->hide();
    ui->label_6->hide();
    ui->label_7->hide();
    ui->lbCopyright->setText(QString::fromUtf8(VER_LEGALCOPYRIGHT_STR));
    ui->lbCopyright->setWordWrap(true);
    adjustSize();
}

AboutDialog::~AboutDialog()
{
    delete ui;
}
