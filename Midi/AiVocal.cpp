#include "AiVocal.h"

#include <QDir>
#include <QCoreApplication>
#include <QTime>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstring>

#define NOMINMAX
#include <windows.h>
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")

#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>
#include "pocketfft_hdronly.h"

// ---- constants of the MDX-Net pipeline (separator.py) -------------------------------------------
static const int HOP = 1024;
static const int DIM_T = 256;
static const int CHUNK = HOP * (DIM_T - 1);     // 261120 samples ~ 5.92 s
static const int TRIM = 3072;                   // edge samples discarded on each side
static const int XF = 1024;                     // cross-fade between consecutive outputs
static const char *MODEL_URL = "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/";

static const AiVocalEngine::ModelInfo kModels[] = {
    { "light", "UVR_MDXNET_1_9703.onnx",     6144, 2048, 1.030f, true,  "เบา (ลื่น กินเครื่องน้อย)" },
    { "hq",    "UVR-MDX-NET-Inst_HQ_3.onnx", 6144, 3072, 1.022f, false, "สะอาดสุด (หนัก ใช้การ์ดจอมาก)" },
    { "kara",  "UVR_MDXNET_KARA_2.onnx",     5120, 2048, 1.065f, false, "เก็บเสียงประสาน (ตัดแค่นักร้องนำ)" },
};

const AiVocalEngine::ModelInfo *AiVocalEngine::models(int *count)
{
    *count = int(sizeof(kModels) / sizeof(kModels[0]));
    return kModels;
}

const AiVocalEngine::ModelInfo *AiVocalEngine::model(const QString &key)
{
    for (const ModelInfo &mi : kModels)
        if (key == mi.key)
            return &mi;
    return &kModels[0];
}

