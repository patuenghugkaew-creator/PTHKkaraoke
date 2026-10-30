#include "AutoMedleyDialog.h"
#include "Config.h"
#include "Midi/MidiPlayer.h"
#include "Midi/MidiSequencer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QKeyEvent>
#include <QSettings>
#include <QFrame>

AutoMedleyDialog::AutoMedleyDialog(QWidget *parent, MidiPlayer *player)
    : QDialog(parent), _player(player)
{
    setWindowTitle(tr("Auto Medley Console (Ctrl + [)"));
    setFixedSize(500, 390);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    setStyleSheet(
        "QDialog { background-color: #0d1117; color: #e6edf3; font-size: 10pt; }"
        "QFrame.card {"
        "  background-color: #161b22; border: 1px solid #30363d; border-radius: 8px; padding: 12px;"
        "}"
        "QLabel { color: #e6edf3; }"
        "QLabel.header { font-size: 12pt; font-weight: bold; color: #58a6ff; }"
        "QLabel.sub { font-size: 8.5pt; color: #8b949e; }"
        "QLabel.fieldLabel { font-size: 10pt; font-weight: bold; color: #c9d1d9; }"
        "QComboBox, QSpinBox {"
        "  background-color: #21262d; color: #58a6ff; font-size: 10.5pt; font-weight: bold;"
        "  border: 1px solid #30363d; border-radius: 6px; padding: 4px 8px; min-height: 30px;"
        "}"
        "QComboBox:focus, QSpinBox:focus { border: 1px solid #58a6ff; background-color: #1c2128; }"
        "QComboBox::drop-down { border: none; width: 22px; }"
        "QComboBox QAbstractItemView {"
        "  background-color: #161b22; color: #e6edf3; selection-background-color: #1f6feb;"
        "  border: 1px solid #30363d; border-radius: 6px; padding: 4px;"
        "}"
        "QPushButton.quick {"
        "  background-color: #21262d; color: #c9d1d9; border: 1px solid #30363d;"
        "  border-radius: 4px; padding: 3px 8px; font-size: 8.5pt; min-height: 20px;"
        "}"
        "QPushButton.quick:hover { background-color: #30363d; color: #58a6ff; border: 1px solid #58a6ff; }"
        "QPushButton.quick:pressed { background-color: #1f6feb; color: #ffffff; }"
        "QPushButton#btnOk {"
        "  background-color: #1f6feb; color: #ffffff; font-weight: bold; border-radius: 6px;"
        "  padding: 8px 20px; border: 1px solid #388bfd; min-width: 80px;"
        "}"
        "QPushButton#btnOk:hover { background-color: #388bfd; }"
        "QPushButton#btnCancel, QPushButton#btnOff, QPushButton#btnStudio {"
        "  background-color: #21262d; color: #c9d1d9; border: 1px solid #30363d;"
        "  border-radius: 6px; padding: 8px 14px; min-width: 70px;"
        "}"
        "QPushButton#btnCancel:hover { background-color: #30363d; color: #f0f6fc; }"
        "QPushButton#btnOff:hover { background-color: #da3633; color: #ffffff; border: 1px solid #f85149; }"
        "QPushButton#btnStudio:hover { background-color: #30363d; color: #58a6ff; border: 1px solid #58a6ff; }"
    );

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(16, 14, 16, 14);

    // Header
    auto *headerLayout = new QVBoxLayout();
    headerLayout->setSpacing(2);
    auto *lblTitle = new QLabel(tr("⚡ Auto Medley Console (เมดเล่ย์อัตโนมัติ)"), this);
    lblTitle->setProperty("class", "header");
    headerLayout->addWidget(lblTitle);

    auto *lblSub = new QLabel(tr("พิมพ์ตัวเลขแล้วกด Enter เพื่อเริ่มทันที | กด Spacebar สลับตัวเลือก"), this);
    lblSub->setProperty("class", "sub");
    headerLayout->addWidget(lblSub);
    mainLayout->addLayout(headerLayout);

    // Card frame containing 4 primary fields
    auto *card = new QFrame(this);
    card->setProperty("class", "card");
    auto *grid = new QGridLayout(card);
    grid->setSpacing(10);
    grid->setContentsMargins(12, 12, 12, 12);
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 1);

    // Field 1: จังหวะ (Rhythm)
    auto *lblRhythm = new QLabel(tr("จังหวะ (Rhythm) :"), card);
    lblRhythm->setProperty("class", "fieldLabel");
    grid->addWidget(lblRhythm, 0, 0);

    _comboRhythm = new QComboBox(card);
    _comboRhythm->setEditable(true);
    _comboRhythm->addItem(tr("901 - ลูกส่งลำซิ่ง (Lum-Zing)"), 1);
    _comboRhythm->addItem(tr("900 - ลูกส่งสามช่า / ลูกทุ่ง (Sam-Cha)"), 0);
    _comboRhythm->addItem(tr("902 - ลูกส่ง 3ช่าลำซิ่ง (3-Cha Lum-Zing)"), 2);
    _comboRhythm->addItem(tr("903 - ลูกส่งดิสโก้ (Disco)"), 3);
    _comboRhythm->addItem(tr("904 - ลูกส่งลำซิ่ง 2 (Lum-Zing 2)"), 4);
    _comboRhythm->addItem(tr("100 - ลูกส่งมาตรฐาน (Standard Rock/Pop)"), 5);
    _comboRhythm->addItem(tr("0 - ตามเพลง (ไม่ใช้ลูกส่งเสริม)"), 6);
    if (_comboRhythm->lineEdit()) {
        connect(_comboRhythm->lineEdit(), &QLineEdit::textEdited, this, &AutoMedleyDialog::onRhythmTextChanged);
    }
    grid->addWidget(_comboRhythm, 0, 1);

    // Field 2: สไตล์ (Style Mode: Lock vs Auto)
    auto *lblStyle = new QLabel(tr("สไตล์ (Style) :"), card);
    lblStyle->setProperty("class", "fieldLabel");
    grid->addWidget(lblStyle, 1, 0);

    _comboStyle = new QComboBox(card);
    _comboStyle->addItem(tr("Lock (คงที่ตามความเร็วเมดเล่ย์)"), 0);
    _comboStyle->addItem(tr("Auto (ตามความเร็วเพลงเดิม)"), 1);
    grid->addWidget(_comboStyle, 1, 1);

    // Field 3: ความเร็ว (Tempo / BPM)
    auto *lblBpm = new QLabel(tr("ความเร็ว (Tempo) :"), card);
    lblBpm->setProperty("class", "fieldLabel");
    grid->addWidget(lblBpm, 2, 0, Qt::AlignTop);

    auto *bpmCol = new QVBoxLayout();
    bpmCol->setSpacing(4);

    _spinBpm = new QSpinBox(card);
    _spinBpm->setRange(0, 250);
    _spinBpm->setAlignment(Qt::AlignCenter);
    _spinBpm->setSpecialValueText(tr("0 (ตามเพลง)"));
    _spinBpm->setSuffix(" BPM");
    bpmCol->addWidget(_spinBpm);

    // Quick BPM chips
    auto *bpmChips = new QHBoxLayout();
    bpmChips->setSpacing(4);
    auto addChip = [this, bpmChips](const QString &text, int val) {
        auto *btn = new QPushButton(text, this);
        btn->setProperty("class", "quick");
        connect(btn, &QPushButton::clicked, this, [this, val]() {
            _spinBpm->setValue(val);
            _spinBpm->setFocus();
        });
        bpmChips->addWidget(btn);
    };
    addChip(tr("0 ตามเพลง"), 0);
    addChip("140", 140);
    addChip(tr("145 สามช่า"), 145);
    addChip(tr("150 ลำซิ่ง"), 150);
    bpmCol->addLayout(bpmChips);

    grid->addLayout(bpmCol, 2, 1);

    // Field 4: เริ่มเพลง (Start Mode: Auto skip intro vs Normal)
    auto *lblStart = new QLabel(tr("เริ่มเพลง (Start) :"), card);
    lblStart->setProperty("class", "fieldLabel");
    grid->addWidget(lblStart, 3, 0);

    _comboStart = new QComboBox(card);
    _comboStart->addItem(tr("Auto (ตัดเกริ่น / ข้ามอินโทร)"), true);
    _comboStart->addItem(tr("Normal (เล่นตั้งแต่หัวเพลง ไม่ข้าม)"), false);
    grid->addWidget(_comboStart, 3, 1);

    // Field 5: ลูกส่งรอ (Transition Wait Bars)
    auto *lblWait = new QLabel(tr("ลูกส่งรอ (Bars) :"), card);
    lblWait->setProperty("class", "fieldLabel");
    grid->addWidget(lblWait, 4, 0);

    _comboWaitBars = new QComboBox(card);
    _comboWaitBars->addItem(tr("1 ห้อง (มาตรฐาน กระชับ)"), 1);
    _comboWaitBars->addItem(tr("2 ห้อง (ส่งยาวเต็มท่อน)"), 2);
    _comboWaitBars->addItem(tr("0 ห้อง (ต่อทันที ไม่ส่ง)"), 0);
    grid->addWidget(_comboWaitBars, 4, 1);

    mainLayout->addWidget(card);

    // Footer actions
    auto *footer = new QHBoxLayout();
    footer->setSpacing(8);

    auto *btnStudio = new QPushButton(tr("⚙️ ตั้งค่าละเอียด..."), this);
    btnStudio->setObjectName("btnStudio");
    btnStudio->setToolTip(tr("เปิดหน้าต่าง Medley Studio เพื่อปรับแต่งคิวและระบบเสียงเชิงลึก"));
    connect(btnStudio, &QPushButton::clicked, this, &AutoMedleyDialog::onStudioClicked);
    footer->addWidget(btnStudio);

    auto *btnOff = new QPushButton(tr("ปิดเมดเลย์ (Ctrl+])"), this);
    btnOff->setObjectName("btnOff");
    connect(btnOff, &QPushButton::clicked, this, &AutoMedleyDialog::onOffClicked);
    footer->addWidget(btnOff);

    footer->addStretch(1);

    auto *btnOk = new QPushButton(tr("ตกลง (Enter)"), this);
    btnOk->setObjectName("btnOk");
    btnOk->setDefault(true);
    connect(btnOk, &QPushButton::clicked, this, &AutoMedleyDialog::onOkClicked);
    footer->addWidget(btnOk);

    auto *btnCancel = new QPushButton(tr("ยกเลิก (Esc)"), this);
    btnCancel->setObjectName("btnCancel");
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(btnCancel);

    mainLayout->addLayout(footer);

    syncControlsFromPlayer();

    // Default focus
    _spinBpm->setFocus();
    _spinBpm->selectAll();
}

