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

QJsonObject entityJson(const char* id, const char* type, const char* className)
{
    QJsonObject entity{{QStringLiteral("type"), QLatin1StringView(type)},
                       {QStringLiteral("classes"), QJsonArray{QLatin1StringView(className)}}};
    if (id) {
        entity.insert(QStringLiteral("id"), QLatin1StringView(id));
    }
    return entity;
}

Preset builtIn(const char* name, const char* doc, const char* tint, QJsonObject entity)
{
    QJsonObject content{{QStringLiteral("name"), QLatin1StringView(name)},
                        {QStringLiteral("doc"), QLatin1StringView(doc)},
                        {QStringLiteral("entities"), QJsonArray{entity}}};
    if (tint) {
        content.insert(QStringLiteral("tint"), QLatin1StringView(tint));
    }
    return Preset{content};
}

QJsonObject withShape(QJsonObject entity, QJsonArray rect)
{
    entity.insert(QStringLiteral("shape"), QJsonObject{{QStringLiteral("rect"), rect}});
    return entity;
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
        definition.tint = reader.optionalString(QStringLiteral("tint"));
        if (!definition.tint.isEmpty() && !COLOR.match(definition.tint).hasMatch()) {
            reader.fail(QStringLiteral("tint"), QStringLiteral("a color such as \"#e0a030\""),
                        definition.tint);
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
            definition.entities.push_back(readDetachedEntity(item));
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

const QMap<QString, Preset>& builtInPresets()
{
    static const QMap<QString, Preset> presets{
        {QStringLiteral("trigger_region"),
         builtIn("Trigger Region", "A region the engine fires an event on enter/exit of", "#e0a030",
                 withShape(entityJson("trigger", "region", "trigger"),
                           QJsonArray{-64, -64, 128, 128}))},
        {QStringLiteral("camera_bounds"),
         builtIn("Camera Bounds", "A region cameras stay inside, with the others like it",
                 "#30a0e0",
                 withShape(entityJson(nullptr, "region", "camera_bounds"),
                           QJsonArray{0, 0, 1920, 1080}))},
        {QStringLiteral("spawn_mark"),
         builtIn("Spawn Mark", "Where a carried entity enters the scene", "#40c070",
                 entityJson("spawn", "mark", "spawn"))},
        {QStringLiteral("camera"),
         builtIn("Camera", "A camera; the one tagged .main is the scene's", nullptr,
                 entityJson("camera", "camera", "main"))},
    };
    return presets;
}

QMap<QString, Preset> availablePresets(const Project& project)
{
    QMap<QString, Preset> presets = builtInPresets();
    presets.insert(project.presets);
    return presets;
}

std::vector<Entity> instantiatePreset(const PresetDefinition& preset, const Scene& scene,
                                      const QString& layer, Point position)
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
        entity.layer = layer;
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
