// make_soundfx: makes PTHK Karaoke's own sound effects (the "PTHK › พื้นฐาน" sound pads bank).
// Every sound is synthesized here from scratch, so the files belong to the project and may be
// shipped with the installer. Output: 44.1 kHz 16-bit stereo WAV, loudness -16 LUFS, peak <= -1 dBFS.
//
//   cl /nologo /O2 /EHsc /std:c++17 /utf-8 make_soundfx.cpp
//   make_soundfx.exe <output folder>
//
// Applause: each person's clap is a short burst of noise shaped by the "hand resonance" of that
// person (a band-pass between ~0.8 and 2.4 kHz, as measured in clapping research), people clap at
// their own rate and place left / right / near / far, and a room reverb puts them in one hall.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

static const int SR = 44100;
static const double PI = 3.14159265358979323846;

struct Stereo
{
    std::vector<float> l, r;
    explicit Stereo(double seconds) : l(size_t(seconds * SR), 0.0f), r(size_t(seconds * SR), 0.0f) {}
    size_t size() const { return l.size(); }
    void add(size_t i, float a, float pan)        // pan -1 left .. +1 right (constant power)
    {
        if (i >= l.size()) return;
        const double t = (pan + 1.0) * PI / 4.0;
        l[i] += float(a * std::cos(t));
        r[i] += float(a * std::sin(t));
    }
};

static std::mt19937 rng(20261103);
static double uni(double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); }
static double gauss(double s) { return std::normal_distribution<double>(0.0, s)(rng); }

