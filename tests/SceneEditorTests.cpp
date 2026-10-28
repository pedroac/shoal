#include <QAction>
#include <QDropEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QtMath>
#include <cmath>

#include "Support.h"
#include "assets/SpriteSheet.h"
#include "core/ProjectDocument.h"
#include "editor/EntityItem.h"
#include "editor/FlowScene.h"
#include "editor/MainWindow.h"
#include "editor/SceneCanvas.h"
#include "editor/SceneTreePanel.h"
#include "editor/SceneView.h"
#include "editor/SheetBrowserPanel.h"
#include "editor/TransformHandle.h"

using namespace shoal::core;
using namespace shoal::editor;

namespace {

const QString LEVEL_1 = QStringLiteral("level_1");
const SheetItem IDLE_SPRITE{SheetItem::Kind::Sprite, QStringLiteral("hero"),
                            QStringLiteral("hero/idle.png")};
const SheetItem IDLE_ANIMATION{SheetItem::Kind::Animation, QStringLiteral("hero"),
                               QStringLiteral("idle")};

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/// The basic fixture with the hero sheet in art/, opened in a window on level_1.
struct Fixture {
    QTemporaryDir dir;
    MainWindow window;
    SceneView* view = nullptr;

    Fixture()
    {
        ProjectFiles files = support::readTree(support::fixturePath(QStringLiteral("basic")));
        for (const char* name : {"hero.sprat", "hero.png"}) {
            files[QStringLiteral("art/") + QLatin1StringView(name)] =
                readFile(support::fixturePath(QStringLiteral("sheets/") + QLatin1StringView(name)));
        }
        if (!support::writeTree(dir.path(), files) || !window.openProject(dir.path()).isEmpty()) {
            return;
        }
        window.resize(1400, 900);
        window.show();
        view = window.openScene(LEVEL_1);
        view->resetZoom();
        view->centerOn(320, 600);
    }

    SceneCanvas& canvas() const { return view->canvas(); }
    ProjectDocument& document() const { return *window.document(); }
    const Scene& scene() const { return *canvas().scene(); }
    QPoint at(QPointF scenePoint) const { return view->mapFromScene(scenePoint); }

    void drag(QPoint from, QPoint to)
    {
        QTest::mousePress(view->viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(view->viewport(), from + (to - from) / 2);
        QTest::mouseMove(view->viewport(), to);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, {}, to);
    }

    /// Drags `item` from the sheet browser over the view and drops it at `at`; returns
    /// whether the view accepted the drop. QDrag::exec() runs its own event loop, so the
    /// events are sent as it would send them.
    bool dropOnView(const SheetItem& item, QPoint point)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
        const QPointF at(point);  // the QPoint constructors are deprecated
#else
        const QPoint at = point;
#endif
        const std::unique_ptr<QMimeData> data(item.toMimeData());
        QDragEnterEvent enter(at, Qt::CopyAction, data.get(), Qt::LeftButton, {});
        QApplication::sendEvent(view->viewport(), &enter);
        if (!enter.isAccepted()) {
            return false;
        }
        QDragMoveEvent move(at, Qt::CopyAction, data.get(), Qt::LeftButton, {});
        QApplication::sendEvent(view->viewport(), &move);
        QDropEvent drop(at, Qt::CopyAction, data.get(), Qt::LeftButton, {});
        QApplication::sendEvent(view->viewport(), &drop);
        return drop.isAccepted();
    }
};

}  // namespace

class SceneEditorTests : public QObject {
    Q_OBJECT
private slots:
    void opensScenesInTabs()
    {
        Fixture f;
        QVERIFY(f.view);
        QTabWidget* tabs = f.window.tabs();
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(tabs->tabText(1), LEVEL_1);
        QCOMPARE(f.window.currentSceneView(), f.view);
        QCOMPARE(f.window.openScene(LEVEL_1), f.view);  // the same tab again
        QCOMPARE(tabs->count(), 2);

        // Double-clicking a node in the flow opens its scene too.
        emit f.window.flowScene()->sceneActivated(QStringLiteral("pause"));
        QCOMPARE(tabs->count(), 3);
        QCOMPARE(tabs->tabText(2), QStringLiteral("pause"));
    }

