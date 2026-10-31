#include <QSignalSpy>
#include <QTest>

#include "Support.h"
#include "core/ProjectDocument.h"
#include "editor/FlowScene.h"
#include "editor/FlowView.h"
#include "editor/SceneNodeItem.h"
#include "editor/TransitionEdgeItem.h"

using namespace shoal::core;
using shoal::editor::FlowScene;
using shoal::editor::FlowView;
using shoal::editor::SceneNodeItem;
using shoal::editor::TransitionEdgeItem;

namespace {

Project fixtureProject()
{
    return *parseProject(support::readTree(support::fixturePath(QStringLiteral("basic")))).project;
}

QStringList transitionIds(const ProjectDocument& document)
{
    QStringList ids;
    for (const Transition& transition : document.project().flow.transitions) {
        ids << transition.id;
    }
    return ids;
}

/// A shown view at actual size, so tests can press and drag with the mouse.
struct Fixture {
    ProjectDocument document{fixtureProject()};
    FlowScene scene{document};
    FlowView view{scene};

    Fixture()
    {
        view.resize(1400, 900);
        view.show();
        view.resetZoom();
        view.centerOn(scene.itemsBoundingRect().center());
    }

    QPoint at(QPointF scenePoint) const { return view.mapFromScene(scenePoint); }
    /// A point on a node's title, away from its port.
    QPoint onNode(const QString& id) const
    {
        return at(scene.node(id)->mapToScene(QPointF(40, 16)));
    }
    QPoint onPort(const QString& id) const
    {
        const SceneNodeItem* node = scene.node(id);
        return at(node->mapToScene(node->portRect().center()));
    }

    void drag(QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = {})
    {
        QWidget* viewport = view.viewport();
        QTest::mousePress(viewport, Qt::LeftButton, modifiers, from);
        QTest::mouseMove(viewport, from + (to - from) / 2);
        QTest::mouseMove(viewport, to);
        QTest::mouseRelease(viewport, Qt::LeftButton, modifiers, to);
    }
};

}  // namespace

class FlowSceneTests : public QObject {
    Q_OBJECT
private slots:
    void showsEveryScene()
    {
        ProjectDocument document(fixtureProject());
        FlowScene scene(document);
        for (const QString& id : document.project().scenes.keys()) {
            const SceneNodeItem* node = scene.node(id);
            QVERIFY2(node, qPrintable(id));
            const Point stored = document.project().flow.nodePositions.value(id);
            QCOMPARE(node->pos(), QPointF(stored.x, stored.y));
        }
        QVERIFY(scene.node(QStringLiteral("main_menu"))->isStart());
        QVERIFY(!scene.node(QStringLiteral("level_1"))->isStart());
        QCOMPARE(scene.node(QStringLiteral("level_1"))->modifierCount(), 2);
    }

    void drawsTransitionsBetweenScenesAsEdges()
    {
        ProjectDocument document(fixtureProject());
        FlowScene scene(document);
        const TransitionEdgeItem* edge = scene.edge(QStringLiteral("l1_to_l2"));
        QVERIFY(edge);
        QCOMPARE(edge->from(), QStringLiteral("level_1"));
        QCOMPARE(edge->to(), QStringLiteral("level_2"));
        QVERIFY(scene.edge(QStringLiteral("menu_start")));
        QVERIFY(scene.edge(QStringLiteral("l1_pause")));

        // `pop` and `push_modifier` lead to no node, so they are listed on the node instead.
        QVERIFY(!scene.edge(QStringLiteral("pause_close")));
        QVERIFY(!scene.edge(QStringLiteral("l1_altar")));
        QCOMPARE(scene.node(QStringLiteral("pause"))->exits(),
                 QStringList{QStringLiteral("pause_close → back")});
        QCOMPARE(scene.node(QStringLiteral("level_1"))->exits(),
                 QStringList{QStringLiteral("l1_altar → modifier altar_reveal")});
    }

    void listsATransitionToAMissingScene()
    {
        Project project = fixtureProject();
        project.flow.transitions[2].to = QStringLiteral("level_9");  // l1_to_l2
        ProjectDocument document(std::move(project));
        FlowScene scene(document);
        QVERIFY(!scene.edge(QStringLiteral("l1_to_l2")));
        QVERIFY(scene.node(QStringLiteral("level_1"))
                    ->exits()
                    .contains(QStringLiteral("l1_to_l2 → level_9 (missing)")));
    }

    void selectsATransitionWithoutAnEdgeOnItsLine()
    {
        Fixture f;
        const QString id = QStringLiteral("pause_close");
        SceneNodeItem* pause = f.scene.node(QStringLiteral("pause"));
        QCOMPARE(pause->exitIds(), QStringList{id});
        const QPoint line = f.at(pause->mapToScene(pause->exitRect(id).center()));
        QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, line);
        QCOMPARE(f.scene.selectedExit(), id);
        QCOMPARE(f.scene.selectedTransitionIds(), QStringList{id});
        QVERIFY(f.scene.selectedSceneIds().isEmpty());  // not the node

