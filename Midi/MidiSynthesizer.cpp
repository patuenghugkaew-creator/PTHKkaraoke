#include "MidiSynthesizer.h"
#include "Midi/FaderTaper.h"

#include "BASSFX/AutoWahFX.h"
#include "BASSFX/ChorusFX.h"
#include "BASSFX/CompressorFX.h"
#include "BASSFX/DistortionFX.h"
#include "BASSFX/EchoFX.h"
#include "BASSFX/Equalizer15BandFX.h"
#include "BASSFX/ReverbFX.h"
#include "BASSFX/VSTFX.h"
#include "Midi/AsioOutput.h"

#include <cstring>
#include <memory>
#include <vector>
#include <atomic>
#include <cmath>
#include <utility>
#include <QTimer>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QCoreApplication>
#include <QThread>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <mutex>
#include <chrono>
#include <thread>
#include <cstdint>
#include <cstddef>
#include "Midi/MidiPlayer.h"
#include "Utils.h"

// defined further down (used while the streams are created)
namespace { void attachTrimDsp(InstrumentType t, DWORD handle); }

// Last value sent for every channel's controller / program / pitch ... (not notes). A VSTi plug-in cannot be asked for
// its channel state, so it is kept here and sent again when a VSTi stream is recreated (e.g. speaker change).
namespace {
QMutex g_evMx;
QHash<quint32, DWORD> g_evState;

void trackEvent(int ch, DWORD et, DWORD param)
{
    if (ch < 0 || ch > 15)
        return;
    if (et == MIDI_EVENT_NOTE || et == MIDI_EVENT_NOTESOFF || et == MIDI_EVENT_SOUNDOFF || et == MIDI_EVENT_KEYPRES)
        return;
    QMutexLocker lock(&g_evMx);
    if (et == MIDI_EVENT_RESET) {               // all controllers reset: forget this channel's state
        for (auto it = g_evState.begin(); it != g_evState.end(); ) {
            if ((it.key() >> 24) == quint32(ch))
                it = g_evState.erase(it);
            else
                ++it;
        }
        return;
    }
    g_evState[(quint32(ch) << 24) | (et & 0xFFFFFF)] = param;
}

#ifndef __linux__
void replayVstiEvents(HSTREAM h)
{
    if (!h)
        return;
    QMutexLocker lock(&g_evMx);
    for (auto it = g_evState.constBegin(); it != g_evState.constEnd(); ++it)
        bv::ProcessEvent(h, int(it.key() >> 24), DWORD(it.key() & 0xFFFFFF), it.value());
}
#endif
}

QMap<int, QString> MidiSynthesizer::outDevices;

MidiSynthesizer::MidiSynthesizer(QObject *parent) : QObject(parent)
{   
    timer.setInterval(8 * 60000);
    timer.start();

    connect(&timer, SIGNAL(timeout()), this, SLOT(compactSoundfont()));

    // create mixers
    for (int dv : outDevices.keys())
    {
        MixerHandle mixer;
        mixer.handle = 0;

        mixer.eq = new Equalizer31BandFX(0, 1);
        mixer.chorus = new Chorus2FX(0, 2);
        mixer.reverb = new Reverb2FX(0, 3);

        mixers.append(mixer);
    }

    QList<int> iSF, dSF;
    for (int i=0; i<128; i++) {
        iSF.append(0);
    }

    for (int i=0; i<16; i++) {
        dSF.append(0);
    }

    for (int i = 0; i < SF_PRESET_COUNT; i++) {
        instmSf.append(iSF);
        drumSf.append(dSF);
    }

    #ifndef __linux__
    BASS_VST_INFO vstinfo;
    for (int i=0; i<4; i++)
    {
        mVstiFiles[i] = "";
        mVstiInfos[i] = vstinfo;
        mVstiTempProgram[i] = 0;
        mVstiTempParams[i] = QList<float>();
        mVstiChunk[i] = QByteArray();
    }
    #endif


    // Map inst
    for (int i=0; i<HANDLE_STREAM_COUNT; i++)
    {
        InstrumentType t = static_cast<InstrumentType>(i);
        Instrument im;
        im.type = t;
        im.mute = false;
        im.solo = false;
        im.enable = true;
        im.volume = 50;
        im.pan = 0;
        im.reverb = 0;
        im.trimDb = 0.0;
        im.bus = -1;
        im.vsti = -1;
        im.device = 0;
        im.speaker = SpeakerType::Default;

        instMap[t] = im;

        handles[t] = 0;
    }

    for (int i=0; i<16; i++)
    {
        chInstType[i] = InstrumentType::Piano;
        chProgram[i] = 0;
    }
    chInstType[9] = InstrumentType::PercussionEtc;
}

MidiSynthesizer::~MidiSynthesizer()
{
    timer.stop();

    for (MidiOut *o : extPorts.values()) {
        if (o) {
            try { o->closePort(); } catch (...) {}
            delete o;
        }
    }
    extPorts.clear();

    if (openned)
        close();

    // Free soundfont
    for (HSOUNDFONT f : synth_HSOUNDFONT) {
        BASS_MIDI_FontUnload(f, -1, -1);
        BASS_MIDI_FontFree(f);
    }
    synth_HSOUNDFONT.clear();

    // free per-channel reverbs
    qDeleteAll(sendRv);
    sendRv.clear();

    // free instruments fx
    for (Instrument inst : instMap.values())
    {
        for (FX *fx : inst.FXs)
            delete fx;
    }


    // free master fx
    qDeleteAll(masterFXs);
    masterFXs.clear();

    // free mixer fx
    for (MixerHandle mixer : mixers)
    {
        delete mixer.eq;
        delete mixer.reverb;
        delete mixer.chorus;
    }
    mixers.clear();

    // --------------

    instMap.clear();

    sfFiles.clear();
    instmSf.clear();
    drumSf.clear();
}

bool MidiSynthesizer::open()
{
    if (openned)
        return true;

    openned = true;

    // ASIO is fed in float anyway: a float decoding mixer needs no restart when the limiter / auto level is switched on
    DWORD f = (useFloat || asioDev >= 0) ? BASS_SAMPLE_FLOAT : 0;

    // Coming back from ASIO: the driver (ASIO4ALL especially) takes the sound card exclusively, which leaves BASS's
    // normal output devices dead ("BASS_Start has not been called", silent until the program restarts).
    // Re-initialise them before any mixer is attached to them.
    if (asioDev < 0 && AsioOutput::recentlyStopped(8000))
    {
        AsioOutput::waitSettle(1000);
        const DWORD keepDev = BASS_GetDevice();
        for (int d : outDevices.keys())
        {
            if (d <= 0) continue;
            if (BASS_SetDevice((DWORD)d)) {
                BASS_Free();
                BASS_Init((DWORD)d, 44100, BASS_DEVICE_SPEAKERS, NULL, NULL);
            }
        }
        if (keepDev != (DWORD)-1) BASS_SetDevice(keepDev);
        AsioOutput::logLine(QStringLiteral("back to Windows Audio: output devices re-initialised"));
    }

    // create mixer, bus
    for (int i=0; i<mixers.count(); i++)
    {
        MixerHandle mixer = mixers[i];
        // ASIO: the main mixer only decodes; BASSASIO pulls the audio from it. The mixers of the other Windows
        // devices must NOT play either: they would keep the sound card (e.g. the USB headset that ASIO4ALL is
        // trying to open) busy, and the ASIO driver then stays silent / takes ages to start.
        const bool asioMain = (asioDev >= 0);
        // NONSTOP: a mixer with nothing in it must keep running, not end (an ended mixer never plays again)
        mixer.handle = BASS_Mixer_StreamCreate(sampleRate, 8, (asioMain ? (f | BASS_STREAM_DECODE) : f) | BASS_MIXER_NONSTOP);
        mixer.eq->setStreamHandle(mixer.handle);
        mixer.reverb->setStreamHandle(mixer.handle);
        mixer.chorus->setStreamHandle(mixer.handle);

        if (!asioMain)
        {
            DWORD device = (i == 0) ? defaultDev : outDevices.keys()[i];
            // right after leaving ASIO the driver (ASIO4ALL especially) may still hold the sound card for a moment:
            // give it time to let go and retry until the mixer really plays, otherwise Windows Audio stays silent
            const bool afterAsio = AsioOutput::recentlyStopped(8000);
            if (afterAsio)
                AsioOutput::waitSettle(800);
            BASS_ChannelSetDevice(mixer.handle, device);
            bool playing = BASS_ChannelPlay(mixer.handle, false) != FALSE;
            int tries = 0;
            while (afterAsio && !playing && tries < 12) {
                QThread::msleep(250);
                tries++;
                BASS_ChannelSetDevice(mixer.handle, device);
                playing = BASS_ChannelPlay(mixer.handle, false) != FALSE;
            }
            if (afterAsio)
                AsioOutput::logLine(QString("back to Windows Audio: mixer %1 device=%2 playing=%3 retries=%4 bassErr=%5")
                                    .arg(i).arg(device).arg(playing).arg(tries).arg(BASS_ErrorGetCode()));
        }

        mixers[i] = mixer;
    }

    // master effects ride on the main output mixer
    if (!mixers.isEmpty())
        for (FX *fx : masterFXs)
            fx->setStreamHandle(mixers[0].handle);
        BASS_CHANNELINFO ci;
        if (BASS_ChannelGetInfo(mixers[0].handle, &ci)) {
            masterChannels = ci.chans;
            PTHKCore::initMasterDsp(ci.chans, ci.freq);
            if (!masterDspHandle)
                masterDspHandle = BASS_ChannelSetDSP(mixers[0].handle, &MidiSynthesizer::masterDspCallback, this, -990);
        }

    // create midi and bus stream
    for (int i=0; i<HANDLE_STREAM_COUNT; i++)
    {
        InstrumentType t = static_cast<InstrumentType>(i);
        handles[t] = createStream(t);
        attachTrimDsp(t, handles[t]);
    }

    // Check device.. volume .. mute.. solo.. bus.. and VST
    for (int i=0; i<HANDLE_STREAM_COUNT; i++)
    {
        InstrumentType t = static_cast<InstrumentType>(i);
        setDevice(t, instMap[t].device);
        setVolume(t, instMap[t].volume);
        setMute(t, instMap[t].mute);
        setSolo(t, instMap[t].solo);
        setBusGroup(t, instMap[t].bus);  // speaker will set here

        // Set fx to stream handle
        for (FX *fx : instMap[t].FXs)
            fx->setStreamHandle(handles[t]);
        attachSendReverb(t, handles[t]);
    }

    setSfToStream();
    setSoundfontPresets(sfPreset);
    setVolume(synth_volume);

    // start the ASIO driver last, once everything is connected to the mixer
    if (asioDev >= 0 && !mixers.isEmpty())
    {
        AsioOutput::setRate(sampleRate);
        if (!AsioOutput::start(mixers[0].handle, asioDev))
        {
            // could not use ASIO: fall back to the normal Windows device
            asioErr = AsioOutput::lastError();
            asioDev = -1;
            close();
            return open();
        }
    }

    return true;
}

