#include "AsioOutput.h"

#ifdef _WIN32

#include <windows.h>
#include <bass.h>
#include <bassmix.h>
#include <bassasio.h>

#include <QString>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QTextStream>
#include <atomic>
#include <cstring>
#include <thread>

namespace {
    DWORD  g_mix    = 0;      // stereo float mixer fed to the ASIO driver
    bool   g_active = false;
    int    g_rate   = 44100;  // sample rate asked from the driver
    std::atomic<int> g_gen{0};  // generation of the running ASIO session (stops the level probe)
    DWORD  g_stopTick = 0;    // GetTickCount() of the last BASS_ASIO_Free (drivers need a moment before a re-init)
    std::atomic<float> g_gainTarget{1.0f};   // master volume x headroom (a decoding mixer ignores BASS_ATTRIB_VOL)
    float  g_gainCur = 1.0f;

    // BASSASIO pulls the audio from a decoding mixer, where the mixer's own volume attribute is not applied:
    // apply the master gain here (smoothed, so moving the master fader does not click)
    void CALLBACK gainDsp(HDSP, DWORD, void *buffer, DWORD length, void *)
    {
        float *p = static_cast<float *>(buffer);
        BASS_CHANNELINFO gi;
        const size_t chans = (g_mix && BASS_ChannelGetInfo(g_mix, &gi) && gi.chans > 0) ? gi.chans : 2;   // 2, or 4..8 in split mode
        const size_t frames = length / (chans * sizeof(float));
        const float target = g_gainTarget.load();
        float g = g_gainCur;
        for (size_t i = 0; i < frames; i++) {
            g += (target - g) * 0.002f;
            for (size_t c = 0; c < chans; c++)
                p[chans * i + c] *= g;
        }
        g_gainCur = g;
    }
    QString g_error;

    // ---- device panel state ----
    unsigned g_pairMask   = 1;      // enabled stereo pairs (bit n = outputs 2n, 2n+1)
    bool     g_split      = false;  // speaker split (see AsioOutput.h)
    int      g_splitCh    = 0;      // driver outputs used by the running split (0 = off)
    int      g_bufLen     = 0;      // requested ASIO buffer length (samples), 0 = driver default
    int      g_outputs    = 0;      // output channels of the running driver
    QStringList g_chanNames;        // their names
    int      g_bmin = 0, g_bmax = 0, g_bpref = 0, g_bgran = 0;
    // several pairs get the same stereo mix: the first (lowest) pair pulls it from the mixer, the others copy it
    float    g_pairBuf[16384];
    DWORD    g_pairBytes = 0;
    DWORD    g_firstPair = 0;

    // copy of what is sent to the driver (mono), read by the RTA
    const int kTap = 16384;
    float g_tap[16384];
    std::atomic<unsigned> g_tapPos{0};
    void tapWrite(const float *data, DWORD bytes, size_t chans)
    {
        if (chans < 1) return;
        const size_t frames = bytes / (chans * sizeof(float));
        unsigned pos = g_tapPos.load(std::memory_order_relaxed);
        for (size_t i = 0; i < frames; i++) {
            const float l = data[i * chans];
            const float r = chans > 1 ? data[i * chans + 1] : l;
            g_tap[pos & (kTap - 1)] = 0.5f * (l + r);
            pos++;
        }
        g_tapPos.store(pos, std::memory_order_release);
    }
    // runs on the mixer after the master gain, whichever way the driver pulls it (BASSASIO itself or pairProc)
    void CALLBACK tapDsp(HDSP, DWORD, void *buffer, DWORD length, void *)
    {
        BASS_CHANNELINFO ti;
        const size_t tc = (g_mix && BASS_ChannelGetInfo(g_mix, &ti) && ti.chans > 0) ? ti.chans : 2;
        tapWrite(static_cast<const float *>(buffer), length, tc);
    }

