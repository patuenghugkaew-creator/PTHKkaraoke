// MainWindow: Medley dialogs, playlist editing, on-screen frames
#include "MainWindow_p.h"

// Opens the Medley settings (BPM / transition bars / drum fill pattern).
// Works whether Medley is currently ON or OFF; OK always turns Medley ON
// with the new settings and re-prepares the next song.
void MainWindow::openMedleySettings()
{
    MedleyDialog dlg(this, player);
    dlg.setModal(true);
    if (dlg.exec() == QDialog::Accepted) {
        if (player->isUseMedley()) {
            applyMedleyToPlayingSong();

            const QString bpmText = player->medleyBPM() > 0 ? QString::number(player->medleyBPM()) + " BPM" : tr("ตามต้นฉบับ");
            const QString rhmName = QString::number(dlg.rhythmCode());
            ui->detail->setDetail(tr("เมดเล่ย์ "), tr("เปิด %1 (%2)").arg(rhmName, bpmText));
            ui->detail->show();
            detailTimer->start(3000);
            if (this->width() < 1160 && ui->chMix->isVisible())
                ui->lcdTime->hide();
        }
    } else if (dlg.isTurnOffRequested()) {
        turnOffMedley();
    }
    if (btnMedley) {
        QSignalBlocker b(btnMedley);
        btnMedley->setChecked(player->isUseMedley());
    }
}

// Medley just switched on (or its tempo changed): the playing song follows the locked tempo,
// gets its medley start / end, and the next song of the queue is prepared.
void MainWindow::applyMedleyToPlayingSong()
{
    if (!player->isUseMedley())
        return;
    if (player->midiFile() && !player->midiFile()->events().isEmpty()) {
        player->midiFile()->setSingleTempo(true);
        const int baseBpm = player->currentBpm() - player->bpmSpeed();
        const int targetBpm = player->medleyBPM() > 0 ? MidiPlayer::fitLockedBpm(player->medleyBPM(), baseBpm, player->medleyKeepFarSongs()) : player->currentBpm();
        player->setBpmSpeed(targetBpm - baseBpm);
        playingSong.setBpmSpeed(player->bpmSpeed());
        player->midiSequencer()->applyAutoMedleyBounds(lyrWidget->curData(),
                                                       playingSong.cutStartBar(),
                                                       playingSong.cutEndBar(),
                                                       lyrWidget->lastActiveLyricTick(),
                                                       player->medleySkipIntro(),
                                                       player->medleyAutoCutOutro(),
                                                       player->medleyWaitBars(),
                                                       player->medleyQuantizeBar(),
                                                       player->medleyIntroLeadInBars());
    }
    const int nextMedleyIdx = (remove_playlist ? 0 : playingIndex + 1);
    if (!player->isPlayerStopped() && (nextMedleyIdx >= 0 && nextMedleyIdx < playlist.size()))
        loadNextMedley(playlist[nextMedleyIdx]);
}

// ---------------- medley sets ----------------
// A set = the queue saved under a name (<name>.bmpl, same format as a saved playlist, with each
// song's own start / end bars) + its medley tempo and bridge (<name>.ini), in Config/MedleySets.

static QString medleySetDir()
{
    const QString d = Config::CONFIG_DIR_PATH + "/MedleySets";
    QDir().mkpath(d);
    return d;
}

static QString medleySetFileBase(const QString &name)
{
    QString f = name.trimmed();
    for (const QChar c : QStringLiteral("\\/:*?\"<>|"))
        f.replace(c, '_');
    return medleySetDir() + "/" + f;
}

