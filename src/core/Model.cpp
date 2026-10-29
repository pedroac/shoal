#include "core/Model.h"

#include <QLocale>

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

constexpr EnumName<Area> AREA_NAMES[] = {
    {Area::Viewport, "viewport"},
    {Area::Safe, "safe"},
};

constexpr EnumName<ImageMode> IMAGE_MODE_NAMES[] = {
    {ImageMode::Stretch, "stretch"},
    {ImageMode::NineSlice, "nineslice"},
};

constexpr EnumName<TintMode> TINT_MODE_NAMES[] = {
    {TintMode::Mix, "mix"},
    {TintMode::Inherit, "inherit"},
    {TintMode::Override, "override"},
};
constexpr EnumName<ContainerType> CONTAINER_TYPE_NAMES[] = {
    {ContainerType::Tiled, "tiled"},
};

int hexDigit(QChar c)
{
    if (c >= u'0' && c <= u'9') {
        return c.unicode() - u'0';
    }
    if (c >= u'a' && c <= u'f') {
        return c.unicode() - u'a' + 10;
    }
    if (c >= u'A' && c <= u'F') {
        return c.unicode() - u'A' + 10;
    }
    return -1;
}

quint8 multiplyChannel(quint8 a, quint8 b)
{
    return quint8((int(a) * int(b) + 127) / 255);
}

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

std::optional<Color> Color::fromName(QStringView name)
{
    if ((name.size() != 7 && name.size() != 9) || name.front() != u'#') {
        return std::nullopt;
    }
    quint8 channels[4] = {0, 0, 0, 255};
    for (qsizetype i = 1; i < name.size(); i += 2) {
        const int high = hexDigit(name[i]);
        const int low = hexDigit(name[i + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        channels[i / 2] = quint8(high * 16 + low);
    }
    return Color{channels[0], channels[1], channels[2], channels[3]};
}

QString Color::name() const
{
    QString text = QStringLiteral("#%1%2%3")
                       .arg(uint(red), 2, 16, QChar(u'0'))
                       .arg(uint(green), 2, 16, QChar(u'0'))
                       .arg(uint(blue), 2, 16, QChar(u'0'));
    if (alpha != 255) {
        text += QStringLiteral("%1").arg(uint(alpha), 2, 16, QChar(u'0'));
    }
    return text;
}

Color Color::multiplied(const Color& other) const
{
    return {multiplyChannel(red, other.red), multiplyChannel(green, other.green),
            multiplyChannel(blue, other.blue), multiplyChannel(alpha, other.alpha)};
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
std::span<const EnumName<Area>> enumNames(Area)
{
    return AREA_NAMES;
}
std::span<const EnumName<ImageMode>> enumNames(ImageMode)
{
    return IMAGE_MODE_NAMES;
}
std::span<const EnumName<TintMode>> enumNames(TintMode)
{
    return TINT_MODE_NAMES;
}
std::span<const EnumName<ContainerType>> enumNames(ContainerType)
{
    return CONTAINER_TYPE_NAMES;
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

QString Length::toString() const
{
    const QString number = QString::number(value, 'g', QLocale::FloatingPointShortest);
    switch (unit) {
        case Unit::Px:
            return number + QStringLiteral("px");
        case Unit::Percent:
            return number + u'%';
        case Unit::Cell:
            return number + QChar(0x25A1);
    }
    return number;
}

}  // namespace shoal::core
