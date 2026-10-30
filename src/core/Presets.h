#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

#include "core/Model.h"

namespace shoal::core {

/// What a preset file says, once read. Presets stay as written in the model (see Preset),
/// and are read when they are used, so a preset the editor can't read never stops a
/// project from opening.
struct PresetDefinition {
    QString name;         ///< What the editor shows; the preset's id when the file has none.
    QString doc;          ///< A tooltip.
    QString swatch;       ///< The editor's color for it, "#rrggbb", or empty.
    QStringList classes;  ///< Given to each top-level entity it creates.
    /// The entities it creates; their positions are relative to the point it is placed at.
    std::vector<Entity> entities;
};

struct PresetRead {
    std::optional<PresetDefinition> definition;  ///< Null when the preset can't be read.
    QString error;  ///< Names the value at fault by its path, as file errors do.
};

/// Reads a preset's `name`, `doc`, `swatch`, `classes` and `entities`.
PresetRead readPresetDefinition(const QString& presetId, const Preset& preset);

/// Shoal's own presets, by id: Trigger Region, Camera Bounds, Spawn Mark and Camera, whose
/// view is the size of the design resolution.
QMap<QString, Preset> builtInPresets(Size designResolution = {1920, 1080});

/// The presets a project can create entities from: the built-in ones and the project's
/// own, which win over a built-in one with the same id.
QMap<QString, Preset> availablePresets(const Project& project);

/// Whether `id` can name a new project preset: a valid id no preset, built-in or not, has.
/// `ignoring` is the preset being renamed, if any.
bool isPresetIdAvailable(const Project& project, QStringView id, QStringView ignoring = {});
/// `base` if it can name a new preset, otherwise `base_2`, `base_3`, ...
QString uniquePresetId(const Project& project, const QString& base);
/// A preset named `name` creating `entities`, as a preset file says it.
Preset presetOf(const QString& name, const std::vector<Entity>& entities);
/// Whether a preset is one of Shoal's own: built in, and not customized by the project.
bool isBuiltInPreset(const Project& project, const QString& id);

/// The entities `preset` creates in `scene`, around `position` in the coordinates of the
/// parent they go into: copies with fresh ids, unique in the scene, nothing linked to the
/// preset.
std::vector<Entity> instantiatePreset(const PresetDefinition& preset, const Scene& scene,
                                      Point position);

}  // namespace shoal::core
