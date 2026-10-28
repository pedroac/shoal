#include "editor/SceneTreePanel.h"

#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>
#include <memory>

#include "core/ProjectDocument.h"
#include "core/SceneCommands.h"
#include "editor/SceneCanvas.h"

namespace shoal::editor {

using core::Entity;
using core::EntityPath;

namespace {

enum Column { NAME, SHOWN, LOCKED };
enum Role { KIND = Qt::UserRole, LAYER_ID, PATH };
enum Kind { LAYER_ROW, ENTITY_ROW };

QString entityLabel(const Entity& entity)
{
    QString label =
        entity.id.isEmpty() ? QStringLiteral("‹%1›").arg(core::enumName(entity.type)) : entity.id;
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
        if (!dragged || !target || dragged->data(NAME, KIND).toInt() != ENTITY_ROW) {
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
      m_removeButton(new QToolButton(this)),
      m_backButton(new QToolButton(this)),
      m_frontButton(new QToolButton(this))
{
    m_tree->setObjectName(QStringLiteral("sceneTree"));
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Layers and entities"), tr("Shown"), tr("Locked")});
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(NAME, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(SHOWN, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(LOCKED, QHeaderView::ResizeToContents);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    const std::pair<QToolButton*, QString> buttons[] = {
        {m_addButton, tr("Add Layer")},
        {m_removeButton, tr("Remove Layer")},
        {m_backButton, tr("Move Layer Back")},
        {m_frontButton, tr("Move Layer Front")},
    };
    auto* buttonRow = new QHBoxLayout;
    for (const auto& [button, text] : buttons) {
        button->setText(text);
        button->setToolTip(text);
        buttonRow->addWidget(button);
    }
    m_addButton->setText(QStringLiteral("+"));
    m_removeButton->setText(QStringLiteral("−"));
    m_backButton->setText(QStringLiteral("↑"));
    m_frontButton->setText(QStringLiteral("↓"));
    buttonRow->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);
    layout->addLayout(buttonRow);

    connect(m_addButton, &QToolButton::clicked, this, &SceneTreePanel::addLayer);
    connect(m_removeButton, &QToolButton::clicked, this, &SceneTreePanel::removeCurrentLayer);
    connect(m_backButton, &QToolButton::clicked, this, &SceneTreePanel::moveCurrentLayerBack);
    connect(m_frontButton, &QToolButton::clicked, this, &SceneTreePanel::moveCurrentLayerFront);
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
    m_canvas = canvas;
    if (m_canvas) {
        connect(m_canvas, &SceneCanvas::contentChanged, this, &SceneTreePanel::rebuild);
        connect(m_canvas, &SceneCanvas::layerStateChanged, this, &SceneTreePanel::rebuild);
        connect(m_canvas, &SceneCanvas::currentLayerChanged, this, &SceneTreePanel::rebuild);
        connect(m_canvas, &QGraphicsScene::selectionChanged, this,
                &SceneTreePanel::onCanvasSelectionChanged);
    }
    setEnabled(m_canvas != nullptr);
    rebuild();
}

QTreeWidgetItem* SceneTreePanel::layerRow(const QString& layerId) const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = m_tree->topLevelItem(i);
        if (row->data(NAME, LAYER_ID).toString() == layerId) {
            return row;
        }
    }
    return nullptr;
}

QTreeWidgetItem* SceneTreePanel::entityRow(const EntityPath& path) const
{
    QTreeWidgetItemIterator it(m_tree);
    for (; *it; ++it) {
        if ((*it)->data(NAME, KIND).toInt() == ENTITY_ROW && pathOf(*it) == path) {
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
        std::function<void(QTreeWidgetItem*, const std::vector<Entity>&, EntityPath&)> addChildren =
            [&](QTreeWidgetItem* parent, const std::vector<Entity>& children, EntityPath& path) {
                for (size_t i = 0; i < children.size(); ++i) {
                    path.append(int(i));
                    auto* row = new QTreeWidgetItem(parent, {entityLabel(children[i])});
                    row->setData(NAME, KIND, ENTITY_ROW);
                    row->setData(NAME, PATH, QVariant::fromValue(path));
                    row->setToolTip(NAME, core::enumName(children[i].type));
                    addChildren(row, children[i].children, path);
                    path.removeLast();
                }
            };
        for (const core::Layer& layer : scene->layers) {
            auto* row = new QTreeWidgetItem(m_tree, {layer.id});
            row->setData(NAME, KIND, LAYER_ROW);
            row->setData(NAME, LAYER_ID, layer.id);
            row->setFlags((row->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDropEnabled) &
                          ~Qt::ItemIsDragEnabled);
            row->setCheckState(SHOWN,
                               m_canvas->isLayerHidden(layer.id) ? Qt::Unchecked : Qt::Checked);
            row->setCheckState(LOCKED,
                               m_canvas->isLayerLocked(layer.id) ? Qt::Checked : Qt::Unchecked);
            QFont font = row->font(NAME);
            font.setBold(layer.id == m_canvas->currentLayer());
            row->setFont(NAME, font);
            if (layer.space == core::Space::Screen) {
                row->setToolTip(NAME, tr("Screen space: laid out in a later version"));
                row->setForeground(NAME, palette().color(QPalette::PlaceholderText));
            }
            for (size_t i = 0; i < scene->entities.size(); ++i) {
                if (scene->entities[i].layer != layer.id) {
                    continue;
                }
                EntityPath path{int(i)};
                auto* top = new QTreeWidgetItem(row, {entityLabel(scene->entities[i])});
                top->setData(NAME, KIND, ENTITY_ROW);
                top->setData(NAME, PATH, QVariant::fromValue(path));
                top->setToolTip(NAME, core::enumName(scene->entities[i].type));
                addChildren(top, scene->entities[i].children, path);
            }
        }
        m_tree->expandAll();
    }
    m_syncing = false;
    onCanvasSelectionChanged();
    updateButtons();
}

void SceneTreePanel::updateButtons()
{
    const core::Scene* scene = m_canvas ? m_canvas->scene() : nullptr;
    const int index = currentLayerIndex();
    m_addButton->setEnabled(scene != nullptr);
    m_removeButton->setEnabled(index >= 0);
    m_backButton->setEnabled(index > 0);
    m_frontButton->setEnabled(scene && index >= 0 && size_t(index) + 1 < scene->layers.size());
}

int SceneTreePanel::currentLayerIndex() const
{
    const core::Scene* scene = m_canvas ? m_canvas->scene() : nullptr;
    return scene ? core::layerIndex(*scene, m_canvas->currentLayer()) : -1;
}

void SceneTreePanel::onTreeSelectionChanged()
{
    if (m_syncing || !m_canvas) {
        return;
    }
    QList<EntityPath> paths;
    QString layer;
    for (QTreeWidgetItem* row : m_tree->selectedItems()) {
        if (row->data(NAME, KIND).toInt() == ENTITY_ROW) {
            paths << pathOf(row);
        }
    }
    if (QTreeWidgetItem* current = m_tree->currentItem()) {
        QTreeWidgetItem* top = current;
        while (top->parent()) {
            top = top->parent();
        }
        layer = top->data(NAME, LAYER_ID).toString();
    }
    m_syncing = true;
    m_canvas->select(paths);
    m_syncing = false;
    if (!layer.isEmpty() && m_canvas->scene() &&
        m_canvas->scene()->layers[size_t(core::layerIndex(*m_canvas->scene(), layer))].space ==
            core::Space::World) {
        m_canvas->setCurrentLayer(layer);  // rebuilds the rows
    }
}

void SceneTreePanel::onCanvasSelectionChanged()
{
    if (m_syncing || !m_canvas) {
        return;
    }
    m_syncing = true;
    const QSignalBlocker blocker(m_tree);
    m_tree->clearSelection();
    for (const EntityPath& path : m_canvas->selectedPaths()) {
        if (QTreeWidgetItem* row = entityRow(path)) {
            row->setSelected(true);
            m_tree->scrollToItem(row);
        }
    }
    m_syncing = false;
}

void SceneTreePanel::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_syncing || !m_canvas || item->data(NAME, KIND).toInt() != LAYER_ROW) {
        return;
    }
    const QString layer = item->data(NAME, LAYER_ID).toString();
    if (column == SHOWN) {
        m_canvas->setLayerHidden(layer, item->checkState(SHOWN) != Qt::Checked);
    }
    else if (column == LOCKED) {
        m_canvas->setLayerLocked(layer, item->checkState(LOCKED) == Qt::Checked);
    }
}

bool SceneTreePanel::dropEntity(const EntityPath& from, QTreeWidgetItem* target, Drop drop)
{
    const core::Scene* scene = m_canvas ? m_canvas->scene() : nullptr;
    if (!scene || !target || !core::entityAt(*scene, from)) {
        return false;
    }
    EntityPath to;
    QString layer;  // for a top-level destination
    if (target->data(NAME, KIND).toInt() == LAYER_ROW) {
        if (drop != Drop::On) {
            return false;
        }
        layer = target->data(NAME, LAYER_ID).toString();
        // After the last entity on that layer, so it is drawn in front of them.
        int index = int(scene->entities.size());
        for (int i = int(scene->entities.size()) - 1; i >= 0; --i) {
            if (scene->entities[size_t(i)].layer == layer) {
                index = i + 1;
                break;
            }
        }
        to = {index};
    }
    else {
        const EntityPath targetPath = pathOf(target);
        if (targetPath == from || core::isAncestor(from, targetPath)) {
            return false;  // onto itself or into its own subtree
        }
        if (drop == Drop::On) {
            to = targetPath;
            to.append(int(core::entityAt(*scene, targetPath)->children.size()));
        }
        else {
            to = targetPath;
            if (drop == Drop::Below) {
                ++to.back();
            }
            if (to.size() == 1) {
                layer = core::layerOf(*scene, targetPath);
            }
        }
    }
    if (to == from) {
        return false;
    }
    const EntityPath final = core::pathAfterRemoval(to, from);
    const bool moves = final != from;
    const bool relayers =
        final.size() == 1 && !layer.isEmpty() && core::layerOf(*scene, from) != layer;
    if (!moves && !relayers) {
        return false;
    }

    core::ProjectDocument& document = m_canvas->document();
    const QString sceneId = m_canvas->sceneId();
    document.commands().beginMacro(tr("Move %1").arg(entityLabel(*core::entityAt(*scene, from))));
    if (moves) {
        document.commands().push(
            std::make_unique<core::MoveEntityCommand>(document, sceneId, from, final));
    }
    if (relayers) {
        Entity entity = *core::entityAt(*m_canvas->scene(), final);
        entity.layer = layer;
        document.commands().push(std::make_unique<core::EditEntitiesCommand>(
            document, sceneId, QList<std::pair<EntityPath, Entity>>{{final, entity}},
            tr("Move to layer %1").arg(layer)));
    }
    document.commands().endMacro();
    m_canvas->select({final});
    return true;
}

// --- layers ---

void SceneTreePanel::addLayer()
{
    const core::Scene* scene = m_canvas ? m_canvas->scene() : nullptr;
    if (!scene) {
        return;
    }
    QStringList ids;
    for (const core::Layer& layer : scene->layers) {
        ids << layer.id;
    }
    QString id = QStringLiteral("layer");
    for (int n = 2; ids.contains(id); ++n) {
        id = QStringLiteral("layer_%1").arg(n);
    }
    core::Layer layer;
    layer.id = id;
    const int index = currentLayerIndex() + 1;  // in front of the current one
    core::ProjectDocument& document = m_canvas->document();
    document.commands().push(std::make_unique<core::AddLayerCommand>(
        document, m_canvas->sceneId(), index > 0 ? index : int(scene->layers.size()), layer));
    m_canvas->setCurrentLayer(id);
}

void SceneTreePanel::removeCurrentLayer()
{
    const int index = currentLayerIndex();
    if (index < 0) {
        return;
    }
    m_canvas->clearSelection();
    core::ProjectDocument& document = m_canvas->document();
    document.commands().push(
        std::make_unique<core::RemoveLayerCommand>(document, m_canvas->sceneId(), index));
}

void SceneTreePanel::moveCurrentLayerBack()
{
    const int index = currentLayerIndex();
    if (index > 0) {
        core::ProjectDocument& document = m_canvas->document();
        document.commands().push(std::make_unique<core::MoveLayerCommand>(
            document, m_canvas->sceneId(), index, index - 1));
    }
}

void SceneTreePanel::moveCurrentLayerFront()
{
    const int index = currentLayerIndex();
    const core::Scene* scene = m_canvas ? m_canvas->scene() : nullptr;
    if (scene && index >= 0 && size_t(index) + 1 < scene->layers.size()) {
        core::ProjectDocument& document = m_canvas->document();
        document.commands().push(std::make_unique<core::MoveLayerCommand>(
            document, m_canvas->sceneId(), index, index + 1));
    }
}

void SceneTreePanel::renameLayer(const QString& layerId)
{
    const core::Scene* scene = m_canvas ? m_canvas->scene() : nullptr;
    const int index = scene ? core::layerIndex(*scene, layerId) : -1;
    if (index < 0) {
        return;
    }
    bool ok = false;
    const QString id = QInputDialog::getText(this, tr("Rename Layer"), tr("New id:"),
                                             QLineEdit::Normal, layerId, &ok)
                           .trimmed();
    if (!ok || id == layerId) {
        return;
    }
    if (!core::isValidId(id) || core::layerIndex(*scene, id) >= 0) {
        QMessageBox::warning(this, tr("Rename Layer"),
                             tr("%1 isn't a valid id, or another layer has it.").arg(id));
        return;
    }
    core::Layer layer = scene->layers[size_t(index)];
    layer.id = id;
    core::ProjectDocument& document = m_canvas->document();
    const bool current = m_canvas->currentLayer() == layerId;
    document.commands().push(
        std::make_unique<core::EditLayerCommand>(document, m_canvas->sceneId(), index, layer));
    if (current) {
        m_canvas->setCurrentLayer(id);
    }
}

void SceneTreePanel::showContextMenu(const QPoint& position)
{
    QTreeWidgetItem* row = m_tree->itemAt(position);
    if (!m_canvas || !row) {
        return;
    }
    QMenu menu(this);
    if (row->data(NAME, KIND).toInt() == LAYER_ROW) {
        const QString layer = row->data(NAME, LAYER_ID).toString();
        menu.addAction(tr("Rename Layer…"), this, [this, layer] { renameLayer(layer); });
        menu.addAction(tr("Remove Layer"), this, [this, layer] {
            m_canvas->setCurrentLayer(layer);
            removeCurrentLayer();
        });
    }
    else {
        menu.addAction(tr("Remove"), m_canvas.data(), &SceneCanvas::removeSelection);
    }
    menu.exec(m_tree->viewport()->mapToGlobal(position));
}

}  // namespace shoal::editor
