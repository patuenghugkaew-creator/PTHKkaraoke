#ifndef MASTERINGDIALOG_H
#define MASTERINGDIALOG_H

// PRO: Mastering chain + Loudness / Auto Level + Master limiter. Header-only, no Q_OBJECT.
// Every change is applied to the running audio at once and saved to the synth config file.

#include <QDialog>
#include <QGroupBox>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QTimer>
#include <QLocale>
#include <QSettings>
#include <cmath>

#include "Config.h"
#include "Midi/MidiSynthesizer.h"
#include "Midi/MasterDsp.h"

class MasteringDialog : public QDialog
{
public:
    // ---- settings -------------------------------------------------------------------------------------------
    static MasterDsp::Params loadSaved()
    {
        MasterDsp::Params p;
        QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
        st.beginGroup(QStringLiteral("Mastering"));
        p.masterOn   = st.value("MasterOn", false).toBool();
        p.compAmount = st.value("Comp", 0.0).toFloat();
        p.lowDb      = st.value("Low", 0.0).toFloat();
        p.midDb      = st.value("Mid", 0.0).toFloat();
        p.highDb     = st.value("High", 0.0).toFloat();
        p.exciter    = st.value("Exciter", 0.0).toFloat();
        p.width      = st.value("Width", 1.0).toFloat();
        p.autoOn     = st.value("AutoOn", false).toBool();
        p.targetLufs = st.value("Target", -16.0).toFloat();
        p.maxBoostDb = st.value("MaxBoost", 6.0).toFloat();
        p.maxCutDb   = st.value("MaxCut", 12.0).toFloat();
        p.limOn      = st.value("LimOn", false).toBool();
        p.ceilingDb  = st.value("Ceiling", -1.0).toFloat();
        p.releaseMs  = st.value("Release", 120.0).toFloat();
        p.softClip   = st.value("SoftClip", false).toBool();
        p.truePeak   = st.value("TruePeak", true).toBool();
        st.endGroup();
        return p;
    }

    static void save(const MasterDsp::Params &p)
    {
        QSettings st(Config::CONFIG_SYNTH_FILE_PATH, QSettings::IniFormat);
        st.beginGroup(QStringLiteral("Mastering"));
        st.setValue("MasterOn", p.masterOn);
        st.setValue("Comp", p.compAmount);
        st.setValue("Low", p.lowDb);
        st.setValue("Mid", p.midDb);
        st.setValue("High", p.highDb);
        st.setValue("Exciter", p.exciter);
        st.setValue("Width", p.width);
        st.setValue("AutoOn", p.autoOn);
        st.setValue("Target", p.targetLufs);
        st.setValue("MaxBoost", p.maxBoostDb);
        st.setValue("MaxCut", p.maxCutDb);
        st.setValue("LimOn", p.limOn);
        st.setValue("Ceiling", p.ceilingDb);
        st.setValue("Release", p.releaseMs);
        st.setValue("SoftClip", p.softClip);
        st.setValue("TruePeak", p.truePeak);
        st.endGroup();
    }

    // at start-up: the saved settings are used only with a PRO licence (without one everything stays off)
    static void applySaved(MidiSynthesizer *synth)
    {
        if (!synth)
            return;
        MasterDsp::Params p;
        if (PTHKCore::isProUnlocked())
            p = loadSaved();
        p.meter = false;
        synth->setMasterParams(p);
    }

