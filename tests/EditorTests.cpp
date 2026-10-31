#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMimeData>
#include <QMovie>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QUrl>

#include "Support.h"
#include "assets/SpratLibrary.h"
#include "core/ProjectDocument.h"
#include "core/ProjectTemplates.h"
#include "editor/AssetBrowserPanel.h"
#include "editor/ColorButton.h"
#include "editor/FlowScene.h"
#include "editor/FlowView.h"
#include "editor/Icons.h"
#include "editor/InspectorPanel.h"
#include "editor/MainWindow.h"
#include "editor/PresetsPanel.h"
#include "editor/ProjectDropArea.h"
#include "editor/SceneCanvas.h"
#include "editor/SceneSettingsPanel.h"
#include "editor/SceneTreePanel.h"
#include "editor/SceneView.h"
#include "editor/ScenesPanel.h"
#include "editor/Splitter.h"
#include "editor/TransitionPanel.h"
#include "editor/TransitionTypePanel.h"
#include "editor/TransitionTypesPanel.h"
#include "editor/WorkspaceTabs.h"

using namespace shoal::core;
using shoal::editor::ColorButton;
using shoal::editor::FlowView;
using shoal::editor::Icon;
using shoal::editor::MainWindow;
using shoal::editor::ProjectDropArea;
using shoal::editor::SceneSettingsPanel;
using shoal::editor::TransitionPanel;
using shoal::editor::TransitionTypePanel;
using shoal::editor::TransitionTypesPanel;

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
        auto* placeholder = qobject_cast<ProjectDropArea*>(window.centralWidget());
        QVERIFY(placeholder);
        QCOMPARE(placeholder->objectName(), QStringLiteral("workspacePlaceholder"));
        QVERIFY(!window.document());
        QVERIFY(!action(window, "saveAction")->isEnabled());
        QVERIFY(!action(window, "undoAction")->isEnabled());
        QVERIFY(!action(window, "addSceneAction")->isEnabled());
        QVERIFY(action(window, "newAction")->isEnabled());
        // The side panels wait for a project's tabs.
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(!window.projectPanel()->isVisible());
        QVERIFY(!window.contextPanel()->isVisible());
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
        // Every tab has its icon: the flow, the presets, each scene, and the side lists.
        QVERIFY(window.openScene(window.document()->project().scenes.firstKey()));
        for (int i = 0; i < window.tabs()->count(); ++i) {
            QVERIFY(!window.tabs()->tabBar()->tabIcon(i).isNull());
        }
        for (int i = 0; i < window.projectPanel()->count(); ++i) {
            QVERIFY(!window.projectPanel()->tabIcon(i).isNull());
        }
    }

    void opensAProjectDroppedOnTheWindow()
    {
        QTemporaryDir dir;
        QVERIFY(copyFixture(dir));
        const QString manifest = QDir(dir.path()).filePath(paths::MANIFEST);
        // Its folder or its manifest, and nothing else.
        QCOMPARE(ProjectDropArea::projectIn({QUrl::fromLocalFile(dir.path())}),
                 QFileInfo(dir.path()).absoluteFilePath());
        QCOMPARE(ProjectDropArea::projectIn({QUrl::fromLocalFile(manifest)}), manifest);
        QVERIFY(ProjectDropArea::projectIn(
                    {QUrl::fromLocalFile(QDir(dir.path()).filePath(QStringLiteral("x.png")))})
                    .isEmpty());
        QVERIFY(
            ProjectDropArea::projectIn({QUrl(QStringLiteral("https://example.com/"))}).isEmpty());
        QVERIFY(ProjectDropArea::projectIn(
                    {QUrl::fromLocalFile(dir.path()), QUrl::fromLocalFile(manifest)})
                    .isEmpty());

        MainWindow window;
        window.show();
        QWidget* area = window.centralWidget();
        QMimeData data;
        data.setUrls({QUrl::fromLocalFile(dir.path())});
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
        const QPointF at(area->rect().center());
#else
        const QPoint at = area->rect().center();
#endif
        QDragEnterEvent enter(at, Qt::CopyAction, &data, Qt::LeftButton, {});
        QApplication::sendEvent(area, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(at, Qt::CopyAction, &data, Qt::LeftButton, {});
        QApplication::sendEvent(area, &drop);
        QVERIFY(drop.isAccepted());
        QTRY_VERIFY(window.document());
        QCOMPARE(window.document()->directory(), QFileInfo(dir.path()).absoluteFilePath());
    }

    void iconsTakeThePalettesColor()
    {
        for (const Icon which :
             {Icon::Add, Icon::Remove, Icon::Filter, Icon::Flow, Icon::Scene, Icon::Preset,
              Icon::Assets, Icon::Transition, Icon::MoveUp, Icon::MoveDown, Icon::DragHandle,
              Icon::Drop, Icon::Error, Icon::Warning}) {
            const QImage image = shoal::editor::icon(which).pixmap(QSize(32, 32)).toImage();
            QVERIFY2(!image.isNull(), qPrintable(QString::number(int(which))));
            bool drawn = false;
            for (int y = 0; y < image.height() && !drawn; ++y) {
                for (int x = 0; x < image.width() && !drawn; ++x) {
                    drawn = qAlpha(image.pixel(x, y)) > 0;
                }
            }
            QVERIFY2(drawn, qPrintable(QString::number(int(which))));  // its file is there
        }
        // A plain one is drawn in the palette's button text; it is black in the file.
        const QImage add = shoal::editor::icon(Icon::Add).pixmap(QSize(32, 32)).toImage();
        const QColor color = QGuiApplication::palette().color(QPalette::ButtonText);
        QCOMPARE(QColor(add.pixel(16, 16)).rgb(), color.rgb());
        QCOMPARE(qAlpha(add.pixel(0, 0)), 0);  // and nothing around it
        const QImage disabled =
            shoal::editor::icon(Icon::Add).pixmap(QSize(32, 32), QIcon::Disabled).toImage();
        QCOMPARE(qAlpha(disabled.pixel(0, 0)), 0);
        QCOMPARE(QColor(disabled.pixel(16, 16)).rgb(),
                 QGuiApplication::palette().color(QPalette::Disabled, QPalette::ButtonText).rgb());
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
    void sidePanelsFollowTheCurrentTab()
    {
        QTemporaryDir dir;
        QVERIFY(copyFixture(dir));
        MainWindow window;
        QVERIFY(window.openProject(dir.path()).isEmpty());
        window.show();
        QTabWidget* project = window.projectPanel();
        QStackedWidget* context = window.contextPanel();
        // Beside the workspace, under its tabs.
        QSplitter* splitter = window.tabs()->splitter();
        QVERIFY(qobject_cast<shoal::editor::Splitter*>(splitter));  // its handles show dots
        QCOMPARE(splitter->widget(0), static_cast<QWidget*>(project));
        QCOMPARE(splitter->widget(2), static_cast<QWidget*>(context));
        QCOMPARE(project->count(), 4);
        QCOMPARE(project->widget(0), static_cast<QWidget*>(window.scenesPanel()));
        QCOMPARE(project->widget(1), static_cast<QWidget*>(window.presetsPanel()));
        QCOMPARE(project->widget(2), static_cast<QWidget*>(window.assetBrowser()));
        QCOMPARE(project->widget(3), static_cast<QWidget*>(window.transitionTypesPanel()));

        const auto shows = [project](QWidget* list) {
            return project->isTabVisible(project->indexOf(list));
        };

        // The flow: the scenes and the transition types, and the selected scene's settings.
        QCOMPARE(project->currentWidget(), static_cast<QWidget*>(window.scenesPanel()));
        QVERIFY(shows(window.scenesPanel()));
        QVERIFY(shows(window.transitionTypesPanel()));
        QVERIFY(!shows(window.presetsPanel()));
        QVERIFY(!shows(window.assetBrowser()));
        QCOMPARE(context->currentWidget(), static_cast<QWidget*>(window.sceneSettingsPanel()));

        // A scene: what to place, the sheets first, and the entity tree over the inspector.
        window.openScene(QStringLiteral("level_1"));
        QVERIFY(!shows(window.scenesPanel()));
        QVERIFY(!shows(window.transitionTypesPanel()));
        QVERIFY(shows(window.presetsPanel()));
        QVERIFY(shows(window.assetBrowser()));
        QCOMPARE(project->currentWidget(), static_cast<QWidget*>(window.assetBrowser()));
        QVERIFY(context->currentWidget()->isAncestorOf(window.sceneTreePanel()));
        QVERIFY(context->currentWidget()->isAncestorOf(window.inspector()));
        project->setCurrentWidget(window.presetsPanel());

        // The presets: the same lists, the presets to choose from, and the same context.
        window.openPresets();
        QVERIFY(!shows(window.scenesPanel()));
        QVERIFY(shows(window.assetBrowser()));
        QCOMPARE(project->currentWidget(), static_cast<QWidget*>(window.presetsPanel()));
        QVERIFY(context->currentWidget()->isAncestorOf(window.inspector()));

        // Back on a scene, through the flow, the list last chosen on a scene.
        window.tabs()->setCurrentIndex(0);
        QVERIFY(shows(window.scenesPanel()));
        window.openScene(QStringLiteral("level_1"));
        QCOMPARE(project->currentWidget(), static_cast<QWidget*>(window.presetsPanel()));

        // Hidden from the View menu, they stay hidden on every tab, and in the next project.
        action(window, "projectPanelAction")->trigger();
        QVERIFY(project->isHidden());
        window.tabs()->setCurrentIndex(0);
        QVERIFY(project->isHidden());
        QVERIFY(window.openProject(dir.path()).isEmpty());
        QVERIFY(project->isHidden());
        QVERIFY(!context->isHidden());
        action(window, "projectPanelAction")->trigger();
        QVERIFY(!project->isHidden());
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

    void transitionPanelEditsTheSelectedTransitionsEffect()
    {
        QTemporaryDir dir;
        QVERIFY(copyFixture(dir));
        // A type of the project's own, beside the built-in ones.
        QVERIFY(support::writeTree(
            dir.path(), {{paths::transitionType(QStringLiteral("ripple")),
                          R"({"format": "shoal-transition", "version": 1, "name": "Ripple",
                              "description": "Rings spread.",
                              "options": [{"name": "rings", "type": "number", "integer": true,
                                           "min": 1, "default": 3},
                                          {"name": "mask", "type": "path"}]})"}}));
        MainWindow window;
        QVERIFY(window.openProject(dir.path()).isEmpty());
        window.show();
        TransitionPanel* panel = window.transitionPanel();
        const QString id = QStringLiteral("l1_to_l2");
        const auto effect = [&] { return window.document()->findTransition(id)->effect; };
        const auto choose = [](QComboBox* box, const QString& data) {
            const int index = box->findData(data);
            QVERIFY(index >= 0);
            box->setCurrentIndex(index);
            emit box->activated(index);
        };

        // The scene settings until one transition is selected, and nothing else.
        QCOMPARE(window.contextPanel()->currentWidget(),
                 static_cast<QWidget*>(window.sceneSettingsPanel()));
        window.flowScene()->select({}, {id});
        QCOMPARE(window.contextPanel()->currentWidget(), static_cast<QWidget*>(panel));
        QVERIFY(panel->summary()->text().contains(QStringLiteral("level_1 → level_2")));
        QCOMPARE(panel->typeBox()->currentData().toString(), QStringLiteral("fade"));
        QCOMPARE(panel->typeBox()->currentText(), QStringLiteral("Fade"));
        QVERIFY(panel->typeBox()->findData(QStringLiteral("ripple")) >= 0);
        QVERIFY(panel->description()->text().contains(QStringLiteral("fades")));
        QVERIFY(!panel->preview()->isHidden());  // its GIF, playing
        QVERIFY(panel->preview()->movie());
        QVERIFY(panel->preview()->movie()->isValid());
        QCOMPARE(panel->preview()->movie()->state(), QMovie::Running);
        QCOMPARE(panel->durationBox()->value(), 400.0);
        QCOMPARE(panel->easingBox()->currentData().toString(), QStringLiteral("ease_out"));
        QVERIFY(qobject_cast<ColorButton*>(panel->optionField(QStringLiteral("color"))));
        auto* hold = qobject_cast<QDoubleSpinBox*>(panel->optionField(QStringLiteral("hold")));
        QVERIFY(hold);
        QCOMPARE(hold->value(), 0.0);  // its default, unset in the file
        hold->setValue(150);
        QCOMPARE(effect()->params.value(QStringLiteral("hold")), QJsonValue(150));

        // Another type keeps the timing, and its fields are its options.
        choose(panel->typeBox(), QStringLiteral("slide"));
        QCOMPARE(effect()->type, QStringLiteral("slide"));
        QCOMPARE(effect()->durationMs, 400);
        QCOMPARE(effect()->params,
                 (QJsonObject{{QStringLiteral("direction"), QStringLiteral("left")}}));
        QVERIFY(!panel->optionField(QStringLiteral("color")));
        auto* direction = qobject_cast<QComboBox*>(panel->optionField(QStringLiteral("direction")));
        QVERIFY(direction);
        direction->setCurrentIndex(direction->findText(QStringLiteral("up")));
        emit direction->activated(direction->currentIndex());
        QCOMPARE(effect()->params.value(QStringLiteral("direction")),
                 QJsonValue(QStringLiteral("up")));
        panel->durationBox()->setValue(250);
        QCOMPARE(effect()->durationMs, 250);
        choose(panel->easingBox(), QString());  // linear, written as none
        QVERIFY(effect()->easing.isEmpty());
        QCOMPARE(window.document()->commands().undoText(),
                 QStringLiteral("Set the effect of transition l1_to_l2"));
        window.document()->commands().undo();
        QCOMPARE(panel->easingBox()->currentData().toString(), QStringLiteral("ease_out"));

        // None takes the effect away; a type brings a new one.
        choose(panel->typeBox(), QString());
        QVERIFY(!effect());
        QVERIFY(!panel->durationBox()->isEnabled());
        QVERIFY(!panel->optionField(QStringLiteral("direction")));
        choose(panel->typeBox(), QStringLiteral("ripple"));
        QCOMPARE(effect()->type, QStringLiteral("ripple"));
        QCOMPARE(effect()->params, (QJsonObject{{QStringLiteral("rings"), 3}}));
        QCOMPARE(panel->description()->text(), QStringLiteral("Rings spread."));
        QVERIFY(panel->preview()->isHidden());  // it has none
        auto* mask = panel->optionField(QStringLiteral("mask"))->findChild<QLineEdit*>();
        QVERIFY(mask);
        mask->setText(QStringLiteral("fx/rings.png"));
        mask->setModified(true);
        emit mask->editingFinished();
        QCOMPARE(effect()->params.value(QStringLiteral("mask")),
                 QJsonValue(QStringLiteral("fx/rings.png")));

        // A type the project doesn't have is shown by its id, without options.
        Effect iris;
        iris.type = QStringLiteral("iris");
        iris.durationMs = 300;
        window.flowScene()->setTransitionEffect(id, iris);
        QCOMPARE(panel->typeBox()->currentText(), QStringLiteral("iris (unknown)"));
        QCOMPARE(panel->durationBox()->value(), 300.0);
        QVERIFY(!panel->optionField(QStringLiteral("rings")));

        // A scene selected too: its settings again.
        window.flowScene()->select({QStringLiteral("level_1")}, {id});
        QCOMPARE(window.contextPanel()->currentWidget(),
                 static_cast<QWidget*>(window.sceneSettingsPanel()));
    }

    void transitionPanelEditsTheWholeTransition()
    {
        QTemporaryDir dir;
        QVERIFY(copyFixture(dir));
        MainWindow window;
        QVERIFY(window.openProject(dir.path()).isEmpty());
        window.show();
        TransitionPanel* panel = window.transitionPanel();
        ProjectDocument& document = *window.document();
        QString id = QStringLiteral("l1_to_l2");
        const auto current = [&] { return *document.findTransition(id); };
        const auto choose = [](QComboBox* box, const QString& data) {
            const int index = box->findData(data);
            QVERIFY(index >= 0);
            box->setCurrentIndex(index);
            emit box->activated(index);
        };
        const auto type = [](QLineEdit* field, const QString& text) {
            field->setText(text);
            field->setModified(true);
            emit field->editingFinished();
        };
        window.flowScene()->select({}, {id});

        // Its id: one taken is refused, with why; another renames it, still selected.
        QCOMPARE(panel->idField()->text(), id);
        type(panel->idField(), QStringLiteral("l1_pause"));
        QVERIFY(panel->message().contains(QStringLiteral("l1_pause")));
        QVERIFY(document.findTransition(id));
        type(panel->idField(), QStringLiteral("west_door"));
        QVERIFY(panel->message().isEmpty());
        QVERIFY(!document.findTransition(id));
        id = QStringLiteral("west_door");
        QCOMPARE(window.flowScene()->selectedTransitionIds(), QStringList{id});
        QCOMPARE(panel->transitionId(), id);

        // Where it goes, by mode.
        QCOMPARE(panel->toBox()->currentData().toString(), QStringLiteral("level_2"));
        choose(panel->toBox(), QStringLiteral("options"));
        QCOMPARE(current().to, QStringLiteral("options"));
        document.commands().undo();
        choose(panel->modeBox(), QStringLiteral("push"));
        QCOMPARE(current().mode, TransitionMode::Push);
        QVERIFY(panel->underlyingBox()->isVisible());
        choose(panel->underlyingBox(), QStringLiteral("running"));
        QCOMPARE(current().underlying, Underlying::Running);
        choose(panel->modeBox(), QStringLiteral("pop"));
        QVERIFY(current().to.isEmpty());
        QVERIFY(!panel->toBox()->isVisible());
        QVERIFY(!panel->carryButton()->isVisible());  // a pop lands on no marks
        choose(panel->modeBox(), QStringLiteral("push_modifier"));
        QCOMPARE(current().modifier, QStringLiteral("altar_reveal"));  // the scene's first
        choose(panel->modifierBox(), QStringLiteral("earthquake"));
        QCOMPARE(current().modifier, QStringLiteral("earthquake"));
        // Back to replace: the scene itself until another is chosen.
        choose(panel->modeBox(), QStringLiteral("replace"));
        QCOMPARE(current().to, QStringLiteral("level_1"));
        QVERIFY(current().modifier.isEmpty());
        choose(panel->toBox(), QStringLiteral("level_2"));

        // Its trigger.
        QCOMPARE(panel->eventField()->text(), QStringLiteral("door_west_entered"));
        QCOMPARE(panel->requiresField()->text(), QStringLiteral("has_key"));
        type(panel->requiresField(), QStringLiteral(" has_key, lamp_lit,, has_key "));
        QCOMPARE(current().trigger.requirements,
                 (QStringList{QStringLiteral("has_key"), QStringLiteral("lamp_lit")}));
        QCOMPARE(panel->requiresField()->text(), QStringLiteral("has_key, lamp_lit"));
        type(panel->eventField(), QStringLiteral("door_opened"));
        QCOMPARE(current().trigger.event, QStringLiteral("door_opened"));

        // The state it carries, from the keys the flow declares.
        QListWidget* state = panel->stateList();
        QCOMPARE(state->count(), 2);
        QCOMPARE(state->item(0)->checkState(), Qt::Checked);
        state->item(0)->setCheckState(Qt::Unchecked);  // inventory
        QCOMPARE(current().persist.state, QStringList{QStringLiteral("player.hp")});
        panel->newStateField()->setText(QStringLiteral("coins"));
        emit panel->newStateField()->returnPressed();
        QCOMPARE(document.project().flow.state.size(), size_t{3});
        QCOMPARE(document.project().flow.state.at(0).key, QStringLiteral("coins"));  // sorted
        QVERIFY(current().persist.state.contains(QStringLiteral("coins")));
        QVERIFY(panel->newStateField()->text().isEmpty());
        QCOMPARE(document.commands().undoText(), QStringLiteral("Declare state key coins"));
        document.commands().undo();  // one step
        QCOMPARE(document.project().flow.state.size(), size_t{2});
        QVERIFY(!current().persist.state.contains(QStringLiteral("coins")));

        // The entities it carries, each onto a mark of the destination.
        QComboBox* mark = panel->markBox(QStringLiteral("player"));
        QVERIFY(mark);
        QCOMPARE(mark->currentData().toString(), QStringLiteral("spawn_west_door"));
        QVERIFY(panel->carryEntityBox()->findData(QStringLiteral("player")) < 0);
        QVERIFY(panel->carryEntityBox()->findData(QStringLiteral("torch")) >= 0);
        QCOMPARE(panel->carryMarkBox()->currentData().toString(),
                 QStringLiteral("spawn_west_door"));
        choose(panel->carryEntityBox(), QStringLiteral("torch"));
        panel->carryButton()->click();
        QCOMPARE(current().persist.entities.value(QStringLiteral("torch")),
                 QStringLiteral("spawn_west_door"));
        QTRY_VERIFY(panel->markBox(QStringLiteral("torch")));
        document.commands().undo();
        QTRY_VERIFY(!panel->markBox(QStringLiteral("torch")));
        QCOMPARE(current().persist.entities.size(), 1);
    }

    void transitionTypesAreEditedInTheProject()
    {
        QTemporaryDir dir;
        QVERIFY(copyFixture(dir));
        MainWindow window;
        QVERIFY(window.openProject(dir.path()).isEmpty());
        window.show();
        ProjectDocument& document = *window.document();
        TransitionTypesPanel* list = window.transitionTypesPanel();
        TransitionTypePanel* panel = window.transitionTypePanel();
        const auto type = [](QLineEdit* field, const QString& text) {
            field->setText(text);
            field->setModified(true);
            emit field->editingFinished();
        };
        const auto file = [&](const QString& id) {
            return document.project().transitionTypes.value(id).content;
        };

        // A built-in type, chosen in the list, shows read-only until it is customized.
        window.projectPanel()->setCurrentWidget(list);
        list->setCurrentType(QStringLiteral("fade"));
        QCOMPARE(window.contextPanel()->currentWidget(), static_cast<QWidget*>(panel));
        QCOMPARE(panel->nameField()->text(), QStringLiteral("Fade"));
        QVERIFY(!panel->nameField()->isEnabled());
        QCOMPARE(panel->optionList()->count(), 2);
        QVERIFY(!panel->customizeButton()->isHidden());
        panel->customizeButton()->click();
        QVERIFY(document.project().transitionTypes.contains(QStringLiteral("fade")));
        QVERIFY(panel->nameField()->isEnabled());
        QVERIFY(!panel->idField()->isEnabled());  // a customized one keeps its id
        QCOMPARE(list->typeRow(QStringLiteral("fade"))->text(),
                 QStringLiteral("Fade (customized)"));
        panel->revertButton()->click();
        QVERIFY(!document.project().transitionTypes.contains(QStringLiteral("fade")));

        // A new one of the project's own: its name, id and options.
        list->newButton()->click();
        QString id = list->currentType();
        QCOMPARE(id, QStringLiteral("transition"));
        QCOMPARE(panel->type(), id);
        type(panel->nameField(), QStringLiteral("Ripple"));
        QCOMPARE(file(id).value(QStringLiteral("name")), QJsonValue(QStringLiteral("Ripple")));
        type(panel->descriptionField(), QStringLiteral("Rings spread."));
        panel->addOptionButton()->click();
        panel->addOptionButton()->click();
        QCOMPARE(panel->optionList()->count(), 2);
        QCOMPARE(panel->optionList()->currentRow(), 1);
        QCOMPARE(panel->optionNameField()->text(), QStringLiteral("option_2"));
        type(panel->optionNameField(), QStringLiteral("direction"));
        const int string = panel->optionTypeBox()->findData(QStringLiteral("string"));
        panel->optionTypeBox()->setCurrentIndex(string);
        emit panel->optionTypeBox()->activated(string);
        QVERIFY(!panel->optionChoicesField()->isHidden());
        type(panel->optionChoicesField(), QStringLiteral("in, out"));
        type(panel->optionDefaultField(), QStringLiteral("out"));
        QCOMPARE(file(id).value(QStringLiteral("options")).toArray().at(1).toObject(),
                 (QJsonObject{{QStringLiteral("name"), QStringLiteral("direction")},
                              {QStringLiteral("type"), QStringLiteral("string")},
                              {QStringLiteral("choices"), QJsonArray{"in", "out"}},
                              {QStringLiteral("default"), QStringLiteral("out")}}));
        // One it couldn't read is refused, with why.
        type(panel->optionDefaultField(), QStringLiteral("sideways"));
        QVERIFY(panel->message().contains(QStringLiteral("sideways")));
        QCOMPARE(file(id)
                     .value(QStringLiteral("options"))
                     .toArray()
                     .at(1)
                     .toObject()
                     .value(QStringLiteral("default")),
                 QJsonValue(QStringLiteral("out")));
        QCOMPARE(panel->optionDefaultField()->text(), QStringLiteral("out"));
        panel->moveOptionUpButton()->click();
        QCOMPARE(file(id)
                     .value(QStringLiteral("options"))
                     .toArray()
                     .at(0)
                     .toObject()
                     .value(QStringLiteral("name")),
                 QJsonValue(QStringLiteral("direction")));
        QCOMPARE(panel->optionList()->currentRow(), 0);

        // An effect of the type follows it when it is renamed.
        Effect effect;
        effect.type = id;
        window.flowScene()->setTransitionEffect(QStringLiteral("l1_to_l2"), effect);
        type(panel->idField(), QStringLiteral("fade"));  // taken
        QVERIFY(panel->message().contains(QStringLiteral("fade")));
        type(panel->idField(), QStringLiteral("ripple"));
        id = QStringLiteral("ripple");
        QCOMPARE(list->currentType(), id);
        QCOMPARE(panel->type(), id);
        QCOMPARE(document.findTransition(u"l1_to_l2")->effect->type, id);

        // Selecting a transition in the flow shows it instead; the list shows the type again.
        window.flowScene()->select({}, {QStringLiteral("l1_to_l2")});
        QCOMPARE(window.contextPanel()->currentWidget(),
                 static_cast<QWidget*>(window.transitionPanel()));
        QCOMPARE(window.transitionPanel()->typeBox()->currentText(), QStringLiteral("Ripple"));
        list->setCurrentType(QStringLiteral("fade"));
        QCOMPARE(window.contextPanel()->currentWidget(), static_cast<QWidget*>(panel));

        // Removed, it's gone; the effect keeps its id, as one the project doesn't have.
        list->setCurrentType(id);
        list->removeButton()->click();
        QVERIFY(!list->typeRow(id));
        QVERIFY(!document.project().transitionTypes.contains(id));
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
