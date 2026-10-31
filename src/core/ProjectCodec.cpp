#include "core/ProjectCodec.h"

#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>

#include "core/JsonReader.h"
#include "core/JsonWriter.h"

namespace shoal::core {

namespace {

constexpr int INT_MAXIMUM = std::numeric_limits<int>::max();
constexpr int INT_MINIMUM = std::numeric_limits<int>::min();

const QString FORMAT_PROJECT = QStringLiteral("shoal-project");
const QString FORMAT_FLOW = QStringLiteral("shoal-flow");
const QString FORMAT_CLASSES = QStringLiteral("shoal-classes");
const QString FORMAT_PRESET = QStringLiteral("shoal-preset");
const QString FORMAT_SCENE = QStringLiteral("shoal-scene");
const QString FORMAT_MODIFIER = QStringLiteral("shoal-modifier");

const QRegularExpression PRESET_PATH(QStringLiteral("^presets/([^/]+)\\.json$"));
const QRegularExpression SCENE_PATH(QStringLiteral("^scenes/([^/]+)/scene\\.json$"));
const QRegularExpression MODIFIER_PATH(QStringLiteral("^scenes/([^/]+)/modifiers/([^/]+)\\.json$"));
const QRegularExpression COLOR(QStringLiteral("^#[0-9A-Fa-f]{6}$"));

// ---------------------------------------------------------------------------------------
// Reading

void checkIds(ObjectReader& reader, const QString& key, const QStringList& ids)
{
    for (qsizetype i = 0; i < ids.size(); ++i) {
        if (!isValidId(ids[i])) {
            reader.failMessage(QStringLiteral("%1[%2]").arg(key).arg(i),
                               QStringLiteral("%1 is not a valid id").arg(describeJson(ids[i])));
        }
    }
}

/// Format 2's `assets`, each kind by alias, every alias unique across the kinds.
Assets readAssets(ObjectReader& reader)
{
    Assets assets;
    QMap<QString, AssetKind> seen;
    for (AssetKind kind : Assets::KINDS) {
        const QString key = enumName(kind);
        QMap<QString, QString>& declared = assets.of(kind);
        declared = reader.stringMap(key);
        checkIds(reader, key, declared.keys());
        for (const QString& alias : declared.keys()) {
            if (const auto other = seen.constFind(alias); other != seen.cend()) {
                reader.failMessage(QStringLiteral("%1.%2").arg(key, alias),
                                   QStringLiteral("%1 is already an alias in %2; an alias is "
                                                  "unique across all kinds of asset")
                                       .arg(describeJson(alias), enumName(*other)));
            }
            seen.insert(alias, kind);
        }
    }
    assets.extras = reader.extras();
    return assets;
}

Manifest readManifest(ObjectReader& reader)
{
    const int version = readHeader(reader, FORMAT_PROJECT, MANIFEST_FORMAT_VERSION);
    Manifest manifest;
    manifest.name = reader.string(QStringLiteral("name"));
    manifest.designResolution = reader.size(QStringLiteral("design_resolution"));
    manifest.fit = reader.enumValue(QStringLiteral("fit"), Fit::Expand);
    manifest.integerScale = reader.boolean(QStringLiteral("integer_scale"), false);
    manifest.start = reader.optionalId(QStringLiteral("start"));

    if (version == 1) {
        const QString sheetsKey = QStringLiteral("sheets");
        manifest.assets.sheets = reader.stringMap(sheetsKey);
        checkIds(reader, sheetsKey, manifest.assets.sheets.keys());
    }
    else {
        ObjectReader assetsReader = reader.child(QStringLiteral("assets"));
        manifest.assets = readAssets(assetsReader);
    }

    ObjectReader exportReader = reader.child(QStringLiteral("export"));
    ExportSettings& settings = manifest.exportSettings;
    if (exportReader.has(QStringLiteral("dir"))) {
        settings.dir = exportReader.string(QStringLiteral("dir"));
    }
    const QString targetsKey = QStringLiteral("targets");
    if (exportReader.has(targetsKey)) {
        settings.targets.clear();
        const QStringList names = exportReader.stringList(targetsKey);
        for (qsizetype i = 0; i < names.size(); ++i) {
            const auto target = enumFromName<ExportTarget>(names[i]);
            if (!target) {
                exportReader.fail(QStringLiteral("%1[%2]").arg(targetsKey).arg(i),
                                  enumChoices<ExportTarget>(), names[i]);
            }
            settings.targets << *target;
        }
    }
    settings.bundle = exportReader.boolean(QStringLiteral("bundle"), false);
    settings.extras = exportReader.extras();

    manifest.extras = reader.extras();
    return manifest;
}

Transition readTransition(ObjectReader& reader)
{
    const QString toKey = QStringLiteral("to");
    const QString modifierKey = QStringLiteral("modifier");
    const QString underlyingKey = QStringLiteral("underlying");

    Transition transition;
    transition.id = reader.id(QStringLiteral("id"));
    transition.from = reader.id(QStringLiteral("from"));
    transition.mode = reader.enumValue(QStringLiteral("mode"), TransitionMode::Replace);

    const QString modeName = enumName(transition.mode);
    switch (transition.mode) {
        case TransitionMode::Replace:
        case TransitionMode::Push:
            if (!reader.has(toKey)) {
                reader.failMessage(toKey, QStringLiteral("missing (a %1 transition needs a "
                                                         "destination scene)")
                                              .arg(modeName));
            }
            break;
        case TransitionMode::Pop:
            if (reader.has(toKey)) {
                reader.failMessage(toKey, QStringLiteral("a pop transition returns to the scene "
                                                         "underneath, so it has no destination"));
            }
            break;
        case TransitionMode::PushModifier:
            if (reader.has(toKey)) {
                reader.failMessage(toKey,
                                   QStringLiteral("a push_modifier transition runs on its own "
                                                  "scene, so it has no destination"));
            }
            if (!reader.has(modifierKey)) {
                reader.failMessage(modifierKey,
                                   QStringLiteral("missing (a push_modifier transition "
                                                  "names the modifier it runs)"));
            }
            break;
    }
    if (transition.mode != TransitionMode::PushModifier && reader.has(modifierKey)) {
        reader.failMessage(modifierKey,
                           QStringLiteral("only push_modifier transitions run a modifier"));
    }
    if (transition.mode != TransitionMode::Push && reader.has(underlyingKey)) {
        reader.failMessage(underlyingKey,
                           QStringLiteral("only push transitions keep a scene underneath"));
    }

    transition.to = reader.optionalId(toKey);
    transition.modifier = reader.optionalId(modifierKey);
    transition.underlying = reader.enumValue(underlyingKey, Underlying::Paused);

    ObjectReader trigger = reader.child(QStringLiteral("trigger"));
    transition.trigger.event = trigger.optionalString(QStringLiteral("event"));
    transition.trigger.requirements = trigger.stringList(QStringLiteral("requires"));
    transition.trigger.extras = trigger.extras();

    if (reader.has(QStringLiteral("effect"))) {
        ObjectReader effectReader = reader.child(QStringLiteral("effect"));
        Effect effect;
        effect.type = effectReader.string(QStringLiteral("type"));
        effect.durationMs = effectReader.integer(QStringLiteral("duration_ms"), 0, 0, INT_MAXIMUM);
        effect.easing = effectReader.optionalString(QStringLiteral("easing"));
        effect.params = effectReader.object(QStringLiteral("params"));
        effect.extras = effectReader.extras();
        transition.effect = effect;
    }

    ObjectReader persist = reader.child(QStringLiteral("persist"));
    transition.persist.state = persist.stringList(QStringLiteral("state"));
    const QString entitiesKey = QStringLiteral("entities");
    transition.persist.entities = persist.stringMap(entitiesKey);
    checkIds(persist, entitiesKey, transition.persist.entities.keys());
    checkIds(persist, entitiesKey, transition.persist.entities.values());
    transition.persist.extras = persist.extras();

    transition.extras = reader.extras();
    return transition;
}

Flow readFlow(ObjectReader& reader)
{
    readHeader(reader, FORMAT_FLOW);
    Flow flow;
    for (ObjectReader& item : reader.objectArray(QStringLiteral("state"))) {
        StateKey key;
        key.key = item.string(QStringLiteral("key"));
        if (key.key.isEmpty()) {
            item.failMessage(QStringLiteral("key"), QStringLiteral("must not be empty"));
        }
        key.doc = item.optionalString(QStringLiteral("doc"));
        key.extras = item.extras();
        flow.state.push_back(std::move(key));
    }
    for (ObjectReader& item : reader.objectArray(QStringLiteral("transitions"))) {
        flow.transitions.push_back(readTransition(item));
    }
    std::stable_sort(flow.state.begin(), flow.state.end(),
                     [](const StateKey& a, const StateKey& b) { return a.key < b.key; });
    std::stable_sort(flow.transitions.begin(), flow.transitions.end(),
                     [](const Transition& a, const Transition& b) { return a.id < b.id; });

    ObjectReader editor = reader.child(QStringLiteral("editor"));
    ObjectReader nodes = editor.child(QStringLiteral("nodes"));
    for (const QString& sceneId : editor.object(QStringLiteral("nodes")).keys()) {
        if (!isValidId(sceneId)) {
            nodes.failMessage(
                sceneId, QStringLiteral("%1 is not a valid scene id").arg(describeJson(sceneId)));
        }
        flow.nodePositions.insert(sceneId, nodes.point(sceneId, {}));
    }
    flow.editorExtras = editor.extras();

    flow.extras = reader.extras();
    return flow;
}

ClassRegistry readClasses(ObjectReader& reader)
{
    readHeader(reader, FORMAT_CLASSES);
    ClassRegistry registry;
    for (auto& [name, item] : reader.objectMap(QStringLiteral("classes"))) {
        ClassDeclaration declaration;
        declaration.doc = item.optionalString(QStringLiteral("doc"));
        declaration.color = item.optionalString(QStringLiteral("color"));
        if (!declaration.color.isEmpty() && !COLOR.match(declaration.color).hasMatch()) {
            item.fail(QStringLiteral("color"), QStringLiteral("a color such as \"#e0a030\""),
                      declaration.color);
        }
        declaration.extras = item.extras();
        registry.classes.insert(name, std::move(declaration));
    }
    registry.extras = reader.extras();
    return registry;
}

/// What reading a format 1 file needs from the rest of the project.
struct Format1 {
    Size designResolution;
};

Transform readTransform(ObjectReader& reader)
{
    Transform transform;
    transform.pos = reader.point(QStringLiteral("pos"), {});
    transform.rot = reader.number(QStringLiteral("rot"), 0);
    transform.scale = reader.point(QStringLiteral("scale"), {1, 1});
    const QString flipKey = QStringLiteral("flip");
    if (reader.has(flipKey)) {
        const QJsonValue flip = reader.value(flipKey);
        const QJsonArray array = flip.toArray();
        if (!flip.isArray() || array.size() != 2 || !array[0].isBool() || !array[1].isBool()) {
            reader.fail(flipKey, QStringLiteral("[horizontal, vertical], each true or false"),
                        flip);
        }
        transform.flipX = array[0].toBool();
        transform.flipY = array[1].toBool();
    }
    transform.extras = reader.extras();
    return transform;
}

Image readImage(ObjectReader& reader)
{
    const QString sheetKey = QStringLiteral("sheet");
    const QString spriteKey = QStringLiteral("sprite");
    const QString animationKey = QStringLiteral("animation");
    const QString assetKey = QStringLiteral("asset");
    Image image;
    if (reader.has(assetKey)) {
        image.asset = reader.id(assetKey);
        for (const QString& key : {sheetKey, spriteKey, animationKey}) {
            if (reader.has(key)) {
                reader.failMessage(key, QStringLiteral("an image is a sheet's sprite or "
                                                       "animation, or an image asset, not both"));
            }
        }
        image.mode = reader.enumValue(QStringLiteral("mode"), ImageMode::Stretch);
        image.extras = reader.extras();
        return image;
    }
    image.sheet = reader.id(sheetKey);
    if (reader.has(spriteKey) && reader.has(animationKey)) {
        reader.failMessage(animationKey,
                           QStringLiteral("an image is a sprite or an animation, not both"));
    }
    if (!reader.has(spriteKey) && !reader.has(animationKey)) {
        reader.failMessage(spriteKey,
                           QStringLiteral("missing (an image names a sprite or an animation)"));
    }
    image.sprite = reader.optionalString(spriteKey);
    image.animation = reader.optionalString(animationKey);
    if (reader.has(spriteKey) ? image.sprite.isEmpty() : image.animation.isEmpty()) {
        reader.failMessage(reader.has(spriteKey) ? spriteKey : animationKey,
                           QStringLiteral("must not be empty"));
    }
    image.mode = reader.enumValue(QStringLiteral("mode"), ImageMode::Stretch);
    image.extras = reader.extras();
    return image;
}

Container readContainer(ObjectReader& reader)
{
    const QString typeKey = QStringLiteral("type");
    const QString tileSizeKey = QStringLiteral("tile_size");
    if (!reader.has(typeKey)) {
        reader.failMessage(typeKey, QStringLiteral("missing"));
    }
    Container container;
    container.type = reader.enumValue(typeKey, ContainerType::Tiled);
    if (!reader.has(tileSizeKey)) {
        reader.failMessage(tileSizeKey, QStringLiteral("missing"));
    }
    const QJsonValue tileSize = reader.value(tileSizeKey);
    const QJsonArray sides = tileSize.toArray();
    const auto isSide = [](const QJsonValue& side) {
        return side.isDouble() && side.toDouble() > 0;
    };
    if (!tileSize.isArray() || sides.size() != 2 || !isSide(sides[0]) || !isSide(sides[1])) {
        reader.fail(tileSizeKey, QStringLiteral("[width, height], each more than 0"), tileSize);
    }
    container.tileSize = SizeF{sides[0].toDouble(), sides[1].toDouble()};
    container.tilePivot = reader.point(QStringLiteral("tile_pivot"), Container{}.tilePivot);
    container.extras = reader.extras();
    return container;
}

Text readText(ObjectReader& reader)
{
    Text text;
    text.font = reader.id(QStringLiteral("font"));
    text.string = reader.optionalString(QStringLiteral("string"));
    text.key = reader.optionalString(QStringLiteral("key"));
    const auto positive = [&](const QString& key, double fallback) {
        const double value = reader.number(key, fallback);
        if (!(value > 0)) {
            reader.fail(key, QStringLiteral("a number more than 0"), reader.value(key));
        }
        return value;
    };
    text.size = positive(QStringLiteral("size"), Text{}.size);
    text.color = reader.color(QStringLiteral("color"), Color{});
    text.align = reader.point(QStringLiteral("align"), Text{}.align);
    text.wrap = reader.enumValue(QStringLiteral("wrap"), TextWrap::Word);
    text.lineHeight = positive(QStringLiteral("line_height"), Text{}.lineHeight);
    text.extras = reader.extras();
    return text;
}

Video readVideo(ObjectReader& reader)
{
    Video video;
    video.clip = reader.id(QStringLiteral("clip"));
    video.loop = reader.boolean(QStringLiteral("loop"), false);
    video.autoplay = reader.boolean(QStringLiteral("autoplay"), false);
    video.mode = reader.enumValue(QStringLiteral("mode"), VideoMode::Stretch);
    video.extras = reader.extras();
    return video;
}

Audio readAudio(ObjectReader& reader)
{
    Audio audio;
    audio.clip = reader.id(QStringLiteral("clip"));
    const QString volume = QStringLiteral("volume");
    audio.volume = reader.number(volume, Audio{}.volume);
    if (!(audio.volume >= 0 && audio.volume <= 1)) {
        reader.fail(volume, QStringLiteral("a number from 0 to 1"), audio.volume);
    }
    audio.loop = reader.boolean(QStringLiteral("loop"), false);
    audio.autoplay = reader.boolean(QStringLiteral("autoplay"), false);
    const QString radius = QStringLiteral("radius");
    if (reader.has(radius)) {
        audio.radius = reader.number(radius, 0);
        if (!(*audio.radius > 0)) {
            reader.fail(radius, QStringLiteral("a number more than 0"), *audio.radius);
        }
    }
    audio.extras = reader.extras();
    return audio;
}

/// An entity's `editor` block: its locks, and what else an editor keeps there.
void readEntityEditor(ObjectReader& reader, Entity& entity)
{
    const QString key = QStringLiteral("locks");
    const QJsonArray locks = reader.array(key);
    for (const QJsonValue& value : locks) {
        const std::optional<Lock> lock = enumFromName<Lock>(value.toString());
        if (!value.isString() || !lock) {
            reader.failMessage(
                key,
                QStringLiteral("%1 is not a property that can be locked").arg(describeJson(value)));
        }
        entity.locks |= *lock;
    }
    entity.editorExtras = reader.extras();
}

/// The fields every entity has, in both formats.
void readCommonFields(ObjectReader& reader, Entity& entity)
{
    const QString classesKey = QStringLiteral("classes");
    entity.id = reader.optionalId(QStringLiteral("id"));
    entity.classes = reader.stringList(classesKey);
    checkIds(reader, classesKey, entity.classes);
    entity.active = reader.boolean(QStringLiteral("active"), true);
    entity.visible = reader.boolean(QStringLiteral("visible"), true);
}

/// Where an entity is in scene format 2, which placed children by `align` and, in a tiled
/// container, by `cell`.
enum class Format2Place { TopLevel, Child, InTiles };

/// An entity in scene format 3, or in format 2 (`version`), whose `align` becomes a
/// percentage of its parent's rect, and whose `cell` becomes tiles in a tiled container.
/// At the top level, or in a tiled container, an `align` did nothing, and is dropped.
Entity readEntityFields(ObjectReader& reader, int version = SCENE_FORMAT_VERSION,
                        Format2Place place = Format2Place::Child)
{
    Entity entity;
    readCommonFields(reader, entity);
    entity.space = reader.optionalEnum<Space>(QStringLiteral("space"));
    entity.parallax = reader.optionalPoint(QStringLiteral("parallax"));
    entity.fit = reader.optionalEnum<Fit>(QStringLiteral("fit"));
    ObjectReader transform = reader.child(QStringLiteral("transform"));
    entity.transform = readTransform(transform);
    entity.size = reader.optionalExtent(QStringLiteral("size"));
    entity.pivot = reader.optionalPoint(QStringLiteral("pivot"));
    if (version >= 3) {
        entity.at = reader.anchor(QStringLiteral("at"));
    }
    else {
        const std::optional<Point> align = reader.optionalPoint(QStringLiteral("align"));
        const Cell cell = reader.cell(QStringLiteral("cell"), {});
        if (place == Format2Place::InTiles) {
            entity.at = {{double(cell.column), Unit::Cell}, {double(cell.row), Unit::Cell}};
        }
        else if (align && place == Format2Place::Child) {
            entity.at = {{align->x * 100, Unit::Percent}, {align->y * 100, Unit::Percent}};
        }
    }
    entity.area = reader.enumValue(QStringLiteral("area"), Area::Viewport);
    if (reader.has(QStringLiteral("image"))) {
        ObjectReader image = reader.child(QStringLiteral("image"));
        entity.image = readImage(image);
    }
    entity.fill = reader.optionalColor(QStringLiteral("fill"));
    entity.tint = reader.color(QStringLiteral("tint"), Color{});
    entity.tintMode = reader.enumValue(QStringLiteral("tint_mode"), TintMode::Mix);
    entity.clip = reader.boolean(QStringLiteral("clip"), false);
    if (reader.has(QStringLiteral("container"))) {
        ObjectReader container = reader.child(QStringLiteral("container"));
        entity.container = readContainer(container);
    }
    // A string is what the editor drew as text before the part: it stays as written.
    if (reader.has(QStringLiteral("text")) && !reader.peek(QStringLiteral("text")).isString()) {
        ObjectReader text = reader.child(QStringLiteral("text"));
        entity.text = readText(text);
    }
    if (reader.has(QStringLiteral("video"))) {
        ObjectReader video = reader.child(QStringLiteral("video"));
        entity.video = readVideo(video);
    }
    if (reader.has(QStringLiteral("audio"))) {
        ObjectReader audio = reader.child(QStringLiteral("audio"));
        entity.audio = readAudio(audio);
    }
    entity.props = reader.object(QStringLiteral("props"));
    if (reader.has(QStringLiteral("editor"))) {
        ObjectReader editor = reader.child(QStringLiteral("editor"));
        readEntityEditor(editor, entity);
    }
    const Format2Place childPlace =
        entity.container && entity.container->type == ContainerType::Tiled ? Format2Place::InTiles
                                                                           : Format2Place::Child;
    for (ObjectReader& child : reader.objectArray(QStringLiteral("children"))) {
        entity.children.push_back(readEntityFields(child, version, childPlace));
    }
    entity.extras = reader.extras();
    return entity;
}

// --- scene format 1 ---

const QRegularExpression LAYER_TERM(QStringLiteral("layer\\(([^)]*)\\)"));

/// `value` with the `layer(x)` terms in its strings rewritten as `group(y)`, where y is the
/// id the layer x became.
QJsonValue renameLayerTerms(const QJsonValue& value, const QMap<QString, QString>& layers)
{
    if (value.isString()) {
        const QString text = value.toString();
        QString renamed;
        qsizetype last = 0;
        for (auto it = LAYER_TERM.globalMatch(text); it.hasNext();) {
            const QRegularExpressionMatch match = it.next();
            const QString layer = match.captured(1);
            renamed += text.mid(last, match.capturedStart() - last);
            renamed += QStringLiteral("group(%1)").arg(layers.value(layer, layer));
            last = match.capturedEnd();
        }
        return last == 0 ? value : QJsonValue(renamed + text.mid(last));
    }
    if (value.isArray()) {
        QJsonArray array;
        for (const QJsonValue& item : value.toArray()) {
            array.append(renameLayerTerms(item, layers));
        }
        return array;
    }
    if (value.isObject()) {
        QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            *it = renameLayerTerms(*it, layers);
        }
        return object;
    }
    return value;
}

std::optional<SizeF> sizeFromJson(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 2 || !array[0].isDouble() || !array[1].isDouble() ||
        array[0].toDouble() < 0 || array[1].toDouble() < 0) {
        return std::nullopt;
    }
    return SizeF{array[0].toDouble(), array[1].toDouble()};
}