// ---- filters
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void norm(double B0, double B1, double B2, double A0, double A1, double A2)
    { b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0; z1 = z2 = 0; }
    void bandpass(double f, double q)
    { const double w = 2 * PI * f / SR, al = std::sin(w) / (2 * q); norm(al, 0, -al, 1 + al, -2 * std::cos(w), 1 - al); }
    void lowpass(double f, double q = 0.7071)
    { const double w = 2 * PI * f / SR, c = std::cos(w), al = std::sin(w) / (2 * q); norm((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); }
    void highpass(double f, double q = 0.7071)
    { const double w = 2 * PI * f / SR, c = std::cos(w), al = std::sin(w) / (2 * q); norm((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); }
};

// ---- Freeverb (Jezar's public-domain reverb): a hall around the sound
struct Comb
{
    std::vector<float> buf; size_t i = 0; float store = 0, fb = 0.84f, damp = 0.2f;
    explicit Comb(size_t n) : buf(n, 0.0f) {}
    float run(float x) { float o = buf[i]; store = o * (1 - damp) + store * damp; buf[i] = x + store * fb; if (++i >= buf.size()) i = 0; return o; }
};
struct Allpass
{
    std::vector<float> buf; size_t i = 0;
    explicit Allpass(size_t n) : buf(n, 0.0f) {}
    float run(float x) { float b = buf[i]; float o = -x + b; buf[i] = x + b * 0.5f; if (++i >= buf.size()) i = 0; return o; }
};
static void reverb(Stereo &s, double room, double damp, double wet, double dry = 1.0)
{
    const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    const int apT[4] = { 556, 441, 341, 225 };
    std::vector<Comb> cl, cr;
    std::vector<Allpass> al, ar;
    for (int t : combT) { cl.emplace_back(t); cr.emplace_back(t + 23); }
    for (int t : apT) { al.emplace_back(t); ar.emplace_back(t + 23); }
    for (auto &c : cl) { c.fb = float(room * 0.28 + 0.7); c.damp = float(damp * 0.4); }
    for (auto &c : cr) { c.fb = float(room * 0.28 + 0.7); c.damp = float(damp * 0.4); }
    for (size_t n = 0; n < s.size(); ++n) {
        const float in = (s.l[n] + s.r[n]) * 0.015f;
        float ol = 0, or_ = 0;
        for (auto &c : cl) ol += c.run(in);
        for (auto &c : cr) or_ += c.run(in);
        for (auto &a : al) ol = a.run(ol);
        for (auto &a : ar) or_ = a.run(or_);
        s.l[n] = float(s.l[n] * dry + ol * wet * 3.0);
        s.r[n] = float(s.r[n] * dry + or_ * wet * 3.0);
    }
}

// ---- one hand clap: noise through the clapper's hand resonance, sharp attack, fast decay
struct Clapper
{
    double fc, q, decay, gain, pan, lp;
};
static void clap(Stereo &s, double at, const Clapper &c, double amp)
{
    Biquad bp, lo, hp;
    bp.bandpass(c.fc * uni(0.95, 1.05), c.q);
    lo.lowpass(c.lp);
    hp.highpass(250.0);
    const int len = int(0.06 * SR);
    const size_t start = size_t(at * SR);
    for (int k = 0; k < len; ++k) {
        const double t = double(k) / SR;
        const double env = (t < 0.0006 ? t / 0.0006 : std::exp(-(t - 0.0006) / c.decay));
        const double n = uni(-1.0, 1.0);
        const double v = hp.run(lo.run(0.75 * bp.run(n) * 3.0 + 0.25 * n)) * env * amp * c.gain;
        s.add(start + size_t(k), float(v), float(c.pan));
    }
}
static Clapper randomClapper(double nearest, double farthest)
{
    Clapper c;
    c.fc = uni(800.0, 2400.0);                 // cupped hands low, flat hands high
    c.q = uni(1.2, 3.5);
    c.decay = uni(0.004, 0.011);
    const double d = uni(nearest, farthest);   // metres
    c.gain = 1.0 / d;
    c.pan = uni(-0.9, 0.9);
    c.lp = 9000.0 / std::sqrt(d);              // far away: duller
    return c;
}
// people clapping freely: `people` people from t0 (each starts a moment apart) until about t1
static void crowd(Stereo &s, int people, double t0, double t1, double nearest, double farthest, double amp)
{
    for (int p = 0; p < people; ++p) {
        const Clapper c = randomClapper(p < 4 ? nearest : nearest * 2.0, farthest);   // a few close ones stand out
        const double rate = uni(3.6, 5.8);      // claps per second
        double t = t0 + uni(0.0, 0.5);
        const double end = t1 + uni(-0.9, 0.4);
        while (t < end) {
            // the last second: some stop, the others clap softer
            double a = amp * uni(0.75, 1.0);
            if (t > end - 1.0)
                a *= std::max(0.15, (end - t));
            clap(s, t, c, a);
            t += (1.0 / rate) * (1.0 + gauss(0.07));
        }
    }
}

// ---- simple instruments
static double saw(double ph) { return 2.0 * (ph - std::floor(ph + 0.5)); }
static double sqr(double ph) { return std::fmod(ph, 1.0) < 0.5 ? 1.0 : -1.0; }

// a drum: sine with a falling pitch + a click
static void drum(Stereo &s, double at, double f0, double f1, double decay, double amp, double pan, double noise = 0.2)
{
    const size_t start = size_t(at * SR);
    const int len = int((decay * 6) * SR);
    double ph = 0;
    Biquad hp; hp.highpass(1500);
    for (int k = 0; k < len; ++k) {
        const double t = double(k) / SR;
        const double f = f1 + (f0 - f1) * std::exp(-t / 0.04);
        ph += f / SR;
        const double env = std::exp(-t / decay);
        const double click = hp.run(uni(-1, 1)) * std::exp(-t / 0.006) * noise;
        s.add(start + size_t(k), float((std::sin(2 * PI * ph) * env + click) * amp), float(pan));
    }
}
static void snare(Stereo &s, double at, double amp, double pan = 0.0)
{
    const size_t start = size_t(at * SR);
    const int len = int(0.25 * SR);
    Biquad bp; bp.bandpass(3000, 0.6);
    double ph = 0;
    for (int k = 0; k < len; ++k) {
        const double t = double(k) / SR;
        ph += 185.0 / SR;
        const double body = std::sin(2 * PI * ph) * std::exp(-t / 0.035) * 0.6;
        const double wires = bp.run(uni(-1, 1)) * std::exp(-t / 0.07) * 1.6;
        s.add(start + size_t(k), float((body + wires) * amp), float(pan));
    }
}
// crash cymbal: six detuned square waves (metallic) + bright noise, long decay
static void crash(Stereo &s, double at, double amp, double seconds = 2.2)
{
    const double fr[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    double ph[6] = {};
    Biquad hpL, hpR, hp2L, hp2R;
    hpL.highpass(4200, 0.9); hpR.highpass(4300, 0.9); hp2L.highpass(7000); hp2R.highpass(7000);
    const size_t start = size_t(at * SR);
    const int len = int(seconds * SR);
    for (int k = 0; k < len; ++k) {
        const double t = double(k) / SR;
        double m = 0;
        for (int i = 0; i < 6; ++i) { ph[i] += fr[i] * 1.6 / SR; m += sqr(ph[i]); }
        const double env = (t < 0.002 ? t / 0.002 : 1.0) * (0.55 * std::exp(-t / 0.35) + 0.45 * std::exp(-t / (seconds * 0.35)));
        const double nl = uni(-1, 1), nr = uni(-1, 1);
        const double l = hpL.run(m * 0.17) + hp2L.run(nl) * 0.6;
        const double r = hpR.run(m * 0.17) + hp2R.run(nr) * 0.6;
        if (start + size_t(k) < s.size()) {
            s.l[start + k] += float(l * env * amp);
            s.r[start + k] += float(r * env * amp);
        }
    }
}
// whistle: a tone near 3 kHz that the pea makes flutter, plus breath
static void whistle(Stereo &s, double at, double len, double amp, double f = 3150.0)
{
    const size_t start = size_t(at * SR);
    const int n = int(len * SR);
    double ph = 0, pea = 0;
    Biquad bp; bp.bandpass(f, 4.0);
    for (int k = 0; k < n; ++k) {
        const double t = double(k) / SR;
        pea += (38.0 + 6.0 * std::sin(2 * PI * 0.7 * t)) / SR;      // the pea's rattle rate
        const double flutter = 0.5 + 0.5 * std::sin(2 * PI * pea);
        const double fm = f * (1.0 + 0.035 * flutter);
        ph += fm / SR;
        const double att = std::min(1.0, t / 0.015), rel = std::min(1.0, (len - t) / 0.03);
        const double env = att * rel * (0.75 + 0.25 * flutter);
        const double tone = std::sin(2 * PI * ph) + 0.12 * std::sin(4 * PI * ph);
        const double breath = bp.run(uni(-1, 1)) * 0.5;
        s.add(start + size_t(k), float((tone + breath) * env * amp), 0.1f);
    }
}
// rising wolf whistle ("ฟิ้ว ฟิ้ว")
static void wolfWhistle(Stereo &s, double at, double amp, double pan)
{
    const size_t start = size_t(at * SR);
    auto glide = [&](double t0, double dur, double fa, double fb, double fc) {
        double ph = 0;
        const int n = int(dur * SR);
        for (int k = 0; k < n; ++k) {
            const double u = double(k) / n;
            const double f = u < 0.5 ? fa + (fb - fa) * (u * 2) : fb + (fc - fb) * ((u - 0.5) * 2);
            ph += f / SR;
            const double env = std::sin(PI * u) * (0.85 + 0.15 * std::sin(2 * PI * 6 * u));
            s.add(start + size_t(t0 * SR) + size_t(k), float(std::sin(2 * PI * ph) * env * amp), float(pan));
        }
    };
    glide(0.0, 0.32, 1500, 2900, 2700);
    glide(0.42, 0.55, 1500, 2950, 1400);
}
// a brass-like tone (sawtooth stack, opening filter): horns and the sad trombone
static void brass(Stereo &s, double at, double len, double f, double amp, double vibrato = 0.0, double wah = 0.0, int voices = 3)
{
    const size_t start = size_t(at * SR);
    const int n = int(len * SR);
    std::vector<double> ph(voices, 0.0);
    Biquad lp;
    for (int k = 0; k < n; ++k) {
        const double t = double(k) / SR;
        const double att = std::min(1.0, t / 0.03), rel = std::min(1.0, (len - t) / 0.06);
        const double vib = 1.0 + vibrato * std::sin(2 * PI * 5.5 * t) * std::min(1.0, t / 0.25);
        double v = 0;
        for (int i = 0; i < voices; ++i) {
            const double det = 1.0 + (i - (voices - 1) / 2.0) * 0.004;
            ph[i] += f * vib * det / SR;
            v += saw(ph[i]);
        }
        // the filter opens with the note (and for "wah" closes again)
        double open = std::min(1.0, t / 0.08);
        if (wah > 0) open *= 1.0 - wah * std::min(1.0, t / std::max(0.05, len));
        if (k % 32 == 0) lp.lowpass(300.0 + 3500.0 * open, 1.2);
        v = lp.run(v / voices);
        v = std::tanh(v * 1.8);
        s.add(start + size_t(k), float(v * att * rel * amp), 0.0f);
    }
}
// a bell (FM): "ติ๊ง-ต่อง"
static void bell(Stereo &s, double at, double f, double amp, double pan, double seconds = 1.6)
{
    const size_t start = size_t(at * SR);
    const int n = int(seconds * SR);
    for (int k = 0; k < n; ++k) {
        const double t = double(k) / SR;
        const double index = 2.2 * std::exp(-t / 0.25);
        const double m = std::sin(2 * PI * f * 1.4 * t);
        const double v = std::sin(2 * PI * f * t + index * m) * std::exp(-t / 0.55)
                       + 0.25 * std::sin(2 * PI * f * 2.76 * t) * std::exp(-t / 0.18);
        const double att = std::min(1.0, t / 0.002);
        s.add(start + size_t(k), float(v * att * amp), float(pan));
    }
}

// ---- loudness (ITU-R BS.1770, gated) and output
struct KW { Biquad a, b; KW() {
    { const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196, K = std::tan(PI * f0 / SR);
      const double Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416), a0 = 1.0 + K / Q + K * K;
      a.b0 = (Vh + Vb * K / Q + K * K) / a0; a.b1 = 2.0 * (K * K - Vh) / a0; a.b2 = (Vh - Vb * K / Q + K * K) / a0;
      a.a1 = 2.0 * (K * K - 1.0) / a0; a.a2 = (1.0 - K / Q + K * K) / a0; }
    { const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan(PI * f0 / SR), a0 = 1.0 + K / Q + K * K;
      b.b0 = 1.0; b.b1 = -2.0; b.b2 = 1.0; b.a1 = 2.0 * (K * K - 1.0) / a0; b.a2 = (1.0 - K / Q + K * K) / a0; } }
    double run(double x) { return b.run(a.run(x)); } };
static double lufs(const Stereo &s)
{
    KW kl, kr;
    std::vector<double> sub;
    double acc = 0; int fill = 0;
    for (size_t n = 0; n < s.size(); ++n) {
        const double l = kl.run(s.l[n]), r = kr.run(s.r[n]);
        acc += l * l + r * r;
        if (++fill == SR / 10) { sub.push_back(acc / fill); acc = 0; fill = 0; }
    }
    std::vector<double> blk;
    for (size_t i = 3; i < sub.size(); ++i) {
        const double ms = (sub[i] + sub[i - 1] + sub[i - 2] + sub[i - 3]) / 4;
        if (-0.691 + 10 * std::log10(ms + 1e-20) > -70) blk.push_back(ms);
    }
    if (blk.empty()) return -100;
    double sum = 0; for (double v : blk) sum += v;
    const double gate = -0.691 + 10 * std::log10(sum / blk.size()) - 10;
    double g = 0; int c = 0;
    for (double v : blk) if (-0.691 + 10 * std::log10(v) > gate) { g += v; ++c; }
    return -0.691 + 10 * std::log10(g / c);
}
static Stereo scaled(Stereo s, double g)
{
    for (size_t n = 0; n < s.size(); ++n) { s.l[n] = float(s.l[n] * g); s.r[n] = float(s.r[n] * g); }
    return s;
}
static void fadeTail(Stereo &s, double seconds)
{
    const size_t n = size_t(seconds * SR);
    for (size_t k = 0; k < n && k < s.size(); ++k) {
        const float g = float(k) / float(n);
        s.l[s.size() - 1 - k] *= g;
        s.r[s.size() - 1 - k] *= g;
    }
}
static bool save(Stereo s, const std::wstring &path, double targetLufs = -16.0)
{
    fadeTail(s, 0.05);
    double peak = 1e-9;
    for (size_t n = 0; n < s.size(); ++n) peak = std::max(peak, double(std::max(std::fabs(s.l[n]), std::fabs(s.r[n]))));
    const double L = lufs(s);
    const double gLoud = std::pow(10.0, (targetLufs - L) / 20.0);
    const double gPeak = std::pow(10.0, -1.0 / 20.0) / peak;
    // sharp claps all at once peak high: a look-ahead limiter takes up to 6 dB off the peaks
    // (1.5 ms ahead, 60 ms release) so they can be as loud as the others
    double g = std::min(gLoud, gPeak * 2.0);
    if (g > gPeak) {
        const double ceiling = std::pow(10.0, -1.0 / 20.0);
        const size_t ahead = size_t(0.0015 * SR);
        const double rel = std::exp(-1.0 / (0.060 * SR));
        std::vector<double> need(s.size());
        for (size_t n = 0; n < s.size(); ++n) {
            const double a = std::max(std::fabs(s.l[n]), std::fabs(s.r[n])) * g;
            need[n] = a > ceiling ? ceiling / a : 1.0;
        }
        double env = 1.0;
        std::vector<float> gl(s.size());
        for (size_t n = 0; n < s.size(); ++n) {
            double m = 1.0;                     // the lowest gain needed within the look-ahead
            for (size_t k = n; k < std::min(s.size(), n + ahead); ++k) m = std::min(m, need[k]);
            env = m < env ? m : m + (env - m) * rel;
            gl[n] = float(env);
        }
        for (size_t n = 0; n < s.size(); ++n) { s.l[n] *= gl[n]; s.r[n] *= gl[n]; }
        peak = 1e-9;
        for (size_t n = 0; n < s.size(); ++n) peak = std::max(peak, double(std::max(std::fabs(s.l[n]), std::fabs(s.r[n]))));
        g = std::min(g, std::pow(10.0, -1.0 / 20.0) / peak);
    }
    FILE *f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    const uint32_t frames = uint32_t(s.size()), data = frames * 4, riff = 36 + data;
    const uint16_t fmt = 1, ch = 2, bits = 16, align = 4;
    const uint32_t rate = SR, brate = SR * 4, fmtLen = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtLen, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f);
    fwrite(&brate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (size_t n = 0; n < s.size(); ++n) {
        const int16_t l = int16_t(std::lround(std::max(-1.0, std::min(1.0, s.l[n] * g)) * 32767));
        const int16_t r = int16_t(std::lround(std::max(-1.0, std::min(1.0, s.r[n] * g)) * 32767));
        fwrite(&l, 2, 1, f); fwrite(&r, 2, 1, f);
    }
    fclose(f);
    std::printf("%.2s  %5.1f s  %6.1f LUFS -> %6.1f LUFS  peak %5.1f dBFS%s\n",
                std::string(path.end() - (path.size() - path.find_last_of(L"/\\") - 1), path.end()).c_str(),
                double(frames) / SR, L, lufs(scaled(s, g)), 20 * std::log10(peak * g), gLoud > gPeak ? "  (limited)" : "");
    return true;
}

int wmain(int argc, wchar_t **argv)
{
    const std::wstring out = argc > 1 ? argv[1] : L".";
    auto p = [&](const wchar_t *name) { return out + L"/" + name; };

    {   // 1. applause: a hall of people
        Stereo s(7.0);
        crowd(s, 46, 0.0, 5.6, 1.2, 9.0, 0.5);
        reverb(s, 0.80, 0.45, 0.22);
        save(s, p(L"01 - ปรบมือ.wav"));
    }
    {   // 2. cheering: louder, more people, wolf whistles
        Stereo s(8.0);
        crowd(s, 72, 0.0, 6.4, 1.0, 11.0, 0.55);
        wolfWhistle(s, 0.6, 0.20, -0.5);
        wolfWhistle(s, 2.4, 0.16, 0.6);
        whistle(s, 4.1, 0.25, 0.05, 2900); whistle(s, 4.45, 0.6, 0.05, 2900);
        reverb(s, 0.85, 0.40, 0.25);
        save(s, p(L"02 - ปรบมือเชียร์.wav"));
    }
    {   // 3. clapping along to a beat (132 BPM), then a short applause
        Stereo s(8.0);
        const double beat = 60.0 / 132.0;
        for (int person = 0; person < 34; ++person) {
            const Clapper c = randomClapper(person < 3 ? 1.2 : 2.5, 9.0);
            for (int b = 0; b < 12; ++b)
                clap(s, 0.2 + b * beat + gauss(0.012), c, 0.55 * uni(0.85, 1.0));
        }
        crowd(s, 40, 0.2 + 12 * beat, 0.2 + 12 * beat + 1.6, 1.5, 9.0, 0.45);
        reverb(s, 0.80, 0.45, 0.22);
        save(s, p(L"03 - ปรบมือตามจังหวะ.wav"));
    }
    {   // 4. the joke drum: tom, tom, crash + kick ("ตึ่ง-ตึ่ง-ฉึ่ง")
        Stereo s(3.2);
        drum(s, 0.05, 260, 170, 0.18, 0.9, -0.3);
        drum(s, 0.27, 190, 120, 0.22, 0.95, 0.3);
        drum(s, 0.58, 110, 48, 0.25, 1.0, 0.0, 0.5);
        snare(s, 0.58, 0.35);
        crash(s, 0.58, 0.55, 2.4);
        reverb(s, 0.55, 0.5, 0.12);
        save(s, p(L"04 - ตบมุก ตึ่งตึ่งฉึ่ง.wav"));
    }
    {   // 5. snare roll getting louder, then crash + kick
        Stereo s(4.4);
        const double len = 1.7;
        for (double t = 0; t < len; t += 1.0 / 16.0)
            snare(s, 0.05 + t + gauss(0.002), 0.25 + 0.75 * (t / len), (std::fmod(t * 16, 2.0) < 1.0 ? -0.15 : 0.15));
        drum(s, 0.05 + len + 0.02, 110, 48, 0.25, 1.0, 0.0, 0.5);
        crash(s, 0.05 + len + 0.02, 0.6, 2.5);
        reverb(s, 0.55, 0.5, 0.12);
        save(s, p(L"05 - กลองรัว ฉาบ.wav"));
    }
    {   // 6. referee whistle: short, short, long
        Stereo s(2.2);
        whistle(s, 0.05, 0.22, 0.5);
        whistle(s, 0.40, 0.22, 0.5);
        whistle(s, 0.75, 0.95, 0.5);
        reverb(s, 0.5, 0.5, 0.10);
        save(s, p(L"06 - นกหวีด.wav"));
    }
    {   // 7. stadium air horn: short, short, long
        Stereo s(2.8);
        const double f = 466.16;               // B flat
        brass(s, 0.05, 0.28, f, 0.6, 0.0, 0.0, 5);
        brass(s, 0.05, 0.28, f * 1.25, 0.35, 0.0, 0.0, 3);
        brass(s, 0.45, 0.28, f, 0.6, 0.0, 0.0, 5);
        brass(s, 0.45, 0.28, f * 1.25, 0.35, 0.0, 0.0, 3);
        brass(s, 0.85, 1.20, f, 0.6, 0.0, 0.0, 5);
        brass(s, 0.85, 1.20, f * 1.25, 0.35, 0.0, 0.0, 3);
        reverb(s, 0.75, 0.4, 0.18);
        save(s, p(L"07 - แตรเชียร์.wav"));
    }
    {   // 8. "ติ๊ง-ต่อง": right answer chime, going up
        Stereo s(2.6);
        bell(s, 0.05, 1046.5, 0.5, -0.2);      // C6
        bell(s, 0.25, 1318.5, 0.5, 0.0);       // E6
        bell(s, 0.45, 1568.0, 0.55, 0.2, 2.0); // G6
        reverb(s, 0.6, 0.3, 0.15);
        save(s, p(L"08 - ติ๊งต่อง.wav"));
    }
    {   // 9. sad trombone: wah, wah, wah, waaah
        Stereo s(3.6);
        const double n1 = 233.08, n2 = 220.0, n3 = 207.65, n4 = 196.0;   // Bb3 A3 Ab3 G3
        brass(s, 0.05, 0.42, n1, 0.7, 0.0, 0.7, 2);
        brass(s, 0.55, 0.42, n2, 0.7, 0.0, 0.7, 2);
        brass(s, 1.05, 0.42, n3, 0.7, 0.0, 0.7, 2);
        brass(s, 1.55, 1.70, n4, 0.7, 0.012, 0.5, 2);
        reverb(s, 0.6, 0.5, 0.12);
        save(s, p(L"09 - แตรเศร้า.wav"));
    }
    return 0;
}
