#include "MainWindow.h"

#include "disktool/core/version.h"

#include <QLabel>
#include <QString>
#include <QTabWidget>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    auto* tabs = new QTabWidget(this);

    const auto addPlaceholder = [this, tabs](const QString& title) {
        auto* label = new QLabel(tr("Раздел в разработке"), tabs);
        label->setAlignment(Qt::AlignCenter);
        tabs->addTab(label, title);
    };

    addPlaceholder(tr("Поиск"));
    addPlaceholder(tr("Анализ диска"));
    addPlaceholder(tr("Очистка"));

    setCentralWidget(tabs);

    // Version comes from the core library, proving app <-> core linkage.
    setWindowTitle(tr("DiscTool %1").arg(QString::fromUtf8(disktool::core::coreVersion())));
    resize(960, 640);
}
