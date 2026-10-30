#include "editor/SceneTreePanel.h"

#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>
#include <memory>

#include "core/ProjectDocument.h"
#include "core/SceneCommands.h"
#include "editor/EntityItem.h"
#include "editor/SceneCanvas.h"

namespace shoal::editor {

using core::Entity;
using core::EntityPath;

namespace {

enum Column { NAME, SHOWN, LOCKED };
enum Role { PATH = Qt::UserRole };

QString entityLabel(const Entity& entity)
{
    QString label =
        entity.id.isEmpty() ? QStringLiteral("‹%1›").arg(core::kindOf(entity)) : entity.id;
    for (const QString& name : entity.classes) {
        label += QStringLiteral(" .") + name;
    }
    return label;
}

EntityPath pathOf(const QTreeWidgetItem* row)
{
    return row->data(NAME, PATH).value<EntityPath>();
}

}  // namespace

/// Turns drops into commands instead of letting the widget move its rows.
class SceneTreePanel::Tree : public QTreeWidget {
public:
    explicit Tree(SceneTreePanel& panel) : QTreeWidget(&panel), m_panel(panel) {}

protected:
    void dropEvent(QDropEvent* event) override
    {
        QTreeWidgetItem* dragged = currentItem();
        QTreeWidgetItem* target = itemAt(event->position().toPoint());
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        if (!dragged) {
            return;
        }
        Drop drop = Drop::On;
        if (dropIndicatorPosition() == AboveItem) {
            drop = Drop::Above;
        }
        else if (dropIndicatorPosition() == BelowItem) {
            drop = Drop::Below;
        }
        m_panel.dropEntity(pathOf(dragged), target, drop);
    }

private:
    SceneTreePanel& m_panel;
};

SceneTreePanel::SceneTreePanel(QWidget* parent)
    : QWidget(parent),
      m_tree(new Tree(*this)),
      m_addButton(new QToolButton(this)),
      m_removeButton(new QToolButton(this))
{
    m_tree->setObjectName(QStringLiteral("sceneTree"));
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Entities"), tr("Shown"), tr("Locked")});
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(NAME, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(SHOWN, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(LOCKED, QHeaderView::ResizeToContents);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    m_addButton->setText(QStringLiteral("+"));
    m_addButton->setToolTip(tr("Add a group to the current parent"));
    m_removeButton->setText(QStringLiteral("−"));
    m_removeButton->setToolTip(tr("Remove the selection"));
    auto* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(m_addButton);
    buttonRow->addWidget(m_removeButton);
    buttonRow->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);
    layout->addLayout(buttonRow);

    connect(m_addButton, &QToolButton::clicked, this, &SceneTreePanel::addGroup);
    connect(m_removeButton, &QToolButton::clicked, this, &SceneTreePanel::removeSelection);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &SceneTreePanel::onTreeSelectionChanged);
    connect(m_tree, &QTreeWidget::itemChanged, this, &SceneTreePanel::onItemChanged);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this,
            &SceneTreePanel::showContextMenu);
    setCanvas(nullptr);
}

QTreeWidget* SceneTreePanel::tree() const
{
    return m_tree;
}

void SceneTreePanel::setCanvas(SceneCanvas* canvas)
{
    if (m_canvas) {
        disconnect(m_canvas, nullptr, this, nullptr);
    }
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_canvas = canvas;
    m_document = canvas ? &canvas->document() : nullptr;
    if (m_canvas) {
        // The top level's row is named after the scene; the canvas follows it first.
        connect(m_document, &core::ProjectDocument::sceneRenamed, this, &SceneTreePanel::rebuild);
        connect(m_canvas, &SceneCanvas::contentChanged, this, &SceneTreePanel::rebuild);
        connect(m_canvas, &SceneCanvas::entityStateChanged, this, &SceneTreePanel::rebuild);
        connect(m_canvas, &SceneCanvas::currentParentChanged, this, &SceneTreePanel::rebuild);
        connect(m_canvas, &QGraphicsScene::selectionChanged, this,
                &SceneTreePanel::onCanvasSelectionChanged);
    }
    setEnabled(m_canvas != nullptr);
    rebuild();
}

