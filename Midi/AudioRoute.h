// Sends the sound of a program (Edge / Chrome) to a chosen Windows output device (e.g. "CABLE Input" of VB-Cable)
// - the same thing as Settings > System > Sound > Volume mixer > the program's "Output" - without opening Settings.
// Uses only classic Core Audio (Windows Vista and newer): no WinRT, so it also starts on Windows 7.
#ifndef AUDIOROUTE_H
#define AUDIOROUTE_H

#ifdef _WIN32

#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <tlhelp32.h>
#include <set>
#include <vector>
#include <string>
#include <QString>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

#pragma comment(lib, "ole32.lib")

// Windows' "set default device" interface (used by most sound-switching tools; stable across Windows versions)
MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8")
IKmPolicyConfig : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat() = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR deviceId, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR deviceId, INT visible) = 0;
};

class AudioRoute
{
public:
    // endpoint id of the current Windows default playback device (empty = unknown)
    static QString defaultPlaybackId()
    {
        QString result;
        IMMDeviceEnumerator *en = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void **>(&en))) || !en)
            return result;
        IMMDevice *d = nullptr;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &d)) && d) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(d->GetId(&id)) && id) {
                result = QString::fromWCharArray(id);
                CoTaskMemFree(id);
            }
            d->Release();
        }
        en->Release();
        return result;
    }

    // does the endpoint exist and is it active?
    static bool playbackDeviceExists(const QString &id)
    {
        if (id.isEmpty()) return false;
        bool ok = false;
        IMMDeviceEnumerator *en = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void **>(&en))) || !en)
            return false;
        IMMDevice *d = nullptr;
        const std::wstring w = id.toStdWString();
        if (SUCCEEDED(en->GetDevice(w.c_str(), &d)) && d) {
            DWORD st = 0;
            ok = SUCCEEDED(d->GetState(&st)) && st == DEVICE_STATE_ACTIVE;
            d->Release();
        }
        en->Release();
        return ok;
    }

    // makes a playback device the Windows default (all roles); returns true when Windows reports it as the default
    static bool setDefaultPlayback(const QString &id, QString *error = nullptr)
    {
        static const CLSID kClient = { 0x870af99c, 0x171d, 0x4f9e, { 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9 } };
        IKmPolicyConfig *pc = nullptr;
        const HRESULT hr = CoCreateInstance(kClient, nullptr, CLSCTX_ALL, __uuidof(IKmPolicyConfig), reinterpret_cast<void **>(&pc));
        if (FAILED(hr) || !pc) {
            if (error) *error = QStringLiteral("policy interface 0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
            log(QStringLiteral("setDefault: no policy interface 0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0')));
            return false;
        }
        const std::wstring w = id.toStdWString();
        HRESULT last = S_OK;
        for (int role = 0; role < 3; role++) {
            const HRESULT h = pc->SetDefaultEndpoint(w.c_str(), ERole(role));
            if (FAILED(h)) last = h;
        }
        pc->Release();
        const bool ok = (defaultPlaybackId() == id);
        log(QStringLiteral("setDefault %1 -> %2 (last hr 0x%3)").arg(id).arg(ok ? QStringLiteral("ok") : QStringLiteral("NOT default"))
                .arg(quint32(last), 8, 16, QLatin1Char('0')));
        if (!ok && error) *error = QStringLiteral("Windows did not change the default device");
        return ok;
    }

    static void log(const QString &s)
    {
        QDir().mkpath(QDir::homePath() + QStringLiteral("/.PTHKKaraoke"));
        QFile f(QDir::homePath() + QStringLiteral("/.PTHKKaraoke/route.log"));
        if (f.size() > 200000) f.remove();
        if (!f.open(QIODevice::Append | QIODevice::Text)) return;
        QTextStream t(&f);
        t << QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz ")) << s << "\n";
    }

    // endpoint id of the first active playback device whose name contains `part` (e.g. "CABLE Input"); empty = none
    static QString findPlaybackDevice(const QString &part, QString *friendly = nullptr)
    {
        QString result;
        IMMDeviceEnumerator *en = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void **>(&en))) || !en)
            return result;
        IMMDeviceCollection *col = nullptr;
        if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) && col) {
            UINT n = 0;
            col->GetCount(&n);
            for (UINT i = 0; i < n && result.isEmpty(); i++) {
                IMMDevice *d = nullptr;
                if (FAILED(col->Item(i, &d)) || !d) continue;
                IPropertyStore *ps = nullptr;
                if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps)) && ps) {
                    PROPVARIANT pv;
                    PropVariantInit(&pv);
                    if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) {
                        const QString name = QString::fromWCharArray(pv.pwszVal);
                        if (name.contains(part, Qt::CaseInsensitive)) {
                            LPWSTR id = nullptr;
                            if (SUCCEEDED(d->GetId(&id)) && id) {
                                result = QString::fromWCharArray(id);
                                CoTaskMemFree(id);
                                if (friendly) *friendly = name;
                            }
                        }
                    }
                    PropVariantClear(&pv);
                    ps->Release();
                }
                d->Release();
            }
            col->Release();
        }
        en->Release();
        return result;
    }

    static std::set<DWORD> tree(DWORD root)
    {
        std::set<DWORD> out;
        out.insert(root);
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return out;
        std::vector<std::pair<DWORD, DWORD>> pp;
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

    // process id of the browser that owns the YouTube app window (title has no "Microsoft Edge" / "Google Chrome"
    // suffix, so an everyday browser tab is not taken); 0 = not found
    static DWORD findYouTubeWindowPid()
    {
        struct Ctx { DWORD pid = 0; } ctx;
        EnumWindows([](HWND h, LPARAM lp) -> BOOL {
            if (!IsWindowVisible(h)) return TRUE;
            wchar_t cls[64] = {};
            GetClassNameW(h, cls, 63);
            if (wcscmp(cls, L"Chrome_WidgetWin_1") != 0) return TRUE;
            wchar_t title[512] = {};
            GetWindowTextW(h, title, 511);
            const QString t = QString::fromWCharArray(title);
            if (!t.contains(QStringLiteral("YouTube"), Qt::CaseInsensitive)) return TRUE;
            if (t.contains(QStringLiteral("Microsoft")) || t.contains(QStringLiteral("Google Chrome"))) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (!pid) return TRUE;
            reinterpret_cast<Ctx *>(lp)->pid = pid;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&ctx));
        return ctx.pid;
    }
};

#endif // _WIN32
#endif // AUDIOROUTE_H
