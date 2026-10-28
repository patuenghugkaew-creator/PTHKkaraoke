#ifndef MASTERDSP_H
#define MASTERDSP_H

// PRO sound-quality processing for the main output mixer. Header-only, no Qt / BASS dependency (plain C++11), so it
// can be tested on its own. The chain, in this order:
//
//   1. Mastering chain  : 3-band compressor (subtractive crossovers: the bands add up to the input exactly),
//                         tone per band, harmonic exciter on the highs, stereo width (mid / side)
//   2. Auto level       : K-weighted loudness (ITU-R BS.1770 / EBU R128 filters, 400 ms momentary + 3 s short-term);
//                         a slow gain (max 1.5 dB per second) moves the short-term loudness towards the target
//   3. Limiter          : look-ahead brick-wall limiter (1.5 ms), optional true-peak detection (4x oversampled),
//                         optional soft clip in front of it, hard safety clip at the ceiling
//
// The mixer applies its master volume AFTER a DSP, so the limiter works against ceiling / master gain (the caller
// passes the gain that will be applied after the DSP): what comes out of the speakers never exceeds the ceiling.
//
// All the parameters can be changed from the GUI thread at any time (picked up by the audio thread between blocks).

#include <algorithm>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>
#include "../PTHKCore/PTHKCore.h"

