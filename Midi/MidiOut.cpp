#include "MidiOut.h"

#include <QHash>
#include <QMutex>

static QMutex g_countMx;
static QHash<QString, int> g_counts;

MidiOut::MidiOut()
{
    oVolume = 1.0f;
    setErrorCallback(&MidiOut::onError, this);     // errors mark the port, never throw
}

MidiOut::~MidiOut()
{
    message.clear();
}

void MidiOut::onError(RtMidiError::Type type, const std::string &, void *user)
{
    if (type == RtMidiError::WARNING || type == RtMidiError::DEBUG_WARNING)
        return;
    static_cast<MidiOut *>(user)->failed.store(true);
}

bool MidiOut::openSafe(int index, const QString &name)
{
    pName = name;
    failed.store(false);
    if (index < 0)
        return false;
    try {
        openPort(unsigned(index));
    } catch (...) {
        failed.store(true);
        return false;
    }
    if (!isPortOpen()) {
        failed.store(true);
        return false;
    }
    return true;
}

int MidiOut::takeCount(const QString &name)
{
    QMutexLocker lk(&g_countMx);
    return g_counts.take(name);
}

void MidiOut::send(std::vector<unsigned char> *m)
{
    if (!isPortOpen())
        return;
    try {
        sendMessage(m);
    } catch (...) {
        failed.store(true);
    }
    if (!pName.isEmpty()) {
        QMutexLocker lk(&g_countMx);
        ++g_counts[pName];
    }
}

void MidiOut::setVolume(float vol)
{
    if (vol < 0.0f) oVolume = 0.0f;
    else if (vol > 1.0f) oVolume = 1.0f;
    else oVolume = vol;

    int vol_14bits = (int)(oVolume * 16383);

    message.clear();
    message.push_back(0xF0);
    message.push_back(0x7F);
    message.push_back(0x7F);
    message.push_back(0x04);
    message.push_back(0x01);
    message.push_back(vol_14bits & 0x7f);
    message.push_back(vol_14bits >> 7);
    message.push_back(0xF7);
    send(&message);
}

void MidiOut::sendNoteOff(int ch, int note, int velocity)
{
    if (note < 0 || note > 127)
        return;
    message.clear();
    message.push_back(0x80 + ch);
    message.push_back(note);
    message.push_back(velocity);
    send(&message);
}

void MidiOut::sendNoteOn(int ch, int note, int velocity)
{
    if (note < 0 || note > 127)
        return;
    message.clear();
    message.push_back(0x90 + ch);
    message.push_back(note);
    message.push_back(velocity);
    send(&message);
}

void MidiOut::sendNoteAftertouch(int ch, int note, int value)
{
    if (note < 0 || note > 127)
        return;
    message.clear();
    message.push_back(0xA0 + ch);
    message.push_back(note);
    message.push_back(value);
    send(&message);
}

void MidiOut::sendController(int ch, int number, int value)
{
    message.clear();
    message.push_back(0xB0 + ch);
    message.push_back(number);
    message.push_back(value);
    send(&message);
}

void MidiOut::sendProgramChange(int ch, int number)
{
    message.clear();
    message.push_back(0xC0 + ch);
    message.push_back(number);
    send(&message);
}

void MidiOut::sendChannelAftertouch(int ch, int value)
{
    message.clear();
    message.push_back(0xD0 + ch);
    message.push_back(value);
    send(&message);
}

void MidiOut::sendPitchBend(int ch, int value)
{
    message.clear();
    message.push_back(0xE0 + ch);
    message.push_back(value & 0x7F);
    message.push_back(value / 128);
    send(&message);
}

void MidiOut::sendAllNotesOff(int ch)
{
    sendController(ch, 123, 0);
}

void MidiOut::sendAllNotesOff()
{
    for (int i=0; i<16; i++) {
        sendAllNotesOff(i);
    }
}

void MidiOut::sendAllSoundOff(int ch)
{
    sendController(ch, 120, 0);
}

void MidiOut::sendAllSoundOff()
{
    for (int i=0; i<16; i++) {
        sendAllSoundOff(i);
    }
}

void MidiOut::sendResetAllControllers(int ch)
{
    sendController(ch, 100, 0);
    sendController(ch, 101, 0);
    sendController(ch, 6, 2);

    sendController(ch, 121, 0); 
}

void MidiOut::sendResetAllControllers()
{
    for (int i=0; i<16; i++) {
        sendResetAllControllers(i);
    }
}
