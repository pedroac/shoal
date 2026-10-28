#include <QTest>

#include "Support.h"
#include "core/ProjectCodec.h"
#include "core/ProjectTemplates.h"

using namespace shoal::core;

namespace {

/// The smallest valid project; error rows add or replace one file.
ProjectFiles minimalProject()
{
    return {
        {paths::MANIFEST, R"({"format": "shoal-project", "version": 1, "name": "T",
                             "design_resolution": [640, 360]})"},
        {paths::FLOW, R"({"format": "shoal-flow", "version": 1})"},
    };
}

QByteArray sceneWithEntity(const QByteArray& entity)
{
    return R"({"format": "shoal-scene", "version": 1, "size": [640, 360],
               "layers": [{"id": "w"}], "entities": [)" +
           entity + "]}";
}

QByteArray flowWithTransition(const QByteArray& transition)
{
    return R"({"format": "shoal-flow", "version": 1, "transitions": [)" + transition + "]}";
}

QStringList errorStrings(const QList<FileError>& errors)
{
    QStringList strings;
    for (const FileError& error : errors) {
        strings << error.toString();
    }
    return strings;
}

}  // namespace

class ProjectCodecTests : public QObject {
    Q_OBJECT
private slots:
    void fixtureRoundTripsByteForByte()
    {
        const ProjectFiles files = support::readTree(support::fixturePath(QStringLiteral("basic")));
        QCOMPARE(files.size(), size_t{10});
        const ParseResult parsed = parseProject(files);
        QVERIFY2(parsed.errors.isEmpty(), qPrintable(errorStrings(parsed.errors).join(u'\n')));

        const ProjectFiles written = serializeProject(*parsed.project);
        QCOMPARE(support::fileNames(written), support::fileNames(files));
        for (const auto& [path, bytes] : files) {
            QCOMPARE(QString::fromUtf8(written.at(path)), QString::fromUtf8(bytes));
        }
    }

    void fixtureLoadsIntoTheModel()
    {
        const ParseResult parsed =
            parseProject(support::readTree(support::fixturePath(QStringLiteral("basic"))));
        QVERIFY(parsed.project);
        const Project& project = *parsed.project;

        QCOMPARE(project.manifest.start, QStringLiteral("main_menu"));
        QCOMPARE(project.manifest.sheets.value(QStringLiteral("hero")),
                 QStringLiteral("art/hero.sprat"));
        QCOMPARE(project.scenes.keys(),
                 (QStringList{QStringLiteral("level_1"), QStringLiteral("level_2"),
                              QStringLiteral("main_menu"), QStringLiteral("pause")}));
        QCOMPARE(project.presets.keys(), QStringList{QStringLiteral("enemy_grunt")});
        QVERIFY(project.classes);
        QCOMPARE(project.classes->classes.size(), 3);

        const Flow& flow = project.flow;
        QCOMPARE(flow.transitions.size(), size_t{5});
        const Transition& toLevel2 = flow.transitions[2];
        QCOMPARE(toLevel2.id, QStringLiteral("l1_to_l2"));
        QCOMPARE(toLevel2.trigger.requirements, QStringList{QStringLiteral("has_key")});
        QVERIFY(toLevel2.effect);
        QCOMPARE(toLevel2.effect->durationMs, 400);
        QCOMPARE(toLevel2.persist.entities.value(QStringLiteral("player")),
                 QStringLiteral("spawn_west_door"));
        QCOMPARE(flow.transitions[0].mode, TransitionMode::PushModifier);
        QCOMPARE(flow.transitions[4].mode, TransitionMode::Pop);
        QCOMPARE(flow.nodePositions.value(QStringLiteral("pause")), (Point{480, 300.5}));
        QCOMPARE(flow.editorExtras.value(QStringLiteral("zoom_hint")).toInt(), 1);
        QCOMPARE(flow.extras.value(QStringLiteral("x_engine_note")).toString(),
                 QStringLiteral("kept as written"));

        const Scene& level1 = project.scenes.value(QStringLiteral("level_1"));
        QCOMPARE(level1.size, (Size{3840, 1080}));
        QCOMPARE(level1.layers[0].parallax, (Point{0.5, 0.5}));
        QCOMPARE(level1.layers[2].space, Space::Screen);
        QCOMPARE(level1.modifiers.keys(),
                 (QStringList{QStringLiteral("altar_reveal"), QStringLiteral("earthquake")}));
        QCOMPARE(level1.modifiers.value(QStringLiteral("altar_reveal")).onEnd.mode,
                 RestoreMode::Restore);
        QCOMPARE(level1.modifiers.value(QStringLiteral("earthquake")).priority, 5);

        const Entity& player = level1.entities[1];
        QCOMPARE(player.type, EntityType::Sprite);
        QVERIFY(player.transform.flipX);
        QCOMPARE(player.props.value(QStringLiteral("speed")).toDouble(), 4.5);
        QCOMPARE(player.extras.value(QStringLiteral("sprite"))[QStringLiteral("sheet")].toString(),
                 QStringLiteral("hero"));

        const Entity& group = level1.entities[3];
        QCOMPARE(group.children.size(), size_t{2});
        QVERIFY(group.children[1].layer.isEmpty());
        QCOMPARE(group.children[1].transform.pos, (Point{0, 80}));
        QVERIFY(!level1.entities[4].active);
        QVERIFY(!level1.entities[4].visible);

        const Scene& pause = project.scenes.value(QStringLiteral("pause"));
        QCOMPARE(pause.fit, std::optional<Fit>(Fit::Letterbox));
        QCOMPARE(pause.layers[0].fit, std::optional<Fit>(Fit::Stretch));
    }

