// Level meter for audio songs (YouTube, downloaded videos, MP3) on the synth mixer.
// Those songs are one finished mix, with no per-instrument notes like MIDI, so the mixer's
// instrument strips are driven by frequency bands instead (an estimate: the bass strip
// follows the low band, cymbals the top band, ...). MASTER shows the real overall level.
// A BASS DSP on the playing source measures the bands; the GUI reads them ~25 times a second.
#ifndef AUDIOBANDMETER_H
#define AUDIOBANDMETER_H

#include <bass.h>
#include <atomic>
#include <cmath>

class AudioBandMeter
{
public:
    static const int kBands = 5;    // low, low-mid, mid, high-mid, high

    AudioBandMeter() { for (auto &b : level) b.store(0.0f); }
    ~AudioBandMeter() { detach(); }

    // measure this channel (0 = none); a freed channel just stops calling the DSP
    void attach(DWORD handle)
    {
        if (handle == source)
            return;
        detach();
        if (!handle)
            return;
        BASS_CHANNELINFO ci;
        if (!BASS_ChannelGetInfo(handle, &ci) || !(ci.flags & BASS_SAMPLE_FLOAT))
            return;     // the meter reads float samples
        chans = int(ci.chans);
        setRate(ci.freq ? ci.freq : 44100);
        dsp = BASS_ChannelSetDSP(handle, &AudioBandMeter::dspProc, this, -100);   // after the vocal cut
        if (dsp)
            source = handle;
    }
    void detach()
    {
        if (source && dsp)
            BASS_ChannelRemoveDSP(source, dsp);
        source = 0;
        dsp = 0;
        for (auto &b : level) b.store(0.0f);
        total.store(0.0f);
    }
    DWORD attached() const { return source; }

    // AI stems (4 ch): how much of the vocal channels is heard (the vocal cut)
    void setStemVocalGain(float g) { vocalGain.store(g); }

    // 0..127 like a MIDI velocity, peak since the last read
    int bandLevel(int b) { return toVel(level[b].exchange(0.0f), bandBoost[b]); }
    int totalLevel() { return toVel(total.exchange(0.0f), 0.0f); }

private:
    struct Biquad {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void bandpass(double f0, double q, double fs)
        {
            const double w = 2.0 * 3.14159265358979 * f0 / fs, al = std::sin(w) / (2.0 * q), a0 = 1.0 + al;
            b0 = float(al / a0); b1 = 0; b2 = float(-al / a0);
            a1 = float(-2.0 * std::cos(w) / a0); a2 = float((1.0 - al) / a0);
            z1 = z2 = 0;
        }
        float run(float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void setRate(DWORD fs)
    {
        static const double centre[kBands] = { 120, 420, 1300, 3800, 9000 };
        static const double q[kBands] = { 0.9, 1.0, 1.0, 1.0, 1.0 };
        for (int b = 0; b < kBands; ++b)
            band[b].bandpass(centre[b], q[b], double(fs));
    }

    static int toVel(float rms, float boostDb)
    {
        if (rms <= 1e-6f)
            return 0;
        const float db = 20.0f * std::log10(rms) + boostDb;       // about -48 .. 0 dB
        const float v = (db + 48.0f) / 48.0f * 127.0f;
        return v < 0 ? 0 : (v > 127 ? 127 : int(v));
    }

    static void CALLBACK dspProc(HDSP, DWORD, void *buffer, DWORD length, void *user)
    {
        AudioBandMeter *m = static_cast<AudioBandMeter *>(user);
        const float *s = static_cast<const float *>(buffer);
        const int ch = m->chans > 0 ? m->chans : 2;
        const int frames = int(length / (sizeof(float) * DWORD(ch)));
        if (frames <= 0)
            return;
        const float vg = m->vocalGain.load();
        double acc[kBands] = { 0, 0, 0, 0, 0 }, all = 0;
        for (int i = 0; i < frames; ++i) {
            const float *f = s + size_t(i) * size_t(ch);
            float x;
            if (ch >= 4)
                x = 0.5f * (f[0] + f[1]) + vg * 0.5f * (f[2] + f[3]);   // music + heard vocals
            else if (ch == 2)
                x = 0.5f * (f[0] + f[1]);
            else
                x = f[0];
            all += double(x) * x;
            for (int b = 0; b < kBands; ++b) {
                const float y = m->band[b].run(x);
                acc[b] += double(y) * y;
            }
        }
        for (int b = 0; b < kBands; ++b) {
            const float r = float(std::sqrt(acc[b] / frames));
            if (r > m->level[b].load()) m->level[b].store(r);
        }
        const float r = float(std::sqrt(all / frames));
        if (r > m->total.load()) m->total.store(r);
    }

    // narrower / quieter bands read a little low: lift them so the strips move alike
    const float bandBoost[kBands] = { 3.0f, 4.0f, 6.0f, 9.0f, 12.0f };

    Biquad band[kBands];
    std::atomic<float> level[kBands];
    std::atomic<float> total{0.0f};
    std::atomic<float> vocalGain{1.0f};
    DWORD source = 0;
    HDSP dsp = 0;
    int chans = 2;
};

#endif // AUDIOBANDMETER_H
