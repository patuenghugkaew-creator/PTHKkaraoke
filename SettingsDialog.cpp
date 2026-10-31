#include "SettingsDialog.h"
#include "ui_SettingsDialog.h"

#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QFontDialog>
#include <QColorDialog>
#include <QFile>
#include <QFileInfo>
#include <QApplication>
#include <QSettings>
#include <QClipboard>
#include <QDialog>
#include <QElapsedTimer>
#include <cmath>
#include <bass.h>
#include <bassmidi.h>

#include "Config.h"
#include "Utils.h"
#include "Midi/MidiHelper.h"
#include "Midi/AsioOutput.h"
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QSignalBlocker>
#include <QTimer>
#include <QSlider>
#include <QScrollArea>
#include <QScrollBar>
#include <functional>
#include <memory>
#include <QFrame>
#include <QPushButton>
#include <QHBoxLayout>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include "Dialogs/MapSoundfontDialog.h"
#include "Dialogs/MapChannelDialog.h"
#include "Dialogs/Equalizer31BandDialog.h"
#include "Dialogs/Chorus2Dialog.h"
#include "Dialogs/Reverb2Dialog.h"

SettingsDialog::SettingsDialog(QWidget *parent, MainWindow *m) :
    QDialog(parent),
    ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);
    this->mainWin = m;
    settings = new QSettings(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    db = mainWin->database();


    { // ทั่วไป
        ui->chbRemoveFromList->setChecked( mainWin->removeFromPlaylist() );
        ui->chbAutoPlaynext->setChecked( mainWin->autoPlayNext() );

        ui->spinSearch->setValue( mainWin->searchTimeout() );
        ui->spinPlaylist->setValue( mainWin->playlistTimout() );

//        int bg = settings->value("BackgroundType", 0).toInt();
//        QString color = settings->value("BackgroundColor", "#525252").toString();
//        QString img = settings->value("BackgroundImage", "").toString();

        ui->lbBgColor->setText(mainWin->backgroundWidget()->backgroundColor().name());
        ui->lbBgColor->setStyleSheet("background-color : " + ui->lbBgColor->text());
        ui->leBgImgPath->setText(mainWin->backgroundWidget()->backgroundImage());
        switch (mainWin->backgroundWidget()->backgroundType()) {
        case Background::Color:
            ui->radioBgColor->setChecked(true);
            ui->lbBgImgN->setEnabled(false);
            ui->btnBgImg->setEnabled(false);
            ui->leBgImgPath->setEnabled(false);
            break;
        case Background::Image:
            ui->radioBgImg->setChecked(true);
            ui->lbBgColorN->setEnabled(false);
            ui->lbBgColor->setEnabled(false);
            ui->btnBgColor->setEnabled(false);
            break;
        default:
            break;
        }

        { // automatic update
            ui->spinUpdateInterval->setMaximum(24 * 7);
            ui->spinUpdateInterval->setValue(qBound(1, settings->value("Update/IntervalHours", 24).toInt(), 24 * 7));
            const bool on = settings->value("Update/Auto", true).toBool();
            ui->chbAutoCheckUpdate->setChecked(on);
            ui->lbUpdateInterval->setEnabled(on);
            ui->spinUpdateInterval->setEnabled(on);
            connect(ui->spinUpdateInterval, SIGNAL(valueChanged(int)), this, SLOT(onSpinUpdateValueChanged(int)));
        }

        connect(ui->spinSearch, SIGNAL(valueChanged(int)), this, SLOT(onSpinSearchValueChanged(int)));
        connect(ui->spinPlaylist, SIGNAL(valueChanged(int)), this, SLOT(onSpinPlaylistValueChanged(int)));

        connect(ui->radioBgColor, SIGNAL(toggled(bool)), this, SLOT(onRadioBgColorToggled(bool)));
        connect(ui->radioBgImg, SIGNAL(toggled(bool)), this, SLOT(onRadioBgImgToggled(bool)));


        // playlist
        //connect(ui->chbRemoveFromList, SIGNAL(toggled(bool)), this, SLOT(on_chbRemoveFromList_toggled(bool)));

        // background
        //connect(ui->radioBgColor, SIGNAL(toggled(bool)), this, SLOT(on_radioBgColor_toggled(bool)));
        //connect(ui->radioBgImg, SIGNAL(toggled(bool)), this, SLOT(on_radioBgImg_toggled(bool)));
    }


    // Lyrics
    {
        QFont font = mainWin->lyricsWidget()->textFont();
        setLabelFontInfo(&font);

        int l1y = mainWin->lyricsWidget()->line1Y();
        int l2y = mainWin->lyricsWidget()->line2Y();
        int anmtTime = mainWin->lyricsWidget()->animationTime();
        bool autosize = mainWin->lyricsWidget()->isAutoFontSize();
        ui->spinLint1Y->setValue(l1y);
        ui->spinLint2Y->setValue(l2y);
        ui->spinAnmtTime->setValue(anmtTime);
        ui->chbLyrAutoSize->setChecked(autosize);

        LinePosition l1 = mainWin->lyricsWidget()->line1Position();
        LinePosition l2 = mainWin->lyricsWidget()->line2Position();
        ui->cbLine1X->setCurrentIndex(static_cast<int>(l1));
        ui->cbLine2X->setCurrentIndex(static_cast<int>(l2));

        QString textColor =  mainWin->lyricsWidget()->textColor().name();
        QString textBorderColor = mainWin->lyricsWidget()->textBorderColor().name();
        QString textBorderOutColor = mainWin->lyricsWidget()->textBorderOutColor().name();
        int tW = mainWin->lyricsWidget()->textBorderWidth();
        int tOW = mainWin->lyricsWidget()->textBorderOutWidth();
        ui->lbTextColor->setText(textColor);
        ui->lbTextColor->setStyleSheet("background-color : " + textColor);
        ui->lbTextBorderColor->setText(textBorderColor);
        ui->lbTextBorderColor->setStyleSheet("background-color : " + textBorderColor);
        ui->lbTextBorderOutColor->setText(textBorderOutColor);
        ui->lbTextBorderOutColor->setStyleSheet("background-color : " + textBorderOutColor);
        ui->spinTextBorderWidth->setValue(tW);
        ui->spinTextBorderOutWidth->setValue(tOW);


        QString curColor =  mainWin->lyricsWidget()->curColor().name();
        ui->lbCurColor->setStyleSheet("background-color : " + curColor);
        ui->lbCurColor->setText(curColor);

        QString curBorderColor =  mainWin->lyricsWidget()->curBorderColor().name();
        QString curBorderOutColor = mainWin->lyricsWidget()->curBorderOutColor().name();
        ui->lbCurBorderColor->setStyleSheet("background-color : " + curBorderColor);
        ui->lbCurBorderColor->setText(curBorderColor);
        ui->lbCurBorderOutColor->setStyleSheet("background-color : " + curBorderOutColor);
        ui->lbCurBorderOutColor->setText(curBorderOutColor);

        int w = mainWin->lyricsWidget()->curBorderWidth();
        int w2 = mainWin->lyricsWidget()->curBorderOutWidth();
        ui->spinCurBorderWidth->setValue(w);
        ui->spinCurBorderOutWidth->setValue(w2);

        connect(ui->spinLint1Y, SIGNAL(valueChanged(int)),
                this, SLOT(onSpinLine1YValueChanged(int)));
        connect(ui->spinLint2Y, SIGNAL(valueChanged(int)),
                this, SLOT(onSpinLine2YValueChanged(int)));
        connect(ui->spinAnmtTime, SIGNAL(valueChanged(int)),
                this, SLOT(onSpinAnmtTimeValueChanged(int)));
        connect(ui->chbLyrAutoSize, SIGNAL(toggled(bool)),
                this, SLOT(onChbLyrAutoSizeToggled(bool)));

        connect(ui->spinTextBorderWidth, SIGNAL(valueChanged(int)),
                this, SLOT(onSpinTextBorderWidthValueChanged(int)));
        connect(ui->spinTextBorderOutWidth, SIGNAL(valueChanged(int)),
                this, SLOT(onSpinTextBorderOutWidthValueChanged(int)));

        connect(ui->spinCurBorderWidth, SIGNAL(valueChanged(int)),
                this, SLOT(onSpinCurBorderWidthValueChanged(int)));
        connect(ui->spinCurBorderOutWidth, SIGNAL(valueChanged(int)),
                this, SLOT(onSpinCurBorderOutWidthValueChanged(int)));
    }


    // Database
    ui->leNCNPath->setText(db->ncnPath());
    if ((db->hnkPath().isEmpty() || !QDir(db->hnkPath()).exists() || db->hnkPath().endsWith("/Songs/HNK")) && !db->ncnPath().isEmpty()) {
        QString autoEmk = QFileInfo(db->ncnPath()).dir().absoluteFilePath("EMK");
        if (QDir(autoEmk).exists()) {
            db->setHNKPath(autoEmk);
            settings->setValue("HNKPath", autoEmk);
        }
    }
    ui->leHNKPath->setText(db->hnkPath());
    ui->leKARPath->setText(db->karPath());
    ui->lbCountSongsValue->setText(QString::number(db->count()) + " เพลง");

    if (db->isRunning())
    {
        ui->barUpdateSongs->setMaximum(db->updateCount());
        ui->btnUpdateSongs->setEnabled(false);
        ui->btnNCNPath->setEnabled(false);
        ui->btnHNKPath->setEnabled(false);
        ui->btnKARPath->setEnabled(false);
        ui->lbCountSongsText->setEnabled(false);
        ui->lbCountSongsValue->setEnabled(false);
    }
    else
    {
        ui->lbUpdateText->hide();
        ui->lbUpdateValue->hide();
    }

    connect(db, SIGNAL(updateCountChanged(int)), ui->barUpdateSongs, SLOT(setMaximum(int)));
    connect(db, SIGNAL(updatePositionChanged(int)), ui->barUpdateSongs, SLOT(setValue(int)));
    connect(db, SIGNAL(updateSongNameChanged(QString)), ui->lbUpdateValue, SLOT(setText(QString)));
    connect(db, SIGNAL(finished()), this, SLOT(on_upDbUpdateFinished()));


    // Device
    initDeviceTab();


    { // Synth tab
        MidiSynthesizer *synth =  mainWin->midiPlayer()->midiSynthesizer();

        ui->chbSfLoadAll->setChecked(synth->isLoadAllSoundfont());
        ui->listsfFiles->addItems(synth->soundfontFiles());

        if (synth->equalizer31BandFXs()[0]->isOn())
            ui->btnEq->setIcon(QIcon(":/Icons/circle_green.png"));
        else
            ui->btnEq->setIcon(QIcon(":/Icons/circle_red.png"));

        if (synth->reverbFXs()[0]->isOn())
            ui->btnReverb->setIcon(QIcon(":/Icons/circle_green.png"));
        else
            ui->btnReverb->setIcon(QIcon(":/Icons/circle_red.png"));

        if (synth->chorusFXs()[0]->isOn())
            ui->btnChorus->setIcon(QIcon(":/Icons/circle_green.png"));
        else
            ui->btnChorus->setIcon(QIcon(":/Icons/circle_red.png"));


        connect(ui->sliderSfVolume, SIGNAL(valueChanged(int)),
                   this, SLOT(onSliderSfValueChanged(int)));

        connect(ui->listsfFiles, SIGNAL(currentRowChanged(int)),
                this, SLOT(onListSfCurrentRowChanged(int)));

        connect(ui->chbSfLoadAll, SIGNAL(toggled(bool)),
                this, SLOT(onChbSfLoadAllToggled(bool)));
    }

}