void AutoMedleyDialog::syncControlsFromPlayer()
{
    int savedBpm = 150;
    int savedPattern = 1; // 901 ลำซิ่ง default
    int savedTempoMode = 0; // Lock
    bool savedSkipIntro = true;
    int savedWaitBars = 1;

    QSettings settings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    settings.beginGroup("Medley");
    savedBpm = settings.value("BPM", 150).toInt();
    savedPattern = settings.value("FillPattern", 1).toInt();
    savedTempoMode = settings.value("TempoMode", 0).toInt();
    savedSkipIntro = settings.value("SkipIntro", true).toBool();
    savedWaitBars = settings.value("TransitionBars", 1).toInt();
    settings.endGroup();

    if (_player) {
        if (!_player->isPlayerStopped() && _player->currentBpm() >= 40 && _player->currentBpm() <= 240)
            savedBpm = _player->currentBpm();
        else if (_player->medleyBPM() > 0)
            savedBpm = _player->medleyBPM();

        savedPattern = _player->medleyFillPattern();
        savedTempoMode = _player->medleyTempoMode();
        savedSkipIntro = _player->medleySkipIntro();
        savedWaitBars = _player->medleyTransitionBars();
    }

    // Set Rhythm
    for (int i = 0; i < _comboRhythm->count(); ++i) {
        if (_comboRhythm->itemData(i).toInt() == savedPattern) {
            _comboRhythm->setCurrentIndex(i);
            break;
        }
    }

    // Set Style
    for (int i = 0; i < _comboStyle->count(); ++i) {
        if (_comboStyle->itemData(i).toInt() == savedTempoMode) {
            _comboStyle->setCurrentIndex(i);
            break;
        }
    }

    // Set BPM
    _spinBpm->setValue(savedBpm);

    // Set Start
    for (int i = 0; i < _comboStart->count(); ++i) {
        if (_comboStart->itemData(i).toBool() == savedSkipIntro) {
            _comboStart->setCurrentIndex(i);
            break;
        }
    }

    // Set Wait Bars
    for (int i = 0; i < _comboWaitBars->count(); ++i) {
        if (_comboWaitBars->itemData(i).toInt() == savedWaitBars) {
            _comboWaitBars->setCurrentIndex(i);
            break;
        }
    }
}

