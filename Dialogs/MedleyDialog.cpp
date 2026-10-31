#include "MedleyDialog.h"
#include "Config.h"
#include "Midi/MidiPlayer.h"
#include "Midi/MidiSequencer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QPushButton>
#include <QKeyEvent>
#include <QSettings>
#include <QFrame>

MedleyDialog::MedleyDialog(QWidget *parent, MidiPlayer *player)
    : QDialog(parent), _player(player)
{
    setWindowTitle(tr("ตั้งค่าระบบเมดเล่ย์ (Medley Settings)"));
    setFixedSize(540, 520);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    setStyleSheet(
        "QDialog { background-color: #0d1117; color: #e6edf3; font-size: 10pt; }"
        "QFrame.card {"
        "  background-color: #161b22; border: 1px solid #30363d; border-radius: 8px; padding: 12px;"
        "}"
        "QLabel { color: #e6edf3; }"
        "QLabel.header { font-size: 12.5pt; font-weight: bold; color: #58a6ff; }"
        "QLabel.sub { font-size: 8.5pt; color: #8b949e; }"
        "QLabel.cardTitle { font-size: 10pt; font-weight: bold; color: #58a6ff; margin-bottom: 2px; }"
        "QLabel.fieldLabel { font-size: 9.5pt; font-weight: bold; color: #c9d1d9; }"
        "QComboBox, QSpinBox {"
        "  background-color: #21262d; color: #58a6ff; font-size: 10.5pt; font-weight: bold;"
        "  border: 1px solid #30363d; border-radius: 6px; padding: 3px 8px; min-height: 28px;"
        "}"
        "QComboBox:focus, QSpinBox:focus { border: 1px solid #58a6ff; background-color: #1c2128; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView {"
        "  background-color: #161b22; color: #e6edf3; selection-background-color: #1f6feb;"
        "  border: 1px solid #30363d; border-radius: 6px; padding: 4px;"
        "}"
        "QPushButton.segBtn {"
        "  background-color: #21262d; color: #c9d1d9; border: 1px solid #30363d;"
        "  border-radius: 6px; padding: 5px 12px; font-size: 9pt; min-height: 24px;"
        "}"
        "QPushButton.segBtn:hover { background-color: #30363d; color: #58a6ff; }"
        "QPushButton.segBtn[active=\"true\"] {"
        "  background-color: #1f6feb; color: #ffffff; font-weight: bold; border: 1px solid #58a6ff;"
        "}"
        "QPushButton.quick {"
        "  background-color: #21262d; color: #c9d1d9; border: 1px solid #30363d;"
        "  border-radius: 4px; padding: 3px 9px; font-size: 8.5pt; min-height: 20px;"
        "}"
        "QPushButton.quick:hover { background-color: #30363d; color: #58a6ff; border: 1px solid #58a6ff; }"
        "QSlider::groove:horizontal { height: 6px; background-color: #30363d; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background-color: #1f6feb; border-radius: 3px; }"
        "QSlider::handle:horizontal {"
        "  background-color: #58a6ff; width: 14px; height: 14px; margin: -4px 0; border-radius: 7px;"
        "}"
        "QSlider::handle:horizontal:hover { background-color: #79c0ff; }"
        "QCheckBox { color: #e6edf3; font-size: 9pt; spacing: 6px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 3px; border: 1px solid #30363d; background-color: #21262d; }"
        "QCheckBox::indicator:checked { background-color: #1f6feb; border-color: #58a6ff; }"
        "QPushButton#btnOk {"
        "  background-color: #1f6feb; color: #ffffff; font-weight: bold; border-radius: 6px;"
        "  padding: 8px 22px; border: 1px solid #388bfd; min-width: 90px;"
        "}"
        "QPushButton#btnOk:hover { background-color: #388bfd; }"
        "QPushButton#btnCancel, QPushButton#btnOff, QPushButton#btnReset {"
        "  background-color: #21262d; color: #c9d1d9; border: 1px solid #30363d;"
        "  border-radius: 6px; padding: 8px 14px; min-width: 70px;"
        "}"
        "QPushButton#btnCancel:hover { background-color: #30363d; color: #f0f6fc; }"
        "QPushButton#btnOff:hover { background-color: #da3633; color: #ffffff; border: 1px solid #f85149; }"
        "QPushButton#btnReset:hover { background-color: #30363d; color: #58a6ff; }"
    );

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(18, 14, 18, 14);

    // Header
    auto *headerLayout = new QVBoxLayout();
    headerLayout->setSpacing(2);
    auto *lblTitle = new QLabel(tr("⚡ ตั้งค่าระบบเมดเล่ย์ (Medley Settings)"), this);
    lblTitle->setProperty("class", "header");
    headerLayout->addWidget(lblTitle);

    auto *lblSub = new QLabel(tr("ป้อนรหัสจังหวะและความเร็วเพื่อเล่นเพลงต่อเนื่องอัตโนมัติ (Ctrl+[ หรือ F11)"), this);
    lblSub->setProperty("class", "sub");
    headerLayout->addWidget(lblSub);
    mainLayout->addLayout(headerLayout);

    // ==========================================
    // Card 1: จังหวะและความเร็วเพลง (Rhythm & Tempo)
    // ==========================================
    auto *card1 = new QFrame(this);
    card1->setProperty("class", "card");
    auto *card1Layout = new QVBoxLayout(card1);
    card1Layout->setSpacing(8);
    card1Layout->setContentsMargins(12, 10, 12, 10);

    auto *lblCard1Title = new QLabel(tr("🎵 จังหวะและความเร็วเพลง (Rhythm & Tempo)"), card1);
    lblCard1Title->setProperty("class", "cardTitle");
    card1Layout->addWidget(lblCard1Title);

    // Row 1: Rhythm
    auto *rhythmRow = new QHBoxLayout();
    rhythmRow->setSpacing(8);
    auto *lblRhythm = new QLabel(tr("จังหวะ (Rhythm) :"), card1);
    lblRhythm->setProperty("class", "fieldLabel");
    lblRhythm->setFixedWidth(115);
    rhythmRow->addWidget(lblRhythm);

    _spinRhythm = new QSpinBox(card1);
    _spinRhythm->setRange(0, 999);
    _spinRhythm->setAlignment(Qt::AlignCenter);
    _spinRhythm->setFixedWidth(75);
    connect(_spinRhythm, QOverload<int>::of(&QSpinBox::valueChanged), this, &MedleyDialog::onRhythmSpinChanged);
    rhythmRow->addWidget(_spinRhythm);

    _comboRhythm = new QComboBox(card1);
    _comboRhythm->addItem(tr("901 - ลูกส่งลำซิ่ง (Lum-Zing)"), 1);
    _comboRhythm->addItem(tr("900 - ลูกส่งสามช่า / ลูกทุ่ง (Sam-Cha)"), 0);
    _comboRhythm->addItem(tr("902 - ลูกส่ง 3ช่าลำซิ่ง (3-Cha Lum-Zing)"), 2);
    _comboRhythm->addItem(tr("903 - ลูกส่งดิสโก้ (Disco)"), 3);
    _comboRhythm->addItem(tr("904 - ลูกส่งลำซิ่ง 2 (Lum-Zing 2)"), 4);
    _comboRhythm->addItem(tr("100 - ลูกส่งมาตรฐาน (Standard Rock/Pop)"), 5);
    _comboRhythm->addItem(tr("0 - ตามเพลง (ไม่ใช้ลูกส่งเสริม)"), 6);
    connect(_comboRhythm, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MedleyDialog::onRhythmComboChanged);
    rhythmRow->addWidget(_comboRhythm, 1);

    card1Layout->addLayout(rhythmRow);

    // Row 2: Tempo
    auto *tempoRow = new QHBoxLayout();
    tempoRow->setSpacing(8);
    auto *lblTempo = new QLabel(tr("ความเร็ว (Tempo) :"), card1);
    lblTempo->setProperty("class", "fieldLabel");
    lblTempo->setFixedWidth(115);
    tempoRow->addWidget(lblTempo);

    _spinBpm = new QSpinBox(card1);
    _spinBpm->setRange(0, 250);
    _spinBpm->setAlignment(Qt::AlignCenter);
    _spinBpm->setSpecialValueText(tr("0 (ตามเพลง)"));
    _spinBpm->setSuffix(" BPM");
    _spinBpm->setFixedWidth(115);
    tempoRow->addWidget(_spinBpm);

    auto *lblMode = new QLabel(tr("โหมด :"), card1);
    lblMode->setProperty("class", "fieldLabel");
    tempoRow->addWidget(lblMode);

    _comboTempoMode = new QComboBox(card1);
    _comboTempoMode->addItem(tr("Lock (คงที่ทุกเพลง)"), 0);
    _comboTempoMode->addItem(tr("Auto (ตามเพลงเดิม)"), 1);
    tempoRow->addWidget(_comboTempoMode, 1);

    card1Layout->addLayout(tempoRow);

    // Row 3: Quick BPM Chips (clearly separated)
    auto *chipsRow = new QHBoxLayout();
    chipsRow->setSpacing(6);
    auto *lblChipsPad = new QLabel("", card1);
    lblChipsPad->setFixedWidth(123);
    chipsRow->addWidget(lblChipsPad);

    auto addChip = [this, chipsRow, card1](const QString &text, int val) {
        auto *btn = new QPushButton(text, card1);
        btn->setProperty("class", "quick");
        connect(btn, &QPushButton::clicked, this, [this, val]() {
            _spinBpm->setValue(val);
            _spinBpm->setFocus();
        });
        chipsRow->addWidget(btn);
    };

    addChip(tr("0 ตามเพลง"), 0);
    addChip("140", 140);
    addChip(tr("145 สามช่า"), 145);
    addChip(tr("150 ลำซิ่ง"), 150);
    chipsRow->addStretch(1);

    card1Layout->addLayout(chipsRow);
    mainLayout->addWidget(card1);

    // ==========================================
    // Card 2: ช่วงรอยต่อและการตัดต่อเพลง (Transition & Cues)
    // ==========================================
    auto *card2 = new QFrame(this);
    card2->setProperty("class", "card");
    auto *card2Layout = new QVBoxLayout(card2);
    card2Layout->setSpacing(8);
    card2Layout->setContentsMargins(12, 10, 12, 10);

    auto *lblCard2Title = new QLabel(tr("🥁 ช่วงรอยต่อและการตัดต่อเพลง (Transition & Cues)"), card2);
    lblCard2Title->setProperty("class", "cardTitle");
    card2Layout->addWidget(lblCard2Title);

    // Transition bars row
    auto *barsRow = new QHBoxLayout();
    barsRow->setSpacing(8);
    auto *lblBars = new QLabel(tr("ลูกส่งช่วงรอยต่อ :"), card2);
    lblBars->setProperty("class", "fieldLabel");
    lblBars->setFixedWidth(115);
    barsRow->addWidget(lblBars);

    _btnBar0 = new QPushButton(tr("0 ห้อง (ต่อทันที)"), card2);
    _btnBar0->setProperty("class", "segBtn");
    connect(_btnBar0, &QPushButton::clicked, this, [this]() { onBarsButtonClicked(0); });
    barsRow->addWidget(_btnBar0);

    _btnBar1 = new QPushButton(tr("ลูกส่ง 1 ห้อง"), card2);
    _btnBar1->setProperty("class", "segBtn");
    connect(_btnBar1, &QPushButton::clicked, this, [this]() { onBarsButtonClicked(1); });
    barsRow->addWidget(_btnBar1);

    _btnBar2 = new QPushButton(tr("ลูกส่ง 2 ห้อง"), card2);
    _btnBar2->setProperty("class", "segBtn");
    connect(_btnBar2, &QPushButton::clicked, this, [this]() { onBarsButtonClicked(2); });
    barsRow->addWidget(_btnBar2);

    barsRow->addStretch(1);
    card2Layout->addLayout(barsRow);

    // Volume row
    auto *volRow = new QHBoxLayout();
    volRow->setSpacing(8);
    auto *lblVol = new QLabel(tr("ความดังลูกส่ง :"), card2);
    lblVol->setProperty("class", "fieldLabel");
    lblVol->setFixedWidth(115);
    volRow->addWidget(lblVol);

    _sliderVol = new QSlider(Qt::Horizontal, card2);
    _sliderVol->setRange(80, 130);
    _sliderVol->setValue(110);
    connect(_sliderVol, &QSlider::valueChanged, this, &MedleyDialog::onVolumeSliderChanged);
    volRow->addWidget(_sliderVol, 1);

    _lblVolVal = new QLabel("110 %", card2);
    _lblVolVal->setFixedWidth(50);
    _lblVolVal->setStyleSheet("font-weight: bold; color: #58a6ff; font-size: 10pt;");
    volRow->addWidget(_lblVolVal);

    card2Layout->addLayout(volRow);

    // Cues: Skip intro & Auto Cut Outro
    _chkSkipIntro = new QCheckBox(tr("ข้ามท่อนเกริ่น / อินโทรของเพลงถัดไป (Auto Skip Intro)"), card2);
    card2Layout->addWidget(_chkSkipIntro);

    auto *outroRow = new QHBoxLayout();
    outroRow->setSpacing(8);
    _chkAutoCutOutro = new QCheckBox(tr("ตัดหางเพลงเมื่อร้องจบ (Auto Cut Outro)"), card2);
    outroRow->addWidget(_chkAutoCutOutro);

    outroRow->addStretch(1);
    auto *lblWait = new QLabel(tr("รอเล่นต่อ :"), card2);
    lblWait->setStyleSheet("color: #8b949e; font-size: 9pt;");
    outroRow->addWidget(lblWait);

    _comboWaitBars = new QComboBox(card2);
    _comboWaitBars->addItem(tr("1 ห้อง (มาตรฐาน)"), 1);
    _comboWaitBars->addItem(tr("2 ห้อง"), 2);
    _comboWaitBars->addItem(tr("0 ห้อง (ทันที)"), 0);
    _comboWaitBars->setFixedWidth(120);
    outroRow->addWidget(_comboWaitBars);

    card2Layout->addLayout(outroRow);

    _chkQuantize = new QCheckBox(tr("ล็อกการตัดเพลงให้ตรงจังหวะตกต้นห้อง (Quantize to Beat 1 Sync)"), card2);
    card2Layout->addWidget(_chkQuantize);

    mainLayout->addWidget(card2);

    // ==========================================
    // Footer Action Buttons
    // ==========================================
    auto *footer = new QHBoxLayout();
    footer->setSpacing(8);

    auto *btnReset = new QPushButton(tr("↺ คืนค่า"), this);
    btnReset->setObjectName("btnReset");
    btnReset->setToolTip(tr("คืนค่าตัวเลือกทั้งหมดกลับเป็นค่ามาตรฐาน"));
    connect(btnReset, &QPushButton::clicked, this, &MedleyDialog::onResetClicked);
    footer->addWidget(btnReset);

    footer->addStretch(1);

    auto *btnOff = new QPushButton(tr("⛔ ปิดเมดเลย์ (Ctrl+])"), this);
    btnOff->setObjectName("btnOff");
    connect(btnOff, &QPushButton::clicked, this, &MedleyDialog::onOffClicked);
    footer->addWidget(btnOff);

    auto *btnCancel = new QPushButton(tr("ยกเลิก (Esc)"), this);
    btnCancel->setObjectName("btnCancel");
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(btnCancel);

    auto *btnOk = new QPushButton(tr("ตกลง (Enter)"), this);
    btnOk->setObjectName("btnOk");
    btnOk->setDefault(true);
    connect(btnOk, &QPushButton::clicked, this, &MedleyDialog::onOkClicked);
    footer->addWidget(btnOk);

    mainLayout->addLayout(footer);

    loadFromPlayer();

    _spinRhythm->setFocus();
    _spinRhythm->selectAll();
}

