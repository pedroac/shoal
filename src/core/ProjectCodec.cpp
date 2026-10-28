#include "core/ProjectCodec.h"

#include <QRegularExpression>
#include <algorithm>
#include <limits>

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

Manifest readManifest(ObjectReader& reader)
{
    readHeader(reader, FORMAT_PROJECT);
    Manifest manifest;
    manifest.name = reader.string(QStringLiteral("name"));
    manifest.designResolution = reader.size(QStringLiteral("design_resolution"));
    manifest.fit = reader.enumValue(QStringLiteral("fit"), Fit::Expand);
    manifest.integerScale = reader.boolean(QStringLiteral("integer_scale"), false);
    manifest.start = reader.optionalId(QStringLiteral("start"));

    const QString sheetsKey = QStringLiteral("sheets");
    manifest.sheets = reader.stringMap(sheetsKey);
    checkIds(reader, sheetsKey, manifest.sheets.keys());

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

Preset readPreset(ObjectReader& reader)
{
    readHeader(reader, FORMAT_PRESET);
    return Preset{reader.extras()};
}

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

/// Where an entity is read: top-level entities name their layer, children inherit their
/// parent's, and a detached entity (a preset's) takes the layer it is placed on.
enum class EntityPlace { TopLevel, Child, Detached };

Entity readEntity(ObjectReader& reader, EntityPlace place)
{
    const QString typeKey = QStringLiteral("type");
    const QString layerKey = QStringLiteral("layer");
    const QString classesKey = QStringLiteral("classes");

    Entity entity;
    entity.id = reader.optionalId(QStringLiteral("id"));
    if (!reader.has(typeKey)) {
        reader.failMessage(typeKey, QStringLiteral("missing"));
    }
    entity.type = reader.enumValue(typeKey, EntityType::Empty);
    if (place == EntityPlace::TopLevel) {
        entity.layer = reader.id(layerKey);
    }
    else if (reader.has(layerKey)) {
        reader.failMessage(layerKey, place == EntityPlace::Child
                                         ? QStringLiteral("children inherit their parent's layer")
                                         : QStringLiteral("it goes on the layer it is placed on"));
    }
    entity.classes = reader.stringList(classesKey);
    checkIds(reader, classesKey, entity.classes);
    entity.active = reader.boolean(QStringLiteral("active"), true);
    entity.visible = reader.boolean(QStringLiteral("visible"), true);
    if (reader.has(QStringLiteral("sprite"))) {
        ObjectReader sprite = reader.child(QStringLiteral("sprite"));
        entity.sprite = SpriteRef{
            sprite.id(QStringLiteral("sheet")), sprite.string(QStringLiteral("path")), {}};
        entity.sprite->extras = sprite.extras();
    }
    if (reader.has(QStringLiteral("animation"))) {
        ObjectReader animation = reader.child(QStringLiteral("animation"));
        entity.animation = AnimationRef{
            animation.id(QStringLiteral("sheet")), animation.string(QStringLiteral("name")), {}};
        entity.animation->extras = animation.extras();
    }
    ObjectReader transform = reader.child(QStringLiteral("transform"));
    entity.transform = readTransform(transform);
    entity.props = reader.object(QStringLiteral("props"));
    for (ObjectReader& child : reader.objectArray(QStringLiteral("children"))) {
        entity.children.push_back(readEntity(child, EntityPlace::Child));
    }
    entity.extras = reader.extras();
    return entity;
}

Scene readScene(ObjectReader& reader)
{
    readHeader(reader, FORMAT_SCENE);
    Scene scene;
    scene.size = reader.size(QStringLiteral("size"));
    scene.fit = reader.optionalEnum<Fit>(QStringLiteral("fit"));
    for (ObjectReader& item : reader.objectArray(QStringLiteral("layers"))) {
        Layer layer;
        layer.id = item.id(QStringLiteral("id"));
        layer.space = item.enumValue(QStringLiteral("space"), Space::World);
        layer.parallax = item.point(QStringLiteral("parallax"), {1, 1});
        layer.fit = item.optionalEnum<Fit>(QStringLiteral("fit"));
        layer.extras = item.extras();
        scene.layers.push_back(std::move(layer));
    }
    for (ObjectReader& item : reader.objectArray(QStringLiteral("entities"))) {
        scene.entities.push_back(readEntity(item, EntityPlace::TopLevel));
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

void writeHeader(JsonWriter& writer, const QString& format)
{
    writer.key(u"format");
    writer.string(format);
    writer.key(u"version");
    writer.integer(FORMAT_VERSION);
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
    writeHeader(writer, FORMAT_PROJECT);
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
    writer.key(u"sheets");
    writeStringMap(writer, manifest.sheets);

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
    writeHeader(writer, FORMAT_PRESET);
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
    writer.key(u"type");
    writer.string(enumName(entity.type));
    if (!entity.layer.isEmpty()) {
        writer.key(u"layer");
        writer.string(entity.layer);
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
    if (entity.sprite) {
        writer.key(u"sprite");
        writer.beginObject();
        writer.key(u"sheet");
        writer.string(entity.sprite->sheet);
        writer.key(u"path");
        writer.string(entity.sprite->path);
        writeExtras(writer, entity.sprite->extras);
        writer.endObject();
    }
    if (entity.animation) {
        writer.key(u"animation");
        writer.beginObject();
        writer.key(u"sheet");
        writer.string(entity.animation->sheet);
        writer.key(u"name");
        writer.string(entity.animation->name);
        writeExtras(writer, entity.animation->extras);
        writer.endObject();
    }
    if (entity.transform != Transform{}) {
        writer.key(u"transform");
        writeTransform(writer, entity.transform);
    }
    if (!entity.props.isEmpty()) {
        writer.key(u"props");
        writer.value(entity.props);
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
    writeHeader(writer, FORMAT_SCENE);
    writer.key(u"size");
    writer.numberArray(
        {static_cast<double>(scene.size.width), static_cast<double>(scene.size.height)});
    if (scene.fit) {
        writer.key(u"fit");
        writer.string(enumName(*scene.fit));
    }
    writer.key(u"layers");
    writer.beginArray();
    for (const Layer& layer : scene.layers) {
        writer.beginObject();
        writer.key(u"id");
        writer.string(layer.id);
        writer.key(u"space");
        writer.string(enumName(layer.space));
        if (layer.parallax != Point{1, 1}) {
            writer.key(u"parallax");
            writePoint(writer, layer.parallax);
        }
        if (layer.fit) {
            writer.key(u"fit");
            writer.string(enumName(*layer.fit));
        }
        writeExtras(writer, layer.extras);
        writer.endObject();
    }
    writer.endArray();
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

Entity readDetachedEntity(ObjectReader& reader)
{
    return readEntity(reader, EntityPlace::Detached);
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
    for (const auto& [path, bytes] : files) {
        if (const auto match = SCENE_PATH.match(path); match.hasMatch()) {
            const QString sceneId = match.captured(1);
            if (!checkPathId(path, sceneId, QStringLiteral("scene"), errors)) {
                continue;
            }
            if (auto scene = readFile<Scene>(path, bytes, errors, readScene)) {
                project.scenes.insert(sceneId, std::move(*scene));
            }
        }
        else if (const auto presetMatch = PRESET_PATH.match(path); presetMatch.hasMatch()) {
            const QString presetId = presetMatch.captured(1);
            if (!checkPathId(path, presetId, QStringLiteral("preset"), errors)) {
                continue;
            }
            if (auto preset = readFile<Preset>(path, bytes, errors, readPreset)) {
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
