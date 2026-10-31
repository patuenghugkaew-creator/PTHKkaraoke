#ifndef SYNTHMIXERDIALOG_H
#define SYNTHMIXERDIALOG_H

#include <QDialog>
#include <QMap>
#include <QSignalMapper>
#include <QButtonGroup>
#include <QTimer>

#include "Midi/MidiPlayer.h"
#include "Widgets/InstCh.h"

class QMenu;

class MainWindow;

namespace Ui {
class SynthMixerDialog;
}

class SynthMixerDialog : public QDialog
{
    Q_OBJECT

public:
    // audio songs: strips follow frequency bands (low..high, 0..127), MASTER the real level
    void showAudioBands(const int *bands, int count, int total);
    explicit SynthMixerDialog(QWidget *parent = 0, MainWindow *mainWin = 0);//, MainWindow *mainWin = 0);
    ~SynthMixerDialog();

    #ifndef __linux__
    void setVSTVendorMenu();
    #endif
    void setFXToSynth();
    void reloadFromSettings();     // apply the saved settings to the running mixer (no restart)
    InstCh* mixChannel(InstrumentType t);
    QMap<InstrumentType, InstCh *> mixChannelMap();
    QMap<InstrumentType, InstCh *> *mixChannelMapPtr();

    bool isStaysOnTop() const { return staysOnTop; }
    void setSkipSave(bool skip) { skipSave = skip; }
    // false: closing the mixer only hides it (the karaoke window owns the program)
    void setCloseQuitsApp(bool quits) { closeQuitsApp = quits; }

public slots:
    void syncQuickChecks();
    void settingValues();
    void setSoundfontPresets(int presets);

private slots:
    void showEqDialog();
    void showReverbDialog();
    void showChorusDialog();

    void setMute(InstrumentType t, bool m);
    void setSolo(InstrumentType t, bool s);
    void setMixLevel(InstrumentType t, int level);
    void resetMixLevel(InstrumentType t);
    void showPeakVU(InstrumentType t, int bus, int ch, int note, int velocity);

    void showChannelMenu(InstrumentType type, const QPoint &pos);
    void showBusMenu(InstrumentType type, const QPoint &globalPos);
    void setBusGroup(int group);
    FX* addFX(const QString &uidStr, bool bypass = false);
    void byPassFX(InstrumentType type, int fxIndex, bool bypass);
    void showFxDialog(InstrumentType type, int fxIndex);
    void showFXRemoveMenu(InstrumentType type, int fxIndex, const QPoint &pos);
    void showVSTiDialog(InstrumentType vstiIndexType);
    void removeFX();
    void showMasterFxMenu(int fxIndex, const QPoint &globalPos);
    void showSoundfontMenu(InstrumentType type, const QPoint &globalPos);
    FX* addMasterFX(uint uid, bool bypass = false);
    void openFxDialog(FX *fx, const QString &ownerName);

    void on_btnMenu_clicked();
    void showBusDlg();
    void showSpeakersDlg();
    void showVSTDirsDlg();
    void showVuDlg();
    void resetChannel();
    void setStaysOnTop(bool stay);

    void changeSoundfontPresets(int presets);

protected:
    void showEvent(QShowEvent *);
    void fitToContent();
    void syncWindowDrop(int drop);
    void setKnobsVisible(bool show, bool animate = true);   // fold / unfold the Pan + Reverb knobs
    void applyKnobProgress(double p);
    class QVariantAnimation *knobAnim = nullptr;
    double knobProgress = 0.0;    // 0 = knobs shown, 1 = folded away
    bool foldAtShow = false;
    int winDrop = 0;              // pixels the window is currently shortened by
    int knobDrop = 0;             // pixels taken away from the strips so far
    void alignMasterMute();
    void hideEvent(QHideEvent *event);
    void setStripsHidden(bool hide);   // master-only view (HIDE button on the master strip)
    bool stripsHidden = false;
    bool event(QEvent *e) override;      // keeps the master frame on the strip line whatever else resizes the window
    void scheduleAlign();
    bool alignPending = false;
    int alignTries = 0;
    qint64 alignStamp = 0;
    QTimer *alignWatch = nullptr;   // slow watchdog: re-aligns when something moved the master frame off the strip line
    int watchFails = 0;
    void logLayout(const QString &why);
    int masterWantHeight() const;   // height the master frame must have to end on the strip line (-1: unknown)
    QRect hideSavedGeo;
    QRect hideNarrowGeo;   // the window right after it was made narrow: moving it meanwhile moves the restore too
    int hideSavedMinW = 0;
    bool fittedOnce = false;   // first show fits the window; auto-hide returns do not
    void closeEvent(QCloseEvent *event);
    void paintEvent(QPaintEvent *event);
    void changeEvent(QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void reject();

private:
    Ui::SynthMixerDialog *ui;

    QTimer settingTimer;
    MainWindow *mainWin;
    MidiPlayer *player;
    MidiSynthesizer *synth;
    InstrumentType currentType;

    QMap<InstrumentType, InstCh*> chInstMap;

    QList<QMenu*> vstVendorMenus;
    QSignalMapper signalVstActionMapper;
    QSignalMapper signalBusActionMapper;
    QSignalMapper *signalBFXActionMapper = nullptr;

    QButtonGroup btnPresets;

    int currentFxIndexToRemove;
    void warnVstNotLoaded(const QString &name);
    bool restoringFx = false;   // true while saved effects are loaded (no warning boxes then)

    // A saved VST effect whose plug-in was not in the VST list at start-up (scan skipped / failed / not ready) is kept
    // here and written back when the settings are saved, so its settings are not lost. It is loaded as soon as the
    // plug-in shows up in the list (restoreOrphanFx, called after a VST scan).
    struct OrphanFx {
        uint uid = 0;
        bool bypass = false;
        int program = 0;
        QList<float> params;
        QByteArray chunk;
    };
    QMap<int, QList<OrphanFx>> orphanFx;     // strip (InstrumentType as int) -> effects
    QList<OrphanFx> orphanMaster;            // master strip effects
    void restoreOrphanFx();
    void applySavedFx(FX *fx, const OrphanFx &o);
    bool addToMaster = false;   // FX menus add to the master strip instead of currentType

    bool staysOnTop = false;
    bool closeQuitsApp = true;
    bool skipSave = false;
    class TitleBar *titleBar = nullptr;
    Qt::Edges edgesAt(const QPoint &pos) const;
    void updateMenuIcon();
    class MasterStrip *masterStrip = nullptr;
    class QToolButton *chkLockDrum = nullptr;
    class QToolButton *chkLockSnare = nullptr;
    class QToolButton *chkLockBass = nullptr;
    class QToolButton *chkLockKick = nullptr;
    class QToolButton *chkNoTune = nullptr;
    class QToolButton *btnDrumTune = nullptr;
    void showDrumTunePopup();
    void showLockMenu(class QToolButton *btn, int kind);   // kind: 0 drum kit, 1 snare, 2 bass

    void mapChInstUI();
    void buildSidePanels();
    void showStripPage(int page);
    void setChInstDetails();
    void createBusActions(InstrumentType t, QMenu *busMenu);
};

#endif // SYNTHMIXERDIALOG_H
