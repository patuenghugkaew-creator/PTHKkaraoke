// AI vocal separation of songs on disk (downloaded YouTube clips, video and MP3 songs), made window
// by window (~5.8 s each) and kept next to the file:
//     <file>.ai.wav        complete: 4-channel WAV, ch 1-2 = music L/R, ch 3-4 = vocals L/R, 44.1 kHz 16-bit
//     <file>.ai.part.wav   while being made: the same layout, windows not made yet are silent
//     <file>.ai.part.map   which windows are made, and the cross-fade tail of each
// While a song plays with the vocal cut on, the windows from the play position on are made first.
// On most PCs that is faster than the song plays, so the clean AI sound starts a few seconds after
// pressing play (or jumping); until then the fast cut is heard (StemMixer). Closing the program keeps
// the windows already made; the work goes on from there next time.
#ifndef AISTEMS_H
#define AISTEMS_H

#include "AiVocal.h"
#include "VocalRemover.h"

#include <bass.h>
#include <bassmix.h>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// ---- a file opened for reading / writing at offsets, shareable while the other side has it open ----
class StemFile
{
public:
    ~StemFile() { close(); }
    bool open(const QString &path, bool write, bool create)
    {
        close();
        h = CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()),
                        write ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        create ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        return h != INVALID_HANDLE_VALUE;
    }
    bool isOpen() const { return h != INVALID_HANDLE_VALUE; }
    void close()
    {
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
    long long size() const
    {
        LARGE_INTEGER s;
        return (isOpen() && GetFileSizeEx(h, &s)) ? s.QuadPart : -1;
    }
    bool resize(long long bytes)
    {
        LARGE_INTEGER p;
        p.QuadPart = bytes;
        return SetFilePointerEx(h, p, nullptr, FILE_BEGIN) && SetEndOfFile(h);
    }
    bool readAt(long long off, void *buf, DWORD n) const
    {
        OVERLAPPED o = {};
        o.Offset = DWORD(off & 0xFFFFFFFF);
        o.OffsetHigh = DWORD(off >> 32);
        DWORD got = 0;
        return ReadFile(h, buf, n, &got, &o) && got == n;
    }
    bool writeAt(long long off, const void *buf, DWORD n)
    {
        OVERLAPPED o = {};
        o.Offset = DWORD(off & 0xFFFFFFFF);
        o.OffsetHigh = DWORD(off >> 32);
        DWORD put = 0;
        return WriteFile(h, buf, n, &put, &o) && put == n;
    }

private:
    HANDLE h = INVALID_HANDLE_VALUE;
};

// ---- what the player needs to know about one song's stems (shared with the maker thread) --------
struct StemTrack
{
    static const int STEP = AiWindowSeparator::STEP;
    static const int WAV_HEADER = 44;
    static const int FRAME_BYTES = 8;           // 4 channels x 16 bit

    QString file;                               // the song
    std::atomic<bool> ready{false};             // frames / nWin / done are set (made by the maker)
    std::atomic<bool> complete{false};          // <file>.ai.wav is there
    long long frames = 0;                       // 44.1 kHz frames
    int nWin = 0;
    std::unique_ptr<std::atomic<unsigned char>[]> done;
    std::atomic<int> doneCount{0};

    // the AI sound of this 44.1 kHz frame can be read
    bool frameReady(long long f) const
    {
        if (complete.load(std::memory_order_acquire))
            return true;
        if (!ready.load(std::memory_order_acquire))
            return false;
        if (f < 0)
            f = 0;
        long long k = f / STEP;
        if (k >= nWin)
            k = nWin - 1;
        return k >= 0 && done[size_t(k)].load(std::memory_order_acquire) != 0;
    }
};

// ---- the maker: one background thread, the playing song first ------------------------------------
class AiStemMaker
{
public:
    static QString stemPath(const QString &f) { return f + ".ai.wav"; }
    static QString partPath(const QString &f) { return f + ".ai.part.wav"; }
    static QString mapPath(const QString &f) { return f + ".ai.part.map"; }
    // complete stems, made after the song file
    static bool hasStems(const QString &f)
    {
        const QFileInfo s(stemPath(f)), v(f);
        return s.exists() && s.size() > 1024 && s.lastModified() >= v.lastModified();
    }
    static bool hasPart(const QString &f) { return QFile::exists(mapPath(f)) && QFile::exists(partPath(f)); }