/// Moves an entity's origin by `offset`, in its own coordinates, keeping where it and its
/// children are drawn.
void moveOrigin(Entity& entity, Point offset)
{
    if (offset == Point{}) {
        return;
    }
    const Transform& t = entity.transform;
    const double radians = t.rot * std::numbers::pi / 180;
    const double x = offset.x * t.scale.x;
    const double y = offset.y * t.scale.y;
    entity.transform.pos.x += x * std::cos(radians) - y * std::sin(radians);
    entity.transform.pos.y += x * std::sin(radians) + y * std::cos(radians);
    for (Entity& child : entity.children) {
        child.transform.pos.x -= offset.x;
        child.transform.pos.y -= offset.y;
    }
}

/// A format 1 region's `shape`: a rect becomes the entity's size and pivot, and a circle's
/// center its origin.
void migrateRegion(Entity& entity, QJsonObject& extras)
{
    const QString shapeKey = QStringLiteral("shape");
    QJsonObject shape = extras.value(shapeKey).toObject();
    const QJsonArray rect = shape.value(QStringLiteral("rect")).toArray();
    const QJsonArray circle = shape.value(QStringLiteral("circle")).toArray();
    const auto numbers = [](const QJsonArray& array, qsizetype count) {
        return array.size() == count &&
               std::all_of(array.begin(), array.end(),
                           [](const QJsonValue& item) { return item.isDouble(); });
    };
    if (numbers(rect, 4) && rect[2].toDouble() >= 0 && rect[3].toDouble() >= 0) {
        const double x = rect[0].toDouble();
        const double y = rect[1].toDouble();
        const double width = rect[2].toDouble();
        const double height = rect[3].toDouble();
        entity.size = Extent::px({width, height});
        // On an axis with no size, a pivot can't say where the rect is: its origin moves.
        const Point pivot{width > 0 ? -x / width : 0, height > 0 ? -y / height : 0};
        moveOrigin(entity, {width > 0 ? 0 : x, height > 0 ? 0 : y});
        if (pivot != Point{}) {
            entity.pivot = pivot;
        }
        shape.remove(QStringLiteral("rect"));
    }
    else if (numbers(circle, 3)) {
        moveOrigin(entity, {circle[0].toDouble(), circle[1].toDouble()});
        shape.insert(QStringLiteral("circle"), circle[2]);
    }
    if (shape.isEmpty()) {
        extras.remove(shapeKey);
    }
    else {
        extras.insert(shapeKey, shape);
    }
}

