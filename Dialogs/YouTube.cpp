#include "YouTube.h"

#ifdef Q_OS_WIN
#include <windows.h>
static void setLowPriorityProcess(QProcess *p)
{
    p->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= BELOW_NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW;
    });
}
#else
static void setLowPriorityProcess(QProcess *) {}
#endif

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <QDateTime>
#include <QInputDialog>
#include <QGridLayout>
#include <QRandomGenerator>
#include <QSet>
#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QListWidget>
#include <QProgressDialog>

#ifdef PTHK_WEBENGINE
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#endif

static const char *YTDLP_URL  = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe";
static const char *FFMPEG_URL = "https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip";

QString YtTools::toolsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/PTHK Karaoke/tools";
}

static QString findTool(const QString &exe)
{
    for (const QString &dir : { YtTools::toolsDir(), QApplication::applicationDirPath() }) {
        QString p = dir + "/" + exe;
        if (QFile::exists(p))
            return p;
    }
    return QStandardPaths::findExecutable(exe);
}

QString YtTools::ytdlpPath()  { return findTool("yt-dlp.exe"); }
QString YtTools::ffmpegPath() { return findTool("ffmpeg.exe"); }

bool YtTools::isYouTubeUrl(const QString &text)
{
    QUrl u = QUrl::fromUserInput(text.trimmed());
    QString host = u.host().toLower();
    if (host.endsWith("youtu.be"))
        return u.path().length() > 1;
    if (!host.endsWith("youtube.com"))
        return false;
    QString path = u.path();
    return (path == "/watch" && QUrlQuery(u).hasQueryItem("v"))
            || path.startsWith("/shorts/") || path.startsWith("/live/");
}

// ---------------------------------------------------------------------------

YouTubeDownloadDialog::YouTubeDownloadDialog(const QString &outDir, const QString &url, QWidget *parent)
    : QDialog(parent), outDir(outDir)
{
    setWindowTitle(tr("ดาวน์โหลดเพลงจาก YouTube"));
    setMinimumWidth(560);

    QString initial = url;
    if (initial.isEmpty() && YtTools::isYouTubeUrl(QApplication::clipboard()->text()))
        initial = QApplication::clipboard()->text().trimmed();

    edUrl = new QLineEdit(initial, this);
    edUrl->setPlaceholderText(tr("วางลิงก์ YouTube เช่น https://www.youtube.com/watch?v=..."));
    chkQueue = new QCheckBox(tr("ดาวน์โหลดเสร็จแล้วเพิ่มเข้าคิวเล่น"), this);
    chkQueue->setChecked(true);
    progress = new QProgressBar(this);
    progress->setRange(0, 100);
    progress->setValue(0);
    lbStatus = new QLabel(tr("ไฟล์จะถูกเก็บที่: ") + QDir::toNativeSeparators(outDir), this);
    lbStatus->setWordWrap(true);
    QLabel *note = new QLabel(tr("<small style='color:gray'>ใช้เฉพาะคลิปที่คุณมีสิทธิ์ใช้ "
                                 "(การดาวน์โหลดอาจขัดเงื่อนไขของ YouTube และเพลงมีลิขสิทธิ์ของเจ้าของ)</small>"), this);
    note->setWordWrap(true);

    btnStart = new QPushButton(tr("ดาวน์โหลด"), this);
    btnStart->setDefault(true);
    QPushButton *btnClose = new QPushButton(tr("ปิด"), this);
    connect(btnStart, &QPushButton::clicked, this, &YouTubeDownloadDialog::start);
    connect(btnClose, &QPushButton::clicked, this, &YouTubeDownloadDialog::reject);

    QHBoxLayout *h = new QHBoxLayout();
    h->addWidget(chkQueue);
    h->addStretch(1);
    h->addWidget(btnStart);
    h->addWidget(btnClose);

    QVBoxLayout *v = new QVBoxLayout(this);
    v->addWidget(new QLabel(tr("ลิงก์ YouTube:"), this));
    v->addWidget(edUrl);
    v->addWidget(progress);
    v->addWidget(lbStatus);
    v->addWidget(note);
    v->addLayout(h);
}

YouTubeDownloadDialog::~YouTubeDownloadDialog()
{
    if (proc) {
        proc->disconnect(this);
        proc->kill();
        proc->waitForFinished(3000);
    }
}

void YouTubeDownloadDialog::reject()
{
    if (proc) {
        proc->disconnect(this);
        proc->kill();
        proc->waitForFinished(3000);
    }
    QDialog::reject();
}

void YouTubeDownloadDialog::setBusy(bool busy)
{
    btnStart->setEnabled(!busy);
    edUrl->setEnabled(!busy);
}

void YouTubeDownloadDialog::start()
{
    if (!YtTools::isYouTubeUrl(edUrl->text())) {
        lbStatus->setText(tr("ลิงก์นี้ไม่ใช่ลิงก์คลิป YouTube"));
        return;
    }
    if (YtTools::ytdlpPath().isEmpty() || YtTools::ffmpegPath().isEmpty()) {
        int r = QMessageBox::question(this, windowTitle(),
                    tr("ครั้งแรกต้องดาวน์โหลดตัวช่วย (ครั้งเดียว):\n"
                       "  - yt-dlp (ประมาณ 18 MB) จาก github.com/yt-dlp/yt-dlp\n"
                       "  - ffmpeg (ดาวน์โหลดประมาณ 190 MB) จาก github.com/yt-dlp/FFmpeg-Builds\n\n"
                       "เก็บไว้ที่ %1\nดาวน์โหลดเลยไหม?").arg(QDir::toNativeSeparators(YtTools::toolsDir())));
        if (r != QMessageBox::Yes)
            return;
        fetchTools();
        return;
    }
    runYtdlp();
}

