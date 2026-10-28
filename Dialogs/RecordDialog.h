#ifndef RECORDDIALOG_H
#define RECORDDIALOG_H

// PRO: record the main output to a WAV file. Header-only, no Q_OBJECT.
// The recording keeps running when this window is closed; open it again to stop.

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <cmath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QLocale>

#include "Midi/MidiSynthesizer.h"
#include "Midi/OutputRecorder.h"

class RecordDialog : public QDialog
{
public:
    RecordDialog(QWidget *parent, MidiSynthesizer *synth) : QDialog(parent), synth(synth)
    {
        setWindowTitle(QString::fromUtf8("บันทึกเสียง (Record)"));
        setMinimumWidth(520);

        QVBoxLayout *v = new QVBoxLayout(this);
        v->setSpacing(8);

        QLabel *help = new QLabel(QString::fromUtf8(
            "บันทึกเสียงที่ออกจากมิกเซอร์ (หลังเอฟเฟกต์ Master) เป็นไฟล์ WAV สเตอริโอ\n"
            "การบันทึกจะทำต่อเนื่องแม้ปิดหน้าต่างนี้ เปิดหน้าต่างนี้อีกครั้งเพื่อหยุด"), this);
        help->setWordWrap(true);
        v->addWidget(help);

        QHBoxLayout *h1 = new QHBoxLayout();
        h1->addWidget(new QLabel(QString::fromUtf8("รูปแบบ :"), this));
        fmt = new QComboBox(this);
        fmt->addItem(QString::fromUtf8("WAV 16-bit"), 16);
        fmt->addItem(QString::fromUtf8("WAV 24-bit"), 24);
        fmt->addItem(QString::fromUtf8("WAV 32-bit float"), 32);
        fmt->setCurrentIndex(1);
        h1->addWidget(fmt);
        h1->addSpacing(16);
        h1->addWidget(new QLabel(QString::fromUtf8("ปรับความดังไฟล์ :"), this));
        gain = new QDoubleSpinBox(this);
        gain->setRange(-24.0, 12.0);
        gain->setSingleStep(0.5);
        gain->setDecimals(1);
        gain->setSuffix(QStringLiteral(" dB"));
        gain->setLocale(QLocale::c());          // Western digits (the Thai locale showed ๐.๐)
        // The box shows 0 dB for the calibrated level (a fixed -2 dB trim is applied underneath, see kCalibrationDb);
        // the value is kept while the program runs.
        gain->setValue(lastGain());
        connect(gain, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [](double v) { lastGain() = v; });
        h1->addWidget(gain);
        h1->addStretch(1);
        v->addLayout(h1);

        dither = new QCheckBox(QString::fromUtf8("Dither (TPDF) — ลดเสียงแตกเม็ดตอนบันทึก 16/24-bit"), this);
        dither->setChecked(true);
        v->addWidget(dither);

        QHBoxLayout *h2 = new QHBoxLayout();
        path = new QLineEdit(this);
        QString dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (dir.isEmpty())
            dir = QDir::homePath();
        path->setText(QDir(dir).filePath(QStringLiteral("PTHK_") +
                      QLocale::c().toString(QDateTime::currentDateTime(), QStringLiteral("yyyyMMdd_HHmmss")) + QStringLiteral(".wav")));
        QPushButton *browse = new QPushButton(QString::fromUtf8("เลือกที่บันทึก..."), this);
        h2->addWidget(path, 1);
        h2->addWidget(browse);
        v->addLayout(h2);

        status = new QLabel(this);
        QFont sf = status->font();
        sf.setBold(true);
        status->setFont(sf);
        v->addWidget(status);

        QHBoxLayout *h3 = new QHBoxLayout();
        h3->addStretch(1);
        startBtn = new QPushButton(this);
        QPushButton *close = new QPushButton(QString::fromUtf8("ปิด"), this);
        for (QPushButton *b : { startBtn, close }) {   // a little bigger (size only)
            QFont bf = b->font();
            bf.setPointSizeF(bf.pointSizeF() > 0 ? bf.pointSizeF() + 1.0 : 10.0);
            b->setFont(bf);
            b->setMinimumHeight(38);
        }
        h3->addWidget(startBtn);
        h3->addWidget(close);
        v->addLayout(h3);

        connect(browse, &QPushButton::clicked, this, [this]() {
            const QString f = QFileDialog::getSaveFileName(this, QString::fromUtf8("บันทึกเป็น"), path->text(),
                                                           QStringLiteral("WAV (*.wav)"));
            if (!f.isEmpty())
                path->setText(f.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive) ? f : f + QStringLiteral(".wav"));
        });
        connect(startBtn, &QPushButton::clicked, this, [this]() { toggle(); });
        connect(close, &QPushButton::clicked, this, &QDialog::accept);