    DWORD CALLBACK pairProc(BOOL, DWORD, void *buffer, DWORD length, void *user)
    {
        const DWORD pair = (DWORD)(size_t)user;
        if (pair == g_firstPair) {
            DWORD n = length;
            if (n > sizeof(g_pairBuf)) n = sizeof(g_pairBuf);
            DWORD got = g_mix ? BASS_ChannelGetData(g_mix, g_pairBuf, n) : 0;
            if (got == (DWORD)-1) got = 0;
            if (got < n) memset((char *)g_pairBuf + got, 0, n - got);
            g_pairBytes = n;
            memcpy(buffer, g_pairBuf, n);
            if (length > n) memset((char *)buffer + n, 0, length - n);
        } else {
            const DWORD n = length < g_pairBytes ? length : g_pairBytes;
            memcpy(buffer, g_pairBuf, n);
            if (length > n) memset((char *)buffer + n, 0, length - n);
        }
        return length;
    }
}

namespace {
    void asioLog(const QString &msg)   // ~/.PTHKKaraoke/asio.log : what the driver reported (for support)
    {
        QFile f(QDir::homePath() + "/.PTHKKaraoke/asio.log");
        if (f.size() > 200000) f.remove();
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream(&f) << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "  " << msg << "\n";
        }
    }
}

QStringList AsioOutput::devices()
{
    QStringList list;
    BASS_ASIO_DEVICEINFO info;
    for (DWORD i = 0; BASS_ASIO_GetDeviceInfo(i, &info); i++)
        list << QString::fromLocal8Bit(info.name);
    return list;
}

namespace {
    QString startMarker() { return QDir::homePath() + "/.PTHKKaraoke/asio.starting"; }
    void markStart() { QDir().mkpath(QDir::homePath() + "/.PTHKKaraoke"); QFile f(startMarker()); if (f.open(QIODevice::WriteOnly)) f.write("1"); }
    void clearStart() { QFile::remove(startMarker()); }
    struct StartGuard { ~StartGuard() { clearStart(); } };     // removed on every normal return; stays only after a hang / crash
}

bool AsioOutput::crashedLastStart()
{
    const bool had = QFile::exists(startMarker());
    if (had) { clearStart(); asioLog("previous ASIO start did not finish: ASIO switched off"); }
    return had;
}

