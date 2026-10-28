#include <QAction>
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

using namespace shoal::core;
using namespace shoal::editor;

namespace {

const QString LEVEL_1 = QStringLiteral("level_1");

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
};

QTEST_MAIN(SceneEditorTests)
#include "SceneEditorTests.moc"