static double nowSec()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ---- one ONNX model: mix (planar 2 x CHUNK) -> instrumental (planar 2 x CHUNK) -------------------
class MdxModel
{
public:
    MdxModel(const std::wstring &path, const AiVocalEngine::ModelInfo &info, bool gpu, int threads)
        : cfg(info), env(ORT_LOGGING_LEVEL_WARNING, "pthk")
    {
        Ort::SessionOptions opts;
        if (threads > 0)
            opts.SetIntraOpNumThreads(threads);
        if (gpu) {
            const OrtDmlApi *dml = nullptr;
            Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void **>(&dml));
            if (dml) {
                opts.DisableMemPattern();
                opts.SetExecutionMode(ORT_SEQUENTIAL);
                OrtStatus *st = dml->SessionOptionsAppendExecutionProvider_DML(opts, 0);
                if (st)
                    Ort::GetApi().ReleaseStatus(st);
                else
                    onGpu = true;
            }
        }
        session.reset(new Ort::Session(env, path.c_str(), opts));
        Ort::AllocatorWithDefaultOptions alloc;
        inName = session->GetInputNameAllocated(0, alloc).get();
        outName = session->GetOutputNameAllocated(0, alloc).get();
        win.resize(cfg.nfft);
        for (int i = 0; i < cfg.nfft; i++)          // periodic Hann
            win[i] = float(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / cfg.nfft));
    }

    bool gpu() const { return onGpu; }

    void instrumental(const float *mix, float *inst)
    {
        const int nfft = cfg.nfft, dimf = cfg.dimf, bins = nfft / 2 + 1, pad = nfft / 2;
        const size_t plane = size_t(dimf) * DIM_T;
        std::vector<float> x(4 * plane, 0.0f);
        std::vector<float> frames(size_t(DIM_T) * nfft);
        std::vector<std::complex<float>> spec(size_t(DIM_T) * bins);
        const pocketfft::shape_t shapeR{ size_t(DIM_T), size_t(nfft) };
        const pocketfft::stride_t strideR{ ptrdiff_t(nfft * sizeof(float)), ptrdiff_t(sizeof(float)) };
        const pocketfft::stride_t strideC{ ptrdiff_t(bins * sizeof(std::complex<float>)), ptrdiff_t(sizeof(std::complex<float>)) };

        // STFT (torch.stft center=True, reflect padding), first dimf bins; bins 0..2 zeroed
        for (int c = 0; c < 2; c++) {
            const float *s = mix + size_t(c) * CHUNK;
            auto at = [&](int i) -> float {         // reflect-padded sample i of the padded signal
                int k = i - pad;
                if (k < 0) k = -k;
                if (k >= CHUNK) k = 2 * (CHUNK - 1) - k;
                return s[k];
            };
            for (int t = 0; t < DIM_T; t++) {
                float *f = &frames[size_t(t) * nfft];
                const int base = t * HOP;
                for (int i = 0; i < nfft; i++)
                    f[i] = at(base + i) * win[i];
            }
            pocketfft::r2c(shapeR, strideR, strideC, { 1 }, pocketfft::FORWARD, frames.data(), spec.data(), 1.0f);
            float *re = &x[size_t(2 * c) * plane], *im = &x[size_t(2 * c + 1) * plane];
            for (int b = 3; b < dimf; b++)
                for (int t = 0; t < DIM_T; t++) {
                    const std::complex<float> v = spec[size_t(t) * bins + b];
                    re[size_t(b) * DIM_T + t] = v.real();
                    im[size_t(b) * DIM_T + t] = v.imag();
                }
        }

        // model
        const int64_t shape[4] = { 1, 4, dimf, DIM_T };
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value in = Ort::Value::CreateTensor<float>(mem, x.data(), x.size(), shape, 4);
        const char *inNames[] = { inName.c_str() };
        const char *outNames[] = { outName.c_str() };
        std::vector<Ort::Value> out = session->Run(Ort::RunOptions{ nullptr }, inNames, &in, 1, outNames, 1);
        const float *y = out[0].GetTensorData<float>();

        // iSTFT with window-square normalisation
        const int totalLen = nfft + HOP * (DIM_T - 1);
        std::vector<float> ola(totalLen), wsum(totalLen, 0.0f);
        for (int t = 0; t < DIM_T; t++)
            for (int i = 0; i < nfft; i++)
                wsum[size_t(t) * HOP + i] += win[i] * win[i];
        for (int c = 0; c < 2; c++) {
            std::fill(spec.begin(), spec.end(), std::complex<float>(0, 0));
            const float *re = y + size_t(2 * c) * plane, *im = y + size_t(2 * c + 1) * plane;
            for (int b = 0; b < dimf; b++)
                for (int t = 0; t < DIM_T; t++)
                    spec[size_t(t) * bins + b] = std::complex<float>(re[size_t(b) * DIM_T + t], im[size_t(b) * DIM_T + t]);
            pocketfft::c2r(shapeR, strideC, strideR, { 1 }, pocketfft::BACKWARD, spec.data(), frames.data(), 1.0f / nfft);
            std::fill(ola.begin(), ola.end(), 0.0f);
            for (int t = 0; t < DIM_T; t++) {
                const float *f = &frames[size_t(t) * nfft];
                float *o = &ola[size_t(t) * HOP];
                for (int i = 0; i < nfft; i++)
                    o[i] += f[i] * win[i];
            }
            const float *s = mix + size_t(c) * CHUNK;
            float *dst = inst + size_t(c) * CHUNK;
            for (int i = 0; i < CHUNK; i++) {
                const float est = ola[pad + i] / std::max(wsum[pad + i], 1e-8f) * cfg.compensate;
                dst[i] = cfg.primaryVocals ? s[i] - est : est;
            }
        }
    }

private:
    AiVocalEngine::ModelInfo cfg;
    Ort::Env env;
    std::unique_ptr<Ort::Session> session;
    std::string inName, outName;
    std::vector<float> win;
    bool onGpu = false;
};

static std::atomic<int> s_devicePref{0};
void AiVocalEngine::setDevicePreference(int pref) { s_devicePref.store(pref); }
int AiVocalEngine::devicePreference() { return s_devicePref.load(); }

// speed-test results etc. go to yt-video.log next to the exe
// Developer timing logs: only when "debug-logs.txt" is next to the exe.
static bool timingLogsOn()
{
    static const int on = QFile::exists(QCoreApplication::applicationDirPath() + "/debug-logs.txt") ? 1 : 0;
    return on == 1;
}