SettingsDialog::~SettingsDialog()
{
    MidiSynthesizer *synth = mainWin->midiPlayer()->midiSynthesizer();

    // setting soundfont volume
    settings->beginWriteArray("SynthSoundfontsVolume");
    for (int i=0; i<ui->listsfFiles->count(); i++) {
        settings->setArrayIndex(i);
        settings->setValue("SoundfontVolume", (int)(synth->soundfontVolume(i) * 100));
    }
    settings->endArray();

    delete settings;
    delete ui;
}

void SettingsDialog::setLabelFontInfo(QFont *font)
{
    QString fFamily = font->family();
    QString fSize = QString::number(font->pointSize());
    QString fStyle = "";

    switch (font->weight()) {
        case QFont::Thin:       fStyle = "Thin";        break;
        case QFont::ExtraLight: fStyle = "ExtraLight";  break;
        case QFont::Light:      fStyle = "Light";       break;
        case QFont::Normal:     fStyle = "Normal";      break;
        case QFont::Medium:     fStyle = "Medium";      break;
        case QFont::DemiBold:   fStyle = "DemiBold";    break;
        case QFont::Bold:       fStyle = "Bold";        break;
        case QFont::ExtraBold:  fStyle = "ExtraBold";   break;
        case QFont::Black:      fStyle = "Black";       break;
    }

    if (font->italic()) fStyle += " Italic";
    if (font->underline()) fStyle += " Underline";
    if (font->strikeOut()) fStyle += " StrikeOut";

    ui->leFont->setText(" " + fFamily + " " + fStyle + " " + fSize);
}

namespace {
// A scroll area that asks for the full height of its content (so a big screen shows everything, as before) but can
// shrink to almost nothing (so a small screen scrolls instead of the dialog growing past the screen edge).
class FitScrollArea : public QScrollArea
{
public:
    explicit FitScrollArea(QWidget *parent = nullptr) : QScrollArea(parent) {}
    QSize sizeHint() const override
    {
        if (QWidget *w = widget())
            return w->sizeHint() + QSize(verticalScrollBar()->sizeHint().width() + 4, 4);
        return QScrollArea::sizeHint();
    }
    QSize minimumSizeHint() const override { return QSize(300, 120); }
};
}

