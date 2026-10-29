#include "PTHKCore.h"
#include "MedleyLoader.h"

#include "Config.h"

#include "Utils.h"
#include "Song.h"
#include "SongDatabase.h"
#include "Midi/MidiPlayer.h"
#include "Midi/HNKFile.h"
#include "Widgets/LyricsWidget.h"


MedleyLoader::MedleyLoader(QObject *parent,
                           Song *song,
                           SongDatabase *songDB,
                           MidiPlayer *player,
                           LyricsWidget *lyrWidget,
                           LyricsWidget *lyrWidget2)
    : QThread(parent)
{
    _song = song;
    _songDb = songDB;
    _player = player;
    _lyrWidget = lyrWidget;
    _lyrWidget2 = lyrWidget2;
}

void MedleyLoader::run()
{
    if (_song->songType() == "NCN")
    {
        QString midPath = _songDb->ncnPath() + _song->path();
        _player->loadNextMedley(midPath, _song->cutStartBar(), _song->cutEndBar(), _song->bpmSpeed(), _song->transpose());

        QString curPath = _songDb->getCurFilePath(midPath);
        if (curPath == "" || !QFile::exists(curPath)) {
            return;
        }

        QString lyrPath = _songDb->getLyrFilePath(midPath);
        if (lyrPath == "" || !QFile::exists(lyrPath)) {
            return;
        }

        _lyrWidget->setLyricsTemp(Utils::readLyrics(lyrPath),
            Utils::readCurFile(curPath, _player->midiFileTemp()->resorution()));
    }
    else if (_song->songType() == "EMK")
    {
        QString emkPath = _songDb->hnkPath() + _song->path();
        if (!QFile::exists(emkPath) && !_songDb->hnkPath().isEmpty())
            emkPath = _songDb->hnkPath() + "/" + _song->path();
        if (!QFile::exists(emkPath) && !_songDb->ncnPath().isEmpty())
            emkPath = QFileInfo(_songDb->ncnPath()).dir().absoluteFilePath("EMK") + _song->path();
        if (!QFile::exists(emkPath))
            emkPath = _songDb->ncnPath() + _song->path();
        if (!QFile::exists(emkPath))
            emkPath = _song->path();

        PTHKCore::EmkSong emk = PTHKCore::readEmk(emkPath);
        if (emk.valid && !emk.midData.isEmpty()) {
            QFile mid(TEMP_MIDI_DIR_PATH);
            if (mid.exists())
                mid.remove();

            mid.open(QFile::ReadWrite);
            mid.write(emk.midData);
            mid.close();

            _player->loadNextMedley(TEMP_MIDI_DIR_PATH, _song->cutStartBar(), _song->cutEndBar(), _song->bpmSpeed(), _song->transpose());

            mid.remove();

            _lyrWidget->setLyricsTemp(Utils::readLyrics(emk.lyrData),
                Utils::readCurFile(emk.curData, _player->midiFileTemp()->resorution()));
        }
    }
    else if (_song->songType() == "SIB")
    {
        QString sibPath = _song->path();
        QStringList candidates;
        candidates << sibPath;
        if (!_songDb->sibPath().isEmpty()) {
            candidates << _songDb->sibPath() + "/" + _song->path();
            candidates << _songDb->sibPath() + _song->path();
            QDir pDir(_songDb->sibPath());
            if (pDir.cdUp()) {
                candidates << pDir.absoluteFilePath(_song->path().startsWith('/') ? _song->path().mid(1) : _song->path());
                candidates << pDir.absolutePath() + _song->path();
            }
        }
        candidates << "D:/REA/SIBSIB KARAOKE/SONG/MIDI" + _song->path();
        candidates << "D:/REA/SIBSIB KARAOKE/SONG/MIDI/" + _song->path();
        candidates << "D:/REA/SIBSIB KARAOKE/SONG/MIDI/SIB" + _song->path();
        candidates << "D:/REA/SIBSIB KARAOKE/SONG/MIDI/SIB/" + _song->path();
        candidates << "D:/REA/SIBSIB KARAOKE/SONG/MIDI/SIBKLDEMO" + _song->path();
        candidates << "D:/REA/SIBSIB KARAOKE/SONG/MIDI/SIBKLDEMO/S/" + _song->id() + ".sibkldemo";
        if (!_song->id().isEmpty()) {
            const QChar fc = _song->id().at(0).toUpper();
            candidates << "D:/REA/SIBSIB KARAOKE/SONG/MIDI/SIB/" + QString(fc) + "/" + _song->id() + ".sib";
        }
        for (const QString &cand : candidates) {
            if (QFile::exists(cand)) {
                sibPath = cand;
                break;
            }
        }

        PTHKCore::SibSong sib = PTHKCore::readSib(sibPath);
        if (sib.valid && !sib.midData.isEmpty()) {
            QFile mid(TEMP_MIDI_DIR_PATH);
            if (mid.exists())
                mid.remove();

            mid.open(QFile::ReadWrite);
            mid.write(sib.midData);
            mid.close();

            _player->loadNextMedley(TEMP_MIDI_DIR_PATH, _song->cutStartBar(), _song->cutEndBar(), _song->bpmSpeed(), _song->transpose());

            mid.remove();

            _lyrWidget->setLyricsTemp(Utils::readLyrics(sib.lyrData),
                Utils::readCurFile(sib.curData, _player->midiFileTemp()->resorution()));
        }
    }
    else if (_song->songType() == "HNK")
    {
        QString hnkPath = _songDb->hnkPath() + _song->path();

        QFile mid(TEMP_MIDI_DIR_PATH);
        if (mid.exists())
            mid.remove();

        mid.open(QFile::ReadWrite);
        mid.write(HNKFile::midData(hnkPath));
        mid.close();

        _player->loadNextMedley(TEMP_MIDI_DIR_PATH, _song->cutStartBar(), _song->cutEndBar(), _song->bpmSpeed(), _song->transpose());

        mid.remove();

        _lyrWidget->setLyricsTemp(Utils::readLyrics(HNKFile::lyrData(hnkPath)),
            Utils::readCurFile(HNKFile::curData(hnkPath), _player->midiFileTemp()->resorution()));
    }
    else if (_song->songType() == "KAR")
    {
        QString karPath = _songDb->karPath() + _song->path();

        _player->loadNextMedley(karPath, _song->cutStartBar(), _song->cutEndBar(), _song->bpmSpeed(), _song->transpose());

        _lyrWidget->setLyricsTemp(_player->midiFileTemp()->lyrics(), _player->midiFileTemp()->lyricsCursor());
    }  else {
        return;
    }

    if (_player->midiSequencerTemp() != nullptr) {
        _player->midiSequencerTemp()->applyAutoMedleyBounds(_lyrWidget->curTempData(),
                                                           _song->cutStartBar(),
                                                           _song->cutEndBar(),
                                                           _lyrWidget->lastActiveTempLyricTick(),
                                                           _player->medleySkipIntro(),
                                                           _player->medleyAutoCutOutro(),
                                                           _player->medleyWaitBars(),
                                                           _player->medleyQuantizeBar());
    }
    if (_lyrWidget2 != nullptr) {
        _lyrWidget2->setLyricsTemp(_lyrWidget->lyrTempData(), _lyrWidget->curTempData());
    }
}