void MedleyDialog::updateBarsSelection(int bars)
{
    _selectedBars = bars;
    _btnBar0->setProperty("active", bars == 0 ? "true" : "false");
    _btnBar1->setProperty("active", bars == 1 ? "true" : "false");
    _btnBar2->setProperty("active", bars == 2 ? "true" : "false");
    _btnBar0->style()->unpolish(_btnBar0);
    _btnBar0->style()->polish(_btnBar0);
    _btnBar1->style()->unpolish(_btnBar1);
    _btnBar1->style()->polish(_btnBar1);
    _btnBar2->style()->unpolish(_btnBar2);
    _btnBar2->style()->polish(_btnBar2);
}

void MedleyDialog::onBarsButtonClicked(int bars)
{
    updateBarsSelection(bars);
}

void MedleyDialog::onVolumeSliderChanged(int val)
{
    if (_lblVolVal) {
        _lblVolVal->setText(QString::number(val) + " %");
    }
}

void MedleyDialog::onRhythmSpinChanged(int code)
{
    if (_isSyncing) return;
    _isSyncing = true;

    int targetIdx = -1;
    switch (code) {
        case 901: targetIdx = 0; break;
        case 900: targetIdx = 1; break;
        case 902: targetIdx = 2; break;
        case 903: targetIdx = 3; break;
        case 904: targetIdx = 4; break;
        case 100: targetIdx = 5; break;
        case 0:   targetIdx = 6; break;
        default:  targetIdx = 6; break;
    }

    if (targetIdx >= 0 && targetIdx < _comboRhythm->count()) {
        _comboRhythm->setCurrentIndex(targetIdx);
    }
    _isSyncing = false;
}

