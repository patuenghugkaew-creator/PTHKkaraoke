// YouTube sound through the main mixer: the embedded browser's audio process is captured (process
// loopback) and played into the synth's main mixer like an MP3 song, so the master EQ / effects /
// volume apply, with key change (BASS_FX) and a vocal cut on top:
//   - AI mode  : UVR MDX-Net separation (AiVocal.h), clean, the sound comes a few seconds late
//                (the picture is delayed to match); vocal and music levels mix freely
//   - fast mode: centre removal in the singing range (VocalRemover.h), no delay
// The browser itself is not muted (on some Windows muting also silences the capture): its audio
// session is turned down to -90 dB, inaudible, and the capture is amplified back by the same amount.
#ifndef YTAUDIO_H
#define YTAUDIO_H

#include "ProcessLoopback.h"
#include "VocalRemover.h"
#include "AiVocal.h"

#ifdef KM_HAVE_PROCLOOP

#include <bass.h>
#include <bassmix.h>
#include <bass_fx.h>
#include <mutex>
#include <vector>

class YtAudio
{
public:
    static const int RATE = AiVocalEngine::SR;          // 44.1 kHz: what the AI models need

    explicit YtAudio(AiVocalEngine *aiEngine) : ai(aiEngine), remover(RATE) {}
    ~YtAudio() { stop(); }

    // mixer: the synth's main mixer; pid: the process that plays the sound
    bool start(DWORD mixer, DWORD pid)
    {
        stop();
        ring.assign(kCapacity, 0.0f);
        readPos = writePos = filled = 0;

        src = BASS_StreamCreate(RATE, 2, BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE, &YtAudio::streamProc, this);
        if (!src)
            return false;
        tempo = BASS_FX_TempoCreate(src, BASS_STREAM_DECODE | BASS_FX_FREESOURCE);
        if (!tempo) {
            BASS_StreamFree(src);
            src = 0;
            return false;
        }
        BASS_ChannelSetAttribute(tempo, BASS_ATTRIB_TEMPO_PITCH, float(semitones));
        BASS_ChannelSetDSP(tempo, &YtAudio::vocalDsp, this, 0);
        if (!BASS_Mixer_StreamAddChannel(mixer, tempo, BASS_MIXER_CHAN_NORAMPIN)) {
            BASS_StreamFree(tempo);
            tempo = src = 0;
            return false;
        }
        cap.setSampleRate(RATE);
        if (!cap.start(pid, [this](const float *d, int n) { push(d, n); }, false)) {
            BASS_Mixer_ChannelRemove(tempo);
            BASS_StreamFree(tempo);
            tempo = src = 0;
            return false;
        }
        capturedPid = pid;
        keepQuiet();
        return true;
    }

    void stop()
    {
        cap.stop();
        if (capturedPid)
            ProcessLoopback::setSessionVolume(capturedPid, 1.0f);   // the browser is heard directly again
        if (tempo) {
            BASS_Mixer_ChannelRemove(tempo);
            BASS_StreamFree(tempo);     // frees the source too
        }
        tempo = src = 0;
        capturedPid = 0;
    }

    // the browser's audio session down to inaudible; new sessions (e.g. the next clip) included
    void keepQuiet()
    {
        if (capturedPid)
            ProcessLoopback::setSessionVolume(capturedPid, kQuiet);
    }

    bool running() const { return tempo != 0 && cap.running(); }
    DWORD pid() const { return capturedPid; }

    void setPitch(int semis)
    {
        semitones = semis;
        if (tempo)
            BASS_ChannelSetAttribute(tempo, BASS_ATTRIB_TEMPO_PITCH, float(semis));
    }
    int pitch() const { return semitones; }

