#include "SynthMixerDialog.h"
#include "ui_SynthMixerDialog.h"

#include "MainWindow.h"

#include <QMenu>
#include <QFileInfo>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QMessageBox>
#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QScrollBar>
#include <QSlider>
#include <QCheckBox>
#include <QToolButton>
#include <QSettings>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QToolButton>
#include <QPushButton>
#include <QButtonGroup>
#include <QInputDialog>
#include <QLineEdit>
#include <QWindow>
#include <QPainter>
#include <QCursor>
#include <QAbstractButton>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRegularExpression>
#include <QPixmap>
#include <QFrame>
#include <QGridLayout>
#include <QScreen>
#include <QGuiApplication>
#include <QIcon>
#include <QFileDialog>
#include <QComboBox>
#include <QGroupBox>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QProgressBar>
#include <QStandardPaths>
#include <QLocale>
#include <cmath>
#include <vector>
#include <atomic>
#include <bassmix.h>
#include "Widgets/TitleBar.h"
#include "Widgets/MasterStrip.h"
#include "Widgets/SysMonitor.h"
#include "Widgets/RtaDialog.h"
#include "Midi/FaderTaper.h"
#include "Theme.h"

#include <bass.h>

#include "Config.h"
#include "BASSFX/FX.h"
#include "Dialogs/SettingVuDialog.h"
#include "Dialogs/VSTDialog.h"
#include "Dialogs/BusDialog.h"
#include "Dialogs/SpeakerDialog.h"
#include "Dialogs/Equalizer31BandDialog.h"
#include "Dialogs/Chorus2Dialog.h"
#include "Dialogs/Reverb2Dialog.h"

#include "FXDialogs/AutoWahFXDialog.h"
#include "FXDialogs/ChorusFXDialog.h"
#include "FXDialogs/CompressorFXDialog.h"
#include "FXDialogs/DistortionFXDialog.h"
#include "FXDialogs/EchoFXDialog.h"
#include "FXDialogs/EQ15BandDialog.h"
#include "FXDialogs/EQ31BandDialog.h"
#include "FXDialogs/ReverbFXDialog.h"

#ifndef __linux__
#include <bass_vst.h>
#include "Vst3/BassVst3.h"
#include "Dialogs/VSTDirsDialog.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Thin, crisp per-button frames drawn behind the widgets, lit on mouse-over.
namespace {
class GroupFrames : public QWidget {
public:
    explicit GroupFrames(QWidget *parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(parent->rect());
        parent->installEventFilter(this);
        // A button that opened a dialog / menu never gets its Leave event, so its glow stayed on after the
        // mouse had gone. Re-check the hot state a few times a second and repaint only when it changed.
        QTimer *poll = new QTimer(this);
        poll->setInterval(120);
        connect(poll, &QTimer::timeout, this, [this]() {
            QVector<bool> now;
            for (const Cell &c : cells)
                now.append(c.w && c.w->isVisible() && c.w->isEnabled()
                           && c.w->rect().contains(c.w->mapFromGlobal(QCursor::pos())));
            if (now != lastHot) {
                lastHot = now;
                update();
            }
        });
        poll->start();
    }
    void addCell(QWidget *w, const QMargins &m) {
        cells.append({ w, m });
        w->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject *o, QEvent *e) override {
        const QEvent::Type t = e->type();
        if (o == parentWidget() && (t == QEvent::Resize || t == QEvent::LayoutRequest || t == QEvent::Show)) {
            setGeometry(parentWidget()->rect());
            lower();
            update();
        } else if (o != parentWidget() && (t == QEvent::Enter || t == QEvent::Leave
                   || t == QEvent::HoverEnter || t == QEvent::HoverLeave || t == QEvent::Move
                   || t == QEvent::Resize || t == QEvent::EnabledChange)) {
            update();
        }
        return QWidget::eventFilter(o, e);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const bool light = palette().color(QPalette::Window).lightness() > 128;
        const QColor edge = light ? QColor(0x9a, 0xa2, 0xb2) : QColor(0x4a, 0x52, 0x66);
        const QColor fill(0, 0, 0, 0);   // no background fill in any theme
        const QColor hEdge = light ? QColor(0x2f, 0x86, 0xf0) : QColor(0x5a, 0xb0, 0xff);
        const QColor hFill = fill;   // no fill on hover: only the light edge + glow
        for (const Cell &c : cells) {
            if (!c.w || !c.w->isVisibleTo(parentWidget()))
                continue;
            QRect r(c.w->mapTo(parentWidget(), QPoint(0, 0)), c.w->size());
            r = r.marginsAdded(c.m);
            const bool hot = c.w->isEnabled() && c.w->rect().contains(c.w->mapFromGlobal(QCursor::pos()));
            if (hot) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(hEdge.red(), hEdge.green(), hEdge.blue(), light ? 48 : 40));
                p.drawRoundedRect(QRectF(r).adjusted(-2, -2, 2, 2), 8, 8);   // soft glow
            }
            QColor hf = hFill;
            if (hot) {
                if (auto *ab = qobject_cast<QAbstractButton *>(c.w))
                    if (ab->isDown())
                        hf = QColor(0x5a, 0xb0, 0xff, 28);   // very faint tint only while pressed
            }
            p.setPen(QPen(hot ? hEdge : edge, hot ? 1.4 : 1));
            p.setBrush(hot ? hf : fill);
            p.drawRoundedRect(QRectF(r).adjusted(0.7, 0.7, -0.7, -0.7), 6, 6);
        }
    }
private:
    struct Cell { QWidget *w; QMargins m; };
    QList<Cell> cells;
    QVector<bool> lastHot;
};
}
#endif


// Modern flat "tile" icon (rounded dark tile, accent outline, bold text) used for the FX strip and the bus numbers.
static QImage modernBadge(const QString &text, const QColor &accent, bool number)
{
    const qreal dpr = 2.0;
    QImage img(int(36 * dpr), int(35 * dpr), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF r(1.0, 1.0, 34.0, 33.0);
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0.0, QColor(0x2b, 0x31, 0x40));
    g.setColorAt(1.0, QColor(0x14, 0x17, 0x20));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(r, 9, 9);

    // thin accent outline + a short accent bar at the bottom edge
    QColor edge = accent;
    edge.setAlpha(200);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(edge, 1.3));
    p.drawRoundedRect(r, 9, 9);
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawRoundedRect(QRectF(12, 28.5, 12, 2.2), 1.1, 1.1);

    QFont f(QStringLiteral("DejaVu Sans"));
    f.setBold(true);
    f.setPixelSize(number ? (text.size() > 1 ? 15 : 19) : (text.size() > 2 ? 12 : 15));
    p.setFont(f);
    p.setPen(number ? QColor(0xf4, 0xf6, 0xfa) : accent.lighter(130));
    p.drawText(QRectF(1, 2, 34, 26), Qt::AlignCenter, text);
    p.end();
    return img;
}

// "lit" colour of the toggle buttons / page buttons: blue normally, orange in the dark orange theme
static QString litFill()   { return Theme::isOrange() ? QStringLiteral("#e8730c") : QStringLiteral("#1f7ae0"); }
static QString litEdge()   { return Theme::isOrange() ? QStringLiteral("#ffc58a") : QStringLiteral("#8cc4ff"); }
static QString panelFill() { return Theme::isOrange() ? QStringLiteral("#e8730c") : QStringLiteral("#2f7fd8"); }
static QString panelEdge() { return Theme::isOrange() ? QStringLiteral("#ffc58a") : QStringLiteral("#5ab0ff"); }

static QString quickButtonCss()
{
    return QStringLiteral(
        "QToolButton { background: transparent; border: none; border-radius: 6px;"
        " padding: 5px 8px; font-weight: bold; }"
        "QToolButton:hover, QToolButton:pressed { background: transparent; border: none; }"
        "QToolButton:checked, QToolButton:checked:hover { background: %1; color: #ffffff; border: 1px solid %2; }")
        .arg(litFill(), litEdge());
}

static QString panelButtonCss()
{
    // explicit text colour: on the dark themes the left-hand buttons looked greyer than the preset tabs
    const bool dark = qApp->palette().color(QPalette::Window).lightness() < 128;
    const QString txt = dark ? QStringLiteral("#ffffff") : QStringLiteral("palette(button-text)");
    return QStringLiteral(
        "QToolButton, QPushButton { background: transparent; border: none; color: %3;"
        " border-radius: 6px; padding: 6px 4px; font-weight: bold; }"
        "QToolButton:hover, QPushButton:hover, QToolButton:pressed, QPushButton:pressed { background: transparent; border: none; }"
        "QToolButton:checked:hover { background: %1; border: 1px solid %2; }"
        "QToolButton:checked { background: %1; color: #ffffff; border: 1px solid %2; }")
        .arg(panelFill(), panelEdge(), txt);
}

// colour of a strip's name tag (read back from its style sheet), used as the tile accent
static QColor tagAccent(QWidget *tag)
{
    if (tag) {
        const QRegularExpressionMatch m =
            QRegularExpression(QStringLiteral("background-color:\\s*(#[0-9a-fA-F]{6})")).match(tag->styleSheet());
        if (m.hasMatch())
            return QColor(m.captured(1));
    }
    return QColor(0x4f, 0xc3, 0xf7);
}