    // ---- window ---------------------------------------------------------------------------------------------
    MasteringDialog(QWidget *parent, MidiSynthesizer *synth) : QDialog(parent), synth(synth)
    {
        setWindowTitle(QString::fromUtf8("Mastering (PRO) — คุณภาพเสียงขั้นสุดท้าย"));
        setMinimumWidth(620);

        QVBoxLayout *v = new QVBoxLayout(this);
        v->setSpacing(8);

        QLabel *help = new QLabel(QString::fromUtf8(
            "ทำงานที่ปลายทางสุดของเสียง (หลังเอฟเฟกต์ Master ก่อนบันทึก/ส่งออก) ทุกการปรับมีผลทันที\n"
            "แนะนำใช้โหมด 32-bit float (ตั้งค่าเสียง) เพื่อให้ Limiter ทำงานเต็มประสิทธิภาพ"), this);
        help->setWordWrap(true);
        v->addWidget(help);

        // ---- mastering chain
        QGroupBox *g1 = new QGroupBox(QString::fromUtf8("Mastering Chain (คอมเพรสเซอร์ 3 ย่าน + Exciter + Stereo Width)"), this);
        QGridLayout *l1 = new QGridLayout(g1);
        masterOn = new QCheckBox(QString::fromUtf8("เปิดใช้ Mastering Chain"), g1);
        l1->addWidget(masterOn, 0, 0, 1, 2);
        presetBox = new QComboBox(g1);
        presetBox->addItem(QString::fromUtf8("— ปรับเอง —"));
        presetBox->addItem(QString::fromUtf8("Default (ค่าเริ่มต้น)"));
        for (int i = 0; i < MasterDsp::presetCount(); i++)
            presetBox->addItem(QString::fromLatin1(MasterDsp::presetName(i)));
        l1->addWidget(new QLabel(QString::fromUtf8("พรีเซ็ต :"), g1), 1, 0);
        l1->addWidget(presetBox, 1, 1, 1, 2);
        comp   = makeSlider(l1, g1, 2, QString::fromUtf8("Compression"), 0, 100, 1.0, QStringLiteral(" %"));
        low    = makeSlider(l1, g1, 3, QString::fromUtf8("Low (<180 Hz)"), -12, 12, 2.0, QStringLiteral(" dB"));
        mid    = makeSlider(l1, g1, 4, QString::fromUtf8("Mid"), -12, 12, 2.0, QStringLiteral(" dB"));
        high   = makeSlider(l1, g1, 5, QString::fromUtf8("High (>3 kHz)"), -12, 12, 2.0, QStringLiteral(" dB"));
        exciter = makeSlider(l1, g1, 6, QString::fromUtf8("Exciter"), 0, 100, 1.0, QStringLiteral(" %"));
        width  = makeSlider(l1, g1, 7, QString::fromUtf8("Stereo Width"), 0, 200, 1.0, QStringLiteral(" %"));
        v->addWidget(g1);

        // ---- loudness / auto level
        QGroupBox *g2 = new QGroupBox(QString::fromUtf8("Loudness (LUFS) + Auto Level"), this);
        QGridLayout *l2 = new QGridLayout(g2);
        autoOn = new QCheckBox(QString::fromUtf8("Auto Level — ปรับความดังเพลงให้เท่ากันอัตโนมัติ"), g2);
        l2->addWidget(autoOn, 0, 0, 1, 4);
        target = makeSpin(g2, -30.0, -6.0, 0.5, QStringLiteral(" LUFS"));
        boost  = makeSpin(g2, 0.0, 12.0, 0.5, QStringLiteral(" dB"));
        cut    = makeSpin(g2, 0.0, 18.0, 0.5, QStringLiteral(" dB"));
        l2->addWidget(new QLabel(QString::fromUtf8("เป้าหมาย :"), g2), 1, 0);
        l2->addWidget(target, 1, 1);
        l2->addWidget(new QLabel(QString::fromUtf8("เพิ่มได้สูงสุด :"), g2), 1, 2);
        l2->addWidget(boost, 1, 3);
        l2->addWidget(new QLabel(QString::fromUtf8("ลดได้สูงสุด :"), g2), 2, 2);
        l2->addWidget(cut, 2, 3);
        lblMom = new QLabel(g2);
        lblShort = new QLabel(g2);
        lblAuto = new QLabel(g2);
        lblOut = new QLabel(g2);
        l2->addWidget(lblMom, 3, 0, 1, 2);
        l2->addWidget(lblShort, 3, 2, 1, 2);
        l2->addWidget(lblAuto, 4, 0, 1, 2);
        l2->addWidget(lblOut, 4, 2, 1, 2);
        v->addWidget(g2);

        // ---- limiter
        QGroupBox *g3 = new QGroupBox(QString::fromUtf8("Master Limiter + Soft Clip"), this);
        QGridLayout *l3 = new QGridLayout(g3);
        limOn = new QCheckBox(QString::fromUtf8("เปิด Limiter (กันเสียงแตก/ clip)"), g3);
        l3->addWidget(limOn, 0, 0, 1, 4);
        ceiling = makeSpin(g3, -6.0, -0.1, 0.1, QStringLiteral(" dBFS"));
        release = makeSpin(g3, 20.0, 1000.0, 10.0, QStringLiteral(" ms"));
        release->setDecimals(0);
        l3->addWidget(new QLabel(QString::fromUtf8("Ceiling :"), g3), 1, 0);
        l3->addWidget(ceiling, 1, 1);
        l3->addWidget(new QLabel(QString::fromUtf8("Release :"), g3), 1, 2);
        l3->addWidget(release, 1, 3);
        softClip = new QCheckBox(QString::fromUtf8("Soft Clip (ซ่อนยอดคลื่นให้นุ่มก่อนเข้า Limiter)"), g3);
        truePeak = new QCheckBox(QString::fromUtf8("True-peak (ตรวจยอด 4x oversampling กัน inter-sample peak)"), g3);
        l3->addWidget(softClip, 2, 0, 1, 4);
        l3->addWidget(truePeak, 3, 0, 1, 4);
        lblGr = new QLabel(g3);
        lblPeak = new QLabel(g3);
        l3->addWidget(lblGr, 4, 0, 1, 2);
        l3->addWidget(lblPeak, 4, 2, 1, 2);
        v->addWidget(g3);

        QHBoxLayout *h = new QHBoxLayout();
        QPushButton *reset = new QPushButton(QString::fromUtf8("รีเซ็ตทั้งหมด"), this);
        QPushButton *close = new QPushButton(QString::fromUtf8("ปิด"), this);
        h->addWidget(reset);
        h->addStretch(1);
        h->addWidget(close);
        v->addLayout(h);

        // ---- load
        loadUi(synth ? synth->masterParams() : MasterDsp::Params());

        // ---- wiring
        for (QCheckBox *c : { masterOn, autoOn, limOn, softClip, truePeak })
            connect(c, &QCheckBox::toggled, this, [this]() { changed(); });
        for (QSlider *s : { comp, low, mid, high, exciter, width })
            connect(s, &QSlider::valueChanged, this, [this]() { if (!loading) presetBox->setCurrentIndex(0); changed(); });
        for (QDoubleSpinBox *s : { target, boost, cut, ceiling, release })
            connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { changed(); });
        connect(presetBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
            if (i <= 0 || loading)
                return;
            MasterDsp::Params p = collect();
            if (i == 1) {          // Default: the chain's own settings back to neutral (limiter / auto level untouched)
                const MasterDsp::Params d;
                p.compAmount = d.compAmount;
                p.lowDb = d.lowDb;
                p.midDb = d.midDb;
                p.highDb = d.highDb;
                p.exciter = d.exciter;
                p.width = d.width;
            } else {
                MasterDsp::applyPreset(p, i - 2);
                p.masterOn = true;
            }
            loadUi(p, i);      // sets the combo while 'loading' is on (no re-entry)
            changed();
        });
        connect(reset, &QPushButton::clicked, this, [this]() {
            MasterDsp::Params p;
            loadUi(p);
            changed();
        });
        connect(close, &QPushButton::clicked, this, &QDialog::accept);

        QTimer *t = new QTimer(this);
        t->setInterval(100);
        connect(t, &QTimer::timeout, this, [this]() { refresh(); });
        t->start();
        changed();     // switches the meter on
        refresh();
    }

    ~MasteringDialog() override
    {
        if (synth) {
            MasterDsp::Params p = collect();
            p.meter = false;
            synth->setMasterParams(p);
        }
    }

