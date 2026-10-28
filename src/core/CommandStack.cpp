#include "core/CommandStack.h"

namespace shoal::core {

class CommandStack::Macro : public Command {
public:
    explicit Macro(QString text) : m_text(std::move(text)) {}

    QString text() const override { return m_text; }

    void redo() override
    {
        for (auto& command : m_children) {
            command->redo();
        }
    }

    void undo() override
    {
        for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
            (*it)->undo();
        }
    }

    void append(std::unique_ptr<Command> command) { m_children.push_back(std::move(command)); }
    bool isEmpty() const { return m_children.empty(); }

private:
    QString m_text;
    std::vector<std::unique_ptr<Command>> m_children;
};

CommandStack::CommandStack(QObject* parent) : QObject(parent) {}

CommandStack::~CommandStack() = default;

CommandStack::State CommandStack::state() const
{
    return {m_index, isClean(), canUndo(), canRedo()};
}

void CommandStack::emitChanges(const State& before)
{
    const State after = state();
    if (after.index != before.index) {
        emit indexChanged(after.index);
    }
    if (after.clean != before.clean) {
        emit cleanChanged(after.clean);
    }
    if (after.canUndo != before.canUndo) {
        emit canUndoChanged(after.canUndo);
    }
    if (after.canRedo != before.canRedo) {
        emit canRedoChanged(after.canRedo);
    }
}

void CommandStack::record(std::unique_ptr<Command> command)
{
    const State before = state();
    m_commands.resize(static_cast<size_t>(m_index));
    if (m_cleanIndex > m_index) {
        m_cleanIndex = -1;
    }
    Command* top = m_index > 0 ? m_commands.back().get() : nullptr;
    const bool mergeable = top && m_index != m_cleanIndex && command->mergeId() != -1 &&
                           command->mergeId() == top->mergeId();
    if (!mergeable || !top->mergeWith(*command)) {
        m_commands.push_back(std::move(command));
        ++m_index;
    }
    emitChanges(before);
}

void CommandStack::push(std::unique_ptr<Command> command)
{
    Q_ASSERT(command);
    command->redo();
    if (!m_openMacros.empty()) {
        m_openMacros.back()->append(std::move(command));
        return;
    }
    record(std::move(command));
}

bool CommandStack::canUndo() const
{
    return m_openMacros.empty() && m_index > 0;
}

bool CommandStack::canRedo() const
{
    return m_openMacros.empty() && m_index < count();
}

QString CommandStack::undoText() const
{
    return canUndo() ? m_commands[static_cast<size_t>(m_index - 1)]->text() : QString();
}

QString CommandStack::redoText() const
{
    return canRedo() ? m_commands[static_cast<size_t>(m_index)]->text() : QString();
}

void CommandStack::undo()
{
    if (!canUndo()) {
        return;
    }
    const State before = state();
    --m_index;
    m_commands[static_cast<size_t>(m_index)]->undo();
    emitChanges(before);
}

void CommandStack::redo()
{
    if (!canRedo()) {
        return;
    }
    const State before = state();
    m_commands[static_cast<size_t>(m_index)]->redo();
    ++m_index;
    emitChanges(before);
}

void CommandStack::beginMacro(const QString& text)
{
    const State before = state();
    m_openMacros.push_back(std::make_unique<Macro>(text));
    emitChanges(before);
}

void CommandStack::endMacro()
{
    Q_ASSERT(!m_openMacros.empty());
    if (m_openMacros.empty()) {
        return;
    }
    const State before = state();
    std::unique_ptr<Macro> macro = std::move(m_openMacros.back());
    m_openMacros.pop_back();
    if (macro->isEmpty()) {
        emitChanges(before);
        return;
    }
    if (!m_openMacros.empty()) {
        m_openMacros.back()->append(std::move(macro));
        return;
    }
    // Its commands are already applied: record it without running it again.
    m_commands.resize(static_cast<size_t>(m_index));
    if (m_cleanIndex > m_index) {
        m_cleanIndex = -1;
    }
    m_commands.push_back(std::move(macro));
    ++m_index;
    emitChanges(before);
}

int CommandStack::index() const
{
    return m_index;
}

int CommandStack::count() const
{
    return static_cast<int>(m_commands.size());
}

void CommandStack::setClean()
{
    const State before = state();
    m_cleanIndex = m_index;
    emitChanges(before);
}

bool CommandStack::isClean() const
{
    return m_openMacros.empty() && m_index == m_cleanIndex;
}

void CommandStack::clear()
{
    const State before = state();
    m_commands.clear();
    m_openMacros.clear();
    m_index = 0;
    m_cleanIndex = 0;
    emitChanges(before);
}

}  // namespace shoal::core
