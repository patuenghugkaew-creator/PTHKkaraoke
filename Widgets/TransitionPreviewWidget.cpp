#include "TransitionPreviewWidget.h"
#include <QPaintEvent>

TransitionPreviewWidget::TransitionPreviewWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(130);
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
                                             int activeTab)
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

    update();
}

void TransitionPreviewWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QRect fullRect = rect().adjusted(2, 2, -2, -2);

    // Background container
    QLinearGradient bgGrad(0, 0, 0, height());
    bgGrad.setColorAt(0.0, QColor(22, 27, 34));
    bgGrad.setColorAt(1.0, QColor(13, 17, 23));
    p.setPen(QPen(QColor(48, 54, 61), 1.5));
    p.setBrush(bgGrad);
    p.drawRoundedRect(fullRect, 8, 8);

    // Partition rects: Upper visualization (approx 72%) and Lower footer badge (approx 28%)
    const int footerH = 26;
    const QRect visRect(fullRect.left() + 8, fullRect.top() + 6, fullRect.width() - 16, fullRect.height() - footerH - 10);
    const QRect footRect(fullRect.left() + 8, fullRect.bottom() - footerH, fullRect.width() - 16, footerH - 2);

    if (_activeTab == 2) {
        // Tab 3: Audio Crossfade visualization
        drawAudioCrossfade(p, visRect);
    } else {
        // Tabs 1 & 2: MIDI Transition & Drum Fill visualization
        drawMidiTransition(p, visRect);
    }

    drawStatusFooter(p, footRect);
}