static void aiLog(const QString &line)
{
    if (!timingLogsOn())
        return;
    QFile f(QCoreApplication::applicationDirPath() + "/yt-video.log");
    if (f.open(QIODevice::Append | QIODevice::Text))
        f.write((QTime::currentTime().toString("hh:mm:ss.zzz ") + "AI: " + line + QChar(10)).toUtf8());
}

// Whole-track separation for files on disk (downloaded video songs). Same model and the
// same trimmed-window + cross-fade stitching as the live engine, but run ahead of time,
// so playback later has no delay and the vocal cut switches instantly.
bool AiVocalEngine::separateTrack(const QString &modelsDir, const QString &key, bool gpu,
                                  const float *mix, long long frames, std::vector<float> &inst,
                                  const std::function<void(int)> &progress,
                                  const std::atomic<bool> *cancel, QString *error)
{
    const ModelInfo *mi = model(key);
    if (!mi) {
        if (error) *error = QString::fromUtf8("ไม่รู้จักรุ่น AI: %1").arg(key);
        return false;
    }
    const QString path = modelsDir + "/" + mi->file;
    if (!QFile::exists(path)) {
        if (error) *error = QString::fromUtf8("ยังไม่มีไฟล์โมเดล AI (%1) เปิดใช้ AI ตัดเสียงร้องกับ YouTube สักครั้งเพื่อดาวน์โหลด").arg(mi->file);
        return false;
    }
    try {
        const int threads = std::max(4, int(std::thread::hardware_concurrency()) / 2);
        MdxModel mm(path.toStdWString(), *mi, gpu, gpu ? 0 : threads);
        const int H = CHUNK - 2 * TRIM - XF;            // output samples per window
        inst.assign(size_t(frames) * 2, 0.0f);
        std::vector<float> win(size_t(2) * CHUNK), out(size_t(2) * CHUNK), tail;
        const long long nWin = (frames + H - 1) / H;
        for (long long k = 0; k < nWin; ++k) {
            if (cancel && cancel->load()) {
                if (error) *error = QString::fromUtf8("ยกเลิกแล้ว");
                return false;
            }
            const long long s = k * H - TRIM;           // window start in the track (may be < 0)
            float peak = 0.0f;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < CHUNK; ++i) {
                    const long long src = s + i;
                    const float v = (src >= 0 && src < frames) ? mix[size_t(src) * 2 + size_t(c)] : 0.0f;
                    win[size_t(c) * CHUNK + size_t(i)] = v;
                    peak = std::max(peak, std::fabs(v));
                }
            if (peak < 1e-4f)
                std::fill(out.begin(), out.end(), 0.0f);
            else
                mm.instrumental(win.data(), out.data());
            for (int c = 0; c < 2; ++c) {
                const float *oc = &out[size_t(c) * CHUNK];
                for (int i = 0; i < H; ++i) {
                    const long long dst = k * H + i;
                    if (dst >= frames)
                        break;
                    float v = oc[TRIM + i];
                    if (!tail.empty() && i < XF) {
                        const float r = float(i) / XF;
                        v = tail[size_t(c) * XF + size_t(i)] * (1.0f - r) + v * r;
                    }
                    inst[size_t(dst) * 2 + size_t(c)] = v;
                }
            }
            tail.assign(size_t(2) * XF, 0.0f);          // overlaps the next window's first XF samples
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < XF; ++i)
                    tail[size_t(c) * XF + size_t(i)] = out[size_t(c) * CHUNK + size_t(TRIM + H + i)];
            if (progress)
                progress(int((k + 1) * 100 / nWin));
        }
        return true;
    } catch (const std::exception &e) {
        if (error) *error = QString::fromLocal8Bit(e.what());
    } catch (...) {
        if (error) *error = QString::fromUtf8("AI แยกเสียงไม่สำเร็จ");
    }
    return false;
}

bool AiVocalEngine::separateOnce(const QString &modelFile, const QString &key, bool gpu, const float *mix, float *inst)
{
    try {
        MdxModel mm(modelFile.toStdWString(), *model(key), gpu, 0);
        mm.instrumental(mix, inst);
        return true;
    } catch (...) {
        return false;
    }
}

// ---- engine ---------------------------------------------------------------------------------------

