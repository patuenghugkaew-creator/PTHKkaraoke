#ifndef MIDISYNTHESIZER_H
#define MIDISYNTHESIZER_H

#include <QObject>
#include <QMap>
#include <QVector>
#include <QTimer>

#include <bass.h>
#include <bassmidi.h>
#include <bassmix.h>
#include <bass_fx.h>

#include "Midi/MidiHelper.h"
#include "Midi/MidiOut.h"
#include "BASSFX/FX.h"
#include "BASSFX/Equalizer31BandFX.h"
#include "BASSFX/Chorus2FX.h"
#include "BASSFX/Reverb2FX.h"
#include "PTHKCore.h"

#define SF_PRESET_COUNT 11

// Preset tab -> soundfont bank number: tab 0 (general) = 000, tabs 1..10 = 101..110.
// A soundfont that has no such bank simply falls back to bank 000.
inline int sfPresetBankNumber(int presetIndex) { return presetIndex <= 0 ? 0 : 100 + presetIndex; }

typedef struct
{
    DWORD handle;
    Equalizer31BandFX *eq;
    Chorus2FX *chorus;
    Reverb2FX *reverb;
} MixerHandle;

typedef struct
{
    InstrumentType type;
    bool mute;
    bool solo;
    bool enable;
    int volume;
    int pan;          // -100 (left) .. 0 (centre) .. +100 (right)
    int reverb;       // per-channel reverb send, 0..100
    double trimDb;    // fine level trim of the strip, -12..+12 dB (on top of the fader)
    int bus;
    int vsti;
    int device;
    SpeakerType speaker;
    QList<FX*> FXs;
} Instrument;

typedef struct
{
    unsigned int uniqueID;
    QString vstName;
    QString vstvendor;
    QString vstPath;
} VSTNamePath;

class MidiSynthesizer : public QObject
{
    Q_OBJECT

public:
    MidiSynthesizer(QObject *parent = nullptr);
    ~MidiSynthesizer();

    bool isOpened() { return openned; }
    // main output mixer (after the master effects); 0 while the synth is closed
    DWORD mainMixerHandle() { return mixers.isEmpty() ? 0 : mixers[0].handle; }

    // ASIO output (-1 = normal Windows device)
    bool setAsioDevice(int asioIndex);
    int  asioDevice() const { return asioDev; }
    bool setAsioDeviceKeepState(int asioIndex);   // same, but the channels' program / volume ... survive the rebuild
    QString asioError() const { return asioErr; }

    bool open();
    void close();

    int defaultDevice();
    bool setDefaultDevice(int dv);
    void setVolume(float vol);
    float volume() { return synth_volume; }
    bool spectrum(float *fft1024, int *rateHz = nullptr);   // master output FFT (for the RTA); false if nothing is playing yet

    // Ignore pitch/tuning commands sent to the drum channel (ch 10): GS drum
    // NRPN pitch, RPN fine/coarse tune and pitch bend. Some songs contain them
    // and make the snare sound tighter/higher, as if it had been re-tuned.
    void setIgnoreDrumPitch(bool ignore) { ignoreDrumPitch = ignore; }
    bool isIgnoreDrumPitch() const { return ignoreDrumPitch; }

    // Drum head tuning (semitones, -12..+12) per drum group, sent as GS drum-pitch NRPN
    // right before each hit, so it survives song resets.
    enum DrumTuneGroup { TuneKick = 0, TuneSnare, TuneTomHigh, TuneTomMid, TuneTomLow, TuneCowbell, DrumTuneGroupCount };
    void setDrumTune(int group, int semitones);
    int  drumTune(int group) const;

    // Force which key is played for the kick (35 or 36) and the snare (38 or 40); 0 = as the song says
    void setDrumNoteChoice(int kick, int snare);
    // Drum kit chosen by the "lock drum" button (-1 = off): the preset's own kit (128:bank) never replaces it
    void setLockedDrumKit(int program);
    int  kickNoteChoice() const { return kickNote; }
    int  snareNoteChoice() const { return snareNote; }

    QStringList soundfontFiles() { return sfFiles; }
    bool addSoundfont(const QString &sfFile);
    void removeSoundfont(int sfIndex);
    void swapSoundfont(int sfIndex, int toIndex);
    float soundfontVolume(int sfIndex);
    void setSoundfontVolume(int sfIndex, float sfvl);
    bool isLoadAllSoundfont() { return sfLoadAll; }
    void setLoadAllSoundfont(bool loadAll);

    // std::vector<int> size 129
    //      1-128 all intrument
    //      129 is drum
    bool setMapSoundfontIndex(int presetIndex, QList<int> intrumentSfIndex, QList<int> drumSfIndex);
    void setSoundfontPresets(int presetIndex);
    int soundfontPresets() { return sfPreset; }
    QList<int> getMapSoundfontIndex(int presetIndex) { return instmSf[presetIndex]; }
    QList<int> getDrumMapSfIndex(int presetIndex) { return drumSf[presetIndex]; }