// Tools are fetched with the curl.exe built into Windows (Schannel TLS, so no OpenSSL DLLs needed).
void YouTubeDownloadDialog::fetchTools()
{
    QDir().mkpath(YtTools::toolsDir());
    pendingTools.clear();
    if (YtTools::ytdlpPath().isEmpty())
        pendingTools << YTDLP_URL;
    if (YtTools::ffmpegPath().isEmpty())
        pendingTools << FFMPEG_URL;
    setBusy(true);
    fetchNext();
}

void YouTubeDownloadDialog::fetchNext()
{
    if (pendingTools.isEmpty()) {
        setBusy(false);
        if (YtTools::ytdlpPath().isEmpty() || YtTools::ffmpegPath().isEmpty()) {
            lbStatus->setText(tr("ดาวน์โหลดตัวช่วยไม่สำเร็จ ตรวจสอบอินเทอร์เน็ตแล้วลองใหม่"));
            return;
        }
        runYtdlp();
        return;
    }

    const QString url = pendingTools.takeFirst();
    const bool isZip = url.endsWith(".zip");
    const QString dir = YtTools::toolsDir();
    const QString target = dir + (isZip ? "/ffmpeg.zip" : "/yt-dlp.exe.part");

    lbStatus->setText(isZip ? tr("กำลังดาวน์โหลด ffmpeg...") : tr("กำลังดาวน์โหลด yt-dlp..."));
    progress->setValue(0);

    proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::readyRead, this, [this]() {
        static const QRegularExpression re("(\\d+(?:\\.\\d+)?)%");
        QString out = QString::fromLatin1(proc->readAll());
        auto it = re.globalMatch(out);
        QString last;
        while (it.hasNext())
            last = it.next().captured(1);
        if (!last.isEmpty())
            progress->setValue(int(last.toDouble()));
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, isZip, dir, target](int code, QProcess::ExitStatus st) {
        proc->deleteLater();
        if (st != QProcess::NormalExit || code != 0) {
            QFile::remove(target);
            pendingTools.clear();
            fetchNext();
            return;
        }
        if (!isZip) {
            QFile::remove(dir + "/yt-dlp.exe");
            QFile::rename(target, dir + "/yt-dlp.exe");
            fetchNext();
            return;
        }
        // unpack only ffmpeg.exe / ffprobe.exe out of the zip (tar.exe ships with Windows 10+)
        lbStatus->setText(tr("กำลังแตกไฟล์ ffmpeg..."));
        const QString tmp = dir + "/ffmpeg-unpack";
        QDir(tmp).removeRecursively();
        QDir().mkpath(tmp);
        QProcess tar;
        tar.start("tar.exe", { "-xf", QDir::toNativeSeparators(target), "-C", QDir::toNativeSeparators(tmp) });
        tar.waitForFinished(300000);
        QDirIterator it(tmp, { "ffmpeg.exe", "ffprobe.exe" }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString f = it.next();
            QString dst = dir + "/" + QFileInfo(f).fileName();
            QFile::remove(dst);
            QFile::rename(f, dst);
        }
        QDir(tmp).removeRecursively();
        QFile::remove(target);
        fetchNext();
    });
    proc->start("curl.exe", { "-L", "--fail", "-#", "-o", QDir::toNativeSeparators(target), url });
}

void YouTubeDownloadDialog::runYtdlp()
{
    QDir().mkpath(outDir);
    setBusy(true);
    progress->setValue(0);
    part = 0;
    lastPct = 0;
    lbStatus->setText(tr("กำลังอ่านข้อมูลคลิป..."));

    {
        QTemporaryFile tf(QDir::tempPath() + "/pthk-yt-XXXXXX.txt");
        tf.setAutoRemove(false);
        tf.open();
        printFile = tf.fileName();
    }

    proc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    proc->setProcessEnvironment(env);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    QStringList *log = new QStringList();
    connect(proc, &QProcess::readyRead, this, [this, log]() {
        static const QRegularExpression pct("\\[PCT\\]\\s*(\\d+(?:\\.\\d+)?)%");
        while (proc->canReadLine()) {
            QString line = QString::fromUtf8(proc->readLine()).trimmed();
            if (line.isEmpty())
                continue;
            QRegularExpressionMatch m = pct.match(line);
            if (m.hasMatch()) {
                int p = int(m.captured(1).toDouble());
                if (p + 50 < lastPct)
                    part++;                     // the audio stream starts after the video stream
                lastPct = p;
                progress->setValue(p);
                lbStatus->setText(part == 0 ? tr("กำลังดาวน์โหลดวิดีโอ... %1%").arg(p)
                                            : tr("กำลังดาวน์โหลดเสียง... %1%").arg(p));
                continue;
            }
            if (line.startsWith("[Merger]") || line.startsWith("[VideoConvertor]"))
                lbStatus->setText(tr("กำลังรวมภาพและเสียง..."));
            log->append(line);
            if (log->size() > 30)
                log->removeFirst();
        }
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, log](int code, QProcess::ExitStatus st) {
        proc->deleteLater();
        setBusy(false);

        QString file;
        QFile pf(printFile);
        if (pf.open(QIODevice::ReadOnly)) {
            const QStringList lines = QString::fromUtf8(pf.readAll()).split('\n', Qt::SkipEmptyParts);
            if (!lines.isEmpty())
                file = lines.last().trimmed();
            pf.close();
        }
        QFile::remove(printFile);

        if (st == QProcess::NormalExit && code == 0 && !file.isEmpty() && QFile::exists(file)) {
            delete log;
            progress->setValue(100);
            lbStatus->setText(tr("ดาวน์โหลดเสร็จ: ") + QFileInfo(file).fileName());
            emit downloaded(file, chkQueue->isChecked());
            accept();
            return;
        }

        QString err;
        for (const QString &l : *log)
            if (l.startsWith("ERROR"))
                err = l;
        delete log;
        lbStatus->setText(tr("ดาวน์โหลดไม่สำเร็จ") + (err.isEmpty() ? QString() : "\n" + err));
    });

    const QString ffmpegDir = QDir::toNativeSeparators(QFileInfo(YtTools::ffmpegPath()).absolutePath());
    QStringList args = {
        "--no-playlist", "--windows-filenames", "--no-mtime", "--newline",
        // H.264 + AAC so Windows Media Foundation can play it
        "-f", "bv*[vcodec^=avc1][height<=1080]+ba[ext=m4a]/b[ext=mp4]/bv*+ba/b",
        "--merge-output-format", "mp4",
        "--ffmpeg-location", ffmpegDir,
        "--progress-template", "download:[PCT]%(progress._percent_str)s",
        "--print-to-file", "after_move:filepath", QDir::toNativeSeparators(printFile),
        "-o", QDir::toNativeSeparators(outDir) + "\\%(title).120s.%(ext)s",
        edUrl->text().trimmed()
    };
    proc->start(YtTools::ytdlpPath(), args);
}

