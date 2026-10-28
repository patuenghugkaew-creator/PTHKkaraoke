#include "VSTDialog.h"

#include <QShowEvent>
#include <QCloseEvent>
#include <QHideEvent>
#include <QTimer>
#include <QPointer>
#include <QStandardPaths>
#include <QFile>
#include <QDateTime>
#include <QTextStream>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

static void vstLog(const QString &msg)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) return;
    QFile f(dir + "/vst_embed.log");
    if (f.size() > 200000) f.remove();
    if (f.open(QIODevice::Append | QIODevice::Text))
        QTextStream(&f) << QDateTime::currentDateTime().toString("hh:mm:ss.zzz ") << msg << "\n";
}

VSTDialog::VSTDialog(QWidget *parent, DWORD fxHandle, const QString &instName) : QDialog(parent)
{
    this->fxHandle = fxHandle;

    // Plug-in editors keep the plain native Windows frame (no neon skin): the plug-in draws
    // its own GUI straight into this window, exactly like in other hosts.
    setProperty("pthkNoSkin", true);
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);

    BASS_VST_INFO info;
    if (bv::GetInfo(fxHandle, &info) && info.hasEditor)
    {
        QString name = info.effectName;
        name += " - ";
        name += info.vendorName;

        setWindowTitle(name + "  [" + instName + "]");
        edW = info.editorWidth;
        edH = info.editorHeight;
        setFixedSize(edW, edH);

        canOpen = true;
    }
}

void VSTDialog::showEvent(QShowEvent *event)
{
    if (!attached) {
        bv::EmbedEditor(fxHandle, NULL);   // make sure no stale embed remains
        embedNow();
        attached = true;
        // Some plug-ins (32-bit ones especially) create their editor window but never paint it
        // into a freshly created parent; verify shortly after and re-embed / repaint if needed.
        QPointer<VSTDialog> self(this);
        QTimer::singleShot(150, this, [self]() { if (self && self->attached) self->verifyEmbed(1); });
    }
    event->accept();
}

void VSTDialog::embedNow()
{
#ifdef Q_OS_WIN
    HWND h = (HWND)winId();
    LONG_PTR st = GetWindowLongPtr(h, GWL_STYLE);
    SetWindowLongPtr(h, GWL_STYLE, st | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    BOOL ok = bv::EmbedEditor(fxHandle, h);
    vstLog(QString("embed fx=%1 host=%2 ok=%3 size=%4x%5 dpr=%6")
           .arg(fxHandle).arg((quintptr)h).arg(ok).arg(edW).arg(edH).arg(devicePixelRatioF()));
#else
    bv::EmbedEditor(fxHandle, (HWND)winId());
#endif
}

void VSTDialog::verifyEmbed(int attempt)
{
#ifdef Q_OS_WIN
    HWND h = (HWND)winId();
    HWND child = GetWindow(h, GW_CHILD);
    vstLog(QString("verify attempt=%1 child=%2 visible=%3").arg(attempt).arg((quintptr)child)
           .arg(child ? IsWindowVisible(child) : 0));
    if (!child && attempt < 4) {
        bv::EmbedEditor(fxHandle, NULL);
        embedNow();
        QPointer<VSTDialog> self(this);
        QTimer::singleShot(250, this, [self, attempt]() { if (self && self->attached) self->verifyEmbed(attempt + 1); });
        return;
    }
    if (child) {
        ShowWindow(child, SW_SHOW);
        RedrawWindow(child, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
    }
#else
    Q_UNUSED(attempt)
#endif
}

void VSTDialog::detachEditor()
{
    if (attached && fxHandle != 0)
        bv::EmbedEditor(fxHandle, NULL);
    attached = false;
}

void VSTDialog::hideEvent(QHideEvent *event)
{
    // reject()/hide() (used by the skinned title-bar close) sends no closeEvent,
    // so the editor must be released here or it can never be embedded again.
    detachEditor();
    QDialog::hideEvent(event);
}

void VSTDialog::closeEvent(QCloseEvent *event)
{
    detachEditor();
    event->accept();
}

VSTDialog::~VSTDialog()
{
    detachEditor();
}
