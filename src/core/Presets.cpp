#include "core/Presets.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <functional>

#include "core/JsonReader.h"
#include "core/ProjectCodec.h"
#include "core/SceneTree.h"

namespace shoal::core {

namespace {

const QRegularExpression COLOR(QStringLiteral("^#[0-9A-Fa-f]{6}$"));

QJsonObject entityJson(const char* id, const char* className)
{
    QJsonObject entity{{QStringLiteral("classes"), QJsonArray{QLatin1StringView(className)}}};
    if (id) {
        entity.insert(QStringLiteral("id"), QLatin1StringView(id));
    }
    return entity;
}

/// `entity` with a rect of `size`, `pivot` on its origin.
QJsonObject withRect(QJsonObject entity, QJsonArray size, QJsonArray pivot = {})
{
    entity.insert(QStringLiteral("size"), size);
    if (!pivot.isEmpty()) {
        entity.insert(QStringLiteral("pivot"), pivot);
    }
    return entity;
}

Preset builtIn(const char* name, const char* doc, const char* swatch, QJsonObject entity)
{
    QJsonObject content{{QStringLiteral("name"), QLatin1StringView(name)},
                        {QStringLiteral("doc"), QLatin1StringView(doc)},
                        {QStringLiteral("entities"), QJsonArray{entity}}};
    if (swatch) {
        content.insert(QStringLiteral("swatch"), QLatin1StringView(swatch));
    }
    return Preset{content};
}

}  // namespace

PresetRead readPresetDefinition(const QString& presetId, const Preset& preset)
{
    PresetRead result;
    try {
        ObjectReader reader(preset.content, QString());
        PresetDefinition definition;
        definition.name = reader.optionalString(QStringLiteral("name"));
        if (definition.name.isEmpty()) {
            definition.name = presetId;
        }
        definition.doc = reader.optionalString(QStringLiteral("doc"));
        definition.swatch = reader.optionalString(QStringLiteral("swatch"));
        if (!definition.swatch.isEmpty() && !COLOR.match(definition.swatch).hasMatch()) {
            reader.fail(QStringLiteral("swatch"), QStringLiteral("a color such as \"#e0a030\""),
                        definition.swatch);
        }
        definition.classes = reader.stringList(QStringLiteral("classes"));
        for (qsizetype i = 0; i < definition.classes.size(); ++i) {
            if (!isValidId(definition.classes[i])) {
                reader.failMessage(QStringLiteral("classes[%1]").arg(i),
                                   QStringLiteral("%1 is not a valid id")
                                       .arg(describeJson(definition.classes[i])));
            }
        }
        for (ObjectReader& item : reader.objectArray(QStringLiteral("entities"))) {
            definition.entities.push_back(readEntity(item));
        }
        if (definition.entities.empty()) {
            reader.failMessage(QStringLiteral("entities"),
                               QStringLiteral("a preset creates at least one entity"));
        }
        result.definition = std::move(definition);
    }
    catch (const FormatError& error) {
        result.error = error.message;
    }
    return result;
}

QMap<QString, Preset> builtInPresets(Size designResolution)
{
    QJsonObject camera =
        withRect(entityJson("camera", "main"),
                 QJsonArray{designResolution.width, designResolution.height}, QJsonArray{0.5, 0.5});
    camera.insert(QStringLiteral("view"), QJsonObject{});
    return {
        {QStringLiteral("trigger_region"),
         builtIn("Trigger Region", "A region the engine fires an event on enter/exit of", "#e0a030",
                 withRect(entityJson("trigger", "trigger"), QJsonArray{128, 128},
                          QJsonArray{0.5, 0.5}))},
        {QStringLiteral("camera_bounds"),
         builtIn("Camera Bounds", "A region cameras stay inside, with the others like it",
                 "#30a0e0",
                 withRect(entityJson(nullptr, "camera_bounds"), QJsonArray{1920, 1080}))},
        {QStringLiteral("spawn_mark"),
         builtIn("Spawn Mark", "Where a carried entity enters the scene", "#40c070",
                 entityJson("spawn", "spawn"))},
        {QStringLiteral("camera"),
         builtIn("Camera", "A camera; the one tagged .main is the scene's", nullptr, camera)},
    };
}

QMap<QString, Preset> availablePresets(const Project& project)
{
    QMap<QString, Preset> presets = builtInPresets(project.manifest.designResolution);
    presets.insert(project.presets);
    return presets;
}

std::vector<Entity> instantiatePreset(const PresetDefinition& preset, const Scene& scene,
                                      Point position)
{
    QSet<QString> taken;
    forEachEntity(scene, [&](const Entity& entity, const EntityPath&) {
        if (!entity.id.isEmpty()) {
            taken.insert(entity.id);
        }
    });
    // Fresh ids, unique among the scene's and the new ones: `spawn`, then `spawn_2`, ...
    const std::function<void(Entity&)> freshen = [&](Entity& entity) {
        if (!entity.id.isEmpty()) {
            QString id = entity.id;
            for (int n = 2; taken.contains(id); ++n) {
                id = QStringLiteral("%1_%2").arg(entity.id).arg(n);
            }
            entity.id = id;
            taken.insert(id);
        }
        for (Entity& child : entity.children) {
            freshen(child);
        }
    };

    std::vector<Entity> entities;
    for (Entity entity : preset.entities) {
        entity.transform.pos.x += position.x;
        entity.transform.pos.y += position.y;
        QStringList classes = preset.classes;
        for (const QString& name : std::as_const(entity.classes)) {
            if (!classes.contains(name)) {
                classes << name;
            }
        }
        entity.classes = classes;
        freshen(entity);
        entities.push_back(std::move(entity));
    }
    return entities;
}

}  // namespace shoal::core
