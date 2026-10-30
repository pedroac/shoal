#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

#include "Support.h"
#include "core/PresetCommands.h"
#include "core/Presets.h"
#include "core/ProjectDocument.h"
#include "core/SceneCommands.h"

using namespace shoal::core;

namespace {

const QString GRUNT = QStringLiteral("enemy_grunt");

Project fixtureProject()
{
    return *parseProject(support::readTree(support::fixturePath(QStringLiteral("basic")))).project;
}

}  // namespace

class PresetCommandsTests : public QObject {
    Q_OBJECT
private slots:
    void presetsAreEntityTrees()
    {
        ProjectDocument document(fixtureProject());
        // A tree id is a preset's, never a scene's.
        QCOMPARE(presetOfTree(presetTreeId(GRUNT)), std::optional(GRUNT));
        QVERIFY(!presetOfTree(QStringLiteral("level_1")));

        const Scene* grunt = document.entityTree(presetTreeId(GRUNT));
        QVERIFY(grunt);
        QCOMPARE(grunt->entities.size(), size_t(1));
        QCOMPARE(grunt->size, Size{});  // placed at a point
        QVERIFY(document.isTreeEditable(presetTreeId(GRUNT)));

        // A built-in one is shown, read-only.
        const QString camera = presetTreeId(QStringLiteral("camera"));
        QVERIFY(document.entityTree(camera));
        QVERIFY(!document.isTreeEditable(camera));
        QVERIFY(!document.entityTree(presetTreeId(QStringLiteral("missing"))));
        QVERIFY(document.entityTree(QStringLiteral("level_1")));
        QVERIFY(document.isTreeEditable(QStringLiteral("level_1")));
    }

    void editingAPresetsEntitiesWritesItsFile()
    {
        ProjectDocument document(fixtureProject());
        QSignalSpy changed(&document, &ProjectDocument::presetChanged);
        const QString tree = presetTreeId(GRUNT);
        Entity entity = document.entityTree(tree)->entities[0];
        entity.transform.pos = Point{4, 8};
        document.commands().push(std::make_unique<EditEntitiesCommand>(
            document, tree, QList<std::pair<EntityPath, Entity>>{{{0}, entity}},
            QStringLiteral("Move")));
        QCOMPARE(changed.count(), 1);
        const QJsonObject written = document.project()
                                        .presets[GRUNT]
                                        .content.value(QStringLiteral("entities"))
                                        .toArray()[0]
                                        .toObject();
        QCOMPARE(written.value(QStringLiteral("transform")).toObject().value(QStringLiteral("pos")),
                 QJsonValue(QJsonArray{4, 8}));
        // What it creates follows.
        const PresetRead read = readPresetDefinition(GRUNT, document.project().presets[GRUNT]);
        QCOMPARE(read.definition->entities[0].transform.pos, (Point{4, 8}));

        document.commands().undo();
        QCOMPARE(document.entityTree(tree)->entities[0].transform.pos, Point{});
        QVERIFY(!document.project()
                     .presets[GRUNT]
                     .content.value(QStringLiteral("entities"))
                     .toArray()[0]
                     .toObject()
                     .contains(QStringLiteral("transform")));
    }

    void customizesAndRevertsABuiltInPreset()
    {
        ProjectDocument document(fixtureProject());
        const QString camera = QStringLiteral("camera");
        QVERIFY(isBuiltInPreset(document.project(), camera));
        document.commands().push(std::make_unique<SetPresetCommand>(
            document, camera, builtInPresets().value(camera), QStringLiteral("Customize")));
        QVERIFY(!isBuiltInPreset(document.project(), camera));
        QVERIFY(document.isTreeEditable(presetTreeId(camera)));
        document.commands().undo();
        QVERIFY(isBuiltInPreset(document.project(), camera));
        QVERIFY(!document.isTreeEditable(presetTreeId(camera)));
    }

    void renamesAPreset()
    {
        ProjectDocument document(fixtureProject());
        QSignalSpy renamed(&document, &ProjectDocument::presetRenamed);
        const QString grunt2 = QStringLiteral("grunt");
        document.commands().push(std::make_unique<RenamePresetCommand>(document, GRUNT, grunt2));
        QCOMPARE(document.commands().undoText(),
                 QStringLiteral("Rename preset enemy_grunt to grunt"));
        QCOMPARE(renamed.count(), 1);
        QVERIFY(document.project().presets.contains(grunt2));
        QVERIFY(!document.project().presets.contains(GRUNT));
        QVERIFY(document.entityTree(presetTreeId(grunt2)));
        QVERIFY(!document.entityTree(presetTreeId(GRUNT)));
        document.commands().undo();
        QVERIFY(document.project().presets.contains(GRUNT));
    }

    void picksPresetIds()
    {
        const Project project = fixtureProject();
        QVERIFY(!isPresetIdAvailable(project, GRUNT));
        QVERIFY(!isPresetIdAvailable(project, QStringLiteral("camera")));  // built in
        QVERIFY(!isPresetIdAvailable(project, QStringLiteral("not an id")));
        QVERIFY(isPresetIdAvailable(project, GRUNT, GRUNT));  // itself, renamed
        QCOMPARE(uniquePresetId(project, GRUNT), QStringLiteral("enemy_grunt_2"));
        QCOMPARE(uniquePresetId(project, QStringLiteral("boss")), QStringLiteral("boss"));

        Entity mark;
        mark.id = QStringLiteral("spot");
        const Preset preset = presetOf(QStringLiteral("Spot"), {mark});
        const PresetRead read = readPresetDefinition(QStringLiteral("spot"), preset);
        QCOMPARE(read.definition->name, QStringLiteral("Spot"));
        QCOMPARE(read.definition->entities[0].id, QStringLiteral("spot"));
    }
};

QTEST_GUILESS_MAIN(PresetCommandsTests)
#include "PresetCommandsTests.moc"