// ---------------------------------------------------------------------------

// ---- history ---------------------------------------------------------------

static QString dataDir()
{
    return QFileInfo(YtTools::toolsDir()).absolutePath();      // %LOCALAPPDATA%\PTHK Karaoke
}

static QString historyFile() { return dataDir() + "/youtube-history.json"; }
static QString genresFile()  { return dataDir() + "/youtube-genres.txt"; }

QVector<YtHistory::Entry> YtHistory::all()
{
    QVector<Entry> out;
    QFile f(historyFile());
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.item.id = o.value("id").toString();
        e.item.title = o.value("title").toString();
        e.item.channel = o.value("channel").toString();
        e.item.seconds = o.value("seconds").toInt();
        e.count = o.value("count").toInt();
        e.last = qint64(o.value("last").toDouble());
        if (!e.item.id.isEmpty())
            out << e;
    }
    return out;
}

static void saveHistory(const QVector<YtHistory::Entry> &list)
{
    QJsonArray arr;
    for (const YtHistory::Entry &e : list) {
        QJsonObject o;
        o["id"] = e.item.id;
        o["title"] = e.item.title;
        o["channel"] = e.item.channel;
        o["seconds"] = e.item.seconds;
        o["count"] = e.count;
        o["last"] = double(e.last);
        arr.append(o);
    }
    QDir().mkpath(dataDir());
    QFile f(historyFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void YtHistory::record(const YtItem &item)
{
    if (item.id.isEmpty())
        return;
    QVector<Entry> list = all();
    int i = 0;
    while (i < list.size() && list[i].item.id != item.id)
        i++;
    if (i == list.size()) {
        Entry e;
        e.item = item;
        list << e;
    } else if (!item.title.isEmpty()) {
        const int secs = list[i].item.seconds;
        list[i].item = item;
        if (item.seconds <= 0)
            list[i].item.seconds = secs;
    }
    list[i].count++;
    list[i].last = QDateTime::currentMSecsSinceEpoch();

    if (list.size() > 500) {        // forget the songs sung least recently
        std::sort(list.begin(), list.end(), [](const Entry &a, const Entry &b) { return a.last > b.last; });
        list.resize(500);
    }
    saveHistory(list);
}

void YtHistory::setDuration(const QString &id, int seconds)
{
    QVector<Entry> list = all();
    for (Entry &e : list)
        if (e.item.id == id && e.item.seconds != seconds) {
            e.item.seconds = seconds;
            saveHistory(list);
            return;
        }
}

QStringList YtHistory::genres()
{
    QFile f(genresFile());
    if (f.open(QIODevice::ReadOnly)) {
        QStringList list;
        for (const QString &l : QString::fromUtf8(f.readAll()).split('\n'))
            if (!l.trimmed().isEmpty())
                list << l.trimmed();
        if (!list.isEmpty())
            return list;
    }
    return { QString::fromUtf8("ลูกทุ่งฮิต"), QString::fromUtf8("เพื่อชีวิต"), QString::fromUtf8("สตริงยุค 90"),
             QString::fromUtf8("ลูกกรุง"), QString::fromUtf8("หมอลำ"), QString::fromUtf8("เพลงช้าซึ้งๆ"),
             QString::fromUtf8("เพลงสากลยุค 80 90"), QString::fromUtf8("เพลงฮิตตอนนี้") };
}

void YtHistory::setGenres(const QStringList &list)
{
    QDir().mkpath(dataDir());
    QFile f(genresFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(list.join('\n').toUtf8());
}

// ---- search ----------------------------------------------------------------

static QString fmtDuration(int sec)
{
    if (sec <= 0)
        return QString();
    if (sec >= 3600)
        return QString("%1:%2:%3").arg(sec / 3600).arg(sec / 60 % 60, 2, 10, QChar('0')).arg(sec % 60, 2, 10, QChar('0'));
    return QString("%1:%2").arg(sec / 60).arg(sec % 60, 2, 10, QChar('0'));
}

static QVector<YtItem> parseEntries(const QByteArray &json)
{
    QVector<YtItem> out;
    const QJsonArray entries = QJsonDocument::fromJson(json).object().value("entries").toArray();
    for (const QJsonValue &v : entries) {
        const QJsonObject o = v.toObject();
        YtItem it;
        it.id = o.value("id").toString();
        if (it.id.length() != 11)
            continue;           // channels / playlists
        it.title = o.value("title").toString();
        it.channel = o.value("channel").toString();
        if (it.channel.isEmpty())
            it.channel = o.value("uploader").toString();
        it.seconds = int(o.value("duration").toDouble());
        out << it;
    }
    return out;
}

// buttons sized to their text (the skinned dialog font is bigger than the default)
static void fitButton(QPushButton *b)
{
    b->setAutoDefault(false);
    b->setMinimumHeight(34);
    b->setMinimumWidth(b->fontMetrics().horizontalAdvance(b->text()) + 30);
}

YouTubeSearchDialog::YouTubeSearchDialog(QWidget *parent, const YtItem &seedItem)
    : QDialog(parent), seed(seedItem)
{
    setWindowTitle(tr("ค้นหาเพลงจาก YouTube"));
    resize(900, 680);
    thumbDir = QDir::tempPath() + "/pthk-yt-thumbs";
    QDir().mkpath(thumbDir);

    if (seed.id.isEmpty()) {        // follow the song sung last
        const QVector<YtHistory::Entry> h = YtHistory::all();
        qint64 last = 0;
        for (const YtHistory::Entry &e : h)
            if (e.last > last) {
                last = e.last;
                seed = e.item;
            }
    }

    edSearch = new QLineEdit(this);
    edSearch->setPlaceholderText(tr("พิมพ์ชื่อเพลง หรือชื่อนักร้อง แล้วกด Enter"));
    QFont f = edSearch->font();
    f.setPointSize(f.pointSize() + 3);
    edSearch->setFont(f);
    QPushButton *btnSearch = new QPushButton(tr("ค้นหา"), this);
    chkKaraoke = new QCheckBox(tr("เฉพาะคาราโอเกะ"), this);
    chkKaraoke->setChecked(true);

    genreBox = new QWidget(this);
    genreGrid = new QGridLayout(genreBox);
    genreGrid->setContentsMargins(0, 0, 0, 0);
    genreGrid->setSpacing(6);
    buildGenreButtons();

    list = new QListWidget(this);
    list->setIconSize(QSize(160, 90));
    list->setSpacing(2);
    QFont lf = list->font();
    lf.setPointSize(lf.pointSize() + 2);
    list->setFont(lf);

    lbStatus = new QLabel(this);

    QPushButton *btnPlay = new QPushButton(tr("เล่น"), this);
    QPushButton *btnQueue = new QPushButton(tr("+ เพิ่มเข้าคิว"), this);
    QPushButton *btnSave = new QPushButton(tr("ดาวน์โหลดเก็บไว้"), this);
    QPushButton *btnClose = new QPushButton(tr("ปิด"), this);
    for (QPushButton *b : { btnSearch, btnPlay, btnQueue, btnSave, btnClose })
        fitButton(b);

    QHBoxLayout *top = new QHBoxLayout();
    top->addWidget(edSearch, 1);
    top->addWidget(btnSearch);
    top->addWidget(chkKaraoke);

    QHBoxLayout *bottom = new QHBoxLayout();
    bottom->addWidget(lbStatus, 1);
    bottom->addWidget(btnPlay);
    bottom->addWidget(btnQueue);
    bottom->addWidget(btnSave);
    bottom->addWidget(btnClose);

    QVBoxLayout *v = new QVBoxLayout(this);
    v->addLayout(top);
    v->addWidget(genreBox);
    v->addWidget(list, 1);
    v->addLayout(bottom);

    auto play = [this]() {
        YtItem it = currentItem();
        if (it.id.isEmpty())
            return;
        emit playNow(it);
        accept();
    };
    auto queue = [this]() {
        YtItem it = currentItem();
        if (it.id.isEmpty())
            return;
        emit addToQueue(it);
        lbStatus->setText(tr("เพิ่มเข้าคิวแล้ว: ") + it.title);
    };
    connect(btnSearch, &QPushButton::clicked, this, &YouTubeSearchDialog::search);
    connect(edSearch, &QLineEdit::returnPressed, this, &YouTubeSearchDialog::search);
    connect(list, &QListWidget::itemActivated, this, play);
    connect(btnPlay, &QPushButton::clicked, this, play);
    connect(btnQueue, &QPushButton::clicked, this, queue);
    connect(btnSave, &QPushButton::clicked, this, [this]() {
        YtItem it = currentItem();
        if (!it.id.isEmpty())
            emit downloadRequested("https://www.youtube.com/watch?v=" + it.id);
    });
    connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);

    showHome();
}

YouTubeSearchDialog::~YouTubeSearchDialog()
{
    pendingThumbs.clear();
    // Search, suggestions and thumbnail downloads may still run (a song is usually picked before
    // all thumbnails arrive). Cut them off first: a "finished" handler running while this dialog
    // is torn down wrote into the already deleted list and crashed the program a little later.
    for (QProcess *p : findChildren<QProcess *>()) {
        p->disconnect();
        p->kill();
    }
}

void YouTubeSearchDialog::buildGenreButtons()
{
    while (QLayoutItem *li = genreGrid->takeAt(0)) {
        delete li->widget();
        delete li;
    }
    QStringList names = YtHistory::genres();
    static const int perRow = 6;
    int n = 0;
    auto place = [this, &n](QPushButton *b) {
        fitButton(b);
        b->setMinimumHeight(30);
        genreGrid->addWidget(b, n / perRow, n % perRow);
        n++;
    };

    QPushButton *home = new QPushButton(tr("หน้าแรก"), genreBox);
    connect(home, &QPushButton::clicked, this, [this]() {
        edSearch->clear();
        showHome();
    });
    place(home);
    for (const QString &g : names) {
        QPushButton *b = new QPushButton(g, genreBox);
        connect(b, &QPushButton::clicked, this, [this, g]() {
            edSearch->setText(g);
            search();
        });
        place(b);
    }
    QPushButton *rnd = new QPushButton(tr("สุ่มเพลง"), genreBox);
    rnd->setToolTip(tr("สุ่มจากเพลงที่เคยร้องและเพลงแนะนำ แล้วเพิ่มเข้าคิว"));
    rnd->setStyleSheet("QPushButton { font-weight: bold; }");
    connect(rnd, &QPushButton::clicked, this, &YouTubeSearchDialog::pickRandom);
    place(rnd);
    QPushButton *edit = new QPushButton(tr("แก้ไขปุ่ม..."), genreBox);
    edit->setToolTip(tr("เพิ่ม / ลบ / เปลี่ยนปุ่มแนวเพลง"));
    connect(edit, &QPushButton::clicked, this, &YouTubeSearchDialog::editGenres);
    place(edit);
}

void YouTubeSearchDialog::editGenres()
{
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(this, tr("ปุ่มแนวเพลง"),
            tr("หนึ่งบรรทัดต่อหนึ่งปุ่ม (กดปุ่มแล้วจะค้นหาคำนั้น เช่น ชื่อนักร้อง หรือแนวเพลง)"),
            YtHistory::genres().join('\n'), &ok);
    if (!ok)
        return;
    QStringList list;
    for (const QString &l : text.split('\n'))
        if (!l.trimmed().isEmpty())
            list << l.trimmed();
    YtHistory::setGenres(list);
    buildGenreButtons();
}

void YouTubeSearchDialog::keyPressEvent(QKeyEvent *e)
{
    if ((e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal) && !edSearch->hasFocus()) {
        YtItem it = currentItem();
        if (!it.id.isEmpty()) {
            emit addToQueue(it);
            lbStatus->setText(tr("เพิ่มเข้าคิวแล้ว: ") + it.title);
        }
        return;
    }
    if (e->key() == Qt::Key_Down && edSearch->hasFocus() && list->count() > 0) {
        list->setFocus();
        for (int i = 0; i < list->count(); i++)
            if (list->item(i)->flags() & Qt::ItemIsSelectable) {
                list->setCurrentRow(i);
                break;
            }
        return;
    }
    QDialog::keyPressEvent(e);
}

YtItem YouTubeSearchDialog::currentItem() const
{
    YtItem it;
    QListWidgetItem *li = list->currentItem();
    if (!li)
        return it;
    it.id = li->data(Qt::UserRole).toString();
    it.title = li->data(Qt::UserRole + 1).toString();
    it.channel = li->data(Qt::UserRole + 2).toString();
    it.seconds = li->data(Qt::UserRole + 3).toInt();
    return it;
}

void YouTubeSearchDialog::addHeader(const QString &text)
{
    QListWidgetItem *h = new QListWidgetItem(text, list);
    h->setFlags(Qt::NoItemFlags);
    QFont f = list->font();
    f.setBold(true);
    f.setPointSize(f.pointSize() + 1);
    h->setFont(f);
    h->setForeground(QColor(0x3a, 0x7b, 0xd5));
    h->setSizeHint(QSize(0, 38));
}

QListWidgetItem *YouTubeSearchDialog::addSong(const YtItem &it, const QString &extra)
{
    QString line2 = it.channel;
    if (it.seconds > 0)
        line2 += "   " + fmtDuration(it.seconds);
    if (!extra.isEmpty())
        line2 += "   " + extra;
    QListWidgetItem *li = new QListWidgetItem(it.title + "\n" + line2, list);
    li->setData(Qt::UserRole, it.id);
    li->setData(Qt::UserRole + 1, it.title);
    li->setData(Qt::UserRole + 2, it.channel);
    li->setData(Qt::UserRole + 3, it.seconds);
    li->setSizeHint(QSize(0, 98));
    QPixmap ph(160, 90);
    ph.fill(QColor(40, 40, 40));
    li->setIcon(QIcon(ph));
    loadThumb(it.id);
    return li;
}

// home page: sung lately, sung most, then YouTube's Mix after the seed song
void YouTubeSearchDialog::showHome()
{
    if (proc) {
        proc->disconnect(this);
        proc->kill();
    }
    homeMode = true;
    pendingThumbs.clear();
    list->clear();

    QVector<YtHistory::Entry> h = YtHistory::all();
    QSet<QString> shown;
    if (!h.isEmpty()) {
        std::sort(h.begin(), h.end(), [](const YtHistory::Entry &a, const YtHistory::Entry &b) { return a.last > b.last; });
        addHeader(tr("ร้องล่าสุด"));
        for (int i = 0; i < h.size() && i < 8; i++) {
            addSong(h[i].item, tr("ร้อง %1 ครั้ง").arg(h[i].count));
            shown.insert(h[i].item.id);
        }
        std::sort(h.begin(), h.end(), [](const YtHistory::Entry &a, const YtHistory::Entry &b) {
            return a.count != b.count ? a.count > b.count : a.last > b.last; });
        QVector<YtHistory::Entry> often;
        for (const YtHistory::Entry &e : h)
            if (e.count > 1 && !shown.contains(e.item.id) && often.size() < 8)
                often << e;
        if (!often.isEmpty()) {
            addHeader(tr("ร้องบ่อย"));
            for (const YtHistory::Entry &e : often)
                addSong(e.item, tr("ร้อง %1 ครั้ง").arg(e.count));
        }
    } else {
        addHeader(tr("ยังไม่มีเพลงที่เคยร้องจาก YouTube  -  ลองกดปุ่มแนวเพลงด้านบน หรือพิมพ์ชื่อเพลง"));
    }

    addHeader(seed.id.isEmpty() ? tr("เพลงฮิตแนะนำ")
                                : tr("เพลงต่อไป  (แนะนำต่อจาก \"%1\")").arg(seed.title.left(60)));
    QListWidgetItem *wait = new QListWidgetItem(tr("กำลังโหลดเพลงแนะนำ..."), list);
    wait->setFlags(Qt::NoItemFlags);
    wait->setData(Qt::UserRole + 9, "wait");

    lbStatus->setText(tr("Enter = เล่น (ถ้ามีเพลงเล่นอยู่จะต่อคิว)    + = เพิ่มเข้าคิว"));
    for (int i = 0; i < list->count(); i++)
        if (list->item(i)->flags() & Qt::ItemIsSelectable) {
            list->setCurrentRow(i);
            break;
        }
    edSearch->setFocus();
    loadSuggestions();
}

QProcess *YouTubeSearchDialog::startYtdlp(const QStringList &args)
{
    QProcess *p = new QProcess(this);
    setLowPriorityProcess(p);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    p->setProcessEnvironment(env);
    p->start(YtTools::ytdlpPath(), args);
    return p;
}

void YouTubeSearchDialog::loadSuggestions()
{
    auto setWaitText = [this](const QString &t) {
        for (int i = 0; i < list->count(); i++)
            if (list->item(i)->data(Qt::UserRole + 9).toString() == "wait")
                list->item(i)->setText(t);
    };
    if (YtTools::ytdlpPath().isEmpty()) {
        setWaitText(tr("(เพลงแนะนำจะแสดงหลังค้นหาเพลงครั้งแรก)"));
        return;
    }
    if (!suggestions.isEmpty()) {       // already loaded in this dialog
        for (int i = list->count() - 1; i >= 0; i--)
            if (list->item(i)->data(Qt::UserRole + 9).toString() == "wait")
                delete list->takeItem(i);
        for (const YtItem &it : suggestions)
            addSong(it);
        return;
    }
    if (mixProc)
        return;

    // YouTube's own "Mix" of songs like the seed song; without a seed, a search for hits
    const QStringList args = seed.id.isEmpty()
        ? QStringList { "--flat-playlist", "-J", "--no-warnings", "ytsearch25:" + QString::fromUtf8("เพลงฮิต คาราโอเกะ") }
        : QStringList { "--flat-playlist", "-J", "--no-warnings", "--playlist-end", "26",
                        "https://www.youtube.com/watch?v=" + seed.id + "&list=RD" + seed.id };
    mixProc = startYtdlp(args);
    connect(mixProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, setWaitText](int, QProcess::ExitStatus) {
        const QByteArray out = mixProc->readAllStandardOutput();
        mixProc->deleteLater();
        for (const YtItem &it : parseEntries(out))
            if (it.id != seed.id)
                suggestions << it;
        if (!homeMode)
            return;
        if (suggestions.isEmpty()) {
            setWaitText(tr("(โหลดเพลงแนะนำไม่ได้ ตรวจสอบอินเทอร์เน็ต)"));
            return;
        }
        loadSuggestions();
    });
}

void YouTubeSearchDialog::pickRandom()
{
    QVector<YtItem> pool = suggestions;
    for (const YtHistory::Entry &e : YtHistory::all())
        pool << e.item;
    for (int i = pool.size() - 1; i >= 0; i--)
        if (pool[i].id == seed.id && pool.size() > 1)
            pool.remove(i);
    if (pool.isEmpty()) {
        lbStatus->setText(tr("ยังไม่มีเพลงให้สุ่ม ลองค้นหาหรือร้องสักเพลงก่อน"));
        return;
    }
    const YtItem it = pool[QRandomGenerator::global()->bounded(pool.size())];
    emit playNow(it);       // plays when nothing is playing, else joins the queue
    lbStatus->setText(tr("สุ่มได้: %1  (กดอีกครั้งเพื่อสุ่มเพิ่ม)").arg(it.title));
}

// yt-dlp.exe is needed for the search (ffmpeg only for downloads)
bool YouTubeSearchDialog::ensureYtdlp()
{
    if (!YtTools::ytdlpPath().isEmpty())
        return true;
    int r = QMessageBox::question(this, windowTitle(),
                tr("ครั้งแรกต้องดาวน์โหลดตัวช่วยค้นหา yt-dlp (ประมาณ 18 MB, ครั้งเดียว)\n"
                   "จาก github.com/yt-dlp/yt-dlp ดาวน์โหลดเลยไหม?"));
    if (r != QMessageBox::Yes)
        return false;

    QDir().mkpath(YtTools::toolsDir());
    const QString part = YtTools::toolsDir() + "/yt-dlp.exe.part";
    QProgressDialog pd(tr("กำลังดาวน์โหลด yt-dlp..."), QString(), 0, 0, this);
    pd.setWindowModality(Qt::WindowModal);
    pd.setMinimumDuration(0);
    pd.show();
    QProcess curl;
    curl.start("curl.exe", { "-L", "--fail", "-s", "-o", QDir::toNativeSeparators(part), YTDLP_URL });
    while (!curl.waitForFinished(100))
        QApplication::processEvents();
    pd.close();
    if (curl.exitStatus() != QProcess::NormalExit || curl.exitCode() != 0) {
        QFile::remove(part);
        QMessageBox::warning(this, windowTitle(), tr("ดาวน์โหลดไม่สำเร็จ ตรวจสอบอินเทอร์เน็ตแล้วลองใหม่"));
        return false;
    }
    QFile::rename(part, YtTools::toolsDir() + "/yt-dlp.exe");
    return !YtTools::ytdlpPath().isEmpty();
}

void YouTubeSearchDialog::search()
{
    QString q = edSearch->text().trimmed();
    if (q.isEmpty()) {
        showHome();
        return;
    }
    if (!ensureYtdlp())
        return;
    if (chkKaraoke->isChecked() && !q.contains(QString::fromUtf8("คาราโอเกะ")) && !q.contains("karaoke", Qt::CaseInsensitive))
        q += QString::fromUtf8(" คาราโอเกะ");

    if (proc) {
        proc->disconnect(this);
        proc->kill();
    }
    homeMode = false;
    pendingThumbs.clear();
    list->clear();
    lbStatus->setText(tr("กำลังค้นหา \"%1\" ...").arg(q));

    proc = startYtdlp({ "--flat-playlist", "-J", "--no-warnings", "ytsearch20:" + q });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int, QProcess::ExitStatus) {
        const QByteArray out = proc->readAllStandardOutput();
        proc->deleteLater();
        for (const YtItem &it : parseEntries(out))
            addSong(it);
        if (list->count() == 0) {
            lbStatus->setText(tr("ไม่พบเพลง หรือเชื่อมต่อ YouTube ไม่ได้"));
            return;
        }
        lbStatus->setText(tr("พบ %1 เพลง    Enter = เล่น (ถ้ามีเพลงเล่นอยู่จะต่อคิว)    + = เพิ่มเข้าคิว").arg(list->count()));
        list->setCurrentRow(0);
        list->setFocus();
    });
}

