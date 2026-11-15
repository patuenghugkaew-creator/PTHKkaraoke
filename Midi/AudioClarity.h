// "เสียงใส" for audio songs (YouTube, videos, MP3): runs in AudioBandMeter's DSP, before the mixer EQ.
//  - a little less mud (300 Hz -2 dB), a touch of presence (3 kHz +1 dB), air (high shelf 10 kHz +2.5 dB)
//  - an exciter that rebuilds the top a lossy copy lost: YouTube's AAC 128k has nothing above 16 kHz.
//    The 5.5..7.5 kHz part is turned into its 3rd harmonic (16.5..22.5 kHz), level-independent, and only
//    what lands above 15.5 kHz is added (nothing is added where the copy still has sound). It follows the song: it is strong when the 16..20 kHz band is missing
//    and fades out when the song already has it (Opus / AAC 256k copies), so good copies are not
//    made harsh.
// Switching on / off glides over ~50 ms (no click).
#ifndef AUDIOCLARITY_H
#define AUDIOCLARITY_H

#include <atomic>
#include <cmath>
#include <cstring>
#include <complex>
#include <vector>

class AudioClarity
{
public:
    static const int kMaxCh = 8;

    void setOn(bool on) { want.store(on); }
    bool isOn() const { return want.load(); }
    float exciterAmount() const { return exSm; }             // 0..1, for tests / logs
    double topRatioDb() const { return ratioDb; }            // 16.5..19.5 kHz vs 8..12 kHz

    // before the first process() for a stream (not while it runs)
    void setup(double fs, int channels)
    {
        rate = fs;
        chans = channels < kMaxCh ? channels : kMaxCh;
        for (int c = 0; c < kMaxCh; ++c) {
            mud[c].peak(300, 0.9, -2.0, fs);
            pres[c].peak(3000, 0.8, 1.0, fs);
            air[c].highShelf(10000, 2.5, fs);
            hpA[c].highpass(5500, 0.7071, fs);
            hpB[c].highpass(5500, 0.7071, fs);
            lpA[c].lowpass(7500, 0.7071, fs);
            lpB[c].lowpass(7500, 0.7071, fs);
            hpC[c].highpass(15500, 0.7071, fs);
            hpD[c].highpass(15500, 0.7071, fs);
            hpE[c].highpass(15500, 0.7071, fs);
            env[c] = 0.0f;
        }
        fill = 0;
        sinceFft = 0;
        ratioDb = 0.0;
        haveRatio = false;
        exAmt = 0.0;
        exSm = 0.0f;
        mix = want.load() ? 1.0f : 0.0f;
        envCoef = float(1.0 - std::exp(-1.0 / (0.010 * fs)));      // 10 ms
        mixStep = float(1.0 / (0.05 * fs));                         // 50 ms glide
    }

    // interleaved float samples, in place
    void process(float *s, int frames, int ch)
    {
        const float target = want.load() ? 1.0f : 0.0f;
        if (mix == 0.0f && target == 0.0f)
            return;
        if (ch > chans)
            ch = chans;

        // how much top is missing: twice a second a spectrum of the input (mono of the first pair)
        for (int i = 0; i < frames; ++i) {
            const float *f = s + size_t(i) * size_t(ch);
            ring[fill] = ch >= 2 ? 0.5f * (f[0] + f[1]) : f[0];
            fill = (fill + 1) % kFft;
        }
        sinceFft += frames;
        if (sinceFft >= int(rate * 0.5)) {
            sinceFft = 0;
            measureTop();
        }
        const float exStep = float(frames) / float(rate * 0.5);   // amount glides over ~0.5 s
        const float exT = float(exAmt);
        exSm = exSm < exT ? (std::min)(exT, exSm + exStep) : (std::max)(exT, exSm - exStep);
        const float exGain = 4.5f * exSm;       // set so a 16 kHz-cut copy gets back about the top a full copy has

        for (int i = 0; i < frames; ++i) {
            if (mix != target)
                mix = target > mix ? (std::min)(target, mix + mixStep) : (std::max)(target, mix - mixStep);
            float *f = s + size_t(i) * size_t(ch);
            for (int c = 0; c < ch; ++c) {
                const float x = f[c];
                float y = air[c].run(pres[c].run(mud[c].run(x)));
                // exciter: odd harmonics of 5.5..7.5 kHz, divided by its own level so it follows the music
                const float h = lpB[c].run(lpA[c].run(hpB[c].run(hpA[c].run(x))));
                const float a = std::fabs(h);
                // peak follower (instant up, ~10 ms down): a / env never passes 1, so no spikes
                env[c] = a > env[c] ? a : env[c] - (env[c] - a) * envCoef;
                const float e = h * a / (env[c] + 1e-4f);
                y += exGain * hpE[c].run(hpD[c].run(hpC[c].run(e)));
                f[c] = x + mix * (y - x);
            }
        }
    }

private:
    static const int kFft = 4096;