        QTimer *t = new QTimer(this);
        t->setInterval(250);
        connect(t, &QTimer::timeout, this, [this]() { refresh(); });
        t->start();
        refresh();
    }

private:
    void toggle()
    {
        OutputRecorder &r = OutputRecorder::instance();
        if (r.isRecording()) {
            r.stop();
            status->setText(QString::fromUtf8("บันทึกแล้ว: ") + QDir::toNativeSeparators(r.path()));
            refresh();
            return;
        }
        QString file = path->text().trimmed();
        if (file.isEmpty())
            return;
        if (QFileInfo::exists(file) &&
            QMessageBox::question(this, windowTitle(), QString::fromUtf8("มีไฟล์ชื่อนี้อยู่แล้ว ต้องการเขียนทับหรือไม่?")) != QMessageBox::Yes)
            return;
        QString err;
        r.setDither(dither->isChecked() && fmt->currentData().toInt() < 32);
        r.setGainDb(gain->value() + kCalibrationDb);   // the box shows 0 dB = the level that matches what is heard
        if (!synth->isOpened() || !r.start(synth->mainMixerHandle(), file, fmt->currentData().toInt(), &err)) {
            status->setStyleSheet(QStringLiteral("color: #ff5a4d;"));
            status->setText(QString::fromUtf8("เริ่มบันทึกไม่ได้: ") + err);
            return;
        }
        refresh();
    }

    void refresh()
    {
        OutputRecorder &r = OutputRecorder::instance();
        const bool rec = r.isRecording();
        startBtn->setText(rec ? QString::fromUtf8("หยุดบันทึก (Stop)") : QString::fromUtf8("เริ่มบันทึก (Rec)"));
        DialogSkin::fitDialogButton(startBtn);
        fmt->setEnabled(!rec);
        gain->setEnabled(!rec);
        path->setEnabled(!rec);
        if (rec) {
            const int s = int(r.seconds());
            status->setStyleSheet(QStringLiteral("color: #ff5a4d;"));
            const float pk = r.peakValue();
            const QString pkText = pk > 0.0f ? QString::number(20.0 * std::log10(double(pk)), 'f', 1) + QStringLiteral(" dBFS")
                                             : QStringLiteral("-inf");
            status->setText(QString::fromUtf8("● REC  %1:%2   (%3 MB)   Peak %4%5")
                                .arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'))
                                .arg(r.bytes() / 1048576.0, 0, 'f', 1).arg(pkText)
                                .arg(pk >= 0.999f ? QString::fromUtf8("  (แตก! ลดความดังไฟล์)") : QString()));
        } else if (status->styleSheet().contains(QLatin1String("ff5a4d")) && status->text().startsWith(QString(QChar(0x25CF)))) {
            status->setStyleSheet(QString());
            status->setText(QString::fromUtf8("หยุดแล้ว"));
        }
    }

    MidiSynthesizer *synth;
    QComboBox *fmt = nullptr;
    QCheckBox *dither = nullptr;
    static constexpr double kCalibrationDb = -2.0;   // built-in trim: the file is cut at full level, so 0 dB in the box means -2 dB
    static double &lastGain() { static double g = 0.0; return g; }
    QDoubleSpinBox *gain = nullptr;
    QLineEdit *path = nullptr;
    QLabel *status = nullptr;
    QPushButton *startBtn = nullptr;
};

#endif // RECORDDIALOG_H