    // called from the maker thread
    std::function<void(const QString &file, int percent)> onProgress;
    std::function<void(const QString &file, bool ok, const QString &error)> onDone;

    AiStemMaker()
    {
        worker = std::thread([this]() { run(); });
    }
    ~AiStemMaker() { shutdown(); }

    void shutdown()
    {
        quit.store(true);
        cv.notify_all();
        if (worker.joinable())
            worker.join();
    }

    // model for new work (a half-made song keeps the model it was started with)
    void configure(const QString &modelsDir, const QString &modelKey, bool gpu)
    {
        std::lock_guard<std::mutex> lock(m);
        cfgModels = modelsDir;
        cfgKey = modelKey;
        cfgGpu = gpu;
    }

    // The stems of a song, for the player: complete, half-made or not started (filled in later).
    std::shared_ptr<StemTrack> track(const QString &file)
    {
        std::lock_guard<std::mutex> lock(m);
        return jobFor(file)->t;
    }

    // The song plays with the vocal cut on, at `sec`: make its windows from there first.
    void setFocus(const QString &file, double sec)
    {
        {
            std::lock_guard<std::mutex> lock(m);
            std::shared_ptr<Job> j = jobFor(file);
            if (j->failed)
                return;
            focusFrame.store((long long)((std::max)(0.0, sec) * AiVocalEngine::SR));
            if (focus == j)
                return;
            focus = j;
        }
        cv.notify_all();
    }
    void clearFocus()
    {
        std::lock_guard<std::mutex> lock(m);
        focus.reset();
    }
    // made in the background (one after another) while no video / YouTube song plays
    void enqueue(const QString &file)
    {
        {
            std::lock_guard<std::mutex> lock(m);
            std::shared_ptr<Job> j = jobFor(file);
            if (j->t->complete.load() || j->failed || background.contains(j))
                return;
            background.append(j);
        }
        cv.notify_all();
    }
    void setBackgroundAllowed(bool on)
    {
        backgroundOn.store(on);
        if (on)
            cv.notify_all();
    }
    QString currentFile() const
    {
        std::lock_guard<std::mutex> lock(m);
        return working ? working->t->file : QString();
    }

private:
    static const int CHUNK = AiWindowSeparator::CHUNK;
    static const int TRIM = AiWindowSeparator::TRIM;
    static const int XF = AiWindowSeparator::XF;
    static const int STEP = AiWindowSeparator::STEP;
    static const int MAP_HEADER = 64;

    struct Job
    {
        std::shared_ptr<StemTrack> t = std::make_shared<StemTrack>();
        bool failed = false;
        bool opened = false;
        QString key;                            // model of this song's windows
        // maker thread only:
        StemFile wav, map;
        std::vector<float> tails;               // nWin x 2 x XF (next window's cross-fade)
        std::vector<qint16> mix;                // decoded song, 44.1 kHz stereo
        long long decoded = 0;
        HSTREAM src = 0, mx = 0;
        int lastPct = -1;
    };

    // under m
    std::shared_ptr<Job> jobFor(const QString &file)
    {
        const QString id = QFileInfo(file).absoluteFilePath().toLower();
        std::shared_ptr<Job> &j = jobs[id];
        if (j && j->t->complete.load() && j != working && !QFile::exists(stemPath(file)))
            j.reset();                          // the stems file was deleted: start again
        if (!j) {
            j = std::make_shared<Job>();
            j->t->file = QFileInfo(file).absoluteFilePath();
            if (hasStems(file)) {
                const long long bytes = QFileInfo(stemPath(file)).size() - StemTrack::WAV_HEADER;
                j->t->frames = (std::max)(0LL, bytes / StemTrack::FRAME_BYTES);
                j->t->complete.store(true);
                j->t->ready.store(true);
            }
        }
        return j;
    }