void MedleyDialog::onRhythmComboChanged(int index)
{
    if (_isSyncing) return;
    _isSyncing = true;

    const int pattern = _comboRhythm->itemData(index).toInt();
    int code = 0;
    switch (pattern) {
        case 1: code = 901; break;
        case 0: code = 900; break;
        case 2: code = 902; break;
        case 3: code = 903; break;
        case 4: code = 904; break;
        case 5: code = 100; break;
        default: code = 0; break;
    }
    _spinRhythm->setValue(code);
    _isSyncing = false;
}

void MedleyDialog::loadFromPlayer()
{
    QSettings settings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    settings.beginGroup("Medley");
    int savedBpm = settings.value("BPM", 150).toInt();
    int savedPattern = settings.value("FillPattern", 1).toInt();
    int savedTempoMode = settings.value("TempoMode", 0).toInt();
    int savedBars = settings.value("TransitionBars", 1).toInt();
    int savedVol = settings.value("FillVolume", 110).toInt();
    bool savedSkipIntro = settings.value("SkipIntro", true).toBool();
    bool savedCutOutro = settings.value("AutoCutOutro", true).toBool();
    int savedWaitBars = settings.value("WaitBars", 1).toInt();
    bool savedQuantize = settings.value("QuantizeBar", true).toBool();
    settings.endGroup();

    if (_player) {
        if (!_player->isPlayerStopped() && _player->currentBpm() >= 40 && _player->currentBpm() <= 240)
            savedBpm = _player->currentBpm();
        else if (_player->medleyBPM() > 0)
            savedBpm = _player->medleyBPM();

        savedPattern = _player->medleyFillPattern();
        savedTempoMode = _player->medleyTempoMode();
        savedBars = _player->medleyTransitionBars();
        savedVol = _player->medleyFillVolume();
        savedSkipIntro = _player->medleySkipIntro();
        savedCutOutro = _player->medleyAutoCutOutro();
        savedWaitBars = _player->medleyWaitBars();
        savedQuantize = _player->medleyQuantizeBar();
    }

    _isSyncing = true;
    int code = 901;
    int comboIdx = 0;
    switch (savedPattern) {
        case 1: code = 901; comboIdx = 0; break;
        case 0: code = 900; comboIdx = 1; break;
        case 2: code = 902; comboIdx = 2; break;
        case 3: code = 903; comboIdx = 3; break;
        case 4: code = 904; comboIdx = 4; break;
        case 5: code = 100; comboIdx = 5; break;
        default: code = 0; comboIdx = 6; break;
    }
    _spinRhythm->setValue(code);
    _comboRhythm->setCurrentIndex(comboIdx);
    _isSyncing = false;

    _spinBpm->setValue(savedBpm);
    for (int i = 0; i < _comboTempoMode->count(); ++i) {
        if (_comboTempoMode->itemData(i).toInt() == savedTempoMode) {
            _comboTempoMode->setCurrentIndex(i);
            break;
        }
    }

    updateBarsSelection(savedBars);

    _sliderVol->setValue(savedVol);
    _lblVolVal->setText(QString::number(savedVol) + " %");

    _chkSkipIntro->setChecked(savedSkipIntro);
    _chkAutoCutOutro->setChecked(savedCutOutro);

    for (int i = 0; i < _comboWaitBars->count(); ++i) {
        if (_comboWaitBars->itemData(i).toInt() == savedWaitBars) {
            _comboWaitBars->setCurrentIndex(i);
            break;
        }
    }

    _chkQuantize->setChecked(savedQuantize);
}