QTreeWidgetItem* SceneTreePanel::entityRow(const EntityPath& path) const
{
    QTreeWidgetItemIterator it(m_tree);
    for (; *it; ++it) {
        if (pathOf(*it) == path) {
            return *it;
        }
    }
    return nullptr;
}

void SceneTreePanel::rebuild()
{
    const QSignalBlocker blocker(m_tree);
    m_syncing = true;
    m_tree->clear();
    const core::Scene* scene = m_canvas ? m_canvas->scene() : nullptr;
    if (scene) {
        // The top level: the scene, holding the top-level entities. It can't be dragged,
        // hidden or locked.
        auto* root = new QTreeWidgetItem(m_tree, {m_canvas->title()});
        root->setData(NAME, PATH, QVariant::fromValue(EntityPath{}));
        root->setFlags(root->flags() & ~Qt::ItemIsDragEnabled);
        root->setIcon(NAME, style()->standardIcon(QStyle::SP_DirHomeIcon));
        QString rootTip = m_canvas->presetId()
                              ? tr("The top level: the preset, placed in the parent new "
                                   "entities go into")
                              : tr("The top level: the scene, %1 × %2, which its entities "
                                   "measure against")
                                    .arg(scene->size.width)
                                    .arg(scene->size.height);
        if (m_canvas->currentParent().isEmpty()) {
            QFont font = root->font(NAME);
            font.setBold(true);
            root->setFont(NAME, font);
            rootTip += tr(", where new entities go");
        }
        root->setToolTip(NAME, rootTip);
        const std::function<void(QTreeWidgetItem*, const std::vector<Entity>&, EntityPath&)>
            addRows = [&](QTreeWidgetItem* parent, const std::vector<Entity>& entities,
                          EntityPath& path) {
                for (size_t i = 0; i < entities.size(); ++i) {
                    const Entity& entity = entities[i];
                    path.append(int(i));
                    auto* row = new QTreeWidgetItem(parent, {entityLabel(entity)});
                    row->setData(NAME, PATH, QVariant::fromValue(path));
                    row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
                    row->setCheckState(SHOWN,
                                       m_canvas->isHidden(path) ? Qt::Unchecked : Qt::Checked);
                    row->setCheckState(LOCKED,
                                       m_canvas->isLocked(path) ? Qt::Checked : Qt::Unchecked);
                    QString tooltip = core::kindOf(entity);
                    if (path == m_canvas->currentParent()) {
                        QFont font = row->font(NAME);
                        font.setBold(true);
                        row->setFont(NAME, font);
                        tooltip += tr(", where new entities go");
                    }
                    if (m_canvas->isInScreenSpace(path)) {
                        tooltip += tr(", in screen space: laid out in a later version");
                        row->setForeground(NAME, palette().color(QPalette::PlaceholderText));
                    }
                    row->setToolTip(NAME, tooltip);
                    addRows(row, entity.children, path);
                    path.removeLast();
                }
            };
        EntityPath path;
        addRows(root, scene->entities, path);
        m_tree->expandAll();
    }
    m_syncing = false;
    onCanvasSelectionChanged();
    updateButtons();
}

void SceneTreePanel::updateButtons()
{
    m_addButton->setEnabled(m_canvas && m_canvas->scene() && m_canvas->canAddEntities());
    m_removeButton->setEnabled(m_canvas && !m_canvas->selectedPaths().isEmpty());
}

void SceneTreePanel::onTreeSelectionChanged()
{
    if (m_syncing || !m_canvas) {
        return;
    }
    // The top level's row stands for no entity selected.
    QList<EntityPath> paths;
    for (QTreeWidgetItem* row : m_tree->selectedItems()) {
        if (!pathOf(row).isEmpty()) {
            paths << pathOf(row);
        }
    }
    m_syncing = true;
    m_canvas->select(paths);
    m_syncing = false;
    updateButtons();
}

void SceneTreePanel::onCanvasSelectionChanged()
{
    if (m_syncing || !m_canvas) {
        return;
    }
    m_syncing = true;
    const QSignalBlocker blocker(m_tree);
    m_tree->clearSelection();
    const QList<EntityPath> selected = m_canvas->selectedPaths();
    for (const EntityPath& path : selected) {
        if (QTreeWidgetItem* row = entityRow(path)) {
            row->setSelected(true);
            m_tree->scrollToItem(row);
        }
    }
    if (QTreeWidgetItem* root = topLevelRow(); root && selected.isEmpty()) {
        root->setSelected(true);
    }
    m_syncing = false;
    updateButtons();
}