SynthMixerDialog::SynthMixerDialog(QWidget *parent, MainWindow *mainWin) : //, MainWindow *mainWin) :
    QDialog(parent),
    ui(new Ui::SynthMixerDialog),
    signalVstActionMapper(this),
    signalBusActionMapper(this)
{
    ui->setupUi(this);
    setProperty("pthkNoSkin", true);   // has its own title bar

    // Frameless window with our own themed neon title bar
    ui->verticalLayout_60->setContentsMargins(6, 6, 6, 6);
    titleBar = new TitleBar(tr("PTHK Karaoke - Mixer"), this);
    ui->verticalLayout_60->insertWidget(0, titleBar);
    {
        FrameOverlay *fo = new FrameOverlay(this);   // 1px outline; also asks Windows 11 for native rounded corners
        fo->setGeometry(rect());
        fo->show();
        fo->raise();
    }
    connect(titleBar->minimizeButton, &QAbstractButton::clicked, this, [this]() { showMinimized(); });
    connect(titleBar->closeButton, &QAbstractButton::clicked, this, [this]() { close(); });
    setMouseTracking(true);

    // timer

    settingTimer.setInterval(10 * 60000);
    settingTimer.start();
    connect(&settingTimer, SIGNAL(timeout()), this, SLOT(settingValues()));

    this->mainWin = mainWin;
    this->player = mainWin->midiPlayer();
    this->synth = player->midiSynthesizer();

    { // Quick switches next to the Menu button (master volume lives in the right-hand strip)
        int pos = ui->horizontalLayout_2->indexOf(ui->btnMenu);

        // Quick switches (same settings as the main settings window / menu)
        // toggle buttons that look like the preset tabs (lit when on)
        auto addChk = [this, pos](const QString &text, const QString &tip) {
            QToolButton *cb = new QToolButton(this);
            cb->setText(text);
            cb->setToolTip(tip);
            cb->setCheckable(true);
            cb->setFocusPolicy(Qt::NoFocus);
            cb->setCursor(Qt::PointingHandCursor);
            cb->setMinimumSize(73, 27);
            cb->setMaximumHeight(27);
            cb->setStyleSheet(quickButtonCss());
            ui->horizontalLayout_2->insertWidget(pos, cb);
            return cb;
        };
        chkLockDrum  = addChk(tr("ล็อกกลอง"),  tr("ล็อกเสียงกลอง (ตั้งชุดกลองในหน้าตั้งค่า)"));
        chkLockSnare = addChk(tr("ล็อกสแนร์"), tr("ล็อกเสียงสแนร์ (ตั้งเบอร์เสียงในหน้าตั้งค่า)"));
        chkLockKick  = addChk(tr("ล็อกกระเดื่อง"), tr("ล็อกเสียงกระเดื่อง (35 / 36)"));
        chkLockBass  = addChk(tr("ล็อกเบส"),   tr("ล็อกเสียงเบส (ตั้งเบอร์เสียงในหน้าตั้งค่า)"));
        chkNoTune    = addChk(tr("ตัดจูนกลอง"), tr("ตัดคำสั่งจูนเสียงกลอง/สแนร์ (ช่อง 10)"));
        btnDrumTune  = addChk(tr("จูนหนังกลอง"), tr("ปรับความตึงหนังกลอง กระเดื่อง สแนร์ ทอม และเลือกเบอร์กระเดื่อง/สแนร์"));
        btnDrumTune->setCheckable(false);
        // sit right next to the Menu button (addChk puts each new button before the previous ones)
        ui->horizontalLayout_2->removeWidget(btnDrumTune);
        ui->horizontalLayout_2->insertWidget(ui->horizontalLayout_2->indexOf(ui->btnMenu), btnDrumTune);
        btnDrumTune->setMinimumWidth(90);
        // tab row lines up with the columns below: tab 0 = left panel width, tab 1 starts where the strips start
        ui->horizontalLayout_4->setSpacing(6);
        ui->btnPreset0->setFixedWidth(78);
        ui->btnMenu->setFixedWidth(88);   // same width as the master strip below it
        ui->horizontalLayout_2->setSpacing(6);   // same gap as between the preset tabs

        syncQuickChecks();

        // clicking a lock button opens a pop-up with the numbers to lock to (and "off")
        connect(chkLockDrum,  &QToolButton::clicked, this, [this]() { showLockMenu(chkLockDrum, 0); });
        connect(chkLockSnare, &QToolButton::clicked, this, [this]() { showLockMenu(chkLockSnare, 1); });
        connect(chkLockBass,  &QToolButton::clicked, this, [this]() { showLockMenu(chkLockBass, 2); });
        connect(chkLockKick,  &QToolButton::clicked, this, [this]() { showLockMenu(chkLockKick, 3); });
        connect(btnDrumTune, &QToolButton::clicked, this, [this]() { showDrumTunePopup(); });

        { // restore saved drum tuning (kick / snare key choice is the kick / snare lock)
            QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
            for (int g = 0; g < MidiSynthesizer::DrumTuneGroupCount; g++)
                synth->setDrumTune(g, st.value(QString("DrumTune%1").arg(g), 0).toInt());
        }

        connect(chkNoTune, &QToolButton::clicked, this, [this](bool on) {
            QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
            player->midiSynthesizer()->setIgnoreDrumPitch(on);
            st.setValue("IgnoreDrumPitch", on);
        });

    }

    { // live CPU / RAM monitor fills the free space between the quick buttons and the Menu button
        QHBoxLayout *row = ui->horizontalLayout_2;
        for (int i = 0; i < row->count(); i++) {
            if (row->itemAt(i)->spacerItem()) {
                delete row->takeAt(i);
                SysMonitor *mon = new SysMonitor(this);
                row->insertWidget(i, mon, 1);
                break;
            }
        }
    }

    updateMenuIcon();

    mapChInstUI();
    setChInstDetails();

    // slim left-right scroll bars (they used to take a thick band of height under the strips)
    {
        const QString slim =
            "QScrollBar:horizontal { height: 6px; margin: 0; background: transparent; border: none; }"
            "QScrollBar::handle:horizontal { background: palette(mid); border-radius: 3px; min-width: 40px; }"
            "QScrollBar::handle:horizontal:hover { background: palette(highlight); }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; height: 0; background: none; border: none; }"
            "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }";
        ui->scrollArea->horizontalScrollBar()->setStyleSheet(slim);
        ui->scrollArea_2->horizontalScrollBar()->setStyleSheet(slim);
    }

    ui->scrollArea->setWidgetResizable(false);
    this->adjustSize();
    this->setMinimumSize(970, height());
    this->setMaximumHeight(height());

    btnPresets.addButton(ui->btnPreset0, 0);
    btnPresets.addButton(ui->btnPreset1, 1);
    btnPresets.addButton(ui->btnPreset2, 2);
    btnPresets.addButton(ui->btnPreset3, 3);
    btnPresets.addButton(ui->btnPreset4, 4);
    btnPresets.addButton(ui->btnPreset5, 5);
    btnPresets.addButton(ui->btnPreset6, 6);
    btnPresets.addButton(ui->btnPreset7, 7);
    btnPresets.addButton(ui->btnPreset8, 8);
    btnPresets.addButton(ui->btnPreset9, 9);
    btnPresets.addButton(ui->btnPreset10, 10);

    buildSidePanels();   // after the preset buttons are registered (styles them)

    { // subtle per-button frames (light up on mouse-over, like the Menu button)
        GroupFrames *gf = new GroupFrames(this);
        const QList<QWidget *> presets = { ui->btnPreset0, ui->btnPreset1, ui->btnPreset2, ui->btnPreset3,
                                           ui->btnPreset4, ui->btnPreset5, ui->btnPreset6, ui->btnPreset7,
                                           ui->btnPreset8, ui->btnPreset9, ui->btnPreset10 };
        for (QWidget *w : presets)
            gf->addCell(w, QMargins(1, 1, 1, 1));
        for (QWidget *w : QList<QWidget *>{ chkLockDrum, chkLockKick, chkLockSnare, chkLockBass, chkNoTune, btnDrumTune })
            gf->addCell(w, QMargins(1, 1, 1, 1));
        // Menu button (top right) and the left-hand button column
        ui->btnMenu->setStyleSheet("QPushButton, QPushButton:hover, QPushButton:pressed { background: transparent; border: none; border-radius: 6px; padding: 2px 8px; }");
        gf->addCell(ui->btnMenu, QMargins(0, 0, 0, 0));
        if (QWidget *lp = findChild<QWidget *>("leftPanel"))
            for (QAbstractButton *b : lp->findChildren<QAbstractButton *>())
                gf->addCell(b, QMargins(0, 0, 0, 0));
        gf->lower();
        gf->show();
    }

    { // Settings
        QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);

        // window parent, stays on top
        staysOnTop = st.value("WindowStaysOnTop", false).toBool();
        {
            Qt::WindowFlags flags = Qt::Window | Qt::FramelessWindowHint
                                    | Qt::WindowMinimizeButtonHint | Qt::WindowSystemMenuHint;
            if (staysOnTop)
                flags |= Qt::WindowStaysOnTopHint;
            setProperty("kmTopmost", staysOnTop);
            this->setParent(0, flags);
        }

        QSize size = st.value("Size", this->size()).toSize();
        resize(size);

        QList<int> splits; splits.append(700); splits.append(200);
        QList<int> splitterSize = st.value("SplitterSize", QVariant::fromValue(splits)).value<QList<int>>();
        int scroll1 = st.value("ScrollInstrument", 0).toInt();
        int scroll2 = st.value("ScrollBusGroup", 0).toInt();

        ui->splitter->setSizes(splitterSize);
        ui->scrollArea->horizontalScrollBar()->setValue(scroll1);
        ui->scrollArea_2->horizontalScrollBar()->setValue(scroll2);

        // soundfont presets
        int presets = st.value("SoundfontPresets", 0).toInt();
        setSoundfontPresets(presets);

        // Bus names -----------------------------------
        {
            QStringList n1 = st.value("BusNames", QStringList()).toStringList();
            QStringList n2 = st.value("BusFullNames", QStringList()).toStringList();
            if (n1.count() == 16 && n2.count() == 16) {
                int start = static_cast<int>(InstrumentType::BusGroup1);
                for (int i=0; i<16; i++) {
                    InstrumentType type = static_cast<InstrumentType>(start + i);
                    chInstMap[type]->setInstrumentNames(n1[i], n2[i]);
                }
            }
        }

        // Master Eq -----------------------------------
        {
            bool eqOn = st.value("MasterEqOn", false).toBool();
            QList<float> eqParams = st.value("MasterEqParams").value<QList<float>>();
            for (auto eq : synth->equalizer31BandFXs()) {
                if (eqOn) eq->on();
                if (eqParams.count() > 0)
                    eq->setParams(eqParams);
            }
        }

        // Master Chorus -----------------------------------
        {
            bool chorusOn = st.value("MasterChorusOn", false).toBool();
            QList<float> chorusParams = st.value("MasterChorusParams").value<QList<float>>();
            for (auto cr : synth->chorusFXs()) {
                if (chorusOn) cr->on();
                if (chorusParams.count() > 0)
                    cr->setParams(chorusParams);
            }
        }

        // Master Reverb -----------------------------------
        {
            bool reverbOn = st.value("MasterReverbOn", false).toBool();
            QList<float> reverbParams = st.value("MasterReverbParams").value<QList<float>>();
            for (auto rv : synth->reverbFXs()) {
                if (reverbOn) rv->on();
                if (reverbParams.count() > 0)
                    rv->setParams(reverbParams);
            }
        }


        // Led Vu -------------------------------------------
        LEDVu *vu = chInstMap.first()->vuBar();
        QString bg = st.value("LedBgColor", vu->backgroundColor().name()).toString();
        QString o1 = st.value("LedColorOn1", vu->ledColorOn1().name()).toString();
        QString o2 = st.value("LedColorOn2", vu->ledColorOn2().name()).toString();
        QString o3 = st.value("LedColorOn3", vu->ledColorOn3().name()).toString();
        QString f1 = st.value("LedColorOff1", vu->ledColorOff1().name()).toString();
        QString f2 = st.value("LedColorOff2", vu->ledColorOff2().name()).toString();
        QString f3 = st.value("LedColorOff3", vu->ledColorOff3().name()).toString();
        bool sph = st.value("ShowPeakHold", vu->isShowPeakHold()).toBool();
        int phm = st.value("PeakHoldMs", vu->peakHoldMs()).toInt();


        st.beginReadArray("SynthMixer");
        for (InstrumentType t : chInstMap.keys())
        {
            st.setArrayIndex(static_cast<int>(t));
            int dv = st.value("Device", 0).toInt();
            int  b = st.value("Bus", -1).toInt();
            int ml = st.value("Volume", 50).toInt();
            int pn = st.value("Pan", 0).toInt();
            int rv = st.value("Reverb", 0).toInt();
            double tr = st.value("Trim", 0.0).toDouble();
            bool m = st.value("Mute", false).toBool();
            bool s = st.value("Solo", false).toBool();
            int  v = st.value("VSTi", -1).toInt();
            int sp = st.value("Speaker", 0).toInt();

            InstCh * ich = chInstMap[t];
            ich->setSliderLevel(ml);
            ich->setPanValue(pn);
            ich->setReverbValue(rv);
            ich->setTrimValue(tr);
            ich->setMuteButton(m);
            ich->setSoloButton(s);

            LEDVu *vBar = ich->vuBar();
            vBar->setBackGroundColor(QColor(bg));
            vBar->setLedColorOn1(QColor(o1));
            vBar->setLedColorOn2(QColor(o2));
            vBar->setLedColorOn3(QColor(o3));
            vBar->setLedColorOff1(QColor(f1));
            vBar->setLedColorOff2(QColor(f2));
            vBar->setLedColorOff3(QColor(f3));
            vBar->setShowPeakHold(sph);
            vBar->setPeakHoldMs(phm);

            synth->setDevice(t, dv);
            synth->setBusGroup(t, b);
            ich->setBusBadge(b);
            synth->setVolume(t, ml);
            synth->setPan(t, pn);
            synth->setReverb(t, rv);
            synth->setTrim(t, tr);
            synth->setMute(t, m);
            synth->setSolo(t, s);
            synth->setUseVSTi(t, v);
            synth->setExtMidiOut(t, st.value("ExtMidiOut", "").toString());
            synth->setSpeaker(t, static_cast<SpeakerType>(sp));

            #ifdef __linux__
            if (t >= InstrumentType::VSTi1 && t <= InstrumentType::VSTi4)
                ich->setEnabled(false);
            #endif
        }
        st.endArray();
    }

    connect(&btnPresets, SIGNAL(buttonClicked(int)), this, SLOT(changeSoundfontPresets(int)));
    connect(&signalVstActionMapper, SIGNAL(mapped(QString)), this, SLOT(addFX(QString)));
    connect(&signalBusActionMapper, SIGNAL(mapped(int)), this, SLOT(setBusGroup(int)));
}

SynthMixerDialog::~SynthMixerDialog()
{
    settingTimer.stop();

    // settings
    this->settingValues();
    // ------------------------

    chInstMap.clear();

    for (QMenu *menu : vstVendorMenus) {
        menu->clear();
        delete menu;
    }
    vstVendorMenus.clear();

    if (signalBFXActionMapper != nullptr)
        delete signalBFXActionMapper;

    delete ui;
}

#ifndef __linux__

void SynthMixerDialog::setVSTVendorMenu()
{
    for (QMenu *menu : vstVendorMenus) {
        menu->clear();
        delete menu;
    }
    vstVendorMenus.clear();

    QList<QString> vendors;

    for (const VSTNamePath &vst : synth->VSTList().values()) {
        int index = vendors.indexOf(vst.vstvendor);
        if (index == -1) {
            vendors.append(vst.vstvendor);
            QMenu *menu = new QMenu(vst.vstvendor);
            vstVendorMenus.append(menu);
            index = vendors.count() - 1;
        }
        QAction *action = vstVendorMenus[index]->addAction(vst.vstName);
        connect(action, SIGNAL(triggered()), &signalVstActionMapper, SLOT(map()));
        signalVstActionMapper.setMapping(action, QString::number(vst.uniqueID));
    }

    restoreOrphanFx();   // saved effects that were waiting for their plug-in
}

#endif

void SynthMixerDialog::warnVstNotLoaded(const QString &name)
{
#ifdef __linux__
    Q_UNUSED(name)
#else
    std::string why = bv::lastError();
    QMessageBox::warning(this, QString::fromUtf8("VST"),
        QString::fromUtf8("ไม่สามารถโหลด \"%1\" เป็นเอฟเฟกต์ได้").arg(name)
        + (why.empty() ? QString() : QString("\n\n") + QString::fromUtf8(why.c_str()))
        + QString::fromUtf8("\n\nสาเหตุที่พบบ่อย: เป็น VST instrument (ไม่ใช่เอฟเฟกต์), บิตไม่ตรงกับโปรแกรม (โปรแกรม 32 บิตใช้ได้เฉพาะ 32 บิต, 64 บิตใช้ได้เฉพาะ 64 บิต), หรือปลั๊กอินติดระบบลิขสิทธิ์\nรายละเอียด: โฟลเดอร์ .PTHKKaraoke ไฟล์ vst3.log"));
#endif
}

// put a saved state on an effect that has just been created: the chunk is the complete plug-in state (program /
// parameters would only revert it); without a chunk the program and parameters are used
void SynthMixerDialog::applySavedFx(FX *fx, const OrphanFx &o)
{
    const bool haveChunk = fx->fxType() == FXType::VSTEffects && o.chunk.length() > 0;
    if (haveChunk) {
        fx->setChunk(o.chunk);
    } else {
        fx->setProgram(o.program);
        if (!o.params.isEmpty())
            fx->setParams(o.params);
    }
}

// saved effects that had no plug-in at start-up: load them now if the plug-in has appeared in the VST list
void SynthMixerDialog::restoreOrphanFx()
{
    if (orphanFx.isEmpty() && orphanMaster.isEmpty())
        return;

    struct RestoreGuard { bool &f; RestoreGuard(bool &x) : f(x) { f = true; } ~RestoreGuard() { f = false; } } restoreGuard(restoringFx);
    const QMap<uint, VSTNamePath> list = synth->VSTList();

    for (auto it = orphanFx.begin(); it != orphanFx.end(); ++it)
    {
        QList<OrphanFx> &lst = it.value();
        for (int k = 0; k < lst.count(); )
        {
            if (!list.contains(lst[k].uid)) { ++k; continue; }
            addToMaster = false;
            currentType = static_cast<InstrumentType>(it.key());
            FX *fx = addFX(QString::number(lst[k].uid), lst[k].bypass);
            if (fx) {
                applySavedFx(fx, lst[k]);
                lst.removeAt(k);
            } else {
                ++k;
            }
        }
    }

    for (int k = 0; k < orphanMaster.count(); )
    {
        if (!list.contains(orphanMaster[k].uid)) { ++k; continue; }
        addToMaster = false;
        FX *fx = addMasterFX(orphanMaster[k].uid, orphanMaster[k].bypass);
        if (fx) {
            applySavedFx(fx, orphanMaster[k]);
            orphanMaster.removeAt(k);
        } else {
            ++k;
        }
    }
}

void SynthMixerDialog::setFXToSynth()
{
    struct RestoreGuard { bool &f; RestoreGuard(bool &x) : f(x) { f = true; } ~RestoreGuard() { f = false; } } restoreGuard(restoringFx);
    QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
    st.beginReadArray("SynthMixer");
    for (InstrumentType t : chInstMap.keys())
    {
        st.setArrayIndex(static_cast<int>(t));

        QList<uint> vstUid = st.value("VstUid").value<QList<uint>>();
        QList<bool> vstBypass = st.value("VstBypass").value<QList<bool>>();
        QList<QList<float>> vstParams = st.value("VstParams").value<QList<QList<float>>>();
        QList<int> vstPrograms = st.value("VstPrograms").value<QList<int>>();
        QList<QByteArray> vstChunks = st.value("VstChunks").value<QList<QByteArray>>();

        addToMaster = false;
        this->currentType = t;

        for (int i=0; i<vstUid.count(); i++)
        {
            OrphanFx o;
            o.uid = vstUid[i];
            o.bypass = i < vstBypass.count() ? vstBypass[i] : false;
            o.program = i < vstPrograms.length() ? vstPrograms[i] : 0;
            if (i < vstParams.length())
                o.params = vstParams[i];
            if (i < vstChunks.length())
                o.chunk = vstChunks[i];

            FX *fx = this->addFX(QString::number(vstUid[i]), o.bypass);

            if (fx == nullptr)
            {
                // the plug-in is not in the VST list this time (scan skipped / failed): keep its saved state so the
                // next save does not erase it
                if (o.uid >= BUILTIN_FX_COUNT)
                    orphanFx[static_cast<int>(t)].append(o);
                continue;
            }

            applySavedFx(fx, o);
        }

    }
    st.endArray();

    // master strip effects
    {
        QList<uint> vstUid = st.value("MasterFxUid").value<QList<uint>>();
        QList<bool> vstBypass = st.value("MasterFxBypass").value<QList<bool>>();
        QList<QList<float>> vstParams = st.value("MasterFxParams").value<QList<QList<float>>>();
        QList<int> vstPrograms = st.value("MasterFxPrograms").value<QList<int>>();
        QList<QByteArray> vstChunks = st.value("MasterFxChunks").value<QList<QByteArray>>();

        addToMaster = false;
        for (int i = 0; i < vstUid.count(); i++)
        {
            OrphanFx o;
            o.uid = vstUid[i];
            o.bypass = i < vstBypass.count() ? vstBypass[i] : false;
            o.program = i < vstPrograms.length() ? vstPrograms[i] : 0;
            if (i < vstParams.length())
                o.params = vstParams[i];
            if (i < vstChunks.length())
                o.chunk = vstChunks[i];

            FX *fx = this->addMasterFX(o.uid, o.bypass);
            if (fx == nullptr)
            {
                if (o.uid >= BUILTIN_FX_COUNT)
                    orphanMaster.append(o);
                continue;
            }

            applySavedFx(fx, o);
        }
    }

    // per-strip soundfont choices
    {
        const int n = st.beginReadArray("StripSoundfont");
        for (int i = 0; i < n; i++) {
            st.setArrayIndex(i);
            const int ty = st.value("Type", -1).toInt();
            const QString file = Config::relocatePath(st.value("File").toString());
            if (ty >= 0 && !file.isEmpty())
                synth->setStripSoundfont(static_cast<InstrumentType>(ty), file, false);
        }
        st.endArray();
        if (n > 0)
            synth->setSoundfontPresets(synth->soundfontPresets());
    }
}

