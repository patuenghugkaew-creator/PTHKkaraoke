#include "Vst3/Vst3Host.h"

#include "pluginterfaces/base/ftypes.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <cstring>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <memory>

#ifdef _WIN32
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace vst3host {

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------

// diagnostics: ~/.PTHKKaraoke/vst3.log (same file as the load failures)
static void vlog(const char *fmt, ...)
{
#ifdef _WIN32
    const char *home = std::getenv("USERPROFILE");
    if (!home) return;
    std::string f = std::string(home) + "\\.PTHKKaraoke\\vst3.log";
    FILE *fp = std::fopen(f.c_str(), "ab");
    if (!fp) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(fp, "%04d-%02d-%02d %02d:%02d:%02d  [host] ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(fp, fmt, ap);
    va_end(ap);
    std::fprintf(fp, "\r\n");
    std::fclose(fp);
#else
    (void)fmt;
#endif
}

static uint32_t hashUid(const char *tuid16)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 16; i++) {
        h ^= (uint8_t)tuid16[i];
        h *= 16777619u;
    }
    return h ? h : 1u;
}

static std::string u16ToUtf8(const char16 *s, int maxLen)
{
    std::string out;
    for (int i = 0; i < maxLen && s[i]; i++) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < maxLen && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        }
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
        else { out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
    }
    return out;
}

static bool endsWithNoCase(const std::string &s, const char *suffix)
{
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++) {
        char a = s[s.size() - n + i], b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

void splitPath(const std::string &full, std::string &path, int &classIndex)
{
    path = full;
    classIndex = -1;
    size_t bar = full.rfind('|');
    if (bar != std::string::npos && bar + 1 < full.size()) {
        bool digits = true;
        for (size_t i = bar + 1; i < full.size(); i++)
            if (full[i] < '0' || full[i] > '9') { digits = false; break; }
        if (digits) {
            path = full.substr(0, bar);
            classIndex = std::atoi(full.c_str() + bar + 1);
        }
    }
}

bool isVst3Path(const std::string &full)
{
    std::string p; int idx;
    splitPath(full, p, idx);
    return endsWithNoCase(p, ".vst3");
}

// ---------------------------------------------------------------------------
// Module (shared library) with ref counting
// ---------------------------------------------------------------------------

struct Module
{
    std::string path;
    void       *lib = nullptr;
    int         refs = 0;
    IPluginFactory *factory = nullptr;
    bool        (*initFn)() = nullptr;
    void        (*exitFn)() = nullptr;
    bool        (*entryFn)(void *) = nullptr;     // linux ModuleEntry
};

static std::mutex gModMutex;
static std::map<std::string, Module *> gModules;

#ifdef _WIN32
static std::wstring utf8ToWide(const std::string &s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 1), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}
#endif

static Module *acquireModule(const std::string &path, std::string *err)
{
    std::lock_guard<std::mutex> lock(gModMutex);

    auto it = gModules.find(path);
    if (it != gModules.end()) {
        it->second->refs++;
        return it->second;
    }

    Module *m = new Module();
    m->path = path;

#ifdef _WIN32
    std::wstring wpath = utf8ToWide(path);
    for (wchar_t &ch : wpath)       // LOAD_WITH_ALTERED_SEARCH_PATH wants backslashes (the path comes from Qt with '/')
        if (ch == L'/') ch = L'\\';
    {
        // Folder style module: Foo.vst3/Contents/<arch>-win/Foo.vst3
        DWORD attr = GetFileAttributesW(wpath.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            size_t sl = wpath.find_last_of(L"/\\");
            std::wstring leaf = (sl == std::wstring::npos) ? wpath : wpath.substr(sl + 1);
#ifdef _WIN64
            wpath += L"\\Contents\\x86_64-win\\" + leaf;
#else
            wpath += L"\\Contents\\x86-win\\" + leaf;
#endif
        }
    }
    m->lib = (void *)LoadLibraryExW(wpath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m->lib) {
        if (err) *err = "LoadLibrary failed";
        delete m;
        return nullptr;
    }
    m->initFn = (bool (*)())GetProcAddress((HMODULE)m->lib, "InitDll");
    m->exitFn = (void (*)())GetProcAddress((HMODULE)m->lib, "ExitDll");
    auto getFactory = (IPluginFactory * (PLUGIN_API *)())GetProcAddress((HMODULE)m->lib, "GetPluginFactory");
#else
    m->lib = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!m->lib) {
        if (err) *err = dlerror() ? dlerror() : "dlopen failed";
        delete m;
        return nullptr;
    }
    m->entryFn = (bool (*)(void *))dlsym(m->lib, "ModuleEntry");
    m->exitFn = (void (*)())dlsym(m->lib, "ModuleExit");
    auto getFactory = (IPluginFactory * (PLUGIN_API *)())dlsym(m->lib, "GetPluginFactory");
#endif

    if (!getFactory) {
        if (err) *err = "GetPluginFactory not found";
#ifdef _WIN32
        FreeLibrary((HMODULE)m->lib);
#else
        dlclose(m->lib);
#endif
        delete m;
        return nullptr;
    }

#ifdef _WIN32
    if (m->initFn && !m->initFn()) {
        if (err) *err = "InitDll failed";
        FreeLibrary((HMODULE)m->lib);
        delete m;
        return nullptr;
    }
