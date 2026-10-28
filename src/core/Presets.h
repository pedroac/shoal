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
    QString tint;         ///< "#rrggbb" for the editor's swatch, or empty.
    QStringList classes;  ///< Given to each top-level entity it creates.
    /// The entities it creates, without a layer; their positions are relative to the
    /// point it is placed at.
    std::vector<Entity> entities;
};

struct PresetRead {
    std::optional<PresetDefinition> definition;  ///< Null when the preset can't be read.
    QString error;  ///< Names the value at fault by its path, as file errors do.
};

/// Reads a preset's `name`, `doc`, `tint`, `classes` and `entities`.
PresetRead readPresetDefinition(const QString& presetId, const Preset& preset);

/// Shoal's own presets, by id: Trigger Region, Camera Bounds, Spawn Mark and Camera.
const QMap<QString, Preset>& builtInPresets();

/// The presets a project can create entities from: the built-in ones and the project's
/// own, which win over a built-in one with the same id.
QMap<QString, Preset> availablePresets(const Project& project);

/// The entities `preset` creates in `scene`, as top-level entities of `layer` around
/// `position`: copies with fresh ids, unique in the scene, nothing linked to the preset.
std::vector<Entity> instantiatePreset(const PresetDefinition& preset, const Scene& scene,
                                      const QString& layer, Point position);

}  // namespace shoal::core
