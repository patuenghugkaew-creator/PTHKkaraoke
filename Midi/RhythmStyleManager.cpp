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

    // 1. Initial Setup Events (ProgramChange, BankSelect, Volume, Pan, Expression, Reverb)
    // Send all setups for channels 0..15 at beatOffset = 0.0
    for (MidiEvent *e : allEvents) {
        if (e->tick() < barTicks) {
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

    int64_t mainStart = findMarker("Main A");
    if (mainStart < 0) mainStart = findMarker("Main B");
    if (mainStart < 0) mainStart = findMarker("Main C");

    // Standard 100.mid or unmarked rhythm: first bar is fill
    if (fillStart < 0) fillStart = 0;
    if (mainStart < 0) mainStart = fillStart;

    // 3. Groove Bars (Main A / B) for (totalBars - 1)
    const int grooveBars = clampedBars - 1;
    for (int b = 0; b < grooveBars; ++b) {
        const double barBeatBase = b * 4.0;
        for (MidiEvent *e : allEvents) {
            if (e->tick() >= uint32_t(mainStart) && e->tick() < uint32_t(mainStart + barTicks)) {
                if (e->eventType() == MidiEventType::NoteOn ||
                    e->eventType() == MidiEventType::NoteOff ||
                    e->eventType() == MidiEventType::Controller ||
                    e->eventType() == MidiEventType::PitchBend)
                {
                    double beat = barBeatBase + double(e->tick() - mainStart) / double(res);
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
