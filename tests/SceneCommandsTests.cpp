#include <QTest>

#include "Support.h"
#include "core/ProjectDocument.h"
#include "core/SceneCommands.h"
#include "core/SceneTree.h"

using namespace shoal::core;

namespace {

const QString LEVEL_1 = QStringLiteral("level_1");
const QString LEVEL_2 = QStringLiteral("level_2");

Project fixtureProject()
{
    return *parseProject(support::readTree(support::fixturePath(QStringLiteral("basic")))).project;
}

const Scene& scene(const ProjectDocument& document, const QString& id)
{
    return *document.project().scenes.find(id);
}

/// Pushes `command`, then checks that undo restores the project exactly and redo
/// reapplies it.
template <typename C, typename... Args>
bool pushAndCheckUndo(ProjectDocument& document, Args&&... args)
{
    const Project before = document.project();
    document.commands().push(std::make_unique<C>(document, std::forward<Args>(args)...));
    const Project after = document.project();
    document.commands().undo();
    if (document.project() != before) {
        return false;
    }
    document.commands().redo();
    return document.project() == after;
}

}  // namespace

class SceneCommandsTests : public QObject {
    Q_OBJECT
private slots:
    void walksTheTree()
    {
        const Project project = fixtureProject();
        const Scene& level1 = project.scenes[LEVEL_1];
        const EntityPath front{3, 1};
        QCOMPARE(findEntity(level1, u"mark_altar_front"), front);
        QCOMPARE(entityAt(level1, front)->id, QStringLiteral("mark_altar_front"));
        QCOMPARE(layerOf(level1, front), QStringLiteral("world"));
        QVERIFY(!entityAt(level1, {3, 2}));
        QVERIFY(findEntity(level1, u"nobody").isEmpty());
        QVERIFY(entityIds(level1).contains(QStringLiteral("mark_altar")));
        QCOMPARE(outermost({{3, 1}, {1}, {3}, {3, 0}, {1}}), (QList<EntityPath>{{1}, {3}}));
        QCOMPARE(layerIndex(level1, u"hud"), 2);
        QCOMPARE(pathAfterRemoval({3, 1}, {2}), (EntityPath{2, 1}));
        QCOMPARE(pathAfterRemoval({3, 1}, {3, 0}), (EntityPath{3, 0}));
        QCOMPARE(pathAfterRemoval({1}, {3, 0}), (EntityPath{1}));
        QCOMPARE(pathAfterRemoval({4}, {3, 0}), (EntityPath{4}));
    }

    void entityIdsAreUniqueInTheirScene()
    {
        ProjectDocument document(fixtureProject());
        QVERIFY(!document.isEntityIdAvailable(LEVEL_1, u"mark_altar"));
        QVERIFY(document.isEntityIdAvailable(LEVEL_1, u"mark_altar", {3, 0}));
        QVERIFY(document.isEntityIdAvailable(LEVEL_2, u"mark_altar"));  // another scene
        QVERIFY(!document.isEntityIdAvailable(LEVEL_1, u"not valid"));
        QCOMPARE(document.uniqueEntityId(LEVEL_1, QStringLiteral("player")),
                 QStringLiteral("player_2"));
    }

    void addsEntitiesAnywhereInTheTree()
    {
        ProjectDocument document(fixtureProject());
        Entity mark;
        mark.id = QStringLiteral("mark_altar_back");
        mark.type = EntityType::Mark;
        QVERIFY(pushAndCheckUndo<AddEntityCommand>(document, LEVEL_1, EntityPath{3, 1}, mark));
        QCOMPARE(entityAt(scene(document, LEVEL_1), {3, 1})->id, mark.id);
        QCOMPARE(entityAt(scene(document, LEVEL_1), {3, 2})->id,
                 QStringLiteral("mark_altar_front"));
        QCOMPARE(document.commands().undoText(), QStringLiteral("Add mark_altar_back"));
    }

    void removesEntitiesWithTheirChildren()
    {
        ProjectDocument document(fixtureProject());
        const auto count = scene(document, LEVEL_1).entities.size();
        // The group's child is listed too, but goes with the group.
        QVERIFY(pushAndCheckUndo<RemoveEntitiesCommand>(document, LEVEL_1,
                                                        QList<EntityPath>{{3, 0}, {1}, {3}}));
        QCOMPARE(scene(document, LEVEL_1).entities.size(), count - 2);
        QVERIFY(findEntity(scene(document, LEVEL_1), u"player").isEmpty());
        QCOMPARE(document.commands().undoText(), QStringLiteral("Remove 2 entities"));
    }

