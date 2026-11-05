#pragma once

#include <QObject>
#include <QPalette>
#include <QString>

namespace disktool::app {

enum class ThemeMode {
    System,
    Light,
    Dark,
};

/// Application-wide light/dark/system theme selection.
///
/// The choice is stored in QSettings (INI) under
/// QStandardPaths::AppConfigLocation so it survives restarts. Light and Dark
/// use the Fusion style with an explicit palette; System restores the style and
/// palette captured at startup.
class Theme final : public QObject
{
    Q_OBJECT

public:
    static Theme& instance();

    void initialize();
    void setMode(ThemeMode mode);

    [[nodiscard]] ThemeMode mode() const { return m_mode; }
    [[nodiscard]] bool isDark() const;
    [[nodiscard]] const QString& settingsPath() const { return m_settingsPath; }

signals:
    void changed(disktool::app::ThemeMode mode);

private:
    explicit Theme(QObject* parent = nullptr);

    void apply(ThemeMode mode, bool persist);
    static QString modeKey(ThemeMode mode);
    static ThemeMode modeFromKey(const QString& key);
    static QPalette lightPalette();
    static QPalette darkPalette();

    QString m_settingsPath;
    QString m_originalStyleName;
    QPalette m_originalPalette;
    ThemeMode m_mode = ThemeMode::System;
    bool m_initialized = false;
};

}  // namespace disktool::app