void MidiSynthesizer::close()
{
    if (!openned)
        return;

    if (masterDspHandle && !mixers.isEmpty()) {
        BASS_ChannelRemoveDSP(mixers[0].handle, masterDspHandle);
        masterDspHandle = 0;
    }

    // Clear FX
    for (InstrumentType t : instMap.keys())
    {
        if (isBusType(t))
            continue;

        for (FX *fx : instMap[t].FXs) {
            fx->setStreamHandle(0);
        }
    }

    for (InstrumentType t : sendRv.keys())
        attachSendReverb(t, 0);

    // clear handles
    for (InstrumentType t: handles.keys())
    {
        if (isBusType(t))
            continue;

        HSTREAM h = handles[t];

        if (t==InstrumentType::VSTi1 || t==InstrumentType::VSTi2
         || t==InstrumentType::VSTi3 || t==InstrumentType::VSTi4)
        {
            #ifndef __linux__
            int vIndex = static_cast<int>(t) - HANDLE_VSTI_START;
            mVstiTempProgram[vIndex] = bv::GetProgram(h);
            mVstiTempParams[vIndex] = FX::getVSTParams(h);

            DWORD length = 0;
            char *chunk = bv::GetChunk(h, false, &length);
            mVstiChunk[vIndex] = QByteArray(chunk, length);

            bv::ChannelFree(h);
            #endif
        }
        else
            BASS_StreamFree(h);

        handles[t] = 0;
    }

    // stop ASIO before the mixer it reads from is freed
    AsioOutput::stop();

    // master effects must let go of the mixer before it is freed
    for (FX *fx : masterFXs)
        fx->setStreamHandle(0);

    // clear mixers fx
    for (int i=0; i<mixers.count(); i++)
    {
        MixerHandle mixer = mixers[i];
        mixer.eq->setStreamHandle(0);
        mixer.reverb->setStreamHandle(0);
        mixer.chorus->setStreamHandle(0);

        BASS_ChannelStop(mixer.handle);
        BASS_StreamFree(mixer.handle);

        mixer.handle = 0;
        mixers[i] = mixer;
    }

    // compact soundfont
    BASS_MIDI_FontCompact(0);

    openned = false;
}

int MidiSynthesizer::defaultDevice()
{
    return defaultDev;
}

bool MidiSynthesizer::setAsioDevice(int asioIndex)
{
    if (asioIndex == asioDev)
        return true;

    const bool wasOpen = openned;
    if (wasOpen)
        close();

    asioDev = asioIndex;
    asioErr.clear();

    if (wasOpen)
        open();      // falls back to asioDev = -1 if the driver can't be started

    return asioDev == asioIndex;
}

bool MidiSynthesizer::setAsioDeviceKeepState(int asioIndex)
{
    if (asioIndex == asioDev)
        return true;
    if (!openned)
        return setAsioDevice(asioIndex);

    const QVector<DWORD> state = snapshotChannels();
    const bool ok = setAsioDevice(asioIndex);
    setSoundfontPresets(sfPreset);
    restoreChannels(state);
    return ok;
}

