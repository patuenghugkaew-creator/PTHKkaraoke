// Automatic update from GitHub Releases.
// The newest release of the project holds the installer (…-x64-setup.exe) and, in its notes,
// a line "SHA256: <hex>" with the installer's checksum. HTTPS goes through WinHTTP (part of
// Windows), so no OpenSSL files are needed next to the program.
#ifndef UPDATER_H
#define UPDATER_H

#include "version.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QVersionNumber>

#include <atomic>
#include <functional>

#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

namespace Updater {

static const char *const kLatestApi =
    "https://api.github.com/repos/patuenghugkaew-creator/PTHKkaraoke/releases/latest";

struct Release {
    bool ok = false;          // the request worked (false: no internet / no release yet)
    int httpStatus = 0;
    QString version;          // "1.2.0"
    QString notes;            // release notes (shown to the user)
    QString assetUrl;         // installer download link
    QString assetName;
    qint64 assetSize = 0;
    QString sha256;           // lower-case hex, empty when the notes have none
};

// "v.1.1.0" -> "1.1.0"
inline QString currentVersion()
{
    QString v = QString::fromLatin1(VER_PRODUCTVERSION_STR);
    v.remove(QRegularExpression("[^0-9.]"));
    while (v.startsWith('.')) v.remove(0, 1);
    return v;
}

inline bool isNewer(const QString &remote, const QString &local)
{
    return QVersionNumber::fromString(remote) > QVersionNumber::fromString(local);
}

// GET a URL. When 'out' is given the body is written there (with progress), otherwise to 'body'.
// Redirects (GitHub -> its file server) are followed by WinHTTP.
inline int httpGet(const QString &url, QByteArray *body, QFile *out = nullptr,
                   const std::function<void(qint64, qint64)> &progress = nullptr,
                   const std::atomic<bool> *cancel = nullptr)
{
    URL_COMPONENTS uc;
    ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {0}, path[2048] = {0};
    uc.lpszHostName = host;  uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;   uc.dwUrlPathLength = 2047;
    wchar_t extra[2048] = {0};
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2047;
    const std::wstring wurl = url.toStdWString();
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
        return 0;
    const std::wstring fullPath = std::wstring(path) + extra;

    int status = 0;
    HINTERNET ses = WinHttpOpen(L"PTHK-Karaoke-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses)   // Windows 7/8: no automatic proxy type
        ses = WinHttpOpen(L"PTHK-Karaoke-Updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses)
        return 0;
    WinHttpSetTimeouts(ses, 10000, 15000, 30000, 60000);
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", fullPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                        : nullptr;
    if (req) {
        // TLS 1.2 on older Windows
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        if (!WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
        }
        const wchar_t *hdr = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
        if (WinHttpSendRequest(req, hdr, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                && WinHttpReceiveResponse(req, nullptr)) {
            DWORD code = 0, sz = sizeof(code);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
            status = int(code);
            qint64 total = -1;
            {
                wchar_t len[32] = {0}; DWORD lsz = sizeof(len);
                if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                        len, &lsz, WINHTTP_NO_HEADER_INDEX))
                    total = QString::fromWCharArray(len).toLongLong();
            }
            qint64 got = 0;
            QByteArray buf;
            for (;;) {
                if (cancel && cancel->load()) { status = -1; break; }
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(req, &avail)) { status = 0; break; }
                if (avail == 0)
                    break;
                buf.resize(int(avail));
                DWORD read = 0;
                if (!WinHttpReadData(req, buf.data(), avail, &read)) { status = 0; break; }
                if (read == 0)
                    break;
                if (out) {
                    if (out->write(buf.constData(), qint64(read)) != qint64(read)) { status = 0; break; }
                } else if (body) {
                    body->append(buf.constData(), int(read));
                }
                got += read;
                if (progress) progress(got, total);
            }
        }
        WinHttpCloseHandle(req);
    }
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return status;
}

// Asks GitHub for the newest release (runs a few seconds at most; call off the UI thread)
inline Release fetchLatest()
{
    Release r;
    QByteArray body;
    r.httpStatus = httpGet(QString::fromLatin1(kLatestApi), &body);
    if (r.httpStatus != 200)
        return r;
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    if (o.isEmpty())
        return r;
    QString tag = o.value("tag_name").toString();
    tag.remove(QRegularExpression("[^0-9.]"));
    while (tag.startsWith('.')) tag.remove(0, 1);
    r.version = tag;
    r.notes = o.value("body").toString();
    const QRegularExpressionMatch m = QRegularExpression("SHA-?256\\s*[:=]\\s*([0-9A-Fa-f]{64})").match(r.notes);
    if (m.hasMatch())
        r.sha256 = m.captured(1).toLower();
    for (const QJsonValue &v : o.value("assets").toArray()) {
        const QJsonObject a = v.toObject();
        const QString name = a.value("name").toString();
        if (name.endsWith("-x64-setup.exe", Qt::CaseInsensitive)) {
            r.assetName = name;
            r.assetUrl = a.value("browser_download_url").toString();
            r.assetSize = qint64(a.value("size").toDouble());
            break;
        }
    }
    r.ok = !r.version.isEmpty() && !r.assetUrl.isEmpty();
    return r;
}

inline QString fileSha256(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

} // namespace Updater

#endif // UPDATER_H
