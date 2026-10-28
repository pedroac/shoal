#include <QApplication>

#include "core/Version.h"
#include "editor/MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Shoal"));
    QApplication::setOrganizationName(QStringLiteral("Sprat"));
    QApplication::setApplicationVersion(shoal::core::version());

    shoal::editor::MainWindow window;
    window.show();
    return QApplication::exec();
}
