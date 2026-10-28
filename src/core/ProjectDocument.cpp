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
    SaveResult result = saveProject(m_project, directory, {});
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

}  // namespace shoal::core
