#include "Remote/RemoteServer.h"

#include <QApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QHostAddress>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNetworkInterface>
#include <QPixmap>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QWheelEvent>

static const int    kMaxRequest  = 64 * 1024;
static const int    kMaxFrameW   = 1600;
static const qint64 kClientAlive = 4000;

static const char *kPage = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<title>PTHK Karaoke</title>
<style>
html,body{margin:0;height:100%;background:#07080d;color:#7dffc0;font:13px sans-serif;overflow:hidden;touch-action:none;overscroll-behavior:none}
#s{position:absolute;left:0;top:0;touch-action:none;-webkit-user-select:none;user-select:none;-webkit-touch-callout:none;-webkit-user-drag:none;max-width:none}
#msg{position:fixed;left:0;right:0;top:0;text-align:center;padding:6px;background:rgba(0,0,0,.75);display:none;z-index:3}
#bar{touch-action:manipulation;position:fixed;right:calc(6px + env(safe-area-inset-right));top:50%;transform:translateY(-50%);display:flex;flex-direction:column;gap:6px;z-index:2;opacity:.8}
#bar.min button.x{display:none}
button{touch-action:manipulation;-webkit-tap-highlight-color:transparent;background:#14202a;color:#7dffc0;border:1px solid #1f5a45;border-radius:8px;padding:8px 12px;font-size:14px;min-width:40px}
</style></head><body>
<div id="msg"></div>
<img id="s" alt="">
<div id="bar"><button id="tg">&#9776;</button><button class="x" id="zi">+</button><button class="x" id="zo">&minus;</button><button class="x" id="rt">&#10227;</button><button class="x" id="esc">Esc</button><button class="x" id="fs">&#9974;</button></div>
<script>
var T=new URLSearchParams(location.search).get('t')||'';
var img=document.getElementById('s'),msg=document.getElementById('msg');
var h='',fails=0,dead=false;
var bar=document.getElementById('bar');
var rotMode=0,zoom=1,px=0,py=0,ROT=false;
function say(t){if(t){msg.textContent=t;msg.style.display='block';}else{msg.style.display='none';}}
function layout(){
  var iw=img.naturalWidth,ih=img.naturalHeight;if(!iw||!ih)return;
  var vv=window.visualViewport;
  var vw=Math.min(document.documentElement.clientWidth,window.innerWidth||1e9,vv?vv.width:1e9);
  var vh=Math.min(document.documentElement.clientHeight,window.innerHeight||1e9,vv?vv.height:1e9);
  var rot=(rotMode==1)||(rotMode==0&&vh>vw&&iw>ih*1.15);
  var bw=rot?ih:iw,bh=rot?iw:ih;
  var s=Math.min(vw/bw,vh/bh)*zoom;
  var w=iw*s,hh=ih*s,W=rot?hh:w,H=rot?w:hh;
  var mx=Math.max(0,(W-vw)/2),my=Math.max(0,(H-vh)/2);
  px=Math.max(-mx,Math.min(mx,px));py=Math.max(-my,Math.min(my,py));
  img.style.width=w+'px';img.style.height=hh+'px';
  img.style.left=((vw-w)/2+px)+'px';img.style.top=((vh-hh)/2+py)+'px';
  img.style.transform=rot?'rotate(90deg)':'none';
  ROT=rot;
  var imgTop=(vh-hh)/2+py;
  if(rot){
    bar.style.flexDirection='column';bar.style.left='auto';
    bar.style.right='calc(6px + env(safe-area-inset-right))';
    bar.style.top='50%';bar.style.transform='translateY(-50%)';
  }else{
    bar.style.flexDirection='row';bar.style.right='auto';bar.style.transform='none';
    var bw=bar.offsetWidth,bhh=bar.offsetHeight;
    bar.style.left=Math.max(4,Math.min(vw-bw-4,vw/2+px-bw/2))+'px';
    var minTop=52;                                  // keep clear of the browser's own top bar
    var above=imgTop-minTop,below=vh-(imgTop+hh)-24;
    var top;
    if(above>=bhh+14)top=imgTop-bhh-14;             // above the mixer (preferred)
    else if(below>=bhh+14)top=imgTop+hh+8;          // otherwise below it
    else top=minTop;
    bar.style.top=Math.max(minTop,top)+'px';
    // safety: measure what really happened and lift the bar off the image
    var rc=bar.getBoundingClientRect();
    if(top<imgTop&&rc.bottom>imgTop-6){
      bar.style.top=Math.max(0,rc.top-(rc.bottom-(imgTop-8)))+'px';
    }
  }
}
window.addEventListener('resize',layout);
if(window.visualViewport)window.visualViewport.addEventListener('resize',layout);
window.addEventListener('orientationchange',function(){setTimeout(layout,200);});
function fetchT(url,opt,ms){
  // never wait forever on a weak Wi-Fi link
  var ac=(typeof AbortController!=='undefined')?new AbortController():null;
  var tm=setTimeout(function(){if(ac)ac.abort();},ms);
  opt=opt||{};if(ac)opt.signal=ac.signal;
  return fetch(url,opt).then(function(r){clearTimeout(tm);return r;},function(e){clearTimeout(tm);throw e;});
}
var gotFrame=true;
function loop(){
  if(dead)return;
  gotFrame=false;
  fetchT('/f?t='+encodeURIComponent(T)+'&h='+h,{cache:'no-store'},5000).then(function(r){
    if(r.status==403){dead=true;say('QRหมดอายุ/ปิดอยู่ กรุณาสแกนใหม่');return;}
    if(r.status==200){
      h=r.headers.get('X-H')||'';gotFrame=true;
      return r.blob().then(function(b){
        var u=URL.createObjectURL(b),old=img.src;
        img.onload=function(){if(old&&old.indexOf('blob:')==0)URL.revokeObjectURL(old);layout();};
        img.src=u;
      });
    }
  }).then(function(){fails=0;say('');setTimeout(loop,gotFrame?30:90);})
    .catch(function(){fails++;say('กำลังเชื่อมต่อ...');setTimeout(loop,Math.min(2000,250*fails));});
}
loop();

var pend=[],sending=false;
function pump(){
  if(sending||!pend.length)return;
  sending=true;
  var o=pend.shift();
  fetchT('/e?t='+encodeURIComponent(T),{method:'POST',body:new URLSearchParams(o)},2500)
    .catch(function(){}).then(function(){sending=false;pump();});
}
function send(o){
  if(o.type=='move'&&pend.length&&pend[pend.length-1].type=='move')pend[pend.length-1]=o;
  else pend.push(o);
  pump();
}
function pos(e){
  var r=img.getBoundingClientRect();
  var dx=e.clientX-r.left,dy=e.clientY-r.top,x,y;
  if(!ROT){x=dx/r.width;y=dy/r.height;}else{x=dy/r.height;y=1-dx/r.width;}
  return {x:Math.max(0,Math.min(1,x)),y:Math.max(0,Math.min(1,y))};
}
var pts={},n=0,down=false,last=null,cx=0,cy=0,acc=0;
function centroid(){
  var k=Object.keys(pts),sx=0,sy=0;
  for(var i=0;i<k.length;i++){sx+=pts[k[i]].clientX;sy+=pts[k[i]].clientY;}
  return {x:sx/k.length,y:sy/k.length};
}
img.addEventListener('pointerdown',function(e){
  e.preventDefault();img.setPointerCapture(e.pointerId);
  pts[e.pointerId]=e;n=Object.keys(pts).length;
  if(n==1){var p=pos(e);last=p;down=true;send({type:'down',x:p.x,y:p.y});}
  else if(n==2){
    if(down){send({type:'cancel',x:last.x,y:last.y});down=false;}
    var c=centroid();cx=c.x;cy=c.y;acc=0;
  }
});
img.addEventListener('pointermove',function(e){
  if(!pts[e.pointerId])return;
  e.preventDefault();pts[e.pointerId]=e;
  if(n==1&&down){var p=pos(e);last=p;send({type:'move',x:p.x,y:p.y});}
  else if(n==2){
    var c=centroid(),dx=c.x-cx,dy=c.y-cy;cx=c.x;cy=c.y;
    if(zoom>1.01){px+=dx;py+=dy;layout();}
    else{
      acc+=ROT?-dx:dy;
      if(Math.abs(acc)>10){var p2=pos(c);send({type:'wheel',x:p2.x,y:p2.y,dy:Math.round(acc)});acc=0;}
    }
  }
});
function end(e){
  if(!pts[e.pointerId])return;
  e.preventDefault();
  var wasOne=(n==1);
  delete pts[e.pointerId];n=Object.keys(pts).length;
  if(wasOne&&down){var p=pos(e);send({type:'up',x:p.x,y:p.y});down=false;}
}
img.addEventListener('pointerup',end);
img.addEventListener('pointercancel',end);
img.addEventListener('contextmenu',function(e){e.preventDefault();});
document.addEventListener('touchmove',function(e){e.preventDefault();},{passive:false});
try{if(localStorage.getItem('bm_min')=='1')bar.className='min';}catch(e){}
function btn(id,fn){
  var b=document.getElementById(id);
  b.addEventListener('pointerdown',function(e){
    e.preventDefault();e.stopPropagation();
    b.style.background='#1f5a45';setTimeout(function(){b.style.background='';},140);
    fn();
  });
  b.addEventListener('click',function(e){e.preventDefault();e.stopPropagation();});
}
btn('tg',function(){
  bar.className=(bar.className=='min')?'':'min';
  try{localStorage.setItem('bm_min',bar.className=='min'?'1':'0');}catch(e){}
  layout();
});
btn('esc',function(){send({type:'key',key:'esc'});});
btn('zi',function(){zoom=Math.min(4,zoom*1.35);layout();});
btn('zo',function(){zoom=Math.max(1,zoom/1.35);if(zoom<1.02){zoom=1;px=0;py=0;}layout();});
btn('rt',function(){rotMode=(rotMode+1)%3;px=0;py=0;layout();});
var de=document.documentElement;
if(!(de.requestFullscreen||de.webkitRequestFullscreen))document.getElementById('fs').style.display='none';   // e.g. iPhone Safari
btn('fs',function(){
  var inFs=document.fullscreenElement||document.webkitFullscreenElement;
  if(inFs){if(document.exitFullscreen)document.exitFullscreen();else if(document.webkitExitFullscreen)document.webkitExitFullscreen();}
  else{if(de.requestFullscreen)de.requestFullscreen();else if(de.webkitRequestFullscreen)de.webkitRequestFullscreen();}
  setTimeout(layout,300);
});
document.addEventListener('fullscreenchange',function(){setTimeout(layout,200);});
</script></body></html>
)HTML";

// ---------------------------------------------------------------------------

RemoteServer::RemoteServer(QWidget *baseWindow, QWidget *ignoreWindow, QObject *parent)
    : QObject(parent), mainWin(baseWindow), ignoreWin(ignoreWindow)
{
    clock.start();
    lastGrab.start();
    connect(&server, &QTcpServer::newConnection, this, &RemoteServer::onNewConnection);

    // close connections that were idle for a while
    idleTimer.setInterval(5000);
    connect(&idleTimer, &QTimer::timeout, this, [this]() {
        const QList<QTcpSocket *> socks = findChildren<QTcpSocket *>();
        for (QTcpSocket *s : socks)
            if (clock.elapsed() - s->property("last").toLongLong() > 20000)
                s->abort();
    });
}

RemoteServer::~RemoteServer()
{
    stop();
}

QStringList RemoteServer::localAddresses()
{
    QStringList prefer, other;

    const QList<QNetworkInterface> ifs = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &ni : ifs)
    {
        if (!(ni.flags() & QNetworkInterface::IsUp) ||
            !(ni.flags() & QNetworkInterface::IsRunning) ||
            (ni.flags() & QNetworkInterface::IsLoopBack))
            continue;

        const QList<QNetworkAddressEntry> entries = ni.addressEntries();
        for (const QNetworkAddressEntry &e : entries)
        {
            QHostAddress a = e.ip();
            if (a.protocol() != QAbstractSocket::IPv4Protocol)
                continue;

            QString s = a.toString();
            if (s.startsWith("169.254."))       // link-local, useless
                continue;

            quint32 v = a.toIPv4Address();
            bool priv = (v >> 24) == 10 ||
                        (v >> 20) == 0xAC1 ||           // 172.16/12
                        (v >> 16) == 0xC0A8;            // 192.168/16
            if (priv) prefer << s; else other << s;
        }
    }
    return prefer + other;
}

