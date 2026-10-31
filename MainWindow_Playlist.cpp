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
        return;
    }
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
