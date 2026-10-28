#include <QLabel>
#include <QMenuBar>
#include <QTest>

#include "assets/SpratLibrary.h"
#include "editor/MainWindow.h"

using shoal::editor::MainWindow;

class EditorTests : public QObject {
    Q_OBJECT
private slots:
    void mainWindowStartsEmpty()
    {
        MainWindow window;
        QCOMPARE(window.windowTitle(), QStringLiteral("Shoal"));
        auto* placeholder = qobject_cast<QLabel*>(window.centralWidget());
        QVERIFY(placeholder);
        QCOMPARE(placeholder->objectName(), QStringLiteral("workspacePlaceholder"));
    }

    void mainWindowHasFileAndHelpMenus()
    {
        MainWindow window;
        QStringList titles;
        for (QAction* action : window.menuBar()->actions()) {
            titles << action->text();
        }
        QCOMPARE(titles, (QStringList{QStringLiteral("&File"), QStringLiteral("&Help")}));
    }

    void aboutNamesBothVersions()
    {
        const QString text = MainWindow::aboutText();
        QVERIFY2(text.contains(QStringLiteral(SHOAL_EXPECTED_VERSION)), qPrintable(text));
        QVERIFY2(text.contains(shoal::assets::spratLibraryVersion()), qPrintable(text));
    }
};

QTEST_MAIN(EditorTests)
#include "EditorTests.moc"
