// MainWindow: Adding songs: import dialog, .zip/.rar archives, drag & drop, new-song scan
#include "MainWindow_p.h"

// .zip / .rar / .7z among the files: unpacked to a temporary folder, the songs inside are
// imported like any others (they are copied into the library), then the folder is removed.
// Windows' own tar (libarchive: zip, rar, 7z) is used, WinRAR's UnRAR or 7-Zip as fallbacks.
// number of files listed in an archive (Windows tar), 0 when unknown
static int archiveEntryCount(const QString &archive)
{
    const QString sysTar = QDir(qEnvironmentVariable("SystemRoot", "C:/Windows")).filePath("System32/tar.exe");
    if (!QFile::exists(sysTar))
        return 0;
    QProcess proc;
    proc.start(sysTar, { "-tf", QDir::toNativeSeparators(archive) });
    if (!proc.waitForStarted(5000))
        return 0;
    while (!proc.waitForFinished(100))
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    if (proc.exitCode() != 0)
        return 0;
    int n = 0;
    for (const QByteArray &line : proc.readAllStandardOutput().split('\n'))
        if (!line.trimmed().isEmpty() && !line.trimmed().endsWith('/'))
            ++n;
    return n;
}

static int countFiles(const QString &dir)
{
    int n = 0;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); ++n; }
    return n;
}

// progress(doneFiles) is called about twice a second while unpacking
static bool unpackArchive(const QString &archive, const QString &dir, QString *error,
                          const std::function<void(int)> &progress = nullptr)
{
    struct Tool { QString exe; QStringList args; };
    QList<Tool> tools;
    const QString sysTar = QDir(qEnvironmentVariable("SystemRoot", "C:/Windows")).filePath("System32/tar.exe");
    if (QFile::exists(sysTar))
        tools << Tool{ sysTar, { "-xf", QDir::toNativeSeparators(archive), "-C", QDir::toNativeSeparators(dir) } };
    for (const QString &unrar : { QStringLiteral("C:/Program Files/WinRAR/UnRAR.exe"),
                                  QStringLiteral("C:/Program Files (x86)/WinRAR/UnRAR.exe") })
        if (QFile::exists(unrar))
            tools << Tool{ unrar, { "x", "-o+", "-y", "-inul", QDir::toNativeSeparators(archive),
                                    QDir::toNativeSeparators(dir) + "\\" } };
    for (const QString &sz : { QStringLiteral("C:/Program Files/7-Zip/7z.exe"),
                               QStringLiteral("C:/Program Files (x86)/7-Zip/7z.exe") })
        if (QFile::exists(sz))
            tools << Tool{ sz, { "x", "-y", "-o" + QDir::toNativeSeparators(dir), QDir::toNativeSeparators(archive) } };

    for (const Tool &t : tools) {
        QProcess proc;
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start(t.exe, t.args);
        if (!proc.waitForStarted(5000))
            continue;
        QElapsedTimer tick;
        tick.start();
        while (!proc.waitForFinished(100)) {
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            if (progress && tick.elapsed() > 500) {
                progress(countFiles(dir));
                tick.restart();
            }
        }
        const bool gotFiles = !QDir(dir).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
        if (progress)
            progress(countFiles(dir));
        if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0 && gotFiles)
            return true;
        if (gotFiles) {             // partly unpacked (e.g. one bad entry): use what came out
            if (error) *error = QObject::tr("แตกไฟล์ได้ไม่ครบ");
            return true;
        }
    }
    if (error)
        *error = tools.isEmpty() ? QObject::tr("ไม่มีโปรแกรมแตกไฟล์ในเครื่อง")
                                 : QObject::tr("แตกไฟล์ไม่ได้ (ไฟล์เสีย หรือมีรหัสผ่าน)");
    return false;
}

