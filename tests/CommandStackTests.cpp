#include <QSignalSpy>
#include <QTest>

#include "core/CommandStack.h"

using shoal::core::Command;
using shoal::core::CommandStack;

namespace {

/// Appends to a log on redo and removes on undo, so the log shows what is applied.
class Append : public Command {
public:
    Append(QStringList& log, QString item, int mergeId = -1)
        : m_log(log), m_item(std::move(item)), m_mergeId(mergeId)
    {
    }
    QString text() const override { return m_item; }
    void redo() override { m_log << m_item; }
    void undo() override { m_log.removeLast(); }
    int mergeId() const override { return m_mergeId; }
    bool mergeWith(const Command& next) override
    {
        // The merged command keeps its own entry and drops the next one's.
        m_log.removeLast();
        m_item += static_cast<const Append&>(next).m_item;
        m_log.last() = m_item;
        return true;
    }

private:
    QStringList& m_log;
    QString m_item;
    int m_mergeId;
};

}  // namespace

class CommandStackTests : public QObject {
    Q_OBJECT
private slots:
    void undoesAndRedoes()
    {
        QStringList log;
        CommandStack stack;
        stack.push(std::make_unique<Append>(log, QStringLiteral("a")));
        stack.push(std::make_unique<Append>(log, QStringLiteral("b")));
        QCOMPARE(log, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
        QCOMPARE(stack.undoText(), QStringLiteral("b"));

        stack.undo();
        QCOMPARE(log, QStringList{QStringLiteral("a")});
        QCOMPARE(stack.redoText(), QStringLiteral("b"));
        stack.undo();
        QVERIFY(log.isEmpty());
        QVERIFY(!stack.canUndo());
        stack.undo();  // nothing left: no effect
        QCOMPARE(stack.index(), 0);

        stack.redo();
        stack.redo();
        QCOMPARE(log, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
        QVERIFY(!stack.canRedo());
    }

    void pushingDropsWhatCouldBeRedone()
    {
        QStringList log;
        CommandStack stack;
        stack.push(std::make_unique<Append>(log, QStringLiteral("a")));
        stack.push(std::make_unique<Append>(log, QStringLiteral("b")));
        stack.undo();
        stack.push(std::make_unique<Append>(log, QStringLiteral("c")));
        QCOMPARE(stack.count(), 2);
        QVERIFY(!stack.canRedo());
        QCOMPARE(log, (QStringList{QStringLiteral("a"), QStringLiteral("c")}));
    }

    void mergesCommandsThatAllowIt()
    {
        QStringList log;
        CommandStack stack;
        stack.push(std::make_unique<Append>(log, QStringLiteral("x"), 1));
        stack.push(std::make_unique<Append>(log, QStringLiteral("y"), 1));
        stack.push(std::make_unique<Append>(log, QStringLiteral("z"), 2));
        QCOMPARE(stack.count(), 2);
        QCOMPARE(log, (QStringList{QStringLiteral("xy"), QStringLiteral("z")}));
        stack.undo();
        stack.undo();
        QVERIFY(log.isEmpty());
    }

    void neverMergesAcrossTheCleanState()
    {
        QStringList log;
        CommandStack stack;
        stack.push(std::make_unique<Append>(log, QStringLiteral("x"), 1));
        stack.setClean();
        stack.push(std::make_unique<Append>(log, QStringLiteral("y"), 1));
        QCOMPARE(stack.count(), 2);
        QVERIFY(!stack.isClean());
        stack.undo();
        QVERIFY(stack.isClean());
    }

    void tracksTheCleanState()
    {
        QStringList log;
        CommandStack stack;
        QSignalSpy cleanChanged(&stack, &CommandStack::cleanChanged);
        QVERIFY(stack.isClean());

        stack.push(std::make_unique<Append>(log, QStringLiteral("a")));
        QVERIFY(!stack.isClean());
        stack.undo();
        QVERIFY(stack.isClean());
        QCOMPARE(cleanChanged.count(), 2);

        // Once the saved state is undone and replaced, it can't be reached again.
        stack.push(std::make_unique<Append>(log, QStringLiteral("b")));
        stack.setClean();
        stack.undo();
        stack.push(std::make_unique<Append>(log, QStringLiteral("c")));
        stack.undo();
        QVERIFY(!stack.isClean());
        stack.redo();
        QVERIFY(!stack.isClean());
    }

    void groupsMacrosIntoOneStep()
    {
        QStringList log;
        CommandStack stack;
        QSignalSpy indexChanged(&stack, &CommandStack::indexChanged);
        stack.beginMacro(QStringLiteral("group"));
        stack.push(std::make_unique<Append>(log, QStringLiteral("a")));
        stack.beginMacro(QStringLiteral("inner"));
        stack.push(std::make_unique<Append>(log, QStringLiteral("b")));
        stack.endMacro();
        QVERIFY(!stack.canUndo());  // not while a macro is open
        stack.endMacro();

        QCOMPARE(stack.count(), 1);
        QCOMPARE(indexChanged.count(), 1);
        QCOMPARE(stack.undoText(), QStringLiteral("group"));
        stack.undo();
        QVERIFY(log.isEmpty());
        stack.redo();
        QCOMPARE(log, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    }

    void dropsEmptyMacros()
    {
        CommandStack stack;
        stack.beginMacro(QStringLiteral("nothing"));
        stack.endMacro();
        QCOMPARE(stack.count(), 0);
        QVERIFY(stack.isClean());
    }

    void signalsAvailabilityChanges()
    {
        QStringList log;
        CommandStack stack;
        QSignalSpy canUndo(&stack, &CommandStack::canUndoChanged);
        QSignalSpy canRedo(&stack, &CommandStack::canRedoChanged);
        stack.push(std::make_unique<Append>(log, QStringLiteral("a")));
        stack.undo();
        QCOMPARE(canUndo.count(), 2);
        QCOMPARE(canRedo.count(), 1);
        QCOMPARE(canRedo.last().first().toBool(), true);
    }
};

QTEST_GUILESS_MAIN(CommandStackTests)
#include "CommandStackTests.moc"