/// Moves `keys` of `extras`, those present, into a part of their own.
void moveIntoPart(QJsonObject& extras, const QString& part, std::initializer_list<const char*> keys)
{
    QJsonObject object;
    for (const char* key : keys) {
        const QString name = QLatin1StringView(key);
        if (extras.contains(name)) {
            object.insert(name, extras.take(name));
        }
    }
    if (!object.isEmpty() || part == u"view") {  // a view marks a camera, even an empty one
        extras.insert(part, object);
    }
}

/// An entity in scene format 1, read into today's model. `layer` receives a top-level
/// entity's layer; null for a child or a preset's entity, which keep a `layer` as written.
Entity readEntityFormat1(ObjectReader& reader, const Format1& format1, QString* layer)
{
    static const QStringList TYPES{
        QStringLiteral("empty"),     QStringLiteral("group"),     QStringLiteral("sprite"),
        QStringLiteral("animation"), QStringLiteral("nineslice"), QStringLiteral("text"),
        QStringLiteral("rect"),      QStringLiteral("mark"),      QStringLiteral("path"),
        QStringLiteral("region"),    QStringLiteral("camera")};
    Entity entity;
    readCommonFields(reader, entity);
    const QString type = reader.optionalString(QStringLiteral("type"));
    if (layer) {
        *layer = reader.optionalId(QStringLiteral("layer"));
    }
    if (reader.has(QStringLiteral("sprite"))) {
        ObjectReader sprite = reader.child(QStringLiteral("sprite"));
        Image image;
        image.sheet = sprite.id(QStringLiteral("sheet"));
        image.sprite = sprite.string(QStringLiteral("path"));
        image.extras = sprite.extras();
        entity.image = image;
    }
    else if (reader.has(QStringLiteral("animation"))) {
        ObjectReader animation = reader.child(QStringLiteral("animation"));
        Image image;
        image.sheet = animation.id(QStringLiteral("sheet"));
        image.animation = animation.string(QStringLiteral("name"));
        image.extras = animation.extras();
        entity.image = image;
    }
    if (type == u"nineslice" && entity.image) {
        entity.image->mode = ImageMode::NineSlice;
    }
    ObjectReader transform = reader.child(QStringLiteral("transform"));
    entity.transform = readTransform(transform);
    entity.props = reader.object(QStringLiteral("props"));
    for (ObjectReader& child : reader.objectArray(QStringLiteral("children"))) {
        entity.children.push_back(readEntityFormat1(child, format1, nullptr));
    }

    QJsonObject extras = reader.extras();
    if (const auto size = sizeFromJson(extras.value(QStringLiteral("size")))) {
        entity.size = Extent::px(*size);
        extras.remove(QStringLiteral("size"));
    }
    if (extras.value(QStringLiteral("layout")).isObject()) {
        QJsonObject layout = extras.value(QStringLiteral("layout")).toObject();
        const QJsonArray pivot = layout.value(QStringLiteral("pivot")).toArray();
        if (pivot.size() == 2 && pivot[0].isDouble() && pivot[1].isDouble()) {
            entity.pivot = Point{pivot[0].toDouble(), pivot[1].toDouble()};
            layout.remove(QStringLiteral("pivot"));
        }
        if (const auto area = enumFromName<Area>(layout.value(QStringLiteral("area")).toString())) {
            entity.area = *area;
            layout.remove(QStringLiteral("area"));
        }
        extras.insert(QStringLiteral("layout"), layout);
    }
    if (type == u"region") {
        migrateRegion(entity, extras);
    }
    else if (type == u"camera") {
        moveIntoPart(extras, QStringLiteral("view"),
                     {"zoom", "bounds", "follow", "zoom_min", "zoom_max", "damping"});
        entity.size = Extent::px(
            {double(format1.designResolution.width), double(format1.designResolution.height)});
        entity.pivot = Point{0.5, 0.5};
    }
    else if (type == u"path") {
        moveIntoPart(extras, QStringLiteral("path"), {"points", "curve", "closed"});
    }
    else if (type == u"rect") {
        if (const auto fill = Color::fromName(extras.value(QStringLiteral("color")).toString())) {
            entity.fill = fill;
            extras.remove(QStringLiteral("color"));
        }
    }
    else if (!type.isEmpty() && !TYPES.contains(type)) {
        extras.insert(QStringLiteral("type"), type);  // kept as written
    }
    entity.extras = extras;
    return entity;
}