bool MidiSynthesizer::setDefaultDevice(int dv)
{
    if (openned && asioDev >= 0)
        setAsioDevice(-1);      // leaving ASIO: rebuild the main mixer on a normal device

    if (openned)
    {
        DWORD mix = mixers[0].handle;
        BASS_ChannelStop(mix);
        bool rs = BASS_ChannelSetDevice(mix, dv);
        BASS_ChannelPlay(mix, false);

        if (rs)
        {
            defaultDev = dv;
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        if (dv < audioDevices().count())
        {
            defaultDev = dv;
            return true;
        }
        else
            return false;
    }
}

bool MidiSynthesizer::spectrum(float *fft1024, int *rateHz)
{
    if (!openned)
        return false;
    if (rateHz)
        *rateHz = sampleRate > 0 ? sampleRate : 44100;
    if (AsioOutput::isActive()) {
        // the main mixer is a decoding stream pulled by the ASIO driver: it can't be read here, so analyse the copy
        // of the signal that was sent to the driver
        const int N = 2048;
        float x[N];
        int rate = 44100;
        if (!AsioOutput::tapRead(x, N, rate))
            return false;
        if (rateHz)
            *rateHz = rate;
        static float re[N], im[N];
        for (int i = 0; i < N; i++) {
            const float w = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * i / (N - 1));   // Hann
            re[i] = x[i] * w;
            im[i] = 0.0f;
        }
        for (int i = 1, j = 0; i < N; i++) {   // bit reversal
            int bit = N >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (int len = 2; len <= N; len <<= 1) {
            const float ang = -2.0f * 3.14159265f / len;
            const float wr = std::cos(ang), wi = std::sin(ang);
            for (int i = 0; i < N; i += len) {
                float cr = 1.0f, ci = 0.0f;
                for (int k = 0; k < len / 2; k++) {
                    const int a = i + k, b = i + k + len / 2;
                    const float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                    const float ncr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr;
                    cr = ncr;
                }
            }
        }
        for (int i = 0; i < 1024; i++)   // same scale as BASS_DATA_FFT2048 (full-scale sine ~ 1.0)
            fft1024[i] = std::sqrt(re[i] * re[i] + im[i] * im[i]) * (4.0f / N);
        return true;
    }
    for (const MixerHandle &m : mixers) {
        if (m.handle == 0)
            continue;
        const DWORD r = BASS_ChannelGetData(m.handle, fft1024, BASS_DATA_FFT2048);
        return r != (DWORD)-1;
    }
    return false;
}

// Volume changes are glided over a few ms instead of jumping: a fader step is up to 1.2 dB below 0 dB, which
// is audible as zipper noise / crackle while dragging. Decoding (ASIO) or stopped channels can't slide, so they
// are set directly.
// Decoding streams (every strip and bus is one) cannot use BASS's own attribute slide, so a fader move
// used to jump straight to the new gain in one step (zipper noise). GainRamper glides them there in a
// straight line (linear in gain) over ~40 ms, driven by a timer on the GUI thread.
namespace {
class GainRamper : public QObject
{
public:
    GainRamper()
    {
        rampTimer = new QTimer(this);
        rampTimer->setInterval(8);
        connect(rampTimer, &QTimer::timeout, this, [this]() { tick(); });
    }

    void set(DWORD h, float target)
    {
        {
            QMutexLocker lock(&mx);
            float cur = 0.0f;
            if (!BASS_ChannelGetAttribute(h, BASS_ATTRIB_VOL, &cur)) {
                ramps.remove(h);
                return;
            }
            auto it = ramps.find(h);
            if (it != ramps.end())
                cur = it->cur;                   // continue from where the last glide got to
            if (std::fabs(cur - target) < 0.0005f) {
                ramps.remove(h);
                BASS_ChannelSetAttribute(h, BASS_ATTRIB_VOL, target);
                return;
            }
            Ramp r;
            r.cur = cur;
            r.target = target;
            r.step = (target - cur) / float(kSteps);
            ramps[h] = r;
        }
        QMetaObject::invokeMethod(this, [this]() { if (!rampTimer->isActive()) rampTimer->start(); }, Qt::QueuedConnection);
    }

private:
    struct Ramp { float cur = 0, target = 0, step = 0; };
    static const int kSteps = 5;   // x 8 ms

    void tick()
    {
        QMutexLocker lock(&mx);
        for (auto it = ramps.begin(); it != ramps.end(); ) {
            Ramp &r = it.value();
            r.cur += r.step;
            const bool done = (r.step >= 0.0f) ? (r.cur >= r.target) : (r.cur <= r.target);
            if (done)
                r.cur = r.target;
            BASS_ChannelSetAttribute(it.key(), BASS_ATTRIB_VOL, r.cur);
            if (done)
                it = ramps.erase(it);
            else
                ++it;
        }
        if (ramps.isEmpty())
            rampTimer->stop();
    }

    QTimer *rampTimer = nullptr;
    QMutex mx;
    QHash<DWORD, Ramp> ramps;
};

GainRamper *gainRamper()
{
    static GainRamper *g = []() {
        GainRamper *p = new GainRamper();
        if (QCoreApplication::instance())
            p->moveToThread(QCoreApplication::instance()->thread());
        return p;
    }();
    return g;
}
}

// the strip's gain from the fader. The fine Trim is NOT part of it: it is a separate gain stage (a DSP on the
// strip's own stream, below), so it works the same whatever the output path is.
static float stripGain(const Instrument &im)
{
    return FaderTaper::gain(im.volume);
}

// ---- per-strip Trim: a gain applied by a DSP on the strip's stream (runs last, after the strip's effects) ----
namespace {
struct TrimDsp
{
    std::atomic<float> target{1.0f};    // set from the GUI thread
    float cur = 1.0f;                   // audio thread only
};
TrimDsp g_trimDsp[64];                  // one per strip (MIDI / VSTi / bus), index = InstrumentType

void CALLBACK trimDspProc(HDSP, DWORD channel, void *buffer, DWORD length, void *user)
{
    TrimDsp *d = static_cast<TrimDsp *>(user);
    const float t = d->target.load(std::memory_order_relaxed);
    if (d->cur == t && t == 1.0f)
        return;
    BASS_CHANNELINFO info;
    if (!BASS_ChannelGetInfo(channel, &info))
        return;
    const bool isFloat = (info.flags & BASS_SAMPLE_FLOAT) != 0;
    const size_t chans = info.chans ? size_t(info.chans) : 1;
    const size_t count = length / (isFloat ? 4 : 2);       // samples
    const size_t frames = count / chans;
    if (!frames)
        return;
    const float from = d->cur;
    const float step = (t - from) / float(frames);        // glide over this block: no zipper noise
    if (isFloat) {
        float *p = static_cast<float *>(buffer);
        for (size_t f = 0; f < frames; f++) {
            const float g = from + step * float(f + 1);
            for (size_t c = 0; c < chans; c++)
                p[f * chans + c] *= g;
        }
    } else {
        short *p = static_cast<short *>(buffer);
        for (size_t f = 0; f < frames; f++) {
            const float g = from + step * float(f + 1);
            for (size_t c = 0; c < chans; c++) {
                float v = float(p[f * chans + c]) * g;
                if (v > 32767.0f) v = 32767.0f;
                if (v < -32768.0f) v = -32768.0f;
                p[f * chans + c] = short(v);
            }
        }
    }
    d->cur = t;
}

void attachTrimDsp(InstrumentType t, DWORD handle)
{
    const int i = static_cast<int>(t);
    if (!handle || i < 0 || i >= 64)
        return;
    g_trimDsp[i].cur = g_trimDsp[i].target.load();         // a new stream starts at the current trim, no glide
    BASS_ChannelSetDSP(handle, trimDspProc, &g_trimDsp[i], -1000);
}
}

static void setVolSmooth(DWORD handle, float gain)
{
    if (handle == 0)
        return;
    BASS_CHANNELINFO info;
    if (!BASS_ChannelGetInfo(handle, &info))
        return;
    if (!(info.flags & BASS_STREAM_DECODE)) {
        if (BASS_ChannelIsActive(handle) != BASS_ACTIVE_STOPPED)
            BASS_ChannelSlideAttribute(handle, BASS_ATTRIB_VOL, gain, 30);
        else
            BASS_ChannelSetAttribute(handle, BASS_ATTRIB_VOL, gain);
    } else {
        gainRamper()->set(handle, gain);
    }
}

void MidiSynthesizer::setVolume(float vol)
{
    synth_volume = vol;
    AsioOutput::setGain(vol * FaderTaper::kOutputHeadroom);   // ASIO feed: a decoding mixer ignores its volume attribute
    for (MixerHandle mix : mixers)
        setVolSmooth(mix.handle, vol * FaderTaper::kOutputHeadroom);
}

bool MidiSynthesizer::addSoundfont(const QString &sfFile)
{
    HSOUNDFONT sf = 0;
    if (PTHKCore::isProtectedSoundfont(sfFile)) {
        sf = PTHKCore::openProtectedSoundfont(sfFile, BASS_MIDI_FONT_NOFX);
    } else {
    #ifdef _WIN32
        sf = BASS_MIDI_FontInit(sfFile.toStdWString().c_str(), BASS_MIDI_FONT_NOFX);
    #else
        sf = BASS_MIDI_FontInit(sfFile.toStdString().c_str(), BASS_MIDI_FONT_NOFX);
    #endif
    }

    if (!sf)
        return false;

    this->sfFiles.append(sfFile);

    if (sfLoadAll)
        BASS_MIDI_FontLoad(sf, -1, -1);
    BASS_MIDI_FontCompact(sf);

    synth_HSOUNDFONT.push_back(sf);

    if (!openned)
        return true;

    if (synth_HSOUNDFONT.size() == 1)
        setSfToStream();

    return true;
}

void MidiSynthesizer::removeSoundfont(int sfIndex)
{
    if (sfIndex < 0 || sfIndex >= synth_HSOUNDFONT.count())
        return;

    HSOUNDFONT sf = synth_HSOUNDFONT.takeAt(sfIndex);
    BASS_MIDI_FontUnload(sf, -1, -1);
    BASS_MIDI_FontFree(sf);
    sfFiles.removeAt(sfIndex);

    for (int presetIndex = 0; presetIndex < SF_PRESET_COUNT; presetIndex++) {
        QList<int> instSfMap = instmSf[presetIndex];
        QList<int> drumSfMap = drumSf[presetIndex];

        for (int i=0; i<instSfMap.count(); i++)
        {
            if (instSfMap[i] == sfIndex)
                instSfMap[i] = 0;
            else if (instSfMap[i] > sfIndex)
                instSfMap[i] = instSfMap[i] - 1;
        }

        for (int i=0; i<drumSfMap.count(); i++)
        {
            if (drumSfMap[i] == sfIndex)
                drumSfMap[i] = 0;
            else if (drumSfMap[i] > sfIndex)
                drumSfMap[i] = drumSfMap[i] - 1;
        }

        setMapSoundfontIndex(presetIndex, instSfMap, drumSfMap);
    }
}

void MidiSynthesizer::swapSoundfont(int sfIndex, int toIndex)
{
    if (sfIndex < 0 || sfIndex >= synth_HSOUNDFONT.count())
        return;

    if (toIndex < 0 || toIndex >= synth_HSOUNDFONT.count())
        return;

    synth_HSOUNDFONT.swap(sfIndex, toIndex);
    sfFiles.swap(sfIndex, toIndex);

    for (int presetIndex = 0; presetIndex < SF_PRESET_COUNT; presetIndex++) {
        QList<int> instSfMap = instmSf[presetIndex];
        QList<int> drumSfMap = drumSf[presetIndex];

        for (int i=0; i<instSfMap.count(); i++)
        {
            if (instSfMap[i] == 0)
                continue;
            if (instSfMap[i] == sfIndex)
                instSfMap[i] = toIndex;
        }

        for (int i=0; i<drumSfMap.count(); i++)
        {
            if (drumSfMap[i] == 0)
                continue;
            if (drumSfMap[i] == sfIndex)
                drumSfMap[i] = toIndex;
        }

        setMapSoundfontIndex(presetIndex, instSfMap, drumSfMap);
    }
}

float MidiSynthesizer::soundfontVolume(int sfIndex)
{
    if (sfIndex < 0 || sfIndex >= synth_HSOUNDFONT.count())
        return -1;

    return BASS_MIDI_FontGetVolume(synth_HSOUNDFONT[sfIndex]);
}

void MidiSynthesizer::setSoundfontVolume(int sfIndex, float sfvl)
{
    if (sfIndex < 0 || sfIndex >= synth_HSOUNDFONT.count())
        return;

    BASS_MIDI_FontSetVolume(synth_HSOUNDFONT[sfIndex], sfvl);
}

void MidiSynthesizer::setLoadAllSoundfont(bool loadAll)
{
    if (loadAll == sfLoadAll)
        return;

    sfLoadAll = loadAll;

    for (HSOUNDFONT sf : synth_HSOUNDFONT)
    {
        if (sfLoadAll)
            BASS_MIDI_FontLoad(sf, -1, -1);
        BASS_MIDI_FontCompact(sf);
    }
}

void MidiSynthesizer::setStripSoundfont(InstrumentType t, const QString &sfPath, bool apply)
{
    const int key = static_cast<int>(t);
    if (key < 0 || !isMidiStrip(key))
        return;

    if (sfPath.isEmpty())
        stripSf.remove(key);
    else
        stripSf[key] = sfPath;

    if (apply)
        setSoundfontPresets(sfPreset);
}

bool MidiSynthesizer::setMapSoundfontIndex(int presetIndex, QList<int> intrumentSfIndex, QList<int> drumSfIndex)
{
    instmSf[presetIndex].clear();
    drumSf[presetIndex].clear();
    instmSf[presetIndex] = intrumentSfIndex;
    drumSf[presetIndex] = drumSfIndex;

    if (intrumentSfIndex.count() < 128 || synth_HSOUNDFONT.count() == 0 || !openned)
        return false;

    if (this->sfPreset == presetIndex) {
        setSoundfontPresets(presetIndex);
    }


    return true;
}

void MidiSynthesizer::setSoundfontPresets(int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= SF_PRESET_COUNT)
        return;

    this->sfPreset = presetIndex;

    if (synth_HSOUNDFONT.count() == 0 || !openned)
        return;

    std::vector<BASS_MIDI_FONT> mFonts;

    // check instrument use another sf
    for (int i=0; i<128; i++)
    {
        if (instmSf[presetIndex].at(i) <= 0)
            continue;

        if (instmSf[presetIndex].at(i) >= synth_HSOUNDFONT.count())
        {
            instmSf[presetIndex][i] = 0;
            continue;
        }

        BASS_MIDI_FONT font;
        font.font = synth_HSOUNDFONT.at(instmSf[presetIndex].at(i));
        font.preset = i;
        font.bank = 0;

        mFonts.push_back(font);
    }

    // defaut sf
    if (synth_HSOUNDFONT.count() > 0) {
        BASS_MIDI_FONT f;
        f.font = synth_HSOUNDFONT.at(0);
        f.preset = -1;
        f.bank = 0;
        mFonts.push_back(f);
    }

    // Preset 101..110: melodic strips look for the soundfont's bank 101..110 first, then bank 000
    const int bankNo = sfPresetBankNumber(presetIndex);
    // program -1 = every program: written out one by one (spreset == dpreset), so each preset
    // such as 101:026 is mapped explicitly and does not depend on how "-1" is interpreted
    auto addBank = [bankNo](std::vector<BASS_MIDI_FONTEX> &v, HSOUNDFONT f, int program) {
        // Only presets that really exist are mapped (a mapping to a missing preset would still be
        // picked and play the default piano): bank 101..110 when the soundfont has it for this
        // program, otherwise the normal bank 000 (soundfonts that only have 000:000-127).
        const int first = program < 0 ? 0 : program;
        const int last  = program < 0 ? 127 : program;
        for (int p = first; p <= last; p++) {
            int srcBank = -1;
            if (BASS_MIDI_FontGetPreset(f, p, bankNo) != nullptr)
                srcBank = bankNo;
            else if (BASS_MIDI_FontGetPreset(f, p, 0) != nullptr)
                srcBank = 0;
            if (srcBank < 0)
                continue;
            BASS_MIDI_FONTEX e;
            e.font = f; e.spreset = p; e.sbank = srcBank;
            e.dpreset = p; e.dbank = 0; e.dbanklsb = 0;
            v.push_back(e);                         // played as bank 0
        }
    };
    std::vector<BASS_MIDI_FONTEX> exFonts;           // all strips without an own soundfont
    if (bankNo != 0) {
        for (int i=0; i<128; i++) {
            const int sfIdx = instmSf[presetIndex].at(i);
            if (sfIdx > 0 && sfIdx < synth_HSOUNDFONT.count())
                addBank(exFonts, synth_HSOUNDFONT.at(sfIdx), i);
        }
        if (synth_HSOUNDFONT.count() > 0)
            addBank(exFonts, synth_HSOUNDFONT.at(0), -1);
    }

    // set to stream
    const int startDrumType = static_cast<int>(InstrumentType::BassDrum);
    for (int i=0; i<HANDLE_STREAM_COUNT; i++) {
        if (!isMidiStrip(i))
            continue;
        HSTREAM h = handles[static_cast<InstrumentType>(i)];
        const bool melodic = (i < startDrumType) || i == HANDLE_CANTHAI;

        // this strip uses its own soundfont (chosen from the strip's icon menu)
        const int own = melodic ? sfFiles.indexOf(stripSf.value(i)) : -1;
        if (bankNo != 0 && melodic && synth_HSOUNDFONT.count() > 0) {
            std::vector<BASS_MIDI_FONTEX> fx;
            if (own > 0 && own < synth_HSOUNDFONT.count()) {
                addBank(fx, synth_HSOUNDFONT.at(own), -1);   // the strip's own soundfont first
                addBank(fx, synth_HSOUNDFONT.at(0), -1);     // then the default soundfont
            } else if (own == 0) {
                addBank(fx, synth_HSOUNDFONT.at(0), -1);
            } else {
                fx = exFonts;
            }
            BASS_MIDI_StreamSetFonts(h, (const void *)fx.data(), (DWORD)fx.size() | BASS_MIDI_FONT_EX);
            continue;
        }
        if (own >= 0 && own < synth_HSOUNDFONT.count()) {
            BASS_MIDI_FONT two[2];
            two[0].font = synth_HSOUNDFONT.at(own);
            two[0].preset = -1;
            two[0].bank = 0;
            two[1].font = synth_HSOUNDFONT.at(0);   // fallback for programs the chosen soundfont lacks
            two[1].preset = -1;
            two[1].bank = 0;
            BASS_MIDI_StreamSetFonts(h, two, own == 0 ? 1 : 2);
            continue;
        }

        BASS_MIDI_StreamSetFonts(h, mFonts.data(), mFonts.size());
    }


    // check drum sf
    int startDrum = static_cast<int>(InstrumentType::BassDrum);
    int li = 0;
    for (int i=startDrum; i<HANDLE_MIDI_COUNT-4; i++)
    {
        if (drumSf[presetIndex].at(li) < 0 || drumSf[presetIndex].at(li) >= synth_HSOUNDFONT.count())
            drumSf[presetIndex][li] = 0;

        int drumSfIdx = drumSf[presetIndex].at(li);
        const int ownDrum = sfFiles.indexOf(stripSf.value(i));
        if (ownDrum >= 0 && ownDrum < synth_HSOUNDFONT.count())
            drumSfIdx = ownDrum;   // soundfont chosen for this drum strip

        InstrumentType type = static_cast<InstrumentType>(i);

        // Drum kit of the preset = drum bank 128 with the preset's number (000 -> 128:000, 101 -> 128:101 ...).
        // It replaces whatever kit the song asks for, when this soundfont has it; a soundfont without
        // that kit keeps the kits the song asks for.
        // The kit of the "lock drum" button (when the soundfont has it) wins over the preset's kit: every
        // kit number the song asks for is played with it, so the change does not depend on a program change.
        int kitSrc = bankNo;
        if (lockedKit >= 0 && BASS_MIDI_FontGetPreset(synth_HSOUNDFONT.at(drumSfIdx), lockedKit, 128) != nullptr)
            kitSrc = lockedKit;
        const bool hasPresetKit = BASS_MIDI_FontGetPreset(synth_HSOUNDFONT.at(drumSfIdx), kitSrc, 128) != nullptr;
        if (bankNo != 0 || hasPresetKit)
        {
            std::vector<BASS_MIDI_FONTEX> kits;
            const HSOUNDFONT sf = synth_HSOUNDFONT.at(drumSfIdx);
            if (hasPresetKit) {
                for (int p = 0; p < 128; p++) {
                    BASS_MIDI_FONTEX e;
                    e.font = sf; e.spreset = kitSrc; e.sbank = 128;
                    e.dpreset = p; e.dbank = 128; e.dbanklsb = 0;
                    kits.push_back(e);
                }
            }
            for (int bk = 128; bk >= 127; bk--) {   // normal drum kits, then the XG drum bank
                for (int p = 0; p < 128; p++) {
                    if (BASS_MIDI_FontGetPreset(sf, p, bk) == nullptr)
                        continue;                // only kits that exist
                    BASS_MIDI_FONTEX e;
                    e.font = sf; e.spreset = p; e.sbank = bk;
                    e.dpreset = p; e.dbank = bk; e.dbanklsb = 0;
                    kits.push_back(e);
                }
            }
            BASS_MIDI_StreamSetFonts(handles[type], (const void *)kits.data(), (DWORD)kits.size() | BASS_MIDI_FONT_EX);
            li++;
            continue;
        }

        BASS_MIDI_FONT font;
        font.font = synth_HSOUNDFONT.at(drumSfIdx);
        font.preset = -1;
        font.bank = 0;

        BASS_MIDI_StreamSetFonts(handles[type], &font, 1);

        li++;
    }

    // the preset decides whether program 24 is the accordion or the khaen: re-route the channels now
    for (int ch = 0; ch < 16; ch++) {
        if (ch == 9)
            continue;
        const InstrumentType nt = melodicTypeFor(ch, chProgram[ch]);
        if (nt == chInstType[ch])
            continue;
        const InstrumentType old = chInstType[ch];
        chInstType[ch] = nt;
        if (HSTREAM oh = handles.value(old, 0))                // the old strip must not keep ringing
            BASS_MIDI_StreamEvent(oh, ch, MIDI_EVENT_NOTESOFF, 0);
    }

    // The channels keep the preset they looked up before the soundfont list changed, so a new
    // bank / drum kit would only be heard after the next program change.  Send each channel's
    // current bank + program again so the change is immediate (also while a song is playing).
    for (int i = 0; i < HANDLE_STREAM_COUNT; i++) {
        if (!isMidiStrip(i))
            continue;
        HSTREAM h = handles[static_cast<InstrumentType>(i)];
        if (!h)
            continue;
        for (int ch = 0; ch < 16; ch++) {
            const DWORD bank = BASS_MIDI_StreamGetEvent(h, ch, MIDI_EVENT_BANK);
            const DWORD lsb  = BASS_MIDI_StreamGetEvent(h, ch, MIDI_EVENT_BANK_LSB);
            const DWORD prog = BASS_MIDI_StreamGetEvent(h, ch, MIDI_EVENT_PROGRAM);
            BASS_MIDI_StreamEvent(h, ch, MIDI_EVENT_BANK, bank);
            BASS_MIDI_StreamEvent(h, ch, MIDI_EVENT_BANK_LSB, lsb);
            BASS_MIDI_StreamEvent(h, ch, MIDI_EVENT_PROGRAM, prog);
        }
    }
}

