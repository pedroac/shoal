#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <vector>

namespace shoal::core {

/// One undoable change to a document. redo() applies it (also the first time) and undo()
/// reverts it, so a command must capture what it needs to revert before it changes anything.
class Command {
public:
    virtual ~Command() = default;

    virtual QString text() const = 0;
    virtual void redo() = 0;
    virtual void undo() = 0;

    /// Commands with the same id other than -1 may merge, such as the steps of one drag.
    virtual int mergeId() const { return -1; }
    /// Absorbs `next`, which has already been applied; returns false to keep both.
    virtual bool mergeWith(const Command& next)
    {
        Q_UNUSED(next);
        return false;
    }
};

/// The undo history of a document, like QUndoStack (which is in QtGui, while core uses
/// QtCore only).
class CommandStack : public QObject {
    Q_OBJECT
public:
    explicit CommandStack(QObject* parent = nullptr);
    ~CommandStack() override;

    /// Applies `command` and records it, dropping whatever could be redone. It merges into
    /// the previous command when both allow it, except across the clean state.
    void push(std::unique_ptr<Command> command);

    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    void undo();
    void redo();

    /// Groups the commands pushed until endMacro() into one step. Macros nest.
    void beginMacro(const QString& text);
    void endMacro();

    /// The number of commands applied; 0 when everything is undone.
    int index() const;
    int count() const;

    /// Marks the current state as saved.
    void setClean();
    bool isClean() const;

    /// Forgets the whole history; the current state becomes clean.
    void clear();

signals:
    void indexChanged(int index);
    void cleanChanged(bool clean);
    void canUndoChanged(bool canUndo);
    void canRedoChanged(bool canRedo);

private:
    class Macro;
    struct State {
        int index;
        bool clean;
        bool canUndo;
        bool canRedo;
    };

    State state() const;
    void emitChanges(const State& before);
    void record(std::unique_ptr<Command> command);

    std::vector<std::unique_ptr<Command>> m_commands;
    std::vector<std::unique_ptr<Macro>> m_openMacros;
    int m_index = 0;
    int m_cleanIndex = 0;  ///< -1 once the saved state can't be reached any more.
};

}  // namespace shoal::core
