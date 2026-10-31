#include "editor/SheetItem.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>

namespace shoal::editor {

namespace {
const QString KIND = QStringLiteral("kind");
const QString SHEET = QStringLiteral("sheet");
const QString NAME = QStringLiteral("name");
const QString SPRITE = QStringLiteral("sprite");
const QString ANIMATION = QStringLiteral("animation");
const QString IMAGE = QStringLiteral("image");
}  // namespace

QString SheetItem::path() const
{
    switch (kind) {
        case Kind::Sprite:
            return QCoreApplication::translate("SheetItem", "Sprites / %1 / Sprites / %2")
                .arg(sheet, name);
        case Kind::Animation:
            return QCoreApplication::translate("SheetItem", "Sprites / %1 / Animations / %2")
                .arg(sheet, name);
        case Kind::Image:
            return QCoreApplication::translate("SheetItem", "Images / %1").arg(sheet);
    }
    return {};
}

core::Entity SheetItem::toEntity() const
{
    core::Entity entity;
    entity.image = toImage();
    return entity;
}

core::Image SheetItem::toImage() const
{
    core::Image image;
    if (kind == Kind::Image) {
        image.asset = sheet;
        return image;
    }
    image.sheet = sheet;
    if (kind == Kind::Animation) {
        image.animation = name;
    }
    else {
        image.sprite = name;
    }
    return image;
}

SheetItem SheetItem::fromImage(const core::Image& image)
{
    return image.isStandalone()  ? SheetItem{Kind::Image, image.asset, {}}
           : image.isAnimation() ? SheetItem{Kind::Animation, image.sheet, image.animation}
                                 : SheetItem{Kind::Sprite, image.sheet, image.sprite};
}

QMimeData* SheetItem::toMimeData() const
{
    const QJsonObject object{
        {KIND, kind == Kind::Animation ? ANIMATION
               : kind == Kind::Image   ? IMAGE
                                       : SPRITE},
        {SHEET, sheet},
        {NAME, name},
    };
    auto* data = new QMimeData;
    data->setData(QLatin1StringView(MIME_TYPE),
                  QJsonDocument(object).toJson(QJsonDocument::Compact));
    data->setText(kind == Kind::Image ? sheet : name);
    return data;
}

std::optional<SheetItem> SheetItem::fromMimeData(const QMimeData* data)
{
    if (!data || !data->hasFormat(QLatin1StringView(MIME_TYPE))) {
        return std::nullopt;
    }
    const QJsonObject object =
        QJsonDocument::fromJson(data->data(QLatin1StringView(MIME_TYPE))).object();
    const QString kind = object.value(KIND).toString();
    SheetItem item;
    item.kind = kind == ANIMATION ? Kind::Animation : kind == IMAGE ? Kind::Image : Kind::Sprite;
    item.sheet = object.value(SHEET).toString();
    item.name = object.value(NAME).toString();
    if ((kind != SPRITE && kind != ANIMATION && kind != IMAGE) || item.sheet.isEmpty() ||
        item.name.isEmpty() != (item.kind == Kind::Image)) {
        return std::nullopt;
    }
    return item;
}

}  // namespace shoal::editor
