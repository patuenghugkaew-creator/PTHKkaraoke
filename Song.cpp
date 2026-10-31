#include "Song.h"
#include <QStringList>

Song::Song(QObject *parent) : QObject(parent)
{
    sId         = "";
    sName       = "";
    sArtist     = "";
    sKey        = "";
    sTempo      = 0;
    sBpmSpeed   = 0;
    sTranspose  = 0;
    sSongType   = "";
    sLyrics     = "";
    sPath       = "";

    sCutStartBar = 0;
    sCutEndBar   = 1;
}

Song::Song(const Song &s)
{
    sId         = s.sId;
    sName       = s.sName;
    sArtist     = s.sArtist;
    sKey        = s.sKey;
    sTempo      = s.sTempo;
    sBpmSpeed   = s.sBpmSpeed;
    sTranspose  = s.sTranspose;
    sSongType   = s.sSongType;
    sLyrics     = s.sLyrics;
    sPath       = s.sPath;

    sCutStartBar = s.sCutStartBar;
    sCutEndBar   = s.sCutEndBar;
}

Song &Song::operator =(const Song &s)
{
    sId         = s.sId;
    sName       = s.sName;
    sArtist     = s.sArtist;
    sKey        = s.sKey;
    sTempo      = s.sTempo;
    sBpmSpeed   = s.sBpmSpeed;
    sTranspose  = s.sTranspose;
    sSongType   = s.sSongType;
    sLyrics     = s.sLyrics;
    sPath       = s.sPath;

    sCutStartBar = s.sCutStartBar;
    sCutEndBar   = s.sCutEndBar;

    return *this;
}

QString Song::detail()
{
    QString d = sId + "  " + sName + " - " + sArtist;
    d += "  (" + QString::number(sTempo + sBpmSpeed);
    d += sKey == "" ? ")" : "-" + sKey + ")";
    if (sBpmSpeed != 0)
    {
        QString s = sBpmSpeed > 0 ? "+" + QString::number(sBpmSpeed) : QString::number(sBpmSpeed);
        d += " (" + s + ")";
    }
    if (sTranspose != 0)
    {
        QString str = sTranspose > 0 ? "+" + QString::number(sTranspose) : QString::number(sTranspose);
        d += " (Key " + str + ")";
    }
    d += "  [" + sSongType + "]";
    return d;
}

QString Song::detailWithoutIDType()
{
    // shown in the playlist: name - artist   120 BPM · Cm  (speed +5) (key +2)
    QString d = sName + " - " + sArtist;
    QStringList info;
    if (sTempo + sBpmSpeed > 0)
        info << QString::number(sTempo + sBpmSpeed) + " BPM";
    if (!sKey.isEmpty())
        info << sKey;
    if (!info.isEmpty())
        d += "   " + info.join(QString(" ") + QChar(0x00B7) + " ");
    if (sBpmSpeed != 0)
        d += QString(" (%1%2)").arg(sBpmSpeed > 0 ? "+" : "").arg(sBpmSpeed);
    if (sTranspose != 0)
        d += QString(" (Key %1%2)").arg(sTranspose > 0 ? "+" : "").arg(sTranspose);
    return d;
}