    // vocal cut and its settings
    void setAiMode(bool on)
    {
        if (on == useAi)
            return;
        useAi = on;
        if (on)
            ai->reset();
        applyCut();
    }
    bool aiMode() const { return useAi; }
    void setVocalCut(bool on) { cut = on; applyCut(); }
    bool isVocalCut() const { return cut; }
    void setLevels(float vocals, float music) { cutVocals = vocals; cutMusic = music; applyCut(); }
    // AI: seconds the sound comes late (0 = no delay); the picture should be delayed as much
    double delaySeconds() const { return useAi && running() && ai->isPlaying() ? ai->delaySeconds() : 0.0; }

private:
    static constexpr float kQuiet = 0.00003f;           // about -90 dB at the speakers
    static const size_t kCapacity = RATE * 2;           // 1 s of stereo floats
    static const size_t kMaxLatency = RATE * 2 / 8;     // keep at most 125 ms waiting

    void applyCut()
    {
        remover.setStrength(1.0f - cutVocals);
        if (useAi && ai->isPlaying()) {
            remover.setEnabled(false);
            if (cut)
                ai->setGains(cutVocals, cutMusic);
            else
                ai->setGains(1.0f, 1.0f);       // the original sound
        } else {
            if (useAi) {
                if (cut)
                    ai->setGains(cutVocals, cutMusic);
                else
                    ai->setGains(1.0f, 1.0f);
            }
            remover.setEnabled(cut);
        }
    }

    // capture thread: the sound comes in at kQuiet, so it is amplified back
    void push(const float *d, int n)
    {
        const float gain = 1.0f / kQuiet;
        if (useAi) {
            scratch.resize(size_t(n));
            for (int i = 0; i < n; i++)
                scratch[size_t(i)] = d[i] * gain;
            ai->push(scratch.data(), n / 2);
            if (ai->isPlaying())
                return;
        }
        std::lock_guard<std::mutex> lock(m);
        for (int i = 0; i < n; i++) {
            ring[writePos] = d[i] * gain;
            writePos = (writePos + 1) % kCapacity;
            if (filled < kCapacity)
                filled++;
            else
                readPos = (readPos + 1) % kCapacity;
        }
        while (filled > kMaxLatency) {      // the mixer fell behind: drop the oldest, stay in time with the picture
            readPos = (readPos + 2) % kCapacity;
            filled -= 2;
        }
    }

    // BASS thread: never runs dry (silence when nothing was captured), so the mixer keeps the channel
    static DWORD CALLBACK streamProc(HSTREAM, void *buffer, DWORD length, void *user)
    {
        YtAudio *self = static_cast<YtAudio *>(user);
        float *out = static_cast<float *>(buffer);
        const size_t want = length / sizeof(float);
        if (self->useAi) {
            if (self->ai->isPlaying()) {
                self->remover.setEnabled(false);
                self->ai->pull(out, int(want / 2));
                return length;
            }
            self->remover.setEnabled(self->cut);
        }
        std::lock_guard<std::mutex> lock(self->m);
        size_t i = 0;
        for (; i < want && self->filled > 0; i++) {
            out[i] = self->ring[self->readPos];
            self->readPos = (self->readPos + 1) % kCapacity;
            self->filled--;
        }
        for (; i < want; i++)
            out[i] = 0.0f;
        return length;
    }

    // fast vocal cut (see VocalRemover.h); always running, so switching it is smooth
    static void CALLBACK vocalDsp(HDSP, DWORD, void *buffer, DWORD length, void *user)
    {
        static_cast<YtAudio *>(user)->remover.process(static_cast<float *>(buffer), length / (2 * sizeof(float)));
    }

    AiVocalEngine *ai;
    ProcessLoopback cap;
    std::mutex m;
    std::vector<float> ring, scratch;
    size_t readPos = 0, writePos = 0, filled = 0;
    HSTREAM src = 0, tempo = 0;
    DWORD capturedPid = 0;
    int semitones = 0;
    VocalRemover remover;
    std::atomic<bool> useAi{false};
    bool cut = false;
    float cutVocals = 0.0f, cutMusic = 1.0f;
};

#endif // KM_HAVE_PROCLOOP
#endif // YTAUDIO_H
