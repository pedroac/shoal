#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStringListModel>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>
#include <QtMath>
#include <cmath>

#include "Support.h"
#include "assets/SpriteSheet.h"
#include "core/AssetCommands.h"
#include "core/Presets.h"
#include "core/ProjectDocument.h"
#include "core/SceneCommands.h"
#include "editor/AssetBrowserPanel.h"
#include "editor/AssetField.h"
#include "editor/ColorButton.h"
#include "editor/EntityItem.h"
#include "editor/FlowScene.h"
#include "editor/InspectorPanel.h"
#include "editor/MainWindow.h"
#include "editor/PointGrid.h"
#include "editor/PresetSettingsPanel.h"
#include "editor/PresetsPanel.h"
#include "editor/PresetsView.h"
#include "editor/SceneCanvas.h"
#include "editor/SceneSettingsPanel.h"
#include "editor/SceneTreePanel.h"
#include "editor/SceneView.h"
#include "editor/TransformHandle.h"
#include "editor/WorkspaceTabs.h"

using namespace shoal::core;
using namespace shoal::editor;

namespace {

const QString LEVEL_1 = QStringLiteral("level_1");
// In level_1: bg {0} holds the bush; world {1} holds the camera, the player, the altar group
// (two marks), the secret door, the room's bounds and the torch; hud {2} is screen space.
const EntityPath BUSH{0, 0};
const EntityPath WORLD{1};
const EntityPath PLAYER{1, 1};
const EntityPath ALTAR{1, 2};
const EntityPath DOOR{1, 3};
const AssetItem IDLE_SPRITE{AssetItem::Kind::Sprite, QStringLiteral("hero"),
                            QStringLiteral("hero/idle.png")};
const AssetItem IDLE_ANIMATION{AssetItem::Kind::Animation, QStringLiteral("hero"),
                               QStringLiteral("idle")};

/// Replaces what `field` holds by typing, then presses Return, as a user would.
void type(QLineEdit* field, const QString& text)
{
    field->setFocus();
    field->selectAll();
    if (text.isEmpty()) {
        QTest::keyClick(field, Qt::Key_Delete);
    }
    else {
        QTest::keyClicks(field, text);
    }
    QTest::keyClick(field, Qt::Key_Return);
}

/// Picks `unit` in a unit box, as a user would.
void choose(QComboBox* box, Unit unit)
{
    const int index = box->findData(int(unit));
    QVERIFY(index >= 0);
    box->setCurrentIndex(index);
    emit box->activated(index);
}

Unit unitOf(const QComboBox* box)
{
    return Unit(box->currentData().toInt());
}

/// Whether a unit box lets `unit` be picked.
bool offers(const QComboBox* box, Unit unit)
{
    const auto* model = qobject_cast<const QStandardItemModel*>(box->model());
    return model->item(box->findData(int(unit)))->isEnabled();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/// Drags `data` over `widget` and drops it there, as QDrag::exec() would send the events.
bool dropOn(QWidget* widget, const QMimeData* data)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
    const QPointF at(widget->rect().center());
#else
    const QPoint at = widget->rect().center();
#endif
    QDragEnterEvent enter(at, Qt::CopyAction, data, Qt::LeftButton, {});
    QApplication::sendEvent(widget, &enter);
    if (!enter.isAccepted()) {
        return false;
    }
    QDropEvent drop(at, Qt::CopyAction, data, Qt::LeftButton, {});
    QApplication::sendEvent(widget, &drop);
    return drop.isAccepted();
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
    /// whether the view accepted the drop.
    bool dropOnView(const AssetItem& item, QPoint at)
    {
        const std::unique_ptr<QMimeData> data(item.toMimeData());
        return dropOnView(data.get(), at);
    }

    /// Drags `data` over the view and drops it at `point`. QDrag::exec() runs its own event
    /// loop, so the events are sent as it would send them.
    bool dropOnView(const QMimeData* data, QPoint point)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
        const QPointF at(point);  // the QPoint constructors are deprecated
#else
        const QPoint at = point;
#endif
        QDragEnterEvent enter(at, Qt::CopyAction, data, Qt::LeftButton, {});
        QApplication::sendEvent(view->viewport(), &enter);
        if (!enter.isAccepted()) {
            return false;
        }
        QDragMoveEvent move(at, Qt::CopyAction, data, Qt::LeftButton, {});
        QApplication::sendEvent(view->viewport(), &move);
        QDropEvent drop(at, Qt::CopyAction, data, Qt::LeftButton, {});
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
        WorkspaceTabs* tabs = f.window.tabs();
        // After the flow and the presets.
        QCOMPARE(tabs->count(), 3);
        QCOMPARE(tabs->tabText(1), QStringLiteral("Presets"));
        QCOMPARE(tabs->tabText(2), LEVEL_1);
        QCOMPARE(f.window.currentSceneView(), f.view);
        QCOMPARE(f.window.openScene(LEVEL_1), f.view);  // the same tab again
        QCOMPARE(tabs->count(), 3);

        // Double-clicking a node in the flow opens its scene too.
        emit f.window.flowScene()->sceneActivated(QStringLiteral("pause"));
        QCOMPARE(tabs->count(), 4);
        QCOMPARE(tabs->tabText(3), QStringLiteral("pause"));
    }

    void tabsFollowRenamesAndRemovals()
    {
        Fixture f;
        QVERIFY(f.window.flowScene()->renameScene(LEVEL_1, QStringLiteral("forest")));
        QCOMPARE(f.window.tabs()->tabText(2), QStringLiteral("forest"));
        QCOMPARE(f.canvas().sceneId(), QStringLiteral("forest"));
        QVERIFY(f.canvas().item(PLAYER));

        f.window.flowScene()->select({QStringLiteral("forest")});
        f.window.flowScene()->removeSelection();
        QCOMPARE(f.window.tabs()->count(), 2);
    }

    void drawsEntitiesWithTheirSprites()
    {
        Fixture f;
        const Scene& scene = f.scene();
        // Every entity has an item, children under their parent's.
        forEachEntity(scene, [&](const Entity&, const EntityPath& path) {
            QVERIFY2(f.canvas().item(path), qPrintable(QString::number(path.first())));
        });
        QCOMPARE(f.canvas().item({1, 2, 1})->parentItem(), f.canvas().item(ALTAR));
        QCOMPARE(f.canvas().item({1, 2, 1})->scenePos(), QPointF(2400, 680));
        // Screen space is laid out in a later version.
        QVERIFY(!f.canvas().item({2})->isVisible());
        QVERIFY(f.canvas().isInScreenSpace({2}));

        // The door is drawn upright from its rotated atlas region, as sprat-lib places it.
        const auto sheet = f.window.sheets().sheet(QStringLiteral("hero"));
        QVERIFY(sheet);
        const auto* door = sheet->sprite(QStringLiteral("props/door.png"));
        QCOMPARE(f.canvas().item(DOOR)->visualRect(),
                 shoal::assets::placeSprite(*door, 1, false, false).bounds);
        // The bush, scaled by 2 and turned by -12.5 degrees: its local x axis.
        const QPointF axis = f.canvas().item(BUSH)->transform().map(QPointF(1, 0));
        QVERIFY(qFuzzyCompare(axis.x(), 2 * std::cos(qDegreesToRadians(-12.5))));
        QVERIFY(qFuzzyCompare(axis.y(), 2 * std::sin(qDegreesToRadians(-12.5))));
    }

    void drawsAPlaceholderForAMissingSprite()
    {
        Fixture f;
        Entity ghost;
        ghost.image = Image{QStringLiteral("hero"), QStringLiteral("nope.png"), {}, {}, {}, {}};
        const EntityPath path = f.canvas().addEntity(ghost, QPointF(100, 100));
        QCOMPARE(path, (EntityPath{3}));  // at the top level, the current parent
        QCOMPARE(f.canvas().item(path)->visualRect(), QRectF(-16, -16, 32, 32));
    }

    void aDragMovesTheSelectionInOneStep()
    {
        Fixture f;
        const QPoint player = f.at(QPointF(320, 630));
        f.drag(player, player + QPoint(40, 10));
        QCOMPARE(f.document().commands().count(), 1);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Move player"));
        QCOMPARE(entityAt(f.scene(), PLAYER)->transform.pos, (Point{360, 650}));
        f.document().commands().undo();
        QCOMPARE(f.canvas().item(PLAYER)->pos(), QPointF(320, 640));
    }

    void aClickNeverMovesAnEntity()
    {
        Fixture f;
        const QPoint player = f.at(QPointF(320, 630));
        // A wobble shorter than the drag distance.
        QTest::mousePress(f.view->viewport(), Qt::LeftButton, {}, player);
        QTest::mouseMove(f.view->viewport(), player + QPoint(2, 1));
        QTest::mouseRelease(f.view->viewport(), Qt::LeftButton, {}, player + QPoint(2, 1));
        // The view scrolling under the pressed button, as when selecting changes the
        // inspector's layout and so the view's size.
        f.canvas().clearSelection();
        QTest::mousePress(f.view->viewport(), Qt::LeftButton, {}, player);
        f.view->horizontalScrollBar()->setValue(f.view->horizontalScrollBar()->value() + 60);
        QApplication::processEvents();
        QTest::mouseRelease(f.view->viewport(), Qt::LeftButton, {}, player);
        QCOMPARE(f.document().commands().count(), 0);
        QCOMPARE(f.canvas().item(PLAYER)->pos(), QPointF(320, 640));
    }

    void theInspectorFitsANarrowPanel()
    {
        Fixture f;
        QVERIFY(f.view);
        // Its sections take no more than the panel's width, with an image and a tiled
        // container's rows showing too.
        f.canvas().select({PLAYER});
        QApplication::processEvents();
        const auto* scroll =
            f.window.inspector()->findChild<QScrollArea*>(QStringLiteral("inspectorScroll"));
        QVERIFY(scroll);
        QVERIFY2(scroll->widget()->minimumSizeHint().width() <= 300,
                 qPrintable(QString::number(scroll->widget()->minimumSizeHint().width())));
        QVERIFY(f.window.inspector()->showsImage());
        f.window.inspector()->containerBox()->setCurrentIndex(1);
        emit f.window.inspector()->containerBox()->activated(1);
        QVERIFY(f.window.inspector()->showsTilePivot());
        QVERIFY(scroll->widget()->minimumSizeHint().width() <= 300);
        // The side panels start narrow, and the canvas takes the rest.
        QVERIFY(f.window.contextPanel()->width() <= 300);
        QVERIFY(f.view->width() > f.window.contextPanel()->width() * 2);
    }

    void snapsToTheGrid()
    {
        Fixture f;
        f.canvas().setSnapping(true);
        const QPoint player = f.at(QPointF(320, 630));
        f.drag(player, player + QPoint(37, 13));
        // 357, 653 snaps to the 16-unit grid.
        QCOMPARE(entityAt(f.scene(), PLAYER)->transform.pos, (Point{352, 656}));
    }

    void nudgesWithTheArrowKeys()
    {
        Fixture f;
        f.canvas().select({PLAYER});
        QTest::keyClick(f.view, Qt::Key_Right);
        QTest::keyClick(f.view, Qt::Key_Up, Qt::ShiftModifier);
        QCOMPARE(entityAt(f.scene(), PLAYER)->transform.pos, (Point{321, 630}));
        QCOMPARE(f.document().commands().count(), 2);
    }

    void handlesFollowASingleSelection()
    {
        Fixture f;
        TransformHandle* rotate = f.canvas().rotateHandle();
        TransformHandle* scale = f.canvas().scaleHandle();
        QVERIFY(rotate && scale);
        QVERIFY(!rotate->isVisible() && !scale->isVisible());

        f.canvas().select({PLAYER});
        QVERIFY(rotate->isVisible() && scale->isVisible());
        const EntityItem* player = f.canvas().item(PLAYER);
        const QRectF rect = player->visualRect();
        QCOMPARE(rotate->scenePos(), player->mapToScene(QPointF(rect.center().x(), rect.top())));
        QCOMPARE(scale->scenePos(), player->mapToScene(rect.bottomRight()));

        // They follow a drag of the entity.
        const QPointF before = scale->scenePos();
        const QPoint at = f.at(QPointF(320, 630));
        f.drag(at, at + QPoint(40, 10));
        QCOMPARE(scale->scenePos(), before + QPointF(40, 10));

        f.canvas().select({PLAYER, ALTAR});
        QVERIFY(!rotate->isVisible() && !scale->isVisible());
        f.canvas().select({});
        QVERIFY(!rotate->isVisible());
    }

    void theRotateHandleTurnsTheEntityInOneStep()
    {
        Fixture f;
        f.canvas().select({PLAYER});
        const QPointF origin = f.canvas().item(PLAYER)->scenePos();
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
        QCOMPARE(entityAt(f.scene(), PLAYER)->transform.rot, 90.0);
        QCOMPARE(entityAt(f.scene(), PLAYER)->transform.pos, (Point{320, 640}));  // not moved
        f.document().commands().undo();
        QCOMPARE(entityAt(f.scene(), PLAYER)->transform.rot, 0.0);
        QCOMPARE(f.canvas().item(PLAYER)->transform(), QTransform());
    }

    void theScaleHandleScalesTheEntity()
    {
        Fixture f;
        f.canvas().select({PLAYER});
        const QPointF origin = f.canvas().item(PLAYER)->scenePos();
        const QPointF corner = f.canvas().scaleHandle()->scenePos();
        f.drag(f.at(corner), f.at(origin + 2 * (corner - origin)));

        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Scale player"));
        const Point scale = entityAt(f.scene(), PLAYER)->transform.scale;
        QVERIFY2(std::abs(scale.x - 2) < 0.05 && std::abs(scale.y - 2) < 0.05,
                 qPrintable(QStringLiteral("%1, %2").arg(scale.x).arg(scale.y)));
        // The handle stays on the outline, which has grown.
        QVERIFY(
            QLineF(f.canvas().scaleHandle()->scenePos(), origin + 2 * (corner - origin)).length() <
            2);
    }

