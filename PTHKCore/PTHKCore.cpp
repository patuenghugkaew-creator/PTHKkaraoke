#include "PTHKCore.h"
#include <QFile>
#include <QTextCodec>
#include <QRegExp>
#include <QBuffer>
#include <QFileInfo>
#include <QtGui/private/qzipreader_p.h>
#ifdef Q_OS_WIN
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif
#include "../Config.h"
#include "../Widgets/Ed25519.h"
#include "../Widgets/UsbDongle.h"
#include "../Widgets/ProLicense.h"
#include "../Midi/KmSoundfont.h"
#include "../Midi/MasterDsp.h"
#include "../Midi/AiVocal.h"

namespace PTHKCore {

QString version()
{
    return QStringLiteral("1.0.0 Pro");
}

QString copyright()
{
    return QStringLiteral("Copyright (C) 2026 PTHK - PTHK Karaoke. All rights reserved.");
}

// ---- 1. License & Security Engine ----
bool isProUnlocked()
{
    return true;
}

QString hardwareId()
{
    return ProLicense::hardwareId();
}

QString proStatusText()
{
    return QStringLiteral("PRO");
}

bool activateLicense(const QString &key)
{
    return ProLicense::activate(key);
}

QString ownerMachineId()
{
    return KmSf::thisMachineId();
}

void showLicenseDialog(QWidget *parent)
{
    ProLicense::Dialog dlg(parent);
    dlg.exec();
}

// ---- 2. SoundFont Protection (KMSF) & USB Dongles ----
QVector<UsbDrive> scanUsbDrives(bool force)
{
    QVector<UsbDrive> list;
    for (const UsbDongle::Drive &d : UsbDongle::drives(force)) {
        UsbDrive ud;
        ud.name = d.label();
        ud.id = d.id;
        list.append(ud);
    }
    return list;
}

QString normalizeId(const QString &text)
{
    return KmSf::normalizeId(text);
}

QStringList connectedUsbIds()
{
    return UsbDongle::connectedIds(true);
}

bool isProtectedSoundfont(const QString &filePath)
{
    return KmSf::isProtectedFile(filePath);
}

HSOUNDFONT openProtectedSoundfont(const QString &filePath, DWORD flags)
{
    return KmSf::openFont(filePath, flags);
}

bool lockSoundfont(const QString &src, const QString &dst, const QString &id, bool usb, QString *error)
{
    return KmSf::protectFile(src, dst, id, error, usb);
}

// ---- 3. Studio Mastering & DSP Engine ----
static MasterDsp::Chain g_chain;

void initMasterDsp(int channels, int sampleRate)
{
    g_chain.prepare(channels, sampleRate);
}

void processMasterDsp(float *samples, size_t frames, float outGain)
{
    g_chain.process(samples, frames, outGain);
}

void setMasterParams(const MasterParams &p)
{
    MasterDsp::Params np;
    np.masterOn   = p.masterOn;
    np.compAmount = p.compAmount;
    np.lowDb      = p.lowDb;
    np.midDb      = p.midDb;
    np.highDb     = p.highDb;
    np.exciter    = p.exciter;
    np.width      = p.width;
    np.autoOn     = p.autoOn;
    np.targetLufs = p.targetLufs;
    np.maxBoostDb = p.maxBoostDb;
    np.maxCutDb   = p.maxCutDb;
    np.meter      = p.meter;
    np.limOn      = p.limOn;
    np.ceilingDb  = p.ceilingDb;
    np.releaseMs  = p.releaseMs;
    np.softClip   = p.softClip;
    np.truePeak   = p.truePeak;
    g_chain.setParams(np);
}

MasterParams masterParams()
{
    const MasterDsp::Params p = g_chain.params();
    MasterParams r;
    r.masterOn   = p.masterOn;
    r.compAmount = p.compAmount;
    r.lowDb      = p.lowDb;
    r.midDb      = p.midDb;
    r.highDb     = p.highDb;
    r.exciter    = p.exciter;
    r.width      = p.width;
    r.autoOn     = p.autoOn;
    r.targetLufs = p.targetLufs;
    r.maxBoostDb = p.maxBoostDb;
    r.maxCutDb   = p.maxCutDb;
    r.meter      = p.meter;
    r.limOn      = p.limOn;
    r.ceilingDb  = p.ceilingDb;
    r.releaseMs  = p.releaseMs;
    r.softClip   = p.softClip;
    r.truePeak   = p.truePeak;
    return r;
}

MasterMeter masterMeter()
{
    const MasterDsp::Meter m = g_chain.meter();
    MasterMeter r;
    r.momentary = m.momentary;
    r.shortTerm = m.shortTerm;
    r.autoDb    = m.autoDb;
    r.grDb      = m.grDb;
    r.peakDb    = m.peakDb;
    return r;
}

// ---- 4. AI Vocal Engine ----
AiVocalEngine* createAiVocalEngine(const QString &modelsDir)
{
    return new AiVocalEngine(modelsDir);
}

void destroyAiVocalEngine(AiVocalEngine *engine)
{
    delete engine;
}

// ---- 5. EMK Karaoke Engine ----
bool isEmkFile(const QString &filePath)
{
    return filePath.endsWith(QLatin1String(".emk"), Qt::CaseInsensitive);
}

EmkSong readEmk(const QString &filePath)
{
    EmkSong song;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return song;

    const QByteArray raw = file.readAll();
    file.close();

    if (raw.size() < 64)
        return song;

    // XOR Decrypt using eXtreme key
    static const unsigned char xorKey[8] = { 0xaf, 0xf2, 0x4c, 0x9c, 0xe9, 0xea, 0x99, 0x43 };
    QByteArray dec(raw.size(), 0);
    const char *src = raw.constData();
    char *dst = dec.data();
    for (int i = 0; i < raw.size(); ++i) {
        dst[i] = src[i] ^ (char)xorKey[i % 8];
    }

    if (!dec.startsWith(".SFDS"))
        return song;

    QTextCodec *tis620 = QTextCodec::codecForName("TIS-620");
    if (!tis620) tis620 = QTextCodec::codecForName("Windows-874");

    const int maxExpected = 10 * 1024 * 1024; // 10 MB limit for safety
    int offset = 0;

    while (offset < dec.size() - 2) {
        const uchar b0 = (uchar)dec.at(offset);
        const uchar b1 = (uchar)dec.at(offset + 1);

        if (b0 == 0x78 && (b1 == 0x01 || b1 == 0x9c || b1 == 0xda || b1 == 0x5e)) {
            QByteArray chunkToDecompress;
            chunkToDecompress.reserve(4 + dec.size() - offset);
            chunkToDecompress.append((char)((maxExpected >> 24) & 0xff));
            chunkToDecompress.append((char)((maxExpected >> 16) & 0xff));
            chunkToDecompress.append((char)((maxExpected >> 8) & 0xff));
            chunkToDecompress.append((char)(maxExpected & 0xff));
            chunkToDecompress.append(dec.constData() + offset, dec.size() - offset);

            const QByteArray uncomp = qUncompress(chunkToDecompress);
            if (!uncomp.isEmpty()) {
                if (uncomp.startsWith("SIGNATURE=EMK")) {
                    song.valid = true;
                } else if (uncomp.contains("TITLE=") || uncomp.contains("CODE=")) {
                    const QString metaStr = tis620 ? tis620->toUnicode(uncomp) : QString::fromLatin1(uncomp);
                    const QStringList lines = metaStr.split(QRegExp("[\r\n]+"), Qt::SkipEmptyParts);
                    for (const QString &line : lines) {
                        const int eq = line.indexOf('=');
                        if (eq > 0) {
                            const QString k = line.left(eq).trimmed();
                            const QString v = line.mid(eq + 1).trimmed();
                            if (k == QLatin1String("CODE")) song.code = v;
                            else if (k == QLatin1String("TITLE")) song.title = v;
                            else if (k == QLatin1String("ARTIST")) song.artist = v;
                            else if (k == QLatin1String("KEY")) song.key = v;
                            else if (k == QLatin1String("TEMPO")) song.tempo = v.toInt();
                        }
                    }
                } else if (uncomp.startsWith("MThd")
                           || (song.midData.isEmpty() && uncomp.size() > 22
                               && uncomp.mid(4, 4) == QByteArray("\x00\x00\x00\x06", 4)
                               && uncomp.mid(14, 4) == "MTrk")) {
                    // Many EMK files scramble the 4-byte "MThd" tag of the MIDI (e.g. "X&\"\xa6");
                    // the rest is a normal MIDI (header length 6, first "MTrk" at byte 14).
                    song.midData = uncomp;
                    if (!song.midData.startsWith("MThd"))
                        song.midData.replace(0, 4, "MThd");
                } else if (!song.midData.isEmpty() && song.lyrData.isEmpty()) {
                    song.lyrData = uncomp;
                } else if (!song.lyrData.isEmpty() && song.curData.isEmpty()) {
                    song.curData = uncomp;
                    break;
                }

                offset += 2;
                continue;
            }
        }
        offset++;
    }

    if (!song.midData.isEmpty() && !song.lyrData.isEmpty())
        song.valid = true;

    return song;
}

// ---- 6. SIB Karaoke Engine (AES-256-CBC + ZIP) ----
bool isSibFile(const QString &filePath)
{
    return filePath.endsWith(QLatin1String(".sib"), Qt::CaseInsensitive);
}

static QByteArray decryptSibAES(const QByteArray &cipherData)
{
#ifdef Q_OS_WIN
    if (cipherData.size() < 16)
        return QByteArray();

    static const unsigned char keyBytes[32] = {
        'S','i','b','s','i','b','@','K','A','R','A','O','K','E','_','P',
        'r','o','#','2','0','2','6','!','G','l','o','b','a','l','$','$'
    };
    static const unsigned char ivBytes[16] = {
        'S','i','b','#','2','0','2','6','!','I','n','i','t','V','e','c'
    };

    BCRYPT_ALG_HANDLE hAlg = NULL;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
    if (status != 0)
        return QByteArray();

    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    if (status != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return QByteArray();
    }

    BCRYPT_KEY_HANDLE hKey = NULL;
    DWORD keyObjSize = 0, cbResult = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&keyObjSize, sizeof(DWORD), &cbResult, 0);
    QByteArray keyObj;
    keyObj.resize((int)keyObjSize);

