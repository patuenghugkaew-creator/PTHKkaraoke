// MainWindow: Song requests from phones (request server), artist codes
#include "MainWindow_p.h"

void MainWindow::ensureRequestServer()
{
    if (reqServer)
        return;
    reqServer = new SongRequestServer(this);
    reqServer->setAutoApprove(settings->value("RequestAutoApprove", false).toBool());

    reqServer->searchLibrary = [this](const QString &text) {
        QJsonArray items;
        for (Song s : db->findSongs(text, 30))
            items.append(QJsonObject{ { "key", "L:" + s.songType() + "|" + s.path() },
                                      { "title", s.name() },
                                      { "artist", s.artist() + "  (" + s.id() + ")" } });
        return items;
    };
    reqServer->queueState = [this]() {
        QJsonObject st;
        if (isSongPlaying())
            st["now"] = QJsonObject{ { "title", playingSong.name() }, { "artist", playingSong.artist() } };
        QJsonArray q;
        for (int i = playingIndex + 1; i < playlist.count() && q.size() < 15; i++)
            q.append(QJsonObject{ { "title", playlist[i]->name() }, { "artist", playlist[i]->artist() } });
        st["queue"] = q;
        return st;
    };
    reqServer->addToQueue = [this](const SongRequestServer::Request &r) {
        if (r.key.startsWith("Y:")) {
            YtItem it;
            it.id = r.key.mid(2);
            it.title = r.title;
            it.channel = r.artist;
            it.seconds = r.seconds;
            addYouTubeToQueue(it, false);
            return true;
        }
        const QString rest = r.key.mid(2);
        const int bar = rest.indexOf('|');
        Song s;
        if (bar < 0 || !db->songByPath(rest.left(bar), rest.mid(bar + 1), &s))
            return false;
        addToPlaylist(&s);
        if (auto_playnext && !isSongPlaying())
            play(playlist.count() - 1);
        else {
            showPlaylist();
            timer2->start(playlist_timeout);
        }
        return true;
    };
    connect(reqServer, &SongRequestServer::requestsChanged, this, &MainWindow::updateRequestToast);

    reqToast = new QFrame(this);
    reqToast->setStyleSheet("QFrame { background: rgba(20, 22, 28, 230); border: 1px solid #3a7bd5; border-radius: 10px; }"
                            "QLabel { color: white; background: transparent; border: none; }"
                            "QPushButton { color: white; border: none; border-radius: 6px; padding: 4px 10px; }");
    new QVBoxLayout(reqToast);
    reqToast->hide();
}

// what waits for approval, top right of the player; buttons never take the keyboard focus
void MainWindow::updateRequestToast()
{
    if (!reqToast)
        return;
    QLayout *lay = reqToast->layout();
    while (QLayoutItem *li = lay->takeAt(0)) {
        if (li->widget())
            li->widget()->deleteLater();
        if (li->layout()) {
            while (QLayoutItem *c = li->layout()->takeAt(0)) {
                if (c->widget()) c->widget()->deleteLater();
                delete c;
            }
        }
        delete li;
    }
    const QList<SongRequestServer::Request> wait = reqServer->pending();
    if (wait.isEmpty()) {
        reqToast->hide();
        return;
    }
    QLabel *head = new QLabel(tr("คำขอเพลงจากมือถือ (%1)").arg(wait.size()), reqToast);
    QFont hf = head->font();
    hf.setBold(true);
    head->setFont(hf);
    lay->addWidget(head);
    for (int i = 0; i < wait.size() && i < 5; i++) {
        const SongRequestServer::Request r = wait[i];
        QHBoxLayout *row = new QHBoxLayout();
        QLabel *t = new QLabel(r.title.left(48) + "\n" + (r.who.isEmpty() ? tr("ไม่ระบุชื่อ") : r.who), reqToast);
        QPushButton *ok = new QPushButton(tr("อนุมัติ"), reqToast);
        QPushButton *no = new QPushButton(tr("ไม่"), reqToast);
        ok->setStyleSheet("QPushButton { background: #2e9e5b; color: white; font-weight: bold; border-radius: 5px; padding: 5px 14px; min-width: 68px; }");
        no->setStyleSheet("QPushButton { background: #c94a4a; color: white; font-weight: bold; border-radius: 5px; padding: 5px 14px; min-width: 54px; }");
        for (QPushButton *b : { ok, no })
            b->setFocusPolicy(Qt::NoFocus);
        const int id = r.id;
        connect(ok, &QPushButton::clicked, this, [this, id]() { reqServer->decide(id, true); });
        connect(no, &QPushButton::clicked, this, [this, id]() { reqServer->decide(id, false); });
        row->addWidget(t, 1);
        row->addWidget(ok);
        row->addWidget(no);
        static_cast<QVBoxLayout *>(lay)->addLayout(row);
    }
    reqToast->setFixedWidth(360);
    reqToast->adjustSize();
    reqToast->move(width() - reqToast->width() - 16, 16);
    reqToast->raise();
    reqToast->show();
}

