#include "VSTDirsDialog.h"
#include "ui_VSTDirsDialog.h"

#include <QSettings>
#include <QDirIterator>
#include <QFileDialog>
#include <QMessageBox>
#include <QMenu>

#include "BASSFX/VSTFX.h"
#include "Dialogs/VSTDialog.h"
#include "Utils.h"
#include "Midi/MidiPlayer.h"
#include "Config.h"


VSTDirsDialog::VSTDirsDialog(QWidget *parent, MainWindow *mainWindow) :
    QDialog(parent),
    ui(new Ui::VSTDirsDialog)
{
    ui->setupUi(this);

    this->mainWindow = mainWindow;
    this->mixDlg = mainWindow->synthMixerDialog();
    this->synth = mainWindow->midiPlayer()->midiSynthesizer();


    // VST dir tab
    {
        QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
        QStringList dirs = st.value("VSTDirs", QStringList()).toStringList();

        QStringList vstDirs;
        vstDirs << QDir::currentPath() + "/VST";
        vstDirs += dirs;

        ui->list->addItems(vstDirs);
        ui->list->setCurrentRow(0);
        if (vstDirs.count() == 1)
            ui->btnRemove->setEnabled(false);
    }


    // VSti Tab
    {
        initVSTiDetail(0, ui->leVSTiPath1, ui->lbVSTiName1, ui->lbVSTiVendor1, ui->btnVSTi1, ui->btnVSTiRemove1);
        initVSTiDetail(1, ui->leVSTiPath2, ui->lbVSTiName2, ui->lbVSTiVendor2, ui->btnVSTi2, ui->btnVSTiRemove2);
        initVSTiDetail(2, ui->leVSTiPath3, ui->lbVSTiName3, ui->lbVSTiVendor3, ui->btnVSTi3, ui->btnVSTiRemove3);
        initVSTiDetail(3, ui->leVSTiPath4, ui->lbVSTiName4, ui->lbVSTiVendor4, ui->btnVSTi4, ui->btnVSTiRemove4);

        if (!synth->isOpened())
        {
            ui->tab_2->setEnabled(false);
        }
    }


    // VSTi used tab
    {
        auto chInstMap = mixDlg->mixChannelMap();

        QStringList vstiNames;
        vstiNames << "Not used"
                  << chInstMap.value(InstrumentType::VSTi1)->fullInstrumentName()
                  << chInstMap.value(InstrumentType::VSTi2)->fullInstrumentName()
                  << chInstMap.value(InstrumentType::VSTi3)->fullInstrumentName()
                  << chInstMap.value(InstrumentType::VSTi4)->fullInstrumentName();

        int i = 0;
        for (InstrumentType t : chInstMap.keys())
        {
            if (t >= InstrumentType::VSTi1)
                break;

            QString vName = vstiNames[synth->useVSTi(t) + 1];

            QTableWidgetItem *nameItem = new QTableWidgetItem(chInstMap.value(t)->fullInstrumentName());
            QTableWidgetItem *vstiItem = new QTableWidgetItem(vName);

            ui->tableVSTiUsed->insertRow(i);
            ui->tableVSTiUsed->setItem(i, 0, nameItem);
            ui->tableVSTiUsed->setItem(i, 1, vstiItem);
            ui->tableVSTiUsed->setItem(i, 2, new QTableWidgetItem(
                synth->extMidiOut(t).isEmpty() ? tr("ใช้เสียงในโปรแกรม") : synth->extMidiOut(t)));
            i++;
        }

        ui->tableVSTiUsed->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        connect(ui->tableVSTiUsed, SIGNAL(customContextMenuRequested(QPoint)),
                this, SLOT(showVSTiSelectMenu(QPoint)));

        connect(ui->tableVSTiUsed, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
            if (auto *item = ui->tableVSTiUsed->item(row, col)) {
                QPoint p = ui->tableVSTiUsed->visualItemRect(item).center();
                showVSTiSelectMenu(p);
            }
        });

        QHBoxLayout *btnLayout = new QHBoxLayout();
        QPushButton *btnAllDrumsVsti1 = new QPushButton(tr("กำหนดชุดกลองทั้งหมด -> VSTi 1"), this);
        btnAllDrumsVsti1->setStyleSheet("padding: 6px 14px; font-weight: bold; background-color: #1976d2; color: white; border-radius: 4px;");
        connect(btnAllDrumsVsti1, &QPushButton::clicked, this, [this]() {
            assignAllDrumsToVSTi(0);
        });

        QPushButton *btnResetAll = new QPushButton(tr("ยกเลิก VSTi ทั้งหมด (ใช้ SoundFont)"), this);
        btnResetAll->setStyleSheet("padding: 6px 14px; border-radius: 4px;");
        connect(btnResetAll, &QPushButton::clicked, this, [this]() {
            assignAllDrumsToVSTi(-1);
        });

        btnLayout->addWidget(btnAllDrumsVsti1);
        btnLayout->addWidget(btnResetAll);
        ui->verticalLayout_7->addLayout(btnLayout);
    }
}

