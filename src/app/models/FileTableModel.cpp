#include "models/FileTableModel.h"

#include "FormatUtils.h"

#include <QDateTime>
#include <QFileInfo>
#include <QString>

#include <chrono>
#include <utility>

namespace fs = std::filesystem;

namespace {

QString pathToQString(const fs::path& path)
{
    const auto text = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()),
                             static_cast<qsizetype>(text.size()));
}

std::chrono::system_clock::time_point toSystemClock(fs::file_time_type time)
{
    // Portable epoch-offset conversion: file_clock has a different epoch on
    // some implementations (notably libstdc++), so the difference between the
    // two clocks "now" is used instead of relying on clock_cast being present.
    const auto offset = std::chrono::system_clock::now().time_since_epoch()
        - fs::file_time_type::clock::now().time_since_epoch();

    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            time.time_since_epoch() + offset));
}

qint64 toEpochSeconds(fs::file_time_type time)
{
    if (time == fs::file_time_type{}) {
        return 0;
    }

    const auto systemTime = toSystemClock(time);
    return std::chrono::duration_cast<std::chrono::seconds>(
               systemTime.time_since_epoch())
        .count();
}

QString formatModified(fs::file_time_type time)
{
    if (time == fs::file_time_type{}) {
        return QStringLiteral("—");
    }

    return QDateTime::fromSecsSinceEpoch(toEpochSeconds(time))
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

}  // namespace

FileTableModel::FileTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int FileTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_entries.size());
}

int FileTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return ColumnCount;
}

QVariant FileTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || static_cast<std::size_t>(index.row()) >= m_entries.size()) {
        return {};
    }

    const auto& entry = m_entries[static_cast<std::size_t>(index.row())];
    const auto column = static_cast<Column>(index.column());

    if (role == Qt::DisplayRole) {
        switch (column) {
        case NameColumn: {
            const auto name = entry.path.filename();
            return pathToQString(name.empty() ? entry.path : name);
        }
        case SizeColumn:
            return entry.isDirectory ? QStringLiteral("—")
                                     : disktool::app::formatBytes(entry.size);
        case ModifiedColumn:
            return formatModified(entry.modified);
        case TypeColumn:
            return entry.isDirectory ? tr("Папка") : tr("Файл");
        case FullPathColumn:
            return pathToQString(entry.path);
        case ColumnCount:
            break;
        }
    }

    if (role == Qt::UserRole) {
        // Used by QSortFilterProxyModel::setSortRole. Numeric columns must not
        // be sorted by their formatted display text.
        switch (column) {
        case NameColumn: {
            const auto name = entry.path.filename();
            return pathToQString(name.empty() ? entry.path : name);
        }
        case SizeColumn:
            return QVariant::fromValue<qulonglong>(
                entry.isDirectory ? 0 : entry.size);
        case ModifiedColumn:
            return QVariant::fromValue<qlonglong>(toEpochSeconds(entry.modified));
        case TypeColumn:
            return entry.isDirectory ? 1 : 0;
        case FullPathColumn:
            return pathToQString(entry.path);
        case ColumnCount:
            break;
        }
    }

    if (role == Qt::DecorationRole && column == NameColumn) {
        return m_iconProvider.icon(QFileInfo(pathToQString(entry.path)));
    }

    if (role == Qt::ToolTipRole) {
        return pathToQString(entry.path);
    }

    if (role == Qt::TextAlignmentRole && column == SizeColumn) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }

    return {};
}

QVariant FileTableModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }

    switch (section) {
    case NameColumn:
        return tr("Имя");
    case SizeColumn:
        return tr("Размер");
    case ModifiedColumn:
        return tr("Изменён");
    case TypeColumn:
        return tr("Тип");
    case FullPathColumn:
        return tr("Полный путь");
    default:
        return {};
    }
}

void FileTableModel::setEntries(std::vector<disktool::core::FileEntry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
}

void FileTableModel::appendEntries(
    const std::vector<disktool::core::FileEntry>& entries)
{
    if (entries.empty()) {
        return;
    }

    const int first = static_cast<int>(m_entries.size());
    const int last = first + static_cast<int>(entries.size()) - 1;
    beginInsertRows(QModelIndex(), first, last);
    m_entries.insert(m_entries.end(), entries.begin(), entries.end());
    endInsertRows();
}

const disktool::core::FileEntry* FileTableModel::entryAt(int row) const
{
    if (row < 0 || static_cast<std::size_t>(row) >= m_entries.size()) {
        return nullptr;
    }
    return &m_entries[static_cast<std::size_t>(row)];
}

void FileTableModel::clearEntries()
{
    setEntries({});
}
