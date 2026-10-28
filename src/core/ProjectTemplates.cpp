#include "core/ProjectTemplates.h"

namespace shoal::core {

namespace {

const QString MAIN_SCENE = QStringLiteral("main");

ClassRegistry builtInClasses()
{
    ClassRegistry registry;
    registry.classes.insert(QStringLiteral("camera_bounds"),
                            {QStringLiteral("Region; cameras stay inside the union of these"),
                             QStringLiteral("#30a0e0"),
                             {}});
    registry.classes.insert(
        QStringLiteral("main"),
        {QStringLiteral("Camera; the one the scene is seen through"), QString(), {}});
    registry.classes.insert(QStringLiteral("spawn"),
                            {QStringLiteral("Mark; where a carried entity enters the scene"),
                             QStringLiteral("#40c070"),
                             {}});
    registry.classes.insert(QStringLiteral("trigger"),
                            {QStringLiteral("Region; the engine fires an event on enter/exit"),
                             QStringLiteral("#e0a030"),
                             {}});
    return registry;
}

}  // namespace

Scene newScene(const Manifest& manifest)
{
    Scene scene;
    scene.size = manifest.designResolution;
    scene.layers.push_back({QStringLiteral("world"), Space::World, {1, 1}, std::nullopt, {}});
    scene.layers.push_back({QStringLiteral("hud"), Space::Screen, {1, 1}, std::nullopt, {}});

    Entity camera;
    camera.id = QStringLiteral("camera");
    camera.type = EntityType::Camera;
    camera.layer = QStringLiteral("world");
    camera.classes = {QStringLiteral("main")};
    camera.transform.pos = {scene.size.width / 2.0, scene.size.height / 2.0};
    scene.entities.push_back(std::move(camera));
    return scene;
}

Project newProject(const QString& name, ProjectTemplate projectTemplate)
{
    Project project;
    project.manifest.name = name;
    switch (projectTemplate) {
        case ProjectTemplate::Default:
            break;
        case ProjectTemplate::PixelArt:
            project.manifest.designResolution = {640, 360};
            project.manifest.fit = Fit::Letterbox;
            project.manifest.integerScale = true;
            break;
    }
    project.manifest.start = MAIN_SCENE;
    project.classes = builtInClasses();
    project.scenes.insert(MAIN_SCENE, newScene(project.manifest));
    project.flow.nodePositions.insert(MAIN_SCENE, {0, 0});
    return project;
}

}  // namespace shoal::core
