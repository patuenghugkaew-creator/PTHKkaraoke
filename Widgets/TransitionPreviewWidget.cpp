#include "TransitionPreviewWidget.h"
#include <QPaintEvent>

TransitionPreviewWidget::TransitionPreviewWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(138);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void TransitionPreviewWidget::setMedleyParams(int waitBars,
                                              bool quantizeBar,
                                              bool skipIntro,
                                              bool autoCutOutro,
                                              int fillBars,
                                              int fillPattern,
                                              int fillVolume,
                                              int tempoMode,
                                              int medleyBpm,
                                              double crossfadeSec,
                                              int crossfadeCurve,
                                              int activeTab,
                                              int introBars)
{
    _waitBars = waitBars;
    _quantizeBar = quantizeBar;
    _skipIntro = skipIntro;
    _autoCutOutro = autoCutOutro;
    _fillBars = fillBars;
    _fillPattern = fillPattern;
    _fillVolume = fillVolume;
    _tempoMode = tempoMode;
    _medleyBpm = medleyBpm;
    _crossfadeSec = crossfadeSec;
    _crossfadeCurve = crossfadeCurve;
    _activeTab = activeTab;
    _introBars = introBars;

    update();
}

void TransitionPreviewWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QRect fullRect = rect().adjusted(2, 2, -2, -2);

    // Dark sleek container
    QLinearGradient bgGrad(0, 0, 0, height());
    bgGrad.setColorAt(0.0, QColor(13, 17, 23));
    bgGrad.setColorAt(1.0, QColor(22, 27, 34));
    p.setPen(QPen(QColor(48, 54, 61), 1.2));
    p.setBrush(bgGrad);
    p.drawRoundedRect(fullRect, 8, 8);

    // Layout zones
    const int footerH = 26;
    const QRect visRect(fullRect.left() + 8, fullRect.top() + 6, fullRect.width() - 16, fullRect.height() - footerH - 10);
    const QRect footRect(fullRect.left() + 8, fullRect.bottom() - footerH, fullRect.width() - 16, footerH - 2);

    if (_activeTab == 2) {
        drawAudioCrossfade(p, visRect);
    } else {
        drawMidiTransition(p, visRect);
    }

    drawStatusFooter(p, footRect);
}

