#include <QJsonArray>
#include <QTest>

#include "Support.h"
#include "core/Presets.h"
#include "core/ProjectTemplates.h"
#include "core/SceneTree.h"

using namespace shoal::core;

namespace {

Project fixtureProject()
{
    return *parseProject(support::readTree(support::fixturePath(QStringLiteral("basic")))).project;
}

QJsonObject spriteEntity()
{
    return {{QStringLiteral("type"), QStringLiteral("sprite")}};
}

}  // namespace

class PresetsTests : public QObject {
    Q_OBJECT
private slots:
    void readsAProjectPreset()
    {
        const Project project = fixtureProject();
        const QString id = QStringLiteral("enemy_grunt");
        const PresetRead read = readPresetDefinition(id, project.presets[id]);
        QVERIFY2(read.definition, qPrintable(read.error));
        QCOMPARE(read.definition->name, id);  // the file has no name
        QCOMPARE(read.definition->tint, QStringLiteral("#c04040"));
        QCOMPARE(read.definition->classes, QStringList{QStringLiteral("enemy")});
        QCOMPARE(read.definition->entities.size(), size_t(1));
        QCOMPARE(read.definition->entities[0].type, EntityType::Sprite);
    }

    void reportsWhatItCantRead_data()
    {
        QTest::addColumn<QJsonObject>("content");
        QTest::addColumn<QString>("error");

        QTest::newRow("no entities")
            << QJsonObject{} << QStringLiteral("entities: a preset creates at least one entity");
        QTest::newRow("a layer")
            << QJsonObject{{QStringLiteral("entities"),
                            QJsonArray{
                                QJsonObject{{QStringLiteral("type"), QStringLiteral("mark")},
                                            {QStringLiteral("layer"), QStringLiteral("world")}}}}}
            << QStringLiteral("entities[0].layer: it goes on the layer it is placed on");
        QTest::newRow("bad type")
            << QJsonObject{{QStringLiteral("entities"),
                            QJsonArray{
                                QJsonObject{{QStringLiteral("type"), QStringLiteral("blob")}}}}}
            << QStringLiteral(
                   "entities[0].type: expected \"empty\", \"group\", \"sprite\", "
                   "\"animation\", \"nineslice\", \"text\", \"rect\", \"mark\", "
                   "\"path\", \"region\" or \"camera\", found \"blob\"");
        QTest::newRow("bad tint")
            << QJsonObject{{QStringLiteral("tint"), QStringLiteral("red")},
                           {QStringLiteral("entities"), QJsonArray{spriteEntity()}}}
            << QStringLiteral("tint: expected a color such as \"#e0a030\", found \"red\"");
        QTest::newRow("bad class")
            << QJsonObject{{QStringLiteral("classes"), QJsonArray{QStringLiteral("two words")}},
                           {QStringLiteral("entities"), QJsonArray{spriteEntity()}}}
            << QStringLiteral("classes[0]: \"two words\" is not a valid id");
    }

    void reportsWhatItCantRead()
    {
        QFETCH(QJsonObject, content);
        QFETCH(QString, error);
        const PresetRead read = readPresetDefinition(QStringLiteral("broken"), Preset{content});
        QVERIFY(!read.definition);
        QCOMPARE(read.error, error);
    }

    void theBuiltInPresetsReadAndTheirClassesAreDeclared()
    {
        const Project project = newProject(QStringLiteral("Game"), ProjectTemplate::Default);
        const QMap<QString, Preset>& presets = builtInPresets();
        QCOMPARE(presets.keys(),
                 (QStringList{QStringLiteral("camera"), QStringLiteral("camera_bounds"),
                              QStringLiteral("spawn_mark"), QStringLiteral("trigger_region")}));
        for (auto it = presets.cbegin(); it != presets.cend(); ++it) {
            const PresetRead read = readPresetDefinition(it.key(), *it);
            QVERIFY2(read.definition, qPrintable(it.key() + u": " + read.error));
            for (const Entity& entity : read.definition->entities) {
                for (const QString& name : entity.classes) {
                    QVERIFY2(project.classes->classes.contains(name), qPrintable(name));
                }
            }
        }
        QCOMPARE(readPresetDefinition(QStringLiteral("spawn_mark"),
                                      presets[QStringLiteral("spawn_mark")])
                     .definition->name,
                 QStringLiteral("Spawn Mark"));
    }

    void aProjectPresetWinsOverABuiltInOne()
    {
        Project project = fixtureProject();
        const QMap<QString, Preset> before = availablePresets(project);
        QVERIFY(before.contains(QStringLiteral("camera")));
        QVERIFY(before.contains(QStringLiteral("enemy_grunt")));
        const Preset own{QJsonObject{{QStringLiteral("entities"), QJsonArray{spriteEntity()}}}};
        project.presets.insert(QStringLiteral("camera"), own);
        QCOMPARE(availablePresets(project)[QStringLiteral("camera")], own);
    }

    void createsCopiesWithFreshIds()
    {
        const Project project = fixtureProject();
        const Scene& level1 = project.scenes[QStringLiteral("level_1")];
        QVERIFY(!findEntity(level1, u"camera").isEmpty());  // taken

        PresetDefinition preset;
        preset.classes = {QStringLiteral("enemy")};
        Entity parent;
        parent.id = QStringLiteral("camera");
        parent.type = EntityType::Group;
        parent.classes = {QStringLiteral("boss"), QStringLiteral("enemy")};
        parent.transform.pos = {10, -5};
        Entity child;
        child.id = QStringLiteral("camera");  // the same base: unique among the new ones too
        child.type = EntityType::Mark;
        child.transform.pos = {3, 4};
        parent.children.push_back(child);
        preset.entities.push_back(parent);
        Entity plain;
        plain.type = EntityType::Sprite;
        preset.entities.push_back(plain);

        const std::vector<Entity> created =
            instantiatePreset(preset, level1, QStringLiteral("bg"), Point{100, 200});
        QCOMPARE(created.size(), size_t(2));
        QCOMPARE(created[0].id, QStringLiteral("camera_2"));
        QCOMPARE(created[0].children[0].id, QStringLiteral("camera_3"));
        QCOMPARE(created[1].id, QString());  // no id stays without one
        QCOMPARE(created[0].layer, QStringLiteral("bg"));
        QCOMPARE(created[0].children[0].layer, QString());  // children inherit it
        QCOMPARE(created[0].transform.pos, (Point{110, 195}));
        QCOMPARE(created[0].children[0].transform.pos, (Point{3, 4}));  // relative to its parent
        QCOMPARE(created[0].classes,
                 (QStringList{QStringLiteral("enemy"), QStringLiteral("boss")}));
        QCOMPARE(created[1].classes, QStringList{QStringLiteral("enemy")});
    }
};

QTEST_GUILESS_MAIN(PresetsTests)
#include "PresetsTests.moc"