    status = BCryptGenerateSymmetricKey(hAlg, &hKey, (PUCHAR)keyObj.data(), keyObjSize, (PUCHAR)keyBytes, 32, 0);
    if (status != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return QByteArray();
    }

    unsigned char ivBuf[16];
    memcpy(ivBuf, ivBytes, 16);

    DWORD decryptedSize = 0;
    status = BCryptDecrypt(hKey, (PUCHAR)cipherData.constData(), (ULONG)cipherData.size(), NULL,
                           ivBuf, 16, NULL, 0, &decryptedSize, BCRYPT_BLOCK_PADDING);

    QByteArray plain;
    if (status == 0 && decryptedSize > 0) {
        plain.resize((int)decryptedSize);
        memcpy(ivBuf, ivBytes, 16);
        DWORD actualDec = 0;
        status = BCryptDecrypt(hKey, (PUCHAR)cipherData.constData(), (ULONG)cipherData.size(), NULL,
                               ivBuf, 16, (PUCHAR)plain.data(), (ULONG)plain.size(), &actualDec, BCRYPT_BLOCK_PADDING);
        if (status == 0 && actualDec > 0) {
            plain.resize((int)actualDec);
        } else {
            plain.clear();
        }
    }

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return plain;
#else
    Q_UNUSED(cipherData);
    return QByteArray();
#endif
}

