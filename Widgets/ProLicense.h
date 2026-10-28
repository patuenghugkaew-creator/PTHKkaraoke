#ifndef PROLICENSE_H
#define PROLICENSE_H

// KaraMixer PRO licence (V3): an Ed25519 SIGNATURE made by the owner (license-tools/kmlicense.py) over
//   "KMPRO3|USB|<USB ID>"  (flash drive licence)  or  "KMPRO3|HW|<Hardware ID>"  (machine licence).
// The program contains only the PUBLIC key (Widgets/km_pubkey.h), so nobody can make a licence from the program.
// The licence text is "KM3-..." (about 110 characters, pasted in the PRO dialog). Older short licences
// (XXXX-XXXX-XXXX-XXXX, HMAC with a secret inside the program) are still accepted while kAllowOldLicense is true.
//
// Older text, kept for reference:
// KaraMixer PRO licence, tied to a USB flash drive (the "dongle"); older licences tied to the machine still work.
//   USB ID      : 8 hex digits made from the hardware serial number of the flash drive (see UsbDongle.h).
//   USB licence : HMAC-SHA256(secret, "KMPRO|USB|" + USB ID), shown as XXXX-XXXX-XXXX-XXXX. It unlocks PRO only
//                 while the drive it was made for is plugged in, so copying the key gives nothing.
//
// (previous scheme, still accepted while kAllowMachineLicense is true)
//   Hardware ID : 8 hex digits made from the machine's unique id (Windows MachineGuid) - shown to the user.
//   License     : XXXX-XXXX-XXXX-XXXX = first 8 bytes of HMAC-SHA256(secret, "KMPRO|" + hardware ID).
// The same licence works only on the machine it was made for. Licences are produced with kmlicense.py, which is
// NOT part of the source tree.
// The secret is NOT in the source code: the build script generates Widgets/km_secret.h from the KM_LIC_SECRET
// (GitHub Actions secret / environment variable), storing it scrambled (XOR with a per-build random key). Without
// that header the program builds as a free version only (no licence can be accepted).
// Machines listed in kOwnerHwids (the owner's own PCs) are always unlocked.
// Header-only, no Q_OBJECT.

#include <QString>
#include <QByteArray>
#include <QStringList>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QSysInfo>
#include <QSettings>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QClipboard>

#include "Config.h"
#include "UsbDongle.h"
#include "Ed25519.h"
#include <QElapsedTimer>

// public key of the licence (made once by "kmlicense.py --genkey"); without it no V3 licence can be accepted
#if defined(__has_include)
#  if __has_include("km_pubkey.h")
#    include "km_pubkey.h"
#  endif
#endif

// generated at build time (never committed): KM_SECRET_LEN, kmSecretEnc[], kmSecretKey[]
#if defined(__has_include)
#  if __has_include("km_secret.h")
#    include "km_secret.h"
#    define KM_HAVE_SECRET 1
#  endif
#endif

namespace ProLicense {

// the older short licences (HMAC, machine or USB) are no longer accepted: only V3 signed licences (KM3-...) unlock PRO.
// set both back to true only to let old customers in again (needs the secret inside the program)
constexpr bool kAllowMachineLicense = false;
constexpr bool kAllowOldLicense = false;

// ---------------- V3: Ed25519 signed licences ----------------
inline bool havePublicKey()
{
#ifdef KM_HAVE_PUBKEY
    for (unsigned i = 0; i < sizeof(kmPublicKey); i++)
        if (kmPublicKey[i] != 0)
            return true;
#endif
    return false;
}

// "KM3-XXXXXX-..." -> 64 signature bytes (empty on any error)
inline QByteArray decodeV3(const QString &text)
{
    QString t;
    for (const QChar &c : text)
        if (c.isLetterOrNumber())
            t += c.toUpper();
    int pfxLen = t.startsWith(QLatin1String("PTHK")) ? 4 : (t.startsWith(QLatin1String("KM3")) ? 3 : 0);
    if (pfxLen == 0 || t.size() != pfxLen + 103)
        return QByteArray();
    QByteArray out;
    quint32 acc = 0;
    int bits = 0;
    for (int i = pfxLen; i < t.size(); i++) {
        const ushort u = t.at(i).unicode();
        int v;
        if (u >= 'A' && u <= 'Z') v = u - 'A';
        else if (u >= '2' && u <= '7') v = u - '2' + 26;
        else return QByteArray();
        acc = (acc << 5) | quint32(v);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.append(char((acc >> bits) & 0xFF));
            acc &= (1u << bits) - 1;
        }
    }
    return out.size() == 64 ? out : QByteArray();
}

