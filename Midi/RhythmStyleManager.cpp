#include "RhythmStyleManager.h"
#include "MidiFile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <algorithm>

RhythmStyleManager &RhythmStyleManager::instance()
{
    static RhythmStyleManager mgr;
    return mgr;
}

RhythmStyleManager::RhythmStyleManager()
{
}

RhythmStyleManager::~RhythmStyleManager()
{
}

int RhythmStyleManager::patternIndexToRhythmId(int patternIndex) const
{
    switch (patternIndex) {
    case 0: return 900; // Sam-Cha
    case 1: return 901; // Lum-Zing
    case 2: return 902; // Sam-Cha Lum-Zing
    case 3: return 903; // Disco / Pop
    case 4: return 904; // Lum-Zing 2
    case 5: return 100; // Standard Rock Fill
    case 6: {
        // Random selection from authentic Thai rhythms
        static const int pool[] = { 900, 901, 902, 903, 904 };
        int idx = QRandomGenerator::global()->bounded(5);
        return pool[idx];
    }
    default:
        return 900;
    }
}

QString RhythmStyleManager::findRhythmFile(int rhythmId) const
{
    const QString fileName = QString("%1.mid").arg(rhythmId);
    const QStringList searchDirs = {
        QCoreApplication::applicationDirPath() + "/Rhythms",
        QCoreApplication::applicationDirPath() + "/../Rhythms",
        "D:/REA/PTHK-Karaoke/Rhythms",
        "D:/eXtreme Karaoke 2026/Rhythms",
        "Rhythms"
    };

    for (const QString &dir : searchDirs) {
        QString candidate = QDir(dir).filePath(fileName);
        if (QFile::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

bool RhythmStyleManager::buildTransitionBridge(int rhythmId, int totalBars, double volumeScale, QVector<RhythmBridgeEvent> &outEvents)
{
    outEvents.clear();
    const int clampedBars = qBound(1, totalBars, 8);
    const QString path = findRhythmFile(rhythmId);

    if (path.isEmpty()) {
        buildFallbackDrumFill(rhythmId == 100 ? 5 : 0, clampedBars, volumeScale, outEvents);
        return false;
    }

    MidiFile midi;
    if (!midi.read(path)) {
        buildFallbackDrumFill(rhythmId == 100 ? 5 : 0, clampedBars, volumeScale, outEvents);
        return false;
    }

    const int res = midi.resorution() > 0 ? midi.resorution() : 120;
    const uint32_t barTicks = res * 4;

    struct MarkerEntry {
        uint32_t tick;
        QString name;
    };
    QVector<MarkerEntry> markers;
    QVector<MidiEvent*> allEvents = midi.events();

    for (MidiEvent *e : allEvents) {
        if (e->eventType() == MidiEventType::Meta && e->metaEventType() == MidiMetaType::Marker) {
            QString name = QString::fromLatin1(e->data()).trimmed();
            markers.append({e->tick(), name});
        }
    }
    std::sort(markers.begin(), markers.end(), [](const MarkerEntry &a, const MarkerEntry &b) {
        return a.tick < b.tick;
    });

    auto findMarker = [&](const QString &markerName) -> int64_t {
        for (const MarkerEntry &m : markers) {
            if (m.name.compare(markerName, Qt::CaseInsensitive) == 0)
                return m.tick;
        }
        return -1;
    };

    // Like eXtreme Karaoke, the medley bridge is the drum kit only.
    // The style files also carry bass/chord/pad parts (ch 10..15, or 1..5 in 904)
    // recorded in the style's own key (e.g. Gm, Cm); playing them clashes with the next song.
    auto isBridgeChannel = [](int ch) { return ch == 9; };

    // 1. Initial Setup Events (ProgramChange, BankSelect, Volume, Pan, Expression, Reverb)
    for (MidiEvent *e : allEvents) {
        if (e->tick() < barTicks && isBridgeChannel(e->channel())) {
            if (e->eventType() == MidiEventType::ProgramChange) {
                outEvents.append({0.0, MidiEventType::ProgramChange, e->channel(), e->data1(), 0});
            } else if (e->eventType() == MidiEventType::Controller) {
                const int ctrl = e->data1();
                int val = e->data2();
                if (ctrl == 7) { // CC 7 Volume: scale by volumeScale
                    val = qBound(1, int(val * volumeScale), 127);
                }
                outEvents.append({0.0, MidiEventType::Controller, e->channel(), ctrl, val});
            }
        }
    }

    // 2. Identify Fill and Main sections
    int64_t fillStart = findMarker("Fill In BA");
    if (fillStart < 0) fillStart = findMarker("Fill In AA");
    if (fillStart < 0) fillStart = findMarker("Fill In BB");
    if (fillStart < 0) fillStart = findMarker("Fill In CC");
    if (fillStart < 0) fillStart = findMarker("Fill In DD");

    // Standard 100.mid or unmarked rhythm: first bar is fill
    if (fillStart < 0) fillStart = 0;

    // Groove source: bars of a Main (or Intro) section that really contain drums.
    // Some styles (e.g. 900 "intro") have no drums in Main A at all, which made the
    // groove bars silent; skip those and use the next Main section that has a beat.
    auto drumHitsInBar = [&](int64_t barStart) {
        int hits = 0;
        for (MidiEvent *e : allEvents) {
            if (e->tick() >= uint32_t(barStart) && e->tick() < uint32_t(barStart + barTicks)
                && e->channel() == 9 && e->eventType() == MidiEventType::NoteOn && e->data2() > 0)
                ++hits;
        }
        return hits;
    };
    auto sectionEnd = [&](int64_t start) {
        for (const MarkerEntry &m : markers)
            if (int64_t(m.tick) > start)
                return int64_t(m.tick);
        return int64_t(midi.events().isEmpty() ? start + barTicks : midi.events().last()->tick());
    };

    QVector<int64_t> grooveSrcBars;
    // Main sections first; intro-type styles (900) keep their beat in Intro C/B/A.
    // Within a section keep only the full bars (>= 75% of its busiest bar), so
    // break/turnaround bars with few hits don't leave a hole in the groove.
    for (const char *name : {"Main A", "Main B", "Main C", "Main D", "Intro C", "Intro B", "Intro A"}) {
        const int64_t secStart = findMarker(name);
        if (secStart < 0)
            continue;
        const int64_t secEnd = sectionEnd(secStart);
        QVector<QPair<int64_t, int>> bars;
        int maxHits = 0;
        for (int64_t bar = secStart; bar + barTicks <= secEnd; bar += barTicks) {
            const int h = drumHitsInBar(bar);
            bars.append({bar, h});
            maxHits = qMax(maxHits, h);
        }
        if (maxHits < 8)
            continue;   // too sparse to carry a groove
        for (const auto &bh : bars)
            if (bh.second * 4 >= maxHits * 3)
                grooveSrcBars.append(bh.first);
        if (!grooveSrcBars.isEmpty())
            break;      // one consistent section is enough
    }

    // 3. Groove Bars for (totalBars - 1)
    const int grooveBars = clampedBars - 1;
    if (grooveBars > 0 && grooveSrcBars.isEmpty()) {
        // No usable drum groove in this style: basic kick/snare/hi-hat so the bridge is never silent.
        auto addNote = [&](double beat, int note, int vel) {
            const int v = qBound(1, int(vel * volumeScale), 127);
            outEvents.append({beat, MidiEventType::NoteOn, 9, note, v});
            outEvents.append({beat + 0.4, MidiEventType::NoteOff, 9, note, 0});
        };
        for (int b = 0; b < grooveBars * 4; ++b) {
            addNote(b, (b % 2 == 0) ? 36 : 38, (b % 2 == 0) ? 110 : 100);
            addNote(b, 42, 85);
            addNote(b + 0.5, 42, 70);
        }
    }
    for (int b = 0; b < grooveBars && !grooveSrcBars.isEmpty(); ++b) {
        const double barBeatBase = b * 4.0;
        const int64_t srcBar = grooveSrcBars[b % grooveSrcBars.size()];
        for (MidiEvent *e : allEvents) {
            if (!isBridgeChannel(e->channel()))
                continue;
            if (e->tick() >= uint32_t(srcBar) && e->tick() < uint32_t(srcBar + barTicks)) {
                if (e->eventType() == MidiEventType::NoteOn ||
                    e->eventType() == MidiEventType::NoteOff ||
                    e->eventType() == MidiEventType::Controller ||
                    e->eventType() == MidiEventType::PitchBend)
                {
                    double beat = barBeatBase + double(int64_t(e->tick()) - srcBar) / double(res);
                    int d2 = e->data2();
                    if (e->eventType() == MidiEventType::NoteOn && d2 > 0) {
                        d2 = qBound(1, int(d2 * volumeScale), 127);
                    }
                    outEvents.append({beat, e->eventType(), e->channel(), e->data1(), d2});
                }
            }
        }
    }

    // 4. Final Transition Fill Bar (Fill In BA)
    const double fillBeatBase = (clampedBars - 1) * 4.0;
    for (MidiEvent *e : allEvents) {
        if (!isBridgeChannel(e->channel()))
            continue;
        if (e->tick() >= uint32_t(fillStart) && e->tick() < uint32_t(fillStart + barTicks)) {
            if (e->eventType() == MidiEventType::NoteOn ||
                e->eventType() == MidiEventType::NoteOff ||
                e->eventType() == MidiEventType::Controller ||
                e->eventType() == MidiEventType::PitchBend)
            {
                double beat = fillBeatBase + double(e->tick() - fillStart) / double(res);
                int d2 = e->data2();
                if (e->eventType() == MidiEventType::NoteOn && d2 > 0) {
                    d2 = qBound(1, int(d2 * volumeScale), 127);
                }
                outEvents.append({beat, e->eventType(), e->channel(), e->data1(), d2});
            }
        }
    }

    // 5. Stable Sort: Time first, then ProgramChange / Controller before NoteOn
    std::sort(outEvents.begin(), outEvents.end(), [](const RhythmBridgeEvent &a, const RhythmBridgeEvent &b) {
        if (qAbs(a.beatOffset - b.beatOffset) > 0.0001)
            return a.beatOffset < b.beatOffset;
        // Priority: Setup (ProgramChange/Controller) before Notes
        int prioA = (a.eventType == MidiEventType::ProgramChange || a.eventType == MidiEventType::Controller) ? 0 : 1;
        int prioB = (b.eventType == MidiEventType::ProgramChange || b.eventType == MidiEventType::Controller) ? 0 : 1;
        return prioA < prioB;
    });

    return true;
}

void RhythmStyleManager::buildFallbackDrumFill(int patternIndex, int totalBars, double volumeScale, QVector<RhythmBridgeEvent> &outEvents)
{
    outEvents.clear();
    const int clampedBars = qBound(1, totalBars, 8);
    const int totalBeats = clampedBars * 4;

    auto addNote = [&](double beat, int note, int vel, double durBeats = 0.4) {
        int scaledVel = qBound(1, int(vel * volumeScale), 127);
        outEvents.append({beat, MidiEventType::NoteOn, 9, note, scaledVel});
        outEvents.append({beat + durBeats, MidiEventType::NoteOff, 9, note, 0});
    };

    // Standard live-band drum fill fallback on Channel 9
    for (int b = 0; b < totalBeats; ++b) {
        // Kick on 1 and 3
        if (b % 2 == 0) addNote(b + 0.0, 36, 110);
        // Snare on 2 and 4
        if (b % 2 == 1) addNote(b + 0.0, 38, 100);
        // Hihat
        addNote(b + 0.0, 42, 85);
        addNote(b + 0.5, 42, 75);
    }

    // Final bar roll & buildup
    double lastBarStart = (clampedBars - 1) * 4.0;
    addNote(lastBarStart + 2.0, 48, 105); // High Mid Tom
    addNote(lastBarStart + 2.5, 47, 108); // Low Mid Tom
    addNote(lastBarStart + 3.0, 45, 115); // Low Tom
    addNote(lastBarStart + 3.5, 43, 118); // High Floor Tom
    addNote(lastBarStart + 3.75, 38, 122); // Snare Roll

    std::sort(outEvents.begin(), outEvents.end(), [](const RhythmBridgeEvent &a, const RhythmBridgeEvent &b) {
        return a.beatOffset < b.beatOffset;
    });
}
