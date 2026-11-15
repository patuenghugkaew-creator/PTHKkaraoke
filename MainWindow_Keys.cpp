// MainWindow: Keyboard shortcuts, search box (Thai fallback + fuzzy), event filter
#include "MainWindow_p.h"

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (ui->songMedley->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Escape: {
            ui->songMedley->hide();
            timer2->start(playlist_timeout);
            break;
        }
        case Qt::Key_Enter:
        case Qt::Key_Return: {
            const int row = ui->playlistWidget->currentRow();
            if (row < 0 || row >= playlist.size()) {
                ui->songMedley->hide();
                break;
            }
            Song *s = playlist[row];
            s->setCutStartBar(ui->songMedley->cutStartBar());
            s->setCutEndBar(ui->songMedley->cutEndBar());
            this->setFocus();
            ui->songMedley->hide();
            timer2->start(playlist_timeout);

            if (ui->playlistWidget->currentRow() == playingIndex + 1) {
                MidiSequencer *seq = player->midiSequencerTemp();
                seq->setCutStartBar(s->cutStartBar());
                seq->setCutEndBar(s->cutEndBar());
            }
            break;
        }
        default:
            break;
        }
        return;
    }

    if (event->modifiers() == Qt::AltModifier
        && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        changeSongLevel(event->key() == Qt::Key_Up ? 1.0 : -1.0);   // this song louder / quieter
        return;
    }
    // Alt + 1..9 (also on the number pad): the sound pads; Alt + 0: the next sound bank.
    // The digit comes from the key itself (Windows virtual key), not its character: with the Thai
    // keyboard layout the 1 key types "ๅ", so event->key() is not Qt::Key_1.
    if (event->modifiers() == Qt::AltModifier && event->key() == Qt::Key_Backspace) {
        stopAllSoundPads();             // every sound pad sound off
        return;
    }
    if ((event->modifiers() & ~Qt::KeypadModifier) == Qt::AltModifier) {
        const quint32 vk = event->nativeVirtualKey();
        int digit = -1;
        if (vk >= 0x30 && vk <= 0x39)           // '0'..'9' above the letters
            digit = int(vk - 0x30);
        else if (vk >= 0x60 && vk <= 0x69)      // VK_NUMPAD0..9
            digit = int(vk - 0x60);
        else if (event->key() >= Qt::Key_0 && event->key() <= Qt::Key_9)
            digit = event->key() - Qt::Key_0;
        if (digit >= 0) {
            if (!event->isAutoRepeat()) {
                if (digit == 0)
                    nextSoundBank();
                else
                    triggerSoundPad(digit - 1);
            }
            return;
        }
    }

    if (event->modifiers() == Qt::ControlModifier) {
        switch (event->key()) {
            case Qt::Key_L:             // lock / unlock screen for shops
                toggleShopLock();
                return;
            case Qt::Key_D:             // drum hold / fill back into the song
                toggleDrumHold();
                return;
            case Qt::Key_BracketRight:
            case Qt::Key_BraceRight:
            case Qt::Key_F4: {
                turnOffMedley();
                return;
            }
            case Qt::Key_BracketLeft:
            case Qt::Key_BraceLeft: {
                openMedleySettings();
                return;
            }
            case Qt::Key_Up:
                if (ui->frameSearch->isVisible())
                {
                    preSetTranspose(db->currentSong()->transpose() + 1);
                }
                else if (ui->playlistWidget->isVisible())
                {
                    int i = ui->playlistWidget->currentRow();
                    if (i < 0)
                        break;
                    Song *s = playlist[i];
                    s->setTranspose(s->transpose() + 1);
                    ui->playlistWidget->updateDetail(i, s);
                    timer2->start(playlist_timeout);
                }
                else
                {
                    ui->sliderVolume->setValue(ui->sliderVolume->value() + 5);
                }
                break;
            case Qt::Key_Down:
                if (ui->frameSearch->isVisible())
                {
                    preSetTranspose(db->currentSong()->transpose() - 1);
                }
                else if (ui->playlistWidget->isVisible())
                {
                    int i = ui->playlistWidget->currentRow();
                    if (i < 0)
                        break;
                    Song *s = playlist[i];
                    s->setTranspose(s->transpose() - 1);
                    ui->playlistWidget->updateDetail(i, s);
                    timer2->start(playlist_timeout);
                }
                else
                {
                    ui->sliderVolume->setValue(ui->sliderVolume->value() - 5);
                }
                break;
            case Qt::Key_Right:
                if (ui->frameSearch->isVisible())
                {
                    preSetBpmSpeed(db->currentSong()->bpmSpeed() + 1);
                }
                else if (ui->playlistWidget->isVisible())
                {
                    int i = ui->playlistWidget->currentRow();
                    if (i < 0)
                        break;
                    Song *s = playlist[i];
                    s->setBpmSpeed(s->bpmSpeed() + 1);
                    ui->playlistWidget->updateDetail(i, s);
                    timer2->start(playlist_timeout);
                }
                break;
            case Qt::Key_Left:
                if (ui->frameSearch->isVisible())
                {
                    preSetBpmSpeed(db->currentSong()->bpmSpeed() - 1);
                }
                else if (ui->playlistWidget->isVisible())
                {
                    int i = ui->playlistWidget->currentRow();
                    if (i < 0)
                        break;
                    Song *s = playlist[i];
                    s->setBpmSpeed(s->bpmSpeed() - 1);
                    ui->playlistWidget->updateDetail(i, s);
                    timer2->start(playlist_timeout);
                }
                break;
            case Qt::Key_X:
                if (ui->frameSearch->isVisible()) {
                    setFrameSearch( db->search("") );
                    ui->lbSearch->setText("_");
                    ui->frameSearch->show();
                    timer2->start(search_timeout);
                }
                break;
            case Qt::Key_Equal:     // same as + / - without Ctrl: + up, - down
            case Qt::Key_Plus:
                if (ui->playlistWidget->isVisible()) {
                    if (isShopStrictLocked()) {
                        notifyShopStrictLocked(tr("เลื่อนลำดับคิวเพลง"));
                        break;
                    }
                    int i = ui->playlistWidget->currentRow();
                    if (i > 0)
                        swapInPlaylist(i, i-1);
                    timer2->start(playlist_timeout);
                }
                break;
            case Qt::Key_Minus:
                if (ui->playlistWidget->isVisible()) {
                    if (isShopStrictLocked()) {
                        notifyShopStrictLocked(tr("เลื่อนลำดับคิวเพลง"));
                        break;
                    }
                    int i = ui->playlistWidget->currentRow();
                    if (i >= 0 && i < ui->playlistWidget->rowCount() - 1)
                        swapInPlaylist(i, i+1);
                    timer2->start(playlist_timeout);
                }
                break;
            case Qt::Key_S:
                if (ui->playlistWidget->isVisible())
                {
                    timer2->stop();
                    savePlaylist();
                    timer2->start(playlist_timeout);
                }
                break;
            case Qt::Key_O:
                if (ui->playlistWidget->isVisible())
                {
                    timer2->stop();
                    loadPlaylist();
                    timer2->start(playlist_timeout);
                }
                else
                    this->sendDrumPads(event, true);
                break;
        case Qt::Key_Delete:            // delete a song: the one in the search bar, or the one playing
            if (ui->frameSearch->isVisible())
                deleteSearchSong();
            else if (!ui->playlistWidget->isVisible())
                deletePlayingSong();
            break;
            default:
                this->sendDrumPads(event, true);
                break;
        }
        return;
    }


    switch (event->key()) {
    case Qt::Key_Escape: {
        hideUIFrame();
        break;
    }
    case Qt::Key_F2: {      // favorite on/off: the song in the search box, else the playing song
        Song *s = ui->frameSearch->isVisible() ? db->currentSong() : &playingSong;
        if (s->path().isEmpty())
            break;
        bool fav = db->toggleFavorite(s);
        ui->detail->setDetail(tr("เพลงโปรด "), fav ? tr("เพิ่มแล้ว") : tr("เอาออกแล้ว"));
        ui->detail->show();
        detailTimer->start(3000);
        break;
    }
    case Qt::Key_F1: {
        showHelpDialog();
        break;
    }
    case Qt::Key_F10: {     // YouTube search: play online or add to the queue
        showYouTubeSearch();
        break;
    }
    case Qt::Key_F12: {     // record music + microphone to a WAV file (stops by itself at the end of the song)
        toggleRecording();
        break;
    }
    case Qt::Key_F3: {      // favorites / recently played / most played
        SongListsDialog dlg(this, db);
        if (dlg.exec() == QDialog::Accepted && dlg.hasSelection()) {
            Song s = dlg.selectedSong();
            addToPlaylist(&s);
            if (auto_playnext && playlist.count() == 1 && !isSongPlaying())
                play(0);
        }
        setFocus();
        break;
    }
    case Qt::Key_F4: {
        if (songInMedley() && player->midiFile() && !player->midiFile()->events().isEmpty()) {
        player->midiFile()->setSingleTempo(true);
        const int baseBpm = player->currentBpm() - player->bpmSpeed();
        const int targetBpm = player->medleyBPM() > 0 ? MidiPlayer::fitLockedBpm(player->medleyBPM(), baseBpm, player->medleyKeepFarSongs()) : player->currentBpm();
        player->setBpmSpeed(targetBpm - baseBpm);
        playingSong.setBpmSpeed(player->bpmSpeed());
        player->midiSequencer()->applyAutoMedleyBounds(lyrWidget->curData(), playingSong.cutStartBar(), playingSong.cutEndBar(), lyrWidget->lastActiveLyricTick(), player->medleySkipIntro(), player->medleyAutoCutOutro(), player->medleyWaitBars(), player->medleyQuantizeBar(), player->medleyIntroLeadInBars());
    }
    if (player->isUseMedley()) {
            QMessageBox box(this);
            box.setWindowTitle(tr("เมดเลย์"));
            box.setText(tr("โหมดเมดเลย์กำลังเปิดอยู่ ต้องการทำอะไร?"));
            QPushButton *btnSettings = box.addButton(tr("ตั้งค่าเมดเลย์"), QMessageBox::AcceptRole);
            QPushButton *btnOff = box.addButton(tr("ปิดเมดเลย์"), QMessageBox::DestructiveRole);
            box.addButton(tr("ยกเลิก"), QMessageBox::RejectRole);
            box.setDefaultButton(btnSettings);
            box.exec();
            if (box.clickedButton() == btnSettings) {
                openMedleySettings();
            } else if (box.clickedButton() == btnOff) {
                turnOffMedley();        // (also gives the playing song its own tempo back)
            }
        } else {
            openMedleySettings();
        }
        if (btnMedley) { QSignalBlocker b(btnMedley); btnMedley->setChecked(player->isUseMedley()); }
        break;
    }
    case Qt::Key_F11: {
        openMedleySettings();
        break;
    }
    case Qt::Key_F5: {
        if (isShopStrictLocked()) { notifyShopStrictLocked(tr("เล่น / เริ่มใหม่")); break; }
        if (player->isPlayerPaused())
            resume();
        else
            play(-1);
        break;
    }
    case Qt::Key_F6: {
        if (isShopStrictLocked()) { notifyShopStrictLocked(tr("พักเพลง")); break; }
        pause();
        break;
    }
    case Qt::Key_F7: {
        if (isShopStrictLocked()) { notifyShopStrictLocked(tr("หยุดเพลง")); break; }
        stop();
        break;
    }
    case Qt::Key_F8: {
        if (isShopStrictLocked()) { notifyShopStrictLocked(tr("เพลงก่อนหน้า")); break; }
        playPrevious();
        break;
    }
    case Qt::Key_Pause:
        toggleDrumHold();
        break;
    case Qt::Key_F9: {
        if (isShopStrictLocked()) { notifyShopStrictLocked(tr("ข้ามไปเพลงถัดไป")); break; }
        playNext();
        break;
    }
    case Qt::Key_Asterisk: {
        ui->chMix->setMuteVoice(!ui->chMix->isMuteVoice());
        // (the state it ended in: during idle music a press may turn the cut on either way)
        ui->detail->setDetail(tr("เสียงร้อง "), ui->chMix->isMuteVoice() ? tr("ปิด") : tr("เปิด"));
        {   // keep the bar button in step with the key
            QSignalBlocker block(btnMuteVocal);
            btnMuteVocal->setChecked(ui->chMix->isMuteVoice());
        }
#ifdef KM_HAVE_PROCLOOP
        applyYtVocalCut();
#endif
        applyVideoVocalCut();
        ui->detail->show();
        detailTimer->start(3000);
        if (this->width() < 1160 && ui->chMix->isVisible()) {
            ui->lcdTime->hide();
        }
        break;
    }
    case Qt::Key_Insert: {
        if (ui->frameSearch->isVisible()) {
            preSetTranspose(db->currentSong()->transpose() + 1);
            break;
        }
        if (ytActive) {
            changeYtKey(+1);
            break;
        }
        if (videoActive && videoAudioTempo) {
            changeVideoKey(+1);
            break;
        }
        playingSong.setTranspose(player->transpose() + 1);
        player->setTranspose(player->transpose() + 1);
        int trp = player->transpose();
        rememberPlayingKey(trp);
        QString t;
        if (trp > 0) t = "+" + QString::number(trp);
        else t = QString::number(trp);
        ui->songDetail->setDetail(&playingSong);
        ui->songDetail->adjustSize();
        ui->detail->setDetail(tr("คีย์เพลง "), t);
        ui->detail->show();
        detailTimer->start(3000);
        if (this->width() < 1160 && ui->chMix->isVisible()) {
            ui->lcdTime->hide();
        }
        break;
    }
    case Qt::Key_Delete: {
        if (ui->frameSearch->isVisible()) {
            preSetTranspose(db->currentSong()->transpose() - 1);
            break;
        }
        if (ui->playlistWidget->isVisible()) {
            if (isShopStrictLocked()) {
                notifyShopStrictLocked(tr("ลบเพลงออกจากคิว"));
                break;
            }
            int i = ui->playlistWidget->currentRow();
            if (i<0 || i >= playlist.size())
                break;
            const QString gone = playlist[i]->name();
            removeFromPlaylist(i);
            if (i < playingIndex)
                playingIndex--;                 // (played songs kept in the queue) the playing one moved up
            ui->detail->setDetail(tr("เอาออกจากคิว "), gone);    // Delete here is not "key down" / "delete the song"
            ui->detail->show();
            detailTimer->start(3000);
            showPlaylist();
            timer2->start(playlist_timeout);
            break;
        }
        if (ytActive) {
            changeYtKey(-1);
            break;
        }
        if (videoActive && videoAudioTempo) {
            changeVideoKey(-1);
            break;
        }
        playingSong.setTranspose(player->transpose() - 1);
        player->setTranspose(player->transpose() - 1);
        int trp = player->transpose();
        rememberPlayingKey(trp);
        QString t;
        if (trp > 0) t = "+" + QString::number(trp);
        else t = QString::number(trp);
        ui->songDetail->setDetail(&playingSong);
        ui->songDetail->adjustSize();
        ui->detail->setDetail(tr("คีย์เพลง "), t);
        ui->detail->show();
        detailTimer->start(3000);
        if (this->width() < 1160 && ui->chMix->isVisible()) {
            ui->lcdTime->hide();
        }
        break;
    }
    case Qt::Key_PageUp:
        if (ui->frameSearch->isVisible()) {
            preSetBpmSpeed(db->currentSong()->bpmSpeed() + 1);
        } else if (ui->playlistWidget->isVisible()) {
            int i = ui->playlistWidget->currentRow();
            if (i < 0)
                break;
            Song *s = playlist[i];
            s->setBpmSpeed(s->bpmSpeed() + 1);
            ui->playlistWidget->updateDetail(i, s);
            timer2->start(playlist_timeout);
        } else {
            addBpmSpeed(1);
        }
        break;
    case Qt::Key_PageDown:
        if (ui->frameSearch->isVisible()) {
            preSetBpmSpeed(db->currentSong()->bpmSpeed() - 1);
        } else if (ui->playlistWidget->isVisible()) {
            int i = ui->playlistWidget->currentRow();
            if (i < 0)
                break;
            Song *s = playlist[i];
            s->setBpmSpeed(s->bpmSpeed() - 1);
            ui->playlistWidget->updateDetail(i, s);
            timer2->start(playlist_timeout);
        } else {
            addBpmSpeed(-1);
        }
        break;
    case Qt::Key_Home:
        if (isShopStrictLocked()) {
            notifyShopStrictLocked(tr("เริ่มเพลงใหม่"));
            break;
        }
        if (playingSong.id() != "") {
            play(-1);
        }
        break;
    case Qt::Key_End:
        if (isShopStrictLocked()) {
            notifyShopStrictLocked(tr("จบเพลง"));
            break;
        }
        endSongSmoothly();
        break;
    case Qt::Key_Up:
        if (ui->frameSearch->isVisible()) {
            if (searchBoxChangeBpm) {
                searchBoxChangeBpm = false;
                ui->lbSearch->setText(db->searchText() + "_");
            }
            setFrameSearch(db->searchPrevious());
            timer2->start(search_timeout);
        } else if (ui->playlistWidget->isVisible()) {
            if (ui->playlistWidget->currentRow() > 0 )
                ui->playlistWidget->setCurrentRow( ui->playlistWidget->currentRow() - 1 );
            timer2->start(playlist_timeout);
        } else {
            ui->frameSearch->hide();
            if (ui->playlistWidget->currentRow() == -1)
                ui->playlistWidget->setCurrentRow(0);
            showPlaylist();
            ui->chMix->hide();
            ui->expandChMix->hide();
            timer2->start(playlist_timeout);
        }
        break;
    case Qt::Key_Down:
        if (ui->frameSearch->isVisible()) {
            if (searchBoxChangeBpm) {
                searchBoxChangeBpm = false;
                ui->lbSearch->setText(db->searchText() + "_");
            }
            setFrameSearch(db->searchNext());
            timer2->start(search_timeout);
        } else if (ui->playlistWidget->isVisible()) {
            if (ui->playlistWidget->currentRow() < ui->playlistWidget->rowCount() - 1 )
                ui->playlistWidget->setCurrentRow( ui->playlistWidget->currentRow() + 1 );
            timer2->start(playlist_timeout);
        } else {
            ui->frameSearch->hide();
            if (ui->playlistWidget->currentRow() == -1)
                ui->playlistWidget->setCurrentRow(0);
            showPlaylist();
            ui->chMix->hide();
            ui->expandChMix->hide();
            timer2->start(playlist_timeout);
        }
        break;
    case Qt::Key_Right:
        if (ui->frameSearch->isVisible()) {
            if (searchBoxChangeBpm) {
                searchBoxChangeBpm = false;
                ui->lbSearch->setText(db->searchText() + "_");
            }
            setFrameSearch( db->searchNext() );
            timer2->start(search_timeout);
        } else {
            searchBoxChangeBpm = false;
            searchFooter.clear();
            db->currentSong()->setBpmSpeed(0);
            db->currentSong()->setTranspose(0);
            ui->playlistWidget->hide();
            if (ui->lbId->text() == "")
                setFrameSearch( db->search("") );
            ui->lbSearch->setText("_");
            showFrameSearch();
            ui->chMix->hide();
            ui->expandChMix->hide();
            timer2->start(search_timeout);
        }
        break;
    case Qt::Key_Left:
        if (ui->frameSearch->isVisible()) {
            if (searchBoxChangeBpm) {
                searchBoxChangeBpm = false;
                ui->lbSearch->setText(db->searchText() + "_");
            }
            setFrameSearch( db->searchPrevious() );
            timer2->start(search_timeout);
        } else {
            searchBoxChangeBpm = false;
            searchFooter.clear();
            db->currentSong()->setBpmSpeed(0);
            db->currentSong()->setTranspose(0);
            ui->playlistWidget->hide();
            if (ui->lbId->text() == "")
                setFrameSearch( db->search("") );
            ui->lbSearch->setText("_");
            showFrameSearch();
            ui->chMix->hide();
            ui->expandChMix->hide();
            timer2->start(search_timeout);
        }
        break;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        if (ui->frameSearch->isVisible()) {
            const int step = (event->key() == Qt::Key_Backtab || (event->modifiers() & Qt::ShiftModifier)) ? -1 : 1;
            setSearchMode(static_cast<SearchType>((int(db->currentSearchType()) + step + 7) % 7));
        }
        break;
    case Qt::Key_Backspace:
        if (ui->frameSearch->isVisible()) {
            if (ui->lbSearch->text() == "_") {
                timer2->start(search_timeout);
                break;
            }
            QString s = ui->lbSearch->text();
            s = s.replace(s.length() - 2, 2, "");
            searchWithThaiFallback(s);
            ui->lbSearch->setText(s + "_");
            timer2->start(search_timeout);
        }
        break;
    case Qt::Key_Enter:
    case Qt::Key_Return:
        if (ui->frameSearch->isVisible()) {
            if (ui->lbId->text().isEmpty())
                break;                  // a message is shown (nothing found / type something): no song
            Song *s = db->currentSong();
            const QString queuedName = s->name();
            addToPlaylist(s);
            if (!idleSwitching) {
                ui->detail->setDetail(tr("เพิ่มเข้าคิวแล้ว · ลำดับที่ %1").arg(playlist.size()), queuedName);
                ui->detail->show();
                detailTimer->start(3000);
            }

            if (auto_playnext && playlist.count() == 1 && !isSongPlaying()) {
                play(0);
            } else {
                hideUIFrame();
            }
        }
        if (ui->playlistWidget->isVisible() && ui->playlistWidget->rowCount() > 0) {
            if (isShopStrictLocked() && isSongPlaying() && !idlePlaying) {
                notifyShopStrictLocked(tr("ข้ามไปเล่นเพลงในคิว"));
                break;
            }
            ui->playlistWidget->hide();
            play(ui->playlistWidget->currentRow());
        }
        break;
    case Qt::Key_Space:
        if (player->isUseMedley() && ui->playlistWidget->isVisible()) {
            const int row = ui->playlistWidget->currentRow();
            if (row < 0 || row >= playlist.size())
                break;                          // empty queue: no song to set the medley cuts of
            timer2->stop();
            Song *s = playlist[row];
            ui->songMedley->setup(s->cutStartBar(), s->cutEndBar());
            ui->songMedley->show();
            break;
        }

        if (ui->frameSearch->isHidden()) {
            showSongDetail();
            timer2->start(songDetail_timeout);
            break;
        }
    // Queue (open with ↑ ↓): + moves the selected song up, nearer to its turn; - moves it down.
    // "=" is the + key without Shift on the main keyboard; the keypad + / - work as well.
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        if (ui->playlistWidget->isVisible()) {
            if (isShopStrictLocked()) {
                notifyShopStrictLocked(tr("เลื่อนลำดับคิวเพลง"));
                break;
            }
            int i = ui->playlistWidget->currentRow();
            if (i > 0)
                swapInPlaylist(i, i-1);
            timer2->start(playlist_timeout);
            break;
        }
    case Qt::Key_Minus:
        if (ui->playlistWidget->isVisible()) {
            if (isShopStrictLocked()) {
                notifyShopStrictLocked(tr("เลื่อนลำดับคิวเพลง"));
                break;
            }
            int i = ui->playlistWidget->currentRow();
            if (i >= 0 && i < ui->playlistWidget->rowCount() - 1)
                swapInPlaylist(i, i+1);
            timer2->start(playlist_timeout);
            break;
        }
    default:
        // keys that type nothing (Alt / Shift / Ctrl alone, control characters) or Alt shortcuts: no search.
        // Alt + number pad digits (the sound pads) also make Windows type an "Alt code" when Alt is let
        // go: Alt+1 = ☺, Alt+2 = ☻, Alt+3 = ♥ ... (the old symbols for codes 1-31). Not search text either.
        {
            static const QString altCodes = QString::fromUtf8("☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼");
            const QString t = event->text();
            if (t.isEmpty() || !t.at(0).isPrint() || altCodes.contains(t.at(0))
                    || (event->modifiers() & Qt::AltModifier))
                break;
        }
        if (searchBoxChangeBpm) {
            ui->lbSearch->setText("_");
            searchBoxChangeBpm = false;
        }
        if (ui->frameSearch->isVisible()) {
            QString s = ui->lbSearch->text();
            s = s.replace(s.length() - 1, 1, "");
            QString text = searchWithThaiFallback(s + event->text());
            ui->lbSearch->setText(text + "_");
            timer2->start(search_timeout);
        } else {
            db->setSearchType(SearchType::ByAll);     // a new search starts on "ทั้งหมด": codes, names and lyrics
            QString text = searchWithThaiFallback(event->text());
            ui->lbSearch->setText(text + "_");
            showFrameSearch();
            ui->chMix->hide();
            ui->expandChMix->hide();
            timer2->start(search_timeout);
        }
        break;
    }
}

