#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

#include "core/CommandStack.h"
#include "core/Model.h"

namespace shoal::core {

class ProjectDocument;

/// Adds a scene, optionally placing its node in the flow view.
class AddSceneCommand : public Command {
public:
    AddSceneCommand(ProjectDocument& document, QString id, Scene scene,
                    std::optional<Point> nodePosition);
    QString text() const override;
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_id;
    Scene m_scene;
    std::optional<Point> m_nodePosition;
};

/// Removes a scene with its modifiers, its node, and the transitions that start or end at
/// it. If it was the start scene, the project is left without one.
class RemoveSceneCommand : public Command {
public:
    RemoveSceneCommand(ProjectDocument& document, QString id);
    QString text() const override;
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_id;
    Scene m_scene;
    std::optional<Point> m_nodePosition;
    std::vector<Transition> m_transitions;
    bool m_wasStart = false;
};

/// Renames a scene (and so its folder) and updates every reference to it.
class RenameSceneCommand : public Command {
public:
    RenameSceneCommand(ProjectDocument& document, QString from, QString to);
    QString text() const override;
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_from;
    QString m_to;
};

/// Moves scene nodes in the flow view. The view pushes one when a drag ends, so each drag
/// is one step however many nodes it moved.
class MoveSceneNodesCommand : public Command {
public:
    MoveSceneNodesCommand(ProjectDocument& document, QMap<QString, Point> to);
    QString text() const override;
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QMap<QString, std::optional<Point>> m_from;
    QMap<QString, Point> m_to;
};

class SetStartSceneCommand : public Command {
public:
    SetStartSceneCommand(ProjectDocument& document, QString id);
    QString text() const override;
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_from;
    QString m_to;
};

/// Changes a scene's size and the fit that overrides the project's.
class SetSceneSettingsCommand : public Command {
public:
    SetSceneSettingsCommand(ProjectDocument& document, QString id, Size size,
                            std::optional<Fit> fit, QString text);
    QString text() const override { return m_text; }
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_id;
    QString m_text;
    Size m_sizeBefore;
    Size m_sizeAfter;
    std::optional<Fit> m_fitBefore;
    std::optional<Fit> m_fitAfter;
};

class AddTransitionCommand : public Command {
public:
    AddTransitionCommand(ProjectDocument& document, Transition transition);
    QString text() const override;
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    Transition m_transition;
};

class RemoveTransitionCommand : public Command {
public:
    RemoveTransitionCommand(ProjectDocument& document, QString id);
    QString text() const override;
    void redo() override;
    void undo() override;

private:
    ProjectDocument& m_document;
    QString m_id;
    Transition m_transition;
};

}  // namespace shoal::core