/// A scene in format 1: each layer becomes a top-level entity holding the entities on it.
/// `layerIds` receives the id each layer's entity took.
Scene readSceneFormat1(ObjectReader& reader, const Format1& format1,
                       QMap<QString, QString>& layerIds)
{
    Scene scene;
    scene.size = reader.size(QStringLiteral("size"));
    scene.fit = reader.optionalEnum<Fit>(QStringLiteral("fit"));

    std::vector<std::pair<QString, Entity>> layers;
    for (ObjectReader& item : reader.objectArray(QStringLiteral("layers"))) {
        Entity layer;
        const QString id = item.id(QStringLiteral("id"));
        if (item.enumValue(QStringLiteral("space"), Space::World) == Space::Screen) {
            layer.space = Space::Screen;
        }
        if (const Point parallax = item.point(QStringLiteral("parallax"), {1, 1});
            parallax != Point{1, 1}) {
            layer.parallax = parallax;
        }
        layer.fit = item.optionalEnum<Fit>(QStringLiteral("fit"));
        layer.extras = item.extras();
        layers.emplace_back(id, std::move(layer));
    }
    std::vector<std::pair<QString, Entity>> entities;
    for (ObjectReader& item : reader.objectArray(QStringLiteral("entities"))) {
        QString layer;
        Entity entity = readEntityFormat1(item, format1, &layer);
        entities.emplace_back(layer, std::move(entity));
    }
    scene.extras = reader.extras();

    // A layer's entity takes the layer's id, or the next free one if an entity has it.
    QSet<QString> taken;
    const std::function<void(const Entity&)> collect = [&](const Entity& entity) {
        taken.insert(entity.id);
        for (const Entity& child : entity.children) {
            collect(child);
        }
    };
    for (const auto& [layer, entity] : entities) {
        collect(entity);
    }
    std::vector<bool> placed(entities.size(), false);
    for (auto& [id, layer] : layers) {
        QString unique = id;
        for (int n = 2; taken.contains(unique); ++n) {
            unique = QStringLiteral("%1_%2").arg(id).arg(n);
        }
        taken.insert(unique);
        layer.id = unique;
        if (layerIds.contains(id)) {
            continue;  // a second layer with the same id holds nothing
        }
        layerIds.insert(id, unique);
        for (size_t i = 0; i < entities.size(); ++i) {
            if (!placed[i] && entities[i].first == id) {
                layer.children.push_back(std::move(entities[i].second));
                placed[i] = true;
            }
        }
    }
    for (auto& [id, layer] : layers) {
        scene.entities.push_back(std::move(layer));
    }
    for (size_t i = 0; i < entities.size(); ++i) {
        if (!placed[i]) {
            Entity& entity = entities[i].second;
            if (!entities[i].first.isEmpty()) {
                entity.extras.insert(QStringLiteral("layer"), entities[i].first);
            }
            scene.entities.push_back(std::move(entity));
        }
    }

    // Camera bounds are selectors too.
    const std::function<void(Entity&)> rename = [&](Entity& entity) {
        const QString viewKey = QStringLiteral("view");
        if (entity.extras.contains(viewKey)) {
            entity.extras.insert(viewKey, renameLayerTerms(entity.extras.value(viewKey), layerIds));
        }
        for (Entity& child : entity.children) {
            rename(child);
        }
    };
    for (Entity& entity : scene.entities) {
        rename(entity);
    }
    return scene;
}