    void nineSlicesResizeInsteadOfScaling()
    {
        Fixture f;
        // A sprite with insets is placed as a nine-slice, its natural size set.
        const AssetItem panelItem{AssetItem::Kind::Sprite, QStringLiteral("hero"),
                                  QStringLiteral("ui/panel.9.png")};
        QVERIFY(f.dropOnView(panelItem, f.at(QPointF(320, 600))));
        const EntityPath path{int(f.scene().entities.size()) - 1};
        const Entity* panel = entityAt(f.scene(), path);
        QCOMPARE(panel->image->mode, ImageMode::NineSlice);
        QCOMPARE(panel->size, std::optional(Extent::px({12, 12})));
        QVERIFY(!panel->pivot);  // its sprite's: its top-left
        EntityItem* item = f.canvas().item(path);
        QCOMPARE(item->visualRect(), QRectF(0, 0, 12, 12));

        // The scale handle resizes it on each axis, and leaves its scale alone.
        const QPointF origin = item->scenePos();
        const QPointF corner = f.canvas().scaleHandle()->scenePos();
        f.drag(f.at(corner), f.at(origin + QPointF(60, 24)));
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Resize nine-slice"));
        panel = entityAt(f.scene(), path);
        QVERIFY(panel->size->isPx());
        const SizeF size{panel->size->width.value, panel->size->height.value};
        QVERIFY2(std::abs(size.width - 60) <= 1 && std::abs(size.height - 24) <= 1,
                 qPrintable(QStringLiteral("%1 x %2").arg(size.width).arg(size.height)));
        QCOMPARE(panel->transform.scale, (Point{1, 1}));
        QCOMPARE(f.canvas().item(path)->visualRect(), QRectF(0, 0, size.width, size.height));

        // The inspector shows its size instead of its scale.
        InspectorPanel* inspector = f.window.inspector();
        QVERIFY(inspector->showsSize());
        QVERIFY(inspector->sizeBox()->isChecked());
        QVERIFY(!inspector->showsScale());
        QCOMPARE(inspector->widthBox()->value(), size.width);
        inspector->widthBox()->setFocus();
        inspector->widthBox()->selectAll();
        QTest::keyClicks(inspector->widthBox(), QStringLiteral("100"));
        QTest::keyClick(inspector->widthBox(), Qt::Key_Return);
        QCOMPARE(entityAt(f.scene(), path)->size, std::optional(Extent::px({100, size.height})));
        f.document().commands().undo();
        QCOMPARE(inspector->widthBox()->value(), size.width);

        // A sprite sized by its sprite shows its scale, and its size can't be typed.
        f.canvas().select({PLAYER});
        QVERIFY(inspector->showsScale());
        QVERIFY(!inspector->sizeBox()->isChecked());
        QVERIFY(!inspector->widthBox()->isEnabled());
        QCOMPARE(inspector->widthBox()->value(), 16.0);  // the idle sprite's original image
        // Its scale handle scales it.
        const QPointF playerCorner = f.canvas().scaleHandle()->scenePos();
        const QPointF playerOrigin = f.canvas().item(PLAYER)->scenePos();
        f.drag(f.at(playerCorner), f.at(playerOrigin + 2 * (playerCorner - playerOrigin)));
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Scale player"));
    }

    void aClickOnAHandleChangesNothing()
    {
        Fixture f;
        f.canvas().select({PLAYER});
        const QPoint corner = f.at(f.canvas().scaleHandle()->scenePos());
        QTest::mouseClick(f.view->viewport(), Qt::LeftButton, {}, corner);
        QCOMPARE(f.document().commands().count(), 0);
        QCOMPARE(f.canvas().selectedPaths(), QList<EntityPath>{PLAYER});
    }

    void deletesTheSelection()
    {
        Fixture f;
        f.canvas().select({PLAYER, {1, 2, 0}});
        auto* remove = f.window.findChild<QAction*>(QStringLiteral("deleteAction"));
        QVERIFY(remove->isEnabled());
        remove->trigger();
        QVERIFY(findEntity(f.scene(), u"player").isEmpty());
        QVERIFY(findEntity(f.scene(), u"mark_altar").isEmpty());
        QVERIFY(f.canvas().selectedPaths().isEmpty());
        QCOMPARE(f.document().commands().count(), 1);
    }

    void hiddenAndLockedEntitiesAreLeftAlone()
    {
        Fixture f;
        f.canvas().setHidden({0}, true);
        QVERIFY(f.canvas().isHidden({0}));
        QVERIFY(!f.canvas().item(BUSH)->isVisible());  // with its parent
        f.canvas().setLocked(WORLD, true);
        f.canvas().select({PLAYER});
        QVERIFY(f.canvas().selectedPaths().isEmpty());
        QVERIFY(f.canvas().isHiddenOrLocked(PLAYER));
        QVERIFY(f.document().commands().isClean());  // editor state, not the project's

        // Known by id, so the state follows the entity when it moves.
        f.canvas().setLocked(WORLD, false);
        f.canvas().setLocked(ALTAR, true);
        QVERIFY(f.window.sceneTreePanel()->dropEntity(ALTAR, nullptr, SceneTreePanel::Drop::On));
        QVERIFY(f.canvas().isLocked({3}));
        QVERIFY(!f.canvas().isLocked(ALTAR));  // the secret door, there now
    }

    void theTreeListsEntitiesAndFollowsTheSelection()
    {
        Fixture f;
        SceneTreePanel* panel = f.window.sceneTreePanel();
        QCOMPARE(panel->canvas(), &f.canvas());
        // Under the top level, the scene: bg, world, hud.
        QCOMPARE(panel->tree()->topLevelItemCount(), 1);
        QTreeWidgetItem* root = panel->topLevelRow();
        QCOMPARE(root, panel->tree()->topLevelItem(0));
        QCOMPARE(root->text(0), LEVEL_1);
        QCOMPARE(root->childCount(), 3);
        QCOMPARE(root->child(1)->text(0), QStringLiteral("world"));
        QCOMPARE(panel->entityRow({1})->parent(), root);
        QVERIFY(panel->entityRow({1, 2, 1}));
        QCOMPARE(panel->entityRow({1, 2, 1})->parent(), panel->entityRow(ALTAR));
        QCOMPARE(panel->entityRow(BUSH)->text(0), QStringLiteral("‹sprite›"));

        // The current parent is in bold: the top level until another is chosen.
        QVERIFY(panel->topLevelRow()->font(0).bold());
        QVERIFY(!panel->entityRow(WORLD)->font(0).bold());
        f.canvas().setCurrentParent(WORLD);
        QVERIFY(panel->entityRow(WORLD)->font(0).bold());
        QVERIFY(!panel->topLevelRow()->font(0).bold());
        // It can't be dragged, hidden or locked.
        QVERIFY(!(panel->topLevelRow()->flags() & Qt::ItemIsDragEnabled));
        QVERIFY(!panel->topLevelRow()->data(1, Qt::CheckStateRole).isValid());

        // Its check boxes hide and lock in the editor, once the row's call returns.
        panel->entityRow({0})->setCheckState(1, Qt::Unchecked);
        QTRY_VERIFY(f.canvas().isHidden({0}));
        panel->entityRow(PLAYER)->setCheckState(2, Qt::Checked);
        QTRY_VERIFY(f.canvas().isLocked(PLAYER));
        QVERIFY(panel->entityRow(PLAYER)->checkState(2) == Qt::Checked);

        // With nothing selected the top level is; choosing it clears the selection.
        QVERIFY(panel->topLevelRow()->isSelected());
        f.canvas().select({ALTAR});
        QVERIFY(panel->entityRow(ALTAR)->isSelected());
        QVERIFY(!panel->topLevelRow()->isSelected());
        panel->tree()->clearSelection();
        panel->entityRow(DOOR)->setSelected(true);
        QCOMPARE(f.canvas().selectedPaths(), QList<EntityPath>{DOOR});
        panel->tree()->setCurrentItem(panel->topLevelRow());
        QVERIFY(f.canvas().selectedPaths().isEmpty());
        QVERIFY(panel->topLevelRow()->isSelected());

        // The flow tab has no scene to show.
        f.window.tabs()->setCurrentIndex(0);
        QVERIFY(!panel->canvas());
        QVERIFY(!panel->isEnabled());
    }

    void droppingInTheTreeMovesEntities()
    {
        Fixture f;
        SceneTreePanel* panel = f.window.sceneTreePanel();
        // The player into the altar group, as its last child: where it is drawn stays.
        QVERIFY(panel->dropEntity(PLAYER, panel->entityRow(ALTAR), SceneTreePanel::Drop::On));
        const EntityPath moved = findEntity(f.scene(), u"player");
        QCOMPARE(moved, (EntityPath{1, 1, 2}));
        QCOMPARE(entityAt(f.scene(), moved)->transform.pos, (Point{-2080, 40}));
        QCOMPARE(f.canvas().item(moved)->scenePos(), QPointF(320, 640));
        QCOMPARE(f.canvas().selectedPaths(), QList<EntityPath>{moved});
        QCOMPARE(f.document().commands().count(), 1);

        // Below the bush, in bg.
        QVERIFY(panel->dropEntity(moved, panel->entityRow(BUSH), SceneTreePanel::Drop::Below));
        QCOMPARE(findEntity(f.scene(), u"player"), (EntityPath{0, 1}));
        // On empty space: in front of everything, at the top level.
        QVERIFY(panel->dropEntity({0, 1}, nullptr, SceneTreePanel::Drop::On));
        QCOMPARE(findEntity(f.scene(), u"player"), (EntityPath{3}));
        QCOMPARE(entityAt(f.scene(), {3})->transform.pos, (Point{320, 640}));
        QCOMPARE(f.document().commands().count(), 3);
        f.document().commands().undo();
        QCOMPARE(findEntity(f.scene(), u"player"), (EntityPath{0, 1}));
        // On the top level's row, the same; nothing goes beside it.
        QVERIFY(panel->dropEntity({0, 1}, panel->topLevelRow(), SceneTreePanel::Drop::On));
        QCOMPARE(findEntity(f.scene(), u"player"), (EntityPath{3}));
        QVERIFY(!panel->dropEntity({3}, panel->topLevelRow(), SceneTreePanel::Drop::Above));

        // Not into itself, nor where it already is.
        QVERIFY(!panel->dropEntity({1, 1}, panel->entityRow({1, 1, 0}), SceneTreePanel::Drop::On));
        QVERIFY(!panel->dropEntity(BUSH, panel->entityRow(BUSH), SceneTreePanel::Drop::Above));
    }

    void newEntitiesGoIntoTheCurrentParent()
    {
        Fixture f;
        SceneTreePanel* panel = f.window.sceneTreePanel();
        // At the top level until another is chosen.
        panel->addGroup();
        QCOMPARE(f.scene().entities.size(), size_t{4});
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Add mark"));

        // Into the altar group, at (2400, 600): in its coordinates.
        f.canvas().setCurrentParent(ALTAR);
        QVERIFY(f.dropOnView(IDLE_SPRITE, f.at(QPointF(2410, 620))));
        const EntityPath sprite{1, 2, 2};
        QCOMPARE(f.canvas().selectedPaths(), QList<EntityPath>{sprite});
        const Point pos = entityAt(f.scene(), sprite)->transform.pos;
        QVERIFY2(std::abs(pos.x - 10) <= 0.5 && std::abs(pos.y - 20) <= 0.5,
                 qPrintable(QStringLiteral("%1, %2").arg(pos.x).arg(pos.y)));
        panel->addGroup();
        QCOMPARE(entityAt(f.scene(), {1, 2, 3})->transform.pos, (Point{0, 0}));

        // Removing it puts new entities back at the top level.
        f.canvas().select({ALTAR});
        f.canvas().removeSelection();
        QVERIFY(f.canvas().currentParent().isEmpty());

        // Not into screen space, which isn't drawn yet.
        f.canvas().setCurrentParent({2});
        QVERIFY(!f.canvas().canAddEntities());
    }

    void reloadsSpritesWhenTheSheetChanges()
    {
        Fixture f;
        const QRectF before = f.canvas().item(BUSH)->visualRect();
        QByteArray sheet = readFile(f.dir.filePath(QStringLiteral("art/hero.sprat")));
        sheet.replace("0 0 0 0 8 8 0 0 0 0 0 0 0 0 4:bush", "0 0 0 0 8 0 0 0 0 0 0 0 0 0 4:bush");
        QFile file(f.dir.filePath(QStringLiteral("art/hero.sprat")));
        QVERIFY(file.open(QIODevice::WriteOnly) && file.write(sheet) == sheet.size());
        file.close();
        // Its pivot is its sprite's: the bush moves down with it.
        QTRY_COMPARE(f.canvas().item(BUSH)->visualRect(), before.translated(0, 8));
    }

    void theSheetBrowserListsSpritesAndAnimations()
    {
        Fixture f;
        AssetBrowserPanel* browser = f.window.assetBrowser();
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
        QCOMPARE(AssetBrowserPanel::itemOf(idle), IDLE_SPRITE);
        QVERIFY(browser->itemRow(IDLE_ANIMATION));
        QVERIFY(!AssetBrowserPanel::itemOf(hero));

        // The manifest declares a ui sheet that isn't there: it says why.
        QTreeWidgetItem* ui = browser->sheetRow(QStringLiteral("ui"));
        QVERIFY(ui);
        QCOMPARE(ui->text(0), QStringLiteral("ui (not loaded)"));
        QCOMPARE(ui->childCount(), 0);
        QVERIFY(ui->toolTip(0).startsWith(QStringLiteral("Couldn't load: ")));

        // A drag carries the sprite, not the sheet.
        const std::unique_ptr<QMimeData> data(
            browser->tree()->model()->mimeData({browser->tree()->indexFromItem(idle)}));
        QCOMPARE(AssetItem::fromMimeData(data.get()), IDLE_SPRITE);
    }