void MainWindow::showSongRequestDialog()
{
    ensureRequestServer();
    QDialog dlg(dialogParent());
    dlg.setWindowTitle(tr("ขอเพลงจากมือถือ"));

    QComboBox *ip = new QComboBox(&dlg);
    ip->addItems(RemoteServer::localAddresses());
    QPushButton *onOff = new QPushButton(&dlg);
    QrWidget *qrC = new QrWidget(&dlg), *qrO = new QrWidget(&dlg);
    QLabel *urlC = new QLabel(&dlg), *urlO = new QLabel(&dlg);
    for (QLabel *l : { urlC, urlO }) {
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setAlignment(Qt::AlignCenter);
        l->setWordWrap(true);
    }
    QCheckBox *autoOk = new QCheckBox(tr("อนุมัติอัตโนมัติ (คำขอเข้าคิวทันที ไม่ต้องรอเจ้าของ)"), &dlg);
    autoOk->setChecked(reqServer->autoApprove());
    QLabel *hint = new QLabel(tr("ลูกค้า: สแกน QR ซ้าย ค้นหาเพลงแล้วกด \"ขอเพลง\"  •  เจ้าของ: สแกน QR ขวา อนุมัติได้จากมือถือ\n"
                                 "คำขอจะขึ้นมุมขวาบนของโปรแกรมด้วย  •  มือถือต้องใช้ Wi-Fi เดียวกับคอมพิวเตอร์"), &dlg);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray;");

    auto refresh = [=]() {
        const bool on = reqServer->isRunning();
        qrC->setText(on ? reqServer->customerUrl() : QString());
        qrO->setText(on ? reqServer->ownerUrl() : QString());
        urlC->setText(on ? reqServer->customerUrl() : QString());
        urlO->setText(on ? reqServer->ownerUrl() : QString());
        onOff->setText(on ? tr("ปิดการขอเพลง") : tr("เปิดการขอเพลงจากมือถือ"));
    };
    connect(onOff, &QPushButton::clicked, &dlg, [=]() {
        if (reqServer->isRunning())
            reqServer->stop();
        else if (ip->currentText().isEmpty() || !reqServer->start(ip->currentText()))
            QMessageBox::warning(dialogParent(), tr("ขอเพลงจากมือถือ"), tr("เปิดไม่สำเร็จ (ไม่พบเครือข่าย Wi-Fi/LAN หรือพอร์ตไม่ว่าง)"));
        refresh();
    });
    connect(ip, QOverload<int>::of(&QComboBox::activated), &dlg, [=](int) {
        if (reqServer->isRunning()) {
            reqServer->start(ip->currentText());
            refresh();
        }
    });
    connect(autoOk, &QCheckBox::toggled, &dlg, [this](bool on) {
        settings->setValue("RequestAutoApprove", on);
        reqServer->setAutoApprove(on);
    });

    QGridLayout *g = new QGridLayout();
    QLabel *lc = new QLabel(tr("QR ลูกค้า (ขอเพลง)"), &dlg), *lo = new QLabel(tr("QR เจ้าของ (อนุมัติ)"), &dlg);
    for (QLabel *l : { lc, lo }) {
        QFont f = l->font();
        f.setBold(true);
        l->setFont(f);
        l->setAlignment(Qt::AlignCenter);
    }
    g->addWidget(lc, 0, 0);
    g->addWidget(lo, 0, 1);
    g->addWidget(qrC, 1, 0, Qt::AlignCenter);
    g->addWidget(qrO, 1, 1, Qt::AlignCenter);
    g->addWidget(urlC, 2, 0);
    g->addWidget(urlO, 2, 1);

    QHBoxLayout *top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("เครือข่าย (IP):"), &dlg));
    top->addWidget(ip, 1);
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    v->addLayout(top);
    v->addWidget(onOff);
    v->addLayout(g);
    v->addWidget(autoOk);
    v->addWidget(hint);
    v->addWidget(bb);
    refresh();
    dlg.exec();
    setFocus();
}

// ---- codes by artist ------------------------------------------------------------------------------

// after songs were added: they get codes in their artist's run (only once codes are in use)
void MainWindow::refreshArtistCodes()
{
    const SongDatabase::CodeStats st = db->artistCodeStats();
    if (st.songs == 0)
        return;
    db->assignArtistCodes(st.first, settings->value("ArtistCodeReserve", 10).toInt(), false);
}

