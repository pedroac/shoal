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
/// user or a later roadmap step writes is lost on save. The entity parts the editor doesn't
/// model yet (views, layouts, shapes, paths, text) and modifier actions round-trip that
/// way until it does.
namespace shoal::core {

/// The versions of the source file formats this Shoal writes. It reads older ones too, and
/// migrates them: scene format 1 had layers and entity types, and preset format 1 too, with
/// a `tint` for its swatch; scene and preset format 2 placed entities by `align` and `cell`
/// where format 3 has units; manifest format 1 declared sheets in a `sheets` key, which
/// format 2 moved under `assets`.
inline constexpr int FORMAT_VERSION = 1;
inline constexpr int MANIFEST_FORMAT_VERSION = 2;
inline constexpr int SCENE_FORMAT_VERSION = 3;
inline constexpr int PRESET_FORMAT_VERSION = 3;

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

/// An entity's size, in pixels: zero on an axis makes its rect a point on that axis.
struct SizeF {
    double width = 0;
    double height = 0;
    bool operator==(const SizeF&) const = default;
};

/// A color with alpha, written `#rrggbb`, or `#rrggbbaa` when it isn't opaque.
struct Color {
    quint8 red = 255;
    quint8 green = 255;
    quint8 blue = 255;
    quint8 alpha = 255;
    bool operator==(const Color&) const = default;

    /// Reads `#rrggbb` or `#rrggbbaa`, in either case; null for anything else.
    static std::optional<Color> fromName(QStringView name);
    /// `#rrggbb`, or `#rrggbbaa` when it isn't opaque, in lowercase.
    QString name() const;
    /// Channel by channel, as tints combine.
    Color multiplied(const Color& other) const;
};

enum class Fit { Expand, Letterbox, Crop, Stretch };
enum class ExportTarget { Raw, Json };
enum class Space { World, Screen };
enum class Area { Viewport, Safe };
enum class ImageMode { Stretch, NineSlice };
enum class TintMode { Mix, Inherit, Override };
enum class ContainerType { Tiled };
enum class TextWrap { Word, None };
enum class VideoMode { Stretch, Fit, Fill };
enum class TransitionMode { Replace, Push, PushModifier, Pop };
enum class Underlying { Paused, Running, Hidden };
enum class RestoreMode { Commit, Restore };
/// The kinds of asset a project declares, each under a key of its own in `assets`.
enum class AssetKind { Sheet, Image, Font, Audio, Video };

// --- project.shoal.json ---

struct ExportSettings {
    QString dir = QStringLiteral("export");
    QList<ExportTarget> targets{ExportTarget::Raw, ExportTarget::Json};
    bool bundle = false;
    QJsonObject extras;
    bool operator==(const ExportSettings&) const = default;
};

/// The files the project's scenes use, each kind by alias. An alias is unique across all
/// kinds, so it names an asset alone.
struct Assets {
    QMap<QString, QString> sheets;  ///< Alias -> path of a .sprat file.
    QMap<QString, QString> images;  ///< Alias -> path of a standalone image.
    QMap<QString, QString> fonts;
    QMap<QString, QString> audio;
    QMap<QString, QString> video;
    QJsonObject extras;
    bool operator==(const Assets&) const = default;

    static constexpr AssetKind KINDS[] = {AssetKind::Sheet, AssetKind::Image, AssetKind::Font,
                                          AssetKind::Audio, AssetKind::Video};
    QMap<QString, QString>& of(AssetKind kind);
    const QMap<QString, QString>& of(AssetKind kind) const;
    /// The kind `alias` is declared in, or null.
    std::optional<AssetKind> kindOf(const QString& alias) const;
    /// Every alias, of every kind.
    QStringList aliases() const;
};

/// The kind of asset a file is, by its extension (in any case), or null for a file Shoal
/// doesn't know.
std::optional<AssetKind> assetKindForFile(QStringView path);

struct Manifest {
    QString name;
    Size designResolution{1920, 1080};
    Fit fit = Fit::Expand;
    bool integerScale = false;
    QString start;  ///< Scene id; empty when the project has no start scene yet.
    Assets assets;
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

/// The smallest scale the editor sets: a zero scale can't be undone by scaling.
constexpr double MIN_SCALE = 1e-3;

struct Transform {
    Point pos;
    double rot = 0;
    Point scale{1, 1};
    bool flipX = false;
    bool flipY = false;
    QJsonObject extras;
    bool operator==(const Transform&) const = default;
};

/// A sprat sprite, by the sheet's alias and the sprite's path (unique in a sheet, unlike
/// its name), a sprat animation, by name, or a standalone image, by its alias. It fills its
/// entity's rect.
struct Image {
    QString sheet;      ///< Empty for a standalone image.
    QString sprite;     ///< The sprite's path; empty for an animation or a standalone image.
    QString animation;  ///< The animation's name; empty for a sprite or a standalone image.
    QString asset;      ///< A standalone image's alias; empty for a sheet's.
    ImageMode mode = ImageMode::Stretch;
    QJsonObject extras;
    bool operator==(const Image&) const = default;

    bool isAnimation() const { return !animation.isEmpty(); }
    bool isStandalone() const { return !asset.isEmpty(); }
    /// The alias of the asset it draws from: its sheet's, or its own.
    const QString& source() const { return isStandalone() ? asset : sheet; }
};

/// A cell of a tiled container's grid: its column and row, counted from the top-left one.
struct Cell {
    int column = 0;
    int row = 0;
    bool operator==(const Cell&) const = default;
};

/// What a length on one axis counts: pixels, a percentage of the parent's rect on that
/// axis, or its parent's tiles (`□`), when the parent is a tiled container.
enum class Unit { Px, Percent, Cell };

/// A length on one axis, in its unit. Written as a number for pixels, and as a string with
/// its unit otherwise: `"50%"`, `"3□"`.
struct Length {
    double value = 0;
    Unit unit = Unit::Px;
    bool operator==(const Length&) const = default;

