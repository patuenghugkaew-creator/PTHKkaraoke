#ifndef MICINPUT_H
#define MICINPUT_H

// Microphone capture shared by the karaoke recorder and the vocal score.
// - keeps the last second of mono samples for OutputRecorder to mix into the recording
// - estimates the sung pitch (autocorrelation over ~46 ms) and the level of every capture block
// Header-only, no Q_OBJECT (same style as OutputRecorder.h).

#include <bass.h>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>
#include <QString>

class MicInput
{
public:
    static constexpr int kRate = 44100;     // same as the synth mixer, so no resampling is needed

    static MicInput &get() { static MicInput m; return m; }

    bool isRunning() const { return rec != 0; }

    // device -1 = Windows' default recording device
    bool start(int device = -1, QString *error = nullptr)
    {
        if (rec)
            return true;
        if (!BASS_RecordInit(device) && BASS_ErrorGetCode() != BASS_ERROR_ALREADY) {
            if (error) *error = QStringLiteral("BASS_RecordInit error %1").arg(BASS_ErrorGetCode());
            return false;
        }
        recDevice = device < 0 ? int(BASS_RecordGetDevice()) : device;
        clear();
        rec = BASS_RecordStart(kRate, 1, MAKELONG(BASS_SAMPLE_FLOAT, 20), &MicInput::recProc, this);
        if (!rec) {
            if (error) *error = QStringLiteral("BASS_RecordStart error %1").arg(BASS_ErrorGetCode());
            BASS_RecordFree();
            return false;
        }
        return true;
    }

    void stop()
    {
        const DWORD r = rec;
        rec = 0;
        if (!r)
            return;
        BASS_ChannelStop(r);
        BASS_RecordSetDevice(DWORD(recDevice));
        BASS_RecordFree();
        clear();
    }

    // latest analysis: pitch in Hz (0 = unvoiced / silence) and RMS level 0..1
    float pitchHz() const { return pitch.load(); }
    float level() const { return rms.load(); }

    // mono samples for the recorder; missing samples are silence
    void pop(float *out, int n)
    {
        std::lock_guard<std::mutex> g(mx);
        int i = 0;
        for (; i < n && fill > 0; i++) {
            out[i] = buf[rd];
            rd = (rd + 1) % kCap;
            fill--;
        }
        for (; i < n; i++)
            out[i] = 0.0f;
    }

    void clear()
    {
        std::lock_guard<std::mutex> g(mx);
        rd = wr = fill = 0;
        pitch = 0.0f;
        rms = 0.0f;
        win.clear();
    }

private:
    static constexpr int kCap = kRate;      // 1 s
    static constexpr int kWin = 2048;       // analysis window

    MicInput() : buf(size_t(kCap), 0.0f) {}

    static BOOL CALLBACK recProc(HRECORD, const void *buffer, DWORD length, void *user)
    {
        static_cast<MicInput *>(user)->feed(static_cast<const float *>(buffer), int(length / sizeof(float)));
        return TRUE;
    }

    void feed(const float *in, int n)
    {
        {
            std::lock_guard<std::mutex> g(mx);
            for (int i = 0; i < n; i++) {
                if (fill >= kCap) {                 // nobody reads (not recording): drop the oldest
                    rd = (rd + 1) % kCap;
                    fill--;
                }
                buf[wr] = in[i];
                wr = (wr + 1) % kCap;
                fill++;
            }
        }

        win.insert(win.end(), in, in + n);
        if (int(win.size()) > kWin)
            win.erase(win.begin(), win.end() - kWin);
        if (int(win.size()) == kWin)
            analyse();
    }

    // Normalised autocorrelation; singing range ~80..1000 Hz.
    void analyse()
    {
        double energy = 0.0;
        for (float v : win)
            energy += double(v) * v;
        const float r = float(std::sqrt(energy / kWin));
        rms = r;
        if (r < 0.01f) {                            // too quiet to be singing
            pitch = 0.0f;
            return;
        }

        const int minLag = kRate / 1000, maxLag = kRate / 80;
        double best = 0.0;
        int bestLag = 0;
        for (int lag = minLag; lag <= maxLag; lag++) {
            double sum = 0.0, e1 = 0.0, e2 = 0.0;
            for (int i = 0; i + lag < kWin; i++) {
                sum += double(win[i]) * win[i + lag];
                e1 += double(win[i]) * win[i];
                e2 += double(win[i + lag]) * win[i + lag];
            }
            const double c = sum / (std::sqrt(e1 * e2) + 1e-12);
            if (c > best) {
                best = c;
                bestLag = lag;
            }
        }
        pitch = (best > 0.6 && bestLag > 0) ? float(kRate) / bestLag : 0.0f;
    }

    std::atomic<DWORD> rec{0};
    int recDevice = -1;
    std::mutex mx;
    std::vector<float> buf;
    int rd = 0, wr = 0, fill = 0;
    std::vector<float> win;                         // analysis window (record thread only)
    std::atomic<float> pitch{0.0f};
    std::atomic<float> rms{0.0f};
};

#endif // MICINPUT_H