void SettingsDialog::initDeviceTab()
{
    // Midi out device: system/virtual MIDI ports first, the SoundFont synthesizer last
    // (on_cbMidiOut_activated treats the last item as the synthesizer).
    MidiPlayer *player = mainWin->midiPlayer();
    for (QString deviceName : MidiPlayer::midiDevices()) {
        ui->cbMidiOut->addItem(deviceName);
    }
    ui->cbMidiOut->addItem("Midi Synthesizer (SoundFont)");
    int dfd = player->midiOutPortNumber();
    ui->cbMidiOut->setCurrentIndex(dfd == -1 ? ui->cbMidiOut->count() - 1 : dfd);


    // Midi In device
    ui->cbMidiIn->addItem("None");
    for (QString deviceName : MidiPlayer::midiInDevices()) {
        ui->cbMidiIn->addItem(deviceName);
    }
    ui->cbMidiIn->setCurrentIndex(player->midiInPortNumber()+1);


    // Audio devices
    QStringList dvnames = MidiSynthesizer::audioDevices();
    int aSelected = 0;
    for (int i=1; i<dvnames.count(); i++)
    {
        ui->cbAudioOut->addItem(dvnames[i]);
        if (i == player->midiSynthesizer()->defaultDevice())
            aSelected = i-1;
    }
    // ASIO drivers are listed after the normal Windows devices
    const int normalCount = dvnames.count() > 0 ? dvnames.count() - 1 : 0;
    const QStringList asioNames = AsioOutput::devices();
    for (const QString &n : asioNames)
        ui->cbAudioOut->addItem("ASIO: " + n);
    if (player->midiSynthesizer()->asioDevice() >= 0
            && player->midiSynthesizer()->asioDevice() < asioNames.count())
        aSelected = normalCount + player->midiSynthesizer()->asioDevice();
    ui->cbAudioOut->setCurrentIndex(aSelected);


    { // lock drum, snare, bass
        ui->cbLockDrum->addItems(MidiHelper::drumKitNumberNames());
        ui->cbLockSnare->addItems(MidiHelper::snareNumberName());
        ui->cbLockKick->addItems(MidiHelper::kickNumberName());

        QStringList intsNames = MidiHelper::GMInstrumentNumberNames();
        for (int i=32; i<40; i++) {
            ui->cbLockBass->addItem(intsNames[i]);
        }

        ui->cbLockDrum->setCurrentIndex( player->lockDrumNumber() );
        ui->cbLockBass->setCurrentIndex( player->lockBassNumber() - 32 );
        {
            const int si = MidiHelper::snareNumbers().indexOf(player->lockSnareNumber());
            ui->cbLockSnare->setCurrentIndex(si >= 0 ? si : 2);
        }

        ui->cbLockKick->setCurrentIndex(player->lockKickNumber() == 35 ? 0 : 1);

        // check lock or not
        if (player->isLockKick())
            ui->chbLockKick->setChecked(true);
        else
            ui->cbLockKick->setEnabled(false);

        if (player->isLockDrum())
            ui->chbLockDrum->setChecked(true);
        else
            ui->cbLockDrum->setEnabled(false);

        if (player->isLockSnare())
            ui->chbLockSnare->setChecked(true);
        else
            ui->cbLockSnare->setEnabled(false);

        if (player->isLockBass())
            ui->chbLockBass->setChecked(true);
        else
            ui->cbLockBass->setEnabled(false);
    }

    MidiSynthesizer *synth = player->midiSynthesizer();
    ui->chbSynthFloat->setChecked(synth->isUseFloattingPoint());
    ui->chbSynthFx->setChecked(synth->isUseFXRC());
    ui->sliderBuffer->setValue(BASS_GetConfig(BASS_CONFIG_BUFFER));

    connect(ui->chbLockDrum, SIGNAL(toggled(bool)), this, SLOT(onChbLockDrumToggled(bool)));
    connect(ui->chbLockSnare, SIGNAL(toggled(bool)), this, SLOT(onChbLockSnareToggled(bool)));
    connect(ui->chbLockKick, SIGNAL(toggled(bool)), this, SLOT(onChbLockKickToggled(bool)));
    connect(ui->chbLockBass, SIGNAL(toggled(bool)), this, SLOT(onChbLockBassToggled(bool)));

    connect(ui->chbSynthFloat, SIGNAL(toggled(bool)), this, SLOT(onChbFloatPointToggled(bool)));
    connect(ui->chbSynthFx, SIGNAL(toggled(bool)), this, SLOT(onChbUseFXToggled(bool)));
    connect(ui->sliderBuffer, SIGNAL(valueChanged(int)), this, SLOT(onSliderBufferValueChanged(int)));

    // ---- Audio quality -------------------------------------------------------------------------------------
    // Voices, reverb strength. Sample rate is fixed at 44.1 kHz and the interpolation is linear.
    if (QVBoxLayout *vl = qobject_cast<QVBoxLayout *>(ui->groupBox_12->parentWidget()->layout()))
    {
        QGroupBox *box = new QGroupBox(QString::fromUtf8("คุณภาพเสียง"), ui->groupBox_12->parentWidget());
        QGridLayout *g = new QGridLayout(box);

        auto pro = [](const QString &t, bool) { return t; };

        QComboBox *cbVoices = new QComboBox(box);
        const int vcs[] = { 256, 500, 750, 1000 };
        for (int n : vcs)
            cbVoices->addItem(pro(QString::number(n), n != 256), n);
        cbVoices->setCurrentIndex(qMax(0, cbVoices->findData(synth->voices())));

        QLabel *note = new QLabel(QString::fromUtf8(
            "ความถี่สูงและค่าความหน่วงต่ำจะเห็นผลเต็มที่เมื่อใช้ ASIO หรือซาวด์การ์ดที่รองรับ"), box);
        note->setWordWrap(true);

        g->addWidget(new QLabel(QString::fromUtf8("จำนวนเสียงสูงสุด (Voices) :"), box), 2, 0);
        g->addWidget(cbVoices, 2, 1);
        QComboBox *cbRv = new QComboBox(box);
        cbRv->addItem(QString::fromUtf8("แบบกระชับ"), false);
        cbRv->addItem(QString::fromUtf8("แบบแรง / หางเสียงยาว"), true);
        cbRv->setCurrentIndex(synth->isReverbStrong() ? 1 : 0);
        g->addWidget(new QLabel(QString::fromUtf8("ความแรงรีเวิร์บของช่อง :"), box), 3, 0);
        g->addWidget(cbRv, 3, 1);
        connect(cbRv, QOverload<int>::of(&QComboBox::activated), this, [this, cbRv, synth](int i) {
            const bool strong = cbRv->itemData(i).toBool();
            synth->setReverbStrong(strong);
            settings->setValue("ReverbStrong", strong);
        });
        g->addWidget(note, 4, 0, 1, 2);
        g->setColumnStretch(1, 1);
        vl->insertWidget(vl->indexOf(ui->groupBox_12) + 1, box);
        ui->groupBox_12->setVisible(false);   // "Midi Synthesizer" box (32-bit float, effects, latency) is not shown any more; the latency lives in the audio device panel

        connect(cbVoices, QOverload<int>::of(&QComboBox::activated), this, [this, cbVoices, synth](int i) {
            const int n = cbVoices->itemData(i).toInt();
            synth->setVoices(n);
            settings->setValue("SynthVoices", n);
        });
    }

    // ---- Audio device panel: device type / output / test / active output channels / sample rate / buffer size ----
    if (QVBoxLayout *dvl = qobject_cast<QVBoxLayout *>(ui->groupBox_12->parentWidget()->layout()))
    {
        QWidget *host = ui->groupBox_12->parentWidget();
        QGroupBox *dev = new QGroupBox(QString::fromUtf8("อุปกรณ์เสียง (Audio device)"), host);
        QGridLayout *dg = new QGridLayout(dev);

        QComboBox *cbType = new QComboBox(dev);
        QComboBox *cbOut = new QComboBox(dev);
        QPushButton *btnTest = new QPushButton(QStringLiteral("Test"), dev);
        QFrame *chFrame = new QFrame(dev);
        chFrame->setFrameShape(QFrame::StyledPanel);
        QVBoxLayout *chLay = new QVBoxLayout(chFrame);
        chLay->setContentsMargins(6, 4, 6, 4);
        chLay->setSpacing(2);
        QComboBox *cbSr = new QComboBox(dev);
        QComboBox *cbBuf = new QComboBox(dev);
        QCheckBox *cbSplit = new QCheckBox(QString::fromUtf8("แยกลำโพงตามแทรก (ASIO): หน้า → 1+2, หลัง → 3+4, กลาง/ซับ → 5+6, ข้าง → 7+8"), dev);
        cbSplit->setToolTip(QString::fromUtf8("ส่งตำแหน่งลำโพงที่ตั้งไว้ในแต่ละแทรก (เมนูแยกลำโพง) ออกเป็นคู่เอาต์พุตของการ์ดเสียง ต้องมีเอาต์พุตอย่างน้อย 4 ช่อง\n"
                                                "เมื่อเปิดใช้ ตัวเลือก Active output channels ด้านบนจะไม่ถูกใช้"));
        QLabel *note = new QLabel(dev);
        note->setWordWrap(true);
        cbOut->setMinimumWidth(260);
        btnTest->setToolTip(QString::fromUtf8("เล่นโน้ตทดสอบสั้นๆ ผ่านเอาต์พุตที่เลือก (ใช้ได้เมื่อเพลงหยุดเล่นอยู่)"));

        dg->addWidget(new QLabel(QStringLiteral("Audio device type:"), dev), 0, 0, Qt::AlignRight | Qt::AlignVCenter);
        dg->addWidget(cbType, 0, 1, 1, 2);
        dg->addWidget(new QLabel(QStringLiteral("Output:"), dev), 1, 0, Qt::AlignRight | Qt::AlignVCenter);
        dg->addWidget(cbOut, 1, 1);
        dg->addWidget(btnTest, 1, 2);
        dg->addWidget(new QLabel(QStringLiteral("Active output channels:"), dev), 2, 0, Qt::AlignRight | Qt::AlignTop);
        dg->addWidget(chFrame, 2, 1, 1, 2);
        {   // the sample rate is fixed at 44.1 kHz: the box stays hidden (its code still keeps the ASIO driver in step)
            QLabel *lbSr = new QLabel(QStringLiteral("Sample rate:"), dev);
            lbSr->setVisible(false);
            cbSr->setVisible(false);
            dg->addWidget(lbSr, 3, 0, Qt::AlignRight | Qt::AlignVCenter);
            dg->addWidget(cbSr, 3, 1, 1, 2);
        }
        dg->addWidget(new QLabel(QStringLiteral("Audio buffer size:"), dev), 4, 0, Qt::AlignRight | Qt::AlignVCenter);
        dg->addWidget(cbBuf, 4, 1, 1, 2);
        dg->addWidget(cbSplit, 5, 0, 1, 3);
        dg->addWidget(note, 6, 0, 1, 3);
        dg->setColumnStretch(1, 1);
        dvl->insertWidget(dvl->indexOf(ui->groupBox_12), dev);

        // the old single controls (output list, latency slider) are replaced by this panel
        ui->label_7->setVisible(false);
        ui->cbAudioOut->setVisible(false);
        // (the latency slider stays: drag it for any value, the Buffer size list above follows it)

        auto normalCount = []() { return qMax(0, MidiSynthesizer::audioDevices().count() - 1); };

        // -------- fill every control from the real state of the synth / ASIO driver --------
        auto refreshFn = std::make_shared<std::function<void()>>();
        auto refresh = [refreshFn]() { (*refreshFn)(); };
        *refreshFn = [=]() {
            const bool isAsio = synth->asioDevice() >= 0;
            const QStringList asioNames = AsioOutput::devices();
            const QStringList winNames = MidiSynthesizer::audioDevices();
            {
                QSignalBlocker b1(cbType), b2(cbOut);
                cbType->clear();
                cbType->addItem(QStringLiteral("Windows Audio"), 0);
                if (!asioNames.isEmpty())
                    cbType->addItem(QStringLiteral("ASIO"), 1);
                cbType->setCurrentIndex(isAsio && cbType->count() > 1 ? 1 : 0);
                cbOut->clear();
                if (isAsio) {
                    for (const QString &n : asioNames) cbOut->addItem(n);
                    cbOut->setCurrentIndex(qBound(0, synth->asioDevice(), qMax(0, cbOut->count() - 1)));
                } else {
                    for (int i = 1; i < winNames.count(); i++) cbOut->addItem(winNames[i]);
                    cbOut->setCurrentIndex(qBound(0, synth->defaultDevice() - 1, qMax(0, cbOut->count() - 1)));
                }
            }

            // active output channels (ASIO: every stereo pair of the driver; Windows: the device's own stereo out)
            while (QLayoutItem *it = chLay->takeAt(0)) {
                if (QWidget *w = it->widget()) w->deleteLater();
                delete it;
            }
            const int outs = isAsio ? AsioOutput::outputCount() : 0;
            if (outs >= 2) {
                const unsigned mask = AsioOutput::pairMask();
                for (int pr = 0; pr < outs / 2; pr++) {
                    QCheckBox *c = new QCheckBox(QStringLiteral("Output channel %1 + %2").arg(2 * pr + 1).arg(2 * pr + 2), chFrame);
                    c->setChecked(pr == 0 ? ((mask & 1u) || mask == 0) : ((mask >> pr) & 1u));
                    chLay->addWidget(c);
                    QObject::connect(c, &QCheckBox::toggled, dev, [=](bool) {
                        unsigned m = 0;
                        int n = 0;
                        QList<QCheckBox *> boxes = chFrame->findChildren<QCheckBox *>();
                        for (int i = 0; i < boxes.count(); i++)
                            if (boxes[i]->isChecked()) { m |= (1u << i); n++; }
                        if (n == 0) {                    // at least one pair must stay on
                            QSignalBlocker bb(c);
                            c->setChecked(true);
                            return;
                        }
                        if (m == AsioOutput::pairMask())
                            return;
                        AsioOutput::setPairMask(m);
                        settings->setValue("AsioPairMask", m);
                        mainWin->stop();
                        if (synth->isOpened()) { synth->close(); synth->open(); }
                        if (synth->asioDevice() < 0)
                            QMessageBox::warning(this, tr("ASIO"), tr("Could not start this ASIO driver. The normal output device is used instead.")
                                                 + QStringLiteral("\n") + synth->asioError());
                        settings->setValue("AsioDevice", synth->asioDevice());
                        // rebuild the panel after this click has finished
                        QTimer::singleShot(0, dev, [refresh]() { refresh(); });
                    });
                }
                chFrame->setEnabled(true);
            } else {
                QCheckBox *c = new QCheckBox(QStringLiteral("Output channel 1 + 2"), chFrame);
                c->setChecked(true);
                c->setEnabled(false);
                chLay->addWidget(c);
                c->setToolTip(QString::fromUtf8("Windows Audio ใช้ช่องสเตอริโอของอุปกรณ์เอง — เลือกช่องได้เมื่อใช้ ASIO"));
            }

            // speaker split: ASIO with 4+ output channels only; the pair list above is not used while it is on
            {
                QSignalBlocker bs(cbSplit);
                const bool canSplit = isAsio && AsioOutput::outputCount() >= 4;
                cbSplit->setEnabled(canSplit || AsioOutput::speakerSplit());
                cbSplit->setChecked(AsioOutput::speakerSplit());
                chFrame->setEnabled(!(AsioOutput::speakerSplit() && AsioOutput::splitChannels() > 0));
            }

            // sample rate
            {
                QSignalBlocker b(cbSr);
                cbSr->clear();
                const int rates[] = { 44100, 48000, 88200, 96000 };
                for (int r : rates) {
                    if (isAsio && AsioOutput::outputCount() > 0 && r != synth->sampleRateHz() && !AsioOutput::rateSupported(r))
                        continue;       // the driver says it cannot run at this rate
                    cbSr->addItem(QString::number(r) + QStringLiteral(" Hz") + QString(), r);
                }
                cbSr->setCurrentIndex(qMax(0, cbSr->findData(synth->sampleRateHz())));
            }

            // buffer size
            {
                QSignalBlocker b(cbBuf);
                cbBuf->clear();
                int rate = isAsio ? AsioOutput::currentRate() : synth->sampleRateHz();
                if (rate < 8000) rate = synth->sampleRateHz() > 0 ? synth->sampleRateHz() : 44100;
                auto label = [rate](int samples) {
                    return QStringLiteral("%1 samples (%2 ms)").arg(samples).arg(samples * 1000.0 / rate, 0, 'f', 1);
                };
                int mn, mx, pref, gran;
                if (isAsio && AsioOutput::bufferInfo(mn, mx, pref, gran)) {
                    cbBuf->addItem(QStringLiteral("Driver default: ") + label(pref), 0);
                    const int cand[] = { 32, 64, 96, 128, 192, 240, 256, 384, 480, 512, 768, 960, 1024, 1536, 2048, 4096 };
                    for (int v : cand) {
                        if (v < mn || v > mx || v == pref) continue;
                        bool okSize = true;
                        if (gran == -1) okSize = (v & (v - 1)) == 0;                  // powers of two only
                        else if (gran > 0) okSize = ((v - mn) % gran) == 0;
                        else okSize = false;                                          // fixed size driver
                        if (okSize) cbBuf->addItem(label(v), v);
                    }
                    cbBuf->setCurrentIndex(qMax(0, cbBuf->findData(AsioOutput::bufferSize())));
                    cbBuf->setEnabled(cbBuf->count() > 1);
                } else {
                    const int msList[] = { 10, 20, 30, 40, 50, 60, 80, 100, 150, 200, 300, 500 };
                    const int cur = ui->sliderBuffer->value();
                    bool found = false;
                    for (int ms : msList) {
                        cbBuf->addItem(label(ms * rate / 1000) + QString(), ms);
                        if (ms == cur) found = true;
                    }
                    if (!found) cbBuf->addItem(label(cur * rate / 1000), cur);
                    cbBuf->setCurrentIndex(qMax(0, cbBuf->findData(cur)));
                    cbBuf->setEnabled(true);
                }
            }

            note->setText(isAsio
                ? (AsioOutput::speakerSplit()
                    ? QString::fromUtf8("ASIO: โหมดแยกลำโพง — แต่ละแทรกออกตามตำแหน่งที่ตั้งไว้ (หน้า 1+2, หลัง 3+4, กลาง/ซับ 5+6, ข้าง 7+8)  •  ถ้าไม่มีเสียง ตรวจว่าเปิดช่องเอาต์พุตของไดรเวอร์แล้ว")
                    : QString::fromUtf8("ASIO: เสียงหลักถูกส่งไปทุกคู่ช่องที่ติ๊กไว้  •  ถ้าไม่มีเสียง ตรวจว่าเปิดช่องเอาต์พุตของไดรเวอร์แล้ว (เช่น ASIO4ALL)"))
                : QString::fromUtf8("Windows Audio  •  ต้องการความหน่วงต่ำมาก ให้เลือกชนิดอุปกรณ์เป็น ASIO"));
        };

        // -------- switching output --------
        auto applyOutput = [=](bool asio, int k) {
            const int target = asio ? normalCount() + k : k;
            if (target < 0 || target >= ui->cbAudioOut->count())
                return;
            QSignalBlocker bl(ui->cbAudioOut);
            ui->cbAudioOut->setCurrentIndex(target);
            on_cbAudioOut_activated(target);          // the proven path: stops playback, rebuilds the synth on the device
            if (asio && synth->asioDevice() >= 0)
                settings->setValue("AsioLastDevice", synth->asioDevice());
        };

        auto busy = std::make_shared<bool>(false);       // ignore clicks while a driver is being switched
        connect(cbType, QOverload<int>::of(&QComboBox::activated), this, [=](int i) {
            if (*busy) return;
            *busy = true;
            struct Rel { std::shared_ptr<bool> b; ~Rel() { *b = false; } } rel{busy};
            const bool asio = cbType->itemData(i).toInt() == 1;
            if (asio) {
                // try the last used driver first, then the others: the first one in the list is often a driver that
                // cannot start on this PC (not installed for this bit-ness, device unplugged, busy ...)
                const int n = AsioOutput::devices().count();
                int k = settings->value("AsioLastDevice", 0).toInt();
                if (k < 0 || k >= n) k = 0;
                mainWin->stop();
                bool ok = false;
                QString why;
                for (int t = 0; t < n && !ok; t++) {
                    const int idx = (k + t) % n;
                    ok = synth->setAsioDevice(idx);
                    if (!ok) why = synth->asioError();
                }
                settings->setValue("AsioDevice", synth->asioDevice());
                if (ok) {
                    settings->setValue("AsioLastDevice", synth->asioDevice());
                } else {
                    QMessageBox::warning(this, tr("ASIO"),
                        tr("Could not start any ASIO driver. The normal output device is used instead.")
                        + (why.isEmpty() ? QString() : QStringLiteral("\n") + why));
                }
            } else {
                applyOutput(false, qBound(0, synth->defaultDevice() - 1, qMax(0, normalCount() - 1)));
            }
            refresh();
        });
        connect(cbOut, QOverload<int>::of(&QComboBox::activated), this, [=](int k) {
            if (*busy) return;
            *busy = true;
            struct Rel { std::shared_ptr<bool> b; ~Rel() { *b = false; } } rel{busy};
            applyOutput(synth->asioDevice() >= 0 || cbType->currentData().toInt() == 1, k);
            refresh();
        });
        connect(cbSplit, &QCheckBox::clicked, this, [=](bool on) {
            if (on == AsioOutput::speakerSplit())
                return;
            AsioOutput::setSpeakerSplit(on);
            settings->setValue("AsioSpeakerSplit", on);
            mainWin->stop();
            if (synth->asioDevice() >= 0 && synth->isOpened()) {     // restart the ASIO feed with / without the split
                synth->close();
                synth->open();
                if (synth->asioDevice() < 0)
                    QMessageBox::warning(this, tr("ASIO"), tr("Could not start this ASIO driver. The normal output device is used instead.")
                                         + QStringLiteral("\n") + synth->asioError());
                settings->setValue("AsioDevice", synth->asioDevice());
            }
            QTimer::singleShot(0, dev, [refresh]() { refresh(); });
        });

        // -------- test note --------
        connect(btnTest, &QPushButton::clicked, this, [=]() {
            if (!synth->isOpened()) {
                QMessageBox::information(this, QStringLiteral("Test"), QString::fromUtf8("ยังไม่ได้เปิดอุปกรณ์เสียง"));
                return;
            }
            if (!mainWin->midiPlayer()->isPlayerStopped()) {
                QMessageBox::information(this, QStringLiteral("Test"), QString::fromUtf8("เพลงกำลังเล่นอยู่ — ฟังเสียงจากเพลงได้เลย หรือหยุดเพลงก่อนกด Test"));
                return;
            }
            synth->sendNoteOn(0, 72, 100);
            QTimer::singleShot(700, [synth]() { synth->sendNoteOff(0, 72, 0); });
        });

        // -------- sample rate / buffer --------
        connect(cbSr, QOverload<int>::of(&QComboBox::activated), this, [=](int i) {
            const int hz = cbSr->itemData(i).toInt();
            if (!mainWin->midiPlayer()->isPlayerStopped())
                mainWin->stop();
            synth->setSampleRate(hz);
            settings->setValue("SynthSampleRate", hz);
            refresh();
        });
        connect(cbBuf, QOverload<int>::of(&QComboBox::activated), this, [=](int i) {
            const int v = cbBuf->itemData(i).toInt();
            if (synth->asioDevice() >= 0 && AsioOutput::outputCount() > 0) {
                AsioOutput::setBufferSize(v);
                settings->setValue("AsioBufferSamples", v);
                mainWin->stop();
                if (synth->isOpened()) { synth->close(); synth->open(); }
                if (synth->asioDevice() < 0)
                    QMessageBox::warning(this, tr("ASIO"), tr("Could not start this ASIO driver. The normal output device is used instead.")
                                         + QStringLiteral("\n") + synth->asioError());
                settings->setValue("AsioDevice", synth->asioDevice());
            } else {
                ui->sliderBuffer->setValue(v);          // the existing latency code (re-open of the synth)
            }
            refresh();
        });
        // the latency slider (kept) moves the Buffer size list along
        connect(ui->sliderBuffer, &QSlider::valueChanged, this, [refresh](int) {
            QTimer::singleShot(0, [refresh]() { refresh(); });
        });

        refresh();
    }

    // Auto hide of the mixer while the karaoke program is used
    if (QVBoxLayout *vl2 = qobject_cast<QVBoxLayout *>(ui->tab_4->layout()))
    {
        QGroupBox *ah = new QGroupBox(QString::fromUtf8("ซ่อนมิกเซอร์อัตโนมัติ (Auto hide)"), ui->tab_4);
        QGridLayout *g = new QGridLayout(ah);
        QCheckBox *chOn = new QCheckBox(QString::fromUtf8("ซ่อนมิกเซอร์เมื่อใช้งานโปรแกรมคาราโอเกะ (คลิก/พิมพ์ในโปรแกรมคาราโอเกะ)"), ah);
        QDoubleSpinBox *spDelay = new QDoubleSpinBox(ah);
        spDelay->setRange(0.5, 60.0);
        spDelay->setSingleStep(0.5);
        spDelay->setDecimals(1);
        spDelay->setSuffix(QString::fromUtf8(" วินาที"));
        QLineEdit *edProg = new QLineEdit(ah);
        edProg->setPlaceholderText(QString::fromUtf8("เช่น karaoke.exe หรือส่วนของชื่อหน้าต่าง (เว้นว่าง = ทุกโปรแกรม)"));
        QLabel *note = new QLabel(QString::fromUtf8(
            "เมื่อเลิกใช้งาน (ไม่มีการพิมพ์/คลิก) หรือสลับไปโปรแกรมอื่น มิกเซอร์จะกลับมาเองหลังเวลาหน่วงที่ตั้งไว้ "
            "แยกหลายชื่อด้วย ; ได้"), ah);
        note->setWordWrap(true);
        g->addWidget(chOn, 0, 0, 1, 2);
        g->addWidget(new QLabel(QString::fromUtf8("หน่วงเวลาก่อนมิกเซอร์กลับมา :"), ah), 1, 0, Qt::AlignRight | Qt::AlignVCenter);
        {   // the theme hides the spin box arrows: explicit - / + buttons next to the value
            QWidget *dw = new QWidget(ah);
            QHBoxLayout *dh = new QHBoxLayout(dw);
            dh->setContentsMargins(0, 0, 0, 0);
            QPushButton *bm = new QPushButton(QString(QChar(0x2212)), dw);
            QPushButton *bp = new QPushButton(QStringLiteral("+"), dw);
            bm->setFixedSize(32, 26);
            bp->setFixedSize(32, 26);
            spDelay->setButtonSymbols(QAbstractSpinBox::NoButtons);
            spDelay->setMinimumWidth(110);
            dh->addWidget(bm);
            dh->addWidget(spDelay);
            dh->addWidget(bp);
            dh->addStretch(1);
            connect(bm, &QPushButton::clicked, this, [spDelay]() { spDelay->setValue(spDelay->value() - 0.5); });
            connect(bp, &QPushButton::clicked, this, [spDelay]() { spDelay->setValue(spDelay->value() + 0.5); });
            g->addWidget(dw, 1, 1, Qt::AlignLeft);
        }
        g->addWidget(new QLabel(QString::fromUtf8("โปรแกรมคาราโอเกะ :"), ah), 2, 0, Qt::AlignRight | Qt::AlignVCenter);
        g->addWidget(edProg, 2, 1);
        g->addWidget(note, 3, 0, 1, 2);
        g->setColumnStretch(1, 1);

        chOn->setChecked(settings->value("MixerAutoHide", false).toBool());
        spDelay->setValue(settings->value("MixerAutoHideDelay", 3.0).toDouble());
        edProg->setText(settings->value("MixerAutoHideProgram", "").toString());
        spDelay->setEnabled(chOn->isChecked());
        edProg->setEnabled(chOn->isChecked());

        auto apply = [this, chOn, spDelay, edProg]() {
            settings->setValue("MixerAutoHide", chOn->isChecked());
            settings->setValue("MixerAutoHideDelay", spDelay->value());
            settings->setValue("MixerAutoHideProgram", edProg->text().trimmed());
            settings->sync();
            mainWin->setMixerAutoHide(chOn->isChecked(), spDelay->value(), edProg->text().trimmed());
        };
        connect(chOn, &QCheckBox::toggled, this, [=](bool on) { spDelay->setEnabled(on); edProg->setEnabled(on); apply(); });
        connect(spDelay, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [=](double) { apply(); });
        connect(edProg, &QLineEdit::editingFinished, this, [=]() { apply(); });
        int at = vl2->count();
        if (at > 0 && vl2->itemAt(at - 1)->spacerItem())
            --at;                                   // keep the trailing spacer at the bottom
        vl2->insertWidget(at, ah);
    }

    // The device tab got taller (Audio device panel): put its content in a scroll area so the settings window can
    // never be taller than the screen (small / high-DPI laptops); a big screen still shows everything at once.
    if (QVBoxLayout *oldLay = qobject_cast<QVBoxLayout *>(ui->tab_4->layout()))
    {
        QWidget *inner = new QWidget;
        QVBoxLayout *nl = new QVBoxLayout(inner);
        int ml, mt, mr, mb;
        oldLay->getContentsMargins(&ml, &mt, &mr, &mb);
        nl->setContentsMargins(ml, mt, mr, mb);
        nl->setSpacing(oldLay->spacing());
        while (oldLay->count() > 0)
            nl->addItem(oldLay->takeAt(0));
        delete oldLay;
        // QLayout::addItem does not reparent: move every widget of the moved items into 'inner' (else they stay
        // children of the tab, hidden behind the scroll area -> blank page).
        {
            std::function<void(QLayout *)> reparent = [&](QLayout *l) {
                for (int i = 0; i < l->count(); ++i)
                {
                    QLayoutItem *it = l->itemAt(i);
                    if (QWidget *w = it->widget())
                    {
                        w->setParent(inner);
                        w->show();
                    }
                    else if (QLayout *sub = it->layout())
                        reparent(sub);
                }
            };
            reparent(nl);
        }

        FitScrollArea *sa = new FitScrollArea(ui->tab_4);
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sa->setWidget(inner);
        inner->show();
        QVBoxLayout *tl = new QVBoxLayout(ui->tab_4);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->addWidget(sa);
    }

    // Player edition: the karaoke-player tabs (playlist, lyrics, song database,
    // background) are used again, so all tabs stay.
}

