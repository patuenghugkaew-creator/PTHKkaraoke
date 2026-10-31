#ifndef MIDISEQUENCER_H
#define MIDISEQUENCER_H

#include <QThread>
#include <QElapsedTimer>
#include <QWaitCondition>
#include <QMutex>
#include <atomic>

#include "MidiFile.h"
#include "RhythmStyleManager.h"

class MidiSequencer : public QThread
{
    Q_OBJECT
public:
    explicit MidiSequencer(QObject *parent = nullptr);
    ~MidiSequencer();

    MidiFile* midiFile() { return _midi; }


    bool isSeqFinished() { return _finished; }
    bool isSeqPlaying() { return _playing; }
    bool isSeqStopped() { return _stopped; }
    bool isSeqPaused() { return (!_playing && !_stopped) ? true : false; }

    int bpmSpeed() { return _midiSpeed; }
    int currentBpm() { return _midiBpm + _midiSpeed; }

    int beatCount();
    int currentBeat();
    int positionTick();
    int durationTick();
    long positionMs();
    long durationMs();

    void setPositionTick(int t);
    void setBpmSpeed(int sp);


    bool load(const QString &file, bool seekFileChunkID = false);
    void stop(bool resetPos = false);

    void setStartTick(int tick);
    void setEndTick(int tick);

    void setCutStartBar(int bar);
    void setCutEndBar(int bar);

    int currentBar();
    uint32_t findVocalReleaseTick(long lastLyricTick) const;
    int startTick() const { return _startTick; }
    int endTick() const { return _endTick; }
    void applyAutoMedleyBounds(const QVector<long> &lyricCursors = QVector<long>(),
                               int manualStartBar = 0,
                               int manualEndBar = 0,
                               long lastSungLyricTick = -1,
                               bool skipIntro = true,
                               bool autoCutOutro = true,
                               int waitBars = 1,
                               bool quantizeBar = true,
                               int introLeadInBars = 2);

    bool isMedleyTransition() const { return _isMedleyTransition; }
    int medleyTransitionBars() const { return _medleyTransitionBars; }
    int medleyFillPattern() const { return _medleyFillPattern; }
    int medleyFillVolume() const { return _medleyFillVolume; }
    int medleyTempoMode() const { return _medleyTempoMode; }
    bool medleyPreserveEffectsTail() const { return _medleyPreserveEffectsTail; }
    void setMedleyTransition(bool enable, int bars = 1, int pattern = 0, int bpm = 120, int fillVolume = 100, int tempoMode = 0, bool preserveEffects = true);
    // Tempo of the song that is ending; the rhythm bridge glides from it to the new song's tempo.
    void setMedleyFromBpm(int bpm) { _medleyFromBpm = bpm; }

    // One shared clock for the whole medley chain (ms, monotonic).
    // The outgoing song reports the exact moment its last bar ended; the next song
    // anchors its bridge and first beat to that moment, so thread start-up time
    // never shifts the beat.
    static double clockMs();
    double idealEndClockMs() const { return _idealEndClockMs; }
    void setMedleyStartClockMs(double ms) { _medleyStartClockMs = ms; }

    // Tick of the last sounding note (Note-On); trailing silence after it is ignored.
    int lastNoteOnTick() const;
    // First bar at/after fromBar where the steady drum groove stops (ending hits,
    // breaks, held final chord). Returns barCount() if it never stops.
    int firstNonGrooveBar(int fromBar);

    // Many karaoke MIDI files have a set-up beat before the music, so the file's
    // bar lines are shifted from the real ones. This is the shift (ticks), found
    // from where the crash cymbals land (they mark beat 1). Cached.
    int downbeatOffset();
    // Bar start / bar number on the song's real (musical) bar grid.
    uint32_t barStartTick(int bar) { return _midi->tickFromBar(bar) + uint32_t(downbeatOffset()); }
    int barAtTick(uint32_t tick) { const uint32_t o = uint32_t(downbeatOffset()); return tick < o ? 0 : _midi->barFromTick(tick - o); }
    // Appends a line to medley-timing.log next to the exe.
    static void timingLog(const QString &line);

