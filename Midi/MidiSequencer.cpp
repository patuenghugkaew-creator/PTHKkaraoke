#include "MidiSequencer.h"
#include "MedleyDrumFills.h"
#include <QRandomGenerator>
#include <QMap>

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
    if (_isMedleyTransition || !_eTimer || !_eTimer->isValid()) {
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
    if (_isMedleyTransition || !_eTimer || !_eTimer->isValid()) {
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
    int endbar = (_midi->barCount() + 1) - bar;
    if (bar <= 0)
        endbar--;

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
        long firstLyricTick = -1;
        for (long c : lyricCursors) {
            if (c > 0) {
                firstLyricTick = c;
                break;
            }
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
            const int firstVocalBar = _midi->barFromTick(uint32_t(firstLyricTick));
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
                // Normal songs: keep the authentic signature intro motif!
                // If intro is short/standard (<= 8 bars), start at Bar 0 (note 1 of the song).
                // This ensures every instrument, motif, and riff enters cleanly without awkward 2-bar mid-phrase cuts!
                if (firstVocalBar <= 8) {
                    startBar = 0;
                } else {
                    // For unusually long intros (> 8 bars), align cleanly to a 4-bar phrase boundary (4 bars before vocal)
                    startBar = qMax(0, ((firstVocalBar - 4) / 4) * 4);
                }
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

            startBar = _midi->barFromTick(uint32_t(qMax(0, firstMusicTick)));
        }

        startBar = qBound(0, startBar, qMax(0, totalBars - 4));
        setStartTick(_midi->tickFromBar(startBar));
    }

    // 2. Outro bar boundary (Cue-Out)
    if (manualEndBar > 0) {
        setCutEndBar(manualEndBar);
    } else {
        // In automated Medley playback, dynamic outro cutting is handled by MainWindow::checkEndAfterLyrics
        // as soon as the singer actually reaches and completes the final lyric.
        // Never set a premature static _endTick here at the start of playback or load time,
        // which cuts songs off in the middle of singing!
        setCutEndBar(totalBars);
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
}

void MidiSequencer::playMedleyTransitionFill()
{
    if (!_playing || _medleyTransitionBars <= 0)
        return;

    const int totalBars = qBound(1, _medleyTransitionBars, 8);
    const int beatsPerBar = 4;
    const int totalBeats = totalBars * beatsPerBar;
    const int startBpm = currentBpm() > 0 ? currentBpm() : 120;
    const int targetBpm = qBound(30, _medleyTargetBpm > 0 ? _medleyTargetBpm : startBpm, 300);
    const double baseBpm = (_medleyTempoMode == 2) ? double(startBpm) : double(targetBpm);
    double beatMs = 60000.0 / baseBpm;
    double totalDurationMs = totalBeats * beatMs;

    struct DrumHit {
        double beatOffset;
        int note;
        int velocity;
    };
    QVector<DrumHit> hits;

    const double volScale = _medleyFillVolume / 100.0;

    // Ensure Channel 9 (Drums) is audible, unmuted, and centered for the drum fill
    MidiEvent volEvt;
    volEvt.setEventType(MidiEventType::Controller);
    volEvt.setChannel(9);
    volEvt.setData1(7); // Main Volume
    volEvt.setData2(127);
    emit playingEvent(volEvt);

    MidiEvent expEvt;
    expEvt.setEventType(MidiEventType::Controller);
    expEvt.setChannel(9);
    expEvt.setData1(11); // Expression
    expEvt.setData2(127);
    emit playingEvent(expEvt);

    MidiEvent panEvt;
    panEvt.setEventType(MidiEventType::Controller);
    panEvt.setChannel(9);
    panEvt.setData1(10); // Pan center
    panEvt.setData2(64);
    emit playingEvent(panEvt);

    // 1. Lead-in bars (if totalBars > 1): play a steady drum groove bridge
    for (int bar = 0; bar < totalBars - 1; ++bar) {
        const double base = bar * beatsPerBar;
        // Beat 1: Kick + Hi-hat (and Crash on very first bar)
        hits.append({ base + 0.0, 36, int(110 * volScale) });
        if (bar == 0)
            hits.append({ base + 0.0, 49, int(100 * volScale) });
        hits.append({ base + 0.0, 42, int(90 * volScale) });
        // Beat 2: Snare + Hi-hat
        hits.append({ base + 1.0, 38, int(105 * volScale) });
        hits.append({ base + 1.0, 42, int(90 * volScale) });
        // Beat 3: Kick + Hi-hat
        hits.append({ base + 2.0, 36, int(105 * volScale) });
        hits.append({ base + 2.0, 42, int(90 * volScale) });
        // Beat 4: Snare + Hi-hat
        hits.append({ base + 3.0, 38, int(105 * volScale) });
        hits.append({ base + 3.0, 42, int(90 * volScale) });
    }

    // 2. Final bar: play selected fill pattern
    const double fillBase = (totalBars - 1) * beatsPerBar;

    int activePattern = _medleyFillPattern;
    if (activePattern == 4) { // Random / Dynamic pattern
        activePattern = QRandomGenerator::global()->bounded(4);
    }

    // Load authentic professional SFF drum fill pattern extracted from eXtreme Karaoke
    int activeCat = 1; // Default to 900 Sam-Cha
    switch (_medleyFillPattern) {
    case 0: activeCat = 1; break; // 900 Sam-Cha (สามช่า)
    case 1: activeCat = 2; break; // 901 Lum-Zing (ลำซิ่ง)
    case 2: activeCat = 3; break; // 902 3-Cha Lum-Zing (3ช่าลำซิ่ง)
    case 3: activeCat = 0; break; // 903 Disco Universal (ดิสโก้)
    case 4: activeCat = 2; break; // 904 Lum-Zing 2 (ลำซิ่ง 2)
    case 5: activeCat = 0; break; // 008 Soul 1 (โซล 1)
    case 6: activeCat = 0; break; // 000 Standard (ลูกส่งมาตรฐาน)
    case 7: // Random / Dynamic
    default: {
        static const int catOptions[] = { 1, 2, 3, 0 };
        activeCat = catOptions[QRandomGenerator::global()->bounded(4)];
        break;
    }
    }

    const DrumFillPattern *pat = getAuthenticDrumFill(activeCat);
    if (pat && pat->count > 0) {
        for (int pIdx = 0; pIdx < pat->count; ++pIdx) {
            const DrumFillNote &dn = pat->notes[pIdx];
            // dn.tick is 0..479 normalized to 1 bar (4 beats)
            double beatOffset = fillBase + (double(dn.tick) / 480.0) * beatsPerBar;
            int scaledVel = qBound(1, int(dn.velocity * volScale), 127);
            hits.append({ beatOffset, dn.note, scaledVel });
        }
    } else {
        hits.append({ fillBase + 0.0, 36, int(112 * volScale) });
        hits.append({ fillBase + 1.0, 38, int(108 * volScale) });
        hits.append({ fillBase + 2.0, 48, int(110 * volScale) });
        hits.append({ fillBase + 3.0, 45, int(115 * volScale) });
        hits.append({ fillBase + 3.75, 49, int(125 * volScale) });
    }

    std::stable_sort(hits.begin(), hits.end(), [](const DrumHit &a, const DrumHit &b) {
        return a.beatOffset < b.beatOffset;
    });

    QElapsedTimer timer;
    timer.start();

    int nextHitIdx = 0;
    int lastEmittedBeat = -1;
    int lastEmittedBpm = startBpm;

    while (_playing) {
        const double elapsedMs = timer.nsecsElapsed() / 1000000.0;
        if (elapsedMs >= totalDurationMs)
            break;

        // Dynamic Tempo Ramp (Morphing) over the transition bridge
        if (_medleyTempoMode == 2 && totalDurationMs > 0) {
            const double progress = qBound(0.0, elapsedMs / totalDurationMs, 1.0);
            const int rampBpm = qBound(30, int(startBpm + (targetBpm - startBpm) * progress), 300);
            if (rampBpm != lastEmittedBpm) {
                lastEmittedBpm = rampBpm;
                beatMs = 60000.0 / double(rampBpm);
                emit bpmChanged(rampBpm);
            }
        }

        const double currentBeatPos = elapsedMs / beatMs;
        const int currentBeatNum = int(currentBeatPos);

        if (currentBeatNum != lastEmittedBeat && currentBeatNum < totalBeats) {
            lastEmittedBeat = currentBeatNum;
            int barIndex = (currentBeatNum / beatsPerBar) + 1;
            int beatInBar = (currentBeatNum % beatsPerBar) + 1;
            emit medleyCountIn(beatInBar, beatsPerBar, barIndex, totalBars);
        }

        while (nextHitIdx < hits.size() && (hits[nextHitIdx].beatOffset * beatMs) <= elapsedMs) {
            MidiEvent evt;
            evt.setEventType(MidiEventType::NoteOn);
            evt.setChannel(9);
            evt.setData1(hits[nextHitIdx].note);
            evt.setData2(hits[nextHitIdx].velocity);
            emit playingEvent(evt);
            nextHitIdx++;
        }

        double nextTimeMs = totalDurationMs;
        if (nextHitIdx < hits.size()) {
            double hitTimeMs = hits[nextHitIdx].beatOffset * beatMs;
            if (hitTimeMs < nextTimeMs)
                nextTimeMs = hitTimeMs;
        }
        double nextBeatMs = (lastEmittedBeat + 1) * beatMs;
        if (nextBeatMs < nextTimeMs)
            nextTimeMs = nextBeatMs;

        double sleepMs = nextTimeMs - (timer.nsecsElapsed() / 1000000.0);
        if (sleepMs > 1.5) {
            _mutex.lock();
            _waitCondition.wait(&_mutex, qBound(1, int(sleepMs - 0.5), 100));
            _mutex.unlock();
        }
    }
}

void MidiSequencer::run()
{
    if (_playing)
        return;

    _mutex.lock();

    _playing = true;
    _stopped = false;
    _finished = false;

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
    int songDrumVol = -1;
    int songDrumPan = -1;
    if (wasMedleyTransition) {
        for (int k = 0; k < _midi->events().count(); ++k) {
            MidiEvent *e = _midi->events()[k];
            if (e->tick() > _startTick)
                break;
            // Stop before any Note-On or Note-Off events at _startTick
            if (e->tick() == _startTick && (e->eventType() == MidiEventType::NoteOn || e->eventType() == MidiEventType::NoteOff))
                break;

            preWarmIndex = k + 1;

            if (e->eventType() == MidiEventType::ProgramChange) {
                emit playingEvent(*e);
            } else if (e->eventType() == MidiEventType::Controller) {
                const int ctrl = e->data1();
                if (e->channel() == 9) {
                    if (ctrl == 7) songDrumVol = e->data2();
                    if (ctrl == 10) songDrumPan = e->data2();
                    // Do not silence or mute drums during the fill
                    if (ctrl == 7 || ctrl == 11 || ctrl == 120 || ctrl == 123)
                        continue;
                }
                emit playingEvent(*e);
            } else if (e->eventType() == MidiEventType::PitchBend) {
                emit playingEvent(*e);
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

    // 2. DRUM FILL PHASE:
    if (wasMedleyTransition && _medleyTransitionBars > 0) {
        playMedleyTransitionFill();
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
    _mutex.unlock();

    if (wasMedleyTransition) {
        // Restore Song 2's native Channel 9 volume & pan if specified
        if (songDrumVol >= 0) {
            MidiEvent v;
            v.setEventType(MidiEventType::Controller);
            v.setChannel(9);
            v.setData1(7);
            v.setData2(songDrumVol);
            emit playingEvent(v);
        }
        if (songDrumPan >= 0) {
            MidiEvent p;
            p.setEventType(MidiEventType::Controller);
            p.setChannel(9);
            p.setData1(10);
            p.setData2(songDrumPan);
            emit playingEvent(p);
        }

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
                _positionMs = endTime;
                _positionTick = _endEvent->tick();
                _mutex.unlock();
            }
            // Release sustain pedal & notes on melodic channels ONLY (keep Channel 9 drums ringing!)
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

        emit playingEvent(*_midi->events()[i]);

        _playedIndex = i;
        _positionTick = _midi->events()[i]->tick();

        _mutex.unlock();

    } // End for loop

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