    void tabsFollowRenamesAndRemovals()
    {
        Fixture f;
        QVERIFY(f.window.flowScene()->renameScene(LEVEL_1, QStringLiteral("forest")));
        QCOMPARE(f.window.tabs()->tabText(1), QStringLiteral("forest"));
        QCOMPARE(f.canvas().sceneId(), QStringLiteral("forest"));
        QVERIFY(f.canvas().item({1}));

        f.window.flowScene()->select({QStringLiteral("forest")});
        f.window.flowScene()->removeSelection();
        QCOMPARE(f.window.tabs()->count(), 1);
    }

    void drawsWorldEntitiesWithTheirSprites()
    {
        Fixture f;
        const Scene& scene = f.scene();
        // Every entity on a world layer has an item, children under their parent's.
        forEachEntity(scene, [&](const Entity&, const EntityPath& path) {
            QVERIFY2(f.canvas().item(path), qPrintable(QString::number(path.first())));
        });
        QCOMPARE(f.canvas().item({3, 1})->parentItem(), f.canvas().item({3}));
        QCOMPARE(f.canvas().item({3, 1})->scenePos(), QPointF(2400, 680));

        // The door is drawn upright from its rotated atlas region, as sprat-lib places it.
        const auto sheet = f.window.sheets().sheet(QStringLiteral("hero"));
        QVERIFY(sheet);
        const auto* door = sheet->sprite(QStringLiteral("props/door.png"));
        QCOMPARE(f.canvas().item({4})->visualRect(),
                 shoal::assets::placeSprite(*door, 1, false, false).bounds);
        // The bush, scaled by 2 and turned by -12.5 degrees: its local x axis.
        const QPointF axis = f.canvas().item({2})->transform().map(QPointF(1, 0));
        QVERIFY(qFuzzyCompare(axis.x(), 2 * std::cos(qDegreesToRadians(-12.5))));
        QVERIFY(qFuzzyCompare(axis.y(), 2 * std::sin(qDegreesToRadians(-12.5))));
    }

    void drawsAPlaceholderForAMissingSprite()
    {
        Fixture f;
        Entity ghost;
        ghost.type = EntityType::Sprite;
        ghost.sprite = SpriteRef{QStringLiteral("hero"), QStringLiteral("nope.png"), {}};
        const EntityPath path = f.canvas().addEntity(ghost, QPointF(100, 100));
        QCOMPARE(f.canvas().item(path)->visualRect(), QRectF(-16, -16, 32, 32));
        QCOMPARE(f.scene().entities.back().layer, QStringLiteral("world"));  // the current one
    }

    void aDragMovesTheSelectionInOneStep()
    {
        Fixture f;
        const QPoint player = f.at(QPointF(320, 630));
        f.drag(player, player + QPoint(40, 10));
        QCOMPARE(f.document().commands().count(), 1);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Move player"));
        QCOMPARE(entityAt(f.scene(), {1})->transform.pos, (Point{360, 650}));
        f.document().commands().undo();
        QCOMPARE(f.canvas().item({1})->pos(), QPointF(320, 640));
    }

    void snapsToTheGrid()
    {
        Fixture f;
        f.canvas().setSnapping(true);
        const QPoint player = f.at(QPointF(320, 630));
        f.drag(player, player + QPoint(37, 13));
        // 357, 653 snaps to the 16-unit grid.
        QCOMPARE(entityAt(f.scene(), {1})->transform.pos, (Point{352, 656}));
    }

    void nudgesWithTheArrowKeys()
    {
        Fixture f;
        f.canvas().select({{1}});
        QTest::keyClick(f.view, Qt::Key_Right);
        QTest::keyClick(f.view, Qt::Key_Up, Qt::ShiftModifier);
        QCOMPARE(entityAt(f.scene(), {1})->transform.pos, (Point{321, 630}));
        QCOMPARE(f.document().commands().count(), 2);
    }

