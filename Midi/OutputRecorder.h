#ifndef OUTPUTRECORDER_H
#define OUTPUTRECORDER_H

// Records the main mixer output (everything you hear, after the master effects) to a WAV file.
// A BASS DSP on the main mixer copies the audio into a queue; a writer thread saves it to disk,
// so a slow disk never makes the sound stutter. Output is always stereo; 8-channel mixers are
// folded down like a normal stereo sound card would do.  16 / 24-bit PCM or 32-bit float.
// Header-only, no Q_OBJECT.

#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QByteArray>
#include <QString>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdint>
#include <cstring>

#include <bass.h>
#include <cmath>
#include <vector>
#include "Midi/AsioOutput.h"
#include "Midi/MicInput.h"

class OutputRecorder
{
public:
    static OutputRecorder &instance()
    {
        static OutputRecorder r;
        return r;
    }

    ~OutputRecorder() { stop(); }

    bool isRecording() const { return running.load(); }
    double seconds() const { return rate > 0 ? double(frames.load()) / rate : 0.0; }
    qint64 bytes() const { return qint64(frames.load()) * 2 * (bits / 8); }
    // extra gain for the file in dB (set before start), and the highest peak so far (0..1+, 1.0 = full scale)
    void setGainDb(double db) { userGain = std::pow(10.0, db / 20.0); }
    // TPDF dither (+-1 LSB triangular noise) before the 16 / 24-bit rounding: no harsh quantisation distortion on quiet tails
    void setDither(bool on) { ditherOn = on; }
    // karaoke recording: also mix the microphone (MicInput, must be running) into the file
    void setMixMic(bool on, double gain = 1.0) { mixMic = on; micGain = float(gain); }
    float peakValue() const { return peak.load(); }
    QString path() const { return filePath; }

    // mixer = the main BASS mixer handle, bits = 16, 24 or 32 (float)
    bool start(DWORD mixer, const QString &file, int bitDepth, QString *error = nullptr)
    {
        stop();

        BASS_CHANNELINFO ci;
        if (!mixer || !BASS_ChannelGetInfo(mixer, &ci)) {
            if (error) *error = QStringLiteral("audio output is not running");
            return false;
        }
        chans = int(ci.chans);
        inFloat = (ci.flags & BASS_SAMPLE_FLOAT) != 0;
        applyVol = (ci.flags & BASS_STREAM_DECODE) == 0;   // a playing mixer applies its volume AFTER the DSP, so do it here
        rate = int(ci.freq);
        bits = (bitDepth == 24 || bitDepth == 32) ? bitDepth : 16;

        out.setFileName(file);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error) *error = QStringLiteral("cannot create the file");
            return false;
        }
        QByteArray hdr(44, 0);
        out.write(hdr);   // placeholder, filled in when recording stops

        filePath = file;
        frames = 0;
        peak = 0.0f;
        pending.clear();
        chan = mixer;
        running = true;
        writer = std::thread([this]() { writeLoop(); });

        // lowest priority: runs after the master effects, so the file is what you hear
        dsp = BASS_ChannelSetDSP(mixer, &OutputRecorder::dspProc, this, -1000);
        if (!dsp) {
            running = false;
            writer.join();
            out.close();
            QFile::remove(file);
            if (error) *error = QStringLiteral("cannot attach to the audio output");
            return false;
        }
        return true;
    }

    void stop()
    {
        if (!running.load())
            return;
        if (dsp)
            BASS_ChannelRemoveDSP(chan, dsp);
        dsp = 0;
        running = false;
        if (writer.joinable())
            writer.join();

        // fill in the WAV header
        const quint32 dataSize = quint32(frames.load() * 2 * (bits / 8));
        QByteArray h(44, 0);
        auto put32 = [&h](int pos, quint32 v) { for (int i = 0; i < 4; i++) h[pos + i] = char((v >> (8 * i)) & 0xff); };
        auto put16 = [&h](int pos, quint16 v) { h[pos] = char(v & 0xff); h[pos + 1] = char(v >> 8); };
        std::memcpy(h.data(), "RIFF", 4);
        put32(4, 36 + dataSize);
        std::memcpy(h.data() + 8, "WAVEfmt ", 8);
        put32(16, 16);
        put16(20, bits == 32 ? 3 : 1);                 // 3 = IEEE float, 1 = PCM
        put16(22, 2);
        put32(24, quint32(rate));
        put32(28, quint32(rate * 2 * (bits / 8)));
        put16(32, quint16(2 * (bits / 8)));
        put16(34, quint16(bits));
        std::memcpy(h.data() + 36, "data", 4);
        put32(40, dataSize);
        out.seek(0);
        out.write(h);
        out.close();
    }

