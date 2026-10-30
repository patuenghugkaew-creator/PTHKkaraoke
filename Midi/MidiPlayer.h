#ifndef MIDIPLAYER_H
#define MIDIPLAYER_H

#include "MidiOut.h"
#include "Channel.h"
#include "MidiSequencer.h"
#include "MidiSynthesizer.h"

#include <QObject>

enum class PlayerState
{
    Playing,
    Stopped,
    Paused,
    Finished
};

#include "Midi/FaderTaper.h"

class MidiPlayer : public QObject
{
    Q_OBJECT
public:
    explicit MidiPlayer(QObject *parent = nullptr);
    ~MidiPlayer();

    static QStringList midiDevices();
    static QStringList midiInDevices();
    static bool isSnareNumber(int num);
    static bool isKickNumber(int num);
    static bool isBassInstrument(int ints);

    MidiSequencer *midiSequencer() { return _midiSeq; }
    MidiSequencer *midiSequencerTemp() { return _midiSeqTemp; }
    MidiSynthesizer *midiSynthesizer() { return _midiSynth; }
    Channel *midiChannel() { return _midiChannels; }
    int midiOutPortNumber() { return _midiPortNum; }
    int midiInPortNumber() { return _midiPortInNum; }
    int volume() { return _volume; }
    // master mute: output gain forced to 0 while the level (_volume) is kept
    bool isMasterMute() { return _masterMute; }
    void setMasterMute(bool on) { _masterMute = on; setVolume(_volume); }
    float effectiveGain() { return _masterMute ? 0.0f : FaderTaper::gain(_volume); }
    // song fade (0..1) for the built-in synth only; MIDI out ports keep effectiveGain()
    void setFadeGain(float f);
    float synthGain() { return effectiveGain() * _fade; }
    int transpose() { return _midiTranspose; }

    int  lockDrumNumber()  { return _lockDrumNumber; }
    int  lockSnareNumber() { return _lockSnareNumber; }
    int  lockKickNumber()  { return _lockKickNumber; }
    int  lockBassNumber()  { return _lockBassBumber; }
    bool isLockDrum()   { return _lockDrum; }
    bool isLockSnare()  { return _lockSnare; }
    bool isLockKick()   { return _lockKick; }
    bool isLockBass()   { return _lockBass; }

    MidiFile *midiFile();
    MidiFile *midiFileTemp();

    bool isUsedMidiSynthesizer();

    bool isPlayerPlaying();
    bool isPlayerStopped();
    bool isPlayerPaused();
    bool isPlayerFinished();
    PlayerState playerState();

    long durationMs();
    long positionMs();
    int durationTick();
    int positionTick();
    int bpmSpeed();
    int currentBpm();
    int currentBeat();
    int beatCount();

    bool setMidiOut(int portNumber);
    bool setMidiIn(int portNumber);
    bool load(const QString &file, bool seekFileChunkID = false);
    void play();
    void stop(bool resetPos = false);
    void setVolume(int v);
    void setVolume(int ch, int v);
    void setInstrument(int ch, int i);
    void setMute(int ch, bool mute);
    void setSolo(int ch, bool solo);
    void setPan(int ch, int v);
    void setReverb(int ch, int v);
    void setChorus(int ch, int v);
    void setLockVolume(int ch, bool lock);

    void setPositionTick(int t);
    void setTranspose(int t);
    void setBpmSpeed(int sp);

    void setLockDrum(bool lock, int number = 0);
    void setLockSnare(bool lock, int number = 38);
    void setLockKick(bool lock, int number = 36);
    void setLockBass(bool lock, int number = 32);

    void setMapChannelOutput(int ch, int port);
    void receiveMidiIn(std::vector< unsigned char > *message);

    bool isUseMedley() { return _useMedley; }
    void setUseMedley(bool use);

    int medleyBPM() { return _medleyBPM; }
    void setMedleyBPM(int bpm);

    int medleyTransitionBars() const { return _medleyTransitionBars; }
    void setMedleyTransitionBars(int bars) { _medleyTransitionBars = bars; }

    int medleyFillPattern() const { return _medleyFillPattern; }
    void setMedleyFillPattern(int pattern) { _medleyFillPattern = pattern; }

    int medleyFillVolume() const { return _medleyFillVolume; }
    void setMedleyFillVolume(int vol) { _medleyFillVolume = vol; }

    int medleyTempoMode() const { return _medleyTempoMode; }
    void setMedleyTempoMode(int mode) { _medleyTempoMode = mode; }

    bool medleyAutoCutOutro() const { return _medleyAutoCutOutro; }
    void setMedleyAutoCutOutro(bool enable) { _medleyAutoCutOutro = enable; }

    int medleyWaitBars() const { return _medleyWaitBars; }
    void setMedleyWaitBars(int bars) { _medleyWaitBars = bars; }

    bool medleyQuantizeBar() const { return _medleyQuantizeBar; }
    void setMedleyQuantizeBar(bool enable) { _medleyQuantizeBar = enable; }