    void handlesFollowASingleSelection()
    {
        Fixture f;
        TransformHandle* rotate = f.canvas().rotateHandle();
        TransformHandle* scale = f.canvas().scaleHandle();
        QVERIFY(rotate && scale);
        QVERIFY(!rotate->isVisible() && !scale->isVisible());

        f.canvas().select({{1}});
        QVERIFY(rotate->isVisible() && scale->isVisible());
        const EntityItem* player = f.canvas().item({1});
        const QRectF rect = player->visualRect();
        QCOMPARE(rotate->scenePos(), player->mapToScene(QPointF(rect.center().x(), rect.top())));
        QCOMPARE(scale->scenePos(), player->mapToScene(rect.bottomRight()));

        // They follow a drag of the entity.
        const QPointF before = scale->scenePos();
        const QPoint at = f.at(QPointF(320, 630));
        f.drag(at, at + QPoint(40, 10));
        QCOMPARE(scale->scenePos(), before + QPointF(40, 10));

        f.canvas().select({{1}, {2}});
        QVERIFY(!rotate->isVisible() && !scale->isVisible());
        f.canvas().select({});
        QVERIFY(!rotate->isVisible());
    }

    void theRotateHandleTurnsTheEntityInOneStep()
    {
        Fixture f;
        f.canvas().select({{1}});
        const QPointF origin = f.canvas().item({1})->scenePos();
        // The handle's knob sits above its position, at a fixed distance on screen.
        const QPoint knob = f.at(f.canvas().rotateHandle()->scenePos()) - QPoint(0, 24);
        // A quarter turn clockwise (y points down) about the origin, Shift snapping it.
        const QPointF arm = f.view->mapToScene(knob) - origin;
        const QPoint target = f.at(origin + QPointF(-arm.y(), arm.x()));
        QTest::mousePress(f.view->viewport(), Qt::LeftButton, {}, knob);
        QTest::mouseEvent(QTest::MouseMove, f.view->viewport(), Qt::NoButton, Qt::ShiftModifier,
                          target);
        QTest::mouseRelease(f.view->viewport(), Qt::LeftButton, Qt::ShiftModifier, target);

        QCOMPARE(f.document().commands().count(), 1);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Rotate player"));
        QCOMPARE(entityAt(f.scene(), {1})->transform.rot, 90.0);
        QCOMPARE(entityAt(f.scene(), {1})->transform.pos, (Point{320, 640}));  // not moved
        f.document().commands().undo();
        QCOMPARE(entityAt(f.scene(), {1})->transform.rot, 0.0);
        QCOMPARE(f.canvas().item({1})->transform(), QTransform());
    }

    void theScaleHandleScalesTheEntity()
    {
        Fixture f;
        f.canvas().select({{1}});
        const QPointF origin = f.canvas().item({1})->scenePos();
        const QPointF corner = f.canvas().scaleHandle()->scenePos();
        f.drag(f.at(corner), f.at(origin + 2 * (corner - origin)));

        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Scale player"));
        const Point scale = entityAt(f.scene(), {1})->transform.scale;
        QVERIFY2(std::abs(scale.x - 2) < 0.05 && std::abs(scale.y - 2) < 0.05,
                 qPrintable(QStringLiteral("%1, %2").arg(scale.x).arg(scale.y)));
        // The handle stays on the outline, which has grown.
        QVERIFY(
            QLineF(f.canvas().scaleHandle()->scenePos(), origin + 2 * (corner - origin)).length() <
            2);
    }

    void aClickOnAHandleChangesNothing()
    {
        Fixture f;
        f.canvas().select({{1}});
        const QPoint corner = f.at(f.canvas().scaleHandle()->scenePos());
        QTest::mouseClick(f.view->viewport(), Qt::LeftButton, {}, corner);
        QCOMPARE(f.document().commands().count(), 0);
        QCOMPARE(f.canvas().selectedPaths(), QList<EntityPath>{{1}});
    }

    void deletesTheSelection()
    {
        Fixture f;
        f.canvas().select({{1}, {3, 0}});
        auto* remove = f.window.findChild<QAction*>(QStringLiteral("deleteAction"));
        QVERIFY(remove->isEnabled());
        remove->trigger();
        QVERIFY(findEntity(f.scene(), u"player").isEmpty());
        QVERIFY(findEntity(f.scene(), u"mark_altar").isEmpty());
        QVERIFY(f.canvas().selectedPaths().isEmpty());
        QCOMPARE(f.document().commands().count(), 1);
    }

