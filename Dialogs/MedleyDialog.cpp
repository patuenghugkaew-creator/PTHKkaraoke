#include "MedleyDialog.h"
#include "ui_MedleyDialog.h"

#include "Midi/MidiPlayer.h"
#include "Midi/MidiSequencer.h"

#include <QKeyEvent>
#include <QSettings>


MedleyDialog::MedleyDialog(QWidget *parent, MidiPlayer *player) :
    QDialog(parent),
    ui(new Ui::MedleyDialog)
{
    ui->setupUi(this);

    _player = player;

    QSettings settings("PTHK", "Karaoke");
    const int defBpm = settings.value("MedleyBPM", 120).toInt();
    const int defBars = settings.value("MedleyTransitionBars", 1).toInt();
    const int defPattern = settings.value("MedleyFillPattern", 0).toInt();

    if (_player && !_player->isPlayerStopped() && _player->currentBpm() >= 40 && _player->currentBpm() <= 240)
        ui->spinBpm->setValue(_player->currentBpm());
    else if (_player && _player->medleyBPM() >= 40)
        ui->spinBpm->setValue(_player->medleyBPM());
    else
        ui->spinBpm->setValue(defBpm);

    if (_player) {
        ui->spinBars->setValue(_player->medleyTransitionBars());
        ui->comboPattern->setCurrentIndex(qBound(0, _player->medleyFillPattern(), ui->comboPattern->count() - 1));
    } else {
        ui->spinBars->setValue(defBars);
        ui->comboPattern->setCurrentIndex(qBound(0, defPattern, ui->comboPattern->count() - 1));
    }

    ui->spinBpm->selectAll();
    ui->spinBpm->setFocus();

    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
}

MedleyDialog::~MedleyDialog()
{
    delete ui;
}

void MedleyDialog::keyPressEvent(QKeyEvent *e)
{
    QWidget::keyPressEvent(e);

    switch (e->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return:
        if (focusWidget() == ui->spinBpm || focusWidget() == ui->spinBars || focusWidget() == ui->comboPattern)
            on_btnOk_clicked();
        break;
    case Qt::Key_Escape:
        close();
        break;
    default:
        break;
    }
}

void MedleyDialog::on_btnOk_clicked()
{
    const int bpm = ui->spinBpm->value();
    const int bars = ui->spinBars->value();
    const int pattern = ui->comboPattern->currentIndex();

    if (_player) {
        _player->setMedleyBPM(bpm);
        _player->setMedleyTransitionBars(bars);
        _player->setMedleyFillPattern(pattern);
        _player->setUseMedley(true);
        if (_player->midiSequencerTemp() != nullptr) {
            _player->midiSequencerTemp()->setMedleyTransition(true, bars, pattern, bpm);
        }
    }

    QSettings settings("PTHK", "Karaoke");
    settings.setValue("MedleyBPM", bpm);
    settings.setValue("MedleyTransitionBars", bars);
    settings.setValue("MedleyFillPattern", pattern);

    close();
}

void MedleyDialog::on_btnCancel_clicked()
{
    close();
}
