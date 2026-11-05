#pragma once

#include "disktool/core/tree_builder.h"

#include <QAbstractItemModel>
#include <QFileIconProvider>

#include <memory>
#include <unordered_map>

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

    [[nodiscard]] const disktool::core::DirNode* root() const { return m_root.get(); }

    [[nodiscard]] const disktool::core::DirNode* nodeFromIndex(
        const QModelIndex& index) const;
    [[nodiscard]] QModelIndex indexForNode(
        const disktool::core::DirNode* node, int column = 0) const;

private:
    void rebuildIndex();
    void indexNode(const disktool::core::DirNode& node,
                   const disktool::core::DirNode* parent, int row);

    std::unique_ptr<disktool::core::DirNode> m_root;
    std::unordered_map<const disktool::core::DirNode*,
                       const disktool::core::DirNode*>
        m_parents;
    std::unordered_map<const disktool::core::DirNode*, int> m_rows;
    QFileIconProvider m_iconProvider;
};