    void hiddenAndLockedLayersAreLeftAlone()
    {
        Fixture f;
        f.canvas().setLayerHidden(QStringLiteral("bg"), true);
        QVERIFY(!f.canvas().item({2})->isVisible());
        f.canvas().setLayerLocked(QStringLiteral("world"), true);
        f.canvas().select({{1}});
        QVERIFY(f.canvas().selectedPaths().isEmpty());
        QVERIFY(f.document().commands().isClean());  // editor state, not the project's
    }

    void theTreeListsLayersAndFollowsTheSelection()
    {
        Fixture f;
        SceneTreePanel* panel = f.window.sceneTreePanel();
        QCOMPARE(panel->canvas(), &f.canvas());
        QCOMPARE(panel->tree()->topLevelItemCount(), 3);  // bg, world, hud
        QTreeWidgetItem* world = panel->layerRow(QStringLiteral("world"));
        QVERIFY(world);
        QCOMPARE(world->font(0).bold(), true);  // the current layer
        QVERIFY(panel->entityRow({3, 1}));
        QCOMPARE(panel->entityRow({3, 1})->parent(), panel->entityRow({3}));

        f.canvas().select({{1}});
        QVERIFY(panel->entityRow({1})->isSelected());
        panel->tree()->clearSelection();
        panel->entityRow({4})->setSelected(true);
        QCOMPARE(f.canvas().selectedPaths(), QList<EntityPath>{{4}});

        // The flow tab has no scene to show.
        f.window.tabs()->setCurrentIndex(0);
        QVERIFY(!panel->canvas());
        QVERIFY(!panel->isEnabled());
    }

    void droppingInTheTreeMovesEntities()
    {
        Fixture f;
        SceneTreePanel* panel = f.window.sceneTreePanel();
        // The player into the altar group, as its last child.
        QVERIFY(panel->dropEntity({1}, panel->entityRow({3}), SceneTreePanel::Drop::On));
        const EntityPath moved = findEntity(f.scene(), u"player");
        QCOMPARE(moved, (EntityPath{2, 2}));
        QCOMPARE(f.canvas().selectedPaths(), QList<EntityPath>{moved});
        QCOMPARE(f.document().commands().count(), 1);

        // Onto the bg layer: top-level again, drawn in front of what bg has.
        QVERIFY(panel->dropEntity(moved, panel->layerRow(QStringLiteral("bg")),
                                  SceneTreePanel::Drop::On));
        const EntityPath back = findEntity(f.scene(), u"player");
        QCOMPARE(back, (EntityPath{2}));
        QCOMPARE(entityAt(f.scene(), back)->layer, QStringLiteral("bg"));
        QCOMPARE(f.document().commands().count(), 2);
        f.document().commands().undo();
        QCOMPARE(findEntity(f.scene(), u"player"), (EntityPath{2, 2}));

        // Not into itself.
        QVERIFY(!panel->dropEntity({2}, panel->entityRow({2, 0}), SceneTreePanel::Drop::On));
    }

    void editsLayersFromThePanel()
    {
        Fixture f;
        SceneTreePanel* panel = f.window.sceneTreePanel();
        panel->addLayer();  // in front of world, the current layer
        QCOMPARE(f.scene().layers[2].id, QStringLiteral("layer"));
        QCOMPARE(f.canvas().currentLayer(), QStringLiteral("layer"));
        panel->moveCurrentLayerBack();
        QCOMPARE(f.scene().layers[1].id, QStringLiteral("layer"));
        panel->removeCurrentLayer();
        QCOMPARE(f.scene().layers.size(), size_t(3));
        QCOMPARE(f.document().commands().count(), 3);
    }

    void reloadsSpritesWhenTheSheetChanges()
    {
        Fixture f;
        const QRectF before = f.canvas().item({2})->visualRect();  // the bush
        QByteArray sheet = readFile(f.dir.filePath(QStringLiteral("art/hero.sprat")));
        sheet.replace("0 0 0 0 8 8 0 0 0 0 0 0 0 0 4:bush", "0 0 0 0 8 0 0 0 0 0 0 0 0 0 4:bush");
        QFile file(f.dir.filePath(QStringLiteral("art/hero.sprat")));
        QVERIFY(file.open(QIODevice::WriteOnly) && file.write(sheet) == sheet.size());
        file.close();
        QTRY_COMPARE(f.canvas().item({2})->visualRect(), before.translated(0, 8));
    }

