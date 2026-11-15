// MultiSoundfontDialog.h: "มัลติซาวด์ฟอนต์" - every instrument from any soundfont.
//
//  - the soundfont files (the first is the main one: every instrument the table leaves alone)
//  - per mixer preset (000 ทั่วไป, 101 ลูกทุ่ง ...): for each of the 128 instruments the soundfont,
//    the sound in it (any bank / program of the file, or the program itself) and its level (0..200 %);
//    for the 16 drum strips the soundfont
//  - "ลองฟัง": a few notes of the chosen sound, on a MIDI stream of its own in the main mixer
//  - "ส่งออกชุดเสียง" / "นำเข้าชุดเสียง": a folder with <name>.pthksf (JSON) and the soundfont files,
//    to move the whole set to another computer
//
// Settings (PTHKKaraoke.conf): SynthSoundfonts, SynthSoundfontsVolume, and for preset n ("" for 0):
// SynthSoundfontsMap n, SynthSoundfontsDrumMap n, SynthSoundfontsSrc n, SynthSoundfontsVol n.
// The engine: MidiSynthesizer::setMapSoundfontIndex / setMapSoundExtras.

#ifndef MULTISOUNDFONTDIALOG_H
#define MULTISOUNDFONTDIALOG_H

#include "Midi/MidiSynthesizer.h"
#include "Midi/MidiHelper.h"
#include "Config.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

class MultiSoundfontDialog : public QDialog
{
public:
    // after the soundfont list changed (the mixer's strip menus list the files)
    std::function<void()> onSoundfontsChanged;

