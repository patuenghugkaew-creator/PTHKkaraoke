#include "MedleyDialog.h"
#include "ui_MedleyDialog.h"

#include "Config.h"
#include "Midi/MidiPlayer.h"
#include "Midi/MidiSequencer.h"

#include <QKeyEvent>
#include <QSettings>
#include <QListView>

MedleyDialog::MedleyDialog(QWidget *parent, MidiPlayer *player) :
    QDialog(parent),
    ui(new Ui::MedleyDialog),
    _player(player)
{
    ui->setupUi(this);

    // Sync volume slider and spinbox
    connect(ui->sliderFillVol, &QSlider::valueChanged, ui->spinFillVol, &QSpinBox::setValue);
    connect(ui->spinFillVol, QOverload<int>::of(&QSpinBox::valueChanged), ui->sliderFillVol, &QSlider::setValue);

    // Connect all parameter changes to update preview & mark Custom preset
    connect(ui->spinBpm, QOverload<int>::of(&QSpinBox::valueChanged), this, &MedleyDialog::onParamChanged);
    connect(ui->comboTempoMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MedleyDialog::onParamChanged);
    connect(ui->chkDrumFill, &QCheckBox::toggled, this, [this](bool checked) {
        ui->spinBars->setEnabled(checked);
        ui->comboPattern->setEnabled(checked);
        ui->sliderFillVol->setEnabled(checked);
        ui->spinFillVol->setEnabled(checked);
        onParamChanged();
    });
    connect(ui->spinBars, QOverload<int>::of(&QSpinBox::valueChanged), this, &MedleyDialog::onParamChanged);
    connect(ui->comboPattern, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MedleyDialog::onParamChanged);
    connect(ui->sliderFillVol, &QSlider::valueChanged, this, &MedleyDialog::onParamChanged);
    connect(ui->chkPreserveEffects, &QCheckBox::toggled, this, &MedleyDialog::onParamChanged);

    connect(ui->chkAutoCutOutro, &QCheckBox::toggled, this, [this](bool checked) {
        ui->spinWaitBars->setEnabled(checked);
        onParamChanged();
    });
    connect(ui->spinWaitBars, QOverload<int>::of(&QSpinBox::valueChanged), this, &MedleyDialog::onParamChanged);
    connect(ui->chkQuantizeBar, &QCheckBox::toggled, this, &MedleyDialog::onParamChanged);
    connect(ui->chkSkipIntro, &QCheckBox::toggled, this, [this](bool checked) {
        ui->spinIntroBars->setEnabled(checked);
        onParamChanged();
    });
    connect(ui->spinIntroBars, QOverload<int>::of(&QSpinBox::valueChanged), this, &MedleyDialog::onParamChanged);
    connect(ui->spinPreBuffer, QOverload<int>::of(&QSpinBox::valueChanged), this, &MedleyDialog::onParamChanged);

    connect(ui->spinCrossfadeSec, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MedleyDialog::onParamChanged);
    connect(ui->comboCrossfadeCurve, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MedleyDialog::onParamChanged);
    connect(ui->spinSilenceDb, QOverload<int>::of(&QSpinBox::valueChanged), this, &MedleyDialog::onParamChanged);
    connect(ui->chkAudioAutoCue, &QCheckBox::toggled, this, &MedleyDialog::onParamChanged);
    connect(ui->chkAudioNormalize, &QCheckBox::toggled, this, &MedleyDialog::onParamChanged);

    for (QComboBox *cb : findChildren<QComboBox*>()) {
        cb->setView(new QListView(cb));
    }

    loadSettings();

    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
}

MedleyDialog::~MedleyDialog()
{
    delete ui;
}