void MidiSynthesizer::sendNoteOff(int ch, int note, int velocity)
{
    if (note < 0 || note > 127)
        return;

    if (ch == 9)
        note = remapDrumNote(note);

    if (MidiOut *xo = extPort(typeOf(ch, note))) {
        xo->sendNoteOff(ch, note, velocity);
        return;
    }

    if (ch == 9)
    {
        int vstiIndex = instMap[MidiHelper::getInstrumentDrumType(note)].vsti;
        if (vstiIndex == -1)
            BASS_MIDI_StreamEvent(getDrumHandleFromNote(note), 9, MIDI_EVENT_NOTE, MAKEWORD(note, 0));
        else
        {
            #ifndef __linux__
            InstrumentType t = static_cast<InstrumentType>(vstiIndex + HANDLE_VSTI_START);
            bv::ProcessEvent(handles[t], 9, MIDI_EVENT_NOTE, MAKEWORD(note, 0));
            #endif
        }
    }
    else
    {
        int vstiIndex = instMap[chInstType[ch]].vsti;
        if (vstiIndex == -1)
            BASS_MIDI_StreamEvent(handles[chInstType[ch]], ch, MIDI_EVENT_NOTE, MAKEWORD(note, 0));
        else
        {
            #ifndef __linux__
            InstrumentType t = static_cast<InstrumentType>(vstiIndex + HANDLE_VSTI_START);
            bv::ProcessEvent(handles[t], ch, MIDI_EVENT_NOTE, MAKEWORD(note, 0));
            #endif
        }
    }
}

void MidiSynthesizer::sendNoteOn(int ch, int note, int velocity)
{
    if (note < 0 || note > 127)
        return;

    if (ch == 9)
        note = remapDrumNote(note);

    if (MidiOut *xo = extPort(typeOf(ch, note))) {
        xo->sendNoteOn(ch, note, velocity);
        return;
    }

    if (ch == 9)
    {
        int vstiIndex = instMap[MidiHelper::getInstrumentDrumType(note)].vsti;
        if (vstiIndex == -1)
        {
            InstrumentType t = MidiHelper::getInstrumentDrumType(note);
            applyDrumTune(note, handles[t]);
            BASS_MIDI_StreamEvent(handles[t], 9, MIDI_EVENT_NOTE, MAKEWORD(note, velocity));
            emit noteOnSended(t, instMap[t].bus, 9, note, velocity);
        }
        else
        {
            #ifndef __linux__
            InstrumentType t = static_cast<InstrumentType>(vstiIndex + HANDLE_VSTI_START);
            bv::ProcessEvent(handles[t], 9, MIDI_EVENT_NOTE, MAKEWORD(note, velocity));
            emit noteOnSended(t, instMap[t].bus, 9, note, velocity);
            #endif
        }
    }
    else
    {
        int vstiIndex = instMap[chInstType[ch]].vsti;
        if (vstiIndex == -1)
        {
            InstrumentType t = chInstType[ch];
            BASS_MIDI_StreamEvent(handles[t], ch, MIDI_EVENT_NOTE, MAKEWORD(note, velocity));
            emit noteOnSended(t, instMap[t].bus, ch, note, velocity);
        }
        else
        {
            #ifndef __linux__
            InstrumentType t = static_cast<InstrumentType>(vstiIndex + HANDLE_VSTI_START);
            bv::ProcessEvent(handles[t], ch, MIDI_EVENT_NOTE, MAKEWORD(note, velocity));
            emit noteOnSended(t, instMap[t].bus, ch, note, velocity);
            #endif
        }
    }
}

// ---------------------------------------------------------------------------
// Drum head tuning and kick/snare key choice
// ---------------------------------------------------------------------------
static int drumGroupNotes(int group, int notes[4])
{
    switch (group) {
    case MidiSynthesizer::TuneKick:    notes[0] = 35; notes[1] = 36; return 2;
    case MidiSynthesizer::TuneSnare:   notes[0] = 38; notes[1] = 40; notes[2] = 31; notes[3] = 34; return 4;
    case MidiSynthesizer::TuneTomHigh: notes[0] = 48; notes[1] = 50; return 2;
    case MidiSynthesizer::TuneTomMid:  notes[0] = 45; notes[1] = 47; return 2;
    case MidiSynthesizer::TuneCowbell: notes[0] = 56; return 1;
    default:                           notes[0] = 41; notes[1] = 43; return 2;   // low tom
    }
}

void MidiSynthesizer::setDrumTune(int group, int semitones)
{
    if (group < 0 || group >= DrumTuneGroupCount)
        return;
    if (semitones < -12) semitones = -12;
    if (semitones > 12)  semitones = 12;
    drumTuneSemis[group] = semitones;

    int notes[4];
    const int noteCount = drumGroupNotes(group, notes);
    for (int i = 0; i < noteCount; i++) {
        noteTune[notes[i]] = semitones;
        noteTuneDirty[notes[i]] = true;     // send it again on the next hit (also when back to 0)
    }
}

int MidiSynthesizer::drumTune(int group) const
{
    return (group >= 0 && group < DrumTuneGroupCount) ? drumTuneSemis[group] : 0;
}

void MidiSynthesizer::setDrumNoteChoice(int kick, int snare)
{
    kickNote  = (kick == 35 || kick == 36) ? kick : 0;
    snareNote = (snare == 38 || snare == 40) ? snare : 0;
}

void MidiSynthesizer::setLockedDrumKit(int program)
{
    if (program == lockedKit)
        return;
    lockedKit = program;
    if (openned)
        setSoundfontPresets(sfPreset);
}

int MidiSynthesizer::remapDrumNote(int note) const
{
    if (kickNote && (note == 35 || note == 36))
        return kickNote;
    if (snareNote && (note == 38 || note == 40))
        return snareNote;
    return note;
}

void MidiSynthesizer::applyDrumTune(int note, HSTREAM stream)
{
    if (note < 0 || note > 127 || !stream)
        return;
    if (noteTune[note] == 0 && !noteTuneDirty[note])
        return;

    noteTuneDirty[note] = false;
    const BYTE value = (BYTE)(64 + noteTune[note]);     // 64 = original pitch, 1 step = 1 semitone
    BYTE data[15] = {
        0xB9, 99, 24,          // NRPN MSB 24: drum instrument pitch (coarse)
        0xB9, 98, (BYTE)note,  // NRPN LSB: which drum key
        0xB9, 6,  value,       // data entry
        0xB9, 101, 127,        // RPN null, so later data-entry messages are not misread
        0xB9, 100, 127
    };
    BASS_MIDI_StreamEvents(stream, BASS_MIDI_EVENTS_RAW, data, 15);
}

void MidiSynthesizer::sendNoteAftertouch(int ch, int note, int value)
{
    if (note < 0 || note > 127)
        return;

    if (ch == 9)
        note = remapDrumNote(note);

    if (MidiOut *xo = extPort(typeOf(ch, note))) {
        xo->sendNoteAftertouch(ch, note, value);
        return;
    }

    if (ch == 9)
    {
        int vstiIndex = instMap[MidiHelper::getInstrumentDrumType(note)].vsti;
        if (vstiIndex == -1)
            BASS_MIDI_StreamEvent(getDrumHandleFromNote(note), 9, MIDI_EVENT_KEYPRES, MAKEWORD(note, value));
        else
        {
            #ifndef __linux__
            InstrumentType t = static_cast<InstrumentType>(vstiIndex + HANDLE_VSTI_START);
            bv::ProcessEvent(handles[t], 9, MIDI_EVENT_KEYPRES, MAKEWORD(note, value));
            #endif
        }
    }
    else
    {
        int vstiIndex = instMap[chInstType[ch]].vsti;
        if (vstiIndex == -1)
            BASS_MIDI_StreamEvent(handles[chInstType[ch]], ch, MIDI_EVENT_KEYPRES, MAKEWORD(note, value));
        else
        {
            #ifndef __linux__
            InstrumentType t = static_cast<InstrumentType>(vstiIndex + HANDLE_VSTI_START);
            bv::ProcessEvent(handles[t], ch, MIDI_EVENT_KEYPRES, MAKEWORD(note, value));
            #endif
        }
    }
}