    MultiSoundfontDialog(QWidget *parent, MidiSynthesizer *s)
        : QDialog(parent), syn(s)
    {
        setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
        setWindowTitle(tr("มัลติซาวด์ฟอนต์ - เลือกเสียงเครื่องดนตรีจากหลายซาวด์ฟอนต์"));
        resize(1320, 780);

        QVBoxLayout *root = new QVBoxLayout(this);
        root->setContentsMargins(12, 10, 12, 10);
        root->setSpacing(8);

        // ---- top: mixer preset + export / import
        QHBoxLayout *top = new QHBoxLayout();
        top->addWidget(new QLabel(tr("ชุดเสียงของมิกเซอร์:"), this));
        cbPreset = new QComboBox(this);
        for (int i = 0; i < SF_PRESET_COUNT; i++)
            cbPreset->addItem(QString("%1  %2").arg(sfPresetBankNumber(i), 3, 10, QChar('0')).arg(presetName(i)));
        cbPreset->setCurrentIndex(qBound(0, syn->soundfontPresets(), SF_PRESET_COUNT - 1));
        cbPreset->setMinimumWidth(170);
        top->addWidget(cbPreset);
        QLabel *presetNote = new QLabel(tr("(แต่ละชุดของปุ่มชุดเสียงในมิกเซอร์ จัดเสียงแยกกันได้)"), this);
        presetNote->setStyleSheet("color: #9ca3af;");
        top->addWidget(presetNote);
        top->addStretch(1);
        QPushButton *btnImport = new QPushButton(tr("นำเข้าชุดเสียง..."), this);
        QPushButton *btnExport = new QPushButton(tr("ส่งออกชุดเสียง..."), this);
        btnImport->setToolTip(tr("เปิดชุดเสียงที่ส่งออกไว้ (ไฟล์ .pthksf พร้อมไฟล์ซาวด์ฟอนต์ในโฟลเดอร์เดียวกัน)\n"
                                 "จะแทนรายการซาวด์ฟอนต์และการจัดเสียงทั้งหมดในเครื่องนี้"));
        btnExport->setToolTip(tr("บันทึกชุดเสียงทั้งหมด (ไฟล์ซาวด์ฟอนต์ + การจัดเสียงทุกชุด) ลงโฟลเดอร์ใหม่\n"
                                 "เอาไปนำเข้าที่เครื่องอื่นได้ทันที"));
        top->addWidget(btnImport);
        top->addWidget(btnExport);
        root->addLayout(top);

        QSplitter *split = new QSplitter(Qt::Horizontal, this);
        root->addWidget(split, 1);

        // ---- left: soundfont files
        QGroupBox *gFiles = new QGroupBox(tr("ไฟล์ซาวด์ฟอนต์"), split);
        QVBoxLayout *vf = new QVBoxLayout(gFiles);
        listFiles = new QListWidget(gFiles);
        listFiles->setToolTip(tr("ไฟล์แรกคือไฟล์หลัก: เครื่องดนตรีที่ไม่ได้จัดไว้ในตาราง ใช้เสียงจากไฟล์นี้"));
        vf->addWidget(listFiles, 1);
        QHBoxLayout *fb = new QHBoxLayout();
        QPushButton *bAdd = new QPushButton(tr("เพิ่ม..."), gFiles);
        QPushButton *bRemove = new QPushButton(tr("ลบ"), gFiles);
        QPushButton *bUp = new QPushButton(tr("▲"), gFiles);
        QPushButton *bDown = new QPushButton(tr("▼"), gFiles);
        bUp->setToolTip(tr("ย้ายขึ้น (ไฟล์บนสุดคือไฟล์หลัก)"));
        bDown->setToolTip(tr("ย้ายลง"));
        bUp->setFixedWidth(34);
        bDown->setFixedWidth(34);
        fb->addWidget(bAdd);
        fb->addWidget(bRemove);
        fb->addStretch(1);
        fb->addWidget(bUp);
        fb->addWidget(bDown);
        vf->addLayout(fb);
        QHBoxLayout *fvRow = new QHBoxLayout();
        fvRow->addWidget(new QLabel(tr("ความดังทั้งไฟล์:"), gFiles));
        slFileVol = new QSlider(Qt::Horizontal, gFiles);
        slFileVol->setRange(0, 200);
        lbFileVol = new QLabel("100%", gFiles);
        lbFileVol->setMinimumWidth(44);
        fvRow->addWidget(slFileVol, 1);
        fvRow->addWidget(lbFileVol);
        vf->addLayout(fvRow);

        // ---- middle: instruments / drums
        tabs = new QTabWidget(split);
        treeInst = makeTree({ tr("เครื่องดนตรี"), tr("ซาวด์ฟอนต์"), tr("เสียงที่ใช้"), tr("ความดัง") });
        treeDrum = makeTree({ tr("ชิ้นกลอง"), tr("ซาวด์ฟอนต์") });
        tabs->addTab(treeInst, tr("เครื่องดนตรี 128 ชิ้น"));
        tabs->addTab(treeDrum, tr("กลอง"));
        const QStringList groups = MidiHelper::instrumentGroupNames();
        const QStringList names = MidiHelper::GMInstrumentNumberNames();
        for (int g = 0; g < 16; g++) {
            QTreeWidgetItem *gi = new QTreeWidgetItem(treeInst, { groups.value(g) });
            QFont f = gi->font(0);
            f.setBold(true);
            gi->setFont(0, f);
            gi->setData(0, Qt::UserRole, -1);
            for (int k = 0; k < 8; k++) {
                const int prog = g * 8 + k;
                QTreeWidgetItem *it = new QTreeWidgetItem(gi, { names.value(prog) });
                it->setData(0, Qt::UserRole, prog);
                instItems[prog] = it;
            }
        }
        for (int d = 0; d < 16; d++) {
            QTreeWidgetItem *it = new QTreeWidgetItem(treeDrum, { drumNames().value(d) });
            it->setData(0, Qt::UserRole, d);
            drumItems[d] = it;
        }

        // ---- right: editor of the selected rows
        QGroupBox *gEdit = new QGroupBox(tr("แก้รายการที่เลือก"), split);
        QVBoxLayout *ve = new QVBoxLayout(gEdit);
        lbSel = new QLabel(gEdit);
        lbSel->setWordWrap(true);
        lbSel->setStyleSheet("font-weight: 600;");
        ve->addWidget(lbSel);
        ve->addWidget(new QLabel(tr("ซาวด์ฟอนต์:"), gEdit));
        cbFile = new QComboBox(gEdit);
        ve->addWidget(cbFile);
        lbSound = new QLabel(tr("เสียงที่ใช้ (เสียงใดก็ได้ในไฟล์):"), gEdit);
        ve->addWidget(lbSound);
        edFind = new QLineEdit(gEdit);
        edFind->setPlaceholderText(tr("ค้นหาชื่อเสียง เช่น piano, sax, 008:"));
        edFind->setClearButtonEnabled(true);
        ve->addWidget(edFind);
        listSound = new QListWidget(gEdit);
        ve->addWidget(listSound, 1);
        lbLevelTitle = new QLabel(tr("ความดังของเครื่องดนตรี:"), gEdit);
        ve->addWidget(lbLevelTitle);
        QHBoxLayout *lv = new QHBoxLayout();
        slLevel = new QSlider(Qt::Horizontal, gEdit);
        slLevel->setRange(0, 200);
        slLevel->setValue(100);
        lbLevel = new QLabel("100%", gEdit);
        lbLevel->setMinimumWidth(44);
        lv->addWidget(slLevel, 1);
        lv->addWidget(lbLevel);
        ve->addLayout(lv);
        QHBoxLayout *eb = new QHBoxLayout();
        btnPreview = new QPushButton(tr("▶ ลองฟัง"), gEdit);
        btnPreview->setToolTip(tr("เล่นเสียงที่เลือกสั้น ๆ (ออกลำโพงเหมือนเพลง)"));
        QPushButton *btnReset = new QPushButton(tr("คืนค่าเดิม"), gEdit);
        btnReset->setToolTip(tr("รายการที่เลือกกลับไปใช้ไฟล์หลัก เสียงเดิม ความดัง 100%"));
        eb->addWidget(btnPreview);
        eb->addWidget(btnReset);
        ve->addLayout(eb);

        split->addWidget(gFiles);
        split->addWidget(tabs);
        split->addWidget(gEdit);
        split->setStretchFactor(0, 2);
        split->setStretchFactor(1, 5);
        split->setStretchFactor(2, 3);
        split->setSizes({ 280, 600, 340 });

        QLabel *hint = new QLabel(tr("เลือกได้หลายแถว (Ctrl / Shift) หรือคลิกชื่อกลุ่มเพื่อเลือกทั้งกลุ่ม  •  "
                                     "เปลี่ยนแล้วมีผลทันที แม้กำลังเล่นเพลงอยู่  •  ความดังของกลองปรับที่มิกเซอร์"), this);
        hint->setStyleSheet("color: #9ca3af;");
        hint->setWordWrap(true);
        root->addWidget(hint);
        QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
        bb->button(QDialogButtonBox::Close)->setText(tr("ปิด"));
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
        root->addWidget(bb);

        applyTimer.setSingleShot(true);
        applyTimer.setInterval(250);
        connect(&applyTimer, &QTimer::timeout, this, [this]() { applyNow(); });

        // ---- signals
        connect(cbPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            applyNow();
            loadPreset();
            refreshAll();
        });
        connect(listFiles, &QListWidget::currentRowChanged, this, [this](int r) {
            QSignalBlocker b(slFileVol);
            const int v = r >= 0 ? qRound(syn->soundfontVolume(r) * 100) : 100;
            slFileVol->setValue(v);
            lbFileVol->setText(QString::number(v) + "%");
        });
        connect(slFileVol, &QSlider::valueChanged, this, [this](int v) {
            lbFileVol->setText(QString::number(v) + "%");
            const int r = listFiles->currentRow();
            if (r < 0)
                return;
            syn->setSoundfontVolume(r, v / 100.0f);
            saveFileVolumes();
        });
        connect(bAdd, &QPushButton::clicked, this, [this]() { addFiles(); });
        connect(bRemove, &QPushButton::clicked, this, [this]() { removeFile(); });
        connect(bUp, &QPushButton::clicked, this, [this]() { moveFile(-1); });
        connect(bDown, &QPushButton::clicked, this, [this]() { moveFile(+1); });
        connect(btnExport, &QPushButton::clicked, this, [this]() { exportSet(); });
        connect(btnImport, &QPushButton::clicked, this, [this]() { importSet(); });

