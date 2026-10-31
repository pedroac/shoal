#pragma once

#include <QString>
#include <optional>

#include "core/Model.h"

class QMimeData;

namespace shoal::editor {

/// An asset as the asset browser hands it to a canvas or to an inspector field, dragged or
/// activated to place at the middle of the view: a sprite or an animation of a sprat sheet,
/// a standalone image, a font, an audio clip or a video.
struct AssetItem {
    enum class Kind { Sprite, Animation, Image, Font, Audio, Video };

    Kind kind = Kind::Sprite;
    QString alias;  ///< The asset's: the sheet's, for a sprite or an animation.
    QString name;   ///< A sprite's path, or an animation's name; empty for the others.

    /// The MIME type of a drag from the asset browser.
    static constexpr const char* MIME_TYPE = "application/x-shoal-asset-item";

    /// Whether it is something an image draws: a sprite, an animation or an image.
    bool isImage() const
    {
        return kind == Kind::Sprite || kind == Kind::Animation || kind == Kind::Image;
    }
    /// The manifest's kind of the asset it is in: a sprite's is its sheet's.
    core::AssetKind assetKind() const;

    /// Where the asset browser lists it, e.g. "Sprites / hero / Animations / run",
    /// "Images / title", "Fonts / body", "Audio / door" or "Video / intro".
    QString path() const;
    /// The image drawing this item, stretched; for an image item only.
    core::Image toImage() const;
    /// What `image` draws.
    static AssetItem fromImage(const core::Image& image);
    QMimeData* toMimeData() const;
    /// The item a drag carries, or nothing if it carries none.
    static std::optional<AssetItem> fromMimeData(const QMimeData* data);

    bool operator==(const AssetItem&) const = default;
};

}  // namespace shoal::editor