/// A preset in format 1: its entities as scene format 1 wrote them, and `tint` for its
/// swatch. Left as written when its entities can't be read, for the presets panel to report
/// when it is used.
QJsonObject migratePresetFormat1(QJsonObject content, const Format1& format1)
{
    const QString tintKey = QStringLiteral("tint");
    const QString swatchKey = QStringLiteral("swatch");
    if (content.contains(tintKey) && !content.contains(swatchKey)) {
        content.insert(swatchKey, content.take(tintKey));
    }
    const QString entitiesKey = QStringLiteral("entities");
    const QJsonArray entities = content.value(entitiesKey).toArray();
    QJsonArray migrated;
    try {
        for (qsizetype i = 0; i < entities.size(); ++i) {
            if (!entities[i].isObject()) {
                return content;
            }
            ObjectReader item(entities[i].toObject(), QStringLiteral("entities[%1]").arg(i));
            migrated.append(entityToJson(readEntityFormat1(item, format1, nullptr)));
        }
    }
    catch (const FormatError&) {
        return content;
    }
    if (content.contains(entitiesKey)) {
        content.insert(entitiesKey, migrated);
    }
    return content;
}

/// A preset in format 2: its entities as scene format 2 wrote them, placed in the parent
/// they are added to. Left as written when they can't be read.
QJsonObject migratePresetFormat2(QJsonObject content)
{
    const QString entitiesKey = QStringLiteral("entities");
    const QJsonArray entities = content.value(entitiesKey).toArray();
    QJsonArray migrated;
    try {
        for (qsizetype i = 0; i < entities.size(); ++i) {
            if (!entities[i].isObject()) {
                return content;
            }
            ObjectReader item(entities[i].toObject(), QStringLiteral("entities[%1]").arg(i));
            migrated.append(entityToJson(readEntityFields(item, 2)));
        }
    }
    catch (const FormatError&) {
        return content;
    }
    if (content.contains(entitiesKey)) {
        content.insert(entitiesKey, migrated);
    }
    return content;
}

Preset readPreset(ObjectReader& reader, const Format1& format1)
{
    const int version = readHeader(reader, FORMAT_PRESET, PRESET_FORMAT_VERSION);
    QJsonObject content = reader.extras();
    if (version == 1) {
        content = migratePresetFormat1(std::move(content), format1);
    }
    else if (version == 2) {
        content = migratePresetFormat2(std::move(content));
    }
    return Preset{content};
}

/// A scene; `layerIds` receives, for a format 1 scene, the ids its layers' entities took.
Scene readScene(ObjectReader& reader, const Format1& format1,
                std::optional<QMap<QString, QString>>& layerIds)
{
    const int version = readHeader(reader, FORMAT_SCENE, SCENE_FORMAT_VERSION);
    if (version == 1) {
        layerIds.emplace();
        return readSceneFormat1(reader, format1, *layerIds);
    }
    Scene scene;
    scene.size = reader.size(QStringLiteral("size"));
    scene.fit = reader.optionalEnum<Fit>(QStringLiteral("fit"));
    for (ObjectReader& item : reader.objectArray(QStringLiteral("entities"))) {
        scene.entities.push_back(readEntityFields(item, version, Format2Place::TopLevel));
    }
    scene.extras = reader.extras();
    return scene;
}

Modifier readModifier(ObjectReader& reader)
{
    readHeader(reader, FORMAT_MODIFIER);
    Modifier modifier;
    modifier.blocking = reader.boolean(QStringLiteral("blocking"), true);
    modifier.skippable = reader.boolean(QStringLiteral("skippable"), false);
    modifier.priority = reader.integer(QStringLiteral("priority"), 0, INT_MINIMUM, INT_MAXIMUM);

    ObjectReader onEnd = reader.child(QStringLiteral("on_end"));
    modifier.onEnd.mode = onEnd.enumValue(QStringLiteral("mode"), RestoreMode::Commit);
    modifier.onEnd.blendMs = onEnd.integer(QStringLiteral("blend_ms"), 0, 0, INT_MAXIMUM);
    modifier.onEnd.restoreFollow = onEnd.boolean(QStringLiteral("restore_follow"), true);
    modifier.onEnd.extras = onEnd.extras();

    const QString actionsKey = QStringLiteral("actions");
    if (reader.has(actionsKey)) {
        modifier.actions = reader.object(actionsKey);
    }
    modifier.extras = reader.extras();
    return modifier;
}

/// Reads one file with `read`, turning a FormatError into a FileError.
template <typename T, typename Read>
std::optional<T> readFile(const QString& path, const QByteArray& bytes, QList<FileError>& errors,
                          Read read)
{
    try {
        ObjectReader reader(parseJsonObject(bytes), QString());
        return read(reader);
    }
    catch (const FormatError& error) {
        errors << FileError{path, error.message, error.line, error.column};
        return std::nullopt;
    }
}

