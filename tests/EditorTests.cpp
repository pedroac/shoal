#include <QAction>
#include <QLabel>
#include <QMenuBar>
#include <QTemporaryDir>
#include <QTest>

#include "Support.h"
#include "assets/SpratLibrary.h"
#include "core/ProjectDocument.h"
#include "core/ProjectTemplates.h"
#include "editor/FlowScene.h"
#include "editor/FlowView.h"
#include "editor/MainWindow.h"

using namespace shoal::core;
using shoal::editor::FlowView;
using shoal::editor::MainWindow;

namespace {

QAction* action(const MainWindow& window, const char* name)
{
    auto* found = window.findChild<QAction*>(QLatin1StringView(name));
    Q_ASSERT(found);
    return found;
}

/// A copy of the basic fixture, which tests may change and save.
bool copyFixture(const QTemporaryDir& dir)
{
    return dir.isValid() &&
           support::writeTree(dir.path(),
                              support::readTree(support::fixturePath(QStringLiteral("basic"))));
}

}  // namespace

class EditorTests : public QObject {
    Q_OBJECT
private slots:
    void startsWithNoProject()
    {
        MainWindow window;
        QCOMPARE(window.windowTitle(), QStringLiteral("Shoal"));
        auto* placeholder = qobject_cast<QLabel*>(window.centralWidget());
        QVERIFY(placeholder);
        QCOMPARE(placeholder->objectName(), QStringLiteral("workspacePlaceholder"));
        QVERIFY(!window.document());
        QVERIFY(!action(window, "saveAction")->isEnabled());
        QVERIFY(!action(window, "undoAction")->isEnabled());
        QVERIFY(!action(window, "addSceneAction")->isEnabled());
        QVERIFY(action(window, "newAction")->isEnabled());
    }

    void hasItsMenus()
    {
        MainWindow window;
        QStringList titles;
        for (QAction* menu : window.menuBar()->actions()) {
            titles << menu->text();
        }
        QCOMPARE(titles, (QStringList{QStringLiteral("&File"), QStringLiteral("&Edit"),
                                      QStringLiteral("F&low"), QStringLiteral("&View"),
                                      QStringLiteral("&Help")}));
    }

    void aboutNamesBothVersions()
    {
        const QString text = MainWindow::aboutText();
        QVERIFY2(text.contains(QStringLiteral(SHOAL_EXPECTED_VERSION)), qPrintable(text));
        QVERIFY2(text.contains(shoal::assets::spratLibraryVersion()), qPrintable(text));
    }

    void opensAProjectInTheFlowView()
    {
        QTemporaryDir dir;
        QVERIFY(copyFixture(dir));
        MainWindow window;
        QCOMPARE(window.openProject(dir.path()), QList<FileError>());
        QVERIFY(window.document());
        QCOMPARE(window.centralWidget(), static_cast<QWidget*>(window.flowView()));
        QCOMPARE(window.windowTitle(), QStringLiteral("Fixture Game[*] — Shoal"));
        QVERIFY(!window.isWindowModified());
        QVERIFY(action(window, "saveAction")->isEnabled());
    }

    void keepsTheCurrentProjectWhenOpeningFails()
    {
        QTemporaryDir good;
        QTemporaryDir bad;
        QVERIFY(copyFixture(good));
        QVERIFY(support::writeTree(bad.path(), {{paths::MANIFEST, "{"}}));
        MainWindow window;
        QVERIFY(window.openProject(good.path()).isEmpty());
        ProjectDocument* document = window.document();

        QVERIFY(!window.openProject(bad.path()).isEmpty());
        QCOMPARE(window.document(), document);
    }

    void editsMarkTheWindowModifiedUntilUndone()
    {
        QTemporaryDir dir;
        QVERIFY(copyFixture(dir));
        MainWindow window;
        QVERIFY(window.openProject(dir.path()).isEmpty());
        QAction* undo = action(window, "undoAction");
        QAction* redo = action(window, "redoAction");

        window.flowScene()->addScene(QPointF(0, 600));
        QVERIFY(window.isWindowModified());
        QVERIFY(undo->isEnabled());
        QCOMPARE(undo->text(), QStringLiteral("&Undo Add scene scene"));

        undo->trigger();
        QVERIFY(!window.isWindowModified());
        QVERIFY(!window.document()->project().scenes.contains(QStringLiteral("scene")));
        QCOMPARE(redo->text(), QStringLiteral("&Redo Add scene scene"));
        redo->trigger();
        QVERIFY(window.document()->project().scenes.contains(QStringLiteral("scene")));
    }

    void selectionEnablesTheSceneActions()
    {
        QTemporaryDir dir;
        QVERIFY(copyFixture(dir));
        MainWindow window;
        QVERIFY(window.openProject(dir.path()).isEmpty());
        QAction* remove = action(window, "deleteAction");
        QAction* start = action(window, "startSceneAction");
        QAction* rename = action(window, "renameSceneAction");
        QVERIFY(!remove->isEnabled());

        window.flowScene()->select({QStringLiteral("main_menu")});  // already the start
        QVERIFY(remove->isEnabled());
        QVERIFY(rename->isEnabled());
        QVERIFY(!start->isEnabled());

        window.flowScene()->select({QStringLiteral("level_1")});
        QVERIFY(start->isEnabled());
        start->trigger();
        QCOMPARE(window.document()->project().manifest.start, QStringLiteral("level_1"));
        QVERIFY(!start->isEnabled());

        window.flowScene()->select({QStringLiteral("level_1"), QStringLiteral("level_2")});
        QVERIFY(!rename->isEnabled());
        remove->trigger();
        QVERIFY(!window.document()->project().scenes.contains(QStringLiteral("level_2")));
        QVERIFY(!remove->isEnabled());
    }

    void savesAndBecomesUnmodified()
    {
        QTemporaryDir dir;
        QVERIFY(copyFixture(dir));
        MainWindow window;
        QVERIFY(window.openProject(dir.path()).isEmpty());
        QVERIFY(window.flowScene()->renameScene(QStringLiteral("pause"), QStringLiteral("paused")));
        QVERIFY(window.isWindowModified());

        QVERIFY(window.save());
        QVERIFY(!window.isWindowModified());
        QVERIFY(
            QFileInfo::exists(QDir(dir.path()).filePath(paths::scene(QStringLiteral("paused")))));
        QVERIFY(!QFileInfo::exists(QDir(dir.path()).filePath(QStringLiteral("scenes/pause"))));
    }

    void savesANewProjectWhereItIsAsked()
    {
        QTemporaryDir dir;
        MainWindow window;
        window.setDocument(std::make_unique<ProjectDocument>(
            newProject(QStringLiteral("Tiny"), ProjectTemplate::PixelArt)));
        QCOMPARE(window.windowTitle(), QStringLiteral("Tiny[*] — Shoal"));
        QVERIFY(window.flowScene()->node(QStringLiteral("main")));

        QVERIFY(window.saveProjectAs(dir.path()).ok());
        QCOMPARE(window.document()->directory(), dir.path());
        QVERIFY(QFileInfo::exists(QDir(dir.path()).filePath(paths::MANIFEST)));
    }

    void closesWithoutAskingWhenNothingChanged()
    {
        QTemporaryDir dir;
        QVERIFY(copyFixture(dir));
        MainWindow window;
        QVERIFY(window.openProject(dir.path()).isEmpty());
        window.show();
        QVERIFY(window.close());
    }
};

QTEST_MAIN(EditorTests)
#include "EditorTests.moc"
