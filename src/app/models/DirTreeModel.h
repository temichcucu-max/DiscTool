#pragma once

#include "disktool/core/tree_builder.h"

#include <QAbstractItemModel>
#include <QFileIconProvider>

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class DirTreeModel final : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Column {
        NameColumn = 0,
        SizeColumn,
        FilesColumn,
        PercentColumn,
        ColumnCount
    };

    explicit DirTreeModel(QObject* parent = nullptr);

    QModelIndex index(int row, int column,
                      const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /// Takes ownership of the tree. The root is exposed as the only top-level
    /// row so that it can be selected in the view.
    void setRoot(std::unique_ptr<disktool::core::DirNode> root);

    /// Merges a scan batch into the current tree. New directories are inserted
    /// with beginInsertRows/endInsertRows; sizes and file counts of existing
    /// nodes are updated and reported with dataChanged. Used for live updates
    /// while AnalysisTab scans; the final tree is rebuilt and sorted when the
    /// scan finishes.
    void appendEntries(
        const std::vector<disktool::core::FileEntry>& entries);

    [[nodiscard]] const disktool::core::DirNode* root() const { return m_root.get(); }

    [[nodiscard]] const disktool::core::DirNode* nodeFromIndex(
        const QModelIndex& index) const;
    [[nodiscard]] QModelIndex indexForNode(
        const disktool::core::DirNode* node, int column = 0) const;

private:
    void rebuildIndex();
    void indexNode(const disktool::core::DirNode& node,
                   const disktool::core::DirNode* parent, int row);
    disktool::core::DirNode* findOrCreateChild(
        disktool::core::DirNode& parent, const std::string& name,
        const std::filesystem::path& fullPath,
        std::unordered_set<const disktool::core::DirNode*>& changed);
    void emitChanged(
        const std::unordered_set<const disktool::core::DirNode*>& changed);

    std::unique_ptr<disktool::core::DirNode> m_root;
    std::unordered_map<const disktool::core::DirNode*,
                       const disktool::core::DirNode*>
        m_parents;
    std::unordered_map<const disktool::core::DirNode*, int> m_rows;
    std::unordered_map<const disktool::core::DirNode*,
                       std::unordered_map<std::string, std::size_t>>
        m_childIndex;
    QFileIconProvider m_iconProvider;
};