bool AsioOutput::start(unsigned long source, int device)
{
    stop();
    g_error.clear();

    if (device < 0 || source == 0)
        return false;

    markStart();
    StartGuard startGuard;

    // many ASIO drivers stay silent (or fail) when re-initialised right after being freed: wait for the driver to settle
    if (g_stopTick != 0) {
        const DWORD since = GetTickCount() - g_stopTick;
        if (since < 1200)
            Sleep(1200 - since);
    }

    if (!BASS_ASIO_Init(device, BASS_ASIO_THREAD)) {
        const int ec = BASS_ASIO_ErrorGetCode();
        {
            BASS_ASIO_DEVICEINFO di; QString nm;
            if (BASS_ASIO_GetDeviceInfo((DWORD)device, &di)) nm = QString::fromLocal8Bit(di.name);
            asioLog(QString("FAIL: init error %1 (driver index %2: %3)").arg(ec).arg(device).arg(nm));
        }
        g_error = QString("ASIO init failed (error %1)").arg(ec);
        if (ec == 3)    // BASS_ASIO_ERROR_DRIVER
            g_error += QString::fromUtf8("\nไดรเวอร์ ASIO เปิดไม่ได้: อาจถูกโปรแกรมอื่นใช้อยู่ (ปิดโปรแกรมเสียงอื่น / ปิด ASIO4ALL ที่ค้างอยู่), "
                                         "อุปกรณ์เสียงของไดรเวอร์นี้ไม่ได้เสียบอยู่ หรือไดรเวอร์ติดตั้งไม่สมบูรณ์");
        return false;
    }

    // the driver must offer at least two output channels (ASIO4ALL has none until a device is switched on in its panel)
    {
        BASS_ASIO_INFO ai;
        if (BASS_ASIO_GetInfo(&ai) && ai.outputs < 2) {
            asioLog(QString("FAIL: driver has %1 output(s), 2 needed").arg(ai.outputs)); g_error = QString("This ASIO driver has no usable stereo output (%1 output channel(s)). "
                              "For ASIO4ALL: open its panel from the tray icon and switch the output device on.").arg(ai.outputs);
            BASS_ASIO_Free();
            return false;
        }
    }

    {
        BASS_ASIO_INFO ai;
        if (BASS_ASIO_GetInfo(&ai)) {
            g_outputs = (int)ai.outputs;
            g_bmin = (int)ai.bufmin; g_bmax = (int)ai.bufmax; g_bpref = (int)ai.bufpref; g_bgran = ai.bufgran;
        }
        g_chanNames.clear();
        for (int c = 0; c < g_outputs; c++) {
            BASS_ASIO_CHANNELINFO ci;
            g_chanNames << (BASS_ASIO_ChannelGetInfo(FALSE, (DWORD)c, &ci) ? QString::fromLocal8Bit(ci.name) : QString());
        }
    }

    // ask for the chosen rate (44.1 kHz free, up to 96 kHz PRO); if the driver refuses, keep its own rate (BASSmix resamples)
    BASS_ASIO_SetRate((double)g_rate);
    double rate = BASS_ASIO_GetRate();
    if (rate < 8000.0)
        rate = 44100.0;

    // speaker split: keep the main mixer's own channel layout (front / rear / centre+sub / side = output pairs 1..4)
    g_splitCh = 0;
    if (g_split) {
        const int usable = g_outputs & ~1;
        if (usable >= 4)
            g_splitCh = usable > 8 ? 8 : usable;
        else
            asioLog(QString("speaker split ignored: the driver has only %1 output(s)").arg(g_outputs));
    }

    // multi-channel main mixer -> stereo for ASIO outputs 1+2 (split mode: 4..8 channels, nothing is mixed down)
    g_mix = BASS_Mixer_StreamCreate((DWORD)rate, g_splitCh ? (DWORD)g_splitCh : 2, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT);
    if (!g_mix || !BASS_Mixer_StreamAddChannel(g_mix, (DWORD)source, g_splitCh ? 0 : BASS_MIXER_CHAN_DOWNMIX)) {
        asioLog(QString("FAIL: ASIO mixer error %1").arg(BASS_ErrorGetCode())); g_error = QString("ASIO mixer failed (error %1)").arg(BASS_ErrorGetCode());
        if (g_mix) BASS_StreamFree(g_mix);
        g_mix = 0;
        BASS_ASIO_Free();
        return false;
    }

    {
        BASS_ASIO_INFO ai2; BASS_ASIO_GetInfo(&ai2);
        BASS_CHANNELINFO ci2; BASS_ChannelGetInfo((DWORD)source, &ci2);
        asioLog(QString("init ok: driver=%1 outputs=%2 rate=%3 bufmin=%4 | source chans=%5 freq=%6 flags=0x%7 | gain=%8")
                .arg(QString::fromLocal8Bit(ai2.name)).arg(ai2.outputs).arg(rate).arg(ai2.bufmin)
                .arg(ci2.chans).arg(ci2.freq).arg(ci2.flags, 0, 16).arg(g_gainTarget.load()));
    }
    g_gainCur = g_gainTarget.load();
    BASS_ChannelSetDSP(g_mix, gainDsp, nullptr, 0);
    BASS_ChannelSetDSP(g_mix, tapDsp, nullptr, -1);   // after the gain: copy for the RTA

    // which stereo pairs get the main mix (default outputs 1+2); pairs the driver does not have are dropped
    // (split mode ignores the mask: the channels go to outputs 1..n in order)
    unsigned mask = g_splitCh ? 1u : g_pairMask;
    for (int pr = 0; pr < 16; pr++)
        if ((mask >> pr) & 1u) { if (2 * pr + 1 >= g_outputs) mask &= ~(1u << pr); }
    if (mask == 0) mask = 1;
    int firstPair = 0;
    while (!((mask >> firstPair) & 1u)) firstPair++;
    g_firstPair = (DWORD)firstPair;
    bool multi = (mask & ~(1u << firstPair)) != 0;

    bool chOk = false;
    if (g_splitCh) {
        chOk = BASS_ASIO_ChannelEnableBASS(FALSE, 0, g_mix, TRUE) != FALSE;      // all channels of the mixer -> outputs 1..n
        asioLog(QString("speaker split: %1 channels -> outputs 1..%1").arg(g_splitCh));
    } else if (!multi) {
        chOk = BASS_ASIO_ChannelEnableBASS(FALSE, (DWORD)(2 * firstPair), g_mix, TRUE) != FALSE;   // one pair: BASSASIO feeds it itself
    } else {
        chOk = true;
        for (int pr = 0; pr < 16 && chOk; pr++) {
            if (!((mask >> pr) & 1u)) continue;
            const DWORD c = (DWORD)(2 * pr);
            chOk = BASS_ASIO_ChannelEnable(FALSE, c, pairProc, (void *)(size_t)pr)
                && BASS_ASIO_ChannelJoin(FALSE, c + 1, (int)c)
                && BASS_ASIO_ChannelSetFormat(FALSE, c, BASS_ASIO_FORMAT_FLOAT);
        }
    }
    asioLog(QString("channels: pairMask=0x%1 used=0x%2 multi=%3 buffer=%4").arg(g_pairMask, 0, 16).arg(mask, 0, 16).arg(multi).arg(g_bufLen));
    if (!chOk) {
        {
            const int ec = BASS_ASIO_ErrorGetCode();
            BASS_ASIO_INFO ai2; QString nm;
            if (BASS_ASIO_GetInfo(&ai2)) nm = QString::fromLocal8Bit(ai2.name);
            asioLog(QString("FAIL: channel setup error %1 (driver: %2)").arg(ec).arg(nm));
            g_error = QString("ASIO channel failed (error %1)").arg(ec);
        }
        BASS_StreamFree(g_mix);
        g_mix = 0;
        BASS_ASIO_Free();
        return false;
    }

    if (!BASS_ASIO_Start((DWORD)g_bufLen, 0)) {
        {
            const int ec = BASS_ASIO_ErrorGetCode();
            BASS_ASIO_INFO ai2; QString nm;
            if (BASS_ASIO_GetInfo(&ai2)) nm = QString::fromLocal8Bit(ai2.name);
            asioLog(QString("FAIL: start error %1 (driver: %2, buffer %3, rate %4)").arg(ec).arg(nm).arg(g_bufLen).arg(BASS_ASIO_GetRate()));
            g_error = QString("ASIO start failed (error %1)").arg(ec);
            g_error += QString::fromUtf8("\nไดรเวอร์นี้เริ่มส่งเสียงไม่ได้ ลองเลือกไดรเวอร์ ASIO อื่น (เช่น FL Studio ASIO หรือ ASIO ของอุปกรณ์เอง) "
                                         "หรือเปลี่ยน Audio buffer / Sample rate แล้วลองใหม่");
        }
        BASS_ASIO_Free();
        BASS_StreamFree(g_mix);
        g_mix = 0;
        return false;
    }

    asioLog(QString("started: asio rate=%1 cpu=%2").arg(BASS_ASIO_GetRate()).arg(BASS_ASIO_GetCPU()));
    g_active = true;
    return true;
}

