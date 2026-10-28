#include "editor/SceneSettingsPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#include "core/ProjectDocument.h"
#include "core/SceneTree.h"
#include "editor/FlowScene.h"
#include "editor/FormFields.h"

namespace shoal::editor {

using core::Fit;

SceneSettingsPanel::SceneSettingsPanel(QWidget* parent)
    : QWidget(parent),
      m_summary(new QLabel(this)),
      m_idField(new QLineEdit(this)),
      m_startBox(new QCheckBox(tr("Start scene"), this)),
      m_widthBox(numberBox(this, QStringLiteral("sceneWidthBox"), MAX_SIZE, 0, 1)),
      m_heightBox(numberBox(this, QStringLiteral("sceneHeightBox"), MAX_SIZE, 0, 1)),
      m_fitBox(new QComboBox(this)),
      m_message(messageLabel(this, QStringLiteral("sceneSettingsMessage")))
{
    m_summary->setObjectName(QStringLiteral("sceneSummary"));
    m_summary->setWordWrap(true);
    m_idField->setObjectName(QStringLiteral("sceneIdField"));
    m_startBox->setObjectName(QStringLiteral("startSceneBox"));
    m_startBox->setToolTip(tr("The scene the game starts at; choose another to change it"));
    m_fitBox->setObjectName(QStringLiteral("sceneFitBox"));
    m_fitBox->setToolTip(tr("How the scene fits screens of another aspect ratio"));
    m_fitBox->addItem(QString());  // the project's, named in refresh()
    for (const auto& [fit, name] : core::enumNames(Fit{})) {
        m_fitBox->addItem(QString::fromLatin1(name), QVariant::fromValue(int(fit)));
    }
    m_widthBox->setMinimum(1);
    m_heightBox->setMinimum(1);
    m_widthBox->setPrefix(QStringLiteral("w "));
    m_heightBox->setPrefix(QStringLiteral("h "));

    auto* size = new QHBoxLayout;
    size->addWidget(m_widthBox);
    size->addWidget(m_heightBox);
    auto* form = new QFormLayout;
    form->addRow(tr("Id"), m_idField);
    form->addRow(QString(), m_startBox);
    form->addRow(tr("Size"), size);
    form->addRow(tr("Fit"), m_fitBox);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_summary);
    layout->addLayout(form);
    layout->addWidget(m_message);
    layout->addStretch();

    m_idField->installEventFilter(this);
    connect(m_idField, &QLineEdit::editingFinished, this, &SceneSettingsPanel::applyId);
    connect(m_startBox, &QCheckBox::clicked, this, &SceneSettingsPanel::setStart);
    connect(m_widthBox, &QDoubleSpinBox::valueChanged, this, &SceneSettingsPanel::applySettings);
    connect(m_heightBox, &QDoubleSpinBox::valueChanged, this, &SceneSettingsPanel::applySettings);
    connect(m_fitBox, &QComboBox::activated, this, &SceneSettingsPanel::applySettings);
    setFlowScene(nullptr);
}

void SceneSettingsPanel::setFlowScene(FlowScene* flow)
{
    if (m_flow) {
        disconnect(m_flow, nullptr, this, nullptr);
        disconnect(&m_flow->document(), nullptr, this, nullptr);
    }
    m_flow = flow;
    if (m_flow) {
        connect(m_flow, &QGraphicsScene::selectionChanged, this, [this] {
            showMessage({});
            refresh();
        });
        connect(&m_flow->document(), &core::ProjectDocument::changed, this,
                &SceneSettingsPanel::refresh);
    }
    showMessage({});
    refresh();
}

QString SceneSettingsPanel::message() const
{
    return m_message->text();  // cleared when hidden
}

bool SceneSettingsPanel::applyId()
{
    const QString from = selected();
    if (from.isEmpty()) {
        return false;
    }
    const QString to = m_idField->text().trimmed();
    if (to == from) {
        showMessage({});
        m_idField->setText(to);
        m_idField->setModified(false);
        return true;
    }
    if (!m_flow->renameScene(from, to)) {
        showMessage(FlowScene::renameRefusal(to));
        return false;
    }
    showMessage({});
    m_idField->setModified(false);
    return true;
}

bool SceneSettingsPanel::eventFilter(QObject* watched, QEvent* event)
{
    // Escape drops what was typed.
    if (watched == m_idField && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        showMessage({});
        m_idField->setModified(false);
        refresh();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void SceneSettingsPanel::refresh()
{
    const QString id = selected();
    for (QWidget* field :
         std::initializer_list<QWidget*>{m_idField, m_widthBox, m_heightBox, m_fitBox}) {
        field->setEnabled(!id.isEmpty());
    }
    if (id.isEmpty()) {
        const qsizetype count = m_flow ? m_flow->selectedSceneIds().size() : 0;
        m_summary->setText(!m_flow     ? tr("Open a project to see its scenes.")
                           : count > 1 ? tr("%n scenes selected.", nullptr, int(count))
                                       : tr("Select a scene to see its settings."));
        m_idField->clear();
        m_startBox->setChecked(false);
        m_startBox->setEnabled(false);
        showValue(m_widthBox, 1);
        showValue(m_heightBox, 1);
        m_fitBox->setCurrentIndex(0);
        return;
    }

    const core::Project& project = m_flow->document().project();
    const core::Scene& scene = *project.scenes.find(id);
    int entities = 0;
    core::forEachEntity(scene, [&](const core::Entity&, const core::EntityPath&) { ++entities; });
    m_summary->setText(QStringLiteral("%1 · %2 · %3")
                           .arg(tr("%n layers", nullptr, int(scene.layers.size())),
                                tr("%n entities", nullptr, entities),
                                tr("%n modifiers", nullptr, int(scene.modifiers.size()))));
    if (!(m_idField->hasFocus() && m_idField->isModified())) {
        m_idField->setText(id);
        m_idField->setModified(false);
    }
    const bool start = project.manifest.start == id;
    m_startBox->setChecked(start);
    m_startBox->setEnabled(!start);
    showValue(m_widthBox, scene.size.width);
    showValue(m_heightBox, scene.size.height);
    m_fitBox->setItemText(0, tr("Project's (%1)").arg(core::enumName(project.manifest.fit)));
    m_fitBox->setCurrentIndex(scene.fit ? m_fitBox->findData(int(*scene.fit)) : 0);
}

void SceneSettingsPanel::showMessage(const QString& message)
{
    editor::showMessage(m_message, message);
}

QString SceneSettingsPanel::selected() const
{
    if (!m_flow) {
        return {};
    }
    const QStringList ids = m_flow->selectedSceneIds();
    return ids.size() == 1 && m_flow->document().project().scenes.contains(ids.first())
               ? ids.first()
               : QString();
}

void SceneSettingsPanel::setStart(bool start)
{
    const QString id = selected();
    if (start && !id.isEmpty()) {
        m_flow->setStartScene(id);  // refreshes, from the document's signal
    }
    else {
        refresh();  // a click can't uncheck the start scene
    }
}

void SceneSettingsPanel::applySettings()
{
    const QString id = selected();
    if (id.isEmpty()) {
        return;
    }
    const std::optional<Fit> fit = m_fitBox->currentIndex() > 0
                                       ? std::optional(Fit(m_fitBox->currentData().toInt()))
                                       : std::nullopt;
    m_flow->setSceneSettings(id, {int(m_widthBox->value()), int(m_heightBox->value())}, fit);
}

}  // namespace shoal::editor
