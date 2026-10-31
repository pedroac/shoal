#pragma once

#include <QJsonObject>
#include <QPointer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QToolButton;

namespace shoal::core {
class ProjectDocument;
}

namespace shoal::editor {

/// A transition type's file, edited: its id, name, description and preview, and its
/// options, each with its name, type, default, range, choices and description. A built-in
/// type is read-only until the project customizes it (Customize copies it into the
/// project; Revert goes back to Shoal's). Each edit is one undo step, and one that would
/// make a file the editor can't read is refused, with why.
class TransitionTypePanel : public QWidget {
    Q_OBJECT
public:
    explicit TransitionTypePanel(QWidget* parent = nullptr);

    void setDocument(core::ProjectDocument* document);
    /// Shows the type `typeId`, following its renames; empty shows nothing.
    void setType(const QString& typeId);
    QString type() const { return m_type; }

    QLabel* summary() const { return m_summary; }
    QPushButton* customizeButton() const { return m_customizeButton; }
    QPushButton* revertButton() const { return m_revertButton; }
    QLineEdit* idField() const { return m_idField; }
    QLineEdit* nameField() const { return m_nameField; }
    QLineEdit* descriptionField() const { return m_descriptionField; }
    /// A GIF, relative to the project's transitions folder.
    QLineEdit* previewField() const { return m_previewField; }
    QListWidget* optionList() const { return m_optionList; }
    QToolButton* addOptionButton() const { return m_addOptionButton; }
    QToolButton* removeOptionButton() const { return m_removeOptionButton; }
    QToolButton* moveOptionUpButton() const { return m_moveUpButton; }
    QToolButton* moveOptionDownButton() const { return m_moveDownButton; }
    /// The current option's fields.
    QLineEdit* optionNameField() const { return m_optionName; }
    /// Its items' data are the option types' names.
    QComboBox* optionTypeBox() const { return m_optionType; }
    QLineEdit* optionDefaultField() const { return m_optionDefault; }
    QLineEdit* optionMinField() const { return m_optionMin; }
    QLineEdit* optionMaxField() const { return m_optionMax; }
    QCheckBox* optionIntegerBox() const { return m_optionInteger; }
    /// Separated by commas.
    QLineEdit* optionChoicesField() const { return m_optionChoices; }
    QLineEdit* optionDescriptionField() const { return m_optionDescription; }
    /// Why the last edit couldn't be applied, or empty.
    QString message() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refresh();
    void showOption();
    /// Whether the project has its own file for the type, which can be edited.
    bool isEditable() const;
    QJsonObject content() const;
    /// Replaces the type's file with `content`, as `text`; refused, with why, if the editor
    /// couldn't read it.
    bool apply(const QJsonObject& content, const QString& text);
    /// Sets or, with an undefined value, removes `key` of the type.
    void setKey(const QString& key, const QJsonValue& value);
    /// The options as the file has them, and the current one's index, or -1.
    QJsonArray options() const;
    int currentOption() const;
    /// Applies `option` in place of the current one.
    void setOption(const QJsonObject& option, const QString& text);
    void applyId();
    void applyOptionField(QObject* field);
    void chooseOptionType(int index);
    void addOption();
    void removeOption();
    void moveOption(int by);
    void browsePreview();
    void showMessage(const QString& message);

    QPointer<core::ProjectDocument> m_document;
    QString m_type;
    int m_option = -1;  ///< The current option, kept across the list being remade.
    QLabel* m_summary;
    QPushButton* m_customizeButton;
    QPushButton* m_revertButton;
    QLineEdit* m_idField;
    QLineEdit* m_nameField;
    QLineEdit* m_descriptionField;
    QLineEdit* m_previewField;
    QToolButton* m_browsePreviewButton;
    QListWidget* m_optionList;
    QToolButton* m_addOptionButton;
    QToolButton* m_removeOptionButton;
    QToolButton* m_moveUpButton;
    QToolButton* m_moveDownButton;
    QWidget* m_optionForm;
    QFormLayout* m_optionLayout;
    QLineEdit* m_optionName;
    QComboBox* m_optionType;
    QLineEdit* m_optionDefault;
    QLineEdit* m_optionMin;
    QLineEdit* m_optionMax;
    QCheckBox* m_optionInteger;
    QLineEdit* m_optionChoices;
    QLineEdit* m_optionDescription;
    QLabel* m_message;
};

}  // namespace shoal::editor
