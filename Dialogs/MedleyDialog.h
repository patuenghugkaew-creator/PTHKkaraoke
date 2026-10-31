#ifndef MEDLEYDIALOG_H
#define MEDLEYDIALOG_H

#include <QDialog>

class MidiPlayer;
class QSpinBox;
class QComboBox;
class QSlider;
class QLabel;
class QCheckBox;
class QPushButton;

class MedleyDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MedleyDialog(QWidget *parent = nullptr, MidiPlayer *player = nullptr);
    ~MedleyDialog() override = default;

    int rhythmCode() const;
    int fillPattern() const;
    int tempo() const;
    int tempoMode() const;
    int transitionBars() const;
    int fillVolume() const;
    bool skipIntro() const;
    bool autoCutOutro() const;
    int waitBars() const;
    bool quantize() const;

    bool isTurnOffRequested() const { return _turnOffRequested; }

protected:
    void keyPressEvent(QKeyEvent *e) override;

private slots:
    void onRhythmSpinChanged(int code);
    void onRhythmComboChanged(int index);
    void onVolumeSliderChanged(int val);
    void onBarsButtonClicked(int bars);
    void onResetClicked();
    void onOkClicked();
    void onOffClicked();

private:
    MidiPlayer *_player = nullptr;

    // Rhythm & Tempo controls
    QSpinBox   *_spinRhythm = nullptr;
    QComboBox  *_comboRhythm = nullptr;
    QSpinBox   *_spinBpm = nullptr;
    QPushButton *_btnTempoLock = nullptr;
    QPushButton *_btnTempoFollow = nullptr;
    QWidget    *_bpmChips = nullptr;
    QCheckBox  *_chkKeepFar = nullptr;
    bool        _tempoFollow = false;   // true = each song keeps its own tempo (BPM 0)

    // Transition & Volume
    int         _selectedBars = 1;
    QPushButton *_btnBar0 = nullptr;
    QPushButton *_btnBar1 = nullptr;
    QPushButton *_btnBar2 = nullptr;
    QPushButton *_btnBar4 = nullptr;
    QLabel     *_lblBarsHint = nullptr;
    QSlider    *_sliderVol = nullptr;
    QLabel     *_lblVolVal = nullptr;

    // Cues controls
    QCheckBox  *_chkSkipIntro = nullptr;
    QCheckBox  *_chkAutoCutOutro = nullptr;
    QComboBox  *_comboWaitBars = nullptr;
    QCheckBox  *_chkQuantize = nullptr;

    bool _turnOffRequested = false;
    bool _isSyncing = false;

    void updateBarsSelection(int bars);
    void updateTempoMode(bool follow);
    void loadFromPlayer();
    void saveToPlayer();
};

#endif // MEDLEYDIALOG_H
