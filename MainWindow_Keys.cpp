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
            Song *s = playlist[ui->playlistWidget->currentRow()];
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

    if (event->modifiers() == Qt::ControlModifier) {
        switch (event->key()) {
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
                    int i = ui->playlistWidget->currentRow();
                    if (i > 0)
                        swapInPlaylist(i, i-1);
                    timer2->start(playlist_timeout);
                }
                break;
            case Qt::Key_Minus:
                if (ui->playlistWidget->isVisible()) {
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
        case Qt::Key_Delete:
            if (ui->frameSearch->isVisible()) {
                DeleteSongDialog dlg(this);
                dlg.setModal(true);
                dlg.adjustSize();
                dlg.setFixedSize(dlg.size());
                dlg.exec();

                if (dlg.removeConfirmed()) {
                    db->removeCurrentSong(dlg.removeFromStorage());
                    setFrameSearch( db->searchNext() );
                    if (ui->frameSearch->isVisible()) {
                        timer2->start(search_timeout);
                    }
                }
            }
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
        if (player->isUseMedley() && player->midiFile() && !player->midiFile()->events().isEmpty()) {
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
                player->setUseMedley(false);
                player->unloadNextMedley();
                if (!player->isPlayerStopped())
                    player->midiSequencer()->setEndTick(0);
                if (btnMedley) { QSignalBlocker b(btnMedley); btnMedley->setChecked(false); }
                ui->detail->setDetail(tr("เมดเลย์ "), tr("ปิด"));
                ui->detail->show();
                detailTimer->start(3000);
                if (this->width() < 1160 && ui->chMix->isVisible())
                    ui->lcdTime->hide();
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
        if (player->isPlayerPaused())
            resume();
        else
            play(-1);
        break;
    }
    case Qt::Key_F6: {
        pause();
        break;
    }
    case Qt::Key_F7: {
        stop();
        break;
    }
    case Qt::Key_F8: {
        playPrevious();
        break;
    }
    case Qt::Key_Pause:
        toggleDrumHold();
        break;
    case Qt::Key_F9: {
        playNext();
        break;
    }
    case Qt::Key_Asterisk: {
        if (ui->chMix->isMuteVoice()) {
            ui->chMix->setMuteVoice(false);
            ui->detail->setDetail(tr("เสียงร้อง "), tr("เปิด"));
        } else {
            ui->chMix->setMuteVoice(true);
            ui->detail->setDetail(tr("เสียงร้อง "), tr("ปิด"));
        }
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
            int i = ui->playlistWidget->currentRow();
            if (i<0)
                break;
            removeFromPlaylist(i);
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
        if (playingSong.id() != "") {
            play(-1);
        }
        break;
    case Qt::Key_End:
        endSongSmoothly();
        break;
    case Qt::Key_Up:
        if (ui->playlistWidget->isVisible()) {
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
        if (ui->playlistWidget->isVisible()) {
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
        if (ui->frameSearch->isVisible()) {
            QString s = ui->lbSearch->text();
            s = s.replace(s.length() - 1, 1, "");
            setFrameSearch( db->nextType(s) );
            {
                QString mode;
                switch (db->currentSearchType()) {
                case SearchType::ByAll:    mode = tr("ทั้งหมด (วิดีโอ/MP3 ขึ้นก่อน)"); break;
                case SearchType::ById:     mode = tr("รหัสเพลง"); break;
                case SearchType::ByName:   mode = tr("ชื่อเพลง"); break;
                case SearchType::ByArtist: mode = tr("ชื่อนักร้อง"); break;
                case SearchType::ByVideo:  mode = tr("เฉพาะวิดีโอ / MP3"); break;
                }
                ui->detail->setDetail(tr("ค้นหา (Tab เปลี่ยน)"), mode);
                ui->detail->show();
                detailTimer->start(1500);
            }
            timer2->start(search_timeout);
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
            Song *s = db->currentSong();
            addToPlaylist(s);

            if (auto_playnext && playlist.count() == 1 && !isSongPlaying()) {
                play(0);
            } else {
                hideUIFrame();
            }
        }
        if (ui->playlistWidget->isVisible() && ui->playlistWidget->rowCount() > 0) {
            ui->playlistWidget->hide();
            play(ui->playlistWidget->currentRow());
        }
        break;
    case Qt::Key_Space:
        if (player->isUseMedley() && ui->playlistWidget->isVisible()) {
            timer2->stop();
            Song *s = playlist[ui->playlistWidget->currentRow()];
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
            int i = ui->playlistWidget->currentRow();
            if (i > 0)
                swapInPlaylist(i, i-1);
            timer2->start(playlist_timeout);
            break;
        }
    case Qt::Key_Minus:
        if (ui->playlistWidget->isVisible()) {
            int i = ui->playlistWidget->currentRow();
            if (i >= 0 && i < ui->playlistWidget->rowCount() - 1)
                swapInPlaylist(i, i+1);
            timer2->start(playlist_timeout);
            break;
        }
    default:
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
            db->setSearchType(SearchType::ByAll);
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
    // Typed with the keyboard still in English mode (e.g. "lyxH" for "สายฝ")?
    // When nothing matches, retry with the same keys on the Thai Kedmanee layout.
    Song *s = db->search(text);
    if (!db->lastSearchFound() && text.contains(QRegExp("[A-Za-z]"))) {
        QString thai = SongDatabase::kedmaneeToThai(text);
        Song *t = db->search(thai);
        if (db->lastSearchFound()) {
            setFrameSearch(t);
            return thai;
        }
        db->search(text);   // keep the typed text as the search text
    }
    if (!db->lastSearchFound()) {
        // nothing exact: approximate search (typos, tone marks, spaces, sound-alike letters)
        Song *f = db->searchFuzzy(text);
        if (!db->isFuzzyResult() && text.contains(QRegExp("[A-Za-z]")))
            f = db->searchFuzzy(SongDatabase::kedmaneeToThai(text));
        if (db->isFuzzyResult()) {
            setFrameSearch(f);
            ui->lbType->setText(ui->lbType->text() + tr("  ≈ ใกล้เคียง"));
            return text;
        }
        db->search(text);
    }
    setFrameSearch(s);
    return text;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
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