void TransitionPreviewWidget::drawMidiTransition(QPainter &p, const QRect &r)
{
    const int w = r.width();
    const int h = r.height();
    const int y = r.top();

    // Visual zones:
    // Deck A (Outro): ~38% of width
    // Transition Bridge: ~24% of width
    // Deck B (Intro): ~38% of width
    const int aWidth = int(w * 0.38);
    const int bridgeWidth = int(w * 0.24);
    const int bWidth = w - aWidth - bridgeWidth;

    const QRect deckARect(r.left(), y, aWidth, h);
    const QRect bridgeRect(r.left() + aWidth, y, bridgeWidth, h);
    const QRect deckBRect(r.left() + aWidth + bridgeWidth, y, bWidth, h);

    // 1. Deck A Track (Purple / Blue gradient)
    QLinearGradient gradA(deckARect.left(), 0, deckARect.right(), 0);
    gradA.setColorAt(0.0, QColor(37, 99, 235, 200));   // Blue
    gradA.setColorAt(1.0, QColor(124, 58, 237, 180));  // Purple
    p.setPen(QPen(QColor(147, 197, 253, 160), 1));
    p.setBrush(gradA);
    p.drawRoundedRect(deckARect.adjusted(0, 4, -4, -4), 6, 6);

    // Simulated waveform bars inside Deck A
    p.setPen(QPen(QColor(255, 255, 255, 70), 2));
    const int numBarsA = 12;
    for (int i = 0; i < numBarsA; ++i) {
        int barX = deckARect.left() + 10 + i * (deckARect.width() - 25) / numBarsA;
        double factor = 1.0 - (i / double(numBarsA)) * 0.45;
        int barH = int((h * 0.28) * factor);
        int midY = y + h / 2 + 6;
        p.drawLine(barX, midY - barH, barX, midY + barH);
    }

    // Deck A Labels
    p.setPen(QColor(255, 255, 255, 230));
    QFont fontH = p.font();
    fontH.setPointSize(9);
    fontH.setBold(true);
    p.setFont(fontH);
    p.drawText(deckARect.adjusted(10, 8, -10, 0), Qt::AlignLeft | Qt::AlignTop, "เพลงเดิม (Deck A)");

    QFont fontSub = p.font();
    fontSub.setPointSize(8);
    fontSub.setBold(false);
    p.setFont(fontSub);
    QString aInfo = _autoCutOutro ? QString("จบคำร้อง + รอ %1 ห้อง").arg(_waitBars) : "เล่นเต็มเพลง";
    p.drawText(deckARect.adjusted(10, 24, -10, 0), Qt::AlignLeft | Qt::AlignTop, aInfo);

    // 2. Transition Bridge (Amber / Orange glow)
    const bool hasBridge = (_fillBars > 0);
    QLinearGradient gradBridge(bridgeRect.left(), 0, bridgeRect.right(), 0);
    if (hasBridge) {
        gradBridge.setColorAt(0.0, QColor(217, 119, 6, 210));  // Amber dark
        gradBridge.setColorAt(1.0, QColor(245, 158, 11, 230)); // Gold
        p.setPen(QPen(QColor(251, 191, 36), 1.5));
    } else {
        gradBridge.setColorAt(0.0, QColor(75, 85, 99, 140));
        gradBridge.setColorAt(1.0, QColor(107, 114, 128, 140));
        p.setPen(QPen(QColor(156, 163, 175), 1));
    }
    p.setBrush(gradBridge);
    p.drawRoundedRect(bridgeRect.adjusted(2, 4, -2, -4), 6, 6);

    // Bridge Labels & Drum info
    p.setFont(fontH);
    p.setPen(QColor(255, 255, 255));
    if (hasBridge) {
        QString patStr = "ลูกส่งมาตรฐาน";
        switch (_fillPattern) {
        case 1: patStr = "ลูกส่งสามช่า"; break;
        case 2: patStr = "เคาะจังหวะนำ"; break;
        case 3: patStr = "ทอมไล่เสียง"; break;
        case 4: patStr = "สุ่มตามจังหวะ"; break;
        default: break;
        }
        p.drawText(bridgeRect.adjusted(4, 8, -4, 0), Qt::AlignCenter | Qt::AlignTop, patStr);

        p.setFont(fontSub);
        QString drumDetail = QString("%1 ห้อง (%2%)").arg(_fillBars).arg(_fillVolume);
        if (_tempoMode == 2) drumDetail += " ~ Ramp";
        p.drawText(bridgeRect.adjusted(4, 25, -4, 0), Qt::AlignCenter | Qt::AlignTop, drumDetail);

        // Drum stick visual dots in the bridge
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 220));
        int dotY = y + h / 2 + 12;
        int dotSpacing = bridgeRect.width() / 5;
        for (int d = 1; d <= 4; ++d) {
            p.drawEllipse(bridgeRect.left() + d * dotSpacing - 3, dotY - 3, 6, 6);
        }
    } else {
        p.drawText(bridgeRect, Qt::AlignCenter, "ต่อทันที (Cut)");
    }

    // Downbeat Sync Marker line (Cyan neon right at bridge exit)
    int syncX = bridgeRect.right();
    p.setPen(QPen(QColor(6, 182, 212, 240), 2.5, Qt::DashLine));
    p.drawLine(syncX, y + 2, syncX, y + h - 2);

    // 3. Deck B Track (Teal / Emerald gradient)
    QLinearGradient gradB(deckBRect.left(), 0, deckBRect.right(), 0);
    gradB.setColorAt(0.0, QColor(13, 148, 136, 180)); // Teal
    gradB.setColorAt(1.0, QColor(16, 185, 129, 210)); // Emerald
    p.setPen(QPen(QColor(110, 231, 183, 160), 1));
    p.setBrush(gradB);
    p.drawRoundedRect(deckBRect.adjusted(2, 4, 0, -4), 6, 6);

    // Simulated waveform bars inside Deck B
    p.setPen(QPen(QColor(255, 255, 255, 70), 2));
    const int numBarsB = 12;
    for (int i = 0; i < numBarsB; ++i) {
        int barX = deckBRect.left() + 10 + i * (deckBRect.width() - 25) / numBarsB;
        double factor = 0.5 + (i / double(numBarsB)) * 0.5;
        int barH = int((h * 0.28) * factor);
        int midY = y + h / 2 + 6;
        p.drawLine(barX, midY - barH, barX, midY + barH);
    }

    // Deck B Labels
    p.setFont(fontH);
    p.setPen(QColor(255, 255, 255, 230));
    p.drawText(deckBRect.adjusted(10, 8, -10, 0), Qt::AlignLeft | Qt::AlignTop, "เพลงถัดไป (Deck B)");

    p.setFont(fontSub);
    QString bInfo = _skipIntro ? "ข้ามอินโทร -> เข้าท่อนดนตรี" : "เล่นตั้งแต่หัวเพลง";
    if (_quantizeBar) bInfo += " (Beat 1)";
    p.drawText(deckBRect.adjusted(10, 24, -10, 0), Qt::AlignLeft | Qt::AlignTop, bInfo);

    // Downbeat crash cymbal / kick marker tag
    p.setBrush(QColor(6, 182, 212, 220));
    p.setPen(Qt::NoPen);
    const QRect tagRect(syncX - 22, y + h - 18, 44, 16);
    p.drawRoundedRect(tagRect, 4, 4);
    p.setPen(QColor(15, 23, 42));
    QFont fontTag = p.font();
    fontTag.setPointSize(7);
    fontTag.setBold(true);
    p.setFont(fontTag);
    p.drawText(tagRect, Qt::AlignCenter, "BEAT 1");
}