void TransitionPreviewWidget::drawMidiTransition(QPainter &p, const QRect &r)
{
    const int w = r.width();
    const int h = r.height();
    const int y = r.top();

    // 3 columns: Deck A (36%), Bridge (28%), Deck B (36%)
    const int aWidth = int(w * 0.36);
    const int bridgeWidth = int(w * 0.28);
    const int bWidth = w - aWidth - bridgeWidth;

    const QRect deckARect(r.left(), y, aWidth, h);
    const QRect bridgeRect(r.left() + aWidth, y, bridgeWidth, h);
    const QRect deckBRect(r.left() + aWidth + bridgeWidth, y, bWidth, h);

    // Vertical sub-zones inside each column:
    // Header label: y + 2 to y + 20 (18px)
    // Graphic / Waveform / Pattern: y + 22 to y + 68 (46px)
    // Sub-caption: y + 72 to y + 92 (20px)

    // -------------------------------------------------------------
    // 1. DECK A (Current Song Outro)
    // -------------------------------------------------------------
    QLinearGradient gradA(deckARect.left(), 0, deckARect.right(), 0);
    gradA.setColorAt(0.0, QColor(30, 58, 138, 160));  // Dark Blue
    gradA.setColorAt(1.0, QColor(76, 29, 149, 140));  // Dark Indigo
    p.setPen(QPen(QColor(59, 130, 246, 120), 1));
    p.setBrush(gradA);
    p.drawRoundedRect(deckARect.adjusted(0, 0, -4, 0), 6, 6);

    // Header A
    QFont fHeader = p.font();
    fHeader.setPointSize(9);
    fHeader.setBold(true);
    p.setFont(fHeader);
    p.setPen(QColor(147, 197, 253)); // Soft blue
    p.drawText(deckARect.adjusted(8, 5, -8, 0), Qt::AlignLeft | Qt::AlignTop, "เพลงเดิม (Deck A)");

    // Waveform bars inside Deck A (neatly positioned between header and caption)
    p.setPen(QPen(QColor(96, 165, 250, 180), 2.5, Qt::SolidLine, Qt::RoundCap));
    const int numBarsA = 10;
    const int barAreaTop = y + 24;
    const int barAreaBottom = y + 66;
    const int barAreaMid = (barAreaTop + barAreaBottom) / 2;
    const int barAreaMaxH = (barAreaBottom - barAreaTop) / 2;

    for (int i = 0; i < numBarsA; ++i) {
        int barX = deckARect.left() + 12 + i * (deckARect.width() - 28) / (numBarsA - 1);
        double decay = 1.0 - (i / double(numBarsA)) * 0.55;
        int barH = int(barAreaMaxH * decay);
        p.drawLine(barX, barAreaMid - barH, barX, barAreaMid + barH);
    }

    // Sub-caption A
    QFont fSub = p.font();
    fSub.setPointSize(8);
    fSub.setBold(false);
    p.setFont(fSub);
    p.setPen(QColor(191, 219, 254));
    QString aInfo;
    if (!_autoCutOutro) {
        aInfo = "เล่นจบเต็มเพลง";
    } else if (_waitBars == 0) {
        aInfo = "จบคำร้อง • ต่อทันที";
    } else {
        aInfo = QString("จบคำร้อง + รอ %1 ห้อง").arg(_waitBars);
    }
    p.drawText(deckARect.adjusted(8, 72, -8, 0), Qt::AlignLeft | Qt::AlignTop, aInfo);

    // -------------------------------------------------------------
    // 2. TRANSITION BRIDGE (Drum Fill-in)
    // -------------------------------------------------------------
    const bool hasBridge = (_fillBars > 0);
    QLinearGradient gradBridge(bridgeRect.left(), 0, bridgeRect.right(), 0);
    if (hasBridge) {
        gradBridge.setColorAt(0.0, QColor(120, 53, 15, 180));  // Warm Amber dark
        gradBridge.setColorAt(1.0, QColor(180, 83, 9, 200));   // Bright Amber
        p.setPen(QPen(QColor(245, 158, 11, 200), 1.2));
    } else {
        gradBridge.setColorAt(0.0, QColor(31, 41, 55, 160));
        gradBridge.setColorAt(1.0, QColor(55, 65, 81, 160));
        p.setPen(QPen(QColor(107, 114, 128, 140), 1));
    }
    p.setBrush(gradBridge);
    p.drawRoundedRect(bridgeRect.adjusted(2, 0, -2, 0), 6, 6);

    // Header Bridge
    p.setFont(fHeader);
    p.setPen(hasBridge ? QColor(253, 230, 138) : QColor(209, 213, 219));
    p.drawText(bridgeRect.adjusted(2, 5, -2, 0), Qt::AlignHCenter | Qt::AlignTop, "ลูกส่งช่วงต่อ");

    // Middle Drum Details
    if (hasBridge) {
        QString patStr = "900 สามช่า";
        switch (_fillPattern) {
        case 0: patStr = "900 สามช่า"; break;
        case 1: patStr = "901 ลำซิ่ง"; break;
        case 2: patStr = "902 3ช่าลำซิ่ง"; break;
        case 3: patStr = "903 ดิสโก้"; break;
        case 4: patStr = "904 ลำซิ่ง 2"; break;
        case 5: patStr = "008 โซล 1"; break;
        case 6: patStr = "000 มาตรฐาน"; break;
        case 7: patStr = "สุ่มตามเพลง"; break;
        default: patStr = "ลูกส่งมาตรฐาน"; break;
        }

        QFont fPat = p.font();
        fPat.setPointSize(8);
        fPat.setBold(true);
        p.setFont(fPat);
        p.setPen(QColor(254, 240, 138));
        p.drawText(QRect(bridgeRect.left() + 2, y + 24, bridgeRect.width() - 4, 16),
                   Qt::AlignCenter, patStr);

        // 4 Rhythm Beat Pills (Beats 1, 2, 3, 4)
        const int dotW = 14;
        const int dotH = 12;
        const int dotSpacing = bridgeRect.width() / 5;
        const int dotY = y + 46;
        p.setFont(fSub);
        for (int d = 1; d <= 4; ++d) {
            int dotX = bridgeRect.left() + d * dotSpacing - (dotW / 2);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(254, 243, 199, 220));
            p.drawRoundedRect(dotX, dotY, dotW, dotH, 3, 3);

            p.setPen(QColor(120, 53, 15));
            p.drawText(QRect(dotX, dotY, dotW, dotH), Qt::AlignCenter, QString::number(d));
        }

        // Sub-caption Bridge
        p.setFont(fSub);
        p.setPen(QColor(253, 230, 138));
        QString bridgeSub = QString("%1 ห้อง (%2%)").arg(_fillBars).arg(_fillVolume);
        if (_tempoMode == 2) bridgeSub += " • BPM Ramp";
        p.drawText(bridgeRect.adjusted(2, 70, -2, 0), Qt::AlignHCenter | Qt::AlignTop, bridgeSub);
    } else {
        p.setFont(fSub);
        p.setPen(QColor(209, 213, 219));
        p.drawText(QRect(bridgeRect.left(), y + 36, bridgeRect.width(), 20), Qt::AlignCenter, "ตัดเข้าทันที (Cut)");
        p.drawText(bridgeRect.adjusted(4, 72, -4, 0), Qt::AlignCenter | Qt::AlignTop, "ไม่ส่งกลอง");
    }

    // -------------------------------------------------------------
    // Downbeat Sync Marker Line & Tag (Cyan neon right at bridge exit)
    // -------------------------------------------------------------
    int syncX = bridgeRect.right();
    p.setPen(QPen(QColor(6, 182, 212, 230), 2.0, Qt::DashLine));
    p.drawLine(syncX, y + 2, syncX, y + h - 2);

    // Beat 1 Pill Badge: placed cleanly inside Deck B's header (top right)
    // Never collides with Bridge or border seams!
    if (_quantizeBar) {
        const QRect tagRect(deckBRect.right() - 54, y + 5, 46, 16);
        p.setBrush(QColor(6, 182, 212));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(tagRect, 3, 3);
        p.setPen(QColor(15, 23, 42));
        QFont fTag = p.font();
        fTag.setPointSize(7);
        fTag.setBold(true);
        p.setFont(fTag);
        p.drawText(tagRect, Qt::AlignCenter, "BEAT 1");
    }

    // -------------------------------------------------------------
    // 3. DECK B (Next Song Intro)
    // -------------------------------------------------------------
    QLinearGradient gradB(deckBRect.left(), 0, deckBRect.right(), 0);
    gradB.setColorAt(0.0, QColor(6, 95, 70, 150));   // Dark Teal
    gradB.setColorAt(1.0, QColor(4, 120, 87, 180));  // Dark Emerald
    p.setPen(QPen(QColor(16, 185, 129, 130), 1));
    p.setBrush(gradB);
    p.drawRoundedRect(deckBRect.adjusted(4, 0, 0, 0), 6, 6);

    // Header B
    p.setFont(fHeader);
    p.setPen(QColor(110, 231, 183)); // Soft emerald
    p.drawText(deckBRect.adjusted(12, 5, -8, 0), Qt::AlignLeft | Qt::AlignTop, "เพลงถัดไป (Deck B)");

    // Waveform bars inside Deck B (rising up from left to right)
    p.setPen(QPen(QColor(52, 211, 153, 190), 2.5, Qt::SolidLine, Qt::RoundCap));
    const int numBarsB = 10;
    for (int i = 0; i < numBarsB; ++i) {
        int barX = deckBRect.left() + 16 + i * (deckBRect.width() - 28) / (numBarsB - 1);
        double grow = 0.45 + (i / double(numBarsB)) * 0.55;
        int barH = int(barAreaMaxH * grow);
        p.drawLine(barX, barAreaMid - barH, barX, barAreaMid + barH);
    }

    // Sub-caption B
    p.setFont(fSub);
    p.setPen(QColor(167, 243, 208));
    QString bInfo;
    if (!_skipIntro) {
        bInfo = "เล่นตั้งแต่หัวเพลง";
    } else if (_introBars == 0) {
        bInfo = "เข้าท่อนร้องทันที";
    } else {
        bInfo = QString("เข้าก่อนร้อง %1 ห้อง").arg(_introBars);
    }
    if (_quantizeBar) bInfo += " (Beat 1)";
    p.drawText(deckBRect.adjusted(10, 72, -6, 0), Qt::AlignLeft | Qt::AlignTop, bInfo);
}