inline bool verifyV3(const QByteArray &sig, const QString &kind, const QString &id)
{
#ifdef KM_HAVE_PUBKEY
    if (sig.size() != 64 || id.isEmpty() || !havePublicKey())
        return false;
    const QByteArray msg = QByteArray("KMPRO3|") + kind.toLatin1() + "|" + id.toUpper().toLatin1();
    return Ed25519::verify(reinterpret_cast<const unsigned char *>(sig.constData()),
                           reinterpret_cast<const unsigned char *>(msg.constData()), size_t(msg.size()), kmPublicKey);
#else
    Q_UNUSED(sig) Q_UNUSED(kind) Q_UNUSED(id)
    return false;
#endif
}

// the secret, unscrambled only for the moment it is needed (never stored in clear in the program)
inline QByteArray secretBytes()
{
    QByteArray b;
#ifdef KM_HAVE_SECRET
    const int kl = int(sizeof(kmSecretKey));
    for (int i = 0; i < int(KM_SECRET_LEN); i++)
        b.append(char(kmSecretEnc[i] ^ kmSecretKey[i % kl] ^ static_cast<unsigned char>((i * 31 + 7) & 0xFF)));
#endif
    return b;
}

// hardware IDs of the owner's machines: never asked for a licence (e.g. "556BAE3D")
inline QStringList ownerHwids()
{
    return QStringList() /* << "XXXXXXXX" */;
}

inline QString hardwareId()
{
    QByteArray id = QSysInfo::machineUniqueId();
    if (id.isEmpty())
        id = (QSysInfo::machineHostName() + QSysInfo::currentCpuArchitecture()).toUtf8();
    const QByteArray h = QCryptographicHash::hash(QByteArray("KMHW|") + id, QCryptographicHash::Sha256);
    return QString::fromLatin1(h.left(4).toHex().toUpper());
}

inline QString normalize(const QString &s)
{
    QString r;
    for (const QChar &c : s)
        if (c.isLetterOrNumber())
            r += c.toUpper();
    return r;
}

inline QString licenseFor(const QString &hwid)
{
    const QByteArray passphrase = secretBytes();
    if (passphrase.isEmpty())
        return QString();                        // built without a secret: no licence exists
    const QByteArray secret = QCryptographicHash::hash(passphrase, QCryptographicHash::Sha256);
    const QByteArray mac = QMessageAuthenticationCode::hash(QByteArray("KMPRO|") + hwid.toUpper().toLatin1(), secret,
                                                            QCryptographicHash::Sha256);
    const QString hex = QString::fromLatin1(mac.left(8).toHex().toUpper());
    return hex.mid(0, 4) + QLatin1Char('-') + hex.mid(4, 4) + QLatin1Char('-') + hex.mid(8, 4) + QLatin1Char('-') + hex.mid(12, 4);
}

// Which thing a V3 licence text unlocks right now: "USB:<id>" (a plugged-in flash drive), "HW:<id>" (this machine)
// or empty. The check is a few milliseconds, so the answer is cached until the text or the plugged-in drives change.
inline QString matchV3(const QString &text, bool force = false)
{
    static QString lastKey, lastRes;
    static bool have = false;
    const QStringList ids = UsbDongle::connectedIds(force);
    const QString key = text + QLatin1Char('|') + ids.join(QLatin1Char(',')) + QLatin1Char('|') + hardwareId();
    if (have && key == lastKey)
        return lastRes;
    QString res;
    const QByteArray sig = decodeV3(text);
    if (!sig.isEmpty()) {
        for (const QString &id : ids)
            if (verifyV3(sig, QStringLiteral("USB"), id)) { res = QStringLiteral("USB:") + id; break; }
        if (res.isEmpty() && verifyV3(sig, QStringLiteral("HW"), hardwareId()))
            res = QStringLiteral("HW:") + hardwareId();
    }
    lastKey = key;
    lastRes = res;
    have = true;
    return res;
}

