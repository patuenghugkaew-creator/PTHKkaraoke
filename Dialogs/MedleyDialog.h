#ifndef MEDLEYDIALOG_H
#define MEDLEYDIALOG_H

#include <QDialog>

class MidiPlayer;

namespace Ui {
class MedleyDialog;
}

class MedleyDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MedleyDialog(QWidget *parent = nullptr, MidiPlayer *player = nullptr);
    ~MedleyDialog() override;

protected:
    void keyPressEvent(QKeyEvent *e) override;

private slots:
    void on_comboPreset_currentIndexChanged(int index);
    void on_tabWidget_currentChanged(int index);
    void on_btnReset_clicked();
    void on_btnOk_clicked();
    void on_btnCancel_clicked();

    void onParamChanged();

private:
    Ui::MedleyDialog *ui;
    MidiPlayer *_player = nullptr;
    bool _isUpdatingUi = false;

    void loadSettings();
    void saveSettings();
    void updatePreview();
    void applyPreset(int index);
};

#endif // MEDLEYDIALOG_H