private:
    QSlider *makeSlider(QGridLayout *l, QWidget *parent, int row, const QString &name, int mn, int mx, double scale,
                        const QString &suffix)
    {
        QSlider *s = new QSlider(Qt::Horizontal, parent);
        s->setRange(mn, mx);
        QLabel *val = new QLabel(parent);
        val->setMinimumWidth(70);
        val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        l->addWidget(new QLabel(name, parent), row, 0);
        l->addWidget(s, row, 1);
        l->addWidget(val, row, 2);
        auto upd = [s, val, scale, suffix]() {
            val->setText(QLocale::c().toString(s->value() / scale, 'f', scale > 1.5 ? 1 : 0) + suffix);
        };
        connect(s, &QSlider::valueChanged, this, upd);
        upd();
        return s;
    }

    QDoubleSpinBox *makeSpin(QWidget *parent, double mn, double mx, double step, const QString &suffix)
    {
        QDoubleSpinBox *s = new QDoubleSpinBox(parent);
        s->setLocale(QLocale::c());          // Western digits
        s->setRange(mn, mx);
        s->setSingleStep(step);
        s->setDecimals(1);
        s->setSuffix(suffix);
        return s;
    }

    MasterDsp::Params collect() const
    {
        MasterDsp::Params p;
        p.masterOn   = masterOn->isChecked();
        p.compAmount = comp->value() / 100.0f;
        p.lowDb      = low->value() / 2.0f;
        p.midDb      = mid->value() / 2.0f;
        p.highDb     = high->value() / 2.0f;
        p.exciter    = exciter->value() / 100.0f;
        p.width      = width->value() / 100.0f;
        p.autoOn     = autoOn->isChecked();
        p.targetLufs = float(target->value());
        p.maxBoostDb = float(boost->value());
        p.maxCutDb   = float(cut->value());
        p.meter      = true;
        p.limOn      = limOn->isChecked();
        p.ceilingDb  = float(ceiling->value());
        p.releaseMs  = float(release->value());
        p.softClip   = softClip->isChecked();
        p.truePeak   = truePeak->isChecked();
        return p;
    }

    void loadUi(const MasterDsp::Params &p, int presetIndex = 0)
    {
        loading = true;
        masterOn->setChecked(p.masterOn);
        comp->setValue(int(std::lround(p.compAmount * 100.0f)));
        low->setValue(int(std::lround(p.lowDb * 2.0f)));
        mid->setValue(int(std::lround(p.midDb * 2.0f)));
        high->setValue(int(std::lround(p.highDb * 2.0f)));
        exciter->setValue(int(std::lround(p.exciter * 100.0f)));
        width->setValue(int(std::lround(p.width * 100.0f)));
        autoOn->setChecked(p.autoOn);
        target->setValue(p.targetLufs);
        boost->setValue(p.maxBoostDb);
        cut->setValue(p.maxCutDb);
        limOn->setChecked(p.limOn);
        ceiling->setValue(p.ceilingDb);
        release->setValue(p.releaseMs);
        softClip->setChecked(p.softClip);
        truePeak->setChecked(p.truePeak);
        presetBox->setCurrentIndex(presetIndex);
        loading = false;
    }

    void changed()
    {
        if (loading || !synth)
            return;
        const MasterDsp::Params p = collect();
        synth->setMasterParams(p);
        MasterDsp::Params s = p;
        s.meter = false;
        save(s);
    }

    static QString lufs(float v)
    {
        return v <= -99.0f ? QString(QChar(0x2014)) : QLocale::c().toString(v, 'f', 1) + QStringLiteral(" LUFS");
    }

    void refresh()
    {
        if (!synth)
            return;
        const MasterDsp::Meter m = synth->masterMeter();
        lblMom->setText(QString::fromUtf8("ความดังขณะนั้น (Momentary) : ") + lufs(m.momentary));
        lblShort->setText(QString::fromUtf8("เฉลี่ย 3 วินาที (Short-term) : ") + lufs(m.shortTerm));
        lblAuto->setText(QString::fromUtf8("Auto Level ปรับอยู่ : ") + QLocale::c().toString(m.autoDb, 'f', 1) + QStringLiteral(" dB"));
        lblOut->setText(QString::fromUtf8("ประมาณหลังปรับ : ") +
                        (m.shortTerm <= -99.0f ? QString(QChar(0x2014)) : lufs(m.shortTerm + m.autoDb)));
        lblGr->setText(QString::fromUtf8("Limiter กดอยู่ : ") + QLocale::c().toString(m.grDb, 'f', 1) + QStringLiteral(" dB"));
        lblPeak->setText(QString::fromUtf8("ยอดเสียงออก (Peak) : ") +
                         (m.peakDb <= -99.0f ? QString(QChar(0x2014)) : QLocale::c().toString(m.peakDb, 'f', 1) + QStringLiteral(" dBFS")));
    }

    MidiSynthesizer *synth = nullptr;
    bool loading = false;

    QCheckBox *masterOn = nullptr, *autoOn = nullptr, *limOn = nullptr, *softClip = nullptr, *truePeak = nullptr;
    QComboBox *presetBox = nullptr;
    QSlider *comp = nullptr, *low = nullptr, *mid = nullptr, *high = nullptr, *exciter = nullptr, *width = nullptr;
    QDoubleSpinBox *target = nullptr, *boost = nullptr, *cut = nullptr, *ceiling = nullptr, *release = nullptr;
    QLabel *lblMom = nullptr, *lblShort = nullptr, *lblAuto = nullptr, *lblOut = nullptr, *lblGr = nullptr, *lblPeak = nullptr;
};

#endif // MASTERINGDIALOG_H