void MidiSynthesizer::sendController(int ch, int number, int value)
{
    if (!extOutName.isEmpty() && ch >= 0 && ch < 16) {
        if (ch == 9)
            extTeeAll(0xB0 | ch, number & 0x7F, value & 0x7F);
        else if (MidiOut *xo = extPort(chInstType[ch]))
            xo->sendController(ch, number, value);
    }

    if (ignoreDrumPitch && ch == 9)
    {
        switch (number) {
        case 99:  dpMode = 1; dpMsb = value; if (value == 24) return; break;   // NRPN MSB 24 = drum pitch
        case 98:  dpMode = 1; dpLsb = value; if (dpMsb == 24) return; break;
        case 101: dpMode = 2; dpMsb = value; break;
        case 100: dpMode = 2; dpLsb = value; break;
        case 121: dpMode = -1; break;
        case 6:
        case 38:
            if (dpMode == 1 && dpMsb == 24)
                return;
            if (dpMode == 2 && dpMsb == 0 && (dpLsb == 1 || dpLsb == 2))   // fine / coarse tune
                return;
            break;
        default: break;
        }
    }

    DWORD et;

    switch (number) {
    case 0:
        et = MIDI_EVENT_BANK; break;
    case 1:
        et = MIDI_EVENT_MODULATION; break;
    case 5:
        et = MIDI_EVENT_PORTATIME; break;
        /*
    case 6:
    case 38:
        if (RPNType == 0)
            return;
        et = RPNType;
        break;
        */
    case 7:
        et = MIDI_EVENT_VOLUME; break;
    case 10:
        et = MIDI_EVENT_PAN; break;
    case 11:
        et = MIDI_EVENT_EXPRESSION; break;
    case 32:
        et = MIDI_EVENT_BANK_LSB; break;
    case 64:
        et = MIDI_EVENT_SUSTAIN; break;
    case 65:
        et = MIDI_EVENT_PORTAMENTO; break;
    case 66:
        et = MIDI_EVENT_SOSTENUTO; break;
    case 67:
        et = MIDI_EVENT_SOFT; break;
    case 71:
        et = MIDI_EVENT_RESONANCE; break;
    case 72:
        et = MIDI_EVENT_RELEASE; break;
    case 73:
        et = MIDI_EVENT_ATTACK; break;
    case 74:
        et = MIDI_EVENT_CUTOFF; break;
    case 75:
        et = MIDI_EVENT_DECAY; break;
    case 84:
        et = MIDI_EVENT_PORTANOTE; break;
    case 91:
        et = MIDI_EVENT_REVERB; break;
    case 93:
        et = MIDI_EVENT_CHORUS; break;
    case 94:
        et = MIDI_EVENT_USERFX; break;
        /*
    case 100:
    case 101:
        if (value == 127) {
            et = MIDI_EVENT_PITCHRANGE;
            break;
        }
        else {
            switch (value) {
            case 0: RPNType = MIDI_EVENT_PITCHRANGE; break;
            case 1: RPNType = MIDI_EVENT_FINETUNE; break;
            case 2: RPNType = MIDI_EVENT_COARSETUNE; break;
            default: RPNType = 0;  qDebug() << " Value " << value; break;
            }
            return;
        }
        */
    case 120:
        et = MIDI_EVENT_SOUNDOFF; break;
    case 121:
        et = MIDI_EVENT_RESET; break;
    case 123:
        et = MIDI_EVENT_NOTESOFF; break;
    case 126:
    case 127:
        et = MIDI_EVENT_MODE; break;
    default:
        if (ch < 0 || ch > 15)
            return;
        BYTE data[3] = { (0xB0 | ch), (number & 0x7F), (value & 0x7F) };
        //qDebug() << (data[0] & 0xF0) << "  " << (data[0] & 0x0F) << "  " << data[1] << "  " << data[2];
        for (int i=0; i<HANDLE_STREAM_COUNT; i++) {
            if (i >= HANDLE_MIDI_COUNT && i != HANDLE_CANTHAI)
                continue;
            HSTREAM h = handles[static_cast<InstrumentType>(i)];
            if (i < HANDLE_VSTI_START || i == HANDLE_CANTHAI)
                BASS_MIDI_StreamEvents(h, BASS_MIDI_EVENTS_RAW, (void*)data, 3);
            #ifndef __linux__
            else
                bv::ProcessEventRaw(h, (void*)data, 3);
            #endif
        }
        return;
    }

    sendToAllMidiStream(ch, et, value);
}

void MidiSynthesizer::sendProgramChange(int ch, int number)
{
    if (!extOutName.isEmpty() && ch >= 0 && ch < 16 && ch != 9) {
        if (MidiOut *xo = extPort(MidiHelper::getInstrumentType(number)))
            xo->sendProgramChange(ch, number);
    }

    sendToAllMidiStream(ch, MIDI_EVENT_PROGRAM, number);

    if (ch != 9) {
        chProgram[ch & 15] = number;
        chInstType[ch] = melodicTypeFor(ch, number);
    }
}

// Program 24 (index 23, accordion family) is the Thai khaen when the SOUNDFONT preset is 101 / 108 / 110 - it then plays
// on the Canthai strip; every other preset (and every other program) keeps the normal family strip
InstrumentType MidiSynthesizer::melodicTypeFor(int ch, int program) const
{
    if (program == 23 && ch >= 0 && ch < 16 && isCanthaiBank(sfPresetBankNumber(sfPreset)))
        return InstrumentType::Canthai;
    return MidiHelper::getInstrumentType(program);
}

void MidiSynthesizer::sendChannelAftertouch(int ch, int value)
{
    if (!extOutName.isEmpty() && ch >= 0 && ch < 16) {
        if (ch == 9)
            extTeeAll(0xD0 | ch, value & 0x7F, 0, true);
        else if (MidiOut *xo = extPort(chInstType[ch]))
            xo->sendChannelAftertouch(ch, value);
    }

    sendToAllMidiStream(ch, MIDI_EVENT_CHANPRES, value);
}

void MidiSynthesizer::sendPitchBend(int ch, int value)
{
    if (!extOutName.isEmpty() && ch >= 0 && ch < 16 && ch != 9) {
        if (MidiOut *xo = extPort(chInstType[ch])) {
            xo->sendPitchBend(ch, value);
            return;
        }
    }

    if (ch == 9)
    {
        if (ignoreDrumPitch)
            return;
        sendToAllMidiStream(ch, MIDI_EVENT_PITCH, value);
    }
    else
    {
        int vstiIndex = instMap[chInstType[ch]].vsti;
        if (vstiIndex == -1)
            BASS_MIDI_StreamEvent(handles[chInstType[ch]], ch, MIDI_EVENT_PITCH, value);
        else
        {
            #ifndef __linux__
            InstrumentType t = static_cast<InstrumentType>(vstiIndex + HANDLE_VSTI_START);
            bv::ProcessEvent(handles[t], ch, MIDI_EVENT_PITCH, value);
            #endif
        }
    }
}

void MidiSynthesizer::sendAllNotesOff(int ch)
{
    if (!extOutName.isEmpty() && ch >= 0 && ch < 16)
        extTeeAll(0xB0 | ch, 123, 0);
    sendToAllMidiStream(ch, MIDI_EVENT_NOTESOFF, 0);
}

void MidiSynthesizer::sendAllNotesOff()
{
    for (int i=0; i<16; i++) {
        sendAllNotesOff(i);
    }
}

void MidiSynthesizer::sendResetAllControllers(int ch)
{
    if (!extOutName.isEmpty() && ch >= 0 && ch < 16)
        extTeeAll(0xB0 | ch, 121, 0);
    sendToAllMidiStream(ch, MIDI_EVENT_RESET, 0);
    sendToAllMidiStream(ch, MIDI_EVENT_PITCHRANGE, 2);
}

void MidiSynthesizer::sendResetAllControllers()
{
    for (int i=0; i<16; i++) {
        sendResetAllControllers(i);
    }
}

int MidiSynthesizer::device(InstrumentType t)
{
    return instMap[t].device;
}

int MidiSynthesizer::busGroup(InstrumentType t)
{
    return instMap[t].bus;
}

int MidiSynthesizer::volume(InstrumentType t)
{
    return instMap[t].volume;
}

bool MidiSynthesizer::isMute(InstrumentType t)
{
    return instMap[t].mute;
}

bool MidiSynthesizer::isSolo(InstrumentType t)
{
    return instMap[t].solo;
}

int MidiSynthesizer::useVSTi(InstrumentType t)
{
    return instMap[t].vsti;
}

SpeakerType MidiSynthesizer::speaker(InstrumentType t)
{
    return instMap[t].speaker;
}

void MidiSynthesizer::setDevice(InstrumentType t, int device)
{
    if (device < 0 || device >= audioDevices().count())
        return;

    if (!isBusType(t) && instMap[t].bus != -1)
        return;

    instMap[t].device = device;

    if (!openned)
        return;

    if (!isBusType(t) && instMap[t].bus != -1)
        return;

    DWORD flag = MidiHelper::getSpeakerFlag(instMap[t].speaker);
    BASS_Mixer_ChannelRemove(handles[t]);
    BASS_Mixer_StreamAddChannel(mixers[asioDev >= 0 ? 0 : device].handle, handles[t], flag);   // ASIO: everything goes to the one main mixer
}

void MidiSynthesizer::setBusGroup(InstrumentType t, int group)
{
    if (isBusType(t))
        return;

    instMap[t].bus = group;

    if (!openned)
        return;

    DWORD flag = MidiHelper::getSpeakerFlag(instMap[t].speaker);

    BASS_Mixer_ChannelRemove(handles[t]);

    if (group == -1)
    {
        DWORD mix = mixers[asioDev >= 0 ? 0 : instMap[t].device].handle;   // ASIO: only the main mixer runs
        BASS_Mixer_StreamAddChannel(mix, handles[t], flag);
    }
    else
    {
        InstrumentType busType = static_cast<InstrumentType>(group + HANDLE_BUS_START);
        BASS_Mixer_StreamAddChannel(handles[busType], handles[t], flag);
    }
}

void MidiSynthesizer::setVolume(InstrumentType t, int volume)
{
    if (volume > 100)
        instMap[t].volume = 100;
    else if (volume < 0)
        instMap[t].volume = 0;
    else
        instMap[t].volume = volume;

    if (!openned)
        return;

    if (!instMap[t].enable)
        return;

    setVolSmooth(handles[t], stripGain(instMap[t]));
    BASS_ChannelSetAttribute(handles[t], BASS_ATTRIB_PAN, instMap[t].pan / 100.0f);   // re-applied whenever the channel is re-attached
}

void MidiSynthesizer::setTrim(InstrumentType t, double db)
{
    instMap[t].trimDb = qBound(-12.0, db, 12.0);
    const int i = static_cast<int>(t);
    if (i >= 0 && i < 64)
        g_trimDsp[i].target.store(float(std::pow(10.0, instMap[t].trimDb / 20.0)));   // the strip's DSP glides to it
}

void MidiSynthesizer::setPan(InstrumentType t, int pan)
{
    instMap[t].pan = qBound(-100, pan, 100);

    if (!openned)
        return;

    BASS_ChannelSetAttribute(handles[t], BASS_ATTRIB_PAN, instMap[t].pan / 100.0f);
}

void MidiSynthesizer::attachSendReverb(InstrumentType t, DWORD stream)
{
    Reverb2FX *rv = sendRv.value(t, nullptr);
    if (rv)
        rv->setStreamHandle(stream);
}