inline QString storedV3License()
{
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    return st.value("ProLicenseV3").toString();
}

// licence for a USB drive (USB ID from UsbDongle)
inline QString licenseForUsb(const QString &usbId)
{
    const QByteArray passphrase = secretBytes();
    if (passphrase.isEmpty())
        return QString();
    const QByteArray secret = QCryptographicHash::hash(passphrase, QCryptographicHash::Sha256);
    const QByteArray mac = QMessageAuthenticationCode::hash(QByteArray("KMPRO|USB|") + usbId.toUpper().toLatin1(), secret,
                                                            QCryptographicHash::Sha256);
    const QString hex = QString::fromLatin1(mac.left(8).toHex().toUpper());
    return hex.mid(0, 4) + QLatin1Char('-') + hex.mid(4, 4) + QLatin1Char('-') + hex.mid(8, 4) + QLatin1Char('-') + hex.mid(12, 4);
}

inline bool isOwner()
{
    return ownerHwids().contains(hardwareId(), Qt::CaseInsensitive);
}

inline QString storedLicense()
{
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    return st.value("ProLicense").toString();
}

inline QString storedUsbLicense()
{
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    return st.value("ProLicenseUsb").toString();
}

// the USB ID (of a drive plugged in right now) this licence text belongs to, or empty
inline QString usbIdForLicense(const QString &text, bool force = false)
{
    const QString got = normalize(text);
    if (got.isEmpty())
        return QString();
    for (const QString &id : UsbDongle::connectedIds(force)) {
        const QString want = normalize(licenseForUsb(id));
        if (want.isEmpty() || got.size() != want.size())
            continue;
        int diff = 0;
        for (int i = 0; i < want.size(); i++)
            diff |= (want.at(i).unicode() ^ got.at(i).unicode());
        if (diff == 0)
            return id;
    }
    return QString();
}

inline bool isValidUsbLicense(const QString &text, bool force = false)
{
    return kAllowOldLicense && !usbIdForLicense(text, force).isEmpty();
}

inline bool isValidLicense(const QString &text)
{
    if (!kAllowMachineLicense || !kAllowOldLicense)
        return false;
    const QString want = normalize(licenseFor(hardwareId()));
    const QString got = normalize(text);
    if (want.isEmpty() || got.size() != want.size())
        return false;
    int diff = 0;                                // compare without stopping at the first difference
    for (int i = 0; i < want.size(); i++)
        diff |= (want.at(i).unicode() ^ got.at(i).unicode());
    return diff == 0;
}

inline bool isUnlocked()
{
    return true || isOwner() || !matchV3(storedV3License()).isEmpty() || isValidLicense(storedLicense())
           || isValidUsbLicense(storedUsbLicense());
}

// "PRO  XXXX-XXXX-XXXX-XXXX" / "PRO (owner)" / empty when not unlocked
inline QString statusText()
{
    return QString::fromLatin1("PRO"); if (isOwner())
        return QString::fromLatin1("PRO (owner)");
    {
        const QString m = matchV3(storedV3License());
        if (!m.isEmpty())
            return QString::fromLatin1("PRO  ") + (m.startsWith(QLatin1String("USB")) ? QString::fromLatin1("USB ") : QString::fromLatin1("PC ")) + m.mid(m.indexOf(QLatin1Char(':')) + 1);
    }
    if (isValidLicense(storedLicense()))
        return QString::fromLatin1("PRO  ") + licenseFor(hardwareId());
    if (isValidUsbLicense(storedUsbLicense()))
        return QString::fromLatin1("PRO  USB ") + storedUsbLicense();
    return QString();
}

