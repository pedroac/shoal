#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "Support.h"
#include "core/FlowCommands.h"
#include "core/ProjectDocument.h"
#include "core/ProjectTemplates.h"
#include "core/SheetCommands.h"

using namespace shoal::core;

namespace {

Project fixtureProject()
{
    return *parseProject(support::readTree(support::fixturePath(QStringLiteral("basic")))).project;
}

}  // namespace

class ProjectDocumentTests : public QObject {
    Q_OBJECT
private slots:
    void addsAScene()
    {
        ProjectDocument document(fixtureProject());
        const Project before = document.project();
        QSignalSpy added(&document, &ProjectDocument::sceneAdded);

        const QString id = document.uniqueSceneId(QStringLiteral("level"));
        QCOMPARE(id, QStringLiteral("level"));
        document.commands().push(std::make_unique<AddSceneCommand>(
            document, id, newScene(document.project().manifest), Point{100, 200}));
        QCOMPARE(added.count(), 1);
        QVERIFY(document.project().scenes.contains(id));
        QCOMPARE(document.project().flow.nodePositions.value(id), (Point{100, 200}));

        document.commands().undo();
        QCOMPARE(document.project(), before);
        document.commands().redo();
        QVERIFY(document.project().scenes.contains(id));
    }

    void removingASceneTakesItsTransitionsAndUndoesExactly()
    {
        ProjectDocument document(fixtureProject());
        const Project before = document.project();
        const QString level1 = QStringLiteral("level_1");
        QCOMPARE(document.transitionsTouching(level1).size(), 4);

        document.commands().push(std::make_unique<RemoveSceneCommand>(document, level1));
        const Project& after = document.project();
        QVERIFY(!after.scenes.contains(level1));
        QVERIFY(!after.flow.nodePositions.contains(level1));
        QCOMPARE(after.flow.transitions.size(), size_t{1});
        QCOMPARE(after.flow.transitions[0].id, QStringLiteral("pause_close"));

        document.commands().undo();
        QCOMPARE(document.project(), before);
    }

    void removingTheStartSceneLeavesNoStart()
    {
        ProjectDocument document(fixtureProject());
        document.commands().push(
            std::make_unique<RemoveSceneCommand>(document, QStringLiteral("main_menu")));
        QVERIFY(document.project().manifest.start.isEmpty());
        document.commands().undo();
        QCOMPARE(document.project().manifest.start, QStringLiteral("main_menu"));
    }

    void renamingASceneUpdatesItsReferences()
    {
        ProjectDocument document(fixtureProject());
        const Project before = document.project();
        QSignalSpy renamed(&document, &ProjectDocument::sceneRenamed);

        document.commands().push(std::make_unique<RenameSceneCommand>(
            document, QStringLiteral("main_menu"), QStringLiteral("title")));
        const Project& after = document.project();
        QCOMPARE(renamed.count(), 1);
        QVERIFY(after.scenes.contains(QStringLiteral("title")));
        QVERIFY(!after.scenes.contains(QStringLiteral("main_menu")));
        QCOMPARE(after.manifest.start, QStringLiteral("title"));
        QCOMPARE(document.findTransition(u"menu_start")->from, QStringLiteral("title"));
        QCOMPARE(after.flow.nodePositions.value(QStringLiteral("title")), (Point{120, 80}));

        document.commands().undo();
        QCOMPARE(document.project(), before);
    }

    void sceneIdsCollideIgnoringCase()
    {
        ProjectDocument document(fixtureProject());
        QVERIFY(!document.isSceneIdAvailable(u"Level_1"));
        QVERIFY(document.isSceneIdAvailable(u"Level_1", u"level_1"));  // renaming its case
        QVERIFY(!document.isSceneIdAvailable(u"level 3"));
        QCOMPARE(document.uniqueSceneId(QStringLiteral("level_1")), QStringLiteral("level_1_2"));
    }

    void movesSeveralNodesInOneStep()
    {
        ProjectDocument document(fixtureProject());
        const Project before = document.project();
        const QString pause = QStringLiteral("pause");
        const QString level1 = QStringLiteral("level_1");
        const QString fresh = QStringLiteral("unplaced");
        document.commands().push(std::make_unique<MoveSceneNodesCommand>(
            document,
            QMap<QString, Point>{
                {pause, Point{500, 320}}, {level1, Point{10, 20}}, {fresh, Point{0, 0}}}));
        QCOMPARE(document.commands().count(), 1);
        QCOMPARE(document.commands().undoText(), QStringLiteral("Move 3 scenes"));
        QCOMPARE(document.project().flow.nodePositions.value(pause), (Point{500, 320}));
        QCOMPARE(document.project().flow.nodePositions.value(level1), (Point{10, 20}));

        document.commands().undo();
        QCOMPARE(document.project(), before);  // also forgets the node that had no position
    }

    void addsAndRemovesTransitionsInIdOrder()
    {
        ProjectDocument document(fixtureProject());
        Transition transition;
        transition.id = document.uniqueTransitionId(QStringLiteral("l1_pause"));
        QCOMPARE(transition.id, QStringLiteral("l1_pause_2"));
        transition.from = QStringLiteral("level_1");
        transition.to = QStringLiteral("pause");
        transition.mode = TransitionMode::Push;
        document.commands().push(std::make_unique<AddTransitionCommand>(document, transition));

        QStringList ids;
        for (const Transition& t : document.project().flow.transitions) {
            ids << t.id;
        }
        QCOMPARE(ids, (QStringList{QStringLiteral("l1_altar"), QStringLiteral("l1_pause"),
                                   QStringLiteral("l1_pause_2"), QStringLiteral("l1_to_l2"),
                                   QStringLiteral("menu_start"), QStringLiteral("pause_close")}));

        const Project before = document.project();
        document.commands().push(
            std::make_unique<RemoveTransitionCommand>(document, QStringLiteral("l1_to_l2")));
        QVERIFY(!document.findTransition(u"l1_to_l2"));
        document.commands().undo();
        QCOMPARE(document.project(), before);
    }

