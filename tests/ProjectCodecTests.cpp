#include <QTest>

#include "Support.h"
#include "core/JsonReader.h"
#include "core/ProjectCodec.h"
#include "core/ProjectTemplates.h"
#include "core/SceneTree.h"
#include "core/TransitionTypes.h"

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

QByteArray sceneWithEntity(const QByteArray& entity, int version = SCENE_FORMAT_VERSION)
{
    return R"({"format": "shoal-scene", "version": )" + QByteArray::number(version) +
           R"(, "size": [640, 360], "entities": [)" + entity + "]}";
}

/// A scene in format 1, with layers: `w` in world space, and `ui` in screen space.
QByteArray format1SceneWithEntity(const QByteArray& entity)
{
    return R"({"format": "shoal-scene", "version": 1, "size": [640, 360],
               "layers": [{"id": "w"}, {"id": "ui", "space": "screen"}], "entities": [)" +
           entity + "]}";
}

/// The scene a file holds, read.
Scene readScene(const QByteArray& scene)
{
    ProjectFiles files = minimalProject();
    files[paths::scene(QStringLiteral("a"))] = scene;
    const ParseResult parsed = parseProject(files);
    if (!parsed.project) {
        qWarning() << parsed.errors.value(0).toString();
        return {};
    }
    return parsed.project->scenes.value(QStringLiteral("a"));
}

