#include "MainWindow.h"
#include "Theme.h"

#include "disktool/core/version.h"

#include <QApplication>
#include <QString>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("DiscTool"));
    QApplication::setApplicationName(QStringLiteral("DiscTool"));
    QApplication::setApplicationVersion(QString::fromUtf8(disktool::core::coreVersion()));

    disktool::app::Theme::instance().initialize();

    MainWindow window;
    window.show();

    return app.exec();
}
