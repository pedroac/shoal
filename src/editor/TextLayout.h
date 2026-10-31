#pragma once

#include <QFont>
#include <QSizeF>
#include <QString>
#include <vector>

#include "core/Model.h"

namespace shoal::editor {

/// A text part laid out as the editor previews it: with Qt, in the font's family, breaking
/// lines at a width when it wraps. Each line is placed in the text's box by the text's
/// alignment, so a centered text centers every line. An engine may break lines a little
/// differently; nothing else depends on it.
struct TextLayout {
    struct Line {
        QString text;
        double width = 0;
        double baseline = 0;  ///< From the top of the block.
    };

    QFont font;
    /// No family: the font isn't loaded, so the application's font stands in.
    bool missing = false;
    std::vector<Line> lines;
    QSizeF size;  ///< The block's: its widest line, and its lines' height.

    /// Lays `text` out in `family` (empty for none), its lines no wider than `width` when it
    /// wraps.
    static TextLayout of(const core::Text& text, const QString& family, double width);
    /// Where the block sits in `rect`, by the text's alignment.
    QPointF origin(const core::Text& text, const QRectF& rect) const;
    /// Where `line` starts in a block at `origin`, on its baseline.
    QPointF lineStart(const core::Text& text, const Line& line, QPointF origin) const;
};

}  // namespace shoal::editor
