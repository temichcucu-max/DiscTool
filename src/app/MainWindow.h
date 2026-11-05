#pragma once

#include <QMainWindow>

class QPushButton;
class SearchTab;
class AnalysisTab;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void updateScanProgress(qulonglong files, qulonglong dirs);
    void updateScanSummary(qulonglong files, qulonglong dirs, qulonglong bytes,
                           qlonglong durationMs, int errorCount, bool cancelled);

    SearchTab* m_searchTab = nullptr;
    AnalysisTab* m_analysisTab = nullptr;
    QWidget* m_errorSource = nullptr;
    QPushButton* m_errorsButton = nullptr;
};
