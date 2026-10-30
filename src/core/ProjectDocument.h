#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>
#include <optional>

#include "core/CommandStack.h"
#include "core/Model.h"
#include "core/ProjectStore.h"
#include "core/SceneTree.h"

namespace shoal::core {

/// An open project: its model, where it lives, and its undo history.
///
/// Views read project() and listen to the signals. Changes go through commands pushed on
/// commands(); the mutators below are for those commands, and each emits its signal and
/// then changed().
class ProjectDocument : public QObject {
    Q_OBJECT
public:
    /// A project that isn't on disk yet; save it with saveAs().
    explicit ProjectDocument(Project project, QObject* parent = nullptr);

    struct OpenResult {
        std::unique_ptr<ProjectDocument> document;  ///< Null when the project can't be read.
        QList<FileError> errors;
    };
    static OpenResult open(const QString& directory);

    const Project& project() const { return m_project; }
    QString directory() const { return m_directory; }
    CommandStack& commands() { return m_commands; }

    /// Saves into directory(); on success the current state becomes clean.
    SaveResult save();
    /// Saves into `directory`, which becomes directory() on success. A project already in
    /// `directory` is replaced: its project files this project doesn't have are removed, so
    /// none of its scenes linger. Other files there are kept.
    SaveResult saveAs(const QString& directory);

    // --- queries ---

    /// The entities of a scene, or of a preset (see presetTreeId()), a built-in one too,
    /// as a scene of size 0 at the point it is placed at. Null when there is no such scene
    /// or preset, or the preset can't be read.
    const Scene* entityTree(const QString& treeId) const;
    /// Whether the entity mutators may change it: a scene, or a project's own preset. A
    /// built-in preset is read-only until a project preset customizes it.
    bool isTreeEditable(const QString& treeId) const;

    /// Whether `id` can name a new scene: a valid id that no other scene uses, ignoring case
    /// (scene ids are folder names). `ignoring` is the scene being renamed, if any.
    bool isSceneIdAvailable(QStringView id, QStringView ignoring = {}) const;
    /// `base` if it is available, otherwise `base_2`, `base_3`, ...
    QString uniqueSceneId(const QString& base) const;
    QString uniqueTransitionId(const QString& base) const;
    const Transition* findTransition(QStringView id) const;
    /// The ids of the transitions that start or end at `sceneId`.
    QStringList transitionsTouching(QStringView sceneId) const;

    /// Whether `id` can name an entity of the scene: a valid id no other entity there has.
    /// `ignoring` is the entity being renamed, if any.
    bool isEntityIdAvailable(const QString& sceneId, QStringView id,
                             const EntityPath& ignoring = {}) const;
    QString uniqueEntityId(const QString& sceneId, const QString& base) const;
    /// The ids of the transitions that name the entity `entityId` of the scene: carrying it
    /// away from there, or landing something on it.
    QStringList transitionsNamingEntity(QStringView sceneId, QStringView entityId) const;
    /// `base` made into an asset alias no declared asset of any kind has: characters an id
    /// can't have become `_`, then `_2`, `_3`, ... is added if needed.
    QString uniqueAssetAlias(const QString& base) const;

    /// How many entities have each class, across every scene: the classes in use, which
    /// with the declarations in classes.json make up the class registry.
    QMap<QString, int> classUses() const;

    // --- mutators, for commands ---

    void insertScene(const QString& id, Scene scene);
    Scene removeScene(const QString& id);
    /// Renames a scene and every reference to it: transitions, the start scene and its node.
    void renameScene(const QString& from, const QString& to);
    void setSceneNodePosition(const QString& id, std::optional<Point> position);
    void setStartScene(const QString& id);
    /// Sets a scene's size, and the fit that overrides the project's (null for none).
    void setSceneSettings(const QString& id, Size size, std::optional<Fit> fit);
    void insertTransition(Transition transition);
    Transition removeTransition(const QString& id);
    /// Replaces the transition with the same id.
    void setTransition(Transition transition);

    /// Inserts `entity` so that it ends up at `path`.
    void insertEntity(const QString& sceneId, const EntityPath& path, Entity entity);
    Entity removeEntity(const QString& sceneId, const EntityPath& path);
    /// Reorders the children of `parent` (the top level if empty): `order[i]` is the index,
    /// before, of the entity that ends at index i.
    void reorderEntities(const QString& sceneId, const EntityPath& parent, const QList<int>& order);
    /// Replaces an entity's own fields; its children stay.
    void setEntityFields(const QString& sceneId, const EntityPath& path, const Entity& fields);
    /// Adds, replaces or removes (null) a project preset; a built-in one with the same id
    /// is then customized, or back as it ships.
    void setPreset(const QString& id, std::optional<Preset> preset);
    /// Gives a project preset another id.
    void renamePreset(const QString& from, const QString& to);
    /// Declares an asset in the manifest, or changes its file; a null `file` removes it.
    void setAsset(AssetKind kind, const QString& alias, std::optional<QString> file);
    /// Replaces classes.json; null removes it.
    void setClassRegistry(std::optional<ClassRegistry> registry);

signals:
    void sceneAdded(const QString& id);
    void sceneRemoved(const QString& id);
    void sceneRenamed(const QString& from, const QString& to);
    void sceneNodeMoved(const QString& id);
    void startSceneChanged(const QString& id);
    /// A scene's size or fit changed.
    void sceneSettingsChanged(const QString& id);
    void transitionAdded(const QString& id);
    void transitionRemoved(const QString& id);
    void transitionChanged(const QString& id);
    /// A scene's entities changed, or a preset's (by presetTreeId()).
    void sceneContentChanged(const QString& sceneId);
    /// A project preset was added, changed or removed, its entities included.
    void presetChanged(const QString& id);
    /// Before presetChanged() for each.
    void presetRenamed(const QString& from, const QString& to);
    /// An asset was declared in the manifest, or its file changed, or it was removed.
    void assetsChanged();
    /// A class was declared, or its declaration changed or was removed.
    void classesChanged();
    void changed();

private:
    ProjectDocument(Project project, QString directory, QStringList files);
    /// A scene, or an editable preset's tree.
    Scene& sceneForEdit(const QString& sceneId);
    void emitSceneContentChanged(const QString& sceneId);

    Project m_project;
    /// Presets' entities as trees, made when first asked for, by preset id.
    mutable QMap<QString, Scene> m_presetTrees;
    QString m_directory;
    QStringList m_files;  ///< The project files on disk after the last load or save.
    CommandStack m_commands;
};

}  // namespace shoal::core
