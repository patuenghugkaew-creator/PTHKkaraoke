#ifndef SONGLEVEL_H
#define SONGLEVEL_H

// Same loudness for every song ("ปรับความดังทุกเพลงให้เท่ากัน").
//
// LoudnessMeter: integrated loudness of a song (ITU-R BS.1770 / EBU R128: K-weighting, 400 ms blocks
// with 75 % overlap, absolute gate -70 LUFS, relative gate -10 LU). Used live and on files.
//
// SongLevel: one DSP on the main mixer. It measures the music and applies one steady gain per song
// (set by MainWindow from the song's measured loudness). It runs before the master chain, so the
// master limiter catches a boost, and before the mixer's volume, so the volume slider does not change
// the reading. Video / MP3 / YouTube sound is turned down on its own channel before the mixer: the
// caller passes that gain (setSourceGain) and the meter takes it back out.
//
// measureFile(): the loudness of a whole audio / video file, decoded as fast as possible (worker thread).

#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

#include <bass.h>
#include "Midi/MasterDsp.h"

#include <QString>

class LoudnessMeter
{
public:
    void prepare(int sampleRate)
    {
        rate = sampleRate > 0 ? sampleRate : 44100;
        subLen = (std::max)(1, rate / 10);          // 100 ms
        kL.setup(rate);
        kR.setup(rate);
        reset();
    }

    void reset()
    {
        kL.reset();
        kR.reset();
        acc = 0.0;
        fill = 0;
        compSum = 0.0;
        compN = 0;
        subs.clear();
    }

    // frames of interleaved float sound; the first two channels are measured (a mono song counts
    // twice, as it is heard on both speakers). gain: what was applied to this sound before it came
    // here (taken back out); 0 = do not use this sound.
    void add(const float *s, size_t frames, int chans, float gain = 1.0f)
    {
        if (chans <= 0)
            return;
        for (size_t i = 0; i < frames; ++i) {
            const double l = kL.process(double(s[i * size_t(chans)]));
            const double r = chans >= 2 ? kR.process(double(s[i * size_t(chans) + 1])) : l;
            acc += l * l + r * r;
            compSum += gain;
            ++compN;
            if (++fill >= subLen)
                endSub();
        }
    }

    // gated integrated loudness, -100 when there is none yet
    double lufs() const { return integrated(subs); }
    // seconds of sound above the absolute gate
    double seconds() const { return audibleSeconds(subs); }

    // the 100 ms block energies (NaN = not usable), for a reading on another thread
    const std::vector<float> &blocks() const { return subs; }

    static double integrated(const std::vector<float> &sub)
    {
        std::vector<double> blk;
        blk.reserve(sub.size());
        for (size_t i = 3; i < sub.size(); ++i) {           // 400 ms block every 100 ms
            const double a = sub[i - 3], b = sub[i - 2], c = sub[i - 1], d = sub[i];
            if (std::isnan(a) || std::isnan(b) || std::isnan(c) || std::isnan(d))
                continue;
            const double ms = (a + b + c + d) / 4.0;
            if (ms > 0.0 && toLufs(ms) > -70.0)
                blk.push_back(ms);
        }
        if (blk.empty())
            return -100.0;
        double sum = 0.0;
        for (double v : blk) sum += v;
        const double relGate = toLufs(sum / double(blk.size())) - 10.0;
        double gated = 0.0;
        int n = 0;
        for (double v : blk)
            if (toLufs(v) > relGate) { gated += v; ++n; }
        return n > 0 ? toLufs(gated / double(n)) : -100.0;
    }

    static double audibleSeconds(const std::vector<float> &sub)
    {
        int n = 0;
        for (float v : sub)
            if (!std::isnan(v) && v > 0.0f && toLufs(v) > -70.0)
                ++n;
        return n / 10.0;
    }

    static double toLufs(double meanSquare) { return -0.691 + 10.0 * std::log10(meanSquare > 1e-12 ? meanSquare : 1e-12); }

private:
    void endSub()
    {
        const double g = compN > 0 ? compSum / double(compN) : 1.0;
        double ms = acc / double(subLen);
        float v;
        if (g < 0.03)                   // (almost) silenced by the volume / a fade: not usable
            v = std::nanf("");
        else
            v = float(ms / (g * g));    // as it would be at full volume
        subs.push_back(v);
        acc = 0.0;
        fill = 0;
        compSum = 0.0;
        compN = 0;
    }

    int rate = 44100, subLen = 4410;
    MasterDsp::detail::KWeight kL, kR;
    double acc = 0.0;
    int fill = 0;
    double compSum = 0.0;
    long long compN = 0;
    std::vector<float> subs;
};