    // Per-strip soundfont: use one particular soundfont for a single instrument strip (0..41), whatever the preset
    // map says. Stored by file path so it survives re-ordering / removing soundfonts. Empty path = follow the preset.
    void setStripSoundfont(InstrumentType t, const QString &sfPath, bool apply = true);
    QString stripSoundfont(InstrumentType t) const { return stripSf.value(static_cast<int>(t)); }
    QMap<int, QString> stripSoundfonts() const { return stripSf; }


    void sendNoteOff(int ch, int note, int velocity);
    void sendNoteOn(int ch, int note, int velocity);
    void sendNoteAftertouch(int ch, int note, int value);
    void sendController(int ch, int number, int value);
    void sendProgramChange(int ch, int number);
    void sendChannelAftertouch(int ch, int value);
    void sendPitchBend(int ch, int value);
    void sendAllNotesOff(int ch);
    void sendAllNotesOff();
    void sendResetAllControllers(int ch);
    void sendResetAllControllers();


    // Instrument Maper
    QMap<InstrumentType, Instrument> instrumentMap() { return instMap; }
    Instrument instrument(InstrumentType t) { return instMap.value(t); }
    int  device(InstrumentType t);
    int  busGroup(InstrumentType t);
    int  volume(InstrumentType t);
    int  pan(InstrumentType t);
    int  reverb(InstrumentType t);
    bool isMute(InstrumentType t);
    bool isSolo(InstrumentType t);
    int  useVSTi(InstrumentType t);
    SpeakerType speaker(InstrumentType t);
    void setDevice(InstrumentType t, int device);
    void setBusGroup(InstrumentType t, int group);
    void setVolume(InstrumentType t, int volume);
    void setPan(InstrumentType t, int pan);
    void setReverbStrong(bool strong);               // false = tight, true = stronger / longer tail
    bool isReverbStrong() const { return reverbStrong; }
    void setReverb(InstrumentType t, int level);   // 0 = dry, 100 = very wet
    double trim(InstrumentType t) { return instMap[t].trimDb; }
    void setTrim(InstrumentType t, double db);     // fine gain per strip, -12..+12 dB in 0.1 dB steps
    void setMute(InstrumentType t, bool m);
    void setSolo(InstrumentType t, bool s);
    void setUseVSTi(InstrumentType t, int vstiIndex);
    void setSpeaker(InstrumentType t, SpeakerType speaker);

    // External MIDI out per instrument (empty name = internal synth only).
    // When set, that instrument's notes are sent to the device instead of the SoundFont.
    void setExtMidiOut(InstrumentType t, const QString &deviceName);
    QString extMidiOut(InstrumentType t) { return extOutName.value(t); }


    static QStringList audioDevices();
    static void audioDevices(const QMap<int, QString> &devices);
    static bool isSoundFontFile(const QString &sfile);

    // Fx ----------------------
    QList<Equalizer31BandFX *> equalizer31BandFXs();
    QList<Reverb2FX *> reverbFXs();
    QList<Chorus2FX *> chorusFXs();

    QMap<uint, VSTNamePath> VSTList() { return _vstList; }
    void setVSTList(const QMap<uint, VSTNamePath> &listMap) { _vstList = listMap; }
    // ------------------------------------------


    bool isUseFloattingPoint() { return useFloat; }
    void setUsetFloattingPoint(bool use);

    // ---- audio quality (the higher settings are PRO options, the caller decides) ----
    int  sampleRateHz() const { return sampleRate; }
    void setSampleRate(int hz);                 // rebuilds the audio chain (playback must be stopped)
    bool isSincInterpolation() const { return sincInter; }
    void setSincInterpolation(bool on);         // applied to the running MIDI streams at once
    int  voices() const { return maxVoices; }
    void setVoices(int n);                      // applied to the running MIDI streams at once

    bool isUseFXRC() { return useFX; }
    void setUseFXRC(bool use);

    HSTREAM getChannelHandle(InstrumentType type);

    FX* addFX(InstrumentType type, DWORD uid);
    bool removeFX(InstrumentType type, int fxIndex);
    void setFXBypass(InstrumentType type, int fxIndex, bool state);
    QList<uint> fxUids(InstrumentType type);
    QList<bool> fxBypass(InstrumentType type);
    QList<int> fxProgram(InstrumentType type);
    QList<QByteArray> fxChunks(InstrumentType type);
    QList<QList<float>> fxParams(InstrumentType type);

    // Master effects: run on the main output mixer, after every channel / bus has been summed
    FX* addMasterFX(DWORD uid);
    bool removeMasterFX(int fxIndex);
    void setMasterFXBypass(int fxIndex, bool state);
    QList<FX*> masterFXList() const { return masterFXs; }
    QList<uint> masterFxUids();
    QList<bool> masterFxBypass();
    QList<int> masterFxProgram();
    QList<QByteArray> masterFxChunks();
    QList<QList<float>> masterFxParams();

    void setMasterParams(const MasterDsp::Params &p);
    MasterDsp::Params masterParams();
    MasterDsp::Meter masterMeter() const;