/// The one entity of a format 1 scene from format1SceneWithEntity(), once migrated: in its
/// layer's entity.
Entity migrated(const QByteArray& entity)
{
    for (const Entity& layer : readScene(format1SceneWithEntity(entity)).entities) {
        if (!layer.children.empty()) {
            return layer.children.front();
        }
    }
    return {};
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
        QCOMPARE(files.size(), size_t{11});
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
        QCOMPARE(project.manifest.assets.sheets.value(QStringLiteral("hero")),
                 QStringLiteral("art/hero.sprat"));
        QCOMPARE(project.scenes.keys(),
                 (QStringList{QStringLiteral("level_1"), QStringLiteral("level_2"),
                              QStringLiteral("main_menu"), QStringLiteral("options"),
                              QStringLiteral("pause")}));
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
        QCOMPARE(level1.entities.size(), size_t{3});
        const Entity& background = level1.entities[0];
        QCOMPARE(background.id, QStringLiteral("bg"));
        QCOMPARE(background.parallax, std::optional(Point{0.5, 0.5}));
        QCOMPARE(level1.entities[2].space, std::optional(Space::Screen));
        QCOMPARE(level1.modifiers.keys(),
                 (QStringList{QStringLiteral("altar_reveal"), QStringLiteral("earthquake")}));
        QCOMPARE(level1.modifiers.value(QStringLiteral("altar_reveal")).onEnd.mode,
                 RestoreMode::Restore);
        QCOMPARE(level1.modifiers.value(QStringLiteral("earthquake")).priority, 5);

        const std::vector<Entity>& world = level1.entities[1].children;
        const Entity& camera = world[0];
        QVERIFY(camera.extras.contains(QStringLiteral("view")));
        QCOMPARE(camera.size, std::optional(Extent::px({1920, 1080})));
        QCOMPARE(camera.pivot, std::optional(Point{0.5, 0.5}));
        const Entity& player = world[1];
        QVERIFY(player.transform.flipX);
        QCOMPARE(player.props.value(QStringLiteral("speed")).toDouble(), 4.5);
        QCOMPARE(player.image->sheet, QStringLiteral("hero"));
        QCOMPARE(player.image->sprite, QStringLiteral("hero/idle.png"));
        QVERIFY(!player.image->isAnimation());
        const Entity& torch = world.back();
        QCOMPARE(torch.image->animation, QStringLiteral("idle"));
        QCOMPARE(torch.image->extras.value(QStringLiteral("x_speed")).toInt(), 2);

        const Entity& group = world[2];
        QCOMPARE(group.children.size(), size_t{2});
        QCOMPARE(group.children[1].transform.pos, (Point{0, 80}));
        QVERIFY(!world[3].active);
        QVERIFY(!world[3].visible);
        QCOMPARE(world[4].size, std::optional(Extent::px({1920, 1080})));

        const Scene& pause = project.scenes.value(QStringLiteral("pause"));
        QCOMPARE(pause.fit, std::optional<Fit>(Fit::Letterbox));
        QCOMPARE(pause.entities[0].fit, std::optional<Fit>(Fit::Stretch));

        const Scene& options = project.scenes.value(QStringLiteral("options"));
        const Entity& panel = options.entities[0].children[0];
        QCOMPARE(panel.image->mode, ImageMode::NineSlice);
        QCOMPARE(panel.tint, (Color{255, 255, 255, 192}));
        QVERIFY(panel.clip);
        const Entity& close = panel.children[0];
        QCOMPARE(close.at, (Anchor{{100, Unit::Percent}, {0, Unit::Percent}}));
        QCOMPARE(close.pivot, std::optional(Point{1, 0}));
        QCOMPARE(close.fill, std::optional(Color{192, 64, 64, 255}));
        QCOMPARE(close.tintMode, TintMode::Override);
        QVERIFY(panel.children[1].extras.contains(QStringLiteral("shape")));
        const Entity& overlay = options.entities[1];
        QCOMPARE(overlay.area, Area::Safe);
        QCOMPARE(overlay.tintMode, TintMode::Inherit);
        const Entity& inventory = options.entities[2];
        QVERIFY(inventory.container);
        QCOMPARE(inventory.container->type, ContainerType::Tiled);
        QCOMPARE(inventory.container->tileSize, (SizeF{64, 64}));
        QCOMPARE(inventory.container->tilePivot, (Point{0.5, 1}));
        QCOMPARE(inventory.children[0].at, (Anchor{{1, Unit::Cell}, {0, Unit::Cell}}));
        QCOMPARE(inventory.children[1].at, (Anchor{{3, Unit::Cell}, {1, Unit::Cell}}));
        QCOMPARE(overlay.at, Anchor{});
    }

    void readsAndWritesAssets()
    {
        // Each kind under `assets`, in the manifest's order of kinds, only those it has.
        ProjectFiles files = minimalProject();
        files[paths::MANIFEST] = R"({"format": "shoal-project", "version": 2, "name": "T",
            "design_resolution": [640, 360],
            "assets": {"video": {"intro": "v/intro.webm"}, "images": {"title": "art/t.png"},
                       "fonts": {"body": "f/body.ttf"}, "audio": {"door": "a/door.ogg"},
                       "sheets": {"hero": "art/hero.sprat"}, "packs": {"keep": true}}})";
        files[paths::scene(QStringLiteral("a"))] = sceneWithEntity(R"({"image": {"asset": "title"}},
                               {"image": {"asset": "title", "mode": "nineslice"}})");
        const ParseResult parsed = parseProject(files);
        QVERIFY2(parsed.project, qPrintable(errorStrings(parsed.errors).join(u'\n')));
        const Assets& assets = parsed.project->manifest.assets;
        QCOMPARE(assets.sheets.value(QStringLiteral("hero")), QStringLiteral("art/hero.sprat"));
        QCOMPARE(assets.images.value(QStringLiteral("title")), QStringLiteral("art/t.png"));
        QCOMPARE(assets.fonts.value(QStringLiteral("body")), QStringLiteral("f/body.ttf"));
        QCOMPARE(assets.audio.value(QStringLiteral("door")), QStringLiteral("a/door.ogg"));
        QCOMPARE(assets.video.value(QStringLiteral("intro")), QStringLiteral("v/intro.webm"));
        QCOMPARE(assets.kindOf(QStringLiteral("door")), AssetKind::Audio);
        QVERIFY(!assets.kindOf(QStringLiteral("nope")));
        QVERIFY(assets.extras.contains(QStringLiteral("packs")));  // kept as written

        const Scene& scene = parsed.project->scenes.value(QStringLiteral("a"));
        const Image& image = *scene.entities[0].image;
        QVERIFY(image.isStandalone());
        QCOMPARE(image.source(), QStringLiteral("title"));
        QCOMPARE(kindOf(scene.entities[0]), QStringLiteral("image"));
        QCOMPARE(scene.entities[1].image->mode, ImageMode::NineSlice);

        const ProjectFiles written = serializeProject(*parsed.project);
        QCOMPARE(QString::fromUtf8(written.at(paths::MANIFEST)), QStringLiteral(R"({
  "format": "shoal-project",
  "version": 2,
  "name": "T",
  "design_resolution": [640, 360],
  "fit": "expand",
  "integer_scale": false,
  "assets": {
    "sheets": {
      "hero": "art/hero.sprat"
    },
    "images": {
      "title": "art/t.png"
    },
    "fonts": {
      "body": "f/body.ttf"
    },
    "audio": {
      "door": "a/door.ogg"
    },
    "video": {
      "intro": "v/intro.webm"
    },
    "packs": {
      "keep": true
    }
  },
  "export": {
    "dir": "export",
    "targets": ["raw", "json"],
    "bundle": false
  }
}
)"));
        QVERIFY(written.at(paths::scene(QStringLiteral("a"))).contains(R"("image": {
        "asset": "title"
      })"));
        const ParseResult again = parseProject(written);
        QVERIFY(again.project);
        QCOMPARE(*again.project, *parsed.project);
    }

    void migratesManifestFormat1Sheets()
    {
        // Format 1 declared sheets at the top level; an `assets` key there is someone else's.
        ProjectFiles files = minimalProject();
        files[paths::MANIFEST] = R"({"format": "shoal-project", "version": 1, "name": "T",
            "design_resolution": [640, 360], "sheets": {"hero": "art/hero.sprat"},
            "assets": "theirs"})";
        const ParseResult parsed = parseProject(files);
        QVERIFY2(parsed.project, qPrintable(errorStrings(parsed.errors).join(u'\n')));
        const Manifest& manifest = parsed.project->manifest;
        QCOMPARE(manifest.assets.sheets.value(QStringLiteral("hero")),
                 QStringLiteral("art/hero.sprat"));
        QCOMPARE(manifest.extras.value(QStringLiteral("assets")).toString(),
                 QStringLiteral("theirs"));
    }

    void knowsTheKindOfAFile()
    {
        QCOMPARE(assetKindForFile(u"art/hero.sprat"), AssetKind::Sheet);
        QCOMPARE(assetKindForFile(u"C:\\art\\Title.PNG"), AssetKind::Image);
        QCOMPARE(assetKindForFile(u"a.jpeg"), AssetKind::Image);
        QCOMPARE(assetKindForFile(u"fonts/Inter.otf"), AssetKind::Font);
        QCOMPARE(assetKindForFile(u"door.ogg"), AssetKind::Audio);
        QCOMPARE(assetKindForFile(u"intro.webm"), AssetKind::Video);
        QVERIFY(!assetKindForFile(u"notes.txt"));
        QVERIFY(!assetKindForFile(u"png"));
        QVERIFY(!assetKindForFile(u"art.png/readme"));
    }

    void format1FixtureMigratesToTheBasicOne()
    {
        // The basic fixture is its format 1 copy, migrated, plus a scene format 1 can't say.
        const ProjectFiles format1 =
            support::readTree(support::fixturePath(QStringLiteral("basic_format1")));
        const ProjectFiles format2 =
            support::readTree(support::fixturePath(QStringLiteral("basic")));
        const ParseResult parsed = parseProject(format1);
        QVERIFY2(parsed.errors.isEmpty(), qPrintable(errorStrings(parsed.errors).join(u'\n')));
        const ProjectFiles written = serializeProject(*parsed.project);
        QCOMPARE(support::fileNames(written), support::fileNames(format1));
        for (const auto& [path, bytes] : written) {
            QCOMPARE(QString::fromUtf8(bytes), QString::fromUtf8(format2.at(path)));
        }
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
            {"active": true, "visible": true, "classes": [], "area": "viewport",
             "transform": {"pos": [0, 0], "rot": 0, "scale": [1, 1], "flip": [false, false]},
             "tint": "\u0023FFFFFF", "tint_mode": "mix", "clip": false,
             "props": {}, "children": []},
            {"image": {"sheet": "hero", "sprite": "a.png", "mode": "stretch"},
             "fill": "\u0023C04040FF", "tint": "\u002300000080"})");

        const ParseResult first = parseProject(files);
        QVERIFY2(first.project, qPrintable(errorStrings(first.errors).join(u'\n')));
        const ProjectFiles canonical = serializeProject(*first.project);

        QCOMPARE(QString::fromUtf8(canonical.at(paths::scene(QStringLiteral("a")))),
                 QStringLiteral("{\n"
                                "  \"format\": \"shoal-scene\",\n"
                                "  \"version\": 3,\n"
                                "  \"size\": [640, 360],\n"
                                "  \"entities\": [\n"
                                "    {},\n"
                                "    {\n"
                                "      \"image\": {\n"
                                "        \"sheet\": \"hero\",\n"
                                "        \"sprite\": \"a.png\"\n"
                                "      },\n"
                                "      \"fill\": \"#c04040\",\n"
                                "      \"tint\": \"#00000080\"\n"
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

    void readsAndWritesText()
    {
        ProjectFiles files = minimalProject();
        files[paths::scene(QStringLiteral("a"))] = sceneWithEntity(R"(
            {"size": [200, 40],
             "text": {"font": "body", "string": "Start\nGame", "key": "menu.start", "size": 24,
                      "color": "\u0023FFCC00", "align": [0, 1], "wrap": "none",
                      "line_height": 1.5, "outline": 2}},
            {"size": [100, 20], "text": {"font": "body", "string": "Go", "size": 32,
                                         "wrap": "word", "align": [0.5, 0.5]}},
            {"text": "Old"})");

        const ParseResult parsed = parseProject(files);
        QVERIFY2(parsed.project, qPrintable(errorStrings(parsed.errors).join(u'\n')));
        const std::vector<Entity>& entities =
            parsed.project->scenes.value(QStringLiteral("a")).entities;
        QVERIFY(entities.at(0).text);
        const Text& text = *entities.at(0).text;
        QCOMPARE(text.font, QStringLiteral("body"));
        QCOMPARE(text.string, QStringLiteral("Start\nGame"));
        QCOMPARE(text.key, QStringLiteral("menu.start"));
        QCOMPARE(text.size, 24.0);
        QCOMPARE(text.color, (Color{255, 204, 0, 255}));
        QCOMPARE(text.align, (Point{0, 1}));
        QCOMPARE(text.wrap, TextWrap::None);
        QCOMPARE(text.lineHeight, 1.5);
        QCOMPARE(text.extras.value(QStringLiteral("outline")), QJsonValue(2));
        QCOMPARE(kindOf(entities.at(0)), QStringLiteral("text"));
        // A string, as the editor drew text before the part, stays as written.
        QVERIFY(!entities.at(2).text);
        QCOMPARE(entities.at(2).extras.value(QStringLiteral("text")),
                 QJsonValue(QStringLiteral("Old")));

        // The defaults are left out.
        const QString written = QString::fromUtf8(
            serializeProject(*parsed.project).at(paths::scene(QStringLiteral("a"))));
        QVERIFY(
            written.contains(QStringLiteral("\"text\": {\n"
                                            "        \"font\": \"body\",\n"
                                            "        \"string\": \"Go\"\n"
                                            "      }")));
        QVERIFY(written.contains(QStringLiteral("\"line_height\": 1.5")));
        QVERIFY(written.contains(QStringLiteral("\"text\": \"Old\"")));
        const ParseResult again = parseProject(serializeProject(*parsed.project));
        QVERIFY(again.project);
        QCOMPARE(*again.project, *parsed.project);
    }

    void readsAndWritesVideo()
    {
        ProjectFiles files = minimalProject();
        files[paths::scene(QStringLiteral("a"))] = sceneWithEntity(R"(
            {"size": [160, 90], "video": {"clip": "intro", "loop": true, "autoplay": true,
                                          "mode": "fit", "muted": true}},
            {"size": [160, 90], "video": {"clip": "intro", "loop": false, "mode": "stretch"}})");

        const ParseResult parsed = parseProject(files);
        QVERIFY2(parsed.project, qPrintable(errorStrings(parsed.errors).join(u'\n')));
        const std::vector<Entity>& entities =
            parsed.project->scenes.value(QStringLiteral("a")).entities;
        QVERIFY(entities.at(0).video);
        const Video& video = *entities.at(0).video;
        QCOMPARE(video.clip, QStringLiteral("intro"));
        QVERIFY(video.loop);
        QVERIFY(video.autoplay);
        QCOMPARE(video.mode, VideoMode::Fit);
        QCOMPARE(video.extras.value(QStringLiteral("muted")), QJsonValue(true));
        QCOMPARE(kindOf(entities.at(0)), QStringLiteral("video"));

        // The defaults are left out.
        const QString written = QString::fromUtf8(
            serializeProject(*parsed.project).at(paths::scene(QStringLiteral("a"))));
        QVERIFY(
            written.contains(QStringLiteral("\"video\": {\n"
                                            "        \"clip\": \"intro\"\n"
                                            "      }")));
        QVERIFY(written.contains(QStringLiteral("\"mode\": \"fit\"")));
        const ParseResult again = parseProject(serializeProject(*parsed.project));
        QVERIFY(again.project);
        QCOMPARE(*again.project, *parsed.project);
    }

    void readsAndWritesTransitionTypes()
    {
        ProjectFiles files = minimalProject();
        files[paths::transitionType(QStringLiteral("ripple"))] = QByteArray(R"(
            {"format": "shoal-transition", "version": 1, "name": "Ripple",
             "preview": "ripple.gif", "options": [{"name": "rings", "type": "number"}],
             "later": true})");
        files[paths::FLOW] = flowWithTransition(R"(
            {"id": "t", "from": "a", "to": "b", "effect": {"type": "ripple", "duration_ms": 300,
             "params": {"rings": "not a number", "seed": 7}}})");
        const ParseResult parsed = parseProject(files);
        QVERIFY2(parsed.project, qPrintable(errorStrings(parsed.errors).join(u'\n')));
        // Kept as written, and read when used.
        const TransitionType& ripple =
            parsed.project->transitionTypes.value(QStringLiteral("ripple"));
        QCOMPARE(ripple.content.value(QStringLiteral("later")), QJsonValue(true));
        QCOMPARE(readTransitionType(QStringLiteral("ripple"), ripple).definition->preview,
                 QStringLiteral("ripple.gif"));
        // An effect's values are the engine's: they load whatever they are.
        const Effect& effect = *parsed.project->flow.transitions.at(0).effect;
        QCOMPARE(effect.params.value(QStringLiteral("rings")),
                 QJsonValue(QStringLiteral("not a number")));

        const ProjectFiles written = serializeProject(*parsed.project);
        QVERIFY(written.contains(paths::transitionType(QStringLiteral("ripple"))));
        QVERIFY(isProjectFile(paths::transitionType(QStringLiteral("ripple"))));
        const ParseResult again = parseProject(written);
        QVERIFY(again.project);
        QCOMPARE(*again.project, *parsed.project);
    }

    void readsAndWritesAudio()
    {
        ProjectFiles files = minimalProject();
        files[paths::scene(QStringLiteral("a"))] = sceneWithEntity(R"(
            {"audio": {"clip": "door", "volume": 0.5, "loop": true, "autoplay": true,
                       "radius": 400, "bus": "sfx"}},
            {"audio": {"clip": "music", "volume": 1, "loop": false}})");

        const ParseResult parsed = parseProject(files);
        QVERIFY2(parsed.project, qPrintable(errorStrings(parsed.errors).join(u'\n')));
        const std::vector<Entity>& entities =
            parsed.project->scenes.value(QStringLiteral("a")).entities;
        QVERIFY(entities.at(0).audio);
        const Audio& audio = *entities.at(0).audio;
        QCOMPARE(audio.clip, QStringLiteral("door"));
        QCOMPARE(audio.volume, 0.5);
        QVERIFY(audio.loop);
        QVERIFY(audio.autoplay);
        QCOMPARE(audio.radius, std::optional(400.0));
        QCOMPARE(audio.extras.value(QStringLiteral("bus")), QJsonValue(QStringLiteral("sfx")));
        QCOMPARE(kindOf(entities.at(0)), QStringLiteral("sound"));
        QVERIFY(!entities.at(1).audio->radius);

        // The defaults are left out.
        const QString written = QString::fromUtf8(
            serializeProject(*parsed.project).at(paths::scene(QStringLiteral("a"))));
        QVERIFY(
            written.contains(QStringLiteral("\"audio\": {\n"
                                            "        \"clip\": \"music\"\n"
                                            "      }")));
        QVERIFY(written.contains(QStringLiteral("\"radius\": 400")));
        const ParseResult again = parseProject(serializeProject(*parsed.project));
        QVERIFY(again.project);
        QCOMPARE(*again.project, *parsed.project);
    }

    void readsAndWritesLocks()
    {
        ProjectFiles files = minimalProject();
        files[paths::scene(QStringLiteral("a"))] = sceneWithEntity(R"(
            {"editor": {"locks": ["tint", "transform.pos.x", "asset"], "note": "keep"}})");

        const ParseResult parsed = parseProject(files);
        QVERIFY2(parsed.project, qPrintable(errorStrings(parsed.errors).join(u'\n')));
        const Entity entity = parsed.project->scenes.value(QStringLiteral("a")).entities.at(0);
        QCOMPARE(entity.locks, Locks(Lock::PosX) | Lock::Asset | Lock::Tint);
        QCOMPARE(entity.editorExtras.value(QStringLiteral("note")),
                 QJsonValue(QStringLiteral("keep")));

        // Written in the editor's order, with what else the block has.
        const QString written = QString::fromUtf8(
            serializeProject(*parsed.project).at(paths::scene(QStringLiteral("a"))));
        QVERIFY2(written.contains(QStringLiteral("\"editor\": {\n"
                                                 "        \"locks\": [\"transform.pos.x\", "
                                                 "\"asset\", \"tint\"],\n"
                                                 "        \"note\": \"keep\"\n"
                                                 "      }")),
                 qPrintable(written));
        const ParseResult again = parseProject(serializeProject(*parsed.project));
        QVERIFY(again.project);
        QCOMPARE(*again.project, *parsed.project);
    }

    void keepsWhatIsLocked()
    {
        Entity before;
        before.transform.pos = {10, 20};
        before.transform.rot = 30;
        before.size = Extent::px({100, 50});
        before.tint = Color{255, 0, 0, 255};
        before.locks = Locks(Lock::PosX) | Lock::Angle | Lock::Width | Lock::Tint;
        Entity edited = before;
        edited.transform.pos = {11, 21};
        edited.transform.rot = 45;
        edited.size = Extent::px({200, 80});
        edited.tint = Color{};
        edited.fill = Color{0, 0, 255, 255};
        edited.locks = Locks(Lock::Angle);
        const Entity kept = keepingLocked(before, edited);
        QCOMPARE(kept.transform.pos, (Point{10, 21}));
        QCOMPARE(kept.transform.rot, 30.0);
        QCOMPARE(kept.size, std::optional(Extent::px({100, 80})));
        QCOMPARE(kept.tint, before.tint);
        QCOMPARE(kept.fill, edited.fill);    // not locked
        QCOMPARE(kept.locks, edited.locks);  // the locks are the edit's
        // A size of its own, with an axis locked, can't be given or taken away.
        edited.size.reset();
        QCOMPARE(keepingLocked(before, edited).size, before.size);
        QCOMPARE(keepingLocked(Entity{}, edited), edited);
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
        QTest::newRow("asset alias in two kinds")
            << paths::MANIFEST
            << QByteArray(R"({"format": "shoal-project", "version": 2, "name": "T",
                             "design_resolution": [640, 360],
                             "assets": {"sheets": {"hero": "a.sprat"},
                                        "audio": {"hero": "hero.ogg"}}})")
            << R"(project.shoal.json: assets.audio.hero: "hero" is already an alias in sheets; )"
               "an alias is unique across all kinds of asset";
        QTest::newRow("asset alias")
            << paths::MANIFEST
            << QByteArray(R"({"format": "shoal-project", "version": 2, "name": "T",
                             "design_resolution": [640, 360],
                             "assets": {"fonts": {"my font": "a.ttf"}}})")
            << R"(project.shoal.json: assets.fonts[0]: "my font" is not a valid id)";
        QTest::newRow("newer manifest")
            << paths::MANIFEST
            << QByteArray(R"({"format": "shoal-project", "version": 3, "name": "T",
                             "design_resolution": [640, 360]})")
            << "project.shoal.json: version: 3 is newer than this Shoal reads (2); update "
               "Shoal to open this project";
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
            << scene << QByteArray(R"({"format": "shoal-scene", "version": 3, "size": "big"})")
            << scene +
                   R"(: size: expected [width, height], whole numbers from 1 to 16777216, found "big")";
        QTest::newRow("newer scene")
            << scene << QByteArray(R"({"format": "shoal-scene", "version": 4, "size": [1, 1]})")
            << scene +
                   ": version: 4 is newer than this Shoal reads (3); update Shoal to open this "
                   "project";
        QTest::newRow("entity id")
            << scene << sceneWithEntity(R"({"id": "9lives"})")
            << scene +
                   R"(: entities[0].id: "9lives" is not a valid id (use letters, digits, _ and -, starting with a letter or _))";
        QTest::newRow("class id")
            << scene << sceneWithEntity(R"({"classes": ["has space"]})")
            << scene + R"(: entities[0].classes[0]: "has space" is not a valid id)";
        QTest::newRow("flip")
            << scene << sceneWithEntity(R"({"transform": {"flip": [1, 0]}})")
            << scene +
                   ": entities[0].transform.flip: expected [horizontal, vertical], each true or "
                   "false, found an array";
        QTest::newRow("child flip")
            << scene << sceneWithEntity(R"({"children": [{"transform": {"flip": true}}]})")
            << scene +
                   ": entities[0].children[0].transform.flip: expected [horizontal, vertical], "
                   "each true or false, found true";
        QTest::newRow("space")
            << scene << sceneWithEntity(R"({"space": "ui"})")
            << scene + R"(: entities[0].space: expected "world" or "screen", found "ui")";
        QTest::newRow("parallax") << scene << sceneWithEntity(R"({"parallax": 0.5})")
                                  << scene + ": entities[0].parallax: expected [x, y], found 0.5";
        QTest::newRow("size")
            << scene << sceneWithEntity(R"({"size": [10, -1]})")
            << scene +
                   ": entities[0].size: expected [width, height], each 0 or more pixels, a "
                   "percentage such as \"50%\" or a number of tiles such as \"2\u25A1\", found "
                   "an array";
        QTest::newRow("size unit")
            << scene << sceneWithEntity(R"({"size": ["10px", 10]})")
            << scene +
                   ": entities[0].size: expected [width, height], each 0 or more pixels, a "
                   "percentage such as \"50%\" or a number of tiles such as \"2\u25A1\", found "
                   "an array";
        QTest::newRow("at")
            << scene << sceneWithEntity(R"({"at": "center"})")
            << scene +
                   ": entities[0].at: expected [x, y], each 0, a percentage such as \"50%\" or a "
                   "tile such as \"3\u25A1\", a whole number from 0, found \"center\"";
        QTest::newRow("at in pixels")
            << scene << sceneWithEntity(R"({"at": [16, "50%"]})")
            << scene +
                   ": entities[0].at: expected [x, y], each 0, a percentage such as \"50%\" or a "
                   "tile such as \"3\u25A1\", a whole number from 0, found an array";
        QTest::newRow("at between tiles")
            << scene << sceneWithEntity(QStringLiteral("{\"at\": [\"1.5\u25A1\", 0]}").toUtf8())
            << scene +
                   ": entities[0].at: expected [x, y], each 0, a percentage such as \"50%\" or a "
                   "tile such as \"3\u25A1\", a whole number from 0, found an array";
        QTest::newRow("format 2 align")
            << scene << sceneWithEntity(R"({"align": "center"})", 2)
            << scene + R"(: entities[0].align: expected [x, y], found "center")";
        QTest::newRow("format 2 cell")
            << scene << sceneWithEntity(R"({"cell": [1, -1]})", 2)
            << scene +
                   ": entities[0].cell: expected [column, row], whole numbers from 0 to "
                   "16777216, found an array";
        QTest::newRow("container type")
            << scene << sceneWithEntity(R"({"container": {"tile_size": [8, 8]}})")
            << scene + ": entities[0].container.type: missing";
        QTest::newRow("unknown container type")
            << scene << sceneWithEntity(R"({"container": {"type": "stack", "tile_size": [8, 8]}})")
            << scene + R"(: entities[0].container.type: expected "tiled", found "stack")";
        QTest::newRow("tile size")
            << scene << sceneWithEntity(R"({"container": {"type": "tiled"}})")
            << scene + ": entities[0].container.tile_size: missing";
        QTest::newRow("empty tile")
            << scene << sceneWithEntity(R"({"container": {"type": "tiled", "tile_size": [8, 0]}})")
            << scene +
                   ": entities[0].container.tile_size: expected [width, height], each more than "
                   "0, found an array";
        QTest::newRow("tile pivot")
            << scene
            << sceneWithEntity(
                   R"({"container": {"type": "tiled", "tile_size": [8, 8], "tile_pivot": 1}})")
            << scene + ": entities[0].container.tile_pivot: expected [x, y], found 1";
        QTest::newRow("area")
            << scene << sceneWithEntity(R"({"area": "notch"})")
            << scene + R"(: entities[0].area: expected "viewport" or "safe", found "notch")";
        QTest::newRow("image sheet")
            << scene << sceneWithEntity(R"({"image": {"sprite": "a.png"}})")
            << scene + ": entities[0].image.sheet: missing";
        QTest::newRow("image without a sprite")
            << scene << sceneWithEntity(R"({"image": {"sheet": "hero"}})")
            << scene +
                   ": entities[0].image.sprite: missing (an image names a sprite or an "
                   "animation)";
        QTest::newRow("image with both")
            << scene
            << sceneWithEntity(
                   R"({"image": {"sheet": "hero", "sprite": "a.png", "animation": "walk"}})")
            << scene +
                   ": entities[0].image.animation: an image is a sprite or an animation, not "
                   "both";
        QTest::newRow("image asset with a sheet")
            << scene << sceneWithEntity(R"({"image": {"asset": "title", "sheet": "hero"}})")
            << scene +
                   ": entities[0].image.sheet: an image is a sheet's sprite or animation, or an "
                   "image asset, not both";
        QTest::newRow("image asset id")
            << scene << sceneWithEntity(R"({"image": {"asset": "2d"}})")
            << scene +
                   R"(: entities[0].image.asset: "2d" is not a valid id (use letters, digits, _ )"
                   "and -, starting with a letter or _)";
        QTest::newRow("empty sprite")
            << scene << sceneWithEntity(R"({"image": {"sheet": "hero", "sprite": ""}})")
            << scene + ": entities[0].image.sprite: must not be empty";
        QTest::newRow("image mode")
            << scene
            << sceneWithEntity(R"({"image": {"sheet": "hero", "sprite": "a.png", "mode": "tile"}})")
            << scene +
                   R"(: entities[0].image.mode: expected "stretch" or "nineslice", found "tile")";
        QTest::newRow("fill")
            << scene << sceneWithEntity(R"({"fill": "\u002312345"})")
            << scene +
                   ": entities[0].fill: expected a color such as \"#e0a030\" or \"#e0a03080\", "
                   "found \"#12345\"";
        QTest::newRow("tint")
            << scene << sceneWithEntity(R"({"tint": "white"})")
            << scene +
                   ": entities[0].tint: expected a color such as \"#e0a030\" or \"#e0a03080\", "
                   "found \"white\"";
        QTest::newRow("tint mode")
            << scene << sceneWithEntity(R"({"tint_mode": "add"})")
            << scene +
                   R"(: entities[0].tint_mode: expected "mix", "inherit" or "override", found "add")";
        QTest::newRow("clip") << scene << sceneWithEntity(R"({"clip": 1})")
                              << scene + ": entities[0].clip: expected true or false, found 1";
        QTest::newRow("entity not an object")
            << scene << sceneWithEntity(R"("player")")
            << scene + R"(: entities[0]: expected an object, found "player")";
        // Format 1 files name the value at fault as they wrote it.
        QTest::newRow("format 1 layer id")
            << scene << QByteArray(R"({"format": "shoal-scene", "version": 1, "size": [640, 360],
                             "layers": [{"id": "far away"}]})")
            << scene +
                   R"(: layers[0].id: "far away" is not a valid id (use letters, digits, _ and -, starting with a letter or _))";
        QTest::newRow("format 1 entity layer")
            << scene << format1SceneWithEntity(R"({"type": "empty", "layer": "9"})")
            << scene +
                   R"(: entities[0].layer: "9" is not a valid id (use letters, digits, _ and -, starting with a letter or _))";
        QTest::newRow("format 1 sprite sheet")
            << scene
            << format1SceneWithEntity(
                   R"({"type": "sprite", "layer": "w", "sprite": {"path": "a.png"}})")
            << scene + ": entities[0].sprite.sheet: missing";
        QTest::newRow("format 1 sprite path")
            << scene
            << format1SceneWithEntity(
                   R"({"type": "sprite", "layer": "w", "sprite": {"sheet": "hero", "path": 3}})")
            << scene + ": entities[0].sprite.path: expected a string, found 3";
        QTest::newRow("format 1 animation name")
            << scene
            << format1SceneWithEntity(
                   R"({"type": "animation", "layer": "w", "animation": {"sheet": "hero"}})")
            << scene + ": entities[0].animation.name: missing";
        QTest::newRow("format 1 child transform")
            << scene
            << format1SceneWithEntity(
                   R"({"type": "group", "layer": "w", "children": [{"type": "mark", "transform": {"pos": 1}}]})")
            << scene + ": entities[0].children[0].transform.pos: expected [x, y], found 1";
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
        QTest::newRow("transition type format")
            << paths::transitionType(QStringLiteral("ripple"))
            << QByteArray(R"({"format": "shoal-preset", "version": 1})")
            << R"(transitions/ripple.json: format: expected "shoal-transition", found "shoal-preset")";
        QTest::newRow("transition type id")
            << QStringLiteral("transitions/two words.json")
            << QByteArray(R"({"format": "shoal-transition", "version": 1})")
            << "transitions/two words.json: \"two words\" is not a valid transition type id (use "
               "letters, digits, _ and -, starting with a letter or _)";
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
        QTest::newRow("text without a font")
            << scene << sceneWithEntity(R"({"size": [10, 10], "text": {"string": "A"}})")
            << "scenes/a/scene.json: entities[0].text.font: missing";
        QTest::newRow("text size 0")
            << scene << sceneWithEntity(R"({"size": [10, 10], "text": {"font": "f", "size": 0}})")
            << "scenes/a/scene.json: entities[0].text.size: expected a number more than 0, found 0";
        QTest::newRow("text wrap")
            << scene
            << sceneWithEntity(R"({"size": [10, 10], "text": {"font": "f", "wrap": "all"}})")
            << R"(scenes/a/scene.json: entities[0].text.wrap: expected "word" or "none", found "all")";
        QTest::newRow("video without a clip")
            << scene << sceneWithEntity(R"({"size": [10, 10], "video": {"loop": true}})")
            << "scenes/a/scene.json: entities[0].video.clip: missing";
        QTest::newRow("video mode")
            << scene
            << sceneWithEntity(R"({"size": [10, 10], "video": {"clip": "v", "mode": "crop"}})")
            << R"(scenes/a/scene.json: entities[0].video.mode: expected "stretch", "fit" or "fill", found "crop")";
        QTest::newRow("audio without a clip")
            << scene << sceneWithEntity(R"({"audio": {"loop": true}})")
            << "scenes/a/scene.json: entities[0].audio.clip: missing";
        QTest::newRow("audio volume")
            << scene << sceneWithEntity(R"({"audio": {"clip": "a", "volume": 2}})")
            << "scenes/a/scene.json: entities[0].audio.volume: expected a number from 0 to 1, "
               "found 2";
        QTest::newRow("audio radius")
            << scene << sceneWithEntity(R"({"audio": {"clip": "a", "radius": 0}})")
            << "scenes/a/scene.json: entities[0].audio.radius: expected a number more than 0, "
               "found 0";
        QTest::newRow("lock")
            << scene << sceneWithEntity(R"({"editor": {"locks": ["position"]}})")
            << R"(scenes/a/scene.json: entities[0].editor.locks: "position" is not a property that can be locked)";
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
            sceneWithEntity(R"({"image": {"sheet": "undeclared", "sprite": "a.png"}})");
        files[paths::scene(QStringLiteral("b"))] =
            format1SceneWithEntity(R"({"type": "empty", "layer": "no_such_layer"})");
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
        QCOMPARE(main.entities.size(), size_t{2});
        QCOMPARE(main.entities[0].id, QStringLiteral("world"));
        QCOMPARE(main.entities[1].id, QStringLiteral("hud"));
        QCOMPARE(main.entities[1].space, std::optional(Space::Screen));
        const Entity& camera = main.entities[0].children.at(0);
        QCOMPARE(kindOf(camera), QStringLiteral("camera"));
        QCOMPARE(camera.classes, QStringList{QStringLiteral("main")});
        QCOMPARE(camera.transform.pos, (Point{320, 180}));
        QCOMPARE(camera.size, std::optional(Extent::px({640, 360})));
        QCOMPARE(camera.pivot, std::optional(Point{0.5, 0.5}));
    }

    // --- scene format 1 ---

    void migratesLayersIntoEntities()
    {
        ProjectFiles files = minimalProject();
        files[paths::scene(QStringLiteral("a"))] =
            R"({"format": "shoal-scene", "version": 1, "size": [640, 360], "fit": "crop",
                "layers": [{"id": "far", "space": "world", "parallax": [0.5, 1], "x_note": 1},
                           {"id": "near", "space": "world"},
                           {"id": "ui", "space": "screen", "fit": "stretch"}],
                "entities": [{"id": "a1", "type": "mark", "layer": "near"},
                             {"id": "near", "type": "mark", "layer": "far"},
                             {"id": "a2", "type": "mark", "layer": "near"},
                             {"id": "lost", "type": "mark", "layer": "gone"},
                             {"id": "b", "type": "mark", "layer": "ui"}]})";
        files[paths::modifier(QStringLiteral("a"), QStringLiteral("m"))] =
            R"({"format": "shoal-modifier", "version": 1,
                "actions": {"seq": [{"do": "set_visible", "target": "layer(near).enemy",
                                     "visible": false},
                                    {"do": "emit", "event": "layer(far) or layer(ui) too"}]}})";
        const ParseResult parsed = parseProject(files);
        QVERIFY2(parsed.project, qPrintable(errorStrings(parsed.errors).join(u'\n')));
        const Scene& scene = parsed.project->scenes.value(QStringLiteral("a"));
        QCOMPARE(scene.fit, std::optional(Fit::Crop));

        // In the layers' order, each holding its entities in theirs.
        QCOMPARE(scene.entities.size(), size_t{4});
        const Entity& far = scene.entities[0];
        QCOMPARE(far.id, QStringLiteral("far"));
        QCOMPARE(far.parallax, std::optional(Point{0.5, 1}));
        QVERIFY(!far.space);  // world space is the default
        QCOMPARE(far.extras.value(QStringLiteral("x_note")).toInt(), 1);
        QCOMPARE(far.children.size(), size_t{1});
        // An entity has the layer's id, so the layer takes the next free one.
        const Entity& near = scene.entities[1];
        QCOMPARE(near.id, QStringLiteral("near_2"));
        QVERIFY(!near.parallax);
        QCOMPARE(near.children.size(), size_t{2});
        QCOMPARE(near.children[1].id, QStringLiteral("a2"));
        const Entity& ui = scene.entities[2];
        QCOMPARE(ui.space, std::optional(Space::Screen));
        QCOMPARE(ui.fit, std::optional(Fit::Stretch));
        // On a missing layer: at the top level, after them, with its layer kept.
        const Entity& lost = scene.entities[3];
        QCOMPARE(lost.id, QStringLiteral("lost"));
        QCOMPARE(lost.extras.value(QStringLiteral("layer")).toString(), QStringLiteral("gone"));

        // The modifier's selectors name the layers' entities as groups.
        const QJsonArray actions = scene.modifiers.value(QStringLiteral("m"))
                                       .actions.toObject()
                                       .value(QStringLiteral("seq"))
                                       .toArray();
        QCOMPARE(actions[0].toObject().value(QStringLiteral("target")).toString(),
                 QStringLiteral("group(near_2).enemy"));
        QCOMPARE(actions[1].toObject().value(QStringLiteral("event")).toString(),
                 QStringLiteral("group(far) or group(ui) too"));
    }

    void migratesEntityTypesIntoParts()
    {
        const Entity sprite = migrated(
            R"({"type": "sprite", "layer": "w", "sprite": {"sheet": "hero", "path": "a.png", "x": 1}})");
        QCOMPARE(sprite.image->sprite, QStringLiteral("a.png"));
        QCOMPARE(sprite.image->mode, ImageMode::Stretch);
        QCOMPARE(sprite.image->extras.value(QStringLiteral("x")).toInt(), 1);
        QVERIFY(!sprite.size);
        QVERIFY(!sprite.pivot);  // the sprite's, as format 1 drew it
        QVERIFY(!sprite.extras.contains(QStringLiteral("type")));

        const Entity animation = migrated(
            R"({"type": "animation", "layer": "w", "animation": {"sheet": "hero", "name": "walk"}})");
        QCOMPARE(animation.image->animation, QStringLiteral("walk"));

        const Entity nineSlice = migrated(
            R"({"type": "nineslice", "layer": "w", "size": [90, 30],
                "sprite": {"sheet": "ui", "path": "panel.png"}})");
        QCOMPARE(nineSlice.image->mode, ImageMode::NineSlice);
        QCOMPARE(nineSlice.size, std::optional(Extent::px({90, 30})));
        QVERIFY(!nineSlice.extras.contains(QStringLiteral("size")));

        const Entity rect = migrated(
            R"({"type": "rect", "layer": "ui", "size": [10, 20], "color": "\u0023336699"})");
        QCOMPARE(rect.fill, std::optional(Color{0x33, 0x66, 0x99, 255}));
        QCOMPARE(rect.size, std::optional(Extent::px({10, 20})));

        const Entity camera = migrated(
            R"({"type": "camera", "layer": "w", "transform": {"pos": [5, 5]},
                "zoom": 2, "bounds": "layer(w).bounds", "x_hint": true})");
        QCOMPARE(camera.size, std::optional(Extent::px({640, 360})));
        QCOMPARE(camera.pivot, std::optional(Point{0.5, 0.5}));
        const QJsonObject view = camera.extras.value(QStringLiteral("view")).toObject();
        QCOMPARE(view.value(QStringLiteral("zoom")).toDouble(), 2.0);
        QCOMPARE(view.value(QStringLiteral("bounds")).toString(),
                 QStringLiteral("group(w).bounds"));
        QVERIFY(camera.extras.value(QStringLiteral("x_hint")).toBool());
        QVERIFY(!camera.extras.contains(QStringLiteral("zoom")));
        QCOMPARE(kindOf(migrated(R"({"type": "camera", "layer": "w"})")), QStringLiteral("camera"));

        const Entity path = migrated(
            R"({"type": "path", "layer": "w", "points": [[0, 0], [10, 0]], "closed": false})");
        QCOMPARE(
            path.extras.value(QStringLiteral("path")).toObject().value(QStringLiteral("points")),
            QJsonValue(QJsonArray{QJsonArray{0, 0}, QJsonArray{10, 0}}));
        QVERIFY(!path.extras.contains(QStringLiteral("points")));

        const Entity layout = migrated(
            R"({"type": "rect", "layer": "ui",
                "layout": {"anchor_min": [0, 0], "pivot": [0.5, 1], "area": "safe"}})");
        QCOMPARE(layout.pivot, std::optional(Point{0.5, 1}));
        QCOMPARE(layout.area, Area::Safe);
        QCOMPARE(layout.extras.value(QStringLiteral("layout")).toObject().keys(),
                 QStringList{QStringLiteral("anchor_min")});

        const Entity unknown = migrated(R"({"type": "light", "layer": "w", "radius": 4})");
        QCOMPARE(unknown.extras.value(QStringLiteral("type")).toString(), QStringLiteral("light"));
        QCOMPARE(unknown.extras.value(QStringLiteral("radius")).toInt(), 4);
    }

    void migratesRegionsKeepingWhereTheyAre()
    {
        // The rect's offset becomes its pivot.
        const Entity region =
            migrated(R"({"type": "region", "layer": "w", "shape": {"rect": [-10, -20, 100, 40]}})");
        QCOMPARE(region.size, std::optional(Extent::px({100, 40})));
        QCOMPARE(region.pivot, std::optional(Point{0.1, 0.5}));
        QVERIFY(!region.extras.contains(QStringLiteral("shape")));

        const Entity atOrigin =
            migrated(R"({"type": "region", "layer": "w", "shape": {"rect": [0, 0, 8, 8]}})");
        QVERIFY(!atOrigin.pivot);

        // No width: a pivot can't say where it is, so its origin moves, its children back.
        const Entity line = migrated(
            R"({"type": "region", "layer": "w", "transform": {"pos": [100, 100], "rot": 90},
                "shape": {"rect": [10, -5, 0, 20]},
                "children": [{"type": "mark", "transform": {"pos": [1, 2]}}]})");
        QCOMPARE(line.size, std::optional(Extent::px({0, 20})));
        QCOMPARE(line.pivot, std::optional(Point{0, 0.25}));
        QCOMPARE(line.transform.pos.x, 100.0);
        QCOMPARE(line.transform.pos.y, 110.0);  // turned a quarter, x is down
        QCOMPARE(line.children[0].transform.pos, (Point{-9, 2}));

        const Entity circle =
            migrated(R"({"type": "region", "layer": "w", "shape": {"circle": [3, 4, 8]}})");
        QCOMPARE(circle.transform.pos, (Point{3, 4}));
        QCOMPARE(circle.extras.value(QStringLiteral("shape")).toObject(),
                 (QJsonObject{{QStringLiteral("circle"), 8}}));
    }

    void migratesFormat1Presets()
    {
        ProjectFiles files = minimalProject();
        files[paths::preset(QStringLiteral("p"))] =
            R"({"format": "shoal-preset", "version": 1, "name": "P", "tint": "\u0023112233",
                "entities": [{"type": "sprite", "sprite": {"sheet": "hero", "path": "a.png"}}]})";
        files[paths::preset(QStringLiteral("broken"))] =
            R"({"format": "shoal-preset", "version": 1, "entities": [{"sprite": 3}]})";
        const ParseResult parsed = parseProject(files);
        QVERIFY2(parsed.project, qPrintable(errorStrings(parsed.errors).join(u'\n')));
        const QJsonObject content = parsed.project->presets.value(QStringLiteral("p")).content;
        QCOMPARE(content.value(QStringLiteral("swatch")).toString(), QStringLiteral("#112233"));
        QVERIFY(!content.contains(QStringLiteral("tint")));
        QCOMPARE(content.value(QStringLiteral("entities")).toArray(),
                 (QJsonArray{QJsonObject{
                     {QStringLiteral("image"),
                      QJsonObject{{QStringLiteral("sheet"), QStringLiteral("hero")},
                                  {QStringLiteral("sprite"), QStringLiteral("a.png")}}}}}));
        // One that can't be read is kept as written, to be reported when it is used.
        QCOMPARE(parsed.project->presets.value(QStringLiteral("broken"))
                     .content.value(QStringLiteral("entities")),
                 QJsonValue(QJsonArray{QJsonObject{{QStringLiteral("sprite"), 3}}}));
        QVERIFY(serializeProject(*parsed.project)
                    .at(paths::preset(QStringLiteral("p")))
                    .startsWith("{\n  \"format\": \"shoal-preset\",\n  \"version\": 3,\n"));
    }

    void migratesFormat2Placement()
    {
        // Format 2 measured positions from an `align` point, as fractions of the parent's
        // rect, and placed the children of a tiled container by their `cell`, 0, 0 by default.
        ProjectFiles files = minimalProject();
        files[paths::scene(QStringLiteral("a"))] = sceneWithEntity(R"(
            {"align": [0.5, 0.5], "size": [100, 50], "children": [
              {"align": [1, 0.25], "transform": {"pos": [-4, 2]}}]},
            {"size": [64, 64], "container": {"type": "tiled", "tile_size": [32, 32]},
             "children": [{"cell": [1, 1], "align": [1, 1]}, {"transform": {"pos": [3, 0]}}]},
            {"cell": [2, 0]})",
                                                                   2);
        files[paths::preset(QStringLiteral("p"))] =
            R"({"format": "shoal-preset", "version": 2, "entities": [{"align": [0, 1]}]})";
        const ParseResult parsed = parseProject(files);
        QVERIFY2(parsed.project, qPrintable(errorStrings(parsed.errors).join(u'\n')));
        const Scene& scene = parsed.project->scenes.value(QStringLiteral("a"));
        // At the top level an align did nothing.
        QCOMPARE(scene.entities[0].at, Anchor{});
        QCOMPARE(scene.entities[0].children[0].at,
                 (Anchor{{100, Unit::Percent}, {25, Unit::Percent}}));
        QCOMPARE(scene.entities[0].children[0].transform.pos, (Point{-4, 2}));
        // In tiles, the cell, and an align did nothing.
        const std::vector<Entity>& tiled = scene.entities[1].children;
        QCOMPARE(tiled[0].at, (Anchor{{1, Unit::Cell}, {1, Unit::Cell}}));
        QCOMPARE(tiled[1].at, (Anchor{{0, Unit::Cell}, {0, Unit::Cell}}));
        QCOMPARE(tiled[1].transform.pos, (Point{3, 0}));
        // Elsewhere a cell did nothing.
        QCOMPARE(scene.entities[2].at, Anchor{});
        QVERIFY(scene.entities[2].extras.isEmpty());

        QCOMPARE(parsed.project->presets.value(QStringLiteral("p"))
                     .content.value(QStringLiteral("entities")),
                 QJsonValue(QJsonArray{
                     QJsonObject{{QStringLiteral("at"),
                                  QJsonArray{QStringLiteral("0%"), QStringLiteral("100%")}}}}));
        const QByteArray written =
            serializeProject(*parsed.project).at(paths::scene(QStringLiteral("a")));
        QVERIFY(written.contains(QStringLiteral("\"at\": [\"1\u25A1\", \"1\u25A1\"]").toUtf8()));
        QVERIFY(written.contains(R"("at": ["100%", "25%"])"));
    }

    void readsAndWritesLengths_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<double>("value");
        QTest::addColumn<int>("unit");
        QTest::newRow("percent") << QStringLiteral("50%") << 50.0 << int(Unit::Percent);
        QTest::newRow("negative percent")
            << QStringLiteral("-12.5%") << -12.5 << int(Unit::Percent);
        QTest::newRow("tiles") << QStringLiteral("3\u25A1") << 3.0 << int(Unit::Cell);
        QTest::newRow("part of a tile") << QStringLiteral("0.5\u25A1") << 0.5 << int(Unit::Cell);
    }

    void readsAndWritesLengths()
    {
        QFETCH(QString, text);
        QFETCH(double, value);
        QFETCH(int, unit);
        const std::optional<Length> length = parseLength(text);
        QCOMPARE(length, std::optional(Length{value, Unit(unit)}));
        QCOMPARE(lengthToJson(*length), QJsonValue(text));
        QCOMPARE(parseLength(12.0), std::optional(Length::px(12)));
        QCOMPARE(lengthToJson(Length::px(12)), QJsonValue(12));
        QVERIFY(!parseLength(QStringLiteral("50")));
        QVERIFY(!parseLength(QStringLiteral("% 50")));
        QVERIFY(!parseLength(QStringLiteral("50 %")));
    }
};

QTEST_GUILESS_MAIN(ProjectCodecTests)
#include "ProjectCodecTests.moc"