void SettingsDialog::on_chbRemoveFromList_toggled(bool checked)
{
    settings->setValue("RemoveFromPlaylist", checked);
    mainWin->setRemoveFromPlaylist(checked);
}

void SettingsDialog::on_chbAutoPlaynext_toggled(bool checked)
{
    settings->setValue("AutoPlayNext", checked);
    mainWin->setAutoPlayNext(checked);
}

void SettingsDialog::onSpinSearchValueChanged(int i)
{
    settings->setValue("SearchTimeout", i);
    mainWin->setSearchTimeout(i);
}

void SettingsDialog::onSpinPlaylistValueChanged(int i)
{
    settings->setValue("PlaylistTimeout", i);
    mainWin->setPlaylistTimeout(i);
}

void SettingsDialog::onRadioBgColorToggled(bool checked)
{
    if (checked) {
        ui->lbBgColorN->setEnabled(true);
        ui->lbBgColor->setEnabled(true);
        ui->btnBgColor->setEnabled(true);

        ui->lbBgImgN->setEnabled(false);
        ui->btnBgImg->setEnabled(false);
        ui->leBgImgPath->setEnabled(false);

        settings->setValue("BackgroundType", Background::Color);
        mainWin->backgroundWidget()->setBackgroundType(Background::Color);
        if (mainWin->secondMonitorDlg() != nullptr)
            mainWin->secondMonitorDlg()->backgroundWidget()->setBackgroundType(Background::Color);
    }
}