    void canonicalizesAndThenStaysStable()
    {
        ProjectFiles files = minimalProject();
        files[paths::MANIFEST] =
            "\xEF\xBB\xBF{\r\n\"design_resolution\": [640, 360],\r\n"
            "\"name\": \"T\", \"version\": 1, \"format\": \"shoal-project\"}";
        files[paths::FLOW] = R"({"version": 1, "format": "shoal-flow", "editor": {"nodes": {}},
            "transitions": [
              {"id": "z", "from": "a", "to": "b"},
              {"id": "a", "from": "b", "to": "a", "mode": "replace", "trigger": {}}]})";
        files[paths::scene(QStringLiteral("a"))] = sceneWithEntity(R"(
            {"type": "empty", "layer": "w", "active": true, "visible": true, "classes": [],
             "transform": {"pos": [0, 0], "rot": 0, "scale": [1, 1], "flip": [false, false]},
             "props": {}, "children": []})");

        const ParseResult first = parseProject(files);
        QVERIFY2(first.project, qPrintable(errorStrings(first.errors).join(u'\n')));
        const ProjectFiles canonical = serializeProject(*first.project);

        QCOMPARE(QString::fromUtf8(canonical.at(paths::scene(QStringLiteral("a")))),
                 QStringLiteral("{\n"
                                "  \"format\": \"shoal-scene\",\n"
                                "  \"version\": 1,\n"
                                "  \"size\": [640, 360],\n"
                                "  \"layers\": [\n"
                                "    {\n"
                                "      \"id\": \"w\",\n"
                                "      \"space\": \"world\"\n"
                                "    }\n"
                                "  ],\n"
                                "  \"entities\": [\n"
                                "    {\n"
                                "      \"type\": \"empty\",\n"
                                "      \"layer\": \"w\"\n"
                                "    }\n"
                                "  ]\n"
                                "}\n"));
        QCOMPARE(QString::fromUtf8(canonical.at(paths::FLOW)),
                 QStringLiteral("{\n"
                                "  \"format\": \"shoal-flow\",\n"
                                "  \"version\": 1,\n"
                                "  \"state\": [],\n"
                                "  \"transitions\": [\n"
                                "    {\n"
                                "      \"id\": \"a\",\n"
                                "      \"from\": \"b\",\n"
                                "      \"to\": \"a\",\n"
                                "      \"mode\": \"replace\"\n"
                                "    },\n"
                                "    {\n"
                                "      \"id\": \"z\",\n"
                                "      \"from\": \"a\",\n"
                                "      \"to\": \"b\",\n"
                                "      \"mode\": \"replace\"\n"
                                "    }\n"
                                "  ]\n"
                                "}\n"));

        const ParseResult second = parseProject(canonical);
        QVERIFY(second.project);
        QCOMPARE(*second.project, *first.project);
        QCOMPARE(serializeProject(*second.project), canonical);
    }

    void reportsWhatTheModelCannotRepresent_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QByteArray>("content");
        QTest::addColumn<QString>("expected");

        const QString scene = paths::scene(QStringLiteral("a"));

        QTest::newRow("format")
            << paths::FLOW << QByteArray(R"({"format": "shoal-scene", "version": 1})")
            << R"(flow.json: format: expected "shoal-flow", found "shoal-scene")";
        QTest::newRow("newer version")
            << paths::FLOW << QByteArray(R"({"format": "shoal-flow", "version": 2})")
            << "flow.json: version: 2 is newer than this Shoal reads (1); update Shoal to open "
               "this project";
        QTest::newRow("missing version") << paths::FLOW << QByteArray(R"({"format": "shoal-flow"})")
                                         << "flow.json: version: missing";
        QTest::newRow("version 0")
            << paths::FLOW << QByteArray(R"({"format": "shoal-flow", "version": 0})")
            << "flow.json: version: expected 1, found 0";
        QTest::newRow("top level") << paths::FLOW << QByteArray("[]")
                                   << "flow.json: expected a JSON object at the top level";
        QTest::newRow("fit")
            << paths::MANIFEST
            << QByteArray(R"({"format": "shoal-project", "version": 1, "name": "T",
                             "design_resolution": [640, 360], "fit": "zoom"})")
            << R"(project.shoal.json: fit: expected "expand", "letterbox", "crop" or "stretch", found "zoom")";
        QTest::newRow("design resolution")
            << paths::MANIFEST
            << QByteArray(R"({"format": "shoal-project", "version": 1, "name": "T",
                             "design_resolution": [640, 0]})")
            << "project.shoal.json: design_resolution: expected [width, height], whole numbers "
               "from 1 to 16777216, found an array";
        QTest::newRow("export target")
            << paths::MANIFEST
            << QByteArray(R"({"format": "shoal-project", "version": 1, "name": "T",
                             "design_resolution": [640, 360], "export": {"targets": ["raw", "xml"]}})")
            << R"(project.shoal.json: export.targets[1]: expected "raw" or "json", found "xml")";
        QTest::newRow("scene size")
            << scene << QByteArray(R"({"format": "shoal-scene", "version": 1, "size": "big"})")
            << scene +
                   R"(: size: expected [width, height], whole numbers from 1 to 16777216, found "big")";
        QTest::newRow("entity id")
            << scene << sceneWithEntity(R"({"id": "9lives", "type": "empty", "layer": "w"})")
            << scene +
                   R"(: entities[0].id: "9lives" is not a valid id (use letters, digits, _ and -, starting with a letter or _))";
        QTest::newRow("entity type missing") << scene << sceneWithEntity(R"({"layer": "w"})")
                                             << scene + ": entities[0].type: missing";
        QTest::newRow("entity type unknown")
            << scene << sceneWithEntity(R"({"type": "light", "layer": "w"})")
            << scene +
                   R"(: entities[0].type: expected "empty", "group", "sprite", "animation", "nineslice", "text", "rect", "mark", "path", "region" or "camera", found "light")";
        QTest::newRow("top-level layer missing") << scene << sceneWithEntity(R"({"type": "empty"})")
                                                 << scene + ": entities[0].layer: missing";
        QTest::newRow("child with a layer")
            << scene
            << sceneWithEntity(
                   R"({"type": "group", "layer": "w", "children": [{"type": "mark", "layer": "w"}]})")
            << scene + ": entities[0].children[0].layer: children inherit their parent's layer";
        QTest::newRow("class id")
            << scene
            << sceneWithEntity(R"({"type": "empty", "layer": "w", "classes": ["has space"]})")
            << scene + R"(: entities[0].classes[0]: "has space" is not a valid id)";
        QTest::newRow("flip")
            << scene
            << sceneWithEntity(R"({"type": "empty", "layer": "w", "transform": {"flip": [1, 0]}})")
            << scene +
                   ": entities[0].transform.flip: expected [horizontal, vertical], each true or "
                   "false, found an array";
        QTest::newRow("entity not an object")
            << scene << sceneWithEntity(R"("player")")
            << scene + R"(: entities[0]: expected an object, found "player")";
        QTest::newRow("replace without destination")
            << paths::FLOW << flowWithTransition(R"({"id": "t", "from": "a"})")
            << "flow.json: transitions[0].to: missing (a replace transition needs a destination "
               "scene)";
        QTest::newRow("pop with destination")
            << paths::FLOW
            << flowWithTransition(R"({"id": "t", "from": "a", "to": "b", "mode": "pop"})")
            << "flow.json: transitions[0].to: a pop transition returns to the scene underneath, so "
               "it has no destination";
        QTest::newRow("push_modifier without modifier")
            << paths::FLOW
            << flowWithTransition(R"({"id": "t", "from": "a", "mode": "push_modifier"})")
            << "flow.json: transitions[0].modifier: missing (a push_modifier transition names the "
               "modifier it runs)";
        QTest::newRow("modifier on replace")
            << paths::FLOW
            << flowWithTransition(R"({"id": "t", "from": "a", "to": "b", "modifier": "m"})")
            << "flow.json: transitions[0].modifier: only push_modifier transitions run a modifier";
        QTest::newRow("underlying on replace")
            << paths::FLOW
            << flowWithTransition(R"({"id": "t", "from": "a", "to": "b", "underlying": "paused"})")
            << "flow.json: transitions[0].underlying: only push transitions keep a scene "
               "underneath";
        QTest::newRow("fractional duration")
            << paths::FLOW
            << flowWithTransition(
                   R"({"id": "t", "from": "a", "to": "b", "effect": {"type": "fade", "duration_ms": 1.5}})")
            << "flow.json: transitions[0].effect.duration_ms: expected an integer, found 1.5";
        QTest::newRow("negative duration")
            << paths::FLOW
            << flowWithTransition(
                   R"({"id": "t", "from": "a", "to": "b", "effect": {"type": "fade", "duration_ms": -1}})")
            << "flow.json: transitions[0].effect.duration_ms: expected an integer from 0 to "
               "2147483647, found -1";
        QTest::newRow("state key")
            << paths::FLOW
            << QByteArray(R"({"format": "shoal-flow", "version": 1, "state": ["hp"]})")
            << R"(flow.json: state[0]: expected an object, found "hp")";
        QTest::newRow("class color")
            << paths::CLASSES << QByteArray(R"({"format": "shoal-classes", "version": 1,
                             "classes": {"trigger": {"color": "orange"}}})")
            << R"(classes.json: classes.trigger.color: expected a color such as "#e0a030", found "orange")";
        QTest::newRow("scene folder name")
            << QStringLiteral("scenes/level 1/scene.json") << sceneWithEntity({})
            << R"(scenes/level 1/scene.json: "level 1" is not a valid scene id (use letters, digits, _ and -, starting with a letter or _))";
        QTest::newRow("modifier without its scene")
            << paths::modifier(QStringLiteral("ghost"), QStringLiteral("m"))
            << QByteArray(R"({"format": "shoal-modifier", "version": 1})")
            << "scenes/ghost/modifiers/m.json: its scene folder has no scene.json";
    }

    void reportsWhatTheModelCannotRepresent()
    {
        QFETCH(QString, path);
        QFETCH(QByteArray, content);
        QFETCH(QString, expected);

        ProjectFiles files = minimalProject();
        files[path] = content;
        const ParseResult parsed = parseProject(files);
        QVERIFY(!parsed.project);
        QCOMPARE(errorStrings(parsed.errors), QStringList{expected});
    }

    void locatesSyntaxErrors()
    {
        ProjectFiles files = minimalProject();
        files[paths::FLOW] = "{\n  \"format\": \"shoal-flow\",\n  \"version\": 1,,\n}";
        const ParseResult parsed = parseProject(files);
        QCOMPARE(parsed.errors.size(), 1);
        QCOMPARE(parsed.errors[0].file, paths::FLOW);
        QCOMPARE(parsed.errors[0].line, 3);
        QVERIFY2(parsed.errors[0].column >= 14, qPrintable(parsed.errors[0].toString()));
    }

    void reportsOneErrorPerFile()
    {
        ProjectFiles files = minimalProject();
        files[paths::FLOW] = R"({"format": "shoal-flow", "version": 9})";
        files[paths::scene(QStringLiteral("a"))] = "not json";
        files[paths::scene(QStringLiteral("b"))] = sceneWithEntity({});
        const ParseResult parsed = parseProject(files);
        QVERIFY(!parsed.project);
        QCOMPARE(parsed.errors.size(), 2);
        QCOMPARE(parsed.errors[0].file, paths::FLOW);
        QCOMPARE(parsed.errors[1].file, paths::scene(QStringLiteral("a")));
    }

    void requiresTheManifestAndTheFlow()
    {
        ParseResult parsed = parseProject({});
        QCOMPARE(errorStrings(parsed.errors),
                 QStringList{QStringLiteral("project.shoal.json: not found; this folder is not a "
                                            "Shoal project")});

        ProjectFiles files = minimalProject();
        files.erase(paths::FLOW);
        parsed = parseProject(files);
        QCOMPARE(errorStrings(parsed.errors), QStringList{QStringLiteral("flow.json: not found")});
    }

    void loadsUnresolvedReferences()
    {
        // Dangling references are for validation to report, not a reason to refuse the project.
        ProjectFiles files = minimalProject();
        files[paths::FLOW] = flowWithTransition(R"({"id": "t", "from": "nowhere", "to": "gone"})");
        files[paths::scene(QStringLiteral("a"))] =
            sceneWithEntity(R"({"type": "empty", "layer": "no_such_layer"})");
        QVERIFY(parseProject(files).project);
    }

    void ignoresFilesOutsideTheProject()
    {
        ProjectFiles files = minimalProject();
        files[QStringLiteral("art/hero.sprat")] = "sprat-raw 2";
        files[QStringLiteral("scenes/a/notes.txt")] = "todo";
        const ParseResult parsed = parseProject(files);
        QVERIFY(parsed.project);
        QVERIFY(parsed.project->scenes.isEmpty());
        QVERIFY(!isProjectFile(u"art/hero.sprat"));
        QVERIFY(isProjectFile(u"scenes/a/modifiers/m.json"));
        QVERIFY(!isProjectFile(u"scenes/a/modifiers/deep/m.json"));
    }

    void templatesRoundTrip_data()
    {
        QTest::addColumn<ProjectTemplate>("projectTemplate");
        QTest::newRow("default") << ProjectTemplate::Default;
        QTest::newRow("pixel art") << ProjectTemplate::PixelArt;
    }

    void templatesRoundTrip()
    {
        QFETCH(ProjectTemplate, projectTemplate);
        const Project project = newProject(QStringLiteral("New Game"), projectTemplate);
        const ParseResult parsed = parseProject(serializeProject(project));
        QVERIFY2(parsed.project, qPrintable(errorStrings(parsed.errors).join(u'\n')));
        QCOMPARE(*parsed.project, project);
    }

    void pixelArtTemplateSetsItsDefaults()
    {
        const Project project = newProject(QStringLiteral("Tiny"), ProjectTemplate::PixelArt);
        QCOMPARE(project.manifest.designResolution, (Size{640, 360}));
        QCOMPARE(project.manifest.fit, Fit::Letterbox);
        QVERIFY(project.manifest.integerScale);

        const Scene& main = project.scenes.value(QStringLiteral("main"));
        QCOMPARE(main.size, (Size{640, 360}));
        QCOMPARE(main.entities.size(), size_t{1});
        QCOMPARE(main.entities[0].type, EntityType::Camera);
        QCOMPARE(main.entities[0].classes, QStringList{QStringLiteral("main")});
        QCOMPARE(main.entities[0].transform.pos, (Point{320, 180}));
    }
};

QTEST_GUILESS_MAIN(ProjectCodecTests)
#include "ProjectCodecTests.moc"
