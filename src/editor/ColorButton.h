#pragma once

#include <QToolButton>

#include "core/Model.h"

namespace shoal::editor {

/// A swatch of a color with alpha; clicking it asks for another one.
class ColorButton : public QToolButton {
    Q_OBJECT
public:
    explicit ColorButton(QWidget* parent = nullptr);

    core::Color color() const { return m_color; }
    /// Shows `color`, without emitting colorChosen().
    void setColor(const core::Color& color);

signals:
    void colorChosen(const core::Color& color);

private:
    void choose();

    core::Color m_color;
};

}  // namespace shoal::editor