bool checkPathId(const QString& path, const QString& id, const QString& what,
                 QList<FileError>& errors)
{
    if (isValidId(id)) {
        return true;
    }
    errors << FileError{path, QStringLiteral("\"%1\" is not a valid %2 id (use letters, digits, _ "
                                             "and -, starting with a letter or _)")
                                  .arg(id, what)};
    return false;
}

// ---------------------------------------------------------------------------------------
// Writing

void writeHeader(JsonWriter& writer, const QString& format, int version = FORMAT_VERSION)
{
    writer.key(u"format");
    writer.string(format);
    writer.key(u"version");
    writer.integer(version);
}

void writeExtras(JsonWriter& writer, const QJsonObject& extras)
{
    for (auto it = extras.begin(); it != extras.end(); ++it) {
        writer.key(it.key());
        writer.value(it.value());
    }
}

void writePoint(JsonWriter& writer, Point point)
{
    writer.numberArray({point.x, point.y});
}

void writeStringMap(JsonWriter& writer, const QMap<QString, QString>& map)
{
    writer.beginObject();
    for (auto it = map.begin(); it != map.end(); ++it) {
        writer.key(it.key());
        writer.string(it.value());
    }
    writer.endObject();
}

QByteArray writeManifest(const Manifest& manifest)
{
    JsonWriter writer;
    writer.beginObject();
    writeHeader(writer, FORMAT_PROJECT, MANIFEST_FORMAT_VERSION);
    writer.key(u"name");
    writer.string(manifest.name);
    writer.key(u"design_resolution");
    writer.numberArray({static_cast<double>(manifest.designResolution.width),
                        static_cast<double>(manifest.designResolution.height)});
    writer.key(u"fit");
    writer.string(enumName(manifest.fit));
    writer.key(u"integer_scale");
    writer.boolean(manifest.integerScale);
    if (!manifest.start.isEmpty()) {
        writer.key(u"start");
        writer.string(manifest.start);
    }
    writer.key(u"assets");
    writer.beginObject();
    for (AssetKind kind : Assets::KINDS) {
        if (!manifest.assets.of(kind).isEmpty()) {
            writer.key(enumName(kind));
            writeStringMap(writer, manifest.assets.of(kind));
        }
    }
    writeExtras(writer, manifest.assets.extras);
    writer.endObject();

    const ExportSettings& settings = manifest.exportSettings;
    writer.key(u"export");
    writer.beginObject();
    writer.key(u"dir");
    writer.string(settings.dir);
    QStringList targets;
    for (ExportTarget target : settings.targets) {
        targets << enumName(target);
    }
    writer.key(u"targets");
    writer.stringArray(targets);
    writer.key(u"bundle");
    writer.boolean(settings.bundle);
    writeExtras(writer, settings.extras);
    writer.endObject();

    writeExtras(writer, manifest.extras);
    writer.endObject();
    return writer.finish();
}

void writeTransition(JsonWriter& writer, const Transition& transition)
{
    writer.beginObject();
    writer.key(u"id");
    writer.string(transition.id);
    writer.key(u"from");
    writer.string(transition.from);
    if (!transition.to.isEmpty()) {
        writer.key(u"to");
        writer.string(transition.to);
    }
    writer.key(u"mode");
    writer.string(enumName(transition.mode));
    if (transition.mode == TransitionMode::PushModifier) {
        writer.key(u"modifier");
        writer.string(transition.modifier);
    }
    if (transition.mode == TransitionMode::Push) {
        writer.key(u"underlying");
        writer.string(enumName(transition.underlying));
    }

    const Trigger& trigger = transition.trigger;
    if (!trigger.event.isEmpty() || !trigger.requirements.isEmpty() || !trigger.extras.isEmpty()) {
        writer.key(u"trigger");
        writer.beginObject();
        if (!trigger.event.isEmpty()) {
            writer.key(u"event");
            writer.string(trigger.event);
        }
        if (!trigger.requirements.isEmpty()) {
            writer.key(u"requires");
            writer.stringArray(trigger.requirements);
        }
        writeExtras(writer, trigger.extras);
        writer.endObject();
    }

    if (transition.effect) {
        const Effect& effect = *transition.effect;
        writer.key(u"effect");
        writer.beginObject();
        writer.key(u"type");
        writer.string(effect.type);
        writer.key(u"duration_ms");
        writer.integer(effect.durationMs);
        if (!effect.easing.isEmpty()) {
            writer.key(u"easing");
            writer.string(effect.easing);
        }
        if (!effect.params.isEmpty()) {
            writer.key(u"params");
            writer.value(effect.params);
        }
        writeExtras(writer, effect.extras);
        writer.endObject();
    }

    const Persist& persist = transition.persist;
    if (!persist.state.isEmpty() || !persist.entities.isEmpty() || !persist.extras.isEmpty()) {
        writer.key(u"persist");
        writer.beginObject();
        if (!persist.state.isEmpty()) {
            writer.key(u"state");
            writer.stringArray(persist.state);
        }
        if (!persist.entities.isEmpty()) {
            writer.key(u"entities");
            writeStringMap(writer, persist.entities);
        }
        writeExtras(writer, persist.extras);
        writer.endObject();
    }

    writeExtras(writer, transition.extras);
    writer.endObject();
}

QByteArray writeFlow(const Flow& flow)
{
    JsonWriter writer;
    writer.beginObject();
    writeHeader(writer, FORMAT_FLOW);

    std::vector<const StateKey*> state;
    for (const StateKey& key : flow.state) {
        state.push_back(&key);
    }
    std::stable_sort(state.begin(), state.end(),
                     [](const StateKey* a, const StateKey* b) { return a->key < b->key; });
    writer.key(u"state");
    writer.beginArray();
    for (const StateKey* key : state) {
        writer.beginObject();
        writer.key(u"key");
        writer.string(key->key);
        if (!key->doc.isEmpty()) {
            writer.key(u"doc");
            writer.string(key->doc);
        }
        writeExtras(writer, key->extras);
        writer.endObject();
    }
    writer.endArray();

    std::vector<const Transition*> transitions;
    for (const Transition& transition : flow.transitions) {
        transitions.push_back(&transition);
    }
    std::stable_sort(transitions.begin(), transitions.end(),
                     [](const Transition* a, const Transition* b) { return a->id < b->id; });
    writer.key(u"transitions");
    writer.beginArray();
    for (const Transition* transition : transitions) {
        writeTransition(writer, *transition);
    }
    writer.endArray();

    if (!flow.nodePositions.isEmpty() || !flow.editorExtras.isEmpty()) {
        writer.key(u"editor");
        writer.beginObject();
        if (!flow.nodePositions.isEmpty()) {
            writer.key(u"nodes");
            writer.beginObject();
            for (auto it = flow.nodePositions.begin(); it != flow.nodePositions.end(); ++it) {
                writer.key(it.key());
                writePoint(writer, it.value());
            }
            writer.endObject();
        }
        writeExtras(writer, flow.editorExtras);
        writer.endObject();
    }

    writeExtras(writer, flow.extras);
    writer.endObject();
    return writer.finish();
}

QByteArray writeClasses(const ClassRegistry& registry)
{
    JsonWriter writer;
    writer.beginObject();
    writeHeader(writer, FORMAT_CLASSES);
    writer.key(u"classes");
    writer.beginObject();
    for (auto it = registry.classes.begin(); it != registry.classes.end(); ++it) {
        writer.key(it.key());
        writer.beginObject();
        if (!it->doc.isEmpty()) {
            writer.key(u"doc");
            writer.string(it->doc);
        }
        if (!it->color.isEmpty()) {
            writer.key(u"color");
            writer.string(it->color);
        }
        writeExtras(writer, it->extras);
        writer.endObject();
    }
    writer.endObject();
    writeExtras(writer, registry.extras);
    writer.endObject();
    return writer.finish();
}

