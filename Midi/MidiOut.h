#ifndef MIDIOUT_H
#define MIDIOUT_H

#ifdef __linux__
#include <rtmidi/RtMidi.h>
#else
#include <RtMidi.h>
#endif

#include <QString>
#include <atomic>

// A MIDI output port. It never throws: RtMidi throws on a failed open or a failed send (a loopMIDI
// port closed while a song plays), and nothing up the call chain catches it, so the program died.
// Here errors only mark the port as failed (hasFailed); the routing timer then reconnects it.
class MidiOut : public RtMidiOut
{
public:
    MidiOut();
    ~MidiOut();

    // open by Windows device number; name = that device's name (kept for reconnects and status)
    bool openSafe(int index, const QString &name);
    QString portName() const { return pName; }
    bool hasFailed() const { return failed.load(); }

    // messages sent to a port (by name) since the last call: the MIDI Mapper's activity lights
    static int takeCount(const QString &name);

    float volume() { return oVolume; }
    void setVolume(float vol);
    void sendNoteOff(int ch, int note, int velocity);
    void sendNoteOn(int ch, int note, int velocity);
    void sendNoteAftertouch(int ch, int note, int value);
    void sendController(int ch, int number, int value);
    void sendProgramChange(int ch, int number);
    void sendChannelAftertouch(int ch, int value);
    void sendPitchBend(int ch, int value);
    void sendAllNotesOff(int ch);
    void sendAllNotesOff();
    void sendAllSoundOff(int ch);
    void sendAllSoundOff();
    void sendResetAllControllers(int ch);
    void sendResetAllControllers();
    void send(std::vector<unsigned char> *m);      // any raw message (counted)

private:
    static void onError(RtMidiError::Type type, const std::string &text, void *user);
    float oVolume;
    std::vector<unsigned char> message;
    QString pName;
    std::atomic<bool> failed{false};
};

#endif // MIDIOUT_H
