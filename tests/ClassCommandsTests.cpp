#include <QSignalSpy>
#include <QTest>

#include "Support.h"
#include "core/ClassCommands.h"
#include "core/ProjectDocument.h"

using namespace shoal::core;

namespace {

Project fixtureProject()
{
    return *parseProject(support::readTree(support::fixturePath(QStringLiteral("basic")))).project;
}

}  // namespace

class ClassCommandsTests : public QObject {
    Q_OBJECT
private slots:
    void countsTheClassesInUse()
    {
        const ProjectDocument document(fixtureProject());
        const QMap<QString, int> uses = document.classUses();
        QCOMPARE(uses.value(QStringLiteral("main")), 2);  // a camera in each level
        QCOMPARE(uses.value(QStringLiteral("camera_bounds")), 1);
        QCOMPARE(uses.value(QStringLiteral("button")), 1);   // used, not declared
        QVERIFY(!uses.contains(QStringLiteral("trigger")));  // declared, not used
    }

    void declaresEditsAndUndeclares()
    {
        ProjectDocument document(fixtureProject());
        const Project before = document.project();
        QSignalSpy classesChanged(&document, &ProjectDocument::classesChanged);

        const ClassDeclaration button{QStringLiteral("Clickable"), QStringLiteral("#ff8000"), {}};
        document.commands().push(std::make_unique<SetClassDeclarationCommand>(
            document, QStringLiteral("button"), button));
        QCOMPARE(document.commands().undoText(), QStringLiteral("Declare class button"));
        QCOMPARE(document.project().classes->classes[QStringLiteral("button")], button);
        QCOMPARE(classesChanged.count(), 1);

        // Editing keeps what Shoal doesn't model, such as trigger's x_owner.
        ClassDeclaration trigger = document.project().classes->classes[QStringLiteral("trigger")];
        trigger.doc = QStringLiteral("Fires on enter");
        document.commands().push(std::make_unique<SetClassDeclarationCommand>(
            document, QStringLiteral("trigger"), trigger));
        QCOMPARE(document.commands().undoText(), QStringLiteral("Edit class trigger"));
        QVERIFY(document.project().classes->classes[QStringLiteral("trigger")].extras.contains(
            QStringLiteral("x_owner")));

        document.commands().push(std::make_unique<SetClassDeclarationCommand>(
            document, QStringLiteral("enemy"), std::nullopt));
        QCOMPARE(document.commands().undoText(), QStringLiteral("Undeclare class enemy"));
        QVERIFY(!document.project().classes->classes.contains(QStringLiteral("enemy")));

        document.commands().undo();
        document.commands().undo();
        document.commands().undo();
        QCOMPARE(document.project(), before);
    }

    void theFirstDeclarationCreatesClassesJson()
    {
        Project project = fixtureProject();
        project.classes.reset();
        ProjectDocument document(project);
        document.commands().push(std::make_unique<SetClassDeclarationCommand>(
            document, QStringLiteral("actor"), ClassDeclaration{}));
        QVERIFY(document.project().classes);
        QCOMPARE(document.project().classes->classes.keys(), QStringList{QStringLiteral("actor")});
        QVERIFY(serializeProject(document.project()).contains(paths::CLASSES));
        document.commands().undo();
        QVERIFY(!document.project().classes);  // no file again
    }
};

QTEST_GUILESS_MAIN(ClassCommandsTests)
#include "ClassCommandsTests.moc"