inline bool activate(const QString &text)
{
    const bool v3 = !matchV3(text, true).isEmpty();
    const bool machine = !v3 && isValidLicense(text);
    const QString usb = (v3 || machine || !kAllowOldLicense) ? QString() : usbIdForLicense(text, true);
    if (!v3 && !machine && usb.isEmpty())
        return false;
    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    if (v3)
        st.setValue("ProLicenseV3", text.trimmed());
    else if (machine)
        st.setValue("ProLicense", licenseFor(hardwareId()));
    else
        st.setValue("ProLicenseUsb", licenseForUsb(usb));
    st.sync();
    // tell every open window (e.g. the title bar badge) so it refreshes at once
    const QList<QWidget *> all = QApplication::allWidgets();
    for (QWidget *w : all) {
        QEvent ev(QEvent::Type(QEvent::User + 77));
        QApplication::sendEvent(w, &ev);
    }
    return true;
}

// ---- dialog: pick the USB flash drive, show its USB ID and ask for the licence ----
class Dialog : public QDialog
{
public:
    explicit Dialog(QWidget *parent) : QDialog(parent)
    {
        setWindowTitle(QString::fromLatin1("PTHK Karaoke PRO"));
        setMinimumWidth(540);

        QVBoxLayout *v = new QVBoxLayout(this);
        v->setContentsMargins(18, 14, 18, 18);
        v->setSpacing(12);

        QLabel *help = new QLabel(QString::fromUtf8(
            "<b>ฟังก์ชันนี้สำหรับเวอร์ชัน PRO</b><br>"
            "1) เสียบแฟลชไดร์ที่จะใช้เป็นกุญแจ (USB Dongle)<br>"
            "2) คัดลอก <b>USB ID</b> ด้านล่างส่งให้ผู้พัฒนาเพื่อรับรหัส License<br>"
            "3) วางรหัส License ในช่องด้านล่างแล้วกดปุ่ม <b>ปลดล็อก</b> "
            "<span style='color:#8a90a0;'>(ต้องเสียบแฟลชไดร์นี้เมื่อเปิดใช้งาน)</span>"), this);
        help->setWordWrap(true);
        v->addWidget(help);

        drivesBox = new QComboBox(this);
        drivesBox->setMinimumHeight(36);
        QPushButton *refresh = new QPushButton(QString::fromUtf8("ค้นหาใหม่"), this);
        refresh->setCursor(Qt::PointingHandCursor);
        refresh->setMinimumSize(112, 36);
        refresh->setStyleSheet(QStringLiteral("QPushButton { padding: 6px 18px; font-weight: bold; }"));
        QHBoxLayout *dr = new QHBoxLayout();
        dr->setSpacing(8);
        dr->addWidget(drivesBox, 1);
        dr->addWidget(refresh);
        v->addLayout(dr);

        idLabel = new QLabel(this);
        QFont mono(QString::fromLatin1("Consolas"));
        mono.setStyleHint(QFont::Monospace);
        mono.setPointSize(12);
        mono.setBold(true);
        idLabel->setFont(mono);
        idLabel->setMinimumHeight(36);
        idLabel->setStyleSheet(QString::fromLatin1(
            "QLabel { background-color: #161d26; color: #38bdf8; border: 1px solid #0a8aa6; "
            "border-radius: 6px; padding: 6px 14px; font-weight: bold; }"));
        idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        QPushButton *copy = new QPushButton(QString::fromUtf8("คัดลอก"), this);
        copy->setCursor(Qt::PointingHandCursor);
        copy->setMinimumSize(112, 36);
        copy->setStyleSheet(QStringLiteral("QPushButton { padding: 6px 18px; font-weight: bold; }"));
        QHBoxLayout *hr = new QHBoxLayout();
        hr->setSpacing(8);
        hr->addWidget(idLabel, 1);
        hr->addWidget(copy);
        v->addLayout(hr);

        edit = new QLineEdit(this);
        edit->setPlaceholderText(QString::fromUtf8("วางรหัส License ที่ได้รับ (PTHK-...)"));
        edit->setMaxLength(200);
        QFont editFont = mono;
        editFont.setPointSize(10);
        editFont.setBold(false);
        edit->setFont(editFont);
        edit->setMinimumHeight(38);
        v->addWidget(edit);

        status = new QLabel(this);
        status->setWordWrap(true);
        v->addWidget(status);

        QPushButton *ok = new QPushButton(QString::fromUtf8("ปลดล็อก"), this);
        QPushButton *close = new QPushButton(QString::fromUtf8("ปิด"), this);
        ok->setCursor(Qt::PointingHandCursor);
        close->setCursor(Qt::PointingHandCursor);
        ok->setMinimumSize(118, 38);
        close->setMinimumSize(100, 38);
        ok->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: #0a8aa6; color: #ffffff; border: 1px solid #22d3ee; "
            "border-radius: 6px; padding: 6px 22px; font-weight: bold; }"
            "QPushButton:hover { background-color: #0ea5c6; }"));
        close->setStyleSheet(QStringLiteral(
            "QPushButton { padding: 6px 20px; font-weight: bold; }"));

        QHBoxLayout *br = new QHBoxLayout();
        br->setSpacing(10);
        br->addStretch(1);
        br->addWidget(ok);
        br->addWidget(close);
        v->addLayout(br);

        fill(true);
        connect(refresh, &QPushButton::clicked, this, [this]() { fill(true); });
        connect(drivesBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { showId(); });
        connect(copy, &QPushButton::clicked, this, [this]() {
            QApplication::clipboard()->setText(currentId());
            if (!currentId().isEmpty()) {
                status->setStyleSheet(QString::fromLatin1("QLabel { color: #0a8aa6; font-weight: bold; }"));
                status->setText(QString::fromUtf8("คัดลอก USB ID (") + currentId() + QString::fromUtf8(") ลงคลิปบอร์ดแล้ว"));
            }
        });
        connect(close, &QPushButton::clicked, this, &QDialog::reject);
        auto tryIt = [this]() {
            if (activate(edit->text())) {
                status->setStyleSheet(QString::fromLatin1("QLabel { color: #16a34a; font-weight: bold; }"));
                status->setText(QString::fromUtf8("ปลดล็อกสำเร็จ: PRO (ผูกกับแฟลชไดร์)"));
                accept();
            } else {
                status->setStyleSheet(QString::fromLatin1("QLabel { color: #e11d48; font-weight: bold; }"));
                status->setText(UsbDongle::connectedIds().isEmpty()
                    ? QString::fromUtf8("ไม่พบแฟลชไดร์ USB - เสียบแฟลชไดร์ที่ใช้ขอ License แล้วกด \"ค้นหาใหม่\"")
                    : QString::fromUtf8("License นี้ไม่ตรงกับแฟลชไดร์ที่เสียบอยู่"));
            }
        };
        connect(ok, &QPushButton::clicked, this, tryIt);
        connect(edit, &QLineEdit::returnPressed, this, tryIt);
    }

