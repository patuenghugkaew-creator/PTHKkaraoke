#ifndef EASYMEDLEYDIALOG_H
#define EASYMEDLEYDIALOG_H

#include <QDialog>

class MidiPlayer;
class QSpinBox;

class EasyMedleyDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EasyMedleyDialog(QWidget *parent = nullptr, MidiPlayer *player = nullptr);
    ~EasyMedleyDialog() override = default;

    int tempo() const;
    bool isTurnOffRequested() const { return _turnOffRequested; }

protected:
    void keyPressEvent(QKeyEvent *e) override;

private slots:
    void onOkClicked();
    void onOffClicked();

private:
    MidiPlayer *_player = nullptr;
    QSpinBox *_spinBpm = nullptr;
    bool _turnOffRequested = false;
};

#endif // EASYMEDLEYDIALOG_H
