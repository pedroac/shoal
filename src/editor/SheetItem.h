#pragma once

#include <QString>
#include <optional>

#include "core/Model.h"

class QMimeData;

namespace shoal::editor {

/// A sprite or an animation of a sprat sheet, or a standalone image, as the asset browser
/// hands it to a canvas: dragged, or activated to place at the middle of the view.
struct SheetItem {
    enum class Kind { Sprite, Animation, Image };

    Kind kind = Kind::Sprite;
    QString sheet;  ///< The sheet's alias, or the standalone image's.
    QString name;   ///< A sprite's path, or an animation's name; empty for an image.

    /// The MIME type of a drag from the sheet browser.
    static constexpr const char* MIME_TYPE = "application/x-shoal-sheet-item";

    /// A new entity drawing this item, with no id yet.
    core::Entity toEntity() const;
    /// The image drawing this item, stretched.
    core::Image toImage() const;
    /// What `image` draws.
    static SheetItem fromImage(const core::Image& image);
    QMimeData* toMimeData() const;
    /// The item a drag carries, or nothing if it carries none.
    static std::optional<SheetItem> fromMimeData(const QMimeData* data);

    bool operator==(const SheetItem&) const = default;
};

}  // namespace shoal::editor
