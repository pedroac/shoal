#include "editor/SheetItem.h"

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
}  // namespace

core::Entity SheetItem::toEntity() const
{
    core::Image image;
    image.sheet = sheet;
    if (kind == Kind::Animation) {
        image.animation = name;
    }
    else {
        image.sprite = name;
    }
    core::Entity entity;
    entity.image = image;
    return entity;
}

QMimeData* SheetItem::toMimeData() const
{
    const QJsonObject object{
        {KIND, kind == Kind::Animation ? ANIMATION : SPRITE},
        {SHEET, sheet},
        {NAME, name},
    };
    auto* data = new QMimeData;
    data->setData(QLatin1StringView(MIME_TYPE),
                  QJsonDocument(object).toJson(QJsonDocument::Compact));
    data->setText(name);
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
    item.kind = kind == ANIMATION ? Kind::Animation : Kind::Sprite;
    item.sheet = object.value(SHEET).toString();
    item.name = object.value(NAME).toString();
    if ((kind != SPRITE && kind != ANIMATION) || item.sheet.isEmpty() || item.name.isEmpty()) {
        return std::nullopt;
    }
    return item;
}

}  // namespace shoal::editor