// thumbnails with Windows' curl.exe (Qt's own HTTPS would need the OpenSSL DLLs)
void YouTubeSearchDialog::loadThumb(const QString &id)
{
    const QString file = thumbDir + "/" + id + ".jpg";
    if (QFile::exists(file)) {
        QPixmap pm(file);
        if (!pm.isNull()) {
            pm = pm.scaled(160, 90, Qt::KeepAspectRatio, Qt::FastTransformation);
            for (int i = 0; i < list->count(); i++)
                if (list->item(i)->data(Qt::UserRole).toString() == id)
                    list->item(i)->setIcon(QIcon(pm));
        }
        return;
    }
    if (!pendingThumbs.contains(id)) {
        pendingThumbs.append(id);
        pumpThumbs();
    }
}

void YouTubeSearchDialog::pumpThumbs()
{
    static const int kMaxConcurrentThumbs = 2;
    while (activeThumbs < kMaxConcurrentThumbs && !pendingThumbs.isEmpty()) {
        const QString id = pendingThumbs.takeFirst();
        const QString file = thumbDir + "/" + id + ".jpg";
        activeThumbs++;

        QProcess *p = new QProcess(this);
        setLowPriorityProcess(p);
        connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, p, id, file](int code, QProcess::ExitStatus) {
            p->deleteLater();
            activeThumbs--;
            if (code == 0 && QFile::exists(file)) {
                QPixmap pm(file);
                if (!pm.isNull()) {
                    pm = pm.scaled(160, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    for (int i = 0; i < list->count(); i++)
                        if (list->item(i)->data(Qt::UserRole).toString() == id)
                            list->item(i)->setIcon(QIcon(pm));
                }
            }
            pumpThumbs();
        });
        p->start("curl.exe", { "-s", "-L", "--fail", "-o", QDir::toNativeSeparators(file),
                               "https://i.ytimg.com/vi/" + id + "/mqdefault.jpg" });
    }
}