QStringList MainWindow::importSongs(const QStringList &files, bool quiet)
{
    static const QStringList archiveExts = { "zip", "rar", "7z" };
    QStringList plain, tempDirs, archiveProblems;
    for (const QString &f : files) {
        const QString ext = QFileInfo(f).suffix().toLower();
        if (!archiveExts.contains(ext)) {
            plain << f;
            continue;
        }
        const QString dir = TEMP_DIR_PATH + "/import-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir().mkpath(dir);
        tempDirs << dir;
        const QString name = QFileInfo(f).fileName();
        QProgressDialog wait(dialogParent());
        wait.setWindowTitle(tr("เพิ่มเพลงจากไฟล์บีบอัด"));
        wait.setCancelButton(nullptr);
        wait.setWindowModality(Qt::WindowModal);
        wait.setMinimumDuration(0);
        wait.setMinimumSize(560, 150);
        wait.setLabelText(tr("กำลังอ่านรายการไฟล์ใน\n%1").arg(name));
        wait.setRange(0, 0);
        wait.show();
        QApplication::processEvents();
        const int total = archiveEntryCount(f);
        if (total > 0)
            wait.setRange(0, total);
        wait.setLabelText(tr("กำลังแตกไฟล์ (ขั้นที่ 1 จาก 2)\n%1").arg(name));
        QString err;
        const bool ok = unpackArchive(f, dir, &err, [&](int done) {
            if (total > 0) {
                wait.setValue(qMin(done, total));
                wait.setLabelText(tr("กำลังแตกไฟล์ (ขั้นที่ 1 จาก 2)\n%1\n%2 / %3 ไฟล์").arg(name).arg(done).arg(total));
            } else {
                wait.setLabelText(tr("กำลังแตกไฟล์ (ขั้นที่ 1 จาก 2)\n%1\n%2 ไฟล์").arg(name).arg(done));
            }
        });
        wait.close();
        if (!ok) {
            archiveProblems << name + "  (" + err + ")";
            continue;
        }
        if (!err.isEmpty())
            archiveProblems << name + "  (" + err + ")";
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString inner = it.next();
            if (archiveExts.contains(QFileInfo(inner).suffix().toLower())) {
                // an archive inside the archive: one more level
                const QString sub = inner + "-unpacked";
                QDir().mkpath(sub);
                QString e2;
                if (unpackArchive(inner, sub, &e2)) {
                    QDirIterator it2(sub, QDir::Files, QDirIterator::Subdirectories);
                    while (it2.hasNext())
                        plain << it2.next();
                }
                continue;
            }
            plain << inner;
        }
    }
    if (!archiveProblems.isEmpty() && !quiet)
        QMessageBox::warning(dialogParent(), tr("เพิ่มเพลงจากไฟล์บีบอัด"),
                             tr("บางไฟล์แตกไม่ได้:\n") + archiveProblems.join("\n"));

    QStringList ids;
    if (!plain.isEmpty())
        ids = importSongsImpl(plain, quiet);
    for (const QString &d : tempDirs)
        QDir(d).removeRecursively();        // the songs were copied into the library
    return ids;
}