        connect(tabs, &QTabWidget::currentChanged, this, [this](int) { selectionChanged(); });
        connect(treeInst, &QTreeWidget::itemSelectionChanged, this, [this]() { selectionChanged(); });
        connect(treeDrum, &QTreeWidget::itemSelectionChanged, this, [this]() { selectionChanged(); });
        connect(cbFile, QOverload<int>::of(&QComboBox::activated), this, [this](int idx) { setFileForSelection(idx); });
        connect(edFind, &QLineEdit::textChanged, this, [this]() { filterSounds(); });
        connect(listSound, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) { setSoundForSelection(it); });
        connect(listSound, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
            setSoundForSelection(it);
            preview();
        });
        connect(slLevel, &QSlider::valueChanged, this, [this](int v) {
            lbLevel->setText(QString::number(v) + "%");
            if (!loadingEditor)
                setLevelForSelection(v);
        });
        connect(btnPreview, &QPushButton::clicked, this, [this]() { preview(); });
        connect(btnReset, &QPushButton::clicked, this, [this]() { resetSelection(); });

        loadPreset();
        refreshFiles();
        refreshAll();
        if (QTreeWidgetItem *first = treeInst->topLevelItem(0)) {
            first->setExpanded(true);
            treeInst->setCurrentItem(first->child(0));
        }
    }

    ~MultiSoundfontDialog() override
    {
        applyNow();
        stopPreview();
        if (previewStream)
            BASS_StreamFree(previewStream);
    }

    static QString presetName(int i)
    {
        static const char *n[] = { "ทั่วไป", "ลูกทุ่ง", "ลูกกรุง", "เพื่อชีวิต", "60's", "สตริง",
                                   "ร็อค", "รีมิกซ์", "ลำซิ่ง", "แจ๊ส", "ดนตรีสด" };
        return (i >= 0 && i < 11) ? QString::fromUtf8(n[i]) : QString::number(i);
    }
    static QStringList drumNames()
    {
        return { "Kick (Bass Drum)", "Snare", "Side Stick / Rimshot", "High Tom", "Mid Tom", "Low Tom",
                 "Hi-hat", "Cowbell", "Crash Cymbal", "Ride Cymbal", "Bongo", "Conga", "Timbale",
                 QString::fromUtf8("ฉิ่ง / Triangle"), QString::fromUtf8("ฉาบ"), "Percussion Etc" };
    }