    void theSheetBrowserListsSpritesAndAnimations()
    {
        Fixture f;
        SheetBrowserPanel* browser = f.window.sheetBrowser();
        QTreeWidgetItem* hero = browser->sheetRow(QStringLiteral("hero"));
        QVERIFY(hero);
        QCOMPARE(hero->childCount(), 2);
        QCOMPARE(hero->child(0)->text(0), QStringLiteral("Sprites (4)"));
        QCOMPARE(hero->child(1)->text(0), QStringLiteral("Animations (1)"));
        // In the sheet's order, by path, each with a thumbnail.
        QCOMPARE(hero->child(0)->child(2)->text(0), QStringLiteral("props/door.png"));
        QTreeWidgetItem* idle = browser->itemRow(IDLE_SPRITE);
        QVERIFY(idle);
        QVERIFY(!idle->icon(0).isNull());
        QCOMPARE(SheetBrowserPanel::itemOf(idle), IDLE_SPRITE);
        QVERIFY(browser->itemRow(IDLE_ANIMATION));
        QVERIFY(!SheetBrowserPanel::itemOf(hero));

        // The manifest declares a ui sheet that isn't there: it says why.
        QTreeWidgetItem* ui = browser->sheetRow(QStringLiteral("ui"));
        QVERIFY(ui);
        QCOMPARE(ui->text(0), QStringLiteral("ui (not loaded)"));
        QCOMPARE(ui->childCount(), 0);
        QVERIFY(ui->toolTip(0).startsWith(QStringLiteral("Couldn't load: ")));

        // A drag carries the sprite, not the sheet.
        const std::unique_ptr<QMimeData> data(
            browser->tree()->model()->mimeData({browser->tree()->indexFromItem(idle)}));
        QCOMPARE(SheetItem::fromMimeData(data.get()), IDLE_SPRITE);
    }

    void theSheetBrowserFilters()
    {
        Fixture f;
        SheetBrowserPanel* browser = f.window.sheetBrowser();
        browser->setFilter(QStringLiteral("DOOR"));
        const auto door = SheetItem{SheetItem::Kind::Sprite, QStringLiteral("hero"),
                                    QStringLiteral("props/door.png")};
        QVERIFY(!browser->itemRow(door)->isHidden());
        QVERIFY(browser->itemRow(IDLE_SPRITE)->isHidden());
        QVERIFY(browser->sheetRow(QStringLiteral("hero"))->child(1)->isHidden());  // animations
        QVERIFY(browser->sheetRow(QStringLiteral("ui"))->isHidden());
        browser->findChild<QLineEdit*>(QStringLiteral("sheetFilter"))->clear();
        QVERIFY(!browser->itemRow(IDLE_SPRITE)->isHidden());
    }

    void theSheetBrowserFollowsTheSheets()
    {
        Fixture f;
        SheetBrowserPanel* browser = f.window.sheetBrowser();
        // A sheet written again by sprat: the bush is renamed, keeping the string total.
        QByteArray sheet = readFile(f.dir.filePath(QStringLiteral("art/hero.sprat")));
        QVERIFY(sheet.contains("props/bush.png"));
        sheet.replace("props/bush.png", "props/tree.png");
        QFile file(f.dir.filePath(QStringLiteral("art/hero.sprat")));
        QVERIFY(file.open(QIODevice::WriteOnly) && file.write(sheet) == sheet.size());
        file.close();
        const auto tree = SheetItem{SheetItem::Kind::Sprite, QStringLiteral("hero"),
                                    QStringLiteral("props/tree.png")};
        QTRY_VERIFY(browser->itemRow(tree));
        QVERIFY(!browser->itemRow(SheetItem{SheetItem::Kind::Sprite, QStringLiteral("hero"),
                                            QStringLiteral("props/bush.png")}));

        // Closing the project empties it.
        f.window.setDocument(nullptr);
        QCOMPARE(browser->tree()->topLevelItemCount(), 0);
    }