    // ---- maker thread ----
    void run()
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        std::unique_ptr<AiWindowSeparator> sep;
        QString sepKey, sepModels;
        bool sepGpu = false;
        std::vector<float> win(size_t(2) * CHUNK), out(size_t(2) * CHUNK);
        // 3/4 of the cores: ~1.6x faster than the song plays with "hq" on an 8-core CPU; idle threads sleep
        const int threads = (std::max)(2, int(std::thread::hardware_concurrency()) * 3 / 4);

        while (!quit.load()) {
            std::shared_ptr<Job> job;
            long long want = -1;
            QString models, newKey;
            bool gpu = false;
            {
                std::unique_lock<std::mutex> lock(m);
                if (focus && !focus->failed && !focus->t->complete.load()) {
                    job = focus;
                    want = focusFrame.load();
                } else if (backgroundOn.load()) {
                    for (int i = 0; i < background.size(); ++i) {
                        if (background[i]->failed || background[i]->t->complete.load()) {
                            background.removeAt(i--);
                            continue;
                        }
                        job = background[i];
                        break;
                    }
                }
                if (!job) {
                    if (working)
                        release(*working);      // free the decoded song while idle
                    working.reset();
                    cv.wait_for(lock, std::chrono::milliseconds(500));
                    continue;
                }
                if (working != job) {
                    if (working)
                        release(*working);
                    working = job;
                }
                models = cfgModels;
                newKey = cfgKey;
                gpu = cfgGpu;
            }

            QString err;
            if (!job->opened && !openJob(*job, newKey, err)) {
                fail(job, err);
                continue;
            }
            if (!sep || sepKey != job->key || sepGpu != gpu || sepModels != models) {
                sep.reset();
                sep.reset(new AiWindowSeparator(models, job->key, gpu, threads));
                sepKey = job->key;
                sepGpu = gpu;
                sepModels = models;
                if (!sep->ok()) {
                    err = sep->error();
                    sep.reset();
                    fail(job, err);
                    continue;
                }
            }
            if (want >= 0)
                want = focusFrame.load();       // where the song is now (opening / loading took time)
            const int k = pickWindow(*job, want);
            if (k < 0) {
                finish(job);
                continue;
            }
            if (!makeWindow(*job, k, *sep, win, out, err)) {
                fail(job, err);
                continue;
            }
            const int pct = int(job->t->doneCount.load() * 100LL / (std::max)(1, job->t->nWin));
            if (pct != job->lastPct) {
                job->lastPct = pct;
                if (onProgress)
                    onProgress(job->t->file, pct);
            }
            if (job->t->doneCount.load() >= job->t->nWin)
                finish(job);
        }
        std::lock_guard<std::mutex> lock(m);
        if (working)
            release(*working);
    }

    void fail(const std::shared_ptr<Job> &job, const QString &err)
    {
        {
            std::lock_guard<std::mutex> lock(m);
            job->failed = true;
            if (focus == job)
                focus.reset();
        }
        release(*job);
        job->wav.close();
        job->map.close();
        if (onDone)
            onDone(job->t->file, false, err);
    }

    // decoded sound and decoder of a song not being worked on
    static void release(Job &j)
    {
        if (j.mx)
            BASS_StreamFree(j.mx);          // frees the source too (AUTOFREE)
        j.mx = j.src = 0;
        std::vector<qint16>().swap(j.mix);
        j.decoded = 0;
    }

    bool startDecoder(Job &j, QString &err)
    {
        const QString f = j.t->file;
        j.src = BASS_StreamCreateFile(FALSE, f.utf16(), 0, 0, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_UNICODE);
        if (!j.src) {
            err = QString::fromUtf8("อ่านเสียงของไฟล์ไม่ได้ (BASS error %1)").arg(BASS_ErrorGetCode());
            return false;
        }
        j.mx = BASS_Mixer_StreamCreate(AiVocalEngine::SR, 2, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_MIXER_END);
        if (!j.mx || !BASS_Mixer_StreamAddChannel(j.mx, j.src, BASS_MIXER_CHAN_NORAMPIN | BASS_STREAM_AUTOFREE)) {
            BASS_StreamFree(j.src);
            if (j.mx)
                BASS_StreamFree(j.mx);
            j.src = j.mx = 0;
            err = QString::fromUtf8("แปลงเสียงไม่ได้");
            return false;
        }
        j.decoded = 0;
        return true;
    }

    // decode the song up to `upTo` frames (it is decoded from the start, ~100x faster than real time)
    bool decodeTo(Job &j, long long upTo, QString &err)
    {
        upTo = (std::min)(upTo, j.t->frames);
        if (j.decoded >= upTo)
            return true;
        if (!j.mx && !startDecoder(j, err))
            return false;
        if (j.mix.size() < size_t(j.t->frames) * 2)
            j.mix.assign(size_t(j.t->frames) * 2, 0);
        std::vector<float> chunk(size_t(AiVocalEngine::SR) * 2);
        while (j.decoded < upTo) {
            if (quit.load())
                return false;
            const DWORD got = BASS_ChannelGetData(j.mx, chunk.data(), DWORD(chunk.size() * sizeof(float)));
            if (got == DWORD(-1) || got == 0) {
                j.decoded = j.t->frames;        // the end (the rest stays silent)
                break;
            }
            const long long n = got / (2 * sizeof(float));
            for (long long i = 0; i < n && j.decoded + i < j.t->frames; ++i)
                for (int c = 0; c < 2; ++c) {
                    const float v = (std::max)(-1.0f, (std::min)(1.0f, chunk[size_t(i) * 2 + size_t(c)]));
                    j.mix[size_t(j.decoded + i) * 2 + size_t(c)] = qint16(std::lround(v * 32767.0f));
                }
            j.decoded += n;
        }
        return true;
    }

    // open the half-made files, or start them
    bool openJob(Job &j, const QString &newKey, QString &err)
    {
        StemTrack &t = *j.t;
        const QString f = t.file;
        if (!QFile::exists(f)) {
            err = QString::fromUtf8("ไม่พบไฟล์เพลง");
            return false;
        }
        if (hasPart(f) && loadPart(j))
            return j.opened = true;

        // new: the song's length, then empty files
        if (!startDecoder(j, err))
            return false;
        const QWORD len = BASS_ChannelGetLength(j.src, BASS_POS_BYTE);
        const double sec = (len == QWORD(-1)) ? -1.0 : BASS_ChannelBytes2Seconds(j.src, len);
        if (sec < 1.0) {
            err = QString::fromUtf8("ไฟล์ไม่มีเสียง หรือสั้นเกินไป");
            return false;
        }
        const long long frames = (long long)std::ceil(sec * AiVocalEngine::SR);
        const int nWin = int((frames + STEP - 1) / STEP);
        j.key = newKey;
        if (!j.wav.open(partPath(f), true, true) || !j.map.open(mapPath(f), true, true)) {
            err = QString::fromUtf8("เขียนไฟล์ไม่ได้: %1").arg(partPath(f));
            return false;
        }
        // WAV header + silent data
        const quint32 rate = AiVocalEngine::SR, chans = 4, bits = 16;
        const quint32 dataBytes = quint32(frames * StemTrack::FRAME_BYTES);
        QByteArray h;
        auto u32 = [&](quint32 v) { h.append(reinterpret_cast<const char *>(&v), 4); };
        auto u16 = [&](quint16 v) { h.append(reinterpret_cast<const char *>(&v), 2); };
        h.append("RIFF", 4); u32(36 + dataBytes); h.append("WAVE", 4);
        h.append("fmt ", 4); u32(16); u16(1); u16(quint16(chans)); u32(rate);
        u32(rate * chans * (bits / 8)); u16(quint16(chans * (bits / 8))); u16(quint16(bits));
        h.append("data", 4); u32(dataBytes);
        if (!j.wav.resize(StemTrack::WAV_HEADER + frames * StemTrack::FRAME_BYTES)
                || !j.wav.writeAt(0, h.constData(), DWORD(h.size()))) {
            err = QString::fromUtf8("พื้นที่ดิสก์ไม่พอสำหรับไฟล์เสียงที่แยกแล้ว");
            return false;
        }
        // map: header, done bytes, tails
        QByteArray mh(MAP_HEADER, 0);
        std::memcpy(mh.data(), "PTHKSTM1", 8);
        std::memcpy(mh.data() + 8, &frames, 8);
        std::memcpy(mh.data() + 16, &nWin, 4);
        const int step = STEP, xf = XF;
        std::memcpy(mh.data() + 20, &step, 4);
        std::memcpy(mh.data() + 24, &xf, 4);
        const QByteArray k = j.key.toLatin1().left(15);
        std::memcpy(mh.data() + 28, k.constData(), size_t(k.size()));
        if (!j.map.resize(tailsOffset(nWin) + (long long)nWin * 2 * XF * 4)
                || !j.map.writeAt(0, mh.constData(), MAP_HEADER)) {
            err = QString::fromUtf8("เขียนไฟล์ไม่ได้: %1").arg(mapPath(f));
            return false;
        }
        j.tails.assign(size_t(nWin) * 2 * XF, 0.0f);
        t.frames = frames;
        t.nWin = nWin;
        t.done.reset(new std::atomic<unsigned char>[size_t(nWin)]);
        for (int i = 0; i < nWin; ++i)
            t.done[size_t(i)].store(0);
        t.doneCount.store(0);
        t.ready.store(true, std::memory_order_release);
        return j.opened = true;
    }

    static long long tailsOffset(int nWin) { return MAP_HEADER + ((nWin + 7) / 8) * 8; }

    bool loadPart(Job &j)
    {
        StemTrack &t = *j.t;
        const QString f = t.file;
        if (!j.map.open(mapPath(f), true, false) || !j.wav.open(partPath(f), true, false))
            return false;
        char mh[MAP_HEADER];
        long long frames = 0;
        int nWin = 0, step = 0, xf = 0;
        if (!j.map.readAt(0, mh, MAP_HEADER) || std::memcmp(mh, "PTHKSTM1", 8) != 0)
            return false;
        std::memcpy(&frames, mh + 8, 8);
        std::memcpy(&nWin, mh + 16, 4);
        std::memcpy(&step, mh + 20, 4);
        std::memcpy(&xf, mh + 24, 4);
        if (step != STEP || xf != XF || frames <= 0 || nWin != int((frames + STEP - 1) / STEP)
                || j.wav.size() != StemTrack::WAV_HEADER + frames * StemTrack::FRAME_BYTES
                || j.map.size() < tailsOffset(nWin) + (long long)nWin * 2 * XF * 4)
            return false;
        j.key = QString::fromLatin1(mh + 28, int(strnlen(mh + 28, 16)));
        std::vector<unsigned char> d(static_cast<size_t>(nWin));
        j.tails.assign(size_t(nWin) * 2 * XF, 0.0f);
        if (!j.map.readAt(MAP_HEADER, d.data(), DWORD(nWin))
                || !j.map.readAt(tailsOffset(nWin), j.tails.data(), DWORD(j.tails.size() * 4)))
            return false;
        t.frames = frames;
        t.nWin = nWin;
        t.done.reset(new std::atomic<unsigned char>[size_t(nWin)]);
        int n = 0;
        for (int i = 0; i < nWin; ++i) {
            t.done[size_t(i)].store(d[size_t(i)] ? 1 : 0);
            n += d[size_t(i)] ? 1 : 0;
        }
        t.doneCount.store(n);
        t.ready.store(true, std::memory_order_release);
        return true;
    }

    // The next window. While a song plays: the window it will be in when this one is made (made
    // sooner, it would be passed already), on to the end, then from the start.
    int pickWindow(const Job &j, long long want) const
    {
        const StemTrack &t = *j.t;
        int from = 0;
        if (want >= 0) {
            double lead = winSec * 1.15 + 0.3;
            const long long need = want + (long long)(lead * AiVocalEngine::SR) + CHUNK;
            if (need > j.decoded)               // decoding up to there first (~150x real time)
                lead += double(need - j.decoded) / AiVocalEngine::SR / 150.0;
            from = int((std::min<long long>)((want + (long long)(lead * AiVocalEngine::SR)) / STEP, t.nWin - 1));
        }
        for (int k = from; k < t.nWin; ++k)
            if (!t.done[size_t(k)].load())
                return k;
        for (int k = 0; k < from; ++k)
            if (!t.done[size_t(k)].load())
                return k;
        return -1;
    }

    bool makeWindow(Job &j, int k, AiWindowSeparator &sep, std::vector<float> &win, std::vector<float> &out, QString &err)
    {
        StemTrack &t = *j.t;
        const long long s = (long long)k * STEP - TRIM;          // window start in the song
        if (!decodeTo(j, s + CHUNK, err))
            return quit.load() ? true : false;
        if (quit.load())
            return true;
        float peak = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < CHUNK; ++i) {
                const long long p = s + i;
                const float v = (p >= 0 && p < t.frames) ? j.mix[size_t(p) * 2 + size_t(c)] / 32768.0f : 0.0f;
                win[size_t(c) * CHUNK + size_t(i)] = v;
                peak = (std::max)(peak, std::fabs(v));
            }
        if (peak < 1e-4f)
            std::fill(out.begin(), out.end(), 0.0f);
        else {
            const DWORD t0 = GetTickCount();
            if (!sep.separate(win.data(), out.data())) {
                err = sep.error();
                return false;
            }
            winSec = 0.7 * winSec + 0.3 * (GetTickCount() - t0) / 1000.0;
        }

        const bool prevDone = k > 0 && t.done[size_t(k - 1)].load();
        const bool nextDone = k + 1 < t.nWin && t.done[size_t(k + 1)].load();
        const long long first = (long long)k * STEP;
        const int n = int((std::min<long long>)(STEP, t.frames - first));
        std::vector<qint16> buf(size_t(n) * 4);
        for (int i = 0; i < n; ++i) {
            const long long p = first + i;
            for (int c = 0; c < 2; ++c) {
                float v = out[size_t(c) * CHUNK + size_t(TRIM + i)];
                if (prevDone && i < XF) {
                    const float r = float(i) / XF;
                    v = j.tails[(size_t(k - 1) * 2 + size_t(c)) * XF + size_t(i)] * (1.0f - r) + v * r;
                }
                const float mixv = j.mix[size_t(p) * 2 + size_t(c)] / 32768.0f;
                buf[size_t(i) * 4 + size_t(c)] = s16(v);
                buf[size_t(i) * 4 + 2 + size_t(c)] = s16(mixv - v);
            }
        }
        if (!j.wav.writeAt(StemTrack::WAV_HEADER + first * StemTrack::FRAME_BYTES, buf.data(), DWORD(buf.size() * 2))) {
            err = QString::fromUtf8("เขียนไฟล์เสียงที่แยกแล้วไม่ได้");
            return false;
        }
        // this window's tail: the cross-fade into window k+1
        float *tail = &j.tails[size_t(k) * 2 * XF];
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < XF; ++i)
                tail[size_t(c) * XF + size_t(i)] = out[size_t(c) * CHUNK + size_t(TRIM + STEP + i)];
        j.map.writeAt(tailsOffset(t.nWin) + (long long)k * 2 * XF * 4, tail, DWORD(2 * XF * 4));
        if (nextDone) {
            // window k+1 was made first: blend its start with this window's tail now
            const long long nf = first + STEP;
            const int m2 = int((std::min<long long>)(XF, t.frames - nf));
            if (m2 > 0) {
                std::vector<qint16> nb(size_t(m2) * 4);
                if (j.wav.readAt(StemTrack::WAV_HEADER + nf * StemTrack::FRAME_BYTES, nb.data(), DWORD(nb.size() * 2))) {
                    for (int i = 0; i < m2; ++i) {
                        const float r = float(i) / XF;
                        for (int c = 0; c < 2; ++c) {
                            const float raw = nb[size_t(i) * 4 + size_t(c)] / 32767.0f;
                            const float v = tail[size_t(c) * XF + size_t(i)] * (1.0f - r) + raw * r;
                            const float mixv = j.mix[size_t(nf + i) * 2 + size_t(c)] / 32768.0f;
                            nb[size_t(i) * 4 + size_t(c)] = s16(v);
                            nb[size_t(i) * 4 + 2 + size_t(c)] = s16(mixv - v);
                        }
                    }
                    j.wav.writeAt(StemTrack::WAV_HEADER + nf * StemTrack::FRAME_BYTES, nb.data(), DWORD(nb.size() * 2));
                }
            }
        }
        const unsigned char one = 1;
        j.map.writeAt(MAP_HEADER + k, &one, 1);
        t.done[size_t(k)].store(1, std::memory_order_release);
        t.doneCount.fetch_add(1);
        return true;
    }

    // all windows made: the part file becomes <file>.ai.wav
    void finish(const std::shared_ptr<Job> &job)
    {
        Job &j = *job;
        StemTrack &t = *j.t;
        j.wav.close();
        j.map.close();
        release(j);
        const QString f = t.file;
        const bool ok = MoveFileExW(reinterpret_cast<const wchar_t *>(partPath(f).utf16()),
                                    reinterpret_cast<const wchar_t *>(stemPath(f).utf16()),
                                    MOVEFILE_REPLACE_EXISTING) != 0;
        if (ok) {
            QFile::remove(mapPath(f));
            t.complete.store(true, std::memory_order_release);
        }
        {
            std::lock_guard<std::mutex> lock(m);
            if (!ok)
                j.failed = true;
            if (focus == job)
                focus.reset();
            background.removeAll(job);
        }
        if (onDone)
            onDone(f, ok, ok ? QString() : QString::fromUtf8("บันทึกไฟล์ไม่ได้: %1").arg(stemPath(f)));
    }

    static qint16 s16(float v)
    {
        v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
        return qint16(v * 32767.0f);
    }

    mutable std::mutex m;
    std::condition_variable_any cv;
    QHash<QString, std::shared_ptr<Job>> jobs;
    std::shared_ptr<Job> focus, working;
    QList<std::shared_ptr<Job>> background;
    std::atomic<long long> focusFrame{0};
    double winSec = 4.0;                        // time to make one window (maker thread)
    std::atomic<bool> backgroundOn{true};
    QString cfgModels, cfgKey = "hq";
    bool cfgGpu = false;
    std::atomic<bool> quit{false};
    std::thread worker;
};