#else
    if (m->entryFn && !m->entryFn(m->lib)) {
        if (err) *err = "ModuleEntry failed";
        dlclose(m->lib);
        delete m;
        return nullptr;
    }
#endif

    m->factory = getFactory();
    if (!m->factory) {
        if (err) *err = "no plug-in factory";
        if (m->exitFn) m->exitFn();
#ifdef _WIN32
        FreeLibrary((HMODULE)m->lib);
#else
        dlclose(m->lib);
#endif
        delete m;
        return nullptr;
    }

    m->refs = 1;
    gModules[path] = m;
    return m;
}

static void releaseModule(Module *m)
{
    std::lock_guard<std::mutex> lock(gModMutex);
    if (--m->refs > 0)
        return;
    // Keep the library loaded: unloading plug-ins is a common crash source and
    // reloading the same file later (e.g. the user re-adds it) stays cheap.
    m->refs = 0;
}

// ---------------------------------------------------------------------------
// COM-like helper objects for the host side
// ---------------------------------------------------------------------------

class HostApp : public IHostApplication
{
public:
    tresult PLUGIN_API getName(String128 name) override
    {
        static const char *n = "PTHK Karaoke";
        int i = 0;
        for (; n[i] && i < 127; i++) name[i] = (char16)n[i];
        name[i] = 0;
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(TUID, TUID, void **obj) override
    {
        *obj = nullptr;
        return kResultFalse;
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IHostApplication)
        QUERY_INTERFACE(iid, obj, IHostApplication::iid, IHostApplication)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1000; }      // static lifetime
    uint32 PLUGIN_API release() override { return 1000; }
};

static HostApp gHostApp;

class MemStream : public IBStream
{
public:
    MemStream() {}
    MemStream(const char *d, size_t n) : data(d, d + n) {}

    tresult PLUGIN_API read(void *buffer, int32 numBytes, int32 *numBytesRead) override
    {
        int64 avail = (int64)data.size() - pos;
        int32 n = (int32)std::max<int64>(0, std::min<int64>(numBytes, avail));
        if (n > 0) std::memcpy(buffer, data.data() + pos, (size_t)n);
        pos += n;
        if (numBytesRead) *numBytesRead = n;
        return kResultOk;
    }
    tresult PLUGIN_API write(void *buffer, int32 numBytes, int32 *numBytesWritten) override
    {
        if (numBytes < 0) return kInvalidArgument;
        if ((size_t)(pos + numBytes) > data.size())
            data.resize((size_t)(pos + numBytes));
        if (numBytes > 0) std::memcpy(data.data() + pos, buffer, (size_t)numBytes);
        pos += numBytes;
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kResultOk;
    }
    tresult PLUGIN_API seek(int64 p, int32 mode, int64 *result) override
    {
        int64 np = pos;
        if (mode == kIBSeekSet) np = p;
        else if (mode == kIBSeekCur) np = pos + p;
        else if (mode == kIBSeekEnd) np = (int64)data.size() + p;
        else return kInvalidArgument;
        if (np < 0) return kResultFalse;
        pos = np;
        if (result) *result = pos;
        return kResultOk;
    }
    tresult PLUGIN_API tell(int64 *p) override
    {
        if (p) *p = pos;
        return kResultOk;
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IBStream)
        QUERY_INTERFACE(iid, obj, IBStream::iid, IBStream)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1000; }      // owned on the stack
    uint32 PLUGIN_API release() override { return 1000; }

    std::vector<char> data;
    int64 pos = 0;
};

class EventList : public IEventList
{
public:
    int32 PLUGIN_API getEventCount() override { return (int32)events.size(); }
    tresult PLUGIN_API getEvent(int32 index, Event &e) override
    {
        if (index < 0 || index >= (int32)events.size()) return kResultFalse;
        e = events[(size_t)index];
        return kResultTrue;
    }
    tresult PLUGIN_API addEvent(Event &e) override
    {
        if (events.size() >= 4096) return kResultFalse;
        events.push_back(e);
        return kResultTrue;
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IEventList)
        QUERY_INTERFACE(iid, obj, IEventList::iid, IEventList)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1000; }
    uint32 PLUGIN_API release() override { return 1000; }

    std::vector<Event> events;
};

class ParamQueue : public IParamValueQueue
{
public:
    ParamID id = 0;
    std::vector<std::pair<int32, ParamValue>> pts;

    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return (int32)pts.size(); }
    tresult PLUGIN_API getPoint(int32 index, int32 &sampleOffset, ParamValue &value) override
    {
        if (index < 0 || index >= (int32)pts.size()) return kResultFalse;
        sampleOffset = pts[(size_t)index].first;
        value = pts[(size_t)index].second;
        return kResultTrue;
    }
    tresult PLUGIN_API addPoint(int32 sampleOffset, ParamValue value, int32 &index) override
    {
        index = (int32)pts.size();
        pts.emplace_back(sampleOffset, value);
        return kResultTrue;
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IParamValueQueue)
        QUERY_INTERFACE(iid, obj, IParamValueQueue::iid, IParamValueQueue)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1000; }
    uint32 PLUGIN_API release() override { return 1000; }
};