void MedleyDialog::onResetClicked()
{
    _isSyncing = true;
    _spinRhythm->setValue(901);
    _comboRhythm->setCurrentIndex(0);
    _isSyncing = false;

    _spinBpm->setValue(150);
    _comboTempoMode->setCurrentIndex(0);

    updateBarsSelection(1);
    _sliderVol->setValue(110);
    _lblVolVal->setText("110 %");

    _chkSkipIntro->setChecked(true);
    _chkAutoCutOutro->setChecked(true);
    _comboWaitBars->setCurrentIndex(0); // 1 bar
    _chkQuantize->setChecked(true);
}

void MedleyDialog::saveToPlayer()
{
    const int pattern = fillPattern();
    const int tMode = tempoMode();
    const int bpm = tempo();
    const int bars = transitionBars();
    const int vol = fillVolume();
    const bool sIntro = skipIntro();
    const bool cOutro = autoCutOutro();
    const int wBars = waitBars();
    const bool qtz = quantize();

    if (_player) {
        _player->setMedleyFillPattern(pattern);
        _player->setMedleyTempoMode(tMode);
        _player->setMedleyBPM(bpm);
        _player->setMedleyTransitionBars(bars);
        _player->setMedleyFillVolume(vol);
        _player->setMedleySkipIntro(sIntro);
        _player->setMedleyAutoCutOutro(cOutro);
        _player->setMedleyWaitBars(wBars);
        _player->setMedleyQuantizeBar(qtz);
        _player->setUseMedley(true);

        if (_player->midiSequencerTemp() != nullptr) {
            _player->midiSequencerTemp()->setMedleyTransition(true,
                                                             bars,
                                                             pattern,
                                                             bpm,
                                                             vol,
                                                             tMode,
                                                             _player->medleyPreserveEffectsTail());
        }
    }

    QSettings settings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    settings.beginGroup("Medley");
    settings.setValue("Enable", true);
    settings.setValue("BPM", bpm);
    settings.setValue("FillPattern", pattern);
    settings.setValue("TempoMode", tMode);
    settings.setValue("TransitionBars", bars);
    settings.setValue("FillVolume", vol);
    settings.setValue("SkipIntro", sIntro);
    settings.setValue("AutoCutOutro", cOutro);
    settings.setValue("WaitBars", wBars);
    settings.setValue("QuantizeBar", qtz);
    settings.endGroup();
}

