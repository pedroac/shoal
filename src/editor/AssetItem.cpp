#include "editor/AssetItem.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>

namespace shoal::editor {

namespace {
const QString KIND = QStringLiteral("kind");
const QString ALIAS = QStringLiteral("alias");
const QString NAME = QStringLiteral("name");

constexpr std::pair<AssetItem::Kind, const char*> KIND_NAMES[] = {
    {AssetItem::Kind::Sprite, "sprite"}, {AssetItem::Kind::Animation, "animation"},
    {AssetItem::Kind::Image, "image"},   {AssetItem::Kind::Font, "font"},
    {AssetItem::Kind::Audio, "audio"},   {AssetItem::Kind::Video, "video"},
};

bool hasName(AssetItem::Kind kind)
{
    return kind == AssetItem::Kind::Sprite || kind == AssetItem::Kind::Animation;
}
}  // namespace

core::AssetKind AssetItem::assetKind() const
{
    switch (kind) {
        case Kind::Sprite:
        case Kind::Animation:
            return core::AssetKind::Sheet;
        case Kind::Image:
            return core::AssetKind::Image;
        case Kind::Font:
            return core::AssetKind::Font;
        case Kind::Audio:
            return core::AssetKind::Audio;
        case Kind::Video:
            return core::AssetKind::Video;
    }
    return core::AssetKind::Sheet;
}

QString AssetItem::path() const
{
    switch (kind) {
        case Kind::Sprite:
            return QCoreApplication::translate("AssetItem", "Sprites / %1 / Sprites / %2")
                .arg(alias, name);
        case Kind::Animation:
            return QCoreApplication::translate("AssetItem", "Sprites / %1 / Animations / %2")
                .arg(alias, name);
        case Kind::Image:
            return QCoreApplication::translate("AssetItem", "Images / %1").arg(alias);
        case Kind::Font:
            return QCoreApplication::translate("AssetItem", "Fonts / %1").arg(alias);
        case Kind::Audio:
            return QCoreApplication::translate("AssetItem", "Audio / %1").arg(alias);
        case Kind::Video:
            return QCoreApplication::translate("AssetItem", "Video / %1").arg(alias);
    }
    return {};
}

core::Image AssetItem::toImage() const
{
    core::Image image;
    if (kind == Kind::Image) {
        image.asset = alias;
        return image;
    }
    image.sheet = alias;
    if (kind == Kind::Animation) {
        image.animation = name;
    }
    else {
        image.sprite = name;
    }
    return image;
}

AssetItem AssetItem::fromImage(const core::Image& image)
{
    return image.isStandalone()  ? AssetItem{Kind::Image, image.asset, {}}
           : image.isAnimation() ? AssetItem{Kind::Animation, image.sheet, image.animation}
                                 : AssetItem{Kind::Sprite, image.sheet, image.sprite};
}

QMimeData* AssetItem::toMimeData() const
{
    QString kindName;
    for (const auto& [value, written] : KIND_NAMES) {
        if (value == kind) {
            kindName = QLatin1StringView(written);
        }
    }
    const QJsonObject object{{KIND, kindName}, {ALIAS, alias}, {NAME, name}};
    auto* data = new QMimeData;
    data->setData(QLatin1StringView(MIME_TYPE),
                  QJsonDocument(object).toJson(QJsonDocument::Compact));
    data->setText(hasName(kind) ? name : alias);
    return data;
}

std::optional<AssetItem> AssetItem::fromMimeData(const QMimeData* data)
{
    if (!data || !data->hasFormat(QLatin1StringView(MIME_TYPE))) {
        return std::nullopt;
    }
    const QJsonObject object =
        QJsonDocument::fromJson(data->data(QLatin1StringView(MIME_TYPE))).object();
    const QString kindName = object.value(KIND).toString();
    std::optional<Kind> kind;
    for (const auto& [value, written] : KIND_NAMES) {
        if (kindName == QLatin1StringView(written)) {
            kind = value;
        }
    }
    AssetItem item;
    item.alias = object.value(ALIAS).toString();
    item.name = object.value(NAME).toString();
    if (!kind || item.alias.isEmpty() || item.name.isEmpty() == hasName(*kind)) {
        return std::nullopt;
    }
    item.kind = *kind;
    return item;
}

}  // namespace shoal::editor
