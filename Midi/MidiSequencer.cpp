#include "MidiSequencer.h"
#include "MedleyDrumFills.h"
#include "RhythmStyleManager.h"
#include <QRandomGenerator>
#include <QMap>
#include <QHash>
#include <QStringList>
#include <QDebug>
#include <QFile>
#include <QDateTime>
#include <QCoreApplication>
#include <QMutexLocker>
#include <chrono>
#include <algorithm>

// Developer timing logs: only when "debug-logs.txt" is next to the exe.
static bool timingLogsOn()
{
    static const int on = QFile::exists(QCoreApplication::applicationDirPath() + "/debug-logs.txt") ? 1 : 0;
    return on == 1;
}

// Writes medley timing measurements next to the exe (medley-timing.log).
static void medleyLog(const QString &line)
{
    if (!timingLogsOn())
        return;
    static QMutex m;
    QMutexLocker lock(&m);
    QFile f(QCoreApplication::applicationDirPath() + "/medley-timing.log");
    if (f.open(QIODevice::Append | QIODevice::Text))
        f.write((QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ") + line + QChar('\n')).toUtf8());
}

double MidiSequencer::clockMs()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void MidiSequencer::timingLog(const QString &line)
{
    medleyLog(line);
}

QVector<RhythmBridgeEvent> MidiSequencer::extractOutgoingGroove()
{
    QVector<RhythmBridgeEvent> out;
    const QVector<MidiEvent*> &ev = _midi->events();
    if (ev.isEmpty())
        return out;
    const int res = qMax(1, _midi->resorution());
    const int cutTick = (_endTick > 0) ? _endTick : lastNoteOnTick();
    const int cutBar = barAtTick(uint32_t(qMax(0, cutTick)));

    // A hit is "on the grid" when it sits on a 16th or a triplet subdivision.
    const int tol = qMax(1, res / 24);
    auto onGrid = [&](int rel) {
        const int s16 = res / 4, s12 = res / 3, s24 = res / 6;
        auto near = [&](int step) { if (step <= 0) return false; const int m = rel % step; return m <= tol || step - m <= tol; };
        return near(s16) || near(s12) || near(s24);
    };

    // The song's main beat = the most frequent steady drum bar pattern (latest
    // occurrence before the cut-out). Ending hits or one-off fills are ignored.
    QHash<QString, int> patternCount;
    QHash<QString, int> patternLastBar;
    for (int bar = 0; bar < cutBar; ++bar) {
        const int b0 = int(barStartTick(bar));
        const int b1 = int(barStartTick(bar + 1));
        if (b1 - b0 != res * 4)
            continue;   // only plain 4/4 bars
        int hits = 0, grid = 0;
        bool kick = false, backbeat = false;
        QStringList sig;
        for (MidiEvent *e : ev) {
            if (int(e->tick()) < b0) continue;
            if (int(e->tick()) >= b1) break;
            if (e->channel() != 9 || e->eventType() != MidiEventType::NoteOn || e->data2() == 0)
                continue;
            ++hits;
            const int rel = int(e->tick()) - b0;
            if (onGrid(rel)) ++grid;
            const int n = e->data1();
            if (n == 35 || n == 36) kick = true;
            if (n == 38 || n == 40 || n == 37 || n == 39 || n == 42 || n == 44 || n == 46 || n == 51) backbeat = true;
            sig << QString("%1:%2").arg((rel * 8 + res / 2) / res).arg(n);   // 32nd-note position + note
        }
        if (hits < 6 || !kick || !backbeat || grid * 100 < hits * 90)
            continue;
        sig.sort();
        const QString key = sig.join(',');
        patternCount[key] += 1;
        patternLastBar[key] = bar;
    }

    int bestBar = -1, bestCount = 0;
    for (auto it = patternCount.constBegin(); it != patternCount.constEnd(); ++it) {
        const int lastBar = patternLastBar.value(it.key());
        if (it.value() > bestCount || (it.value() == bestCount && lastBar > bestBar)) {
            bestCount = it.value();
            bestBar = lastBar;
        }
    }

    if (bestBar >= 0) {
        const int b0 = int(barStartTick(bestBar));
        const int b1 = int(barStartTick(bestBar + 1));
        for (MidiEvent *e : ev) {
            if (int(e->tick()) < b0) continue;
            if (int(e->tick()) >= b1) break;
            if (e->channel() != 9 || e->eventType() != MidiEventType::NoteOn || e->data2() == 0)
                continue;
            const double beat = double(int(e->tick()) - b0) / res;
            out.append({beat, MidiEventType::NoteOn, 9, e->data1(), e->data2()});
            out.append({qMin(3.99, beat + 0.2), MidiEventType::NoteOff, 9, e->data1(), 0});
        }
        std::sort(out.begin(), out.end(), [](const RhythmBridgeEvent &a, const RhythmBridgeEvent &b) {
            return a.beatOffset < b.beatOffset;
        });
        medleyLog(QString("bridge groove: song's main beat, bar %1 (pattern used in %2 bars)").arg(bestBar).arg(bestCount));
        return out;
    }
    medleyLog("bridge groove: ending song has no steady drum bar - using rhythm file");
    return out;
}

int MidiSequencer::downbeatOffset()
{
    if (_downbeatOffset >= 0)
        return _downbeatOffset;
    _downbeatOffset = 0;
    const int res = qMax(1, _midi->resorution());
    const int barTicks = res * 4;
    int crash[4] = {0, 0, 0, 0};
    for (MidiEvent *e : _midi->events()) {
        if (e->channel() != 9 || e->eventType() != MidiEventType::NoteOn || e->data2() == 0)
            continue;
        const int n = e->data1();
        if (n != 49 && n != 57 && n != 55 && n != 52)
            continue;
        const int ph = int(e->tick()) % barTicks;
        const int inBeat = ph % res;
        if (inBeat > res / 8 && res - inBeat > res / 8)
            continue;   // not on a beat
        crash[((ph + res / 2) / res) % 4]++;
    }
    int best = 0;
    for (int b = 1; b < 4; ++b)
        if (crash[b] > crash[best]) best = b;
    // Only shift when the crashes clearly agree on another beat.
    if (best != 0 && crash[best] >= 8 && crash[best] >= 2 * crash[0] + 2)
        _downbeatOffset = best * res;
    medleyLog(QString("downbeat: crashes per beat %1/%2/%3/%4 -> beat 1 is shifted by %5 beat(s)")
              .arg(crash[0]).arg(crash[1]).arg(crash[2]).arg(crash[3]).arg(_downbeatOffset / res));
    return _downbeatOffset;
}

int MidiSequencer::firstNonGrooveBar(int fromBar)
{
    const int total = _midi->barCount();
    const QVector<MidiEvent*> &ev = _midi->events();
    for (int bar = qMax(0, fromBar); bar < total; ++bar) {
        const uint32_t b0 = barStartTick(bar), b1 = barStartTick(bar + 1);
        int hits = 0;
        bool kick = false;
        for (MidiEvent *e : ev) {
            if (e->tick() < b0) continue;
            if (e->tick() >= b1) break;
            if (e->channel() == 9 && e->eventType() == MidiEventType::NoteOn && e->data2() > 0) {
                ++hits;
                if (e->data1() == 35 || e->data1() == 36) kick = true;
            }
        }
        if (hits < 4 || !kick)
            return bar;
    }
    return total;
}

QVector<RhythmBridgeEvent> MidiSequencer::softStopEvents(int fromIndex, int at)
{
    QVector<RhythmBridgeEvent> ev;
    const int window = qMax(1, _midi->resorution() / 8);
    // the notes the band plays on that beat 1 ("lands" together)
    for (int k = fromIndex; k < _midi->events().count(); ++k) {
        MidiEvent *e = _midi->events()[k];
        if (int(e->tick()) >= at + window)
            break;
        if (e == _endEvent || e->channel() == 9)
            continue;
        if (e->eventType() == MidiEventType::NoteOn && e->data2() > 0)
            ev.append({ 0.0, MidiEventType::NoteOn, e->channel(), e->data1(), e->data2() });
    }
    // ring for a beat, then fade the expression to nothing over the next beat
    const int steps = 8;
    for (int st = 1; st <= steps; ++st) {
        const double beat = 1.0 + double(st) / steps;
        for (int ch = 0; ch < 16; ++ch) {
            if (ch == 9) continue;
            ev.append({ beat, MidiEventType::Controller, ch, 11, int(_lastExpr[ch] * (1.0 - double(st) / steps)) });
        }
    }
    // then silence, and the expression back for when the music returns
    for (int ch = 0; ch < 16; ++ch) {
        if (ch == 9) continue;
        ev.append({ 2.01, MidiEventType::Controller, ch, 64, 0 });
        ev.append({ 2.01, MidiEventType::Controller, ch, 123, 0 });
        ev.append({ 2.02, MidiEventType::Controller, ch, 11, _lastExpr[ch] });
    }
    return ev;
}

void MidiSequencer::toggleDrumHold()
{
    if (_holdActive.load()) {
        _releaseRequested.store(true);
        _waitCondition.wakeAll();
    } else if (_holdRequested.load()) {
        _holdRequested.store(false);        // pressed twice before it stopped: never mind
        emit drumHoldChanged(0);
    } else if (_playing) {
        _holdTick = -1;
        _releaseRequested.store(false);
        _holdRequested.store(true);
    }
}

// Runs in the play thread when the song reaches _holdTick (a bar line). Plays the song's main
// drum beat bar after bar until released, then one fill bar, and returns at the moment the song
// should go on (beat 1 after the fill), so the caller resumes at _holdTick right on time.
void MidiSequencer::runDrumHold(int fromIndex)
{
    const double holdAt = clockMs();
    _holdActive.store(true);
    _holdRequested.store(false);
    emit drumHoldChanged(2);

    auto sendCC = [this](int ch, int cc, int val) {
        MidiEvent e;
        e.setEventType(MidiEventType::Controller);
        e.setChannel(ch);
        e.setData1(cc);
        e.setData2(val);
        emit playingEvent(e);
    };
    Q_UNUSED(sendCC);
    // the band lands on beat 1, rings, fades out (no chop); the drums carry on
    const QVector<RhythmBridgeEvent> landing = softStopEvents(fromIndex, _holdTick);

    QVector<RhythmBridgeEvent> groove = extractOutgoingGroove();
    if (groove.isEmpty()) {                 // no steady beat in the song: a plain one
        for (int b = 0; b < 4; ++b) {
            const int kickSnare = (b % 2 == 0) ? 36 : 38;
            groove.append({ double(b), MidiEventType::NoteOn, 9, kickSnare, b % 2 == 0 ? 110 : 100 });
            groove.append({ b + 0.2, MidiEventType::NoteOff, 9, kickSnare, 0 });
            groove.append({ double(b), MidiEventType::NoteOn, 9, 42, 85 });
            groove.append({ b + 0.2, MidiEventType::NoteOff, 9, 42, 0 });
            groove.append({ b + 0.5, MidiEventType::NoteOn, 9, 42, 70 });
            groove.append({ b + 0.7, MidiEventType::NoteOff, 9, 42, 0 });
        }
    }
    // the fill bar: the beat on 1-2, snare 16ths on 3, toms high -> low on 4
    QVector<RhythmBridgeEvent> fill;
    for (const RhythmBridgeEvent &g : groove)
        if (g.beatOffset < 2.0)
            fill.append(g);
    auto hit = [&fill](double beat, int note, int vel) {
        fill.append({ beat, MidiEventType::NoteOn, 9, note, vel });
        fill.append({ beat + 0.2, MidiEventType::NoteOff, 9, note, 0 });
    };
    hit(2.0, 36, 105);
    hit(2.0, 38, 92); hit(2.25, 38, 96); hit(2.5, 38, 102); hit(2.75, 38, 108);
    hit(3.0, 36, 110);
    hit(3.0, 50, 112); hit(3.25, 48, 114); hit(3.5, 45, 118); hit(3.75, 43, 122);
    auto byTime = [](const RhythmBridgeEvent &a, const RhythmBridgeEvent &b) { return a.beatOffset < b.beatOffset; };
    std::stable_sort(groove.begin(), groove.end(), byTime);
    std::stable_sort(fill.begin(), fill.end(), byTime);
    QVector<RhythmBridgeEvent> firstBar = groove + landing;     // beat 0..2: the band's tail
    std::stable_sort(firstBar.begin(), firstBar.end(), byTime);

    const double beatMs = 60000.0 / qMax(30, currentBpm());
    const double barMs = 4.0 * beatMs;
    int bar = 0;
    int fillBar = -1;
    while (_playing) {
        const bool isFill = (bar == fillBar);
        const QVector<RhythmBridgeEvent> &pat = isFill ? fill : (bar == 0 ? firstBar : groove);
        const double barStart = holdAt + bar * barMs;
        int k = 0;
        while (_playing) {
            const double now = clockMs();
            if (fillBar < 0 && _releaseRequested.load()) {
                fillBar = bar + 1;          // the next bar is the fill
                emit drumHoldChanged(3);
            }
            while (k < pat.size() && barStart + pat[k].beatOffset * beatMs <= now) {
                MidiEvent e;
                e.setEventType(pat[k].eventType);
                e.setChannel(pat[k].channel);
                e.setData1(pat[k].data1);
                e.setData2(pat[k].data2);
                emit playingEvent(e);
                ++k;
            }
            if (now >= barStart + barMs)
                break;
            double next = barStart + barMs;
            if (k < pat.size())
                next = qMin(next, barStart + pat[k].beatOffset * beatMs);
            const double sleepMs = next - clockMs();
            if (sleepMs > 1.5) {
                _mutex.lock();
                _waitCondition.wait(&_mutex, qBound(1, int(sleepMs - 0.5), 20));
                _mutex.unlock();
            }
        }
        if (isFill)
            break;
        ++bar;
    }

    // beat 1 of the song again: crash + kick on top of the song's own first notes
    if (_playing) {
        MidiEvent crash;
        crash.setEventType(MidiEventType::NoteOn);
        crash.setChannel(9);
        crash.setData1(49);
        crash.setData2(115);
        emit playingEvent(crash);
        crash.setData1(36);
        crash.setData2(118);
        emit playingEvent(crash);
    }

    // carry on at _holdTick exactly one fill bar later than the last groove bar
    const double resumeAt = holdAt + (bar + 1) * barMs;
    _mutex.lock();
    _eTimer->restart();
    _startPlayTime = long(_midi->timeFromTick(uint32_t(_holdTick), _midiSpeed) * 1000)
                     - long(qMax(0.0, clockMs() - resumeAt) + 0.5);
    _positionTick = _holdTick;
    _mutex.unlock();
    _releaseRequested.store(false);
    _holdActive.store(false);
    _holdTick = -1;
    emit drumHoldChanged(0);
}

int MidiSequencer::lastNoteOnTick() const
{
    const QVector<MidiEvent*> &ev = _midi->events();
    for (int i = ev.size() - 1; i >= 0; --i)
        if (ev[i]->eventType() == MidiEventType::NoteOn && ev[i]->data2() > 0)
            return int(ev[i]->tick());
    return 0;
}

// Max lateness we treat as start-up delay and absorb by snapping to the shared clock.
static const double kMaxAnchorLateMs = 250.0;

MidiSequencer::MidiSequencer(QObject *parent) : QThread(parent)
{
    _midi = new MidiFile();
    _eTimer = new QElapsedTimer();
}

MidiSequencer::~MidiSequencer()
{
    stop();

    delete _eTimer;
    delete _midi;
}

int MidiSequencer::beatCount()
{
    if (_midi->events().count() == 0) {
        return 0;
    } else {
        return _midi->beatFromTick(durationTick());
    }
}

int MidiSequencer::currentBeat()
{
    return _midi->beatFromTick(positionTick());
}

int MidiSequencer::positionTick()
{
    if (_isMedleyTransition || _holdActive.load() || !_eTimer || !_eTimer->isValid()) {
        return _positionTick;
    }
    if (_playing) {
        return _midi->tickFromTimeMs(_eTimer->elapsed()  + _startPlayTime, _midiSpeed);
    } else {
        return _positionTick;
    }
}

int MidiSequencer::durationTick()
{
    return _midi->events().back()->tick();
}

long MidiSequencer::positionMs()
{
    if (_isMedleyTransition || _holdActive.load() || !_eTimer || !_eTimer->isValid()) {
        return _positionMs;
    }
    return _playing ? _midi->timeFromTick(positionTick()) * 1000 : _positionMs;
}

long MidiSequencer::durationMs()
{
    return _midi->timeFromTick(durationTick()) * 1000;
}

void MidiSequencer::setPositionTick(int t)
{
    bool playAfterSeek = _playing;
    if (_playing)
        stop();

    uint32_t tick = 0;
    if (t < _startTick)
        tick = _startTick;
    else if ((_endTick > 0) && (t > _endTick))
        tick = _endTick;
    else if (t > durationTick())
        tick = durationTick();
    else
        tick = t;

    _mutex.lock();

    _playedIndex = eventIndexFromTick(tick);
    _startPlayTime = _midi->timeFromTick(tick, _midiSpeed) * 1000;
    _positionMs = _startPlayTime;
    _positionTick = tick;

    int chProg[16];
    int chVol[16];
    int chPan[16];
    int chRev[16];
    int chCho[16];
    int chExp[16];
    for (int ch = 0; ch < 16; ch++) {
        chProg[ch] = -1;
        chVol[ch]  = -1;
        chPan[ch]  = -1;
        chRev[ch]  = -1;
        chCho[ch]  = -1;
        chExp[ch]  = -1;
    }
    int lastBpm = -1;
    for (int i = 0; i < _playedIndex && i < _midi->events().size(); i++) {
        MidiEvent *e = _midi->events()[i];
        if (e->eventType() == MidiEventType::ProgramChange) {
            int c = e->channel();
            if (c >= 0 && c < 16) chProg[c] = e->data1();
        } else if (e->eventType() == MidiEventType::Controller) {
            int c = e->channel();
            if (c >= 0 && c < 16) {
                switch (e->data1()) {
                case 7:  chVol[c] = e->data2(); break;
                case 10: chPan[c] = e->data2(); break;
                case 11: chExp[c] = e->data2(); break;
                case 91: chRev[c] = e->data2(); break;
                case 93: chCho[c] = e->data2(); break;
                default: break;
                }
            }
        } else if (e->eventType() == MidiEventType::Meta && e->metaEventType() == MidiMetaType::SetTempo) {
            if (!(_midi->isSingleTempo() && (e != _midi->tempoEvents()[0])))
                lastBpm = e->bpm();
        }
    }
    if (lastBpm > 0)
        _midiBpm = lastBpm;
    _mutex.unlock();

    if (lastBpm > 0)
        emit bpmChanged(_midiBpm + _midiSpeed);

    for (int ch = 0; ch < 16; ch++) {
        if (chProg[ch] >= 0) {
            MidiEvent ev;
            ev.setEventType(MidiEventType::ProgramChange);
            ev.setChannel(ch);
            ev.setData1(chProg[ch]);
            emit playingEvent(ev);
        }
        auto sendCc = [this, ch](int cc, int val) {
            if (val < 0) return;
            MidiEvent ev;
            ev.setEventType(MidiEventType::Controller);
            ev.setChannel(ch);
            ev.setData1(cc);
            ev.setData2(val);
            emit playingEvent(ev);
        };
        sendCc(7,  chVol[ch]);
        sendCc(10, chPan[ch]);
        sendCc(11, chExp[ch]);
        sendCc(91, chRev[ch]);
        sendCc(93, chCho[ch]);
    }
    _mutex.lock();

    _mutex.unlock();

    if (playAfterSeek)
        start();
}

void MidiSequencer::setBpmSpeed(int sp)
{
    if (sp == _midiSpeed)
        return;

    if ((_midiBpm + sp) < 20 || (_midiBpm + sp) > 250)
        return;

    _mutex.lock();

    if (_playing) {
        _midiSpeedTemp = sp;
        _midiChangeBpmSpeed = true;
    } else {
        _midiSpeed = sp;
    }

    _mutex.unlock();

    emit bpmChanged(_midiBpm + sp);
}

bool MidiSequencer::load(const QString &file, bool seekFileChunkID)
{
    if (!_stopped)
        stop();

    if (!_midi->read(file, seekFileChunkID))
        return false;

    _midiSpeed = 0;
    _midiSpeedTemp = 0;
    _midiChangeBpmSpeed = false;
    _endTick = _midi->events().last()->tick();

    _finished = false;

    if (_midi->tempoEvents().count() > 0) {
        _midiBpm = _midi->tempoEvents()[0]->bpm();
    } else {
        _midiBpm = 120;
    }

    return true;
}

void MidiSequencer::stop(bool resetPos)
{
    if (_stopped)
        return;

    _mutex.lock();
    _playing = false;
    _stopped = false;
    _startPlayIndex = _playedIndex;
    _startPlayTime = positionMs();

    _waitCondition.wakeAll();
    _mutex.unlock();

    wait();

    if (resetPos) {
        _mutex.lock();
        if (false) _stopped = true;
        _stopped = true;
        _startPlayTime = _midi->timeFromTick(_startTick, _midiSpeed) * 1000;;
        _startPlayIndex = eventIndexFromTick(_startTick);
        _playedIndex = _startPlayIndex;
        _positionMs = _startPlayTime;
        _positionTick = _startTick;
        _mutex.unlock();
    }
}

void MidiSequencer::setStartTick(int tick)
{
    _startTick = tick;

    if (_stopped) {
        _mutex.lock();
        _startPlayTime = _midi->timeFromTick(tick, _midiSpeed) * 1000;
        _startPlayIndex = eventIndexFromTick(tick);
        _playedIndex = _startPlayIndex;
        _positionMs = _startPlayTime;
        _positionTick = tick;
        _mutex.unlock();
    }
}

void MidiSequencer::setEndTick(int tick)
{
    if (_midi->events().isEmpty()) return;
    if (tick == 0) {
        _endTick = 0;
        if (_endEvent != nullptr) {
            _midi->takeEvent(_endEventIndex);
            delete _endEvent;
            _endEvent = nullptr;
            _endEventIndex = 0;
        }
    } else if (tick > _startTick) {
        _endTick = tick;
        if (_endEvent == nullptr) {
            _endEvent = new MidiEvent();
        } else {
            _midi->takeEvent(_endEventIndex);
        }
        _endEvent->setTick(tick);

        MidiEvent *halfEvent = _midi->events()[ _midi->events().size() / 2 ];

        int index;

        if (tick >= halfEvent->tick()) {
            index = _midi->events().size() - 1;
            for (int i = index; i >= 0; i--) {
                index = i;
                if (_midi->events()[i]->tick() < tick)
                    break;
            }
        } else {
            index = 0;
            for (int i = index; i < _midi->events().size(); i++) {
                index = i;
                if (_midi->events()[i]->tick() >= tick)
                    break;
            }
        }

        _endEventIndex = index;
        _midi->insertEvent(_endEventIndex, _endEvent);
    }
}

void MidiSequencer::setCutStartBar(int bar)
{
    setStartTick(_midi->tickFromBar(bar));
}

void MidiSequencer::setCutEndBar(int bar)
{
    if (bar <= 0) {
        setEndTick(0); // 0 = do not cut end (play to natural end of song)
        return;
    }

    int totalBars = _midi->barCount();
    int endbar = (totalBars + 1) - bar;
    if (endbar < 1)
        endbar = 1;
    if (endbar > totalBars)
        endbar = totalBars;

    setEndTick(_midi->tickFromBar(endbar));
}

int MidiSequencer::currentBar()
{
    return _midi->barFromTick(positionTick());
}

uint32_t MidiSequencer::findVocalReleaseTick(long lastLyricTick) const
{
    if (_midi == nullptr || _midi->events().isEmpty() || lastLyricTick <= 0)
        return uint32_t(qMax(0L, lastLyricTick));

    const int res = qMax(1, _midi->resorution());
    const int barTicks = res * 4;
    // Vocal phrase must sustain at least to the end of the current bar + 1 downbeat
    uint32_t minEndTick = _midi->tickFromBar(_midi->barFromTick(uint32_t(lastLyricTick)) + 1);
    uint32_t maxReleaseTick = minEndTick;

    // Scan for any melody/guide notes active or triggered around the final lyric on non-drum channels (ch != 9)
    const long windowStart = qMax(0L, lastLyricTick - res);
    const long windowEnd = lastLyricTick + res * 2;

    for (int i = 0; i < _midi->events().size(); ++i) {
        MidiEvent *e = _midi->events()[i];
        if (e->tick() < uint32_t(windowStart))
            continue;
        if (e->tick() > uint32_t(windowEnd + barTicks * 2))
            break;

        if (e->channel() != 9 && e->eventType() == MidiEventType::NoteOn && e->data2() > 0 && e->tick() <= uint32_t(windowEnd)) {
            const int note = e->data1();
            const int ch = e->channel();
            for (int j = i + 1; j < _midi->events().size(); ++j) {
                MidiEvent *nextE = _midi->events()[j];
                if (nextE->channel() == ch && nextE->data1() == note) {
                    if (nextE->eventType() == MidiEventType::NoteOff || (nextE->eventType() == MidiEventType::NoteOn && nextE->data2() == 0)) {
                        if (nextE->tick() > maxReleaseTick) {
                            maxReleaseTick = nextE->tick();
                        }
                        break;
                    }
                }
            }
        }
    }

    return maxReleaseTick;
}

void MidiSequencer::applyAutoMedleyBounds(const QVector<long> &lyricCursors,
                                         int manualStartBar,
                                         int manualEndBar,
                                         long lastSungLyricTick,
                                         bool skipIntro,
                                         bool autoCutOutro,
                                         int waitBars,
                                         bool quantizeBar,
                                         int introLeadInBars)
{
    if (_midi->events().isEmpty())
        return;

    const int res = qMax(1, _midi->resorution());
    const int totalBars = _midi->barCount();

    // 1. Auto-detect first active musical bar (Cue-In)
    // If skipIntro is enabled: jump directly to introLeadInBars (e.g. 1-2 bars) before the first lyric
    // so the singer starts singing right after the transition without waiting through a long intro!
    // Also intelligently detects "เกริ่น" (Lam-Kern / Recitative Intro): if singing begins several bars
    // before the main rhythm/groove starts, skip the rubato recitative and lock directly onto the main dance groove!
    if (manualStartBar > 0) {
        setCutStartBar(manualStartBar);
    } else if (!skipIntro) {
        setCutStartBar(0);
    } else {
        // Lyric cursors are only trusted when they fall inside the song.
        // Instrumental tracks often ship a dummy .cur (e.g. just the title text),
        // which decodes to positions far past the end and used to push the start
        // to the last bars of the song.
        const long songEndTick = long(_midi->events().last()->tick());
        long firstLyricTick = -1;
        for (long c : lyricCursors) {
            if (c > 0) {
                firstLyricTick = c;
                break;
            }
        }
        if (firstLyricTick >= songEndTick * 9 / 10) {
            medleyLog(QString("next song: lyric cursor %1 is outside the song (end %2) - treated as instrumental")
                      .arg(firstLyricTick).arg(songEndTick));
            firstLyricTick = -1;
        }

        // Detect where the continuous drum groove (Kick & Snare) actually starts
        int firstGrooveBar = -1;
        QMap<int, int> barDrumHits; // bar -> count of kick/snare hits
        for (MidiEvent *e : _midi->events()) {
            if (e->eventType() == MidiEventType::NoteOn && e->data2() > 0 && e->channel() == 9) {
                const int n = e->data1();
                if (n == 35 || n == 36 || n == 38 || n == 40 || n == 37) {
                    const int b = _midi->barFromTick(e->tick());
                    barDrumHits[b]++;
                }
            }
        }

        for (auto it = barDrumHits.begin(); it != barDrumHits.end(); ++it) {
            const int bar = it.key();
            const int hits = it.value();
            if (hits >= 2) {
                if (barDrumHits.value(bar + 1, 0) >= 2 || barDrumHits.value(bar + 2, 0) >= 2) {
                    firstGrooveBar = bar;
                    break;
                }
            }
        }

        // Also check if there's a tempo jump (e.g., slow recitative intro -> fast dance groove)
        int tempoJumpBar = -1;
        if (_midi->tempoEvents().size() > 1) {
            const int baseBpm = _midi->tempoEvents()[0]->bpm();
            for (int i = 1; i < _midi->tempoEvents().size(); ++i) {
                MidiEvent *te = _midi->tempoEvents()[i];
                if (te->bpm() >= int(baseBpm * 1.25) && te->bpm() >= 90) {
                    tempoJumpBar = _midi->barFromTick(te->tick());
                    break;
                }
            }
        }

        int targetGrooveBar = firstGrooveBar;
        if (tempoJumpBar > 0) {
            if (targetGrooveBar < 0 || (tempoJumpBar > 0 && tempoJumpBar <= targetGrooveBar))
                targetGrooveBar = tempoJumpBar;
        }

        int startBar = 0;
        const int leadIn = qBound(0, introLeadInBars, 8);

        if (firstLyricTick > 0) {
            const int firstVocalBar = barAtTick(uint32_t(firstLyricTick));
            bool isRecitative = false;

            // If the first vocal starts 3+ bars before the main drum groove begins,
            // or if there is a distinct tempo jump into the fast groove:
            if ((targetGrooveBar > firstVocalBar + 2) || (tempoJumpBar > 0 && tempoJumpBar > firstVocalBar)) {
                isRecitative = true;
            }

            if (isRecitative && targetGrooveBar >= 0) {
                // "ตัดเกริ่น" (Auto Recitative Cut): skip the slow rubato intro and land right on Beat 1 of the main dance groove!
                startBar = targetGrooveBar;
            } else {
                // Like eXtreme's "Start: Auto" (ตัดเกริ่น): the bridge already works as
                // the intro, so enter right at the singer's bar. If the voice comes in
                // during the first half of that bar, start one bar earlier so the
                // singer hears the band for at least 2 beats.
                const int vocalBarTick = int(barStartTick(firstVocalBar));
                const double beatInBar = double(firstLyricTick - vocalBarTick) / res;
                startBar = (beatInBar >= 2.0) ? firstVocalBar : firstVocalBar - 1;
                startBar = qMax(0, startBar);
                Q_UNUSED(leadIn);
            }
        } else {
            // Instrumental fallback: skip leading silence to first band/drum groove
            int firstMusicTick = 0;
            int firstBandTick = -1;
            int firstGrooveDrumTick = -1;
            for (MidiEvent *e : _midi->events()) {
                if (e->eventType() == MidiEventType::NoteOn && e->data2() > 0) {
                    if (e->channel() != 9) {
                        if (firstBandTick < 0) firstBandTick = int(e->tick());
                    } else {
                        const int n = e->data1();
                        if (n == 35 || n == 36 || n == 38 || n == 40) {
                            if (firstGrooveDrumTick < 0) firstGrooveDrumTick = int(e->tick());
                        }
                    }
                    if (firstBandTick >= 0 && firstGrooveDrumTick >= 0)
                        break;
                }
            }
            if (firstBandTick >= 0 && firstGrooveDrumTick >= 0)
                firstMusicTick = qMax(firstBandTick, firstGrooveDrumTick);
            else if (firstBandTick >= 0)
                firstMusicTick = firstBandTick;
            else if (firstGrooveDrumTick >= 0)
                firstMusicTick = firstGrooveDrumTick;

            startBar = barAtTick(uint32_t(qMax(0, firstMusicTick)));
        }

        startBar = qBound(0, startBar, qMax(0, totalBars - 4));
        setStartTick(int(barStartTick(startBar)));
        medleyLog(QString("song start: bar %1 of %2 (first sung word %3)")
                  .arg(startBar).arg(totalBars)
                  .arg(firstLyricTick > 0 ? QString("bar %1").arg(barAtTick(uint32_t(firstLyricTick))) : QString("none")));
    }

    // 2. Outro bar boundary (Cue-Out)
    if (manualEndBar > 0) {
        setCutEndBar(manualEndBar);
    } else {
        // In automated Medley playback, dynamic outro cutting is handled by MainWindow::checkEndAfterLyrics
        // as soon as the singer actually reaches and completes the final lyric.
        // Set endTick to 0 (no static cut) so song does not cut prematurely at bar 1!
        setEndTick(0);
    }
}

void MidiSequencer::setMedleyTransition(bool enable, int bars, int pattern, int bpm, int fillVolume, int tempoMode, bool preserveEffects)
{
    _isMedleyTransition = enable;
    _medleyTransitionBars = qBound(0, bars, 8);
    _medleyFillPattern = qBound(0, pattern, 7);
    _medleyTargetBpm = qBound(0, bpm, 300);
    _medleyFillVolume = qBound(50, fillVolume, 150);
    _medleyTempoMode = qBound(0, tempoMode, 2);
    _medleyPreserveEffectsTail = preserveEffects;

    // Load/parse the rhythm file now (while the current song is still playing),
    // so the bridge can start exactly on time.
    _bridgeReady = false;
    _bridgeEvents.clear();
    if (enable && _medleyTransitionBars > 0) {
        const int rhythmId = RhythmStyleManager::instance().patternIndexToRhythmId(_medleyFillPattern);
        RhythmStyleManager::instance().buildTransitionBridge(rhythmId, qBound(1, _medleyTransitionBars, 8),
                                                             _medleyFillVolume / 100.0, _bridgeEvents);
        _bridgeReady = true;
    }
}

quint16 MidiSequencer::playMedleyTransitionFill()
{
    if (!_playing || _medleyTransitionBars <= 0)
        return 0;

    const int totalBars = qBound(1, _medleyTransitionBars, 8);
    const int beatsPerBar = 4;
    const int totalBeats = totalBars * beatsPerBar;
    const int songBpm = currentBpm() > 0 ? currentBpm() : 120;
    const int targetBpm = qBound(30, _medleyTargetBpm > 0 ? _medleyTargetBpm : songBpm, 300);
    // Start at the outgoing song's tempo and glide to the new song's tempo,
    // so the groove keeps the beat the singer was already feeling.
    const int fromBpm = qBound(30, _medleyFromBpm > 0 ? _medleyFromBpm : targetBpm, 300);

    const double volScale = _medleyFillVolume / 100.0;
    const int rhythmId = RhythmStyleManager::instance().patternIndexToRhythmId(_medleyFillPattern);

    QVector<RhythmBridgeEvent> bridgeEvents;
    if (_bridgeReady)
        bridgeEvents = _bridgeEvents;
    else
        RhythmStyleManager::instance().buildTransitionBridge(rhythmId, totalBars, volScale, bridgeEvents);

    // Preferred: keep playing the ending song's own groove, then a fill that counts
    // straight into beat 1 of the next song (like a live drummer waiting for the band).
    if (!_songGroove.isEmpty()) {
        bridgeEvents.clear();
        auto add = [&](double beat, MidiEventType t, int note, int vel) {
            bridgeEvents.append({beat, t, 9, note,
                                 t == MidiEventType::NoteOn ? qBound(1, int(vel * volScale), 127) : 0});
        };
        auto hit = [&](double beat, int note, int vel) {
            add(beat, MidiEventType::NoteOn, note, vel);
            add(beat + 0.2, MidiEventType::NoteOff, note, 0);
        };
        const int fillBar = totalBars - 1;
        for (int b = 0; b < totalBars; ++b) {
            for (const RhythmBridgeEvent &g : _songGroove) {
                // Last bar: groove only on beats 1-2, then the fill takes over.
                if (b == fillBar && g.beatOffset >= 2.0)
                    continue;
                add(b * 4.0 + g.beatOffset, g.eventType, g.data1, g.data2);
            }
        }
        // Crash on beat 1 together with the song's ending hit.
        bool grooveHasCrash = false;
        for (const RhythmBridgeEvent &g : _songGroove)
            if (g.beatOffset < 0.05 && (g.data1 == 49 || g.data1 == 57)) grooveHasCrash = true;
        if (!grooveHasCrash)
            hit(0.0, 49, 110);
        // Fill on beats 3-4: snare 16ths building up, then toms high -> low.
        const double f = fillBar * 4.0;
        hit(f + 2.0, 36, 105);
        hit(f + 2.0, 38, 92);  hit(f + 2.25, 38, 96); hit(f + 2.5, 38, 102); hit(f + 2.75, 38, 108);
        hit(f + 3.0, 36, 110);
        hit(f + 3.0, 50, 112); hit(f + 3.25, 48, 114); hit(f + 3.5, 45, 118); hit(f + 3.75, 43, 122);
        std::stable_sort(bridgeEvents.begin(), bridgeEvents.end(), [](const RhythmBridgeEvent &x, const RhythmBridgeEvent &y) {
            return x.beatOffset < y.beatOffset;
        });
    }

    quint16 usedChannels = 1u << 9;
    for (const RhythmBridgeEvent &re : bridgeEvents)
        usedChannels |= quint16(1u << (re.channel & 0x0F));

    // Ensure Channel 9 (Drums) is audible and unmuted
    auto sendCC = [this](int ch, int cc, int val) {
        MidiEvent e;
        e.setEventType(MidiEventType::Controller);
        e.setChannel(ch);
        e.setData1(cc);
        e.setData2(val);
        emit playingEvent(e);
    };
    if (_songGroove.isEmpty()) {
        // Rhythm-file bridge: make sure its kit is audible.
        // (With the song's own groove we keep the song's drum level, so nothing jumps.)
        sendCC(9, 7, 127);   // Main Volume
        sendCC(9, 11, 127);  // Expression
        sendCC(9, 10, 64);   // Pan center
    }

    // Half/double-time neighbours (e.g. 145 -> 72) share the same pulse: keep the
    // bridge steady instead of gliding, so beat 1 of the new song lands naturally.
    const double ratio = double(targetBpm) / double(fromBpm);
    const bool samePulse = qAbs(ratio - 0.5) < 0.03 || qAbs(ratio - 2.0) < 0.12;
    const int rampTo = samePulse ? fromBpm : targetBpm;
    auto bpmAtBeat = [&](double beat) {
        const double p = qBound(0.0, beat / double(totalBeats), 1.0);
        return fromBpm + (rampTo - fromBpm) * p;
    };

    if (fromBpm != targetBpm)
        emit bpmChanged(fromBpm);

    // Beat 1 of the bridge = the exact moment the previous song's last bar ended.
    double baseClock = clockMs();
    if (_medleyStartClockMs > 0) {
        const double late = baseClock - _medleyStartClockMs;
        if (late >= 0 && late < kMaxAnchorLateMs)
            baseClock = _medleyStartClockMs;
        medleyLog(QString("bridge start late by %1 ms%2").arg(late, 0, 'f', 2)
                  .arg((late >= 0 && late < kMaxAnchorLateMs) ? " (snapped to grid)" : " (not snapped)"));
    }

    double beatPos = 0.0;       // integrated so tempo changes never make the groove jump
    double lastMs = 0.0;
    int nextHitIdx = 0;
    int lastEmittedBeat = -1;
    int lastEmittedBpm = fromBpm;
    double hitLateMax = 0.0, hitLateSum = 0.0;
    int hitCount = 0;

    while (_playing) {
        const double nowMs = clockMs() - baseClock;
        const double bpmNow = bpmAtBeat(beatPos);
        beatPos += (nowMs - lastMs) * bpmNow / 60000.0;
        lastMs = nowMs;
        if (beatPos >= totalBeats) {
            // Where the bridge ideally ended, so the next song's beat 1 lands on the grid.
            _fillEndClockMs = baseClock + nowMs - (beatPos - totalBeats) * 60000.0 / bpmNow;
            break;
        }

        const int roundedBpm = int(bpmNow + 0.5);
        if (roundedBpm != lastEmittedBpm) {
            lastEmittedBpm = roundedBpm;
            emit bpmChanged(roundedBpm);
        }

        const int currentBeatNum = int(beatPos);
        if (currentBeatNum != lastEmittedBeat) {
            lastEmittedBeat = currentBeatNum;
            int barIndex = (currentBeatNum / beatsPerBar) + 1;
            int beatInBar = (currentBeatNum % beatsPerBar) + 1;
            emit medleyCountIn(beatInBar, beatsPerBar, barIndex, totalBars);
        }

        while (nextHitIdx < bridgeEvents.size() && bridgeEvents[nextHitIdx].beatOffset <= beatPos) {
            const RhythmBridgeEvent &re = bridgeEvents[nextHitIdx];
            if (re.eventType == MidiEventType::NoteOn && re.data2 > 0) {
                const double lateMs = (beatPos - re.beatOffset) * 60000.0 / bpmNow;
                hitLateMax = qMax(hitLateMax, lateMs);
                hitLateSum += lateMs;
                ++hitCount;
            }
            MidiEvent evt;
            evt.setEventType(re.eventType);
            evt.setChannel(re.channel);
            evt.setData1(re.data1);
            evt.setData2(re.data2);
            emit playingEvent(evt);
            nextHitIdx++;
        }

        double nextBeat = double(totalBeats);
        if (nextHitIdx < bridgeEvents.size() && bridgeEvents[nextHitIdx].beatOffset < nextBeat)
            nextBeat = bridgeEvents[nextHitIdx].beatOffset;
        if (lastEmittedBeat + 1 < nextBeat)
            nextBeat = lastEmittedBeat + 1;

        const double sleepMs = (nextBeat - beatPos) * 60000.0 / bpmNow;
        if (!_warmEvents.isEmpty() && sleepMs > 3.0) {
            for (MidiEvent *e : _warmEvents)
                emit playingEvent(*e);
            _warmEvents.clear();
            continue;   // re-check the clock after the burst
        }
        if (sleepMs > 1.5) {
            _mutex.lock();
            _waitCondition.wait(&_mutex, qBound(1, int(sleepMs - 0.5), 20));
            _mutex.unlock();
        }
    }

    if (hitCount > 0)
        medleyLog(QString("bridge %1 bars, %2 -> %3 BPM, %4 drum hits sent late avg %5 ms / max %6 ms")
                  .arg(totalBars).arg(fromBpm).arg(targetBpm).arg(hitCount)
                  .arg(hitLateSum / hitCount, 0, 'f', 2).arg(hitLateMax, 0, 'f', 2));

    // Let go of everything the bridge left ringing on melodic channels.
    // The downbeat crash/kick is added at handoff only when the new song lacks one.
    for (int ch = 0; ch < 16; ++ch) {
        if (ch == 9) continue;
        sendCC(ch, 64, 0);   // Sustain Off
        sendCC(ch, 123, 0);  // All Notes Off
    }
    return usedChannels;
}

void MidiSequencer::run()
{
    if (_playing)
        return;

    _mutex.lock();

    _playing = true;
    _stopped = false;
    _finished = false;
    _idealEndClockMs = -1.0;
    _fillEndClockMs = -1.0;
    _endAnnounced = false;

    if (_isMedleyTransition || _positionTick < _startTick || _playedIndex == 0) {
        _positionTick = _startTick;
        _startPlayTime = _midi->timeFromTick(_startTick, _midiSpeed) * 1000;
        _positionMs = _startPlayTime;
    } else {
        _startPlayTime = _midi->timeFromTick(_positionTick, _midiSpeed) * 1000;
        _positionMs = _startPlayTime;
    }

    _mutex.unlock();

    const bool wasMedleyTransition = _isMedleyTransition;

    // 1. PRE-WARM / PRE-SEND PHASE:
    // If this is a medley transition, pre-send all setup events (ProgramChanges, BankSelect,
    // Volumes, Pans, Reverbs, Chorus, and SetTempo) for ALL channels up to _startTick
    // BEFORE playMedleyTransitionFill() begins.
    // This gives BASSMIDI 1 to 2 whole seconds (the entire duration of the drum fill)
    // to load soundfont samples and prepare channel voice tables in advance!
    int preWarmIndex = 0;
    QVector<MidiEvent*> songSetupEvents; // re-applied after the bridge overwrites shared channels
    if (wasMedleyTransition) {
        // Events at _startTick are ordered by track, so one track's beat-1 notes can come
        // before another track's program changes. Keep scanning past those notes so every
        // channel's setup is pre-sent; preWarmIndex only covers the part before the first
        // note, so no note is skipped. (Stopping at the first note left later tracks to
        // load their instruments at beat 1, which made songs starting at bar 0 enter late.)
        bool reachedNote = false;
        for (int k = 0; k < _midi->events().count(); ++k) {
            MidiEvent *e = _midi->events()[k];
            if (e->tick() > _startTick)
                break;
            if (e->tick() == _startTick && (e->eventType() == MidiEventType::NoteOn || e->eventType() == MidiEventType::NoteOff)) {
                reachedNote = true;
                continue;
            }

            if (!reachedNote)
                preWarmIndex = k + 1;

            // Collected only. They are sent during the bridge right after its first
            // hits (so the bridge starts on time) and again at the song's beat 1.
            if (e->eventType() == MidiEventType::ProgramChange
                || e->eventType() == MidiEventType::PitchBend) {
                songSetupEvents.append(e);
            } else if (e->eventType() == MidiEventType::Controller) {
                const int ctrl = e->data1();
                if (ctrl == 120 || ctrl == 123)
                    continue; // never cut sound during the bridge
                songSetupEvents.append(e);
            } else if (e->eventType() == MidiEventType::Meta && e->metaEventType() == MidiMetaType::SetTempo) {
                if (_midi->isSingleTempo() && (e != _midi->tempoEvents()[0]))
                    continue;
                _midiBpm = e->bpm();
                emit bpmChanged(_midiBpm + _midiSpeed);
            }
        }
    } else if (_positionTick <= _startTick) {
        for (MidiEvent *e : _midi->events()) {
            if (e->tick() >= _startTick)
                break;
            if (e->eventType() != MidiEventType::Meta) {
                switch (e->eventType()) {
                case MidiEventType::NoteOff:
                case MidiEventType::NoteOn:
                    break;
                default:
                    emit playingEvent(*e);
                }
            } else if (e->metaEventType() == MidiMetaType::SetTempo) {
                if (_midi->isSingleTempo() && (e != _midi->tempoEvents()[0]))
                    continue;
                _midiBpm = e->bpm();
                emit bpmChanged(_midiBpm + _midiSpeed);
            }
        }
    }

    if (wasMedleyTransition && _medleyStartClockMs > 0)
        medleyLog(QString("next song thread ready %1 ms after previous song ended")
                  .arg(clockMs() - _medleyStartClockMs, 0, 'f', 2));

    // Melodic-channel setup is sent early (during the bridge) so the soundfont has
    // time to load the new song's instruments; ch 9 belongs to the bridge until beat 1.
    _warmEvents.clear();
    for (MidiEvent *e : songSetupEvents)
        if (e->channel() != 9)
            _warmEvents.append(e);

    // Started early by the player: everything is prepared, now wait for the exact
    // moment the previous song ends. With no bridge, wait 5 ms more so the previous
    // song's final notes-off can't land on top of our first notes.
    if (wasMedleyTransition && _medleyStartClockMs > 0) {
        const double target = _medleyStartClockMs + (_medleyTransitionBars > 0 ? 0.0 : 5.0);
        while (_playing) {
            const double remain = target - clockMs();
            if (remain <= 0)
                break;
            if (remain > 2.0) {
                _mutex.lock();
                _waitCondition.wait(&_mutex, qBound(1, int(remain - 1.5), 50));
                _mutex.unlock();
            }
        }
        if (!_playing)
            return;
    }

    // 2. DRUM FILL PHASE:
    quint16 bridgeChannels = 0;
    if (wasMedleyTransition && _medleyTransitionBars > 0) {
        bridgeChannels = playMedleyTransitionFill();
    }
    _isMedleyTransition = false;

    if (!_playing)
        return;

    bool seqEnded = false;

    // 3. SEAMLESS HANDOFF PHASE:
    // When the fill finishes, we are right on Beat 1 of the new song!
    _mutex.lock();
    if (wasMedleyTransition) {
        _startPlayTime = _midi->timeFromTick(_startTick, _midiSpeed) * 1000;
        _positionMs = _startPlayTime;
        _positionTick = _startTick;
        _playedIndex = qMax(preWarmIndex, eventIndexFromTick(_startTick));
    } else {
        _startPlayTime = _midi->timeFromTick(_positionTick, _midiSpeed) * 1000;
        _positionMs = _startPlayTime;
    }
    _eTimer->restart();
    if (wasMedleyTransition) {
        // Pretend our clock started at the ideal beat-1 moment (end of bridge, or end of
        // the previous song when there is no bridge), so the new song stays on the grid.
        const double anchor = (_fillEndClockMs > 0) ? _fillEndClockMs : _medleyStartClockMs;
        if (anchor > 0) {
            const double late = clockMs() - anchor;
            if (late > 0 && late < kMaxAnchorLateMs)
                _startPlayTime -= long(late + 0.5);
            medleyLog(QString("song start late by %1 ms%2").arg(late, 0, 'f', 2)
                      .arg((late > 0 && late < kMaxAnchorLateMs) ? " (snapped to grid)" : ""));
        }
    }
    _mutex.unlock();

    if (wasMedleyTransition) {
        // The rhythm bridge set its own instruments/volumes on the channels it used.
        // Put those channels back to GM defaults, then re-apply the new song's setup,
        // so the new song starts with its own sounds.
        auto sendCC = [this](int ch, int cc, int val) {
            MidiEvent e;
            e.setEventType(MidiEventType::Controller);
            e.setChannel(ch);
            e.setData1(cc);
            e.setData2(val);
            emit playingEvent(e);
        };
        for (int ch = 0; ch < 16; ++ch) {
            if (!(bridgeChannels & (1u << ch)))
                continue;
            sendCC(ch, 121, 0);  // Reset All Controllers
            sendCC(ch, 7, 100);
            sendCC(ch, 10, 64);
            sendCC(ch, 91, 40);
            sendCC(ch, 93, 0);
            sendCC(ch, 0, 0);
            sendCC(ch, 32, 0);
            MidiEvent pc;
            pc.setEventType(MidiEventType::ProgramChange);
            pc.setChannel(ch);
            pc.setData1(0);   // GM piano / standard drum kit on ch 9
            emit playingEvent(pc);
        }
        for (MidiEvent *e : songSetupEvents)
            emit playingEvent(*e);

        // Check if the new song already has Kick / Crash at _startTick
        bool hasKick = false;
        bool hasCrash = false;
        const int searchEnd = qMin(_midi->events().count(), _playedIndex + 30);
        for (int k = _playedIndex; k < searchEnd; ++k) {
            MidiEvent *e = _midi->events()[k];
            if (e->tick() > _startTick + (_midi->resorution() / 8))
                break;
            if (e->eventType() == MidiEventType::NoteOn && e->data2() > 0 && e->channel() == 9) {
                const int n = e->data1();
                if (n == 35 || n == 36) hasKick = true;
                if (n == 49 || n == 57 || n == 51 || n == 52 || n == 55) hasCrash = true;
            }
        }

        {
            // Diagnostics: how far after beat 1 the new song's first strong drum hit is.
            int firstHit = -1;
            for (int k = _playedIndex; k < _midi->events().count(); ++k) {
                MidiEvent *e = _midi->events()[k];
                if (e->eventType() == MidiEventType::NoteOn && e->channel() == 9 && e->data2() >= 80) {
                    firstHit = int(e->tick());
                    break;
                }
            }
            const int res = qMax(1, _midi->resorution());
            medleyLog(QString("new song: start bar %1 (tick %2), kick=%3 crash=%4, first strong drum hit %5 beats after start")
                      .arg(barAtTick(uint32_t(_startTick))).arg(_startTick)
                      .arg(hasKick).arg(hasCrash)
                      .arg(firstHit < 0 ? QString("none") : QString::number(double(firstHit - _startTick) / res, 'f', 2)));
        }

        const double volScale = _medleyFillVolume / 100.0;
        if (!hasCrash) {
            MidiEvent crashEvt;
            crashEvt.setEventType(MidiEventType::NoteOn);
            crashEvt.setChannel(9);
            crashEvt.setData1(49);
            crashEvt.setData2(qBound(1, int(115 * volScale), 127));
            emit playingEvent(crashEvt);
        }
        if (!hasKick) {
            MidiEvent kickEvt;
            kickEvt.setEventType(MidiEventType::NoteOn);
            kickEvt.setChannel(9);
            kickEvt.setData1(36);
            kickEvt.setData2(qBound(1, int(118 * volScale), 127));
            emit playingEvent(kickEvt);
        }
    }

    for (int i = _playedIndex; i < _midi->events().count(); i++) {

        if (!_playing)
            break;

        // Drum hold: stop at the next real bar line, wait for that moment, then hold.
        if (_holdRequested.load() && !_holdActive.load()) {
            if (_holdTick < 0) {
                const int curBar = barAtTick(uint32_t(qMax(0, _positionTick)));
                _holdTick = int(barStartTick(curBar + 1));
                emit drumHoldChanged(1);
            }
            if (int(_midi->events()[i]->tick()) >= _holdTick) {
                const double holdMs = _midi->timeFromTick(uint32_t(_holdTick), _midiSpeed) * 1000.0;
                while (_playing && _holdRequested.load()) {
                    const double remain = holdMs - double(_startPlayTime) - _eTimer->nsecsElapsed() / 1000000.0;
                    if (remain <= 0)
                        break;
                    _mutex.lock();
                    _waitCondition.wait(&_mutex, qBound(1, int(remain), 20));
                    _mutex.unlock();
                }
                if (_playing && _holdRequested.load()) {
                    _positionTick = _holdTick;
                    _positionMs = long(holdMs);
                    runDrumHold(i);
                }
                if (!_playing)
                    break;
            }
        }

        // Tell the player shortly before the cut-out so the next medley song can pre-start.
        if (!_endAnnounced && (_endEvent != nullptr) && (_endTick > 0)) {
            const double endMs = _midi->timeFromTick(uint32_t(_endEvent->tick()), _midiSpeed) * 1000.0;
            const double remain = endMs - double(_startPlayTime) - _eTimer->nsecsElapsed() / 1000000.0;
            if (remain <= 400.0 || _midi->events()[i]->tick() >= _endEvent->tick()) {
                _endAnnounced = true;
                emit endApproaching(clockMs() + qMax(0.0, remain));
            }
        }

        if ((_endEvent != nullptr) && (_endTick > 0) && _midi->events()[i]->tick() >= _endEvent->tick()) {
            if (_playing) {
                _mutex.lock();
                long endTime = (_midi->timeFromTick(uint32_t(_endEvent->tick()), _midiSpeed) * 1000);
                long waitTime = endTime - _startPlayTime - _eTimer->elapsed();
                if (waitTime > 2) {
                    _waitCondition.wait(&_mutex, waitTime - 1);
                    while ((_midi->timeFromTick(uint32_t(_endEvent->tick()), _midiSpeed) * 1000) - _startPlayTime - _eTimer->elapsed() > 0) {
                        if (!_playing) break;
                    }
                } else if (waitTime > 0) {
                    while ((_midi->timeFromTick(uint32_t(_endEvent->tick()), _midiSpeed) * 1000) - _startPlayTime - _eTimer->elapsed() > 0) {
                        if (!_playing) break;
                    }
                }
                // The exact moment the last bar ended, minus however late this thread woke up.
                const double overshoot = (double(_startPlayTime) + _eTimer->nsecsElapsed() / 1000000.0) - double(endTime);
                _idealEndClockMs = clockMs() - qMax(0.0, overshoot);
                _positionMs = endTime;
                _positionTick = _endEvent->tick();
                _mutex.unlock();
            }
            // Release sustain pedal & notes on melodic channels ONLY (keep Channel 9 drums ringing!)
            auto releaseMelodic = [this]() {
                MidiEvent sustainOff;
                sustainOff.setEventType(MidiEventType::Controller);
                sustainOff.setData1(64);
                sustainOff.setData2(0);
                MidiEvent notesOff;
                notesOff.setEventType(MidiEventType::Controller);
                notesOff.setData1(123);
                notesOff.setData2(0);
                for (int ch = 0; ch < 16; ch++) {
                    if (ch == 9) continue;
                    sustainOff.setChannel(ch);
                    emit playingEvent(sustainOff);
                    notesOff.setChannel(ch);
                    emit playingEvent(notesOff);
                }
            };
            if (!(_playing && _endHitHold))
                releaseMelodic();       // no drum bridge: the next song starts right now

            // Band-style ending: the song still hits its chord on beat 1 (together with
            // the bridge's crash), lets it ring for one beat, then lets go - instead of
            // chopping everything off. Only when a drum bridge follows (with no bridge
            // the next song's notes start right here).
            if (_playing && _endHitHold) {
                // land on beat 1, ring 1 beat, fade 1 beat, then silence (no chop)
                const QVector<RhythmBridgeEvent> tail = softStopEvents(i, int(_endEvent->tick()));
                const double beatMs = 60000.0 / qMax(30, currentBpm());
                const double t0 = clockMs();
                int k = 0, hitNotes = 0;
                while (_playing && k < tail.size()) {
                    const double due = t0 + tail[k].beatOffset * beatMs;
                    const double remain = due - clockMs();
                    if (remain > 1.5) {
                        _mutex.lock();
                        _waitCondition.wait(&_mutex, qBound(1, int(remain - 0.5), 20));
                        _mutex.unlock();
                        continue;
                    }
                    MidiEvent e;
                    e.setEventType(tail[k].eventType);
                    e.setChannel(tail[k].channel);
                    e.setData1(tail[k].data1);
                    e.setData2(tail[k].data2);
                    emit playingEvent(e);
                    if (tail[k].eventType == MidiEventType::NoteOn) ++hitNotes;
                    ++k;
                }
                if (!_playing)
                    releaseMelodic();
                medleyLog(QString("ending: %1 notes land on beat 1, ring 1 beat, fade 1 beat").arg(hitNotes));
            }
            seqEnded = true;
            break;
        }
        if (false) {
            MidiEvent evt;
            evt.setEventType(MidiEventType::Controller);
            evt.setData1(123);
            evt.setData2(0);
            for (int ch = 0; ch < 16; ch++) {
                evt.setChannel(ch);
                emit playingEvent(evt);
            }
            seqEnded = true;
            break;
        }

        _mutex.lock();

        if (_midi->events()[i]->eventType() != MidiEventType::Meta) {

            uint32_t tick = _midi->events()[i]->tick();

            if (_midiChangeBpmSpeed) {
                _midiChangeBpmSpeed = false;
                _midiSpeed = _midiSpeedTemp;
                _startPlayTime = _midi->timeFromTick(_midi->events()[i-1]->tick(), _midiSpeed) * 1000;
                _eTimer->restart();
            }

            long eventTime = (_midi->timeFromTick(tick, _midiSpeed)  * 1000);
            long waitTime = eventTime - _startPlayTime  - _eTimer->elapsed();

            if (waitTime > 2) {
                _waitCondition.wait(&_mutex, waitTime - 1);
                while ((_midi->timeFromTick(tick, _midiSpeed) * 1000) - _startPlayTime - _eTimer->elapsed() > 0) {
                    if (!_playing) break;
                }
            } else if (waitTime > 0) {
                while ((_midi->timeFromTick(tick, _midiSpeed) * 1000) - _startPlayTime - _eTimer->elapsed() > 0) {
                    if (!_playing) break;
                }
            }

            _positionMs = eventTime;


        } else { // Meta event
            if (_midi->events()[i]->metaEventType() == MidiMetaType::SetTempo) {
                if (_midi->isSingleTempo() && (_midi->events()[i] != _midi->tempoEvents()[0])) {
                    _playedIndex = i;
                    _positionTick = _midi->events()[i]->tick();
                    _mutex.unlock();
                    continue;
                }
                _midiBpm = _midi->events()[i]->bpm();
                emit bpmChanged(_midiBpm + _midiSpeed);
            }
        }

        {
            MidiEvent *pe = _midi->events()[i];
            if (pe->eventType() == MidiEventType::Controller && pe->data1() == 11)
                _lastExpr[pe->channel() & 15] = pe->data2();
        }
        emit playingEvent(*_midi->events()[i]);

        _playedIndex = i;
        _positionTick = _midi->events()[i]->tick();

        _mutex.unlock();

    } // End for loop

    if (_idealEndClockMs < 0)
        _idealEndClockMs = clockMs();

    _mutex.lock();
    _playing = false;
    if (false) _stopped = true;
    _mutex.unlock();
    if (seqEnded || (_playedIndex == _midi->events().size() -1)) {
        _mutex.lock();
        _finished = true;
        _stopped = true;
        _mutex.unlock();
    }
}

int MidiSequencer::eventIndexFromTick(int tick)
{
    int index = 0;
    for (MidiEvent *e :_midi->events()) {
        if (e->tick() >= tick)
            break;

        index++;
    }

    return index;
}