void MedleyDialog::loadSettings()
{
    _isUpdatingUi = true;

    QSettings settings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    settings.beginGroup("Medley");

    int preset = settings.value("Preset", 0).toInt();
    int bpm = settings.value("BPM", 120).toInt();
    int tempoMode = settings.value("TempoMode", 0).toInt();
    bool drumFill = settings.value("DrumFill", true).toBool();
    int transitionBars = qMax(1, settings.value("TransitionBars", 1).toInt());
    int fillPattern = settings.value("FillPattern", 0).toInt();
    int fillVolume = settings.value("FillVolume", 100).toInt();
    bool preserveEffects = settings.value("PreserveEffectsTail", true).toBool();

    bool autoCutOutro = settings.value("AutoCutOutro", true).toBool();
    int waitBars = settings.value("WaitBars", 1).toInt();
    bool quantizeBar = settings.value("QuantizeBar", true).toBool();
    bool skipIntro = settings.value("SkipIntro", true).toBool();
    int introLeadInBars = settings.value("IntroLeadInBars", 2).toInt();
    int preBufferSec = settings.value("PreBufferSec", 20).toInt();

    double crossfadeSec = settings.value("AudioCrossfadeSec", 3.0).toDouble();
    int crossfadeCurve = settings.value("AudioCurve", 0).toInt();
    int silenceDb = settings.value("AudioThresholdDb", -32).toInt();
    bool audioAutoCue = settings.value("AudioAutoCue", true).toBool();
    bool audioNormalize = settings.value("AudioNormalize", false).toBool();

    settings.endGroup();

    if (_player) {
        if (!_player->isPlayerStopped() && _player->currentBpm() >= 40 && _player->currentBpm() <= 240)
            bpm = _player->currentBpm();
        else if (_player->medleyBPM() >= 40)
            bpm = _player->medleyBPM();

        tempoMode = _player->medleyTempoMode();
        if (_player->medleyTransitionBars() > 0) {
            transitionBars = _player->medleyTransitionBars();
            drumFill = true;
        }
        fillPattern = _player->medleyFillPattern();
        fillVolume = _player->medleyFillVolume();
        preserveEffects = _player->medleyPreserveEffectsTail();

        autoCutOutro = _player->medleyAutoCutOutro();
        waitBars = _player->medleyWaitBars();
        quantizeBar = _player->medleyQuantizeBar();
        skipIntro = _player->medleySkipIntro();
        introLeadInBars = _player->medleyIntroLeadInBars();
        preBufferSec = _player->medleyPreBufferSec();

        crossfadeSec = _player->medleyAudioCrossfadeSec();
        crossfadeCurve = _player->medleyAudioCurve();
        silenceDb = _player->medleyAudioThresholdDb();
        audioAutoCue = _player->medleyAudioAutoCue();
        audioNormalize = _player->medleyAudioNormalize();
        preset = _player->medleyPreset();
    }

    ui->spinBpm->setValue(qBound(40, bpm, 250));
    ui->comboTempoMode->setCurrentIndex(qBound(0, tempoMode, ui->comboTempoMode->count() - 1));
    ui->chkDrumFill->setChecked(drumFill);
    ui->spinBars->setValue(qBound(1, transitionBars, 4));
    ui->comboPattern->setCurrentIndex(qBound(0, fillPattern, ui->comboPattern->count() - 1));
    ui->sliderFillVol->setValue(qBound(50, fillVolume, 130));
    ui->spinFillVol->setValue(qBound(50, fillVolume, 130));
    ui->chkPreserveEffects->setChecked(preserveEffects);

    ui->chkAutoCutOutro->setChecked(autoCutOutro);
    ui->spinWaitBars->setValue(qBound(0, waitBars, 8));
    ui->chkQuantizeBar->setChecked(quantizeBar);
    ui->chkSkipIntro->setChecked(skipIntro);
    ui->spinIntroBars->setValue(qBound(1, introLeadInBars, 8));
    ui->spinIntroBars->setEnabled(skipIntro);
    ui->spinPreBuffer->setValue(qBound(10, preBufferSec, 60));

    ui->spinCrossfadeSec->setValue(qBound(0.5, crossfadeSec, 10.0));
    ui->comboCrossfadeCurve->setCurrentIndex(qBound(0, crossfadeCurve, ui->comboCrossfadeCurve->count() - 1));
    ui->spinSilenceDb->setValue(qBound(-45, silenceDb, -18));
    ui->chkAudioAutoCue->setChecked(audioAutoCue);
    ui->chkAudioNormalize->setChecked(audioNormalize);

    ui->comboPreset->setCurrentIndex(qBound(0, preset, ui->comboPreset->count() - 1));

    ui->spinBars->setEnabled(drumFill);
    ui->comboPattern->setEnabled(drumFill);
    ui->sliderFillVol->setEnabled(drumFill);
    ui->spinFillVol->setEnabled(drumFill);
    ui->spinWaitBars->setEnabled(autoCutOutro);

    _isUpdatingUi = false;

    updatePreview();
}

