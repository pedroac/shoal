#include "core/ProjectDocument.h"

#include <algorithm>

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

bool ProjectDocument::isEntityIdAvailable(const QString& sceneId, QStringView id,
                                          const EntityPath& ignoring) const
{
    const auto scene = m_project.scenes.find(sceneId);
    if (!isValidId(id) || scene == m_project.scenes.end()) {
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

QString ProjectDocument::uniqueSheetAlias(const QString& base) const
{
    QString alias;
    for (const QChar c : base) {
        const bool allowed = c.isLetterOrNumber() || c == u'_' || c == u'-';
        alias += allowed && c.unicode() < 128 ? c : QChar(u'_');
    }
    if (alias.isEmpty() || !isValidId(alias)) {
        alias.prepend(u'_');  // ids start with a letter or _
    }
    const QMap<QString, QString>& sheets = m_project.manifest.sheets;
    if (!sheets.contains(alias)) {
        return alias;
    }
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1_%2").arg(alias).arg(n);
        if (!sheets.contains(candidate)) {
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
    const auto it = m_project.scenes.find(sceneId);
    Q_ASSERT(it != m_project.scenes.end());
    return *it;
}

void ProjectDocument::emitSceneContentChanged(const QString& sceneId)
{
    emit sceneContentChanged(sceneId);
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

void ProjectDocument::setSheet(const QString& alias, std::optional<QString> file)
{
    if (file) {
        m_project.manifest.sheets.insert(alias, *file);
    }
    else {
        m_project.manifest.sheets.remove(alias);
    }
    emit sheetsChanged();
    emit changed();
}

void ProjectDocument::setClassRegistry(std::optional<ClassRegistry> registry)
{
    m_project.classes = std::move(registry);
    emit classesChanged();
    emit changed();
}

void ProjectDocument::insertLayer(const QString& sceneId, int index, Layer layer)
{
    std::vector<Layer>& layers = sceneForEdit(sceneId).layers;
    Q_ASSERT(index >= 0 && size_t(index) <= layers.size());
    layers.insert(layers.begin() + index, std::move(layer));
    emitSceneContentChanged(sceneId);
}

Layer ProjectDocument::removeLayer(const QString& sceneId, int index)
{
    std::vector<Layer>& layers = sceneForEdit(sceneId).layers;
    Q_ASSERT(index >= 0 && size_t(index) < layers.size());
    Layer removed = std::move(layers[size_t(index)]);
    layers.erase(layers.begin() + index);
    emitSceneContentChanged(sceneId);
    return removed;
}

void ProjectDocument::setLayer(const QString& sceneId, int index, Layer layer)
{
    std::vector<Layer>& layers = sceneForEdit(sceneId).layers;
    Q_ASSERT(index >= 0 && size_t(index) < layers.size());
    layers[size_t(index)] = std::move(layer);
    emitSceneContentChanged(sceneId);
}

void ProjectDocument::moveLayer(const QString& sceneId, int from, int to)
{
    std::vector<Layer>& layers = sceneForEdit(sceneId).layers;
    Q_ASSERT(from >= 0 && size_t(from) < layers.size() && to >= 0 && size_t(to) < layers.size());
    Layer layer = std::move(layers[size_t(from)]);
    layers.erase(layers.begin() + from);
    layers.insert(layers.begin() + to, std::move(layer));
    emitSceneContentChanged(sceneId);
}

}  // namespace shoal::core