QStringList MainWindow::medleySetNames() const
{
    QStringList names;
    QDir dir(medleySetDir());
    for (const QFileInfo &fi : dir.entryInfoList({ "*.ini" }, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        if (!QFile::exists(fi.absolutePath() + "/" + fi.completeBaseName() + ".bmpl"))
            continue;
        QSettings ini(fi.absoluteFilePath(), QSettings::IniFormat);
        ini.setIniCodec("UTF-8");
        names << ini.value("Name", fi.completeBaseName()).toString();
    }
    return names;
}

void MainWindow::saveMedleySet()
{
    // the queue as it is now; with "remove played songs" the playing song is no longer in it
    QList<Song *> songs;
    Song current;
    if (remove_playlist && !player->isPlayerStopped() && !playingSong.id().isEmpty()) {
        current = playingSong;
        songs << &current;
    }
    songs << playlist;
    if (songs.isEmpty()) {
        QMessageBox::information(dialogParent(), tr("ชุดเมดเลย์"),
                                 tr("ยังไม่มีเพลงในคิว\nเพิ่มเพลงเข้าคิวให้ครบ แล้วค่อยบันทึกเป็นชุด"));
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(dialogParent(), tr("บันทึกชุดเมดเลย์"),
            tr("ตั้งชื่อชุด (มี %1 เพลง) เช่น \"สามช่า 150\", \"งานแต่ง\":").arg(songs.count()),
            QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    const QString base = medleySetFileBase(name);
    if (QFile::exists(base + ".bmpl")
            && QMessageBox::question(dialogParent(), tr("บันทึกชุดเมดเลย์"),
                                     tr("มีชุด \"%1\" อยู่แล้ว ต้องการบันทึกทับไหม?").arg(name)) != QMessageBox::Yes)
        return;
    if (!Utils::savePlaylist(base + ".bmpl", songs)) {
        QMessageBox::warning(dialogParent(), tr("บันทึกชุดเมดเลย์"), tr("บันทึกไม่สำเร็จ"));
        return;
    }
    QSettings ini(base + ".ini", QSettings::IniFormat);
    ini.setIniCodec("UTF-8");
    ini.setValue("Name", name);
    ini.setValue("BPM", player->medleyBPM());
    ini.setValue("TempoMode", player->medleyTempoMode());
    ini.setValue("TransitionBars", player->medleyTransitionBars());
    ini.setValue("FillVolume", player->medleyFillVolume());
    ini.setValue("Songs", songs.count());
    ini.sync();
    ui->detail->setDetail(tr("ชุดเมดเลย์ "), tr("บันทึก \"%1\" (%2 เพลง)").arg(name).arg(songs.count()));
    ui->detail->show();
    detailTimer->start(3000);
}

// On stage: pick a set -> medley on with its tempo / bridge, its songs become the queue.
// While a song plays it carries on and the set follows it; when stopped, press play to start.
void MainWindow::loadMedleySet(const QString &name)
{
    const QString base = medleySetFileBase(name);
    QList<Song *> songs;
    if (!Utils::loadPlaylist(base + ".bmpl", songs) || songs.isEmpty()) {
        QMessageBox::warning(dialogParent(), tr("ชุดเมดเลย์"), tr("เปิดชุด \"%1\" ไม่ได้").arg(name));
        return;
    }
    // songs whose file is gone (another computer / drive) are left out, and named
    int missing = 0;
    QStringList missingNames;
    for (int i = songs.size() - 1; i >= 0; --i) {
        Song *s = songs[i];
        bool exists = true;
        const QString t = s->songType();
        if (t == "VIDEO" || t == "MP3")
            exists = QFile::exists(Config::relocatePath(s->path()));
        if (!exists) {
            ++missing;
            missingNames.prepend(s->name());
            delete songs.takeAt(i);
        }
    }

    // its medley settings
    QSettings ini(base + ".ini", QSettings::IniFormat);
    ini.setIniCodec("UTF-8");
    player->setMedleyBPM(ini.value("BPM", player->medleyBPM()).toInt());
    player->setMedleyTempoMode(ini.value("TempoMode", player->medleyTempoMode()).toInt());
    player->setMedleyTransitionBars(ini.value("TransitionBars", player->medleyTransitionBars()).toInt());
    player->setMedleyFillVolume(ini.value("FillVolume", player->medleyFillVolume()).toInt());
    player->saveMedleySettings();

    // the queue: everything after the playing song is replaced by the set
    const bool playing = !player->isPlayerStopped();
    const int from = playing ? (remove_playlist ? 0 : playingIndex + 1) : 0;
    while (playlist.size() > from)
        delete playlist.takeLast();
    playlist << songs;
    if (!playing)
        playingIndex = -1;
    ui->playlistWidget->setPlaylist(playlist);

    if (!player->isUseMedley())
        player->setUseMedley(true);
    if (playing)
        applyMedleyToPlayingSong();
    if (btnMedley) {
        QSignalBlocker b(btnMedley);
        btnMedley->setChecked(player->isUseMedley());
    }

    const QString bpm = player->medleyBPM() > 0 && player->medleyTempoMode() != 1
                            ? QString::number(player->medleyBPM()) + " BPM" : tr("ความเร็วตามต้นฉบับ");
    ui->detail->setDetail(tr("ชุดเมดเลย์ ") + name,
                          tr("%1 เพลง, %2, ลูกส่ง %3 ห้อง%4")
                              .arg(songs.size()).arg(bpm).arg(player->medleyTransitionBars())
                              .arg(playing ? QString() : tr("  -  กดเล่นเพื่อเริ่ม")));
    ui->detail->show();
    detailTimer->start(5000);
    if (missing > 0)
        QMessageBox::information(dialogParent(), tr("ชุดเมดเลย์"),
                                 tr("ไม่พบไฟล์เพลง %1 เพลง (ข้ามไป):\n%2").arg(missing).arg(missingNames.join("\n")));
}

void MainWindow::manageMedleySets()
{
    QDialog dlg(dialogParent());
    dlg.setWindowTitle(tr("จัดการชุดเมดเลย์"));
    dlg.resize(460, 380);
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    QListWidget *list = new QListWidget(&dlg);
    v->addWidget(list, 1);
    auto refresh = [this, list]() {
        list->clear();
        for (const QString &n : medleySetNames()) {
            QSettings ini(medleySetFileBase(n) + ".ini", QSettings::IniFormat);
            ini.setIniCodec("UTF-8");
            const int bpm = ini.value("BPM", 0).toInt();
            QListWidgetItem *it = new QListWidgetItem(QString("%1   (%2 เพลง%3)").arg(n)
                    .arg(ini.value("Songs", 0).toInt())
                    .arg(bpm > 0 && ini.value("TempoMode", 0).toInt() != 1 ? QString(", %1 BPM").arg(bpm) : QString()), list);
            it->setData(Qt::UserRole, n);
        }
    };
    refresh();
    QHBoxLayout *row = new QHBoxLayout();
    QPushButton *btnOpen = new QPushButton(tr("เปิดชุดนี้"), &dlg);
    QPushButton *btnRename = new QPushButton(tr("เปลี่ยนชื่อ"), &dlg);
    QPushButton *btnDelete = new QPushButton(tr("ลบชุด"), &dlg);
    QPushButton *btnClose = new QPushButton(tr("ปิด"), &dlg);
    for (QPushButton *b : { btnOpen, btnRename, btnDelete })
        row->addWidget(b);
    row->addStretch();
    row->addWidget(btnClose);
    v->addLayout(row);
    auto selected = [list]() { return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString(); };
    connect(btnOpen, &QPushButton::clicked, &dlg, [&, selected]() {
        const QString n = selected();
        if (n.isEmpty()) return;
        dlg.accept();
        loadMedleySet(n);
    });
    connect(list, &QListWidget::itemDoubleClicked, btnOpen, &QPushButton::click);
    connect(btnRename, &QPushButton::clicked, &dlg, [&, selected, refresh]() {
        const QString n = selected();
        if (n.isEmpty()) return;
        bool ok = false;
        const QString nn = QInputDialog::getText(&dlg, tr("เปลี่ยนชื่อชุด"), tr("ชื่อใหม่:"), QLineEdit::Normal, n, &ok).trimmed();
        if (!ok || nn.isEmpty() || nn == n) return;
        const QString from = medleySetFileBase(n), to = medleySetFileBase(nn);
        if (QFile::exists(to + ".bmpl")) {
            QMessageBox::information(&dlg, tr("เปลี่ยนชื่อชุด"), tr("มีชุดชื่อนี้อยู่แล้ว"));
            return;
        }
        QFile::rename(from + ".bmpl", to + ".bmpl");
        QFile::rename(from + ".ini", to + ".ini");
        QSettings ini(to + ".ini", QSettings::IniFormat);
        ini.setIniCodec("UTF-8");
        ini.setValue("Name", nn);
        ini.sync();
        refresh();
    });
    connect(btnDelete, &QPushButton::clicked, &dlg, [&, selected, refresh]() {
        const QString n = selected();
        if (n.isEmpty()) return;
        if (QMessageBox::question(&dlg, tr("ลบชุด"), tr("ลบชุด \"%1\"? (ไฟล์เพลงไม่ถูกลบ)").arg(n)) != QMessageBox::Yes)
            return;
        const QString b = medleySetFileBase(n);
        QFile::remove(b + ".bmpl");
        QFile::remove(b + ".ini");
        refresh();
    });
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::reject);
    dlg.exec();
}

void MainWindow::turnOffMedley()
{
    if (!player) return;
    player->setUseMedley(false);
    player->unloadNextMedley();
    if (!player->isPlayerStopped() && player->midiSequencer()) {
        player->midiSequencer()->setEndTick(0);
    }
    if (btnMedley) {
        QSignalBlocker b(btnMedley);
        btnMedley->setChecked(false);
    }
    ui->detail->setDetail(tr("เมดเลย์ "), tr("ปิด (Off)"));
    ui->detail->show();
    detailTimer->start(3000);
    if (this->width() < 1160 && ui->chMix->isVisible())
        ui->lcdTime->hide();
}

void MainWindow::openAutoMedley()
{
    openMedleySettings();
}

void MainWindow::openEasyMedley()
{
    openMedleySettings();
}

void MainWindow::loadNextMedley(Song *song)
{
    if (!song || (song->songType() != "NCN" && song->songType() != "EMK" && song->songType() != "HNK" && song->songType() != "KAR" && song->songType() != "SIB")) {
        player->unloadNextMedley();
        nextMedleyTitle.clear();
        return;
    }
    nextMedleyTitle = song->name() + (song->artist().isEmpty() ? QString() : " - " + song->artist());
    if ((medleyLoader != nullptr) && medleyLoader->isRunning())
        medleyLoader->wait();

    medleyLoader = new MedleyLoader(this, song, db, player, lyrWidget, secondLyr);
    connect(medleyLoader, &MedleyLoader::finished, [this](){
        medleyLoader->deleteLater();
        medleyLoader = nullptr;
    });
    medleyLoader->start();
}

void MainWindow::addToPlaylist(Song *song)
{
    Song *songToAdd = new Song();
    *songToAdd = *song;

    if (player->isUseMedley() && player->midiFile() && !player->midiFile()->events().isEmpty()) {
        player->midiFile()->setSingleTempo(true);
        const int baseBpm = player->currentBpm() - player->bpmSpeed();
        const int targetBpm = player->medleyBPM() > 0 ? MidiPlayer::fitLockedBpm(player->medleyBPM(), baseBpm, player->medleyKeepFarSongs()) : player->currentBpm();
        player->setBpmSpeed(targetBpm - baseBpm);
        playingSong.setBpmSpeed(player->bpmSpeed());
        player->midiSequencer()->applyAutoMedleyBounds(lyrWidget->curData(), playingSong.cutStartBar(), playingSong.cutEndBar(), lyrWidget->lastActiveLyricTick(), player->medleySkipIntro(), player->medleyAutoCutOutro(), player->medleyWaitBars(), player->medleyQuantizeBar(), player->medleyIntroLeadInBars());
    }
    playlist.append(songToAdd);
    ui->playlistWidget->addSong(songToAdd);

    const int nextMedleyIdx = (remove_playlist ? 0 : playingIndex + 1);
    if (player->isUseMedley() && !player->isPlayerStopped() && (playlist.size() == (nextMedleyIdx + 1))) {
        loadNextMedley(songToAdd);
    }
}

void MainWindow::removeFromPlaylist(int index)
{
    delete playlist.takeAt(index);
    ui->playlistWidget->removeRow(index);

    if (playlist.size() == 0)
        player->unloadNextMedley();

    if (player->isUseMedley() && (index == playingIndex + 1) && (playlist.size() > playingIndex + 1))
        loadNextMedley(playlist[playingIndex + 1]);
}

void MainWindow::swapInPlaylist(int index, int toIndex)
{
    playlist.swap(index, toIndex);
    ui->playlistWidget->swapRow(index, toIndex);
    ui->playlistWidget->setCurrentRow(toIndex);

    // the playing song (kept in the queue when "remove played songs" is off) moves too
    if (playingIndex == index)
        playingIndex = toIndex;
    else if (playingIndex == toIndex)
        playingIndex = index;

    bool isNextSong = (index == playingIndex + 1) || (toIndex == playingIndex + 1);

    if (player->isUseMedley() && isNextSong && (playlist.size() > playingIndex + 1)) {
        loadNextMedley(playlist[playingIndex + 1]);
    }
}

void MainWindow::updateShutdownRequest()
{
    qApp->quit();
}

void MainWindow::showFrameSearch()
{
    ui->chMix->hide();
    ui->expandChMix->hide();
    ui->songDetail->hide();
    ui->playlistWidget->hide();

    ui->frameSearch->show();
}

void MainWindow::showPlaylist()
{
    ui->chMix->hide();
    ui->expandChMix->hide();
    ui->songDetail->hide();
    ui->frameSearch->hide();

    ui->playlistWidget->show();
}

void MainWindow::showSongDetail()
{
    ui->chMix->hide();
    ui->expandChMix->hide();
    ui->frameSearch->hide();
    ui->playlistWidget->hide();

    ui->songDetail->show();
}


void MainWindow::showChMix()
{
    if (ui->playlistWidget->isVisible()
            || ui->frameSearch->isVisible()
            || ui->songDetail->isVisible())
    {
        return;
    }

    ui->chMix->hide();
    ui->expandChMix->hide();

    if (false && ui->detail->isVisible() && this->width() < 1160) {
        ui->lcdTime->hide();
    }
}

void MainWindow::onChMixMouseLeaved()
{
    if (ui->chMix->isLock())
        return;

    ui->chMix->hide();
    ui->expandChMix->hide();
}

void MainWindow::onChMixLockChanged(bool lock)
{
    if (lock) {
        settings->setValue("ChMixLock", true);
    } else {
        settings->setValue("ChMixLock", false);
    }
}

void MainWindow::hideUIFrame()
{
    ui->frameSearch->hide();
    ui->playlistWidget->hide();
    ui->songDetail->hide();

    if (false && ui->chMix->isLock()) {
        ui->chMix->hide();
        ui->expandChMix->hide();
    }
}

void MainWindow::showCurrentTime()
{
    QTime time = QTime::currentTime();
    QString currentTime = locale.toString(time, "hh:mm:ss");
    ui->lcdTime->display(currentTime);
}

void MainWindow::setFrameSearch(Song *s)
{
    ui->lbId->setText(s->id());
    ui->lbName->setText(" " + s->name());
    ui->lbBtw->setText("-");
    ui->lbArtist->setText(s->artist());
    if (s->key() != "")
        ui->lbTempoKey->setText(" (" + QString::number(s->tempo()) + "-" + s->key() + ")");
    else
        ui->lbTempoKey->setText(" (" + QString::number(s->tempo()) + ")");
    ui->lbType->setText("[" + s->songType() + "]");
    ui->lbLyrics->setText(s->lyrics().replace("\r\n", " "));

    if (s->artist().length() == 0) {
        ui->lbBtw->hide();
    }
}
