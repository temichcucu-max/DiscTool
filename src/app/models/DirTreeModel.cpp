#include "models/DirTreeModel.h"

#include "FormatUtils.h"

#include <QFileInfo>
#include <QLocale>
#include <QString>

#include <cstddef>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

QString pathToQString(const fs::path& path)
{
    const auto text = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()),
                             static_cast<qsizetype>(text.size()));
}

QString nameToQString(const std::string& name)
{
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

std::string pathToUtf8(const fs::path& path)
{
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

}  // namespace

DirTreeModel::DirTreeModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

QModelIndex DirTreeModel::index(int row, int column,
                                const QModelIndex& parent) const
{
    if (row < 0 || column < 0 || column >= ColumnCount || !m_root) {
        return {};
    }

    if (!parent.isValid()) {
        if (row != 0) {
            return {};
        }
        return createIndex(0, column, m_root.get());
    }

    if (parent.column() != 0) {
        return {};
    }

    const auto* parentNode = nodeFromIndex(parent);
    if (!parentNode || static_cast<std::size_t>(row) >= parentNode->children.size()) {
        return {};
    }

    return createIndex(row, column,
                       parentNode->children[static_cast<std::size_t>(row)].get());
}

QModelIndex DirTreeModel::parent(const QModelIndex& child) const
{
    if (!m_root || !child.isValid()) {
        return {};
    }

    const auto* node = nodeFromIndex(child);
    if (!node || node == m_root.get()) {
        return {};
    }

    const auto parentIt = m_parents.find(node);
    if (parentIt == m_parents.end() || parentIt->second == nullptr) {
        return {};
    }

    const auto* parentNode = parentIt->second;
    if (parentNode == m_root.get()) {
        // The root is the only top-level row.
        return createIndex(0, 0, m_root.get());
    }

    const auto rowIt = m_rows.find(parentNode);
    if (rowIt == m_rows.end()) {
        return {};
    }

    return createIndex(rowIt->second, 0,
                       const_cast<disktool::core::DirNode*>(parentNode));
}

int DirTreeModel::rowCount(const QModelIndex& parent) const
{
    if (!m_root) {
        return 0;
    }

    if (!parent.isValid()) {
        return 1;  // the root node
    }

    if (parent.column() != 0) {
        return 0;
    }

    const auto* node = nodeFromIndex(parent);
    if (!node) {
        return 0;
    }

    return static_cast<int>(node->children.size());
}

int DirTreeModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return ColumnCount;
}

QVariant DirTreeModel::data(const QModelIndex& index, int role) const
{
    const auto* node = nodeFromIndex(index);
    if (!node) {
        return {};
    }

    const auto column = static_cast<Column>(index.column());

    const auto* parentNode = [this, node]() -> const disktool::core::DirNode* {
        const auto it = m_parents.find(node);
        return it == m_parents.end() ? nullptr : it->second;
    }();

    const double percent = (parentNode != nullptr && parentNode->cumulativeSize > 0)
        ? 100.0 * static_cast<double>(node->cumulativeSize)
            / static_cast<double>(parentNode->cumulativeSize)
        : -1.0;

    if (role == Qt::DisplayRole) {
        switch (column) {
        case NameColumn:
            return nameToQString(node->name);
        case SizeColumn:
            return disktool::app::formatBytes(node->cumulativeSize);
        case FilesColumn:
            return QLocale().toString(
                static_cast<qulonglong>(node->cumulativeFileCount));
        case PercentColumn:
            return percent < 0.0
                ? QStringLiteral("—")
                : QStringLiteral("%1 %").arg(QLocale().toString(percent, 'f', 1));
        case ColumnCount:
            break;
        }
    }

    if (role == Qt::UserRole) {
        // Numeric values for sorting.
        switch (column) {
        case NameColumn:
            return nameToQString(node->name);
        case SizeColumn:
            return QVariant::fromValue<qulonglong>(
                static_cast<qulonglong>(node->cumulativeSize));
        case FilesColumn:
            return QVariant::fromValue<qulonglong>(
                static_cast<qulonglong>(node->cumulativeFileCount));
        case PercentColumn:
            return percent;
        case ColumnCount:
            break;
        }
    }

    if (role == Qt::DecorationRole && column == NameColumn) {
        return m_iconProvider.icon(QFileInfo(pathToQString(node->path)));
    }

    if (role == Qt::ToolTipRole) {
        return pathToQString(node->path);
    }

    if (role == Qt::TextAlignmentRole) {
        if (column == SizeColumn || column == FilesColumn
            || column == PercentColumn) {
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        }
    }

    return {};
}

QVariant DirTreeModel::headerData(int section, Qt::Orientation orientation,
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
    case FilesColumn:
        return tr("Файлов");
    case PercentColumn:
        return tr("% родителя");
    default:
        return {};
    }
}