    void setsTheStartScene()
    {
        ProjectDocument document(fixtureProject());
        QSignalSpy changed(&document, &ProjectDocument::startSceneChanged);
        document.commands().push(
            std::make_unique<SetStartSceneCommand>(document, QStringLiteral("level_1")));
        QCOMPARE(document.project().manifest.start, QStringLiteral("level_1"));
        document.commands().undo();
        QCOMPARE(document.project().manifest.start, QStringLiteral("main_menu"));
        QCOMPARE(changed.count(), 2);
    }

    void savingMarksTheHistoryClean()
    {
        QTemporaryDir dir;
        ProjectDocument document(newProject(QStringLiteral("Game"), ProjectTemplate::Default));
        QVERIFY(document.directory().isEmpty());
        QVERIFY(document.saveAs(dir.path()).ok());
        QCOMPARE(document.directory(), dir.path());

        document.commands().push(std::make_unique<RenameSceneCommand>(
            document, QStringLiteral("main"), QStringLiteral("intro")));
        QVERIFY(!document.commands().isClean());
        const SaveResult saved = document.save();
        QVERIFY(saved.ok());
        QVERIFY(document.commands().isClean());
        // The renamed scene's old folder is gone, and the project reopens as saved.
        QCOMPARE(saved.removed, QStringList{paths::scene(QStringLiteral("main"))});

        ProjectDocument::OpenResult reopened = ProjectDocument::open(dir.path());
        QVERIFY(reopened.document);
        QCOMPARE(reopened.document->project(), document.project());
        QCOMPARE(reopened.document->project().manifest.start, QStringLiteral("intro"));
    }

    void saveAsReplacesAProjectAlreadyThere()
    {
        QTemporaryDir dir;
        const QString notes = QStringLiteral("notes.txt");
        ProjectFiles existing = support::readTree(support::fixturePath(QStringLiteral("basic")));
        existing[notes] = "not a project file";
        QVERIFY(support::writeTree(dir.path(), existing));

        ProjectDocument document(newProject(QStringLiteral("Game"), ProjectTemplate::Default));
        const SaveResult saved = document.saveAs(dir.path());
        QVERIFY(saved.ok());
        QVERIFY(saved.removed.contains(paths::scene(QStringLiteral("level_1"))));

        // None of the old scenes come back, and the unrelated file is still there.
        ProjectDocument::OpenResult reopened = ProjectDocument::open(dir.path());
        QVERIFY(reopened.document);
        QCOMPARE(reopened.document->project(), document.project());
        QVERIFY(QFile::exists(QDir(dir.path()).filePath(notes)));
    }

    void makesUniqueSheetAliases()
    {
        ProjectDocument document(fixtureProject());  // declares hero and ui
        QCOMPARE(document.uniqueSheetAlias(QStringLiteral("sheet")), QStringLiteral("sheet"));
        QCOMPARE(document.uniqueSheetAlias(QStringLiteral("hero")), QStringLiteral("hero_2"));
        QCOMPARE(document.uniqueSheetAlias(QStringLiteral("my sheet.v2")),
                 QStringLiteral("my_sheet_v2"));
        QCOMPARE(document.uniqueSheetAlias(QStringLiteral("2d")), QStringLiteral("_2d"));
        QCOMPARE(document.uniqueSheetAlias(QStringLiteral("héros")), QStringLiteral("h_ros"));
        QCOMPARE(document.uniqueSheetAlias({}), QStringLiteral("_"));
    }

    void addsChangesAndRemovesSheets()
    {
        ProjectDocument document(fixtureProject());
        const QMap<QString, QString> before = document.project().manifest.sheets;
        QSignalSpy changed(&document, &ProjectDocument::sheetsChanged);
        CommandStack& commands = document.commands();
        const QString items = QStringLiteral("items");

        commands.push(
            std::make_unique<SetSheetCommand>(document, items, QStringLiteral("art/items.sprat")));
        QCOMPARE(commands.undoText(), QStringLiteral("Add sheet items"));
        QCOMPARE(document.project().manifest.sheets.value(items),
                 QStringLiteral("art/items.sprat"));
        commands.push(
            std::make_unique<SetSheetCommand>(document, items, QStringLiteral("art/items2.sprat")));
        QCOMPARE(commands.undoText(), QStringLiteral("Change sheet items"));
        commands.push(std::make_unique<SetSheetCommand>(document, items, std::nullopt));
        QCOMPARE(commands.undoText(), QStringLiteral("Remove sheet items"));
        QVERIFY(!document.project().manifest.sheets.contains(items));
        QCOMPARE(changed.count(), 3);

        commands.undo();
        QCOMPARE(document.project().manifest.sheets.value(items),
                 QStringLiteral("art/items2.sprat"));
        commands.undo();
        commands.undo();
        QCOMPARE(document.project().manifest.sheets, before);
    }

    void openingReportsErrors()
    {
        QTemporaryDir dir;
        QVERIFY(support::writeTree(dir.path(), {{paths::MANIFEST, "{"}}));
        ProjectDocument::OpenResult opened = ProjectDocument::open(dir.path());
        QVERIFY(!opened.document);
        QCOMPARE(opened.errors.size(), 2);  // the broken manifest, and no flow.json
    }
};

QTEST_GUILESS_MAIN(ProjectDocumentTests)
#include "ProjectDocumentTests.moc"