private:
    OutputRecorder() = default;

    static void CALLBACK dspProc(HDSP, DWORD, void *buffer, DWORD length, void *user)
    {
        static_cast<OutputRecorder *>(user)->feed(buffer, length);
    }

    float sample(const void *buf, size_t index) const
    {
        return inFloat ? static_cast<const float *>(buf)[index]
                       : static_cast<const int16_t *>(buf)[index] / 32768.0f;
    }

    void feed(const void *buf, DWORD length)
    {
        if (!running.load() || chans < 1)
            return;
        const size_t unit = inFloat ? 4 : 2;
        const size_t n = length / (unit * size_t(chans));
        if (!n)
            return;
        const int ob = bits / 8;
        QByteArray o;
        o.resize(int(n * 2 * size_t(ob)));
        char *p = o.data();
        const float k = 0.70710678f;
        // the master volume / headroom of the mixer, which is applied after this DSP: a playing mixer has it as its
        // volume attribute, the decoding mixer behind ASIO gets it from AsioOutput (same value the ASIO feed uses)
        float vol = 1.0f;
        if (applyVol) {
            if (!BASS_ChannelGetAttribute(chan, BASS_ATTRIB_VOL, &vol))
                vol = 1.0f;
        } else if (AsioOutput::isActive()) {
            vol = AsioOutput::gain();
        }
        vol *= float(userGain);
        float pk = peak.load();

        if (mixMic.load()) {
            micBuf.resize(n);
            MicInput::get().pop(micBuf.data(), int(n));
        }

        for (size_t i = 0; i < n; i++) {
            const size_t b = i * size_t(chans);
            float l, r;
            if (chans == 1) {
                l = r = sample(buf, b);
            } else {
                l = sample(buf, b);
                r = sample(buf, b + 1);
                if (chans == 4) {
                    l += k * sample(buf, b + 2);
                    r += k * sample(buf, b + 3);
                } else if (chans >= 6) {
                    const float c = k * (sample(buf, b + 2) + sample(buf, b + 3));   // centre + LFE
                    l += c + k * sample(buf, b + 4);
                    r += c + k * sample(buf, b + 5);
                    if (chans >= 8) {
                        l += k * sample(buf, b + 6);
                        r += k * sample(buf, b + 7);
                    }
                }
            }
            const float mic = mixMic.load() ? micBuf[i] * micGain : 0.0f;
            for (int s = 0; s < 2; s++) {
                float v = (s == 0 ? l : r) * vol + mic;
                if (std::fabs(v) > pk) pk = std::fabs(v);
                if (bits == 32) {
                    std::memcpy(p, &v, 4);
                } else {
                    if (ditherOn) {
                        const float lsb = (bits == 16) ? (1.0f / 32768.0f) : (1.0f / 8388608.0f);
                        v += (nextRand() - nextRand()) * lsb;
                    }
                    if (v > 1.0f) v = 1.0f;
                    if (v < -1.0f) v = -1.0f;
                    if (bits == 16) {
                        const int16_t q = int16_t(v * 32767.0f + (v < 0 ? -0.5f : 0.5f));
                        std::memcpy(p, &q, 2);
                    } else {
                        const int32_t q = int32_t(v * 8388607.0f + (v < 0 ? -0.5f : 0.5f));
                        p[0] = char(q & 0xff);
                        p[1] = char((q >> 8) & 0xff);
                        p[2] = char((q >> 16) & 0xff);
                    }
                }
                p += ob;
            }
        }
        peak = pk;
        {
            QMutexLocker lk(&mutex);
            pending.append(o);
        }
        frames += n;
    }

    // uniform 0..1 (xorshift32; the audio thread is the only user)
    float nextRand()
    {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return float(rngState >> 8) * (1.0f / 16777216.0f);
    }

    void writeLoop()
    {
        while (true) {
            QByteArray chunk;
            {
                QMutexLocker lk(&mutex);
                chunk.swap(pending);
            }
            if (!chunk.isEmpty())
                out.write(chunk);
            else if (!running.load())
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        // one last drain after the DSP was removed
        QByteArray chunk;
        {
            QMutexLocker lk(&mutex);
            chunk.swap(pending);
        }
        if (!chunk.isEmpty())
            out.write(chunk);
    }

    std::atomic<bool> running{false};
    std::atomic<quint64> frames{0};
    std::thread writer;
    QMutex mutex;
    QByteArray pending;
    QFile out;
    QString filePath;
    DWORD chan = 0;
    HDSP dsp = 0;
    bool ditherOn = false;
    uint32_t rngState = 2463534242u;
    int chans = 2;
    int rate = 44100;
    int bits = 16;
    bool inFloat = true;
    bool applyVol = true;
    double userGain = 1.0;
    std::atomic<bool> mixMic{false};
    float micGain = 1.0f;
    std::vector<float> micBuf;                  // audio thread only
    std::atomic<float> peak{0.0f};
};

#endif // OUTPUTRECORDER_H