int MedleyDialog::rhythmCode() const
{
    return _spinRhythm ? _spinRhythm->value() : 901;
}

int MedleyDialog::fillPattern() const
{
    const int code = rhythmCode();
    switch (code) {
        case 900: return 0;
        case 901: return 1;
        case 902: return 2;
        case 903: return 3;
        case 904: return 4;
        case 100: return 5;
        default:  return 6;
    }
}

int MedleyDialog::tempo() const
{
    return _spinBpm ? _spinBpm->value() : 150;
}

int MedleyDialog::tempoMode() const
{
    return _comboTempoMode ? _comboTempoMode->currentData().toInt() : 0;
}

int MedleyDialog::transitionBars() const
{
    return _selectedBars;
}

int MedleyDialog::fillVolume() const
{
    return _sliderVol ? _sliderVol->value() : 110;
}

bool MedleyDialog::skipIntro() const
{
    return _chkSkipIntro ? _chkSkipIntro->isChecked() : true;
}

bool MedleyDialog::autoCutOutro() const
{
    return _chkAutoCutOutro ? _chkAutoCutOutro->isChecked() : true;
}

int MedleyDialog::waitBars() const
{
    return _comboWaitBars ? _comboWaitBars->currentData().toInt() : 1;
}

bool MedleyDialog::quantize() const
{
    return _chkQuantize ? _chkQuantize->isChecked() : true;
}

void MedleyDialog::onOkClicked()
{
    saveToPlayer();
    accept();
}

void MedleyDialog::onOffClicked()
{
    _turnOffRequested = true;
    reject();
}

void MedleyDialog::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
        onOkClicked();
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    if (e->modifiers() == Qt::ControlModifier && (e->key() == Qt::Key_BracketRight || e->key() == Qt::Key_BraceRight)) {
        onOffClicked();
        return;
    }
    QDialog::keyPressEvent(e);
}