class ParamChanges : public IParameterChanges
{
public:
    std::vector<std::unique_ptr<ParamQueue>> queues;
    int32 used = 0;

    void clear() { used = 0; }

    int32 PLUGIN_API getParameterCount() override { return used; }
    IParamValueQueue *PLUGIN_API getParameterData(int32 index) override
    {
        if (index < 0 || index >= used) return nullptr;
        return queues[(size_t)index].get();
    }
    IParamValueQueue *PLUGIN_API addParameterData(const ParamID &pid, int32 &index) override
    {
        for (int32 i = 0; i < used; i++) {
            if (queues[(size_t)i]->id == pid) {
                index = i;
                return queues[(size_t)i].get();
            }
        }
        if ((size_t)used >= queues.size())
            queues.emplace_back(new ParamQueue());
        ParamQueue *q = queues[(size_t)used].get();
        q->id = pid;
        q->pts.clear();
        index = used++;
        return q;
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IParameterChanges)
        QUERY_INTERFACE(iid, obj, IParameterChanges::iid, IParameterChanges)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1000; }
    uint32 PLUGIN_API release() override { return 1000; }
};

// ---------------------------------------------------------------------------
// Plugin implementation
// ---------------------------------------------------------------------------

class Impl : public IComponentHandler, public IPlugFrame
{
public:
    virtual ~Impl() {}
    Info info;
    Module *module = nullptr;
    IComponent *component = nullptr;
    IAudioProcessor *processor = nullptr;
    IEditController *controller = nullptr;
    IMidiMapping *midiMapping = nullptr;
    bool controllerSeparate = false;
    bool connected = false;
    bool active = false;

    int  inCh = 0, outCh = 2;           // channel count of main buses
    int  numInBuses = 0, numOutBuses = 0;
    double sampleRate = 44100.0;
    enum { kMaxBlock = 4096 };

    std::vector<std::vector<float>> inBuf;   // planar
    std::vector<float *> inPtr;
    std::vector<AudioBusBuffers> inBuses;

    // Multi-bus output storage: allOutBuf[bus][channel][sample]
    std::vector<std::vector<std::vector<float>>> allOutBuf;
    std::vector<std::vector<float *>> allOutPtr;
    std::vector<AudioBusBuffers> outBuses;

    std::mutex lock;                    // protects pending queues
    std::vector<Event> pendingEvents;
    std::vector<std::pair<ParamID, ParamValue>> pendingParams;
    std::set<int> activeNotes;          // (ch << 8) | note

    EventList inEvents, outEvents;
    ParamChanges inParams, outParams;
    ProcessContext ctx;
    int64 samplePos = 0;
    int diagBlocks = 0, diagNotes = 0, diagBad = 0;
    float diagPeak = 0.0f;
    bool diagDone = false;
    std::string diagName;

    volatile bool bypassed = false;

    IPlugView *view = nullptr;
    int viewW = 0, viewH = 0;

    // --- IComponentHandler ---
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue v) override
    {
        std::lock_guard<std::mutex> g(lock);
        pendingParams.emplace_back(id, v);
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }

    // --- IPlugFrame ---
    tresult PLUGIN_API resizeView(IPlugView *v, ViewRect *r) override
    {
        if (!r) return kInvalidArgument;
        viewW = r->right - r->left;
        viewH = r->bottom - r->top;
        if (v) v->onSize(r);
        return kResultOk;
    }

    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IComponentHandler)
        QUERY_INTERFACE(iid, obj, IComponentHandler::iid, IComponentHandler)
        QUERY_INTERFACE(iid, obj, IPlugFrame::iid, IPlugFrame)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1000; }
    uint32 PLUGIN_API release() override { return 1000; }

    void queueParamByMidi(int ch, int ctrl, double value)
    {
        if (!midiMapping) return;
        ParamID pid = 0;
        if (midiMapping->getMidiControllerAssignment(0, (int16)ch, (CtrlNumber)ctrl, pid) == kResultOk) {
            std::lock_guard<std::mutex> g(lock);
            pendingParams.emplace_back(pid, std::min(1.0, std::max(0.0, value)));
        } else if (ch != 0 && midiMapping->getMidiControllerAssignment(0, 0, (CtrlNumber)ctrl, pid) == kResultOk) {
            std::lock_guard<std::mutex> g(lock);
            pendingParams.emplace_back(pid, std::min(1.0, std::max(0.0, value)));
        }
    }

    void pushEvent(const Event &e)
    {
        std::lock_guard<std::mutex> g(lock);
        if (pendingEvents.size() < 8192)
            pendingEvents.push_back(e);
    }

    void teardown();
};

static void fillInfoFromFactory(IPluginFactory *f, int idx, ClassEntry &ce)
{
    PClassInfo ci;
    if (f->getClassInfo(idx, &ci) != kResultOk)
        return;
    ce.index = idx;
    ce.name = ci.name;
    ce.uid = hashUid(ci.cid);

    PFactoryInfo fi;
    if (f->getFactoryInfo(&fi) == kResultOk)
        ce.vendor = fi.vendor;

    IPluginFactory2 *f2 = nullptr;
    if (f->queryInterface(IPluginFactory2::iid, (void **)&f2) == kResultOk && f2) {
        PClassInfo2 c2;
        if (f2->getClassInfo2(idx, &c2) == kResultOk) {
            ce.subCategories = c2.subCategories;
            if (c2.vendor[0]) ce.vendor = c2.vendor;
        }
        f2->release();
    }
    ce.isInstrument = ce.subCategories.find("Instrument") != std::string::npos;
}

