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
        editMedleySet(QString());     // empty queue: build the set in the editor
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

// The list of sets: create, edit, play, delete.
void MainWindow::manageMedleySets()
{
    QDialog dlg(dialogParent());
    dlg.setWindowTitle(tr("ชุดเมดเลย์"));
    dlg.resize(520, 420);
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    QLabel *hint = new QLabel(tr("ชุดเมดเลย์ = รายการเพลงที่จัดไว้ล่วงหน้า พร้อมความเร็วและลูกส่ง\nบนเวทีเลือกจากเมนูคลิกขวา > ชุดเมดเลย์ แล้วกดเล่น"), &dlg);
    hint->setStyleSheet("color: gray;");
    v->addWidget(hint);
    QListWidget *list = new QListWidget(&dlg);
    v->addWidget(list, 1);
    QLabel *empty = new QLabel(tr("ยังไม่มีชุด  -  กด \"+ สร้างชุดใหม่\""), &dlg);
    empty->setAlignment(Qt::AlignCenter);
    v->addWidget(empty);
    auto refresh = [this, list, empty]() {
        list->clear();
        for (const QString &n : medleySetNames()) {
            QSettings ini(medleySetFileBase(n) + ".ini", QSettings::IniFormat);
            ini.setIniCodec("UTF-8");
            const int bpm = ini.value("BPM", 0).toInt();
            QListWidgetItem *it = new QListWidgetItem(QString("%1   (%2 เพลง, %3, ลูกส่ง %4 ห้อง)").arg(n)
                    .arg(ini.value("Songs", 0).toInt())
                    .arg(bpm > 0 ? QString("%1 BPM").arg(bpm) : tr("ความเร็วตามต้นฉบับ"))
                    .arg(ini.value("TransitionBars", 1).toInt()), list);
            it->setData(Qt::UserRole, n);
        }
        empty->setVisible(list->count() == 0);
        if (list->count() > 0)
            list->setCurrentRow(0);
    };
    refresh();

    QHBoxLayout *row = new QHBoxLayout();
    QPushButton *btnNew = new QPushButton(tr("+ สร้างชุดใหม่"), &dlg);
    QPushButton *btnEdit = new QPushButton(tr("แก้ไขชุด"), &dlg);
    QPushButton *btnPlay = new QPushButton(tr("ใช้ชุดนี้"), &dlg);
    QPushButton *btnDelete = new QPushButton(tr("ลบชุด"), &dlg);
    QPushButton *btnClose = new QPushButton(tr("ปิด"), &dlg);
    btnNew->setStyleSheet("QPushButton{font-weight:bold;color:#16a34a;}");
    for (QPushButton *b : { btnNew, btnEdit, btnPlay, btnDelete }) {
        b->setMinimumHeight(34);
        b->setAutoDefault(false);
        row->addWidget(b);
    }
    row->addStretch();
    row->addWidget(btnClose);
    v->addLayout(row);

    auto selected = [list]() { return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString(); };
    connect(btnNew, &QPushButton::clicked, &dlg, [&, refresh]() { if (editMedleySet(QString())) refresh(); });
    connect(btnEdit, &QPushButton::clicked, &dlg, [&, selected, refresh]() {
        const QString n = selected();
        if (!n.isEmpty() && editMedleySet(n)) refresh();
    });
    connect(list, &QListWidget::itemDoubleClicked, btnEdit, &QPushButton::click);
    connect(btnPlay, &QPushButton::clicked, &dlg, [&, selected]() {
        const QString n = selected();
        if (n.isEmpty()) return;
        dlg.accept();
        loadMedleySet(n);
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

// Create / edit one set: name, tempo, bridge, and its songs (search the song library on the
// left, the set on the right).
bool MainWindow::editMedleySet(const QString &name)
{
    QDialog dlg(dialogParent());
    dlg.setWindowTitle(name.isEmpty() ? tr("สร้างชุดเมดเลย์") : tr("แก้ไขชุดเมดเลย์"));
    dlg.resize(900, 560);
    QVBoxLayout *v = new QVBoxLayout(&dlg);

    // name, tempo, bridge
    QSettings *ini = nullptr;
    QList<Song *> songs;
    if (!name.isEmpty()) {
        Utils::loadPlaylist(medleySetFileBase(name) + ".bmpl", songs);
        ini = new QSettings(medleySetFileBase(name) + ".ini", QSettings::IniFormat, &dlg);
        ini->setIniCodec("UTF-8");
    }
    QGridLayout *top = new QGridLayout();
    QLineEdit *nameEdit = new QLineEdit(name, &dlg);
    nameEdit->setPlaceholderText(tr("เช่น สามช่า 150, ลูกทุ่งรำวง, งานแต่ง"));
    top->addWidget(new QLabel(tr("ชื่อชุด:"), &dlg), 0, 0);
    top->addWidget(nameEdit, 0, 1, 1, 3);

    const int savedBpm = ini ? ini->value("BPM", 150).toInt() : (player->medleyBPM() > 0 ? player->medleyBPM() : 150);
    QRadioButton *rFollow = new QRadioButton(tr("ตามต้นฉบับแต่ละเพลง"), &dlg);
    QRadioButton *rLock = new QRadioButton(tr("ล็อกทุกเพลงที่"), &dlg);
    QSpinBox *spinBpm = new QSpinBox(&dlg);
    spinBpm->setRange(40, 250);
    spinBpm->setSuffix(" BPM");
    spinBpm->setValue(savedBpm > 0 ? savedBpm : 150);
    (savedBpm > 0 ? rLock : rFollow)->setChecked(true);
    spinBpm->setEnabled(rLock->isChecked());
    connect(rLock, &QRadioButton::toggled, spinBpm, &QSpinBox::setEnabled);
    QHBoxLayout *tempoRow = new QHBoxLayout();
    tempoRow->addWidget(rFollow);
    tempoRow->addSpacing(12);
    tempoRow->addWidget(rLock);
    tempoRow->addWidget(spinBpm);
    tempoRow->addStretch();
    top->addWidget(new QLabel(tr("ความเร็ว:"), &dlg), 1, 0);
    top->addLayout(tempoRow, 1, 1, 1, 3);

    QComboBox *bars = new QComboBox(&dlg);
    bars->addItem(tr("0 ห้อง (ต่อทันที)"), 0);
    bars->addItem(tr("1 ห้อง (ลูกส่ง)"), 1);
    bars->addItem(tr("2 ห้อง (จังหวะ 1 + ลูกส่ง 1)"), 2);
    bars->addItem(tr("3 ห้อง (จังหวะ 2 + ลูกส่ง 1)"), 3);
    bars->addItem(tr("4 ห้อง (จังหวะ 3 + ลูกส่ง 1)"), 4);
    bars->setCurrentIndex(qBound(0, ini ? ini->value("TransitionBars", 2).toInt() : player->medleyTransitionBars(), 4));
    top->addWidget(new QLabel(tr("ลูกส่งระหว่างเพลง:"), &dlg), 2, 0);
    top->addWidget(bars, 2, 1);
    top->setColumnStretch(3, 1);
    v->addLayout(top);

    // left: find songs / right: the set
    QHBoxLayout *mid = new QHBoxLayout();
    QVBoxLayout *left = new QVBoxLayout();
    left->addWidget(new QLabel(tr("1. ค้นหาเพลง (ชื่อเพลง / ศิลปิน / รหัส)"), &dlg));
    QLineEdit *find = new QLineEdit(&dlg);
    find->setPlaceholderText(tr("พิมพ์แล้วรอสักครู่..."));
    find->setClearButtonEnabled(true);
    left->addWidget(find);
    QListWidget *results = new QListWidget(&dlg);
    left->addWidget(results, 1);
    QLabel *legend = new QLabel(tr("สีเขียว = ความเร็วเข้ากับที่ล็อกไว้ (ต่อกันเนียน)"), &dlg);
    legend->setStyleSheet("color: #15803d;");
    left->addWidget(legend);
    QPushButton *btnAdd = new QPushButton(tr("เพิ่มเข้าชุด  >>"), &dlg);
    btnAdd->setMinimumHeight(34);
    btnAdd->setStyleSheet("QPushButton{font-weight:bold;color:#16a34a;}");
    left->addWidget(btnAdd);
    mid->addLayout(left, 1);

    QVBoxLayout *right = new QVBoxLayout();
    right->addWidget(new QLabel(tr("2. เพลงในชุด (เรียงตามลำดับที่จะเล่น)"), &dlg));
    QListWidget *setList = new QListWidget(&dlg);
    right->addWidget(setList, 1);
    QHBoxLayout *setBtns = new QHBoxLayout();
    QPushButton *btnRemove = new QPushButton(tr("ลบ"), &dlg);
    QPushButton *btnUp = new QPushButton(tr("ขึ้น"), &dlg);
    QPushButton *btnDown = new QPushButton(tr("ลง"), &dlg);
    QPushButton *btnFromQueue = new QPushButton(tr("เพิ่มจากคิวตอนนี้"), &dlg);
    for (QPushButton *b : { btnRemove, btnUp, btnDown, btnFromQueue }) {
        b->setAutoDefault(false);
        setBtns->addWidget(b);
    }
    right->addLayout(setBtns);
    QLabel *count = new QLabel(&dlg);
    right->addWidget(count);
    mid->addLayout(right, 1);
    v->addLayout(mid, 1);

    // tempo / key text of a song, and a green mark when it suits the locked tempo
    // (same tempo +-8, or half / double time, as the medley plays it)
    auto tempoText = [](Song &s) {
        QStringList t;
        if (s.tempo() > 0) t << QString("%1 BPM").arg(s.tempo());
        if (!s.key().isEmpty()) t << s.key();
        return t.isEmpty() ? QString() : "  [" + t.join(", ") + "]";
    };
    auto suits = [rLock, spinBpm](Song &s) {
        if (!rLock->isChecked() || s.tempo() <= 0) return false;
        const int lock = spinBpm->value();
        for (double f : { 1.0, 0.5, 2.0 })
            if (qAbs(s.tempo() * f - lock) <= 8) return true;
        return false;
    };

    // the set's songs live here (deleted when the dialog closes)
    struct Owned { QList<Song *> list; ~Owned() { qDeleteAll(list); } } owned;
    owned.list = songs;
    auto showSet = [setList, count, &owned, tempoText, suits]() {
        const int row = setList->currentRow();
        setList->clear();
        for (int i = 0; i < owned.list.size(); ++i) {
            Song *s = owned.list[i];
            QListWidgetItem *it = new QListWidgetItem(QString("%1. %2 - %3  (%4)%5").arg(i + 1)
                    .arg(s->name(), s->artist(), s->songType(), tempoText(*s)), setList);
            if (suits(*s)) it->setForeground(QColor("#15803d"));
        }
        setList->setCurrentRow(qMin(row, setList->count() - 1));
        count->setText(QObject::tr("%1 เพลง").arg(owned.list.size()));
    };
    showSet();

    QList<Song> found;
    QTimer *debounce = new QTimer(&dlg);
    debounce->setSingleShot(true);
    debounce->setInterval(300);
    connect(find, &QLineEdit::textChanged, debounce, [debounce]() { debounce->start(); });
    auto showFound = [results, &found, tempoText, suits]() {
        const int row = results->currentRow();
        results->clear();
        for (Song &s : found) {
            QListWidgetItem *it = new QListWidgetItem(QString("%1  %2 - %3  (%4)%5")
                    .arg(s.id(), s.name(), s.artist(), s.songType(), tempoText(s)), results);
            if (suits(s)) it->setForeground(QColor("#15803d"));
        }
        if (row >= 0 && row < results->count())
            results->setCurrentRow(row);
    };
    // the green marks follow the tempo setting
    connect(rLock, &QRadioButton::toggled, &dlg, [showFound, showSet]() { showFound(); showSet(); });
    connect(spinBpm, QOverload<int>::of(&QSpinBox::valueChanged), &dlg, [showFound, showSet]() { showFound(); showSet(); });
    connect(debounce, &QTimer::timeout, &dlg, [this, find, results, &found, showFound]() {
        found = find->text().trimmed().isEmpty() ? QList<Song>() : db->findSongs(find->text().trimmed(), 100);
        showFound();
        if (results->count() > 0)
            results->setCurrentRow(0);
    });
    auto addSelected = [results, &found, &owned, showSet, setList]() {
        const QList<QListWidgetItem *> items = results->selectedItems();
        for (QListWidgetItem *it : items) {
            const int r = results->row(it);
            if (r >= 0 && r < found.size())
                owned.list << new Song(found[r]);
        }
        showSet();
        setList->setCurrentRow(setList->count() - 1);
    };
    results->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(btnAdd, &QPushButton::clicked, &dlg, addSelected);
    connect(results, &QListWidget::itemDoubleClicked, &dlg, addSelected);
    connect(find, &QLineEdit::returnPressed, &dlg, [debounce, addSelected, results]() {
        if (debounce->isActive() || results->count() == 0) return;
        addSelected();
    });
    connect(btnRemove, &QPushButton::clicked, &dlg, [setList, &owned, showSet]() {
        const int r = setList->currentRow();
        if (r < 0) return;
        delete owned.list.takeAt(r);
        showSet();
    });
    auto move = [setList, &owned, showSet](int d) {
        const int r = setList->currentRow(), t = r + d;
        if (r < 0 || t < 0 || t >= owned.list.size()) return;
        owned.list.swapItemsAt(r, t);
        setList->setCurrentRow(t);
        showSet();
        setList->setCurrentRow(t);
    };
    connect(btnUp, &QPushButton::clicked, &dlg, [move]() { move(-1); });
    connect(btnDown, &QPushButton::clicked, &dlg, [move]() { move(1); });
    connect(btnFromQueue, &QPushButton::clicked, &dlg, [this, &owned, showSet]() {
        if (remove_playlist && !player->isPlayerStopped() && !playingSong.id().isEmpty())
            owned.list << new Song(playingSong);
        for (Song *s : playlist)
            owned.list << new Song(*s);
        showSet();
    });

    QHBoxLayout *bottom = new QHBoxLayout();
    QPushButton *btnSave = new QPushButton(tr("บันทึกชุด"), &dlg);
    QPushButton *btnCancel = new QPushButton(tr("ยกเลิก"), &dlg);
    btnSave->setMinimumSize(140, 38);
    btnSave->setAutoDefault(false);
    btnCancel->setAutoDefault(false);
    bottom->addStretch();
    bottom->addWidget(btnSave);
    bottom->addWidget(btnCancel);
    v->addLayout(bottom);
    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnSave, &QPushButton::clicked, &dlg, [&]() {
        const QString n = nameEdit->text().trimmed();
        if (n.isEmpty()) {
            QMessageBox::information(&dlg, dlg.windowTitle(), tr("กรุณาตั้งชื่อชุด"));
            nameEdit->setFocus();
            return;
        }
        if (owned.list.isEmpty()) {
            QMessageBox::information(&dlg, dlg.windowTitle(), tr("ยังไม่มีเพลงในชุด\nค้นหาเพลงทางซ้าย แล้วกด \"เพิ่มเข้าชุด\""));
            find->setFocus();
            return;
        }
        const QString base = medleySetFileBase(n);
        if (n != name && QFile::exists(base + ".bmpl")
                && QMessageBox::question(&dlg, dlg.windowTitle(), tr("มีชุด \"%1\" อยู่แล้ว บันทึกทับไหม?").arg(n)) != QMessageBox::Yes)
            return;
        if (!Utils::savePlaylist(base + ".bmpl", owned.list)) {
            QMessageBox::warning(&dlg, dlg.windowTitle(), tr("บันทึกไม่สำเร็จ"));
            return;
        }
        QSettings out(base + ".ini", QSettings::IniFormat);
        out.setIniCodec("UTF-8");
        out.setValue("Name", n);
        out.setValue("BPM", rLock->isChecked() ? spinBpm->value() : 0);
        out.setValue("TempoMode", rLock->isChecked() ? 0 : 1);
        out.setValue("TransitionBars", bars->currentData().toInt());
        out.setValue("FillVolume", ini ? ini->value("FillVolume", player->medleyFillVolume()).toInt() : player->medleyFillVolume());
        out.setValue("Songs", owned.list.size());
        out.sync();
        if (!name.isEmpty() && n != name) {         // renamed: drop the old files
            QFile::remove(medleySetFileBase(name) + ".bmpl");
            QFile::remove(medleySetFileBase(name) + ".ini");
        }
        dlg.accept();
    });
    nameEdit->setFocus();
    return dlg.exec() == QDialog::Accepted;
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