private:
    MidiSynthesizer *syn;
    QComboBox *cbPreset = nullptr;
    QListWidget *listFiles = nullptr;
    QSlider *slFileVol = nullptr;
    QLabel *lbFileVol = nullptr;
    QTabWidget *tabs = nullptr;
    QTreeWidget *treeInst = nullptr, *treeDrum = nullptr;
    QTreeWidgetItem *instItems[128] = {};
    QTreeWidgetItem *drumItems[16] = {};
    QLabel *lbSel = nullptr, *lbSound = nullptr, *lbLevelTitle = nullptr, *lbLevel = nullptr;
    QComboBox *cbFile = nullptr;
    QLineEdit *edFind = nullptr;
    QListWidget *listSound = nullptr;
    QSlider *slLevel = nullptr;
    QPushButton *btnPreview = nullptr;
    bool loadingEditor = false;

    // the preset being edited (written to the engine by applyNow, a moment after the last change)
    int preset = 0;
    QList<int> mapSf, mapSrc, mapVol, mapDrum;
    bool dirty = false;
    QTimer applyTimer;

    HSTREAM previewStream = 0;
    QList<QTimer *> previewTimers;

    QTreeWidget *makeTree(const QStringList &headers)
    {
        QTreeWidget *t = new QTreeWidget(this);
        t->setHeaderLabels(headers);
        t->setSelectionMode(QAbstractItemView::ExtendedSelection);
        t->setRootIsDecorated(true);
        t->setUniformRowHeights(true);
        t->setAlternatingRowColors(true);
        // every column fits the panel: the sound name takes what is left (no sideways scrolling)
        t->header()->setStretchLastSection(false);
        t->header()->setSectionResizeMode(QHeaderView::Interactive);
        t->setColumnWidth(0, 200);
        t->setColumnWidth(1, 165);
        if (headers.size() > 2) {
            t->header()->setSectionResizeMode(2, QHeaderView::Stretch);
            t->header()->setSectionResizeMode(3, QHeaderView::Fixed);
            t->setColumnWidth(3, 62);
        } else {
            t->header()->setSectionResizeMode(1, QHeaderView::Stretch);
        }
        return t;
    }

    bool drumsTab() const { return tabs->currentWidget() == treeDrum; }

    QString fileName(int sf) const
    {
        const QStringList f = syn->soundfontFiles();
        if (sf < 0 || sf >= f.size())
            return tr("(ไม่มีไฟล์)");
        return QFileInfo(f.at(sf)).completeBaseName();
    }
    QString soundName(int sf, int prog, int src) const
    {
        const HSOUNDFONT f = syn->soundfontHandle(sf);
        if (src < 0) {
            const char *n = f ? BASS_MIDI_FontGetPreset(f, prog, 0) : nullptr;
            return n ? tr("ตามเครื่องดนตรี (%1)").arg(QString::fromLocal8Bit(n).trimmed())
                     : tr("ตามเครื่องดนตรี");
        }
        const char *n = f ? BASS_MIDI_FontGetPreset(f, src % 128, src / 128) : nullptr;
        return QString("%1:%2 %3").arg(src / 128, 3, 10, QChar('0')).arg(src % 128, 3, 10, QChar('0'))
            .arg(n ? QString::fromLocal8Bit(n).trimmed() : tr("(ไม่มีในไฟล์นี้)"));
    }

    // ---- settings ---------------------------------------------------------------------------------
    static QString suffix(int p) { return p > 0 ? QString::number(p) : QString(); }

    void saveMaps(int p)
    {
        QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        st.setValue("SynthSoundfontsMap" + suffix(p), QVariant::fromValue(syn->getMapSoundfontIndex(p)));
        st.setValue("SynthSoundfontsDrumMap" + suffix(p), QVariant::fromValue(syn->getDrumMapSfIndex(p)));
        st.setValue("SynthSoundfontsSrc" + suffix(p), QVariant::fromValue(syn->getMapSoundSource(p)));
        st.setValue("SynthSoundfontsVol" + suffix(p), QVariant::fromValue(syn->getMapSoundVolume(p)));
    }
    void saveFileVolumes()
    {
        QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
        const QStringList files = syn->soundfontFiles();
        st.beginWriteArray("SynthSoundfontsVolume");
        for (int i = 0; i < files.count(); i++) {
            st.setArrayIndex(i);
            st.setValue("SoundfontVolume", qRound(syn->soundfontVolume(i) * 100));
        }
        st.endArray();
    }
    void saveFilesAndAllMaps()
    {
        {
            QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
            st.setValue("SynthSoundfonts", QVariant::fromValue(syn->soundfontFiles()));
        }
        saveFileVolumes();
        for (int p = 0; p < SF_PRESET_COUNT; p++)
            saveMaps(p);
    }

    // ---- the preset being edited ------------------------------------------------------------------
    void loadPreset()
    {
        preset = cbPreset->currentIndex();
        mapSf = syn->getMapSoundfontIndex(preset);
        mapSrc = syn->getMapSoundSource(preset);
        mapVol = syn->getMapSoundVolume(preset);
        mapDrum = syn->getDrumMapSfIndex(preset);
        while (mapSf.size() < 128) mapSf.append(0);
        while (mapSrc.size() < 128) mapSrc.append(-1);
        while (mapVol.size() < 128) mapVol.append(100);
        while (mapDrum.size() < 16) mapDrum.append(0);
        dirty = false;
    }
    void changed()
    {
        dirty = true;
        applyTimer.start();
    }
    void applyNow()
    {
        applyTimer.stop();
        if (!dirty)
            return;
        dirty = false;
        // the extras first (no rebuild yet when the file map then changes: one rebuild each)
        syn->setMapSoundExtras(preset, mapSrc, mapVol);
        syn->setMapSoundfontIndex(preset, mapSf, mapDrum);
        saveMaps(preset);
    }

    // ---- file list --------------------------------------------------------------------------------
    void refreshFiles()
    {
        const QStringList files = syn->soundfontFiles();
        const int keep = listFiles->currentRow();
        listFiles->clear();
        for (int i = 0; i < files.size(); i++) {
            QListWidgetItem *it = new QListWidgetItem(
                (i == 0 ? tr("★ ") : QString("%1. ").arg(i + 1)) + QFileInfo(files.at(i)).fileName(), listFiles);
            it->setToolTip(files.at(i) + (i == 0 ? tr("\nไฟล์หลัก") : QString()));
        }
        listFiles->setCurrentRow(qBound(-1, keep < 0 ? 0 : keep, files.size() - 1));
        QSignalBlocker b(cbFile);
        cbFile->clear();
        for (int i = 0; i < files.size(); i++)
            cbFile->addItem((i == 0 ? tr("★ ") : QString()) + QFileInfo(files.at(i)).completeBaseName(), i);
    }
    void filesChanged()
    {
        loadPreset();
        saveFilesAndAllMaps();
        refreshFiles();
        refreshAll();
        if (onSoundfontsChanged)
            onSoundfontsChanged();
    }
    void addFiles()
    {
        applyNow();
        const QStringList add = QFileDialog::getOpenFileNames(this, tr("เพิ่มซาวด์ฟอนต์"), QString(),
                                                              tr("ซาวด์ฟอนต์ (*.sf2 *.SF2 *.sf3 *.sfz *.kmsf)"));
        QStringList failed;
        for (const QString &f : add) {
            const QString path = QDir::fromNativeSeparators(f);
            if (syn->soundfontFiles().contains(path))
                continue;
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const bool ok = syn->addSoundfont(path);
            QApplication::restoreOverrideCursor();
            if (ok)
                syn->setSoundfontVolume(syn->soundfontFiles().size() - 1, 1.0f);
            else
                failed << QFileInfo(path).fileName();
        }
        if (!failed.isEmpty())
            QMessageBox::warning(this, tr("เพิ่มซาวด์ฟอนต์"), tr("เปิดไฟล์นี้ไม่ได้:\n") + failed.join('\n'));
        if (!add.isEmpty()) {
            syn->setSoundfontPresets(syn->soundfontPresets());
            filesChanged();
        }
    }
    void removeFile()
    {
        applyNow();
        const int r = listFiles->currentRow();
        if (r < 0)
            return;
        if (QMessageBox::question(this, tr("ลบซาวด์ฟอนต์"),
                tr("เอา \"%1\" ออกจากรายการ?\nเครื่องดนตรีที่ใช้ไฟล์นี้จะกลับไปใช้ไฟล์หลัก (ไฟล์บนดิสก์ไม่ถูกลบ)")
                    .arg(QFileInfo(syn->soundfontFiles().value(r)).fileName())) != QMessageBox::Yes)
            return;
        syn->removeSoundfont(r);
        filesChanged();
    }
    void moveFile(int dir)
    {
        applyNow();
        const int r = listFiles->currentRow();
        const int to = r + dir;
        if (r < 0 || to < 0 || to >= syn->soundfontFiles().size())
            return;
        syn->swapSoundfont(r, to);
        filesChanged();
        listFiles->setCurrentRow(to);
    }

    // ---- table ------------------------------------------------------------------------------------
    void refreshAll()
    {
        for (int prog = 0; prog < 128; prog++) {
            QTreeWidgetItem *it = instItems[prog];
            const bool custom = mapSf[prog] != 0 || mapSrc[prog] >= 0 || mapVol[prog] != 100;
            it->setText(1, mapSf[prog] == 0 ? tr("★ %1").arg(fileName(0)) : fileName(mapSf[prog]));
            it->setText(2, soundName(mapSf[prog], prog, mapSrc[prog]));
            it->setText(3, QString::number(mapVol[prog]) + "%");
            QFont f = it->font(0);
            f.setBold(custom);
            for (int c = 0; c < 4; c++)
                it->setFont(c, f);
            it->setForeground(3, mapVol[prog] != 100 ? QBrush(QColor("#f59e0b")) : QBrush());
        }
        for (int g = 0; g < treeInst->topLevelItemCount(); g++) {
            QTreeWidgetItem *gi = treeInst->topLevelItem(g);
            int changedCount = 0;
            for (int k = 0; k < gi->childCount(); k++) {
                const int prog = gi->child(k)->data(0, Qt::UserRole).toInt();
                if (mapSf[prog] != 0 || mapSrc[prog] >= 0 || mapVol[prog] != 100)
                    ++changedCount;
            }
            gi->setText(1, changedCount ? tr("จัดเสียงเอง %1 ชิ้น").arg(changedCount) : tr("ไฟล์หลัก"));
        }
        for (int d = 0; d < 16; d++)
            drumItems[d]->setText(1, mapDrum[d] == 0 ? tr("★ %1").arg(fileName(0)) : fileName(mapDrum[d]));
        selectionChanged();
    }

    // the programs (or drum strips) of the selection; a group row = its 8 instruments
    QList<int> selection() const
    {
        QList<int> out;
        QTreeWidget *t = drumsTab() ? treeDrum : treeInst;
        for (QTreeWidgetItem *it : t->selectedItems()) {
            const int v = it->data(0, Qt::UserRole).toInt();
            if (v >= 0) {
                if (!out.contains(v))
                    out << v;
            } else {
                for (int k = 0; k < it->childCount(); k++) {
                    const int p = it->child(k)->data(0, Qt::UserRole).toInt();
                    if (!out.contains(p))
                        out << p;
                }
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    void selectionChanged()
    {
        const QList<int> sel = selection();
        const bool drums = drumsTab();
        loadingEditor = true;
        if (sel.isEmpty()) {
            lbSel->setText(tr("เลือกเครื่องดนตรีในตารางก่อน"));
        } else if (sel.size() == 1) {
            lbSel->setText(drums ? drumNames().value(sel.first())
                                 : MidiHelper::GMInstrumentNumberNames().value(sel.first()));
        } else {
            lbSel->setText(tr("เลือกไว้ %1 รายการ").arg(sel.size()));
        }
        const int sf = sel.isEmpty() ? 0 : (drums ? mapDrum[sel.first()] : mapSf[sel.first()]);
        cbFile->setCurrentIndex(qMax(0, cbFile->findData(sf)));
        cbFile->setEnabled(!sel.isEmpty());
        lbSound->setVisible(!drums);
        edFind->setVisible(!drums);
        listSound->setVisible(!drums);
        lbLevelTitle->setVisible(!drums);
        slLevel->setVisible(!drums);
        lbLevel->setVisible(!drums);
        if (!drums) {
            slLevel->setValue(sel.isEmpty() ? 100 : mapVol[sel.first()]);
            lbLevel->setText(QString::number(slLevel->value()) + "%");
            fillSounds(sf, sel.isEmpty() ? -1 : mapSrc[sel.first()]);
        }
        btnPreview->setEnabled(!sel.isEmpty());
        loadingEditor = false;
    }

    void fillSounds(int sf, int currentSrc)
    {
        listSound->clear();
        QListWidgetItem *own = new QListWidgetItem(tr("ตามเครื่องดนตรีเดิม (เสียงเลขเดียวกับที่เพลงสั่ง)"), listSound);
        own->setData(Qt::UserRole, -1);
        QFont bf = own->font();
        bf.setBold(true);
        own->setFont(bf);
        const HSOUNDFONT f = syn->soundfontHandle(sf);
        for (DWORD p : syn->soundfontPresetList(sf)) {
            const int bank = int(p >> 16), prog = int(p & 0xffff);
            if (bank >= 127)
                continue;                               // drum kits
            const char *n = BASS_MIDI_FontGetPreset(f, prog, bank);
            QListWidgetItem *it = new QListWidgetItem(
                QString("%1:%2  %3").arg(bank, 3, 10, QChar('0')).arg(prog, 3, 10, QChar('0'))
                    .arg(n ? QString::fromLocal8Bit(n).trimmed() : QString()), listSound);
            it->setData(Qt::UserRole, bank * 128 + prog);
        }
        for (int i = 0; i < listSound->count(); i++)
            if (listSound->item(i)->data(Qt::UserRole).toInt() == currentSrc) {
                listSound->setCurrentRow(i);
                listSound->scrollToItem(listSound->item(i), QAbstractItemView::PositionAtCenter);
                break;
            }
        filterSounds();
    }
    void filterSounds()
    {
        const QString t = edFind->text().trimmed();
        for (int i = 1; i < listSound->count(); i++)
            listSound->item(i)->setHidden(!t.isEmpty() && !listSound->item(i)->text().contains(t, Qt::CaseInsensitive));
    }

    void setFileForSelection(int comboIdx)
    {
        const int sf = cbFile->itemData(comboIdx).toInt();
        const QList<int> sel = selection();
        if (sel.isEmpty())
            return;
        for (int i : sel) {
            if (drumsTab()) {
                mapDrum[i] = sf;
            } else {
                if (mapSf[i] != sf)
                    mapSrc[i] = -1;                     // a sound of the old file means nothing in the new one
                mapSf[i] = sf;
            }
        }
        changed();
        refreshAll();
    }
    void setSoundForSelection(QListWidgetItem *it)
    {
        if (!it || drumsTab())
            return;
        const int src = it->data(Qt::UserRole).toInt();
        const int sf = cbFile->currentData().toInt();
        for (int i : selection()) {
            mapSf[i] = sf;
            mapSrc[i] = src;
        }
        changed();
        refreshAll();
    }
    void setLevelForSelection(int v)
    {
        if (drumsTab())
            return;
        for (int i : selection())
            mapVol[i] = v;
        changed();
        // only the level column (the whole refresh would rebuild the sound list while dragging)
        for (int i : selection()) {
            instItems[i]->setText(3, QString::number(v) + "%");
            instItems[i]->setForeground(3, v != 100 ? QBrush(QColor("#f59e0b")) : QBrush());
        }
    }
    void resetSelection()
    {
        for (int i : selection()) {
            if (drumsTab()) {
                mapDrum[i] = 0;
            } else {
                mapSf[i] = 0;
                mapSrc[i] = -1;
                mapVol[i] = 100;
            }
        }
        changed();
        refreshAll();
    }

    // ---- preview ----------------------------------------------------------------------------------
    void stopPreview()
    {
        for (QTimer *t : previewTimers) {
            t->stop();
            t->deleteLater();
        }
        previewTimers.clear();
        if (previewStream) {
            BASS_MIDI_StreamEvent(previewStream, 0, MIDI_EVENT_NOTESOFF, 0);
            BASS_MIDI_StreamEvent(previewStream, 9, MIDI_EVENT_NOTESOFF, 0);
        }
    }
    bool ensurePreviewStream()
    {
        const DWORD mixer = syn->mainMixerHandle();
        if (!syn->isOpened() || !mixer)
            return false;
        if (previewStream && BASS_Mixer_ChannelGetMixer(previewStream) != mixer) {
            BASS_StreamFree(previewStream);
            previewStream = 0;
        }
        if (!previewStream) {
            BASS_CHANNELINFO ci;
            const DWORD freq = BASS_ChannelGetInfo(mixer, &ci) ? ci.freq : 44100;
            previewStream = BASS_MIDI_StreamCreate(16, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT, freq);
            if (!previewStream)
                return false;
            if (!BASS_Mixer_StreamAddChannel(mixer, previewStream, BASS_MIXER_CHAN_NORAMPIN)) {
                BASS_StreamFree(previewStream);
                previewStream = 0;
                return false;
            }
        }
        return true;
    }
    void at(int ms, std::function<void()> fn)
    {
        QTimer *t = new QTimer(this);
        t->setSingleShot(true);
        connect(t, &QTimer::timeout, this, [fn]() { fn(); });
        t->start(ms);
        previewTimers << t;
    }
    void note(int ch, int key, int vel, int onMs, int lenMs)
    {
        const HSTREAM s = previewStream;
        at(onMs, [s, ch, key, vel]() { BASS_MIDI_StreamEvent(s, ch, MIDI_EVENT_NOTE, MAKEWORD(key, vel)); });
        at(onMs + lenMs, [s, ch, key]() { BASS_MIDI_StreamEvent(s, ch, MIDI_EVENT_NOTE, MAKEWORD(key, 0)); });
    }
    void preview()
    {
        stopPreview();
        const QList<int> sel = selection();
        if (sel.isEmpty())
            return;
        if (!ensurePreviewStream()) {
            QMessageBox::information(this, tr("ลองฟัง"), tr("ลองฟังไม่ได้: ตอนนี้ไม่ได้ใช้ซาวด์ฟอนต์ในโปรแกรมเล่นเพลง "
                                                          "(ตั้งให้ส่งออก MIDI ไปเครื่องอื่นอยู่)"));
            return;
        }
        applyNow();
        const int first = sel.first();
        if (drumsTab()) {
            const HSOUNDFONT f = syn->soundfontHandle(mapDrum[first]);
            if (!f)
                return;
            BASS_MIDI_FONT font = { f, -1, 0 };
            BASS_MIDI_StreamSetFonts(previewStream, &font, 1);
            BASS_ChannelSetAttribute(previewStream, BASS_ATTRIB_VOL, 1.0f);
            BASS_MIDI_StreamEvent(previewStream, 9, MIDI_EVENT_PROGRAM, 0);
            // two bars of a plain beat at 110 BPM
            const int b = 60000 / 110 / 2;
            for (int i = 0; i < 16; i++) {
                note(9, 42, 80, i * b, b / 2);                              // hi-hat
                if (i % 4 == 0) note(9, 36, 112, i * b, b);                 // kick
                if (i % 8 == 4) note(9, 38, 105, i * b, b);                 // snare
                if (i == 14) note(9, 45, 105, i * b, b);                    // tom
                if (i == 15) note(9, 43, 110, i * b, b);
            }
            note(9, 49, 115, 16 * b, b * 2);                                // crash
            note(9, 36, 112, 16 * b, b);
            return;
        }
        const int sf = mapSf[first], src = mapSrc[first];
        const HSOUNDFONT f = syn->soundfontHandle(sf);
        if (!f)
            return;
        const int sp = src >= 0 ? src % 128 : first;
        const int sb = src >= 0 ? src / 128 : 0;
        BASS_MIDI_FONTEX e = { f, sp, sb, 0, 0, 0 };
        if (!BASS_MIDI_FontGetPreset(f, sp, sb))
            e = { syn->soundfontHandle(0), first, 0, 0, 0, 0 };     // the file lacks it: as the song would hear it
        BASS_MIDI_StreamSetFonts(previewStream, &e, 1 | BASS_MIDI_FONT_EX);
        BASS_ChannelSetAttribute(previewStream, BASS_ATTRIB_VOL, mapVol[first] / 100.0f);
        BASS_MIDI_StreamEvent(previewStream, 0, MIDI_EVENT_BANK, 0);
        BASS_MIDI_StreamEvent(previewStream, 0, MIDI_EVENT_PROGRAM, 0);
        // a bass plays low, the others an arpeggio and a chord
        const int group = first / 8;
        if (group == 4) {
            const int k[] = { 36, 36, 43, 45, 48, 45, 43, 40 };
            for (int i = 0; i < 8; i++)
                note(0, k[i], 105, i * 280, 260);
        } else {
            const int base = (group == 7 || group == 8 || group == 9 || group == 10) ? 67 : 60;   // horns / leads higher
            const int k[] = { 0, 4, 7, 12, 7, 4 };
            for (int i = 0; i < 6; i++)
                note(0, base + k[i], 100, i * 230, 220);
            for (int c : { 0, 4, 7, 12 })
                note(0, base + c, 96, 6 * 230 + 80, 1300);
        }
    }

    // ---- export / import --------------------------------------------------------------------------
    // copies a (big) file in pieces, the dialog stays alive; false when cancelled / failed
    static bool copyWithProgress(const QString &from, const QString &to, QProgressDialog &pd, qint64 &done)
    {
        QFile in(from), out(to);
        if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        QByteArray buf;
        while (!(buf = in.read(4 * 1024 * 1024)).isEmpty()) {
            if (out.write(buf) != buf.size())
                return false;
            done += buf.size();
            pd.setValue(int(done / (1024 * 1024)));
            QApplication::processEvents();
            if (pd.wasCanceled()) {
                out.close();
                QFile::remove(to);
                return false;
            }
        }
        return true;
    }

    void exportSet()
    {
        applyNow();
        const QStringList files = syn->soundfontFiles();
        if (files.isEmpty())
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("ส่งออกชุดเสียง"), tr("ชื่อชุดเสียง:"),
                                                   QLineEdit::Normal, tr("ชุดเสียงร้าน"), &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        const QString parent = QFileDialog::getExistingDirectory(this, tr("เลือกที่เก็บชุดเสียง (เช่น แฟลชไดรฟ์)"));
        if (parent.isEmpty())
            return;
        QString safe = name;
        safe.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
        const QString dir = QDir(parent).absoluteFilePath(safe);
        QDir().mkpath(dir);

        // file names in the set (two files of the same name from different folders: numbered)
        QStringList outNames;
        qint64 total = 0;
        for (const QString &f : files) {
            QString n = QFileInfo(f).fileName();
            for (int k = 2; outNames.contains(n, Qt::CaseInsensitive); k++)
                n = QFileInfo(f).completeBaseName() + QString(" (%1).").arg(k) + QFileInfo(f).suffix();
            outNames << n;
            total += QFileInfo(f).size();
        }
        QProgressDialog pd(tr("กำลังคัดลอกไฟล์ซาวด์ฟอนต์..."), tr("ยกเลิก"), 0, int(total / (1024 * 1024)) + 1, this);
        pd.setWindowModality(Qt::WindowModal);
        pd.setMinimumDuration(0);
        pd.setMinimumWidth(520);
        qint64 done = 0;
        for (int i = 0; i < files.size(); i++) {
            const QString to = QDir(dir).absoluteFilePath(outNames[i]);
            pd.setLabelText(tr("กำลังคัดลอก %1 (%2/%3)...").arg(outNames[i]).arg(i + 1).arg(files.size()));
            if (QFileInfo(to).exists() && QFileInfo(to).size() == QFileInfo(files[i]).size()) {
                done += QFileInfo(to).size();
                continue;                                   // already there
            }
            if (!copyWithProgress(files[i], to, pd, done)) {
                if (!pd.wasCanceled())
                    QMessageBox::warning(this, tr("ส่งออกชุดเสียง"), tr("คัดลอกไม่ได้: ") + files[i]);
                return;
            }
        }
        pd.setValue(pd.maximum());

        QJsonArray sfs;
        for (int i = 0; i < files.size(); i++)
            sfs.append(QJsonObject{ { "file", outNames[i] }, { "volume", qRound(syn->soundfontVolume(i) * 100) } });
        QJsonArray presets;
        for (int p = 0; p < SF_PRESET_COUNT; p++) {
            const QList<int> sf = syn->getMapSoundfontIndex(p), src = syn->getMapSoundSource(p),
                             vol = syn->getMapSoundVolume(p), drum = syn->getDrumMapSfIndex(p);
            QJsonArray inst, dr;
            for (int i = 0; i < 128; i++)
                inst.append(QJsonArray{ sf.value(i, 0), src.value(i, -1), vol.value(i, 100) });
            for (int i = 0; i < 16; i++)
                dr.append(drum.value(i, 0));
            presets.append(QJsonObject{ { "index", p }, { "name", presetName(p) }, { "inst", inst }, { "drum", dr } });
        }
        const QJsonObject doc{ { "format", "PTHK multi soundfont" }, { "version", 1 }, { "name", name },
                               { "soundfonts", sfs }, { "presets", presets } };
        QFile jf(QDir(dir).absoluteFilePath(safe + ".pthksf"));
        if (!jf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(this, tr("ส่งออกชุดเสียง"), tr("บันทึกไฟล์ไม่ได้: ") + jf.fileName());
            return;
        }
        jf.write(QJsonDocument(doc).toJson(QJsonDocument::Indented));
        jf.close();
        QMessageBox::information(this, tr("ส่งออกชุดเสียง"),
            tr("ส่งออกแล้ว: %1\n\nซาวด์ฟอนต์ %2 ไฟล์ และการจัดเสียงทั้ง 11 ชุด\n"
               "ที่เครื่องอื่น: มัลติซาวด์ฟอนต์ › นำเข้าชุดเสียง... แล้วเลือกไฟล์ %3")
                .arg(QDir::toNativeSeparators(dir)).arg(files.size()).arg(safe + ".pthksf"));
    }

    void importSet()
    {
        applyNow();
        const QString file = QFileDialog::getOpenFileName(this, tr("นำเข้าชุดเสียง"), QString(),
                                                          tr("ชุดเสียง PTHK (*.pthksf)"));
        if (file.isEmpty())
            return;
        QFile jf(file);
        if (!jf.open(QIODevice::ReadOnly))
            return;
        const QJsonObject doc = QJsonDocument::fromJson(jf.readAll()).object();
        jf.close();
        if (doc.value("format").toString() != "PTHK multi soundfont") {
            QMessageBox::warning(this, tr("นำเข้าชุดเสียง"), tr("ไฟล์นี้ไม่ใช่ชุดเสียงของ PTHK"));
            return;
        }
        const QDir srcDir = QFileInfo(file).absoluteDir();
        const QJsonArray sfs = doc.value("soundfonts").toArray();
        QStringList names, missing;
        QList<int> vols;
        qint64 total = 0;
        for (const QJsonValue &v : sfs) {
            const QString n = v.toObject().value("file").toString();
            names << n;
            vols << v.toObject().value("volume").toInt(100);
            if (!srcDir.exists(n))
                missing << n;
            else
                total += QFileInfo(srcDir.absoluteFilePath(n)).size();
        }
        if (names.isEmpty())
            return;
        if (!missing.isEmpty()) {
            QMessageBox::warning(this, tr("นำเข้าชุดเสียง"),
                tr("ไม่พบไฟล์ซาวด์ฟอนต์ในโฟลเดอร์เดียวกับชุดเสียง:\n") + missing.join('\n'));
            return;
        }
        const QString setName = doc.value("name").toString(QFileInfo(file).completeBaseName());
        if (QMessageBox::question(this, tr("นำเข้าชุดเสียง"),
                tr("นำเข้า \"%1\" (ซาวด์ฟอนต์ %2 ไฟล์)?\n\nรายการซาวด์ฟอนต์และการจัดเสียงทั้งหมดในเครื่องนี้จะถูกแทนด้วยชุดนี้\n"
                   "ไฟล์จะถูกคัดลอกมาเก็บในเครื่อง (ถอดแฟลชไดรฟ์ได้หลังนำเข้า)").arg(setName).arg(names.size()))
            != QMessageBox::Yes)
            return;

        // into <data>\..\SoundFonts\<set> (writable in a normal and a portable install)
        QString safe = setName;
        safe.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
        const QString dest = QDir::cleanPath(QDir(Config::DATABASE_DIR_PATH).absoluteFilePath("../SoundFonts/" + safe));
        QStringList finalPaths;
        const bool inPlace = QDir::cleanPath(srcDir.absolutePath()).compare(dest, Qt::CaseInsensitive) == 0;
        if (!inPlace) {
            QDir().mkpath(dest);
            QProgressDialog pd(tr("กำลังคัดลอกไฟล์ซาวด์ฟอนต์..."), tr("ยกเลิก"), 0, int(total / (1024 * 1024)) + 1, this);
            pd.setWindowModality(Qt::WindowModal);
            pd.setMinimumDuration(0);
            pd.setMinimumWidth(520);
            qint64 done = 0;
            for (int i = 0; i < names.size(); i++) {
                const QString from = srcDir.absoluteFilePath(names[i]);
                const QString to = QDir(dest).absoluteFilePath(names[i]);
                pd.setLabelText(tr("กำลังคัดลอก %1 (%2/%3)...").arg(names[i]).arg(i + 1).arg(names.size()));
                if (!(QFileInfo(to).exists() && QFileInfo(to).size() == QFileInfo(from).size())) {
                    if (!copyWithProgress(from, to, pd, done)) {
                        if (!pd.wasCanceled())
                            QMessageBox::warning(this, tr("นำเข้าชุดเสียง"), tr("คัดลอกไม่ได้: ") + from);
                        return;
                    }
                } else {
                    done += QFileInfo(to).size();
                }
                finalPaths << QDir::fromNativeSeparators(to);
            }
            pd.setValue(pd.maximum());
        } else {
            for (const QString &n : names)
                finalPaths << QDir::fromNativeSeparators(srcDir.absoluteFilePath(n));
        }

        // the soundfont list, in the set's order
        QApplication::setOverrideCursor(Qt::WaitCursor);
        int guard = 512;
        while (guard-- > 0 && !syn->soundfontFiles().isEmpty())
            syn->removeSoundfont(0);
        QStringList failed;
        for (int i = 0; i < finalPaths.size(); i++) {
            if (syn->addSoundfont(finalPaths[i]))
                syn->setSoundfontVolume(syn->soundfontFiles().size() - 1, vols.value(i, 100) / 100.0f);
            else
                failed << names[i];
        }
        // file numbers in the set -> numbers now (a file that did not open: the main one)
        QList<int> remap;
        for (int i = 0; i < finalPaths.size(); i++)
            remap << qMax(0, syn->soundfontFiles().indexOf(finalPaths[i]));
        for (const QJsonValue &pv : doc.value("presets").toArray()) {
            const QJsonObject po = pv.toObject();
            const int p = po.value("index").toInt(-1);
            if (p < 0 || p >= SF_PRESET_COUNT)
                continue;
            QList<int> sf, src, vol, drum;
            const QJsonArray inst = po.value("inst").toArray();
            for (int i = 0; i < 128; i++) {
                const QJsonArray e = inst.at(i).toArray();
                sf << remap.value(e.at(0).toInt(0), 0);
                src << e.at(1).toInt(-1);
                vol << e.at(2).toInt(100);
            }
            const QJsonArray dr = po.value("drum").toArray();
            for (int i = 0; i < 16; i++)
                drum << remap.value(dr.at(i).toInt(0), 0);
            syn->setMapSoundExtras(p, src, vol);
            syn->setMapSoundfontIndex(p, sf, drum);
        }
        syn->setSoundfontPresets(syn->soundfontPresets());
        QApplication::restoreOverrideCursor();
        filesChanged();
        if (!failed.isEmpty())
            QMessageBox::warning(this, tr("นำเข้าชุดเสียง"), tr("เปิดไฟล์นี้ไม่ได้ (ใช้ไฟล์หลักแทน):\n") + failed.join('\n'));
        else
            QMessageBox::information(this, tr("นำเข้าชุดเสียง"), tr("นำเข้า \"%1\" แล้ว").arg(setName));
    }
};

#endif // MULTISOUNDFONTDIALOG_H
