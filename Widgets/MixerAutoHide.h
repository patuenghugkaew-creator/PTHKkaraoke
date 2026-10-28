#ifndef MIXERAUTOHIDE_H
#define MIXERAUTOHIDE_H

// Auto-hide of the mixer window while the karaoke program (another program) is being used.
//   * karaoke program in front and the user is typing / clicking in it  -> the mixer hides itself
//   * no keyboard / mouse input for <delay> seconds, or another program comes to the front for <delay> seconds
//     -> the mixer comes back (without stealing the focus)
// The karaoke program is recognised by the program (.exe) name or a part of its window title typed in Settings;
// when that is left empty, any other program counts. Windows only (does nothing elsewhere). Header-only.

#include <QObject>
#include <QTimer>
#include <QWidget>
#include <QRect>
#include <QCursor>
#include <QApplication>
#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

#ifdef _WIN32
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

class MixerAutoHide : public QObject
{
public:
    explicit MixerAutoHide(QWidget *mixer, QObject *parent = nullptr) : QObject(parent), mixer(mixer)
    {
        timer.setInterval(40);
        QObject::connect(&timer, &QTimer::timeout, this, [this]() { tick(); });
    }

    void configure(bool on, double delaySec, const QString &program)
    {
        enabled = on;
        delayMs = qBound(300, int(delaySec * 1000.0), 120000);
        names.clear();
        for (const QString &s : program.split(QRegExp("[;,]"), QString::SkipEmptyParts))
            if (!s.trimmed().isEmpty())
                names << s.trimmed().toLower();
#ifdef _WIN32
        if (enabled) {
            if (!timer.isActive())
                timer.start();
        } else {
            timer.stop();
            restore();
        }
#endif
    }

    bool isEnabled() const { return enabled; }

private:
    QWidget *mixer;
    QTimer timer;
    bool enabled = false;
    bool hiddenByUs = false;
    int delayMs = 3000;
    QStringList names;
    mutable DWORD cachePid = 0;
    mutable QString cacheExe;
    QElapsedTimer awayClock;     // how long the karaoke program has not been in front
    bool awayRunning = false;
    QElapsedTimer lastAct;       // last click / key press in the karaoke program
    bool haveAct = false;

    // small diagnostic log (only state changes): ~/.PTHKKaraoke/autohide.log - kept short
    void log(const QString &msg) const
    {
        QDir().mkpath(QDir::homePath() + QStringLiteral("/.PTHKKaraoke"));
        QFile f(QDir::homePath() + QStringLiteral("/.PTHKKaraoke/autohide.log"));
        if (f.size() > 64 * 1024)
            f.remove();
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream t(&f);
            t.setCodec("UTF-8");
            t << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")) << "  " << msg << "\n";
        }
    }

    void restore()
    {
        awayRunning = false;
        if (hiddenByUs && mixer) {
            hiddenByUs = false;
            log(QStringLiteral("restore: mixer shown again"));
            mixer->setProperty("kmAutoRestore", true);   // window is unchanged: skip the heavy re-fit
            mixer->setAttribute(Qt::WA_ShowWithoutActivating, true);
            mixer->show();
#ifdef _WIN32
            // the karaoke program is in front (often full-screen / topmost): put the mixer above it without taking
            // the focus, otherwise it stays hidden behind it until Win+Tab
            HWND h = reinterpret_cast<HWND>(mixer->winId());
            const HWND fg = GetForegroundWindow();
            const bool fgTop = fg && (GetWindowLongW(fg, GWL_EXSTYLE) & WS_EX_TOPMOST);
            SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            if (!mixer->property("kmTopmost").toBool() && !fgTop)
                SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#endif
            mixer->raise();
        }
    }

#ifdef _WIN32
    QString exeNameOf(DWORD pid) const
    {
        if (pid == cachePid && !cacheExe.isEmpty())
            return cacheExe;
        QString res;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h) {
            wchar_t buf[MAX_PATH * 2];
            DWORD n = DWORD(sizeof(buf) / sizeof(buf[0]));
            if (QueryFullProcessImageNameW(h, 0, buf, &n))
                res = QString::fromWCharArray(buf, int(n)).replace('\\', '/');   // full path
            CloseHandle(h);
        }
        cachePid = pid;
        cacheExe = res;
        return res;
    }

    // is the window in front the karaoke program?
    bool karaokeInFront() const
    {
        HWND fg = GetForegroundWindow();
        if (!fg)
            return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid == GetCurrentProcessId())
            return false;                       // our own windows are never "the karaoke program"
        wchar_t cls[128] = {0};
        GetClassNameW(fg, cls, 127);
        const QString c = QString::fromWCharArray(cls);
        if (c == QLatin1String("Shell_TrayWnd") || c == QLatin1String("Shell_SecondaryTrayWnd")
            || c == QLatin1String("Progman") || c == QLatin1String("WorkerW"))
            return false;                       // taskbar / desktop
        if (names.isEmpty())
            return true;                        // any other program
        const QString exe = exeNameOf(pid).toLower();
        wchar_t title[256] = {0};
        GetWindowTextW(fg, title, 255);
        const QString t = QString::fromWCharArray(title).toLower();
        for (const QString &n : names)
            if ((!exe.isEmpty() && exe.contains(n)) || t.contains(n))
                return true;
        return false;
    }

    QString frontInfo() const
    {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg)
            GetWindowThreadProcessId(fg, &pid);
        wchar_t title[128] = {0};
        if (fg)
            GetWindowTextW(fg, title, 127);
        return exeNameOf(pid) + QStringLiteral(" | ") + QString::fromWCharArray(title);
    }

    void tick()
    {
        if (!enabled || !mixer)
            return;
        if (QApplication::activeModalWidget())  // never hide the mixer under an open settings / message dialog
            return;

        if (!karaokeInFront()) {
            // karaoke program not in front: bring the mixer back after the delay
            if (hiddenByUs) {
                if (!awayRunning) { awayClock.start(); awayRunning = true; }
                else if (awayClock.elapsed() >= delayMs) restore();
            }
            return;
        }
        awayRunning = false;

        // activity = a mouse click (outside the mixer) or a key press; moving the mouse does not count
        bool act = false;
        const bool overMixer = mixer->isVisible() && mixer->frameGeometry().contains(QCursor::pos());
        if (!overMixer)
            for (int vk : { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON })
                if (GetAsyncKeyState(vk) & 0x8001) { act = true; break; }
        if (!act)
            for (int vk = 8; vk < 255; ++vk) {
                if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2)
                    continue;
                // only real keys, and only "is down now" (bit 15): the "pressed since last call" bit (bit 0) and
                // virtual keys such as IME / packet / OEM-clear can report activity all the time, which kept the
                // mixer hidden until another window was brought to the front
                if ((vk > 0x5F && vk < 0x88) || (vk >= 0xBA && vk <= 0xE2) || vk < 0x5C) {
                    if (vk == VK_PROCESSKEY || vk == VK_PACKET)
                        continue;
                    if (GetAsyncKeyState(vk) & 0x8000) { act = true; break; }
                }
            }
        if (act) {
            lastAct.start();
            haveAct = true;
            if (!hiddenByUs && mixer->isVisible()) {
                hiddenByUs = true;
                log(QStringLiteral("hide: activity in the front program (%1)").arg(frontInfo()));
                mixer->hide();
            }
        } else if (hiddenByUs && (!haveAct || lastAct.elapsed() >= delayMs)) {
            restore();                          // no click / key for the delay: back again
        }
    }
#else
    void tick() {}
#endif
};

#endif // MIXERAUTOHIDE_H