int AutoMedleyDialog::fillPattern() const
{
    return _comboRhythm ? _comboRhythm->currentData().toInt() : 1;
}

int AutoMedleyDialog::rhythmCode() const
{
    const int p = fillPattern();
    switch (p) {
        case 0: return 900;
        case 1: return 901;
        case 2: return 902;
        case 3: return 903;
        case 4: return 904;
        case 5: return 100;
        default: return 0;
    }
}

int AutoMedleyDialog::tempo() const
{
    return _spinBpm ? _spinBpm->value() : 150;
}

int AutoMedleyDialog::tempoMode() const
{
    return _comboStyle ? _comboStyle->currentData().toInt() : 0;
}

bool AutoMedleyDialog::skipIntro() const
{
    return _comboStart ? _comboStart->currentData().toBool() : true;
}

int AutoMedleyDialog::waitBars() const
{
    return _comboWaitBars ? _comboWaitBars->currentData().toInt() : 1;
}

void AutoMedleyDialog::onRhythmTextChanged(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return;

    for (int i = 0; i < _comboRhythm->count(); ++i) {
        const QString itemText = _comboRhythm->itemText(i);
        if (itemText.startsWith(trimmed, Qt::CaseInsensitive)) {
            _comboRhythm->setCurrentIndex(i);
            break;
        }
    }
}