bool RemoteServer::start(const QString &ip)
{
    stop();

    ipText = ip;

    // random token (the QR code carries it)
    static const char alphabet[] = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    token.clear();
    for (int i = 0; i < 16; i++)
        token += QChar(alphabet[QRandomGenerator::global()->bounded((int)sizeof(alphabet) - 1)]);

    for (quint16 p = 8765; p < 8775; p++)
    {
        if (server.listen(QHostAddress::AnyIPv4, p))
        {
            idleTimer.start();
            return true;
        }
    }
    return false;
}

void RemoteServer::stop()
{
    idleTimer.stop();
    if (server.isListening())
        server.close();

    // drop all open connections
    const QList<QTcpSocket *> socks = findChildren<QTcpSocket *>();
    for (QTcpSocket *s : socks)
    {
        s->disconnect(this);
        s->abort();
        s->deleteLater();
    }

    token.clear();
    clients.clear();
    dragWindow = nullptr;
    pressWidget = nullptr;
    emit clientsChanged();
}

QString RemoteServer::url() const
{
    if (!server.isListening())
        return QString();
    return QString("http://%1:%2/?t=%3").arg(ipText).arg(server.serverPort()).arg(token);
}

int RemoteServer::clientCount() const
{
    qint64 now = clock.elapsed();
    int n = 0;
    for (auto it = clients.constBegin(); it != clients.constEnd(); ++it)
        if (now - it.value() < kClientAlive)
            n++;
    return n;
}

