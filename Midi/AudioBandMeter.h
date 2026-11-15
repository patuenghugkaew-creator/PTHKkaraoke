// Mixer for audio songs (YouTube, downloaded videos, MP3) on the synth mixer.
// Those songs are one finished mix, with no per-instrument notes like MIDI, so each instrument strip
// stands for the part of the sound where that instrument mostly lives (an estimate: bass ~90 Hz,
// kick ~60 Hz, sax ~1 kHz, hi-hat ~9 kHz, ...):
//  - its fader raises / lowers that part of the sound (mute = cut by 24 dB); audio songs have their own
//    fader set, kept apart from the MIDI one (SynthMixerDialog::setAudioMode).
//    The faders drive a fixed 18-band graphic EQ (half an octave apart, 40 Hz .. 14.5 kHz). Each strip
//    reaches the bands around its frequency; where strips overlap the strongest one wins (two strips at
//    +6 dB give +6 dB there, not +12), so pushing many faders up cannot pile the gain up.
//  - its meter follows that band, compared with the whole song (a strong band shows high) and with
//    its own last ~1.5 s (hits flash), so the strips move with the music instead of all sitting high.
// MASTER shows the real overall level.
// One BASS DSP on the playing source does both; the GUI reads the levels ~25 times a second.
#ifndef AUDIOBANDMETER_H
#define AUDIOBANDMETER_H

#include <bass.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <algorithm>
#include "AudioClarity.h"

class AudioBandMeter
{
public:
    static const int kStrips = 42;      // InstrumentType::Piano .. PercussionEtc
    static const int kMaxCh = 8;
    static const int kEqBands = 18;

    AudioBandMeter()
    {
        for (auto &l : level) l.store(0.0f);
        for (int s = 0; s < kStrips; ++s) { avg[s] = kNoAvg; gainDb[s] = 0.0f; }
        std::memset(pending, 0, sizeof(pending));
        std::memset(active, 0, sizeof(active));
        std::memset(z, 0, sizeof(z));
    }
    ~AudioBandMeter() { detach(); }

    // measure and EQ this channel (0 = none); a freed channel just stops calling the DSP
    void attach(DWORD handle)
    {
        if (handle == source)
            return;
        detach();
        if (!handle)
            return;
        BASS_CHANNELINFO ci;
        if (!BASS_ChannelGetInfo(handle, &ci) || !(ci.flags & BASS_SAMPLE_FLOAT))
            return;     // the DSP works on float samples
        chans = int(ci.chans) < kMaxCh ? int(ci.chans) : kMaxCh;
        {
            std::lock_guard<std::mutex> lk(mx);
            rate = ci.freq ? double(ci.freq) : 44100.0;
            makeCoefs();
            dirty.store(true);
        }
        std::memset(z, 0, sizeof(z));
        clarity.setup(rate, chans);
        for (int s = 0; s < kStrips; ++s) {
            meterBp[s][0].bandpass(table()[s].mf, table()[s].mq, rate);
            meterBp[s][1].bandpass(table()[s].mf, table()[s].mq, rate);
        }
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
        for (auto &l : level) l.store(0.0f);
        total.store(0.0f);
        for (int s = 0; s < kStrips; ++s) avg[s] = kNoAvg;
        avgTotal = kNoAvg;
    }
    DWORD attached() const { return source; }

    // "เสียงใส" (see AudioClarity.h), any thread
    void setClear(bool on) { clarity.setOn(on); }
    bool isClear() const { return clarity.isOn(); }

    // AI stems (4 ch): how much of the vocal channels is heard (the vocal cut)
    void setStemVocalGain(float g) { vocalGain.store(g); }

    // EQ of one strip in dB (0 = flat), -24 .. +12. Called from the GUI thread.
    void setStripGain(int strip, float db)
    {
        if (strip < 0 || strip >= kStrips)
            return;
        db = db < -24.0f ? -24.0f : (db > 12.0f ? 12.0f : db);
        if (std::fabs(db) < 0.05f)
            db = 0.0f;
        std::lock_guard<std::mutex> lk(mx);
        if (gainDb[strip] == db)
            return;
        gainDb[strip] = db;
        makeCoefs();
        dirty.store(true);
    }

    // the EQ curve now, in dB at each band centre (for tests / logs)
    void curve(double *out, double *freq = nullptr)
    {
        std::lock_guard<std::mutex> lk(mx);
        for (int k = 0; k < kEqBands; ++k) {
            const double f = (std::min)(bandFreq(k), rate * 0.45);
            double r = 0;
            for (int j = 0; j < kEqBands; ++j)
                if (pending[j].on)
                    r += dbAt(pending[j], f, rate);
            out[k] = r;
            if (freq)
                freq[k] = f;
        }
    }

