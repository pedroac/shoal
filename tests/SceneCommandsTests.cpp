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
        // level_1: bg {0} with the bush, world {1} with camera, player, altar_group (two
        // marks), secret_door, room_1_bounds and torch, and hud {2}.
        const Project project = fixtureProject();
        const Scene& level1 = project.scenes[LEVEL_1];
        const EntityPath front{1, 2, 1};
        QCOMPARE(findEntity(level1, u"mark_altar_front"), front);
        QCOMPARE(entityAt(level1, front)->id, QStringLiteral("mark_altar_front"));
        QVERIFY(!entityAt(level1, {1, 2, 2}));
        QVERIFY(findEntity(level1, u"nobody").isEmpty());
        QVERIFY(entityIds(level1).contains(QStringLiteral("mark_altar")));
        QCOMPARE(outermost({{3, 1}, {1}, {3}, {3, 0}, {1}}), (QList<EntityPath>{{1}, {3}}));
        QCOMPARE(pathAfterRemoval({3, 1}, {2}), (EntityPath{2, 1}));
        QCOMPARE(pathAfterRemoval({3, 1}, {3, 0}), (EntityPath{3, 0}));
        QCOMPARE(pathAfterRemoval({1}, {3, 0}), (EntityPath{1}));
        QCOMPARE(pathAfterRemoval({4}, {3, 0}), (EntityPath{4}));
    }

    void namesWhatEntitiesAre()
    {
        const Scene level1 = fixtureProject().scenes[LEVEL_1];
        const auto kind = [&](const EntityPath& path) { return kindOf(*entityAt(level1, path)); };
        QCOMPARE(kind({1, 0}), QStringLiteral("camera"));
        QCOMPARE(kind({1, 1}), QStringLiteral("sprite"));
        QCOMPARE(kind({1, 2}), QStringLiteral("group"));
        QCOMPARE(kind({1, 2, 0}), QStringLiteral("mark"));
        QCOMPARE(kind({1, 4}), QStringLiteral("region"));
        QCOMPARE(kind({1, 5}), QStringLiteral("animation"));
        Entity rect;
        rect.size = Extent::px({1, 1});
        rect.fill = Color{};
        QCOMPARE(kindOf(rect), QStringLiteral("rect"));
        QCOMPARE(describe(rect), QStringLiteral("rect"));
        rect.id = QStringLiteral("box");
        QCOMPARE(describe(rect), QStringLiteral("box"));
    }

    void placesRectsPivotsAndAnchors()
    {
        // An image's size and pivot come from its sprite until it has its own.
        const ImageMetricsFn metrics = [](const Image&) {
            return std::optional(ImageMetrics{{40, 20}, {0.25, 1}});
        };
        const Frame none;
        Entity sprite;
        sprite.image = Image{QStringLiteral("hero"), QStringLiteral("a.png"), {}, {}, {}, {}};
        QCOMPARE(entityRect(sprite, none, metrics), std::optional(RectF{-10, -20, 40, 20}));
        QVERIFY(!entityRect(sprite, none));  // with no sheets to read
        sprite.size = Extent::px({80, 40});
        QCOMPARE(entityRect(sprite, none, metrics), std::optional(RectF{-20, -40, 80, 40}));
        sprite.pivot = Point{0.5, 0.5};
        QCOMPARE(entityRect(sprite, none, metrics), std::optional(RectF{-40, -20, 80, 40}));
        QVERIFY(!entityRect(Entity{}, none));
        QCOMPARE(entityPivot(Entity{}), (Point{0, 0}));

        // A parent 100 x 50 about its center offers its rect to measure against.
        Entity parent;
        parent.size = Extent::px({100, 50});
        parent.pivot = Point{0.5, 0.5};
        const Frame frame = childFrame(parent, none);
        QCOMPARE(frame.rect, std::optional(RectF{-50, -25, 100, 50}));
        QVERIFY(!frame.grid);
        QVERIFY(frame.offers(Unit::Percent));
        QVERIFY(!frame.offers(Unit::Cell));
        QVERIFY(!none.offers(Unit::Percent));

        // A position in pixels is from the parent's origin; one in percent from the
        // rect's top-left corner, each axis in its own unit.
        Entity child;
        child.transform.pos = Point{-8, -4};
        QCOMPARE(anchorPoint(frame, child.at), (Point{0, 0}));
        child.at = Anchor{{100, Unit::Percent}, {}};
        QCOMPARE(anchorPoint(frame, child.at), (Point{50, 0}));
        child.at.y = {50, Unit::Percent};
        QCOMPARE(anchorPoint(frame, child.at), (Point{50, 0}));
        child.at.y = {100, Unit::Percent};
        QCOMPARE(anchorPoint(frame, child.at), (Point{50, 25}));
        QCOMPARE(anchorPoint(none, child.at), (Point{0, 0}));  // nothing to measure against

        // Sizes in percent follow the parent's rect.
        Entity bar;
        bar.size = Extent{{50, Unit::Percent}, Length::px(8)};
        QCOMPARE(entitySize(bar, frame), std::optional(SizeF{50, 8}));
        QCOMPARE(entitySize(bar, none), std::optional(SizeF{0, 8}));
        QCOMPARE(extentIn(*bar.size, SizeF{25, 16}, frame),
                 (Extent{{25, Unit::Percent}, Length::px(16)}));
        QCOMPARE(extentIn(*bar.size, SizeF{25, 16}, none), Extent::px({25, 16}));
        QCOMPARE(lengthIn(Unit::Percent, 10, 1, frame), std::optional(Length{20, Unit::Percent}));
        QVERIFY(!lengthIn(Unit::Cell, 10, 1, frame));

        // The top level measures against the scene.
        Scene scene;
        scene.size = Size{640, 360};
        QCOMPARE(sceneFrame(scene).rect, std::optional(RectF{0, 0, 640, 360}));

        // Changing a unit, or the pivot, keeps it where it is.
        const Entity inPercent = reanchored(frame, Entity{}, 0, Unit::Percent);
        QCOMPARE(inPercent.at.x, (Length{50, Unit::Percent}));  // the origin, half way
        QCOMPARE(inPercent.transform.pos, (Point{0, 0}));
        const Entity back = reanchored(frame, child, 1, Unit::Px);
        QCOMPARE(back.at.y, Length{});
        QCOMPARE(back.transform.pos, (Point{-8, 21}));
        QCOMPARE(reanchored(none, child, 0, Unit::Cell), child);  // not offered
        Entity box;
        box.size = Extent::px({10, 20});
        box.transform.pos = Point{100, 100};
        box.transform.rot = 90;
        box.transform.scale = Point{2, 1};
        // The pivot moves half the box right, which is down once turned, twice as far.
        QCOMPARE(positionForPivot(box, none, Point{0.5, 0}), (Point{100, 110}));
        QCOMPARE(positionForPivot(Entity{}, none, Point{0.5, 0}), (Point{0, 0}));
    }

    void placesChildrenOnTiles()
    {
        // 100 x 50 about its center: two columns of 40 x 20 and two rows fit, from
        // (-50, -25); children land on each tile's bottom-center.
        const Frame none;
        Entity tiles;
        tiles.size = Extent::px({100, 50});
        tiles.pivot = Point{0.5, 0.5};
        tiles.container = Container{ContainerType::Tiled, SizeF{40, 20}, Point{0.5, 1}, {}};
        const std::optional<TileGrid> grid = tileGrid(tiles, none);
        QVERIFY(grid);
        QCOMPARE(grid->origin, (Point{-50, -25}));
        QCOMPARE(grid->columns, 2);
        QCOMPARE(grid->rows, 2);
        QCOMPARE(grid->point(Cell{1, 0}), (Point{10, -5}));
        const Frame frame = childFrame(tiles, none);
        QVERIFY(frame.offers(Unit::Cell));

        // A position in tiles is from the tile's point; each axis in its own unit.
        Entity child;
        child.at = Anchor{{1, Unit::Cell}, {1, Unit::Cell}};
        QCOMPARE(anchorPoint(frame, child.at), (Point{10, 15}));
        child.at.y = {0, Unit::Percent};
        QCOMPARE(anchorPoint(frame, child.at), (Point{10, -25}));

        // The nearest tile, kept in the grid.
        QCOMPARE(grid->nearest(0, -28), 0);
        QCOMPARE(grid->nearest(0, 500), 1);
        QCOMPARE(grid->nearest(1, -500), 0);
        // A size in tiles.
        Entity wide;
        wide.size = Extent{{2, Unit::Cell}, {0.5, Unit::Cell}};
        QCOMPARE(entitySize(wide, frame), std::optional(SizeF{80, 10}));

        // A drop goes on the tile nearest to it, keeping its offset.
        Entity dropped;
        dropped.at = Anchor{{0, Unit::Cell}, {0, Unit::Cell}};
        dropped.transform.pos = Point{4, -6};
        const Entity moved = movedTo(frame, dropped, Point{14, 12});
        QCOMPARE(moved.at, (Anchor{{1, Unit::Cell}, {1, Unit::Cell}}));
        QCOMPARE(moved.transform.pos, (Point{4, -6}));

        // No rect, no tiles; a free container has no grid either.
        Entity noRect = tiles;
        noRect.size.reset();
        QVERIFY(!tileGrid(noRect, none));
        QVERIFY(!tileGrid(Entity{}, none));
        QCOMPARE(anchorPoint(childFrame(noRect, none), child.at), (Point{0, 0}));

        // Tiling a container keeps its children where they are, on the nearest tiles, and
        // freeing it does too, in pixels.
        Entity free = tiles;
        free.container.reset();
        const Frame freeFrame = childFrame(free, none);
        Entity loose;
        loose.transform.pos = Point{12, 14};
        loose.at = Anchor{{0, Unit::Percent}, {0, Unit::Percent}};  // at (-38, -11)
        loose.size = Extent{{10, Unit::Percent}, Length::px(4)};
        const Entity tiled = placedInContainer(freeFrame, frame, loose);
        QCOMPARE(tiled.at, (Anchor{{0, Unit::Cell}, {0, Unit::Cell}}));
        QCOMPARE(tiled.transform.pos, (Point{-8, -6}));
        QCOMPARE(tiled.size, loose.size);  // the same rect, the same percentage
        const Entity freed = placedInContainer(frame, freeFrame, tiled);
        QCOMPARE(freed.at, Anchor{});
        QCOMPARE(freed.transform.pos, (Point{-38, -11}));
        // A percentage stays one where there is a rect, its offset making up the rest.
        const Entity kept = placedInContainer(freeFrame, freeFrame, loose);
        QCOMPARE(kept, loose);
        // Out of any rect, it is in pixels.
        const Entity out = placedInContainer(freeFrame, none, loose);
        QCOMPARE(out.at, Anchor{});
        QCOMPARE(out.size, std::optional(Extent::px({10, 4})));
    }

    void combinesTints()
    {
        const Color received{255, 128, 0, 128};
        Entity entity;
        entity.tint = Color{128, 255, 255, 255};
        QCOMPARE(effectiveTint(entity, received), (Color{128, 128, 0, 128}));
        entity.tintMode = TintMode::Inherit;
        QCOMPARE(effectiveTint(entity, received), received);
        entity.tintMode = TintMode::Override;
        QCOMPARE(effectiveTint(entity, received), entity.tint);
        QCOMPARE(Color::fromName(u"#FF8000"), std::optional(Color{255, 128, 0, 255}));
        QCOMPARE(Color::fromName(u"#ff800080"), std::optional(Color{255, 128, 0, 128}));
        QVERIFY(!Color::fromName(u"#ff80"));
        QVERIFY(!Color::fromName(u"ff8000"));
        QVERIFY(!Color::fromName(u"#gg8000"));
        QCOMPARE((Color{255, 128, 0, 255}).name(), QStringLiteral("#ff8000"));
        QCOMPARE((Color{255, 128, 0, 128}).name(), QStringLiteral("#ff800080"));
    }

    void entityIdsAreUniqueInTheirScene()
    {
        ProjectDocument document(fixtureProject());
        QVERIFY(!document.isEntityIdAvailable(LEVEL_1, u"mark_altar"));
        QVERIFY(document.isEntityIdAvailable(LEVEL_1, u"mark_altar", {1, 2, 0}));
        QVERIFY(document.isEntityIdAvailable(LEVEL_2, u"mark_altar"));  // another scene
        QVERIFY(!document.isEntityIdAvailable(LEVEL_1, u"not valid"));
        QCOMPARE(document.uniqueEntityId(LEVEL_1, QStringLiteral("player")),
                 QStringLiteral("player_2"));

        // l1_to_l2 carries the player away from level 1 onto spawn_west_door in level 2.
        const QStringList l1ToL2{QStringLiteral("l1_to_l2")};
        QCOMPARE(document.transitionsNamingEntity(LEVEL_1, u"player"), l1ToL2);
        QCOMPARE(document.transitionsNamingEntity(LEVEL_2, u"spawn_west_door"), l1ToL2);
        QVERIFY(document.transitionsNamingEntity(LEVEL_2, u"player").isEmpty());
        QVERIFY(document.transitionsNamingEntity(LEVEL_1, u"spawn_west_door").isEmpty());
    }

    void addsEntitiesAnywhereInTheTree()
    {
        ProjectDocument document(fixtureProject());
        Entity mark;
        mark.id = QStringLiteral("mark_altar_back");
        QVERIFY(pushAndCheckUndo<AddEntityCommand>(document, LEVEL_1, EntityPath{1, 2, 1}, mark));
        QCOMPARE(entityAt(scene(document, LEVEL_1), {1, 2, 1})->id, mark.id);
        QCOMPARE(entityAt(scene(document, LEVEL_1), {1, 2, 2})->id,
                 QStringLiteral("mark_altar_front"));
        QCOMPARE(document.commands().undoText(), QStringLiteral("Add mark_altar_back"));
    }

    void removesEntitiesWithTheirChildren()
    {
        ProjectDocument document(fixtureProject());
        const auto count = entityAt(scene(document, LEVEL_1), {1})->children.size();
        // The group's child is listed too, but goes with the group.
        QVERIFY(pushAndCheckUndo<RemoveEntitiesCommand>(
            document, LEVEL_1, QList<EntityPath>{{1, 2, 0}, {1, 1}, {1, 2}}));
        QCOMPARE(entityAt(scene(document, LEVEL_1), {1})->children.size(), count - 2);
        QVERIFY(findEntity(scene(document, LEVEL_1), u"player").isEmpty());
        QCOMPARE(document.commands().undoText(), QStringLiteral("Remove 2 entities"));
    }

    void editsOnlyAnEntitysOwnFields()
    {
        ProjectDocument document(fixtureProject());
        Entity group = *entityAt(scene(document, LEVEL_1), {1, 2});
        group.transform.pos = Point{10, 20};
        group.children.clear();  // ignored: children aren't the edit's to change
        QVERIFY(pushAndCheckUndo<EditEntitiesCommand>(
            document, LEVEL_1, QList<std::pair<EntityPath, Entity>>{{{1, 2}, group}},
            QStringLiteral("Move altar_group")));
        const Entity* edited = entityAt(scene(document, LEVEL_1), {1, 2});
        QCOMPARE(edited->transform.pos, (Point{10, 20}));
        QCOMPARE(edited->children.size(), size_t(2));
    }

    void renamingAnEntityRenamesItInTransitions()
    {
        ProjectDocument document(fixtureProject());
        Entity player = *entityAt(scene(document, LEVEL_1), {1, 1});
        player.id = QStringLiteral("hero");
        QVERIFY(pushAndCheckUndo<EditEntitiesCommand>(
            document, LEVEL_1, QList<std::pair<EntityPath, Entity>>{{{1, 1}, player}},
            QStringLiteral("Rename")));
        Entity mark = *entityAt(scene(document, LEVEL_2), {0, 1});
        mark.id = QStringLiteral("west_door");
        QVERIFY(pushAndCheckUndo<EditEntitiesCommand>(
            document, LEVEL_2, QList<std::pair<EntityPath, Entity>>{{{0, 1}, mark}},
            QStringLiteral("Rename")));

        const Transition* transition = document.findTransition(u"l1_to_l2");
        QCOMPARE(transition->persist.entities,
                 (QMap<QString, QString>{{QStringLiteral("hero"), QStringLiteral("west_door")}}));
    }

    void movesEntitiesBetweenParents()
    {
        ProjectDocument document(fixtureProject());
        // The bush, in bg, into the altar group: it stays where it was drawn, and the group
        // is at (2400, 600).
        QVERIFY(pushAndCheckUndo<MoveEntityCommand>(document, LEVEL_1, EntityPath{0, 0},
                                                    EntityPath{1, 2, 2}));
        const Entity* bush = entityAt(scene(document, LEVEL_1), {1, 2, 2});
        QCOMPARE(bush->image->sprite, QStringLiteral("props/bush.png"));
        QCOMPARE(bush->transform.pos, (Point{-1699.75, 50}));
        QCOMPARE(bush->transform.rot, -12.5);
        QCOMPARE(bush->transform.scale, (Point{2, 2}));
        QVERIFY(entityAt(scene(document, LEVEL_1), {0})->children.empty());

        // A mark out of the group, to the top.
        QVERIFY(pushAndCheckUndo<MoveEntityCommand>(document, LEVEL_1, EntityPath{1, 2, 0},
                                                    EntityPath{0}));
        const Entity* mark = entityAt(scene(document, LEVEL_1), {0});
        QCOMPARE(mark->id, QStringLiteral("mark_altar"));
        QCOMPARE(mark->transform.pos, (Point{2400, 600}));

        // Reordering among siblings.
        QVERIFY(
            pushAndCheckUndo<MoveEntityCommand>(document, LEVEL_1, EntityPath{0}, EntityPath{1}));
        QCOMPARE(entityAt(scene(document, LEVEL_1), {1})->id, QStringLiteral("mark_altar"));
        QCOMPARE(entityAt(scene(document, LEVEL_1), {1})->transform.pos, (Point{2400, 600}));
    }

    void movingUnderATurnedParentKeepsWhereTheEntityIsDrawn()
    {
        Project project = fixtureProject();
        Scene& level1 = project.scenes[LEVEL_1];
        Transform& group = entityAt(level1, {1, 2})->transform;
        group.pos = Point{100, 100};
        group.rot = 90;
        group.scale = Point{2, 2};
        Transform& bush = entityAt(level1, {0, 0})->transform;
        bush.pos = Point{100, 300};
        bush.rot = 370;
        bush.scale = Point{-1, 1};
        bush.flipY = true;
        ProjectDocument document(project);

        // 200 below the group, which turns a quarter clockwise and doubles: 100 along its x.
        QVERIFY(pushAndCheckUndo<MoveEntityCommand>(document, LEVEL_1, EntityPath{0, 0},
                                                    EntityPath{1, 2, 2}));
        const Transform inside = entityAt(scene(document, LEVEL_1), {1, 2, 2})->transform;
        QCOMPARE(inside.pos, (Point{100, 0}));
        QCOMPARE(inside.rot, 280.0);
        QCOMPARE(inside.scale, (Point{-0.5, 0.5}));
        QVERIFY(inside.flipY);

        // And back out, to where it started.
        QVERIFY(pushAndCheckUndo<MoveEntityCommand>(document, LEVEL_1, EntityPath{1, 2, 2},
                                                    EntityPath{0, 0}));
        const Transform outside = entityAt(scene(document, LEVEL_1), {0, 0})->transform;
        QCOMPARE(outside.pos, (Point{100, 300}));
        QCOMPARE(outside.rot, 370.0);
        QCOMPARE(outside.scale, (Point{-1, 1}));
    }

    void movingAnEntityKeepsItsPercentages()
    {
        Project project = fixtureProject();
        Scene& level1 = project.scenes[LEVEL_1];
        Entity panel;
        panel.id = QStringLiteral("panel");
        panel.transform.pos = Point{200, 100};
        panel.size = Extent::px({100, 50});
        panel.pivot = Point{0.5, 0.5};
        Entity button;
        button.id = QStringLiteral("button");
        // Its origin at (260, 130) on the scene's 3840 x 1080: 6.25% across.
        button.at = Anchor{{6.25, Unit::Percent}, {}};
        button.transform.pos = Point{20, 130};
        button.size = Extent{{1.25, Unit::Percent}, Length::px(10)};  // 48 wide
        level1.entities.push_back(panel);
        level1.entities.push_back(button);
        ProjectDocument document(project);
        QCOMPARE(level1.size, (Size{3840, 1080}));

        // Into the panel, at (60, 30) from its center: x still in percent, now of the panel's
        // rect, which starts at -50, and as wide as it was.
        QVERIFY(pushAndCheckUndo<MoveEntityCommand>(document, LEVEL_1, EntityPath{4},
                                                    EntityPath{3, 0}));
        const Entity* moved = entityAt(scene(document, LEVEL_1), {3, 0});
        QCOMPARE(moved->at, (Anchor{{6.25, Unit::Percent}, {}}));
        QCOMPARE(moved->transform.pos, (Point{103.75, 30}));
        QCOMPARE(moved->size, (std::optional(Extent{{48, Unit::Percent}, Length::px(10)})));
    }

    void movingIntoATiledContainerPicksTheNearestTile()
    {
        Project project = fixtureProject();
        Scene& level1 = project.scenes[LEVEL_1];
        Entity tiles;
        tiles.id = QStringLiteral("tiles");
        tiles.transform.pos = Point{100, 100};
        tiles.size = Extent::px({128, 64});
        tiles.container = Container{ContainerType::Tiled, SizeF{32, 32}, Point{0.5, 0.5}, {}};
        Entity crate;
        crate.id = QStringLiteral("crate");
        crate.transform.pos = Point{183, 150};  // (83, 50) in the tiles: near cell 2, 1
        level1.entities.push_back(tiles);
        level1.entities.push_back(crate);
        ProjectDocument document(project);

        QVERIFY(pushAndCheckUndo<MoveEntityCommand>(document, LEVEL_1, EntityPath{4},
                                                    EntityPath{3, 0}));
        const Entity* moved = entityAt(scene(document, LEVEL_1), {3, 0});
        QCOMPARE(moved->at, (Anchor{{2, Unit::Cell}, {1, Unit::Cell}}));
        QCOMPARE(moved->transform.pos, (Point{3, 2}));

        // And out again, where it was drawn, in pixels.
        QVERIFY(pushAndCheckUndo<MoveEntityCommand>(document, LEVEL_1, EntityPath{3, 0},
                                                    EntityPath{4}));
        moved = entityAt(scene(document, LEVEL_1), {4});
        QCOMPARE(moved->at, Anchor{});
        QCOMPARE(moved->transform.pos, (Point{183, 150}));
    }

    void arrangesEntitiesAmongTheirSiblings()
    {
        // world's children: camera 0, player 1, altar_group 2, secret_door 3,
        // room_1_bounds 4, torch 5.
        const Scene level1 = fixtureProject().scenes[LEVEL_1];
        const EntityPath world{1};
        const EntityPath player{1, 1};
        const auto ordersOf = [&](const QList<EntityPath>& paths, Arrange arrange) {
            return arrangedOrders(level1, paths, arrange);
        };
        using Orders = QMap<EntityPath, QList<int>>;

        QCOMPARE(ordersOf({player}, Arrange::Forward), (Orders{{world, {0, 2, 1, 3, 4, 5}}}));
        QCOMPARE(ordersOf({player}, Arrange::ToFront), (Orders{{world, {0, 2, 3, 4, 5, 1}}}));
        QCOMPARE(ordersOf({player}, Arrange::Backward), (Orders{{world, {1, 0, 2, 3, 4, 5}}}));
        QCOMPARE(ordersOf({{1, 5}}, Arrange::ToBack), (Orders{{world, {5, 0, 1, 2, 3, 4}}}));
        // Several move as one block, keeping their order.
        QCOMPARE(ordersOf({{1, 1}, {1, 2}}, Arrange::Backward),
                 (Orders{{world, {1, 2, 0, 3, 4, 5}}}));
        QCOMPARE(ordersOf({{1, 4}, {1, 1}}, Arrange::ToFront),
                 (Orders{{world, {0, 2, 3, 5, 1, 4}}}));
        // At the top level, as anywhere.
        QCOMPARE(ordersOf({{0}}, Arrange::ToFront), (Orders{{{}, {1, 2, 0}}}));

        // Nothing to do: already in front or behind.
        QVERIFY(ordersOf({{1, 5}}, Arrange::ToFront).isEmpty());
        QVERIFY(ordersOf({{1, 5}}, Arrange::Forward).isEmpty());
        QVERIFY(ordersOf({{1, 0}}, Arrange::Backward).isEmpty());
        QVERIFY(ordersOf({{2}}, Arrange::ToFront).isEmpty());
        QVERIFY(ordersOf({}, Arrange::ToFront).isEmpty());

        // Children trade places with their siblings.
        const Orders children = ordersOf({{1, 2, 0}}, Arrange::ToFront);
        QCOMPARE(children, (Orders{{{1, 2}, {1, 0}}}));
        // A path follows its own list and its ancestors' ones.
        const Orders both = ordersOf({{1, 2, 0}, player}, Arrange::ToFront);
        QCOMPARE(both.size(), 2);
        QCOMPARE(pathAfterReorder({1, 2, 0}, both), (EntityPath{1, 1, 1}));
        QCOMPARE(pathAfterReorder(player, both), (EntityPath{1, 5}));
        QCOMPARE(pathAfterReorder({1, 3}, both), (EntityPath{1, 2}));
        QCOMPARE(pathAfterReorder({0}, both), (EntityPath{0}));
    }

    void reordersEntities()
    {
        ProjectDocument document(fixtureProject());
        const QList<int> order{0, 2, 1, 3, 4, 5};
        QVERIFY(pushAndCheckUndo<ReorderEntitiesCommand>(document, LEVEL_1, EntityPath{1}, order,
                                                         QStringLiteral("Bring Forward")));
        QCOMPARE(entityAt(scene(document, LEVEL_1), {1, 2})->id, QStringLiteral("player"));
        QCOMPARE(entityAt(scene(document, LEVEL_1), {1, 1, 1})->id,
                 QStringLiteral("mark_altar_front"));
        QCOMPARE(document.commands().undoText(), QStringLiteral("Bring Forward"));

        QVERIFY(pushAndCheckUndo<ReorderEntitiesCommand>(document, LEVEL_1, EntityPath{1, 1},
                                                         QList<int>{1, 0}, QStringLiteral("x")));
        QCOMPARE(entityAt(scene(document, LEVEL_1), {1, 1, 0})->id,
                 QStringLiteral("mark_altar_front"));
    }
};

QTEST_GUILESS_MAIN(SceneCommandsTests)
#include "SceneCommandsTests.moc"