bool scan(const std::string &fileUtf8, std::vector<ClassEntry> &out, std::string *err)
{
    std::string path; int idx;
    splitPath(fileUtf8, path, idx);

    Module *m = acquireModule(path, err);
    if (!m) return false;

    IPluginFactory *f = m->factory;
    int count = f->countClasses();
    for (int i = 0; i < count; i++) {
        PClassInfo ci;
        if (f->getClassInfo(i, &ci) != kResultOk)
            continue;
        if (std::strcmp(ci.category, kVstAudioEffectClass) != 0)
            continue;
        ClassEntry ce;
        fillInfoFromFactory(f, i, ce);
        out.push_back(ce);
    }
    releaseModule(m);
    return !out.empty();
}

Plugin::Plugin() : d(new Impl()) {}

void Impl::teardown()
{
    if (view) {
        view->setFrame(nullptr);
        view->removed();
        view->release();
        view = nullptr;
    }
    if (processor && active) {
        processor->setProcessing(false);
    }
    if (component && active) {
        component->setActive(false);
        active = false;
    }
    if (controller) {
        controller->setComponentHandler(nullptr);
    }
    if (connected && component && controller) {
        IConnectionPoint *cp1 = nullptr, *cp2 = nullptr;
        component->queryInterface(IConnectionPoint::iid, (void **)&cp1);
        controller->queryInterface(IConnectionPoint::iid, (void **)&cp2);
        if (cp1 && cp2) {
            cp1->disconnect(cp2);
            cp2->disconnect(cp1);
        }
        if (cp1) cp1->release();
        if (cp2) cp2->release();
        connected = false;
    }
    if (midiMapping) { midiMapping->release(); midiMapping = nullptr; }
    if (controller && controllerSeparate) {
        controller->terminate();
    }
    if (controller) { controller->release(); controller = nullptr; }
    if (processor) { processor->release(); processor = nullptr; }
    if (component) {
        component->terminate();
        component->release();
        component = nullptr;
    }
    if (module) {
        releaseModule(module);
        module = nullptr;
    }
}

Plugin::~Plugin()
{
    d->teardown();
    delete d;
}