void RemoteServer::touchClient(const QString &ip)
{
    bool isNew = !clients.contains(ip) || (clock.elapsed() - clients.value(ip) >= kClientAlive);
    clients[ip] = clock.elapsed();
    if (isNew)
        emit clientsChanged();
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

void RemoteServer::onNewConnection()
{
    while (server.hasPendingConnections())
    {
        QTcpSocket *s = server.nextPendingConnection();
        s->setParent(this);
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);   // no Nagle delay
        s->setProperty("last", clock.elapsed());

        connect(s, &QTcpSocket::readyRead, this, [this, s]() { onReadyRead(s); });
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
    }
}

void RemoteServer::onReadyRead(QTcpSocket *s)
{
    s->setProperty("last", clock.elapsed());

    QByteArray buf = s->property("buf").toByteArray();
    buf += s->readAll();

    // keep-alive: one socket may carry many requests
    while (!buf.isEmpty())
    {
        if (buf.size() > kMaxRequest * 2)
        {
            s->abort();
            return;
        }

        int headEnd = buf.indexOf("\r\n\r\n");
        if (headEnd < 0)
            break;                                  // wait for the rest

        QByteArray head = buf.left(headEnd);
        QList<QByteArray> lines = head.split('\n');
        QList<QByteArray> reqLine = lines.value(0).trimmed().split(' ');
        if (reqLine.size() < 2)
        {
            s->abort();
            return;
        }

        int contentLength = 0;
        for (int i = 1; i < lines.size(); i++)
        {
            QByteArray l = lines[i].trimmed();
            if (l.toLower().startsWith("content-length:"))
                contentLength = l.mid(15).trimmed().toInt();
        }

        if (contentLength < 0 || contentLength > kMaxRequest)
        {
            s->abort();
            return;
        }

        int total = headEnd + 4 + contentLength;
        if (buf.size() < total)
            break;                                  // body not complete yet

        QByteArray body = buf.mid(headEnd + 4, contentLength);
        buf.remove(0, total);

        QByteArray target = reqLine[1];
        QByteArray path = target;
        QByteArray query;
        int q = target.indexOf('?');
        if (q >= 0)
        {
            path = target.left(q);
            query = target.mid(q + 1);
        }

        handle(s, reqLine[0], QString::fromLatin1(path), query, body);
    }

    s->setProperty("buf", buf);
}