void SettingsDialog::onRadioBgImgToggled(bool checked)
{
    if (checked) {
        ui->lbBgImgN->setEnabled(true);
        ui->btnBgImg->setEnabled(true);
        ui->leBgImgPath->setEnabled(true);

        ui->lbBgColorN->setEnabled(false);
        ui->lbBgColor->setEnabled(false);
        ui->btnBgColor->setEnabled(false);

        settings->setValue("BackgroundType", Background::Image);
        mainWin->backgroundWidget()->setBackgroundType(Background::Image);
        if (mainWin->secondMonitorDlg() != nullptr)
            mainWin->secondMonitorDlg()->backgroundWidget()->setBackgroundType(Background::Image);
    }
}

void SettingsDialog::on_btnBgColor_clicked()
{
    QColor lC(ui->lbBgColor->text());
    QColor color = QColorDialog::getColor(lC, this, "Select Color",
                                          QColorDialog::DontUseNativeDialog);
    if (color.isValid()) {
        settings->setValue("BackgroundColor", color.name());
        ui->lbBgColor->setText(color.name());
        ui->lbBgColor->setStyleSheet("background-color : " + color.name());
        mainWin->backgroundWidget()->setBackgroundColor(color);
        if (mainWin->secondMonitorDlg() != nullptr)
            mainWin->secondMonitorDlg()->backgroundWidget()->setBackgroundColor(color);
    }
}

void SettingsDialog::on_btnBgImg_clicked()
{
    //QStringList filters;
    //filters << "Image files (*.png *.PNG *.jpg *.JPG)";
    QString f = QFileDialog::getOpenFileName(this, "Select File", Utils::LAST_OPEN_DIR,
                                             "Image files (*.png *.PNG *.jpg *.JPG)");
    if (f != "") {
        settings->setValue("BackgroundImage", f);
        ui->leBgImgPath->setText(f);
        mainWin->backgroundWidget()->setBackgroundImage(f);
        if (mainWin->secondMonitorDlg() != nullptr)
            mainWin->secondMonitorDlg()->backgroundWidget()->setBackgroundImage(f);

        Utils::LAST_OPEN_DIR = QFileInfo(f).dir().absolutePath();//QDir().absoluteFilePath(f);
    }
}

