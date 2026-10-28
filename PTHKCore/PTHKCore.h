#ifndef PTHKCORE_H
#define PTHKCORE_H

#include "pthkcore_global.h"
#include <QString>
#include <QStringList>
#include <QVector>
#include <QByteArray>
#include <bass.h>
#include <bassmidi.h>

class QWidget;
class AiVocalEngine;

namespace PTHKCore {

// Version & Information
PTHKCORE_EXPORT QString version();
PTHKCORE_EXPORT QString copyright();

// ---- 1. License & Security Engine ----
PTHKCORE_EXPORT bool isProUnlocked();
PTHKCORE_EXPORT QString hardwareId();
PTHKCORE_EXPORT QString proStatusText();
PTHKCORE_EXPORT bool activateLicense(const QString &key);
PTHKCORE_EXPORT QString ownerMachineId();
PTHKCORE_EXPORT void showLicenseDialog(QWidget *parent);

// ---- 2. SoundFont Protection (KMSF) & USB Dongles ----
struct UsbDrive {
    QString name;
    QString id;
    QString label() const { return name; }
};

PTHKCORE_EXPORT QVector<UsbDrive> scanUsbDrives(bool force = false);
PTHKCORE_EXPORT QString normalizeId(const QString &text);
PTHKCORE_EXPORT QStringList connectedUsbIds();
PTHKCORE_EXPORT bool isProtectedSoundfont(const QString &filePath);
PTHKCORE_EXPORT HSOUNDFONT openProtectedSoundfont(const QString &filePath, DWORD flags);
PTHKCORE_EXPORT bool lockSoundfont(const QString &src, const QString &dst, const QString &id, bool usb, QString *error);

// ---- 3. Studio Mastering & DSP Engine ----
struct MasterParams {
    bool  masterOn = false;
    float compAmount = 0.0f;
    float lowDb = 0.0f;
    float midDb = 0.0f;
    float highDb = 0.0f;
    float exciter = 0.0f;
    float width = 1.0f;
    bool  autoOn = false;
    float targetLufs = -16.0f;
    float maxBoostDb = 6.0f;
    float maxCutDb = 12.0f;
    bool  meter = false;
    bool  limOn = false;
    float ceilingDb = -1.0f;
    float releaseMs = 120.0f;
    bool  softClip = false;
    bool  truePeak = true;
};

struct MasterMeter {
    float momentary = -100.0f;
    float shortTerm = -100.0f;
    float autoDb = 0.0f;
    float grDb = 0.0f;
    float peakDb = -100.0f;
};

PTHKCORE_EXPORT void initMasterDsp(int channels, int sampleRate);
PTHKCORE_EXPORT void processMasterDsp(float *samples, size_t frames, float outGain = 1.0f);
PTHKCORE_EXPORT void setMasterParams(const MasterParams &p);
PTHKCORE_EXPORT MasterParams masterParams();
PTHKCORE_EXPORT MasterMeter masterMeter();

// ---- 4. AI Vocal Engine ----
PTHKCORE_EXPORT AiVocalEngine* createAiVocalEngine(const QString &modelsDir);
PTHKCORE_EXPORT void destroyAiVocalEngine(AiVocalEngine *engine);

// ---- 5. EMK Karaoke Engine ----
struct EmkSong {
    bool valid = false;
    QString code;
    QString title;
    QString artist;
    QString key;
    int tempo = 120;
    QByteArray midData;
    QByteArray lyrData;
    QByteArray curData;
};

PTHKCORE_EXPORT bool isEmkFile(const QString &filePath);
PTHKCORE_EXPORT EmkSong readEmk(const QString &filePath);

// ---- 6. SIB Karaoke Engine (SIBSIB Karaoke AES-256 + ZIP) ----
struct SibSong {
    bool valid = false;
    QString code;
    QString title;
    QString artist;
    QString key;
    int tempo = 120;
    QByteArray midData;
    QByteArray lyrData;
    QByteArray curData;
};

PTHKCORE_EXPORT bool isSibFile(const QString &filePath);
PTHKCORE_EXPORT SibSong readSib(const QString &filePath);
PTHKCORE_EXPORT SibSong readSibData(const QByteArray &data);

} // namespace PTHKCore

// Compatibility namespace for MasterDsp
namespace MasterDsp {
    using Params = PTHKCore::MasterParams;
    using Meter = PTHKCore::MasterMeter;
}

#endif // PTHKCORE_H