void TransitionPreviewWidget::drawAudioCrossfade(QPainter &p, const QRect &r)
{
    const int w = r.width();
    const int h = r.height();
    const int y = r.top();

    QPainterPath pathA;
    QPainterPath pathB;

    const int curveSteps = 50;
    const double pi_2 = 3.14159265358979323846 / 2.0;

    const int graphTop = y + 24;
    const int graphBottom = y + h - 8;
    const int graphHeight = graphBottom - graphTop;

    for (int i = 0; i <= curveSteps; ++i) {
        double t = double(i) / curveSteps;
        int px = r.left() + int(t * w);

        double gainA = 0.0;
        double gainB = 0.0;

        if (_crossfadeCurve == 0) {
            gainA = std::cos(t * pi_2);
            gainB = std::sin(t * pi_2);
        } else {
            gainA = 1.0 - t;
            gainB = t;
        }

        int pyA = graphBottom - int(gainA * graphHeight);
        int pyB = graphBottom - int(gainB * graphHeight);

        if (i == 0) {
            pathA.moveTo(px, pyA);
            pathB.moveTo(px, pyB);
        } else {
            pathA.lineTo(px, pyA);
            pathB.lineTo(px, pyB);
        }
    }

    // Curve gradient fills
    QPainterPath fillA = pathA;
    fillA.lineTo(r.right(), graphBottom);
    fillA.lineTo(r.left(), graphBottom);
    fillA.closeSubpath();
    QLinearGradient gradCurveA(0, graphTop, 0, graphBottom);
    gradCurveA.setColorAt(0.0, QColor(244, 114, 182, 80));
    gradCurveA.setColorAt(1.0, QColor(244, 114, 182, 5));
    p.fillPath(fillA, gradCurveA);

    QPainterPath fillB = pathB;
    fillB.lineTo(r.right(), graphBottom);
    fillB.lineTo(r.left(), graphBottom);
    fillB.closeSubpath();
    QLinearGradient gradCurveB(0, graphTop, 0, graphBottom);
    gradCurveB.setColorAt(0.0, QColor(52, 211, 153, 80));
    gradCurveB.setColorAt(1.0, QColor(52, 211, 153, 5));
    p.fillPath(fillB, gradCurveB);

    // Curve lines
    p.setPen(QPen(QColor(244, 114, 182), 2.5)); // Magenta
    p.drawPath(pathA);

    p.setPen(QPen(QColor(52, 211, 153), 2.5));  // Green
    p.drawPath(pathB);

    // Labels
    QFont fHeader = p.font();
    fHeader.setPointSize(9);
    fHeader.setBold(true);
    p.setFont(fHeader);

    p.setPen(QColor(244, 114, 182));
    p.drawText(r.adjusted(12, 5, 0, 0), Qt::AlignLeft | Qt::AlignTop, "Deck A เฟดออก (Fade Out)");

    p.setPen(QColor(52, 211, 153));
    p.drawText(r.adjusted(0, 5, -12, 0), Qt::AlignRight | Qt::AlignTop, "Deck B เฟดเข้า (Fade In)");

    // Center badge placed at bottom center so it never collides with Deck A/B headers
    QFont fSub = p.font();
    fSub.setPointSize(8);
    fSub.setBold(false);
    p.setFont(fSub);
    p.setPen(QColor(229, 231, 235));
    QString curveName = (_crossfadeCurve == 0) ? "Equal-Power (Cosine)" : "Linear (เชิงเส้น)";
    QString centerText = QString("Crossfade ซ้อน %1 วินาที • %2").arg(_crossfadeSec, 0, 'f', 1).arg(curveName);
    p.drawText(r.adjusted(0, 0, 0, -5), Qt::AlignHCenter | Qt::AlignBottom, centerText);
}

