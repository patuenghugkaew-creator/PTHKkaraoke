// Per-process audio capture (Windows 10 2004+ "process loopback"): captures only the sound of one program
// (and its child processes), without a virtual cable and without routing the program in Windows' Volume mixer.
// Optionally mutes that program's sound at the speakers, so the sound is heard only through KaraMixer.
#ifndef PROCESSLOOPBACK_H
#define PROCESSLOOPBACK_H

#ifdef _WIN32
#if defined(__has_include)
#  if __has_include(<audioclientactivationparams.h>)
#    define KM_HAVE_PROCLOOP 1
#  endif
#endif
#endif

#ifdef KM_HAVE_PROCLOOP

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <audioclientactivationparams.h>
#include <tlhelp32.h>
#include <atomic>
#include <functional>
#include <set>
#include <vector>
#include <thread>
#include <QString>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

#pragma comment(lib, "mmdevapi.lib")
#pragma comment(lib, "ole32.lib")

class ProcessLoopback
{
public:
    // data = interleaved float stereo at sampleRate (48 kHz unless set); count = number of floats
    using Sink = std::function<void(const float *data, int count)>;

    ~ProcessLoopback() { stop(); }

    // 0 = capture is working and the program is not muted, 1 = program muted (and still captured),
    // 2 = muting stopped the capture, so the program was un-muted again (its sound plays at the speakers too)
    int muteState() const { return muted.load(); }
    bool running() const { return th.joinable(); }
    void setSampleRate(int hz) { sampleRate = hz; }
    QString lastError() const { return err; }

    static bool alive(DWORD pid)
    {
        if (!pid) return false;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return false;
        DWORD code = 0;
        const bool ok = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
        CloseHandle(h);
        return ok;
    }

    bool start(DWORD pid, Sink sink, bool muteAtSpeakers)
    {
        stop();
        err.clear();
        if (!alive(pid)) { err = QStringLiteral("process not running"); return false; }
        stopFlag = false;
        started = false;
        failed = false;
        muted = 0;
        th = std::thread([=]() { run(pid, sink, muteAtSpeakers); });
        for (int i = 0; i < 100 && !started && !failed; i++)
            Sleep(50);
        if (failed || !started) {
            stop();
            if (err.isEmpty()) err = QStringLiteral("capture did not start");
            return false;
        }
        return true;
    }

    void stop()
    {
        stopFlag = true;
        if (th.joinable()) th.join();
    }

    // volume (0..1) of the program's audio sessions on the default speakers
    static void setSessionVolume(DWORD root, float vol)
    {
        forEachSession(root, [vol](ISimpleAudioVolume *v) { v->SetMasterVolume(vol, nullptr); });
    }

    // A child process (not root itself) that has an audio session on the default speakers, e.g. the
    // audio service of the embedded browser; active sessions first. 0 = none.
    static DWORD findAudioChild(DWORD root)
    {
        std::set<DWORD> pids = tree(root);
        pids.erase(root);
        DWORD found = 0;
        bool foundActive = false;
        IMMDeviceEnumerator *en = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void **>(&en))) || !en)
            return 0;
        IMMDevice *dev = nullptr;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev)) && dev) {
            IAudioSessionManager2 *mgr = nullptr;
            if (SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                        reinterpret_cast<void **>(&mgr))) && mgr) {
                IAudioSessionEnumerator *se = nullptr;
                if (SUCCEEDED(mgr->GetSessionEnumerator(&se)) && se) {
                    int n = 0;
                    se->GetCount(&n);
                    for (int i = 0; i < n && !foundActive; i++) {
                        IAudioSessionControl *ctl = nullptr;
                        if (FAILED(se->GetSession(i, &ctl)) || !ctl) continue;
                        IAudioSessionControl2 *c2 = nullptr;
                        if (SUCCEEDED(ctl->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void **>(&c2))) && c2) {
                            DWORD sp = 0;
                            AudioSessionState st = AudioSessionStateInactive;
                            if (SUCCEEDED(c2->GetProcessId(&sp)) && pids.count(sp)) {
                                ctl->GetState(&st);
                                found = sp;
                                foundActive = (st == AudioSessionStateActive);
                            }
                            c2->Release();
                        }
                        ctl->Release();
                    }
                    se->Release();
                }
                mgr->Release();
            }
            dev->Release();
        }
        en->Release();
        return found;
    }