    #ifndef __linux__
    BASS_VST_INFO vstiInfo(int vstiIndex);
    QStringList vstiFiles();
    QString vstiFile(int vstiIndex);
    DWORD vstiHandle(int vstiIndex);
    int vstiProgram(int vstiIndex);
    QList<float> vstiParams(int vstiIndex);
    QByteArray vstiChunk(int vstiIndex);
    DWORD setVSTiFile(int vstiIndex, const QString &file);
    void removeVSTiFile(int vstiIndex);
    #endif

    const int HANDLE_STREAM_COUNT = 63;        // 46 MIDI/VSTi + 16 buses + the Canthai strip (62)
    const int HANDLE_CANTHAI = 62;
    const int HANDLE_MIDI_COUNT = 46;
    const int HANDLE_VSTI_START = 42;
    const int HANDLE_VSTI_COUNT = 4;
    const int HANDLE_BUS_COUNT = 16;
    const int HANDLE_BUS_START = 46;

public slots:
    void compactSoundfont();

signals:
    void noteOnSended(InstrumentType t, int bus, int ch, int note, int velocity);

private:
    DWORD createStream(InstrumentType t);

    void sendToAllMidiStream(int ch, DWORD eventType, DWORD param);
    void setSfToStream();
    void calculateEnable();
    HSTREAM getDrumHandleFromNote(int drumNote);

private:
    MidiOut *extPort(InstrumentType t);
    InstrumentType typeOf(int ch, int note);
    void extTeeAll(int status, int d1, int d2, bool oneData = false);
    QMap<InstrumentType, QString> extOutName;
    QMap<QString, MidiOut*> extPorts;

    QTimer timer;

    QList<MixerHandle> mixers;
    //MixerManager mixers;
    //HSTREAM mixHandle;
    QMap<InstrumentType, HSTREAM> handles;
    QList<HSOUNDFONT> synth_HSOUNDFONT;
    int sfPreset = 0;
    QStringList sfFiles;
    QMap<int, QString> stripSf;   // InstrumentType -> soundfont file chosen for that strip
    QList<QList<int>> instmSf;
    QList<QList<int>> drumSf;
    QMap<InstrumentType, Instrument> instMap;
    QMap<InstrumentType, Reverb2FX *> sendRv;       // per-channel reverb (free-verb on the channel stream)
    void attachSendReverb(InstrumentType t, DWORD stream);
    InstrumentType chInstType[16];
    int chProgram[16];          // last program change of every channel (to re-route when the preset changes)

    // Strip that plays a melodic program: normally the family of the program, but program 24 (index 23) goes to the
    // Canthai (khaen) strip when the soundfont preset is 101 / 108 / 110 (the MIDI's own bank select is not used)
    InstrumentType melodicTypeFor(int ch, int program) const;
    static bool isCanthaiBank(int bank) { return bank == 101 || bank == 108 || bank == 110; }
    // every MIDI stream that carries notes (melodic, drums, Canthai - not the VSTi / bus streams)
    bool isMidiStrip(int i) const { return i < HANDLE_MIDI_COUNT - 4 || i == HANDLE_CANTHAI; }

    #ifndef __linux__
    QString mVstiFiles[4];
    BASS_VST_INFO mVstiInfos[4];
    int mVstiTempProgram[4];
    QList<float> mVstiTempParams[4];
    QByteArray mVstiChunk[4];
    #endif

    // unique ID,  VSTNamePAth
    QMap<uint, VSTNamePath> _vstList;
    // ---------------------------

    float synth_volume = 1.0f;
    bool openned = false;
    bool ignoreDrumPitch = true;
    int  drumTuneSemis[DrumTuneGroupCount] = { 0, 0, 0, 0, 0, 0 };
    int  lockedKit = -1;
    int  noteTune[128] = { 0 };
    bool noteTuneDirty[128] = { false };
    int  kickNote = 0;
    int  snareNote = 0;
    int  remapDrumNote(int note) const;
    void applyDrumTune(int note, HSTREAM stream);
    int  dpMode = -1;   // -1 none, 1 NRPN, 2 RPN (last parameter selected on ch 10)
    int  dpMsb = 127;
    int  dpLsb = 127;
    bool useSolo = false;

    int defaultDev = 1;
    int asioDev = -1;
    QString asioErr;
    bool useFloat = true;
    int sampleRate = 44100;
    QVector<DWORD> snapshotChannels();
    void restoreChannels(const QVector<DWORD> &state);
    bool sincInter = true;
    bool reverbStrong = false;
    int maxVoices = 500;
    bool useFX = false;
    QList<FX*> masterFXs;
    int masterChannels = 2;
    HDSP masterDspHandle = 0;
    static void CALLBACK masterDspCallback(HDSP handle, DWORD channel, void *buffer, DWORD length, void *user);
    bool sfLoadAll = false;

    DWORD RPNType = 0;

    // device number, name
    static QMap<int, QString> outDevices;
};

#endif // MIDISYNTHESIZER_H
