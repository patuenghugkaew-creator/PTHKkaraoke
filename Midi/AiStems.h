// AI vocal separation for video songs on disk (e.g. downloaded YouTube clips), done once ahead
// of time in the background. The result is one 4-channel WAV next to the video:
//     <video>.ai.wav   ch 1-2 = music (instrumental) L/R, ch 3-4 = vocals L/R, 44.1 kHz 16-bit
// At playback this file replaces the video's own sound; a BASS mixing matrix picks how much of
// music / vocals is heard, so the vocal cut switches instantly, with AI quality and no delay.
#ifndef AISTEMS_H
#define AISTEMS_H

#include "AiVocal.h"

#include <bass.h>
#include <bassmix.h>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

class AiStemMaker
{
public:
    ~AiStemMaker() { shutdown(); }

    static QString stemPath(const QString &video) { return video + ".ai.wav"; }
    // usable stems: exist and were made after the video file
    static bool hasStems(const QString &video)
    {
        const QFileInfo s(stemPath(video)), v(video);
        return s.exists() && s.size() > 1024 && s.lastModified() >= v.lastModified();
    }

    // called from the worker thread
    std::function<void(const QString &video, int percent)> onProgress;
    std::function<void(const QString &video, bool ok, const QString &error)> onDone;

    void enqueue(const QString &video, const QString &modelsDir, const QString &modelKey, bool gpu)
    {
        QMutexLocker lock(&m);
        if (video == current || queue.contains(video) || hasStems(video))
            return;
        queue.append(video);
        cfgModels = modelsDir;
        cfgKey = modelKey;
        cfgGpu = gpu;
        if (!worker.joinable()) {
            worker = std::thread([this]() { run(); });
        }
    }

    bool busy() const { QMutexLocker lock(&m); return !current.isEmpty() || !queue.isEmpty(); }
    QString currentVideo() const { QMutexLocker lock(&m); return current; }

    void shutdown()
    {
        cancel.store(true);
        if (worker.joinable())
            worker.join();
    }

private:
    void run()
    {
        for (;;) {
            QString video, models, key;
            bool gpu;
            {
                QMutexLocker lock(&m);
                if (queue.isEmpty() || cancel.load()) {
                    current.clear();
                    worker.detach();        // finished: a later enqueue starts a new thread
                    return;
                }
                video = current = queue.takeFirst();
                models = cfgModels;
                key = cfgKey;
                gpu = cfgGpu;
            }
            QString err;
            const bool ok = makeStems(video, models, key, gpu, err);
            if (onDone)
                onDone(video, ok, err);
            QMutexLocker lock(&m);
            current.clear();
        }
    }

    bool makeStems(const QString &video, const QString &models, const QString &key, bool gpu, QString &err)
    {
        // 1. the video's sound, resampled to 44.1 kHz stereo float
        std::vector<float> mix;
        {
            HSTREAM src = BASS_StreamCreateFile(FALSE, video.utf16(), 0, 0,
                                                BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_UNICODE);
            if (!src) {
                err = QString::fromUtf8("อ่านเสียงของไฟล์วิดีโอไม่ได้ (BASS error %1)").arg(BASS_ErrorGetCode());
                return false;
            }
            HSTREAM mx = BASS_Mixer_StreamCreate(AiVocalEngine::SR, 2,
                                                 BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_MIXER_END);
            if (!mx || !BASS_Mixer_StreamAddChannel(mx, src, BASS_MIXER_CHAN_NORAMPIN | BASS_STREAM_AUTOFREE)) {
                BASS_StreamFree(src);
                if (mx) BASS_StreamFree(mx);
                err = QString::fromUtf8("แปลงเสียงไม่ได้");
                return false;
            }
            std::vector<float> chunk(44100 * 2);
            for (;;) {
                if (cancel.load()) { BASS_StreamFree(mx); err = QString::fromUtf8("ยกเลิกแล้ว"); return false; }
                const DWORD got = BASS_ChannelGetData(mx, chunk.data(), DWORD(chunk.size() * sizeof(float)));
                if (got == DWORD(-1) || got == 0)
                    break;
                mix.insert(mix.end(), chunk.begin(), chunk.begin() + got / sizeof(float));
            }
            BASS_StreamFree(mx);
        }
        const long long frames = (long long)(mix.size() / 2);
        if (frames < AiVocalEngine::SR) {
            err = QString::fromUtf8("ไฟล์ไม่มีเสียง หรือสั้นเกินไป");
            return false;
        }
        if (onProgress)
            onProgress(video, 0);

        // 2. separation (the long part)
        std::vector<float> inst;
        if (!AiVocalEngine::separateTrack(models, key, gpu, mix.data(), frames, inst,
                                          [&](int pct) { if (onProgress) onProgress(video, pct); },
                                          &cancel, &err))
            return false;

        // 3. one 4-channel file: music L/R, vocals L/R (written to .part, then renamed)
        const QString out = stemPath(video), part = out + ".part";
        QFile f(part);
        if (!f.open(QIODevice::WriteOnly)) {
            err = QString::fromUtf8("เขียนไฟล์ไม่ได้: %1").arg(part);
            return false;
        }
        const quint32 rate = AiVocalEngine::SR, chans = 4, bits = 16;
        const quint32 dataBytes = quint32(frames) * chans * (bits / 8);
        auto u32 = [&](quint32 v) { f.write(reinterpret_cast<const char *>(&v), 4); };
        auto u16 = [&](quint16 v) { f.write(reinterpret_cast<const char *>(&v), 2); };
        f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
        f.write("fmt ", 4); u32(16); u16(1); u16(quint16(chans)); u32(rate);
        u32(rate * chans * (bits / 8)); u16(quint16(chans * (bits / 8))); u16(quint16(bits));
        f.write("data", 4); u32(dataBytes);
        auto s16 = [](float v) {
            v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
            return qint16(v * 32767.0f);
        };
        std::vector<qint16> buf;
        buf.reserve(size_t(44100) * 4);
        for (long long i = 0; i < frames; ++i) {
            const float iL = inst[size_t(i) * 2], iR = inst[size_t(i) * 2 + 1];
            const float mL = mix[size_t(i) * 2], mR = mix[size_t(i) * 2 + 1];
            buf.push_back(s16(iL)); buf.push_back(s16(iR));
            buf.push_back(s16(mL - iL)); buf.push_back(s16(mR - iR));
            if (buf.size() >= size_t(44100) * 4) {
                f.write(reinterpret_cast<const char *>(buf.data()), qint64(buf.size() * 2));
                buf.clear();
            }
        }
        f.write(reinterpret_cast<const char *>(buf.data()), qint64(buf.size() * 2));
        f.close();
        QFile::remove(out);
        if (!QFile::rename(part, out)) {
            err = QString::fromUtf8("บันทึกไฟล์ไม่ได้: %1").arg(out);
            return false;
        }
        return true;
    }

    mutable QMutex m;
    QStringList queue;
    QString current;
    QString cfgModels, cfgKey;
    bool cfgGpu = false;
    std::atomic<bool> cancel{false};
    std::thread worker;
};

#endif // AISTEMS_H