void MainWindow::setSearchMode(SearchType mode)
{
    db->setSearchType(mode);
    if (searchChips)
        searchChips->setCurrent(int(mode));
    QString s = ui->lbSearch->text();
    if (s.endsWith('_'))
        s.chop(1);
    searchBoxChangeBpm = false;
    if (s.isEmpty() && mode != SearchType::ByLyrics && mode != SearchType::ByTempo) {
        searchFooter.clear();
        ++lyricsSearchGen;
        if (lyricsSearchTimer)
            lyricsSearchTimer->stop();
        setFrameSearch(db->search(""));
    } else {
        searchWithThaiFallback(s);
    }
    showFrameSearch();
    timer2->start(search_timeout);
}

QString MainWindow::searchWithThaiFallback(const QString &text)
{
    QElapsedTimer searchT;
    searchT.start();
    struct Report {
        QElapsedTimer &t; const QString &txt; MainWindow *w;
        ~Report() {
            const double ms = t.nsecsElapsed() / 1e6;
            uiLog(QString("search \"%1\" %2 ms%3").arg(txt).arg(ms, 0, 'f', 1)
                  .arg(w->db->isFuzzyResult() ? " (approximate)" : ""));
        }
    } report{ searchT, text, this };
    searchFooter.clear();
    ++lyricsSearchGen;
    if (lyricsSearchTimer)
        lyricsSearchTimer->stop();
    // lyrics: searched in the background once the typing pauses (see searchLyricsLater)
    if (db->currentSearchType() == SearchType::ByLyrics) {
        db->search(text);                   // keeps the typed text for Tab
        searchLyricsLater(text);
        return text;
    }
    // tempo: the songs within 5 BPM of the number typed
    if (db->currentSearchType() == SearchType::ByTempo) {
        Song *s = db->search(text);
        if (db->lastSearchFound()) {
            searchFooter = tr("♩ เพลงความเร็วใกล้ %1 BPM พบ %2 เพลง   (↑ ↓ เลือกเพลง   Enter เพิ่มเข้าคิว)").arg(s->tempo()).arg(db->resultCount());
            setFrameSearch(s);
            ui->lbType->setText(ui->lbType->text() + tr("  ♩ %1 BPM (%2 เพลง)").arg(s->tempo()).arg(db->resultCount()));
        } else {
            showSearchMessage(text.trimmed().isEmpty() ? tr("พิมพ์ความเร็ว (BPM) เช่น 145") : tr("ไม่มีเพลงความเร็วใกล้ %1").arg(text));
        }
        return text;
    }
    // song codes (digits only) are not looked for in the lyrics
    const bool canAutoLyrics = (db->currentSearchType() == SearchType::ByAll)
            && text.contains(QRegExp("[^0-9\\s]"))
            && (LyricsIndex::normalize(text).size() >= 3
                || (text.contains(QRegExp("[A-Za-z]")) && LyricsIndex::normalize(SongDatabase::kedmaneeToThai(text)).size() >= 3));
    // Typed with the keyboard still in English mode (e.g. "lyxH" for "สายฝ")?
    // When nothing matches, retry with the same keys on the Thai Kedmanee layout.
    Song *s = db->search(text);
    if (!db->lastSearchFound() && text.contains(QRegExp("[A-Za-z]"))) {
        QString thai = SongDatabase::kedmaneeToThai(text);
        Song *t = db->search(thai);
        if (db->lastSearchFound()) {
            setFrameSearch(t);
            if (canAutoLyrics && LyricsIndex::normalize(thai).size() >= 4)
                searchLyricsLater(thai, true);
            return thai;
        }
        db->search(text);   // keep the typed text as the search text
    }
    if (db->lastSearchFound()) {
        setFrameSearch(s);
        if (canAutoLyrics && LyricsIndex::normalize(text).size() >= 4)
            searchLyricsLater(text, true);
        return text;
    }
    // nothing exact: approximate search (typos, tone marks, spaces, sound-alike letters)
    Song *f = db->searchFuzzy(text);
    if (!db->isFuzzyResult() && text.contains(QRegExp("[A-Za-z]")))
        f = db->searchFuzzy(SongDatabase::kedmaneeToThai(text));
    if (db->isFuzzyResult()) {
        searchFooter = tr("≈ ค้นชื่อใกล้เคียง พบ %1 เพลง   (↑ ↓ เลือกเพลง   Enter เพิ่มเข้าคิว)").arg(db->resultCount());
        setFrameSearch(f);
        ui->lbType->setText(ui->lbType->text() + tr("  ≈ ใกล้เคียง"));
        if (canAutoLyrics)
            searchLyricsLater(text, true);
        return text;
    }
    db->search(text);
    if (canAutoLyrics) {
        searchLyricsLater(text, false);
        return text;
    }
    if (!text.trimmed().isEmpty()) {
        showSearchMessage(tr("ไม่พบเพลง \"%1\"").arg(text));
        return text;
    }
    setFrameSearch(s);
    return text;
}