void RemoteServer::reply(QTcpSocket *s, int code, const QByteArray &contentType,
                         const QByteArray &body, const QByteArray &extraHeaders)
{
    QByteArray status;
    switch (code) {
    case 200: status = "OK"; break;
    case 204: status = "No Content"; break;
    case 403: status = "Forbidden"; break;
    default:  status = "Not Found"; code = 404; break;
    }

    QByteArray out = "HTTP/1.1 " + QByteArray::number(code) + " " + status + "\r\n";
    out += "Content-Type: " + contentType + "\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Cache-Control: no-store\r\n";
    out += "Connection: keep-alive\r\nKeep-Alive: timeout=15\r\n";
    out += "Access-Control-Expose-Headers: X-H\r\n";
    out += extraHeaders;
    out += "\r\n";
    out += body;

    s->write(out);
}

void RemoteServer::handle(QTcpSocket *s, const QByteArray &method, const QString &path,
                          const QByteArray &query, const QByteArray &body)
{
    QUrlQuery uq{QString::fromLatin1(query)};

    if (token.isEmpty() || uq.queryItemValue("t") != token)
    {
        reply(s, 403, "text/plain; charset=utf-8", "Forbidden");
        return;
    }

    QString ip = s->peerAddress().toString();
    if (ip.startsWith("::ffff:"))
        ip = ip.mid(7);

    if (path == "/")
    {
        touchClient(ip);
        reply(s, 200, "text/html; charset=utf-8", QByteArray(kPage));
        return;
    }

    if (path == "/f")
    {
        touchClient(ip);

        if (!grabFrame())
        {
            reply(s, 204, "text/plain", QByteArray());
            return;
        }

        if (uq.queryItemValue("h") == lastHash)
        {
            reply(s, 204, "text/plain", QByteArray());
            return;
        }

        reply(s, 200, "image/jpeg", lastJpeg, "X-H: " + lastHash.toLatin1() + "\r\n");
        return;
    }

    if (path == "/e" && method == "POST")
    {
        touchClient(ip);

        QMap<QString, QString> p;
        QUrlQuery bq{QString::fromUtf8(body)};
        const QList<QPair<QString, QString> > items = bq.queryItems(QUrl::FullyDecoded);
        for (const QPair<QString, QString> &it : items)
            p.insert(it.first, it.second);

        // answer first: an injected click may open a modal dialog (e.g. the exit
        // question) whose nested event loop would otherwise block this reply
        reply(s, 200, "text/plain", "ok");
        QTimer::singleShot(0, this, [this, p]() { inject(p); });
        return;
    }

    reply(s, 404, "text/plain", "Not found");
}

