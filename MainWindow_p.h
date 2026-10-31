// Private header shared by the MainWindow_*.cpp files: the includes they all need and
// two small diagnostics helpers. Not for use outside MainWindow.
#ifndef MAINWINDOW_P_H
#define MAINWINDOW_P_H

#include "Midi/VocalRemover.h"
#include <QMouseEvent>
#include "MainWindow.h"
#include "ui_MainWindow.h"

#include <QTime>
#include <QTimer>
#include <QCursor>
#include <QMenu>
#include <QFrame>
#include <QJsonObject>
#include <QJsonArray>
#include <QRadioButton>
#include <QHash>
#include <QSet>
#include <QActionGroup>
#include <QCloseEvent>
#include <QMessageBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QDataStream>
#include <QSaveFile>
#include <QProcess>
#include <QSettings>
#include <QWindow>
#include <QStandardPaths>
#include <QDateTime>
#include <QLocale>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <functional>
#include <QElapsedTimer>
#include <QUuid>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QSlider>
#include <QGroupBox>
#include <QGridLayout>
#include <QDialog>
#include <QClipboard>
#include <QListWidget>
#include <QDesktopServices>
#include <QUrl>
#include <QTextCodec>
#include <QProgressDialog>
#include <QThread>
#include <QTextBrowser>
#include <bassmix.h>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <algorithm>
#include <cmath>

#include "Config.h"
#include "Theme.h"
#include "Utils.h"
#include "MedleyLoader.h"
#include "DrumPadsKey.h"
#include "SettingsDialog.h"
#include "Widgets/MixerAutoHide.h"
#include "Midi/MidiFile.h"
#include "Midi/HNKFile.h"
#include "Midi/AsioOutput.h"
#include "Dialogs/AboutDialog.h"
#include "Dialogs/LockSoundfontDialog.h"
#include "Dialogs/MasteringDialog.h"
#include "PTHKCore.h"
#include "Dialogs/MapSoundfontDialog.h"
#include "Dialogs/MapChannelDialog.h"
#include "Dialogs/BusDialog.h"
#include "Dialogs/SpeakerDialog.h"
#include <QToolTip>
#include <QCursor>
#ifndef __linux__
#include "BASSFX/VSTFX.h"
#include "Vst3/BassVst3.h"
#include <QScreen>
#include <QGuiApplication>
#endif
#include "Dialogs/Equalizer31BandDialog.h"
#include "Dialogs/Chorus2Dialog.h"
#include "Widgets/MenuIcons.h"
#include "Dialogs/Reverb2Dialog.h"
#include "Dialogs/DeleteSongDialog.h"
#include "Dialogs/MedleyDialog.h"
// Developer timing logs: only when "debug-logs.txt" is next to the exe.
inline bool timingLogsOn()
{
    static const int on = QFile::exists(QCoreApplication::applicationDirPath() + "/debug-logs.txt") ? 1 : 0;
    return on == 1;
}

// ui-timing.log next to the exe: UI stalls and search times (diagnostics)
inline void uiLog(const QString &line)
{
    if (!timingLogsOn())
        return;
    QFile f(QCoreApplication::applicationDirPath() + "/ui-timing.log");
    if (f.open(QIODevice::Append | QIODevice::Text))
        f.write((QTime::currentTime().toString("hh:mm:ss.zzz ") + line + QChar(10)).toUtf8());
}

#include "Midi/AiStems.h"
#include "Midi/AudioBandMeter.h"
#include "SibBackfill.h"
#include "Dialogs/DuplicateSongsDialog.h"
#include "Dialogs/EasyMedleyDialog.h"
#include "Dialogs/SongListsDialog.h"
#include "Dialogs/YouTube.h"
#include "Midi/YtAudio.h"
#include "Midi/AiVocal.h"
#include "Midi/MicInput.h"
#include "Midi/OutputRecorder.h"
#include "Dmx/DmxLights.h"
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include "Remote/RemoteServer.h"
#include "Remote/RemoteDialog.h"
#include "Remote/SongRequestServer.h"

#ifndef __linux__
#include "Dialogs/VSTDirsDialog.h"
#include "Updater.h"
#include <QProgressDialog>
#include <QThread>
#include <QPointer>
#include <thread>
#endif


#endif // MAINWINDOW_P_H