AiVocalEngine::AiVocalEngine(const QString &modelsDir) : dir(modelsDir)
{
    buf.assign(size_t(2) * CHUNK, 0.0f);
    workerThread = std::thread([this]() { worker(); });
}

AiVocalEngine::~AiVocalEngine()
{
    quit = true;
    cv.notify_all();
    if (workerThread.joinable()) workerThread.join();
    if (loaderThread.joinable()) loaderThread.join();
}

QString AiVocalEngine::modelKey() const
{
    std::lock_guard<std::mutex> lock(m);
    return wantedKey;
}

void AiVocalEngine::selectModel(const QString &key)
{
    {
        std::lock_guard<std::mutex> lock(m);
        if (key == wantedKey)
            return;
        wantedKey = key;
        if (key == loadedKey)
            return;
    }
    if (loaderThread.joinable())
        loaderThread.detach();          // detach background loader so GUI thread never hangs
    loaderThread = std::thread([this, key]() { loadModel(key); });
}

void AiVocalEngine::reloadModel()
{
    QString key;
    {
        std::lock_guard<std::mutex> lock(m);
        key = wantedKey;
        if (key.isEmpty())
            return;
        loadedKey.clear();
    }
    if (loaderThread.joinable())
        loaderThread.detach();
    loaderThread = std::thread([this, key]() { loadModel(key); });
}

void AiVocalEngine::loadModel(QString key)
{
    isLoading = true;
    const ModelInfo *mi = model(key);
    const QString path = dir + "/" + mi->file;
    try {
        if (!QFile::exists(path)) {
            { std::lock_guard<std::mutex> lock(m); loadMsg = QString::fromUtf8("กำลังดาวน์โหลดโมเดล AI..."); }
            QDir().mkpath(dir);
            const QString part = path + ".part";
            const std::wstring url = (QString(MODEL_URL) + mi->file).toStdWString();
            if (URLDownloadToFileW(nullptr, url.c_str(), part.toStdWString().c_str(), 0, nullptr) != S_OK
                    || !QFile::rename(part, path)) {
                QFile::remove(part);
                throw std::runtime_error("download failed");
            }
        }
        { std::lock_guard<std::mutex> lock(m); loadMsg = QString::fromUtf8("กำลังโหลดโมเดล AI..."); }

        // speed test: GPU (DirectML) and CPU, keep the faster (the GPU also draws the video)
        std::vector<float> test(size_t(2) * CHUNK), outv(size_t(2) * CHUNK);
        for (size_t i = 0; i < test.size(); i++)
            test[i] = 0.1f * std::sin(0.01f * float(i)) * std::cos(0.0003f * float(i));
        auto timeIt = [&](MdxModel &mm) {
            mm.instrumental(test.data(), outv.data());      // warm-up
            const double t0 = nowSec();
            mm.instrumental(test.data(), outv.data());
            return nowSec() - t0;
        };
        std::unique_ptr<MdxModel> best;
        double bestT = 1e9;
        QString bestProv;
        const int pref = s_devicePref.load();
        double gpuT = -1, cpuT = -1;
        if (pref != 1) {
            try {
                std::unique_ptr<MdxModel> g(new MdxModel(path.toStdWString(), *mi, true, 0));
                if (g->gpu()) {
                    bestT = gpuT = timeIt(*g);
                    bestProv = QString::fromUtf8("การ์ดจอ (DirectML)");
                    best = std::move(g);
                }
            } catch (...) {}
        }
        if (pref != 2 || !best) {
            const int threads = std::max(4, int(std::thread::hardware_concurrency()) / 2);
            std::unique_ptr<MdxModel> c(new MdxModel(path.toStdWString(), *mi, false, threads));
            const double tc = cpuT = timeIt(*c);
            if (pref == 1 || !best || tc * 1.8 <= 4.0 || tc < bestT * 0.8) {
                bestT = tc;
                bestProv = "CPU";
                best = std::move(c);
            }
        }
        aiLog(QString("model %1 speed test: GPU %2 s, CPU %3 s per window -> using %4 (preference %5)")
              .arg(key).arg(gpuT, 0, 'f', 2).arg(cpuT, 0, 'f', 2)
              .arg(bestProv == "CPU" ? "CPU" : "GPU").arg(pref == 1 ? "CPU" : pref == 2 ? "GPU" : "auto"));
        {
            std::lock_guard<std::mutex> lock(m);
            if (key != wantedKey) { isLoading = false; return; }   // another model was picked meanwhile
            sep = std::shared_ptr<MdxModel>(std::move(best));
            loadedKey = key;
            provider = bestProv;
            computeSec = bestT;
            // The hop must fit inside one model window (CHUNK minus both trimmed edges and the
            // cross-fade, ~5.76 s); 6 s overran it and crashed when the model ran slowly (CPU).
            hopSec = std::min(5.5, std::max(4.0, std::ceil(bestT * 2.5 / 0.5) * 0.5));
            cushion = std::max(1.2, std::round(bestT * 6.0) / 10.0);
            aiError = bestT > 4.5 ? QString::fromUtf8("เครื่องช้าเกินไปสำหรับรุ่นนี้ ลองรุ่นเบา หรือโหมดเร็ว") : QString();
            loadMsg.clear();
        }
        aiReady = true;
        reset();
    } catch (const std::exception &e) {
        std::lock_guard<std::mutex> lock(m);
        aiError = QString::fromUtf8("โหลดโมเดล AI ไม่สำเร็จ: ") + e.what();
    } catch (...) {
        std::lock_guard<std::mutex> lock(m);
        aiError = QString::fromUtf8("โหลดโมเดล AI ไม่สำเร็จ");
    }
    isLoading = false;
}