void AutoMedleyDialog::onOkClicked()
{
    const int pattern = fillPattern();
    const int tMode = tempoMode();
    const int bpm = tempo();
    const bool sIntro = skipIntro();
    const int wBars = waitBars();

    if (_player) {
        _player->setMedleyFillPattern(pattern);
        _player->setMedleyTempoMode(tMode);
        _player->setMedleyBPM(bpm);
        _player->setMedleySkipIntro(sIntro);
        if (wBars > 0) _player->setMedleyTransitionBars(wBars);
        _player->setUseMedley(true);

        if (_player->midiSequencerTemp() != nullptr) {
            _player->midiSequencerTemp()->setMedleyTransition(true,
                                                             _player->medleyTransitionBars() > 0 ? _player->medleyTransitionBars() : 1,
                                                             pattern,
                                                             bpm,
                                                             _player->medleyFillVolume(),
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
    settings.setValue("SkipIntro", sIntro);
    settings.setValue("TransitionBars", wBars > 0 ? wBars : 1);
    settings.endGroup();

    accept();
}

void AutoMedleyDialog::onOffClicked()
{
    _turnOffRequested = true;
    reject();
}

void AutoMedleyDialog::onStudioClicked()
{
    _openStudioRequested = true;
    reject();
}

void AutoMedleyDialog::keyPressEvent(QKeyEvent *e)
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
    if (e->key() == Qt::Key_Space) {
        if (_comboStyle && _comboStyle->hasFocus()) {
            _comboStyle->setCurrentIndex((_comboStyle->currentIndex() + 1) % _comboStyle->count());
            return;
        }
        if (_comboStart && _comboStart->hasFocus()) {
            _comboStart->setCurrentIndex((_comboStart->currentIndex() + 1) % _comboStart->count());
            return;
        }
        if (_comboWaitBars && _comboWaitBars->hasFocus()) {
            _comboWaitBars->setCurrentIndex((_comboWaitBars->currentIndex() + 1) % _comboWaitBars->count());
            return;
        }
    }
    QDialog::keyPressEvent(e);
}