void SceneTreePanel::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_syncing || !m_canvas) {
        return;
    }
    const EntityPath path = pathOf(item);
    if (path.isEmpty()) {
        return;  // the top level has no check boxes
    }
    const bool checked = item->checkState(column) == Qt::Checked;
    // Applied once the row's own call returns: the canvas rebuilds the rows, this one too.
    QMetaObject::invokeMethod(
        this,
        [this, path, column, checked] {
            if (!m_canvas) {
                return;
            }
            if (column == SHOWN) {
                m_canvas->setHidden(path, !checked);
            }
            else if (column == LOCKED) {
                m_canvas->setLocked(path, checked);
            }
        },
        Qt::QueuedConnection);
}

bool SceneTreePanel::dropEntity(const EntityPath& from, QTreeWidgetItem* target, Drop drop)
{
    const core::Scene* scene = m_canvas ? m_canvas->scene() : nullptr;
    if (!scene || !core::entityAt(*scene, from)) {
        return false;
    }
    EntityPath to;
    if (!target || (pathOf(target).isEmpty() && drop == Drop::On)) {
        to = {int(scene->entities.size())};  // in front of everything
    }
    else if (pathOf(target).isEmpty()) {
        return false;  // nothing goes beside the top level
    }
    else {
        const EntityPath targetPath = pathOf(target);
        if (targetPath == from || core::isAncestor(from, targetPath)) {
            return false;  // onto itself or into its own subtree
        }
        to = targetPath;
        if (drop == Drop::On) {
            to.append(int(core::entityAt(*scene, targetPath)->children.size()));
        }
        else if (drop == Drop::Below) {
            ++to.back();
        }
    }
    if (to == from) {
        return false;
    }
    const EntityPath final = core::pathAfterRemoval(to, from);
    if (final == from) {
        return false;
    }
    m_canvas->moveEntity(from, final);
    return true;
}

void SceneTreePanel::addGroup()
{
    if (!m_canvas || !m_canvas->canAddEntities()) {
        return;
    }
    // At the parent's origin: a group draws nothing, and its children are placed in it.
    const EntityPath parent = m_canvas->currentParent();
    const EntityItem* parentItem = parent.isEmpty() ? nullptr : m_canvas->item(parent);
    const bool snapping = m_canvas->snapping();
    m_canvas->setSnapping(false);
    m_canvas->addEntity(Entity{}, parentItem ? parentItem->mapToScene(QPointF()) : QPointF());
    m_canvas->setSnapping(snapping);
}

void SceneTreePanel::removeSelection()
{
    if (m_canvas) {
        m_canvas->removeSelection();
    }
}

void SceneTreePanel::showContextMenu(const QPoint& position)
{
    if (!m_canvas || !m_canvas->scene()) {
        return;
    }
    QTreeWidgetItem* row = m_tree->itemAt(position);
    if (row && pathOf(row).isEmpty()) {
        row = nullptr;  // the top level: only where new entities go
    }
    QMenu menu(this);
    if (row) {
        const EntityPath path = pathOf(row);
        m_canvas->addArrangeActions(menu);
        menu.addSeparator();
        QAction* here = menu.addAction(tr("New Entities Go Here"), this,
                                       [this, path] { m_canvas->setCurrentParent(path); });
        here->setEnabled(path != m_canvas->currentParent() && !m_canvas->isInScreenSpace(path));
    }
    QAction* top = menu.addAction(tr("New Entities Go at the Top Level"), this,
                                  [this] { m_canvas->setCurrentParent({}); });
    top->setEnabled(!m_canvas->currentParent().isEmpty());
    if (row) {
        menu.addSeparator();
        menu.addAction(tr("Make Preset"), m_canvas.data(), &SceneCanvas::makePresetFromSelection);
        menu.addAction(tr("Remove"), m_canvas.data(), &SceneCanvas::removeSelection);
    }
    menu.exec(m_tree->viewport()->mapToGlobal(position));
}

}  // namespace shoal::editor
