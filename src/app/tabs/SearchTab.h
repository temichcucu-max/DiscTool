#pragma once

#include "ScanJob.h"

#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

class QCheckBox;
class QLineEdit;
class QPoint;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;
class FileTableModel;

class SearchTab final : public QWidget
{
    Q_OBJECT

public:
    explicit SearchTab(QWidget* parent = nullptr);
    ~SearchTab() override;

    void setAdminPrivileges(bool isAdmin);
    void showErrorsDialog();

signals:
    void scanStarted();
    void scanProgress(qulonglong files, qulonglong dirs);
    void scanFinished(qulonglong files, qulonglong dirs, qulonglong bytes,
                      qlonglong durationMs, int errorCount, bool cancelled);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void browseForDirectory();
    void startScan();
    void cancelScan();
    void onScanFinished();
    void setScanning(bool scanning);
    void appendBatch(const std::vector<disktool::core::FileEntry>& entries);
    void queueBatch(const std::vector<disktool::core::FileEntry>& entries);
    void flushPendingBatch();
    void showContextMenu(const QPoint& position);

    QLineEdit* m_pathEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    QPushButton* m_scanButton = nullptr;
    QCheckBox* m_includeHidden = nullptr;
    QCheckBox* m_useMft = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QTableView* m_table = nullptr;
    FileTableModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;

    QFutureWatcher<disktool::app::ScanJobResult> m_watcher;
    std::atomic_bool m_cancelFlag{false};
    std::vector<std::string> m_lastErrors;
    std::mutex m_batchMutex;
    std::vector<disktool::core::FileEntry> m_incomingBatch;
    QTimer m_batchTimer;
    bool m_isAdmin = false;
    bool m_acceptBatches = false;
    bool m_initialScanStarted = false;
};
