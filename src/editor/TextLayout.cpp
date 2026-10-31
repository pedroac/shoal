#include "editor/TextLayout.h"

#include <QFontMetricsF>
#include <QRectF>
#include <QTextLayout>
#include <algorithm>
#include <cmath>

namespace shoal::editor {

namespace {
/// A line's width when it doesn't wrap: wider than any text, within what Qt's fixed point holds.
constexpr double NO_WRAP_WIDTH = 1e6;
}  // namespace

TextLayout TextLayout::of(const core::Text& text, const QString& family, double width)
{
    TextLayout layout;
    layout.missing = family.isEmpty();
    if (!layout.missing) {
        layout.font = QFont(family);
    }
    layout.font.setPixelSize(std::max(1, int(std::lround(text.size))));
    layout.font.setHintingPreference(QFont::PreferNoHinting);
    const QFontMetricsF metrics(layout.font);
    const double lineSpacing = text.size * text.lineHeight;
    // Half the leading above each line, half below, as CSS does.
    const double baseline =
        (lineSpacing - (metrics.ascent() + metrics.descent())) / 2 + metrics.ascent();
    const bool wraps = text.wrap == core::TextWrap::Word && width > 0;

    double widest = 0;
    const QStringList paragraphs = text.string.split(u'\n');
    for (const QString& paragraph : paragraphs) {
        QTextLayout lines(paragraph, layout.font);
        QTextOption option;
        option.setWrapMode(wraps ? QTextOption::WordWrap : QTextOption::NoWrap);
        lines.setTextOption(option);
        lines.beginLayout();
        for (QTextLine line = lines.createLine(); line.isValid(); line = lines.createLine()) {
            // Unwrapped, as wide as it needs: a line not given a width has no text yet.
            line.setLineWidth(wraps ? width : NO_WRAP_WIDTH);
            const QString part = paragraph.mid(line.textStart(), line.textLength());
            const double lineWidth = line.naturalTextWidth();
            layout.lines.push_back(
                {part, lineWidth, double(layout.lines.size()) * lineSpacing + baseline});
            widest = std::max(widest, lineWidth);
        }
        lines.endLayout();
        if (paragraph.isEmpty() && lines.lineCount() == 0) {
            layout.lines.push_back({{}, 0, double(layout.lines.size()) * lineSpacing + baseline});
        }
    }
    layout.size = QSizeF(widest, double(layout.lines.size()) * lineSpacing);
    return layout;
}

QPointF TextLayout::origin(const core::Text& text, const QRectF& rect) const
{
    return {rect.x() + (rect.width() - size.width()) * text.align.x,
            rect.y() + (rect.height() - size.height()) * text.align.y};
}

QPointF TextLayout::lineStart(const core::Text& text, const Line& line, QPointF origin) const
{
    return {origin.x() + (size.width() - line.width) * text.align.x, origin.y() + line.baseline};
}

}  // namespace shoal::editor