VSTDirsDialog::~VSTDirsDialog()
{
    QStringList vstDirs;
    for (int i=1; i<ui->list->count(); i++) {
        QListWidgetItem *item = ui->list->item(i);
        vstDirs << item->text();
    }

    QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
    st.setValue("VSTDirs", vstDirs);

    delete ui;
}

void VSTDirsDialog::on_btnClose_clicked()
{
    this->close();
}

void VSTDirsDialog::on_btnAdd_clicked()
{
    // Qt's own dialog (not the Windows one): it also lists the files of a folder (greyed), so the user can see that
    // the plug-ins (*.dll / *.vst3) really are in the folder being chosen. The Windows folder dialog shows folders only.
    QFileDialog dlg(this, "เลือกโฟลเดอร์ VST (เลือกโฟลเดอร์ที่มีไฟล์ .dll / .vst3 อยู่ข้างใน)", Utils::LAST_OPEN_DIR);
    dlg.setFileMode(QFileDialog::Directory);
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);
    dlg.setOption(QFileDialog::ShowDirsOnly, false);
    dlg.setOption(QFileDialog::DontResolveSymlinks, true);
    dlg.setNameFilter("VST (*.dll *.DLL *.vst3 *.VST3);;All files (*)");

    if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty())
        return;

    QString dir = dlg.selectedFiles().first();

    if (dir == "")
        return;

    ui->list->addItem(dir);
    ui->btnRemove->setEnabled(true);

    Utils::LAST_OPEN_DIR = dir;
}

void VSTDirsDialog::on_btnRemove_clicked()
{
    if (ui->list->count() == 1)
        return;

    QListWidgetItem *item = ui->list->takeItem(ui->list->currentRow());
    delete item;

    if (ui->list->count() == 1)
        ui->btnRemove->setEnabled(false);
}

void VSTDirsDialog::on_btnUpdate_clicked()
{
    if (QApplication::keyboardModifiers() & Qt::ControlModifier)
        Utils::clearVstSkipList();          // Ctrl+click: scan the plug-ins that were skipped before as well
    Utils::resetVstScanCounters();
    QStringList filesPath;
    QStringList filesName;

    for (int i=0; i<ui->list->count(); i++) {
        QListWidgetItem *item = ui->list->item(i);
        QDirIterator it(item->text(), QStringList() << "*.DLL" << "*.dll" << "*.vst3" << "*.VST3",
                        QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString p = it.next();
            if (p.contains(".vst3/", Qt::CaseInsensitive))
                continue;
            filesPath << p;
            filesName << it.fileName();
        }
    }

    if (filesPath.count() == 0) {
        ui->progressBar->setMaximum(1);
        ui->progressBar->setValue(1);
        synth->setVSTList(QMap<uint, VSTNamePath>());
        mixDlg->setVSTVendorMenu();
        return;
    }

    ui->progressBar->setValue(0);
    ui->progressBar->setMaximum(filesPath.count());

    QMap<uint, VSTNamePath> vstList;

    for (int i=0; i<filesPath.count(); i++) {

        ui->lbVSTName->setText(filesName[i]);
        QApplication::processEvents();

        QList<VSTNamePath> infos;

        if (!Utils::vstInfos(filesPath[i], &infos))
        {
            ui->progressBar->setValue(i+1);
            QApplication::processEvents();
            continue;
        }

        for (const VSTNamePath &info : infos)
            vstList[info.uniqueID] = info;

        ui->progressBar->setValue(i+1);
        QApplication::processEvents();
    }

    ui->lbVSTName->setText(Utils::vstSkippedCount() > 0
        ? QString::fromUtf8("ข้ามปลั๊กอินที่มีปัญหา %1 ตัว (Ctrl+คลิก อัพเดท เพื่อสแกนซ้ำ)").arg(Utils::vstSkippedCount())
        : QString());
    QApplication::processEvents();

    synth->setVSTList(vstList);
    mixDlg->setVSTVendorMenu();
}

