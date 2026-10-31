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
#include <QStyle>

namespace {

// Re-apply the stylesheet after changing a dynamic property such as "active".
void repolish(QWidget *w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
}

QLabel *fieldLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setProperty("class", "fieldLabel");
    return l;
}

QFrame *makeCard(const QString &title, QWidget *parent, QVBoxLayout **outLayout)
{
    auto *card = new QFrame(parent);
    card->setProperty("class", "card");
    auto *lay = new QVBoxLayout(card);
    lay->setSpacing(10);
    lay->setContentsMargins(16, 12, 16, 14);
    auto *t = new QLabel(title, card);
    t->setProperty("class", "cardTitle");
    lay->addWidget(t);
    *outLayout = lay;
    return card;
}

} // namespace

MedleyDialog::MedleyDialog(QWidget *parent, MidiPlayer *player)
    : QDialog(parent), _player(player)
{
    setWindowTitle(tr("ตั้งค่าเมดเลย์"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setMinimumWidth(600);

    setStyleSheet(
        "QDialog { background-color: #0d1117; color: #e6edf3; font-size: 10pt; }"
        "QFrame[class=\"card\"] { background-color: #161b22; border: 1px solid #30363d; border-radius: 10px; }"
        "QLabel { color: #e6edf3; background: transparent; }"
        "QLabel[class=\"header\"] { font-size: 14pt; font-weight: bold; color: #f0f6fc; }"
        "QLabel[class=\"sub\"] { font-size: 9pt; color: #8b949e; }"
        "QLabel[class=\"cardTitle\"] { font-size: 10.5pt; font-weight: bold; color: #58a6ff; }"
        "QLabel[class=\"fieldLabel\"] { font-size: 9.5pt; color: #c9d1d9; }"
        "QLabel[class=\"hint\"] { font-size: 9pt; color: #7ee787; background-color: #12261e;"
        "  border: 1px solid #238636; border-radius: 6px; padding: 6px 10px; }"
        "QComboBox, QSpinBox {"
        "  background-color: #21262d; color: #e6edf3; font-size: 10pt;"
        "  border: 1px solid #30363d; border-radius: 6px; padding: 4px 8px; min-height: 26px;"
        "}"
        "QSpinBox { color: #58a6ff; font-weight: bold; }"
        "QComboBox:focus, QSpinBox:focus { border: 1px solid #58a6ff; }"
        "QComboBox:disabled, QSpinBox:disabled { color: #6e7681; background-color: #161b22; }"
        "QComboBox::drop-down { border: none; width: 22px; }"
        "QComboBox QAbstractItemView {"
        "  background-color: #161b22; color: #e6edf3; selection-background-color: #1f6feb;"
        "  border: 1px solid #30363d; padding: 4px;"
        "}"
        "QPushButton[class=\"segBtn\"] {"
        "  background-color: #21262d; color: #c9d1d9; border: 1px solid #30363d;"
        "  border-radius: 6px; padding: 7px 10px; font-size: 9.5pt;"
        "}"
        "QPushButton[class=\"segBtn\"]:hover { background-color: #30363d; color: #f0f6fc; }"
        "QPushButton[class=\"segBtn\"][active=\"true\"] {"
        "  background-color: #1f6feb; color: #ffffff; font-weight: bold; border: 1px solid #58a6ff;"
        "}"
        "QPushButton[class=\"quick\"] {"
        "  background-color: transparent; color: #8b949e; border: 1px solid #30363d;"
        "  border-radius: 12px; padding: 3px 12px; font-size: 9pt;"
        "}"
        "QPushButton[class=\"quick\"]:hover { color: #58a6ff; border: 1px solid #58a6ff; }"
        "QPushButton[class=\"quick\"]:disabled { color: #484f58; border-color: #21262d; }"
        "QSlider::groove:horizontal { height: 6px; background-color: #30363d; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background-color: #1f6feb; border-radius: 3px; }"
        "QSlider::handle:horizontal {"
        "  background-color: #58a6ff; width: 16px; height: 16px; margin: -5px 0; border-radius: 8px;"
        "}"
        "QSlider::handle:horizontal:hover { background-color: #79c0ff; }"
        "QCheckBox { color: #e6edf3; font-size: 9.5pt; spacing: 8px; background: transparent; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border-radius: 4px; border: 1px solid #484f58; background-color: #21262d; }"
        "QCheckBox::indicator:checked { background-color: #1f6feb; border-color: #58a6ff; }"
        "QPushButton#btnOk {"
        "  background-color: #1f6feb; color: #ffffff; font-weight: bold; border-radius: 6px;"
        "  padding: 8px 26px; border: 1px solid #388bfd;"
        "}"
        "QPushButton#btnOk:hover { background-color: #388bfd; }"
        "QPushButton#btnCancel, QPushButton#btnOff, QPushButton#btnReset {"
        "  background-color: #21262d; color: #c9d1d9; border: 1px solid #30363d;"
        "  border-radius: 6px; padding: 8px 16px;"
        "}"
        "QPushButton#btnCancel:hover { background-color: #30363d; color: #f0f6fc; }"
        "QPushButton#btnOff { color: #ff7b72; }"
        "QPushButton#btnOff:hover { background-color: #da3633; color: #ffffff; border: 1px solid #f85149; }"
        "QPushButton#btnReset { background-color: transparent; border: none; color: #8b949e; padding: 8px 4px; }"
        "QPushButton#btnReset:hover { color: #58a6ff; }"
    );

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    // Let the dialog grow to fit its content at any Windows text scaling.
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);

    // ---------- Header ----------
    auto *lblTitle = new QLabel(tr("ตั้งค่าเมดเลย์"), this);
    lblTitle->setProperty("class", "header");
    mainLayout->addWidget(lblTitle);
    auto *lblSub = new QLabel(tr("เล่นเพลงต่อกันอัตโนมัติ พร้อมจังหวะเชื่อมระหว่างเพลง   •   เปิดหน้านี้: Ctrl+[ หรือ F11"), this);
    lblSub->setProperty("class", "sub");
    lblSub->setWordWrap(true);
    mainLayout->addWidget(lblSub);

    // ---------- Card 1: rhythm & tempo ----------
    QVBoxLayout *c1;
    QFrame *card1 = makeCard(tr("🎵  จังหวะและความเร็ว"), this, &c1);
    auto *g1 = new QGridLayout();
    g1->setHorizontalSpacing(12);
    g1->setVerticalSpacing(10);
    g1->setColumnStretch(1, 1);

    // Rhythm: code box (type 900/901… quickly) + readable list
    g1->addWidget(fieldLabel(tr("จังหวะลูกส่ง"), card1), 0, 0);
    auto *rhythmBox = new QHBoxLayout();
    rhythmBox->setSpacing(8);
    _spinRhythm = new QSpinBox(card1);
    _spinRhythm->setRange(0, 999);
    _spinRhythm->setAlignment(Qt::AlignCenter);
    _spinRhythm->setButtonSymbols(QAbstractSpinBox::NoButtons);
    _spinRhythm->setMinimumWidth(70);
    _spinRhythm->setToolTip(tr("พิมพ์รหัสจังหวะได้เลย เช่น 900, 901, 903"));
    connect(_spinRhythm, QOverload<int>::of(&QSpinBox::valueChanged), this, &MedleyDialog::onRhythmSpinChanged);
    rhythmBox->addWidget(_spinRhythm);

    _comboRhythm = new QComboBox(card1);
    _comboRhythm->addItem(tr("901  ลำซิ่ง"), 1);
    _comboRhythm->addItem(tr("900  สามช่า / ลูกทุ่ง"), 0);
    _comboRhythm->addItem(tr("902  สามช่าลำซิ่ง"), 2);
    _comboRhythm->addItem(tr("903  ดิสโก้"), 3);
    _comboRhythm->addItem(tr("904  ลำซิ่ง 2"), 4);
    _comboRhythm->addItem(tr("100  มาตรฐาน (ร็อก/ป๊อป)"), 5);
    _comboRhythm->addItem(tr("0  สุ่มจังหวะไทย"), 6);
    connect(_comboRhythm, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MedleyDialog::onRhythmComboChanged);
    rhythmBox->addWidget(_comboRhythm, 1);
    g1->addLayout(rhythmBox, 0, 1);

    // Tempo: two clear choices instead of BPM 0 + a separate mode box
    g1->addWidget(fieldLabel(tr("ความเร็ว"), card1), 1, 0);
    auto *tempoChoice = new QHBoxLayout();
    tempoChoice->setSpacing(6);
    _btnTempoLock = new QPushButton(tr("ใช้ความเร็วเดียวทุกเพลง"), card1);
    _btnTempoLock->setProperty("class", "segBtn");
    connect(_btnTempoLock, &QPushButton::clicked, this, [this]() { updateTempoMode(false); });
    tempoChoice->addWidget(_btnTempoLock, 1);
    _btnTempoFollow = new QPushButton(tr("ตามต้นฉบับแต่ละเพลง"), card1);
    _btnTempoFollow->setProperty("class", "segBtn");
    connect(_btnTempoFollow, &QPushButton::clicked, this, [this]() { updateTempoMode(true); });
    tempoChoice->addWidget(_btnTempoFollow, 1);
    g1->addLayout(tempoChoice, 1, 1);

    // Tempo: any value 40-250. Type it, nudge with -5/-1/+1/+5, drag the slider,
    // or use the 3 quick buttons (right-click a quick button to store the current tempo on it).
    _bpmChips = new QWidget(card1);     // container for all tempo controls (disabled in "follow" mode)
    auto *bpmBox = new QVBoxLayout(_bpmChips);
    bpmBox->setContentsMargins(0, 0, 0, 0);
    bpmBox->setSpacing(8);

    auto *bpmRow = new QHBoxLayout();
    bpmRow->setSpacing(6);
    auto nudge = [this](const QString &text, int delta) {
        auto *b = new QPushButton(text, _bpmChips);
        b->setProperty("class", "quick");
        b->setCursor(Qt::PointingHandCursor);
        b->setAutoRepeat(true);             // hold to keep changing
        b->setAutoRepeatDelay(350);
        b->setAutoRepeatInterval(70);
        connect(b, &QPushButton::clicked, this, [this, delta]() { _spinBpm->setValue(_spinBpm->value() + delta); });
        return b;
    };
    bpmRow->addWidget(nudge(QStringLiteral("−5"), -5));
    bpmRow->addWidget(nudge(QStringLiteral("−1"), -1));
    _spinBpm = new QSpinBox(_bpmChips);
    _spinBpm->setRange(40, 250);
    _spinBpm->setAlignment(Qt::AlignCenter);
    _spinBpm->setSuffix(" BPM");
    _spinBpm->setButtonSymbols(QAbstractSpinBox::NoButtons);
    _spinBpm->setKeyboardTracking(false);
    _spinBpm->setMinimumWidth(120);
    _spinBpm->setToolTip(tr("พิมพ์ความเร็วได้เลย (40–250) หรือใช้ล้อเมาส์ / ลูกศรขึ้น-ลง"));
    _spinBpm->setStyleSheet("font-size: 13pt;");
    bpmRow->addWidget(_spinBpm);
    bpmRow->addWidget(nudge(QStringLiteral("+1"), +1));
    bpmRow->addWidget(nudge(QStringLiteral("+5"), +5));
    auto *slider = new QSlider(Qt::Horizontal, _bpmChips);
    slider->setRange(40, 250);
    bpmRow->addWidget(slider, 1);
    connect(slider, &QSlider::valueChanged, _spinBpm, &QSpinBox::setValue);
    connect(_spinBpm, QOverload<int>::of(&QSpinBox::valueChanged), slider, [slider](int v) {
        if (slider->value() != v) slider->setValue(v);
    });
    bpmBox->addLayout(bpmRow);

    // Quick buttons: values come from settings so the user can keep their own favourites.
    auto *chipsRow = new QHBoxLayout();
    chipsRow->setSpacing(6);
    auto *lblQuick = new QLabel(tr("ปุ่มลัด:"), _bpmChips);
    lblQuick->setProperty("class", "sub");
    chipsRow->addWidget(lblQuick);
    const int defaults[3] = {140, 145, 150};
    for (int k = 0; k < 3; ++k) {
        const QString key = QString("Medley/QuickBpm%1").arg(k + 1);
        auto *btn = new QPushButton(_bpmChips);
        btn->setProperty("class", "quick");
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(tr("คลิก = ใช้ความเร็วนี้\nคลิกขวา = บันทึกความเร็วปัจจุบันลงปุ่มนี้"));
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        auto label = [](int v) {
            if (v == 145) return QString("145 สามช่า");
            if (v == 150) return QString("150 ลำซิ่ง");
            return QString::number(v);
        };
        const int v0 = QSettings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat).value(key, defaults[k]).toInt();
        btn->setProperty("bpm", v0);
        btn->setText(label(v0));
        connect(btn, &QPushButton::clicked, this, [this, btn]() { _spinBpm->setValue(btn->property("bpm").toInt()); });
        connect(btn, &QWidget::customContextMenuRequested, this, [this, btn, key, label]() {
            const int v = _spinBpm->value();
            btn->setProperty("bpm", v);
            btn->setText(label(v));
            QSettings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat).setValue(key, v);
        });
        chipsRow->addWidget(btn);
    }
    chipsRow->addStretch(1);
    bpmBox->addLayout(chipsRow);

    g1->addWidget(_bpmChips, 2, 1);

    _chkKeepFar = new QCheckBox(tr("ไม่บังคับเพลงช้า (เพลงช้าเล่นครึ่งความเร็วนี้ เพลงที่ต่างมากเล่นความเร็วเดิม)"), card1);
    _chkKeepFar->setToolTip(tr("ติ๊ก: เพลงช้าไม่ถูกเร่งเป็นเพลงเร็ว\nไม่ติ๊ก: ทุกเพลงเล่นที่ความเร็วที่ตั้งไว้พอดี"));
    g1->addWidget(_chkKeepFar, 3, 1);

    c1->addLayout(g1);
    mainLayout->addWidget(card1);

    // ---------- Card 2: transition ----------
    QVBoxLayout *c2;
    QFrame *card2 = makeCard(tr("🥁  ช่วงรอยต่อระหว่างเพลง"), this, &c2);

    c2->addWidget(fieldLabel(tr("ก่อนเข้าเพลงถัดไป ให้เล่น"), card2));
    auto *barsGrid = new QGridLayout();
    barsGrid->setSpacing(6);
    auto makeBarBtn = [this, card2](const QString &text, int bars) {
        auto *b = new QPushButton(text, card2);
        b->setProperty("class", "segBtn");
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, bars]() { onBarsButtonClicked(bars); });
        return b;
    };
    _btnBar0 = makeBarBtn(tr("ต่อทันที (ไม่มีลูกส่ง)"), 0);
    _btnBar1 = makeBarBtn(tr("ลูกส่ง 1 ห้อง"), 1);
    _btnBar2 = makeBarBtn(tr("จังหวะ 1 + ลูกส่ง 1"), 2);
    _btnBar4 = makeBarBtn(tr("จังหวะ 3 + ลูกส่ง 1  ★ แนะนำ"), 4);
    barsGrid->addWidget(_btnBar0, 0, 0);
    barsGrid->addWidget(_btnBar1, 0, 1);
    barsGrid->addWidget(_btnBar2, 1, 0);
    barsGrid->addWidget(_btnBar4, 1, 1);
    barsGrid->setColumnStretch(0, 1);
    barsGrid->setColumnStretch(1, 1);
    c2->addLayout(barsGrid);

    _lblBarsHint = new QLabel(card2);
    _lblBarsHint->setProperty("class", "hint");
    _lblBarsHint->setWordWrap(true);
    c2->addWidget(_lblBarsHint);

    auto *volRow = new QHBoxLayout();
    volRow->setSpacing(10);
    volRow->addWidget(fieldLabel(tr("ความดังลูกส่ง"), card2));
    _sliderVol = new QSlider(Qt::Horizontal, card2);
    _sliderVol->setRange(80, 130);
    _sliderVol->setValue(110);
    connect(_sliderVol, &QSlider::valueChanged, this, &MedleyDialog::onVolumeSliderChanged);
    volRow->addWidget(_sliderVol, 1);
    _lblVolVal = new QLabel("110 %", card2);
    _lblVolVal->setMinimumWidth(52);
    _lblVolVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    _lblVolVal->setStyleSheet("font-weight: bold; color: #58a6ff;");
    volRow->addWidget(_lblVolVal);
    c2->addLayout(volRow);

    mainLayout->addWidget(card2);

    // ---------- Card 3: cutting songs ----------
    QVBoxLayout *c3;
    QFrame *card3 = makeCard(tr("✂️  การตัดต่อเพลง"), this, &c3);

    _chkSkipIntro = new QCheckBox(tr("ข้ามอินโทรของเพลงถัดไป"), card3);
    _chkSkipIntro->setToolTip(tr("Auto Skip Intro: เริ่มเพลงถัดไปใกล้ท่อนร้อง"));
    c3->addWidget(_chkSkipIntro);

    auto *outroRow = new QHBoxLayout();
    outroRow->setSpacing(8);
    _chkAutoCutOutro = new QCheckBox(tr("ตัดหางเพลงเมื่อร้องจบ  แล้วรออีก"), card3);
    _chkAutoCutOutro->setToolTip(tr("Auto Cut Outro: ไม่ต้องรอดนตรีท้ายเพลงจนจบ"));
    outroRow->addWidget(_chkAutoCutOutro);
    _comboWaitBars = new QComboBox(card3);
    _comboWaitBars->addItem(tr("0 ห้อง (ทันที)"), 0);
    _comboWaitBars->addItem(tr("1 ห้อง"), 1);
    _comboWaitBars->addItem(tr("2 ห้อง"), 2);
    outroRow->addWidget(_comboWaitBars);
    outroRow->addStretch(1);
    connect(_chkAutoCutOutro, &QCheckBox::toggled, _comboWaitBars, &QWidget::setEnabled);
    c3->addLayout(outroRow);

    _chkQuantize = new QCheckBox(tr("ตัดเพลงให้ตรงจังหวะต้นห้อง"), card3);
    _chkQuantize->setToolTip(tr("Quantize to Beat 1: ตัดตรงจังหวะที่ 1 ของห้อง ทำให้ต่อเพลงไม่หลุดจังหวะ"));
    c3->addWidget(_chkQuantize);

    mainLayout->addWidget(card3);

    // ---------- Footer ----------
    auto *footer = new QHBoxLayout();
    footer->setSpacing(8);

    auto *btnReset = new QPushButton(tr("↺ คืนค่าเริ่มต้น"), this);
    btnReset->setObjectName("btnReset");
    btnReset->setCursor(Qt::PointingHandCursor);
    connect(btnReset, &QPushButton::clicked, this, &MedleyDialog::onResetClicked);
    footer->addWidget(btnReset);
    footer->addStretch(1);

    auto *btnOff = new QPushButton(tr("ปิดเมดเลย์"), this);
    btnOff->setObjectName("btnOff");
    btnOff->setToolTip(tr("Ctrl+]"));
    connect(btnOff, &QPushButton::clicked, this, &MedleyDialog::onOffClicked);
    footer->addWidget(btnOff);

    auto *btnCancel = new QPushButton(tr("ยกเลิก"), this);
    btnCancel->setObjectName("btnCancel");
    btnCancel->setToolTip(tr("Esc"));
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(btnCancel);

    auto *btnOk = new QPushButton(tr("ตกลง"), this);
    btnOk->setObjectName("btnOk");
    btnOk->setToolTip(tr("Enter"));
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
    if (bars != 0 && bars != 1 && bars != 2 && bars != 4)
        bars = (bars >= 3) ? 4 : 1;
    _selectedBars = bars;

    const QList<QPair<QPushButton*, int>> btns = {
        {_btnBar0, 0}, {_btnBar1, 1}, {_btnBar2, 2}, {_btnBar4, 4}
    };
    for (const auto &p : btns) {
        p.first->setProperty("active", p.second == bars ? "true" : "false");
        repolish(p.first);
    }

    QString hint;
    switch (bars) {
    case 0: hint = tr("จบเพลง  →  เข้าเพลงถัดไปทันที"); break;
    case 1: hint = tr("จบเพลง  →  ลูกส่ง 1 ห้อง  →  เข้าเพลงถัดไป"); break;
    case 2: hint = tr("จบเพลง  →  จังหวะ 1 ห้อง  →  ลูกส่ง 1 ห้อง  →  เข้าเพลงถัดไป"); break;
    default: hint = tr("จบเพลง  →  จังหวะ 3 ห้อง  →  ลูกส่ง 1 ห้อง  →  เข้าเพลงถัดไป"); break;
    }
    _lblBarsHint->setText(QStringLiteral("▶  ") + hint);

    if (_sliderVol)
        _sliderVol->setEnabled(bars > 0);
}