    void droppingFilesOnTheAssetBrowserDeclaresThem()
    {
        Fixture f;
        // A sheet outside the project folder, as sprat-gui would write it anywhere.
        QTemporaryDir elsewhere;
        const QString sheetFile = elsewhere.filePath(QStringLiteral("sheet.sprat"));
        QVERIFY(QFile::copy(support::fixturePath(QStringLiteral("sheets/hero.sprat")), sheetFile));
        QVERIFY(QFile::copy(support::fixturePath(QStringLiteral("sheets/hero.png")),
                            elsewhere.filePath(QStringLiteral("hero.png"))));
        AssetBrowserPanel* browser = f.window.assetBrowser();
        QWidget* viewport = browser->tree()->viewport();
        const auto drop = [&](const QList<QUrl>& urls) {
            QMimeData data;
            data.setUrls(urls);
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
            const QPointF at(viewport->rect().center());
#else
            const QPoint at = viewport->rect().center();
#endif
            QDragEnterEvent enter(at, Qt::CopyAction, &data, Qt::LeftButton, {});
            QApplication::sendEvent(viewport, &enter);
            if (!enter.isAccepted()) {
                return false;
            }
            QDropEvent dropEvent(at, Qt::CopyAction, &data, Qt::LeftButton, {});
            QApplication::sendEvent(viewport, &dropEvent);
            return dropEvent.isAccepted();
        };

        const auto assets = [&] { return f.window.document()->project().manifest.assets; };
        // Files of a kind Shoal doesn't know, or not local, are refused.
        const QString notes = elsewhere.filePath(QStringLiteral("notes.txt"));
        QVERIFY(QFile(notes).open(QIODevice::WriteOnly));
        QVERIFY(!drop({QUrl::fromLocalFile(notes)}));
        QVERIFY(!drop({QUrl(QStringLiteral("https://example.com/sheet.sprat"))}));

        QVERIFY(drop({QUrl::fromLocalFile(sheetFile)}));
        // Named after the file, relative to the project, with its sprites listed.
        const QString alias = QStringLiteral("sheet");
        QCOMPARE(assets().sheets.value(alias), QDir(f.dir.path()).relativeFilePath(sheetFile));
        QCOMPARE(browser->sheetRow(alias)->parent(), browser->kindRow(AssetKind::Sheet));
        QTreeWidgetItem* row = browser->sheetRow(alias);
        QVERIFY(row);
        QCOMPARE(row->text(0), alias);
        QCOMPARE(row->child(0)->text(0), QStringLiteral("Sprites (4)"));
        QVERIFY(
            browser->itemRow({AssetItem::Kind::Sprite, alias, QStringLiteral("hero/idle.png")}));

        // Dropping it again adds nothing; it's one undo step either way.
        QVERIFY(drop({QUrl::fromLocalFile(sheetFile)}));
        QCOMPARE(assets().sheets.size(), 3);
        QCOMPARE(f.window.document()->commands().undoText(), QStringLiteral("Add asset"));
        f.window.document()->commands().undo();
        QVERIFY(!browser->sheetRow(alias));
        QCOMPARE(assets().sheets.size(), 2);

        // An image is a standalone image, its alias unique across the kinds: hero is a sheet.
        QVERIFY(!browser->kindRow(AssetKind::Image));
        QVERIFY(drop({QUrl::fromLocalFile(elsewhere.filePath(QStringLiteral("hero.png")))}));
        const QString image = QStringLiteral("hero_2");
        QVERIFY(assets().images.contains(image));
        QTreeWidgetItem* imageRow = browser->itemRow({AssetItem::Kind::Image, image, {}});
        QVERIFY(imageRow);
        QCOMPARE(imageRow->parent(), browser->kindRow(AssetKind::Image));
        QVERIFY(!imageRow->icon(0).isNull());
        QVERIFY(imageRow->flags() & Qt::ItemIsDragEnabled);

        // A font is listed under Fonts, by alias; one that isn't a font can't be dragged.
        const QString font = elsewhere.filePath(QStringLiteral("Body.TTF"));
        QVERIFY(QFile(font).open(QIODevice::WriteOnly));
        QVERIFY(drop({QUrl::fromLocalFile(font)}));
        QCOMPARE(assets().fonts.keys(), QStringList{QStringLiteral("Body")});
        QTreeWidgetItem* fontRow = browser->assetRow(QStringLiteral("Body"));
        QVERIFY(fontRow);
        QCOMPARE(fontRow->parent(), browser->kindRow(AssetKind::Font));
        QCOMPARE(fontRow->text(0), QStringLiteral("Body (not loaded)"));
        QVERIFY(!(fontRow->flags() & Qt::ItemIsDragEnabled));
        f.window.document()->commands().undo();
        QVERIFY(!browser->kindRow(AssetKind::Font));

        // A second file with the same name gets its own alias.
        QTemporaryDir other;
        QVERIFY(QFile::copy(sheetFile, other.filePath(QStringLiteral("sheet.sprat"))));
        QCOMPARE(f.window.addAssets({sheetFile, other.filePath(QStringLiteral("sheet.sprat"))}),
                 QStringList({alias, QStringLiteral("sheet_2")}));
        QCOMPARE(f.window.document()->commands().undoText(), QStringLiteral("Add assets"));
    }