void AsioOutput::stop()
{
    ++g_gen;
    if (g_active) {
        BASS_ASIO_Stop();
        BASS_ASIO_Free();
        g_stopTick = GetTickCount();
        g_active = false;
    }
    if (g_mix) {
        BASS_StreamFree(g_mix);
        g_mix = 0;
    }
    g_outputs = 0;
    g_splitCh = 0;
    g_chanNames.clear();
}

bool AsioOutput::isActive()   { return g_active; }

bool AsioOutput::tapRead(float *mono, int count, int &rateHz)
{
    if (!g_active || count <= 0 || count > kTap)
        return false;
    const unsigned end = g_tapPos.load(std::memory_order_acquire);
    if (end < unsigned(count))
        return false;
    for (int i = 0; i < count; i++)
        mono[i] = g_tap[(end - unsigned(count) + unsigned(i)) & (kTap - 1)];
    rateHz = g_rate > 0 ? g_rate : 44100;
    return true;
}
QString AsioOutput::lastError() { return g_error; }
void AsioOutput::setRate(int hz) { g_rate = hz > 0 ? hz : 44100; }
void AsioOutput::setGain(float g) { g_gainTarget = g; }
float AsioOutput::gain() { return g_gainTarget.load(); }

void AsioOutput::setPairMask(unsigned mask) { g_pairMask = mask ? mask : 1u; }
unsigned AsioOutput::pairMask() { return g_pairMask; }
void AsioOutput::setSpeakerSplit(bool on) { g_split = on; }
bool AsioOutput::speakerSplit() { return g_split; }
int AsioOutput::splitChannels() { return g_active ? g_splitCh : 0; }
void AsioOutput::setBufferSize(int samples) { g_bufLen = samples > 0 ? samples : 0; }
int AsioOutput::bufferSize() { return g_bufLen; }
int AsioOutput::outputCount() { return g_active ? g_outputs : 0; }
QString AsioOutput::outputName(int channel) { return (g_active && channel >= 0 && channel < g_chanNames.count()) ? g_chanNames[channel] : QString(); }
bool AsioOutput::bufferInfo(int &mn, int &mx, int &pref, int &gran)
{
    if (!g_active) return false;
    mn = g_bmin; mx = g_bmax; pref = g_bpref; gran = g_bgran;
    return true;
}
bool AsioOutput::rateSupported(int hz) { return g_active && BASS_ASIO_CheckRate((double)hz) != FALSE; }
int AsioOutput::currentRate() { return g_active ? (int)(BASS_ASIO_GetRate() + 0.5) : 0; }
void AsioOutput::waitSettle(int ms)
{
    if (g_stopTick == 0) return;
    const DWORD since = GetTickCount() - g_stopTick;
    if ((int)since < ms) Sleep((DWORD)(ms - (int)since));
}
bool AsioOutput::recentlyStopped(int ms) { return g_stopTick != 0 && (int)(GetTickCount() - g_stopTick) < ms; }
void AsioOutput::logLine(const QString &msg) { asioLog(msg); }

