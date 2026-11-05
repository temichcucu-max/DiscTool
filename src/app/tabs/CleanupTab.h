#pragma once

#include "TrashWorker.h"
#include "disktool/core/cleanup.h"
#include "disktool/core/scanner.h"

#include <QFutureWatcher>
#include <QWidget>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStandardItemModel;
class QTableView;

class CleanupTab final : public QWidget
{
    Q_OBJECT

public:
    explicit CleanupTab(QWidget* parent = nullptr);
    ~CleanupTab() override;

    void showErrorsDialog();

signals:
    void scanStarted();
    void scanProgress(qulonglong files, qulonglong dirs);
    void scanFinished(qulonglong files, qulonglong dirs, qulonglong bytes,
                      qlonglong durationMs, int errorCount, bool cancelled);

private:
    enum Column {
        TypeColumn = 0,
        PathColumn,
        SizeColumn,
        ColumnCount
    };

    void browseForDirectory();
    void startScan();
    void cancelScan();
    void onScanFinished();
    void setScanning(bool scanning);

    void populate(const disktool::core::CleanupResult& result);
    void appendItem(const disktool::core::CleanupItem& item);
    void updateSummary();
    void updateDeleteButton();
    [[nodiscard]] std::vector<int> checkedRows() const;

    void deleteSelected();
    void cancelTrash();
    void onTrashFinished();
    void setDeleting(bool deleting);

    QLineEdit* m_pathEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    QPushButton* m_scanButton = nullptr;
    QCheckBox* m_findFiles = nullptr;
    QCheckBox* m_findDirectories = nullptr;
    QTableView* m_table = nullptr;
    QStandardItemModel* m_model = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QPushButton* m_cancelTrashButton = nullptr;

    QFutureWatcher<disktool::core::ScanResult> m_watcher;
    QFutureWatcher<TrashResult> m_trashWatcher;
    TrashWorker* m_trashWorker = nullptr;
    std::atomic_bool m_cancelFlag{false};
    std::vector<std::string> m_lastErrors;
    std::filesystem::path m_rootPath;
    std::vector<disktool::core::CleanupItem> m_items;
};