    void editsOnlyAnEntitysOwnFields()
    {
        ProjectDocument document(fixtureProject());
        Entity group = *entityAt(scene(document, LEVEL_1), {3});
        group.transform.pos = Point{10, 20};
        group.children.clear();  // ignored: children aren't the edit's to change
        QVERIFY(pushAndCheckUndo<EditEntitiesCommand>(
            document, LEVEL_1, QList<std::pair<EntityPath, Entity>>{{{3}, group}},
            QStringLiteral("Move altar_group")));
        const Entity* edited = entityAt(scene(document, LEVEL_1), {3});
        QCOMPARE(edited->transform.pos, (Point{10, 20}));
        QCOMPARE(edited->children.size(), size_t(2));
    }

    void renamingAnEntityRenamesItInTransitions()
    {
        ProjectDocument document(fixtureProject());
        Entity player = *entityAt(scene(document, LEVEL_1), {1});
        player.id = QStringLiteral("hero");
        QVERIFY(pushAndCheckUndo<EditEntitiesCommand>(
            document, LEVEL_1, QList<std::pair<EntityPath, Entity>>{{{1}, player}},
            QStringLiteral("Rename")));
        Entity mark = *entityAt(scene(document, LEVEL_2), {1});
        mark.id = QStringLiteral("west_door");
        QVERIFY(pushAndCheckUndo<EditEntitiesCommand>(
            document, LEVEL_2, QList<std::pair<EntityPath, Entity>>{{{1}, mark}},
            QStringLiteral("Rename")));

        const Transition* transition = document.findTransition(u"l1_to_l2");
        QCOMPARE(transition->persist.entities,
                 (QMap<QString, QString>{{QStringLiteral("hero"), QStringLiteral("west_door")}}));
    }

    void movesEntitiesBetweenParents()
    {
        ProjectDocument document(fixtureProject());
        // The bush, on `bg`, into the altar group at {3}: it gives up its layer.
        const EntityPath into = pathAfterRemoval({3, 0}, {2});
        QCOMPARE(into, (EntityPath{2, 0}));
        QVERIFY(pushAndCheckUndo<MoveEntityCommand>(document, LEVEL_1, EntityPath{2}, into));
        const Entity* bush = entityAt(scene(document, LEVEL_1), {2, 0});
        QCOMPARE(bush->sprite->path, QStringLiteral("props/bush.png"));
        QVERIFY(bush->layer.isEmpty());

        // A mark out of the group, to the top: it stays on the layer it was drawn on.
        QVERIFY(pushAndCheckUndo<MoveEntityCommand>(document, LEVEL_1, EntityPath{2, 1},
                                                    EntityPath{0}));
        const Entity* mark = entityAt(scene(document, LEVEL_1), {0});
        QCOMPARE(mark->id, QStringLiteral("mark_altar"));
        QCOMPARE(mark->layer, QStringLiteral("world"));

        // Reordering among siblings.
        QVERIFY(
            pushAndCheckUndo<MoveEntityCommand>(document, LEVEL_1, EntityPath{0}, EntityPath{1}));
        QCOMPARE(entityAt(scene(document, LEVEL_1), {1})->id, QStringLiteral("mark_altar"));
    }

    void removingALayerRemovesItsEntities()
    {
        ProjectDocument document(fixtureProject());
        QVERIFY(pushAndCheckUndo<RemoveLayerCommand>(document, LEVEL_1, 0));  // bg
        const Scene& level1 = scene(document, LEVEL_1);
        QCOMPARE(layerIndex(level1, u"bg"), -1);
        for (const Entity& entity : level1.entities) {
            QVERIFY(entity.layer != QStringLiteral("bg"));
        }
    }

    void renamingALayerMovesItsEntities()
    {
        ProjectDocument document(fixtureProject());
        Layer layer = scene(document, LEVEL_1).layers[1];
        layer.id = QStringLiteral("main");
        layer.parallax = Point{0.9, 1};
        QVERIFY(pushAndCheckUndo<EditLayerCommand>(document, LEVEL_1, 1, layer));
        QCOMPARE(document.commands().undoText(), QStringLiteral("Rename layer world to main"));
        QCOMPARE(entityAt(scene(document, LEVEL_1), {0})->layer, QStringLiteral("main"));
        QCOMPARE(entityAt(scene(document, LEVEL_1), {2})->layer, QStringLiteral("bg"));
    }

    void addsAndReordersLayers()
    {
        ProjectDocument document(fixtureProject());
        Layer layer;
        layer.id = QStringLiteral("fg");
        QVERIFY(pushAndCheckUndo<AddLayerCommand>(document, LEVEL_1, 2, layer));
        QVERIFY(pushAndCheckUndo<MoveLayerCommand>(document, LEVEL_1, 0, 3));
        QStringList ids;
        for (const Layer& l : scene(document, LEVEL_1).layers) {
            ids << l.id;
        }
        QCOMPARE(ids, (QStringList{QStringLiteral("world"), QStringLiteral("fg"),
                                   QStringLiteral("hud"), QStringLiteral("bg")}));
        QCOMPARE(document.commands().undoText(), QStringLiteral("Move layer bg"));
    }
};

QTEST_GUILESS_MAIN(SceneCommandsTests)
#include "SceneCommandsTests.moc"