void VSTDirsDialog::initVSTiDetail(int vstiIndex, QLineEdit *le, QLabel *lbName, QLabel *lbVendor, QPushButton *btn, QPushButton *btn2)
{
    DWORD vsti = synth->vstiHandle(vstiIndex);
    if (vsti)
    {
        BASS_VST_INFO info = synth->vstiInfo(vstiIndex);
        lbName->setText(info.effectName);
        lbVendor->setText(info.vendorName);
        le->setText(synth->vstiFile(vstiIndex));
        btn->setText("");
        btn->setIcon(QIcon(":/Icons/settings.png"));
        btn2->setEnabled(true);
    }
}

bool VSTDirsDialog::setVSTiFile(int vstiIndex, QLineEdit *le, QLabel *lbName, QLabel *lbVendor, QPushButton *btn, QPushButton *btn2)
{
    #ifdef _WIN32
    QString filter = tr("VST file (*.dll *.DLL *.vst3 *.VST3)");
    #elif
    QString filter = tr("VST file (*.vst *.VST)");
    #endif

    QString file = QFileDialog::getOpenFileName(this, tr("เลือกไฟล์ VST instruments"),
                                                Utils::LAST_OPEN_DIR, filter);

    if (file == "")
        return false;

    Utils::LAST_OPEN_DIR = QFileInfo(file).dir().absolutePath();

    PlayerState state = mainWindow->midiPlayer()->playerState();
    int position = mainWindow->midiPlayer()->positionTick();
    if (synth->isOpened() && mainWindow->midiPlayer()->isPlayerPlaying())
        mainWindow->stop();

#ifdef _WIN32
    if (!Utils::is64BitDll(file))
    {
        QMessageBox::warning(this, tr("ไม่รองรับปลั๊กอิน 32-bit"),
                             tr("ไฟล์ \"%1\" เป็นปลั๊กอินแบบ 32-bit\nโปรแกรม PTHK Karaoke ทำงานแบบ 64-bit ไม่สามารถโหลดปลั๊กอินนี้ได้\nโปรดเลือกไฟล์ที่เป็นเวอร์ชัน 64-bit (x64)").arg(QFileInfo(file).fileName()));
        return false;
    }
#endif

    this->setEnabled(false);
    DWORD vsti = synth->setVSTiFile(vstiIndex, file);
    this->setEnabled(true);

    if (!vsti)
    {
        QMessageBox::information(this, tr("เปิดไฟล์แล้วเหลว"),
                                 tr("ไม่สามารถเปิดไฟล์ VSTi ที่เลือกได้\nโปรดลองอีกครั้ง"));
        return false;
    }

    initVSTiDetail(vstiIndex, le, lbName, lbVendor, btn, btn2);

    QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
    st.beginWriteArray("VSTiGroup");
    st.setArrayIndex(vstiIndex);
    st.setValue("VstiFilePath", file);
    st.endArray();

    if (synth->isOpened())
    {
        switch (state) {
        case PlayerState::Playing:
            mainWindow->play(-1, position);
            break;
        case PlayerState::Paused:
            mainWindow->midiPlayer()->setPositionTick(position);
            mainWindow->lyricsWidget()->setSeekPositionCursor(position);
            if (mainWindow->secondLyrics() != nullptr)
                mainWindow->secondLyrics()->setSeekPositionCursor(position);
            break;
        default:
            break;
        }
    }

    return true;
}