private:
    class Handler : public IActivateAudioInterfaceCompletionHandler, public IAgileObject
    {
    public:
        Handler() { ev = CreateEventW(nullptr, FALSE, FALSE, nullptr); }
        ~Handler() { if (ev) CloseHandle(ev); }
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void **o) override
        {
            if (r == __uuidof(IUnknown) || r == __uuidof(IActivateAudioInterfaceCompletionHandler))
                *o = static_cast<IActivateAudioInterfaceCompletionHandler *>(this);
            else if (r == __uuidof(IAgileObject))
                *o = static_cast<IAgileObject *>(this);
            else { *o = nullptr; return E_NOINTERFACE; }
            AddRef();
            return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++ref; }
        ULONG STDMETHODCALLTYPE Release() override { const long n = --ref; if (!n) delete this; return n; }
        HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation *op) override
        {
            HRESULT hra = E_FAIL;
            IUnknown *u = nullptr;
            if (SUCCEEDED(op->GetActivateResult(&hra, &u)) && SUCCEEDED(hra) && u) {
                u->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void **>(&client));
                u->Release();
            }
            hr = hra;
            SetEvent(ev);
            return S_OK;
        }
        std::atomic<long> ref{1};
        HANDLE ev = nullptr;
        HRESULT hr = E_FAIL;
        IAudioClient *client = nullptr;
    };

    static void log(const QString &s)
    {
        QDir().mkpath(QDir::homePath() + QStringLiteral("/.PTHKKaraoke"));
        QFile f(QDir::homePath() + QStringLiteral("/.PTHKKaraoke/procloop.log"));
        if (f.size() > 200000) f.remove();
        if (!f.open(QIODevice::Append | QIODevice::Text)) return;
        QTextStream t(&f);
        t << QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz ")) << s << "\n";
    }

    static std::set<DWORD> tree(DWORD root)
    {
        std::set<DWORD> out;
        out.insert(root);
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return out;
        std::vector<std::pair<DWORD, DWORD>> pp;       // pid, parent
        PROCESSENTRY32W e;
        e.dwSize = sizeof(e);
        if (Process32FirstW(snap, &e))
            do { pp.push_back({ e.th32ProcessID, e.th32ParentProcessID }); } while (Process32NextW(snap, &e));
        CloseHandle(snap);
        for (bool grew = true; grew;) {
            grew = false;
            for (const auto &p : pp)
                if (!out.count(p.first) && out.count(p.second)) { out.insert(p.first); grew = true; }
        }
        return out;
    }

    static void setMute(DWORD root, bool mute)
    {
        forEachSession(root, [mute](ISimpleAudioVolume *v) { v->SetMute(mute ? TRUE : FALSE, nullptr); });
    }

    static void forEachSession(DWORD root, const std::function<void(ISimpleAudioVolume *)> &fn)
    {
        const std::set<DWORD> pids = tree(root);
        IMMDeviceEnumerator *en = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void **>(&en))) || !en)
            return;
        IMMDevice *dev = nullptr;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev)) && dev) {
            IAudioSessionManager2 *mgr = nullptr;
            if (SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                        reinterpret_cast<void **>(&mgr))) && mgr) {
                IAudioSessionEnumerator *se = nullptr;
                if (SUCCEEDED(mgr->GetSessionEnumerator(&se)) && se) {
                    int n = 0;
                    se->GetCount(&n);
                    for (int i = 0; i < n; i++) {
                        IAudioSessionControl *ctl = nullptr;
                        if (FAILED(se->GetSession(i, &ctl)) || !ctl) continue;
                        IAudioSessionControl2 *c2 = nullptr;
                        if (SUCCEEDED(ctl->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void **>(&c2))) && c2) {
                            DWORD sp = 0;
                            if (SUCCEEDED(c2->GetProcessId(&sp)) && pids.count(sp)) {
                                ISimpleAudioVolume *v = nullptr;
                                if (SUCCEEDED(ctl->QueryInterface(__uuidof(ISimpleAudioVolume), reinterpret_cast<void **>(&v))) && v) {
                                    fn(v);
                                    v->Release();
                                }
                            }
                            c2->Release();
                        }
                        ctl->Release();
                    }
                    se->Release();
                }
                mgr->Release();
            }
            dev->Release();
        }
        en->Release();
    }

    void run(DWORD pid, Sink sink, bool muteWanted)
    {
        const HRESULT hrc = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IAudioClient *client = nullptr;
        IAudioCaptureClient *cap = nullptr;
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        bool didMute = false;

        do {
            AUDIOCLIENT_ACTIVATION_PARAMS p = {};
            p.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
            p.ProcessLoopbackParams.TargetProcessId = pid;
            p.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
            PROPVARIANT pv = {};
            pv.vt = VT_BLOB;
            pv.blob.cbSize = sizeof(p);
            pv.blob.pBlobData = reinterpret_cast<BYTE *>(&p);

            Handler *h = new Handler();
            IActivateAudioInterfaceAsyncOperation *op = nullptr;
            HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
                                                     &pv, h, &op);
            if (SUCCEEDED(hr))
                WaitForSingleObject(h->ev, 5000);
            client = h->client;
            if (client) client->AddRef();
            const HRESULT hra = SUCCEEDED(hr) ? h->hr : hr;
            if (op) op->Release();
            h->Release();
            if (!client) {
                err = QStringLiteral("ActivateAudioInterfaceAsync 0x%1").arg(quint32(hra), 8, 16, QLatin1Char('0'));
                log(err);
                break;
            }

            WAVEFORMATEX wf = {};
            wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
            wf.nChannels = 2;
            wf.nSamplesPerSec = DWORD(sampleRate);
            wf.wBitsPerSample = 32;
            wf.nBlockAlign = 8;
            wf.nAvgBytesPerSec = DWORD(sampleRate) * 8;
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                    200000, 0, &wf, nullptr);
            if (FAILED(hr)) { err = QStringLiteral("Initialize 0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0')); log(err); break; }
            hr = client->SetEventHandle(ev);
            if (FAILED(hr)) { err = QStringLiteral("SetEventHandle 0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0')); log(err); break; }
            hr = client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void **>(&cap));
            if (FAILED(hr) || !cap) { err = QStringLiteral("GetService 0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0')); log(err); break; }
            hr = client->Start();
            if (FAILED(hr)) { err = QStringLiteral("Start 0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0')); log(err); break; }

            log(QStringLiteral("capture started, pid %1, mute=%2").arg(pid).arg(muteWanted));
            started = true;

            float recent = 0.0f;                 // loudest sample since the last second tick
            float beforeMute = 0.0f;             // loudest sample in the second before muting
            float afterMute = 0.0f;
            DWORD tick = GetTickCount(), muteAt = 0;
            bool judged = false;

            while (!stopFlag) {
                WaitForSingleObject(ev, 100);
                UINT32 n = 0;
                while (!stopFlag && SUCCEEDED(cap->GetNextPacketSize(&n)) && n > 0) {
                    BYTE *buf = nullptr;
                    DWORD flags = 0;
                    if (FAILED(cap->GetBuffer(&buf, &n, &flags, nullptr, nullptr))) break;
                    const int cnt = int(n) * 2;
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        static thread_local std::vector<float> z;
                        z.assign(size_t(cnt), 0.0f);
                        sink(z.data(), cnt);
                    } else {
                        const float *f = reinterpret_cast<const float *>(buf);
                        float m = 0.0f;
                        for (int i = 0; i < cnt; i++) { const float a = f[i] < 0 ? -f[i] : f[i]; if (a > m) m = a; }
                        if (m > recent) recent = m;
                        sink(f, cnt);
                    }
                    cap->ReleaseBuffer(n);
                }

                const DWORD now = GetTickCount();
                if (now - tick >= 1000) {
                    tick = now;
                    if (!didMute) {
                        if (muteWanted && muted.load() != 2 && recent > 0.002f) {   // sound is arriving: now mute it at the speakers
                            beforeMute = recent;
                            setMute(pid, true);
                            didMute = true;
                            muted = 1;
                            muteAt = now;
                            afterMute = 0.0f;
                            judged = false;
                            log(QStringLiteral("muted program at speakers (peak before %1)").arg(double(beforeMute)));
                        }
                    } else {
                        setMute(pid, true);           // new audio sessions of the program (e.g. next video) get muted too
                        if (now - muteAt > 400 && recent > afterMute) afterMute = recent;
                        if (!judged && now - muteAt >= 2500) {
                            judged = true;
                            log(QStringLiteral("peak after mute %1").arg(double(afterMute)));
                            if (afterMute < 0.0005f && beforeMute > 0.02f) {
                                // muting silences the capture on this Windows: undo it
                                setMute(pid, false);
                                didMute = false;
                                muted = 2;
                                log(QStringLiteral("capture went silent when muted -> un-muted again"));
                            }
                        }
                    }
                    recent = 0.0f;
                }
                if (!alive(pid)) { log(QStringLiteral("program ended")); break; }
            }
        } while (false);

        if (cap) cap->Release();
        if (client) { client->Stop(); client->Release(); }
        if (didMute || muted.load() == 1) setMute(pid, false);
        if (muted.load() == 1) muted = 0;
        if (ev) CloseHandle(ev);
        if (!started) failed = true;
        if (SUCCEEDED(hrc)) CoUninitialize();
        log(QStringLiteral("capture stopped"));
    }

    std::thread th;
    std::atomic<bool> stopFlag{false}, started{false}, failed{false};
    std::atomic<int> muted{0};
    int sampleRate = 48000;
    QString err;
};

#endif // KM_HAVE_PROCLOOP
#endif // PROCESSLOOPBACK_H