class SongLevel
{
public:
    static SongLevel &get() { static SongLevel s; return s; }

    // ---- audio thread (main mixer DSP)
    void prepare(int sampleRate, int channels)
    {
        std::lock_guard<std::mutex> lock(mx);
        fs = sampleRate > 0 ? sampleRate : 44100;
        chans = channels > 0 ? channels : 2;
        meter.prepare(fs);
        cur = target.load();
    }

    void process(float *s, size_t frames)
    {
        if (resetAsked.exchange(false)) {
            std::lock_guard<std::mutex> lock(mx);
            meter.reset();
        }
        {
            // the reading on the GUI thread takes this lock only for a moment
            std::unique_lock<std::mutex> lock(mx, std::try_to_lock);
            if (lock.owns_lock())
                meter.add(s, frames, chans, sourceGain.load());
        }
        const float t = target.load();
        const unsigned seq = targetSeq.load();
        if (seq != seenSeq) {           // a new gain: glide to it over the asked time
            seenSeq = seq;
            const double rampFrames = (std::max)(1.0, rampMs.load() * 0.001 * fs);
            step = float((t - cur) / rampFrames);
        }
        if (cur == t) {
            if (cur != 1.0f) {
                const size_t n = frames * size_t(chans);
                for (size_t i = 0; i < n; ++i)
                    s[i] *= cur;
            }
            return;
        }
        for (size_t i = 0; i < frames; ++i) {
            if ((step > 0.0f && cur + step >= t) || (step < 0.0f && cur + step <= t) || step == 0.0f)
                cur = t;
            else
                cur += step;
            for (int c = 0; c < chans; ++c)
                s[i * size_t(chans) + size_t(c)] *= cur;
        }
    }

    // ---- GUI thread
    void setGainDb(double db, double glideMs)
    {
        rampMs.store(glideMs);
        target.store(float(MasterDsp::detail::dbToLin(db)));
        targetSeq.fetch_add(1);
    }
    double gainDb() const { return MasterDsp::detail::linToDb(target.load()); }
    // the gain put on the playing song's own channel before the mixer (video / MP3 / YouTube volume)
    void setSourceGain(float g) { sourceGain.store(g); }
    // a new song: measure from here
    void restartMeasure() { resetAsked.store(true); }
    // loudness so far (LUFS, -100 when none) and how many seconds of music it is made of
    void reading(double *lufs, double *seconds)
    {
        std::vector<float> copy;
        {
            std::lock_guard<std::mutex> lock(mx);
            if (resetAsked.load()) {        // asked, not done yet: nothing measured for this song
                copy.clear();
            } else {
                copy = meter.blocks();
            }
        }
        if (lufs) *lufs = LoudnessMeter::integrated(copy);
        if (seconds) *seconds = LoudnessMeter::audibleSeconds(copy);
    }

    // ---- worker thread: a whole file (video / MP3). -100 when it can't be read.
    static double measureFile(const QString &path)
    {
        const HSTREAM h = BASS_StreamCreateFile(FALSE, path.utf16(), 0, 0,
                                                BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_UNICODE);
        if (!h)
            return -100.0;
        BASS_CHANNELINFO ci;
        if (!BASS_ChannelGetInfo(h, &ci) || ci.chans == 0) {
            BASS_StreamFree(h);
            return -100.0;
        }
        LoudnessMeter m;
        m.prepare(int(ci.freq));
        std::vector<float> buf(size_t(16384) * ci.chans);
        for (;;) {
            const DWORD got = BASS_ChannelGetData(h, buf.data(), DWORD(buf.size() * sizeof(float)) | BASS_DATA_FLOAT);
            if (got == DWORD(-1) || got == 0)
                break;
            m.add(buf.data(), size_t(got) / (sizeof(float) * ci.chans), int(ci.chans));
        }
        BASS_StreamFree(h);
        return m.seconds() >= 5.0 ? m.lufs() : -100.0;
    }

private:
    std::mutex mx;
    LoudnessMeter meter;
    int fs = 44100, chans = 2;
    std::atomic<float> target{1.0f};
    std::atomic<unsigned> targetSeq{0};
    std::atomic<double> rampMs{300.0};
    std::atomic<float> sourceGain{1.0f};
    std::atomic<bool> resetAsked{false};
    // audio thread only
    float cur = 1.0f, step = 0.0f;
    unsigned seenSeq = 0;
};

#endif // SONGLEVEL_H