    // GUI: one strip's meter 0..127 (call every ~40 ms: totalLevel() first, then each strip)
    int stripLevel(int strip)
    {
        const float r = level[strip].exchange(0.0f);
        if (r <= 1e-7f || avgTotal == kNoAvg)
            return 0;
        const float e = 20.0f * std::log10(r);
        if (avg[strip] == kNoAvg)
            avg[strip] = e;
        avg[strip] += (e - avg[strip]) * kAvgStep;
        float base = (avg[strip] - avgTotal + 30.0f) / 30.0f;       // a strong band sits high
        base = base < 0 ? 0 : (base > 1 ? 1 : base);
        float v = 0.15f + 0.6f * base + (e - avg[strip]) / 24.0f;    // hits flash
        v = v < 0 ? 0 : (v > 1 ? 1 : v);
        return int(v * 127.0f);
    }
    // MASTER: the real level, 0..127 over -48..0 dB. Also keeps the song's average for stripLevel().
    int totalLevel()
    {
        const float r = total.exchange(0.0f);
        if (r <= 1e-7f)
            return 0;
        const float e = 20.0f * std::log10(r);
        if (avgTotal == kNoAvg)
            avgTotal = e;
        avgTotal += (e - avgTotal) * kAvgStep;
        const float v = (e + 48.0f) / 48.0f * 127.0f;
        return v < 0 ? 0 : (v > 127 ? 127 : int(v));
    }

private:
    // where each instrument mostly lives: centre f and width q (like a peaking EQ); kind 3 = it also
    // reaches everything above f (cymbals); f2 = a second centre (0: none). mf / mq: the meter's band.
    struct Band { float f, q; int kind; float f2, q2; float mf, mq; };
    static const Band *table()
    {
        static const Band t[kStrips] = {
            {  500, 0.9f, 1,    0, 0,     500, 0.9f },   // Piano
            {  700, 0.8f, 1,    0, 0,     700, 0.8f },   // Organ
            { 1100, 1.0f, 1,    0, 0,    1100, 1.0f },   // Accordion
            { 2500, 1.2f, 1,    0, 0,    2500, 1.2f },   // Chromatic percussion
            { 3000, 1.2f, 1,    0, 0,    3000, 1.2f },   // Percussive
            {   90, 0.9f, 1,    0, 0,      90, 0.9f },   // Bass
            {  600, 1.0f, 1,    0, 0,     600, 1.0f },   // Guitar nylon
            { 2000, 1.0f, 1,    0, 0,    2000, 1.0f },   // Guitar steel
            {  800, 1.0f, 1,    0, 0,     800, 1.0f },   // Guitar jazz
            { 1800, 1.0f, 1,    0, 0,    1800, 1.0f },   // Guitar clean
            { 1500, 0.9f, 1,    0, 0,    1500, 0.9f },   // Overdriven guitar
            { 2200, 0.9f, 1,    0, 0,    2200, 0.9f },   // Distortion guitar
            { 3200, 1.2f, 1,    0, 0,    3200, 1.2f },   // Guitar harmonics
            { 1500, 1.0f, 1,    0, 0,    1500, 1.0f },   // Trumpet
            {  900, 0.9f, 1,    0, 0,     900, 0.9f },   // Brass
            { 1200, 1.0f, 1,    0, 0,    1200, 1.0f },   // Synth brass
            { 1000, 1.0f, 1,    0, 0,    1000, 1.0f },   // Saxophone
            { 1600, 1.2f, 1,    0, 0,    1600, 1.2f },   // Reed
            { 2500, 1.0f, 1,    0, 0,    2500, 1.0f },   // Pipe
            { 1200, 0.6f, 1,    0, 0,    1200, 0.6f },   // Strings
            {  800, 0.7f, 1,    0, 0,     800, 0.7f },   // Ensemble
            { 2000, 1.0f, 1,    0, 0,    2000, 1.0f },   // Synth lead
            {  600, 0.6f, 1,    0, 0,     600, 0.6f },   // Synth pad
            { 5000, 1.0f, 1,    0, 0,    5000, 1.0f },   // Synth effects
            { 1500, 1.2f, 1,    0, 0,    1500, 1.2f },   // Ethnic (phin, khaen, ...)
            { 6000, 1.0f, 1,    0, 0,    6000, 1.0f },   // Sound effects
            {   60, 1.4f, 1,    0, 0,      60, 1.4f },   // Kick
            {  220, 1.4f, 1, 4000, 1.2f, 4000, 1.2f },   // Snare: body + crack
            { 1800, 1.5f, 1,    0, 0,    1800, 1.5f },   // Side stick
            {  320, 1.4f, 1,    0, 0,     320, 1.4f },   // High tom
            {  200, 1.4f, 1,    0, 0,     200, 1.4f },   // Mid tom
            {  120, 1.4f, 1,    0, 0,     120, 1.4f },   // Low tom
            { 8000, 0.7f, 3,    0, 0,    9000, 1.0f },   // Hi-hat
            {  800, 2.0f, 1,    0, 0,     800, 2.0f },   // Cowbell
            {10000, 0.7f, 3,    0, 0,   11000, 1.0f },   // Crash
            { 6000, 1.2f, 1,    0, 0,    6000, 1.2f },   // Ride
            {  400, 1.4f, 1,    0, 0,     400, 1.4f },   // Bongo
            {  250, 1.4f, 1,    0, 0,     250, 1.4f },   // Conga
            {  600, 1.4f, 1,    0, 0,     600, 1.4f },   // Timbale
            { 5000, 2.0f, 1,    0, 0,    5000, 2.0f },   // Ching (small cymbals)
            { 7000, 1.2f, 1,    0, 0,    7000, 1.2f },   // Chap (Thai cymbals)
            { 3500, 1.0f, 1,    0, 0,    3500, 1.0f },   // Other percussion
        };
        return t;
    }