QStringList MainWindow::importSongsImpl(const QStringList &files, bool quiet)
{
    QStringList ids;
    QStringList videoExts = { "mp4", "mkv", "avi", "mpg", "mpeg", "wmv", "vob", "dat" };
    QStringList added, problems;

    QProgressDialog progress(tr("กำลังเพิ่มเพลง..."), tr("ยกเลิก"), 0, files.count(), dialogParent());
    progress.setWindowTitle(tr("เพิ่มเพลง"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);

    // NCN songs: a .mid with its .lyr and .cur of the same name (among the chosen files, next to
    // the .mid, or in Lyrics / Cursor folders laid out like an NCN library) is an NCN song. Its three
    // files are copied into the NCN library (Song / Lyrics / Cursor, sub folder = first letter, like
    // the songs already there) and added to the database at once.
    QHash<QString, QString> chosenLyr, chosenCur;       // lower-case name -> file
    for (const QString &f : files) {
        QFileInfo fi(f);
        const QString ext = fi.suffix().toLower();
        if (ext == "lyr")
            chosenLyr.insert(fi.completeBaseName().toLower(), fi.absoluteFilePath());
        else if (ext == "cur")
            chosenCur.insert(fi.completeBaseName().toLower(), fi.absoluteFilePath());
    }
    auto findPart = [](const QFileInfo &mid, const QString &folder, const QString &ext,
                       const QHash<QString, QString> &chosen) -> QString {
        const QString base = mid.completeBaseName();
        if (chosen.contains(base.toLower()))
            return chosen.value(base.toLower());
        QDir d(mid.absolutePath());
        const QString sub = d.dirName();
        QStringList dirs = { d.absolutePath() };
        QDir up(d);
        if (up.cdUp()) {
            dirs << up.absoluteFilePath(folder) << up.absoluteFilePath(folder + "/" + sub);
            QDir up2(up);
            if (up2.cdUp())
                dirs << up2.absoluteFilePath(folder + "/" + sub);
        }
        for (const QString &dir : dirs) {
            const QString p = dir + "/" + base + "." + ext;     // NTFS: any upper / lower case
            if (QFile::exists(p))
                return QFileInfo(p).absoluteFilePath();
        }
        return QString();
    };

    // first find the NCN songs (a folder of songs as received can hold tens of thousands)
    struct NcnSong { QString mid, lyr, cur; };
    QVector<NcnSong> ncnSongs;
    QSet<QString> ncnDone;          // .mid files handled as NCN songs
    progress.setMinimumWidth(460);
    progress.setLabelText(tr("กำลังตรวจไฟล์เพลง..."));
    QApplication::processEvents();
    for (const QString &f : files) {
        QFileInfo mid(f);
        const QString ext = mid.suffix().toLower();
        if (ext != "mid" && ext != "midi")
            continue;
        const QString lyr = findPart(mid, "Lyrics", "lyr", chosenLyr);
        const QString cur = findPart(mid, "Cursor", "cur", chosenCur);
        if (lyr.isEmpty() || cur.isEmpty())
            continue;               // a plain MIDI / KAR song
        ncnDone.insert(f);
        ncnSongs.append({ mid.absoluteFilePath(), lyr, cur });
    }

    const QString ncnRoot = db->ncnPath();
    int ncnSkipped = 0;
    int duplicateSkipped = 0;
    QSet<QString> existingSongKeys = db->songNameArtistKeys();
    if (!ncnSongs.isEmpty()) {
        progress.setMaximum(ncnSongs.size() + files.count());
        const QSet<QString> inDb = db->songPaths("NCN");
        db->beginBatch();
        for (int n = 0; n < ncnSongs.size(); n++) {
            if (progress.wasCanceled())
                break;
            if (n % 20 == 0) {
                progress.setValue(n);
                progress.setLabelText(tr("กำลังเพิ่มเพลง NCN  %1 / %2\nเพิ่มแล้ว %3   มีอยู่แล้ว %4")
                                      .arg(n + 1).arg(ncnSongs.size()).arg(added.size()).arg(ncnSkipped));
                QApplication::processEvents();
            }
            const QFileInfo mid(ncnSongs[n].mid);
            const QString lyr = ncnSongs[n].lyr;
            const QString cur = ncnSongs[n].cur;

            const QString id = mid.completeBaseName();
            if (ncnRoot.isEmpty() || !QDir(ncnRoot + "/Song").exists()) {
                problems << id + tr("  (ยังไม่ได้ตั้งโฟลเดอร์ NCN ในหน้าตั้งค่า)");
                break;
            }

            const QString sub = id.left(1).toUpper();
            const QString dstMid = ncnRoot + "/Song/" + sub + "/" + mid.fileName();
            const QString dstLyr = ncnRoot + "/Lyrics/" + sub + "/" + QFileInfo(lyr).fileName();
            const QString dstCur = ncnRoot + "/Cursor/" + sub + "/" + QFileInfo(cur).fileName();

            QString rel = dstMid;
            rel.replace(ncnRoot, "");
            const bool inLibrary = QFileInfo(dstMid).absoluteFilePath().compare(mid.absoluteFilePath(), Qt::CaseInsensitive) == 0;
            const bool sameFile = QFile::exists(dstMid) && QFileInfo(dstMid).size() == mid.size();
            if ((inLibrary || sameFile) && inDb.contains(rel.toLower())) {
                ncnSkipped++;           // already in the library and in the database
                continue;
            }

            // Check if song name + artist is already in library (skip duplicate songs)
            QString ncnTitle, ncnArtist;
            QFile lyrF(lyr);
            if (lyrF.open(QFile::ReadOnly)) {
                QStringList lines = Utils::decodeThaiText(lyrF.read(4096)).split(QRegExp("[\r\n]+"), Qt::SkipEmptyParts);
                lyrF.close();
                if (lines.size() > 0) ncnTitle = lines[0].trimmed();
                if (lines.size() > 1) ncnArtist = lines[1].trimmed();
            }
            const QString songKey = SongDatabase::makeSongKey(ncnTitle, ncnArtist);
            if (!songKey.isEmpty() && existingSongKeys.contains(songKey)) {
                duplicateSkipped++;
                continue;
            }
            if (!inLibrary) {
                if (QFile::exists(dstMid) && QFileInfo(dstMid).size() != mid.size()) {
                    problems << id + tr("  (มีเพลงรหัสนี้ในคลังแล้ว แต่เป็นไฟล์อื่น จึงไม่เขียนทับ)");
                    continue;
                }
                bool copied = true;
                const QList<QPair<QString, QString>> parts = { { mid.absoluteFilePath(), dstMid }, { lyr, dstLyr }, { cur, dstCur } };
                for (const auto &pr : parts) {
                    QDir().mkpath(QFileInfo(pr.second).absolutePath());
                    if (!QFile::exists(pr.second) && !QFile::copy(pr.first, pr.second))
                        copied = false;
                }
                if (!copied) {
                    problems << id + tr("  (คัดลอกไม่ได้)");
                    continue;
                }
            }

            db->removeSongFile("NCN", rel);
            if (db->insertNCN(ncnRoot, id, dstMid)) {
                if (!songKey.isEmpty())
                    existingSongKeys.insert(songKey);
                ids << id;
                added << id + "   (NCN)";
            } else {
                problems << id + tr("  (ไฟล์ NCN เสียหาย อ่านไม่ได้)");
            }
        }
        db->endBatch();
    }

    for (int i = 0; i < files.count(); i++) {
        if (i % 200 == 0)
            progress.setValue(ncnSongs.size() + i);
        if (progress.wasCanceled())
            break;

        QFileInfo fi(files[i]);
        QString ext = fi.suffix().toLower();
        if (ncnDone.contains(files[i]) || ext == "lyr" || ext == "cur")
            continue;               // NCN song parts, done above
        if ((ext == "mid" || ext == "midi") && !ncnSongs.isEmpty()) {
            problems << fi.fileName() + tr("  (เพลง NCN ไม่ครบ: ไม่มีไฟล์ .lyr หรือ .cur)");
            continue;
        }
        QString type, root;
        if (videoExts.contains(ext)) {
            type = "VIDEO";
            root = db->videoPath();
        } else if (ext == "mp3") {
            type = "MP3";
            root = db->mp3Path();
        } else if (ext == "emk") {
            type = "EMK";
            root = db->ncnPath();
        } else if (ext == "sib") {
            type = "SIB";
            root = db->sibPath().isEmpty() ? "D:/REA/SIBSIB KARAOKE/SONG/MIDI/SIB" : db->sibPath();
        } else if (ext == "kar" || ext == "mid" || ext == "midi") {
            type = "KAR";
            root = db->karPath();
        } else {
            if (ext != "lrc")       // a .lrc comes along with its MP3
                problems << fi.fileName() + tr("  (ไม่รองรับชนิดไฟล์นี้)");
            continue;
        }

        QString songTitle, songArtist;
        if (type == "VIDEO" || type == "MP3") {
            QString base = fi.completeBaseName();
            QRegExp numPrefix("^(\\d+)[\\s_\\-.]+(.*)$");
            if (numPrefix.indexIn(base) == 0 && !numPrefix.cap(2).trimmed().isEmpty()) {
                base = numPrefix.cap(2);
            }
            songTitle = base.trimmed();
            int sep = base.indexOf(" - ");
            if (sep > 0) {
                songTitle = base.left(sep).trimmed();
                songArtist = base.mid(sep + 3).trimmed();
            }
        } else if (type == "EMK") {
            PTHKCore::EmkSong s = PTHKCore::readEmk(fi.absoluteFilePath());
            songTitle = s.title.trimmed();
            songArtist = s.artist.trimmed();
        } else if (type == "SIB") {
            PTHKCore::SibSong s = PTHKCore::readSib(fi.absoluteFilePath());
            songTitle = s.title.trimmed();
            songArtist = s.artist.trimmed();
        } else if (type == "KAR") {
            songTitle = fi.completeBaseName().trimmed();
        }

        const QString mediaKey = SongDatabase::makeSongKey(songTitle, songArtist);
        if (!mediaKey.isEmpty() && existingSongKeys.contains(mediaKey)) {
            duplicateSkipped++;
            continue;
        }

        root = QDir::fromNativeSeparators(root);
        while (root.endsWith('/'))
            root.chop(1);
        if (!QDir().mkpath(root)) {
            problems << fi.fileName() + tr("  (สร้างโฟลเดอร์ไม่ได้: ") + root + ")";
            continue;
        }

        QString src = QDir::fromNativeSeparators(fi.absoluteFilePath());
        QString dst = src;
        if (!src.startsWith(root + "/", Qt::CaseInsensitive)) {
            dst = root + "/" + fi.fileName();
            progress.setLabelText(tr("กำลังคัดลอก ") + fi.fileName());
            if (!QFile::exists(dst) && !QFile::copy(src, dst)) {
                problems << fi.fileName() + tr("  (คัดลอกไม่ได้)");
                continue;
            }
            if (type == "MP3") {
                QString lrc = fi.absolutePath() + "/" + fi.completeBaseName() + ".lrc";
                QString lrcDst = root + "/" + fi.completeBaseName() + ".lrc";
                if (QFile::exists(lrc) && !QFile::exists(lrcDst))
                    QFile::copy(lrc, lrcDst);
            }
        }

        QString rel = dst.mid(root.length());
        db->removeSongFile(type, rel);
        bool ok = (type == "EMK") ? db->insertEMK(root, fi.completeBaseName(), dst)
                                  : (type == "SIB") ? db->insertSIB(root, fi.completeBaseName(), dst)
                                  : (type == "KAR") ? db->insertKAR(root, "-------", dst, QFileInfo(dst).fileName())
                                  : db->insertVideo(root, dst, QFileInfo(dst).fileName(), type, type == "MP3" ? "M" : "V");
        if (ok) {
            if (!mediaKey.isEmpty())
                existingSongKeys.insert(mediaKey);
            ids << db->songIdFor(type, rel);
            added << ids.last() + "   " + QFileInfo(dst).completeBaseName();
        } else
            problems << fi.fileName() + tr("  (อ่านไฟล์ไม่ได้)");
    }
    progress.setValue(progress.maximum());

    QString msg;
    if (!added.isEmpty())
        msg += tr("เพิ่มแล้ว %1 เพลง (พิมพ์รหัสหรือชื่อเพลงเพื่อเล่น):\n").arg(added.count()) + added.mid(0, 15).join("\n");
    if (added.count() > 15)
        msg += "\n...";
    if (ncnSkipped > 0)
        msg += (msg.isEmpty() ? "" : "\n\n") + tr("มีอยู่ในคลังแล้ว %1 เพลง (ข้าม)").arg(ncnSkipped);
    if (duplicateSkipped > 0)
        msg += (msg.isEmpty() ? "" : "\n\n") + tr("ชื่อเพลงและศิลปินซ้ำกับในคลัง %1 เพลง (ข้าม)").arg(duplicateSkipped);
    if (!problems.isEmpty())
        msg += (msg.isEmpty() ? "" : "\n\n") + tr("เพิ่มไม่ได้ %1 เพลง:\n").arg(problems.count()) + problems.mid(0, 10).join("\n");
    if (problems.count() > 10)
        msg += "\n...";
    if (msg.isEmpty())
        msg = tr("ไม่พบไฟล์เพลงที่เพิ่มได้ในที่ที่เลือก");
    if (!added.isEmpty())
        refreshArtistCodes();
    if (!msg.isEmpty() && (!quiet || !problems.isEmpty()))
        QMessageBox::information(dialogParent(), tr("เพิ่มเพลง"), msg);
    setFocus();
    return ids;
}

void MainWindow::showYouTubeDownload(const QString &url)
{
    QString dir = QDir::fromNativeSeparators(db->videoPath());
    while (dir.endsWith('/'))
        dir.chop(1);
    YouTubeDownloadDialog *dlg = new YouTubeDownloadDialog(dir + "/YouTube", url, dialogParent());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &YouTubeDownloadDialog::downloaded, this, [this](const QString &file, bool enqueue) {
        enqueueAiStems(file);       // AI vocal separation in the background, once
        const QStringList ids = importSongs({ file }, enqueue);
        if (!enqueue || ids.isEmpty())
            return;
        Song *s = db->search(ids.first());
        if (!s || s->id() != ids.first())
            return;
        addToPlaylist(s);
        hideUIFrame();
        if (auto_playnext && !isSongPlaying())
            play(playlist.count() - 1);
        else {
            showPlaylist();
            timer2->start(playlist_timeout);    // the queue shows briefly, then hides again
        }
    });
    dlg->show();
}