void VSTDirsDialog::showVSTiDialog(DWORD vsti, const QString &instName)
{
    VSTDialog *dlg = new VSTDialog(this, vsti, instName);

    if (!dlg->isCanOpen())
    {
        delete dlg;
        return;
    }

    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void VSTDirsDialog::removeVSTiFile(int vstiIndex, QLineEdit *le, QLabel *lbName, QLabel *lbVendor, QPushButton *btn, QPushButton *btn2)
{
    DWORD vstiHandle = synth->vstiHandle(vstiIndex);

    PlayerState state = mainWindow->midiPlayer()->playerState();
    int position = mainWindow->midiPlayer()->positionTick();

    if (synth->isOpened() && (vstiHandle != 0) && mainWindow->midiPlayer()->isPlayerPlaying())
        mainWindow->stop();

    synth->removeVSTiFile(vstiIndex);

    le->setText("");
    lbName->setText("");
    lbVendor->setText("");
    btn->setText("...");
    btn->setIcon(QIcon());
    btn2->setEnabled(false);

    QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
    st.beginWriteArray("VSTiGroup");
    st.setArrayIndex(vstiIndex);
    st.setValue("VstiFilePath", "");
    st.endArray();

    if (synth->isOpened() && (vstiHandle != 0))
    {
        switch (state) {
        case PlayerState::Playing:
            mainWindow->play(-1, position);
            break;
        case PlayerState::Paused:
            mainWindow->midiPlayer()->setPositionTick(position);
            mainWindow->lyricsWidget()->setSeekPositionCursor(position);
            if (mainWindow->secondLyrics() != nullptr)
                mainWindow->secondLyrics()->setSeekPositionCursor(position);
            break;
        default:
            break;
        }
    }
}

void VSTDirsDialog::on_btnVSTi1_clicked()
{
    DWORD vsti = synth->vstiHandle(0);

    if (vsti == 0)
        setVSTiFile(0, ui->leVSTiPath1, ui->lbVSTiName1, ui->lbVSTiVendor1, ui->btnVSTi1, ui->btnVSTiRemove1);
    else
        showVSTiDialog(vsti, mixDlg->mixChannel(InstrumentType::VSTi1)->fullInstrumentName());
}

void VSTDirsDialog::on_btnVSTi2_clicked()
{
    DWORD vsti = synth->vstiHandle(1);

    if (vsti == 0)
        setVSTiFile(1, ui->leVSTiPath2, ui->lbVSTiName2, ui->lbVSTiVendor2, ui->btnVSTi2, ui->btnVSTiRemove2);
    else
        showVSTiDialog(vsti, mixDlg->mixChannel(InstrumentType::VSTi2)->fullInstrumentName());
}

void VSTDirsDialog::on_btnVSTi3_clicked()
{
    DWORD vsti = synth->vstiHandle(2);

    if (vsti == 0)
        setVSTiFile(2, ui->leVSTiPath3, ui->lbVSTiName3, ui->lbVSTiVendor3, ui->btnVSTi3, ui->btnVSTiRemove3);
    else
        showVSTiDialog(vsti, mixDlg->mixChannel(InstrumentType::VSTi3)->fullInstrumentName());
}

void VSTDirsDialog::on_btnVSTi4_clicked()
{
    DWORD vsti = synth->vstiHandle(3);

    if (vsti == 0)
        setVSTiFile(3, ui->leVSTiPath4, ui->lbVSTiName4, ui->lbVSTiVendor4, ui->btnVSTi4, ui->btnVSTiRemove4);
    else
        showVSTiDialog(vsti, mixDlg->mixChannel(InstrumentType::VSTi4)->fullInstrumentName());
}

void VSTDirsDialog::on_btnVSTiRemove1_clicked()
{
    removeVSTiFile(0, ui->leVSTiPath1, ui->lbVSTiName1, ui->lbVSTiVendor1, ui->btnVSTi1, ui->btnVSTiRemove1);
}

void VSTDirsDialog::on_btnVSTiRemove2_clicked()
{
    removeVSTiFile(1, ui->leVSTiPath2, ui->lbVSTiName2, ui->lbVSTiVendor2, ui->btnVSTi2, ui->btnVSTiRemove2);
}

void VSTDirsDialog::on_btnVSTiRemove3_clicked()
{
    removeVSTiFile(2, ui->leVSTiPath3, ui->lbVSTiName3, ui->lbVSTiVendor3, ui->btnVSTi3, ui->btnVSTiRemove3);
}

void VSTDirsDialog::on_btnVSTiRemove4_clicked()
{
    removeVSTiFile(3, ui->leVSTiPath4, ui->lbVSTiName4, ui->lbVSTiVendor4, ui->btnVSTi4, ui->btnVSTiRemove4);
}

void VSTDirsDialog::showVSTiSelectMenu(const QPoint &pos)
{
    auto chInstMap = mixDlg->mixChannelMap();

    QStringList vstiNames;
    vstiNames << "Not used"
              << chInstMap.value(InstrumentType::VSTi1)->fullInstrumentName()
              << chInstMap.value(InstrumentType::VSTi2)->fullInstrumentName()
              << chInstMap.value(InstrumentType::VSTi3)->fullInstrumentName()
              << chInstMap.value(InstrumentType::VSTi4)->fullInstrumentName();

    QMenu menu(ui->tableVSTiUsed);
    vstiSignalMapper = new QSignalMapper();

    for (int i=-1; i<4; i++)
    {
        QAction *act = menu.addAction(vstiNames[i+1]);
        connect(act, SIGNAL(triggered()), vstiSignalMapper, SLOT(map()));
        vstiSignalMapper->setMapping(act, i);
    }

    connect(vstiSignalMapper, SIGNAL(mapped(int)), this, SLOT(setVSTiUsed(int)));

    // External MIDI out for the selected instrument(s)
    menu.addSeparator();
    QMenu *midiMenu = menu.addMenu(tr("MIDI Out ภายนอก"));
    QStringList outs = MidiPlayer::midiDevices();
    QAction *none = midiMenu->addAction(tr("ไม่ใช้ (ใช้เสียงในโปรแกรม)"));
    connect(none, &QAction::triggered, this, [this]() { setExtMidiOutSelected(QString()); });
    midiMenu->addSeparator();
    for (const QString &name : outs) {
        QAction *a = midiMenu->addAction(name);
        connect(a, &QAction::triggered, this, [this, name]() { setExtMidiOutSelected(name); });
    }

    menu.exec(ui->tableVSTiUsed->mapToGlobal(pos));

    delete vstiSignalMapper;
}

void VSTDirsDialog::setVSTiUsed(int selected)
{
    auto chInstMap = mixDlg->mixChannelMap();

    QStringList vstiNames;
    vstiNames << "Not used"
              << chInstMap.value(InstrumentType::VSTi1)->fullInstrumentName()
              << chInstMap.value(InstrumentType::VSTi2)->fullInstrumentName()
              << chInstMap.value(InstrumentType::VSTi3)->fullInstrumentName()
              << chInstMap.value(InstrumentType::VSTi4)->fullInstrumentName();

    PlayerState state = mainWindow->midiPlayer()->playerState();
    int position = mainWindow->midiPlayer()->positionTick();

    if (synth->isOpened() && mainWindow->midiPlayer()->isPlayerPlaying())
        mainWindow->stop();

    QModelIndexList indexList = selectedVstiRows();
    for (QModelIndex index : indexList)
    {
        QTableWidgetItem *item = ui->tableVSTiUsed->item(index.row(), 1);
        item->setText(vstiNames[selected+1]);

        InstrumentType type = static_cast<InstrumentType>(index.row());
        synth->setUseVSTi(type, selected);

        // "Not used" also cancels the external MIDI out of that instrument
        if (selected == -1) {
            synth->setExtMidiOut(type, QString());
            QTableWidgetItem *xi = ui->tableVSTiUsed->item(index.row(), 2);
            if (xi)
                xi->setText(tr("ใช้เสียงในโปรแกรม"));
        }
    }

    if (synth->isOpened())
    {
        switch (state) {
        case PlayerState::Playing:
            mainWindow->play(-1, position);
            break;
        case PlayerState::Paused:
            mainWindow->midiPlayer()->setPositionTick(position);
            mainWindow->lyricsWidget()->setSeekPositionCursor(position);
            if (mainWindow->secondLyrics() != nullptr)
                mainWindow->secondLyrics()->setSeekPositionCursor(position);
            break;
        default:
            break;
        }
    }
}


void VSTDirsDialog::setExtMidiOutSelected(const QString &deviceName)
{
    // stop first: changing the routing while notes are sounding leaves hanging notes
    if (synth->isOpened() && mainWindow->midiPlayer()->isPlayerPlaying())
        mainWindow->stop();

    QModelIndexList indexList = selectedVstiRows();
    for (QModelIndex index : indexList)
    {
        InstrumentType type = static_cast<InstrumentType>(index.row());
        synth->setExtMidiOut(type, deviceName);

        // show what is really active (a busy/missing device falls back to internal)
        QString now = synth->extMidiOut(type);
        QTableWidgetItem *item = ui->tableVSTiUsed->item(index.row(), 2);
        if (item)
            item->setText(now.isEmpty() ? tr("ใช้เสียงในโปรแกรม") : now);
    }
    synth->sendAllNotesOff();
}


QModelIndexList VSTDirsDialog::selectedVstiRows()
{
    QModelIndexList list = ui->tableVSTiUsed->selectionModel()->selectedRows();
    if (list.isEmpty() && ui->tableVSTiUsed->currentRow() >= 0)
        list << ui->tableVSTiUsed->model()->index(ui->tableVSTiUsed->currentRow(), 0);
    return list;
}

void VSTDirsDialog::assignAllDrumsToVSTi(int vstiIndex)
{
    auto chInstMap = mixDlg->mixChannelMap();
    QStringList vstiNames;
    vstiNames << tr("Not used")
              << chInstMap.value(InstrumentType::VSTi1)->fullInstrumentName()
              << chInstMap.value(InstrumentType::VSTi2)->fullInstrumentName()
              << chInstMap.value(InstrumentType::VSTi3)->fullInstrumentName()
              << chInstMap.value(InstrumentType::VSTi4)->fullInstrumentName();

    QString assignedName = vstiNames[vstiIndex + 1];

    for (int row = 0; row < ui->tableVSTiUsed->rowCount(); row++) {
        InstrumentType type = static_cast<InstrumentType>(row);
        if (type >= InstrumentType::BassDrum && type <= InstrumentType::PercussionEtc) {
            synth->setUseVSTi(type, vstiIndex);
            if (QTableWidgetItem *item = ui->tableVSTiUsed->item(row, 1))
                item->setText(assignedName);
            if (vstiIndex == -1) {
                synth->setExtMidiOut(type, QString());
                if (QTableWidgetItem *xi = ui->tableVSTiUsed->item(row, 2))
                    xi->setText(tr("ใช้เสียงในโปรแกรม"));
            }
        }
    }
    synth->sendAllNotesOff();
    QMessageBox::information(this, tr("ตั้งค่าสำเร็จ"),
        vstiIndex >= 0
            ? tr("กำหนดให้ชิ้นส่วนกลองทั้งหมดส่งเสียงไปยัง %1 เรียบร้อยแล้ว").arg(assignedName)
            : tr("ยกเลิก VSTi และกลับไปใช้เสียง SoundFont ปกติเรียบร้อยแล้ว"));
}
