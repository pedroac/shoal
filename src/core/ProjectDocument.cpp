#include "core/ProjectDocument.h"

#include <QJsonArray>
#include <algorithm>

#include "core/Presets.h"
#include "core/ProjectCodec.h"

namespace shoal::core {

namespace {

auto transitionPosition(std::vector<Transition>& transitions, const QString& id)
{
    return std::lower_bound(transitions.begin(), transitions.end(), id,
                            [](const Transition& t, const QString& key) { return t.id < key; });
}

}  // namespace

ProjectDocument::ProjectDocument(Project project, QObject* parent)
    : QObject(parent), m_project(std::move(project))
{
}

ProjectDocument::ProjectDocument(Project project, QString directory, QStringList files)
    : m_project(std::move(project)), m_directory(std::move(directory)), m_files(std::move(files))
{
}

ProjectDocument::OpenResult ProjectDocument::open(const QString& directory)
{
    LoadResult loaded = loadProject(directory);
    OpenResult result;
    result.errors = std::move(loaded.errors);
    if (loaded.project) {
        result.document.reset(
            new ProjectDocument(std::move(*loaded.project), directory, std::move(loaded.files)));
    }
    return result;
}

SaveResult ProjectDocument::save()
{
    Q_ASSERT(!m_directory.isEmpty());
    SaveResult result = saveProject(m_project, m_directory, m_files);
    m_files = result.files;
    if (result.ok()) {
        m_commands.setClean();
    }
    return result;
}

SaveResult ProjectDocument::saveAs(const QString& directory)
{
    SaveResult result = saveProject(m_project, directory, findProjectFiles(directory));
    if (result.ok()) {
        m_directory = directory;
        m_files = result.files;
        m_commands.setClean();
    }
    return result;
}

bool ProjectDocument::isSceneIdAvailable(QStringView id, QStringView ignoring) const
{
    if (!isValidId(id)) {
        return false;
    }
    for (const QString& existing : m_project.scenes.keys()) {
        if (idsCollideAsFileNames(existing, id) && existing != ignoring) {
            return false;
        }
    }
    return true;
}

QString ProjectDocument::uniqueSceneId(const QString& base) const
{
    if (isSceneIdAvailable(base)) {
        return base;
    }
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1_%2").arg(base).arg(n);
        if (isSceneIdAvailable(candidate)) {
            return candidate;
        }
    }
}

QString ProjectDocument::uniqueTransitionId(const QString& base) const
{
    if (!findTransition(base)) {
        return base;
    }
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1_%2").arg(base).arg(n);
        if (!findTransition(candidate)) {
            return candidate;
        }
    }
}

const Transition* ProjectDocument::findTransition(QStringView id) const
{
    for (const Transition& transition : m_project.flow.transitions) {
        if (transition.id == id) {
            return &transition;
        }
    }
    return nullptr;
}

QStringList ProjectDocument::transitionsTouching(QStringView sceneId) const
{
    QStringList ids;
    for (const Transition& transition : m_project.flow.transitions) {
        if (transition.from == sceneId || transition.to == sceneId) {
            ids << transition.id;
        }
    }
    return ids;
}

const Scene* ProjectDocument::entityTree(const QString& treeId) const
{
    const std::optional<QString> presetId = presetOfTree(treeId);
    if (!presetId) {
        const auto scene = m_project.scenes.find(treeId);
        return scene == m_project.scenes.end() ? nullptr : &*scene;
    }
    if (const auto tree = m_presetTrees.constFind(*presetId); tree != m_presetTrees.cend()) {
        return &*tree;
    }
    const QMap<QString, Preset> presets = availablePresets(m_project);
    if (!presets.contains(*presetId)) {
        return nullptr;
    }
    const PresetRead read = readPresetDefinition(*presetId, presets.value(*presetId));
    if (!read.definition) {
        return nullptr;
    }
    Scene tree;
    tree.entities = read.definition->entities;
    return &*m_presetTrees.insert(*presetId, std::move(tree));
}

bool ProjectDocument::isTreeEditable(const QString& treeId) const
{
    const std::optional<QString> presetId = presetOfTree(treeId);
    if (!presetId) {
        return m_project.scenes.contains(treeId);
    }
    return m_project.presets.contains(*presetId) && entityTree(treeId);
}

bool ProjectDocument::isEntityIdAvailable(const QString& sceneId, QStringView id,
                                          const EntityPath& ignoring) const
{
    const Scene* scene = entityTree(sceneId);
    if (!isValidId(id) || !scene) {
        return false;
    }
    const EntityPath found = findEntity(*scene, id);
    return found.isEmpty() || found == ignoring;
}

