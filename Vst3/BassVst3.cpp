#include "Vst3/BassVst3.h"
#include "Vst3/Vst3Host.h"

#include <bassmidi.h>

#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

namespace bv {

namespace {

struct Entry
{
    vst3host::Plugin *plugin = nullptr;
    bool   instrument = false;
    DWORD  stream = 0;          // instrument: its own stream. effect: the stream it is attached to
    DWORD  dsp = 0;             // effect: BASS dsp handle
    DWORD  chans = 2;
    bool   isFloat = true;
    std::vector<char>  chunk;   // GetChunk buffer
    std::vector<float> tmp;     // 16-bit conversion
    std::vector<float> tmp2;    // stereo pair of a multi-channel stream
    ~Entry() { delete plugin; }
};

std::mutex gMutex;
std::map<DWORD, std::shared_ptr<Entry>> gMap;
std::atomic<int> gCount(0);
DWORD gNextFxHandle = 0x7F000000;

std::shared_ptr<Entry> find(DWORD h)
{
    if (gCount.load() == 0)
        return nullptr;
    std::lock_guard<std::mutex> g(gMutex);
    auto it = gMap.find(h);
    return it == gMap.end() ? nullptr : it->second;
}

void add(DWORD h, const std::shared_ptr<Entry> &e)
{
    std::lock_guard<std::mutex> g(gMutex);
    gMap[h] = e;
    gCount = (int)gMap.size();
}

void remove(DWORD h)
{
    std::lock_guard<std::mutex> g(gMutex);
    gMap.erase(h);
    gCount = (int)gMap.size();
}

std::string toUtf8(const void *file, DWORD flags)
{
    if (!file) return std::string();
#ifdef _WIN32
    if (flags & BASS_UNICODE) {
        const wchar_t *w = (const wchar_t *)file;
        int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string s((size_t)(n > 0 ? n : 1), '\0');
        if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
        if (!s.empty() && s.back() == '\0') s.pop_back();
        return s;
    }
    // ANSI path
    const char *a = (const char *)file;
    int wn = MultiByteToWideChar(CP_ACP, 0, a, -1, nullptr, 0);
    std::wstring w((size_t)(wn > 0 ? wn : 1), L'\0');
    if (wn > 0) MultiByteToWideChar(CP_ACP, 0, a, -1, &w[0], wn);
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)(n > 0 ? n : 1), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
#else
    (void)flags;
    return std::string((const char *)file);
#endif
}

std::string gLastError;

void logFailure(const std::string &path, const std::string &why)
{
    gLastError = why;
#ifdef _WIN32
    const char *home = std::getenv("USERPROFILE");
    if (!home) return;
    std::string f = std::string(home) + "\\.PTHKKaraoke\\vst3.log";
    FILE *fp = std::fopen(f.c_str(), "ab");
    if (!fp) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(fp, "%04d-%02d-%02d %02d:%02d:%02d  %s  ->  %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                 path.c_str(), why.c_str());
    std::fclose(fp);
#else
    (void)path;
#endif
}

void copyStr(char *dst, size_t cap, const std::string &s)
{
    size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
    std::memcpy(dst, s.data(), n);
    dst[n] = 0;
}

// ---- BASS callbacks ----

DWORD CALLBACK instrumentProc(HSTREAM, void *buffer, DWORD length, void *user)
{
    Entry *e = (Entry *)user;
    if (!e || !e->plugin) {
        std::memset(buffer, 0, length);
        return length;
    }
    const DWORD bytesPerSample = e->isFloat ? 4 : 2;
    int frames = (int)(length / (e->chans * bytesPerSample));
    if (e->isFloat) {
        e->plugin->process(nullptr, (float *)buffer, frames, (int)e->chans);
    } else {
        e->tmp.resize((size_t)frames * e->chans);
        e->plugin->process(nullptr, e->tmp.data(), frames, (int)e->chans);
        short *o = (short *)buffer;
        for (size_t i = 0; i < e->tmp.size(); i++) {
            float v = e->tmp[i] * 32767.0f;
            o[i] = (short)(v > 32767.f ? 32767 : (v < -32768.f ? -32768 : v));
        }
    }
    return length;
}

void CALLBACK effectProc(HDSP, DWORD, void *buffer, DWORD length, void *user)
{
    Entry *e = (Entry *)user;
    if (!e || !e->plugin || e->chans < 1)
        return;
    const DWORD bytesPerSample = e->isFloat ? 4 : 2;
    if (e->chans > 2) {
        // multi-channel stream (the master mixer has 8): the effect works on the first stereo pair, the rest passes untouched
        const int C = (int)e->chans;
        const int frames = (int)(length / (C * bytesPerSample));
        e->tmp.resize((size_t)frames * 2);
        e->tmp2.resize((size_t)frames * 2);
        if (e->isFloat) {
            float *b = (float *)buffer;
            for (int i = 0; i < frames; i++) { e->tmp[2 * i] = b[i * C]; e->tmp[2 * i + 1] = b[i * C + 1]; }
            e->plugin->process(e->tmp.data(), e->tmp2.data(), frames, 2);
            for (int i = 0; i < frames; i++) { b[i * C] = e->tmp2[2 * i]; b[i * C + 1] = e->tmp2[2 * i + 1]; }
        } else {
            short *b = (short *)buffer;
            for (int i = 0; i < frames; i++) { e->tmp[2 * i] = b[i * C] / 32768.0f; e->tmp[2 * i + 1] = b[i * C + 1] / 32768.0f; }
            e->plugin->process(e->tmp.data(), e->tmp2.data(), frames, 2);
            for (int i = 0; i < frames; i++) {
                for (int k = 0; k < 2; k++) {
                    float v = e->tmp2[2 * i + k] * 32767.0f;
                    b[i * C + k] = (short)(v > 32767.f ? 32767 : (v < -32768.f ? -32768 : v));
                }
            }
        }
        return;
    }
    int frames = (int)(length / (e->chans * bytesPerSample));
    if (e->isFloat) {
        e->plugin->process((float *)buffer, (float *)buffer, frames, (int)e->chans);
    } else {
        short *s = (short *)buffer;
        size_t n = (size_t)frames * e->chans;
        e->tmp.resize(n);
        for (size_t i = 0; i < n; i++)
            e->tmp[i] = s[i] / 32768.0f;
        std::vector<float> out(n);
        e->plugin->process(e->tmp.data(), out.data(), frames, (int)e->chans);
        for (size_t i = 0; i < n; i++) {
            float v = out[i] * 32767.0f;
            s[i] = (short)(v > 32767.f ? 32767 : (v < -32768.f ? -32768 : v));
        }
    }
}

} // namespace

bool isVst3Handle(DWORD h) { return find(h) != nullptr; }
std::string lastError() { return gLastError; }

// ---------------------------------------------------------------------------

DWORD ChannelSetDSP(DWORD chHandle, const void *dllFile, DWORD flags, int priority)
{
    std::string path = toUtf8(dllFile, flags);
    if (!vst3host::isVst3Path(path))
        return BASS_VST_ChannelSetDSP(chHandle, dllFile, flags, priority);

    BASS_CHANNELINFO ci;
    if (!BASS_ChannelGetInfo(chHandle, &ci))
        return 0;

    std::string err;
    gLastError.clear();
    vst3host::Plugin *p = vst3host::Plugin::load(path, false, ci.freq ? ci.freq : 44100, &err);
    if (!p) {
        logFailure(path, err.empty() ? std::string("plug-in could not be loaded") : err);
        return 0;
    }
    if (p->isInstrument() || p->info().chansIn == 0) {      // not an effect
        delete p;
        logFailure(path, "this VST3 is an instrument (no audio input), not an effect");
        return 0;
    }

    auto e = std::make_shared<Entry>();
    e->plugin = p;
    e->instrument = false;
    e->stream = chHandle;
    e->chans = ci.chans;
    e->isFloat = (ci.flags & BASS_SAMPLE_FLOAT) != 0;
    e->dsp = BASS_ChannelSetDSP(chHandle, effectProc, e.get(), priority);

    DWORD h;
    {
        std::lock_guard<std::mutex> g(gMutex);
        h = ++gNextFxHandle;
    }
    add(h, e);
    return h;
}

BOOL ChannelRemoveDSP(DWORD chHandle, DWORD vstHandle)
{
    auto e = find(vstHandle);
    if (!e)
        return BASS_VST_ChannelRemoveDSP(chHandle, vstHandle);

    if (e->dsp)
        BASS_ChannelRemoveDSP(e->stream, e->dsp);
    e->dsp = 0;
    remove(vstHandle);          // plugin deleted when the last reference goes away
    return TRUE;
}

DWORD ChannelCreate(DWORD freq, DWORD chans, const void *dllFile, DWORD flags)
{
    std::string path = toUtf8(dllFile, flags);
    if (!vst3host::isVst3Path(path))
        return BASS_VST_ChannelCreate(freq, chans, dllFile, flags);

    std::string err;
    vst3host::Plugin *p = vst3host::Plugin::load(path, true, freq ? freq : 44100, &err);
    if (!p)
        return 0;

    auto e = std::make_shared<Entry>();
    e->plugin = p;
    e->instrument = true;
    e->chans = chans ? chans : 2;
    e->isFloat = (flags & BASS_SAMPLE_FLOAT) != 0;

    DWORD bflags = flags & ~(DWORD)BASS_UNICODE;
    HSTREAM s = BASS_StreamCreate(freq, e->chans, bflags, instrumentProc, e.get());
    if (!s)
        return 0;               // 'e' (and the plugin) are released here
    e->stream = s;
    add(s, e);
    return s;
}

BOOL ChannelFree(DWORD h)
{
    auto e = find(h);
    if (!e)
        return BASS_VST_ChannelFree(h);

    remove(h);
    if (e->instrument && e->stream)
        BASS_StreamFree(e->stream);     // stops the callback before 'e' is released
    return TRUE;
}

BOOL GetInfo(DWORD h, BASS_VST_INFO *ret)
{
    auto e = find(h);
    if (!e)
        return BASS_VST_GetInfo(h, ret);
    if (!ret)
        return FALSE;

    const vst3host::Info &i = e->plugin->info();
    std::memset(ret, 0, sizeof(BASS_VST_INFO));
    ret->channelHandle = e->stream;
    ret->uniqueID = i.uid;
    copyStr(ret->effectName, sizeof(ret->effectName), i.name);
    copyStr(ret->productName, sizeof(ret->productName), i.name);
    copyStr(ret->vendorName, sizeof(ret->vendorName), i.vendor);
    ret->effectVstVersion = 3000;
    ret->hostVstVersion = 3000;
    ret->chansIn = (DWORD)i.chansIn;
    ret->chansOut = (DWORD)i.chansOut;
    ret->hasEditor = i.hasEditor ? 1 : 0;
    ret->editorWidth = (DWORD)i.editorWidth;
    ret->editorHeight = (DWORD)i.editorHeight;
    ret->isInstrument = i.isInstrument ? 1 : 0;
    ret->dspHandle = e->dsp;
    return TRUE;
}

BOOL EmbedEditor(DWORD h, void *parent)
{
    auto e = find(h);
    if (!e)
#ifdef _WIN32
        return BASS_VST_EmbedEditor(h, (HWND)parent);
#else
        return BASS_VST_EmbedEditor(h, parent);
#endif
    return e->plugin->embedEditor(parent) ? TRUE : FALSE;
}

int GetParamCount(DWORD h)
{
    auto e = find(h);
    return e ? e->plugin->paramCount() : BASS_VST_GetParamCount(h);
}

float GetParam(DWORD h, int i)
{
    auto e = find(h);
    return e ? e->plugin->param(i) : BASS_VST_GetParam(h, i);
}

BOOL SetParam(DWORD h, int i, float v)
{
    auto e = find(h);
    if (!e)
        return BASS_VST_SetParam(h, i, v);
    e->plugin->setParam(i, v);
    return TRUE;
}

char *GetChunk(DWORD h, BOOL isPreset, DWORD *length)
{
    auto e = find(h);
    if (!e)
        return BASS_VST_GetChunk(h, isPreset, length);
    e->chunk = e->plugin->saveState();
    if (length) *length = (DWORD)e->chunk.size();
    return e->chunk.empty() ? nullptr : e->chunk.data();
}

DWORD SetChunk(DWORD h, BOOL isPreset, const char *chunk, DWORD length)
{
    auto e = find(h);
    if (!e)
        return BASS_VST_SetChunk(h, isPreset, chunk, length);
    return e->plugin->loadState(chunk, length) ? 1 : 0;
}

int GetProgram(DWORD h)
{
    auto e = find(h);
    return e ? 0 : BASS_VST_GetProgram(h);
}

BOOL SetProgram(DWORD h, int i)
{
    auto e = find(h);
    return e ? TRUE : BASS_VST_SetProgram(h, i);
}

BOOL SetBypass(DWORD h, BOOL b)
{
    auto e = find(h);
    if (!e)
        return BASS_VST_SetBypass(h, b);
    e->plugin->setBypass(b != 0);
    return TRUE;
}

// ---- MIDI -----------------------------------------------------------------

static void sendCC(vst3host::Plugin *p, int ch, int cc, int value)
{
    p->controller(ch, cc, value);
}

BOOL ProcessEvent(DWORD h, DWORD midiCh, DWORD event, DWORD param)
{
    auto e = find(h);
    if (!e)
        return BASS_VST_ProcessEvent(h, midiCh, event, param);

    vst3host::Plugin *p = e->plugin;
    int ch = (int)(midiCh & 15);
    int lo = (int)(param & 0xFF), hi = (int)((param >> 8) & 0xFF);

    switch (event) {
    case MIDI_EVENT_NOTE:
        if (hi > 0) p->noteOn(ch, lo, hi); else p->noteOff(ch, lo, 0);
        break;
    case MIDI_EVENT_KEYPRES:        p->polyPressure(ch, lo, hi); break;
    case MIDI_EVENT_PROGRAM:        p->programChange(ch, (int)param); break;
    case MIDI_EVENT_CHANPRES:       p->channelPressure(ch, (int)param); break;
    case MIDI_EVENT_PITCH:          p->pitchBend(ch, (int)param); break;
    case MIDI_EVENT_BANK:           sendCC(p, ch, 0, (int)param); break;
    case MIDI_EVENT_BANK_LSB:       sendCC(p, ch, 32, (int)param); break;
    case MIDI_EVENT_MODULATION:     sendCC(p, ch, 1, (int)param); break;
    case MIDI_EVENT_PORTATIME:      sendCC(p, ch, 5, (int)param); break;
    case MIDI_EVENT_VOLUME:         sendCC(p, ch, 7, (int)param); break;
    case MIDI_EVENT_PAN:            sendCC(p, ch, 10, (int)param); break;
    case MIDI_EVENT_EXPRESSION:     sendCC(p, ch, 11, (int)param); break;
    case MIDI_EVENT_SUSTAIN:        sendCC(p, ch, 64, (int)param); break;
    case MIDI_EVENT_PORTAMENTO:     sendCC(p, ch, 65, (int)param); break;
    case MIDI_EVENT_SOSTENUTO:      sendCC(p, ch, 66, (int)param); break;
    case MIDI_EVENT_SOFT:           sendCC(p, ch, 67, (int)param); break;
    case MIDI_EVENT_RESONANCE:      sendCC(p, ch, 71, (int)param); break;
    case MIDI_EVENT_RELEASE:        sendCC(p, ch, 72, (int)param); break;
    case MIDI_EVENT_ATTACK:         sendCC(p, ch, 73, (int)param); break;
    case MIDI_EVENT_CUTOFF:         sendCC(p, ch, 74, (int)param); break;
    case MIDI_EVENT_DECAY:          sendCC(p, ch, 75, (int)param); break;
    case MIDI_EVENT_PORTANOTE:      sendCC(p, ch, 84, (int)param); break;
    case MIDI_EVENT_REVERB:         sendCC(p, ch, 91, (int)param); break;
    case MIDI_EVENT_CHORUS:         sendCC(p, ch, 93, (int)param); break;
    case MIDI_EVENT_USERFX:         sendCC(p, ch, 94, (int)param); break;
    case MIDI_EVENT_CONTROL:        sendCC(p, ch, lo, hi); break;
    case MIDI_EVENT_SOUNDOFF:
    case MIDI_EVENT_NOTESOFF:       p->allNotesOff(ch); break;
    case MIDI_EVENT_RESET:          sendCC(p, ch, 121, 0); break;
    default: break;                 // everything else has no VST3 equivalent
    }
    return TRUE;
}

BOOL ProcessEventRaw(DWORD h, const void *event, DWORD length)
{
    auto e = find(h);
    if (!e)
        return BASS_VST_ProcessEventRaw(h, event, length);

    unsigned char b[3] = { 0, 0, 0 };
    if (length == 0) {                  // packed message in the pointer value
        size_t v = (size_t)event;
        b[0] = (unsigned char)((v >> 16) & 0xFF);
        b[1] = (unsigned char)((v >> 8) & 0xFF);
        b[2] = (unsigned char)(v & 0xFF);
    } else {
        const unsigned char *s = (const unsigned char *)event;
        for (DWORD i = 0; i < 3 && i < length; i++) b[i] = s[i];
    }

    int st = b[0] & 0xF0, ch = b[0] & 0x0F, d1 = b[1] & 0x7F, d2 = b[2] & 0x7F;
    vst3host::Plugin *p = e->plugin;
    switch (st) {
    case 0x80: p->noteOff(ch, d1, d2); break;
    case 0x90: if (d2 > 0) p->noteOn(ch, d1, d2); else p->noteOff(ch, d1, 0); break;
    case 0xA0: p->polyPressure(ch, d1, d2); break;
    case 0xB0: p->controller(ch, d1, d2); break;
    case 0xC0: p->programChange(ch, d1); break;
    case 0xD0: p->channelPressure(ch, d1); break;
    case 0xE0: p->pitchBend(ch, d1 | (d2 << 7)); break;
    default: break;
    }
    return TRUE;
}

} // namespace bv