Plugin *Plugin::load(const std::string &fullPathUtf8, bool wantInstrument,
                     double sampleRate, std::string *err)
{
    std::string path; int wantIndex;
    splitPath(fullPathUtf8, path, wantIndex);

    Module *m = acquireModule(path, err);
    if (!m) return nullptr;

    IPluginFactory *f = m->factory;
    int count = f->countClasses();

    // pick the class
    int pick = -1;
    ClassEntry picked;
    if (wantIndex >= 0) {
        if (wantIndex < count) {
            PClassInfo ci;
            if (f->getClassInfo(wantIndex, &ci) == kResultOk &&
                std::strcmp(ci.category, kVstAudioEffectClass) == 0) {
                pick = wantIndex;
                fillInfoFromFactory(f, pick, picked);
            }
        }
    } else {
        int fallback = -1;
        for (int i = 0; i < count; i++) {
            PClassInfo ci;
            if (f->getClassInfo(i, &ci) != kResultOk) continue;
            if (std::strcmp(ci.category, kVstAudioEffectClass) != 0) continue;
            ClassEntry ce;
            fillInfoFromFactory(f, i, ce);
            if (fallback < 0) fallback = i;
            if (ce.isInstrument == wantInstrument) {
                pick = i;
                picked = ce;
                break;
            }
        }
        if (pick < 0 && fallback >= 0) {
            pick = fallback;
            fillInfoFromFactory(f, pick, picked);
        }
    }
    if (pick < 0) {
        if (err) *err = "no audio class";
        releaseModule(m);
        return nullptr;
    }

    PClassInfo ci;
    f->getClassInfo(pick, &ci);

    Plugin *p = new Plugin();
    Impl *d = p->d;
    d->module = m;
    d->sampleRate = sampleRate > 0 ? sampleRate : 44100.0;
    d->info.name = picked.name;
    d->info.vendor = picked.vendor;
    d->info.uid = picked.uid;
    d->info.isInstrument = picked.isInstrument;

    auto fail = [&](const char *msg) -> Plugin * {
        if (err) *err = msg;
        delete p;
        return nullptr;
    };

    // --- component ---
    if (f->createInstance(ci.cid, IComponent::iid, (void **)&d->component) != kResultOk || !d->component)
        return fail("createInstance failed");

    if (d->component->initialize(&gHostApp) != kResultOk) {
        d->component->release();
        d->component = nullptr;
        return fail("component initialize failed");
    }

    if (d->component->queryInterface(IAudioProcessor::iid, (void **)&d->processor) != kResultOk || !d->processor)
        return fail("no IAudioProcessor");

    // --- controller ---
    if (d->component->queryInterface(IEditController::iid, (void **)&d->controller) != kResultOk || !d->controller) {
        d->controller = nullptr;
        TUID cid;
        if (d->component->getControllerClassId(cid) == kResultOk) {
            if (f->createInstance(cid, IEditController::iid, (void **)&d->controller) == kResultOk && d->controller) {
                d->controllerSeparate = true;
                if (d->controller->initialize(&gHostApp) != kResultOk) {
                    d->controller->release();
                    d->controller = nullptr;
                    d->controllerSeparate = false;
                }
            }
        }
    }
    if (d->controller) {
        d->controller->setComponentHandler(d);
        d->controller->queryInterface(IMidiMapping::iid, (void **)&d->midiMapping);

        if (d->controllerSeparate) {
            IConnectionPoint *cp1 = nullptr, *cp2 = nullptr;
            d->component->queryInterface(IConnectionPoint::iid, (void **)&cp1);
            d->controller->queryInterface(IConnectionPoint::iid, (void **)&cp2);
            if (cp1 && cp2) {
                cp1->connect(cp2);
                cp2->connect(cp1);
                d->connected = true;
            }
            if (cp1) cp1->release();
            if (cp2) cp2->release();

            // controller learns the initial component state
            MemStream ms;
            if (d->component->getState(&ms) == kResultOk) {
                ms.pos = 0;
                d->controller->setComponentState(&ms);
            }
        }
    }

    // --- buses ---
    d->numInBuses = d->component->getBusCount(kAudio, kInput);
    d->numOutBuses = d->component->getBusCount(kAudio, kOutput);

    if (d->numOutBuses <= 0)
        return fail("plug-in has no audio output");

    {
        std::vector<SpeakerArrangement> inArr((size_t)d->numInBuses), outArr((size_t)d->numOutBuses);
        for (int i = 0; i < d->numInBuses; i++) {
            SpeakerArrangement a = SpeakerArr::kStereo;
            d->processor->getBusArrangement(kInput, i, a);
            inArr[(size_t)i] = a;
        }
        for (int i = 0; i < d->numOutBuses; i++) {
            SpeakerArrangement a = SpeakerArr::kStereo;
            d->processor->getBusArrangement(kOutput, i, a);
            outArr[(size_t)i] = a;
        }
        if (d->numInBuses > 0) inArr[0] = SpeakerArr::kStereo;
        outArr[0] = SpeakerArr::kStereo;
        const tresult rArr = d->processor->setBusArrangements(d->numInBuses ? inArr.data() : nullptr, d->numInBuses,
                                                              outArr.data(), d->numOutBuses);
        vlog("%s: buses in=%d out=%d, setBusArrangements(stereo)=0x%x", picked.name.c_str(), d->numInBuses, d->numOutBuses, (unsigned)rArr);

        SpeakerArrangement a;
        if (d->numInBuses > 0 && d->processor->getBusArrangement(kInput, 0, a) == kResultOk)
            d->inCh = SpeakerArr::getChannelCount(a);
        if (d->processor->getBusArrangement(kOutput, 0, a) == kResultOk)
            d->outCh = SpeakerArr::getChannelCount(a);
        if (d->outCh <= 0) d->outCh = 2;
    }

    for (int i = 0; i < d->numInBuses; i++)
        d->component->activateBus(kAudio, kInput, i, i == 0);
    for (int i = 0; i < d->numOutBuses; i++)
        d->component->activateBus(kAudio, kOutput, i, true);
    int evIn = d->component->getBusCount(kEvent, kInput);
    for (int i = 0; i < evIn; i++)
        d->component->activateBus(kEvent, kInput, i, true);

    if (d->processor->canProcessSampleSize(kSample32) != kResultTrue)
        return fail("32-bit float processing not supported");

    ProcessSetup setup;
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = Impl::kMaxBlock;
    setup.sampleRate = d->sampleRate;
    if (d->processor->setupProcessing(setup) != kResultOk)
        return fail("setupProcessing failed");

    if (d->component->setActive(true) != kResultOk)
        return fail("setActive failed");
    d->active = true;
    const tresult rProc = d->processor->setProcessing(true);
    d->diagName = picked.name;
    vlog("%s: loaded (%s), channels in=%d out=%d, rate=%.0f, setProcessing=0x%x", picked.name.c_str(),
         picked.isInstrument ? "instrument" : "effect", d->inCh, d->outCh, d->sampleRate, (unsigned)rProc);

    // --- buffers ---
    d->inBuf.assign((size_t)std::max(d->inCh, 0), std::vector<float>(Impl::kMaxBlock));
    d->inPtr.resize(d->inBuf.size());
    for (size_t c = 0; c < d->inBuf.size(); c++)
        d->inPtr[c] = d->inBuf[c].data();
    d->inBuses.resize((size_t)d->numInBuses);

    d->allOutBuf.resize((size_t)d->numOutBuses);
    d->allOutPtr.resize((size_t)d->numOutBuses);
    d->outBuses.resize((size_t)d->numOutBuses);
    for (int b = 0; b < d->numOutBuses; b++) {
        SpeakerArrangement a = SpeakerArr::kStereo;
        int chs = 2;
        if (d->processor->getBusArrangement(kOutput, b, a) == kResultOk)
            chs = SpeakerArr::getChannelCount(a);
        if (chs <= 0) chs = 2;
        d->allOutBuf[(size_t)b].assign((size_t)chs, std::vector<float>(Impl::kMaxBlock, 0.0f));
        d->allOutPtr[(size_t)b].resize((size_t)chs);
        for (int c = 0; c < chs; c++)
            d->allOutPtr[(size_t)b][(size_t)c] = d->allOutBuf[(size_t)b][(size_t)c].data();
        d->outBuses[(size_t)b].silenceFlags = 0;
        d->outBuses[(size_t)b].numChannels = chs;
        d->outBuses[(size_t)b].channelBuffers32 = d->allOutPtr[(size_t)b].data();
    }

    std::memset(&d->ctx, 0, sizeof(ProcessContext));
    d->ctx.sampleRate = d->sampleRate;
    d->ctx.tempo = 120.0;
    d->ctx.timeSigNumerator = 4;
    d->ctx.timeSigDenominator = 4;
    d->ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid | ProcessContext::kTimeSigValid;

    // --- info ---
    d->info.chansIn = d->numInBuses > 0 ? d->inCh : 0;
    d->info.chansOut = d->outCh;
    if (d->controller) {
        IPlugView *v = d->controller->createView(ViewType::kEditor);
        if (v) {
            ViewRect r;
            if (v->getSize(&r) == kResultOk) {
                d->info.editorWidth = r.right - r.left;
                d->info.editorHeight = r.bottom - r.top;
            }
#ifdef _WIN32
            d->info.hasEditor = v->isPlatformTypeSupported(kPlatformTypeHWND) == kResultTrue;
#else
            d->info.hasEditor = false;
#endif
            v->release();
        }
    }
    d->viewW = d->info.editorWidth;
    d->viewH = d->info.editorHeight;

    return p;
}