// new NCN / HNK files only (the full update reads all ~90,000 songs again)
void MainWindow::updateNewSongs()
{
    if (db->isRunning())
        return;
    db->setUpdateType(UpdateType::UpdateNewSongs);
    auto *conn = new QMetaObject::Connection;
    *conn = connect(db, &QThread::finished, this, [this, conn]() {
        disconnect(*conn);
        delete conn;
        if (db->lastAdded() > 0 || db->lastRemoved() > 0)
            refreshArtistCodes();
        QString infoText;
        if (db->lastAdded() == 0 && db->lastRemoved() == 0 && db->lastSkippedDuplicate() == 0) {
            infoText = tr("ไม่มีเพลงใหม่ ฐานข้อมูลตรงกับโฟลเดอร์ NCN แล้ว");
        } else {
            infoText = tr("เพิ่มเพลงใหม่ %1 เพลง\n").arg(db->lastAdded());
            if (db->lastSkippedDuplicate() > 0)
                infoText += tr("ชื่อเพลงและศิลปินซ้ำ (ข้าม) %1 เพลง\n").arg(db->lastSkippedDuplicate());
            infoText += tr("ลบเพลงที่ไม่มีไฟล์แล้ว %1 เพลง").arg(db->lastRemoved());
        }
        QMessageBox::information(dialogParent(), tr("เพิ่มเพลง NCN ใหม่"), infoText);
    });
    db->start();
}

