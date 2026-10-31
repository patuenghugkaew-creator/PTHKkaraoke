// MainWindow: Automatic update from GitHub Releases
#include "MainWindow_p.h"

void MainWindow::showCheckUpdateDialog()
{
    checkForUpdate(true);
}

// Asks GitHub for the newest release in the background.
// manual: the user clicked the menu (always answers); otherwise quiet unless there is an update.
void MainWindow::checkForUpdate(bool manual)
{
    if (updateChecking)
        return;
    updateChecking = true;
    if (manual)
        QApplication::setOverrideCursor(Qt::WaitCursor);
    QPointer<MainWindow> self(this);
    std::thread([self, manual]() {
        const Updater::Release r = Updater::fetchLatest();
        QMetaObject::invokeMethod(qApp, [self, r, manual]() {
            if (manual)
                QApplication::restoreOverrideCursor();
            if (!self)
                return;
            self->updateChecking = false;
            self->settings->setValue("Update/LastCheck", QDateTime::currentDateTime());
            const QString cur = Updater::currentVersion();
            if (!r.ok) {
                if (manual) {
                    if (r.httpStatus == 200 || r.httpStatus == 404)
                        QMessageBox::information(self->dialogParent(), tr("ตรวจหาอัปเดต"),
                            tr("ใช้เวอร์ชันล่าสุดอยู่แล้ว (%1)").arg(cur));
                    else
                        QMessageBox::warning(self->dialogParent(), tr("ตรวจหาอัปเดต"),
                            tr("เชื่อมต่อเซิร์ฟเวอร์อัปเดตไม่ได้\nกรุณาตรวจสอบอินเทอร์เน็ต แล้วลองใหม่อีกครั้ง"));
                }
                return;
            }
            if (!Updater::isNewer(r.version, cur)) {
                if (manual)
                    QMessageBox::information(self->dialogParent(), tr("ตรวจหาอัปเดต"),
                        tr("ใช้เวอร์ชันล่าสุดอยู่แล้ว (%1)").arg(cur));
                return;
            }
            if (!manual && self->settings->value("Update/SkipVersion").toString() == r.version)
                return;
            self->offerUpdate(r, manual);
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::offerUpdate(const Updater::Release &r, bool manual)
{
    // found by the automatic check while a song is playing: ask after the song
    const bool busy = player->playerState() == PlayerState::Playing || videoActive || ytActive;
    if (!manual && busy) {
        const Updater::Release copy = r;
        QTimer::singleShot(60000, this, [this, copy]() { offerUpdate(copy, false); });
        return;
    }
    QMessageBox box(dialogParent());
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(tr("มีเวอร์ชันใหม่"));
    box.setText(tr("PTHK Karaoke เวอร์ชัน %1 พร้อมให้อัปเดตแล้ว\n(ตอนนี้ใช้เวอร์ชัน %2)\n\n"
                   "เพลง ตั้งค่า และฐานข้อมูลเพลงยังอยู่ครบ")
                    .arg(r.version, Updater::currentVersion()));
    QString notes = r.notes;
    notes.remove(QRegularExpression("(?im)^.*SHA-?256.*$"));
    if (!notes.trimmed().isEmpty())
        box.setDetailedText(notes.trimmed());
    QPushButton *btnNow = box.addButton(tr("อัปเดตเลย"), QMessageBox::AcceptRole);
    box.addButton(tr("ไว้ภายหลัง"), QMessageBox::RejectRole);
    QPushButton *btnSkip = manual ? nullptr : box.addButton(tr("ข้ามเวอร์ชันนี้"), QMessageBox::DestructiveRole);
    box.setDefaultButton(btnNow);
    box.exec();
    if (box.clickedButton() == btnNow)
        installUpdate(r);
    else if (btnSkip && box.clickedButton() == btnSkip)
        settings->setValue("Update/SkipVersion", r.version);
}

// Downloads the installer, checks it, runs it silently into this same folder and closes the
// program; the installer opens the program again when it is done.
void MainWindow::installUpdate(const Updater::Release &r)
{
    const QString dir = QDir::fromNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                        + "/PTHK-Update";
    QDir().mkpath(dir);
    const QString file = dir + "/" + (r.assetName.isEmpty() ? QStringLiteral("PTHK-Karaoke-setup.exe") : r.assetName);
    QFile::remove(file);

    QProgressDialog dlg(tr("กำลังดาวน์โหลดเวอร์ชัน %1...").arg(r.version), tr("ยกเลิก"), 0, 1000, dialogParent());
    dlg.setWindowTitle(tr("อัปเดต PTHK Karaoke"));
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(0);
    dlg.setMinimumWidth(380);
    dlg.setValue(0);

    std::atomic<bool> cancel{false}, done{false};
    std::atomic<qint64> got{0}, total{r.assetSize};
    std::atomic<int> status{0};
    const QString url = r.assetUrl;
    std::thread t([&]() {
        QFile f(file);
        if (f.open(QIODevice::WriteOnly)) {
            status = Updater::httpGet(url, nullptr, &f, [&](qint64 g, qint64 tot) {
                got = g;
                if (tot > 0) total = tot;
            }, &cancel);
            f.close();
        }
        done = true;
    });
    while (!done) {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        if (dlg.wasCanceled())
            cancel = true;
        const qint64 tot = total.load(), g = got.load();
        if (tot > 0) {
            dlg.setValue(int(qMin<qint64>(999, g * 1000 / tot)));
            dlg.setLabelText(tr("กำลังดาวน์โหลดเวอร์ชัน %1...  %2 / %3 MB")
                             .arg(r.version).arg(g / 1048576.0, 0, 'f', 1).arg(tot / 1048576.0, 0, 'f', 1));
        }
        QThread::msleep(30);
    }
    t.join();
    dlg.reset();

    if (cancel) {
        QFile::remove(file);
        return;
    }
    QString problem;
    if (status != 200)
        problem = tr("ดาวน์โหลดไม่สำเร็จ (รหัส %1) กรุณาตรวจสอบอินเทอร์เน็ต").arg(status.load());
    else if (r.assetSize > 0 && QFileInfo(file).size() != r.assetSize)
        problem = tr("ดาวน์โหลดไม่ครบ กรุณาลองใหม่");
    else if (!r.sha256.isEmpty() && Updater::fileSha256(file) != r.sha256)
        problem = tr("ไฟล์ที่ดาวน์โหลดไม่ถูกต้อง (ตรวจรหัส SHA-256 ไม่ผ่าน) จึงไม่ติดตั้ง");
    if (!problem.isEmpty()) {
        QFile::remove(file);
        QMessageBox::warning(dialogParent(), tr("อัปเดต PTHK Karaoke"), problem);
        return;
    }

    // same folder, same mode (portable stays portable)
    const QString appDir = QDir::toNativeSeparators(QApplication::applicationDirPath());
    QStringList args { "/SILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CLOSEAPPLICATIONS", "/update=1",
                       "/DIR=" + appDir };
    const QString pf = QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles"));
    const bool inProgramFiles = !pf.isEmpty()
            && QApplication::applicationDirPath().startsWith(pf, Qt::CaseInsensitive);
    args << (inProgramFiles ? "/ALLUSERS" : "/CURRENTUSER");
    if (Config::isPortableMode())
        args << "/MERGETASKS=portable";

    if (QMessageBox::question(dialogParent(), tr("อัปเดต PTHK Karaoke"),
            tr("ดาวน์โหลดเสร็จแล้ว\nโปรแกรมจะปิดเพื่อติดตั้ง แล้วเปิดขึ้นมาใหม่เอง (ประมาณ 1 นาที)\n\nติดตั้งเลยไหม?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
        return;
    if (!QProcess::startDetached(file, args, dir)) {
        QMessageBox::warning(dialogParent(), tr("อัปเดต PTHK Karaoke"),
                             tr("เปิดตัวติดตั้งไม่ได้\nไฟล์อยู่ที่: %1").arg(QDir::toNativeSeparators(file)));
        return;
    }
    QTimer::singleShot(0, this, [this]() { close(); });
}
