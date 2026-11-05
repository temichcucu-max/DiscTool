#pragma once

#include "disktool/core/scanner.h"

#include <QAbstractTableModel>
#include <QFileIconProvider>

#include <vector>

class FileTableModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        NameColumn = 0,
        SizeColumn,
        ModifiedColumn,
        TypeColumn,
        FullPathColumn,
        ColumnCount
    };

    explicit FileTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setEntries(std::vector<disktool::core::FileEntry> entries);
    void appendEntries(const std::vector<disktool::core::FileEntry>& entries);
    void clearEntries();

    [[nodiscard]] const disktool::core::FileEntry* entryAt(int row) const;

private:
    std::vector<disktool::core::FileEntry> m_entries;
    QFileIconProvider m_iconProvider;
};