// ---- the player side: a DSP on the song's own (stereo) sound -------------------------------------
// Where the AI windows are made: music (+ the vocal level) from the stems. Elsewhere: the fast cut.
// Both paths have the same delay (the fast cut's), and the change between them glides (~30 ms).
class StemMixer
{
public:
    StemMixer(std::shared_ptr<StemTrack> t, int sampleRate)
        : track(std::move(t)), rate(sampleRate > 0 ? sampleRate : 44100), fast(rate)
    {
        lat = VocalRemover::latency();
        dl.assign(size_t(lat) * 2, 0.0f);
        dv.assign(size_t(lat), 0);
        rampStep = 1.0f / (0.03f * float(rate));
        ratio = double(AiVocalEngine::SR) / double(rate);
    }

    void setCut(bool on) { cut.store(on); }
    void setLevels(float vocals, float music) { gV.store(vocals); gM.store(music); }
    // the source was moved (BASS_SYNC_SETPOS): its new position in frames
    void setPosition(long long frame)
    {
        pos.store(frame);
        jumped.store(true);
    }
    long long position() const { return pos.load(); }
    // the AI sound is heard now (for the status line)
    bool aiHeard() const { return heard.load(); }
    std::shared_ptr<StemTrack> stems() const { return track; }

    // interleaved stereo float at `rate`, in place (DSP thread)
    void process(float *s, int n)
    {
        if (n <= 0)
            return;
        if (jumped.exchange(false)) {           // after a jump the delayed sound belongs to the old place
            std::fill(dl.begin(), dl.end(), 0.0f);
            std::fill(dv.begin(), dv.end(), 0);
            validCount = 0;
            w = 0.0f;
        }
        const bool on = cut.load();
        fast.setEnabled(on);
        const long long p0 = pos.fetch_add(n);
        dry.assign(s, s + size_t(n) * 2);
        fast.process(s, size_t(n));             // s = fast cut (or the plain sound), `lat` late

        // the stems for [p0, p0+n) (44.1 kHz frames, interpolated to the song's rate), usable when
        // made here and a little further (so a fade back to the fast cut ends before they stop)
        const long long fa = (long long)std::floor(p0 * ratio);
        const long long fb = (long long)std::floor((p0 + n) * ratio) + 2;
        const long long ahead = (long long)(0.04 * AiVocalEngine::SR);
        const bool valid = on && track && track->frameReady(fa) && track->frameReady(fb + ahead);
        if (!valid && validCount == 0 && w == 0.0f) {
            for (int i = 0; i < n; ++i)         // AI not heard: keep the delay line in step
                pushDelay(0.0f, 0.0f, 0);
            heard.store(false);
            return;
        }
        if (valid)
            readStems(fa, fb);
        const float gv = gV.load(), gm = gM.load();
        for (int i = 0; i < n; ++i) {
            float aiL = 0.0f, aiR = 0.0f;
            if (valid) {
                const double f = (p0 + i) * ratio - double(fa);
                const long long i0 = (long long)f;
                const float fr = float(f - double(i0));
                float inst[2];
                for (int c = 0; c < 2; ++c) {
                    const float a = at(i0, c), b = at(i0 + 1, c);
                    inst[c] = a + (b - a) * fr;
                }
                aiL = inst[0] * gm + (dry[size_t(i) * 2] - inst[0]) * gv;
                aiR = inst[1] * gm + (dry[size_t(i) * 2 + 1] - inst[1]) * gv;
            }
            float dL, dR;
            unsigned char ok;
            pushDelay(aiL, aiR, valid ? 1 : 0, &dL, &dR, &ok);
            const float target = (on && ok) ? 1.0f : 0.0f;
            w += (target > w) ? rampStep : -rampStep;
            w = w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);
            s[size_t(i) * 2] = dL * w + s[size_t(i) * 2] * (1.0f - w);
            s[size_t(i) * 2 + 1] = dR * w + s[size_t(i) * 2 + 1] * (1.0f - w);
        }
        heard.store(w > 0.5f);
    }