// ---- player ----------------------------------------------------------------

#ifdef PTHK_WEBENGINE
#include <QSettings>

static const char *kPlayerOrigin = "https://pthk-karaoke.local";

YouTubePlayer::YouTubePlayer(QWidget *parent) : QWebEngineView(parent)
{
    // Clean up any stale persistent ServiceWorker / CacheStorage data that crashes QtWebEngine 5.15
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!appData.isEmpty()) {
        QDir(appData + "/QtWebEngine/Default/Service Worker").removeRecursively();
        QDir(appData + "/QtWebEngine/Default/Cache").removeRecursively();
    }

    // Use an off-the-record (in-memory) profile and disable CacheStorage/ServiceWorker APIs
    // to prevent Chromium 83 LegacyCacheStorageCache null-pointer crashes on YouTube embeds.
    QWebEngineProfile *prof = new QWebEngineProfile(this);
    prof->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    prof->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);

    QWebEngineScript swDisableScript;
    swDisableScript.setName(QStringLiteral("DisableServiceWorkerAndCacheStorage"));
    swDisableScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
    swDisableScript.setWorldId(QWebEngineScript::MainWorld);
    swDisableScript.setRunsOnSubFrames(true);
    swDisableScript.setSourceCode(QStringLiteral(
        "(function(){"
        "  var dummyCache = { open: function(){ return Promise.reject(); }, match: function(){ return Promise.reject(); }, has: function(){ return Promise.resolve(false); }, delete: function(){ return Promise.resolve(false); }, keys: function(){ return Promise.resolve([]); } };"
        "  try { Object.defineProperty(window, 'caches', { get: function(){ return dummyCache; }, configurable: true }); } catch(e) {}"
        "  try { Object.defineProperty(navigator, 'serviceWorker', { get: function(){ return undefined; }, configurable: true }); } catch(e) {}"
        "  try { if (navigator.storage && navigator.storage.estimate) {"
        "    navigator.storage.estimate = function(){ return Promise.resolve({usage:0, quota:0}); };"
        "  } } catch(e) {}"
        "})();"
    ));
    prof->scripts()->insert(swDisableScript);

    QWebEnginePage *customPage = new QWebEnginePage(prof, this);
    setPage(customPage);

    page()->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    page()->setBackgroundColor(Qt::black);
    setContextMenuPolicy(Qt::NoContextMenu);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents);   // clicks / right-click go to the karaoke screen
    pollTimer.setInterval(250);
    connect(&pollTimer, &QTimer::timeout, this, &YouTubePlayer::poll);
}