void SettingsDialog::on_chbAutoCheckUpdate_toggled(bool checked)
{
    ui->lbUpdateInterval->setEnabled(checked);
    ui->spinUpdateInterval->setEnabled(checked);
    settings->setValue("Update/Auto", checked);
}

void SettingsDialog::onSpinUpdateValueChanged(int value)
{
    settings->setValue("Update/IntervalHours", value);
}

void SettingsDialog::on_btnNCNPath_clicked()
{
    QString path = QFileDialog::getExistingDirectory(this, tr("เลือกที่เก็บไฟล์เพลง NCN"), Utils::LAST_OPEN_DIR,
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (path == "")
        return;

    Utils::LAST_OPEN_DIR = path;

    if (db->isNCNPath(path)) {
        settings->setValue("NCNPath", path);
        ui->leNCNPath->setText(path);
        db->setNcnPath(path);
    } else {
        QString title = tr("ที่เก็บเพลงไม่ถูกต้อง");
        QString msg = tr("ต้องมีโฟลเดอร์ Cursor, Lyrics, และ Song อยู่ในที่เก็บเพลง");
        QMessageBox::information(this, title, msg, QMessageBox::Ok);
    }

}

void SettingsDialog::on_btnHNKPath_clicked()
{
    QString path = QFileDialog::getExistingDirectory(
                this, tr("เลือกที่เก็บไฟล์เพลง EMK"), Utils::LAST_OPEN_DIR,
                QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (path == "")
        return;

    Utils::LAST_OPEN_DIR = path;

    ui->leHNKPath->setText(path);
    db->setHNKPath(path);
    settings->setValue("HNKPath", path);
}

void SettingsDialog::on_btnKARPath_clicked()
{
    QString path = QFileDialog::getExistingDirectory(
                this, tr("เลือกที่เก็บไฟล์เพลง KAR"), Utils::LAST_OPEN_DIR,
                QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (path == "")
        return;

    Utils::LAST_OPEN_DIR = path;

    ui->leKARPath->setText(path);
    db->setKarPath(path);
    settings->setValue("KARPath", path);
}

void SettingsDialog::on_btnUpdateSongs_clicked()
{
    if (!db->isNCNPath(ui->leNCNPath->text())) {
        QString title = tr("ไม่สามารถปรับปรุงฐานข้อมูลได้");
        QString msg = tr("ไม่มีโฟลเดอร์ Cursor, Lyrics, หรือ Song อยู่ในที่เก็บเพลง NCN"
                         "\nโปรดเลือกตำแหน่งที่เก็บเพลงให้ถูกต้อง");
        QMessageBox::information(this, title, msg, QMessageBox::Ok);
        return;
    }

    settings->setValue("NCNPath", ui->leNCNPath->text());
    settings->setValue("HNKPath", ui->leHNKPath->text());

    ui->lbUpdateText->show();
    ui->lbUpdateValue->show();

    ui->btnUpdateSongs->setEnabled(false);
    ui->btnNCNPath->setEnabled(false);
    ui->btnHNKPath->setEnabled(false);
    ui->lbCountSongsText->setEnabled(false);
    ui->lbCountSongsValue->setEnabled(false);

    db->setUpdateType(UpdateType::UpdateAll);
    db->start();
}

void SettingsDialog::on_upDbUpdateFinished()
{ 
    ui->btnHNKPath->setEnabled(true);
    ui->btnNCNPath->setEnabled(true);
    ui->btnKARPath->setEnabled(true);
    ui->lbCountSongsText->setEnabled(true);
    ui->lbCountSongsValue->setEnabled(true);
    ui->lbUpdateText->hide();
    ui->lbUpdateValue->hide();
    ui->btnUpdateSongs->setEnabled(true);
    ui->lbCountSongsValue->setText(QString::number(db->count()) + " เพลง");
}

void SettingsDialog::on_btnMapChannel_clicked()
{
    MapChannelDialog d(this, mainWin->midiPlayer());
    d.setModal(true);
    d.adjustSize();
    d.setMinimumSize(d.size());
    d.exec();
}

void SettingsDialog::on_cbMidiOut_activated(int index)
{
    const int synthIndex = ui->cbMidiOut->count() - 1;
    const int port = (index == synthIndex) ? -1 : index;
    if (port == mainWin->midiPlayer()->midiOutPortNumber())
        return;     // same output re-selected: don't stop playback / reopen port

    mainWin->stop();
    mainWin->midiPlayer()->setMidiOut(port);

    // show what is really active (a busy/missing port keeps the previous output)
    const int cur = mainWin->midiPlayer()->midiOutPortNumber();
    ui->cbMidiOut->setCurrentIndex(cur == -1 ? synthIndex : cur);
    settings->setValue("MidiOut", cur);
    // remembered by device name, because port numbers shift (same as MIDI-in)
    settings->setValue("MidiOutName", cur == -1 ? QString() : ui->cbMidiOut->itemText(cur));

    QList<int> ports;
    for (int i=0; i<16; i++) {
        ports.append(mainWin->midiPlayer()->midiChannel()[i].port());
    }
    settings->setValue("MidiChannelMapper", QVariant::fromValue(ports));
}

void SettingsDialog::on_cbMidiIn_activated(int index)
{
    if (index == mainWin->midiPlayer()->midiInPortNumber() + 1) {
        // same device re-selected: nothing to do (don't stop playback / reopen port)
        settings->setValue("MidiInName", index > 0 ? ui->cbMidiIn->itemText(index) : QString());
        return;
    }
    // Changing the port while notes/program changes are streaming leaves the synth
    // with lost instrument assignments (everything turns to piano), so stop first.
    mainWin->stop();
    mainWin->midiPlayer()->setMidiIn(index-1);
    ui->cbMidiIn->setCurrentIndex(mainWin->midiPlayer()->midiInPortNumber() + 1);
    int cur = ui->cbMidiIn->currentIndex();
    settings->setValue("MidiIn", cur-1);
    settings->setValue("MidiInName", cur > 0 ? ui->cbMidiIn->itemText(cur) : QString());
}

void SettingsDialog::on_cbAudioOut_activated(int index)
{
    /*if (ui->cbAudioOut->currentText() == "Default") {
        BASS_SetDevice(-1);
        settings->setValue("AudioOut", -1);
        return;
    }*/
    MidiSynthesizer *synth = mainWin->midiPlayer()->midiSynthesizer();

    const int normalCount = MidiSynthesizer::audioDevices().count() - 1;

    if (index >= normalCount)
    {
        // ASIO driver selected
        const int asio = index - normalCount;
        if (synth->asioDevice() == asio)
            return;

        mainWin->stop();
        if (!synth->setAsioDevice(asio))
        {
            QString why = synth->asioError();
            QMessageBox::warning(this, tr("ASIO"),
                                 tr("Could not start this ASIO driver. The normal output device is used instead.")
                                 + (why.isEmpty() ? QString() : "\n" + why));
            // synth fell back to the normal device: show it in the list
            int sel = synth->defaultDevice() - 1;
            if (sel < 0 || sel >= normalCount) sel = 0;
            ui->cbAudioOut->setCurrentIndex(sel);
        }
        settings->setValue("AsioDevice", synth->asioDevice());
        return;
    }

    if (synth->asioDevice() < 0 && index+1 == synth->defaultDevice())
        return;     // same device re-selected: don't rebuild the synth

    // Stop playback first so changing the output device mid-play can't garble the sound.
    mainWin->stop();
    synth->setAsioDevice(-1);
    settings->setValue("AsioDevice", -1);
    synth->setDefaultDevice(index+1);
    settings->setValue("SynthDefaultDevice", index+1);
    /*if (BASS_SetDevice(index) == BASS_OK) {
        settings->setValue("defaultAudioDevice", index);
    } else {
        QString title = "ไม่สามรถใช้อุปกรณ์เสียงที่เลือกได้";
        QString msg = "โปรดเลือกอุปกรณ์เสียงขาออกตัวอื่น หรือลองใหม่ในภายหลัง";
        QMessageBox::information(this, title, msg, QMessageBox::Ok);
        BASS_SetDevice(-1);
        ui->cbAudioOut->setCurrentIndex( BASS_GetDevice() );
    }*/
}

void SettingsDialog::onChbLockDrumToggled(bool checked)
{
    if (checked) {
        ui->cbLockDrum->setEnabled(true);
        mainWin->midiPlayer()->setLockDrum(true, ui->cbLockDrum->currentIndex());
    }
    else {
        ui->cbLockDrum->setEnabled(false);
        mainWin->midiPlayer()->setLockDrum(false);
    }

    settings->setValue("MidiLockDrum", checked);
}

void SettingsDialog::onChbLockSnareToggled(bool checked)
{
    if (checked) {
        ui->cbLockSnare->setEnabled(true);
        const QList<int> sn = MidiHelper::snareNumbers();
        const int si = ui->cbLockSnare->currentIndex();
        mainWin->midiPlayer()->setLockSnare(true, (si >= 0 && si < sn.count()) ? sn[si] : 38);
    }
    else {
        ui->cbLockSnare->setEnabled(false);
        mainWin->midiPlayer()->setLockSnare(false);
    }

    settings->setValue("MidiLockSnare", checked);
}

void SettingsDialog::onChbLockKickToggled(bool checked)
{
    if (checked) {
        ui->cbLockKick->setEnabled(true);
        mainWin->midiPlayer()->setLockKick(true, ui->cbLockKick->currentIndex() == 0 ? 35 : 36);
    }
    else {
        ui->cbLockKick->setEnabled(false);
        mainWin->midiPlayer()->setLockKick(false);
    }

    settings->setValue("MidiLockKick", checked);
}

void SettingsDialog::onChbLockBassToggled(bool checked)
{
    if (checked) {
        ui->cbLockBass->setEnabled(true);
        mainWin->midiPlayer()->setLockBass(true, ui->cbLockBass->currentIndex() + 32);
    }
    else {
        ui->cbLockBass->setEnabled(false);
        mainWin->midiPlayer()->setLockBass(false);
    }

    settings->setValue("MidiLockBass", checked);
}

void SettingsDialog::on_cbLockDrum_activated(int index)
{
    mainWin->midiPlayer()->setLockDrum(true, index);
    settings->setValue("MidiLockDrumNumber", index);
}

void SettingsDialog::on_cbLockSnare_activated(int index)
{
    const QList<int> sn = MidiHelper::snareNumbers();
    const int n = (index >= 0 && index < sn.count()) ? sn[index] : 38;

    mainWin->midiPlayer()->setLockSnare(true, n);
    settings->setValue("MidiLockSnareNumber", n);
}

void SettingsDialog::on_cbLockKick_activated(int index)
{
    const int n = (index == 0) ? 35 : 36;
    mainWin->midiPlayer()->setLockKick(true, n);
    settings->setValue("MidiLockKickNumber", n);
}

void SettingsDialog::on_cbLockBass_activated(int index)
{
    mainWin->midiPlayer()->setLockBass(true, index + 32);
    settings->setValue("MidiLockBassNumber", index + 32);
}

void SettingsDialog::onChbFloatPointToggled(bool checked)
{
    if (mainWin->midiPlayer()->midiOutPortNumber() == -1)
        mainWin->stop();

    mainWin->midiPlayer()->midiSynthesizer()->setUsetFloattingPoint(checked);
    settings->setValue("SynthFloatPoint", checked);
}

void SettingsDialog::onChbUseFXToggled(bool checked)
{
    mainWin->midiPlayer()->midiSynthesizer()->setUseFXRC(checked);
    settings->setValue("SynthUseFXRC", checked);
}

void SettingsDialog::onSliderBufferValueChanged(int value)
{
    MidiSynthesizer *synth = mainWin->midiPlayer()->midiSynthesizer();

    if (!mainWin->midiPlayer()->isPlayerStopped())
        mainWin->stop();

    if (synth->isOpened()) {
        synth->close();
        BASS_SetConfig(BASS_CONFIG_BUFFER, value);
        synth->open();
    } else {
        BASS_SetConfig(BASS_CONFIG_BUFFER, value);
    }

    settings->setValue("SynthBuffer", value);
}

void SettingsDialog::on_btnFont_clicked()
{
    bool ok;
    QFont f = QFontDialog::getFont(&ok, mainWin->lyricsWidget()->textFont(),
                                   this, "Select Font", QFontDialog::DontUseNativeDialog);
    if (ok) {
        settings->setValue("LyricsFamily", f.family());
        settings->setValue("LyricsSize", f.pointSize());
        settings->setValue("LyricsWeight", f.weight());
        settings->setValue("LyricsItalic", f.italic());
        settings->setValue("LyricsUnderline", f.underline());
        settings->setValue("LyricsStrikeOut", f.strikeOut());

        setLabelFontInfo(&f);

        mainWin->lyricsWidget()->setTextFont(f);
        if (mainWin->secondLyrics() != nullptr)
            mainWin->secondLyrics()->setTextFont(f);
    }
}

void SettingsDialog::onSpinLine1YValueChanged(int v)
{
    settings->setValue("LyricsLine1Y", v);
    mainWin->lyricsWidget()->setLine1Y(v);
    if (mainWin->secondLyrics() != nullptr)
        mainWin->secondLyrics()->setLine1Y(v);
}

void SettingsDialog::onSpinLine2YValueChanged(int v)
{
    settings->setValue("LyricsLine2Y", v);
    mainWin->lyricsWidget()->setLine2Y(v);
    if (mainWin->secondLyrics() != nullptr)
        mainWin->secondLyrics()->setLine2Y(v);
}

void SettingsDialog::onSpinAnmtTimeValueChanged(int v)
{
    settings->setValue("LyricsAnimationTime", v);
    mainWin->lyricsWidget()->setAnimationTime(v);
    if (mainWin->secondLyrics() != nullptr)
        mainWin->secondLyrics()->setAnimationTime(v);
}

void SettingsDialog::onChbLyrAutoSizeToggled(bool checked)
{
    settings->setValue("LyricsAutoFontSize", checked);
    mainWin->lyricsWidget()->setAutoFontSize(checked);
    if (mainWin->secondLyrics() != nullptr)
        mainWin->secondLyrics()->setAutoFontSize(checked);
}

void SettingsDialog::on_cbLine1X_activated(int index)
{
    settings->setValue("LyricsLine1X", index);
    LinePosition p = static_cast<LinePosition>(index);
    mainWin->lyricsWidget()->setLine1Position(p);
    if (mainWin->secondLyrics() != nullptr)
        mainWin->secondLyrics()->setLine1Position(p);
}

void SettingsDialog::on_cbLine2X_activated(int index)
{
    settings->setValue("LyricsLine2X", index);
    LinePosition p = static_cast<LinePosition>(index);
    mainWin->lyricsWidget()->setLine2Position(p);
    if (mainWin->secondLyrics() != nullptr)
        mainWin->secondLyrics()->setLine2Position(p);
}

void SettingsDialog::on_btnTextColor_clicked()
{
    QColor lC = mainWin->lyricsWidget()->textColor();
    QColor color = QColorDialog::getColor(lC, this, "Select Color",
                                          QColorDialog::DontUseNativeDialog);

    if (color.isValid()) {
        QString cn = color.name();
        settings->setValue("LyricsTextColor", cn);
        ui->lbTextColor->setStyleSheet("background-color : " + cn);
        ui->lbTextColor->setText(cn);

        mainWin->lyricsWidget()->setTextColor(color);
        if (mainWin->secondLyrics() != nullptr)
            mainWin->secondLyrics()->setTextColor(color);
    }
}

void SettingsDialog::on_btnTextBorderColor_clicked()
{
    QColor lC = mainWin->lyricsWidget()->textBorderColor();
    QColor color = QColorDialog::getColor(lC, this, "Select Color",
                                          QColorDialog::DontUseNativeDialog);

    if (color.isValid()) {
        QString cn = color.name();
        settings->setValue("LyricsTextBorderColor", cn);
        ui->lbTextBorderColor->setStyleSheet("background-color : " + cn);
        ui->lbTextBorderColor->setText(cn);

        mainWin->lyricsWidget()->setTextBorderColor(color);
        if (mainWin->secondLyrics() != nullptr)
            mainWin->secondLyrics()->setTextBorderColor(color);
    }
}

void SettingsDialog::on_btnTextBorderOutColor_clicked()
{
    QColor lC = mainWin->lyricsWidget()->textBorderOutColor();
    QColor color = QColorDialog::getColor(lC, this, "Select Color",
                                          QColorDialog::DontUseNativeDialog);

    if (color.isValid()) {
        QString cn = color.name();
        settings->setValue("LyricsTextBorderOutColor", cn);
        ui->lbTextBorderOutColor->setStyleSheet("background-color : " + cn);
        ui->lbTextBorderOutColor->setText(cn);

        mainWin->lyricsWidget()->setTextBorderOutColor(color);
        if (mainWin->secondLyrics() != nullptr)
            mainWin->secondLyrics()->setTextBorderOutColor(color);
    }
}

void SettingsDialog::onSpinTextBorderWidthValueChanged(int arg1)
{
    settings->setValue("LyricsTextBorderWidth", arg1);
    mainWin->lyricsWidget()->setTextBorderWidth(arg1);
    if (mainWin->secondLyrics() != nullptr)
        mainWin->secondLyrics()->setTextBorderWidth(arg1);
}

void SettingsDialog::onSpinTextBorderOutWidthValueChanged(int arg1)
{
    settings->setValue("LyricsTextBorderOutWidth", arg1);
    mainWin->lyricsWidget()->setTextBorderOutWidth(arg1);
    if (mainWin->secondLyrics() != nullptr)
        mainWin->secondLyrics()->setTextBorderOutWidth(arg1);
}

void SettingsDialog::on_btnCurColor_clicked()
{
    QColor lC = mainWin->lyricsWidget()->curColor();
    QColor color = QColorDialog::getColor(lC, this, "Select Color",
                                          QColorDialog::DontUseNativeDialog);

    if (color.isValid()) {
        QString cn = color.name();
        settings->setValue("LyricsCurColor", cn);
        ui->lbCurColor->setStyleSheet("background-color : " + cn);
        ui->lbCurColor->setText(cn);

        mainWin->lyricsWidget()->setCurColor(color);
        if (mainWin->secondLyrics() != nullptr)
            mainWin->secondLyrics()->setCurColor(color);
    }
}

void SettingsDialog::on_btnCurBorderColor_clicked()
{
    QColor lC = mainWin->lyricsWidget()->curBorderColor();
    QColor color = QColorDialog::getColor(lC, this, "Select Color",
                                          QColorDialog::DontUseNativeDialog);

    if (color.isValid()) {
        QString cn = color.name();
        settings->setValue("LyricsCurBorderColor", cn);
        ui->lbCurBorderColor->setStyleSheet("background-color : " + cn);
        ui->lbCurBorderColor->setText(cn);

        mainWin->lyricsWidget()->setCurBorderColor(color);
        if (mainWin->secondLyrics() != nullptr)
            mainWin->secondLyrics()->setCurBorderColor(color);
    }
}

void SettingsDialog::on_btnCurBorderOutColor_clicked()
{
    QColor lC = mainWin->lyricsWidget()->curBorderOutColor();
    QColor color = QColorDialog::getColor(lC, this, "Select Color",
                                          QColorDialog::DontUseNativeDialog);

    if (color.isValid()) {
        QString cn = color.name();
        settings->setValue("LyricsCurBorderOutColor", cn);
        ui->lbCurBorderOutColor->setStyleSheet("background-color : " + cn);
        ui->lbCurBorderOutColor->setText(cn);

        mainWin->lyricsWidget()->setCurBorderOutColor(color);
        if (mainWin->secondLyrics() != nullptr)
            mainWin->secondLyrics()->setCurBorderOutColor(color);
    }
}

void SettingsDialog::onSpinCurBorderWidthValueChanged(int arg1)
{
    settings->setValue("LyricsCurBorderWidth", arg1);
    mainWin->lyricsWidget()->setCurBorderWidth(arg1);
    if (mainWin->secondLyrics() != nullptr)
        mainWin->secondLyrics()->setCurBorderWidth(arg1);
}

void SettingsDialog::onSpinCurBorderOutWidthValueChanged(int arg1)
{
    settings->setValue("LyricsCurBorderOutWidth", arg1);
    mainWin->lyricsWidget()->setCurBorderOutWidth(arg1);
    if (mainWin->secondLyrics() != nullptr)
        mainWin->secondLyrics()->setCurBorderOutWidth(arg1);
}

void SettingsDialog::on_btnSfAdd_clicked()
{
    QStringList sfFiles = QFileDialog::getOpenFileNames(this,
                                                        tr("Add Soundfont"),
                                                        Utils::LAST_OPEN_DIR,
                                                        "SoundFont (*.sf2 *.SF2 *.kmsf *.KMSF *.sfz *.SFZ)");

    if (sfFiles.count() > 0)
        Utils::LAST_OPEN_DIR = QFileInfo(sfFiles[0]).dir().absolutePath();

    MidiSynthesizer *synth = mainWin->midiPlayer()->midiSynthesizer();
    for (const QString &sf : sfFiles)
    {
        if (synth->addSoundfont(sf))
            ui->listsfFiles->addItem(sf);
        else
        {
            QString title = tr("ไฟล์ซาวด์ฟ้อนท์ไม่ถูกต้อง");
            QString msg = tr("ไม่สามารถอ่านไฟล์ ") + sf + tr(" ได้\nโปรดลองเลือกไฟล์อื่น");
            QMessageBox::warning(this, title, msg, QMessageBox::Ok);
        }
    }
    settings->setValue("SynthSoundfonts", QVariant::fromValue(synth->soundfontFiles()));

    for (int i = 0; i < SF_PRESET_COUNT; i++) {
        QString sfKey = "SynthSoundfontsMap";
        QString drKey = "SynthSoundfontsDrumMap";
        if (i > 0) {
            sfKey = sfKey + QString::number(i);
            drKey = drKey + QString::number(i);
        }
        settings->setValue(sfKey, QVariant::fromValue(synth->getMapSoundfontIndex(i)));
        settings->setValue(drKey, QVariant::fromValue(synth->getDrumMapSfIndex(i)));
    }
}

void SettingsDialog::on_btnSfRemove_clicked()
{
    int i = ui->listsfFiles->currentRow();
    if (i < 0)
        return;

    delete ui->listsfFiles->takeItem(i);

    MidiSynthesizer *synth = mainWin->midiPlayer()->midiSynthesizer();
    synth->removeSoundfont(i);

    settings->setValue("SynthSoundfonts", QVariant::fromValue(synth->soundfontFiles()));

    for (int i = 0; i < SF_PRESET_COUNT; i++) {
        QString sfKey = "SynthSoundfontsMap";
        QString drKey = "SynthSoundfontsDrumMap";
        if (i > 0) {
            sfKey = sfKey + QString::number(i);
            drKey = drKey + QString::number(i);
        }
        settings->setValue(sfKey, QVariant::fromValue(synth->getMapSoundfontIndex(i)));
        settings->setValue(drKey, QVariant::fromValue(synth->getDrumMapSfIndex(i)));
    }
}

void SettingsDialog::on_btnSfUp_clicked()
{
    int i = ui->listsfFiles->currentRow();
    if (i <= 0)
        return;

    QListWidgetItem *item = ui->listsfFiles->takeItem(i);
    ui->listsfFiles->insertItem(i-1, item);
    ui->listsfFiles->setCurrentRow(i-1);

    MidiSynthesizer *synth = mainWin->midiPlayer()->midiSynthesizer();
    synth->swapSoundfont(i, i-1);

    settings->setValue("SynthSoundfonts", QVariant::fromValue(synth->soundfontFiles()));

    for (int i = 0; i < SF_PRESET_COUNT; i++) {
        QString sfKey = "SynthSoundfontsMap";
        QString drKey = "SynthSoundfontsDrumMap";
        if (i > 0) {
            sfKey = sfKey + QString::number(i);
            drKey = drKey + QString::number(i);
        }
        settings->setValue(sfKey, QVariant::fromValue(synth->getMapSoundfontIndex(i)));
        settings->setValue(drKey, QVariant::fromValue(synth->getDrumMapSfIndex(i)));
    }
}

void SettingsDialog::on_btnSfDown_clicked()
{
    int i = ui->listsfFiles->currentRow();
    if (i == (ui->listsfFiles->count() - 1) )
        return;

    QListWidgetItem *item = ui->listsfFiles->takeItem(i);
    ui->listsfFiles->insertItem(i+1, item);
    ui->listsfFiles->setCurrentRow(i+1);

    MidiSynthesizer *synth = mainWin->midiPlayer()->midiSynthesizer();
    synth->swapSoundfont(i, i+1);

    settings->setValue("SynthSoundfonts", QVariant::fromValue(synth->soundfontFiles()));

    for (int i = 0; i < SF_PRESET_COUNT; i++) {
        QString sfKey = "SynthSoundfontsMap";
        QString drKey = "SynthSoundfontsDrumMap";
        if (i > 0) {
            sfKey = sfKey + QString::number(i);
            drKey = drKey + QString::number(i);
        }
        settings->setValue(sfKey, QVariant::fromValue(synth->getMapSoundfontIndex(i)));
        settings->setValue(drKey, QVariant::fromValue(synth->getDrumMapSfIndex(i)));
    }
}

void SettingsDialog::onSliderSfValueChanged(int value)
{
    int i = ui->listsfFiles->currentRow();
    if (i == -1)
        return;

    MidiSynthesizer *synth = mainWin->midiPlayer()->midiSynthesizer();
    synth->setSoundfontVolume(i, value / 100.0f);
    ui->lbSfVolume->setText(QString::number(value));
}

void SettingsDialog::onListSfCurrentRowChanged(int currentRow)
{
    if (currentRow == -1)
        return;

    disconnect(ui->sliderSfVolume, SIGNAL(valueChanged(int)),
               this, SLOT(onSliderSfValueChanged(int)));

    MidiSynthesizer *synth = mainWin->midiPlayer()->midiSynthesizer();
    int v = synth->soundfontVolume(currentRow) * 100;
    ui->sliderSfVolume->setValue(v);
    ui->lbSfVolume->setText(QString::number(v));

    connect(ui->sliderSfVolume, SIGNAL(valueChanged(int)),
            this, SLOT(onSliderSfValueChanged(int)));
}

void SettingsDialog::onChbSfLoadAllToggled(bool value)
{
    mainWin->midiPlayer()->midiSynthesizer()->setLoadAllSoundfont(value);
    settings->setValue("SynthSoundfontsLoadAll", value);
}

void SettingsDialog::on_btnSfMap_clicked()
{
    MapSoundfontDialog msfDlg(this, mainWin->midiPlayer()->midiSynthesizer());
    msfDlg.setModal(true);
    msfDlg.setMinimumSize(msfDlg.size());
    msfDlg.exec();
}

void SettingsDialog::on_btnEq_clicked()
{
    if (Equalizer31BandDialog::isOpenned())
        return;

    MidiSynthesizer *synth = mainWin->midiPlayer()->midiSynthesizer();

    Equalizer31BandDialog eqdlg(this, synth->equalizer31BandFXs());
    eqdlg.setModal(true);
    eqdlg.adjustSize();
    eqdlg.setFixedSize(eqdlg.size());
    eqdlg.exec();

    if (synth->equalizer31BandFXs()[0]->isOn())
        ui->btnEq->setIcon(QIcon(":/Icons/circle_green.png"));
    else
        ui->btnEq->setIcon(QIcon(":/Icons/circle_red.png"));
}

void SettingsDialog::on_btnChorus_clicked()
{
    if (Chorus2Dialog::isOpenned())
        return;

    MidiSynthesizer *synth = mainWin->midiPlayer()->midiSynthesizer();

    Chorus2Dialog crDlg(this, synth->chorusFXs());
    crDlg.setModal(true);
    crDlg.adjustSize();
    crDlg.setFixedSize(crDlg.size());
    crDlg.exec();

    if (synth->chorusFXs()[0]->isOn())
        ui->btnChorus->setIcon(QIcon(":/Icons/circle_green.png"));
    else
        ui->btnChorus->setIcon(QIcon(":/Icons/circle_red.png"));
}

void SettingsDialog::on_btnReverb_clicked()
{
    if (Reverb2Dialog::isOpenned())
        return;

    MidiSynthesizer *synth = mainWin->midiPlayer()->midiSynthesizer();

    Reverb2Dialog rvDlg(this, synth->reverbFXs());
    rvDlg.setModal(true);
    rvDlg.adjustSize();
    rvDlg.setFixedSize(rvDlg.size());
    rvDlg.exec();

    if (synth->reverbFXs()[0]->isOn())
        ui->btnReverb->setIcon(QIcon(":/Icons/circle_green.png"));
    else
        ui->btnReverb->setIcon(QIcon(":/Icons/circle_red.png"));
}

void SettingsDialog::on_btnClose_clicked()
{
    close();
}