    // The steady drum groove of this song's last full bar before its cut-out
    // (ch 9, beats 0..4). Empty when the song has no clean, on-grid groove.
    QVector<RhythmBridgeEvent> extractOutgoingGroove();
    // Groove handed over from the previous song; the bridge keeps playing it.
    void setOutgoingGroove(const QVector<RhythmBridgeEvent> &g) { _songGroove = g; }
    // Set when a drum bridge follows this song: end with a beat-1 hit that rings 1 beat.
    void setEndHitHold(bool on) { _endHitHold = on; }

    // Drum hold ("ค้างจังหวะกลอง"): first call - at the next bar line the band stops and only
    // the song's own drum beat keeps going (lyrics wait too); second call - the next bar is a
    // drum fill and the song continues from where it stopped, on beat 1.
    void toggleDrumHold();
    bool isDrumHoldOn() const { return _holdRequested.load() || _holdActive.load(); }

public slots:

signals:
    void bpmChanged(int bpm);
    void playingEvent(MidiEvent e);
    void medleyCountIn(int beat, int beatsPerBar, int bar, int totalBars);
    // Emitted ~0.4 s before the cut-out point (shared clock ms), so the next
    // medley song can be started early and wait for that exact moment.
    void endApproaching(double endClockMs);
    // 1 = stops at the next bar, 2 = drums only, 3 = fill then the song, 0 = back to normal
    void drumHoldChanged(int state);

protected:
    void run();

private:
    int eventIndexFromTick(int tick);
    quint16 playMedleyTransitionFill(); // returns mask of channels the bridge used

private:
    MidiFile *_midi;
    QElapsedTimer *_eTimer;

    int     _midiBpm = 120;
    int     _midiSpeed = 0;
    int     _midiSpeedTemp = 0;
    bool    _midiChangeBpmSpeed = false;

    int     _positionTick = 0;
    long    _positionMs = 0;

    int     _playedIndex = 0;
    long    _startPlayTime = 0;
    long    _startPlayIndex = 0;
    bool    _finished = false;
    bool    _stopped = true;
    bool    _playing = false;

    int _startTick = 0;
    int _endTick = 0;
    int _endEventIndex = 0;
    MidiEvent *_endEvent = nullptr;

    bool _isMedleyTransition = false;
    int  _medleyTransitionBars = 1;
    int  _medleyFillPattern = 0;
    int  _medleyTargetBpm = 120;
    int  _medleyFromBpm = 0;
    double _idealEndClockMs = -1.0;
    double _medleyStartClockMs = -1.0;
    double _fillEndClockMs = -1.0;
    bool _endAnnounced = false;
    QVector<MidiEvent*> _warmEvents;  // new song's setup, sent during the bridge
    QVector<RhythmBridgeEvent> _bridgeEvents;  // built ahead of time in setMedleyTransition()
    bool _bridgeReady = false;
    QVector<RhythmBridgeEvent> _songGroove;
    int _downbeatOffset = -1;
    bool _endHitHold = false;
    std::atomic<bool> _holdRequested{false}, _releaseRequested{false}, _holdActive{false};
    int _holdTick = -1;
    void runDrumHold(int fromIndex);
    int _lastExpr[16] = { 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127 };
    // the band's "landing" when the music stops at tick `at`: its beat-1 notes, ring 1 beat,
    // fade over 1 beat, then silence (beats relative to that moment, melodic channels only)
    QVector<RhythmBridgeEvent> softStopEvents(int fromIndex, int at);
    int  _medleyFillVolume = 100;
    int  _medleyTempoMode = 0;
    bool _medleyPreserveEffectsTail = true;

    QWaitCondition _waitCondition;
    QMutex _mutex;
};

#endif // MIDISEQUENCER_H
