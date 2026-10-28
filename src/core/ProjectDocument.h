#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>
#include <optional>

#include "core/CommandStack.h"
#include "core/Model.h"
#include "core/ProjectStore.h"

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

    /// Whether `id` can name a new scene: a valid id that no other scene uses, ignoring case
    /// (scene ids are folder names). `ignoring` is the scene being renamed, if any.
    bool isSceneIdAvailable(QStringView id, QStringView ignoring = {}) const;
    /// `base` if it is available, otherwise `base_2`, `base_3`, ...
    QString uniqueSceneId(const QString& base) const;
    QString uniqueTransitionId(const QString& base) const;
    const Transition* findTransition(QStringView id) const;
    /// The ids of the transitions that start or end at `sceneId`.
    QStringList transitionsTouching(QStringView sceneId) const;

    // --- mutators, for commands ---

    void insertScene(const QString& id, Scene scene);
    Scene removeScene(const QString& id);
    /// Renames a scene and every reference to it: transitions, the start scene and its node.
    void renameScene(const QString& from, const QString& to);
    void setSceneNodePosition(const QString& id, std::optional<Point> position);
    void setStartScene(const QString& id);
    void insertTransition(Transition transition);
    Transition removeTransition(const QString& id);

signals:
    void sceneAdded(const QString& id);
    void sceneRemoved(const QString& id);
    void sceneRenamed(const QString& from, const QString& to);
    void sceneNodeMoved(const QString& id);
    void startSceneChanged(const QString& id);
    void transitionAdded(const QString& id);
    void transitionRemoved(const QString& id);
    void changed();

private:
    ProjectDocument(Project project, QString directory, QStringList files);

    Project m_project;
    QString m_directory;
    QStringList m_files;  ///< The project files on disk after the last load or save.
    CommandStack m_commands;
};

}  // namespace shoal::core
