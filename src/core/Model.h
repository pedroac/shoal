#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <optional>
#include <span>
#include <vector>

/// The project model: plain values mirroring the source files described in docs/DESIGN.md.
///
/// Every object keeps the keys this version of Shoal doesn't model in `extras`, so nothing a
/// user or a later roadmap step writes is lost on save. Type-specific entity data (sprites,
/// shapes, camera settings, layout) and modifier actions round-trip that way until the
/// editor models them.
namespace shoal::core {

/// The version of every source file format this Shoal reads and writes.
inline constexpr int FORMAT_VERSION = 1;

struct Point {
    double x = 0;
    double y = 0;
    bool operator==(const Point&) const = default;
};

struct Size {
    int width = 0;
    int height = 0;
    bool operator==(const Size&) const = default;
};

enum class Fit { Expand, Letterbox, Crop, Stretch };
enum class ExportTarget { Raw, Json };
enum class Space { World, Screen };
enum class EntityType {
    Empty,
    Group,
    Sprite,
    Animation,
    NineSlice,
    Text,
    Rect,
    Mark,
    Path,
    Region,
    Camera,
};
enum class TransitionMode { Replace, Push, PushModifier, Pop };
enum class Underlying { Paused, Running, Hidden };
enum class RestoreMode { Commit, Restore };

// --- project.shoal.json ---

struct ExportSettings {
    QString dir = QStringLiteral("export");
    QList<ExportTarget> targets{ExportTarget::Raw, ExportTarget::Json};
    bool bundle = false;
    QJsonObject extras;
    bool operator==(const ExportSettings&) const = default;
};

struct Manifest {
    QString name;
    Size designResolution{1920, 1080};
    Fit fit = Fit::Expand;
    bool integerScale = false;
    QString start;                  ///< Scene id; empty when the project has no start scene yet.
    QMap<QString, QString> sheets;  ///< Alias -> path of a .sprat file.
    ExportSettings exportSettings;
    QJsonObject extras;
    bool operator==(const Manifest&) const = default;
};

// --- flow.json ---

struct StateKey {
    QString key;
    QString doc;
    QJsonObject extras;
    bool operator==(const StateKey&) const = default;
};

struct Trigger {
    QString event;             ///< Empty while the transition has no trigger yet.
    QStringList requirements;  ///< `requires` in the file.
    QJsonObject extras;
    bool operator==(const Trigger&) const = default;
};

struct Effect {
    QString type;
    int durationMs = 0;
    QString easing;  ///< Empty means the default, linear.
    QJsonObject params;
    QJsonObject extras;
    bool operator==(const Effect&) const = default;
};

struct Persist {
    QStringList state;
    QMap<QString, QString> entities;  ///< Carried entity id -> mark id in the destination.
    QJsonObject extras;
    bool operator==(const Persist&) const = default;
};

struct Transition {
    QString id;
    QString from;
    QString to;  ///< Empty for `pop` and `push_modifier`.
    TransitionMode mode = TransitionMode::Replace;
    QString modifier;                            ///< Only for `push_modifier`.
    Underlying underlying = Underlying::Paused;  ///< Only for `push`.
    Trigger trigger;
    std::optional<Effect> effect;
    Persist persist;
    QJsonObject extras;
    bool operator==(const Transition&) const = default;
};

struct Flow {
    std::vector<StateKey> state;          ///< Sorted by key.
    std::vector<Transition> transitions;  ///< Sorted by id.
    QMap<QString, Point> nodePositions;   ///< Scene id -> position in the flow view.
    QJsonObject editorExtras;             ///< Unmodeled keys of the `editor` block.
    QJsonObject extras;
    bool operator==(const Flow&) const = default;
};

// --- classes.json ---

struct ClassDeclaration {
    QString doc;
    QString color;  ///< "#rrggbb", or empty.
    QJsonObject extras;
    bool operator==(const ClassDeclaration&) const = default;
};

struct ClassRegistry {
    QMap<QString, ClassDeclaration> classes;
    QJsonObject extras;
    bool operator==(const ClassRegistry&) const = default;
};

// --- presets/<id>.json ---

/// Presets are kept as they are written until the preset editor models them.
struct Preset {
    QJsonObject content;  ///< Everything but `format` and `version`.
    bool operator==(const Preset&) const = default;
};

// --- scenes/<id>/scene.json ---

struct Layer {
    QString id;
    Space space = Space::World;
    Point parallax{1, 1};
    std::optional<Fit> fit;  ///< Overrides the scene's or the project's fit.
    QJsonObject extras;
    bool operator==(const Layer&) const = default;
};

struct Transform {
    Point pos;
    double rot = 0;
    Point scale{1, 1};
    bool flipX = false;
    bool flipY = false;
    QJsonObject extras;
    bool operator==(const Transform&) const = default;
};

struct Entity {
    QString id;  ///< Optional: only entities something refers to need one.
    EntityType type = EntityType::Empty;
    QString layer;  ///< Top-level entities only; children inherit their parent's layer.
    QStringList classes;
    bool active = true;
    bool visible = true;
    Transform transform;
    QJsonObject props;  ///< Kept as written until typed props are modeled.
    QJsonObject extras;
    std::vector<Entity> children;
    bool operator==(const Entity&) const = default;
};

// --- scenes/<id>/modifiers/<id>.json ---

struct OnEnd {
    RestoreMode mode = RestoreMode::Commit;
    int blendMs = 0;
    bool restoreFollow = true;
    QJsonObject extras;
    bool operator==(const OnEnd&) const = default;
};

struct Modifier {
    bool blocking = true;
    bool skippable = false;
    int priority = 0;
    OnEnd onEnd;
    QJsonValue actions = QJsonObject{{QStringLiteral("seq"), QJsonArray{}}};  ///< Kept as written.
    QJsonObject extras;
    bool operator==(const Modifier&) const = default;
};

struct Scene {
    Size size;
    std::optional<Fit> fit;  ///< Overrides the project's fit.
    std::vector<Layer> layers;
    std::vector<Entity> entities;
    QMap<QString, Modifier> modifiers;  ///< Modifier id (its file name) -> modifier.
    QJsonObject extras;
    bool operator==(const Scene&) const = default;
};

// --- the whole project ---

struct Project {
    Manifest manifest;
    Flow flow;
    std::optional<ClassRegistry> classes;  ///< Absent when the project has no classes.json.
    QMap<QString, Preset> presets;
    QMap<QString, Scene> scenes;  ///< Scene id (its folder name) -> scene.
    bool operator==(const Project&) const = default;
};

/// Whether `id` is a valid id for a scene, modifier, preset, layer, entity, class or
/// transition: `[A-Za-z_][A-Za-z0-9_-]*`.
bool isValidId(QStringView id);

/// Ids are compared without case where they name files, so a project stays valid on
/// case-insensitive file systems.
bool idsCollideAsFileNames(QStringView a, QStringView b);

// --- enum names, as written in the files ---

template <typename E>
struct EnumName {
    E value;
    const char* name;
};

std::span<const EnumName<Fit>> enumNames(Fit);
std::span<const EnumName<ExportTarget>> enumNames(ExportTarget);
std::span<const EnumName<Space>> enumNames(Space);
std::span<const EnumName<EntityType>> enumNames(EntityType);
std::span<const EnumName<TransitionMode>> enumNames(TransitionMode);
std::span<const EnumName<Underlying>> enumNames(Underlying);
std::span<const EnumName<RestoreMode>> enumNames(RestoreMode);

template <typename E>
QString enumName(E value)
{
    for (const auto& entry : enumNames(E{})) {
        if (entry.value == value) {
            return QString::fromLatin1(entry.name);
        }
    }
    return {};
}

template <typename E>
std::optional<E> enumFromName(QStringView name)
{
    for (const auto& entry : enumNames(E{})) {
        if (name == QLatin1StringView(entry.name)) {
            return entry.value;
        }
    }
    return std::nullopt;
}

}  // namespace shoal::core