// ---------------------------------------------------------------------------
// screen mirroring
// ---------------------------------------------------------------------------

QWidget *RemoteServer::targetWindow() const
{
    if (QWidget *w = QApplication::activePopupWidget())
        return w;
    if (QWidget *w = QApplication::activeModalWidget())
        if (!w->property("pthkRemoteSkip").toBool())
            return w;

    QWidget *a = QApplication::activeWindow();
    if (a && a != ignoreWin && a->isVisible() && !a->isMinimized() &&
        !a->property("pthkRemoteSkip").toBool())
        return a;

    return mainWin;
}

bool RemoteServer::grabFrame()
{
    // share one capture between several phones / fast polling
    if (!lastJpeg.isEmpty() && lastGrab.elapsed() < 40)
        return true;

    // an open popup menu is always shown, even while a press is still in progress
    QWidget *w = QApplication::activePopupWidget();
    if (!w)
        w = dragWindow ? dragWindow.data() : targetWindow();
    if (!w || w->width() < 2 || w->height() < 2)
        return false;

    QImage img = w->grab().toImage().convertToFormat(QImage::Format_RGB32);
    if (img.isNull())
        return false;

    if (img.width() > kMaxFrameW)
        img = img.scaledToWidth(kMaxFrameW, Qt::SmoothTransformation);

    QCryptographicHash md5(QCryptographicHash::Md5);
    md5.addData(reinterpret_cast<const char *>(img.constBits()), int(img.sizeInBytes()));
    // the source window identity / size is part of the hash
    md5.addData(QByteArray::number(reinterpret_cast<quintptr>(w)));
    md5.addData(QByteArray::number(img.width()) + "x" + QByteArray::number(img.height()));
    QString hash = QString::fromLatin1(md5.result().toHex().left(16));

    lastGrab.restart();

    if (hash == lastHash && !lastJpeg.isEmpty())
        return true;

    QByteArray jpg;
    QBuffer buf(&jpg);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "JPEG", 70);

    lastJpeg = jpg;
    lastHash = hash;
    return !lastJpeg.isEmpty();
}