void MedleyDialog::saveSettings()
{
    const int preset = ui->comboPreset->currentIndex();
    const int bpm = ui->spinBpm->value();
    const int tempoMode = ui->comboTempoMode->currentIndex();
    const bool drumFill = ui->chkDrumFill->isChecked();
    const int spinBarsVal = ui->spinBars->value();
    const int activeTransitionBars = drumFill ? spinBarsVal : 0;
    const int fillPattern = ui->comboPattern->currentIndex();
    const int fillVolume = ui->spinFillVol->value();
    const bool preserveEffects = ui->chkPreserveEffects->isChecked();

    const bool autoCutOutro = ui->chkAutoCutOutro->isChecked();
    const int waitBars = ui->spinWaitBars->value();
    const bool quantizeBar = ui->chkQuantizeBar->isChecked();
    const bool skipIntro = ui->chkSkipIntro->isChecked();
    const int introLeadInBars = ui->spinIntroBars->value();
    const int preBufferSec = ui->spinPreBuffer->value();

    const double crossfadeSec = ui->spinCrossfadeSec->value();
    const int crossfadeCurve = ui->comboCrossfadeCurve->currentIndex();
    const int silenceDb = ui->spinSilenceDb->value();
    const bool audioAutoCue = ui->chkAudioAutoCue->isChecked();
    const bool audioNormalize = ui->chkAudioNormalize->isChecked();

    if (_player) {
        _player->setMedleyPreset(preset);
        _player->setMedleyBPM(bpm);
        _player->setMedleyTempoMode(tempoMode);
        _player->setMedleyTransitionBars(activeTransitionBars);
        _player->setMedleyFillPattern(fillPattern);
        _player->setMedleyFillVolume(fillVolume);
        _player->setMedleyPreserveEffectsTail(preserveEffects);

        _player->setMedleyAutoCutOutro(autoCutOutro);
        _player->setMedleyWaitBars(waitBars);
        _player->setMedleyQuantizeBar(quantizeBar);
        _player->setMedleySkipIntro(skipIntro);
        _player->setMedleyIntroLeadInBars(introLeadInBars);
        _player->setMedleyPreBufferSec(preBufferSec);

        _player->setMedleyAudioCrossfadeSec(crossfadeSec);
        _player->setMedleyAudioCurve(crossfadeCurve);
        _player->setMedleyAudioThresholdDb(silenceDb);
        _player->setMedleyAudioAutoCue(audioAutoCue);
        _player->setMedleyAudioNormalize(audioNormalize);

        _player->setUseMedley(true);

        if (_player->midiSequencerTemp() != nullptr) {
            _player->midiSequencerTemp()->setMedleyTransition(true,
                                                             activeTransitionBars,
                                                             fillPattern,
                                                             bpm,
                                                             fillVolume,
                                                             tempoMode,
                                                             preserveEffects);
        }
    }

    QSettings settings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    settings.beginGroup("Medley");
    settings.setValue("Enable", true);
    settings.setValue("Preset", preset);
    settings.setValue("BPM", bpm);
    settings.setValue("TempoMode", tempoMode);
    settings.setValue("DrumFill", drumFill);
    settings.setValue("TransitionBars", spinBarsVal);
    settings.setValue("FillPattern", fillPattern);
    settings.setValue("FillVolume", fillVolume);
    settings.setValue("PreserveEffectsTail", preserveEffects);

    settings.setValue("AutoCutOutro", autoCutOutro);
    settings.setValue("WaitBars", waitBars);
    settings.setValue("QuantizeBar", quantizeBar);
    settings.setValue("SkipIntro", skipIntro);
    settings.setValue("IntroLeadInBars", introLeadInBars);
    settings.setValue("PreBufferSec", preBufferSec);

    settings.setValue("AudioCrossfadeSec", crossfadeSec);
    settings.setValue("AudioCurve", crossfadeCurve);
    settings.setValue("AudioThresholdDb", silenceDb);
    settings.setValue("AudioAutoCue", audioAutoCue);
    settings.setValue("AudioNormalize", audioNormalize);
    settings.endGroup();
}

void MedleyDialog::updatePreview()
{
    const bool autoCutOutro = ui->chkAutoCutOutro->isChecked();
    const int waitBars = autoCutOutro ? ui->spinWaitBars->value() : 0;
    const bool quantizeBar = ui->chkQuantizeBar->isChecked();
    const bool skipIntro = ui->chkSkipIntro->isChecked();
    const int introBars = skipIntro ? ui->spinIntroBars->value() : 0;
    const int fillBars = ui->chkDrumFill->isChecked() ? ui->spinBars->value() : 0;
    const int fillPattern = ui->comboPattern->currentIndex();
    const int fillVolume = ui->spinFillVol->value();
    const int tempoMode = ui->comboTempoMode->currentIndex();
    const int bpm = ui->spinBpm->value();
    const double crossfadeSec = ui->spinCrossfadeSec->value();
    const int crossfadeCurve = ui->comboCrossfadeCurve->currentIndex();
    const int activeTab = ui->tabWidget->currentIndex();

    ui->previewWidget->setMedleyParams(waitBars,
                                      quantizeBar,
                                      skipIntro,
                                      autoCutOutro,
                                      fillBars,
                                      fillPattern,
                                      fillVolume,
                                      tempoMode,
                                      bpm,
                                      crossfadeSec,
                                      crossfadeCurve,
                                      activeTab,
                                      introBars);
}