const Info &Plugin::info() const { return d->info; }
bool Plugin::isInstrument() const { return d->info.isInstrument; }

void Plugin::setBypass(bool b) { d->bypassed = b; }
bool Plugin::bypass() const { return d->bypassed; }

void Plugin::process(const float *in, float *out, int frames, int hostChans)
{
    Impl *m = d;
    if (frames <= 0) return;
    if (hostChans < 1) hostChans = 1;
    if (hostChans > 2) hostChans = 2;

    const bool hasIn = m->numInBuses > 0 && in != nullptr;

    if (m->bypassed && hasIn) {
        if (out != in)
            std::memcpy(out, in, sizeof(float) * (size_t)frames * (size_t)hostChans);
        return;
    }

    int done = 0;
    while (done < frames) {
        const int maxBlock = Impl::kMaxBlock;
        int n = std::min(frames - done, maxBlock);

        // --- silence out buffers upfront ---
        for (int b = 0; b < m->numOutBuses; b++) {
            for (auto &chBuf : m->allOutBuf[(size_t)b])
                std::fill(chBuf.begin(), chBuf.begin() + n, 0.0f);
        }

        // --- input to planar ---
        if (m->numInBuses > 0) {
            for (int c = 0; c < (int)m->inBuf.size(); c++) {
                float *dst = m->inBuf[(size_t)c].data();
                if (!in) {
                    std::memset(dst, 0, sizeof(float) * (size_t)n);
                } else if (m->inBuf.size() == 1) {
                    for (int i = 0; i < n; i++) {
                        const float *s = in + (size_t)(done + i) * hostChans;
                        dst[i] = hostChans == 2 ? 0.5f * (s[0] + s[1]) : s[0];
                    }
                } else if (c < 2) {
                    for (int i = 0; i < n; i++) {
                        const float *s = in + (size_t)(done + i) * hostChans;
                        dst[i] = s[hostChans == 2 ? c : 0];
                    }
                } else {
                    std::memset(dst, 0, sizeof(float) * (size_t)n);
                }
                m->inPtr[(size_t)c] = dst;
            }
        }

        // --- buses ---
        for (int b = 0; b < m->numInBuses; b++) {
            AudioBusBuffers &bb = m->inBuses[(size_t)b];
            bb.silenceFlags = 0;
            bb.numChannels = b == 0 ? (int32)m->inBuf.size() : 0;
            bb.channelBuffers32 = b == 0 ? m->inPtr.data() : nullptr;
        }
        for (int b = 0; b < m->numOutBuses; b++) {
            AudioBusBuffers &bb = m->outBuses[(size_t)b];
            bb.silenceFlags = 0;
            bb.numChannels = (int32)m->allOutBuf[(size_t)b].size();
            bb.channelBuffers32 = m->allOutPtr[(size_t)b].data();
        }

        // --- events and parameter changes ---
        m->inEvents.events.clear();
        m->outEvents.events.clear();
        m->inParams.clear();
        m->outParams.clear();
        {
            std::lock_guard<std::mutex> g(m->lock);
            m->inEvents.events.swap(m->pendingEvents);
            for (auto &pc : m->pendingParams) {
                int32 idx;
                IParamValueQueue *q = m->inParams.addParameterData(pc.first, idx);
                if (q) {
                    int32 pi;
                    q->addPoint(0, pc.second, pi);
                }
            }
            m->pendingParams.clear();
        }

        ProcessData data;
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = n;
        data.numInputs = m->numInBuses;
        data.numOutputs = m->numOutBuses;
        data.inputs = m->numInBuses ? m->inBuses.data() : nullptr;
        data.outputs = m->outBuses.data();
        data.inputParameterChanges = &m->inParams;
        data.outputParameterChanges = &m->outParams;
        data.inputEvents = &m->inEvents;
        data.outputEvents = &m->outEvents;
        m->ctx.projectTimeSamples = m->samplePos;
        m->ctx.continousTimeSamples = m->samplePos;
        data.processContext = &m->ctx;

        tresult r = m->processor->process(data);
        m->samplePos += n;
        if (!m->diagDone && (!m->info.isInstrument || m->diagNotes > 0)) {      // instruments: count from the first note
            if (r != kResultOk && r != kResultTrue && m->diagBad++ < 3)
                vlog("%s: process() returned 0x%x", m->diagName.c_str(), (unsigned)r);
            for (int b = 0; b < m->numOutBuses; b++) {
                for (const auto &chBuf : m->allOutBuf[(size_t)b]) {
                    for (int i = 0; i < n; i++) {
                        const float a0 = std::fabs(chBuf[(size_t)i]);
                        if (a0 > m->diagPeak) m->diagPeak = a0;
                    }
                }
            }
            if (++m->diagBlocks >= 600) {
                m->diagDone = true;
                vlog("%s: first %d blocks: output peak %.5f, notes sent %d, process errors %d", m->diagName.c_str(),
                     m->diagBlocks, (double)m->diagPeak, m->diagNotes, m->diagBad);
            }
        }

        // --- planar to interleaved (mix all output buses down to stereo) ---
        float *dst = out + (size_t)done * hostChans;
        if (r != kResultOk && r != kResultTrue) {
            std::memset(dst, 0, sizeof(float) * (size_t)n * (size_t)hostChans);
        } else {
            for (int i = 0; i < n; i++) {
                float sumL = 0.0f;
                float sumR = 0.0f;
                for (int b = 0; b < m->numOutBuses; b++) {
                    const auto &bBuf = m->allOutBuf[(size_t)b];
                    if (bBuf.size() >= 2) {
                        sumL += bBuf[0][(size_t)i];
                        sumR += bBuf[1][(size_t)i];
                    } else if (bBuf.size() == 1) {
                        float v = bBuf[0][(size_t)i];
                        sumL += v;
                        sumR += v;
                    }
                }
                if (hostChans == 2) {
                    dst[2 * i] = sumL;
                    dst[2 * i + 1] = sumR;
                } else {
                    dst[i] = 0.5f * (sumL + sumR);
                }
            }
        }
        done += n;
    }
}

