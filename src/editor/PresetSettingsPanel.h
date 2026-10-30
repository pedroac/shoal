#pragma once

#include <QPointer>
#include <QWidget>
#include <optional>

#include "core/Model.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

class ColorButton;

/// A preset's settings: its id (its file's name), its name, the classes it gives the
/// entities it creates, its color in the editor and its description. A built-in preset is
/// shown read-only, with Customize, which copies it into the project to be changed; a
/// customized one has Revert, which brings back Shoal's. Each edit is one undo step.
class PresetSettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit PresetSettingsPanel(QWidget* parent = nullptr);

    /// Shows `document`'s preset `presetId`; null, or an empty id, shows nothing.
    void setPreset(core::ProjectDocument* document, const QString& presetId);
    QString preset() const { return m_preset; }

    QLabel* summary() const { return m_summary; }
    QLineEdit* idField() const { return m_idField; }
    QLineEdit* nameField() const { return m_nameField; }
    QLineEdit* classesField() const { return m_classesField; }
    QCheckBox* swatchBox() const { return m_swatchBox; }
    ColorButton* swatchButton() const { return m_swatchButton; }
    QLineEdit* docField() const { return m_docField; }
    QPushButton* customizeButton() const { return m_customizeButton; }
    QPushButton* revertButton() const { return m_revertButton; }
    /// Why the last edit couldn't be applied, or empty.
    QString message() const;

public slots:
    /// Renames the preset to the id field; false, with a message, if it can't be its id.
    bool applyId();
    void applyName();
    /// Applies the classes field; false, with a message, if a class isn't a valid id.
    bool applyClasses();
    /// Sets its color in the editor; null for none.
    void setSwatch(std::optional<core::Color> swatch);
    void applyDoc();
    /// Copies a built-in preset into the project, to be changed.
    void customize();
    /// Removes the project's copy of a built-in preset.
    void revert();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refresh();
    void showMessage(const QString& message);
    /// Pushes the preset with `key` set to `value`, or removed for an empty one.
    void change(const QString& key, const QJsonValue& value, const QString& text);

    QPointer<core::ProjectDocument> m_document;
    QString m_preset;
    QLabel* m_summary;
    QLineEdit* m_idField;
    QLineEdit* m_nameField;
    QLineEdit* m_classesField;
    QCheckBox* m_swatchBox;
    ColorButton* m_swatchButton;
    QLineEdit* m_docField;
    QPushButton* m_customizeButton;
    QPushButton* m_revertButton;
    QLabel* m_message;
};

}  // namespace shoal::editor
