#include "core/Model.h"

namespace shoal::core {

namespace {

bool isIdStart(QChar c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || c == u'_';
}

bool isIdPart(QChar c)
{
    return isIdStart(c) || (c >= u'0' && c <= u'9') || c == u'-';
}

constexpr EnumName<Fit> FIT_NAMES[] = {
    {Fit::Expand, "expand"},
    {Fit::Letterbox, "letterbox"},
    {Fit::Crop, "crop"},
    {Fit::Stretch, "stretch"},
};

constexpr EnumName<ExportTarget> EXPORT_TARGET_NAMES[] = {
    {ExportTarget::Raw, "raw"},
    {ExportTarget::Json, "json"},
};

constexpr EnumName<Space> SPACE_NAMES[] = {
    {Space::World, "world"},
    {Space::Screen, "screen"},
};

constexpr EnumName<EntityType> ENTITY_TYPE_NAMES[] = {
    {EntityType::Empty, "empty"},         {EntityType::Group, "group"},
    {EntityType::Sprite, "sprite"},       {EntityType::Animation, "animation"},
    {EntityType::NineSlice, "nineslice"}, {EntityType::Text, "text"},
    {EntityType::Rect, "rect"},           {EntityType::Mark, "mark"},
    {EntityType::Path, "path"},           {EntityType::Region, "region"},
    {EntityType::Camera, "camera"},
};

constexpr EnumName<TransitionMode> TRANSITION_MODE_NAMES[] = {
    {TransitionMode::Replace, "replace"},
    {TransitionMode::Push, "push"},
    {TransitionMode::PushModifier, "push_modifier"},
    {TransitionMode::Pop, "pop"},
};

constexpr EnumName<Underlying> UNDERLYING_NAMES[] = {
    {Underlying::Paused, "paused"},
    {Underlying::Running, "running"},
    {Underlying::Hidden, "hidden"},
};

constexpr EnumName<RestoreMode> RESTORE_MODE_NAMES[] = {
    {RestoreMode::Commit, "commit"},
    {RestoreMode::Restore, "restore"},
};

}  // namespace

bool isValidId(QStringView id)
{
    if (id.isEmpty() || !isIdStart(id.front())) {
        return false;
    }
    for (QChar c : id) {
        if (!isIdPart(c)) {
            return false;
        }
    }
    return true;
}

bool idsCollideAsFileNames(QStringView a, QStringView b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

std::span<const EnumName<Fit>> enumNames(Fit)
{
    return FIT_NAMES;
}
std::span<const EnumName<ExportTarget>> enumNames(ExportTarget)
{
    return EXPORT_TARGET_NAMES;
}
std::span<const EnumName<Space>> enumNames(Space)
{
    return SPACE_NAMES;
}
std::span<const EnumName<EntityType>> enumNames(EntityType)
{
    return ENTITY_TYPE_NAMES;
}
std::span<const EnumName<TransitionMode>> enumNames(TransitionMode)
{
    return TRANSITION_MODE_NAMES;
}
std::span<const EnumName<Underlying>> enumNames(Underlying)
{
    return UNDERLYING_NAMES;
}
std::span<const EnumName<RestoreMode>> enumNames(RestoreMode)
{
    return RESTORE_MODE_NAMES;
}

}  // namespace shoal::core