void MedleyDialog::updateTempoMode(bool follow)
{
    _tempoFollow = follow;
    _btnTempoLock->setProperty("active", follow ? "false" : "true");
    _btnTempoFollow->setProperty("active", follow ? "true" : "false");
    repolish(_btnTempoLock);
    repolish(_btnTempoFollow);
    _bpmChips->setEnabled(!follow);
    if (_chkKeepFar) _chkKeepFar->setEnabled(!follow);
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

    bool follow = (savedTempoMode == 1 || savedBpm == 0);
    if (_player) {
        follow = (_player->medleyTempoMode() == 1 || _player->medleyBPM() == 0);
        if (_player->medleyBPM() > 0)
            savedBpm = _player->medleyBPM();
        else if (!_player->isPlayerStopped() && _player->currentBpm() >= 40 && _player->currentBpm() <= 240)
            savedBpm = _player->currentBpm();

        savedPattern = _player->medleyFillPattern();
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

    _spinBpm->setValue(savedBpm > 0 ? savedBpm : 150);
    _chkKeepFar->setChecked(_player ? _player->medleyKeepFarSongs() : true);
    updateTempoMode(follow);

    updateBarsSelection(savedBars);

    _sliderVol->setValue(savedVol);
    _lblVolVal->setText(QString::number(savedVol) + " %");

    _chkSkipIntro->setChecked(savedSkipIntro);
    _chkAutoCutOutro->setChecked(savedCutOutro);
    _comboWaitBars->setEnabled(savedCutOutro);

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
    _chkKeepFar->setChecked(true);
    updateTempoMode(false);

    updateBarsSelection(4);
    _sliderVol->setValue(110);
    _lblVolVal->setText("110 %");

    _chkSkipIntro->setChecked(true);
    _chkAutoCutOutro->setChecked(true);
    _comboWaitBars->setCurrentIndex(_comboWaitBars->findData(1));
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
        _player->setMedleyKeepFarSongs(_chkKeepFar->isChecked());
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
    settings.setValue("KeepFarSongs", _chkKeepFar->isChecked());
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
    // 0 BPM = every song keeps its own tempo (read by MidiPlayer::loadNextMedley)
    if (_tempoFollow)
        return 0;
    return _spinBpm ? _spinBpm->value() : 150;
}

int MedleyDialog::tempoMode() const
{
    return _tempoFollow ? 1 : 0;
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