    void arrangesTheSelectionInDrawOrder()
    {
        Fixture f;
        const auto action = [&](const char* name) {
            return f.window.findChild<QAction*>(QLatin1StringView(name));
        };
        QAction* forward = action("bringForwardAction");
        QAction* toFront = action("bringToFrontAction");
        QAction* backward = action("sendBackwardAction");
        QAction* toBack = action("sendToBackAction");
        QVERIFY(forward && toFront && backward && toBack);
        QVERIFY(!forward->isEnabled());  // nothing selected

        // The player passes the altar group, among world's children.
        f.canvas().select({PLAYER});
        QVERIFY(forward->isEnabled() && toFront->isEnabled());
        forward->trigger();
        QCOMPARE(entityAt(f.scene(), {1, 2})->id, QStringLiteral("player"));
        QCOMPARE(entityAt(f.scene(), {1, 1})->id, QStringLiteral("altar_group"));
        // Still selected, and drawn in front of the group.
        QCOMPARE(f.canvas().selectedPaths(), (QList<EntityPath>{{1, 2}}));
        QVERIFY(f.canvas().item({1, 2})->zValue() > f.canvas().item({1, 1})->zValue());
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Bring Forward"));

        toFront->trigger();
        QCOMPARE(entityAt(f.scene(), {1, 5})->id, QStringLiteral("player"));
        QCOMPARE(f.canvas().selectedPaths(), (QList<EntityPath>{{1, 5}}));
        QVERIFY(!forward->isEnabled() && !toFront->isEnabled());  // already in front
        QVERIFY(backward->isEnabled() && toBack->isEnabled());

        // Ctrl+Shift+[ sends it to the back.
        f.window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&f.window));
        f.view->setFocus();
        QTest::keyClick(f.view, Qt::Key_BracketLeft, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(entityAt(f.scene(), {1, 0})->id, QStringLiteral("player"));
        QVERIFY(!backward->isEnabled());

        f.document().commands().undo();
        f.document().commands().undo();
        f.document().commands().undo();
        QCOMPARE(entityAt(f.scene(), PLAYER)->id, QStringLiteral("player"));
    }

    void theAssetBrowserFilters()
    {
        Fixture f;
        AssetBrowserPanel* browser = f.window.assetBrowser();
        browser->setFilter(QStringLiteral("DOOR"));
        const auto door = AssetItem{AssetItem::Kind::Sprite, QStringLiteral("hero"),
                                    QStringLiteral("props/door.png")};
        QVERIFY(!browser->itemRow(door)->isHidden());
        QVERIFY(browser->itemRow(IDLE_SPRITE)->isHidden());
        QVERIFY(browser->sheetRow(QStringLiteral("hero"))->child(1)->isHidden());  // animations
        QVERIFY(browser->sheetRow(QStringLiteral("ui"))->isHidden());
        browser->findChild<QLineEdit*>(QStringLiteral("assetFilter"))->clear();
        QVERIFY(!browser->itemRow(IDLE_SPRITE)->isHidden());
    }

    void theAssetBrowserFollowsTheSheets()
    {
        Fixture f;
        AssetBrowserPanel* browser = f.window.assetBrowser();
        // A sheet written again by sprat: the bush is renamed, keeping the string total.
        QByteArray sheet = readFile(f.dir.filePath(QStringLiteral("art/hero.sprat")));
        QVERIFY(sheet.contains("props/bush.png"));
        sheet.replace("props/bush.png", "props/tree.png");
        QFile file(f.dir.filePath(QStringLiteral("art/hero.sprat")));
        QVERIFY(file.open(QIODevice::WriteOnly) && file.write(sheet) == sheet.size());
        file.close();
        const auto tree = AssetItem{AssetItem::Kind::Sprite, QStringLiteral("hero"),
                                    QStringLiteral("props/tree.png")};
        QTRY_VERIFY(browser->itemRow(tree));
        QVERIFY(!browser->itemRow(AssetItem{AssetItem::Kind::Sprite, QStringLiteral("hero"),
                                            QStringLiteral("props/bush.png")}));

        // Closing the project empties it.
        f.window.setDocument(nullptr);
        QCOMPARE(browser->tree()->topLevelItemCount(), 0);
    }

    void aFontIsDraggedAndPlacedAsAText()
    {
        Fixture f;
        AssetBrowserPanel* browser = f.window.assetBrowser();
        const QString body = QStringLiteral("body");
        QVERIFY(QFile::copy(support::fixturePath(QStringLiteral("fonts/box.ttf")),
                            f.dir.filePath(QStringLiteral("art/box.ttf"))));
        f.document().commands().push(std::make_unique<SetAssetCommand>(
            f.document(), AssetKind::Font, body, QStringLiteral("art/box.ttf")));
        QCOMPARE(f.window.fonts().family(body), QStringLiteral("Shoal Box"));

        // Listed by alias, in itself, and dragged as a font.
        const AssetItem item{AssetItem::Kind::Font, body, {}};
        QTreeWidgetItem* row = browser->itemRow(item);
        QVERIFY(row);
        QCOMPARE(row->text(0), body);
        QCOMPARE(row->font(0).family(), QStringLiteral("Shoal Box"));
        QVERIFY(row->flags() & Qt::ItemIsDragEnabled);
        const std::unique_ptr<QMimeData> data(
            browser->tree()->model()->mimeData({browser->tree()->indexFromItem(row)}));
        QCOMPARE(AssetItem::fromMimeData(data.get()), std::optional(item));
        QCOMPARE(item.path(), QStringLiteral("Fonts / body"));

        // Dropped, it is a text, "Text", in a rect sized to it, centered on its origin: in the
        // box font every glyph is 0.6 em wide, and a line 1.2 em high.
        QVERIFY(f.dropOnView(data.get(), f.at(QPointF(400, 300))));
        const EntityPath path{int(f.scene().entities.size()) - 1};
        const Entity* text = entityAt(f.scene(), path);
        QVERIFY(text->text && !text->image);
        QCOMPARE(text->text->font, body);
        QCOMPARE(text->text->string, QStringLiteral("Text"));
        QCOMPARE(text->text->wrap, TextWrap::Word);
        QCOMPARE(text->size,
                 std::optional(Extent::px({std::ceil(4 * 0.6 * 32), std::ceil(1.2 * 32)})));
        QCOMPARE(text->pivot, std::optional(Point{0.5, 0.5}));
        QCOMPARE(kindOf(*text), QStringLiteral("text"));

        // The inspector shows the font as its asset, and the text's section.
        InspectorPanel* inspector = f.window.inspector();
        AssetField* field = inspector->assetField();
        QCOMPARE(f.canvas().selectedPaths(), QList<EntityPath>{path});
        QCOMPARE(field->item(), std::optional(item));
        QCOMPARE(field->lineEdit()->text(), QStringLiteral("Fonts / body"));
        QVERIFY(!field->isMissing());
        QVERIFY(!inspector->textSection()->isHidden());
        QVERIFY(!inspector->showsImage());
        field->completer()->setCompletionPrefix(QStringLiteral("BODY"));
        QCOMPARE(field->completer()->completionCount(), 1);

        // A sprite makes it an image, as placed from the sprite.
        const std::unique_ptr<QMimeData> sprite(IDLE_SPRITE.toMimeData());
        QVERIFY(dropOn(field, sprite.get()));
        text = entityAt(f.scene(), path);
        QVERIFY(text->image && !text->text);
        QVERIFY(!text->size && !text->pivot);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Make text an image"));
        QVERIFY(inspector->showsImage());
        QVERIFY(inspector->textSection()->isHidden());
        const std::optional<RectF> rect = entityRect(*text, Frame{}, f.canvas().imageMetrics());
        QVERIFY(rect);

        // A font makes it a text again, in the rect the sprite drew, so nothing moves.
        QVERIFY(dropOn(field, data.get()));
        text = entityAt(f.scene(), path);
        QVERIFY(text->text && !text->image);
        QCOMPARE(text->text->string, QStringLiteral("Text"));
        QCOMPARE(text->size, std::optional(Extent::px({rect->width, rect->height})));
        QCOMPARE(entityRect(*text, Frame{}, f.canvas().imageMetrics()), rect);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Make sprite a text"));

        // An image with a text shows its image as its asset, and its text's font apart.
        f.document().commands().undo();
        inspector->setTextFont(body);
        text = entityAt(f.scene(), path);
        QVERIFY(text->image && text->text);
        QCOMPARE(field->item(), std::optional(IDLE_SPRITE));
        QCOMPARE(inspector->fontField()->item(), std::optional(item));
        QVERIFY(!inspector->fontField()->isHidden());
        // Cleared, it loses its image first, then its text.
        field->clearButton()->click();
        text = entityAt(f.scene(), path);
        QVERIFY(!text->image && text->text);
        QCOMPARE(field->item(), std::optional(item));
        field->clearButton()->click();
        QVERIFY(!entityAt(f.scene(), path)->text);
        QVERIFY(field->lineEdit()->text().isEmpty());
        QVERIFY(inspector->textSection()->isHidden());

        // Typed, a font gives a guide a text in a rect of its own.
        f.canvas().select({{1, 2, 0}});
        QVERIFY(!entityAt(f.scene(), {1, 2, 0})->image);
        type(field->lineEdit(), QStringLiteral("fonts / body"));
        QVERIFY(entityAt(f.scene(), {1, 2, 0})->text);
    }

    void aVideoIsDraggedAndPlacedInARectOfItsOwn()
    {
        Fixture f;
        AssetBrowserPanel* browser = f.window.assetBrowser();
        const QString intro = QStringLiteral("intro");
        f.document().commands().push(std::make_unique<SetAssetCommand>(
            f.document(), AssetKind::Video, intro, QStringLiteral("video/intro.webm")));

        // Listed by alias, and dragged as a video though Shoal doesn't decode it.
        const AssetItem item{AssetItem::Kind::Video, intro, {}};
        QTreeWidgetItem* row = browser->itemRow(item);
        QVERIFY(row);
        QCOMPARE(row->parent(), browser->kindRow(AssetKind::Video));
        QVERIFY(row->flags() & Qt::ItemIsDragEnabled);
        const std::unique_ptr<QMimeData> data(
            browser->tree()->model()->mimeData({browser->tree()->indexFromItem(row)}));
        QCOMPARE(AssetItem::fromMimeData(data.get()), std::optional(item));
        QCOMPARE(item.path(), QStringLiteral("Video / intro"));

        // Dropped, it is a video in a 16:9 rect a quarter of the design width, centered.
        QVERIFY(f.dropOnView(data.get(), f.at(QPointF(400, 300))));
        const EntityPath path{int(f.scene().entities.size()) - 1};
        const Entity* video = entityAt(f.scene(), path);
        QVERIFY(video->video && !video->image && !video->text);
        QCOMPARE(video->video->clip, intro);
        const double width =
            std::round(f.document().project().manifest.designResolution.width / 4.0);
        QCOMPARE(video->size, std::optional(Extent::px({width, std::round(width * 9 / 16)})));
        QCOMPARE(video->pivot, std::optional(Point{0.5, 0.5}));
        QCOMPARE(kindOf(*video), QStringLiteral("video"));
        QVERIFY(!f.canvas().item(path)->boundingRect().isEmpty());

        // The inspector shows the clip as its asset, and the video's section.
        InspectorPanel* inspector = f.window.inspector();
        AssetField* field = inspector->assetField();
        QCOMPARE(field->item(), std::optional(item));
        QCOMPARE(field->lineEdit()->text(), QStringLiteral("Video / intro"));
        QVERIFY(!field->isMissing());
        QVERIFY(!inspector->videoSection()->isHidden());
        QVERIFY(inspector->videoField()->isHidden());
        field->completer()->setCompletionPrefix(QStringLiteral("INTRO"));
        QCOMPARE(field->completer()->completionCount(), 1);
        inspector->videoLoopBox()->click();
        QVERIFY(entityAt(f.scene(), path)->video->loop);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Loop the video of video"));
        inspector->videoAutoplayBox()->click();
        QVERIFY(entityAt(f.scene(), path)->video->autoplay);
        inspector->videoModeBox()->setCurrentIndex(
            inspector->videoModeBox()->findData(int(VideoMode::Fill)));
        emit inspector->videoModeBox()->activated(inspector->videoModeBox()->currentIndex());
        QCOMPARE(entityAt(f.scene(), path)->video->mode, VideoMode::Fill);

        // A sprite makes it an image, as placed from the sprite.
        const std::unique_ptr<QMimeData> sprite(IDLE_SPRITE.toMimeData());
        QVERIFY(dropOn(field, sprite.get()));
        video = entityAt(f.scene(), path);
        QVERIFY(video->image && !video->video);
        QVERIFY(!video->size && !video->pivot);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Make video an image"));
        QVERIFY(inspector->videoSection()->isHidden());
        const std::optional<RectF> rect = entityRect(*video, Frame{}, f.canvas().imageMetrics());
        QVERIFY(rect);

        // A video makes it a video again, in the rect the sprite drew, so nothing moves.
        QVERIFY(dropOn(field, data.get()));
        video = entityAt(f.scene(), path);
        QVERIFY(video->video && !video->image);
        QCOMPARE(video->size, std::optional(Extent::px({rect->width, rect->height})));
        QCOMPARE(entityRect(*video, Frame{}, f.canvas().imageMetrics()), rect);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Make sprite a video"));

        // An undeclared clip stays, shown as missing; cleared, the video goes.
        f.document().commands().push(
            std::make_unique<SetAssetCommand>(f.document(), AssetKind::Video, intro, std::nullopt));
        QVERIFY(field->isMissing());
        field->clearButton()->click();
        QVERIFY(!entityAt(f.scene(), path)->video);
        QVERIFY(inspector->videoSection()->isHidden());
    }

    void aClipIsDraggedAndPlacedAsASound()
    {
        Fixture f;
        AssetBrowserPanel* browser = f.window.assetBrowser();
        const QString door = QStringLiteral("door");
        f.document().commands().push(std::make_unique<SetAssetCommand>(
            f.document(), AssetKind::Audio, door, QStringLiteral("audio/door.ogg")));

        // Listed by alias, and dragged as a clip though Shoal doesn't decode it.
        const AssetItem item{AssetItem::Kind::Audio, door, {}};
        QTreeWidgetItem* row = browser->itemRow(item);
        QVERIFY(row);
        QCOMPARE(row->parent(), browser->kindRow(AssetKind::Audio));
        QVERIFY(row->flags() & Qt::ItemIsDragEnabled);
        const std::unique_ptr<QMimeData> data(
            browser->tree()->model()->mimeData({browser->tree()->indexFromItem(row)}));
        QCOMPARE(AssetItem::fromMimeData(data.get()), std::optional(item));
        QCOMPARE(item.path(), QStringLiteral("Audio / door"));

        // Dropped, it is a sound source where it is, heard everywhere, with no rect.
        const QPointF where(400, 300);
        QVERIFY(f.dropOnView(data.get(), f.at(where)));
        const EntityPath path{int(f.scene().entities.size()) - 1};
        const Entity* sound = entityAt(f.scene(), path);
        QVERIFY(sound->audio && !sound->image && !sound->size);
        QCOMPARE(sound->audio->clip, door);
        QVERIFY(!sound->audio->radius);
        QCOMPARE(kindOf(*sound), QStringLiteral("sound"));
        // Its speaker is what picks it.
        EntityItem* speaker = f.canvas().item(path);
        QVERIFY(!speaker->visualRect().isEmpty());
        QVERIFY(speaker->visualRect().width() < 32);

        // The inspector shows the clip as its asset, and the sound's section.
        InspectorPanel* inspector = f.window.inspector();
        AssetField* field = inspector->assetField();
        QCOMPARE(field->item(), std::optional(item));
        QCOMPARE(field->lineEdit()->text(), QStringLiteral("Audio / door"));
        QVERIFY(!field->isMissing());
        QVERIFY(!inspector->audioSection()->isHidden());
        QVERIFY(inspector->audioField()->isHidden());
        QVERIFY(inspector->radiusBox()->isHidden());

        // Positional, it has a radius, drawn when selected but never picked.
        inspector->positionalBox()->click();
        QCOMPARE(entityAt(f.scene(), path)->audio->radius, std::optional(400.0));
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Position the sound of sound"));
        QVERIFY(!inspector->radiusBox()->isHidden());
        inspector->radiusBox()->setValue(250);
        QCOMPARE(entityAt(f.scene(), path)->audio->radius, std::optional(250.0));
        QVERIFY(speaker->boundingRect().contains(QRectF(-250, -250, 500, 500)));
        QVERIFY(speaker->visualRect().width() < 32);
        inspector->volumeBox()->setValue(0.5);
        QCOMPARE(entityAt(f.scene(), path)->audio->volume, 0.5);
        inspector->audioLoopBox()->click();
        inspector->audioAutoplayBox()->click();
        QVERIFY(entityAt(f.scene(), path)->audio->loop);
        QVERIFY(entityAt(f.scene(), path)->audio->autoplay);
        inspector->positionalBox()->click();
        QVERIFY(!entityAt(f.scene(), path)->audio->radius);

        // A sprite makes it an image; a clip makes it a sound again, with no rect.
        const std::unique_ptr<QMimeData> sprite(IDLE_SPRITE.toMimeData());
        QVERIFY(dropOn(field, sprite.get()));
        sound = entityAt(f.scene(), path);
        QVERIFY(sound->image && !sound->audio);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Make sound an image"));
        QVERIFY(dropOn(field, data.get()));
        sound = entityAt(f.scene(), path);
        QVERIFY(sound->audio && !sound->image && !sound->size && !sound->pivot);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Make sprite a sound"));

        // A sprite with a sound, like a torch: the image is its asset, the clip apart.
        f.document().commands().undo();
        inspector->setSoundClip(door);
        sound = entityAt(f.scene(), path);
        QVERIFY(sound->image && sound->audio);
        QCOMPARE(field->item(), std::optional(IDLE_SPRITE));
        QCOMPARE(inspector->audioField()->item(), std::optional(item));
        QVERIFY(!inspector->audioField()->isHidden());
        inspector->removeAudioButton()->click();
        QVERIFY(!entityAt(f.scene(), path)->audio);
        QVERIFY(inspector->audioSection()->isHidden());
    }

    void aStandaloneImageIsPlacedAndDrawnAsASprite()
    {
        Fixture f;
        AssetBrowserPanel* browser = f.window.assetBrowser();
        const QString title = QStringLiteral("title");
        f.document().commands().push(std::make_unique<SetAssetCommand>(
            f.document(), AssetKind::Image, title, QStringLiteral("art/hero.png")));
        const AssetItem item{AssetItem::Kind::Image, title, {}};
        QVERIFY(browser->itemRow(item));
        const QSize pixels = QImage(f.dir.filePath(QStringLiteral("art/hero.png"))).size();
        QVERIFY(!pixels.isEmpty());

        // Dropped, it is an image entity, its size the image's and its pivot its top-left.
        const QPointF where(400, 300);
        QVERIFY(f.dropOnView(item, f.at(where)));
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Add image"));
        const EntityPath path{int(f.scene().entities.size()) - 1};
        const Entity* added = entityAt(f.scene(), path);
        QCOMPARE(added->image->asset, title);
        QVERIFY(added->image->sheet.isEmpty());
        QVERIFY(!added->size && !added->pivot);
        const std::optional<RectF> rect = entityRect(*added, Frame{}, f.canvas().imageMetrics());
        QVERIFY(rect);
        QCOMPARE(rect->x, 0.0);
        QCOMPARE(rect->width, double(pixels.width()));
        QCOMPARE(rect->height, double(pixels.height()));
        QCOMPARE(f.canvas().item(path)->sceneBoundingRect().size().toSize(), pixels);

        // With it selected, the asset field shows it, and chooses among images too.
        f.canvas().select({path});
        AssetField* field = f.window.inspector()->assetField();
        QCOMPARE(field->item(), std::optional(item));
        QCOMPARE(field->lineEdit()->text(), QStringLiteral("Images / title"));
        QCOMPARE(f.window.inspector()->summary()->text(), QStringLiteral("image · title"));
        QVERIFY(field->choose(IDLE_SPRITE.path()));
        QCOMPARE(entityAt(f.scene(), path)->image->sprite, IDLE_SPRITE.name);
        QVERIFY(field->choose(QStringLiteral("images / TITLE")));
        QCOMPARE(entityAt(f.scene(), path)->image->asset, title);

        // Removed from the manifest, its row goes, and the entity's image is missing.
        f.document().commands().push(
            std::make_unique<SetAssetCommand>(f.document(), AssetKind::Image, title, std::nullopt));
        QVERIFY(!browser->itemRow(item));
        QVERIFY(!browser->kindRow(AssetKind::Image));
        QVERIFY(field->isMissing());
        field->completer()->setCompletionPrefix(title);
        QCOMPARE(field->completer()->completionCount(), 0);
        QVERIFY(!field->choose(QStringLiteral("Images / other")));
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
        QCOMPARE(added->image->sheet, QStringLiteral("hero"));
        QCOMPARE(added->image->sprite, QStringLiteral("hero/idle.png"));
        QVERIFY(!added->size && !added->pivot);  // its sprite's
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
        QCOMPARE(added.image->animation, QStringLiteral("idle"));
        QCOMPARE(added.transform.pos, (Point{352, 656}));
    }

    void aHiddenOrLockedParentRefusesDrops()
    {
        Fixture f;
        f.canvas().setCurrentParent(ALTAR);
        f.canvas().setLocked(WORLD, true);  // an ancestor of it
        QVERIFY(!f.dropOnView(IDLE_SPRITE, f.at(QPointF(320, 600))));
        f.canvas().setLocked(WORLD, false);
        f.canvas().setHidden(ALTAR, true);
        QVERIFY(!f.dropOnView(IDLE_SPRITE, f.at(QPointF(320, 600))));
        QCOMPARE(f.document().commands().count(), 0);

        // Something else dropped is left to the view.
        QMimeData text;
        text.setText(QStringLiteral("hero/idle.png"));
        QDropEvent drop(f.at(QPointF(320, 600)), Qt::CopyAction, &text, Qt::LeftButton, {});
        QApplication::sendEvent(f.view->viewport(), &drop);
        QCOMPARE(f.document().commands().count(), 0);
    }

    void theInspectorShowsTheSelection()
    {
        Fixture f;
        InspectorPanel* inspector = f.window.inspector();
        // Nothing selected: the top level, the scene, with its settings.
        QVERIFY(inspector->showsTopLevel());
        QVERIFY(inspector->summary()->text().startsWith(QStringLiteral("The top level")));
        QCOMPARE(inspector->topLevel()->idField()->text(), LEVEL_1);
        QCOMPARE(inspector->topLevel()->widthBox()->value(), 3840.0);

        f.canvas().select({PLAYER});
        QVERIFY(!inspector->showsTopLevel());
        QCOMPARE(inspector->summary()->text(), QStringLiteral("sprite · hero · hero/idle.png"));
        QCOMPARE(inspector->idField()->text(), QStringLiteral("player"));
        QCOMPARE(inspector->classesField()->text(), QStringLiteral("actor"));
        QVERIFY(inspector->activeBox()->isChecked());
        QVERIFY(inspector->classesField()->toolTip().contains(QStringLiteral("not declared")));

        f.canvas().select({DOOR});  // the secret door is inactive and hidden
        QVERIFY(!inspector->activeBox()->isChecked());
        QVERIFY(!inspector->visibleBox()->isChecked());

        f.canvas().select({PLAYER, DOOR});
        QCOMPARE(inspector->summary()->text(), QStringLiteral("2 entities selected."));
        QVERIFY(!inspector->idField()->isEnabled());
        QVERIFY(!inspector->showsTopLevel());

        f.window.tabs()->setCurrentIndex(0);  // the flow
        QCOMPARE(inspector->summary()->text(),
                 QStringLiteral("Open a scene to inspect its entities."));
        QVERIFY(!inspector->showsTopLevel());
    }

    void theAssetFieldChoosesTheSelectedEntitysImage()
    {
        Fixture f;
        f.window.show();
        InspectorPanel* inspector = f.window.inspector();
        AssetField* field = inspector->assetField();
        QLineEdit* edit = field->lineEdit();
        // Nothing selected: nothing to choose for.
        QVERIFY(!field->isEnabled());

        // The player's sprite, by its path in the asset browser.
        f.canvas().select({PLAYER});
        QVERIFY(field->isEnabled());
        QCOMPARE(field->item(), std::optional(IDLE_SPRITE));
        QCOMPARE(edit->text(), QStringLiteral("Sprites / hero / Sprites / hero/idle.png"));
        QCOMPARE(IDLE_ANIMATION.path(), QStringLiteral("Sprites / hero / Animations / idle"));
        QVERIFY(!field->isMissing());

        // Typed, ignoring case: one undo step.
        type(edit, QStringLiteral("sprites / hero / sprites / props/bush.png"));
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Set the image of player"));
        QCOMPARE(entityAt(f.scene(), PLAYER)->image->sprite, QStringLiteral("props/bush.png"));
        QCOMPARE(edit->text(), QStringLiteral("Sprites / hero / Sprites / props/bush.png"));

        // Text that names no asset is red and never chosen; Escape shows the image again.
        const int commands = f.document().commands().count();
        type(edit, QStringLiteral("nothing"));
        QVERIFY(!field->isValid());
        QCOMPARE(edit->palette().color(QPalette::Text), QColor(200, 60, 40));
        QCOMPARE(f.document().commands().count(), commands);
        QTest::keyClick(edit, Qt::Key_Escape);
        QCOMPARE(edit->text(), QStringLiteral("Sprites / hero / Sprites / props/bush.png"));
        QVERIFY(field->isValid());

        // The popup lists the assets whose path contains the text, and chooses one.
        QCompleter* completer = field->completer();
        QCOMPARE(completer->filterMode(), Qt::MatchContains);
        completer->setCompletionPrefix(QStringLiteral("IDLE"));
        QCOMPARE(completer->completionCount(), 2);  // the sprite and the animation
        QModelIndex animation;
        for (int i = 0; i < completer->completionCount(); ++i) {
            const QModelIndex index = completer->completionModel()->index(i, 0);
            if (index.data().toString() == IDLE_ANIMATION.path()) {
                animation = index;
            }
            QVERIFY(!index.data(Qt::DecorationRole).value<QIcon>().isNull());
        }
        QVERIFY(animation.isValid());
        emit completer->activated(animation);
        QCOMPARE(entityAt(f.scene(), PLAYER)->image->animation, IDLE_ANIMATION.name);
        QCOMPARE(edit->text(), IDLE_ANIMATION.path());
        // Browsing lists them all: the sheet's four sprites and its animation.
        field->browseButton()->click();
        QCOMPARE(completer->completionPrefix(), QString());
        QCOMPARE(completer->completionCount(), 5);
        completer->popup()->hide();

        // Dropped from the asset browser.
        const AssetItem door{AssetItem::Kind::Sprite, QStringLiteral("hero"),
                             QStringLiteral("props/door.png")};
        const std::unique_ptr<QMimeData> data(door.toMimeData());
        QVERIFY(dropOn(field, data.get()));
        QCOMPARE(entityAt(f.scene(), PLAYER)->image->sprite, door.name);
        // Text isn't an asset.
        QMimeData text;
        text.setText(QStringLiteral("hero"));
        QVERIFY(!dropOn(field, &text));

        // Cleared: no image, its size kept.
        field->clearButton()->click();
        QVERIFY(!entityAt(f.scene(), PLAYER)->image);
        QVERIFY(entityAt(f.scene(), PLAYER)->size);
        QVERIFY(edit->text().isEmpty());
        QVERIFY(!field->clearButton()->isEnabled());
        QVERIFY(!inspector->showsImage());
        // And given one again.
        QVERIFY(dropOn(field, data.get()));
        QCOMPARE(entityAt(f.scene(), PLAYER)->image->sprite, door.name);
        f.document().commands().undo();
        f.document().commands().undo();
        QCOMPARE(entityAt(f.scene(), PLAYER)->image->sprite, door.name);
        QCOMPARE(field->item(), std::optional(door));

        // An asset that is gone stays, shown as missing.
        Image gone = door.toImage();
        gone.sprite = QStringLiteral("props/gone.png");
        inspector->setImage(gone);
        QVERIFY(field->isMissing());
        QCOMPARE(edit->text(),
                 QStringLiteral("Sprites / hero / Sprites / props/gone.png (missing)"));
        QCOMPARE(entityAt(f.scene(), PLAYER)->image->sprite, gone.sprite);

        // A nine-slice stays one with another sprite.
        Entity panel;
        panel.image = IDLE_SPRITE.toImage();
        panel.image->sprite = QStringLiteral("ui/panel.9.png");
        panel.image->mode = ImageMode::NineSlice;
        panel.size = Extent::px({30, 30});
        const EntityPath path = f.canvas().addEntity(panel, QPointF(100, 100));
        f.canvas().select({path});
        QVERIFY(dropOn(field, data.get()));
        QCOMPARE(entityAt(f.scene(), path)->image->mode, ImageMode::NineSlice);

        // Several selected: nothing to choose for.
        f.canvas().select({PLAYER, DOOR});
        QVERIFY(!field->isEnabled());
    }

    void theAssetBrowserOnlyBrowses()
    {
        Fixture f;
        f.window.show();
        f.window.projectPanel()->setCurrentWidget(f.window.assetBrowser());
        AssetBrowserPanel* assets = f.window.assetBrowser();
        QTreeWidget* tree = assets->tree();
        const auto centerOf = [&](const AssetItem& item) {
            QTreeWidgetItem* row = assets->itemRow(item);
            tree->scrollToItem(row);
            return tree->visualItemRect(row).center();
        };
        f.canvas().select({PLAYER});
        const QPoint animation = centerOf(IDLE_ANIMATION);

        // A click selects the row; the selected entity keeps its image.
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, animation);
        QCOMPARE(tree->currentItem(), assets->itemRow(IDLE_ANIMATION));
        QCOMPARE(entityAt(f.scene(), PLAYER)->image->sprite, IDLE_SPRITE.name);
        QCOMPARE(f.document().commands().count(), 0);

        // A press to drag away leaves the current row.
        QTest::mousePress(tree->viewport(), Qt::LeftButton, {}, centerOf(IDLE_SPRITE));
        QCOMPARE(tree->currentItem(), assets->itemRow(IDLE_ANIMATION));
        QTest::mouseRelease(tree->viewport(), Qt::LeftButton, {}, centerOf(IDLE_SPRITE));

        // A double-click places it, whatever is selected.
        const size_t count = f.scene().entities.size();
        QTest::mouseDClick(tree->viewport(), Qt::LeftButton, {}, centerOf(IDLE_SPRITE));
        QCOMPARE(f.scene().entities.size(), count + 1);
    }

    void thePresetsTellAClickFromADrag()
    {
        Fixture f;
        f.window.show();
        PresetsPanel* presets = f.window.presetsPanel();
        f.window.projectPanel()->setCurrentWidget(presets);
        QListWidget* list = presets->list();
        const auto centerOf = [&](const QString& id) {
            QListWidgetItem* row = presets->presetRow(id);
            list->scrollToItem(row);
            return list->visualItemRect(row).center();
        };
        presets->setCurrentPreset(QStringLiteral("camera"));
        const int commands = f.document().commands().count();
        const QPoint trigger = centerOf(QStringLiteral("trigger_region"));

        // Pressed, to drag it away: the current preset stays, the one the Presets tab edits.
        QTest::mousePress(list->viewport(), Qt::LeftButton, {}, trigger);
        QCOMPARE(presets->currentPreset(), QStringLiteral("camera"));
        QTest::mouseRelease(list->viewport(), Qt::LeftButton, {}, trigger);
        QCOMPARE(presets->currentPreset(), QStringLiteral("trigger_region"));
        QCOMPARE(f.document().commands().count(), commands);

        // A double-click places it.
        QTest::mouseDClick(list->viewport(), Qt::LeftButton, {}, trigger);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Add Trigger Region"));
    }

    void thePresetsTabShowsBuiltInPresetsReadOnly()
    {
        Fixture f;
        const QString camera = QStringLiteral("camera");
        PresetsView* presets = f.window.openPresets(camera);
        QCOMPARE(f.window.tabs()->currentWidget(), presets);
        SceneView* view = presets->view();
        QVERIFY(view);
        QCOMPARE(f.window.currentSceneView(), view);
        SceneCanvas& canvas = view->canvas();
        QCOMPARE(canvas.presetId(), std::optional(camera));
        QCOMPARE(f.window.sceneTreePanel()->canvas(), &canvas);
        QCOMPARE(f.window.sceneTreePanel()->topLevelRow()->text(0), camera);

        // Built in: shown, but nothing can be picked or added.
        QVERIFY(!canvas.isEditable());
        QVERIFY(canvas.item({0}));
        QVERIFY(!(canvas.item({0})->flags() & QGraphicsItem::ItemIsSelectable));
        QVERIFY(!canvas.canAddEntities());
        QVERIFY(!f.window.presetsPanel()->removeButton()->isEnabled());
        InspectorPanel* inspector = f.window.inspector();
        QVERIFY(inspector->showsTopLevel());
        PresetSettingsPanel* settings = inspector->presetTopLevel();
        QCOMPARE(settings->preset(), camera);
        QVERIFY(!settings->customizeButton()->isHidden());
        QVERIFY(settings->revertButton()->isHidden());
        QVERIFY(!settings->nameField()->isEnabled());

        // Customized, it is the project's to change.
        settings->customizeButton()->click();
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Customize preset camera"));
        QVERIFY(canvas.isEditable());
        QVERIFY(canvas.item({0})->flags() & QGraphicsItem::ItemIsSelectable);
        QVERIFY(settings->nameField()->isEnabled());
        QVERIFY(!settings->idField()->isEnabled());  // it customizes the built-in by its id
        settings->nameField()->setText(QStringLiteral("Main Camera"));
        settings->applyName();
        QCOMPARE(f.document().project().presets[camera].content.value(QStringLiteral("name")),
                 QJsonValue(QStringLiteral("Main Camera")));
        QCOMPARE(f.window.presetsPanel()->presetRow(camera)->text(), QStringLiteral("Main Camera"));
        QCOMPARE(f.window.presetsPanel()->removeButton()->text(), QStringLiteral("Revert"));
        settings->setSwatch(Color{16, 32, 48, 100});
        QCOMPARE(f.document().project().presets[camera].content.value(QStringLiteral("swatch")),
                 QJsonValue(QStringLiteral("#102030")));  // opaque

        // Reverted, Shoal's comes back.
        settings->revertButton()->click();
        QVERIFY(isBuiltInPreset(f.document().project(), camera));
        QVERIFY(!canvas.isEditable());
        QCOMPARE(f.window.presetsPanel()->presetRow(camera)->text(), QStringLiteral("Camera"));
    }

    void thePresetsTabAddsRenamesAndRemovesPresets()
    {
        Fixture f;
        PresetsView* presets = f.window.openPresets();
        QVERIFY(!presets->view());
        const QString id = f.window.presetsPanel()->newPreset();
        QCOMPARE(id, QStringLiteral("preset"));
        QCOMPARE(f.window.presetsPanel()->currentPreset(), id);
        QCOMPARE(presets->view()->canvas().presetId(), std::optional(id));
        QVERIFY(presets->view()->canvas().isEditable());

        // Sprites go in as they go in a scene.
        const EntityPath placed = presets->view()->place(IDLE_SPRITE, QPointF(10, 0));
        QCOMPARE(placed, (EntityPath{1}));
        QCOMPARE(f.document()
                     .project()
                     .presets[id]
                     .content.value(QStringLiteral("entities"))
                     .toArray()
                     .size(),
                 2);

        // Renamed, the canvas and the list follow.
        presets->view()->canvas().select({});
        PresetSettingsPanel* settings = f.window.inspector()->presetTopLevel();
        settings->idField()->setText(QStringLiteral("camera"));
        QVERIFY(!settings->applyId());  // a built-in one's
        QCOMPARE(settings->message(), QStringLiteral("Another preset is already “camera”."));
        settings->idField()->setText(QStringLiteral("pickup"));
        QVERIFY(settings->applyId());
        QCOMPARE(f.window.presetsPanel()->currentPreset(), QStringLiteral("pickup"));
        QCOMPARE(presets->view()->canvas().presetId(), std::optional(QStringLiteral("pickup")));
        QVERIFY(presets->view()->canvas().item(placed));

        // A preset keeps one entity at least.
        SceneCanvas& canvas = presets->view()->canvas();
        canvas.select({{0}, {1}});
        canvas.removeSelection();
        QCOMPARE(canvas.scene()->entities.size(), size_t(2));

        // Duplicated, then removed.
        const QString copy = f.window.presetsPanel()->duplicatePreset();
        QCOMPARE(copy, QStringLiteral("pickup_2"));
        QCOMPARE(f.document().project().presets[copy].content.value(QStringLiteral("name")),
                 QJsonValue(QStringLiteral("New Preset Copy")));
        QCOMPARE(f.window.presetsPanel()->currentPreset(), copy);
        f.window.presetsPanel()->removePreset();
        QVERIFY(!f.document().project().presets.contains(copy));
        QVERIFY(!presets->view());
        f.document().commands().undo();
        QVERIFY(f.document().project().presets.contains(copy));
    }

    void makesAPresetOfTheSelection()
    {
        Fixture f;
        const Entity player = *entityAt(f.scene(), PLAYER);
        f.canvas().select({PLAYER});
        const QString id = f.window.makePresetFromSelection();
        QCOMPARE(id, QStringLiteral("player"));
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Make preset player"));
        PresetsView* presets = f.window.presetsView();
        QCOMPARE(f.window.tabs()->currentWidget(), presets);
        QCOMPARE(presets->view()->canvas().presetId(), std::optional(id));

        // As drawn, around the center of what it draws.
        const Scene* tree = f.document().entityTree(presetTreeId(id));
        QCOMPARE(tree->entities.size(), size_t(1));
        const Entity& made = tree->entities[0];
        QCOMPARE(made.id, player.id);
        QCOMPARE(made.image, player.image);
        QCOMPARE(made.transform.flipX, player.transform.flipX);
        const EntityItem* item = presets->view()->canvas().item({0});
        const QPointF center = item->mapRectToScene(item->boundingRect()).center();
        QVERIFY(std::abs(center.x()) < 1e-6 && std::abs(center.y()) < 1e-6);
        // Placing it brings the player back, a fresh id given.
        f.window.tabs()->setCurrentWidget(f.view);
        const QList<EntityPath> paths = f.view->placePreset(id, QPointF(100, 100));
        QCOMPARE(entityAt(f.scene(), paths.first())->id, QStringLiteral("player_2"));
    }

    void aPresetLocksPropertiesOfTheEntitiesItCreates()
    {
        Fixture f;
        InspectorPanel* inspector = f.window.inspector();
        f.canvas().select({PLAYER});
        const QString id = f.window.makePresetFromSelection();
        PresetsView* presets = f.window.presetsView();
        SceneCanvas& presetCanvas = presets->view()->canvas();

        // In the preset, the inspector has its Defaults and its Locks.
        presetCanvas.select({{0}});
        QVERIFY(!inspector->tabs()->isHidden());
        QCOMPARE(inspector->tabs()->tabText(0), QStringLiteral("Defaults"));
        inspector->tabs()->setCurrentIndex(1);
        QVERIFY(inspector->editableButton(Lock::PosX)->isChecked());
        for (const Lock lock : {Lock::PosX, Lock::Angle, Lock::Tint}) {
            inspector->lockButton(lock)->click();
        }
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Lock the tint of player"));
        const Scene* tree = f.document().entityTree(presetTreeId(id));
        QCOMPARE(tree->entities[0].locks, Locks(Lock::PosX) | Lock::Angle | Lock::Tint);
        QVERIFY(inspector->lockButton(Lock::PosX)->isChecked());
        QVERIFY(!inspector->editableButton(Lock::PosX)->isChecked());
        // Its defaults stay editable there: they are what the locks keep.
        inspector->tabs()->setCurrentIndex(0);
        QVERIFY(inspector->xBox()->isEnabled());
        QVERIFY(inspector->lockNote()->isHidden());
        inspector->xBox()->setValue(5);
        QCOMPARE(f.document().entityTree(presetTreeId(id))->entities[0].transform.pos.x, 5.0);

        // Placed, the entity has its locks, in force: those fields are read-only.
        f.window.tabs()->setCurrentWidget(f.view);
        const QList<EntityPath> paths = f.view->placePreset(id, QPointF(100, 100));
        const EntityPath placed = paths.first();
        QCOMPARE(entityAt(f.scene(), placed)->locks, Locks(Lock::PosX) | Lock::Angle | Lock::Tint);
        // Its locked x is the preset's, wherever it is dropped; its y follows the drop.
        const Entity& preset = f.document().entityTree(presetTreeId(id))->entities[0];
        QCOMPARE(entityAt(f.scene(), placed)->transform.pos.x, 5.0);
        QCOMPARE(entityAt(f.scene(), placed)->transform.pos.y, preset.transform.pos.y + 100);
        const QList<EntityPath> again = f.view->placePreset(id, QPointF(700, 300));
        QCOMPARE(entityAt(f.scene(), again.first())->transform.pos.x, 5.0);
        f.document().commands().undo();
        f.canvas().select({placed});
        f.canvas().select({placed});
        QVERIFY(inspector->tabs()->isHidden());
        QVERIFY(!inspector->xBox()->isEnabled());
        QVERIFY(!inspector->angleBox()->isEnabled());
        QVERIFY(!inspector->tintButton()->isEnabled());
        QVERIFY(inspector->yBox()->isEnabled());
        QVERIFY(!inspector->lockNote()->isHidden());
        QCOMPARE(inspector->lockNote()->text(),
                 QStringLiteral("Locked by its preset: position x, angle, tint."));

        // Every edit of a locked property is refused: the inspector's, a nudge, a drag.
        const Entity before = *entityAt(f.scene(), placed);
        const int commands = f.document().commands().count();
        inspector->setTint(Color{255, 0, 0, 255});
        f.canvas().nudgeSelection(QPoint(1, 0), false);
        QCOMPARE(f.document().commands().count(), commands);
        QCOMPARE(*entityAt(f.scene(), placed), before);
        // Dragged, it moves on its free axis only.
        const EntityItem* item = f.canvas().item(placed);
        const QPoint at = f.at(item->mapRectToScene(item->visualRect()).center());
        f.drag(at, at + QPoint(40, 10));
        const Entity* moved = entityAt(f.scene(), placed);
        QCOMPARE(moved->transform.pos.x, before.transform.pos.x);
        QCOMPARE(moved->transform.pos.y, before.transform.pos.y + 10);
        QCOMPARE(f.canvas().item(placed)->pos().x(), item->pos().x());

        // Locks are the preset's: a scene has no Locks tab, and can't unlock one.
        QVERIFY(inspector->tabs()->isHidden());
        QVERIFY(!inspector->showsLocks());
        const int beforeUnlock = f.document().commands().count();
        inspector->setLocked(Lock::PosX, false);
        QCOMPARE(f.document().commands().count(), beforeUnlock);
        QVERIFY(entityAt(f.scene(), placed)->locks.testFlag(Lock::PosX));
        // Back in the preset, its tabs show again.
        f.window.tabs()->setCurrentWidget(presets);
        presetCanvas.select({{0}});
        QVERIFY(!inspector->tabs()->isHidden());
    }

    void theTopLevelEditsTheScene()
    {
        Fixture f;
        InspectorPanel* inspector = f.window.inspector();
        SceneSettingsPanel* topLevel = inspector->topLevel();
        // A top-level entity in percent follows the scene's size.
        Entity marker;
        marker.at = Anchor{{50, Unit::Percent}, {50, Unit::Percent}};
        const EntityPath path{int(f.scene().entities.size())};
        f.document().commands().push(
            std::make_unique<AddEntityCommand>(f.document(), LEVEL_1, path, marker));
        QCOMPARE(f.canvas().item(path)->scenePos(), QPointF(1920, 540));
        f.canvas().select({});
        QVERIFY(inspector->showsTopLevel());

        topLevel->widthBox()->setValue(1000);
        QCOMPARE(f.scene().size, (Size{1000, 1080}));
        QCOMPARE(f.canvas().item(path)->scenePos(), QPointF(500, 540));
        f.document().commands().undo();
        QCOMPARE(f.canvas().item(path)->scenePos(), QPointF(1920, 540));

        // Renamed, it still shows the scene.
        topLevel->idField()->setText(QStringLiteral("cave"));
        QVERIFY(topLevel->applyId());
        QCOMPARE(f.canvas().sceneId(), QStringLiteral("cave"));
        QCOMPARE(topLevel->idField()->text(), QStringLiteral("cave"));
        QCOMPARE(f.window.sceneTreePanel()->topLevelRow()->text(0), QStringLiteral("cave"));
        QVERIFY(topLevel->widthBox()->isEnabled());
    }

    void renamingAnIdRenamesItInTheTransitions()
    {
        Fixture f;
        InspectorPanel* inspector = f.window.inspector();
        f.canvas().select({PLAYER});
        type(inspector->idField(), QStringLiteral("hero"));

        QCOMPARE(f.document().commands().count(), 1);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Rename player to hero"));
        QCOMPARE(entityAt(f.scene(), PLAYER)->id, QStringLiteral("hero"));
        const Transition* l1ToL2 = f.document().findTransition(u"l1_to_l2");
        QVERIFY(l1ToL2->persist.entities.contains(QStringLiteral("hero")));
        QCOMPARE(inspector->idField()->text(), QStringLiteral("hero"));
        QVERIFY(f.window.sceneTreePanel()->entityRow(PLAYER)->text(0).startsWith(u"hero"));

        f.document().commands().undo();
        QCOMPARE(inspector->idField()->text(), QStringLiteral("player"));
        QVERIFY(f.document()
                    .findTransition(u"l1_to_l2")
                    ->persist.entities.contains(QStringLiteral("player")));
    }

    void refusesIdsItCantUse()
    {
        Fixture f;
        InspectorPanel* inspector = f.window.inspector();
        f.canvas().select({PLAYER});
        type(inspector->idField(), QStringLiteral("two words"));
        QVERIFY(inspector->message().startsWith(QStringLiteral("“two words” isn't a valid id")));
        type(inspector->idField(), QStringLiteral("camera"));
        QCOMPARE(inspector->message(),
                 QStringLiteral("Another entity of this scene is already “camera”."));
        // A transition carries the player away: it can't lose its id.
        type(inspector->idField(), QString());
        QCOMPARE(inspector->message(),
                 QStringLiteral("The transitions l1_to_l2 refer to “player”; it needs an id."));
        QCOMPARE(f.document().commands().count(), 0);

        // Escape puts the id back.
        QTest::keyClick(inspector->idField(), Qt::Key_Escape);
        QCOMPARE(inspector->idField()->text(), QStringLiteral("player"));
        QVERIFY(inspector->message().isEmpty());

        // An entity nothing refers to can be named and unnamed.
        f.canvas().select({BUSH});
        type(inspector->idField(), QStringLiteral("bush"));
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Name sprite bush"));
        type(inspector->idField(), QString());
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Remove the id of bush"));
        QCOMPARE(entityAt(f.scene(), BUSH)->id, QString());
    }

    void editsClasses()
    {
        Fixture f;
        InspectorPanel* inspector = f.window.inspector();
        f.canvas().select({PLAYER});
        type(inspector->classesField(), QStringLiteral(".actor, enemy actor"));
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Set the classes of player"));
        QCOMPARE(entityAt(f.scene(), PLAYER)->classes,
                 (QStringList{QStringLiteral("actor"), QStringLiteral("enemy")}));
        QCOMPARE(inspector->classesField()->text(), QStringLiteral("actor enemy"));
        QVERIFY(inspector->classesField()->toolTip().contains(QStringLiteral("<b>.enemy</b>")));

        type(inspector->classesField(), QStringLiteral("9lives"));
        QVERIFY(inspector->message().startsWith(QStringLiteral("“9lives” isn't a valid class")));
        QCOMPARE(f.document().commands().count(), 1);
    }

    void completesClassesFromTheRegistry()
    {
        Fixture f;
        InspectorPanel* inspector = f.window.inspector();
        QCompleter* completer = inspector->classCompleter();
        // Declared (trigger, enemy) and used (button, actor) alike.
        const QStringList known = qobject_cast<QStringListModel*>(completer->model())->stringList();
        for (const char* name : {"actor", "button", "camera_bounds", "enemy", "trigger"}) {
            QVERIFY2(known.contains(QLatin1StringView(name)), name);
        }

        f.canvas().select({PLAYER});
        QLineEdit* field = inspector->classesField();
        field->setFocus();
        field->end(false);
        QTest::keyClicks(field, QStringLiteral(" .cam"));
        QCOMPARE(completer->completionPrefix(), QStringLiteral("cam"));
        QCOMPARE(completer->currentCompletion(), QStringLiteral("camera_bounds"));
        emit completer->activated(QStringLiteral("camera_bounds"));
        QCOMPARE(field->text(), QStringLiteral("actor .camera_bounds "));
        QVERIFY(inspector->applyClasses());
        QCOMPARE(entityAt(f.scene(), PLAYER)->classes,
                 (QStringList{QStringLiteral("actor"), QStringLiteral("camera_bounds")}));
    }

    void togglesActiveAndVisible()
    {
        Fixture f;
        InspectorPanel* inspector = f.window.inspector();
        f.canvas().select({PLAYER});
        inspector->activeBox()->click();
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Deactivate player"));
        QVERIFY(!entityAt(f.scene(), PLAYER)->active);
        inspector->visibleBox()->click();
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Hide player"));
        QVERIFY(!entityAt(f.scene(), PLAYER)->visible);
        f.document().commands().undo();
        QVERIFY(inspector->visibleBox()->isChecked());
    }

    void editsTheTransform()
    {
        Fixture f;
        InspectorPanel* inspector = f.window.inspector();
        f.canvas().select({PLAYER});
        const auto typeNumber = [](QDoubleSpinBox* box, const QString& text) {
            box->setFocus();
            box->selectAll();
            QTest::keyClicks(box, text);
            QTest::keyClick(box, Qt::Key_Return);
        };

        QCOMPARE(inspector->xBox()->value(), 320.0);
        QCOMPARE(inspector->yBox()->value(), 640.0);
        typeNumber(inspector->xBox(), QStringLiteral("400.5"));
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Move player"));
        QCOMPARE(entityAt(f.scene(), PLAYER)->transform.pos, (Point{400.5, 640}));
        QCOMPARE(f.canvas().item(PLAYER)->pos(), QPointF(400.5, 640));
        f.document().commands().undo();
        QCOMPARE(inspector->xBox()->value(), 320.0);
        // A drag on the canvas shows in the boxes.
        const QPoint player = f.at(QPointF(320, 630));
        f.drag(player, player + QPoint(40, 10));
        QCOMPARE(inspector->xBox()->value(), 360.0);
        QCOMPARE(inspector->yBox()->value(), 650.0);
        f.document().commands().undo();

        typeNumber(inspector->angleBox(), QStringLiteral("45"));
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Rotate player"));
        QCOMPARE(entityAt(f.scene(), PLAYER)->transform.rot, 45.0);
        QVERIFY(inspector->angleBox()->text().endsWith(QStringLiteral("°")));

        typeNumber(inspector->scaleXBox(), QStringLiteral("2"));
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Scale player"));
        QCOMPARE(entityAt(f.scene(), PLAYER)->transform.scale, (Point{2, 1}));
        const int count = f.document().commands().count();
        typeNumber(inspector->scaleYBox(), QStringLiteral("0"));
        QCOMPARE(inspector->message(), QStringLiteral("A scale can't be zero."));
        QCOMPARE(f.document().commands().count(), count);
        QCOMPARE(inspector->scaleYBox()->value(), 1.0);

        QVERIFY(inspector->flipXBox()->isChecked());  // the player faces left
        inspector->flipXBox()->click();
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Unflip player horizontally"));
        QVERIFY(!entityAt(f.scene(), PLAYER)->transform.flipX);
        inspector->flipYBox()->click();
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Flip player vertically"));
        QVERIFY(entityAt(f.scene(), PLAYER)->transform.flipY);

        // Undo shows the model again, as do edits from the canvas.
        f.document().commands().undo();
        QVERIFY(!inspector->flipYBox()->isChecked());
        f.document().commands().undo();
        f.document().commands().undo();
        QCOMPARE(inspector->scaleXBox()->value(), 1.0);
        f.canvas().commitTransform(PLAYER, 90, Point{1, 1}, QStringLiteral("Rotate"));
        QCOMPARE(inspector->angleBox()->value(), 90.0);
    }

    void thePresetsPanelListsBuiltInAndProjectPresets()
    {
        Fixture f;
        PresetsPanel* panel = f.window.presetsPanel();
        QStringList names;
        for (int i = 0; i < panel->list()->count(); ++i) {
            names << panel->list()->item(i)->text();
        }
        QCOMPARE(names, (QStringList{QStringLiteral("Camera"), QStringLiteral("Camera Bounds"),
                                     QStringLiteral("enemy_grunt"), QStringLiteral("Spawn Mark"),
                                     QStringLiteral("Trigger Region")}));
        QVERIFY(panel->presetRow(QStringLiteral("enemy_grunt"))
                    ->toolTip()
                    .contains(QStringLiteral("presets/enemy_grunt.json")));

        // A preset the editor can't read still lets the project open, and says why.
        QFile broken(f.dir.filePath(QStringLiteral("presets/broken.json")));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write(R"({ "format": "shoal-preset", "version": 1, "entities": [] })");
        broken.close();
        QVERIFY(f.window.openProject(f.dir.path()).isEmpty());
        QListWidgetItem* row = panel->presetRow(QStringLiteral("broken"));
        QVERIFY(row);
        QCOMPARE(row->text(), QStringLiteral("broken (can't read)"));
        QVERIFY(row->toolTip().contains(QStringLiteral("a preset creates at least one entity")));
        QVERIFY(!(row->flags() & Qt::ItemIsDragEnabled));
    }

    void droppingAPresetCreatesFreshCopies()
    {
        Fixture f;
        const std::unique_ptr<QMimeData> camera(PresetsPanel::mimeData(QStringLiteral("camera")));
        QVERIFY(f.dropOnView(camera.get(), f.at(QPointF(500, 600))));
        QCOMPARE(f.document().commands().count(), 1);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Add Camera"));
        const EntityPath path{int(f.scene().entities.size()) - 1};
        const Entity* added = entityAt(f.scene(), path);
        QCOMPARE(added->id, QStringLiteral("camera_2"));  // level_1 has a camera already
        QCOMPARE(kindOf(*added), QStringLiteral("camera"));
        QCOMPARE(added->classes, QStringList{QStringLiteral("main")});
        QCOMPARE(added->size, std::optional(Extent::px({1920, 1080})));
        QVERIFY(std::abs(added->transform.pos.x - 500) <= 0.5);
        QCOMPARE(f.canvas().selectedPaths(), QList<EntityPath>{path});
        QCOMPARE(f.window.inspector()->idField()->text(), QStringLiteral("camera_2"));

        // The project's own: a sprite tagged .enemy.
        const std::unique_ptr<QMimeData> grunt(
            PresetsPanel::mimeData(QStringLiteral("enemy_grunt")));
        QVERIFY(f.dropOnView(grunt.get(), f.at(QPointF(520, 600))));
        QCOMPARE(f.scene().entities.back().classes, QStringList{QStringLiteral("enemy")});

        f.document().commands().undo();
        f.document().commands().undo();
        QCOMPARE(f.document().commands().index(), 0);
        QCOMPARE(int(f.scene().entities.size()), path.first());

        // Not into a locked parent.
        f.canvas().setCurrentParent(WORLD);
        f.canvas().setLocked(WORLD, true);
        QVERIFY(!f.dropOnView(camera.get(), f.at(QPointF(500, 600))));
    }

    void activatingAPresetPlacesItInTheMiddleOfTheView()
    {
        Fixture f;
        PresetsPanel* panel = f.window.presetsPanel();
        emit panel->list()->itemActivated(panel->presetRow(QStringLiteral("trigger_region")));
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Add Trigger Region"));
        const Entity& added = f.scene().entities.back();
        QCOMPARE(kindOf(added), QStringLiteral("region"));
        QCOMPARE(added.id, QStringLiteral("trigger"));
        const QPointF middle = f.view->visibleCenter();
        QCOMPARE(added.transform.pos, (Point{middle.x(), middle.y()}));
    }

    void activatingARowPlacesItInTheMiddleOfTheView()
    {
        Fixture f;
        AssetBrowserPanel* browser = f.window.assetBrowser();
        emit browser->tree()->itemActivated(browser->itemRow(IDLE_ANIMATION), 0);
        QCOMPARE(f.document().commands().count(), 1);
        const Entity& added = f.scene().entities.back();
        QCOMPARE(kindOf(added), QStringLiteral("animation"));
        const QPointF middle = f.view->mapToScene(f.view->viewport()->rect().center());
        QCOMPARE(added.transform.pos, (Point{middle.x(), middle.y()}));

        // With the flow tab current there is nowhere to put it.
        f.window.tabs()->setCurrentIndex(0);
        emit browser->tree()->itemActivated(browser->itemRow(IDLE_SPRITE), 0);
        QCOMPARE(f.document().commands().count(), 1);
        QVERIFY(f.window.statusBar()->currentMessage().startsWith(QStringLiteral("Open a scene")));
    }

    // --- parts, in the options scene: a nine-slice panel at (960, 540), 800 x 600 about its
    // center, that clips and is tinted; its close button, aligned to its top-right corner;
    // and a slider area aligned to its center. An inventory at (160, 160), 256 x 128, is
    // a tiled container of 64 x 64 tiles, 4 by 2, its children landing on each tile's
    // bottom-center: the hero in cell 1, 0, and the bush 8 up from cell 3, 1 ---

    void alignsChildrenToTheirParentsRect()
    {
        Fixture f;
        SceneView* view = f.window.openScene(QStringLiteral("options"));
        SceneCanvas& canvas = view->canvas();
        const EntityPath panel{0, 0};
        const EntityPath close{0, 0, 0};
        // The panel's rect runs from (-400, -300) to (400, 300) about its origin.
        QCOMPARE(canvas.item(panel)->rect(), std::optional(QRectF(-400, -300, 800, 600)));
        QCOMPARE(canvas.item(panel)->scenePos(), QPointF(960, 540));
        // The close button is 16 in from the top-right corner, its own top-right there.
        QCOMPARE(canvas.item(close)->pos(), QPointF(384, -284));
        QCOMPARE(canvas.item(close)->rect(), std::optional(QRectF(-48, 0, 48, 48)));
        QCOMPARE(canvas.item({0, 0, 1})->pos(), QPointF(0, 40));  // the center, then 40 down
        QVERIFY(canvas.item(panel)->flags() & QGraphicsItem::ItemClipsChildrenToShape);

        // A drag stores the position from the alignment point.
        canvas.select({close});
        canvas.item(close)->setPos(canvas.item(close)->pos() + QPointF(-10, 5));
        canvas.commitMoves();
        QCOMPARE(entityAt(*canvas.scene(), close)->transform.pos, (Point{-26, 21}));
    }

    void changingAUnitOrThePivotKeepsTheEntityInPlace()
    {
        Fixture f;
        SceneView* view = f.window.openScene(QStringLiteral("options"));
        SceneCanvas& canvas = view->canvas();
        InspectorPanel* inspector = f.window.inspector();
        // At the panel's top-right corner: 100%, 0%, 16 in from it; the panel is 800 x 600
        // about its center, at (960, 540).
        const EntityPath close{0, 0, 0};
        const auto corner = [&] {
            const EntityItem* item = canvas.item(close);
            return item->mapToScene(item->rect()->topLeft());
        };
        const QPointF where = corner();
        QCOMPARE(where, QPointF(1296, 256));
        canvas.select({close});
        QCOMPARE(unitOf(inspector->xUnitBox()), Unit::Percent);
        QCOMPARE(inspector->xBox()->value(), 100.0);
        QCOMPARE(inspector->yBox()->value(), 0.0);
        QVERIFY(inspector->showsOffset());
        QCOMPARE(inspector->offsetXBox()->value(), -16.0);
        QCOMPARE(inspector->offsetYBox()->value(), 16.0);
        // Its parent offers pixels and percentages, not tiles.
        QVERIFY(offers(inspector->xUnitBox(), Unit::Px));
        QVERIFY(offers(inspector->xUnitBox(), Unit::Percent));
        QVERIFY(!offers(inspector->xUnitBox(), Unit::Cell));
        QVERIFY(inspector->pivotBox()->isChecked());

        // x in pixels: from the panel's origin, its center.
        choose(inspector->xUnitBox(), Unit::Px);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Measure close in px"));
        const Entity* entity = entityAt(*canvas.scene(), close);
        QCOMPARE(entity->at, (Anchor{{}, {0, Unit::Percent}}));
        QCOMPARE(entity->transform.pos, (Point{384, 16}));
        QCOMPARE(inspector->xBox()->value(), 384.0);
        QVERIFY(!inspector->offsetXBox()->isEnabled());
        QCOMPARE(corner(), where);

        // Its pivot to its top-left: its rect stays where it is.
        inspector->pivotGrid()->button(0, 0)->click();
        entity = entityAt(*canvas.scene(), close);
        QCOMPARE(entity->pivot, std::optional(Point{0, 0}));
        QCOMPARE(entity->transform.pos, (Point{336, 16}));
        QCOMPARE(corner(), where);

        // Both in pixels: no offset.
        choose(inspector->yUnitBox(), Unit::Px);
        entity = entityAt(*canvas.scene(), close);
        QCOMPARE(entity->at, Anchor{});
        QCOMPARE(entity->transform.pos, (Point{336, -284}));
        QVERIFY(!inspector->showsOffset());
        QCOMPARE(corner(), where);

        // x in percent again: 92% across, with no offset.
        choose(inspector->xUnitBox(), Unit::Percent);
        entity = entityAt(*canvas.scene(), close);
        QCOMPARE(entity->at.x, (Length{92, Unit::Percent}));
        QCOMPARE(entity->transform.pos, (Point{0, -284}));
        QCOMPARE(corner(), where);

        // Typed, a percentage moves it: to the right edge.
        inspector->xBox()->setValue(100);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Move close"));
        QCOMPARE(corner(), QPointF(1360, 256));
        inspector->offsetXBox()->setValue(-48);
        QCOMPARE(corner(), QPointF(1312, 256));

        for (int i = 0; i < 6; ++i) {
            f.document().commands().undo();
        }
        QCOMPARE(corner(), where);
        QCOMPARE(entityAt(*canvas.scene(), close)->transform.pos, (Point{-16, 16}));
    }

    void theInspectorMeasuresSizesInUnits()
    {
        Fixture f;
        SceneView* view = f.window.openScene(QStringLiteral("options"));
        SceneCanvas& canvas = view->canvas();
        InspectorPanel* inspector = f.window.inspector();
        const EntityPath close{0, 0, 0};
        canvas.select({close});
        QCOMPARE(unitOf(inspector->widthUnitBox()), Unit::Px);
        QCOMPARE(inspector->widthBox()->value(), 48.0);

        // In percent of the panel's 800: the same size.
        choose(inspector->widthUnitBox(), Unit::Percent);
        QCOMPARE(f.document().commands().undoText(),
                 QStringLiteral("Measure the size of close in %"));
        QCOMPARE(entityAt(*canvas.scene(), close)->size,
                 std::optional(Extent{{6, Unit::Percent}, Length::px(48)}));
        QCOMPARE(inspector->widthBox()->value(), 6.0);
        QCOMPARE(canvas.item(close)->rect()->width(), 48.0);

        // It follows the panel.
        inspector->widthBox()->setValue(10);
        QCOMPARE(canvas.item(close)->rect()->width(), 80.0);
        canvas.select({{0, 0}});
        inspector->widthBox()->setValue(400);
        QCOMPARE(canvas.item(close)->rect()->width(), 40.0);

        // The scale handle keeps the percentage.
        canvas.select({close});
        canvas.commitSize(close, QSizeF(20, 48));
        QCOMPARE(entityAt(*canvas.scene(), close)->size,
                 std::optional(Extent{{5, Unit::Percent}, Length::px(48)}));
    }

    void placesChildrenOnTiles()
    {
        Fixture f;
        SceneView* view = f.window.openScene(QStringLiteral("options"));
        SceneCanvas& canvas = view->canvas();
        const EntityPath inventory{2};
        const EntityPath hero{2, 0};
        const EntityPath bush{2, 1};
        QCOMPARE(canvas.item(hero)->pos(), QPointF(96, 64));
        QCOMPARE(canvas.item(hero)->scenePos(), QPointF(256, 224));
        QCOMPARE(canvas.item(bush)->pos(), QPointF(224, 120));
        QVERIFY(canvas.item(hero)->placement().frame.grid);
        QVERIFY(!canvas.item(inventory)->placement().frame.grid);
        // Its grid shows when it helps: with it, or one of its children, selected, or as
        // the current parent.
        QVERIFY(canvas.item(inventory)->tileGrid());
        QVERIFY(canvas.gridPaths().isEmpty());
        canvas.select({hero});
        QCOMPARE(canvas.gridPaths(), QList<EntityPath>{inventory});
        canvas.select({inventory});
        QCOMPARE(canvas.gridPaths(), QList<EntityPath>{inventory});
        canvas.select({});
        QVERIFY(canvas.gridPaths().isEmpty());
        canvas.setCurrentParent(inventory);
        QCOMPARE(canvas.gridPaths(), QList<EntityPath>{inventory});
        canvas.setCurrentParent({});
        QVERIFY(canvas.gridPaths().isEmpty());
        // It takes no clicks: the child under it is still picked.
        canvas.select({hero});
        QCOMPARE(qgraphicsitem_cast<EntityItem*>(canvas.itemAt(QPointF(256, 214), {})),
                 canvas.item(hero));
        canvas.select({});

        // A drag goes from tile to tile: 70 right and 60 down is nearest to tile 2, 1.
        view->resetZoom();
        view->centerOn(300, 250);
        const QPoint from = view->mapFromScene(QPointF(256, 214));
        const QPoint to = view->mapFromScene(QPointF(326, 274));
        QTest::mousePress(view->viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(view->viewport(), from + (to - from) / 2);
        QTest::mouseMove(view->viewport(), to);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, {}, to);
        const Entity* moved = entityAt(*canvas.scene(), hero);
        QCOMPARE(moved->at, (Anchor{{2, Unit::Cell}, {1, Unit::Cell}}));
        QCOMPARE(moved->transform.pos, (Point{0, 0}));
        QCOMPARE(canvas.item(hero)->pos(), QPointF(160, 128));
        QCOMPARE(f.document().commands().count(), 1);

        // The arrow keys move it a tile, staying in the grid.
        const auto tile = [&](const EntityPath& path) {
            const Anchor& at = entityAt(*canvas.scene(), path)->at;
            return QPoint(int(at.x.value), int(at.y.value));
        };
        canvas.select({hero});
        QTest::keyClick(view, Qt::Key_Left);
        QCOMPARE(tile(hero), QPoint(1, 1));
        QTest::keyClick(view, Qt::Key_Down);  // the last row already
        QTest::keyClick(view, Qt::Key_Left, Qt::ShiftModifier);
        QCOMPARE(tile(hero), QPoint(0, 1));
        QCOMPARE(f.document().commands().count(), 3);

        // A sprite dropped in it goes on the tile under the cursor.
        canvas.setCurrentParent(inventory);
        const EntityPath placed = view->place(IDLE_SPRITE, QPointF(192, 260));
        QCOMPARE(placed, (EntityPath{2, 2}));
        QCOMPARE(entityAt(*canvas.scene(), placed)->at, (Anchor{{0, Unit::Cell}, {1, Unit::Cell}}));
        QCOMPARE(entityAt(*canvas.scene(), placed)->transform.pos, (Point{0, 0}));
    }

    void theInspectorEditsContainers()
    {
        Fixture f;
        SceneView* view = f.window.openScene(QStringLiteral("options"));
        SceneCanvas& canvas = view->canvas();
        InspectorPanel* inspector = f.window.inspector();
        const EntityPath inventory{2};
        const EntityPath hero{2, 0};

        // A child of a tiled container is placed in tiles, which only it is offered.
        canvas.select({hero});
        QCOMPARE(unitOf(inspector->xUnitBox()), Unit::Cell);
        QVERIFY(offers(inspector->xUnitBox(), Unit::Cell));
        QCOMPARE(inspector->xBox()->value(), 1.0);
        QCOMPARE(inspector->xBox()->decimals(), 0);
        inspector->yBox()->setValue(1);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Move slot_hero"));
        QCOMPARE(entityAt(*canvas.scene(), hero)->at, (Anchor{{1, Unit::Cell}, {1, Unit::Cell}}));
        canvas.select({{0, 0, 0}});
        QVERIFY(!offers(inspector->xUnitBox(), Unit::Cell));
        QVERIFY(!inspector->showsTilePivot());

        // The tiles: the children keep their cells.
        canvas.select({inventory});
        QCOMPARE(inspector->containerBox()->currentData().toInt(), int(ContainerType::Tiled));
        QVERIFY(inspector->showsTilePivot());
        QVERIFY(inspector->tilePivotGrid()->button(1, 2)->isChecked());  // bottom-center
        inspector->tileWidthBox()->setValue(32);
        QCOMPARE(entityAt(*canvas.scene(), inventory)->container->tileSize, (SizeF{32, 64}));
        QCOMPARE(canvas.item(hero)->pos(), QPointF(48, 128));
        inspector->tilePivotGrid()->button(0, 0)->click();
        QCOMPARE(entityAt(*canvas.scene(), inventory)->container->tilePivot, (Point{0, 0}));
        QCOMPARE(canvas.item(hero)->pos(), QPointF(32, 64));
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Set the tiles of inventory"));

        // Freed, its children stay where they are drawn, in pixels.
        const QPointF where = canvas.item(hero)->scenePos();
        inspector->setContainer(std::nullopt);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Stop tiling inventory"));
        QVERIFY(!entityAt(*canvas.scene(), inventory)->container);
        QCOMPARE(entityAt(*canvas.scene(), hero)->at, Anchor{});
        QCOMPARE(entityAt(*canvas.scene(), hero)->transform.pos, (Point{32, 64}));
        QCOMPARE(canvas.item(hero)->scenePos(), where);
        QVERIFY(!inspector->showsTilePivot());
        f.document().commands().undo();
        QCOMPARE(entityAt(*canvas.scene(), hero)->at, (Anchor{{1, Unit::Cell}, {1, Unit::Cell}}));

        // Tiled, an entity without a rect gets one, and its children go on the nearest
        // tiles where they are: the panel, at (960, 540), on the last of 2 x 2 tiles of 32.
        canvas.select({{0}});
        const QPointF panel = canvas.item({0, 0})->scenePos();
        inspector->setContainer(ContainerType::Tiled);
        const Entity* backdrop = entityAt(*canvas.scene(), {0});
        QCOMPARE(backdrop->size, std::optional(Extent::px({64, 64})));
        QCOMPARE(backdrop->container, std::optional(Container{}));
        const Entity* tiled = entityAt(*canvas.scene(), {0, 0});
        QCOMPARE(tiled->at, (Anchor{{1, Unit::Cell}, {1, Unit::Cell}}));
        QCOMPARE(tiled->transform.pos, (Point{912, 492}));
        QCOMPARE(canvas.item({0, 0})->scenePos(), panel);
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Tile backdrop"));
    }

    void tintsPassDownTheTree()
    {
        Fixture f;
        SceneView* view = f.window.openScene(QStringLiteral("options"));
        SceneCanvas& canvas = view->canvas();
        const Color translucent{255, 255, 255, 192};
        QCOMPARE(canvas.item({0, 0})->placement().tint, translucent);
        QCOMPARE(canvas.item({0, 0, 0})->placement().tint, Color{});      // its own only
        QCOMPARE(canvas.item({0, 0, 1})->placement().tint, translucent);  // mixed
        QCOMPARE(canvas.item({1})->placement().tint, Color{});  // inherits the top level's

        InspectorPanel* inspector = f.window.inspector();
        canvas.select({{0}});
        inspector->setTint(Color{255, 0, 0, 255});
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Tint backdrop"));
        QCOMPARE(canvas.item({0, 0, 1})->placement().tint, (Color{255, 0, 0, 192}));
        QCOMPARE(canvas.item({0, 0, 0})->placement().tint, Color{});
        canvas.select({{0, 0, 0}});
        QCOMPARE(inspector->tintModeBox()->currentData().toInt(), int(TintMode::Override));
        inspector->setTintMode(TintMode::Mix);
        QCOMPARE(canvas.item({0, 0, 0})->placement().tint, (Color{255, 0, 0, 192}));
        // The atlas it draws is multiplied once per color.
        const SceneCanvas::SpriteVisual visual =
            canvas.spriteVisual(QStringLiteral("hero"), QStringLiteral("ui/panel.9.png"));
        const QImage tinted = canvas.tintedAtlas(visual, Color{255, 0, 0, 255}).toImage();
        const QImage atlas = visual.atlas.toImage();
        const QPoint blue = visual.sprite->rect.center();
        QCOMPARE(qGreen(tinted.pixel(blue)), 0);
        QCOMPARE(qBlue(tinted.pixel(blue)), 0);
        QVERIFY(qBlue(atlas.pixel(blue)) > 0);
    }

    void theInspectorEditsParts()
    {
        Fixture f;
        InspectorPanel* inspector = f.window.inspector();

        // A size of its own keeps the one its sprite gives it, so nothing moves.
        f.canvas().select({PLAYER});
        const QRectF drawn = f.canvas().item(PLAYER)->visualRect();
        inspector->sizeBox()->click();
        QCOMPARE(f.document().commands().undoText(), QStringLiteral("Size player"));
        QCOMPARE(entityAt(f.scene(), PLAYER)->size, std::optional(Extent::px({16, 24})));
        QCOMPARE(f.canvas().item(PLAYER)->visualRect(), drawn);
        QVERIFY(inspector->widthBox()->isEnabled());
        inspector->sizeBox()->click();
        QVERIFY(!entityAt(f.scene(), PLAYER)->size);

        // The pivot, likewise: the sprite's until set.
        inspector->pivotBox()->click();
        QCOMPARE(entityAt(f.scene(), PLAYER)->pivot, std::optional(Point{0.5, 1}));
        QCOMPARE(f.canvas().item(PLAYER)->visualRect(), drawn);
        inspector->pivotBox()->click();
        QVERIFY(!entityAt(f.scene(), PLAYER)->pivot);

        // The image's mode, and taking it away, which leaves its rect where it was.
        QVERIFY(inspector->showsImage());
        inspector->setImageMode(ImageMode::NineSlice);
        QCOMPARE(entityAt(f.scene(), PLAYER)->image->mode, ImageMode::NineSlice);
        inspector->assetField()->clearButton()->click();
        const Entity* player = entityAt(f.scene(), PLAYER);
        QVERIFY(!player->image);
        QCOMPARE(player->size, std::optional(Extent::px({16, 24})));
        QCOMPARE(player->pivot, std::optional(Point{0.5, 1}));
        QCOMPARE(kindOf(*player), QStringLiteral("region"));
        QVERIFY(!inspector->showsImage());

        // A fill gives a mark a rect to fill.
        f.canvas().select({{1, 2, 0}});
        inspector->setFill(Color{0, 128, 0, 255});
        const Entity* mark = entityAt(f.scene(), {1, 2, 0});
        QCOMPARE(mark->fill, std::optional(Color{0, 128, 0, 255}));
        QCOMPARE(mark->size, std::optional(Extent::px({64, 64})));
        QVERIFY(inspector->fillBox()->isChecked());

        // Clipping, space and parallax.
        f.canvas().select({ALTAR});
        inspector->clipBox()->click();
        QVERIFY(entityAt(f.scene(), ALTAR)->clip);
        f.canvas().select({{0}});
        QVERIFY(inspector->parallaxBox()->isChecked());
        QCOMPARE(inspector->parallaxXBox()->value(), 0.5);
        inspector->setParallax(std::nullopt);
        QVERIFY(!entityAt(f.scene(), {0})->parallax);
        inspector->setSpace(Space::Screen);
        QCOMPARE(entityAt(f.scene(), {0})->space, std::optional(Space::Screen));
        QVERIFY(!f.canvas().item(BUSH)->isVisible());  // screen space isn't drawn yet
        QCOMPARE(f.document().commands().count(), 10);
    }
};

QTEST_MAIN(SceneEditorTests)
#include "SceneEditorTests.moc"