    static Length px(double value) { return {value, Unit::Px}; }
    /// `"50%"`, `"3□"` or `"12px"`, for the editor to show.
    QString toString() const;
};

/// An entity's size, each side in its own unit: 0 or more pixels, a percentage of the
/// parent's rect, or a number of its tiles.
struct Extent {
    Length width;
    Length height;
    bool operator==(const Extent&) const = default;

    static Extent px(SizeF size) { return {Length::px(size.width), Length::px(size.height)}; }
    bool isPx() const { return width.unit == Unit::Px && height.unit == Unit::Px; }
};

/// Where an entity's position is measured from, on each axis: the parent's origin (pixels,
/// with a value of 0), a percentage of the parent's rect from its top-left corner, or a
/// tile of a tiled parent, a whole number from 0, at the tile's pivot. Its `pos` adds an
/// offset in pixels.
struct Anchor {
    Length x;
    Length y;
    bool operator==(const Anchor&) const = default;

    bool isPx() const { return x.unit == Unit::Px && y.unit == Unit::Px; }
    Length& axis(int index) { return index == 0 ? x : y; }
    const Length& axis(int index) const { return index == 0 ? x : y; }
};

/// How an entity places its children. A tiled one lays a grid of tiles over its rect, as
/// many as fit, and puts each child's origin on its cell's `tilePivot`.
struct Container {
    ContainerType type = ContainerType::Tiled;
    SizeF tileSize{32, 32};     ///< Each side more than 0.
    Point tilePivot{0.5, 0.5};  ///< The point of each tile, as fractions of it.
    QJsonObject extras;
    bool operator==(const Container&) const = default;
};

/// Text in its entity's rect, in a font the manifest declares. The engine lays it out; the
/// editor's preview, with Qt, is approximate.
struct Text {
    QString font;           ///< A `fonts` alias.
    QString string;         ///< As written; `\n` breaks a line.
    QString key;            ///< A localization key, or empty; `string` is then the fallback.
    double size = 32;       ///< The font's em, in pixels.
    Color color;            ///< Multiplied by the entity's tint.
    Point align{0.5, 0.5};  ///< Where the text sits in the rect, as a pivot.
    TextWrap wrap = TextWrap::Word;
    double lineHeight = 1.2;  ///< A multiple of `size`.
    QJsonObject extras;
    bool operator==(const Text&) const = default;
};

/// A video in its entity's rect, from a `video` alias. Shoal doesn't decode it: the editor
/// draws the rect with the clip's name.
struct Video {
    QString clip;  ///< A `video` alias.
    bool loop = false;
    bool autoplay = false;
    VideoMode mode = VideoMode::Stretch;  ///< How the frames fill the rect.
    QJsonObject extras;
    bool operator==(const Video&) const = default;
};

/// A sound source at its entity's origin, from an `audio` alias. Shoal doesn't decode it.
struct Audio {
    QString clip;       ///< An `audio` alias.
    double volume = 1;  ///< 0 to 1; a hint.
    bool loop = false;
    bool autoplay = false;
    /// Heard from the origin, fading out at it, in pixels; null is heard everywhere.
    std::optional<double> radius;
    QJsonObject extras;
    bool operator==(const Audio&) const = default;
};

/// A node of a scene's tree: the same fields for every entity, and the parts it has, which
/// say what it is. Parts the editor doesn't model yet (`view`, `layout`, `shape`, `path`)
/// are kept in `extras`.
struct Entity {
    QString id;  ///< Optional: only entities something refers to need one.
    QStringList classes;
    bool active = true;
    bool visible = true;
    std::optional<Space> space;     ///< Set: it and its descendants are in that space.
    std::optional<Point> parallax;  ///< Set: it and its descendants scroll by it.
    std::optional<Fit> fit;         ///< Overrides the viewport fit, with `space`.
    Transform transform;
    /// Its rect's size; null for automatic: an image's natural size, or no rect.
    std::optional<Extent> size;
    /// The fraction of its rect on its origin; null for automatic (see entityPivot()).
    std::optional<Point> pivot;
    /// Where `transform.pos` is measured from in the parent (`at` in the file).
    Anchor at;
    Area area = Area::Viewport;
    std::optional<Image> image;
    std::optional<Color> fill;
    Color tint;
    TintMode tintMode = TintMode::Mix;
    bool clip = false;
    /// How it places its children; null places them freely.
    std::optional<Container> container;
    std::optional<Text> text;
    std::optional<Video> video;
    std::optional<Audio> audio;
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

/// Whether `id` is a valid id for a scene, modifier, preset, entity, class or transition:
/// `[A-Za-z_][A-Za-z0-9_-]*`.
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
std::span<const EnumName<Area>> enumNames(Area);
std::span<const EnumName<ImageMode>> enumNames(ImageMode);
std::span<const EnumName<TintMode>> enumNames(TintMode);
std::span<const EnumName<ContainerType>> enumNames(ContainerType);
std::span<const EnumName<TextWrap>> enumNames(TextWrap);
std::span<const EnumName<VideoMode>> enumNames(VideoMode);
std::span<const EnumName<TransitionMode>> enumNames(TransitionMode);
std::span<const EnumName<Underlying>> enumNames(Underlying);
std::span<const EnumName<RestoreMode>> enumNames(RestoreMode);
/// As the keys of `assets`: `sheets`, `images`, `fonts`, `audio`, `video`.
std::span<const EnumName<AssetKind>> enumNames(AssetKind);

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