QString ProjectDocument::uniqueEntityId(const QString& sceneId, const QString& base) const
{
    if (isEntityIdAvailable(sceneId, base)) {
        return base;
    }
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1_%2").arg(base).arg(n);
        if (isEntityIdAvailable(sceneId, candidate)) {
            return candidate;
        }
    }
}

QStringList ProjectDocument::transitionsNamingEntity(QStringView sceneId,
                                                     QStringView entityId) const
{
    QStringList ids;
    for (const Transition& transition : m_project.flow.transitions) {
        const Persist& persist = transition.persist;
        const bool carried =
            transition.from == sceneId && persist.entities.contains(entityId.toString());
        const bool landed =
            transition.to == sceneId && !persist.entities.keys(entityId.toString()).isEmpty();
        if (carried || landed) {
            ids << transition.id;
        }
    }
    return ids;
}

QString ProjectDocument::uniqueAssetAlias(const QString& base) const
{
    QString alias;
    for (const QChar c : base) {
        const bool allowed = c.isLetterOrNumber() || c == u'_' || c == u'-';
        alias += allowed && c.unicode() < 128 ? c : QChar(u'_');
    }
    if (alias.isEmpty() || !isValidId(alias)) {
        alias.prepend(u'_');  // ids start with a letter or _
    }
    const Assets& assets = m_project.manifest.assets;
    if (!assets.kindOf(alias)) {
        return alias;
    }
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1_%2").arg(alias).arg(n);
        if (!assets.kindOf(candidate)) {
            return candidate;
        }
    }
}

QMap<QString, int> ProjectDocument::classUses() const
{
    QMap<QString, int> uses;
    for (const Scene& scene : m_project.scenes) {
        forEachEntity(scene, [&](const Entity& entity, const EntityPath&) {
            for (const QString& name : entity.classes) {
                ++uses[name];
            }
        });
    }
    return uses;
}

void ProjectDocument::insertScene(const QString& id, Scene scene)
{
    Q_ASSERT(!m_project.scenes.contains(id));
    m_project.scenes.insert(id, std::move(scene));
    emit sceneAdded(id);
    emit changed();
}

Scene ProjectDocument::removeScene(const QString& id)
{
    Q_ASSERT(m_project.scenes.contains(id));
    Scene scene = m_project.scenes.take(id);
    emit sceneRemoved(id);
    emit changed();
    return scene;
}

void ProjectDocument::renameScene(const QString& from, const QString& to)
{
    Q_ASSERT(m_project.scenes.contains(from) && !m_project.scenes.contains(to));
    m_project.scenes.insert(to, m_project.scenes.take(from));
    if (m_project.manifest.start == from) {
        m_project.manifest.start = to;
    }
    for (Transition& transition : m_project.flow.transitions) {
        if (transition.from == from) {
            transition.from = to;
        }
        if (transition.to == from) {
            transition.to = to;
        }
    }
    QMap<QString, Point>& nodes = m_project.flow.nodePositions;
    if (nodes.contains(from)) {
        nodes.insert(to, nodes.take(from));
    }
    emit sceneRenamed(from, to);
    emit changed();
}

void ProjectDocument::setSceneNodePosition(const QString& id, std::optional<Point> position)
{
    if (position) {
        m_project.flow.nodePositions.insert(id, *position);
    }
    else {
        m_project.flow.nodePositions.remove(id);
    }
    emit sceneNodeMoved(id);
    emit changed();
}

void ProjectDocument::setStartScene(const QString& id)
{
    m_project.manifest.start = id;
    emit startSceneChanged(id);
    emit changed();
}

void ProjectDocument::setSceneSettings(const QString& id, Size size, std::optional<Fit> fit)
{
    Scene& scene = sceneForEdit(id);
    scene.size = size;
    scene.fit = fit;
    emit sceneSettingsChanged(id);
    emit changed();
}

void ProjectDocument::insertTransition(Transition transition)
{
    Q_ASSERT(!findTransition(transition.id));
    const QString id = transition.id;
    std::vector<Transition>& transitions = m_project.flow.transitions;
    transitions.insert(transitionPosition(transitions, id), std::move(transition));
    emit transitionAdded(id);
    emit changed();
}

Transition ProjectDocument::removeTransition(const QString& id)
{
    std::vector<Transition>& transitions = m_project.flow.transitions;
    const auto it = std::find_if(transitions.begin(), transitions.end(),
                                 [&](const Transition& t) { return t.id == id; });
    Q_ASSERT(it != transitions.end());
    Transition removed = std::move(*it);
    transitions.erase(it);
    emit transitionRemoved(id);
    emit changed();
    return removed;
}