void MedleyDialog::applyPreset(int index)
{
    _isUpdatingUi = true;

    switch (index) {
    case 0: // Party & Variety
        ui->chkAutoCutOutro->setChecked(true);
        ui->spinWaitBars->setValue(1);
        ui->chkQuantizeBar->setChecked(true);
        ui->chkSkipIntro->setChecked(true);
        ui->spinIntroBars->setValue(2);
        ui->spinPreBuffer->setValue(20);

        ui->comboTempoMode->setCurrentIndex(0); // Lock BPM
        ui->chkDrumFill->setChecked(true);
        ui->spinBars->setValue(1);
        ui->comboPattern->setCurrentIndex(1);   // Sam-Cha
        ui->sliderFillVol->setValue(110);
        ui->chkPreserveEffects->setChecked(true);

        ui->spinCrossfadeSec->setValue(2.5);
        ui->comboCrossfadeCurve->setCurrentIndex(0); // Equal-Power
        ui->spinSilenceDb->setValue(-32);
        ui->chkAudioAutoCue->setChecked(true);
        ui->chkAudioNormalize->setChecked(true);
        break;

    case 1: // Restaurant & Lounge
        ui->chkAutoCutOutro->setChecked(true);
        ui->spinWaitBars->setValue(2);
        ui->chkQuantizeBar->setChecked(true);
        ui->chkSkipIntro->setChecked(true);
        ui->spinIntroBars->setValue(4);
        ui->spinPreBuffer->setValue(25);

        ui->comboTempoMode->setCurrentIndex(2); // Tempo Ramp / Morph
        ui->chkDrumFill->setChecked(true);
        ui->spinBars->setValue(2);
        ui->comboPattern->setCurrentIndex(0);   // Standard Rock/Pop
        ui->sliderFillVol->setValue(90);
        ui->chkPreserveEffects->setChecked(true);

        ui->spinCrossfadeSec->setValue(4.0);
        ui->comboCrossfadeCurve->setCurrentIndex(0); // Equal-Power
        ui->spinSilenceDb->setValue(-36);
        ui->chkAudioAutoCue->setChecked(true);
        ui->chkAudioNormalize->setChecked(true);
        break;

    case 2: // Live Band
        ui->chkAutoCutOutro->setChecked(true);
        ui->spinWaitBars->setValue(1);
        ui->chkQuantizeBar->setChecked(true);
        ui->chkSkipIntro->setChecked(true);
        ui->spinIntroBars->setValue(2);
        ui->spinPreBuffer->setValue(30);

        ui->comboTempoMode->setCurrentIndex(0); // Lock BPM
        ui->chkDrumFill->setChecked(true);
        ui->spinBars->setValue(1);
        ui->comboPattern->setCurrentIndex(0);   // Standard Rock
        ui->sliderFillVol->setValue(115);
        ui->chkPreserveEffects->setChecked(true);

        ui->spinCrossfadeSec->setValue(2.0);
        ui->comboCrossfadeCurve->setCurrentIndex(0);
        ui->spinSilenceDb->setValue(-30);
        ui->chkAudioAutoCue->setChecked(true);
        ui->chkAudioNormalize->setChecked(false);
        break;

    case 3: // Custom
    default:
        break;
    }

    ui->spinBars->setEnabled(ui->chkDrumFill->isChecked());
    ui->comboPattern->setEnabled(ui->chkDrumFill->isChecked());
    ui->sliderFillVol->setEnabled(ui->chkDrumFill->isChecked());
    ui->spinFillVol->setEnabled(ui->chkDrumFill->isChecked());
    ui->spinWaitBars->setEnabled(ui->chkAutoCutOutro->isChecked());
    ui->spinIntroBars->setEnabled(ui->chkSkipIntro->isChecked());

    _isUpdatingUi = false;
    updatePreview();
}

void MedleyDialog::on_comboPreset_currentIndexChanged(int index)
{
    if (!_isUpdatingUi && index >= 0 && index < 3) {
        applyPreset(index);
    }
}

void MedleyDialog::on_tabWidget_currentChanged(int)
{
    updatePreview();
}

void MedleyDialog::onParamChanged()
{
    if (_isUpdatingUi)
        return;

    // If user changes a parameter manually, switch preset combo to "Custom"
    if (ui->comboPreset->currentIndex() != 3) {
        _isUpdatingUi = true;
        ui->comboPreset->setCurrentIndex(3);
        _isUpdatingUi = false;
    }

    updatePreview();
}

void MedleyDialog::on_btnReset_clicked()
{
    applyPreset(0); // Reset to Party / Variety preset
    ui->comboPreset->setCurrentIndex(0);
}

void MedleyDialog::on_btnOk_clicked()
{
    saveSettings();
    accept();
}

void MedleyDialog::on_btnCancel_clicked()
{
    reject();
}

void MedleyDialog::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return:
        on_btnOk_clicked();
        break;
    case Qt::Key_Escape:
        reject();
        break;
    default:
        QDialog::keyPressEvent(e);
        break;
    }
}