private:
    // delay line for the AI sound (the fast cut's delay), with "usable" flags
    void pushDelay(float l, float r, unsigned char ok,
                   float *outL = nullptr, float *outR = nullptr, unsigned char *outOk = nullptr)
    {
        if (outL) *outL = dl[size_t(dpos) * 2];
        if (outR) *outR = dl[size_t(dpos) * 2 + 1];
        if (outOk) *outOk = dv[size_t(dpos)];
        validCount += int(ok) - int(dv[size_t(dpos)]);
        dl[size_t(dpos) * 2] = l;
        dl[size_t(dpos) * 2 + 1] = r;
        dv[size_t(dpos)] = ok;
        dpos = (dpos + 1) % lat;
    }

    float at(long long i, int c) const
    {
        if (i < 0 || i >= (long long)(rd.size() / 4))
            return 0.0f;
        return rd[size_t(i) * 4 + size_t(c)] / 32767.0f;
    }

    // stems frames [a, b) into rd (silent where not readable)
    void readStems(long long a, long long b)
    {
        const long long n = (std::max)(0LL, b - a);
        rd.assign(size_t(n) * 4, 0);
        if (!track || !track->ready.load(std::memory_order_acquire))
            return;
        if (!file.isOpen()) {
            const DWORD now = GetTickCount();
            if (now - lastTry < 500)
                return;
            lastTry = now;
            const QString f = track->file;
            if (!file.open(AiStemMaker::stemPath(f), false, false))
                file.open(AiStemMaker::partPath(f), false, false);
            if (!file.isOpen())
                return;
        }
        const long long lo = (std::max)(0LL, a), hi = (std::min)(b, track->frames);
        if (hi <= lo)
            return;
        file.readAt(StemTrack::WAV_HEADER + lo * StemTrack::FRAME_BYTES, &rd[size_t(lo - a) * 4],
                    DWORD((hi - lo) * StemTrack::FRAME_BYTES));
    }

    std::shared_ptr<StemTrack> track;
    int rate;
    double ratio = 1.0;
    VocalRemover fast;
    int lat = 1024;
    std::vector<float> dl, dry;
    std::vector<unsigned char> dv;
    int dpos = 0, validCount = 0;
    std::atomic<bool> jumped{false}, heard{false};
    std::vector<qint16> rd;
    StemFile file;
    DWORD lastTry = 0;
    float w = 0.0f, rampStep = 0.001f;
    std::atomic<bool> cut{false};
    std::atomic<float> gV{0.0f}, gM{1.0f};
    std::atomic<long long> pos{0};
};

#endif // AISTEMS_H
