#ifndef AUTOMEDLEYDIALOG_H
#define AUTOMEDLEYDIALOG_H

#include <QDialog>

class MidiPlayer;
class QComboBox;
class QSpinBox;
class QPushButton;

class AutoMedleyDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AutoMedleyDialog(QWidget *parent = nullptr, MidiPlayer *player = nullptr);
    ~AutoMedleyDialog() override = default;

    int rhythmCode() const;
    int fillPattern() const;
    int tempo() const;
    int tempoMode() const;
    bool skipIntro() const;
    int waitBars() const;

    bool isTurnOffRequested() const { return _turnOffRequested; }
    bool isOpenStudioRequested() const { return _openStudioRequested; }

protected:
    void keyPressEvent(QKeyEvent *e) override;

private slots:
    void onOkClicked();
    void onOffClicked();
    void onStudioClicked();
    void onRhythmTextChanged(const QString &text);

private:
    MidiPlayer *_player = nullptr;

    QComboBox *_comboRhythm = nullptr;
    QComboBox *_comboStyle = nullptr;
    QSpinBox  *_spinBpm = nullptr;
    QComboBox *_comboStart = nullptr;
    QComboBox *_comboWaitBars = nullptr;

    bool _turnOffRequested = false;
    bool _openStudioRequested = false;

    void syncControlsFromPlayer();
};

#endif // AUTOMEDLEYDIALOG_H