void MidiSynthesizer::setReverb(InstrumentType t, int level)
{
    level = qBound(0, level, 100);
    instMap[t].reverb = level;

    Reverb2FX *rv = sendRv.value(t, nullptr);

    if (level == 0)
    {
        if (rv)
            rv->off();          // fully dry: no processing at all
        return;
    }

    if (!rv)
    {
        rv = new Reverb2FX(0, -100);   // lowest priority: after the channel's own FX
        rv->setDryMix(1.0f);
        rv->setRoomSize(reverbStrong ? 0.8f : 0.75f);
        rv->setDamp(reverbStrong ? 0.35f : 0.45f);
        rv->setWidth(1.0f);
        sendRv[t] = rv;
        if (openned)
            rv->setStreamHandle(handles.value(t, 0));
    }

    // The knob has to be clearly audible over its whole travel: the free-verb wet level goes up to 3.0
    // (it was capped at 1.6, so even a full knob was faint), and the room grows with the knob.
    const float k = level / 100.0f;
    if (reverbStrong) {
        rv->setWetMix(3.0f * std::pow(k, 0.85f));
        rv->setRoomSize(0.7f + 0.25f * k);
        rv->setDamp(0.35f);
    } else {                    // tight and short
        rv->setWetMix(1.6f * k);
        rv->setRoomSize(0.75f);
        rv->setDamp(0.45f);
    }
    rv->setDryMix(1.0f);
    rv->on();
}

void MidiSynthesizer::setReverbStrong(bool strong)
{
    if (strong == reverbStrong)
        return;
    reverbStrong = strong;
    const QList<InstrumentType> keys = sendRv.keys();
    for (InstrumentType t : keys)
        setReverb(t, instMap[t].reverb);        // re-apply every strip with the new strength
}

int MidiSynthesizer::reverb(InstrumentType t)
{
    return instMap[t].reverb;
}

int MidiSynthesizer::pan(InstrumentType t)
{
    return instMap[t].pan;
}

void MidiSynthesizer::setMute(InstrumentType t, bool m)
{
    instMap[t].mute = m;
    calculateEnable();

    if (!openned)
        return;

    if (m)
        setVolSmooth(handles[t], 0.0f);
    else
        setVolSmooth(handles[t], stripGain(instMap[t]));
}

void MidiSynthesizer::setSolo(InstrumentType t, bool s)
{
    instMap[t].solo = s;

    bool us = false;

    for (const Instrument &im : instMap.values()) {
        if (im.solo) {
            us = true;
            break;
        }
    }
    useSolo = us;

    calculateEnable();

    if (!openned)
        return;

    for (const Instrument itr : instMap.values()) {

        if (itr.enable)
            setVolSmooth(handles[itr.type], stripGain(itr));
        else
            setVolSmooth(handles[itr.type], 0.0f);
    }
}

void MidiSynthesizer::setUseVSTi(InstrumentType t, int vstiIndex)
{
    if (isBusType(t))
        return;

    if (vstiIndex < -1 || vstiIndex > 4)
        return;

    #ifndef __linux__
    int oldVstiIndex = instMap[t].vsti;
    instMap[t].vsti = vstiIndex;

    // release the notes still held in the VSTi this strip USED to play (not the new one), otherwise
    // they hang and ring on after the switch
    if (oldVstiIndex != -1 && oldVstiIndex != vstiIndex && vstiHandle(oldVstiIndex) != 0)
    {
        InstrumentType type = static_cast<InstrumentType>(oldVstiIndex + HANDLE_VSTI_START);
        for (int ch=0; ch<16; ch++)
            bv::ProcessEvent(handles[type], ch, MIDI_EVENT_NOTESOFF, 0);
    }
    #endif
}

namespace {
const DWORD kRestoreEvents[] = {
    MIDI_EVENT_BANK, MIDI_EVENT_BANK_LSB, MIDI_EVENT_DRUMS, MIDI_EVENT_PROGRAM,
    MIDI_EVENT_VOLUME, MIDI_EVENT_PAN, MIDI_EVENT_EXPRESSION, MIDI_EVENT_MODULATION, MIDI_EVENT_SUSTAIN,
    MIDI_EVENT_REVERB, MIDI_EVENT_CHORUS, MIDI_EVENT_PITCHRANGE, MIDI_EVENT_PITCH,
    MIDI_EVENT_CUTOFF, MIDI_EVENT_RESONANCE, MIDI_EVENT_ATTACK, MIDI_EVENT_RELEASE, MIDI_EVENT_DECAY,
    MIDI_EVENT_FINETUNE, MIDI_EVENT_COARSETUNE
};
const int kRestoreCount = int(sizeof(kRestoreEvents) / sizeof(kRestoreEvents[0]));
}

// state of one MIDI stream's 16 channels (program, volume, pan, reverb ...), read from / written back to the stream
static QVector<DWORD> snapshotStream(HSTREAM h)
{
    QVector<DWORD> v(16 * kRestoreCount, DWORD(-1));
    if (!h)
        return v;
    for (int ch = 0; ch < 16; ch++)
        for (int k = 0; k < kRestoreCount; k++)
            v[ch * kRestoreCount + k] = BASS_MIDI_StreamGetEvent(h, ch, kRestoreEvents[k]);
    return v;
}

static void restoreStream(HSTREAM h, const QVector<DWORD> &v)
{
    if (!h || v.size() != 16 * kRestoreCount)
        return;
    for (int ch = 0; ch < 16; ch++)
        for (int k = 0; k < kRestoreCount; k++) {
            const DWORD val = v[ch * kRestoreCount + k];
            if (val != DWORD(-1))
                BASS_MIDI_StreamEvent(h, ch, kRestoreEvents[k], val);
        }
}

void MidiSynthesizer::setSpeaker(InstrumentType t, SpeakerType speaker)
{
    if (isBusType(t))
        return;

    instMap[t].speaker = speaker;

    if (!openned)
        return;

    // take the strip's fx and send reverb off the old stream first, so none of them is left running on a
    // stream that is about to be freed
    for (FX *fx : instMap[t].FXs)
        fx->setStreamHandle(0);
    attachSendReverb(t, 0);

    // The new stream starts with every channel at its defaults (piano, default volume, no reverb ...): that is why
    // the sound used to come out quieter / different until the song was restarted. Keep the old stream's channel
    // state and write it back to the new one.
    const bool midiStream = isMidiStrip(static_cast<int>(t));
    const QVector<DWORD> chState = midiStream ? snapshotStream(handles[t]) : QVector<DWORD>();

    // VSTi: the plug-in's current sound (program / parameters / chunk) is read from the running instance before it is
    // freed - the copy kept for the next start-up can be older than what the user has just dialled in
    #ifndef __linux__
    const int sIdx = static_cast<int>(t);
    const bool vstiStream = sIdx >= HANDLE_VSTI_START && sIdx < HANDLE_MIDI_COUNT;
    if (vstiStream && handles[t]) {
        const int vIndex = sIdx - HANDLE_VSTI_START;
        mVstiTempProgram[vIndex] = bv::GetProgram(handles[t]);
        mVstiTempParams[vIndex] = FX::getVSTParams(handles[t]);
        DWORD length = 0;
        char *chunk = bv::GetChunk(handles[t], false, &length);
        if (chunk && length > 0)
            mVstiChunk[vIndex] = QByteArray(chunk, int(length));
    }
    #endif

    BASS_Mixer_ChannelRemove(handles[t]);
    #ifndef __linux__
    bv::ChannelFree(handles[t]);
    #endif
    BASS_StreamFree(handles[t]);

    handles[t] = createStream(t);
    attachTrimDsp(t, handles[t]);

    // Check device.. volume .. mute.. solo.. bus.. and VST
    setDevice(t, instMap[t].device);
    setVolume(t, instMap[t].volume);
    setMute(t, instMap[t].mute);
    setSolo(t, instMap[t].solo);
    setBusGroup(t, instMap[t].bus); // speaker will set in here

    // Set fx to stream handle
    for (FX *fx : instMap[t].FXs)
        fx->setStreamHandle(handles[t]);
    attachSendReverb(t, handles[t]);

    setSoundfontPresets(sfPreset);          // soundfonts / presets onto the new stream first ...
    if (midiStream)
        restoreStream(handles[t], chState);  // ... then the channels' program, volume, pan, reverb ...
    #ifndef __linux__
    if (vstiStream)
        replayVstiEvents(handles[t]);        // VSTi: the last controllers / programs sent to it
    #endif
}

QStringList MidiSynthesizer::audioDevices()
{
    return QStringList(outDevices.values());
}

void MidiSynthesizer::audioDevices(const QMap<int, QString> &devices)
{
    outDevices = devices;
}

bool MidiSynthesizer::isSoundFontFile(const QString &sfile)
{
    bool result = true;

    HSOUNDFONT f = 0;
    if (PTHKCore::isProtectedSoundfont(sfile)) {
        f = PTHKCore::openProtectedSoundfont(sfile, 0);
    } else {
    #ifdef _WIN32
        f = BASS_MIDI_FontInit(sfile.toStdWString().c_str(), 0);
    #else
        f = BASS_MIDI_FontInit(sfile.toStdString().c_str(), 0);
    #endif
    }

    if (!f)
        result = false;

    BASS_MIDI_FontFree(f);

    return result;
}

QList<Equalizer31BandFX *> MidiSynthesizer::equalizer31BandFXs()
{
    QList<Equalizer31BandFX *> eqs;

    for (MixerHandle mix : mixers)
        eqs.append(mix.eq);

    return eqs;
}

QList<Reverb2FX *> MidiSynthesizer::reverbFXs()
{
    QList<Reverb2FX *> rvs;

    for (MixerHandle mix : mixers)
        rvs.append(mix.reverb);

    return rvs;
}

QList<Chorus2FX *> MidiSynthesizer::chorusFXs()
{
    QList<Chorus2FX *> chs;

    for (MixerHandle mix : mixers)
        chs.append(mix.chorus);

    return chs;
}

// ---- keep the instruments when the audio chain is rebuilt (sample rate / 32-bit float change) ----
// The MIDI streams are new after close()/open(), so every channel would fall back to piano + the default drum kit
// until the song (or the karaoke program feeding the MIDI input) sends its program changes again.  The state of
// each channel is read before the rebuild and sent back afterwards, so the music simply carries on.

QVector<DWORD> MidiSynthesizer::snapshotChannels()
{
    const int streams = HANDLE_STREAM_COUNT;    // indexed by strip number; the VSTi / bus streams stay empty
    QVector<DWORD> v(streams * 16 * kRestoreCount, DWORD(-1));
    for (int i = 0; i < streams; i++) {
        if (!isMidiStrip(i))
            continue;
        HSTREAM h = handles[static_cast<InstrumentType>(i)];
        if (!h)
            continue;
        for (int ch = 0; ch < 16; ch++)
            for (int k = 0; k < kRestoreCount; k++)
                v[(i * 16 + ch) * kRestoreCount + k] = BASS_MIDI_StreamGetEvent(h, ch, kRestoreEvents[k]);
    }
    return v;
}

void MidiSynthesizer::restoreChannels(const QVector<DWORD> &v)
{
    const int streams = HANDLE_STREAM_COUNT;
    if (v.size() != streams * 16 * kRestoreCount)
        return;
    for (int i = 0; i < streams; i++) {
        if (!isMidiStrip(i))
            continue;
        HSTREAM h = handles[static_cast<InstrumentType>(i)];
        if (!h)
            continue;
        for (int ch = 0; ch < 16; ch++)
            for (int k = 0; k < kRestoreCount; k++) {
                const DWORD val = v[(i * 16 + ch) * kRestoreCount + k];
                if (val != DWORD(-1))
                    BASS_MIDI_StreamEvent(h, ch, kRestoreEvents[k], val);
            }
    }
}