    struct Coef { double b0, b1, b2, a1, a2; int on; };

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

    static double bandFreq(int k) { return 40.0 * std::pow(2.0, k * 0.5); }    // 40 Hz .. 14.5 kHz
    static constexpr double kBandQ = 2.0;

    // RBJ peaking EQ
    static Coef peak(double f0, double q, double db, double fs)
    {
        const double A = std::pow(10.0, db / 40.0), w = 2.0 * 3.14159265358979 * f0 / fs;
        const double al = std::sin(w) / (2.0 * q), cw = std::cos(w), a0 = 1 + al / A;
        return { (1 + al * A) / a0, -2 * cw / a0, (1 - al * A) / a0, -2 * cw / a0, (1 - al / A) / a0, 1 };
    }
    static double dbAt(const Coef &p, double f, double fs)     // a filter's gain at f, in dB
    {
        const double w = 2.0 * 3.14159265358979 * f / fs, c1 = std::cos(w), s1 = std::sin(w);
        const double c2 = std::cos(2 * w), s2 = std::sin(2 * w);
        const double nr = p.b0 + p.b1 * c1 + p.b2 * c2, ni = -(p.b1 * s1 + p.b2 * s2);
        const double dr = 1 + p.a1 * c1 + p.a2 * c2, di = -(p.a1 * s1 + p.a2 * s2);
        return 10.0 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di));
    }

    // how much of a strip's gain reaches a band: 1 at its centre, 0.5 at the edge of its width
    static double reach(const Band &b, double fc)
    {
        auto bell = [fc](double f, double q) {
            const double bw = 2.0 / std::log(2.0) * std::asinh(1.0 / (2.0 * q));   // width in octaves
            const double d = std::log2(fc / f) / (bw * 0.5);
            return std::exp(-d * d * std::log(2.0));
        };
        double w = bell(b.f, b.q);
        if (b.kind == 3 && fc >= b.f)
            w = 1.0;
        if (b.f2 > 0)
            w = (std::max)(w, bell(b.f2, b.q2));
        return w;
    }

    // the faders -> the 18 band gains -> filter coefficients (mx held)
    void makeCoefs()
    {
        const double nyq = rate * 0.45;
        double target[kEqBands];
        bool any = false;
        for (int k = 0; k < kEqBands; ++k) {
            double up = 0, down = 0;          // the strongest boost and the deepest cut win
            for (int s = 0; s < kStrips; ++s) {
                if (gainDb[s] == 0.0f)
                    continue;
                const double c = gainDb[s] * reach(table()[s], bandFreq(k));
                if (c > up) up = c;
                if (c < down) down = c;
            }
            target[k] = up + down;
            if (std::fabs(target[k]) >= 0.05)
                any = true;
        }
        if (!any) {
            for (int k = 0; k < kEqBands; ++k)
                pending[k].on = 0;
            return;
        }
        // neighbouring bands overlap: correct the gains a few times so the curve hits the targets
        double g[kEqBands];
        Coef p[kEqBands];
        for (int k = 0; k < kEqBands; ++k)
            g[k] = target[k];
        for (int it = 0; it < 6; ++it) {
            for (int k = 0; k < kEqBands; ++k)
                p[k] = peak((std::min)(bandFreq(k), nyq), kBandQ, g[k], rate);
            for (int k = 0; k < kEqBands; ++k) {
                double resp = 0;
                for (int j = 0; j < kEqBands; ++j)
                    resp += dbAt(p[j], (std::min)(bandFreq(k), nyq), rate);
                g[k] += 0.6 * (target[k] - resp);
                g[k] = g[k] < -30 ? -30 : (g[k] > 18 ? 18 : g[k]);
            }
        }
        for (int k = 0; k < kEqBands; ++k) {
            pending[k] = peak((std::min)(bandFreq(k), nyq), kBandQ, g[k], rate);
            pending[k].on = std::fabs(g[k]) >= 0.02 ? 1 : 0;
        }
    }

    static void CALLBACK dspProc(HDSP, DWORD, void *buffer, DWORD length, void *user)
    {
        AudioBandMeter *m = static_cast<AudioBandMeter *>(user);
        float *s = static_cast<float *>(buffer);
        const int ch = m->chans > 0 ? m->chans : 2;
        const int frames = int(length / (sizeof(float) * DWORD(ch)));
        if (frames <= 0)
            return;

        // new fader settings: take them when the GUI is not writing (never wait here)
        if (m->dirty.load() && m->mx.try_lock()) {
            for (int k = 0; k < kEqBands; ++k) {
                if (m->pending[k].on && !m->active[k].on)
                    std::memset(m->z[k], 0, sizeof(m->z[k]));   // a band coming in starts clean
                const Coef &c = m->pending[k];
                m->active[k] = { float(c.b0), float(c.b1), float(c.b2), float(c.a1), float(c.a2), c.on };
            }
            m->dirty.store(false);
            m->mx.unlock();
        }

        m->clarity.process(s, frames, ch);      // "เสียงใส" first, then the faders' EQ

        // EQ: only the bands that are not flat
        for (int k = 0; k < kEqBands; ++k) {
            const FCoef c = m->active[k];
            if (!c.on)
                continue;
            for (int cch = 0; cch < ch; ++cch) {
                float z1 = m->z[k][cch][0], z2 = m->z[k][cch][1];
                float *p = s + cch;
                for (int f = 0; f < frames; ++f, p += ch) {
                    const float x = *p, y = c.b0 * x + z1;
                    z1 = c.b1 * x - c.a1 * y + z2;
                    z2 = c.b2 * x - c.a2 * y;
                    *p = y;
                }
                m->z[k][cch][0] = z1;
                m->z[k][cch][1] = z2;
            }
        }

        // meters, after the EQ
        const float vg = m->vocalGain.load();
        double acc[kStrips] = {}, all = 0;
        for (int i = 0; i < frames; ++i) {
            const float *f = s + size_t(i) * size_t(ch);
            float x;
            if (ch >= 4)
                x = 0.5f * (f[0] + f[1]) + vg * 0.5f * (f[2] + f[3]);   // music + heard vocals
            else if (ch >= 2)
                x = 0.5f * (f[0] + f[1]);
            else
                x = f[0];
            all += double(x) * x;
            for (int b = 0; b < kStrips; ++b) {
                const float y = m->meterBp[b][1].run(m->meterBp[b][0].run(x));   // 2 stages: sharper
                acc[b] += double(y) * y;
            }
        }
        for (int b = 0; b < kStrips; ++b) {
            const float r = float(std::sqrt(acc[b] / frames));
            if (r > m->level[b].load()) m->level[b].store(r);
        }
        const float r = float(std::sqrt(all / frames));
        if (r > m->total.load()) m->total.store(r);
    }

    struct FCoef { float b0, b1, b2, a1, a2; int on; };

    static constexpr float kNoAvg = -999.0f;
    static constexpr float kAvgStep = 0.04f / 1.5f;     // ~1.5 s average at 25 reads a second

    // GUI side
    float avg[kStrips];
    float avgTotal = kNoAvg;
    float gainDb[kStrips];
    Coef pending[kEqBands];
    std::mutex mx;
    std::atomic<bool> dirty{false};

    // DSP side
    FCoef active[kEqBands];
    float z[kEqBands][kMaxCh][2];
    Biquad meterBp[kStrips][2];
    std::atomic<float> level[kStrips];
    std::atomic<float> total{0.0f};
    std::atomic<float> vocalGain{1.0f};
    AudioClarity clarity;
    double rate = 44100.0;
    DWORD source = 0;
    HDSP dsp = 0;
    int chans = 2;
};

#endif // AUDIOBANDMETER_H
