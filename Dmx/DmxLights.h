#ifndef DMXLIGHTS_H
#define DMXLIGHTS_H

// Stage lights that follow the music over DMX512.
// Interfaces: Enttec DMX USB Pro (framed serial protocol) or an Open DMX / FTDI cable (raw 250 kbaud).
// Fixtures: RGB + dimmer pars (4 channels each: R, G, B, dimmer) from the start address.
// Effects: kick drum = brightness pulse, snare = white flash, each beat = next colour; stopped = dim warm light.

#include <QObject>
#include <QSerialPort>
#include <QTimer>
#include <QByteArray>
#include <QColor>

class DmxLights : public QObject
{
    Q_OBJECT
public:
    enum Mode { Off = 0, EnttecPro, OpenDmx };

    explicit DmxLights(QObject *parent = nullptr);
    ~DmxLights();

    // returns false (and the reason) when the port cannot be opened
    bool configure(Mode mode, const QString &port, int startChannel, int fixtures, QString *error = nullptr);
    Mode mode() const { return _mode; }
    bool isActive() const { return _mode != Off && serial.isOpen(); }

    void setPlaying(bool playing) { _playing = playing; }
    void kick();
    void snare();
    void beat(int beatIndex);
    void testFlash();           // full white for a moment (settings "Test" button)

private:
    void sendFrame();

    QSerialPort serial;
    QTimer frameTimer;
    Mode _mode = Off;
    int _start = 1;
    int _fixtures = 4;
    bool _playing = false;
    int _beat = -1;
    int _colorIndex = 0;
    float _pulse = 0.0f;        // 0..1, decays every frame
    float _white = 0.0f;
    bool _blackout = false;     // last frame before closing: everything off
    QByteArray dmx;             // start code + 512 channels
};

#endif // DMXLIGHTS_H