namespace MasterDsp {

const double kPi = 3.14159265358979323846;

// MasterDsp::Params and Meter defined in PTHKCore.h

// presets of the mastering chain (only the chain's own fields are changed)
inline int presetCount() { return 5; }

inline const char *presetName(int i)
{
    static const char *n[] = { "Karaoke Balanced", "Bright", "Warm", "Club", "Vocal Focus" };
    return (i >= 0 && i < 5) ? n[i] : "";
}

inline void applyPreset(Params &p, int i)
{
    struct P { float comp, lo, mi, hi, ex, w; };
    static const P t[5] = {
        { 0.35f,  1.0f,  0.0f,  1.5f, 0.15f, 1.10f },   // Karaoke Balanced
        { 0.30f,  0.0f,  0.5f,  3.0f, 0.30f, 1.15f },   // Bright
        { 0.35f,  2.5f,  0.5f, -1.5f, 0.00f, 1.00f },   // Warm
        { 0.60f,  3.0f, -0.5f,  2.0f, 0.25f, 1.30f },   // Club
        { 0.40f, -1.0f,  2.0f,  1.5f, 0.20f, 1.00f }    // Vocal Focus
    };
    if (i < 0 || i >= 5)
        return;
    p.compAmount = t[i].comp;
    p.lowDb = t[i].lo;
    p.midDb = t[i].mi;
    p.highDb = t[i].hi;
    p.exciter = t[i].ex;
    p.width = t[i].w;
}

// ---------------------------------------------------------------------------------------------------------------
namespace detail {

inline double dbToLin(double db) { return std::pow(10.0, db / 20.0); }
inline double linToDb(double x) { return 20.0 * std::log10(x > 1e-12 ? x : 1e-12); }

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    inline double process(double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
    void set(double B0, double B1, double B2, double A1, double A2) { b0 = B0; b1 = B1; b2 = B2; a1 = A1; a2 = A2; }

    void lowpass(double fc, double fs, double q)
    {
        fc = (std::min)(fc, fs * 0.45);
        const double w = 2.0 * kPi * fc / fs, c = std::cos(w), al = std::sin(w) / (2.0 * q), a0 = 1.0 + al;
        set((1.0 - c) / 2.0 / a0, (1.0 - c) / a0, (1.0 - c) / 2.0 / a0, -2.0 * c / a0, (1.0 - al) / a0);
    }
    void highpass(double fc, double fs, double q)
    {
        fc = (std::min)(fc, fs * 0.45);
        const double w = 2.0 * kPi * fc / fs, c = std::cos(w), al = std::sin(w) / (2.0 * q), a0 = 1.0 + al;
        set((1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, -2.0 * c / a0, (1.0 - al) / a0);
    }
};

// 4th order Butterworth low-pass = two biquads
struct Lowpass4
{
    Biquad a, b;
    void setup(double fc, double fs) { a.lowpass(fc, fs, 0.5411961); b.lowpass(fc, fs, 1.3065630); }
    inline double process(double x) { return b.process(a.process(x)); }
    void reset() { a.reset(); b.reset(); }
};

// ITU-R BS.1770 K-weighting (shelf + RLB high-pass), coefficients for any sample rate
struct KWeight
{
    Biquad s1, s2;
    void setup(double fs)
    {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(kPi * f0 / fs);
            const double Vh = std::pow(10.0, G / 20.0);
            const double Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s1.set((Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                   2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0);
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan(kPi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            s2.set(1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0);
        }
    }
    inline double process(double x) { return s2.process(s1.process(x)); }
    void reset() { s1.reset(); s2.reset(); }
};

struct BandComp
{
    double env = 0.0;
    double att = 0.0, rel = 0.0;
    double thrDb = -20.0;
    void setup(double attMs, double relMs, double thr, double fs)
    {
        att = std::exp(-1.0 / (attMs * 0.001 * fs));
        rel = std::exp(-1.0 / (relMs * 0.001 * fs));
        thrDb = thr;
        env = 0.0;
    }
    // level = linked absolute level of the band; returns the linear gain
    inline double process(double level, double ratio)
    {
        env = (level > env) ? att * env + (1.0 - att) * level : rel * env + (1.0 - rel) * level;
        if (ratio <= 1.0001)
            return 1.0;
        const double e = linToDb(env);
        if (e <= thrDb)
            return 1.0;
        return dbToLin(-(e - thrDb) * (1.0 - 1.0 / ratio));
    }
};

inline float softClipTo(float x, float ceil)
{
    // gentle saturation: linear up to 70 % of the ceiling, then a tanh knee that reaches the ceiling
    const float k = 0.7f * ceil, ax = std::fabs(x);
    if (ax <= k)
        return x;
    const float y = k + (ceil - k) * std::tanh((ax - k) / (ceil - k));
    return x < 0 ? -y : y;
}

} // namespace detail

// ---------------------------------------------------------------------------------------------------------------
class Chain
{
public:
    Chain() {}


    // set the format (call before the first process(), from a non-audio thread, while no audio is running through it)
    void prepare(int channels, int sampleRate)
    {
        chans = channels < 1 ? 1 : channels;
        fs = sampleRate > 8000 ? sampleRate : 44100;
        const double f = double(fs);

        for (int c = 0; c < 2; c++) {
            lpLow[c].setup(180.0, f);
            lpMid[c].setup(3000.0, f);
            exHp1[c].highpass(3500.0, f, 0.7071);
            exHp2[c].highpass(3500.0, f, 0.7071);
            kw[c].setup(f);
            lpLow[c].reset(); lpMid[c].reset(); exHp1[c].reset(); exHp2[c].reset(); kw[c].reset();
        }
        comp[0].setup(25.0, 220.0, -16.0, f);
        comp[1].setup(10.0, 130.0, -14.0, f);
        comp[2].setup(3.0, 80.0, -18.0, f);

        // loudness: 100 ms blocks, 30 of them = 3 s
        subLen = (std::max)(1, fs / 10);
        subCount = 0;
        subAcc = 0.0;
        subFill = 0;
        subHead = 0;
        for (int i = 0; i < 30; i++)
            subRing[i] = 0.0;
        autoDb = 0.0;
        autoLin = 1.0;
        autoSmooth = 1.0 - std::exp(-1.0 / (0.05 * f));

        // limiter
        la = 6;
        L = (std::max)(16, int(0.0015 * f));
        R = L + 32;
        ring.assign(size_t(R) * size_t(chans), 0.0f);
        qring.assign(size_t(L), 1.0f);
        mring.assign(size_t(L), 1.0f);
        sumM = double(L);
        env = 1.0;
        n = 0;
        limPrimed = false;
        for (int p = 0; p < 3; p++) {
            const double t = 5.0 + double(p + 1) / 4.0;
            double sum = 0.0;
            for (int i = 0; i < 12; i++) {
                const double d = t - double(i);
                const double s = (std::fabs(d) < 1e-9) ? 1.0 : std::sin(kPi * d) / (kPi * d);
                const double w = (std::fabs(d) < 6.0) ? 0.5 * (1.0 + std::cos(kPi * d / 6.0)) : 0.0;
                tp[p][i] = float(s * w);
                sum += s * w;
            }
            for (int i = 0; i < 12; i++)
                tp[p][i] = float(double(tp[p][i]) / sum);
        }

        peakVal = 0.0f;
        mMom = mShort = -100.0f;
        mAuto = 0.0f;
        mGr = 0.0f;
        mPeak = -100.0f;
        P = Params();
        applyParams(P);
        dirty.store(true);
    }

    // thread safe: the audio thread picks the new parameters up at the start of its next block
    void setParams(const Params &p)
    {
        std::lock_guard<std::mutex> lk(pm);
        pending = p;
        dirty.store(true, std::memory_order_release);
    }

    Params params()
    {
        std::lock_guard<std::mutex> lk(pm);
        return pending;
    }

    Meter meter() const
    {
        Meter m;
        m.momentary = mMom.load();
        m.shortTerm = mShort.load();
        m.autoDb = mAuto.load();
        m.grDb = mGr.load();
        m.peakDb = mPeak.load();
        return m;
    }

    // interleaved float samples, `frames` frames of `chans` channels. outGain = the gain the mixer applies after this DSP.
    void process(float *x, size_t frames, float outGain)
    {
        if (dirty.load(std::memory_order_acquire) && pm.try_lock()) {
            const Params np = pending;
            dirty.store(false, std::memory_order_relaxed);
            pm.unlock();
            applyParams(np);
        }

        const bool doMaster = P.masterOn && chans >= 2;
        const bool doLoud = (P.autoOn || P.meter) && chans >= 2;
        const bool doLim = P.limOn;
        if (!doMaster && !doLoud && !doLim && autoLin == 1.0)
            return;

        if (!(outGain > 1e-4f))
            outGain = 1e-4f;
        const float cPre = float(ceilLin) / outGain;          // ceiling in front of the master volume
        if (doLim && !limPrimed)
            primeLimiter();
        if (!doLim)
            limPrimed = false;

        const bool autoActive = P.autoOn;
        float blockGr = 1.0f, blockPeak = 0.0f;

        for (size_t f = 0; f < frames; f++) {
            float *fr = x + f * size_t(chans);

            if (doMaster)
                masterFrame(fr[0], fr[1]);

            if (doLoud) {
                const double l = kw[0].process(fr[0]), r = kw[1].process(fr[1]);
                subAcc += l * l + r * r;
                if (++subFill >= subLen)
                    endSubBlock();
            }

            if (autoActive || autoLin != 1.0) {
                autoLin += (autoTarget - autoLin) * autoSmooth;
                const float g = float(autoLin);
                for (int c = 0; c < chans; c++)
                    fr[c] *= g;
            }

            if (doLim)
                limiterFrame(fr, cPre, blockGr);

            if (chans >= 2) {
                const float pk = (std::max)(std::fabs(fr[0]), std::fabs(fr[1]));
                if (pk > blockPeak)
                    blockPeak = pk;
            }
        }

        // meters (per block)
        if (doLim)
            mGr.store(float(detail::linToDb(blockGr)));
        else
            mGr.store(0.0f);
        blockPeak *= outGain;
        peakVal = (blockPeak > peakVal) ? blockPeak : peakVal * std::pow(0.5f, float(frames) / (0.7f * float(fs)));
        mPeak.store(float(detail::linToDb(peakVal)));
        mAuto.store(float(autoDb));

        // keep the running sum of the limiter exact (no drift)
        if (doLim) {
            double s = 0.0;
            for (int i = 0; i < L; i++)
                s += mring[size_t(i)];
            sumM = s;
        }
    }

    // 16-bit or float interleaved buffer
    void processBuffer(void *buf, unsigned bytes, bool isFloat, float outGain)
    {
        if (isFloat) {
            process(static_cast<float *>(buf), bytes / 4 / size_t(chans), outGain);
            return;
        }
        const size_t frames = bytes / 2 / size_t(chans);
        tmp.resize(frames * size_t(chans));
        short *s = static_cast<short *>(buf);
        for (size_t i = 0; i < tmp.size(); i++)
            tmp[i] = float(s[i]) * (1.0f / 32768.0f);
        process(tmp.data(), frames, outGain);
        for (size_t i = 0; i < tmp.size(); i++) {
            float v = tmp[i] * 32768.0f;
            if (v > 32767.0f) v = 32767.0f;
            if (v < -32768.0f) v = -32768.0f;
            s[i] = short(v < 0 ? v - 0.5f : v + 0.5f);
        }
    }

    int channels() const { return chans; }
    int latencyFrames() const { return P.limOn ? (L - 1 + la) : 0; }

private:
    // ---- parameters
    void applyParams(const Params &np)
    {
        P = np;
        ceilLin = detail::dbToLin((std::min)(-0.05, double(P.ceilingDb)));
        relCoef = 1.0 - std::exp(-1.0 / ((std::max)(10.0, double(P.releaseMs)) * 0.001 * double(fs)));
        ratio = 1.0 + 3.0 * double((std::min)(1.0f, (std::max)(0.0f, P.compAmount)));
        const double mk = detail::dbToLin(8.0 * (1.0 - 1.0 / ratio));   // make-up gain, about +6 dB at 4:1
        toneLin[0] = mk * detail::dbToLin((std::min)(6.0f, (std::max)(-6.0f, P.lowDb)));
        toneLin[1] = mk * detail::dbToLin((std::min)(6.0f, (std::max)(-6.0f, P.midDb)));
        toneLin[2] = mk * detail::dbToLin((std::min)(6.0f, (std::max)(-6.0f, P.highDb)));
        excAmt = 0.8 * double((std::min)(1.0f, (std::max)(0.0f, P.exciter)));
        widthV = double((std::min)(2.0f, (std::max)(0.0f, P.width)));
        if (!P.autoOn)
            autoTarget = 1.0;                          // gain glides back to unity
        if (!P.autoOn)
            autoDb = 0.0;
        else
            autoTarget = detail::dbToLin(autoDb);
    }

    // ---- 1. mastering chain on the front pair
    inline void masterFrame(float &l, float &r)
    {
        const double in[2] = { double(l), double(r) };
        double band[2][3];
        for (int c = 0; c < 2; c++) {
            const double lo = lpLow[c].process(in[c]);
            const double rest = in[c] - lo;
            const double mi = lpMid[c].process(rest);
            band[c][0] = lo;
            band[c][1] = mi;
            band[c][2] = rest - mi;
        }
        double g[3];
        for (int b = 0; b < 3; b++) {
            const double level = (std::max)(std::fabs(band[0][b]), std::fabs(band[1][b]));
            g[b] = comp[b].process(level, ratio) * toneLin[b];
        }
        double out[2];
        for (int c = 0; c < 2; c++)
            out[c] = band[c][0] * g[0] + band[c][1] * g[1] + band[c][2] * g[2];

        if (excAmt > 0.0) {
            for (int c = 0; c < 2; c++) {
                const double h = exHp1[c].process(in[c]);
                const double s = 3.0 * h - std::tanh(3.0 * h);     // the harmonics a soft saturator would add
                out[c] += excAmt * exHp2[c].process(s);
            }
        }

        if (std::fabs(widthV - 1.0) > 1e-3) {
            const double m = 0.5 * (out[0] + out[1]), s = 0.5 * (out[0] - out[1]) * widthV;
            out[0] = m + s;
            out[1] = m - s;
        }
        l = float(out[0]);
        r = float(out[1]);
    }

    // ---- 2. loudness, every 100 ms
    void endSubBlock()
    {
        const double ms = subAcc / double(subLen);       // sum of the two channels' mean squares
        subAcc = 0.0;
        subFill = 0;
        subRing[subHead] = ms;
        subHead = (subHead + 1) % 30;
        if (subCount < 30)
            subCount++;

        auto lufs = [&](int blocks) {
            const int use = (std::min)(blocks, subCount);
            if (use <= 0)
                return -100.0;
            double s = 0.0;
            for (int i = 0; i < use; i++)
                s += subRing[(subHead - 1 - i + 60) % 30];
            s /= double(use);
            return (s > 1e-12) ? -0.691 + 10.0 * std::log10(s) : -100.0;
        };
        const double mom = lufs(4), sh = lufs(30);
        mMom.store(float(mom));
        mShort.store(float(sh));

        if (P.autoOn && subCount >= 10 && sh > -50.0) {   // at least 1 s of programme, and not silence
            const double desired = (std::min)(double(P.maxBoostDb), (std::max)(-double(P.maxCutDb), double(P.targetLufs) - sh));
            const double step = (std::min)(0.15, (std::max)(-0.15, desired - autoDb));
            autoDb += step;
            autoTarget = detail::dbToLin(autoDb);
        }
    }

    // ---- 3. limiter
    void primeLimiter()
    {
        std::fill(ring.begin(), ring.end(), 0.0f);
        std::fill(qring.begin(), qring.end(), 1.0f);
        std::fill(mring.begin(), mring.end(), 1.0f);
        sumM = double(L);
        env = 1.0;
        n = 0;
        limPrimed = true;
    }

    inline float sampleAt(int64_t idx, int c) const
    {
        return ring[size_t(idx % R) * size_t(chans) + size_t(c)];
    }

    void limiterFrame(float *fr, float cPre, float &blockGr)
    {
        if (P.softClip)
            for (int c = 0; c < chans; c++)
                fr[c] = detail::softClipTo(fr[c], cPre);

        float *slot = &ring[size_t(n % R) * size_t(chans)];
        for (int c = 0; c < chans; c++)
            slot[c] = fr[c];

        // requirement of the sample `la` frames back (the true-peak estimate needs the samples around it)
        const int lookBack = P.truePeak ? la : 0;
        const int64_t m = n - lookBack;
        float q = 1.0f;
        if (m >= 0) {
            float pk = 0.0f;
            for (int c = 0; c < chans; c++)
                pk = (std::max)(pk, std::fabs(sampleAt(m, c)));
            if (P.truePeak && chans >= 2 && n >= 11) {
                for (int c = 0; c < 2; c++) {
                    float h[12];
                    for (int i = 0; i < 12; i++)
                        h[i] = sampleAt(m - 5 + i, c);
                    for (int p = 0; p < 3; p++) {
                        float v = 0.0f;
                        for (int i = 0; i < 12; i++)
                            v += tp[p][i] * h[i];
                        pk = (std::max)(pk, std::fabs(v));
                    }
                }
            }
            if (pk > cPre)
                q = cPre / pk;
        }

        const size_t qi = size_t(n % L);
        qring[qi] = q;
        float mn = 1.0f;
        for (int i = 0; i < L; i++)
            mn = (std::min)(mn, qring[size_t(i)]);
        const double old = mring[qi];
        mring[qi] = mn;
        sumM += double(mn) - old;
        double gavg = sumM / double(L);
        if (gavg > 1.0)
            gavg = 1.0;

        env = (gavg < env) ? gavg : env + (gavg - env) * relCoef;
        const float e = float(env);
        if (e < blockGr)
            blockGr = e;

        const int64_t k = n - (L - 1) - lookBack;
        for (int c = 0; c < chans; c++) {
            float v = (k >= 0) ? sampleAt(k, c) * e : 0.0f;
            if (v > cPre) v = cPre;
            if (v < -cPre) v = -cPre;
            fr[c] = v;
        }
        n++;
    }

    // ---- state
    int chans = 2, fs = 44100;
    Params P, pending;
    std::mutex pm;
    std::atomic<bool> dirty{false};

    detail::Lowpass4 lpLow[2], lpMid[2];
    detail::Biquad exHp1[2], exHp2[2];
    detail::BandComp comp[3];
    double ratio = 1.0, toneLin[3] = { 1, 1, 1 }, excAmt = 0.0, widthV = 1.0;

    detail::KWeight kw[2];
    int subLen = 4410, subFill = 0, subHead = 0, subCount = 0;
    double subAcc = 0.0, subRing[30];
    double autoDb = 0.0, autoLin = 1.0, autoTarget = 1.0, autoSmooth = 0.001;

    int L = 66, R = 98, la = 6;
    std::vector<float> ring, qring, mring, tmp;
    double sumM = 0.0, env = 1.0, relCoef = 0.001, ceilLin = 0.89;
    int64_t n = 0;
    bool limPrimed = false;
    float tp[3][12];

    float peakVal = 0.0f;
    std::atomic<float> mMom{-100.0f}, mShort{-100.0f}, mAuto{0.0f}, mGr{0.0f}, mPeak{-100.0f};
};

} // namespace MasterDsp

#endif // MASTERDSP_H
