#ifndef RHYTHMSTYLEMANAGER_H
#define RHYTHMSTYLEMANAGER_H

#include <QString>
#include <QVector>
#include "MidiEvent.h"

struct RhythmBridgeEvent {
    double beatOffset;          // in beats from transition start (0.0 to totalBeats)
    MidiEventType eventType;    // NoteOn, NoteOff, Controller, ProgramChange, PitchBend
    int channel;                // 0..15 (Ch 9=Drums, 10=Percussion, 11=Bass, 12=Chords/Phin, 13=Brass, 14=Organ, 15=Khaen)
    int data1;                  // Note number, Controller CC#, or Program#
    int data2;                  // Velocity (Note) or Controller value
};

class RhythmStyleManager {
public:
    static RhythmStyleManager &instance();

    // Map combo box index (0..6) to rhythm number (900, 901, 902, 903, 904, 100)
    int patternIndexToRhythmId(int patternIndex) const;

    // Load and build a multi-channel arranger transition bridge
    bool buildTransitionBridge(int rhythmId, int totalBars, double volumeScale, QVector<RhythmBridgeEvent> &outEvents);

    // Fallback drum fill generator if no rhythm file is available
    void buildFallbackDrumFill(int patternIndex, int totalBars, double volumeScale, QVector<RhythmBridgeEvent> &outEvents);

private:
    RhythmStyleManager();
    ~RhythmStyleManager();

    QString findRhythmFile(int rhythmId) const;
};

#endif // RHYTHMSTYLEMANAGER_H
