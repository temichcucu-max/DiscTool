#include "MainWindow.h"

#include "disktool/core/version.h"

#include <QApplication>
#include <QString>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("DiscTool"));
    QApplication::setApplicationVersion(QString::fromUtf8(disktool::core::coreVersion()));

    MainWindow window;
    window.show();

    return app.exec();
}
