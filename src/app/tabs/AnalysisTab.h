#pragma once

#include "disktool/core/scanner.h"
#include "disktool/core/tree_builder.h"

#include <QFutureWatcher>
#include <QModelIndex>
#include <QWidget>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

class QLineEdit;
class QPushButton;
class QTreeView;
class DirTreeModel;
class TreemapWidget;

class AnalysisTab final : public QWidget
{
    Q_OBJECT

public:
    explicit AnalysisTab(QWidget* parent = nullptr);
    ~AnalysisTab() override;

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

    void onCurrentIndexChanged(const QModelIndex& current,
                               const QModelIndex& previous);
    void showNode(const QModelIndex& index);
    void updateBreadcrumb(const disktool::core::DirNode* node);
    void goUp();
    void activateTreemapItem(std::size_t index);

    QLineEdit* m_pathEdit = nullptr;
    QPushButton* m_browseButton = nullptr;
    QPushButton* m_scanButton = nullptr;
    QTreeView* m_tree = nullptr;
    DirTreeModel* m_model = nullptr;
    TreemapWidget* m_treemap = nullptr;
    QLineEdit* m_breadcrumb = nullptr;
    QPushButton* m_upButton = nullptr;

    QFutureWatcher<disktool::core::ScanResult> m_watcher;
    std::atomic_bool m_cancelFlag{false};
    std::vector<std::string> m_lastErrors;
    std::filesystem::path m_rootPath;
    const disktool::core::DirNode* m_currentNode = nullptr;
};