void TransitionPreviewWidget::drawStatusFooter(QPainter &p, const QRect &r)
{
    p.setPen(QPen(QColor(48, 54, 61), 1.0));
    p.setBrush(QColor(10, 14, 23));
    p.drawRoundedRect(r, 4, 4);

    QFont font = p.font();
    font.setPointSize(8);
    p.setFont(font);
    p.setPen(QColor(156, 163, 175));

    QString syncStr = _quantizeBar ? "ล็อกห้อง: เปิด" : "ล็อกห้อง: ปิด";
    QString drumStr = (_fillBars > 0) ? QString("ลูกส่ง: %1 ห้อง (%2%)").arg(_fillBars).arg(_fillVolume) : "ลูกส่ง: ปิด";
    QString tempoStr = (_tempoMode == 0) ? (_medleyBpm > 0 ? QString("ความเร็ว: ล็อก %1 BPM").arg(_medleyBpm) : "ความเร็ว: ตามเพลง")
                     : (_tempoMode == 1) ? "ความเร็ว: ตามต้นฉบับ" : "ความเร็ว: Ramp เชื่อมห้อง";
    QString fadeStr = QString("Audio เฟด: %1s").arg(_crossfadeSec, 0, 'f', 1);

    QString summary = QString("[ %1 ]   •   [ %2 ]   •   [ %3 ]   •   [ %4 ]")
                          .arg(syncStr)
                          .arg(drumStr)
                          .arg(tempoStr)
                          .arg(fadeStr);

    p.drawText(r, Qt::AlignCenter, summary);
}
