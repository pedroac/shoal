#include <QApplication>
#include <QCommandLineParser>

#include "core/Version.h"
#include "editor/MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Shoal"));
    QApplication::setOrganizationName(QStringLiteral("Sprat"));
    QApplication::setApplicationVersion(shoal::core::version());

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QApplication::translate("main", "A visual scene graph and scene flow editor."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("project"),
        QApplication::translate("main", "A project folder, or its project.shoal.json, to open."),
        QStringLiteral("[project]"));
    parser.process(app);

    shoal::editor::MainWindow window;
    window.show();
    if (!parser.positionalArguments().isEmpty()) {
        window.openProjectAt(parser.positionalArguments().first());
    }
    return QApplication::exec();
}
