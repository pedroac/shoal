#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

#include "Support.h"
#include "assets/SpratLibrary.h"
#include "core/ProjectDocument.h"
#include "core/ProjectTemplates.h"
#include "editor/FlowScene.h"
#include "editor/FlowView.h"
#include "editor/MainWindow.h"
#include "editor/SceneCanvas.h"
#include "editor/SceneSettingsPanel.h"
#include "editor/SceneView.h"
#include "editor/ScenesPanel.h"

using namespace shoal::core;
using shoal::editor::FlowView;
using shoal::editor::MainWindow;
using shoal::editor::SceneSettingsPanel;

namespace {

QAction* action(const MainWindow& window, const char* name)
{
    auto* found = window.findChild<QAction*>(QLatin1StringView(name));
    Q_ASSERT(found);
    return found;
}

QDockWidget* dock(const MainWindow& window, const char* name)
{
    auto* found = window.findChild<QDockWidget*>(QLatin1StringView(name));
    Q_ASSERT(found);
    return found;
}

/// Whether a dock is open, and listed in the View menu.
bool offered(const MainWindow& window, const char* name)
{
    const QDockWidget* found = dock(window, name);
    const bool listed = found->toggleViewAction()->isVisible();
    Q_ASSERT(listed || found->isHidden());
    return listed && !found->isHidden();
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
                                      QStringLiteral("&Arrange"), QStringLiteral("F&low"),
                                      QStringLiteral("&View"), QStringLiteral("&Help")}));
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
        QCOMPARE(window.centralWidget(), static_cast<QWidget*>(window.tabs()));
        QCOMPARE(window.tabs()->widget(0), static_cast<QWidget*>(window.flowView()));
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
    void docksFollowTheCurrentTab()
    {
        QTemporaryDir dir;
        QVERIFY(copyFixture(dir));
        MainWindow window;
        QVERIFY(window.openProject(dir.path()).isEmpty());
        window.show();
        const auto onFlow = [&] {
            return offered(window, "scenesDock") && offered(window, "sceneSettingsDock") &&
                   !offered(window, "sceneTreeDock") && !offered(window, "presetsDock") &&
                   !offered(window, "inspectorDock");
        };
        const auto onScene = [&] {
            return !offered(window, "scenesDock") && !offered(window, "sceneSettingsDock") &&
                   offered(window, "sceneTreeDock") && offered(window, "presetsDock") &&
                   offered(window, "inspectorDock");
        };
        QVERIFY(onFlow());
        QVERIFY(offered(window, "sheetBrowserDock") && offered(window, "classesDock"));

        window.openScene(QStringLiteral("level_1"));
        QVERIFY(onScene());
        QVERIFY(offered(window, "sheetBrowserDock") && offered(window, "classesDock"));

        // A dock the user closes stays closed when they come back to that kind of tab.
        dock(window, "inspectorDock")->close();
        window.tabs()->setCurrentIndex(0);
        QVERIFY(onFlow());
        dock(window, "scenesDock")->close();
        window.openScene(QStringLiteral("level_2"));
        QVERIFY(dock(window, "inspectorDock")->isHidden());
        QVERIFY(dock(window, "inspectorDock")->toggleViewAction()->isVisible());
        QVERIFY(offered(window, "sceneTreeDock"));
        window.tabs()->setCurrentIndex(0);
        QVERIFY(dock(window, "scenesDock")->isHidden());
        QVERIFY(offered(window, "sceneSettingsDock"));
    }

    void scenesPanelSharesTheFlowSelection()
    {
        QTemporaryDir dir;
        QVERIFY(copyFixture(dir));
        MainWindow window;
        QVERIFY(window.openProject(dir.path()).isEmpty());
        QListWidget* list = window.scenesPanel()->list();
        QCOMPARE(list->count(), 5);
        QCOMPARE(window.scenesPanel()->sceneRow(QStringLiteral("main_menu"))->text(),
                 QStringLiteral("main_menu (start)"));

        window.flowScene()->select({QStringLiteral("level_2")});
        QCOMPARE(list->selectedItems(),
                 QList{window.scenesPanel()->sceneRow(QStringLiteral("level_2"))});
        window.scenesPanel()->sceneRow(QStringLiteral("pause"))->setSelected(true);
        QCOMPARE(window.flowScene()->selectedSceneIds(),
                 (QStringList{QStringLiteral("level_2"), QStringLiteral("pause")}));

        window.flowScene()->addScene(QPointF(0, 600));
        QVERIFY(window.scenesPanel()->sceneRow(QStringLiteral("scene")));

        emit list->itemActivated(window.scenesPanel()->sceneRow(QStringLiteral("level_1")));
        QVERIFY(window.currentSceneView());
        QCOMPARE(window.currentSceneView()->canvas().sceneId(), QStringLiteral("level_1"));
    }

    void sceneSettingsEditTheSelectedScene()
    {
        QTemporaryDir dir;
        QVERIFY(copyFixture(dir));
        MainWindow window;
        QVERIFY(window.openProject(dir.path()).isEmpty());
        SceneSettingsPanel* panel = window.sceneSettingsPanel();
        const ProjectDocument& document = *window.document();
        QVERIFY(!panel->idField()->isEnabled());

        window.flowScene()->select({QStringLiteral("pause")});
        QCOMPARE(panel->idField()->text(), QStringLiteral("pause"));
        QCOMPARE(panel->widthBox()->value(), 1920.0);
        QCOMPARE(panel->fitBox()->currentText(), QStringLiteral("letterbox"));
        QCOMPARE(panel->fitBox()->itemText(0), QStringLiteral("Project's (expand)"));
        QVERIFY(!panel->startBox()->isChecked());

        panel->widthBox()->setValue(1280);
        QCOMPARE(document.project().scenes.value(QStringLiteral("pause")).size, (Size{1280, 1080}));
        panel->fitBox()->setCurrentIndex(0);
        emit panel->fitBox()->activated(0);
        QCOMPARE(document.project().scenes.value(QStringLiteral("pause")).fit, std::nullopt);
        action(window, "undoAction")->trigger();
        QCOMPARE(panel->fitBox()->currentText(), QStringLiteral("letterbox"));
        action(window, "undoAction")->trigger();
        QCOMPARE(panel->widthBox()->value(), 1920.0);

        panel->idField()->setText(QStringLiteral("1bad"));
        QVERIFY(!panel->applyId());
        QVERIFY(!panel->message().isEmpty());
        panel->idField()->setText(QStringLiteral("level_1"));
        QVERIFY(!panel->applyId());
        QCOMPARE(panel->message(),
                 shoal::editor::FlowScene::renameRefusal(QStringLiteral("level_1")));
        panel->idField()->setText(QStringLiteral("paused"));
        QVERIFY(panel->applyId());
        QVERIFY(document.project().scenes.contains(QStringLiteral("paused")));
        QCOMPARE(window.flowScene()->selectedSceneIds(), QStringList{QStringLiteral("paused")});

        panel->startBox()->click();
        QCOMPARE(document.project().manifest.start, QStringLiteral("paused"));
        QVERIFY(!panel->startBox()->isEnabled());

        window.flowScene()->select({QStringLiteral("level_1"), QStringLiteral("level_2")});
        QVERIFY(!panel->idField()->isEnabled());
        QCOMPARE(panel->summary()->text(), QStringLiteral("2 scenes selected."));
    }
};

QTEST_MAIN(EditorTests)
#include "EditorTests.moc"