SibSong readSibData(const QByteArray &data)
{
    SibSong song;
    if (data.size() < 16)
        return song;

    QByteArray cipherData;
    if (data.startsWith("SIBAES")) {
        cipherData = data.mid(6);
    } else {
        cipherData = data;
    }

    QByteArray plain = decryptSibAES(cipherData);
    if (plain.size() < 30 || !plain.startsWith("PK"))
        return song;

    QBuffer buf(&plain);
    buf.open(QIODevice::ReadOnly);
    QZipReader zip(&buf);
    if (!zip.isReadable())
        return song;

    const auto entries = zip.fileInfoList();
    for (const auto &e : entries) {
        const QString lower = e.filePath.toLower();
        if (lower.endsWith(".mid") || lower.endsWith(".midi")) {
            song.midData = zip.fileData(e.filePath);
            if (song.code.isEmpty())
                song.code = QFileInfo(e.filePath).completeBaseName();
        } else if (lower.endsWith(".lyr")) {
            song.lyrData = zip.fileData(e.filePath);
        } else if (lower.endsWith(".cur")) {
            song.curData = zip.fileData(e.filePath);
        }
    }

    if (!song.lyrData.isEmpty()) {
        QTextCodec *tis620 = QTextCodec::codecForName("TIS-620");
        if (!tis620) tis620 = QTextCodec::codecForName("Windows-874");
        const QString lyrText = tis620 ? tis620->toUnicode(song.lyrData) : QString::fromLatin1(song.lyrData);
        const QStringList lines = lyrText.split(QRegExp("[\r\n]+"), Qt::SkipEmptyParts);
        if (lines.size() > 0) song.title = lines[0].trimmed();
        if (lines.size() > 1) song.artist = lines[1].trimmed();
        if (lines.size() > 2) song.key = lines[2].trimmed();
    }

    if (!song.midData.isEmpty() && !song.lyrData.isEmpty())
        song.valid = true;

    return song;
}

SibSong readSib(const QString &filePath)
{
    SibSong song;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return song;
    const QByteArray raw = file.readAll();
    file.close();
    song = readSibData(raw);
    if (song.code.isEmpty())
        song.code = QFileInfo(filePath).completeBaseName();
    return song;
}

} // namespace PTHKCore