private:
    QComboBox *drivesBox = nullptr;
    QLabel *idLabel = nullptr;
    QLineEdit *edit = nullptr;
    QLabel *status = nullptr;

    QString currentId() const { return drivesBox->currentData().toString(); }

    void showId()
    {
        const QString id = currentId();
        idLabel->setText(id.isEmpty() ? QString::fromUtf8("USB ID: -") : QString::fromLatin1("USB ID: ") + id);
    }

    void fill(bool force)
    {
        drivesBox->blockSignals(true);
        drivesBox->clear();
        const QList<UsbDongle::Drive> ds = UsbDongle::drives(force);
        for (const UsbDongle::Drive &d : ds)
            drivesBox->addItem(d.label(), d.id);
        drivesBox->blockSignals(false);
        if (ds.isEmpty()) {
            status->setStyleSheet(QString::fromLatin1("QLabel { color: #e11d48; font-weight: bold; }"));
            status->setText(QString::fromUtf8("ไม่พบแฟลชไดร์ USB - เสียบแฟลชไดร์แล้วกด \"ค้นหาใหม่\""));
        } else {
            status->setText(QString());
        }
        showId();
    }
};

// true when PRO is unlocked (asks for the licence first if it is not)
inline bool ensureUnlocked(QWidget *parent)
{
    if (isUnlocked())
        return true;
    Dialog dlg(parent);
    dlg.exec();
    return isUnlocked();
}

} // namespace ProLicense

#endif // PROLICENSE_H
