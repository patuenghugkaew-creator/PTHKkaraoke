// Vocal remover for stereo music (YouTube sound): short-time FFT, and in every frequency band of the
// singing range the part that is the same in the left and the right channel (the centred lead voice)
// is taken out. Bass, drums and instruments panned to the sides stay. Runs all the time with the
// same delay, so switching it on / off is smooth (the strength glides) and never jumps in time.
#ifndef VOCALREMOVER_H
#define VOCALREMOVER_H

#include <cmath>
#include <complex>
#include <vector>

class VocalRemover
{
public:
    explicit VocalRemover(int sampleRate = 48000) : rate(sampleRate)
    {
        win.resize(N);
        for (int i = 0; i < N; i++)        // sqrt-Hann for analysis and synthesis
            win[i] = float(std::sqrt(0.5 - 0.5 * std::cos(2.0 * kPi * i / N)));
        inL.assign(N, 0.0f); inR.assign(N, 0.0f);
        outL.assign(N, 0.0f); outR.assign(N, 0.0f);
        fl.resize(N); fr.resize(N);
        gainPrev.assign(N / 2 + 1, 0.0f);
        loBin = int(120.0 * N / rate);
        hiBin = int(7000.0 * N / rate);
        // bit reversal and twiddles
        rev.resize(N);
        int bits = 0;
        while ((1 << bits) < N) bits++;
        for (int i = 0; i < N; i++) {
            int r = 0;
            for (int b = 0; b < bits; b++)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[i] = r;
        }
        tw.resize(N / 2);
        for (int i = 0; i < N / 2; i++)
            tw[i] = std::polar(1.0f, float(-2.0 * kPi * i / N));
    }

    void setEnabled(bool on) { enabledFlag = on; target = on ? cutAmount : 0.0f; }
    void setStrength(float s) { cutAmount = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s); target = enabledFlag ? cutAmount : 0.0f; }
    bool enabled() const { return enabledFlag; }

    // interleaved stereo float, processed in place (delayed by N samples)
    void process(float *s, size_t frames)
    {
        for (size_t f = 0; f < frames; f++) {
            inL[pos] = s[2 * f];
            inR[pos] = s[2 * f + 1];
            s[2 * f] = outL[pos];
            s[2 * f + 1] = outR[pos];
            outL[pos] = outR[pos] = 0.0f;
            pos = (pos + 1) % N;
            if (++sinceHop == H) {
                sinceHop = 0;
                frame();
            }
        }
    }

private:
    static constexpr int N = 1024;          // ~21 ms at 48 kHz (low latency, fast FFT)
    static constexpr int H = N / 4;         // 75 % overlap
    static constexpr double kPi = 3.14159265358979323846;

    void fft(std::vector<std::complex<float>> &a, bool inverse)
    {
        for (int i = 0; i < N; i++)
            if (i < rev[i]) std::swap(a[i], a[rev[i]]);
        for (int len = 2; len <= N; len <<= 1) {
            const int step = N / len;
            for (int i = 0; i < N; i += len)
                for (int j = 0; j < len / 2; j++) {
                    std::complex<float> w = tw[j * step];
                    if (inverse) w = std::conj(w);
                    const std::complex<float> u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                }
        }
    }

    void frame()
    {
        // the last N input samples, oldest first, windowed
        for (int i = 0; i < N; i++) {
            const int k = (pos + i) % N;
            fl[i] = std::complex<float>(inL[k] * win[i], 0.0f);
            fr[i] = std::complex<float>(inR[k] * win[i], 0.0f);
        }
        strength += (target - strength) * 0.15f;   // glide on / off over a few frames
        if (strength > 0.001f) {
            fft(fl, false);
            fft(fr, false);
            for (int b = loBin; b <= hiBin; b++) {
                const std::complex<float> L = fl[b], R = fr[b];
                const float e = std::norm(L) + std::norm(R);
                if (e < 1e-12f) continue;
                // 1 = same in both channels (centre, in phase), lower = panned / wide
                const float sim = 2.0f * (L.real() * R.real() + L.imag() * R.imag()) / e;
                float g = (sim - 0.55f) / 0.40f;
                g = g < 0 ? 0 : (g > 1 ? 1 : g);
                g = 0.6f * gainPrev[b] + 0.4f * g;          // less "warble" between frames
                gainPrev[b] = g;
                const std::complex<float> C = (L + R) * (0.5f * g * strength);
                fl[b] = L - C;
                fr[b] = R - C;
                if (b > 0 && b < N / 2) {                   // keep the spectrum of a real signal
                    fl[N - b] = std::conj(fl[b]);
                    fr[N - b] = std::conj(fr[b]);
                }
            }
            fft(fl, true);
            fft(fr, true);
            const float scale = 1.0f / N;
            for (int i = 0; i < N; i++) {
                fl[i] *= scale;
                fr[i] *= scale;
            }
        }
        // overlap-add (sqrt-Hann twice = Hann; Hann at 75 % overlap sums to 2)
        for (int i = 0; i < N; i++) {
            const int k = (pos + i) % N;
            outL[k] += fl[i].real() * win[i] * 0.5f;
            outR[k] += fr[i].real() * win[i] * 0.5f;
        }
    }

    int rate;
    int loBin = 0, hiBin = 0;
    std::vector<float> win, inL, inR, outL, outR, gainPrev;
    std::vector<std::complex<float>> fl, fr, tw;
    std::vector<int> rev;
    int pos = 0, sinceHop = 0;
    float strength = 0.0f, target = 0.0f, cutAmount = 1.0f;
    bool enabledFlag = false;
};

#endif // VOCALREMOVER_H