// ---- song transitions ------------------------------------------------------------------------


// "+ เพิ่มเพลง": a whole folder of new songs (e.g. a set of NCN songs as received) or single files
void MainWindow::showImportDialog()
{
    QMenu m(this);
    QAction *aFolder = m.addAction(tr("เลือกทั้งโฟลเดอร์ (เช่น โฟลเดอร์เพลง NCN ที่ได้มา)..."));
    QAction *aFiles = m.addAction(tr("เลือกไฟล์เพลง (NCN / วิดีโอ / MP3 / KAR)..."));
    QAction *aArchive = m.addAction(tr("เพิ่มจากไฟล์บีบอัด (.zip / .rar / .7z)..."));
    QAction *pick = m.exec(QCursor::pos());
    if (pick == nullptr)
        return;

    QStringList files;
    if (pick == aFolder) {
        const QString dir = QFileDialog::getExistingDirectory(dialogParent(), tr("เลือกโฟลเดอร์ที่มีเพลงใหม่"),
                                                              Utils::LAST_OPEN_DIR);
        if (dir.isEmpty())
            return;
        Utils::LAST_OPEN_DIR = dir;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            files << it.next();
            if (files.size() % 2000 == 0)
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        QApplication::restoreOverrideCursor();
    } else if (pick == aArchive) {
        files = QFileDialog::getOpenFileNames(
                    dialogParent(), tr("เลือกไฟล์บีบอัดที่มีเพลง"), Utils::LAST_OPEN_DIR,
                    tr("ไฟล์บีบอัด (*.zip *.rar *.7z);;ทุกไฟล์ (*)"));
        if (files.isEmpty())
            return;
        Utils::LAST_OPEN_DIR = QFileInfo(files.first()).absolutePath();
    } else if (pick == aFiles) {
        files = QFileDialog::getOpenFileNames(
                    dialogParent(), tr("เลือกไฟล์เพลงที่จะเพิ่ม (เพลง NCN เลือก .mid .lyr .cur มาด้วยกัน)"), Utils::LAST_OPEN_DIR,
                    tr("เพลงคาราโอเกะ (*.mid *.midi *.lyr *.cur *.mp4 *.mkv *.avi *.mpg *.mpeg *.wmv *.vob *.dat *.mp3 *.kar *.emk *.sib *.zip *.rar *.7z);;ไฟล์บีบอัด (*.zip *.rar *.7z);;ทุกไฟล์ (*)"));
        if (files.isEmpty())
            return;
        Utils::LAST_OPEN_DIR = QFileInfo(files.first()).absolutePath();
    }
    if (!files.isEmpty())
        importSongs(files);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    QStringList files;
    for (const QUrl &u : event->mimeData()->urls()) {
        QFileInfo fi(u.toLocalFile());
        if (fi.isDir()) {           // a dropped folder: every file in it
            QDirIterator it(fi.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext())
                files << it.next();
        } else if (fi.isFile()) {
            files << fi.absoluteFilePath();
        }
    }
    event->acceptProposedAction();
    if (!files.isEmpty())
        importSongs(files);
}
