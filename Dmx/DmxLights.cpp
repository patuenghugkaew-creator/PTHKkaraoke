#include "DmxLights.h"

#include <QThread>

static const QColor kPalette[] = {
    QColor(255, 0, 0), QColor(0, 0, 255), QColor(0, 255, 0), QColor(255, 0, 255),
    QColor(255, 160, 0), QColor(0, 255, 255), QColor(255, 255, 0), QColor(140, 0, 255),
};
static const int kPaletteSize = int(sizeof(kPalette) / sizeof(kPalette[0]));

DmxLights::DmxLights(QObject *parent) : QObject(parent), dmx(513, 0)
{
    frameTimer.setInterval(40);         // 25 frames per second
    connect(&frameTimer, &QTimer::timeout, this, &DmxLights::sendFrame);
}

DmxLights::~DmxLights()
{
    if (isActive()) {                   // lights off when the program ends
        _blackout = true;
        sendFrame();
    }
    serial.close();
}

bool DmxLights::configure(Mode mode, const QString &port, int startChannel, int fixtures, QString *error)
{
    frameTimer.stop();
    serial.close();
    _mode = Off;
    _start = qBound(1, startChannel, 509);
    _fixtures = qBound(1, fixtures, (512 - _start + 1) / 4);

    if (mode == Off || port.isEmpty())
        return true;

    serial.setPortName(port);
    if (mode == OpenDmx) {
        serial.setBaudRate(250000);
        serial.setStopBits(QSerialPort::TwoStop);
    } else {
        serial.setBaudRate(115200);     // ignored by the Pro (USB), but must be a valid value
        serial.setStopBits(QSerialPort::OneStop);
    }
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setFlowControl(QSerialPort::NoFlowControl);

    if (!serial.open(QIODevice::WriteOnly)) {
        if (error) *error = serial.errorString();
        return false;
    }
    _mode = mode;
    frameTimer.start();
    return true;
}

void DmxLights::kick()
{
    _pulse = 1.0f;
}

void DmxLights::snare()
{
    _white = 1.0f;
}

void DmxLights::beat(int beatIndex)
{
    if (beatIndex == _beat)
        return;
    _beat = beatIndex;
    _colorIndex = (_colorIndex + 1) % kPaletteSize;
}

void DmxLights::testFlash()
{
    _white = 1.0f;
    _pulse = 1.0f;
}

void DmxLights::sendFrame()
{
    if (_mode == Off || !serial.isOpen())
        return;

    // compute the fixtures
    if (_blackout)
        dmx.fill(0);
    for (int f = 0; f < _fixtures && !_blackout; f++) {
        const int ch = _start + f * 4;          // 1-based DMX address; dmx[0] is the start code
        int r, g, b, dim;
        if (_playing) {
            // neighbouring fixtures get neighbouring colours, so the stage is not one flat colour
            const QColor c = kPalette[(_colorIndex + f) % kPaletteSize];
            const float w = _white;
            r = int(c.red() * (1 - w) + 255 * w);
            g = int(c.green() * (1 - w) + 255 * w);
            b = int(c.blue() * (1 - w) + 255 * w);
            dim = int(110 + 145 * qMax(_pulse, _white));
        } else {
            r = 255; g = 120; b = 40; dim = 40;  // waiting between songs: dim warm light
        }
        dmx[ch]     = char(qBound(0, r, 255));
        dmx[ch + 1] = char(qBound(0, g, 255));
        dmx[ch + 2] = char(qBound(0, b, 255));
        dmx[ch + 3] = char(qBound(0, dim, 255));
    }
    _pulse *= 0.80f;
    _white *= 0.70f;

    if (_mode == EnttecPro) {
        // label 6 = "Output Only Send DMX Packet": 0x7E, label, length LSB/MSB, data (start code + channels), 0xE7
        QByteArray pkt;
        pkt.append(char(0x7E));
        pkt.append(char(6));
        pkt.append(char(dmx.size() & 0xFF));
        pkt.append(char((dmx.size() >> 8) & 0xFF));
        pkt.append(dmx);
        pkt.append(char(0xE7));
        serial.write(pkt);
    } else {
        // Open DMX: break (>88 us), mark after break, then start code + 512 channels at 250 kbaud 8N2
        serial.setBreakEnabled(true);
        QThread::usleep(110);
        serial.setBreakEnabled(false);
        QThread::usleep(12);
        serial.write(dmx);
    }
    serial.flush();
}