void AiVocalEngine::reset()
{
    std::lock_guard<std::mutex> lock(m);
    generation++;
    inQ.clear();
    inIdx.clear();
    fifo.clear();
    fifoFrames = 0;
    playing = false;
    windowReset = true;             // the window belongs to the worker thread: it clears it itself
    arrivals.clear();
    delay = -1;
}

void AiVocalEngine::push(const float *data, int frames)
{
    const double now = nowSec();
    std::lock_guard<std::mutex> lock(m);
    inQ.emplace_back(data, data + size_t(frames) * 2);
    inIdx.push_back(inCount);
    inCount += frames;
    arrivals.emplace_back(inCount, now);
    while (arrivals.size() > 4000)
        arrivals.pop_front();
    lastInput = now;
    cv.notify_one();
}

bool AiVocalEngine::receiving() const
{
    std::lock_guard<std::mutex> lock(m);
    return nowSec() - lastInput < 1.0;
}

double AiVocalEngine::arrivalTime(long long idx)
{
    if (arrivals.empty())
        return -1;
    for (const auto &a : arrivals)
        if (a.first > idx)
            return a.second - double(a.first - idx) / SR - 0.05;
    const auto &a = arrivals.back();
    return a.second - double(a.first - idx) / SR - 0.05;
}

void AiVocalEngine::worker()
{
    while (!quit) {
        std::vector<float> block;
        long long idx0 = 0;
        int gen = 0;
        std::shared_ptr<MdxModel> model;
        {
            std::unique_lock<std::mutex> lock(m);
            cv.wait_for(lock, std::chrono::milliseconds(200), [this]() { return quit || !inQ.empty(); });
            if (quit) return;
            if (inQ.empty()) continue;
            block = std::move(inQ.front());
            inQ.pop_front();
            idx0 = inIdx.front();
            inIdx.pop_front();
            gen = generation;
            model = aiReady ? sep : nullptr;
            if (!model) {
                // model still loading: pass the sound through unchanged (planar inst = mix, voc = 0)
                const int n = int(block.size() / 2);
                Block b;
                b.data.assign(size_t(4) * n, 0.0f);
                for (int i = 0; i < n; i++) {
                    b.data[i] = block[2 * i];
                    b.data[size_t(n) + i] = block[2 * i + 1];
                }
                b.idx = idx0;
                fifoFrames += n;
                fifo.push_back(std::move(b));
                while (fifoFrames > SR / 2) {       // keep it short (no AI delay yet)
                    fifoFrames -= static_cast<long long>(fifo.front().data.size() / 4);
                    fifo.pop_front();
                }
                continue;
            }
            if (static_cast<long long>(inQ.size()) * static_cast<long long>(block.size() / 2) > static_cast<long long>((hopSec + 6) * SR)) {
                // far behind after a long stall: start over
                generation++;
                inQ.clear(); inIdx.clear(); fifo.clear(); fifoFrames = 0; playing = false;
                windowReset = true;
                continue;
            }
            if (windowReset) {
                windowReset = false;
                std::fill(buf.begin(), buf.end(), 0.0f);
                pending = 0;
                tailInst.clear();
                hopSamples = std::min(int(hopSec * SR), CHUNK - 2 * TRIM - XF);
            }
        }
        // stream the block into the window (planar, newest at the end)
        const int n = int(block.size() / 2);
        int i = 0;
        total = idx0;
        while (i < n) {
            const int take = std::min(n - i, hopSamples - pending);
            for (int c = 0; c < 2; c++) {
                float *b = &buf[size_t(c) * CHUNK];
                std::memmove(b, b + take, size_t(CHUNK - take) * sizeof(float));
                for (int k = 0; k < take; k++)
                    b[CHUNK - take + k] = block[2 * size_t(i + k) + c];
            }
            pending += take;
            total += take;
            i += take;
            if (pending >= hopSamples) {
                pending = 0;
                if (gen == generation)
                    runWindow(*model);
            }
        }
    }
}

