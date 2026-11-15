// MIDI routing by port name: the main MIDI out, MIDI in, the 16 channels and (in MidiSynthesizer)
// each instrument. Applied at start and every 2 s, so ports that show up later (loopMIDI started after
// PTHK, a port closed and opened again) are connected without touching the settings, and a missing
// port never loses what the user chose. See Dialogs/MidiMapperDialog.h for the window.
#include "MainWindow_p.h"
#include "Dialogs/MidiMapperDialog.h"

static const char *kSynthPort = "@synth";

QString MainWindow::midiOutWant() const { return settings->value("MidiOutName", "").toString(); }
QString MainWindow::midiInWant() const { return settings->value("MidiInName", "").toString(); }

void MainWindow::setMidiOutWant(const QString &name)
{
    settings->setValue("MidiOutName", name);
}

void MainWindow::setMidiInWant(const QString &name)
{
    settings->setValue("MidiInName", name);
}

MainWindow::MidiRoute MainWindow::midiChannelWant(int ch) const
{
    MidiRoute r;
    const QStringList ports = settings->value("MidiChannelPorts").toStringList();
    const QStringList dual = settings->value("MidiChannelDual").toStringList();
    if (ch >= 0 && ch < ports.size())
        r.port = ports[ch];
    if (ch >= 0 && ch < dual.size())
        r.dual = dual[ch] == "1";
    return r;
}

void MainWindow::setMidiChannelWant(int ch, const MidiRoute &r)
{
    if (ch < 0 || ch >= 16)
        return;
    QStringList ports = settings->value("MidiChannelPorts").toStringList();
    QStringList dual = settings->value("MidiChannelDual").toStringList();
    while (ports.size() < 16) ports << QString();
    while (dual.size() < 16) dual << "0";
    ports[ch] = r.port;
    dual[ch] = r.dual ? "1" : "0";
    settings->setValue("MidiChannelPorts", ports);
    settings->setValue("MidiChannelDual", dual);
}

QString MainWindow::midiSynthPortKey() { return QString::fromLatin1(kSynthPort); }

void MainWindow::applyMidiRouting()
{
    const QStringList outs = MidiPlayer::midiDevices();
    const QStringList ins = MidiPlayer::midiInDevices();

    // MIDI in
    const QString inWant = midiInWant();
    const int inIdx = inWant.isEmpty() ? -1 : ins.indexOf(inWant);
    if (inIdx != player->midiInPortNumber())
        player->setMidiIn(inIdx);
    const QString inName = player->midiInPortNumber() >= 0 ? inWant : QString();

    player->reopenFailedOutputs();

    // main out: never the port we listen on (what PTHK sends would come straight back in: a loop)
    const QString outWant = midiOutWant();
    const int mainIdx = (outWant.isEmpty() || outWant == inName) ? -1 : outs.indexOf(outWant);
    if (mainIdx != player->midiOutPortNumber() && !player->setMidiOutLive(mainIdx))
        player->setMidiOutLive(-1);
    const int mainNow = player->midiOutPortNumber();

    // channels: follow the main out, the SoundFont, or a port of their own (missing -> SoundFont)
    for (int ch = 0; ch < 16; ++ch) {
        const MidiRoute r = midiChannelWant(ch);
        int port;
        if (r.port.isEmpty())
            port = mainNow;
        else if (r.port == kSynthPort)
            port = -1;
        else
            port = (r.port == inName) ? -1 : outs.indexOf(r.port);
        if (port != player->midiChannel()[ch].port()) {
            player->setMapChannelOutput(ch, port);
            if (player->midiChannel()[ch].port() != port)
                player->setMapChannelOutput(ch, -1);       // could not open it: play it here
        }
        player->setChannelDual(ch, r.dual && player->midiChannel()[ch].port() != -1);
    }

    // instruments (mixer strips)
    player->midiSynthesizer()->reconnectExt(inName);
}

bool MainWindow::isMidiRoutedOut()
{
    if (!midiOutWant().isEmpty())
        return true;
    for (int ch = 0; ch < 16; ++ch) {
        const QString p = midiChannelWant(ch).port;
        if (!p.isEmpty() && p != kSynthPort)
            return true;
    }
    MidiSynthesizer *syn = player->midiSynthesizer();
    for (int t = 0; t <= int(InstrumentType::PercussionEtc); ++t)
        if (!syn->extMidiOut(static_cast<InstrumentType>(t)).isEmpty())
            return true;
    return !syn->extMidiOut(InstrumentType::Canthai).isEmpty();
}

// the "no sound" way out: main out, the 16 channels and every instrument back on the SoundFont
void MainWindow::resetMidiRoutingToSoundfont()
{
    stop();
    setMidiOutWant(QString());
    for (int ch = 0; ch < 16; ++ch)
        setMidiChannelWant(ch, MidiRoute());
    MidiSynthesizer *syn = player->midiSynthesizer();
    QList<int> types;
    for (int t = 0; t <= int(InstrumentType::PercussionEtc); ++t)
        types << t;
    types << int(InstrumentType::Canthai);
    for (int t : types) {
        syn->setExtMidiOut(static_cast<InstrumentType>(t), QString());
        syn->setExtMidiDual(static_cast<InstrumentType>(t), false);
    }
    applyMidiRouting();
    saveSynthMixerSettings();
}

void MainWindow::saveSynthMixerSettings()
{
    if (synthMix)
        synthMix->settingValues();
}

void MainWindow::showMidiMapper()
{
    MidiMapperDialog dlg(this, dialogParent());
    dlg.exec();
    saveSynthMixerSettings();
}