    void droppingASpritePlacesItWhereTheCursorIs()
    {
        Fixture f;
        f.view->zoomIn();  // the drop point is mapped through the zoom and the pan
        QVERIFY(f.view->zoom() > 1);
        f.view->centerOn(500, 500);
        const QPointF where(520, 488);
        QVERIFY(f.dropOnView(IDLE_SPRITE, f.at(where)));

        QCOMPARE(f.document().commands().count(), 1);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Add sprite"));
        const EntityPath path{int(f.scene().entities.size()) - 1};
        const Entity* added = entityAt(f.scene(), path);
        QCOMPARE(added->type, EntityType::Sprite);
        QCOMPARE(added->sprite->sheet, QStringLiteral("hero"));
        QCOMPARE(added->sprite->path, QStringLiteral("hero/idle.png"));
        QCOMPARE(added->layer, f.canvas().currentLayer());
        QVERIFY(std::abs(added->transform.pos.x - where.x()) <= 0.5);
        QVERIFY(std::abs(added->transform.pos.y - where.y()) <= 0.5);
        // Selected, and in the scene tree.
        QCOMPARE(f.canvas().selectedPaths(), QList<EntityPath>{path});
        QVERIFY(f.window.sceneTreePanel()->entityRow(path));
        QVERIFY(f.window.sceneTreePanel()->entityRow(path)->isSelected());

        f.document().commands().undo();
        QVERIFY(!entityAt(f.scene(), path));
        QVERIFY(!f.window.sceneTreePanel()->entityRow(path));
        f.document().commands().redo();
        QVERIFY(f.window.sceneTreePanel()->entityRow(path));
    }

    void dropsSnapToTheGrid()
    {
        Fixture f;
        f.canvas().setSnapping(true);
        QVERIFY(f.dropOnView(IDLE_ANIMATION, f.at(QPointF(357, 653))));
        const Entity& added = f.scene().entities.back();
        QCOMPARE(added.type, EntityType::Animation);
        QCOMPARE(added.animation->name, QStringLiteral("idle"));
        QCOMPARE(added.transform.pos, (Point{352, 656}));
    }

    void aHiddenOrLockedLayerRefusesDrops()
    {
        Fixture f;
        const QString layer = f.canvas().currentLayer();
        f.canvas().setLayerLocked(layer, true);
        QVERIFY(!f.dropOnView(IDLE_SPRITE, f.at(QPointF(320, 600))));
        f.canvas().setLayerLocked(layer, false);
        f.canvas().setLayerHidden(layer, true);
        QVERIFY(!f.dropOnView(IDLE_SPRITE, f.at(QPointF(320, 600))));
        QCOMPARE(f.document().commands().count(), 0);

        // Something else dropped is left to the view.
        QMimeData text;
        text.setText(QStringLiteral("hero/idle.png"));
        QDropEvent drop(f.at(QPointF(320, 600)), Qt::CopyAction, &text, Qt::LeftButton, {});
        QApplication::sendEvent(f.view->viewport(), &drop);
        QCOMPARE(f.document().commands().count(), 0);
    }

    void activatingARowPlacesItInTheMiddleOfTheView()
    {
        Fixture f;
        SheetBrowserPanel* browser = f.window.sheetBrowser();
        emit browser->tree()->itemActivated(browser->itemRow(IDLE_ANIMATION), 0);
        QCOMPARE(f.document().commands().count(), 1);
        const Entity& added = f.scene().entities.back();
        QCOMPARE(added.type, EntityType::Animation);
        const QPointF middle = f.view->mapToScene(f.view->viewport()->rect().center());
        QCOMPARE(added.transform.pos, (Point{middle.x(), middle.y()}));

        // With the flow tab current there is nowhere to put it.
        f.window.tabs()->setCurrentIndex(0);
        emit browser->tree()->itemActivated(browser->itemRow(IDLE_SPRITE), 0);
        QCOMPARE(f.document().commands().count(), 1);
        QVERIFY(f.window.statusBar()->currentMessage().startsWith(QStringLiteral("Open a scene")));
    }
};

QTEST_MAIN(SceneEditorTests)
#include "SceneEditorTests.moc"