// ---- midi ----

void Plugin::noteOn(int ch, int note, int velocity)
{
    if (velocity <= 0) { noteOff(ch, note, 0); return; }
    Event e;
    std::memset(&e, 0, sizeof(e));
    e.busIndex = 0;
    e.sampleOffset = 0;
    e.type = Event::kNoteOnEvent;
    e.noteOn.channel = (int16)ch;
    e.noteOn.pitch = (int16)note;
    e.noteOn.velocity = std::min(1.0f, velocity / 127.0f);
    e.noteOn.noteId = -1;
    e.noteOn.tuning = 0.f;
    d->pushEvent(e);
    d->diagNotes++;
    {
        std::lock_guard<std::mutex> g(d->lock);
        d->activeNotes.insert((ch << 8) | note);
    }
    // Most instrument plugins (drum samplers like SSD5, EZdrummer, Addictive Drums)
    // listen on MIDI Channel 1 (index 0). If the host sent on channel 9 (drums) or other,
    // also dispatch to channel 0 so single-timbral instruments always trigger.
    if (d->info.isInstrument && ch != 0) {
        Event e0 = e;
        e0.noteOn.channel = 0;
        d->pushEvent(e0);
        std::lock_guard<std::mutex> g(d->lock);
        d->activeNotes.insert(note);
    }
}

void Plugin::noteOff(int ch, int note, int velocity)
{
    Event e;
    std::memset(&e, 0, sizeof(e));
    e.busIndex = 0;
    e.sampleOffset = 0;
    e.type = Event::kNoteOffEvent;
    e.noteOff.channel = (int16)ch;
    e.noteOff.pitch = (int16)note;
    e.noteOff.velocity = std::min(1.0f, velocity / 127.0f);
    e.noteOff.noteId = -1;
    e.noteOff.tuning = 0.f;
    d->pushEvent(e);
    {
        std::lock_guard<std::mutex> g(d->lock);
        d->activeNotes.erase((ch << 8) | note);
    }
    if (d->info.isInstrument && ch != 0) {
        Event e0 = e;
        e0.noteOff.channel = 0;
        d->pushEvent(e0);
        std::lock_guard<std::mutex> g(d->lock);
        d->activeNotes.erase(note);
    }
}

void Plugin::polyPressure(int ch, int note, int value)
{
    Event e;
    std::memset(&e, 0, sizeof(e));
    e.busIndex = 0;
    e.sampleOffset = 0;
    e.type = Event::kPolyPressureEvent;
    e.polyPressure.channel = (int16)ch;
    e.polyPressure.pitch = (int16)note;
    e.polyPressure.pressure = value / 127.0f;
    e.polyPressure.noteId = -1;
    d->pushEvent(e);
}

void Plugin::controller(int ch, int cc, int value)
{
    d->queueParamByMidi(ch, cc, value / 127.0);
}

