#pragma once

#include <QLocale>
#include <QString>

#include <cstdint>

namespace disktool::app {

/// Human-readable byte count: 512 B, 1.5 KB, 12.0 MB, 3.4 GB.
inline QString formatBytes(std::uintmax_t bytes)
{
    constexpr double step = 1024.0;

    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(
            QLocale().toString(static_cast<qulonglong>(bytes)));
    }

    static constexpr const char* units[] = {"KB", "MB", "GB", "TB", "PB"};

    double value = static_cast<double>(bytes);
    int unit = -1;
    do {
        value /= step;
        ++unit;
    } while (value >= step && unit < 4);

    return QStringLiteral("%1 %2").arg(
        QLocale().toString(value, 'f', 1),
        QString::fromLatin1(units[unit]));
}

}  // namespace disktool::app
