#include "Remote/SongRequestServer.h"
#include "Dialogs/YouTube.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QUrlQuery>

static const int kMaxRequest = 16 * 1024;
static const int kMaxPendingPerPhone = 3;

// one page for customers and the owner (the owner also sees the requests to approve)
static const char *kPage1 = R"HTML(<!doctype html>
<html lang="th"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ขอเพลง - PTHK Karaoke</title>
<style>
:root{--bg:#14161c;--card:#1f232c;--line:#2e3442;--fg:#eef1f6;--mut:#9aa3b5;--acc:#3a7bd5;--ok:#2e9e5b;--no:#c94a4a}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px system-ui,"Leelawadee UI",Tahoma,sans-serif}
header{padding:14px 16px;background:#0e1015;border-bottom:1px solid var(--line);display:flex;gap:10px;align-items:center}
header b{font-size:19px}header span{color:var(--mut);font-size:13px}
main{padding:12px 16px 40px;max-width:640px;margin:auto}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px;margin:12px 0}
h3{margin:0 0 8px;font-size:15px;color:var(--mut);font-weight:600}
input{width:100%;padding:11px 12px;border-radius:10px;border:1px solid var(--line);background:#11141a;color:var(--fg);font-size:16px}
.row{display:flex;gap:8px;margin-top:8px}.row>*{flex:1}
button{padding:10px 12px;border:0;border-radius:10px;background:var(--acc);color:#fff;font-size:15px;cursor:pointer}
button.sec{background:#2b303b}button.ok{background:var(--ok)}button.no{background:var(--no)}
button.on{outline:2px solid #fff}
.item{display:flex;gap:10px;align-items:center;padding:9px 0;border-top:1px solid var(--line)}
.item:first-child{border-top:0}.item .t{flex:1;min-width:0}
.item .t div{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.small{color:var(--mut);font-size:13px}.st{font-size:13px;padding:3px 8px;border-radius:8px;background:#2b303b}
.st.approved{background:var(--ok)}.st.rejected{background:var(--no)}
#toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);background:#000c;padding:10px 16px;border-radius:10px;display:none}
.owner{display:none}body.isowner .owner{display:block}
</style></head><body>
<header><b>PTHK Karaoke</b><span id="role">ขอเพลง</span></header>
<main>
<div class="card owner"><h3>รออนุมัติ</h3><div id="pending" class="small">ยังไม่มีคำขอ</div>
<div class="row"><button class="sec" id="auto" onclick="toggleAuto()">อนุมัติอัตโนมัติ: ปิด</button></div></div>
<div class="card"><h3>ชื่อของคุณ / โต๊ะ</h3><input id="who" placeholder="เช่น โต๊ะ 5 หรือ ชื่อเล่น" maxlength="30"></div>
<div class="card"><h3>ค้นหาเพลง</h3>
<input id="q" placeholder="พิมพ์ชื่อเพลง ชื่อนักร้อง หรือรหัสเพลง" enterkeyhint="search">
<div class="row"><button id="sLib" onclick="search('lib')">ค้นในเครื่อง</button><button id="sYt" class="sec" onclick="search('yt')">ค้นจาก YouTube</button></div>
<div id="res" class="small" style="margin-top:8px"></div></div>
<div class="card"><h3>คำขอของฉัน</h3><div id="mine" class="small">ยังไม่ได้ขอเพลง</div></div>
<div class="card"><h3>กำลังเล่น / คิวถัดไป</h3><div id="queue" class="small">-</div></div>
</main><div id="toast"></div>
)HTML";

static const char *kPage2 = R"HTML(<script>
const K=new URLSearchParams(location.search).get('k');const OWNER=%OWNER%;
if(OWNER){document.body.classList.add('isowner');document.getElementById('role').textContent='เจ้าของร้าน';}
const $=id=>document.getElementById(id);
$('who').value=localStorage.getItem('pthkWho')||'';$('who').oninput=()=>localStorage.setItem('pthkWho',$('who').value);
$('q').onkeydown=e=>{if(e.key==='Enter')search(lastSrc)};
let lastSrc='lib',results=[];
function esc(s){return String(s||'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function toast(t){const e=$('toast');e.textContent=t;e.style.display='block';clearTimeout(e._t);e._t=setTimeout(()=>e.style.display='none',2500)}
function dur(s){return s>0?Math.floor(s/60)+':'+String(s%60).padStart(2,'0'):''}
async function api(path,body){const u='/api/'+path+'?k='+encodeURIComponent(K)+(body&&!body.post?'&'+new URLSearchParams(body):'');
 const r=await fetch(u,body&&body.post?{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(body.post)}:{});return r.json()}
async function search(src){lastSrc=src;$('sLib').className=src==='lib'?'':'sec';$('sYt').className=src==='yt'?'':'sec';
 const q=$('q').value.trim();if(!q)return;$('res').textContent=src==='yt'?'กำลังค้นจาก YouTube...':'กำลังค้นหา...';
 try{const d=await api('search',{q,src});results=d.items||[];
  if(d.msg){$('res').textContent=d.msg;return}
  if(!results.length){$('res').textContent='ไม่พบเพลง';return}
  $('res').innerHTML=results.map((it,i)=>`<div class="item"><div class="t"><div>${esc(it.title)}</div><div class="small">${esc(it.artist)} ${dur(it.seconds)}</div></div><button onclick="ask(${i})">ขอเพลง</button></div>`).join('')
 }catch(e){$('res').textContent='เชื่อมต่อไม่ได้'}}
async function ask(i){const it=results[i];
 const d=await api('request',{post:{key:it.key,title:it.title,artist:it.artist||'',seconds:it.seconds||0,who:$('who').value.trim()}});
 toast(d.msg||'ส่งคำขอแล้ว');refresh()}
async function decide(id,ok){await api('decide',{post:{id,ok:ok?1:0}});refresh()}
async function toggleAuto(){await api('auto',{post:{on:$('auto').dataset.on==='1'?0:1}});refresh()}
const ST={pending:'รออนุมัติ',approved:'เข้าคิวแล้ว',rejected:'ไม่อนุมัติ'};
async function refresh(){try{const d=await api('state');
 $('queue').innerHTML=(d.now?`<div class="item"><div class="t"><div>▶ ${esc(d.now.title)}</div><div class="small">${esc(d.now.artist)}</div></div></div>`:'')+
  ((d.queue||[]).map((s,i)=>`<div class="item"><div class="t"><div>${i+1}. ${esc(s.title)}</div><div class="small">${esc(s.artist)}</div></div></div>`).join('')||(d.now?'':'ยังไม่มีเพลงในคิว'));
 $('mine').innerHTML=(d.mine||[]).map(r=>`<div class="item"><div class="t"><div>${esc(r.title)}</div></div><span class="st ${r.status}">${ST[r.status]}</span></div>`).join('')||'ยังไม่ได้ขอเพลง';
 if(OWNER){$('auto').dataset.on=d.auto?'1':'0';$('auto').textContent='อนุมัติอัตโนมัติ: '+(d.auto?'เปิด':'ปิด');$('auto').className=d.auto?'ok':'sec';
  $('pending').innerHTML=(d.pending||[]).map(r=>`<div class="item"><div class="t"><div>${esc(r.title)}</div><div class="small">${esc(r.who||'ไม่ระบุชื่อ')} · ${esc(r.artist)}</div></div><button class="ok" onclick="decide(${r.id},1)">อนุมัติ</button><button class="no" onclick="decide(${r.id},0)">ไม่</button></div>`).join('')||'ยังไม่มีคำขอ'}
}catch(e){}}
refresh();setInterval(refresh,3000);
</script></body></html>)HTML";

static QString randomToken()
{
    static const char alphabet[] = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    QString t;
    for (int i = 0; i < 12; i++)
        t += QChar(alphabet[QRandomGenerator::global()->bounded(int(sizeof(alphabet)) - 1)]);
    return t;
}

SongRequestServer::SongRequestServer(QObject *parent) : QObject(parent)
{
    clock.start();
    connect(&server, &QTcpServer::newConnection, this, &SongRequestServer::onNewConnection);
}

SongRequestServer::~SongRequestServer()
{
    stop();
}

bool SongRequestServer::start(const QString &ip)
{
    stop();
    ipText = ip;
    custToken = randomToken();
    ownerToken = randomToken();
    for (quint16 p = 8780; p < 8790; p++)
        if (server.listen(QHostAddress::AnyIPv4, p))
            return true;
    return false;
}

void SongRequestServer::stop()
{
    if (server.isListening())
        server.close();
    for (QTcpSocket *s : findChildren<QTcpSocket *>()) {
        s->disconnect(this);
        s->abort();
        s->deleteLater();
    }
    custToken.clear();
    ownerToken.clear();
}

QString SongRequestServer::customerUrl() const
{
    return isRunning() ? QString("http://%1:%2/?k=%3").arg(ipText).arg(server.serverPort()).arg(custToken) : QString();
}

QString SongRequestServer::ownerUrl() const
{
    return isRunning() ? QString("http://%1:%2/?k=%3").arg(ipText).arg(server.serverPort()).arg(ownerToken) : QString();
}

void SongRequestServer::setAutoApprove(bool on)
{
    autoOk = on;
    if (on)                             // what is waiting goes into the queue as well
        for (const Request &r : pending())
            decide(r.id, true);
    emit requestsChanged();
}

QList<SongRequestServer::Request> SongRequestServer::pending() const
{
    QList<Request> out;
    for (const Request &r : requests)
        if (r.status == "pending")
            out << r;
    return out;
}

void SongRequestServer::decide(int id, bool approve)
{
    for (Request &r : requests)
        if (r.id == id && r.status == "pending") {
            r.status = (approve && addToQueue && addToQueue(r)) ? "approved" : "rejected";
            emit requestsChanged();
            return;
        }
}

// ---- HTTP -------------------------------------------------------------------------------------

void SongRequestServer::onNewConnection()
{
    while (server.hasPendingConnections()) {
        QTcpSocket *s = server.nextPendingConnection();
        s->setParent(this);
        connect(s, &QTcpSocket::readyRead, this, [this, s]() { onReadyRead(s); });
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
    }
}

void SongRequestServer::onReadyRead(QTcpSocket *s)
{
    QByteArray buf = s->property("buf").toByteArray() + s->readAll();
    while (!buf.isEmpty()) {
        if (buf.size() > kMaxRequest * 2) { s->abort(); return; }
        const int headEnd = buf.indexOf("\r\n\r\n");
        if (headEnd < 0)
            break;
        const QList<QByteArray> lines = buf.left(headEnd).split('\n');
        const QList<QByteArray> reqLine = lines.value(0).trimmed().split(' ');
        if (reqLine.size() < 2) { s->abort(); return; }
        int contentLength = 0;
        for (int i = 1; i < lines.size(); i++) {
            const QByteArray l = lines[i].trimmed();
            if (l.toLower().startsWith("content-length:"))
                contentLength = l.mid(15).trimmed().toInt();
        }
        if (contentLength < 0 || contentLength > kMaxRequest) { s->abort(); return; }
        const int total = headEnd + 4 + contentLength;
        if (buf.size() < total)
            break;
        const QByteArray body = buf.mid(headEnd + 4, contentLength);
        buf.remove(0, total);
        const QByteArray target = reqLine[1];
        const int q = target.indexOf('?');
        handle(s, reqLine[0], QString::fromLatin1(q >= 0 ? target.left(q) : target),
               q >= 0 ? target.mid(q + 1) : QByteArray(), body);
    }
    s->setProperty("buf", buf);
}

void SongRequestServer::reply(QTcpSocket *s, int code, const QByteArray &contentType, const QByteArray &body)
{
    QByteArray out = "HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK" : code == 403 ? " Forbidden" : " Not Found") + "\r\n";
    out += "Content-Type: " + contentType + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Cache-Control: no-store\r\nConnection: keep-alive\r\n\r\n" + body;
    s->write(out);
}

void SongRequestServer::replyJson(QTcpSocket *s, const QJsonObject &o)
{
    reply(s, 200, "application/json; charset=utf-8", QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QJsonObject SongRequestServer::stateFor(const QString &ip, bool owner) const
{
    QJsonObject st = queueState ? queueState() : QJsonObject();
    QJsonArray mine, pend;
    for (const Request &r : requests) {
        QJsonObject o{ { "id", r.id }, { "title", r.title }, { "artist", r.artist }, { "who", r.who }, { "status", r.status } };
        if (r.ip == ip && clock.elapsed() - r.time < 3 * 3600 * 1000)
            mine.append(o);
        if (owner && r.status == "pending")
            pend.append(o);
    }
    QJsonArray last;                    // the newest few of this phone
    for (int i = qMax(0, mine.size() - 8); i < mine.size(); i++)
        last.append(mine[i]);
    st["mine"] = last;
    if (owner) {
        st["pending"] = pend;
        st["auto"] = autoOk;
    }
    return st;
}

void SongRequestServer::handle(QTcpSocket *s, const QByteArray &method, const QString &path,
                               const QByteArray &query, const QByteArray &body)
{
    // form encoding writes spaces as '+'; QUrlQuery decodes %XX (UTF-8) itself
    const QUrlQuery uq{ QString::fromLatin1(QByteArray(query).replace('+', "%20")) };
    const QUrlQuery bq{ QString::fromLatin1(QByteArray(body).replace('+', "%20")) };
    const QString k = uq.queryItemValue("k", QUrl::FullyDecoded);
    const bool owner = !ownerToken.isEmpty() && k == ownerToken;
    if (!owner && (custToken.isEmpty() || k != custToken)) {
        reply(s, 403, "text/plain; charset=utf-8", "Forbidden");
        return;
    }
    QString ip = s->peerAddress().toString();
    if (ip.startsWith("::ffff:"))
        ip = ip.mid(7);

    if (path == "/") {
        QByteArray page = QByteArray(kPage1) + QByteArray(kPage2).replace("%OWNER%", owner ? "true" : "false");
        reply(s, 200, "text/html; charset=utf-8", page);
        return;
    }
    if (path == "/api/state") {
        replyJson(s, stateFor(ip, owner));
        return;
    }
    if (path == "/api/search") {
        const QString text = uq.queryItemValue("q", QUrl::FullyDecoded).trimmed().left(80);
        if (uq.queryItemValue("src") == "yt") {
            searchYouTube(s, text);
            return;
        }
        replyJson(s, QJsonObject{ { "items", searchLibrary ? searchLibrary(text) : QJsonArray() } });
        return;
    }
    if (path == "/api/request" && method == "POST") {
        Request r;
        r.key = bq.queryItemValue("key", QUrl::FullyDecoded).left(400);
        r.title = bq.queryItemValue("title", QUrl::FullyDecoded).left(200);
        r.artist = bq.queryItemValue("artist", QUrl::FullyDecoded).left(120);
        r.who = bq.queryItemValue("who", QUrl::FullyDecoded).left(30);
        r.seconds = bq.queryItemValue("seconds").toInt();
        if (!(r.key.startsWith("L:") || r.key.startsWith("Y:")) || r.title.isEmpty()) {
            replyJson(s, QJsonObject{ { "ok", false }, { "msg", QString::fromUtf8("ข้อมูลเพลงไม่ถูกต้อง") } });
            return;
        }
        int waiting = 0;
        for (const Request &o : requests)
            if (o.ip == ip && o.status == "pending")
                waiting++;
        if (!owner && waiting >= kMaxPendingPerPhone) {
            replyJson(s, QJsonObject{ { "ok", false }, { "msg", QString::fromUtf8("รอเจ้าของอนุมัติคำขอเดิมก่อนนะครับ") } });
            return;
        }
        r.id = nextId++;
        r.ip = ip;
        r.time = clock.elapsed();
        r.status = "pending";
        requests << r;
        while (requests.size() > 300)
            requests.removeFirst();
        emit requestsChanged();
        if (owner || autoOk)
            decide(r.id, true);
        const QString st = requests.last().status;
        replyJson(s, QJsonObject{ { "ok", true }, { "msg", st == "approved" ? QString::fromUtf8("เข้าคิวแล้ว")
                                                           : st == "rejected" ? QString::fromUtf8("เพิ่มเพลงไม่สำเร็จ")
                                                           : QString::fromUtf8("ส่งคำขอแล้ว รอเจ้าของอนุมัติ") } });
        return;
    }
    if (owner && path == "/api/decide" && method == "POST") {
        decide(bq.queryItemValue("id").toInt(), bq.queryItemValue("ok") == "1");
        replyJson(s, QJsonObject{ { "ok", true } });
        return;
    }
    if (owner && path == "/api/auto" && method == "POST") {
        setAutoApprove(bq.queryItemValue("on") == "1");
        replyJson(s, QJsonObject{ { "ok", true } });
        return;
    }
    reply(s, 404, "text/plain", "Not found");
}

// YouTube search with yt-dlp (the same helper as the YouTube search of the player)
void SongRequestServer::searchYouTube(QTcpSocket *s, const QString &text)
{
    if (YtTools::ytdlpPath().isEmpty()) {
        replyJson(s, QJsonObject{ { "items", QJsonArray() },
                                  { "msg", QString::fromUtf8("ยังค้นจาก YouTube ไม่ได้ (เจ้าของต้องเปิดค้นหา YouTube ในโปรแกรมครั้งแรกก่อน)") } });
        return;
    }
    QString q = text;
    if (!q.contains(QString::fromUtf8("คาราโอเกะ")) && !q.contains("karaoke", Qt::CaseInsensitive))
        q += QString::fromUtf8(" คาราโอเกะ");
    QProcess *p = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    p->setProcessEnvironment(env);
    QPointer<QTcpSocket> sock(s);
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, p, sock](int, QProcess::ExitStatus) {
        const QJsonArray entries = QJsonDocument::fromJson(p->readAllStandardOutput()).object().value("entries").toArray();
        p->deleteLater();
        if (!sock)
            return;
        QJsonArray items;
        for (const QJsonValue &v : entries) {
            const QJsonObject o = v.toObject();
            const QString id = o.value("id").toString();
            if (id.length() != 11)
                continue;
            QString ch = o.value("channel").toString();
            if (ch.isEmpty())
                ch = o.value("uploader").toString();
            items.append(QJsonObject{ { "key", "Y:" + id }, { "title", o.value("title").toString() },
                                      { "artist", ch }, { "seconds", int(o.value("duration").toDouble()) } });
        }
        replyJson(sock, QJsonObject{ { "items", items } });
    });
    p->start(YtTools::ytdlpPath(), { "--flat-playlist", "-J", "--no-warnings", "ytsearch15:" + q });
}
