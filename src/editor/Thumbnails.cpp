#include "editor/Thumbnails.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <algorithm>

#include "assets/SheetCache.h"
#include "assets/SpriteSheet.h"
#include "editor/AssetItem.h"

namespace shoal::editor {

QIcon spriteThumbnail(const assets::Sheet& sheet, const assets::Sprite& sprite,
                      const QPixmap& atlas, bool flipX, bool flipY, int size, double pixelRatio)
{
    const assets::SpritePlacement placement =
        assets::placeSprite(sprite, 1.0 / sheet.scale, flipX, flipY);
    const QRectF bounds = placement.bounds;
    if (atlas.isNull() || bounds.isEmpty()) {
        return {};
    }
    const int pixels = int(size * pixelRatio);
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    const double factor = std::min(pixels / bounds.width(), pixels / bounds.height());
    painter.translate(pixels / 2.0, pixels / 2.0);
    painter.scale(factor, factor);
    painter.translate(-bounds.center());
    painter.setTransform(placement.transform, true);
    painter.drawPixmap(placement.target, atlas, placement.source);
    painter.end();
    pixmap.setDevicePixelRatio(pixelRatio);
    return QIcon(pixmap);
}

QIcon fontThumbnail(const QString& family, int size, double pixelRatio)
{
    const int pixels = int(size * pixelRatio);
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    QFont font(family);
    font.setPixelSize(pixels * 3 / 5);
    painter.setFont(font);
    painter.setPen(QApplication::palette().color(QPalette::Text));
    painter.drawText(QRect(0, 0, pixels, pixels), Qt::AlignCenter, QStringLiteral("Aa"));
    painter.end();
    pixmap.setDevicePixelRatio(pixelRatio);
    return QIcon(pixmap);
}

QIcon itemThumbnail(const assets::SheetCache& sheets, const AssetItem& item, int size,
                    double pixelRatio)
{
    const std::shared_ptr<const assets::Sheet> sheet =
        item.isImage() ? sheets.sheet(item.alias) : nullptr;
    if (!sheet || sheet->sprites.empty()) {
        return {};
    }
    const assets::Sprite* sprite = nullptr;
    bool flipX = false;
    bool flipY = false;
    if (item.kind == AssetItem::Kind::Image) {
        sprite = &sheet->sprites.front();
    }
    else if (item.kind == AssetItem::Kind::Sprite) {
        sprite = sheet->sprite(item.name);
    }
    else if (const assets::Animation* animation = sheet->animation(item.name);
             animation && !animation->frames.empty() &&
             size_t(animation->frames.front()) < sheet->sprites.size()) {
        sprite = &sheet->sprites[size_t(animation->frames.front())];
        flipX = animation->flipX;
        flipY = animation->flipY;
    }
    if (!sprite || size_t(sprite->atlas) >= sheet->atlases.size()) {
        return {};
    }
    return spriteThumbnail(*sheet, *sprite,
                           QPixmap::fromImage(sheet->atlases[size_t(sprite->atlas)]), flipX, flipY,
                           size, pixelRatio);
}

}  // namespace shoal::editor
