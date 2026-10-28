// AI vocal / instrument separation for the YouTube sound (UVR MDX-Net ONNX models, onnxruntime +
// DirectML, CPU fallback). C++ port of the myKaraoke engine (separator.py / engine.py):
// the model works on ~5.9 s windows; every "hop" seconds the newest window is separated and the
// next hop of sound comes out, so the sound is a few seconds late and the picture is delayed to match.
#ifndef AIVOCAL_H
#define AIVOCAL_H

#include <QString>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "../PTHKCore/pthkcore_global.h"

class MdxModel;

class PTHKCORE_EXPORT AiVocalEngine
{
public:
    static const int SR = 44100;

    struct ModelInfo {
        const char *key;            // "light", "hq", "kara"
        const char *file;
        int nfft;
        int dimf;
        float compensate;
        bool primaryVocals;         // the model gives the vocals (instrumental = mix - vocals)
        const char *nameTh;
    };
    static const ModelInfo *models(int *count);
    static const ModelInfo *model(const QString &key);

    // one window (planar 2 x 261120 samples) through a model file, for testing; false on error
    static bool separateOnce(const QString &modelFile, const QString &key, bool gpu, const float *mix, float *inst);

    explicit AiVocalEngine(const QString &modelsDir);
    ~AiVocalEngine();

    void selectModel(const QString &key);      // loads (and downloads) in the background
    QString modelKey() const;

    // capture thread: interleaved stereo at 44.1 kHz
    void push(const float *data, int frames);
    // BASS thread: interleaved stereo at 44.1 kHz, inst * gInst + vocals * gVoc; always fills
    void pull(float *out, int frames);
    void setGains(float vocals, float music) { gVoc = vocals; gInst = music; }
    void reset();                               // a new song / a jump: drop what is queued

    bool ready() const { return aiReady; }
    bool loading() const { return isLoading; }
    bool isPlaying() const { std::lock_guard<std::mutex> lock(m); return playing; }
    QString statusText() const;                 // for the settings dialog
    double delaySeconds() const;                // picture delay needed (0 = not running)
    bool receiving() const;

private:
    struct Block { std::vector<float> data; long long idx; };   // 4 x n: inst L/R, voc L/R (planar)

    void loadModel(QString key);
    void worker();
    void runWindow(MdxModel &model);
    void emitBlock(std::vector<float> &&planar4, int n, long long idx);
    double arrivalTime(long long idx);

    QString dir;
    mutable std::mutex m;
    QString wantedKey, loadedKey;
    std::shared_ptr<MdxModel> sep;
    std::atomic<bool> aiReady{false}, isLoading{false};
    QString loadMsg, aiError, provider;
    double computeSec = 0, hopSec = 4.0, cushion = 1.2;

    // input
    std::condition_variable cv;
    std::deque<std::vector<float>> inQ;          // interleaved blocks
    std::deque<long long> inIdx;
    long long inCount = 0;
    std::deque<std::pair<long long, double>> arrivals;   // (end index, time)
    double lastInput = 0;

    // streaming window (planar, CHUNK per channel)
    std::vector<float> buf;
    int pending = 0;
    long long total = 0;
    std::vector<float> tailInst;                 // 2 x XF
    int hopSamples = 0;
    bool windowReset = true;                     // the worker clears its window before the next block

    // output FIFO
    std::deque<Block> fifo;
    long long fifoFrames = 0;
    bool playing = false;
    double delay = -1;
    std::atomic<float> gVoc{0.0f}, gInst{1.0f};
    std::atomic<int> generation{0};

    std::thread workerThread, loaderThread;
    std::atomic<bool> quit{false};
};

#endif // AIVOCAL_H
