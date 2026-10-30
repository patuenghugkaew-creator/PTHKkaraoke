#include "EasyMedleyDialog.h"
#include "Config.h"
#include "Midi/MidiPlayer.h"
#include "Midi/MidiSequencer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QKeyEvent>
#include <QSettings>

EasyMedleyDialog::EasyMedleyDialog(QWidget *parent, MidiPlayer *player)
    : QDialog(parent), _player(player)
{
    setWindowTitle(tr("Easy Medley Tempo (F11)"));
    setFixedSize(460, 240);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    setStyleSheet(
        "QDialog { background-color: #0d1117; color: #e6edf3; font-size: 10pt; }"
        "QLabel { color: #e6edf3; }"
        "QSpinBox {"
        "  background-color: #161b22; color: #58a6ff; font-size: 14pt; font-weight: bold;"
        "  border: 2px solid #30363d; border-radius: 6px; padding: 4px; min-height: 36px;"
        "}"
        "QSpinBox:focus { border: 2px solid #58a6ff; background-color: #21262d; }"
        "QPushButton.quick {"
        "  background-color: #21262d; color: #c9d1d9; border: 1px solid #30363d;"
        "  border-radius: 4px; padding: 4px 8px; font-size: 9pt; min-width: 44px; min-height: 22px;"
        "}"
        "QPushButton.quick:hover { background-color: #30363d; color: #58a6ff; border: 1px solid #58a6ff; }"
        "QPushButton.quick:pressed { background-color: #1f6feb; color: #ffffff; }"
        "QPushButton#btnOk {"
        "  background-color: #1f6feb; color: #ffffff; font-weight: bold; border-radius: 6px;"
        "  padding: 8px 22px; border: 1px solid #388bfd; min-width: 80px;"
        "}"
        "QPushButton#btnOk:hover { background-color: #388bfd; }"
        "QPushButton#btnCancel, QPushButton#btnOff {"
        "  background-color: #21262d; color: #c9d1d9; border: 1px solid #30363d;"
        "  border-radius: 6px; padding: 8px 16px; min-width: 70px;"
        "}"
        "QPushButton#btnCancel:hover { background-color: #30363d; color: #f0f6fc; }"
        "QPushButton#btnOff:hover { background-color: #da3633; color: #ffffff; border: 1px solid #f85149; }"
    );

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(18, 16, 18, 16);

    // Title / Subtitle
    auto *lblTitle = new QLabel(tr("⚡ Easy Medley Tempo (ความเร็วเพลงต่อเนื่อง)"), this);
    lblTitle->setStyleSheet("font-size: 11pt; font-weight: bold; color: #58a6ff;");
    mainLayout->addWidget(lblTitle);

    auto *lblDesc = new QLabel(tr("พิมพ์ตัวเลขความเร็วที่ต้องการ (ใส่ 0 หรือเว้นว่าง = ใช้ความเร็วตามต้นฉบับเพลง)"), this);
    lblDesc->setStyleSheet("color: #8b949e; font-size: 9pt;");
    mainLayout->addWidget(lblDesc);

    // Center input row
    auto *inputRow = new QHBoxLayout();
    auto *lblBpmPrompt = new QLabel(tr("ความเร็ว (Tempo) :"), this);
    lblBpmPrompt->setStyleSheet("font-weight: bold; font-size: 11pt;");
    inputRow->addWidget(lblBpmPrompt);

    _spinBpm = new QSpinBox(this);
    _spinBpm->setRange(0, 250);
    _spinBpm->setAlignment(Qt::AlignCenter);
    _spinBpm->setSpecialValueText(tr("0 (ตามเพลง)"));
    _spinBpm->setSuffix(" BPM");

    // Default BPM value
    int currentBpm = 150;
    QSettings settings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    settings.beginGroup("Medley");
    int savedBpm = settings.value("BPM", 150).toInt();
    settings.endGroup();

    if (_player) {
        if (!_player->isPlayerStopped() && _player->currentBpm() >= 40 && _player->currentBpm() <= 240)
            currentBpm = _player->currentBpm();
        else if (_player->medleyBPM() > 0)
            currentBpm = _player->medleyBPM();
        else if (savedBpm > 0)
            currentBpm = savedBpm;
    }
    _spinBpm->setValue(currentBpm);
    inputRow->addWidget(_spinBpm, 1);
    mainLayout->addLayout(inputRow);

    // Quick buttons row
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);

    auto addQuickBtn = [this, btnRow](const QString &text, int val) {
        auto *btn = new QPushButton(text, this);
        btn->setProperty("class", "quick");
        connect(btn, &QPushButton::clicked, this, [this, val]() {
            _spinBpm->setValue(val);
            _spinBpm->setFocus();
            _spinBpm->selectAll();
        });
        btnRow->addWidget(btn);
    };

    addQuickBtn(tr("0 ตามเพลง"), 0);
    addQuickBtn("130", 130);
    addQuickBtn("140", 140);
    addQuickBtn("145", 145);
    addQuickBtn("150", 150);
    addQuickBtn("155", 155);
    addQuickBtn("160", 160);

    mainLayout->addLayout(btnRow);
    mainLayout->addSpacing(4);

    // Bottom action buttons
    auto *footerLayout = new QHBoxLayout();
    footerLayout->setSpacing(10);

    auto *btnOff = new QPushButton(tr("ปิดเมดเลย์ (Ctrl+])"), this);
    btnOff->setObjectName("btnOff");
    connect(btnOff, &QPushButton::clicked, this, &EasyMedleyDialog::onOffClicked);
    footerLayout->addWidget(btnOff);

    footerLayout->addStretch(1);

    auto *btnOk = new QPushButton(tr("ตกลง (Enter)"), this);
    btnOk->setObjectName("btnOk");
    btnOk->setDefault(true);
    connect(btnOk, &QPushButton::clicked, this, &EasyMedleyDialog::onOkClicked);
    footerLayout->addWidget(btnOk);

    auto *btnCancel = new QPushButton(tr("ยกเลิก (Esc)"), this);
    btnCancel->setObjectName("btnCancel");
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    footerLayout->addWidget(btnCancel);

    mainLayout->addLayout(footerLayout);

    _spinBpm->setFocus();
    _spinBpm->selectAll();
}

int EasyMedleyDialog::tempo() const
{
    return _spinBpm ? _spinBpm->value() : 0;
}

void EasyMedleyDialog::onOkClicked()
{
    const int bpm = tempo();
    if (_player) {
        _player->setMedleyBPM(bpm);
        _player->setUseMedley(true);

        if (_player->midiSequencerTemp() != nullptr) {
            _player->midiSequencerTemp()->setMedleyTransition(true,
                                                             _player->medleyTransitionBars() > 0 ? _player->medleyTransitionBars() : 1,
                                                             _player->medleyFillPattern(),
                                                             bpm,
                                                             _player->medleyFillVolume(),
                                                             _player->medleyTempoMode(),
                                                             _player->medleyPreserveEffectsTail());
        }
    }

    QSettings settings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    settings.beginGroup("Medley");
    settings.setValue("Enable", true);
    settings.setValue("BPM", bpm);
    settings.endGroup();

    accept();
}

void EasyMedleyDialog::onOffClicked()
{
    _turnOffRequested = true;
    reject();
}

void EasyMedleyDialog::keyPressEvent(QKeyEvent *e)
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