void ProjectDocument::setTransition(Transition transition)
{
    const auto it = transitionPosition(m_project.flow.transitions, transition.id);
    Q_ASSERT(it != m_project.flow.transitions.end() && it->id == transition.id);
    const QString id = transition.id;
    *it = std::move(transition);
    emit transitionChanged(id);
    emit changed();
}

Scene& ProjectDocument::sceneForEdit(const QString& sceneId)
{
    if (const std::optional<QString> presetId = presetOfTree(sceneId)) {
        Q_ASSERT(isTreeEditable(sceneId));
        return m_presetTrees[*presetId];
    }
    const auto it = m_project.scenes.find(sceneId);
    Q_ASSERT(it != m_project.scenes.end());
    return *it;
}

void ProjectDocument::emitSceneContentChanged(const QString& sceneId)
{
    // A preset's entities go back into its file as they are now.
    const std::optional<QString> presetId = presetOfTree(sceneId);
    if (presetId) {
        QJsonArray entities;
        for (const Entity& entity : m_presetTrees.value(*presetId).entities) {
            entities.append(entityToJson(entity));
        }
        m_project.presets[*presetId].content.insert(QStringLiteral("entities"), entities);
    }
    emit sceneContentChanged(sceneId);
    if (presetId) {
        emit presetChanged(*presetId);
    }
    emit changed();
}

void ProjectDocument::renamePreset(const QString& from, const QString& to)
{
    Q_ASSERT(m_project.presets.contains(from) && !m_project.presets.contains(to));
    m_project.presets.insert(to, m_project.presets.take(from));
    m_presetTrees.remove(from);
    m_presetTrees.remove(to);
    emit presetRenamed(from, to);
    emit sceneContentChanged(presetTreeId(from));
    emit sceneContentChanged(presetTreeId(to));
    emit presetChanged(from);
    emit presetChanged(to);
    emit changed();
}

void ProjectDocument::setPreset(const QString& id, std::optional<Preset> preset)
{
    if (preset) {
        m_project.presets.insert(id, std::move(*preset));
    }
    else {
        m_project.presets.remove(id);
    }
    m_presetTrees.remove(id);  // made again from the file
    emit sceneContentChanged(presetTreeId(id));
    emit presetChanged(id);
    emit changed();
}

void ProjectDocument::insertEntity(const QString& sceneId, const EntityPath& path, Entity entity)
{
    std::vector<Entity>* siblings = siblingsOf(sceneForEdit(sceneId), path);
    Q_ASSERT(siblings && !path.isEmpty() && size_t(path.back()) <= siblings->size());
    siblings->insert(siblings->begin() + path.back(), std::move(entity));
    emitSceneContentChanged(sceneId);
}

Entity ProjectDocument::removeEntity(const QString& sceneId, const EntityPath& path)
{
    std::vector<Entity>* siblings = siblingsOf(sceneForEdit(sceneId), path);
    Q_ASSERT(siblings && !path.isEmpty() && size_t(path.back()) < siblings->size());
    const auto it = siblings->begin() + path.back();
    Entity removed = std::move(*it);
    siblings->erase(it);
    emitSceneContentChanged(sceneId);
    return removed;
}

void ProjectDocument::reorderEntities(const QString& sceneId, const EntityPath& parent,
                                      const QList<int>& order)
{
    Scene& scene = sceneForEdit(sceneId);
    Entity* parentEntity = parent.isEmpty() ? nullptr : entityAt(scene, parent);
    Q_ASSERT(parent.isEmpty() || parentEntity);
    std::vector<Entity>& siblings = parentEntity ? parentEntity->children : scene.entities;
    Q_ASSERT(size_t(order.size()) == siblings.size());
    std::vector<Entity> reordered;
    reordered.reserve(siblings.size());
    for (const int index : order) {
        reordered.push_back(std::move(siblings[size_t(index)]));
    }
    siblings = std::move(reordered);
    emitSceneContentChanged(sceneId);
}

void ProjectDocument::setEntityFields(const QString& sceneId, const EntityPath& path,
                                      const Entity& fields)
{
    Entity* entity = entityAt(sceneForEdit(sceneId), path);
    Q_ASSERT(entity);
    std::vector<Entity> children = std::move(entity->children);
    *entity = fields;
    entity->children = std::move(children);
    emitSceneContentChanged(sceneId);
}

void ProjectDocument::setAsset(AssetKind kind, const QString& alias, std::optional<QString> file)
{
    QMap<QString, QString>& declared = m_project.manifest.assets.of(kind);
    if (file) {
        declared.insert(alias, *file);
    }
    else {
        declared.remove(alias);
    }
    emit assetsChanged();
    emit changed();
}

void ProjectDocument::setClassRegistry(std::optional<ClassRegistry> registry)
{
    m_project.classes = std::move(registry);
    emit classesChanged();
    emit changed();
}

}  // namespace shoal::core