void TransitionPreviewWidget::drawAudioCrossfade(QPainter &p, const QRect &r)
{
    const int w = r.width();
    const int h = r.height();
    const int y = r.top();

    // Deck A fade-out curve (Neon Magenta / Pink) and Deck B fade-in curve (Neon Green / Cyan)
    QPainterPath pathA;
    QPainterPath pathB;

    const int curveSteps = 50;
    const double pi_2 = 3.14159265358979323846 / 2.0;

    const int graphTop = y + 18;
    const int graphBottom = y + h - 10;
    const int graphHeight = graphBottom - graphTop;

    for (int i = 0; i <= curveSteps; ++i) {
        double t = double(i) / curveSteps;
        int px = r.left() + int(t * w);

        double gainA = 0.0;
        double gainB = 0.0;

        if (_crossfadeCurve == 0) {
            // Equal-Power Cosine curve
            gainA = std::cos(t * pi_2);
            gainB = std::sin(t * pi_2);
        } else {
            // Linear curve
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

    // Draw curve fills
    QPainterPath fillA = pathA;
    fillA.lineTo(r.right(), graphBottom);
    fillA.lineTo(r.left(), graphBottom);
    fillA.closeSubpath();
    QLinearGradient gradCurveA(0, graphTop, 0, graphBottom);
    gradCurveA.setColorAt(0.0, QColor(236, 72, 153, 90));
    gradCurveA.setColorAt(1.0, QColor(236, 72, 153, 10));
    p.fillPath(fillA, gradCurveA);

    QPainterPath fillB = pathB;
    fillB.lineTo(r.right(), graphBottom);
    fillB.lineTo(r.left(), graphBottom);
    fillB.closeSubpath();
    QLinearGradient gradCurveB(0, graphTop, 0, graphBottom);
    gradCurveB.setColorAt(0.0, QColor(16, 185, 129, 90));
    gradCurveB.setColorAt(1.0, QColor(16, 185, 129, 10));
    p.fillPath(fillB, gradCurveB);

    // Draw lines
    p.setPen(QPen(QColor(236, 72, 153), 2.5)); // Pink / Magenta
    p.drawPath(pathA);

    p.setPen(QPen(QColor(16, 185, 129), 2.5)); // Emerald
    p.drawPath(pathB);

    // Labels
    QFont fontH = p.font();
    fontH.setPointSize(9);
    fontH.setBold(true);
    p.setFont(fontH);

    p.setPen(QColor(244, 114, 182));
    p.drawText(r.adjusted(12, 4, 0, 0), Qt::AlignLeft | Qt::AlignTop, "Deck A เฟดออก (Fade Out)");

    p.setPen(QColor(52, 211, 153));
    p.drawText(r.adjusted(0, 4, -12, 0), Qt::AlignRight | Qt::AlignTop, "Deck B เฟดเข้า (Fade In)");

    // Center badge
    QFont fontSub = p.font();
    fontSub.setPointSize(8);
    fontSub.setBold(false);
    p.setFont(fontSub);
    p.setPen(QColor(229, 231, 235));
    QString curveName = (_crossfadeCurve == 0) ? "Equal-Power (Cosine)" : "Linear (เชิงเส้น)";
    QString centerText = QString("Crossfade ซ้อน %1 วินาที • %2").arg(_crossfadeSec, 0, 'f', 1).arg(curveName);
    p.drawText(r.adjusted(0, 4, 0, 0), Qt::AlignHCenter | Qt::AlignTop, centerText);
}

void TransitionPreviewWidget::drawStatusFooter(QPainter &p, const QRect &r)
{
    // Footer container
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(17, 24, 39, 210));
    p.drawRoundedRect(r, 4, 4);

    QFont font = p.font();
    font.setPointSize(8);
    p.setFont(font);
    p.setPen(QColor(156, 163, 175));

    QString syncStr = _quantizeBar ? "ล็อกห้องเพลง (Bar Sync)" : "ตัดทันที";
    QString tempoStr = (_tempoMode == 0) ? QString("ล็อก %1 BPM").arg(_medleyBpm)
                     : (_tempoMode == 1) ? "เปลี่ยน BPM ทันที" : "BPM Ramp เชื่อมจังหวะ";
    QString drumStr = (_fillBars > 0) ? QString("ลูกส่ง %1 ห้อง").arg(_fillBars) : "ไม่ส่งกลอง";
    QString fadeStr = QString("Audio เฟด %1s").arg(_crossfadeSec, 0, 'f', 1);

    QString summary = QString("⚡ %1   |   🥁 %2   |   ⏱ %3   |   🎚 %4")
                          .arg(syncStr)
                          .arg(drumStr)
                          .arg(tempoStr)
                          .arg(fadeStr);

    p.drawText(r, Qt::AlignCenter, summary);
}