// ---- "ค้นจากเนื้อร้อง" (LyricsIndex.h) ---------------------------------------------------------------

QString MainWindow::lyricsDbPath() const
{
    return Config::DATABASE_DIR_PATH + "/Lyrics.db3";
}

// the index of the songs' full lyrics: built in the background, then only new songs are added
void MainWindow::startLyricsIndex()
{
    if (!db || (lyricsIndex && lyricsIndex->running()))
        return;
    delete lyricsIndex;
    lyricsIndex = new LyricsIndex();
    lyricsIndex->onFinished = [](int added) {
        if (added > 0)
            uiLog(QString("lyrics index: %1 songs added").arg(added));
    };
    lyricsIndex->start(lyricsDbPath(), db->database()->databaseName(), db->ncnPath(), db->hnkPath(), db->sibPath());
}

// a line in the search bar instead of a song (searching, nothing found, what to type)
void MainWindow::showSearchMessage(const QString &msg)
{
    if (searchChips)
        searchChips->setCurrent(int(db->currentSearchType()));
    ui->lbId->setText(QString());
    ui->lbName->setText(" " + msg);
    ui->lbBtw->hide();
    ui->lbArtist->setText(QString());
    ui->lbTempoKey->setText(QString());
    ui->lbType->setText(QString());
    ui->lbLyrics->setText(QString());
    ui->frame_3->setVisible(false);
    if (searchResults) {
        searchResults->setRows({}, -1, QString());
        searchResults->hide();
    }
    ui->frame_4->show();                // the message goes in the song line (hidden while a list shows)
    ui->frameSearch->setFixedHeight(168);
    fitSearchBar();
}

