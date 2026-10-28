#pragma once

#include <QString>

#include "core/Model.h"

namespace shoal::core {

/// Starting points for a new project. They only set defaults; nothing about a template is
/// saved with the project.
enum class ProjectTemplate {
    Default,   ///< 1920x1080, `expand`.
    PixelArt,  ///< 640x360, `letterbox` with `integer_scale`.
};

/// A new project from `projectTemplate`, with one scene, `main`, as its start scene, and
/// declarations for the classes Shoal's built-in presets use.
Project newProject(const QString& name, ProjectTemplate projectTemplate);

/// A new scene the size of the design resolution, with a `world` and a `hud` layer and a
/// `#camera` tagged `.main` at its center.
Scene newScene(const Manifest& manifest);

}  // namespace shoal::core