void AiVocalEngine::runWindow(MdxModel &model)
{
    const int hop = hopSamples;
    const int end = CHUNK - TRIM, start = end - hop;
    const long long idx = total - TRIM - hop;
    std::vector<float> out(size_t(4) * hop, 0.0f);

    float peak = 0;
    for (int c = 0; c < 2; c++)
        for (int k = start - XF; k < CHUNK; k++)
            peak = std::max(peak, std::fabs(buf[size_t(c) * CHUNK + k]));
    if (peak < 1e-4f) {                     // silence (video paused): skip the model
        tailInst.clear();
        emitBlock(std::move(out), hop, idx);
        return;
    }
    std::vector<float> inst(size_t(2) * CHUNK);
    const double t0 = nowSec();
    isComputing.store(true);
    model.instrumental(buf.data(), inst.data());
    isComputing.store(false);
    const double dt = nowSec() - t0;
    {
        std::lock_guard<std::mutex> lock(m);
        computeSec = 0.8 * computeSec + 0.2 * dt;
    }
    for (int c = 0; c < 2; c++) {
        const float *mixc = &buf[size_t(c) * CHUNK];
        const float *ic = &inst[size_t(c) * CHUNK];
        float *oi = &out[size_t(c) * hop];
        float *ov = &out[size_t(2 + c) * hop];
        for (int k = 0; k < hop; k++)
            oi[k] = ic[start + k];
        if (!tailInst.empty())
            for (int k = 0; k < XF && k < hop; k++) {
                const float r = float(k) / XF;
                oi[k] = tailInst[size_t(c) * XF + k] * (1 - r) + oi[k] * r;
            }
        for (int k = 0; k < hop; k++)
            ov[k] = mixc[start + k] - oi[k];
    }
    tailInst.assign(size_t(2) * XF, 0.0f);
    for (int c = 0; c < 2; c++)
        for (int k = 0; k < XF && end + k < CHUNK; k++)
            tailInst[size_t(c) * XF + k] = inst[size_t(c) * CHUNK + end + k];
    emitBlock(std::move(out), hop, idx);
}

void AiVocalEngine::emitBlock(std::vector<float> &&planar4, int n, long long idx)
{
    std::lock_guard<std::mutex> lock(m);
    if (!playing && fifo.empty()) {         // cushion against compute-time spikes
        Block z;
        z.data.assign(size_t(4) * int(cushion * SR), 0.0f);
        z.idx = -1;
        fifoFrames += int(cushion * SR);
        fifo.push_back(std::move(z));
    }
    Block b;
    b.data = std::move(planar4);
    b.idx = idx;
    fifoFrames += n;
    fifo.push_back(std::move(b));
    if (!playing && fifoFrames >= 1)
        playing = true;
}