#else   // non-Windows: ASIO not available

QStringList AsioOutput::devices() { return QStringList(); }
bool AsioOutput::start(unsigned long, int) { return false; }
void AsioOutput::stop() {}
bool AsioOutput::isActive() { return false; }
bool AsioOutput::tapRead(float *, int, int &) { return false; }
QString AsioOutput::lastError() { return QString(); }
void AsioOutput::setRate(int) {}
void AsioOutput::setGain(float) {}
float AsioOutput::gain() { return 1.0f; }
void AsioOutput::setPairMask(unsigned) {}
unsigned AsioOutput::pairMask() { return 1; }
void AsioOutput::setSpeakerSplit(bool) {}
bool AsioOutput::speakerSplit() { return false; }
int AsioOutput::splitChannels() { return 0; }
void AsioOutput::setBufferSize(int) {}
int AsioOutput::bufferSize() { return 0; }
int AsioOutput::outputCount() { return 0; }
QString AsioOutput::outputName(int) { return QString(); }
bool AsioOutput::bufferInfo(int &, int &, int &, int &) { return false; }
bool AsioOutput::rateSupported(int) { return false; }
int AsioOutput::currentRate() { return 0; }
void AsioOutput::waitSettle(int) {}
bool AsioOutput::recentlyStopped(int) { return false; }
bool AsioOutput::crashedLastStart() { return false; }
void AsioOutput::logLine(const QString &) {}

#endif
