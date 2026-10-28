#pragma once

#include <QPointer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QDoubleSpinBox;

namespace shoal::editor {

class FlowScene;

/// The flow's selected scene: its id, whether the game starts there, its size and its
/// fit. Each edit is one undo step. Renaming it renames its folder and every reference.
class SceneSettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SceneSettingsPanel(QWidget* parent = nullptr);

    /// Shows `flow`'s selected scene; null shows nothing.
    void setFlowScene(FlowScene* flow);

    QLabel* summary() const { return m_summary; }
    QLineEdit* idField() const { return m_idField; }
    QCheckBox* startBox() const { return m_startBox; }
    QDoubleSpinBox* widthBox() const { return m_widthBox; }
    QDoubleSpinBox* heightBox() const { return m_heightBox; }
    /// The first entry is the project's fit; then each fit, as written in the files.
    QComboBox* fitBox() const { return m_fitBox; }
    /// Why the last edit couldn't be applied, or empty.
    QString message() const;

public slots:
    /// Renames the scene to the id field; false, with a message, if it can't be its id.
    bool applyId();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refresh();
    void showMessage(const QString& message);
    /// The one selected scene, or empty.
    QString selected() const;
    void setStart(bool start);
    void applySettings();

    QPointer<FlowScene> m_flow;
    QLabel* m_summary;
    QLineEdit* m_idField;
    QCheckBox* m_startBox;
    QDoubleSpinBox* m_widthBox;
    QDoubleSpinBox* m_heightBox;
    QComboBox* m_fitBox;
    QLabel* m_message;
};

}  // namespace shoal::editor
