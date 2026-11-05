#include "MainWindow.h"

#include "FormatUtils.h"
#include "disktool/core/version.h"
#include "tabs/AnalysisTab.h"
#include "tabs/CleanupTab.h"
#include "tabs/SearchTab.h"

#include <QIcon>
#include <QLocale>
#include <QPushButton>
#include <QStatusBar>
#include <QString>
#include <QTabWidget>
#include <QWidget>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#endif

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.ico")));

    auto* tabs = new QTabWidget(this);

    m_searchTab = new SearchTab(tabs);
    tabs->addTab(m_searchTab, tr("Поиск"));

    m_analysisTab = new AnalysisTab(tabs);
    tabs->addTab(m_analysisTab, tr("Анализ диска"));

    m_cleanupTab = new CleanupTab(tabs);
    tabs->addTab(m_cleanupTab, tr("Очистка"));

    // MFT/USN access requires an elevated process. The Search tab keeps its
    // checkbox visible and explains the requirement when it is clicked.
#ifdef _WIN32
    const bool isAdmin = IsUserAnAdmin() != FALSE;
#else
    const bool isAdmin = false;
#endif
    m_searchTab->setAdminPrivileges(isAdmin);

    setCentralWidget(tabs);

    m_errorsButton = new QPushButton(tr("Ошибки"), this);
    m_errorsButton->setFlat(true);
    m_errorsButton->setCursor(Qt::PointingHandCursor);
    m_errorsButton->hide();
    statusBar()->addPermanentWidget(m_errorsButton);

    connect(m_errorsButton, &QPushButton::clicked, this, [this] {
        if (m_errorSource == m_analysisTab) {
            m_analysisTab->showErrorsDialog();
        } else if (m_errorSource == m_searchTab) {
            m_searchTab->showErrorsDialog();
        } else if (m_errorSource == m_cleanupTab) {
            m_cleanupTab->showErrorsDialog();
        }
    });

    const auto beginScan = [this](QWidget* source) {
        m_errorSource = source;
        m_errorsButton->hide();
        statusBar()->showMessage(tr("Сканирование..."));
    };

    connect(m_searchTab, &SearchTab::scanStarted, this,
            [this, beginScan] { beginScan(m_searchTab); });
    connect(m_analysisTab, &AnalysisTab::scanStarted, this,
            [this, beginScan] { beginScan(m_analysisTab); });
    connect(m_cleanupTab, &CleanupTab::scanStarted, this,
            [this, beginScan] { beginScan(m_cleanupTab); });

    connect(m_searchTab, &SearchTab::scanProgress,
            this, &MainWindow::updateScanProgress);
    connect(m_analysisTab, &AnalysisTab::scanProgress,
            this, &MainWindow::updateScanProgress);
    connect(m_cleanupTab, &CleanupTab::scanProgress,
            this, &MainWindow::updateScanProgress);

    connect(m_searchTab, &SearchTab::scanFinished,
            this, &MainWindow::updateScanSummary);
    connect(m_analysisTab, &AnalysisTab::scanFinished,
            this, &MainWindow::updateScanSummary);
    connect(m_cleanupTab, &CleanupTab::scanFinished,
            this, &MainWindow::updateScanSummary);

    // Version comes from the core library, proving app <-> core linkage.
    setWindowTitle(tr("DiscTool %1").arg(QString::fromUtf8(disktool::core::coreVersion())));
    resize(960, 640);
}

void MainWindow::updateScanProgress(qulonglong files, qulonglong dirs)
{
    statusBar()->showMessage(
        tr("Сканирование... Найдено: %1 файлов, %2 папок")
            .arg(QLocale().toString(files), QLocale().toString(dirs)));
}

void MainWindow::updateScanSummary(qulonglong files, qulonglong dirs,
                                   qulonglong bytes, qlonglong durationMs,
                                   int errorCount, bool cancelled)
{
    QString message;
    if (cancelled) {
        message += tr("Отменено. ");
    }

    message += tr("Найдено: %1 файлов, %2 папок, %3 за %4 с")
                   .arg(QLocale().toString(files),
                        QLocale().toString(dirs),
                        disktool::app::formatBytes(bytes),
                        QLocale().toString(static_cast<double>(durationMs) / 1000.0,
                                           'f', 2));

    if (errorCount > 0) {
        message += tr("; ошибок: %1").arg(errorCount);
    } else {
        message += tr("; ошибок нет");
    }

    statusBar()->showMessage(message);

    m_errorsButton->setText(tr("Ошибки: %1").arg(errorCount));
    m_errorsButton->setVisible(errorCount > 0);
}