// Applies the saved settings (SynthMixer.conf / PTHKKaraoke.conf) to the RUNNING mixer, so a loaded config takes effect
// without restarting the program and the music keeps playing. It mirrors what the constructor and setFXToSynth()
// do at start-up, but only touches what really changed (a stream / mixer is rebuilt only when its device, bus or
// speaker is different), so there is no gap in the sound.
void SynthMixerDialog::reloadFromSettings()
{
    struct RestoreGuard { bool &f; RestoreGuard(bool &x) : f(x) { f = true; } ~RestoreGuard() { f = false; } } restoreGuard(restoringFx);
    skipSave = true;      // the auto-save timer must not write the old state over the new files meanwhile

    QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
    QSettings app(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    st.sync();
    app.sync();

    // ---- effects now loaded are removed, the saved chains are loaded again
    for (InstrumentType t : chInstMap.keys()) {
        int guard = 64;
        while (guard-- > 0 && !synth->instrument(t).FXs.isEmpty()) {
            if (!synth->removeFX(t, 0))
                break;
            chInstMap[t]->removeVSTLabel(0);
        }
    }
    {
        int guard = 64;
        while (guard-- > 0 && !synth->masterFXList().isEmpty()) {
            if (!synth->removeMasterFX(0))
                break;
            masterStrip->removeFxLabel(0);
        }
    }

    // per-strip soundfont choices: forget the old ones, setFXToSynth() reads the saved ones
    {
        const QMap<int, QString> oldSf = synth->stripSoundfonts();
        for (auto it = oldSf.constBegin(); it != oldSf.constEnd(); ++it)
            synth->setStripSoundfont(static_cast<InstrumentType>(it.key()), QString(), false);
    }

    // soundfont preset tab
    setSoundfontPresets(st.value("SoundfontPresets", 0).toInt());

    // effect chains (strips + master) and strip soundfonts
    setFXToSynth();
    synth->setSoundfontPresets(synth->soundfontPresets());

    // ---- bus names
    {
        const QStringList n1 = st.value("BusNames", QStringList()).toStringList();
        const QStringList n2 = st.value("BusFullNames", QStringList()).toStringList();
        if (n1.count() == 16 && n2.count() == 16) {
            const int start = static_cast<int>(InstrumentType::BusGroup1);
            for (int i = 0; i < 16; i++) {
                const InstrumentType type = static_cast<InstrumentType>(start + i);
                if (chInstMap.contains(type))
                    chInstMap[type]->setInstrumentNames(n1[i], n2[i]);
            }
        }
    }

    // ---- master EQ / chorus / reverb (on AND off)
    {
        const bool eqOn = st.value("MasterEqOn", false).toBool();
        const QList<float> eqParams = st.value("MasterEqParams").value<QList<float>>();
        for (auto eq : synth->equalizer31BandFXs()) {
            if (eqParams.count() > 0)
                eq->setParams(eqParams);
            if (eqOn) eq->on(); else eq->off();
        }
        const bool chorusOn = st.value("MasterChorusOn", false).toBool();
        const QList<float> chorusParams = st.value("MasterChorusParams").value<QList<float>>();
        for (auto cr : synth->chorusFXs()) {
            if (chorusParams.count() > 0)
                cr->setParams(chorusParams);
            if (chorusOn) cr->on(); else cr->off();
        }
        const bool reverbOn = st.value("MasterReverbOn", false).toBool();
        const QList<float> reverbParams = st.value("MasterReverbParams").value<QList<float>>();
        for (auto rv : synth->reverbFXs()) {
            if (reverbParams.count() > 0)
                rv->setParams(reverbParams);
            if (reverbOn) rv->on(); else rv->off();
        }
    }

    // ---- LED colours
    LEDVu *vu0 = chInstMap.first()->vuBar();
    const QString bg = st.value("LedBgColor", vu0->backgroundColor().name()).toString();
    const QString o1 = st.value("LedColorOn1", vu0->ledColorOn1().name()).toString();
    const QString o2 = st.value("LedColorOn2", vu0->ledColorOn2().name()).toString();
    const QString o3 = st.value("LedColorOn3", vu0->ledColorOn3().name()).toString();
    const QString f1 = st.value("LedColorOff1", vu0->ledColorOff1().name()).toString();
    const QString f2 = st.value("LedColorOff2", vu0->ledColorOff2().name()).toString();
    const QString f3 = st.value("LedColorOff3", vu0->ledColorOff3().name()).toString();
    const bool sph = st.value("ShowPeakHold", vu0->isShowPeakHold()).toBool();
    const int phm = st.value("PeakHoldMs", vu0->peakHoldMs()).toInt();

    // ---- every strip
    st.beginReadArray("SynthMixer");
    for (InstrumentType t : chInstMap.keys())
    {
        st.setArrayIndex(static_cast<int>(t));
        const int dv = st.value("Device", 0).toInt();
        const int b = st.value("Bus", -1).toInt();
        const int ml = st.value("Volume", 50).toInt();
        const int pn = st.value("Pan", 0).toInt();
        const int rv = st.value("Reverb", 0).toInt();
        const double tr = st.value("Trim", 0.0).toDouble();
        const bool m = st.value("Mute", false).toBool();
        const bool s = st.value("Solo", false).toBool();
        const int v = st.value("VSTi", -1).toInt();
        const int sp = st.value("Speaker", 0).toInt();
        const QString ext = st.value("ExtMidiOut", "").toString();

        InstCh *ich = chInstMap[t];
        ich->setSliderLevel(ml);
        ich->setPanValue(pn);
        ich->setReverbValue(rv);
        ich->setTrimValue(tr);
        ich->setMuteButton(m);
        ich->setSoloButton(s);

        LEDVu *vBar = ich->vuBar();
        vBar->setBackGroundColor(QColor(bg));
        vBar->setLedColorOn1(QColor(o1));
        vBar->setLedColorOn2(QColor(o2));
        vBar->setLedColorOn3(QColor(o3));
        vBar->setLedColorOff1(QColor(f1));
        vBar->setLedColorOff2(QColor(f2));
        vBar->setLedColorOff3(QColor(f3));
        vBar->setShowPeakHold(sph);
        vBar->setPeakHoldMs(phm);

        // routing: only rebuilt when it is really different
        if (synth->device(t) != dv && synth->busGroup(t) != -1)
            synth->setBusGroup(t, -1);          // setDevice() is ignored while the strip is on a bus
        if (synth->device(t) != dv)
            synth->setDevice(t, dv);
        if (synth->busGroup(t) != b)
            synth->setBusGroup(t, b);
        ich->setBusBadge(b);

        synth->setVolume(t, ml);
        synth->setPan(t, pn);
        synth->setReverb(t, rv);
        synth->setTrim(t, tr);
        synth->setMute(t, m);
        synth->setSolo(t, s);
        if (synth->useVSTi(t) != v)
            synth->setUseVSTi(t, v);
        if (synth->extMidiOut(t) != ext)
            synth->setExtMidiOut(t, ext);
        if (static_cast<int>(synth->speaker(t)) != sp)
            synth->setSpeaker(t, static_cast<SpeakerType>(sp));
    }
    st.endArray();

    // ---- master volume / mute (application config)
    {
        const int vl = app.value("MidiVolume", 50).toInt();
        const bool mm = app.value("MasterMute", false).toBool();
        masterStrip->setLevel(vl);
        player->setVolume(vl);
        masterStrip->btnMute->setChecked(mm);    // also tells the player (same slot as a click)
        player->setMasterMute(mm);
    }

    // ---- drum tuning and the quick buttons
    for (int g = 0; g < MidiSynthesizer::DrumTuneGroupCount; g++)
        synth->setDrumTune(g, app.value(QString("DrumTune%1").arg(g), 0).toInt());
    synth->setIgnoreDrumPitch(app.value("IgnoreDrumPitch", true).toBool());
    syncQuickChecks();

    skipSave = false;
}

InstCh *SynthMixerDialog::mixChannel(InstrumentType t)
{
    return chInstMap[t];
}

QMap<InstrumentType, InstCh *> SynthMixerDialog::mixChannelMap()
{
    return chInstMap;
}

QMap<InstrumentType, InstCh *> *SynthMixerDialog::mixChannelMapPtr()
{
    return &chInstMap;
}

void SynthMixerDialog::settingValues()
{
    if (skipSave)
        return;

    {   // master volume lives in the application config
        QSettings app(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        app.setValue("MidiVolume", player->volume());
    }

    QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
    st.setValue("Size", this->size());
    st.setValue("SplitterSize", QVariant::fromValue(ui->splitter->sizes()));
    st.setValue("ScrollInstrument", ui->scrollArea->horizontalScrollBar()->value());
    st.setValue("ScrollBusGroup", ui->scrollArea_2->horizontalScrollBar()->value());

    st.setValue("WindowNoParent", parent() == 0);
    st.setValue("WindowStaysOnTop", staysOnTop);

    // soundfont presets
    st.setValue("SoundfontPresets", synth->soundfontPresets());

    // Master Eq
    auto eq = synth->equalizer31BandFXs()[0];
    st.setValue("MasterEqOn", eq->isOn());
    st.setValue("MasterEqParams", QVariant::fromValue(eq->params()));

    // Master Chorus
    auto chorus = synth->chorusFXs()[0];
    st.setValue("MasterChorusOn", chorus->isOn());
    st.setValue("MasterChorusParams", QVariant::fromValue(chorus->params()));

    // Master Reverb
    auto reverb = synth->reverbFXs()[0];
    st.setValue("MasterReverbOn", reverb->isOn());
    st.setValue("MasterReverbParams", QVariant::fromValue(reverb->params()));

    // Per-strip soundfont choices (by file path)
    st.beginWriteArray("StripSoundfont");
    {
        int n = 0;
        const QMap<int, QString> ssf = synth->stripSoundfonts();
        for (auto it = ssf.constBegin(); it != ssf.constEnd(); ++it, ++n) {
            st.setArrayIndex(n);
            st.setValue("Type", it.key());
            st.setValue("File", it.value());
        }
    }
    st.endArray();

    // Master strip effects
    {
        QList<uint> uids = synth->masterFxUids();
        QList<bool> byp = synth->masterFxBypass();
        QList<int> progs = synth->masterFxProgram();
        QList<QList<float>> pars = synth->masterFxParams();
        QList<QByteArray> chunks = synth->masterFxChunks();
        for (const OrphanFx &o : orphanMaster) {   // effects whose plug-in was missing at start-up stay saved
            uids << o.uid; byp << o.bypass; progs << o.program; pars << o.params; chunks << o.chunk;
        }
        st.setValue("MasterFxUid", QVariant::fromValue(uids));
        st.setValue("MasterFxBypass", QVariant::fromValue(byp));
        st.setValue("MasterFxPrograms", QVariant::fromValue(progs));
        st.setValue("MasterFxParams", QVariant::fromValue(pars));
        st.setValue("MasterFxChunks", QVariant::fromValue(chunks));
    }


    LEDVu *vu = chInstMap.first()->vuBar();
    st.setValue("LedColorOn1", vu->ledColorOn1().name());
    st.setValue("LedColorOn2", vu->ledColorOn2().name());
    st.setValue("LedColorOn3", vu->ledColorOn3().name());
    st.setValue("LedColorOff1", vu->ledColorOff1().name());
    st.setValue("LedColorOff2", vu->ledColorOff2().name());
    st.setValue("LedColorOff3", vu->ledColorOff3().name());
    st.setValue("ShowPeakHold", vu->isShowPeakHold());
    st.setValue("PeakHoldMs", vu->peakHoldMs());


    #ifndef __linux__
    st.beginWriteArray("VSTiGroup");
    for (int i=0; i<synth->HANDLE_VSTI_COUNT; i++)
    {
        st.setArrayIndex(i);

        st.setValue("VstiFilePath", synth->vstiFile(i));
        st.setValue("VstiPrograms", synth->vstiProgram(i));
        st.setValue("VstiParams", QVariant::fromValue(synth->vstiParams(i)));
        st.setValue("VstiChunk", synth->vstiChunk(i));
    }
    st.endArray();
    #endif


    QStringList busNames;
    QStringList busFullNames;
    int start = static_cast<int>(InstrumentType::BusGroup1);
    for (int i=0; i<16; i++) {
        InstrumentType type = static_cast<InstrumentType>(start + i);
        busNames.append(chInstMap[type]->instrumentName());
        busFullNames.append(chInstMap[type]->fullInstrumentName());
    }
    st.setValue("BusNames", busNames);
    st.setValue("BusFullNames", busFullNames);


    st.beginWriteArray("SynthMixer");
    for (InstrumentType t : chInstMap.keys())
    {
        st.setArrayIndex(static_cast<int>(t));
        st.setValue("Volume", synth->volume(t));
        st.setValue("Pan", synth->pan(t));
        st.setValue("Reverb", synth->reverb(t));
        st.setValue("Trim", synth->trim(t));
        st.setValue("Mute", synth->isMute(t));
        st.setValue("Solo", synth->isSolo(t));
        st.setValue("Bus", synth->busGroup(t));
        st.setValue("VSTi", synth->useVSTi(t));
        st.setValue("ExtMidiOut", synth->extMidiOut(t));

        QList<uint> fxU = synth->fxUids(t);
        QList<bool> fxB = synth->fxBypass(t);
        QList<int> fxP = synth->fxProgram(t);
        QList<QList<float>> fxA = synth->fxParams(t);
        QList<QByteArray> fxC = synth->fxChunks(t);
        for (const OrphanFx &o : orphanFx.value(static_cast<int>(t))) {   // plug-in was missing at start-up: keep its state
            fxU << o.uid; fxB << o.bypass; fxP << o.program; fxA << o.params; fxC << o.chunk;
        }
        st.setValue("VstUid", QVariant::fromValue(fxU));
        st.setValue("VstBypass", QVariant::fromValue(fxB));
        st.setValue("VstPrograms", QVariant::fromValue(fxP));
        st.setValue("VstParams", QVariant::fromValue(fxA));
        st.setValue("VstChunks", QVariant::fromValue(fxC));
    }
    st.endArray();
}

void SynthMixerDialog::setSoundfontPresets(int presets)
{
    changeSoundfontPresets(presets);
}

void SynthMixerDialog::showEqDialog()
{
    if (Equalizer31BandDialog::isOpenned())
        return;

    Equalizer31BandDialog *dlg = new Equalizer31BandDialog(this, synth->equalizer31BandFXs());
    dlg->adjustSize();
    dlg->setFixedSize(dlg->size());
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    dlg->show();
}

void SynthMixerDialog::showReverbDialog()
{
    if (Reverb2Dialog::isOpenned())
        return;

    Reverb2Dialog *dlg = new Reverb2Dialog(this, synth->reverbFXs());
    dlg->adjustSize();
    dlg->setFixedSize(dlg->size());
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    dlg->show();
}

void SynthMixerDialog::showChorusDialog()
{
    if (Chorus2Dialog::isOpenned())
        return;

    Chorus2Dialog *dlg = new Chorus2Dialog(this, synth->chorusFXs());
    dlg->adjustSize();
    dlg->setFixedSize(dlg->size());
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    dlg->show();
}

void SynthMixerDialog::setMute(InstrumentType t, bool m)
{
    synth->setMute(t, m);

    if (!isBusType(t))
        return;

    for (InstrumentType type : synth->instrumentMap().keys()) {
        if (synth->instrument(type).bus != (static_cast<int>(t) - synth->HANDLE_MIDI_COUNT))
            continue;
        synth->setMute(type, m);
        chInstMap[type]->setMuteButton(m);
    }
}

void SynthMixerDialog::setSolo(InstrumentType t, bool s)
{
    synth->setSolo(t, s);

    if (!isBusType(t))
        return;

    for (InstrumentType type : synth->instrumentMap().keys()) {
        if (synth->instrument(type).bus != (static_cast<int>(t) - synth->HANDLE_MIDI_COUNT))
            continue;
        synth->setSolo(type, s);
        chInstMap[type]->setSoloButton(s);
    }
}

void SynthMixerDialog::setMixLevel(InstrumentType t, int level)
{
    synth->setVolume(t, level);
}

void SynthMixerDialog::resetMixLevel(InstrumentType t)
{
    chInstMap[t]->setSliderLevel(50);
    synth->setVolume(t, 50);
}

void SynthMixerDialog::showAudioBands(const int *bands, int count, int total)
{
    using T = InstrumentType;
    static const QVector<QVector<T>> byBand = {
        { T::Bass, T::BassDrum, T::LowTom },                                                     // 60-250 Hz
        { T::Piano, T::Organ, T::Accordion, T::AcousticGuitarNylon, T::AcousticGuitarSteel,
          T::Strings, T::Ensemble, T::SynthPad, T::MidTom, T::Conga },                           // 250-700 Hz
        { T::ElectricGuitarJazz, T::ElectricGuitarClean, T::OverdrivenGuitar, T::DistortionGuitar,
          T::Trumpet, T::Brass, T::Saxophone, T::Reed, T::Pipe, T::SynthLead, T::Ethnic,
          T::HighTom, T::Bongo, T::Timbale },                                                    // 0.7-2.5 kHz
        { T::Snare, T::SideStick, T::ChromaticPercussion, T::HarmonicsGuitar, T::SynthBrass,
          T::Cowbell, T::SynthEffects },                                                         // 2.5-6 kHz
        { T::Hihat, T::CrashCymbal, T::RideCymbal, T::SmallCupShapedCymbals, T::ThaiChap,
          T::Percussive, T::PercussionEtc, T::SoundEffects },                                   // 6-14 kHz
    };
    for (int b = 0; b < count && b < byBand.size(); ++b) {
        if (bands[b] <= 0)
            continue;
        for (T t : byBand[b])
            if (InstCh *c = chInstMap.value(t, nullptr))
                c->peak(bands[b]);
    }
    if (masterStrip && total > 0)
        masterStrip->peak(qMin(127, total));
}

void SynthMixerDialog::showPeakVU(InstrumentType t, int bus,  int ch, int note, int velocity)
{
    chInstMap[t]->peak(velocity);
    if (masterStrip)
        masterStrip->peak(qMin(127, int(velocity * player->effectiveGain())));
    if(bus > -1) {
        InstrumentType bType = static_cast<InstrumentType>(bus + synth->HANDLE_MIDI_COUNT);
        chInstMap[bType]->peak(velocity);
    }
}

// crisp "hamburger" icon for the Menu button, drawn in the current text color
void SynthMixerDialog::updateMenuIcon()
{
    const qreal dpr = devicePixelRatioF();
    const int sz = 16;
    QPixmap pm(int(sz * dpr), int(sz * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(palette().color(QPalette::ButtonText), 2, Qt::SolidLine, Qt::FlatCap);
    p.setPen(pen);
    for (int y : { 3, 8, 13 })
        p.drawLine(QPointF(1, y), QPointF(sz - 1, y));
    p.end();
    ui->btnMenu->setIcon(QIcon(pm));
    ui->btnMenu->setIconSize(QSize(sz, sz));
}

void SynthMixerDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        updateMenuIcon();

        // the lit colour of the toggle / page buttons follows the theme (orange in Dark Orange)
        const QString quick = quickButtonCss();
        for (QToolButton *b : { chkLockDrum, chkLockKick, chkLockSnare, chkLockBass, chkNoTune, btnDrumTune })
            if (b && b->styleSheet() != quick)
                b->setStyleSheet(quick);
        if (QWidget *lp = findChild<QWidget *>("leftPanel")) {
            const QString panel = panelButtonCss();
            if (lp->styleSheet() != panel)
                lp->setStyleSheet(panel);
        }
    }
}

void SynthMixerDialog::syncQuickChecks()
{
    if (!chkLockDrum)
        return;
    const QList<QToolButton *> boxes = { chkLockDrum, chkLockKick, chkLockSnare, chkLockBass, chkNoTune };
    for (QToolButton *b : boxes)
        b->blockSignals(true);
    chkLockDrum->setChecked(player->isLockDrum());
    chkLockSnare->setChecked(player->isLockSnare());
    chkLockKick->setChecked(player->isLockKick());
    chkLockBass->setChecked(player->isLockBass());
    chkNoTune->setChecked(player->midiSynthesizer()->isIgnoreDrumPitch());
    for (QToolButton *b : boxes)
        b->blockSignals(false);

    // tooltips show which number each lock is set to
    {
        const QStringList kits = MidiHelper::drumKitNumberNames();
        const QStringList snares = MidiHelper::snareNumberName();
        const QStringList gm = MidiHelper::GMInstrumentNumberNames();
        const int kit = player->lockDrumNumber();
        const int sn  = player->lockSnareNumber();
        const int bs  = player->lockBassNumber();
        chkLockDrum->setToolTip(tr("ล็อกเสียงกลอง: %1 (คลิกเพื่อเลือกเบอร์)")
                                    .arg(kit >= 0 && kit < kits.count() ? kits[kit] : QString::number(kit)));
        chkLockSnare->setToolTip(tr("ล็อกเสียงสแนร์: %1 (คลิกเพื่อเลือกเบอร์)")
                                     .arg(MidiHelper::snareNumbers().indexOf(sn) >= 0 && MidiHelper::snareNumbers().indexOf(sn) < snares.count()
                                              ? snares[MidiHelper::snareNumbers().indexOf(sn)] : QString::number(sn)));
        const QStringList kicks = MidiHelper::kickNumberName();
        chkLockKick->setToolTip(tr("ล็อกเสียงกระเดื่อง: %1 (คลิกเพื่อเลือกเบอร์)")
                                    .arg(player->lockKickNumber() == 35 ? kicks[0] : kicks[1]));
        chkLockBass->setToolTip(tr("ล็อกเสียงเบส: %1 (คลิกเพื่อเลือกเบอร์)")
                                    .arg(bs >= 0 && bs < gm.count() ? gm[bs] : QString::number(bs)));
    }
}

void SynthMixerDialog::showEvent(QShowEvent *ev)
{
    // Coming back from the karaoke auto-hide the window is exactly as it was left: re-fitting and re-aligning it
    // on every return made it lag and slowly pushed the frames out of place. Only a real first show does that.
    // Also "quick" when the window comes back from being minimised (spontaneous show) or while the strips are hidden
    // (master-only view): fitToContent() there measured the layout without the hidden panels and made the window
    // too short, which cut the master frame and pushed MUTE out of view.
    const bool quick = (property("kmAutoRestore").toBool() || (ev && ev->spontaneous()) || stripsHidden) && fittedOnce;
    setProperty("kmAutoRestore", false);
    if (!quick) {
        fitToContent();
        fittedOnce = true;
        if (foldAtShow) {   // saved as folded: start from the measured full-height layout, then fold it
            foldAtShow = false;
            setKnobsVisible(false, false);
        }
        QTimer::singleShot(0, this, [this]() { alignMasterMute(); });   // after the layout has settled
        QTimer::singleShot(150, this, [this]() { alignMasterMute(); }); // and once more when everything is final
    } else {
        QTimer::singleShot(0, this, [this]() { alignMasterMute(); });   // cheap and repeatable: keeps the frame on the line
    }
    if (FrameOverlay *fo = findChild<FrameOverlay *>())   // keep the corner overlay above every strip
        fo->raise();
    syncQuickChecks();
    connect(synth, SIGNAL(noteOnSended(InstrumentType,int,int,int,int)),
            this, SLOT(showPeakVU(InstrumentType,int,int,int,int)), Qt::UniqueConnection);
}

// Fit the window to its content: strip areas as tall as their strips, window as tall as the layout.
void SynthMixerDialog::fitToContent()
{
    // The window height used to be frozen in the constructor, before the side panels, master strip and
    // button rows existed (and before the user's font / DPI scaling was applied). On some PCs that left the
    // window shorter than its content, so the bottom (names, scroll bar, VST button) was cut off.
    // Fit it to the real content now, on every show.
    // each strip area must be as tall as its strips (+ the slim scroll bar), otherwise the strips can be
    // scrolled up / down inside it
    for (QScrollArea *sa : { ui->scrollArea, ui->scrollArea_2 }) {
        if (!sa->widget())
            continue;
        const int h = sa->widget()->minimumSizeHint().height() + sa->horizontalScrollBar()->sizeHint().height()
                      + 2 * sa->frameWidth();
        // fixed height (min = max): a strip area must never be stretched taller than its strips. Without the
        // maximum, the layout's size hint followed whatever height the area had (e.g. after the window was
        // re-created by the "always on top" switch) and the whole mixer grew taller than the screen.
        sa->widget()->resize(sa->widget()->width(), sa->widget()->minimumSizeHint().height());
        sa->setMinimumHeight(h);
        sa->setMaximumHeight(h);
        sa->verticalScrollBar()->setValue(0);
    }
    // the master panel is taller than the strips by the slim scroll bar below them; leave that band empty so
    // the master's MUTE lines up with the strips' name tags
    if (masterStrip && masterStrip->parentWidget() && masterStrip->parentWidget()->layout() && ui->scrollArea->widget()) {
        const int gap = qMax(0, ui->scrollArea->height() - ui->scrollArea->widget()->height() - ui->scrollArea->frameWidth());
        masterStrip->parentWidget()->layout()->setContentsMargins(0, 0, 0, 0);   // frame extends to the scroll bar line
        masterStrip->setMaximumHeight(QWIDGETSIZE_MAX);   // alignMasterMute() sets the real limit again
        (void)gap;
    }
    if (QLayout *lay = layout()) {
        lay->activate();
        const int need = qMax(lay->sizeHint().height(), lay->minimumSize().height());
        if (need > 0 && (height() != need || minimumHeight() != need || maximumHeight() != need)) {
            setMinimumHeight(need);
            setMaximumHeight(need);
            if (!isMaximized() && !isFullScreen())
                resize(width(), need);
        }
    }
}

// Fold / unfold the Pan and Reverb knobs of every strip with a short slide (the mixer window gets shorter,
// so less of the karaoke window is covered).  Every container is changed by the same number of pixels as the
// strips, so nothing has to be measured again.
void SynthMixerDialog::setKnobsVisible(bool show, bool animate)
{
    const double target = show ? 0.0 : 1.0;
    if (!knobAnim) {
        knobAnim = new QVariantAnimation(this);
        knobAnim->setDuration(200);
        knobAnim->setEasingCurve(QEasingCurve::InOutQuad);
        connect(knobAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            applyKnobProgress(v.toDouble());
        });
        connect(knobAnim, &QVariantAnimation::finished, this, [this]() {
            syncWindowDrop(knobDrop);   // fold: the window shrinks once, at the end
            QTimer::singleShot(0, this, [this]() { alignMasterMute(); });
            QTimer::singleShot(150, this, [this]() { alignMasterMute(); });
        });
    }
    knobAnim->stop();
    if (!animate || !isVisible()) {
        applyKnobProgress(target);
        syncWindowDrop(knobDrop);
        QTimer::singleShot(0, this, [this]() { alignMasterMute(); });
        return;
    }
    if (!show)
        syncWindowDrop(winDrop);          // nothing to do yet: window shrinks when the fold has finished
    else
        syncWindowDrop(0);                // unfold: the window grows at once, the strips then slide into the room
    knobAnim->setStartValue(knobProgress);
    knobAnim->setEndValue(target);
    knobAnim->start();
}

// Move the window height by the difference between the pixels it is currently shortened by (winDrop) and `drop`.
// Resizing the native window on every animation frame made the whole mixer flicker, so it is done only at the
// start (unfold) or the end (fold) of the slide; the frames in between only move the contents.
void SynthMixerDialog::syncWindowDrop(int drop)
{
    const int d = drop - winDrop;
    if (d == 0)
        return;
    winDrop = drop;
    if (isMaximized() || isFullScreen())
        return;
    // lift the limits first so the resize can't be clamped by the old ones
    setMinimumHeight(qMax(0, minimumHeight() - d));
    setMaximumHeight(qMax(minimumHeight(), maximumHeight() - d));
    resize(width(), qMax(minimumHeight(), height() - d));
}

void SynthMixerDialog::applyKnobProgress(double p)
{
    setUpdatesEnabled(false);
    int newDrop = knobDrop;
    for (InstCh *c : chInstMap.values())
        if (c)
            newDrop = c->setKnobCollapse(p);
    knobProgress = p;
    if (masterStrip)
        masterStrip->setCollapse(newDrop);
    const int delta = newDrop - knobDrop;
    if (delta == 0) {
        setUpdatesEnabled(true);
        return;
    }
    knobDrop = newDrop;

    // the strip areas and their contents follow by the same amount
    for (QScrollArea *sa : { ui->scrollArea, ui->scrollArea_2 }) {
        if (sa->minimumHeight() > 0) {
            const int nh = qMax(0, sa->minimumHeight() - delta);
            sa->setMinimumHeight(nh);
            sa->setMaximumHeight(nh);
        }
        if (QWidget *w = sa->widget())
            w->resize(w->width(), qMax(1, w->height() - delta));
    }
    setUpdatesEnabled(true);
    update();
}

int SynthMixerDialog::masterWantHeight() const
{
    if (!masterStrip || !ui->scrollArea || !ui->scrollArea->isVisible())
        return -1;
    const QWidget *bar = ui->scrollArea;
    const int top = masterStrip->mapTo(const_cast<SynthMixerDialog *>(this), QPoint(0, 0)).y();
    return bar->mapTo(const_cast<SynthMixerDialog *>(this), QPoint(0, 0)).y() + bar->height() - top;
}

// Put the master strip's bottom edge on the strips' bottom edge, and the MUTE button exactly on the name-tag row
// (same top, same height) whatever the font / DPI of this PC. MUTE is placed by absolute position, so it can
// never drift away from the tags.
void SynthMixerDialog::alignMasterMute()
{
    static bool busy = false;
    if (busy || !masterStrip || !masterStrip->btnMute || !masterStrip->parentWidget() || !masterStrip->parentWidget()->layout())
        return;
    busy = true;

    InstCh *ref = nullptr;
    for (InstCh *ch : chInstMap.values()) {
        if (ch && ch->nameTag() && ch->nameTag()->isVisible()) { ref = ch; break; }
    }
    if (ref) {
        QWidget *tag = ref->nameTag();
        QWidget *frame = ref->parentWidget() ? ref->parentWidget() : ref; (void)frame;
        QLayout *rl = masterStrip->parentWidget()->layout();

        // The master frame ends exactly on the strip area's bottom line (scroll bar line): one calculated height,
        // applied once - no iteration, nothing that can fight with another correction (that made it flicker).
        {
            rl->setContentsMargins(0, 0, 0, 0);
            layout()->activate();
            const int want = masterWantHeight();
            if (want > 150 && masterStrip->maximumHeight() != want) {
                // the contents are placed by position, so the strip may be shorter than its nominal minimum
                masterStrip->setMinimumHeight(qMin(masterStrip->minimumHeight(), want));
                masterStrip->setMaximumHeight(want);
                layout()->activate();
            }
        }

        // Line the master's FX box, fader, meter and "0.0 dB" read-out up with the strips'. Every size is
        // computed from positions that do not depend on the sizes being set (no feedback, no iteration), so
        // calling this again gives the same result.
        FaderSlider *refSl = ref->findChild<FaderSlider *>(QStringLiteral("slider"));
        QWidget *refDb = ref->dbLabel();   // the strip's real (visible) read-out label
        if (refSl && refDb && masterStrip->layout()) {
            // The reference strip's own layout may not have been run yet (its slider then still has its
            // minimum height, 121, and the master got that too - shorter than the strips' real faders):
            // lay out every ancestor, outermost first, then the strip itself.
            QList<QWidget *> chain;
            for (QWidget *w = ref; w && w != this; w = w->parentWidget())
                chain.prepend(w);
            layout()->activate();
            for (QWidget *w : chain)
                if (w->layout())
                    w->layout()->activate();
            masterStrip->layout()->activate();
        }
        if (refSl && refDb && refSl->height() >= 60 && masterStrip->layout()) {
            const int sp    = masterStrip->layout()->spacing();
            const int rTop  = refSl->mapTo(this, QPoint(0, 0)).y();                       // strips' fader top
            const int fxTop = masterStrip->fxList->mapTo(this, QPoint(0, 0)).y();         // does not depend on the FX height
            masterStrip->alignLayout(qBound(20, rTop - sp - fxTop, 140));
            layout()->activate();

            // fader / meter / read-out are placed by absolute position, copied from the reference strip
            const int sTop  = masterStrip->mapTo(this, QPoint(0, 0)).y();
            const int tagY  = tag->mapTo(this, QPoint(0, 0)).y() - sTop;
            const QRect fR(3, rTop - sTop, 48, refSl->height());
            const QRect dR(3, refDb->mapTo(this, QPoint(0, 0)).y() - sTop, masterStrip->width() - 6, refDb->height());
            const int tTop = dR.bottom() + 4;
            // the TRIM block must end ABOVE the HIDE button (which sits right over MUTE): otherwise on a short window
            // its lowest button (0 dB) is drawn on top of HIDE
            const int hideTopY = tagY - tag->height() - 6;
            const QRect tR(3, tTop, masterStrip->width() - 6, qMax(0, hideTopY - 4 - tTop));
            // sanity check: every part must lie inside the master frame. On some PCs (small screen, odd DPI) the
            // reference strip is measured before it has been laid out and the numbers come out wrong: the master
            // then shows an empty frame. In that case use the built-in placement instead.
            const int mH = masterStrip->height(), mW = masterStrip->width();
            const bool colOk = fR.width() > 10 && fR.height() >= 60 && fR.y() >= 0 && fR.bottom() < mH
                            && dR.y() >= 0 && dR.bottom() < mH && dR.width() > 10
                            && tR.y() >= 0 && tR.bottom() < mH && mW > 20;
            if (colOk) {
                masterStrip->placeColumn(fR, dR, tR);
            } else {
                logLayout(QStringLiteral("bad column placement -> default"));
                masterStrip->useDefaultPlacement(true, false, false);
            }
        }
        layout()->activate();

        const int tagTop = tag->mapTo(this, QPoint(0, 0)).y();
        const int stripTop = masterStrip->mapTo(this, QPoint(0, 0)).y();
        {
            const int mw = qMin(tag->width(), masterStrip->width() - 2);
            const QRect mR((masterStrip->width() - mw) / 2, tagTop - stripTop, mw, tag->height());                       // same size as a strip's name tag
            // HIDE sits right above MUTE, same size (the empty box under the read-out / TRIM block)
            const QRect hR((masterStrip->width() - mw) / 2, tagTop - stripTop - tag->height() - 6, mw, tag->height());
            const int mH2 = masterStrip->height();
            const bool muteOk = mR.width() > 10 && mR.height() > 8 && mR.y() >= 0 && mR.bottom() < mH2;
            const bool hideOk = hR.width() > 10 && hR.height() > 8 && hR.y() >= 0 && hR.bottom() < mH2;
            if (muteOk)
                masterStrip->placeMute(mR);
            else {
                logLayout(QStringLiteral("bad mute placement -> default"));
                masterStrip->useDefaultPlacement(false, true, false);
            }
            if (hideOk)
                masterStrip->placeHide(hR);
            else
                masterStrip->useDefaultPlacement(false, false, true);
        }   // same size as a strip's name tag
    }
    busy = false;
}

// Master-only view: hide the left panel, both strip areas, the preset tabs and the quick switches so only the title
// bar, the Menu button and the master strip stay. The window keeps its height (the master strip is placed for it)
// and only gets narrower, keeping its right edge where it was; showing everything again restores the old width.
void SynthMixerDialog::setStripsHidden(bool hide)
{
    if (hide == stripsHidden)
        return;
    stripsHidden = hide;
    QList<QWidget *> ws;
    if (QWidget *lp = findChild<QWidget *>("leftPanel"))
        ws << lp;
    ws << ui->splitter;
    ws << ui->btnPreset0 << ui->btnPreset1 << ui->btnPreset2 << ui->btnPreset3 << ui->btnPreset4 << ui->btnPreset5
       << ui->btnPreset6 << ui->btnPreset7 << ui->btnPreset8 << ui->btnPreset9 << ui->btnPreset10;
    ws << chkLockDrum << chkLockSnare << chkLockKick << chkLockBass << chkNoTune << btnDrumTune;
    if (masterStrip && masterStrip->btnHide && masterStrip->btnHide->isChecked() != hide) {
        masterStrip->btnHide->blockSignals(true);
        masterStrip->btnHide->setChecked(hide);
        masterStrip->btnHide->blockSignals(false);
    }
    setUpdatesEnabled(false);
    if (hide) {
        hideSavedGeo = geometry();
        hideSavedMinW = minimumWidth();
        for (QWidget *w : ws)
            if (w) w->hide();
        setMinimumWidth(0);
        layout()->activate();
        const int w = qMax(layout()->minimumSize().width(), 260);
        const QRect g = hideSavedGeo;
        setGeometry(g.right() + 1 - w, g.y(), w, g.height());
        hideNarrowGeo = geometry();   // the real one: the window may refuse to get as narrow as asked
    } else {
        for (QWidget *w : ws)
            if (w) w->show();
        setMinimumWidth(hideSavedMinW);
        // show everything again, centred horizontally on the screen the window is on (keeps its height position)
        const QRect g = geometry();
        const int w = hideSavedGeo.width() > 0 ? hideSavedGeo.width() : 970;
        QScreen *scr = QGuiApplication::screenAt(g.center());
        if (!scr) scr = QGuiApplication::primaryScreen();
        const QRect av = scr ? scr->availableGeometry() : QRect(0, 0, 1920, 1080);
        int x = av.x() + (av.width() - w) / 2;
        if (w >= av.width()) x = av.x();
        setGeometry(x, g.y(), w, g.height());
        layout()->activate();
        // the same fitting a normal show does: strip areas, window height and master frame come out exactly as
        // at start-up however many times HIDE was pressed (without it the height / frame drifted a little each time)
        fitToContent();
        if (!isMaximized() && !isFullScreen())
            setGeometry(x, g.y(), w, height());
        QTimer::singleShot(0, this, [this]() { alignMasterMute(); });
        QTimer::singleShot(150, this, [this]() { alignMasterMute(); });
    }
    setUpdatesEnabled(true);
    if (FrameOverlay *fo = findChild<FrameOverlay *>())
        fo->raise();
    update();
}

// Self-healing: other things (the browser opening, the auto-hide coming back, a font / DPI change, ...) can resize the
// window or its layout. After any such event, check - once, shortly after - that the master frame still ends on the
// strip line, and re-align only if it does not. A few attempts at most, so it can never loop.
bool SynthMixerDialog::event(QEvent *e)
{
    const bool r = QDialog::event(e);
    switch (e->type()) {
    case QEvent::LayoutRequest:
    case QEvent::Resize:
    case QEvent::WindowActivate:
    case QEvent::Show:
        scheduleAlign();
        break;
    default:
        break;
    }
    return r;
}

// ~/.PTHKKaraoke/layout.log : numbers of the master / strip geometry when a correction was needed (for support)
void SynthMixerDialog::logLayout(const QString &why)
{
    if (!masterStrip || !ui->scrollArea)
        return;
    QDir().mkpath(QDir::homePath() + QStringLiteral("/.PTHKKaraoke"));
    QFile f(QDir::homePath() + QStringLiteral("/.PTHKKaraoke/layout.log"));
    if (f.size() > 64 * 1024)
        f.remove();
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    QWidget *bar = ui->scrollArea;
    const int barBot = bar->mapTo(this, QPoint(0, 0)).y() + bar->height();
    const int mTop = masterStrip->mapTo(this, QPoint(0, 0)).y();
    QTextStream t(&f);
    t.setCodec("UTF-8");
    t << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")) << "  " << why
      << "  win=" << width() << "x" << height() << " barBot=" << barBot << " masterTop=" << mTop
      << " masterH=" << masterStrip->height() << " min=" << masterStrip->minimumHeight() << " max=" << masterStrip->maximumHeight()
      << " rightMargin=" << (masterStrip->parentWidget() && masterStrip->parentWidget()->layout() ? masterStrip->parentWidget()->layout()->contentsMargins().bottom() : -1)
      << " knobDrop=" << knobDrop << " winDrop=" << winDrop << "\n";
}

void SynthMixerDialog::scheduleAlign()
{
    if (alignPending || stripsHidden || !masterStrip)
        return;
    alignPending = true;
    QTimer::singleShot(80, this, [this]() {
        alignPending = false;
        if (!isVisible() || stripsHidden || !masterStrip || !ui->scrollArea->isVisible())
            return;
        const int want = masterWantHeight();
        if (want <= 150 || masterStrip->maximumHeight() == want) {   // already set to the right height
            alignTries = 0;
            return;
        }
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (alignTries == 0 || now - alignStamp > 3000) {
            alignTries = 0;
            alignStamp = now;
        }
        if (alignTries >= 4)
            return;
        alignTries++;
        logLayout(QStringLiteral("event-fix"));
        alignMasterMute();
        logLayout(QStringLiteral("  after"));
    });

    if (!alignWatch) {
        alignWatch = new QTimer(this);
        alignWatch->setInterval(400);
        connect(alignWatch, &QTimer::timeout, this, [this]() {
            if (!isVisible() || stripsHidden || !masterStrip || !ui->scrollArea->isVisible())
                return;
            const int want0 = masterWantHeight();
            if (want0 <= 150 || masterStrip->maximumHeight() == want0) {
                watchFails = 0;
                alignWatch->setInterval(400);
                return;
            }
            logLayout(QStringLiteral("watchdog-fix"));
            alignMasterMute();
            logLayout(QStringLiteral("  after"));
            if (masterStrip->maximumHeight() != masterWantHeight()) {
                if (++watchFails >= 5)
                    alignWatch->setInterval(5000);   // cannot be fixed: do not keep poking it
            } else {
                watchFails = 0;
            }
        });
        alignWatch->start();
    }
}

void SynthMixerDialog::hideEvent(QHideEvent *event)
{
    disconnect(synth, SIGNAL(noteOnSended(InstrumentType,int,int,int,int)),
            this, SLOT(showPeakVU(InstrumentType,int,int,int,int)));
}

// Left: page buttons (1-16 / 17-32 / 33-46 / all) and the phone-remote button.
// Right: vertical master volume.  The console look follows the dark theme.
void SynthMixerDialog::buildSidePanels()
{
    // Buttons are transparent: GroupFrames paints the frame / hover light behind them.
    const QString panelCss = panelButtonCss();

    // ---- left panel
    QWidget *left = new QWidget(this);
    left->setObjectName("leftPanel");
    left->setFixedWidth(78);
    // The buttons stretch to the strips' height; their own (expanded) size hints must not make the window taller
    // than the strips need - that left an empty band under the strips and the master frame.
    left->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Ignored);
    left->setStyleSheet(panelCss);
    QVBoxLayout *lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(4);

    struct PageDef { const char *text; int first; int last; };   // 1-based strip numbers
    static const PageDef pages[4] = { { "1-16", 1, 16 }, { "17-32", 17, 32 }, { "33-46", 33, 46 }, { "ALL", 1, 46 } };
    QButtonGroup *grp = new QButtonGroup(this);
    grp->setExclusive(true);
    for (int i = 0; i < 4; i++) {
        QToolButton *b = new QToolButton(left);
        b->setText(pages[i].text);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumHeight(24);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        b->setToolTip(tr("แสดงช่องที่ %1 ถึง %2").arg(pages[i].first).arg(pages[i].last));
        grp->addButton(b, i);
        lv->addWidget(b);
    }
    grp->button(3)->setChecked(true);   // same as before: everything visible

    connect(grp, QOverload<int>::of(&QButtonGroup::buttonClicked), this, [this](int id) {
        showStripPage(id);
        QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
        st.setValue("StripPage", id);
    });

    lv->addSpacing(4);

    // ---- real-time analyzer
    QPushButton *btnRta = new QPushButton(tr("RTA"), left);
    btnRta->setFocusPolicy(Qt::NoFocus);
    btnRta->setMinimumHeight(24);
    btnRta->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    btnRta->setToolTip(tr("กราฟความถี่เสียงแบบเรียลไทม์ (Master)"));
    connect(btnRta, &QPushButton::clicked, this, [this]() {
        RtaDialog *d = findChild<RtaDialog *>(QStringLiteral("rtaDlg"));
        if (!d) {
            d = new RtaDialog(this, [this](float *buf, int &rate) { return synth->spectrum(buf, &rate); });
            d->setObjectName(QStringLiteral("rtaDlg"));
            d->setAttribute(Qt::WA_DeleteOnClose);
        }
        d->show();
        d->raise();
        d->activateWindow();
    });
    lv->addWidget(btnRta);

    // ---- mixer presets (volume / pan / reverb / mute / solo of every strip + master volume)
    QPushButton *btnPreset = new QPushButton(tr("PRESET"), left);
    btnPreset->setFocusPolicy(Qt::NoFocus);
    btnPreset->setMinimumHeight(24);
    btnPreset->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    btnPreset->setToolTip(tr("บันทึก / เรียกการตั้งค่ามิกเซอร์ทั้งชุด"));
    lv->addWidget(btnPreset);

    // show / hide the Pan and Reverb knobs of all strips (lit = shown)
    QToolButton *btnKnobs = new QToolButton(left);
    btnKnobs->setText(tr("PAN / REV"));
    btnKnobs->setCheckable(true);
    btnKnobs->setFocusPolicy(Qt::NoFocus);
    btnKnobs->setMinimumHeight(24);
    btnKnobs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    btnKnobs->setToolTip(tr("ซ่อน / แสดงปุ่มหมุน Pan และ Reverb ของทุกช่อง (ซ่อนแล้วมิกเซอร์เตี้ยลง บังหน้าจอคาราโอเกะน้อยลง)"));
    {
        QSettings ks(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
        const bool shown = ks.value("ShowPanReverbKnobs", true).toBool();
        btnKnobs->setChecked(shown);
        foldAtShow = !shown;   // applied in showEvent, after the window has been fitted to its full height
    }
    connect(btnKnobs, &QToolButton::toggled, this, [this](bool on) {
        QSettings ks(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
        ks.setValue("ShowPanReverbKnobs", on);
        setKnobsVisible(on);
    });
    lv->addWidget(btnKnobs);

    // VST / VSTi management (plug-in folders + scan)
    QPushButton *btnVst = new QPushButton(tr("VST / VSTi"), left);
    btnVst->setFocusPolicy(Qt::NoFocus);
    btnVst->setMinimumHeight(24);
    btnVst->setToolTip(tr("จัดการปลั๊กอิน VST / VSTi (โฟลเดอร์ปลั๊กอินและการสแกน)"));
    connect(btnVst, &QPushButton::clicked, this, [this]() { showVSTDirsDlg(); });
    lv->addWidget(btnVst);

    btnVst->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);   // bottom edge lines up with the strip area

    // one strip -> values (used by "load preset" and "reset all")
    auto applyStrip = [this](InstrumentType t, int vol, int pan, int rev, bool m, bool s) {
        InstCh *ich = chInstMap.value(t, nullptr);
        if (!ich)
            return;
        ich->setSliderLevel(vol);      synth->setVolume(t, vol);
        ich->setPanValue(pan);         synth->setPan(t, pan);
        ich->setReverbValue(rev);      synth->setReverb(t, rev);
        ich->setMuteButton(m);         synth->setMute(t, m);
        ich->setSoloButton(s);         synth->setSolo(t, s);
    };
    auto applyMaster = [this](int v) {
        masterStrip->btnMute->setChecked(false);
        masterStrip->setLevel(v);
        player->setVolume(v);
    };

    connect(btnPreset, &QPushButton::clicked, this, [this, btnPreset, applyStrip, applyMaster]() {
        QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
        st.beginGroup("MixerPresets");
        const QStringList names = st.childGroups();
        st.endGroup();

        QMenu menu(this);
        if (names.isEmpty()) {
            QAction *none = menu.addAction(tr("(ยังไม่มี Preset)"));
            none->setEnabled(false);
        }
        for (const QString &n : names)
            menu.addAction(n)->setData(QStringLiteral("load:") + n);
        menu.addSeparator();
        menu.addAction(tr("บันทึกการตั้งค่าปัจจุบันเป็น Preset…"))->setData(QStringLiteral("save"));
        QMenu *delMenu = menu.addMenu(tr("ลบ Preset"));
        delMenu->setEnabled(!names.isEmpty());
        for (const QString &n : names)
            delMenu->addAction(n)->setData(QStringLiteral("del:") + n);
        menu.addSeparator();
        menu.addAction(tr("รีเซ็ตทั้งหมด (ค่าเริ่มต้น)"))->setData(QStringLiteral("reset"));

        QAction *act = menu.exec(btnPreset->mapToGlobal(QPoint(btnPreset->width(), 0)));
        if (!act)
            return;
        const QString cmd = act->data().toString();

        if (cmd == QLatin1String("reset")) {
            if (QMessageBox::question(this, tr("รีเซ็ตทั้งหมด"),
                    tr("คืนค่าเฟดเดอร์ Pan Reverb Mute Solo ของทุกช่องเป็นค่าเริ่มต้น?")) != QMessageBox::Yes)
                return;
            for (InstrumentType t : chInstMap.keys())
                applyStrip(t, 50, 0, 0, false, false);
            applyMaster(50);
        }
        else if (cmd == QLatin1String("save")) {
            bool ok = false;
            QString name = QInputDialog::getText(this, tr("บันทึก Preset"), tr("ชื่อ Preset:"),
                                                 QLineEdit::Normal, QString(), &ok).trimmed();
            name.replace('/', '-').replace('\\', '-');
            if (!ok || name.isEmpty())
                return;
            if (names.contains(name) &&
                [this]() {   // NoIcon: Windows plays no warning sound over the music
                    QMessageBox box(QMessageBox::NoIcon, tr("บันทึก Preset"), tr("มี Preset ชื่อนี้แล้ว เขียนทับ?"),
                                    QMessageBox::Yes | QMessageBox::No, this);
                    box.setDefaultButton(QMessageBox::No);
                    return box.exec();
                }() != QMessageBox::Yes)
                return;
            QSettings w(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
            w.beginGroup("MixerPresets");
            w.beginGroup(name);
            for (InstrumentType t : chInstMap.keys()) {
                w.setValue(QString("i%1").arg(static_cast<int>(t)),
                           QString("%1,%2,%3,%4,%5").arg(synth->volume(t)).arg(synth->pan(t)).arg(synth->reverb(t))
                               .arg(synth->isMute(t) ? 1 : 0).arg(synth->isSolo(t) ? 1 : 0));
            }
            w.setValue("master", masterStrip->fader->level());
            w.endGroup();
            w.endGroup();
        }
        else if (cmd.startsWith(QLatin1String("del:"))) {
            QSettings w(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
            w.beginGroup("MixerPresets");
            w.remove(cmd.mid(4));
            w.endGroup();
        }
        else if (cmd.startsWith(QLatin1String("load:"))) {
            QSettings r(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
            r.beginGroup("MixerPresets");
            r.beginGroup(cmd.mid(5));
            for (InstrumentType t : chInstMap.keys()) {
                const QStringList p = r.value(QString("i%1").arg(static_cast<int>(t)), QString()).toString().split(',');
                if (p.count() < 5)
                    continue;
                applyStrip(t, p[0].toInt(), p[1].toInt(), p[2].toInt(), p[3].toInt() != 0, p[4].toInt() != 0);
            }
            applyMaster(r.value("master", 50).toInt());
            r.endGroup();
            r.endGroup();
        }
    });

    // ---- right panel: master strip (crimson, same layout as the instrument strips)
    QWidget *right = new QWidget(this);
    right->setFixedWidth(88);
    QVBoxLayout *rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(0);
    masterStrip = new MasterStrip(right);
    masterStrip->setLevel(player->volume());
    masterStrip->setCollapse(knobDrop);   // PAN/REV already folded at start-up
    // (no onResized hook: re-aligning on every resize of the master strip fed back into itself - the
    // alignment changes the strip's size, which asked for another alignment - and the column kept shaking.
    // It is now aligned only when the window is shown, after PAN/REV is folded, and after On Top is toggled.)
    masterStrip->onFxBypass = [this](int i, bool b) { synth->setMasterFXBypass(i, b); };
    masterStrip->onFxOpen = [this](int i) {
        const QList<FX *> l = synth->masterFXList();
        if (i >= 0 && i < l.count())
            openFxDialog(l[i], QStringLiteral("MASTER"));
    };
    masterStrip->onFxMenu = [this](int i, const QPoint &p) { showMasterFxMenu(i, p); };
    masterStrip->setToolTip(tr("ระดับเสียงรวม (Master Volume) — ดับเบิลคลิกที่เฟเดอร์เพื่อรีเซ็ตเป็น 0 dB"));
    rv->addWidget(masterStrip, 1);
    connect(masterStrip->fader, static_cast<void (FaderSlider::*)(int)>(&FaderSlider::levelChanged),
            this, [this](int v) {
                if (masterStrip->btnMute->isChecked())
                    masterStrip->btnMute->setChecked(false);   // moving the fader un-mutes
                player->setVolume(v);
            });
    masterStrip->btnMute->setChecked(player->isMasterMute());
    connect(masterStrip->btnHide, &QPushButton::toggled, this, [this](bool on) { setStripsHidden(on); });
    connect(masterStrip->btnMute, &QPushButton::toggled, this, [this](bool on) {
        player->setMasterMute(on);   // level is kept; only the output gain is forced to 0
        QSettings app(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        app.setValue("MasterMute", on);
    });

    // ---- put them around the splitter
    QHBoxLayout *body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(6);
    ui->splitter->setHandleWidth(3);   // slim divider between instrument and bus strips
    ui->splitter->setStyleSheet("QSplitter::handle { background: palette(mid); }");
    ui->verticalLayout_60->removeWidget(ui->splitter);
    body->addWidget(left);
    body->addWidget(ui->splitter, 1);
    body->addWidget(right);
    ui->verticalLayout_60->addLayout(body, 1);

    // ---- flat console-style preset tabs along the top
    const QString tabCss =
        "QToolButton { background: transparent; border: none; border-radius: 6px;"
        " padding: 5px 2px; font-weight: bold; }"
        "QToolButton:hover, QToolButton:pressed { background: transparent; border: none; }"
        "QToolButton:checked:hover { background: #d92b2b; border: 1px solid #ff7a7a; }"
        "QToolButton:checked { background: #d92b2b; color: #ffffff; border: 1px solid #ff7a7a; }";
    for (auto btn : btnPresets.buttons()) {
        btn->setStyleSheet(tabCss);
        btn->setToolTip(tr("ปรีเซ็ต %1 (ซาวด์ฟ้อนท์ที่ไม่มีเลขนี้จะใช้ 000)")
                            .arg(sfPresetBankNumber(btnPresets.id(btn)), 3, 10, QChar('0')));
    }

    QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
    int page = qBound(0, st.value("StripPage", 3).toInt(), 3);
    grp->button(page)->setChecked(true);
    showStripPage(page);
}

// page 0..2 = strips 1-16 / 17-32 / 33-46, page 3 = all
void SynthMixerDialog::showStripPage(int page)
{
    static const int first[4] = { 1, 17, 33, 1 };
    static const int last[4]  = { 16, 32, 46, 46 };
    if (page < 0 || page > 3)
        return;
    for (int n = 1; n <= 46; n++) {
        QWidget *f = (n == 1) ? static_cast<QWidget *>(ui->frame)
                              : findChild<QWidget *>(QString("frame_%1").arg(n));
        if (f)
            f->setVisible(n >= first[page] && n <= last[page]);
    }
    // strip 63 = Canthai: sits next to the Pipe strip (17-32 page) and shows on the ALL page
    if (QWidget *f = findChild<QWidget *>(QStringLiteral("frame_63")))
        f->setVisible(page == 1 || page == 3);
    // the scroll area does not resize its contents (widgetResizable is off): shrink them to the visible strips
    if (ui->scrollAreaWidgetContents->layout())
        ui->scrollAreaWidgetContents->layout()->activate();
    ui->scrollAreaWidgetContents->adjustSize();
    ui->scrollArea->horizontalScrollBar()->setValue(0);
}

void SynthMixerDialog::mapChInstUI()
{
    chInstMap[InstrumentType::Piano]                = ui->ch;
    chInstMap[InstrumentType::Organ]                = ui->ch_2;
    chInstMap[InstrumentType::Accordion]            = ui->ch_3;
    chInstMap[InstrumentType::ChromaticPercussion]  = ui->ch_4;
    chInstMap[InstrumentType::Percussive]           = ui->ch_5;
    chInstMap[InstrumentType::Bass]                 = ui->ch_6;
    chInstMap[InstrumentType::AcousticGuitarNylon]  = ui->ch_7;
    chInstMap[InstrumentType::AcousticGuitarSteel]  = ui->ch_8;
    chInstMap[InstrumentType::ElectricGuitarJazz]   = ui->ch_9;
    chInstMap[InstrumentType::ElectricGuitarClean]  = ui->ch_10;
    chInstMap[InstrumentType::OverdrivenGuitar]     = ui->ch_11;
    chInstMap[InstrumentType::DistortionGuitar]     = ui->ch_12;
    chInstMap[InstrumentType::HarmonicsGuitar]      = ui->ch_13;
    chInstMap[InstrumentType::Trumpet]      = ui->ch_14;
    chInstMap[InstrumentType::Brass]        = ui->ch_15;
    chInstMap[InstrumentType::SynthBrass]   = ui->ch_16;
    chInstMap[InstrumentType::Saxophone]    = ui->ch_17;
    chInstMap[InstrumentType::Reed]         = ui->ch_18;
    chInstMap[InstrumentType::Pipe]         = ui->ch_19;
    chInstMap[InstrumentType::Strings]      = ui->ch_20;
    chInstMap[InstrumentType::Ensemble]     = ui->ch_21;
    chInstMap[InstrumentType::SynthLead]    = ui->ch_22;
    chInstMap[InstrumentType::SynthPad]     = ui->ch_23;
    chInstMap[InstrumentType::SynthEffects] = ui->ch_24;
    chInstMap[InstrumentType::Ethnic]       = ui->ch_25;
    chInstMap[InstrumentType::SoundEffects] = ui->ch_26;

    chInstMap[InstrumentType::BassDrum]     = ui->ch_27;
    chInstMap[InstrumentType::Snare]        = ui->ch_28;
    chInstMap[InstrumentType::SideStick]    = ui->ch_29;

    chInstMap[InstrumentType::HighTom]      = ui->ch_30;
    chInstMap[InstrumentType::MidTom]       = ui->ch_31;
    chInstMap[InstrumentType::LowTom]       = ui->ch_32;

    chInstMap[InstrumentType::Hihat]        = ui->ch_33;
    chInstMap[InstrumentType::Cowbell]      = ui->ch_34;
    chInstMap[InstrumentType::CrashCymbal]  = ui->ch_35;
    chInstMap[InstrumentType::RideCymbal]   = ui->ch_36;
    chInstMap[InstrumentType::Bongo]        = ui->ch_37;
    chInstMap[InstrumentType::Conga]        = ui->ch_38;
    chInstMap[InstrumentType::Timbale]      = ui->ch_39;
    chInstMap[InstrumentType::SmallCupShapedCymbals] = ui->ch_40;
    chInstMap[InstrumentType::ThaiChap]     = ui->ch_41;
    chInstMap[InstrumentType::PercussionEtc] = ui->ch_42;

    chInstMap[InstrumentType::VSTi1]        = ui->ch_43;
    chInstMap[InstrumentType::VSTi2]        = ui->ch_44;
    chInstMap[InstrumentType::VSTi3]        = ui->ch_45;
    chInstMap[InstrumentType::VSTi4]        = ui->ch_46;

    chInstMap[InstrumentType::BusGroup1]    = ui->ch_47;
    chInstMap[InstrumentType::BusGroup2]    = ui->ch_48;
    chInstMap[InstrumentType::BusGroup3]    = ui->ch_49;
    chInstMap[InstrumentType::BusGroup4]    = ui->ch_50;
    chInstMap[InstrumentType::BusGroup5]    = ui->ch_51;
    chInstMap[InstrumentType::BusGroup6]    = ui->ch_52;
    chInstMap[InstrumentType::BusGroup7]    = ui->ch_53;
    chInstMap[InstrumentType::BusGroup8]    = ui->ch_54;
    chInstMap[InstrumentType::BusGroup9]    = ui->ch_55;
    chInstMap[InstrumentType::BusGroup10]   = ui->ch_56;
    chInstMap[InstrumentType::BusGroup11]   = ui->ch_57;
    chInstMap[InstrumentType::BusGroup12]   = ui->ch_58;
    chInstMap[InstrumentType::BusGroup13]   = ui->ch_59;
    chInstMap[InstrumentType::BusGroup14]   = ui->ch_60;
    chInstMap[InstrumentType::BusGroup15]   = ui->ch_61;
    chInstMap[InstrumentType::BusGroup16]   = ui->ch_62;

    chInstMap[InstrumentType::Canthai]      = ui->ch_63;   // khaen strip (drawn after the Pipe strip)
}

void SynthMixerDialog::setChInstDetails()
{
    // 63 (appended after the buses: Canthai)
    QString names[63] = { "Piano", "Organ", "Accordn", "Chmt.P", "Percsve",
                        "Bass", "Nylon", "Steel", "Jazz", "Clean",
                        "Overdrv", "Distrn", "Harmncs", "Trumpet", "Brass",
                        "S.Brass", "Saxphn", "Reed", "Pipe", "Strings",
                        "Ensembl", "SynthL", "SynthP", "SynthE", "Ethnic", "SoundFX",
                        "Kick", "Snare", "S.Stick", "HighTom", "MidTom",
                        "LowTom", "Hihat", "Cowbell", "Crash", "Ride",
                        "Bongo", "Conga", "Timbale", "ฉิ่ง", "ฉาบ", "PercEtc",

                        "VSTi 1", "VSTi 2", "VSTi 3", "VSTi 4",

                        "Bus 1", "Bus 2", "Bus 3", "Bus 4",
                        "Bus 5", "Bus 6", "Bus 7", "Bus 8",
                        "Bus 9", "Bus 10", "Bus 11", "Bus 12",
                        "Bus 13", "Bus 14", "Bus 15", "Bus 16",

                        "Canthai" };

    QString tooltips[63] = { "Piano", "Organ", "Accordion", "Chromatic Percussion",
                             "Percussive", "Bass", "Acoustic Guitar (nylon)",
                             "Acoustic Guitar (steel)", "Electric Guitar (jazz)",
                             "Electric Guitar (clean)", "Overdriven Guitar",
                             "Distortion Guitar", "Harmonics Guitar", "Trumpet",
                             "Brass", "Synth Brass", "Saxophone", "Reed", "Pipe",
                             "Strings", "Ensemble", "Synth Lead", "Synth Pad",
                             "Synth Effects", "Ethnic", "Sound effects",
                             "Kick (Bass Drum)", "Snare", "Side Stick/Rimshot",
                             "High Tom", "Mid Tom", "Low Tom", "Hi-hat", "Cowbell",
                             "Crash Cymbal", "Ride Cymbal", "Bongo", "Conga",
                             "Timbale", "ฉิ่ง / Triangle",
                             "ฉาบ", "Percussion Etc.",

                             "VST instruments 1", "VST instruments 2",
                             "VST instruments 3", "VST instruments 4",

                             "Bus Group 1", "Bus Group 2", "Bus Group 3", "Bus Group 4",
                             "Bus Group 5", "Bus Group 6", "Bus Group 7", "Bus Group 8",
                             "Bus Group 9", "Bus Group 10", "Bus Group 11", "Bus Group 12",
                             "Bus Group 13", "Bus Group 14", "Bus Group 15", "Bus Group 16",

                             "Canthai (Khaen / แคน)"};

    QString rc = ":/Icons/Instruments/";
    QString imgs[63] = { "Piano.png", "Organ.png", "Accordion.png",
                         "Chromatic Percussion.png", "Percussive.png",
                         "Bass.png", "Nylon.png", "Steel.png", "Jazz.png",
                         "Clean.png", "Overdriven.png", "Distortion.png",
                         "Harmonics.png", "Trumpet.png", "Brass.png",
                         "Synth Brass.png", "Saxophone.png", "Reed.png",
                         "Pipe.png", "Strings.png", "Ensemble.png",
                         "Synth Lead.png", "Synth Pad.png", "Synth Effects.png",
                         "Ethnic.png", "FX.png",
                         "Kick.png", "Snare.png", "Side Stick.png",
                         "High Tom.png", "Mid Tom.png", "Low Tom.png",
                         "Hihat.png", "Cowbell.png", "Crash.png", "Ride.png",
                         "Bongo.png", "Conga.png", "Timbale.png", "Ching.png",
                         "Chab.png", "Percussion Etc.png",

                         "vst.png", "vst.png", "vst.png", "vst.png",

                         "bs1.png", "bs2.png", "bs3.png", "bs4.png",
                         "bs5.png", "bs6.png", "bs7.png", "bs8.png",
                         "bs9.png", "bs10.png", "bs11.png", "bs12.png",
                         "bs13.png", "bs14.png", "bs15.png", "bs16.png",

                         "Canthai.png" };


    for (int i=0; i<synth->HANDLE_STREAM_COUNT; i++)
    {
        InstCh *ich = chInstMap.values()[i];

        ich->setInstrumentType(chInstMap.keys()[i]);
        ich->setInstrumentNames(names[i], tooltips[i]);

        if (i == 25)         // "FX" strip
            ich->setInstrumentImage(modernBadge(QStringLiteral("FX"), QColor(0xb3, 0x88, 0xff), false));
        else if (i >= 42 && i < 46)   // VSTi 1..4
            ich->setInstrumentImage(modernBadge(QStringLiteral("VSTi"), QColor(0x4f, 0xc3, 0xf7), false));
        else if (i >= 46 && i < 62)   // bus 1..16
            ich->setInstrumentImage(modernBadge(QString::number(i - 45), tagAccent(ich->nameTag()), true));
        else
            ich->setInstrumentImage(QImage(rc + imgs[i]));

        connect(ich, SIGNAL(muteChanged(InstrumentType,bool)),
                this, SLOT(setMute(InstrumentType,bool)));

        connect(ich, SIGNAL(soloChanged(InstrumentType,bool)),
                this, SLOT(setSolo(InstrumentType,bool)));

        connect(ich, SIGNAL(sliderLevelChanged(InstrumentType,int)),
                this, SLOT(setMixLevel(InstrumentType,int)));

        connect(ich, &InstCh::panChanged, this, [this](InstrumentType ty, int pn) {
            synth->setPan(ty, pn);
        });
        connect(ich, &InstCh::reverbChanged, this, [this](InstrumentType ty, int lv) {
            synth->setReverb(ty, lv);
        });
        connect(ich, &InstCh::trimChanged, this, [this](InstrumentType ty, double db) {
            synth->setTrim(ty, db);
        });

        connect(ich, SIGNAL(sliderDoubleClicked(InstrumentType)),
                this, SLOT(resetMixLevel(InstrumentType)));

        connect(ich, SIGNAL(menuRequested(InstrumentType,QPoint)),
                this, SLOT(showChannelMenu(InstrumentType,QPoint)));
        connect(ich, SIGNAL(busMenuRequested(InstrumentType,QPoint)),
                this, SLOT(showBusMenu(InstrumentType,QPoint)));

        connect(ich, SIGNAL(fxByPassChanged(InstrumentType,int,bool)),
                this, SLOT(byPassFX(InstrumentType,int,bool)));

        connect(ich, SIGNAL(fxDoubleClicked(InstrumentType,int)),
                this, SLOT(showFxDialog(InstrumentType,int)));

        connect(ich, SIGNAL(fxRemoveMenuRequested(InstrumentType,int,QPoint)),
                this, SLOT(showFXRemoveMenu(InstrumentType,int,QPoint)));

        connect(ich, SIGNAL(imageDoubleClicked(InstrumentType)),
                this, SLOT(showVSTiDialog(InstrumentType)));
        connect(ich, &InstCh::imageMenuRequested, this,
                [this](InstrumentType ty, const QPoint &p) { showSoundfontMenu(ty, p); });
    }
}

void SynthMixerDialog::createBusActions(InstrumentType t, QMenu *busMenu)
{
    QList<QString> names;
    names.append("Master (Default)");
    int start = static_cast<int>(InstrumentType::BusGroup1);
    for (int i=0; i<16; i++) {
        InstrumentType type = static_cast<InstrumentType>(start + i);
        names.append(chInstMap[type]->fullInstrumentName());
    }

    for (int i=0; i<17; i++) {
        QAction *act = busMenu->addAction(names[i]);
        connect(act, SIGNAL(triggered()), &signalBusActionMapper, SLOT(map()));
        signalBusActionMapper.setMapping(act, i-1);
        if (synth->instrument(t).bus == (i-1)) {
            act->setCheckable(true);
            act->setChecked(true);
        }
    }
}

void SynthMixerDialog::showChannelMenu(InstrumentType type, const QPoint &pos)
{
    currentType = type;
    addToMaster = false;

    QMenu menu(this);

    // VST
    #ifndef __linux__
    QMenu *vstMenu = menu.addMenu(tr("VST Effects"));
    if (vstVendorMenus.isEmpty()) {
        QAction *emptyAct = vstMenu->addAction(tr("(ยังไม่มีปลั๊กอินเอฟเฟกต์ VST)"));
        emptyAct->setEnabled(false);
        QAction *setupAct = vstMenu->addAction(tr("เปิดหน้าตั้งค่า VST..."));
        connect(setupAct, &QAction::triggered, this, &SynthMixerDialog::showVSTDirsDlg);
    } else {
        for (QMenu *m : vstVendorMenus) {
            vstMenu->addMenu(m);
        }
    }
    #endif

    // Built-in FX
    {
        if (signalBFXActionMapper != nullptr)
            delete signalBFXActionMapper;

        signalBFXActionMapper = new QSignalMapper(this);

        menu.addSeparator();

        QMenu *fxMenu = menu.addMenu("Built-in Effects");

        for (int i=0; i<BUILTIN_FX_COUNT; i++)
        {
            QAction *act1 = fxMenu->addAction(BUILTIN_FX_NAMES[i]);
            connect(act1, SIGNAL(triggered()), signalBFXActionMapper, SLOT(map()));
            signalBFXActionMapper->setMapping(act1, QString::number(i));
        }
    }

    // Bus
    if (!isBusType(type))
    {
        menu.addSeparator();
        QMenu *busMenu = menu.addMenu("Bus Group");
        createBusActions(type, busMenu);
    }

    connect(signalBFXActionMapper, SIGNAL(mapped(QString)), this, SLOT(addFX(QString)));

    menu.exec(chInstMap[type]->mapToGlobal(pos));
}

void SynthMixerDialog::showBusMenu(InstrumentType type, const QPoint &globalPos)
{
    currentType = type;
    QMenu menu(this);
    createBusActions(type, &menu);
    menu.exec(globalPos);
}

void SynthMixerDialog::setBusGroup(int group)
{
    synth->setBusGroup(currentType, group);
    if (chInstMap.contains(currentType))
        chInstMap[currentType]->setBusBadge(group);
}

FX* SynthMixerDialog::addFX(const QString &uidStr, bool bypass)
{
    uint uid = uidStr.toUInt();
    if (addToMaster)
        return addMasterFX(uid, bypass);
    VSTNamePath vst = synth->VSTList()[uid];
    Instrument inst = synth->instrument(currentType);

    FX *fx = synth->addFX(currentType, uid);

    if (fx == nullptr)
        return fx;

    // a VST that could not be loaded as an effect (instrument, wrong bit-depth, VST3 refused ...): say so instead of
    // leaving a dead entry in the strip (not while saved effects are being restored: they stay as they were)
    #ifndef __linux__
    if (!restoringFx && fx->fxType() == FXType::VSTEffects && fx->fxHandle() == 0)
    {
        synth->removeFX(currentType, synth->instrument(currentType).FXs.count() - 1);
        warnVstNotLoaded(vst.vstName);
        return nullptr;
    }
    #endif

    int i = inst.FXs.count();
    synth->setFXBypass(currentType, i, bypass);

    QString name;
    if (uid < BUILTIN_FX_COUNT)
        name = BUILTIN_FX_NAMES[uid];
    else
        name = vst.vstName;

    chInstMap[currentType]->addFXLabel(name, inst.FXs.count(), bypass);

    return fx;
}

void SynthMixerDialog::byPassFX(InstrumentType type, int fxIndex, bool bypass)
{
    synth->setFXBypass(type, fxIndex, bypass);
}

void SynthMixerDialog::showFxDialog(InstrumentType type, int fxIndex)
{
    openFxDialog(synth->instrument(type).FXs[fxIndex], chInstMap[type]->fullInstrumentName());
}

void SynthMixerDialog::openFxDialog(FX *fx, const QString &ownerName)
{
    if (!fx)
        return;

    if (fx->fxType() == FXType::VSTEffects)
    {
        if (!synth->isOpened())
            return;

        #ifndef __linux__
        if (fx->fxHandle() != 0)
        {
            VSTDialog *dlg = new VSTDialog(this, fx->fxHandle(), ownerName);

            if (!dlg->isCanOpen())
            {
                delete dlg;
                return;
            }

            dlg->setAttribute(Qt::WA_DeleteOnClose);
            // save the effect's settings as soon as its window is closed (not only at program exit)
            connect(dlg, &QObject::destroyed, this, [this]() { settingValues(); });
            dlg->show();
        }
        #endif
    }
    else
    {   
        QDialog *d = nullptr;

        switch (fx->fxType()) {
        case FXType::AutoWah:
            d = new AutoWahFXDialog(this, dynamic_cast<AutoWahFX*>(fx));
            break;
        case FXType::Chorus:
            d = new ChorusFXDialog(this, dynamic_cast<ChorusFX*>(fx));
            break;
        case FXType::Compressor:
            d = new CompressorFXDialog(this, dynamic_cast<CompressorFX*>(fx));
            break;
        case FXType::Distortion:
            d = new DistortionFXDialog(this, dynamic_cast<DistortionFX*>(fx));
            break;
        case FXType::Echo:
            d = new EchoFXDialog(this, dynamic_cast<EchoFX*>(fx));
            break;
        case FXType::EQ15Band:
            d = new EQ15BandDialog(this, dynamic_cast<Equalizer15BandFX*>(fx));
            break;
        case FXType::EQ31Band:
            d = new EQ31BandDialog(this, dynamic_cast<Equalizer31BandFX*>(fx));
            break;
        case FXType::Reverb:
            d = new ReverbFXDialog(this, dynamic_cast<ReverbFX*>(fx));
            break;
        }

        if (d != nullptr)
        {
            int i = static_cast<int>(fx->fxType());
            d->setWindowTitle(BUILTIN_FX_NAMES[i] + "  [" + ownerName + "]");
            d->adjustSize();
            d->setFixedSize(d->size());
            d->setAttribute(Qt::WA_DeleteOnClose);
            d->show();
        }
    }
}

void SynthMixerDialog::showFXRemoveMenu(InstrumentType type, int fxIndex, const QPoint &pos)
{
    currentType = type;
    currentFxIndexToRemove = fxIndex;

    QMenu menu(this);

    menu.addAction("Remove", this, SLOT(removeFX()));

    menu.exec(chInstMap[type]->mapToGlobal(pos));
}

void SynthMixerDialog::showVSTiDialog(InstrumentType vstiIndexType)
{
    int index = static_cast<int>(vstiIndexType) - synth->HANDLE_VSTI_START;

    if (index < 0 || index > 3)
        return;

    #ifdef __linux__
    return;
    #else
    DWORD vsti = synth->vstiHandle(index);
    if (vsti == 0)
        return;

    VSTDialog *dlg = new VSTDialog(this, vsti, chInstMap[vstiIndexType]->fullInstrumentName());
    if (!dlg->isCanOpen())
    {
        delete dlg;
        return;
    }

    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QObject::destroyed, this, [this]() { settingValues(); });
    dlg->show();
    #endif
}

// Right click on an instrument icon: pick which soundfont that strip plays with.
void SynthMixerDialog::showSoundfontMenu(InstrumentType type, const QPoint &globalPos)
{
    const QStringList files = synth->soundfontFiles();
    const QString current = synth->stripSoundfont(type);

    QMenu menu(this);
    QAction *title = menu.addAction(tr("ซาวด์ฟอนต์ของ: %1").arg(chInstMap.contains(type) ? chInstMap[type]->fullInstrumentName() : QString()));
    title->setEnabled(false);
    menu.addSeparator();

    if (files.isEmpty()) {
        menu.addAction(tr("ยังไม่มีซาวด์ฟอนต์ (เพิ่มได้ที่เมนูตั้งค่า)"))->setEnabled(false);
        menu.exec(globalPos);
        return;
    }

    QAction *def = menu.addAction(tr("ตามพรีเซ็ต (ค่าเริ่มต้น)"));
    def->setCheckable(true);
    def->setChecked(current.isEmpty() || files.indexOf(current) < 0);
    connect(def, &QAction::triggered, this, [this, type]() {
        synth->setStripSoundfont(type, QString());
        settingValues();
    });

    menu.addSeparator();
    for (const QString &f : files) {
        QAction *a = menu.addAction(QFileInfo(f).fileName());
        a->setToolTip(f);
        a->setCheckable(true);
        a->setChecked(f == current);
        connect(a, &QAction::triggered, this, [this, type, f]() {
            synth->setStripSoundfont(type, f);
            settingValues();
        });
    }
    menu.exec(globalPos);
}

// ---- master strip FX ----
FX* SynthMixerDialog::addMasterFX(uint uid, bool bypass)
{
    FX *fx = synth->addMasterFX(uid);
    if (fx == nullptr)
        return nullptr;

    #ifndef __linux__
    if (!restoringFx && fx->fxType() == FXType::VSTEffects && fx->fxHandle() == 0)
    {
        synth->removeMasterFX(synth->masterFXList().count() - 1);
        warnVstNotLoaded(synth->VSTList()[uid].vstName);
        return nullptr;
    }
    #endif

    const int i = synth->masterFXList().count() - 1;
    synth->setMasterFXBypass(i, bypass);

    QString name;
    if (uid < BUILTIN_FX_COUNT)
        name = BUILTIN_FX_NAMES[uid];
    else
        name = synth->VSTList()[uid].vstName;

    masterStrip->addFxLabel(name, i, bypass);
    return fx;
}

void SynthMixerDialog::showMasterFxMenu(int fxIndex, const QPoint &globalPos)
{
    QMenu menu(this);

    if (fxIndex >= 0) {
        menu.addAction(tr("ลบเอฟเฟกต์นี้ออก (Remove)"), this, [this, fxIndex]() {
            if (synth->removeMasterFX(fxIndex))
                masterStrip->removeFxLabel(fxIndex);
        });
        menu.addSeparator();
    }

    #ifndef __linux__
    QMenu *vstMenu = menu.addMenu(tr("VST Effects"));
    if (vstVendorMenus.isEmpty()) {
        QAction *emptyAct = vstMenu->addAction(tr("(ยังไม่มีปลั๊กอินเอฟเฟกต์ VST)"));
        emptyAct->setEnabled(false);
        QAction *setupAct = vstMenu->addAction(tr("เปิดหน้าตั้งค่า VST..."));
        connect(setupAct, &QAction::triggered, this, &SynthMixerDialog::showVSTDirsDlg);
    } else {
        for (QMenu *m : vstVendorMenus)
            vstMenu->addMenu(m);
    }
    #endif

    if (signalBFXActionMapper != nullptr)
        delete signalBFXActionMapper;
    signalBFXActionMapper = new QSignalMapper(this);

    menu.addSeparator();
    QMenu *fxMenu = menu.addMenu("Built-in Effects");
    for (int i = 0; i < BUILTIN_FX_COUNT; i++)
    {
        QAction *act = fxMenu->addAction(BUILTIN_FX_NAMES[i]);
        connect(act, SIGNAL(triggered()), signalBFXActionMapper, SLOT(map()));
        signalBFXActionMapper->setMapping(act, QString::number(i));
    }
    connect(signalBFXActionMapper, SIGNAL(mapped(QString)), this, SLOT(addFX(QString)));

    addToMaster = true;       // addFX() (VST and built-in menus) now targets the master strip
    menu.exec(globalPos);
    addToMaster = false;
}

void SynthMixerDialog::removeFX()
{
    if (synth->removeFX(currentType, currentFxIndexToRemove))
    {
        chInstMap[currentType]->removeVSTLabel(currentFxIndexToRemove);
    }
}

void SynthMixerDialog::on_btnMenu_clicked()
{
    // One unified menu (shared with the main window code) holds every setting.
    QPoint point = mapToGlobal(QPoint(width() - 260, ui->btnMenu->height() + 5));
    mainWin->showContextMenuAt(point);
}


void SynthMixerDialog::showBusDlg()
{
    BusDialog dlg(this, &chInstMap, synth);
    dlg.setModal(true);
    dlg.adjustSize();
    dlg.exec();
}

void SynthMixerDialog::showSpeakersDlg()
{
    SpeakerDialog dlg(this, &chInstMap, mainWin);
    dlg.setModal(true);
    dlg.adjustSize();
    dlg.exec();
}

void SynthMixerDialog::showVSTDirsDlg()
{
    #ifndef __linux__
    VSTDirsDialog dlg(this, mainWin);
    dlg.setModal(true);
    dlg.adjustSize();
    dlg.setMinimumSize(dlg.size());
    dlg.exec();
    #endif
}

void SynthMixerDialog::showVuDlg()
{
    QList<LEDVu*> vus;
    for (InstCh *ch : chInstMap.values())
    {
        vus.append(ch->vuBar());
    }

    SettingVuDialog vdlg(this, vus);
    vdlg.setModal(true);
    vdlg.adjustSize();
    vdlg.setFixedSize(vdlg.size());
    vdlg.setWindowTitle(vdlg.windowTitle() + " (PTHK Karaoke)");
    vdlg.exec();
}

void SynthMixerDialog::resetChannel()
{
    for (InstrumentType t : chInstMap.keys())
    {
        resetMixLevel(t);
        chInstMap[t]->setMuteButton(false);
        chInstMap[t]->setSoloButton(false);
        synth->setMute(t, false);
        synth->setSolo(t, false);
    }
}

void SynthMixerDialog::setStaysOnTop(bool stay)
{
    this->staysOnTop = stay;
    setProperty("kmTopmost", stay);   // read by DialogSkin so dialogs opened from here stay above the mixer
#ifdef Q_OS_WIN
    // Switch the native "topmost" state in place: changing the Qt window flag destroys and re-creates the
    // window, which flashed the whole mixer (and made it jump) every time On Top was clicked.
    if (isVisible() && winId()) {
        SetWindowPos(reinterpret_cast<HWND>(winId()), stay ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }
#endif
    // changing the flag re-creates the native window: keep its position and size, and re-fit afterwards
    const QRect geo = geometry();
    this->setWindowFlag(Qt::WindowStaysOnTopHint, stay);
    this->show();
    fitToContent();
    if (!isMaximized() && !isFullScreen())
        setGeometry(geo.x(), geo.y(), geo.width(), height());
    QTimer::singleShot(0, this, [this]() { alignMasterMute(); });
}

Qt::Edges SynthMixerDialog::edgesAt(const QPoint &pos) const
{
    Qt::Edges e;
    const int m = 6;
    if (pos.x() < m)
        e |= Qt::LeftEdge;
    else if (pos.x() >= width() - m)
        e |= Qt::RightEdge;
    return e;
}

void SynthMixerDialog::mouseMoveEvent(QMouseEvent *event)
{
    setCursor(edgesAt(event->pos()) ? Qt::SizeHorCursor : Qt::ArrowCursor);
    QDialog::mouseMoveEvent(event);
}

void SynthMixerDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        Qt::Edges e = edgesAt(event->pos());
        if (e && windowHandle()) {
            windowHandle()->startSystemResize(e);
            return;
        }
    }
    QDialog::mousePressEvent(event);
}

void SynthMixerDialog::paintEvent(QPaintEvent *event)
{
    QDialog::paintEvent(event);   // outline is drawn by the FrameOverlay on top
}

void SynthMixerDialog::closeEvent(QCloseEvent *event)
{
    // Player edition: the karaoke window owns the program; closing the mixer only hides it.
    if (!closeQuitsApp) {
        settingValues();
        event->accept();
        return;
    }

    QMessageBox::StandardButton resBtn = QMessageBox::question(
                                            this, tr("ออกจากโปรแกรม"),
                                            tr("ท่านต้องการออกจากโปรแกรม?"),
                                            QMessageBox::Yes|QMessageBox::No);
    if (resBtn != QMessageBox::Yes) {
        event->ignore();
        return;
    }

    settingValues();

    event->accept();
    qApp->quit();
}

void SynthMixerDialog::reject()
{
    // Esc key: behave like the close button (confirm, then exit).
    close();
}

void SynthMixerDialog::changeSoundfontPresets(int presets)
{
    for (auto btn : btnPresets.buttons()) {
        btn->setChecked(false);
    }

    btnPresets.button(presets)->setChecked(true);
    synth->setSoundfontPresets(presets);
}


// "Drum head tuning" window: same skinned dialog as the other windows (title bar, rounded frame).
// Changes apply immediately (so they can be heard while a song plays); OK just closes it.
void SynthMixerDialog::showDrumTunePopup()
{
    if (QDialog *old = findChild<QDialog *>("drumTuneDlg")) {
        old->show();
        old->raise();
        old->activateWindow();
        return;
    }

    QDialog *pop = new QDialog(this);
    pop->setObjectName("drumTuneDlg");
    pop->setAttribute(Qt::WA_DeleteOnClose);
    pop->setWindowTitle(tr("จูนหนังกลอง"));
    pop->setMinimumWidth(300);

    QVBoxLayout *root = new QVBoxLayout(pop);
    root->setContentsMargins(16, 12, 16, 14);
    root->setSpacing(10);

    QGridLayout *g = new QGridLayout();
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(7);
    root->addLayout(g);

    auto title = [pop](const QString &text) {
        QLabel *l = new QLabel(text, pop);
        QFont f = l->font();
        f.setBold(true);
        l->setFont(f);
        return l;
    };
    auto stepButton = [pop](const QString &text) {
        QToolButton *b = new QToolButton(pop);
        b->setText(text);
        b->setAutoRaise(false);
        b->setAutoRepeat(true);
        b->setAutoRepeatDelay(350);
        b->setAutoRepeatInterval(70);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(34, 28);
        QFont f = b->font();
        f.setBold(true);
        f.setPointSize(f.pointSize() + 3);
        b->setFont(f);
        return b;
    };
    auto fmt = [](int v) { return v > 0 ? QString("+%1").arg(v) : QString::number(v); };

    int row = 0;
    g->addWidget(title(tr("จูนความตึงหนังกลอง (เซมิโทน)")), row++, 0, 1, 4);

    struct Item { const char *name; int group; };
    const Item items[] = {
        { QT_TR_NOOP("กระเดื่อง"), MidiSynthesizer::TuneKick },
        { QT_TR_NOOP("สแนร์"),     MidiSynthesizer::TuneSnare },
        { QT_TR_NOOP("ทอมสูง (High)"),  MidiSynthesizer::TuneTomHigh },
        { QT_TR_NOOP("ทอมกลาง (Mid)"),  MidiSynthesizer::TuneTomMid },
        { QT_TR_NOOP("ทอมต่ำ (Low)"),   MidiSynthesizer::TuneTomLow },
        { QT_TR_NOOP("คาวเบลล์ (Cowbell)"), MidiSynthesizer::TuneCowbell },
    };

    QList<QLabel *> valueLabels;
    for (const Item &it : items)
    {
        const int group = it.group;
        QLabel *name = new QLabel(tr(it.name), pop);
        QToolButton *minus = stepButton(QStringLiteral("-"));
        QToolButton *plus  = stepButton(QStringLiteral("+"));
        QLabel *val = new QLabel(fmt(synth->drumTune(group)), pop);
        val->setAlignment(Qt::AlignCenter);
        val->setFixedWidth(48);
        QFont vf = val->font();
        vf.setBold(true);
        val->setFont(vf);
        valueLabels << val;

        g->addWidget(name, row, 0);
        g->addWidget(minus, row, 1);
        g->addWidget(val, row, 2);
        g->addWidget(plus, row, 3);
        row++;

        auto change = [this, group, val, fmt](int delta) {
            const int v = qBound(-12, synth->drumTune(group) + delta, 12);
            synth->setDrumTune(group, v);
            val->setText(fmt(v));
            QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
            st.setValue(QString("DrumTune%1").arg(group), v);
        };
        connect(minus, &QToolButton::clicked, pop, [change]() { change(-1); });
        connect(plus,  &QToolButton::clicked, pop, [change]() { change(+1); });
    }

    // key choice (click the lit one again to go back to "as in the song")
    g->addWidget(title(tr("เลือกเบอร์เสียง")), row++, 0, 1, 4);

    auto keyRow = [&](const QString &label, const QList<int> &keys, int current, bool kick) {
        g->addWidget(new QLabel(label, pop), row, 0);
        QHBoxLayout *kl = new QHBoxLayout();
        kl->setSpacing(6);
        QList<QToolButton *> both;
        for (int i = 0; i < keys.count(); i++) {
            QToolButton *b = new QToolButton(pop);
            b->setText(QString::number(keys[i]));
            b->setCheckable(true);
            b->setChecked(current == keys[i]);
            b->setFocusPolicy(Qt::NoFocus);
            b->setCursor(Qt::PointingHandCursor);
            b->setFixedHeight(28);
            b->setMinimumWidth(keys.count() > 2 ? 40 : 60);
            kl->addWidget(b);
            both << b;
        }
        g->addLayout(kl, row, 1, 1, 3);
        row++;

        for (int i = 0; i < keys.count(); i++) {
            connect(both[i], &QToolButton::clicked, pop, [this, both, keys, i, kick](bool on) {
                for (int j = 0; j < both.count(); j++)
                    if (j != i)
                        both[j]->setChecked(false);
                both[i]->setChecked(on);
                // same as the kick / snare lock buttons and the Settings window
                QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
                if (kick) {
                    if (on) { player->setLockKick(true, keys[i]); st.setValue("MidiLockKickNumber", keys[i]); }
                    else player->setLockKick(false);
                    st.setValue("MidiLockKick", on);
                } else {
                    if (on) { player->setLockSnare(true, keys[i]); st.setValue("MidiLockSnareNumber", keys[i]); }
                    else player->setLockSnare(false);
                    st.setValue("MidiLockSnare", on);
                }
                syncQuickChecks();
            });
        }
    };
    keyRow(tr("กระเดื่อง"), QList<int>() << 35 << 36, player->isLockKick() ? player->lockKickNumber() : 0, true);
    keyRow(tr("สแนร์"),     MidiHelper::snareNumbers(), player->isLockSnare() ? player->lockSnareNumber() : 0, false);

    QHBoxLayout *bottom = new QHBoxLayout();
    bottom->setSpacing(8);
    QPushButton *reset = new QPushButton(tr("รีเซ็ตทั้งหมด"), pop);
    QPushButton *ok = new QPushButton(tr("OK"), pop);
    ok->setDefault(true);
    reset->setFocusPolicy(Qt::NoFocus);
    reset->setCursor(Qt::PointingHandCursor);
    ok->setCursor(Qt::PointingHandCursor);
    reset->setMinimumHeight(30);
    ok->setMinimumHeight(30);
    bottom->addWidget(reset, 1);
    bottom->addWidget(ok, 1);
    root->addLayout(bottom);

    connect(reset, &QPushButton::clicked, pop, [this, pop, valueLabels]() {
        QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        for (int i = 0; i < MidiSynthesizer::DrumTuneGroupCount; i++) {
            synth->setDrumTune(i, 0);
            st.setValue(QString("DrumTune%1").arg(i), 0);
        }
        player->setLockKick(false);
        player->setLockSnare(false);
        st.setValue("MidiLockKick", false);
        st.setValue("MidiLockSnare", false);
        syncQuickChecks();
        for (QLabel *l : valueLabels)
            l->setText("0");
        for (QToolButton *b : pop->findChildren<QToolButton *>())
            if (b->isCheckable())
                b->setChecked(false);
    });
    connect(ok, &QPushButton::clicked, pop, &QDialog::accept);

    pop->adjustSize();
    pop->show();     // not modal: tuning can be heard while a song plays
}


// Pop-up under a lock button: pick the number to lock to (or switch the lock off).
// Applies to the player, the saved settings and the button state (the Settings window reads them too).
void SynthMixerDialog::showLockMenu(QToolButton *btn, int kind)
{
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);

    QStringList names;
    QList<int> numbers;
    bool locked = false;
    int current = 0;
    const char *keyOn = nullptr, *keyNum = nullptr;

    if (kind == 0) {
        names = MidiHelper::drumKitNumberNames();
        for (int i = 0; i < names.count(); i++) numbers << i;
        locked = player->isLockDrum();  current = player->lockDrumNumber();
        keyOn = "MidiLockDrum";  keyNum = "MidiLockDrumNumber";
    } else if (kind == 3) {
        names = MidiHelper::kickNumberName();
        numbers << 35 << 36;
        locked = player->isLockKick();  current = player->lockKickNumber();
        keyOn = "MidiLockKick";  keyNum = "MidiLockKickNumber";
    } else if (kind == 1) {
        names = MidiHelper::snareNumberName();
        numbers = MidiHelper::snareNumbers();
        locked = player->isLockSnare();  current = player->lockSnareNumber();
        keyOn = "MidiLockSnare";  keyNum = "MidiLockSnareNumber";
    } else {
        const QStringList gm = MidiHelper::GMInstrumentNumberNames();
        for (int i = 32; i < 40 && i < gm.count(); i++) {
            names << gm[i];
            numbers << i;
        }
        locked = player->isLockBass();  current = player->lockBassNumber();
        keyOn = "MidiLockBass";  keyNum = "MidiLockBassNumber";
    }

    QMenu menu(this);
    menu.setMinimumWidth(qMax(btn->width(), 200));

    QAction *off = menu.addAction(tr("ปิดล็อก (ตามไฟล์เพลง)"));
    off->setCheckable(true);
    off->setChecked(!locked);
    menu.addSeparator();

    QList<QAction *> acts;
    for (int i = 0; i < names.count(); i++) {
        QAction *a = menu.addAction(names[i]);
        a->setCheckable(true);
        a->setChecked(locked && numbers[i] == current);
        acts << a;
    }

    // close by itself when the mouse leaves the pop-up
    QTimer leave;
    leave.setInterval(120);
    bool entered = false;
    int outside = 0;
    connect(&leave, &QTimer::timeout, &menu, [&]() {
        const QRect area = menu.frameGeometry();
        const QPoint pos = QCursor::pos();
        if (area.contains(pos)) { entered = true; outside = 0; return; }
        if (!entered && area.adjusted(-40, -40, 40, 40).contains(pos)) { outside = 0; return; }
        outside += leave.interval();
        if (outside >= (entered ? 350 : 1500))
            menu.close();
    });
    leave.start();

    QAction *chosen = menu.exec(btn->mapToGlobal(QPoint(0, btn->height() + 2)));
    leave.stop();

    if (chosen == off) {
        if (kind == 0) player->setLockDrum(false);
        else if (kind == 1) player->setLockSnare(false);
        else if (kind == 3) player->setLockKick(false);
        else player->setLockBass(false);
        st.setValue(keyOn, false);
    } else if (chosen) {
        const int idx = acts.indexOf(chosen);
        if (idx >= 0) {
            const int n = numbers[idx];
            if (kind == 0) player->setLockDrum(true, n);
            else if (kind == 1) player->setLockSnare(true, n);
            else if (kind == 3) player->setLockKick(true, n);
            else player->setLockBass(true, n);
            st.setValue(keyOn, true);
            st.setValue(keyNum, n);
        }
    }

    syncQuickChecks();     // button lit = lock on; tooltips show the number
}