void YouTubePlayer::playVideo(const QString &videoId, int volume)
{
    static const QRegularExpression validId("^[A-Za-z0-9_-]{11}$");
    if (!validId.match(videoId).hasMatch()) {
        emit failed(tr("รหัสคลิปไม่ถูกต้อง"), false);
        return;
    }
    active = true;
    started = false;
    paused = false;
    duration = 0;
    waitedMs = 0;

    const int clampedVol = qBound(0, volume, 100);
    const QString vq = QSettings().value("YtVideoQuality", "hd720").toString();

    // If IFrame is already loaded and ready, tell JS to switch video directly via loadVideoById!
    if (pageReady) {
        const QString js = QString(
            "if (window.pthkPlayerReady && window.P && typeof window.P.loadVideoById === 'function') {"
            "  E = 0; P.setVolume(%1);"
            "  P.loadVideoById({videoId: '%2', suggestedQuality: '%3'});"
            "} else {"
            "  window.pendingVideo = {id: '%2', vol: %1, q: '%3'};"
            "}"
        ).arg(clampedVol).arg(videoId).arg(vq);
        page()->runJavaScript(js);
        pollTimer.start();
        return;
    }

    const QString html = QString(
        "<!DOCTYPE html><html><head><meta charset='utf-8'><style>"
        "html,body{margin:0;height:100%;background:#000;overflow:hidden}#p{width:100%;height:100%}"
        "</style></head><body><div id='p'></div><script>"
        "var P=null,E=0,VQ='%4',pthkPlayerReady=false,pendingVideo=null;"
        "function onYouTubeIframeAPIReady(){P=new YT.Player('p',{"
        "width:'100%',height:'100%',videoId:'%1',"
        "playerVars:{autoplay:1,controls:0,rel:0,modestbranding:1,iv_load_policy:3,playsinline:1,fs:0,disablekb:1,origin:'%3'},"
        "events:{"
        "onReady:function(e){"
        "  pthkPlayerReady=true;"
        "  if(pendingVideo){"
        "    e.target.setVolume(pendingVideo.vol);"
        "    if(pendingVideo.q&&e.target.setPlaybackQuality)e.target.setPlaybackQuality(pendingVideo.q);"
        "    e.target.loadVideoById({videoId:pendingVideo.id,suggestedQuality:pendingVideo.q});"
        "    pendingVideo=null;"
        "  }else{"
        "    e.target.setVolume(%2);"
        "    if(VQ&&e.target.setPlaybackQuality)e.target.setPlaybackQuality(VQ);"
        "    e.target.playVideo();"
        "  }"
        "},"
        "onStateChange:function(e){if(e.data===1&&VQ&&e.target.setPlaybackQuality)e.target.setPlaybackQuality(VQ);},"
        "onError:function(e){E=e.data;}"
        "}});}"
        "function pthkState(){if(!P||!P.getPlayerState)return JSON.stringify({s:-2,e:E});"
        "return JSON.stringify({s:P.getPlayerState(),t:P.getCurrentTime(),d:P.getDuration(),e:E});}"
        "</script><script src='https://www.youtube.com/iframe_api'></script></body></html>")
        .arg(videoId).arg(clampedVol).arg(kPlayerOrigin).arg(vq);
    setHtml(html, QUrl(QString(kPlayerOrigin) + "/"));
    pageReady = true;
    pollTimer.start();
}

