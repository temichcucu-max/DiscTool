#include "Theme.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleHints>

namespace disktool::app {

Theme::Theme(QObject* parent)
    : QObject(parent)
{
    QString directory = QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation);
    if (directory.isEmpty()) {
        directory = QDir::homePath() + QStringLiteral("/.disktool");
    }
    QDir().mkpath(directory);
    m_settingsPath = QDir(directory).filePath(QStringLiteral("settings.ini"));
}

Theme& Theme::instance()
{
    static Theme theme;
    return theme;
}

void Theme::initialize()
{
    if (m_initialized) {
        return;
    }

    m_originalStyleName = QApplication::style() != nullptr
        ? QApplication::style()->objectName()
        : QString();
    m_originalPalette = QApplication::palette();
    m_initialized = true;

    QSettings settings(m_settingsPath, QSettings::IniFormat);
    const auto stored = settings.value(QStringLiteral("theme"),
                                       QStringLiteral("system"))
                            .toString();
    apply(modeFromKey(stored), false);
}

void Theme::setMode(ThemeMode mode)
{
    apply(mode, true);
}

void Theme::apply(ThemeMode mode, bool persist)
{
    m_mode = mode;

    switch (mode) {
    case ThemeMode::System:
        // Qt 6.8+ can ask the platform for the system scheme directly. Older
        // releases (including the 6.7 build used by this project) have only
        // the colorScheme() getter, so restore the style/palette captured at
        // startup instead.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Unknown);
#endif
        if (!m_originalStyleName.isEmpty()) {
            QApplication::setStyle(m_originalStyleName);
        }
        QApplication::setPalette(m_originalPalette);
        break;

    case ThemeMode::Light:
        QApplication::setStyle(QStringLiteral("Fusion"));
        QApplication::setPalette(lightPalette());
        break;

    case ThemeMode::Dark:
        QApplication::setStyle(QStringLiteral("Fusion"));
        QApplication::setPalette(darkPalette());
        break;
    }

    if (persist) {
        QSettings settings(m_settingsPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("theme"), modeKey(mode));
        settings.sync();
    }

    emit changed(mode);
}

bool Theme::isDark() const
{
    if (m_mode == ThemeMode::Dark) {
        return true;
    }
    if (m_mode == ThemeMode::Light) {
        return false;
    }

    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme == Qt::ColorScheme::Dark) {
        return true;
    }
    if (scheme == Qt::ColorScheme::Light) {
        return false;
    }

    return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

QString Theme::modeKey(ThemeMode mode)
{
    switch (mode) {
    case ThemeMode::Light:
        return QStringLiteral("light");
    case ThemeMode::Dark:
        return QStringLiteral("dark");
    case ThemeMode::System:
        break;
    }
    return QStringLiteral("system");
}

ThemeMode Theme::modeFromKey(const QString& key)
{
    if (key.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0) {
        return ThemeMode::Light;
    }
    if (key.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
        return ThemeMode::Dark;
    }
    return ThemeMode::System;
}

QPalette Theme::lightPalette()
{
    const QColor window(245, 245, 245);
    const QColor windowText(26, 26, 26);
    const QColor base(255, 255, 255);
    const QColor alternate(244, 244, 244);
    const QColor button(240, 240, 240);
    const QColor highlight(47, 128, 237);
    const QColor disabled(154, 154, 154);

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, windowText);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, alternate);
    palette.setColor(QPalette::ToolTipBase, QColor(255, 255, 220));
    palette.setColor(QPalette::ToolTipText, windowText);
    palette.setColor(QPalette::Text, windowText);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, windowText);
    palette.setColor(QPalette::BrightText, QColor(200, 0, 0));
    palette.setColor(QPalette::Link, QColor(0, 102, 204));
    palette.setColor(QPalette::LinkVisited, QColor(128, 0, 128));
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::PlaceholderText, QColor(128, 128, 128));

    palette.setColor(QPalette::Light, QColor(255, 255, 255));
    palette.setColor(QPalette::Midlight, QColor(234, 234, 234));
    palette.setColor(QPalette::Mid, QColor(208, 208, 208));
    palette.setColor(QPalette::Dark, QColor(160, 160, 160));
    palette.setColor(QPalette::Shadow, QColor(112, 112, 112));

    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight,
                     QColor(180, 180, 180));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                     QColor(255, 255, 255));

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    palette.setColor(QPalette::Accent, highlight);
#endif

    return palette;
}

QPalette Theme::darkPalette()
{
    const QColor window(43, 43, 43);
    const QColor windowText(230, 230, 230);
    const QColor base(30, 30, 30);
    const QColor alternate(42, 42, 42);
    const QColor button(53, 53, 53);
    const QColor highlight(47, 128, 237);
    const QColor disabled(122, 122, 122);

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, windowText);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, alternate);
    palette.setColor(QPalette::ToolTipBase, QColor(28, 28, 28));
    palette.setColor(QPalette::ToolTipText, windowText);
    palette.setColor(QPalette::Text, windowText);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, windowText);
    palette.setColor(QPalette::BrightText, QColor(255, 120, 120));
    palette.setColor(QPalette::Link, QColor(96, 165, 250));
    palette.setColor(QPalette::LinkVisited, QColor(192, 132, 252));
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::PlaceholderText, QColor(156, 163, 175));

    // Light/Mid/Dark/Shadow drive Fusion's borders and scrollbars.
    palette.setColor(QPalette::Light, QColor(74, 74, 74));
    palette.setColor(QPalette::Midlight, QColor(61, 61, 61));
    palette.setColor(QPalette::Mid, QColor(42, 42, 42));
    palette.setColor(QPalette::Dark, QColor(21, 21, 21));
    palette.setColor(QPalette::Shadow, QColor(13, 13, 13));

    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight,
                     QColor(68, 68, 68));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                     QColor(154, 154, 154));

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    palette.setColor(QPalette::Accent, highlight);
#endif

    return palette;
}

}  // namespace disktool::app