// The lyrics are looked up on another thread a moment after the last key (a search reads the
// whole index, about a second); only the answer to the newest text is shown.
void MainWindow::searchLyricsLater(const QString &text, bool quiet)
{
    lyricsSearchText = text;
    const int idxPct = lyricsIndex ? lyricsIndex->progressPercent() : 100;
    const QString building = (lyricsIndex && lyricsIndex->running()) ? tr("  (กำลังสร้างดัชนีเนื้อร้อง %1%)").arg(idxPct) : QString();
    const int normLen = qMax(LyricsIndex::normalize(text).size(),
                             text.contains(QRegExp("[A-Za-z]")) ? LyricsIndex::normalize(SongDatabase::kedmaneeToThai(text)).size() : 0);
    if (normLen < 3) {
        if (!quiet)
            showSearchMessage(tr("พิมพ์ท่อนเนื้อร้องที่จำได้ อย่างน้อย 3 ตัวอักษร") + building);
        return;
    }
    if (!quiet)
        showSearchMessage(tr("กำลังค้นหาเนื้อร้อง \"%1\" ...").arg(text) + building);
    if (!lyricsSearchTimer) {
        lyricsSearchTimer = new QTimer(this);
        lyricsSearchTimer->setSingleShot(true);
        lyricsSearchTimer->setInterval(450);
        connect(lyricsSearchTimer, &QTimer::timeout, this, [this]() {
            if (!lyricsPool) {
                lyricsPool = new QThreadPool(this);
                lyricsPool->setMaxThreadCount(1);
            }
            const int gen = ++lyricsSearchGen;
            const QString t = lyricsSearchText, file = lyricsDbPath(), songsFile = db->database()->databaseName();
            QPointer<MainWindow> self(this);
            lyricsPool->start([self, gen, t, file, songsFile]() {
                QElapsedTimer clock;
                clock.start();
                auto keys = LyricsIndex::find(file, t);
                if (keys.isEmpty() && t.contains(QRegExp("[A-Za-z]")))
                    keys = LyricsIndex::find(file, SongDatabase::kedmaneeToThai(t));
                // the songs of those lyrics, here too: hundreds of them took a third of a second on screen
                const QVector<qint64> rows = SongDatabase::rowsForKeys(songsFile, keys);
                const qint64 ms = clock.elapsed();
                QMetaObject::invokeMethod(qApp, [self, gen, t, rows, ms]() {
                    if (!self || gen != self->lyricsSearchGen)
                        return;                     // typed on since: a newer search is coming
                    self->onLyricsFound(t, rows, ms);
                }, Qt::QueuedConnection);
            });
        });
    }
    lyricsSearchTimer->start();
}