void DirTreeModel::setRoot(std::unique_ptr<disktool::core::DirNode> root)
{
    beginResetModel();
    m_root = std::move(root);
    rebuildIndex();
    endResetModel();
}

void DirTreeModel::appendEntries(
    const std::vector<disktool::core::FileEntry>& entries)
{
    if (!m_root || entries.empty()) {
        return;
    }

    std::unordered_set<const disktool::core::DirNode*> changed;

    for (const auto& entry : entries) {
        const auto relative = entry.path.lexically_relative(m_root->path);
        if (relative.empty() || relative.is_absolute()) {
            continue;
        }

        const fs::path dot = ".";
        const fs::path dotdot = "..";

        std::vector<fs::path> components;
        bool escapesRoot = false;
        for (const auto& component : relative) {
            if (component == dot) {
                continue;
            }
            if (component == dotdot) {
                escapesRoot = true;
                break;
            }
            components.push_back(component);
        }

        if (escapesRoot || components.empty()) {
            continue;
        }

        const std::size_t directoryCount = entry.isDirectory
            ? components.size()
            : components.size() - 1;

        std::vector<disktool::core::DirNode*> chain;
        chain.reserve(directoryCount + 1);
        chain.push_back(m_root.get());

        auto* current = m_root.get();
        fs::path currentPath = m_root->path;

        for (std::size_t position = 0; position < directoryCount; ++position) {
            currentPath /= components[position];
            const std::string name = pathToUtf8(components[position]);
            current = findOrCreateChild(*current, name, currentPath, changed);
            chain.push_back(current);
        }

        if (entry.isDirectory) {
            continue;
        }

        // Files themselves are not rows in this tree: their size and count
        // belong to the containing directory and to every ancestor.
        auto* containingDirectory = chain.back();
        containingDirectory->ownSize += entry.size;
        ++containingDirectory->ownFileCount;

        for (auto* node : chain) {
            node->cumulativeSize += entry.size;
            ++node->cumulativeFileCount;
            changed.insert(node);
        }
    }

    emitChanged(changed);
}

disktool::core::DirNode* DirTreeModel::findOrCreateChild(
    disktool::core::DirNode& parent, const std::string& name,
    const fs::path& fullPath,
    std::unordered_set<const disktool::core::DirNode*>& changed)
{
    auto& byName = m_childIndex[&parent];
    const auto found = byName.find(name);
    if (found != byName.end()) {
        return parent.children[found->second].get();
    }

    const QModelIndex parentIndex = indexForNode(&parent, 0);
    const int row = static_cast<int>(parent.children.size());

    beginInsertRows(parentIndex, row, row);

    auto child = std::make_unique<disktool::core::DirNode>();
    child->path = fullPath;
    child->name = name;
    child->isDirectory = true;

    auto* raw = child.get();
    parent.children.push_back(std::move(child));
    m_parents[raw] = &parent;
    m_rows[raw] = row;
    byName.emplace(name, static_cast<std::size_t>(row));

    endInsertRows();

    changed.insert(&parent);
    return raw;
}

void DirTreeModel::emitChanged(
    const std::unordered_set<const disktool::core::DirNode*>& changed)
{
    for (const auto* node : changed) {
        const QModelIndex first = indexForNode(node, NameColumn);
        const QModelIndex last = indexForNode(node, ColumnCount - 1);
        if (first.isValid() && last.isValid()) {
            emit dataChanged(first, last);
        }
    }
}

const disktool::core::DirNode* DirTreeModel::nodeFromIndex(
    const QModelIndex& index) const
{
    if (!index.isValid()) {
        return nullptr;
    }
    return static_cast<const disktool::core::DirNode*>(index.internalPointer());
}

QModelIndex DirTreeModel::indexForNode(const disktool::core::DirNode* node,
                                       int column) const
{
    if (!m_root || node == nullptr || column < 0 || column >= ColumnCount) {
        return {};
    }

    if (node == m_root.get()) {
        return createIndex(0, column, m_root.get());
    }

    const auto rowIt = m_rows.find(node);
    const auto parentIt = m_parents.find(node);
    if (rowIt == m_rows.end() || parentIt == m_parents.end()
        || parentIt->second == nullptr) {
        return {};
    }

    return createIndex(rowIt->second, column,
                       const_cast<disktool::core::DirNode*>(node));
}

void DirTreeModel::rebuildIndex()
{
    m_parents.clear();
    m_rows.clear();
    m_childIndex.clear();

    if (m_root) {
        indexNode(*m_root, nullptr, 0);
    }
}

void DirTreeModel::indexNode(const disktool::core::DirNode& node,
                             const disktool::core::DirNode* parent, int row)
{
    m_parents[&node] = parent;
    m_rows[&node] = row;

    auto& byName = m_childIndex[&node];
    for (std::size_t position = 0; position < node.children.size(); ++position) {
        byName[node.children[position]->name] = position;
        indexNode(*node.children[position], &node, static_cast<int>(position));
    }
}
