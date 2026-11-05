#pragma once

#include "disktool/core/scanner.h"

#include <QFutureWatcher>
#include <QWidget>

#include <atomic>
#include <string>
#include <vector>

class QCheckBox;
class QLineEdit;
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

    void showErrorsDialog();

signals:
    void scanStarted();
    void scanProgress(qulonglong files, qulonglong dirs);
    void scanFinished(qulonglong files, qulonglong dirs, qulonglong bytes,
                      qlonglong durationMs, int errorCount, bool cancelled);

private:
    void browseForDirectory();
    void startScan();
    void cancelScan();
    void onScanFinished();
    void setScanning(bool scanning);

    QLineEdit* m_pathEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    QPushButton* m_scanButton = nullptr;
    QCheckBox* m_includeHidden = nullptr;
    QTableView* m_table = nullptr;
    FileTableModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;

    QFutureWatcher<disktool::core::ScanResult> m_watcher;
    std::atomic_bool m_cancelFlag{false};
    std::vector<std::string> m_lastErrors;
};