    // energy 16.5..19.5 kHz vs 8..12 kHz: about -40 dB on AAC 128k (nothing above 16 kHz),
    // -14 dB on a full-range copy. The exciter works fully below -36 dB and not above -16 dB.
    void measureTop()
    {
        if (rate < 40000)
            return;
        std::vector<std::complex<float>> a(kFft);
        for (int i = 0; i < kFft; ++i) {
            const float w = 0.5f - 0.5f * std::cos(6.2831853f * i / (kFft - 1));
            a[i] = ring[(fill + i) % kFft] * w;
        }
        fft(a);
        const double bin = rate / kFft;
        double mid = 0, top = 0;
        for (int k = int(8000 / bin); k < int(12000 / bin); ++k) mid += std::norm(a[k]);
        for (int k = int(16500 / bin); k < int((std::min)(19500.0, rate * 0.45) / bin); ++k) top += std::norm(a[k]);
        if (mid < 1e-9)
            return;         // quiet: keep the last decision
        const double r = 10.0 * std::log10((top + 1e-20) / mid);
        ratioDb = haveRatio ? ratioDb + (r - ratioDb) * 0.3 : r;
        haveRatio = true;
        const double amt = (-ratioDb - 16.0) / 20.0;
        exAmt = amt < 0 ? 0 : (amt > 1 ? 1 : amt);
    }
    static void fft(std::vector<std::complex<float>> &a)
    {
        const int n = int(a.size());
        for (int i = 1, j = 0; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[i], a[j]);
        }
        for (int len = 2; len <= n; len <<= 1) {
            const float ang = -6.2831853f / len;
            const std::complex<float> wl(std::cos(ang), std::sin(ang));
            for (int i = 0; i < n; i += len) {
                std::complex<float> w(1, 0);
                for (int j = 0; j < len / 2; ++j) {
                    const std::complex<float> u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }

    struct Bq {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void set(double B0, double B1, double B2, double A0, double A1, double A2)
        {
            b0 = float(B0 / A0); b1 = float(B1 / A0); b2 = float(B2 / A0);
            a1 = float(A1 / A0); a2 = float(A2 / A0);
            z1 = z2 = 0;
        }
        void peak(double f, double q, double db, double fs)
        {
            const double A = std::pow(10.0, db / 40.0), w = 6.283185307179586 * f / fs;
            const double al = std::sin(w) / (2 * q), cw = std::cos(w);
            set(1 + al * A, -2 * cw, 1 - al * A, 1 + al / A, -2 * cw, 1 - al / A);
        }
        void highShelf(double f, double db, double fs)
        {
            if (f > fs * 0.45) f = fs * 0.45;
            const double A = std::pow(10.0, db / 40.0), w = 6.283185307179586 * f / fs;
            const double cw = std::cos(w), sa = 2 * std::sqrt(A) * std::sin(w) / 2.0 * std::sqrt(2.0);
            set(A * ((A + 1) + (A - 1) * cw + sa), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sa),
                (A + 1) - (A - 1) * cw + sa, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sa);
        }
        void highpass(double f, double q, double fs)
        {
            if (f > fs * 0.45) f = fs * 0.45;
            const double w = 6.283185307179586 * f / fs, al = std::sin(w) / (2 * q), cw = std::cos(w);
            set((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + al, -2 * cw, 1 - al);
        }
        void lowpass(double f, double q, double fs)
        {
            if (f > fs * 0.45) f = fs * 0.45;
            const double w = 6.283185307179586 * f / fs, al = std::sin(w) / (2 * q), cw = std::cos(w);
            set((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + al, -2 * cw, 1 - al);
        }
        void bandpass(double f, double q, double fs)
        {
            const double w = 6.283185307179586 * f / fs, al = std::sin(w) / (2 * q), cw = std::cos(w);
            set(al, 0, -al, 1 + al, -2 * cw, 1 - al);
        }
        float run(float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    std::atomic<bool> want{true};
    Bq mud[kMaxCh], pres[kMaxCh], air[kMaxCh], hpA[kMaxCh], hpB[kMaxCh], lpA[kMaxCh], lpB[kMaxCh];
    Bq hpC[kMaxCh], hpD[kMaxCh], hpE[kMaxCh];
    float env[kMaxCh] = {};
    float ring[kFft] = {};
    int fill = 0, sinceFft = 0;
    bool haveRatio = false;
    double ratioDb = 0, exAmt = 0, rate = 44100;
    float exSm = 0.0f, mix = 1.0f, mixStep = 0.001f, envCoef = 0.01f;
    int chans = 2;
};

#endif // AUDIOCLARITY_H