void Plugin::channelPressure(int ch, int value)
{
    d->queueParamByMidi(ch, kAfterTouch, value / 127.0);
}

void Plugin::pitchBend(int ch, int value14)
{
    d->queueParamByMidi(ch, kPitchBend, value14 / 16383.0);
}

void Plugin::programChange(int ch, int program)
{
    d->queueParamByMidi(ch, kCtrlProgramChange, program / 127.0);
}

void Plugin::allNotesOff(int ch)
{
    std::vector<int> notes;
    {
        std::lock_guard<std::mutex> g(d->lock);
        for (int n : d->activeNotes)
            if (ch < 0 || (n >> 8) == ch)
                notes.push_back(n);
    }
    for (int n : notes)
        noteOff(n >> 8, n & 0xFF, 0);
    if (ch >= 0) d->queueParamByMidi(ch, kCtrlAllNotesOff, 0.0);
    else for (int c = 0; c < 16; c++) d->queueParamByMidi(c, kCtrlAllNotesOff, 0.0);
}

void Plugin::reset()
{
    allNotesOff(-1);
    for (int c = 0; c < 16; c++) d->queueParamByMidi(c, 121, 0.0);
}

// ---- parameters / state ----

int Plugin::paramCount() const
{
    return d->controller ? d->controller->getParameterCount() : 0;
}

float Plugin::param(int index) const
{
    if (!d->controller) return 0.f;
    ParameterInfo pi;
    if (d->controller->getParameterInfo(index, pi) != kResultOk) return 0.f;
    return (float)d->controller->getParamNormalized(pi.id);
}

void Plugin::setParam(int index, float v)
{
    if (!d->controller) return;
    ParameterInfo pi;
    if (d->controller->getParameterInfo(index, pi) != kResultOk) return;
    if (pi.flags & ParameterInfo::kIsReadOnly) return;
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    d->controller->setParamNormalized(pi.id, v);
    std::lock_guard<std::mutex> g(d->lock);
    d->pendingParams.emplace_back(pi.id, (ParamValue)v);
}

static void putU32(std::vector<char> &v, uint32_t x)
{
    for (int i = 0; i < 4; i++) v.push_back((char)((x >> (8 * i)) & 0xFF));
}

static uint32_t getU32(const char *p)
{
    return (uint32_t)(uint8_t)p[0] | ((uint32_t)(uint8_t)p[1] << 8) |
           ((uint32_t)(uint8_t)p[2] << 16) | ((uint32_t)(uint8_t)p[3] << 24);
}

std::vector<char> Plugin::saveState()
{
    MemStream comp, ctrl;
    if (d->component) d->component->getState(&comp);
    if (d->controller) d->controller->getState(&ctrl);

    std::vector<char> out;
    out.push_back('B'); out.push_back('V'); out.push_back('3'); out.push_back('S');
    putU32(out, (uint32_t)comp.data.size());
    out.insert(out.end(), comp.data.begin(), comp.data.end());
    putU32(out, (uint32_t)ctrl.data.size());
    out.insert(out.end(), ctrl.data.begin(), ctrl.data.end());
    return out;
}

bool Plugin::loadState(const char *data, size_t size)
{
    if (size < 12 || std::memcmp(data, "BV3S", 4) != 0)
        return false;
    uint32_t n1 = getU32(data + 4);
    if (8 + (size_t)n1 + 4 > size) return false;
    const char *c1 = data + 8;
    uint32_t n2 = getU32(c1 + n1);
    if (8 + (size_t)n1 + 4 + (size_t)n2 > size) return false;
    const char *c2 = c1 + n1 + 4;

    bool ok = true;
    if (d->component && n1 > 0) {
        MemStream s(c1, n1);
        ok = d->component->setState(&s) == kResultOk;
        if (d->controller) {
            MemStream s2(c1, n1);
            d->controller->setComponentState(&s2);
        }
    }
    if (d->controller && n2 > 0) {
        MemStream s(c2, n2);
        d->controller->setState(&s);
    }
    return ok;
}

// ---- editor ----

void Plugin::editorSize(int &w, int &h) const
{
    w = d->viewW;
    h = d->viewH;
}

bool Plugin::embedEditor(void *parent)
{
    if (!parent) {
        if (d->view) {
            d->view->setFrame(nullptr);
            d->view->removed();
            d->view->release();
            d->view = nullptr;
        }
        return true;
    }

    if (!d->controller) return false;

    if (d->view) {          // already attached: re-create cleanly
        d->view->setFrame(nullptr);
        d->view->removed();
        d->view->release();
        d->view = nullptr;
    }

    IPlugView *v = d->controller->createView(ViewType::kEditor);
    if (!v) return false;

#ifdef _WIN32
    if (v->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue) {
        v->release();
        return false;
    }
    v->setFrame(d);
    ViewRect r;
    if (v->getSize(&r) == kResultOk) {
        d->viewW = r.right - r.left;
        d->viewH = r.bottom - r.top;
    }
    if (v->attached(parent, kPlatformTypeHWND) != kResultOk) {
        v->setFrame(nullptr);
        v->release();
        return false;
    }
    d->view = v;
    return true;
#else
    (void)parent;
    v->release();
    return false;
#endif
}

} // namespace vst3host