void AiVocalEngine::pull(float *out, int frames)
{
    std::lock_guard<std::mutex> lock(m);
    const bool ai = aiReady && sep;
    if (!playing) {
        if (fifoFrames >= std::max(frames, ai ? 1 : SR / 10))
            playing = true;
        else {
            std::fill(out, out + size_t(frames) * 2, 0.0f);
            return;
        }
    }
    const double limit = ai ? hopSec + cushion + 2.0 : 0.5;
    if (fifoFrames > limit * SR) {          // clocks drifted apart: keep the delay bounded
        long long drop = fifoFrames - static_cast<long long>((limit - 0.7) * SR);
        while (drop > 0 && !fifo.empty()) {
            Block &f = fifo.front();
            const long long have = static_cast<long long>(f.data.size() / 4);
            if (have <= drop) { drop -= have; fifoFrames -= have; fifo.pop_front(); }
            else {
                const int k = int(drop), rest = int(have - k);
                std::vector<float> d(size_t(4) * rest);
                for (int c = 0; c < 4; c++)
                    std::copy(f.data.begin() + size_t(c) * have + k, f.data.begin() + size_t(c) * have + have, d.begin() + size_t(c) * rest);
                f.data.swap(d);
                if (f.idx >= 0) f.idx += k;
                fifoFrames -= k;
                drop = 0;
            }
        }
    }
    const float gi = gInst, gv = gVoc;
    int got = 0;
    long long firstIdx = -1;
    int firstOff = 0;
    while (got < frames && !fifo.empty()) {
        Block &f = fifo.front();
        const int have = int(f.data.size() / 4);
        const int take = std::min(frames - got, have);
        if (firstIdx < 0 && f.idx >= 0) { firstIdx = f.idx; firstOff = got; }
        for (int k = 0; k < take; k++) {
            const float l = f.data[k] * gi + f.data[size_t(2) * have + k] * gv;
            const float r = f.data[size_t(have) + k] * gi + f.data[size_t(3) * have + k] * gv;
            out[2 * size_t(got + k)] = std::max(-1.0f, std::min(1.0f, l));
            out[2 * size_t(got + k) + 1] = std::max(-1.0f, std::min(1.0f, r));
        }
        got += take;
        fifoFrames -= take;
        if (take == have)
            fifo.pop_front();
        else {
            const int rest = have - take;
            std::vector<float> d(size_t(4) * rest);
            for (int c = 0; c < 4; c++)
                std::copy(f.data.begin() + size_t(c) * have + take, f.data.begin() + size_t(c) * have + have, d.begin() + size_t(c) * rest);
            f.data.swap(d);
            if (f.idx >= 0) f.idx += take;
        }
    }
    if (firstIdx >= 0) {
        const double at = arrivalTime(firstIdx);
        if (at > 0) {
            const double d = nowSec() + double(firstOff) / SR + 0.12 - at;    // + output buffer
            if (d > 0 && d < 20)
                delay = (delay < 0 || std::fabs(d - delay) > 0.3) ? d : 0.95 * delay + 0.05 * d;
        }
    }
    if (got < frames) {
        std::fill(out + size_t(got) * 2, out + size_t(frames) * 2, 0.0f);
        playing = false;                     // ran dry: buffer again, and a bigger safety margin
        if (ai)
            cushion = std::min(3.0, cushion + 0.4);
    }
}

double AiVocalEngine::delaySeconds() const
{
    std::lock_guard<std::mutex> lock(m);
    if (!aiReady || !sep)
        return 0;
    if (delay > 0 && playing)
        return delay;
    return hopSec + double(TRIM) / SR + computeSec + cushion + 0.2;
}

QString AiVocalEngine::statusText() const
{
    std::lock_guard<std::mutex> lock(m);
    if (!aiError.isEmpty())
        return aiError;
    if (isLoading)
        return loadMsg.isEmpty() ? QString::fromUtf8("กำลังโหลดโมเดล AI...") : loadMsg;
    if (!aiReady)
        return QString::fromUtf8("ยังไม่ได้โหลดโมเดล");
    return QString::fromUtf8("พร้อม: %1, %2 วินาทีต่อช่วง").arg(provider).arg(computeSec, 0, 'f', 2);
}