void MidiSynthesizer::setUsetFloattingPoint(bool use)
{
    if (use == useFloat)
        return;

    useFloat = use;

    const QVector<DWORD> state = openned ? snapshotChannels() : QVector<DWORD>();
    close();
    open();

    setSoundfontPresets(sfPreset);
    restoreChannels(state);
}

void MidiSynthesizer::setSampleRate(int hz)
{
    if (hz < 8000 || hz > 192000 || hz == sampleRate)
        return;

    sampleRate = hz;
    if (!openned)
        return;

    const QVector<DWORD> state = snapshotChannels();
    close();
    open();

    setSoundfontPresets(sfPreset);
    restoreChannels(state);
}

void MidiSynthesizer::setSincInterpolation(bool on)
{
    if (on == sincInter)
        return;

    sincInter = on;
    if (!openned)
        return;

    for (int i=0; i<HANDLE_STREAM_COUNT; i++) {   // MIDI streams only (not VSTi / bus)
        if (!isMidiStrip(i))
            continue;
        HSTREAM h = handles[static_cast<InstrumentType>(i)];
        if (h)
            BASS_ChannelFlags(h, on ? BASS_MIDI_SINCINTER : 0, BASS_MIDI_SINCINTER);
    }
}

void MidiSynthesizer::setVoices(int n)
{
    n = qBound(64, n, 2000);
    maxVoices = n;
    BASS_SetConfig(BASS_CONFIG_MIDI_VOICES, n);   // streams created from now on
    if (!openned)
        return;

    for (int i=0; i<HANDLE_STREAM_COUNT; i++) {
        if (!isMidiStrip(i))
            continue;
        HSTREAM h = handles[static_cast<InstrumentType>(i)];
        if (h)
            BASS_ChannelSetAttribute(h, BASS_ATTRIB_MIDI_VOICES, float(n));
    }
}

void MidiSynthesizer::setUseFXRC(bool use)
{
    if (use == useFX)
        return;

    useFX = use;

    if (useFX) {
        for (int i=0; i<HANDLE_STREAM_COUNT; i++) {
            if (i >= HANDLE_MIDI_COUNT && i != HANDLE_CANTHAI)
                continue;
            HSTREAM h = handles[static_cast<InstrumentType>(i)];
            BASS_ChannelFlags(h, 0, BASS_MIDI_NOFX); // remove the flag
        }
    } else {
        for (int i=0; i<HANDLE_STREAM_COUNT; i++) {
            if (i >= HANDLE_MIDI_COUNT && i != HANDLE_CANTHAI)
                continue;
            HSTREAM h = handles[static_cast<InstrumentType>(i)];
            BASS_ChannelFlags(h, BASS_MIDI_NOFX, BASS_MIDI_NOFX); // set flag
        }
    }
}

HSTREAM MidiSynthesizer::getChannelHandle(InstrumentType type)
{
    return handles[type];
}

FX *MidiSynthesizer::addFX(InstrumentType type, DWORD uid)
{
    FX *fx = nullptr;

    if (uid < BUILTIN_FX_COUNT)
    {
        FXType fxType = static_cast<FXType>(uid);
        switch (fxType) {
        case FXType::AutoWah:
            fx = new AutoWahFX(handles[type], instMap[type].FXs.count());
            break;
        case FXType::Chorus:
            fx = new ChorusFX(handles[type], instMap[type].FXs.count());
            break;
        case FXType::Compressor:
            fx = new CompressorFX(handles[type], instMap[type].FXs.count());
            break;
        case FXType::Distortion:
            fx = new DistortionFX(handles[type], instMap[type].FXs.count());
            break;
        case FXType::Echo:
            fx = new EchoFX(handles[type], instMap[type].FXs.count());
            break;
        case FXType::EQ15Band:
            fx = new Equalizer15BandFX(handles[type], instMap[type].FXs.count());
            break;
        case FXType::EQ31Band:
            fx = new Equalizer31BandFX(handles[type], instMap[type].FXs.count());
            break;
        case FXType::Reverb:
            fx = new ReverbFX(handles[type], instMap[type].FXs.count());
            break;
        }
    }
    else
    {
        #ifndef __linux__
        if (_vstList.contains(uid))
            fx = new VSTFX(_vstList[uid].vstPath, handles[type], instMap[type].FXs.count());
        else
            fx = nullptr;
        #endif
    }

    if (fx != nullptr)
    {
        fx->setBypass(false);
        instMap[type].FXs.append(fx);
    }

    return fx;
}

bool MidiSynthesizer::removeFX(InstrumentType type, int fxIndex)
{
    if (fxIndex >= instMap[type].FXs.count())
        return false;

    delete instMap[type].FXs.takeAt(fxIndex);

    return true;
}

void MidiSynthesizer::setFXBypass(InstrumentType type, int fxIndex, bool state)
{

    if (fxIndex >= instMap[type].FXs.count())
        return;

    instMap[type].FXs[fxIndex]->setBypass(state);
}

QList<uint> MidiSynthesizer::fxUids(InstrumentType type)
{
    QList<uint> uids;

    for (FX *fx : instMap[type].FXs)
        uids.append(fx->uids());

    return uids;
}

QList<bool> MidiSynthesizer::fxBypass(InstrumentType type)
{
    QList<bool> bypass;

    for (FX *fx : instMap[type].FXs)
        bypass.append(fx->isBypass());

    return bypass;
}

QList<int> MidiSynthesizer::fxProgram(InstrumentType type)
{
    QList<int> programs;

    for (FX *fx : instMap[type].FXs)
        programs.append(fx->program());

    return programs;
}

QList<QByteArray> MidiSynthesizer::fxChunks(InstrumentType type)
{
    QList<QByteArray> chunks;

    for (FX *fx : instMap[type].FXs)
        chunks.append(fx->chunk());

    return chunks;
}

QList<QList<float> > MidiSynthesizer::fxParams(InstrumentType type)
{
    QList<QList<float>> params;

    for (FX *fx : instMap[type].FXs)
        params.append(fx->params());

    return params;
}

// ---------------------------------------------------------------------------
// Master effects (on the main output mixer)
// ---------------------------------------------------------------------------
FX *MidiSynthesizer::addMasterFX(DWORD uid)
{
    const DWORD h = mixers.isEmpty() ? 0 : mixers[0].handle;
    const int idx = masterFXs.count();
    FX *fx = nullptr;

    if (uid < BUILTIN_FX_COUNT)
    {
        switch (static_cast<FXType>(uid)) {
        case FXType::AutoWah:    fx = new AutoWahFX(h, idx); break;
        case FXType::Chorus:     fx = new ChorusFX(h, idx); break;
        case FXType::Compressor: fx = new CompressorFX(h, idx); break;
        case FXType::Distortion: fx = new DistortionFX(h, idx); break;
        case FXType::Echo:       fx = new EchoFX(h, idx); break;
        case FXType::EQ15Band:   fx = new Equalizer15BandFX(h, idx); break;
        case FXType::EQ31Band:   fx = new Equalizer31BandFX(h, idx); break;
        case FXType::Reverb:     fx = new ReverbFX(h, idx); break;
        default: break;
        }
    }
    else
    {
        #ifndef __linux__
        if (_vstList.contains(uid))
            fx = new VSTFX(_vstList[uid].vstPath, h, idx);
        #endif
    }

    if (fx != nullptr)
    {
        fx->setBypass(false);
        masterFXs.append(fx);
    }
    return fx;
}

bool MidiSynthesizer::removeMasterFX(int fxIndex)
{
    if (fxIndex < 0 || fxIndex >= masterFXs.count())
        return false;
    delete masterFXs.takeAt(fxIndex);
    return true;
}

void MidiSynthesizer::setMasterFXBypass(int fxIndex, bool state)
{
    if (fxIndex < 0 || fxIndex >= masterFXs.count())
        return;
    masterFXs[fxIndex]->setBypass(state);
}

QList<uint> MidiSynthesizer::masterFxUids()
{
    QList<uint> v;
    for (FX *fx : masterFXs) v.append(fx->uids());
    return v;
}

QList<bool> MidiSynthesizer::masterFxBypass()
{
    QList<bool> v;
    for (FX *fx : masterFXs) v.append(fx->isBypass());
    return v;
}

QList<int> MidiSynthesizer::masterFxProgram()
{
    QList<int> v;
    for (FX *fx : masterFXs) v.append(fx->program());
    return v;
}

QList<QByteArray> MidiSynthesizer::masterFxChunks()
{
    QList<QByteArray> v;
    for (FX *fx : masterFXs) v.append(fx->chunk());
    return v;
}

QList<QList<float> > MidiSynthesizer::masterFxParams()
{
    QList<QList<float>> v;
    for (FX *fx : masterFXs) v.append(fx->params());
    return v;
}

#ifndef __linux__

BASS_VST_INFO MidiSynthesizer::vstiInfo(int vstiIndex)
{
    if (vstiIndex < 0 || vstiIndex > 3)
        return BASS_VST_INFO();
    else
        return mVstiInfos[vstiIndex];
}

QStringList MidiSynthesizer::vstiFiles()
{
    return QStringList() << mVstiFiles[0] << mVstiFiles[1]
            << mVstiFiles[2] << mVstiFiles[3];
}

QString MidiSynthesizer::vstiFile(int vstiIndex)
{
    if (vstiIndex < 0 || vstiIndex > 3)
        return "";
    else
        return mVstiFiles[vstiIndex];
}

DWORD MidiSynthesizer::vstiHandle(int vstiIndex)
{
    InstrumentType t = static_cast<InstrumentType>(HANDLE_VSTI_START+vstiIndex);
    return handles[t];
}

int MidiSynthesizer::vstiProgram(int vstiIndex)
{
    DWORD h = vstiHandle(vstiIndex);
    if (isOpened() && h != 0)
        return bv::GetProgram(h);
    else
        return mVstiTempProgram[vstiIndex];
}

QList<float> MidiSynthesizer::vstiParams(int vstiIndex)
{
    DWORD h = vstiHandle(vstiIndex);
    if (isOpened() && h != 0)
        return FX::getVSTParams(h);
    else
        return mVstiTempParams[vstiIndex];
}

QByteArray MidiSynthesizer::vstiChunk(int vstiIndex)
{
    DWORD h = vstiHandle(vstiIndex);
    if (isOpened() && h != 0)
    {
        DWORD length = 0;
        char *chunk = bv::GetChunk(h, false, &length);
        return QByteArray(chunk, length);
    }
    else
    {
        return mVstiChunk[vstiIndex];
    }
}