QByteArray writePreset(const Preset& preset)
{
    JsonWriter writer;
    writer.beginObject();
    writeHeader(writer, FORMAT_PRESET, PRESET_FORMAT_VERSION);
    writeExtras(writer, preset.content);
    writer.endObject();
    return writer.finish();
}

void writeTransform(JsonWriter& writer, const Transform& transform)
{
    writer.beginObject();
    if (transform.pos != Point{}) {
        writer.key(u"pos");
        writePoint(writer, transform.pos);
    }
    if (transform.rot != 0) {
        writer.key(u"rot");
        writer.number(transform.rot);
    }
    if (transform.scale != Point{1, 1}) {
        writer.key(u"scale");
        writePoint(writer, transform.scale);
    }
    if (transform.flipX || transform.flipY) {
        writer.key(u"flip");
        writer.booleanArray({transform.flipX, transform.flipY});
    }
    writeExtras(writer, transform.extras);
    writer.endObject();
}

void writeEntity(JsonWriter& writer, const Entity& entity)
{
    writer.beginObject();
    if (!entity.id.isEmpty()) {
        writer.key(u"id");
        writer.string(entity.id);
    }
    if (!entity.classes.isEmpty()) {
        writer.key(u"classes");
        writer.stringArray(entity.classes);
    }
    if (!entity.active) {
        writer.key(u"active");
        writer.boolean(false);
    }
    if (!entity.visible) {
        writer.key(u"visible");
        writer.boolean(false);
    }
    if (entity.space) {
        writer.key(u"space");
        writer.string(enumName(*entity.space));
    }
    if (entity.parallax) {
        writer.key(u"parallax");
        writePoint(writer, *entity.parallax);
    }
    if (entity.fit) {
        writer.key(u"fit");
        writer.string(enumName(*entity.fit));
    }
    if (entity.transform != Transform{}) {
        writer.key(u"transform");
        writeTransform(writer, entity.transform);
    }
    if (entity.at != Anchor{}) {
        writer.key(u"at");
        writer.value(QJsonArray{lengthToJson(entity.at.x), lengthToJson(entity.at.y)});
    }
    if (entity.size) {
        writer.key(u"size");
        writer.value(
            QJsonArray{lengthToJson(entity.size->width), lengthToJson(entity.size->height)});
    }
    if (entity.pivot) {
        writer.key(u"pivot");
        writePoint(writer, *entity.pivot);
    }
    if (entity.area != Area::Viewport) {
        writer.key(u"area");
        writer.string(enumName(entity.area));
    }
    if (entity.image) {
        const Image& image = *entity.image;
        writer.key(u"image");
        writer.beginObject();
        if (image.isStandalone()) {
            writer.key(u"asset");
            writer.string(image.asset);
        }
        else {
            writer.key(u"sheet");
            writer.string(image.sheet);
            writer.key(image.isAnimation() ? u"animation" : u"sprite");
            writer.string(image.isAnimation() ? image.animation : image.sprite);
        }
        if (image.mode != ImageMode::Stretch) {
            writer.key(u"mode");
            writer.string(enumName(image.mode));
        }
        writeExtras(writer, image.extras);
        writer.endObject();
    }
    if (entity.fill) {
        writer.key(u"fill");
        writer.string(entity.fill->name());
    }
    if (entity.tint != Color{}) {
        writer.key(u"tint");
        writer.string(entity.tint.name());
    }
    if (entity.tintMode != TintMode::Mix) {
        writer.key(u"tint_mode");
        writer.string(enumName(entity.tintMode));
    }
    if (entity.clip) {
        writer.key(u"clip");
        writer.boolean(true);
    }
    if (entity.container) {
        const Container& container = *entity.container;
        writer.key(u"container");
        writer.beginObject();
        writer.key(u"type");
        writer.string(enumName(container.type));
        writer.key(u"tile_size");
        writer.numberArray({container.tileSize.width, container.tileSize.height});
        if (container.tilePivot != Container{}.tilePivot) {
            writer.key(u"tile_pivot");
            writePoint(writer, container.tilePivot);
        }
        writeExtras(writer, container.extras);
        writer.endObject();
    }
    if (entity.text) {
        const Text& text = *entity.text;
        writer.key(u"text");
        writer.beginObject();
        writer.key(u"font");
        writer.string(text.font);
        writer.key(u"string");
        writer.string(text.string);
        if (!text.key.isEmpty()) {
            writer.key(u"key");
            writer.string(text.key);
        }
        if (text.size != Text{}.size) {
            writer.key(u"size");
            writer.number(text.size);
        }
        if (text.color != Color{}) {
            writer.key(u"color");
            writer.string(text.color.name());
        }
        if (text.align != Text{}.align) {
            writer.key(u"align");
            writePoint(writer, text.align);
        }
        if (text.wrap != TextWrap::Word) {
            writer.key(u"wrap");
            writer.string(enumName(text.wrap));
        }
        if (text.lineHeight != Text{}.lineHeight) {
            writer.key(u"line_height");
            writer.number(text.lineHeight);
        }
        writeExtras(writer, text.extras);
        writer.endObject();
    }
    if (entity.video) {
        const Video& video = *entity.video;
        writer.key(u"video");
        writer.beginObject();
        writer.key(u"clip");
        writer.string(video.clip);
        if (video.loop) {
            writer.key(u"loop");
            writer.boolean(true);
        }
        if (video.autoplay) {
            writer.key(u"autoplay");
            writer.boolean(true);
        }
        if (video.mode != VideoMode::Stretch) {
            writer.key(u"mode");
            writer.string(enumName(video.mode));
        }
        writeExtras(writer, video.extras);
        writer.endObject();
    }
    if (entity.audio) {
        const Audio& audio = *entity.audio;
        writer.key(u"audio");
        writer.beginObject();
        writer.key(u"clip");
        writer.string(audio.clip);
        if (audio.volume != Audio{}.volume) {
            writer.key(u"volume");
            writer.number(audio.volume);
        }
        if (audio.loop) {
            writer.key(u"loop");
            writer.boolean(true);
        }
        if (audio.autoplay) {
            writer.key(u"autoplay");
            writer.boolean(true);
        }
        if (audio.radius) {
            writer.key(u"radius");
            writer.number(*audio.radius);
        }
        writeExtras(writer, audio.extras);
        writer.endObject();
    }
    if (!entity.props.isEmpty()) {
        writer.key(u"props");
        writer.value(entity.props);
    }
    if (entity.locks || !entity.editorExtras.isEmpty()) {
        writer.key(u"editor");
        writer.beginObject();
        if (entity.locks) {
            QStringList locks;
            for (const auto& entry : enumNames(Lock{})) {
                if (entity.locks.testFlag(entry.value)) {
                    locks << QLatin1StringView(entry.name);
                }
            }
            writer.key(u"locks");
            writer.stringArray(locks);
        }
        writeExtras(writer, entity.editorExtras);
        writer.endObject();
    }
    writeExtras(writer, entity.extras);
    if (!entity.children.empty()) {
        writer.key(u"children");
        writer.beginArray();
        for (const Entity& child : entity.children) {
            writeEntity(writer, child);
        }
        writer.endArray();
    }
    writer.endObject();
}