void YouTubePlayer::poll()
{
    page()->runJavaScript("typeof pthkState=='function'?pthkState():''", [this](const QVariant &v) {
        if (!active)
            return;
        const QJsonObject o = QJsonDocument::fromJson(v.toString().toUtf8()).object();
        const int err = o.value("e").toInt();
        if (err != 0) {
            active = false;
            pollTimer.stop();
            QString msg;
            bool canDownload = false;
            switch (err) {
            case 2:   msg = tr("รหัสคลิปไม่ถูกต้อง"); break;
            case 5:   msg = tr("ตัวเล่น YouTube เล่นคลิปนี้ไม่ได้"); canDownload = true; break;
            case 100: msg = tr("ไม่พบคลิปนี้ (ถูกลบ หรือเป็นคลิปส่วนตัว)"); break;
            default:  msg = tr("เจ้าของคลิปไม่อนุญาตให้เล่นนอกเว็บ YouTube"); canDownload = true; break;
            }
            emit failed(msg, canDownload);
            return;
        }
        const int state = o.isEmpty() ? -2 : o.value("s").toInt();
        if (state == 1 || state == 2 || state == 3)
            started = true;
        if (!started) {
            waitedMs += pollTimer.interval();
            if (waitedMs > 25000) {
                active = false;
                pollTimer.stop();
                emit failed(tr("โหลดคลิปจาก YouTube ไม่ได้ ตรวจสอบอินเทอร์เน็ต"), false);
            }
            return;
        }
        const qint64 d = qint64(o.value("d").toDouble() * 1000);
        if (d > 0 && d != duration) {
            duration = d;
            emit durationChanged(d);
        }
        emit positionChanged(qint64(o.value("t").toDouble() * 1000));
        if (state == 0) {           // ended
            active = false;
            pollTimer.stop();
            emit ended();
        }
    });
}

void YouTubePlayer::pause()
{
    paused = true;
    page()->runJavaScript("P&&P.pauseVideo&&P.pauseVideo()");
}

void YouTubePlayer::resume()
{
    paused = false;
    page()->runJavaScript("P&&P.playVideo&&P.playVideo()");
}

void YouTubePlayer::seek(qint64 ms, bool allowSeekAhead)
{
    page()->runJavaScript(QString("P&&P.seekTo&&P.seekTo(%1,%2)").arg(ms / 1000.0).arg(allowSeekAhead ? "true" : "false"));
}

void YouTubePlayer::setVolume(int volume)
{
    page()->runJavaScript(QString("P&&P.setVolume&&P.setVolume(%1)").arg(qBound(0, volume, 100)));
}

void YouTubePlayer::stopVideo()
{
    active = false;
    paused = false;
    started = false;
    pollTimer.stop();
    if (pageReady) {
        page()->runJavaScript("if (window.P && typeof window.P.stopVideo === 'function') P.stopVideo();");
    }
}
#endif