DWORD MidiSynthesizer::setVSTiFile(int vstiIndex, const QString &file)
{
    if (vstiIndex < 0 || vstiIndex > 3)
        return 0;

    InstrumentType t = static_cast<InstrumentType>(HANDLE_VSTI_START+vstiIndex);
    DWORD vsti = handles[t];

    if (file == mVstiFiles[vstiIndex])
        return vsti;

#ifdef _WIN32
    if (!file.isEmpty() && !Utils::is64BitDll(file)) {
        mVstiFiles[vstiIndex] = "";
        handles[t] = 0;
        return 0;
    }
#endif

    mVstiFiles[vstiIndex] = file;

    if (!openned)
        return 0;

    // detach the strip's fx / send reverb from the old stream before it is freed
    for (FX *fx : instMap[t].FXs)
        fx->setStreamHandle(0);
    attachSendReverb(t, 0);

    BASS_Mixer_ChannelRemove(vsti);
    bv::ChannelFree(vsti);

    vsti = createStream(t);
    if (vsti)
        attachTrimDsp(t, vsti);

    if (vsti)
    {
        BASS_VST_INFO vstinfo;
        bv::GetInfo(vsti, &vstinfo);
        mVstiInfos[vstiIndex] = vstinfo;
        mVstiTempProgram[vstiIndex] = 0;
        mVstiTempParams[vstiIndex].clear();

        handles[t] = vsti;

        setDevice(t, instMap[t].device);
        setVolume(t, instMap[t].volume);
        setMute(t, instMap[t].mute);
        setSolo(t, instMap[t].solo);
        setBusGroup(t, instMap[t].bus); // speaker will set here

        // Set stream handle to FX
        for (FX *fx : instMap[t].FXs)
            fx->setStreamHandle(vsti);
        attachSendReverb(t, vsti);

        return vsti;
    }
    else
    {
        mVstiFiles[vstiIndex] = "";
        handles[t] = 0;
        return 0;
    }
}

void MidiSynthesizer::removeVSTiFile(int vstiIndex)
{
    if (vstiIndex < 0 || vstiIndex > 3)
        return ;

    InstrumentType t = static_cast<InstrumentType>(HANDLE_VSTI_START+vstiIndex);
    DWORD vsti = handles[t];

    if (openned) {
        for (FX *fx : instMap[t].FXs)
            fx->setStreamHandle(0);
        attachSendReverb(t, 0);
    }

    BASS_Mixer_ChannelRemove(vsti);
    bv::ChannelFree(vsti);

    handles[t] = 0;
    mVstiFiles[vstiIndex] = "";
    mVstiInfos[vstiIndex] = BASS_VST_INFO();
    mVstiTempProgram[vstiIndex] = 0;
    mVstiTempParams[vstiIndex].clear();
}

#endif

void MidiSynthesizer::compactSoundfont()
{
    BASS_MIDI_FontCompact(0);
}

DWORD MidiSynthesizer::createStream(InstrumentType t)
{
    int index = static_cast<int>(t);

    DWORD f = useFloat ? BASS_SAMPLE_FLOAT : 0;

    if (index < HANDLE_MIDI_COUNT || index == HANDLE_CANTHAI)  // Midi & VSTi stream (+ the Canthai MIDI strip)
    {
        if (isMidiStrip(index)) // Midi
        {
             DWORD flags = f|BASS_STREAM_DECODE|BASS_MIDI_NOSYSRESET;
             if (sincInter) flags = flags|BASS_MIDI_SINCINTER;
             if (!useFX) flags = flags|BASS_MIDI_NOFX;
             if (!MidiHelper::isStereoSpeaker(instMap[t].speaker))
                 flags = flags|BASS_SAMPLE_MONO;

            HSTREAM ms = BASS_MIDI_StreamCreate(16, flags, sampleRate);
            if (ms)
                BASS_ChannelSetAttribute(ms, BASS_ATTRIB_MIDI_VOICES, float(maxVoices));
            return ms;
        }
        else // VSTi
        {
            #ifdef __linux__
            return 0;
            #else
            int vIndex = index - (HANDLE_MIDI_COUNT - HANDLE_VSTI_COUNT);
            int chan = MidiHelper::isStereoSpeaker(instMap[t].speaker) ? 2 : 1;
            if (mVstiFiles[vIndex] == "")
                return 0;
            #ifdef _WIN32
            DWORD h = bv::ChannelCreate(sampleRate, chan, mVstiFiles[vIndex].toStdWString().c_str(),
                                       f|BASS_UNICODE|BASS_STREAM_DECODE);
            #else
            DWORD h = bv::ChannelCreate(sampleRate, chan, mVstiFiles[vIndex].toStdString().c_str(),
                                       f|BASS_STREAM_DECODE);
            #endif
            if (h)
            {
                BASS_VST_INFO info;
                bv::GetInfo(h, &info);
                mVstiInfos[vIndex] = info;

                if (mVstiChunk[vIndex].length() > 0) {
                    // chunk = complete state; don't revert it with program/params
                    bv::SetChunk(h, false, mVstiChunk[vIndex].constData(), mVstiChunk[vIndex].length());
                } else {
                    bv::SetProgram(h, mVstiTempProgram[vIndex]);
                    FX::setVSTParams(h, mVstiTempParams[vIndex]);
                }
            }
            return h; // vsti handle
            #endif
        }
    }
    else // bus stream
    {
        int chan = MidiHelper::isStereoSpeaker(instMap[t].speaker) ? 2 : 1;
        // NONSTOP: a bus that is empty for a moment (e.g. a VSTi is loaded into it later) must not end for good
        return BASS_Mixer_StreamCreate(sampleRate, chan, f|BASS_STREAM_DECODE|BASS_MIXER_NONSTOP);
    }
}

void MidiSynthesizer::sendToAllMidiStream(int ch, DWORD eventType, DWORD param)
{
    trackEvent(ch, eventType, param);
    for (int i=0; i<HANDLE_STREAM_COUNT; i++) {
        if (i >= HANDLE_MIDI_COUNT && i != HANDLE_CANTHAI)
            continue;                                   // bus streams take no MIDI
        HSTREAM stream = handles[static_cast<InstrumentType>(i)];
        if (i < HANDLE_VSTI_START || i == HANDLE_CANTHAI)
            BASS_MIDI_StreamEvent(stream, ch, eventType, param);
        #ifndef __linux__
        else
            bv::ProcessEvent(stream, ch, eventType, param);
        #endif
    }
}

void MidiSynthesizer::setSfToStream()
{
    if (synth_HSOUNDFONT.size() > 0)
    {
        BASS_MIDI_FONT font;
        font.font = synth_HSOUNDFONT.at(0);
        font.preset = -1;
        font.bank = 0;

        for (int i=0; i<HANDLE_STREAM_COUNT; i++) {
            if (i >= HANDLE_MIDI_COUNT && i != HANDLE_CANTHAI)
                continue;
            HSTREAM h = handles[static_cast<InstrumentType>(i)];
            BASS_MIDI_StreamSetFonts(h, &font, 1); // set sf stream
        }
    }

    // Reset map intrument sf
    /*
    instmSf.clear();
    drumSf.clear();
    for (int i=0; i<128; i++) {
        instmSf.append(0);
    }
    for (int i=0; i<16; i++) {
        drumSf.append(0);
    }
    */
}

void MidiSynthesizer::calculateEnable()
{
    for (Instrument i : instMap.values()) {
        //Instrument i = im.second;
        if (useSolo) {
            if (i.mute)
                instMap[i.type].enable = false;
            else if (i.solo)
                instMap[i.type].enable = true;
            else
                instMap[i.type].enable = false;
        }
        else {
            if (i.mute)
                instMap[i.type].enable = false;
            else
                instMap[i.type].enable = true;
        }
    }
}

HSTREAM MidiSynthesizer::getDrumHandleFromNote(int drumNote)
{
    switch (drumNote) {

    // Bass drum
    case 35:
    case 36:
        return handles[InstrumentType::BassDrum];

    // snare (31 and 34: the alternative snare keys of the "lock snare" button)
    case 31:
    case 34:
    case 38:
    case 40:
        return handles[InstrumentType::Snare];

    // Side Stick/Rimshot
    case 37:
        return handles[InstrumentType::SideStick];

    // Low Tom
    case 41:
    case 43:
        return handles[InstrumentType::LowTom];

    // Mid Tom
    case 45:
    case 47:
        return handles[InstrumentType::MidTom];

    // High Tom
    case 48:
    case 50:
        return handles[InstrumentType::HighTom];

    // Hi-hat
    case 42:
    case 44:
    case 46:
        return handles[InstrumentType::Hihat];

    // Cowbell
    case 56:
        return handles[InstrumentType::Cowbell];

    // Crash Cymbal ( 55 = Splash Cymbal )
    case 49:
    case 52:
    case 55:
    case 57:
        return handles[InstrumentType::CrashCymbal];

    // Ride Cymbal
    case 51:
    case 59:
        return handles[InstrumentType::RideCymbal];

    // Bongo
    case 60:
    case 61:
        return handles[InstrumentType::Bongo];

    // Conga
    case 62:
    case 63:
    case 64:
        return handles[InstrumentType::Conga];

    // Timbale
    case 65:
    case 66:
        return handles[InstrumentType::Timbale];

    // ฉิ่ง
    case 80:
    case 81:
        return handles[InstrumentType::SmallCupShapedCymbals];

    // ฉาบ
    case 82:
    case 83:
        return handles[InstrumentType::ThaiChap];

    default:
        return handles[InstrumentType::PercussionEtc];
    }
}


void MidiSynthesizer::setExtMidiOut(InstrumentType t, const QString &deviceName)
{
    if (deviceName.isEmpty()) {
        extOutName.remove(t);
        return;
    }

    if (!extPorts.contains(deviceName)) {
        int idx = MidiPlayer::midiDevices().indexOf(deviceName);
        MidiOut *out = nullptr;
        if (idx >= 0) {
            try {
                out = new MidiOut();
                out->openPort(idx);
                if (!out->isPortOpen()) {
                    delete out;
                    out = nullptr;
                }
            } catch (...) {
                delete out;
                out = nullptr;
            }
        }
        if (out == nullptr) {       // device missing or busy -> fall back to internal sound
            extOutName.remove(t);
            return;
        }
        extPorts[deviceName] = out;
    }

    extOutName[t] = deviceName;
}

MidiOut *MidiSynthesizer::extPort(InstrumentType t)
{
    if (extOutName.isEmpty())
        return nullptr;
    auto it = extOutName.constFind(t);
    if (it == extOutName.constEnd() && t == InstrumentType::Canthai)
        it = extOutName.constFind(InstrumentType::Accordion);   // no port of its own: same as the accordion strip
    if (it == extOutName.constEnd())
        return nullptr;
    return extPorts.value(it.value(), nullptr);
}

InstrumentType MidiSynthesizer::typeOf(int ch, int note)
{
    if (ch == 9)
        return MidiHelper::getInstrumentDrumType(note);
    return chInstType[ch];
}

void MidiSynthesizer::extTeeAll(int status, int d1, int d2, bool oneData)
{
    // send one raw message to every external port in use
    std::vector<unsigned char> m;
    m.push_back((unsigned char)status);
    m.push_back((unsigned char)d1);
    if (!oneData)
        m.push_back((unsigned char)d2);
    for (MidiOut *o : extPorts.values()) {
        if (o) {
            try { o->sendMessage(&m); } catch (...) {}
        }
    }
}

void CALLBACK MidiSynthesizer::masterDspCallback(HDSP, DWORD, void *buffer, DWORD length, void *user)
{
    MidiSynthesizer *s = reinterpret_cast<MidiSynthesizer *>(user);
    if (!s || !buffer || length == 0)
        return;
    float *samples = reinterpret_cast<float *>(buffer);
    const int c = s->masterChannels;
    if (c <= 0)
        return;
    const size_t frames = size_t(length) / (size_t(c) * sizeof(float));
    PTHKCore::processMasterDsp(samples, frames, 1.0f);
}

void MidiSynthesizer::setMasterParams(const MasterDsp::Params &p)
{
    PTHKCore::setMasterParams(p);
}

MasterDsp::Params MidiSynthesizer::masterParams()
{
    return PTHKCore::masterParams();
}

MasterDsp::Meter MidiSynthesizer::masterMeter() const
{
    return PTHKCore::masterMeter();
}