// ---------------------------------------------------------------------------
// input injection
// ---------------------------------------------------------------------------

void RemoteServer::inject(const QMap<QString, QString> &p)
{
    const QString type = p.value("type");

    if (type == "key")
    {
        if (p.value("key") == "esc")
        {
            QWidget *w = QApplication::activePopupWidget();
            if (!w) w = QApplication::focusWidget();
            if (!w) w = targetWindow();
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(w, &press);
            QApplication::sendEvent(w, &release);
            lastJpeg.clear();
        }
        return;
    }

    double nx = qBound(0.0, p.value("x").toDouble(), 1.0);
    double ny = qBound(0.0, p.value("y").toDouble(), 1.0);

    QWidget *win = nullptr;
    if (type == "down")
        win = targetWindow();
    else if (dragWindow)
        win = dragWindow.data();
    else
        win = targetWindow();

    if (!win)
        return;

    QPoint wp(qBound(0, qRound(nx * win->width()),  win->width()  - 1),
              qBound(0, qRound(ny * win->height()), win->height() - 1));

    if (type == "down")
    {
        QWidget *child = win->childAt(wp);
        if (!child)
            child = win;

        dragWindow = win;
        pressWidget = child;

        QPoint lp = child->mapFrom(win, wp);
        QPoint gp = win->mapToGlobal(wp);
        QMouseEvent ev(QEvent::MouseButtonPress, QPointF(lp), QPointF(gp),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(child, &ev);
    }
    else if (type == "move")
    {
        if (!pressWidget)
            return;
        QPoint lp = pressWidget->mapFrom(win, wp);
        QPoint gp = win->mapToGlobal(wp);
        QMouseEvent ev(QEvent::MouseMove, QPointF(lp), QPointF(gp),
                       Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(pressWidget.data(), &ev);
    }
    else if (type == "up" || type == "cancel")
    {
        QWidget *pw = pressWidget.data();
        QWidget *pwin = win;
        if (pw)
        {
            // cancel: release far outside the widget, so buttons do not "click"
            QPoint lp = (type == "cancel") ? QPoint(-5000, -5000) : pw->mapFrom(win, wp);
            QPoint gp = win->mapToGlobal(wp);
            QMouseEvent ev(QEvent::MouseButtonRelease, QPointF(lp), QPointF(gp),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(pw, &ev);        // may run a nested event loop
        }
        // a new press may have started meanwhile (inside that nested loop)
        if (pressWidget.data() == pw)
        {
            pressWidget = nullptr;
            dragWindow = nullptr;
        }
        Q_UNUSED(pwin);
    }
    else if (type == "wheel")
    {
        QWidget *child = win->childAt(wp);
        if (!child)
            child = win;
        QPoint lp = child->mapFrom(win, wp);
        QPoint gp = win->mapToGlobal(wp);
        int dy = p.value("dy").toInt() * 3;
        QWheelEvent ev(QPointF(lp), QPointF(gp), QPoint(0, 0), QPoint(0, dy),
                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(child, &ev);
    }

    // the screen changes because of the input: force a fresh capture
    lastJpeg.clear();
}
