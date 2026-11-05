#include "MainWindow.h"

#include "FormatUtils.h"
#include "disktool/core/version.h"
#include "tabs/SearchTab.h"

#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QStatusBar>
#include <QString>
#include <QTabWidget>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    auto* tabs = new QTabWidget(this);

    m_searchTab = new SearchTab(tabs);
    tabs->addTab(m_searchTab, tr("Поиск"));

    const auto addPlaceholder = [this, tabs](const QString& title) {
        auto* label = new QLabel(tr("Раздел в разработке"), tabs);
        label->setAlignment(Qt::AlignCenter);
        tabs->addTab(label, title);
    };

    addPlaceholder(tr("Анализ диска"));
    addPlaceholder(tr("Очистка"));

    setCentralWidget(tabs);

    m_errorsButton = new QPushButton(tr("Ошибки"), this);
    m_errorsButton->setFlat(true);
    m_errorsButton->setCursor(Qt::PointingHandCursor);
    m_errorsButton->hide();
    statusBar()->addPermanentWidget(m_errorsButton);

    connect(m_errorsButton, &QPushButton::clicked,
            m_searchTab, &SearchTab::showErrorsDialog);
    connect(m_searchTab, &SearchTab::scanStarted, this, [this] {
        m_errorsButton->hide();
        statusBar()->showMessage(tr("Сканирование..."));
    });
    connect(m_searchTab, &SearchTab::scanProgress,
            this, &MainWindow::updateScanProgress);
    connect(m_searchTab, &SearchTab::scanFinished,
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