void MainWindow::onLyricsFound(const QString &text, const QVector<qint64> &rows, qint64 ms)
{
    uiLog(QString("lyrics search \"%1\": %2 songs in %3 ms").arg(text).arg(rows.size()).arg(ms));
    if (!ui->frameSearch->isVisible())
        return;
    if (db->currentSearchType() == SearchType::ByAll) {
        // One list: the songs found by code / name / artist (or a name like it) first, then the
        // songs whose lyrics have the text, each song once. The song picked so far stays picked.
        const bool found = db->lastSearchFound();
        const bool approx = db->isFuzzyResult();
        const QVector<qint64> names = found ? db->currentRows(300) : QVector<qint64>();
        const int pos = found ? db->resultIndex() : 0;
        QVector<qint64> merged = names;
        QSet<qint64> seen(names.begin(), names.end());
        for (const qint64 r : rows)
            if (!seen.contains(r)) {
                seen.insert(r);
                merged.append(r);
            }
        const int byLyrics = merged.size() - names.size();
        if (byLyrics == 0) {
            if (!found)
                showSearchMessage(tr("ไม่พบเพลงหรือเนื้อร้อง \"%1\"").arg(text));
            return;                         // nothing more than what is shown
        }
        db->showRows(merged, text, pos, names.size());
        if (names.isEmpty())
            searchFooter = tr("♪ เพลงที่เนื้อร้องมีคำนี้ %1 เพลง   (↑ ↓ เลือกเพลง   Enter เพิ่มเข้าคิว)").arg(byLyrics);
        else
            searchFooter = tr("%1 %2 เพลง   ♪ เนื้อร้องตรงอีก %3 เพลง ต่อท้ายรายการ   (↑ ↓ เลือก   Enter เข้าคิว)")
                           .arg(approx ? tr("≈ ชื่อใกล้เคียง") : tr("ชื่อ / รหัสตรง"))
                           .arg(names.size() >= 300 ? QString("300+") : QString::number(names.size()))
                           .arg(byLyrics);
        setFrameSearch(db->currentSong());
        timer2->start(search_timeout);
        return;
    }
    if (db->currentSearchType() != SearchType::ByLyrics)
        return;
    if (db->showRows(rows, text)) {
        searchFooter = tr("♪ พบเนื้อร้อง \"%1\" ทั้งหมด %2 เพลง   (↑ ↓ เลือกเพลง   Enter เพิ่มเข้าคิว)").arg(text).arg(db->resultCount());
        setFrameSearch(db->currentSong());
        ui->lbType->setText(ui->lbType->text() + tr("  ♪ เนื้อร้อง (%1 เพลง)").arg(db->resultCount()));
        timer2->start(search_timeout);
        return;
    }
    QString msg = tr("ไม่พบเนื้อร้อง \"%1\"").arg(text);
    if (LyricsIndex::songCount(lyricsDbPath()) == 0)
        msg = tr("ยังไม่มีดัชนีเนื้อร้อง (โปรแกรมกำลังสร้างอยู่เบื้องหลัง ลองใหม่อีกสักครู่)");
    else if (lyricsIndex && lyricsIndex->running())
        msg += tr("  (ดัชนีเนื้อร้องสร้างไปแล้ว %1%)").arg(lyricsIndex->progressPercent());
    showSearchMessage(msg);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->frameSearch) {
        if (event->type() == QEvent::Hide && searchResults)
            searchResults->hide();
        else if (event->type() == QEvent::Show)
            updateSearchResults();
    }
    if (event->type() == QEvent::MouseButtonDblClick) {
        // double-click on the screen (picture, video, lyrics): full screen on / off. Not on the bar,
        // the song search, the queue or the channel mixer, which use double-clicks themselves.
        QWidget *w = qobject_cast<QWidget *>(watched);
        auto inside = [w](QWidget *area) { return area && (w == area || area->isAncestorOf(w)); };
        if (w && w->window() == this && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton
                && !inside(ui->frameControl) && !inside(ui->frameSearch) && !inside(searchResults)
                && !inside(ui->playlistWidget) && !inside(ui->chMix) && !inside(ui->songMedley)) {
            showFullScreenOrNormal();
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonDblClick && ui->frameSearch->isVisible()) {
        // double-click on the song found = Enter (into the queue / play)
        QWidget *w = qobject_cast<QWidget *>(watched);
        if (w && w != searchChips && (w == ui->frameSearch || ui->frameSearch->isAncestorOf(w))) {
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            keyPressEvent(&enter);
            setFocus();
            return true;
        }
    }
    if (watched == ui->frameControl && event->type() == QEvent::Resize && !fitBarQueued) {
        fitBarQueued = true;        // once the resize is over
        QTimer::singleShot(0, this, [this]() { fitBarQueued = false; fitControlBar(); });
    }
    if (watched == ui->sliderPosition) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                const int w = ui->sliderPosition->width();
                if (w > 0) {
                    const int margin = 8;
                    const int avail = qMax(1, w - 2 * margin);
                    const double ratio = qBound(0.0, double(me->pos().x() - margin) / double(avail), 1.0);
                    const int minVal = ui->sliderPosition->minimum();
                    const int maxVal = ui->sliderPosition->maximum();
                    const int val = int(minVal + ratio * (maxVal - minVal));
                    ui->sliderPosition->grabMouse();
                    ui->sliderPosition->setValue(val);
                    ui->sliderPosition->setSliderDown(true);
                    emit ui->sliderPosition->sliderMoved(val);
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseMove) {
            QMouseEvent *me = static_cast<QMouseEvent *>(event);
            if (me->buttons() & Qt::LeftButton) {
                const int w = ui->sliderPosition->width();
                if (w > 0) {
                    const int margin = 8;
                    const int avail = qMax(1, w - 2 * margin);
                    const double ratio = qBound(0.0, double(me->pos().x() - margin) / double(avail), 1.0);
                    const int minVal = ui->sliderPosition->minimum();
                    const int maxVal = ui->sliderPosition->maximum();
                    const int val = int(minVal + ratio * (maxVal - minVal));
                    ui->sliderPosition->setValue(val);
                    emit ui->sliderPosition->sliderMoved(val);
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                if (ui->sliderPosition->mouseGrabber() == ui->sliderPosition)
                    ui->sliderPosition->releaseMouse();
                if (ui->sliderPosition->isSliderDown())
                    ui->sliderPosition->setSliderDown(false);
                return true;
            }
        } else if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide) {
            if (ui->sliderPosition->mouseGrabber() == ui->sliderPosition)
                ui->sliderPosition->releaseMouse();
            if (ui->sliderPosition->isSliderDown())
                ui->sliderPosition->setSliderDown(false);
        }
    }
    // Player shortcuts typed while the mixer is the active window go to the player.
    // Esc stays with the mixer (closes it); typing into a text / number field stays there.
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
            && QApplication::activeWindow() == synthMix && synthMix->isVisible()
            && QApplication::activeModalWidget() == nullptr && QApplication::activePopupWidget() == nullptr) {
        QKeyEvent *k = static_cast<QKeyEvent *>(event);
        QWidget *f = QApplication::focusWidget();
        const bool editing = qobject_cast<QLineEdit *>(f) || qobject_cast<QAbstractSpinBox *>(f)
                || (qobject_cast<QComboBox *>(f) && qobject_cast<QComboBox *>(f)->isEditable());
        if (k->key() != Qt::Key_Escape && !editing && watched == QApplication::focusObject()) {
            if (event->type() == QEvent::KeyPress)
                keyPressEvent(k);
            else
                keyReleaseEvent(k);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (event->modifiers() == Qt::ControlModifier) {
        this->sendDrumPads(event, false);
    }
}