void MainWindow::showArtistCodeDialog()
{
    QDialog dlg(dialogParent());
    dlg.setWindowTitle(tr("จัดรหัสเพลงตามศิลปิน"));
    dlg.setMinimumWidth(520);

    QLabel *intro = new QLabel(tr("ให้รหัส 7 หลักกับทุกเพลง เรียงตามศิลปิน (เพลงของศิลปินเดียวกันได้เลขติดกัน เรียงตามชื่อเพลง) "
                                  "และเว้นเลขท้ายของแต่ละศิลปินไว้ให้เพลงใหม่ของศิลปินคนนั้น\n"
                                  "รหัสเดิมของเพลงยังใช้ได้ตามปกติ ไฟล์เพลงไม่ถูกแก้ไข (ใช้ร่วมกับโปรแกรมอื่นได้)\n"
                                  "จัดซ้ำได้ทุกเมื่อ รหัสที่ให้ไปแล้วจะไม่เปลี่ยน เพลงที่เพิ่มใหม่จะได้รหัสในช่วงของศิลปินของตัวเอง"), &dlg);
    intro->setWordWrap(true);

    QSpinBox *start = new QSpinBox(&dlg);
    start->setRange(1000000, 9000000);
    start->setSingleStep(100000);
    QSpinBox *reserve = new QSpinBox(&dlg);
    reserve->setRange(0, 200);
    reserve->setValue(settings->value("ArtistCodeReserve", 10).toInt());
    reserve->setSuffix(tr(" เลข"));
    QLabel *status = new QLabel(&dlg);
    status->setWordWrap(true);

    auto refresh = [=]() {
        const SongDatabase::CodeStats st = db->artistCodeStats();
        start->setValue(st.songs > 0 ? int(st.first) : 8000000);
        start->setEnabled(st.songs == 0);
        status->setText(st.songs == 0 ? tr("ยังไม่ได้จัดรหัส")
                                      : tr("จัดรหัสแล้ว %1 เพลง %2 ศิลปิน  (รหัส %3 - %4)\nพิมพ์รหัส 7 หลักแล้วกด Enter เพื่อเล่นได้เลย")
                                            .arg(st.songs).arg(st.artists).arg(st.first).arg(st.last));
    };

    auto run = [=](bool fresh) {
        settings->setValue("ArtistCodeReserve", reserve->value());
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const SongDatabase::CodeStats st = db->assignArtistCodes(start->value(), reserve->value(), fresh);
        QApplication::restoreOverrideCursor();
        refresh();
        QMessageBox::information(dialogParent(), tr("จัดรหัสเพลงตามศิลปิน"),
                                 tr("ให้รหัสใหม่ %1 เพลง\nเพลงที่ไม่มีไฟล์แล้ว (คืนรหัส) %2 เพลง\nทั้งหมด %3 เพลง %4 ศิลปิน")
                                     .arg(st.added).arg(st.removed).arg(st.songs).arg(st.artists));
    };

    QPushButton *bAssign = new QPushButton(tr("จัดรหัส / ให้รหัสเพลงใหม่"), &dlg);
    QPushButton *bFresh = new QPushButton(tr("ล้างแล้วจัดใหม่ทั้งหมด..."), &dlg);
    QPushButton *bExport = new QPushButton(tr("ส่งออกสมุดเพลง (Excel / CSV)..."), &dlg);
    connect(bAssign, &QPushButton::clicked, &dlg, [=]() { run(false); });
    connect(bFresh, &QPushButton::clicked, &dlg, [=]() {
        if (QMessageBox::question(dialogParent(), tr("จัดรหัสเพลงตามศิลปิน"),
                                  tr("รหัสตามศิลปินทั้งหมดจะเปลี่ยนใหม่ สมุดเพลงที่พิมพ์ไว้จะใช้ไม่ได้\nต้องการจัดใหม่ทั้งหมดหรือไม่?"))
                == QMessageBox::Yes)
            run(true);
    });
    connect(bExport, &QPushButton::clicked, &dlg, [=]() {
        const QString file = QFileDialog::getSaveFileName(dialogParent(), tr("ส่งออกสมุดเพลง"),
                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + tr("/สมุดเพลง PTHK Karaoke.csv"),
                tr("ไฟล์ CSV (เปิดด้วย Excel) (*.csv)"));
        if (file.isEmpty())
            return;
        if (db->exportSongBook(file))
            QMessageBox::information(dialogParent(), tr("ส่งออกสมุดเพลง"), tr("บันทึกแล้ว:\n") + file);
        else
            QMessageBox::warning(dialogParent(), tr("ส่งออกสมุดเพลง"), tr("บันทึกไม่สำเร็จ"));
    });

    QFormLayout *form = new QFormLayout();
    form->addRow(tr("รหัสเริ่มต้น:"), start);
    form->addRow(tr("เว้นเลขให้เพลงใหม่ ต่อศิลปิน:"), reserve);
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addWidget(bAssign);
    buttons->addWidget(bFresh);
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    v->addWidget(intro);
    v->addLayout(form);
    v->addWidget(status);
    v->addLayout(buttons);
    v->addWidget(bExport);
    v->addWidget(bb);
    refresh();
    dlg.exec();
    setFocus();
}