QByteArray writeScene(const Scene& scene)
{
    JsonWriter writer;
    writer.beginObject();
    writeHeader(writer, FORMAT_SCENE, SCENE_FORMAT_VERSION);
    writer.key(u"size");
    writer.numberArray(
        {static_cast<double>(scene.size.width), static_cast<double>(scene.size.height)});
    if (scene.fit) {
        writer.key(u"fit");
        writer.string(enumName(*scene.fit));
    }
    writer.key(u"entities");
    writer.beginArray();
    for (const Entity& entity : scene.entities) {
        writeEntity(writer, entity);
    }
    writer.endArray();
    writeExtras(writer, scene.extras);
    writer.endObject();
    return writer.finish();
}

QByteArray writeModifier(const Modifier& modifier)
{
    JsonWriter writer;
    writer.beginObject();
    writeHeader(writer, FORMAT_MODIFIER);
    writer.key(u"blocking");
    writer.boolean(modifier.blocking);
    writer.key(u"skippable");
    writer.boolean(modifier.skippable);
    if (modifier.priority != 0) {
        writer.key(u"priority");
        writer.integer(modifier.priority);
    }
    const OnEnd& onEnd = modifier.onEnd;
    if (onEnd != OnEnd{}) {
        writer.key(u"on_end");
        writer.beginObject();
        writer.key(u"mode");
        writer.string(enumName(onEnd.mode));
        if (onEnd.blendMs != 0) {
            writer.key(u"blend_ms");
            writer.integer(onEnd.blendMs);
        }
        if (!onEnd.restoreFollow) {
            writer.key(u"restore_follow");
            writer.boolean(false);
        }
        writeExtras(writer, onEnd.extras);
        writer.endObject();
    }
    writer.key(u"actions");
    writer.value(modifier.actions);
    writeExtras(writer, modifier.extras);
    writer.endObject();
    return writer.finish();
}

}  // namespace

QString FileError::toString() const
{
    if (line > 0) {
        return QStringLiteral("%1:%2:%3: %4").arg(file).arg(line).arg(column).arg(message);
    }
    return QStringLiteral("%1: %2").arg(file, message);
}

Entity readEntity(ObjectReader& reader)
{
    return readEntityFields(reader);
}

QJsonObject entityToJson(const Entity& entity)
{
    JsonWriter writer;
    writeEntity(writer, entity);
    return parseJsonObject(writer.finish());
}

namespace paths {

QString scene(const QString& sceneId)
{
    return QStringLiteral("scenes/%1/scene.json").arg(sceneId);
}

QString modifier(const QString& sceneId, const QString& modifierId)
{
    return QStringLiteral("scenes/%1/modifiers/%2.json").arg(sceneId, modifierId);
}

QString preset(const QString& presetId)
{
    return QStringLiteral("presets/%1.json").arg(presetId);
}

}  // namespace paths

bool isProjectFile(QStringView path)
{
    return path == paths::MANIFEST || path == paths::FLOW || path == paths::CLASSES ||
           PRESET_PATH.matchView(path).hasMatch() || SCENE_PATH.matchView(path).hasMatch() ||
           MODIFIER_PATH.matchView(path).hasMatch();
}

ParseResult parseProject(const ProjectFiles& files)
{
    ParseResult result;
    QList<FileError>& errors = result.errors;
    Project project;

    const auto manifestFile = files.find(paths::MANIFEST);
    if (manifestFile == files.end()) {
        errors << FileError{paths::MANIFEST, QStringLiteral("not found; this folder is not a "
                                                            "Shoal project")};
        return result;
    }
    if (auto manifest =
            readFile<Manifest>(paths::MANIFEST, manifestFile->second, errors, readManifest)) {
        project.manifest = std::move(*manifest);
    }

    const auto flowFile = files.find(paths::FLOW);
    if (flowFile == files.end()) {
        errors << FileError{paths::FLOW, QStringLiteral("not found")};
    }
    else if (auto flow = readFile<Flow>(paths::FLOW, flowFile->second, errors, readFlow)) {
        project.flow = std::move(*flow);
    }

    if (const auto classesFile = files.find(paths::CLASSES); classesFile != files.end()) {
        project.classes =
            readFile<ClassRegistry>(paths::CLASSES, classesFile->second, errors, readClasses);
    }

    // Scenes before modifiers, so each modifier finds its host scene.
    const Format1 format1{project.manifest.designResolution};
    QMap<QString, QMap<QString, QString>> format1Layers;  // by scene, for its modifiers
    for (const auto& [path, bytes] : files) {
        if (const auto match = SCENE_PATH.match(path); match.hasMatch()) {
            const QString sceneId = match.captured(1);
            if (!checkPathId(path, sceneId, QStringLiteral("scene"), errors)) {
                continue;
            }
            std::optional<QMap<QString, QString>> layerIds;
            auto scene = readFile<Scene>(path, bytes, errors, [&](ObjectReader& reader) {
                return readScene(reader, format1, layerIds);
            });
            if (scene) {
                project.scenes.insert(sceneId, std::move(*scene));
            }
            if (layerIds) {
                format1Layers.insert(sceneId, *layerIds);
            }
        }
        else if (const auto presetMatch = PRESET_PATH.match(path); presetMatch.hasMatch()) {
            const QString presetId = presetMatch.captured(1);
            if (!checkPathId(path, presetId, QStringLiteral("preset"), errors)) {
                continue;
            }
            auto preset = readFile<Preset>(path, bytes, errors, [&](ObjectReader& reader) {
                return readPreset(reader, format1);
            });
            if (preset) {
                project.presets.insert(presetId, std::move(*preset));
            }
        }
    }
    for (const auto& [path, bytes] : files) {
        const auto match = MODIFIER_PATH.match(path);
        if (!match.hasMatch()) {
            continue;
        }
        const QString sceneId = match.captured(1);
        const QString modifierId = match.captured(2);
        if (!checkPathId(path, modifierId, QStringLiteral("modifier"), errors)) {
            continue;
        }
        const bool hostFound = files.contains(paths::scene(sceneId));
        if (!hostFound) {
            errors << FileError{path, QStringLiteral("its scene folder has no scene.json")};
            continue;
        }
        auto modifier = readFile<Modifier>(path, bytes, errors, readModifier);
        const auto host = project.scenes.find(sceneId);
        if (modifier && format1Layers.contains(sceneId)) {
            // Its host had layers: its selectors name them as groups now.
            modifier->actions = renameLayerTerms(modifier->actions, format1Layers.value(sceneId));
        }
        if (modifier && host != project.scenes.end()) {
            host->modifiers.insert(modifierId, std::move(*modifier));
        }
    }

    if (errors.isEmpty()) {
        result.project = std::move(project);
    }
    return result;
}

ProjectFiles serializeProject(const Project& project)
{
    ProjectFiles files;
    files[paths::MANIFEST] = writeManifest(project.manifest);
    files[paths::FLOW] = writeFlow(project.flow);
    if (project.classes) {
        files[paths::CLASSES] = writeClasses(*project.classes);
    }
    for (auto it = project.presets.begin(); it != project.presets.end(); ++it) {
        files[paths::preset(it.key())] = writePreset(it.value());
    }
    for (auto scene = project.scenes.begin(); scene != project.scenes.end(); ++scene) {
        files[paths::scene(scene.key())] = writeScene(scene.value());
        for (auto it = scene->modifiers.begin(); it != scene->modifiers.end(); ++it) {
            files[paths::modifier(scene.key(), it.key())] = writeModifier(it.value());
        }
    }
    return files;
}

}  // namespace shoal::core