    bool medleySkipIntro() const { return _medleySkipIntro; }
    void setMedleySkipIntro(bool enable) { _medleySkipIntro = enable; }

    int medleyIntroLeadInBars() const { return _medleyIntroLeadInBars; }
    void setMedleyIntroLeadInBars(int bars) { _medleyIntroLeadInBars = qBound(0, bars, 8); }

    bool medleyPreserveEffectsTail() const { return _medleyPreserveEffectsTail; }
    void setMedleyPreserveEffectsTail(bool enable) { _medleyPreserveEffectsTail = enable; }

    int medleyPreBufferSec() const { return _medleyPreBufferSec; }
    void setMedleyPreBufferSec(int sec) { _medleyPreBufferSec = sec; }

    double medleyAudioCrossfadeSec() const { return _medleyAudioCrossfadeSec; }
    void setMedleyAudioCrossfadeSec(double sec) { _medleyAudioCrossfadeSec = sec; }

    int medleyAudioCurve() const { return _medleyAudioCurve; }
    void setMedleyAudioCurve(int curve) { _medleyAudioCurve = curve; }

    int medleyAudioThresholdDb() const { return _medleyAudioThresholdDb; }
    void setMedleyAudioThresholdDb(int db) { _medleyAudioThresholdDb = db; }

    bool medleyAudioAutoCue() const { return _medleyAudioAutoCue; }
    void setMedleyAudioAutoCue(bool enable) { _medleyAudioAutoCue = enable; }

    bool medleyAudioNormalize() const { return _medleyAudioNormalize; }
    void setMedleyAudioNormalize(bool enable) { _medleyAudioNormalize = enable; }

    int medleyPreset() const { return _medleyPreset; }
    void setMedleyPreset(int preset) { _medleyPreset = preset; }

    void loadMedleySettings();
    void saveMedleySettings();

    bool loadNextMedley(const QString &file, int cutStartBar, int cutEndBar, int midiSpeed, int transpose);
    void unloadNextMedley();

public slots:
    void sendEvent(MidiEvent e);
    void onSeqFinished();

signals:
    void loaded();
    void finished();
    void sendedEvent(MidiEvent e);
    void bpmChanged(int bpm);
    void nextMedleyStarted();
    void nextMedleyAfterStarted();
    void medleyCountIn(int beat, int beatsPerBar, int bar, int totalBars);

private slots:
    void onSeqBpmChanged(int bpm);
    void onSeqSongSwapped();

private:
    void sendEventToDevices(MidiEvent evt);
    void sendAllNotesOff(int ch);
    void sendAllNotesOff();
    void sendAllSoundOff(int ch);
    void sendAllSoundOff();
    void sendResetAllControllers(int ch);
    void sendResetAllControllers();

    int getNoteNumberToPlay(int ch, int defaultNote);
    void calculateUsedPort();

private:
    MidiSequencer *_midiSeq;
    MidiSequencer *_midiSeqTemp = nullptr;
    QMap<int, MidiOut*> _midiOuts;
    MidiSynthesizer     *_midiSynth;
    RtMidiIn            *_midiIn = nullptr;
    Channel             _midiChannels[16];
    int                 _midiPortNum = 0;
    int                 _midiPortInNum = -1;
    int                 _volume = 50;
    bool                _masterMute = false;
    float               _fade = 1.0f;
    int                 _midiTranspose = 0;
    int                 _midiTransposeTemp = 0;
    int                 _seqIndex = 0;
    int                 _medleyBPM = 120;
    int                 _medleyTransitionBars = 1;
    int                 _medleyFillPattern = 0;
    int                 _medleyFillVolume = 100;
    int                 _medleyTempoMode = 0;
    bool                _medleyAutoCutOutro = true;
    int                 _medleyWaitBars = 1;
    bool                _medleyQuantizeBar = true;
    bool                _medleySkipIntro = true;
    int                 _medleyIntroLeadInBars = 2;
    bool                _medleyPreserveEffectsTail = true;
    int                 _medleyPreBufferSec = 20;
    double              _medleyAudioCrossfadeSec = 3.0;
    int                 _medleyAudioCurve = 0;
    int                 _medleyAudioThresholdDb = -32;
    bool                _medleyAudioAutoCue = true;
    bool                _medleyAudioNormalize = false;
    int                 _medleyPreset = 0;
    bool                _useMedley = false;
    bool                _useSolo = false;

    QString _medleyId = "";

    MidiEvent   _tempEvent, _midiInEvent;
    MidiEvent*  _playingEventPtr = nullptr;

    bool    _lockDrum  = false;
    bool    _lockSnare = false;
    bool    _lockKick  = false;
    bool    _lockBass  = false;
    int     _lockDrumNumber  = 0;
    int     _songDrumProgram = 0;   // drum kit the song itself asked for (restored when the lock is turned off)
    int     _lockSnareNumber = 38;
    int     _lockKickNumber  = 36;
    int     _lockBassBumber  = 32;
};

void midiIncallback( double deltatime, std::vector< unsigned char > *message, void *userData );

#endif // MIDIPLAYER_H