        // Selecting something else, or clicking nothing, unselects it.
        f.scene.select({QStringLiteral("level_1")});
        QVERIFY(f.scene.selectedExit().isEmpty());
        QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, line);
        QSignalSpy changed(&f.scene, &QGraphicsScene::selectionChanged);
        QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, f.at(QPointF(-2000, -2000)));
        QVERIFY(f.scene.selectedTransitionIds().isEmpty());
        QVERIFY(!changed.isEmpty());

        // Removing the selection removes it.
        f.scene.select({}, {id});
        f.scene.removeSelection();
        QVERIFY(!f.document.findTransition(id));
        QVERIFY(f.scene.selectedExit().isEmpty());
    }

    void anEditKeepsTheTransitionSelected()
    {
        Fixture f;
        const QString id = QStringLiteral("l1_to_l2");
        f.scene.select({}, {id});
        // Popping, it loses its edge: its exit is selected.
        Transition pop = *f.document.findTransition(id);
        pop.mode = TransitionMode::Pop;
        pop.to.clear();
        QVERIFY(f.scene.setTransition(id, pop, QStringLiteral("Pop")));
        QVERIFY(!f.scene.edge(id));
        QCOMPARE(f.scene.selectedExit(), id);
        // Renamed, under its new id; an id taken is refused.
        Transition renamed = pop;
        renamed.id = QStringLiteral("l1_back");
        QVERIFY(f.scene.setTransition(id, renamed, QStringLiteral("Rename")));
        QCOMPARE(f.scene.selectedTransitionIds(), QStringList{QStringLiteral("l1_back")});
        Transition taken = renamed;
        taken.id = QStringLiteral("l1_pause");
        QVERIFY(!f.scene.setTransition(QStringLiteral("l1_back"), taken, QStringLiteral("Taken")));
        // Back to a destination: its edge again.
        f.document.commands().undo();
        f.document.commands().undo();
        QVERIFY(f.scene.edge(id));
    }

    void edgesBetweenTheSameScenesBendApart()
    {
        ProjectDocument document(fixtureProject());
        FlowScene scene(document);
        const QString back =
            scene.addTransition(QStringLiteral("level_2"), QStringLiteral("level_1"));
        const QRectF there = scene.edge(QStringLiteral("l1_to_l2"))->boundingRect();
        const QRectF backAgain = scene.edge(back)->boundingRect();
        QVERIFY(there != backAgain);
        QVERIFY(qAbs(there.center().y() - backAgain.center().y()) > 20);
    }

    void placesScenesWithoutAPositionWithoutChangingTheProject()
    {
        Project project = fixtureProject();
        project.flow.nodePositions.remove(QStringLiteral("level_2"));
        ProjectDocument document(project);
        FlowScene scene(document);

        const SceneNodeItem* placed = scene.node(QStringLiteral("level_2"));
        const SceneNodeItem* pause = scene.node(QStringLiteral("pause"));
        QVERIFY(placed->pos().y() > pause->pos().y() + pause->bodyRect().height());
        QCOMPARE(document.project(), project);
        QCOMPARE(document.commands().count(), 0);

        // Placement is stable: other edits don't move it.
        scene.addScene(QPointF(2000, 2000));
        QCOMPARE(scene.node(QStringLiteral("level_2"))->pos(), placed->pos());
    }

    void followsUndoAndRedo()
    {
        ProjectDocument document(fixtureProject());
        FlowScene scene(document);
        const QString id = scene.addScene(QPointF(10.4, 20.6));
        QCOMPARE(id, QStringLiteral("scene"));
        QVERIFY(scene.node(id));
        QCOMPARE(scene.node(id)->pos(), QPointF(10, 21));
        QCOMPARE(scene.selectedSceneIds(), QStringList{id});

        document.commands().undo();
        QVERIFY(!scene.node(id));
        document.commands().redo();
        QVERIFY(scene.node(id));
    }

    void renamesScenes()
    {
        ProjectDocument document(fixtureProject());
        FlowScene scene(document);
        QVERIFY(!scene.renameScene(QStringLiteral("pause"), QStringLiteral("Level_1")));
        QVERIFY(!scene.renameScene(QStringLiteral("pause"), QStringLiteral("not an id")));
        QCOMPARE(document.commands().count(), 0);

        SceneNodeItem* node = scene.node(QStringLiteral("pause"));
        QVERIFY(scene.renameScene(QStringLiteral("pause"), QStringLiteral("paused")));
        QCOMPARE(scene.node(QStringLiteral("paused")), node);  // the same item, renamed
        QCOMPARE(node->sceneId(), QStringLiteral("paused"));
        QVERIFY(!scene.node(QStringLiteral("pause")));
        QCOMPARE(scene.edge(QStringLiteral("l1_pause"))->to(), QStringLiteral("paused"));

        document.commands().undo();
        QCOMPARE(scene.node(QStringLiteral("pause")), node);
        QCOMPARE(scene.edge(QStringLiteral("l1_pause"))->to(), QStringLiteral("pause"));
    }

    void setsTheStartScene()
    {
        ProjectDocument document(fixtureProject());
        FlowScene scene(document);
        scene.setStartScene(QStringLiteral("level_1"));
        QVERIFY(scene.node(QStringLiteral("level_1"))->isStart());
        QVERIFY(!scene.node(QStringLiteral("main_menu"))->isStart());
        scene.setStartScene(QStringLiteral("level_1"));  // already: no step
        QCOMPARE(document.commands().count(), 1);
    }

    void removesTheSelectionInOneStep()
    {
        ProjectDocument document(fixtureProject());
        const Project before = document.project();
        FlowScene scene(document);
        scene.select({QStringLiteral("level_1")}, {QStringLiteral("menu_start")});
        scene.removeSelection();

        QCOMPARE(document.commands().count(), 1);
        QCOMPARE(document.commands().undoText(), QStringLiteral("Remove 2 items"));
        QVERIFY(!scene.node(QStringLiteral("level_1")));
        // The level's own transitions go with it.
        QCOMPARE(transitionIds(document), QStringList{QStringLiteral("pause_close")});
        QVERIFY(!scene.edge(QStringLiteral("l1_pause")));

        document.commands().undo();
        QCOMPARE(document.project(), before);
        QVERIFY(scene.node(QStringLiteral("level_1")));
        QVERIFY(scene.edge(QStringLiteral("l1_to_l2")));
    }

    void aDragIsOneStep()
    {
        Fixture f;
        const QPointF start = f.scene.node(QStringLiteral("pause"))->pos();
        f.drag(f.onNode(QStringLiteral("pause")),
               f.onNode(QStringLiteral("pause")) + QPoint(37, 15));

        QCOMPARE(f.document.commands().count(), 1);
        QCOMPARE(f.document.commands().undoText(), QStringLiteral("Move scene pause"));
        // 300.5 + 15 is rounded, so flow.json gets whole numbers from the view.
        const Point moved = f.document.project().flow.nodePositions.value(QStringLiteral("pause"));
        QCOMPARE(moved, (Point{start.x() + 37, std::round(start.y() + 15)}));
        QCOMPARE(f.scene.node(QStringLiteral("pause"))->pos(), QPointF(moved.x, moved.y));

        f.document.commands().undo();
        QCOMPARE(f.scene.node(QStringLiteral("pause"))->pos(), start);
    }

    void draggingSeveralNodesIsOneStep()
    {
        Fixture f;
        f.scene.select({QStringLiteral("level_1"), QStringLiteral("level_2")});
        f.drag(f.onNode(QStringLiteral("level_1")),
               f.onNode(QStringLiteral("level_1")) + QPoint(0, 50));
        QCOMPARE(f.document.commands().count(), 1);
        QCOMPARE(f.document.commands().undoText(), QStringLiteral("Move 2 scenes"));
        QCOMPARE(f.document.project().flow.nodePositions.value(QStringLiteral("level_2")),
                 (Point{840, 130}));
    }

    void aClickMovesNothing()
    {
        Fixture f;
        const QPoint point = f.onNode(QStringLiteral("pause"));
        QTest::mouseClick(f.view.viewport(), Qt::LeftButton, {}, point);
        QCOMPARE(f.scene.selectedSceneIds(), QStringList{QStringLiteral("pause")});
        QCOMPARE(f.document.commands().count(), 0);
        QVERIFY(f.document.commands().isClean());
    }

    void draggingFromAPortAddsATransition()
    {
        Fixture f;
        f.drag(f.onPort(QStringLiteral("main_menu")), f.onNode(QStringLiteral("level_2")));
        const Transition* added = f.document.findTransition(u"main_menu_to_level_2");
        QVERIFY(added);
        QCOMPARE(added->from, QStringLiteral("main_menu"));
        QCOMPARE(added->to, QStringLiteral("level_2"));
        QCOMPARE(added->mode, TransitionMode::Replace);
        QVERIFY(f.scene.edge(added->id));
        QCOMPARE(f.scene.selectedTransitionIds(), QStringList{added->id});
        // The node didn't move.
        QCOMPARE(f.document.commands().count(), 1);
    }

    void aConnectionDroppedOnNothingAddsNothing()
    {
        Fixture f;
        f.drag(f.onPort(QStringLiteral("main_menu")),
               f.onPort(QStringLiteral("main_menu")) + QPoint(0, 400));
        f.drag(f.onPort(QStringLiteral("main_menu")), f.onNode(QStringLiteral("main_menu")));
        QCOMPARE(f.document.commands().count(), 0);
        QCOMPARE(f.scene.items().size(),
                 qsizetype(f.document.project().scenes.size() + 3));  // no stray line
    }
};

QTEST_MAIN(FlowSceneTests)
#include "FlowSceneTests.moc"
