#include "Theme.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QList>
#include <QPair>
#include <QPalette>
#include <QSettings>
#include <QString>
#include <QStyleFactory>
#include <QTimer>

#include "Config.h"

namespace Theme {

namespace {

struct Colors {
    QString window, base, altBase, button, buttonHover, border, border2;
    QString text, dim, dimTab, accent, accentText, title, handle;
    QString light, midlight, mid, dark, shadow;
};

Mode g_mode = Dark;
bool g_dark = true;
bool g_orange = false;
QTimer *g_watch = nullptr;

Colors darkColors()
{
    Colors c;
    c.window = "#101114";  c.base = "#17181c";  c.altBase = "#1d1f24";
    c.button = "#23252c";  c.buttonHover = "#2e3139";
    c.border = "#4f5463";  c.border2 = "#363a46";
    c.text = "#f4f6fa";    c.dim = "#8a90a0";   c.dimTab = "#b8bece";
    c.accent = "#0a8aa6";  c.accentText = "#ffffff";
    c.title = "#22d3ee";   c.handle = "#3a3d46";
    c.light = "#30333b";   c.midlight = "#2a2d35"; c.mid = "#4a4e5a";
    c.dark = "#0b0b0d";    c.shadow = "#6b7080";
    return c;
}

Colors orangeColors()
{
    Colors c;
    c.window = "#15110d";  c.base = "#1b1611";  c.altBase = "#221b14";
    c.button = "#2c2218";  c.buttonHover = "#3b2c1c";
    c.border = "#6e553a";  c.border2 = "#483829";
    c.text = "#fff3e3";    c.dim = "#a89279";   c.dimTab = "#dcc5a6";
    c.accent = "#e8730c";  c.accentText = "#ffffff";
    c.title = "#ff9a2e";   c.handle = "#4b3a29";
    c.light = "#3b2e21";   c.midlight = "#31261b"; c.mid = "#5c4731";
    c.dark = "#0d0a07";    c.shadow = "#7d6850";
    return c;
}

Colors lightColors()
{
    Colors c;
    c.window = "#f2f3f7";  c.base = "#ffffff";  c.altBase = "#f6f7fa";
    c.button = "#e9ebf1";  c.buttonHover = "#dfe2ea";
    c.border = "#9097a6";  c.border2 = "#bcc1cd";
    c.text = "#0f1015";    c.dim = "#6a7080";   c.dimTab = "#3b4150";
    c.accent = "#0a8aa6";  c.accentText = "#ffffff";
    c.title = "#08738c";   c.handle = "#c3c7d2";
    c.light = "#ffffff";   c.midlight = "#eceef3"; c.mid = "#b8bcc6";
    c.dark = "#a6abb8";    c.shadow = "#7d8290";
    return c;
}

bool systemIsDark()
{
#ifdef Q_OS_WIN
    QSettings s("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                QSettings::NativeFormat);
    return s.value("AppsUseLightTheme", 1).toInt() == 0;
#else
    return false;
#endif
}

// Bold chevron for combo-box drop-down buttons (the default arrow is too faint on dark).
// Written once per colour to the temp folder and referenced from the style sheet.
QString arrowImage(const QString &colorName, const QString &fileTag)
{
    const QString dir = QDir::tempPath() + "/PTHKKaraokeTheme";
    QDir().mkpath(dir);
    const QString file = dir + "/combo-arrow-" + fileTag + ".png";

    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    {
        QPainter g(&pm);
        g.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor(colorName), 2.6);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        g.setPen(pen);
        g.drawPolyline(QPolygonF() << QPointF(3.2, 5.6) << QPointF(8.0, 10.4) << QPointF(12.8, 5.6));
    }
    pm.save(file, "PNG");
    return file;
}

QString buildStyleSheet(const Colors &c)
{
    QString css =
        "QToolTip { background-color: @btn; color: @text; border: 1px solid @border; padding: 4px; }"
        "QMenu { background-color: @base; color: @text; border: 1px solid @border2; border-radius: 10px; padding: 6px; }"
        "QMenu::item { padding: 7px 28px 7px 22px; border-radius: 5px; }"
        "QMenu::item:selected { background-color: @accent; color: @accentText; }"
        "QMenu::item:disabled { color: @dim; }"
        "QMenu::separator { height: 1px; background: @border2; margin: 5px 8px; }"
        "QPushButton { background-color: @btn; color: @text; border: 1px solid @border; border-radius: 6px; }"
        "QPushButton:hover { background-color: @btnHover; border-color: @accent; }"
        "QPushButton:pressed { background-color: @accent; color: @accentText; }"
        "QPushButton:checked { background-color: @accent; color: @accentText; border-color: @accent; }"
        "QPushButton:disabled { color: @dim; }"
        "QToolButton { background-color: @btn; color: @dimtab; border: 1px solid @border; border-radius: 5px; padding: 2px 4px; }"
        "QToolButton:hover { background-color: @btnHover; color: @text; border-color: @accent; }"
        "QToolButton:checked { background-color: @accent; color: @accentText; font-weight: bold; border: 2px solid @title; }"
        "QToolButton:checked:hover { background-color: @accent; color: @accentText; }"
        "QToolButton:disabled { color: @dim; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background-color: @base; color: @text;"
        "  border: 1px solid @border; border-radius: 6px; padding: 3px 6px; selection-background-color: @accent; }"
        "InstCh QSpinBox, InstCh QDoubleSpinBox, ChMx QSpinBox, ChMx QDoubleSpinBox { padding: 0px 10px 0px 2px; font-size: 8pt; font-weight: normal; min-width: 0px; }"
        "QSpinBox::up-button, QDoubleSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::down-button { width: 9px; border: none; background: transparent; }"
        "QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: @accent; }"
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 24px;"
        "  border: none; border-left: 1px solid @border; background: transparent; }"
        "QComboBox::drop-down:hover { background-color: @btnHover; }"
        "QComboBox::down-arrow { image: url(\"@arrowNormal\"); width: 16px; height: 16px; }"
        "QComboBox::down-arrow:hover, QComboBox::down-arrow:on { image: url(\"@arrowHover\"); }"
        "QComboBox QAbstractItemView { background-color: @base; color: @text;"
        "  selection-background-color: @accent; selection-color: @accentText; border: 1px solid @border; }"
        "QGroupBox { border: 1px solid @border2; border-radius: 8px; margin-top: 10px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: @title; }"
        "QTabWidget::pane { border: 1px solid @border2; border-radius: 6px; }"
        "QTabBar::tab { background: @base; color: @dimtab; padding: 7px 16px; border: 1px solid @border2;"
        "  border-bottom: none; border-top-left-radius: 6px; border-top-right-radius: 6px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: @btn; color: @text; border-color: @accent; }"
        "QTabBar::tab:hover { color: @text; }"
        "QListWidget, QTreeWidget, QTableWidget, QListView, QTableView { background-color: @base; color: @text;"
        "  border: 1px solid @border2; border-radius: 6px; alternate-background-color: @altbase; }"
        "QHeaderView::section { background-color: @btn; color: @text; border: none; padding: 5px; }"
        "QSplitter::handle { background-color: @border2; }"
        "QScrollBar:horizontal { background: @base; height: 12px; margin: 0; border: none; }"
        "QScrollBar::handle:horizontal { background: @handle; border-radius: 5px; min-width: 30px; }"
        "QScrollBar::handle:horizontal:hover { background: @accent; }"
        "QScrollBar:vertical { background: @base; width: 12px; margin: 0; border: none; }"
        "QScrollBar::handle:vertical { background: @handle; border-radius: 5px; min-height: 30px; }"
        "QScrollBar::handle:vertical:hover { background: @accent; }"
        "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; background: none; border: none; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: none; }"
        "QSlider::groove:horizontal { height: 4px; background: @border2; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: @text; width: 14px; margin: -6px 0; border-radius: 7px; }"
        "QCheckBox, QRadioButton { spacing: 8px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid @dimtab; border-radius: 4px; background-color: @base; }"
        "QCheckBox::indicator:hover { border-color: @accent; }"
        "QCheckBox::indicator:checked { background-color: @accent; border-color: @accent; image: url(:/Icons/check_white.png); }"
        "QCheckBox::indicator:disabled { border-color: @border; background-color: @altbase; }"
        "QRadioButton::indicator { width: 14px; height: 14px; border: 1px solid @dimtab; border-radius: 8px; background-color: @base; }"
        "QRadioButton::indicator:hover { border-color: @accent; }"
        "QRadioButton::indicator:checked { border-color: @accent; background-color: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5,"
        "  stop:0 @accentText, stop:0.4 @accentText, stop:0.5 @accent, stop:1 @accent); }"
        "QRadioButton::indicator:disabled { border-color: @border; }";

    // longer tokens first so that e.g. @accentText is not eaten by @accent
    QList<QPair<QString, QString> > tokens;
    tokens << qMakePair(QString("@accentText"), c.accentText)
           << qMakePair(QString("@accent"), c.accent)
           << qMakePair(QString("@altbase"), c.altBase)
           << qMakePair(QString("@base"), c.base)
           << qMakePair(QString("@btnHover"), c.buttonHover)
           << qMakePair(QString("@btn"), c.button)
           << qMakePair(QString("@border2"), c.border2)
           << qMakePair(QString("@border"), c.border)
           << qMakePair(QString("@dimtab"), c.dimTab)
           << qMakePair(QString("@dim"), c.dim)
           << qMakePair(QString("@text"), c.text)
           << qMakePair(QString("@title"), c.title)
           << qMakePair(QString("@handle"), c.handle);
    for (int i = 0; i < tokens.count(); i++)
        css.replace(tokens[i].first, tokens[i].second);
    css.replace("@arrowNormal", arrowImage(c.text, "n" + c.text.mid(1)));
    css.replace("@arrowHover", arrowImage(c.accent, "h" + c.accent.mid(1)));
    return css;
}

void applyColors(bool dark, bool orange = false)
{
    const Colors c = orange ? orangeColors() : (dark ? darkColors() : lightColors());

    QPalette p;
    p.setColor(QPalette::Window, QColor(c.window));
    p.setColor(QPalette::WindowText, QColor(c.text));
    p.setColor(QPalette::Base, QColor(c.base));
    p.setColor(QPalette::AlternateBase, QColor(c.altBase));
    p.setColor(QPalette::ToolTipBase, QColor(c.button));
    p.setColor(QPalette::ToolTipText, QColor(c.text));
    p.setColor(QPalette::Text, QColor(c.text));
    p.setColor(QPalette::Button, QColor(c.button));
    p.setColor(QPalette::ButtonText, QColor(c.text));
    p.setColor(QPalette::BrightText, QColor("#ffffff"));
    p.setColor(QPalette::Link, QColor(c.title));
    p.setColor(QPalette::Highlight, QColor(c.accent));
    p.setColor(QPalette::HighlightedText, QColor(c.accentText));
    p.setColor(QPalette::Light, QColor(c.light));
    p.setColor(QPalette::Midlight, QColor(c.midlight));
    p.setColor(QPalette::Mid, QColor(c.mid));
    p.setColor(QPalette::Dark, QColor(c.dark));
    p.setColor(QPalette::Shadow, QColor(c.shadow));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(c.dim));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(c.dim));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(c.dim));

    g_dark = dark;
    g_orange = orange;
    qApp->setPalette(p);
    // Fusion draws a dial's tick marks with the Dark role (near-black in the dark theme = invisible on the
    // dark window): give every QDial a lighter one so the scale marks show in the FX windows too.
    {
        QPalette pd = p;
        pd.setColor(QPalette::Dark, orange ? QColor("#d9b98f") : (dark ? QColor("#aab1c2") : QColor(c.dark)));
        qApp->setPalette(pd, "QDial");
    }
    qApp->setStyleSheet(buildStyleSheet(c));
}

void applyCurrent()
{
    if (g_mode == DarkOrange) {
        applyColors(true, true);
        return;
    }
    bool dark = (g_mode == Dark) || (g_mode == System && systemIsDark());
    applyColors(dark);
}

}

void init(QApplication &app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    int m = st.value("Theme", static_cast<int>(Dark)).toInt();
    g_mode = (m == Dark) ? Dark : (m == Light) ? Light : (m == DarkOrange) ? DarkOrange : System;

    applyCurrent();

    // follow the Windows setting while the program is running
    g_watch = new QTimer(&app);
    g_watch->setInterval(2000);
    QObject::connect(g_watch, &QTimer::timeout, []() {
        if (g_mode == System && systemIsDark() != g_dark)
            applyCurrent();
    });
    g_watch->start();
}

Mode mode()
{
    return g_mode;
}

void setMode(Mode m)
{
    g_mode = m;

    QSettings st(Config::CONFIG_APP_FILE_PATH, QSettings::IniFormat);
    st.setValue("Theme", static_cast<int>(m));

    applyCurrent();
}

bool isDark()
{
    return g_dark;
}

bool isOrange()
{
    return g_orange;
}

}
