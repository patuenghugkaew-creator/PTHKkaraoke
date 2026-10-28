#ifndef ASIOOUTPUT_H
#define ASIOOUTPUT_H

#include <QStringList>

// Optional ASIO output (Windows only).  The synth's main mixer is created as a
// decoding stream and BASSASIO pulls the audio from it, so the sound goes
// straight to the selected ASIO driver instead of a normal Windows device.
namespace AsioOutput
{
    QStringList devices();                    // ASIO driver names (empty if none installed)
    bool        start(unsigned long source, int device);   // source = BASS decoding mixer
    void        stop();
    bool        isActive();
    QString     lastError();
    void        setGain(float g);                      // master gain (volume x headroom) applied to the ASIO feed
    float       gain();
    void        setRate(int hz);                       // rate to ask the driver for (default 44100)

    // ---- audio device panel (Settings) ----
    void        setPairMask(unsigned mask);            // bit n = driver outputs 2n+1 / 2n+2 receive the main mix (default 1 = outputs 1+2)
    unsigned    pairMask();
    // Speaker split: instead of one stereo mix copied to the chosen pairs, the main mixer's speaker layout goes to the
    // driver as it is: front -> outputs 1+2, rear -> 3+4, centre/sub -> 5+6, side -> 7+8 (needs 4+ outputs; else off)
    void        setSpeakerSplit(bool on);
    bool        speakerSplit();
    int         splitChannels();                       // driver outputs used by the split (0 = not splitting)
    void        setBufferSize(int samples);            // ASIO buffer length in samples, 0 = driver default
    int         bufferSize();
    int         outputCount();                         // output channels of the running driver (0 when ASIO is not running)
    QString     outputName(int channel);               // channel name reported by the running driver
    bool        bufferInfo(int &minSamples, int &maxSamples, int &prefSamples, int &granularity);   // running driver only
    bool        rateSupported(int hz);                 // running driver only
    void        waitSettle(int ms);                    // sleep until ms have passed since ASIO was released (no-op if longer ago / never used)
    bool        recentlyStopped(int ms);               // ASIO was released less than ms ago
    bool        crashedLastStart();                    // the previous ASIO start never finished (hang / crash): true once, then cleared
    void        logLine(const QString &msg);           // append to ~/.PTHKKaraoke/asio.log
    bool        tapRead(float *mono, int count, int &rateHz);   // last `count` mono samples sent to the driver (for the RTA); false if not running
    int         currentRate();                         // rate the running driver really uses (0 when not running)
}

#endif // ASIOOUTPUT_H
