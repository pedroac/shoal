#include "editor/InspectorPanel.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

#include "assets/FontCache.h"
#include "assets/SheetCache.h"
#include "core/ProjectDocument.h"
#include "core/SceneCommands.h"
#include "editor/AssetField.h"
#include "editor/ColorButton.h"
#include "editor/FormFields.h"
#include "editor/PointGrid.h"
#include "editor/PresetSettingsPanel.h"
#include "editor/SceneCanvas.h"
#include "editor/SceneSettingsPanel.h"

namespace shoal::editor {

using core::Entity;
using core::EntityPath;

namespace {

constexpr double MAX_POSITION = 1000000;
constexpr double MAX_FRACTION = 100;
/// The size a rect starts at when it is given to an entity without an image.
constexpr double NEW_RECT_SIZE = 64;
/// The radius a sound is given when it is made positional, in pixels.
constexpr double NEW_SOUND_RADIUS = 400;
constexpr double MAX_CELL = 1 << 24;  // as the files allow
constexpr core::Unit UNITS[] = {core::Unit::Px, core::Unit::Percent, core::Unit::Cell};

const QRegularExpression SEPARATORS(QStringLiteral("[\\s,]+"));

QString unitName(core::Unit unit)
{
    switch (unit) {
        case core::Unit::Px:
            return QStringLiteral("px");
        case core::Unit::Percent:
            return QStringLiteral("%");
        case core::Unit::Cell:
            return QString(QChar(0x25A1));
    }
    return {};
}

QComboBox* unitBox(QWidget* parent, const QString& name)
{
    auto* box = new QComboBox(parent);
    box->setObjectName(name);
    box->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    for (const core::Unit unit : UNITS) {
        box->addItem(unitName(unit), int(unit));
    }
    box->setToolTip(QObject::tr("px: pixels; %: of its parent's rect; %1: its parent's tiles, "
                                "in a tiled container")
                        .arg(unitName(core::Unit::Cell)));
    return box;
}

/// Shows `current`, with the units `frame` doesn't offer greyed out. Every unit stays
/// listed, so the box keeps its width.
void showUnits(QComboBox* box, core::Unit current, const core::Frame& frame)
{
    const QSignalBlocker blocker(box);
    auto* model = qobject_cast<QStandardItemModel*>(box->model());
    for (int i = 0; i < box->count(); ++i) {
        const auto unit = core::Unit(box->itemData(i).toInt());
        model->item(i)->setEnabled(unit == current || frame.offers(unit));
    }
    box->setCurrentIndex(box->findData(int(current)));
}

core::Unit unitOf(const QComboBox* box)
{
    return core::Unit(box->currentData().toInt());
}

/// Sets a number box up for `unit`: tiles are whole, and from 0 on a position.
void showLength(QDoubleSpinBox* box, const core::Length& length, bool position)
{
    {
        const QSignalBlocker blocker(box);
        const bool cells = length.unit == core::Unit::Cell;
        box->setDecimals(cells && position ? 0 : 2);
        box->setRange(cells || !position ? 0 : -MAX_POSITION, cells ? MAX_CELL : MAX_POSITION);
        box->setSuffix(length.unit == core::Unit::Px ? QString()
                                                     : QStringLiteral(" ") + unitName(length.unit));
    }
    showValue(box, length.value);
}

/// `widgets` side by side, at their size, or sharing the row's width.
QWidget* row(QWidget* parent, std::initializer_list<QWidget*> widgets, bool packed = true)
{
    auto* widget = new QWidget(parent);
    auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins({});
    for (QWidget* item : widgets) {
        layout->addWidget(item, packed ? 0 : 1);
    }
    if (packed) {
        layout->addStretch();
    }
    return widget;
}

/// A titled section of `layout`, and the form for its rows; untitled, just the form.
QFormLayout* addSection(QVBoxLayout* layout, const QString& title)
{
    if (!title.isEmpty()) {
        auto* heading = new QLabel(title);
        QFont font = heading->font();
        font.setBold(true);
        heading->setFont(font);
        auto* line = new QFrame;
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        auto* header = new QHBoxLayout;
        header->setContentsMargins(0, heading->fontMetrics().height() / 2, 0, 0);
        header->addWidget(heading);
        header->addWidget(line, 1);
        layout->addLayout(header);
    }
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(form);
    return form;
}

template <typename E>
void addEnumItems(QComboBox* box, std::initializer_list<std::pair<E, QString>> items)
{
    for (const auto& [value, text] : items) {
        box->addItem(text, int(value));
    }
}

/// Where the class being typed starts: after the last separator before the cursor, and
/// after its dot.
qsizetype tokenStart(const QString& text, qsizetype cursor)
{
    qsizetype start = cursor;
    while (start > 0 && !text[start - 1].isSpace() && text[start - 1] != u',') {
        --start;
    }
    if (start < cursor && text[start] == u'.') {
        ++start;
    }
    return start;
}

}  // namespace

InspectorPanel::InspectorPanel(QWidget* parent)
    : QWidget(parent),
      m_summary(new QLabel(this)),
      m_idField(new QLineEdit(this)),
      m_classesField(new QLineEdit(this)),
      m_activeBox(new QCheckBox(tr("Active"), this)),
      m_visibleBox(new QCheckBox(tr("Visible"), this)),
      m_xBox(numberBox(this, QStringLiteral("xBox"), MAX_POSITION, 2, 1)),
      m_yBox(numberBox(this, QStringLiteral("yBox"), MAX_POSITION, 2, 1)),
      m_xUnitBox(unitBox(this, QStringLiteral("xUnitBox"))),
      m_yUnitBox(unitBox(this, QStringLiteral("yUnitBox"))),
      m_offsetXBox(numberBox(this, QStringLiteral("offsetXBox"), MAX_POSITION, 2, 1)),
      m_offsetYBox(numberBox(this, QStringLiteral("offsetYBox"), MAX_POSITION, 2, 1)),
      m_angleBox(numberBox(this, QStringLiteral("angleBox"), 360, 2, 15)),
      m_scaleXBox(numberBox(this, QStringLiteral("scaleXBox"), 1000, 3, 0.1)),
      m_scaleYBox(numberBox(this, QStringLiteral("scaleYBox"), 1000, 3, 0.1)),
      m_widthBox(numberBox(this, QStringLiteral("widthBox"), MAX_SIZE, 2, 1)),
      m_heightBox(numberBox(this, QStringLiteral("heightBox"), MAX_SIZE, 2, 1)),
      m_widthUnitBox(unitBox(this, QStringLiteral("widthUnitBox"))),
      m_heightUnitBox(unitBox(this, QStringLiteral("heightUnitBox"))),
      m_flipXBox(new QCheckBox(tr("Horizontal"), this)),
      m_flipYBox(new QCheckBox(tr("Vertical"), this)),
      m_sizeBox(new QCheckBox(this)),
      m_pivotBox(new QCheckBox(tr("Own"), this)),
      m_pivotGrid(new PointGrid(this)),
      m_pivotXBox(numberBox(this, QStringLiteral("pivotXBox"), MAX_FRACTION, 3, 0.25)),
      m_pivotYBox(numberBox(this, QStringLiteral("pivotYBox"), MAX_FRACTION, 3, 0.25)),
      m_assetField(new AssetField(AssetField::Accepts::Any, this)),
      m_imageModeBox(new QComboBox(this)),
      m_fontField(new AssetField(AssetField::Accepts::Fonts, this)),
      m_textEdit(new QPlainTextEdit(this)),
      m_textKeyField(new QLineEdit(this)),
      m_textSizeBox(numberBox(this, QStringLiteral("textSizeBox"), MAX_SIZE, 2, 1)),
      m_textColorButton(new ColorButton(this)),
      m_textAlignGrid(new PointGrid(this)),
      m_textWrapBox(new QComboBox(this)),
      m_lineHeightBox(numberBox(this, QStringLiteral("lineHeightBox"), 10, 2, 0.1)),
      m_textSection(new QWidget(this)),
      m_audioField(new AssetField(AssetField::Accepts::Sounds, this)),
      m_volumeBox(numberBox(this, QStringLiteral("volumeBox"), 1, 2, 0.1)),
      m_audioLoopBox(new QCheckBox(tr("Loop"), this)),
      m_audioAutoplayBox(new QCheckBox(tr("Autoplay"), this)),
      m_positionalBox(new QCheckBox(tr("Positional"), this)),
      m_radiusBox(numberBox(this, QStringLiteral("radiusBox"), MAX_SIZE, 0, 10)),
      m_removeAudioButton(new QToolButton(this)),
      m_audioSection(new QWidget(this)),
      m_videoField(new AssetField(AssetField::Accepts::Videos, this)),
      m_videoLoopBox(new QCheckBox(tr("Loop"), this)),
      m_videoAutoplayBox(new QCheckBox(tr("Autoplay"), this)),
      m_videoModeBox(new QComboBox(this)),
      m_videoSection(new QWidget(this)),
      m_fillBox(new QCheckBox(this)),
      m_fillButton(new ColorButton(this)),
      m_tintButton(new ColorButton(this)),
      m_tintModeBox(new QComboBox(this)),
      m_clipBox(new QCheckBox(tr("Children only inside its rect"), this)),
      m_containerBox(new QComboBox(this)),
      m_tileWidthBox(numberBox(this, QStringLiteral("tileWidthBox"), MAX_SIZE, 2, 1)),
      m_tileHeightBox(numberBox(this, QStringLiteral("tileHeightBox"), MAX_SIZE, 2, 1)),
      m_tilePivotGrid(new PointGrid(this)),
      m_tilePivotXBox(numberBox(this, QStringLiteral("tilePivotXBox"), MAX_FRACTION, 3, 0.25)),
      m_tilePivotYBox(numberBox(this, QStringLiteral("tilePivotYBox"), MAX_FRACTION, 3, 0.25)),
      m_spaceBox(new QComboBox(this)),
      m_parallaxBox(new QCheckBox(this)),
      m_parallaxXBox(numberBox(this, QStringLiteral("parallaxXBox"), MAX_FRACTION, 3, 0.1)),
      m_parallaxYBox(numberBox(this, QStringLiteral("parallaxYBox"), MAX_FRACTION, 3, 0.1)),
      m_message(messageLabel(this, QStringLiteral("inspectorMessage"))),
      m_entityForm(new QWidget(this)),
      m_topLevel(new SceneSettingsPanel(this)),
      m_presetTopLevel(new PresetSettingsPanel(this)),
      m_pages(new QStackedWidget(this)),
      m_tabs(new QTabBar(this)),
      m_locksPage(new QWidget(this)),
      m_lockNote(new QLabel(this)),
      m_scaleRow(new QWidget(this)),
      m_sizeRow(new QWidget(this)),
      m_imageRow(nullptr),
      m_offsetRow(nullptr),
      m_tilePivotRow(nullptr),
      m_completer(new QCompleter(this)),
      m_classNames(new QStringListModel(this))
{
    m_summary->setObjectName(QStringLiteral("inspectorSummary"));
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_idField->setObjectName(QStringLiteral("idField"));
    m_idField->setPlaceholderText(tr("none: nothing refers to it"));
    m_classesField->setObjectName(QStringLiteral("classesField"));
    m_classesField->setPlaceholderText(tr("e.g. enemy flying"));
    for (QDoubleSpinBox* box : {m_xBox, m_offsetXBox}) {
        box->setPrefix(QStringLiteral("x "));
    }
    for (QDoubleSpinBox* box : {m_yBox, m_offsetYBox}) {
        box->setPrefix(QStringLiteral("y "));
    }
    m_angleBox->setSuffix(QStringLiteral("°"));
    m_angleBox->setToolTip(tr("Clockwise, in degrees, about the entity's position"));
    m_scaleXBox->setPrefix(QStringLiteral("x "));
    m_scaleYBox->setPrefix(QStringLiteral("y "));
    m_widthBox->setMinimum(0);
    m_heightBox->setMinimum(0);
    m_widthBox->setPrefix(QStringLiteral("w "));
    m_heightBox->setPrefix(QStringLiteral("h "));
    m_sizeBox->setObjectName(QStringLiteral("sizeBox"));
    m_sizeRow->setToolTip(
        tr("Its rect, which its image fills: a nine-slice's borders keep their size, and the "
           "edges and the middle stretch"));
    m_pivotXBox->setPrefix(QStringLiteral("x "));
    m_pivotYBox->setPrefix(QStringLiteral("y "));
    m_pivotBox->setObjectName(QStringLiteral("pivotBox"));
    m_pivotBox->setToolTip(
        tr("The point of its rect on its position, which it turns and scales about; an image's "
           "is its sprite's until set"));
    m_imageModeBox->setObjectName(QStringLiteral("imageModeBox"));
    addEnumItems(m_imageModeBox, {std::pair{core::ImageMode::Stretch, tr("Stretch")},
                                  std::pair{core::ImageMode::NineSlice, tr("Nine-slice")}});
    m_assetField->setObjectName(QStringLiteral("assetField"));
    m_assetField->setToolTip(
        tr("What it draws: a sprite, an animation or an image makes it an image, a font a text"));
    m_fontField->setObjectName(QStringLiteral("fontField"));
    m_textEdit->setObjectName(QStringLiteral("textEdit"));
    m_textEdit->setTabChangesFocus(true);
    m_textEdit->setToolTip(tr("Applied on leaving it, or on Ctrl+Enter; Enter breaks a line"));
    m_textEdit->setFixedHeight(m_textEdit->fontMetrics().lineSpacing() * 3 +
                               2 * m_textEdit->frameWidth() + 8);
    m_textKeyField->setObjectName(QStringLiteral("textKeyField"));
    m_textKeyField->setPlaceholderText(tr("none: the text as written"));
    m_textKeyField->setToolTip(
        tr("A localization key the engine looks up; the text is then its sample and fallback"));
    m_textSizeBox->setMinimum(1);
    m_textSizeBox->setSuffix(QStringLiteral(" px"));
    m_textSizeBox->setToolTip(tr("The font's size, its em, in pixels"));
    m_textColorButton->setObjectName(QStringLiteral("textColorButton"));
    m_textColorButton->setToolTip(tr("Multiplied by its tint"));
    m_textAlignGrid->setObjectName(QStringLiteral("textAlignGrid"));
    m_textAlignGrid->setToolTip(tr("Where the text sits in its rect"));
    m_textWrapBox->setObjectName(QStringLiteral("textWrapBox"));
    addEnumItems(m_textWrapBox, {std::pair{core::TextWrap::Word, tr("At words")},
                                 std::pair{core::TextWrap::None, tr("Never")}});
    m_textWrapBox->setToolTip(tr("Whether lines break at its rect's width"));
    m_lineHeightBox->setMinimum(0.1);
    m_lineHeightBox->setSuffix(QStringLiteral(" ×"));
    m_lineHeightBox->setToolTip(tr("The distance between lines, as a multiple of the size"));
    m_audioField->setObjectName(QStringLiteral("audioField"));
    m_volumeBox->setMinimum(0);
    m_volumeBox->setToolTip(tr("From 0 to 1: a hint, the engine's to mix"));
    m_audioLoopBox->setObjectName(QStringLiteral("audioLoopBox"));
    m_audioAutoplayBox->setObjectName(QStringLiteral("audioAutoplayBox"));
    m_audioAutoplayBox->setToolTip(tr("Starts it when the entity becomes active"));
    m_positionalBox->setObjectName(QStringLiteral("positionalBox"));
    m_positionalBox->setToolTip(
        tr("Heard from its origin, fading out to nothing at the radius; off, it is heard "
           "everywhere"));
    m_radiusBox->setMinimum(1);
    m_radiusBox->setSuffix(QStringLiteral(" px"));
    m_removeAudioButton->setObjectName(QStringLiteral("removeAudioButton"));
    m_removeAudioButton->setText(tr("Remove"));
    m_removeAudioButton->setToolTip(tr("Remove the sound"));
    m_videoField->setObjectName(QStringLiteral("videoField"));
    m_videoLoopBox->setObjectName(QStringLiteral("videoLoopBox"));
    m_videoAutoplayBox->setObjectName(QStringLiteral("videoAutoplayBox"));
    m_videoAutoplayBox->setToolTip(tr("Starts it when the entity becomes active"));
    m_videoModeBox->setObjectName(QStringLiteral("videoModeBox"));
    addEnumItems(m_videoModeBox, {std::pair{core::VideoMode::Stretch, tr("Stretch")},
                                  std::pair{core::VideoMode::Fit, tr("Fit")},
                                  std::pair{core::VideoMode::Fill, tr("Fill")}});
    m_videoModeBox->setToolTip(
        tr("How its frames fill the rect: stretched, inside it keeping their aspect ratio, or "
           "covering it, cropped"));
    m_fillBox->setObjectName(QStringLiteral("fillBox"));
    m_fillButton->setObjectName(QStringLiteral("fillButton"));
    m_tintButton->setObjectName(QStringLiteral("tintButton"));
    m_tintButton->setToolTip(tr("Multiplies its image, fill and text, and its children's"));
    m_tintModeBox->setObjectName(QStringLiteral("tintModeBox"));
    addEnumItems(m_tintModeBox, {std::pair{core::TintMode::Mix, tr("Mix with its parent's")},
                                 std::pair{core::TintMode::Inherit, tr("Its parent's")},
                                 std::pair{core::TintMode::Override, tr("Its own only")}});
    m_clipBox->setObjectName(QStringLiteral("clipBox"));
    m_containerBox->setObjectName(QStringLiteral("containerBox"));
    m_containerBox->addItem(tr("Free"), -1);
    addEnumItems(m_containerBox, {std::pair{core::ContainerType::Tiled, tr("Tiled")}});
    m_containerBox->setToolTip(
        tr("How it places its children: freely, or on a grid of tiles over its rect, as many "
           "as fit"));
    // Tiles have a size, and cells are counted from the top-left one.
    m_tileWidthBox->setMinimum(1);
    m_tileHeightBox->setMinimum(1);
    m_tileWidthBox->setPrefix(QStringLiteral("w "));
    m_tileHeightBox->setPrefix(QStringLiteral("h "));
    m_tilePivotXBox->setPrefix(QStringLiteral("x "));
    m_tilePivotYBox->setPrefix(QStringLiteral("y "));
    m_spaceBox->setObjectName(QStringLiteral("spaceBox"));
    m_spaceBox->addItem(tr("Its parent's"), -1);
    addEnumItems(m_spaceBox, {std::pair{core::Space::World, tr("World")},
                              std::pair{core::Space::Screen, tr("Screen")}});
    m_spaceBox->setToolTip(
        tr("Set near the top of the tree: screen space stays on the screen "
           "and is laid out in a later version"));
    m_parallaxBox->setObjectName(QStringLiteral("parallaxBox"));
    m_parallaxBox->setToolTip(tr("How much it and its descendants scroll with the camera"));
    m_parallaxXBox->setPrefix(QStringLiteral("x "));
    m_parallaxYBox->setPrefix(QStringLiteral("y "));
    m_flipXBox->setObjectName(QStringLiteral("flipXBox"));
    m_flipYBox->setObjectName(QStringLiteral("flipYBox"));
    m_flipXBox->setToolTip(tr("Mirrors the entity's own drawing, not its children"));
    m_flipYBox->setToolTip(m_flipXBox->toolTip());

    m_completer->setModel(m_classNames);
    m_completer->setWidget(m_classesField);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);

    // The entity's fields, in sections: what it is, then where, its rect, how it looks, how
    // it places its children, and how it moves with the camera.
    auto* entityLayout = new QVBoxLayout(m_entityForm);
    entityLayout->setContentsMargins({});
    QFormLayout* form = addSection(entityLayout, {});
    form->addRow(tr("Id"), m_idField);
    form->addRow(tr("Classes"), m_classesField);
    form->addRow(QString(), row(this, {m_activeBox, m_visibleBox}));

    form = addSection(entityLayout, tr("Transform"));
    // An axis a line, with its unit.
    auto* position = new QGridLayout;
    position->addWidget(m_xBox, 0, 0);
    position->addWidget(m_xUnitBox, 0, 1);
    position->addWidget(m_yBox, 1, 0);
    position->addWidget(m_yUnitBox, 1, 1);
    position->setColumnStretch(0, 1);
    form->addRow(tr("Position"), position);
    form->labelForField(position)->setToolTip(
        tr("In its parent's coordinates: pixels from its parent's origin, a percentage of its "
           "parent's rect from its top-left corner, or a tile of its parent's grid"));
    m_offsetRow = row(this, {m_offsetXBox, m_offsetYBox}, false);
    m_offsetRow->setToolTip(tr("Pixels added to a position in percent or in tiles"));
    form->addRow(tr("Offset"), m_offsetRow);
    form->addRow(tr("Angle"), m_angleBox);
    auto* scale = new QHBoxLayout(m_scaleRow);
    scale->setContentsMargins({});
    scale->addWidget(m_scaleXBox);
    scale->addWidget(m_scaleYBox);
    form->addRow(tr("Scale"), m_scaleRow);
    form->addRow(tr("Flip"), row(this, {m_flipXBox, m_flipYBox}));

    // A checkbox to have one, beside the fields it enables.
    form = addSection(entityLayout, tr("Rect"));
    auto* size = new QGridLayout(m_sizeRow);
    size->setContentsMargins({});
    size->addWidget(m_widthBox, 0, 0);
    size->addWidget(m_widthUnitBox, 0, 1);
    size->addWidget(m_sizeBox, 0, 2);
    size->addWidget(m_heightBox, 1, 0);
    size->addWidget(m_heightUnitBox, 1, 1);
    size->setColumnStretch(0, 1);
    form->addRow(tr("Size"), m_sizeRow);
    auto* pivotRow = new QWidget(this);
    auto* pivot = new QGridLayout(pivotRow);
    pivot->setContentsMargins({});
    pivot->addWidget(m_pivotGrid, 0, 0, 2, 1);
    pivot->addWidget(m_pivotBox, 0, 1, 1, 2);
    pivot->addWidget(m_pivotXBox, 1, 1);
    pivot->addWidget(m_pivotYBox, 1, 2);
    pivot->setColumnStretch(1, 1);
    pivot->setColumnStretch(2, 1);
    form->addRow(tr("Pivot"), pivotRow);

    form = addSection(entityLayout, tr("Look"));
    form->addRow(tr("Asset"), m_assetField);
    m_imageRow = row(this, {m_imageModeBox});
    form->addRow(tr("Mode"), m_imageRow);
    form->addRow(tr("Fill"), row(this, {m_fillBox, m_fillButton}));
    form->addRow(tr("Tint"), row(this, {m_tintButton, m_tintModeBox}));

    // Only for an entity with a text.
    auto* textLayout = new QVBoxLayout(m_textSection);
    textLayout->setContentsMargins({});
    entityLayout->addWidget(m_textSection);
    form = addSection(textLayout, tr("Text"));
    form->addRow(tr("Font"), m_fontField);
    form->addRow(tr("Text"), m_textEdit);
    form->addRow(tr("Key"), m_textKeyField);
    form->addRow(tr("Size"), row(this, {m_textSizeBox, m_textColorButton}));
    form->addRow(tr("Align"), m_textAlignGrid);
    form->addRow(tr("Wrap"), m_textWrapBox);
    form->addRow(tr("Line height"), m_lineHeightBox);

    // Only for an entity with a sound.
    auto* audioLayout = new QVBoxLayout(m_audioSection);
    audioLayout->setContentsMargins({});
    entityLayout->addWidget(m_audioSection);
    form = addSection(audioLayout, tr("Audio"));
    form->addRow(tr("Clip"), m_audioField);
    form->addRow(tr("Volume"), m_volumeBox);
    form->addRow(QString(), row(this, {m_audioLoopBox, m_audioAutoplayBox}));
    form->addRow(QString(), row(this, {m_positionalBox, m_radiusBox}));
    form->addRow(QString(), row(this, {m_removeAudioButton}));

    // Only for an entity with a video.
    auto* videoLayout = new QVBoxLayout(m_videoSection);
    videoLayout->setContentsMargins({});
    entityLayout->addWidget(m_videoSection);
    form = addSection(videoLayout, tr("Video"));
    form->addRow(tr("Clip"), m_videoField);
    form->addRow(QString(), row(this, {m_videoLoopBox, m_videoAutoplayBox}));
    form->addRow(tr("Mode"), m_videoModeBox);

    form = addSection(entityLayout, tr("Children"));
    form->addRow(tr("Clip"), m_clipBox);
    form->addRow(tr("Container"), row(this, {m_containerBox, m_tileWidthBox, m_tileHeightBox}));
    m_tilePivotRow = row(this, {m_tilePivotGrid, m_tilePivotXBox, m_tilePivotYBox});
    m_tilePivotRow->setToolTip(tr("The point of each tile its children's pivots land on"));
    form->addRow(tr("Tile pivot"), m_tilePivotRow);

    form = addSection(entityLayout, tr("Camera"));
    form->addRow(tr("Space"), m_spaceBox);
    form->addRow(tr("Parallax"), row(this, {m_parallaxBox, m_parallaxXBox, m_parallaxYBox}));

    // The Locks tab: for each property a preset can lock, Editable or Locked.
    auto* locksLayout = new QVBoxLayout(m_locksPage);
    locksLayout->setContentsMargins({});
    const auto addLock = [&](QFormLayout* section, const QString& label, core::Lock lock) {
        auto* editable = new QToolButton(m_locksPage);
        auto* locked = new QToolButton(m_locksPage);
        editable->setText(tr("Editable"));
        locked->setText(tr("Locked"));
        auto* group = new QButtonGroup(m_locksPage);
        for (QToolButton* button : {editable, locked}) {
            button->setCheckable(true);
            button->setAutoRaise(false);
            group->addButton(button);
        }
        group->setExclusive(true);
        editable->setChecked(true);
        locked->setObjectName(QStringLiteral("lock:") + core::enumName(lock));
        locked->setToolTip(
            tr("Scenes show it read-only and refuse every edit of it, until it "
               "is unlocked on the entity"));
        m_lockButtons.insert(lock, locked);
        m_editableButtons.insert(lock, editable);
        connect(locked, &QToolButton::clicked, this, [this, lock] { setLocked(lock, true); });
        connect(editable, &QToolButton::clicked, this, [this, lock] { setLocked(lock, false); });
        section->addRow(label, row(m_locksPage, {editable, locked}));
    };
    form = addSection(locksLayout, tr("Transform"));
    addLock(form, tr("Position x"), core::Lock::PosX);
    addLock(form, tr("Position y"), core::Lock::PosY);
    addLock(form, tr("Angle"), core::Lock::Angle);
    addLock(form, tr("Scale w"), core::Lock::ScaleX);
    addLock(form, tr("Scale h"), core::Lock::ScaleY);
    addLock(form, tr("Flip"), core::Lock::Flip);
    form = addSection(locksLayout, tr("Rect"));
    addLock(form, tr("Rect w"), core::Lock::Width);
    addLock(form, tr("Rect h"), core::Lock::Height);
    addLock(form, tr("Pivot"), core::Lock::Pivot);
    form = addSection(locksLayout, tr("Look"));
    addLock(form, tr("Asset"), core::Lock::Asset);
    addLock(form, tr("Fill"), core::Lock::Fill);
    addLock(form, tr("Tint"), core::Lock::Tint);
    form = addSection(locksLayout, tr("Children"));
    addLock(form, tr("Clip"), core::Lock::Clip);
    addLock(form, tr("Container"), core::Lock::Container);
    form = addSection(locksLayout, tr("Camera"));
    addLock(form, tr("Space"), core::Lock::Space);
    addLock(form, tr("Parallax"), core::Lock::Parallax);

    m_tabs->setObjectName(QStringLiteral("inspectorTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->setExpanding(false);
    m_tabs->addTab(tr("Defaults"));
    m_tabs->addTab(tr("Locks"));
    connect(m_tabs, &QTabBar::currentChanged, this, &InspectorPanel::refresh);
    m_lockNote->setObjectName(QStringLiteral("lockNote"));
    m_lockNote->setWordWrap(true);
    m_lockNote->setVisible(false);

    m_topLevel->setObjectName(QStringLiteral("topLevelSettings"));
    m_topLevel->layout()->setContentsMargins({});
    m_pages->addWidget(m_entityForm);
    m_pages->addWidget(m_locksPage);
    m_pages->addWidget(m_topLevel);
    m_presetTopLevel->setObjectName(QStringLiteral("presetTopLevelSettings"));
    m_presetTopLevel->layout()->setContentsMargins({});
    m_pages->addWidget(m_presetTopLevel);
    // Scrolled when the panel is shorter than the form, never wider than the panel.
    auto* content = new QWidget;
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->addWidget(m_summary);
    contentLayout->addWidget(m_tabs);
    contentLayout->addWidget(m_lockNote);
    contentLayout->addWidget(m_pages);
    contentLayout->addWidget(m_message);
    contentLayout->addStretch();
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("inspectorScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins({});
    layout->addWidget(scroll);

    m_activeBox->setToolTip(
        tr("An inactive entity is authored but not present until an action "
           "activates it"));
    m_idField->installEventFilter(this);
    m_classesField->installEventFilter(this);
    connect(m_idField, &QLineEdit::editingFinished, this, &InspectorPanel::applyId);
    // The completer's popup takes focus with Qt::PopupFocusReason, which doesn't finish
    // editing.
    connect(m_classesField, &QLineEdit::editingFinished, this, &InspectorPanel::applyClasses);
    connect(m_classesField, &QLineEdit::textEdited, this, &InspectorPanel::completeClass);
    connect(m_completer, qOverload<const QString&>(&QCompleter::activated), this,
            &InspectorPanel::insertClass);
    connect(m_activeBox, &QCheckBox::clicked, this, &InspectorPanel::setActive);
    connect(m_visibleBox, &QCheckBox::clicked, this, &InspectorPanel::setVisibleFlag);
    connect(m_xBox, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::applyPosition);
    connect(m_yBox, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::applyPosition);
    connect(m_offsetXBox, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::applyOffset);
    connect(m_offsetYBox, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::applyOffset);
    connect(m_xUnitBox, &QComboBox::activated, this,
            [this] { setPositionUnit(0, unitOf(m_xUnitBox)); });
    connect(m_yUnitBox, &QComboBox::activated, this,
            [this] { setPositionUnit(1, unitOf(m_yUnitBox)); });
    connect(m_widthUnitBox, &QComboBox::activated, this,
            [this] { setSizeUnit(0, unitOf(m_widthUnitBox)); });
    connect(m_heightUnitBox, &QComboBox::activated, this,
            [this] { setSizeUnit(1, unitOf(m_heightUnitBox)); });
    connect(m_angleBox, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::applyAngle);
    connect(m_scaleXBox, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::applyScale);
    connect(m_scaleYBox, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::applyScale);
    connect(m_widthBox, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::applySize);
    connect(m_heightBox, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::applySize);
    connect(m_flipXBox, &QCheckBox::clicked, this, &InspectorPanel::setFlipX);
    connect(m_flipYBox, &QCheckBox::clicked, this, &InspectorPanel::setFlipY);
    connect(m_sizeBox, &QCheckBox::clicked, this, &InspectorPanel::setOwnSize);
    connect(m_pivotBox, &QCheckBox::clicked, this, [this](bool own) {
        EntityPath path;
        if (const Entity* entity = selected(&path)) {
            applyPivot(own ? std::optional(core::entityPivot(*entity, m_canvas->imageMetrics()))
                           : std::nullopt);
        }
    });
    connect(m_pivotGrid, &PointGrid::pointChosen, this,
            [this](core::Point point) { applyPivot(point); });
    const auto applyPivotBoxes = [this] {
        applyPivot(core::Point{m_pivotXBox->value(), m_pivotYBox->value()});
    };
    connect(m_pivotXBox, &QDoubleSpinBox::valueChanged, this, applyPivotBoxes);
    connect(m_pivotYBox, &QDoubleSpinBox::valueChanged, this, applyPivotBoxes);
    connect(m_imageModeBox, &QComboBox::activated, this,
            [this] { setImageMode(core::ImageMode(m_imageModeBox->currentData().toInt())); });
    connect(m_assetField, &AssetField::chosen, this, &InspectorPanel::setAsset);
    connect(m_fontField, &AssetField::chosen, this, [this](const std::optional<AssetItem>& item) {
        setTextFont(item ? std::optional(item->alias) : std::nullopt);
    });
    m_textEdit->installEventFilter(this);
    m_textKeyField->installEventFilter(this);
    connect(m_textKeyField, &QLineEdit::editingFinished, this, &InspectorPanel::applyTextKey);
    connect(m_textSizeBox, &QDoubleSpinBox::valueChanged, this, [this](double pixels) {
        changeText([pixels](core::Text& text) { text.size = pixels; }, tr("Size the text of %1"));
    });
    connect(m_textColorButton, &ColorButton::colorChosen, this, [this](const core::Color& color) {
        changeText([color](core::Text& text) { text.color = color; }, tr("Color the text of %1"));
    });
    connect(m_textAlignGrid, &PointGrid::pointChosen, this, [this](core::Point align) {
        changeText([align](core::Text& text) { text.align = align; }, tr("Align the text of %1"));
    });
    connect(m_textWrapBox, &QComboBox::activated, this, [this] {
        const auto wrap = core::TextWrap(m_textWrapBox->currentData().toInt());
        changeText([wrap](core::Text& text) { text.wrap = wrap; }, tr("Wrap the text of %1"));
    });
    connect(m_lineHeightBox, &QDoubleSpinBox::valueChanged, this, [this](double height) {
        changeText([height](core::Text& text) { text.lineHeight = height; },
                   tr("Space the lines of %1"));
    });
    connect(m_audioField, &AssetField::chosen, this, [this](const std::optional<AssetItem>& item) {
        setSoundClip(item ? std::optional(item->alias) : std::nullopt);
    });
    connect(m_volumeBox, &QDoubleSpinBox::valueChanged, this, [this](double volume) {
        changeAudio([volume](core::Audio& audio) { audio.volume = volume; },
                    tr("Set the volume of %1"));
    });
    connect(m_audioLoopBox, &QCheckBox::clicked, this, [this](bool loop) {
        changeAudio([loop](core::Audio& audio) { audio.loop = loop; },
                    loop ? tr("Loop the sound of %1") : tr("Play the sound of %1 once"));
    });
    connect(m_audioAutoplayBox, &QCheckBox::clicked, this, [this](bool autoplay) {
        changeAudio(
            [autoplay](core::Audio& audio) { audio.autoplay = autoplay; },
            autoplay ? tr("Autoplay the sound of %1") : tr("Don't autoplay the sound of %1"));
    });
    connect(m_positionalBox, &QCheckBox::clicked, this, [this](bool positional) {
        changeAudio(
            [positional](core::Audio& audio) {
                audio.radius = positional ? std::optional(NEW_SOUND_RADIUS) : std::nullopt;
            },
            positional ? tr("Position the sound of %1") : tr("Hear the sound of %1 everywhere"));
    });
    connect(m_radiusBox, &QDoubleSpinBox::valueChanged, this, [this](double radius) {
        changeAudio(
            [radius](core::Audio& audio) {
                if (audio.radius) {
                    audio.radius = radius;
                }
            },
            tr("Set the radius of %1"));
    });
    connect(m_removeAudioButton, &QToolButton::clicked, this,
            [this] { setSoundClip(std::nullopt); });
    connect(m_videoField, &AssetField::chosen, this, [this](const std::optional<AssetItem>& item) {
        setVideoClip(item ? std::optional(item->alias) : std::nullopt);
    });
    connect(m_videoLoopBox, &QCheckBox::clicked, this, [this](bool loop) {
        changeVideo([loop](core::Video& video) { video.loop = loop; },
                    loop ? tr("Loop the video of %1") : tr("Play the video of %1 once"));
    });
    connect(m_videoAutoplayBox, &QCheckBox::clicked, this, [this](bool autoplay) {
        changeVideo(
            [autoplay](core::Video& video) { video.autoplay = autoplay; },
            autoplay ? tr("Autoplay the video of %1") : tr("Don't autoplay the video of %1"));
    });
    connect(m_videoModeBox, &QComboBox::activated, this, [this] {
        const auto mode = core::VideoMode(m_videoModeBox->currentData().toInt());
        changeVideo([mode](core::Video& video) { video.mode = mode; },
                    tr("Set how the video of %1 fills it"));
    });
    connect(m_fillBox, &QCheckBox::clicked, this, [this](bool fill) {
        setFill(fill ? std::optional(m_fillButton->color()) : std::nullopt);
    });
    connect(m_fillButton, &ColorButton::colorChosen, this,
            [this](const core::Color& color) { setFill(color); });
    connect(m_tintButton, &ColorButton::colorChosen, this, &InspectorPanel::setTint);
    connect(m_tintModeBox, &QComboBox::activated, this,
            [this] { setTintMode(core::TintMode(m_tintModeBox->currentData().toInt())); });
    connect(m_clipBox, &QCheckBox::clicked, this, &InspectorPanel::setClip);
    connect(m_containerBox, &QComboBox::activated, this, [this] {
        const int type = m_containerBox->currentData().toInt();
        setContainer(type < 0 ? std::nullopt : std::optional(core::ContainerType(type)));
    });
    connect(m_tileWidthBox, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::applyTileSize);
    connect(m_tileHeightBox, &QDoubleSpinBox::valueChanged, this, &InspectorPanel::applyTileSize);
    connect(m_tilePivotGrid, &PointGrid::pointChosen, this,
            [this](core::Point point) { applyTilePivot(point); });
    const auto applyTilePivotBoxes = [this] {
        applyTilePivot(core::Point{m_tilePivotXBox->value(), m_tilePivotYBox->value()});
    };
    connect(m_tilePivotXBox, &QDoubleSpinBox::valueChanged, this, applyTilePivotBoxes);
    connect(m_tilePivotYBox, &QDoubleSpinBox::valueChanged, this, applyTilePivotBoxes);
    connect(m_spaceBox, &QComboBox::activated, this, [this] {
        const int space = m_spaceBox->currentData().toInt();
        setSpace(space < 0 ? std::nullopt : std::optional(core::Space(space)));
    });
    connect(m_parallaxBox, &QCheckBox::clicked, this, [this](bool set) {
        setParallax(set ? std::optional(core::Point{1, 1}) : std::nullopt);
    });
    const auto applyParallaxBoxes = [this] {
        setParallax(core::Point{m_parallaxXBox->value(), m_parallaxYBox->value()});
    };
    connect(m_parallaxXBox, &QDoubleSpinBox::valueChanged, this, applyParallaxBoxes);
    connect(m_parallaxYBox, &QDoubleSpinBox::valueChanged, this, applyParallaxBoxes);
    setCanvas(nullptr);
}

void InspectorPanel::setCanvas(SceneCanvas* canvas)
{
    if (m_canvas) {
        disconnect(m_canvas, nullptr, this, nullptr);
    }
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_canvas = canvas;
    m_document = canvas ? &canvas->document() : nullptr;
    for (AssetField* field : {m_assetField, m_fontField, m_audioField, m_videoField}) {
        field->setCaches(canvas ? &canvas->sheets() : nullptr, canvas ? &canvas->fonts() : nullptr);
    }
    refreshClips();
    if (m_canvas) {
        connect(m_document, &core::ProjectDocument::assetsChanged, this,
                &InspectorPanel::refreshClips);
        connect(m_canvas, &SceneCanvas::contentChanged, this, &InspectorPanel::refresh);
        connect(m_canvas, &QGraphicsScene::selectionChanged, this, [this] {
            showMessage({});
            refresh();
        });
        connect(m_document, &core::ProjectDocument::classesChanged, this,
                &InspectorPanel::refreshClasses);
    }
    showMessage({});
    refreshClasses();
    refresh();
}

void InspectorPanel::setFlowScene(FlowScene* flow)
{
    m_topLevel->setFlowScene(flow);
    refresh();
}

bool InspectorPanel::showsTopLevel() const
{
    return m_pages->currentWidget() != m_entityForm;
}

QString InspectorPanel::message() const
{
    return m_message->isVisible() ? m_message->text() : QString();
}

std::optional<QStringList> InspectorPanel::parseClasses(const QString& text, QString* error)
{
    QStringList classes;
    for (QString name : text.split(SEPARATORS, Qt::SkipEmptyParts)) {
        if (name.startsWith(u'.')) {
            name.remove(0, 1);
        }
        if (!core::isValidId(name)) {
            if (error) {
                *error = tr("“%1” isn't a valid class: use letters, digits, _ and -, "
                            "starting with a letter or _.")
                             .arg(name);
            }
            return std::nullopt;
        }
        if (!classes.contains(name)) {
            classes << name;
        }
    }
    return classes;
}

bool InspectorPanel::applyId()
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity) {
        return false;
    }
    const QString id = m_idField->text().trimmed();
    if (id == entity->id) {
        showMessage({});
        m_idField->setText(id);
        return true;
    }
    const QString sceneId = m_canvas->sceneId();
    if (id.isEmpty()) {
        // Transitions would be left naming nothing.
        const QStringList naming = m_document->transitionsNamingEntity(sceneId, entity->id);
        if (!naming.isEmpty()) {
            showMessage(tr("The transitions %1 refer to “%2”; it needs an id.")
                            .arg(naming.join(QStringLiteral(", ")), entity->id));
            return false;
        }
    }
    else if (!core::isValidId(id)) {
        showMessage(tr("“%1” isn't a valid id: use letters, digits, _ and -, starting with a "
                       "letter or _.")
                        .arg(id));
        return false;
    }
    else if (!m_document->isEntityIdAvailable(sceneId, id, path)) {
        showMessage(tr("Another entity of this scene is already “%1”.").arg(id));
        return false;
    }
    Entity changed = *entity;
    changed.id = id;
    const QString text = id.isEmpty()           ? tr("Remove the id of %1").arg(entity->id)
                         : entity->id.isEmpty() ? tr("Name %1 %2").arg(core::kindOf(*entity), id)
                                                : tr("Rename %1 to %2").arg(entity->id, id);
    push(path, std::move(changed), text);
    return true;
}

bool InspectorPanel::applyClasses()
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity) {
        return false;
    }
    QString error;
    const std::optional<QStringList> classes = parseClasses(m_classesField->text(), &error);
    if (!classes) {
        showMessage(error);
        return false;
    }
    if (*classes == entity->classes) {
        showMessage({});
        refresh();  // tidies what was typed
        return true;
    }
    Entity changed = *entity;
    changed.classes = *classes;
    push(path, std::move(changed), tr("Set the classes of %1").arg(core::describe(*entity)));
    return true;
}

void InspectorPanel::applyPosition()
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity) {
        return;
    }
    Entity changed = *entity;
    for (int axis = 0; axis < 2; ++axis) {
        const double value = (axis == 0 ? m_xBox : m_yBox)->value();
        core::Length& at = changed.at.axis(axis);
        if (at.unit == core::Unit::Px) {
            (axis == 0 ? changed.transform.pos.x : changed.transform.pos.y) = value;
        }
        else {
            at.value = value;
        }
    }
    if (changed != *entity) {
        push(path, std::move(changed), tr("Move %1").arg(core::describe(*entity)));
    }
}

void InspectorPanel::applyOffset()
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity) {
        return;
    }
    Entity changed = *entity;
    if (changed.at.x.unit != core::Unit::Px) {
        changed.transform.pos.x = m_offsetXBox->value();
    }
    if (changed.at.y.unit != core::Unit::Px) {
        changed.transform.pos.y = m_offsetYBox->value();
    }
    if (changed != *entity) {
        push(path, std::move(changed), tr("Move %1").arg(core::describe(*entity)));
    }
}

void InspectorPanel::setPositionUnit(int axis, core::Unit unit)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || entity->at.axis(axis).unit == unit) {
        return;
    }
    const Entity changed = core::reanchored(frameOf(path), *entity, axis, unit);
    if (changed == *entity) {
        refresh();  // a unit its parent doesn't offer
        return;
    }
    push(path, changed, tr("Measure %1 in %2").arg(core::describe(*entity), unitName(unit)));
}

void InspectorPanel::setSizeUnit(int axis, core::Unit unit)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || !entity->size) {
        return;
    }
    const core::Frame frame = frameOf(path);
    const std::optional<core::SizeF> size = core::entitySize(*entity, frame);
    core::Extent units = *entity->size;
    (axis == 0 ? units.width : units.height).unit = unit;
    const core::Extent changedSize = core::extentIn(units, *size, frame);
    if (changedSize == *entity->size) {
        refresh();  // a unit its parent doesn't offer
        return;
    }
    Entity changed = *entity;
    changed.size = changedSize;
    push(path, std::move(changed),
         tr("Measure the size of %1 in %2").arg(core::describe(*entity), unitName(unit)));
}

void InspectorPanel::applyAngle()
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || m_angleBox->value() == entity->transform.rot) {
        return;
    }
    Entity changed = *entity;
    changed.transform.rot = m_angleBox->value();
    push(path, std::move(changed), tr("Rotate %1").arg(core::describe(*entity)));
}

bool InspectorPanel::applyScale()
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity) {
        return false;
    }
    const core::Point scale{m_scaleXBox->value(), m_scaleYBox->value()};
    if (std::abs(scale.x) < core::MIN_SCALE || std::abs(scale.y) < core::MIN_SCALE) {
        showMessage(tr("A scale can't be zero."));
        refresh();
        return false;
    }
    if (scale == entity->transform.scale) {
        showMessage({});
        return true;
    }
    Entity changed = *entity;
    changed.transform.scale = scale;
    push(path, std::move(changed), tr("Scale %1").arg(core::describe(*entity)));
    return true;
}

void InspectorPanel::applySize()
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || !entity->size) {
        return;
    }
    Entity changed = *entity;
    changed.size->width.value = m_widthBox->value();
    changed.size->height.value = m_heightBox->value();
    if (changed != *entity) {
        push(path, std::move(changed), tr("Resize %1").arg(core::describe(*entity)));
    }
}

void InspectorPanel::setOwnSize(bool own)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || own == entity->size.has_value()) {
        return;
    }
    Entity changed = *entity;
    if (own) {
        // The size it has, so nothing moves; a rect for an entity without an image.
        changed.size =
            core::Extent::px(core::entitySize(*entity, frameOf(path), m_canvas->imageMetrics())
                                 .value_or(core::SizeF{NEW_RECT_SIZE, NEW_RECT_SIZE}));
    }
    else {
        changed.size.reset();
    }
    const QString name = core::describe(*entity);
    push(path, std::move(changed),
         own             ? tr("Size %1").arg(name)
         : entity->image ? tr("Size %1 by its sprite").arg(name)
                         : tr("Remove the rect of %1").arg(name));
}

void InspectorPanel::applyPivot(std::optional<core::Point> pivot)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || pivot == entity->pivot) {
        return;
    }
    Entity changed = *entity;
    changed.transform.pos =
        core::positionForPivot(*entity, frameOf(path), pivot, m_canvas->imageMetrics());
    changed.pivot = pivot;
    push(path, std::move(changed), tr("Set the pivot of %1").arg(core::describe(*entity)));
}

void InspectorPanel::setImageMode(core::ImageMode mode)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || !entity->image || entity->image->mode == mode) {
        return;
    }
    Entity changed = *entity;
    changed.image->mode = mode;
    push(path, std::move(changed),
         mode == core::ImageMode::NineSlice
             ? tr("Draw %1 as a nine-slice").arg(core::describe(*entity))
             : tr("Stretch %1").arg(core::describe(*entity)));
}

void InspectorPanel::setImage(const std::optional<core::Image>& image)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!image) {
        removeImage();
        return;
    }
    if (!entity) {
        return;
    }
    core::Image changedImage = *image;
    if (entity->image && !entity->image->isAnimation() && !image->isAnimation()) {
        changedImage.mode = entity->image->mode;  // a nine-slice stays one
    }
    if (entity->image == changedImage) {
        return;
    }
    Entity changed = *entity;
    changed.image = changedImage;
    push(path, std::move(changed), tr("Set the image of %1").arg(core::describe(*entity)));
}

void InspectorPanel::removeImage()
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || !entity->image) {
        return;
    }
    // What its sprite gave it stays, so nothing else moves.
    const core::ImageMetricsFn metrics = m_canvas->imageMetrics();
    Entity changed = *entity;
    if (!changed.size) {
        if (const auto size = core::entitySize(*entity, frameOf(path), metrics)) {
            changed.size = core::Extent::px(*size);
        }
    }
    if (changed.size) {
        changed.pivot = core::entityPivot(*entity, metrics);
    }
    changed.image.reset();
    push(path, std::move(changed), tr("Remove the image of %1").arg(core::describe(*entity)));
}

void InspectorPanel::setFill(std::optional<core::Color> fill)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || fill == entity->fill) {
        return;
    }
    Entity changed = *entity;
    changed.fill = fill;
    if (fill && !changed.size && !changed.image) {
        changed.size = core::Extent::px({NEW_RECT_SIZE, NEW_RECT_SIZE});  // a fill needs a rect
    }
    push(path, std::move(changed),
         fill ? tr("Fill %1").arg(core::describe(*entity))
              : tr("Remove the fill of %1").arg(core::describe(*entity)));
}

void InspectorPanel::setAsset(const std::optional<AssetItem>& item)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity) {
        return;
    }
    const std::optional<AssetItem> current = assetOf(*entity);
    if (!item) {
        if (entity->image) {
            removeImage();
        }
        else if (entity->video) {
            setVideoClip(std::nullopt);
        }
        else if (entity->text) {
            setTextFont(std::nullopt);
        }
        else {
            setSoundClip(std::nullopt);
        }
        return;
    }
    enum Part { IMAGE, VIDEO, TEXT, SOUND };
    const auto partOf = [](const AssetItem& asset) {
        return asset.isImage()                        ? IMAGE
               : asset.kind == AssetItem::Kind::Video ? VIDEO
               : asset.kind == AssetItem::Kind::Font  ? TEXT
                                                      : SOUND;
    };
    const Part wanted = partOf(*item);
    if (!current || partOf(*current) == wanted) {
        // Another asset for the same part, or a first one.
        switch (wanted) {
            case IMAGE:
                setImage(item->toImage());
                break;
            case VIDEO:
                setVideoClip(item->alias);
                break;
            case TEXT:
                setTextFont(item->alias);
                break;
            case SOUND:
                setSoundClip(item->alias);
                break;
        }
        return;
    }

    // Its part replaces the entity's, in the rect that one was drawn in, so nothing moves.
    const core::ImageMetricsFn metrics = m_canvas->imageMetrics();
    Entity changed = *entity;
    if (!changed.size) {
        if (const auto size = core::entitySize(*entity, frameOf(path), metrics)) {
            changed.size = core::Extent::px(*size);
        }
    }
    if (changed.size) {
        changed.pivot = core::entityPivot(*entity, metrics);
    }
    switch (partOf(*current)) {
        case IMAGE:
            changed.image.reset();
            break;
        case VIDEO:
            changed.video.reset();
            break;
        case TEXT:
            changed.text.reset();
            break;
        case SOUND:
            changed.audio.reset();
            break;
    }
    // An image takes its sprite's size and pivot, and a sound needs no rect, unless another
    // part needs it.
    const auto dropRect = [&] {
        if (!changed.fill && !changed.clip && !changed.container && !changed.text &&
            !changed.video) {
            changed.size.reset();
            changed.pivot.reset();
        }
    };
    QString undoText;
    switch (wanted) {
        case IMAGE:
            changed.image = item->toImage();
            dropRect();
            undoText = tr("Make %1 an image");
            break;
        case VIDEO: {
            core::Video video = entity->video.value_or(core::Video{});
            video.clip = item->alias;
            changed.video = video;
            if (!changed.size) {
                changed.size = core::Extent::px(m_canvas->newVideoSize());
            }
            undoText = tr("Make %1 a video");
            break;
        }
        case TEXT: {
            core::Text text = entity->text.value_or(core::Text{});
            if (!entity->text) {
                text.string = tr("Text");
            }
            text.font = item->alias;
            changed.text = text;
            if (!changed.size) {
                changed.size = textSize(text);
            }
            undoText = tr("Make %1 a text");
            break;
        }
        case SOUND: {
            core::Audio audio = entity->audio.value_or(core::Audio{});
            audio.clip = item->alias;
            changed.audio = audio;
            dropRect();
            undoText = tr("Make %1 a sound");
            break;
        }
    }
    push(path, std::move(changed), undoText.arg(core::describe(*entity)));
}

std::optional<AssetItem> InspectorPanel::assetOf(const Entity& entity)
{
    if (entity.image) {
        return AssetItem::fromImage(*entity.image);
    }
    if (entity.video) {
        return AssetItem{AssetItem::Kind::Video, entity.video->clip, {}};
    }
    if (entity.text) {
        return AssetItem{AssetItem::Kind::Font, entity.text->font, {}};
    }
    if (entity.audio) {
        return AssetItem{AssetItem::Kind::Audio, entity.audio->clip, {}};
    }
    return std::nullopt;
}

void InspectorPanel::setSoundClip(const std::optional<QString>& clip)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || (!clip && !entity->audio) || (entity->audio && clip == entity->audio->clip)) {
        return;
    }
    Entity changed = *entity;
    if (!clip) {
        changed.audio.reset();
        push(path, std::move(changed), tr("Remove the sound of %1").arg(core::describe(*entity)));
        return;
    }
    if (changed.audio) {
        changed.audio->clip = *clip;
        push(path, std::move(changed), tr("Set the sound of %1").arg(core::describe(*entity)));
        return;
    }
    core::Audio audio;
    audio.clip = *clip;
    changed.audio = audio;
    push(path, std::move(changed), tr("Add a sound to %1").arg(core::describe(*entity)));
}

void InspectorPanel::changeAudio(const std::function<void(core::Audio&)>& change,
                                 const QString& text)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || !entity->audio) {
        return;
    }
    Entity changed = *entity;
    change(*changed.audio);
    if (changed.audio != entity->audio) {
        push(path, std::move(changed), text.arg(core::describe(*entity)));
    }
}

void InspectorPanel::refreshAudio(const Entity& entity)
{
    m_audioSection->setVisible(entity.audio.has_value());
    const core::Audio shown = entity.audio.value_or(core::Audio{});
    m_audioField->setItem(entity.audio
                              ? std::optional(AssetItem{AssetItem::Kind::Audio, shown.clip, {}})
                              : std::nullopt);
    // The asset field shows the clip, unless it shows another part's asset.
    setRowVisible(m_audioField, entity.audio && (entity.image || entity.video || entity.text));
    showValue(m_volumeBox, shown.volume);
    m_audioLoopBox->setChecked(shown.loop);
    m_audioAutoplayBox->setChecked(shown.autoplay);
    m_positionalBox->setChecked(shown.radius.has_value());
    m_radiusBox->setVisible(shown.radius.has_value());
    showValue(m_radiusBox, shown.radius.value_or(NEW_SOUND_RADIUS));
}

core::Extent InspectorPanel::textSize(core::Text text) const
{
    text.wrap = core::TextWrap::None;
    const TextLayout layout = m_canvas->layOutText(text, 0);
    return core::Extent::px({std::ceil(layout.size.width()), std::ceil(layout.size.height())});
}

void InspectorPanel::setVideoClip(const std::optional<QString>& clip)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || (!clip && !entity->video) || (entity->video && clip == entity->video->clip)) {
        return;
    }
    Entity changed = *entity;
    if (!clip) {
        changed.video.reset();
        push(path, std::move(changed), tr("Remove the video of %1").arg(core::describe(*entity)));
        return;
    }
    if (changed.video) {
        changed.video->clip = *clip;
        push(path, std::move(changed), tr("Set the video of %1").arg(core::describe(*entity)));
        return;
    }
    core::Video video;
    video.clip = *clip;
    changed.video = video;
    // A video needs a rect: a new one's, if it has none, keeping its origin.
    if (!core::entityRect(*entity, frameOf(path), m_canvas->imageMetrics())) {
        changed.size = core::Extent::px(m_canvas->newVideoSize());
    }
    push(path, std::move(changed), tr("Add a video to %1").arg(core::describe(*entity)));
}

void InspectorPanel::changeVideo(const std::function<void(core::Video&)>& change,
                                 const QString& text)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || !entity->video) {
        return;
    }
    Entity changed = *entity;
    change(*changed.video);
    if (changed.video != entity->video) {
        push(path, std::move(changed), text.arg(core::describe(*entity)));
    }
}

void InspectorPanel::refreshClips()
{
    const core::Assets* assets = m_document ? &m_document->project().manifest.assets : nullptr;
    const QStringList audio = assets ? assets->audio.keys() : QStringList();
    const QStringList videos = assets ? assets->video.keys() : QStringList();
    for (AssetField* field : {m_assetField, m_audioField, m_videoField}) {
        field->setClips(audio, videos);
    }
}

void InspectorPanel::refreshVideo(const Entity& entity)
{
    m_videoSection->setVisible(entity.video.has_value());
    const core::Video shown = entity.video.value_or(core::Video{});
    m_videoField->setItem(entity.video
                              ? std::optional(AssetItem{AssetItem::Kind::Video, shown.clip, {}})
                              : std::nullopt);
    // The asset field shows the clip, unless it shows the image.
    setRowVisible(m_videoField, entity.video && entity.image);
    m_videoLoopBox->setChecked(shown.loop);
    m_videoAutoplayBox->setChecked(shown.autoplay);
    m_videoModeBox->setCurrentIndex(m_videoModeBox->findData(int(shown.mode)));
}

void InspectorPanel::setTextFont(const std::optional<QString>& font)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || (!font && !entity->text) || (entity->text && font == entity->text->font)) {
        return;
    }
    Entity changed = *entity;
    if (!font) {
        changed.text.reset();
        push(path, std::move(changed), tr("Remove the text of %1").arg(core::describe(*entity)));
        return;
    }
    if (changed.text) {
        changed.text->font = *font;
        push(path, std::move(changed), tr("Set the font of %1").arg(core::describe(*entity)));
        return;
    }
    core::Text text;
    text.font = *font;
    text.string = tr("Text");
    changed.text = text;
    // A text needs a rect: one sized to it, if it has none, keeping its origin.
    if (!core::entityRect(*entity, frameOf(path), m_canvas->imageMetrics())) {
        changed.size = textSize(text);
    }
    push(path, std::move(changed), tr("Add a text to %1").arg(core::describe(*entity)));
}

void InspectorPanel::applyText()
{
    if (!m_textEdit->document()->isModified()) {
        return;
    }
    m_textEdit->document()->setModified(false);
    const QString string = m_textEdit->toPlainText();
    changeText([&string](core::Text& text) { text.string = string; }, tr("Write the text of %1"));
}

void InspectorPanel::applyTextKey()
{
    if (!m_textKeyField->isModified()) {
        return;
    }
    m_textKeyField->setModified(false);
    const QString key = m_textKeyField->text().trimmed();
    changeText([&key](core::Text& text) { text.key = key; }, tr("Set the text key of %1"));
}

void InspectorPanel::changeText(const std::function<void(core::Text&)>& change, const QString& text)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || !entity->text) {
        return;
    }
    Entity changed = *entity;
    change(*changed.text);
    if (changed.text != entity->text) {
        push(path, std::move(changed), text.arg(core::describe(*entity)));
    }
}

void InspectorPanel::refreshText(const Entity& entity)
{
    m_textSection->setVisible(entity.text.has_value());
    const core::Text shown = entity.text.value_or(core::Text{});
    m_fontField->setItem(entity.text
                             ? std::optional(AssetItem{AssetItem::Kind::Font, shown.font, {}})
                             : std::nullopt);
    // The asset field shows the font, unless it shows the image or the video.
    setRowVisible(m_fontField, entity.text && (entity.image || entity.video));
    // Keep what is being typed.
    if (!(m_textEdit->hasFocus() && m_textEdit->document()->isModified())) {
        const QSignalBlocker blocker(m_textEdit);
        if (m_textEdit->toPlainText() != shown.string) {
            m_textEdit->setPlainText(shown.string);
        }
        m_textEdit->document()->setModified(false);
    }
    if (!(m_textKeyField->hasFocus() && m_textKeyField->isModified())) {
        m_textKeyField->setText(shown.key);
        m_textKeyField->setModified(false);
    }
    showValue(m_textSizeBox, shown.size);
    m_textColorButton->setColor(shown.color);
    m_textAlignGrid->setPoint(shown.align);
    m_textWrapBox->setCurrentIndex(m_textWrapBox->findData(int(shown.wrap)));
    showValue(m_lineHeightBox, shown.lineHeight);
}

void InspectorPanel::setTint(const core::Color& tint)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (entity && tint != entity->tint) {
        Entity changed = *entity;
        changed.tint = tint;
        push(path, std::move(changed), tr("Tint %1").arg(core::describe(*entity)));
    }
}

void InspectorPanel::setTintMode(core::TintMode mode)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (entity && mode != entity->tintMode) {
        Entity changed = *entity;
        changed.tintMode = mode;
        push(path, std::move(changed), tr("Tint %1").arg(core::describe(*entity)));
    }
}

void InspectorPanel::setClip(bool clip)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (entity && clip != entity->clip) {
        Entity changed = *entity;
        changed.clip = clip;
        push(path, std::move(changed),
             (clip ? tr("Clip to %1") : tr("Stop clipping to %1")).arg(core::describe(*entity)));
    }
}

void InspectorPanel::setContainer(std::optional<core::ContainerType> type)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity ||
        type == (entity->container ? std::optional(entity->container->type) : std::nullopt)) {
        return;
    }
    const core::ImageMetricsFn metrics = m_canvas->imageMetrics();
    const core::Frame frame = frameOf(path);
    Entity changed = *entity;
    if (type) {
        changed.container = core::Container{};
        changed.container->type = *type;
        if (!core::entitySize(changed, frame, metrics)) {
            changed.size = core::Extent::px({NEW_RECT_SIZE, NEW_RECT_SIZE});  // tiles need a rect
        }
    }
    else {
        changed.container.reset();
    }
    // Its children stay where they are drawn.
    QList<std::pair<EntityPath, Entity>> changes{{path, changed}};
    const core::Frame before = core::childFrame(*entity, frame, metrics);
    const core::Frame after = core::childFrame(changed, frame, metrics);
    for (size_t i = 0; i < entity->children.size(); ++i) {
        EntityPath childPath = path;
        childPath.append(int(i));
        changes.append(
            {childPath, core::placedInContainer(before, after, entity->children[i], metrics)});
    }
    showMessage({});
    const QString name = core::describe(*entity);
    const QList<std::pair<EntityPath, Entity>> kept = m_canvas->keepingLocks(changes);
    if (kept.isEmpty()) {
        refresh();
        return;
    }
    m_document->commands().push(std::make_unique<core::EditEntitiesCommand>(
        *m_document, m_canvas->sceneId(), kept,
        type ? tr("Tile %1").arg(name) : tr("Stop tiling %1").arg(name)));
}

void InspectorPanel::applyTileSize()
{
    EntityPath path;
    const Entity* entity = selected(&path);
    const core::SizeF size{m_tileWidthBox->value(), m_tileHeightBox->value()};
    if (!entity || !entity->container || size == entity->container->tileSize) {
        return;
    }
    Entity changed = *entity;
    changed.container->tileSize = size;
    push(path, std::move(changed), tr("Set the tiles of %1").arg(core::describe(*entity)));
}

void InspectorPanel::applyTilePivot(core::Point pivot)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (!entity || !entity->container || pivot == entity->container->tilePivot) {
        return;
    }
    Entity changed = *entity;
    changed.container->tilePivot = pivot;
    push(path, std::move(changed), tr("Set the tiles of %1").arg(core::describe(*entity)));
}

void InspectorPanel::setSpace(std::optional<core::Space> space)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (entity && space != entity->space) {
        Entity changed = *entity;
        changed.space = space;
        push(path, std::move(changed), tr("Set the space of %1").arg(core::describe(*entity)));
    }
}

void InspectorPanel::setParallax(std::optional<core::Point> parallax)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    if (entity && parallax != entity->parallax) {
        Entity changed = *entity;
        changed.parallax = parallax;
        push(path, std::move(changed), tr("Set the parallax of %1").arg(core::describe(*entity)));
    }
}

QFormLayout* InspectorPanel::formOf(const QWidget* field) const
{
    for (QFormLayout* form : m_entityForm->findChildren<QFormLayout*>()) {
        if (form->indexOf(field) >= 0) {
            return form;
        }
    }
    Q_ASSERT(false);
    return nullptr;
}

void InspectorPanel::setRowVisible(QWidget* field, bool visible)
{
    formOf(field)->setRowVisible(field, visible);
}

bool InspectorPanel::showsScale() const
{
    return formOf(m_scaleRow)->isRowVisible(m_scaleRow);
}

bool InspectorPanel::showsSize() const
{
    return formOf(m_sizeRow)->isRowVisible(m_sizeRow);
}

bool InspectorPanel::showsLocks() const
{
    return m_pages->currentWidget() == m_locksPage;
}

bool InspectorPanel::showsImage() const
{
    return formOf(m_imageRow)->isRowVisible(m_imageRow);
}

bool InspectorPanel::showsOffset() const
{
    return formOf(m_offsetRow)->isRowVisible(m_offsetRow);
}

bool InspectorPanel::showsTilePivot() const
{
    return formOf(m_tilePivotRow)->isRowVisible(m_tilePivotRow);
}

bool InspectorPanel::eventFilter(QObject* watched, QEvent* event)
{
    // The text applies on leaving it, or on Ctrl+Enter; Escape drops what was typed.
    if (watched == m_textEdit && event->type() == QEvent::FocusOut) {
        applyText();
    }
    if ((watched == m_textEdit || watched == m_textKeyField) && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            m_textEdit->document()->setModified(false);
            m_textKeyField->setModified(false);
            refresh();
            return true;
        }
        if (watched == m_textEdit &&
            (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            key->modifiers().testFlag(Qt::ControlModifier)) {
            applyText();
            return true;
        }
    }
    // Escape drops what was typed.
    if ((watched == m_idField || watched == m_classesField) && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape &&
        !m_completer->popup()->isVisible()) {
        showMessage({});
        static_cast<QLineEdit*>(watched)->setModified(false);
        refresh();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void InspectorPanel::refresh()
{
    const QList<EntityPath> paths = m_canvas ? m_canvas->selectedPaths() : QList<EntityPath>();
    EntityPath path;
    const Entity* entity = selected(&path);
    // Nothing selected: the top level, the scene itself.
    const bool topLevel = m_canvas && m_canvas->scene() && paths.isEmpty();
    const std::optional<QString> preset = m_canvas ? m_canvas->presetId() : std::nullopt;
    // A preset's entity: its defaults, or its locks. Locks are a preset's only: a scene's
    // entity shows its properties, and what its preset locked read-only.
    const bool tabs = !topLevel && preset && m_canvas->scene();
    const bool locksTab = tabs && m_tabs->currentIndex() == 1;
    m_tabs->setVisible(tabs);
    m_pages->setCurrentWidget(!topLevel ? (locksTab ? m_locksPage : m_entityForm)
                              : preset  ? static_cast<QWidget*>(m_presetTopLevel)
                                        : m_topLevel);
    refreshLocks(topLevel ? nullptr : entity);
    m_topLevel->setScene(topLevel && !preset ? std::optional(m_canvas->sceneId()) : std::nullopt);
    m_presetTopLevel->setPreset(m_document, topLevel && preset ? *preset : QString());
    for (QWidget* field : std::initializer_list<QWidget*>{
             m_idField,       m_classesField,  m_activeBox,         m_visibleBox,
             m_xBox,          m_yBox,          m_xUnitBox,          m_yUnitBox,
             m_offsetXBox,    m_offsetYBox,    m_angleBox,          m_scaleXBox,
             m_scaleYBox,     m_widthBox,      m_heightBox,         m_widthUnitBox,
             m_heightUnitBox, m_flipXBox,      m_flipYBox,          m_sizeBox,
             m_pivotBox,      m_pivotGrid,     m_pivotXBox,         m_pivotYBox,
             m_imageModeBox,  m_assetField,    m_fontField,         m_textEdit,
             m_textKeyField,  m_textSizeBox,   m_textColorButton,   m_textAlignGrid,
             m_textWrapBox,   m_lineHeightBox, m_videoField,        m_videoLoopBox,
             m_audioField,    m_volumeBox,     m_audioLoopBox,      m_audioAutoplayBox,
             m_positionalBox, m_radiusBox,     m_removeAudioButton, m_videoAutoplayBox,
             m_videoModeBox,  m_fillBox,       m_fillButton,        m_tintButton,
             m_tintModeBox,   m_clipBox,       m_spaceBox,          m_parallaxBox,
             m_parallaxXBox,  m_parallaxYBox,  m_containerBox,      m_tileWidthBox,
             m_tileHeightBox, m_tilePivotGrid, m_tilePivotXBox,     m_tilePivotYBox}) {
        field->setEnabled(entity != nullptr);
    }
    if (!entity) {
        m_summary->setText(
            !m_canvas || !m_canvas->scene() ? tr("Open a scene to inspect its entities.")
            : topLevel && preset
                ? tr("The top level: the preset %1, placed in the parent new entities go "
                     "into.")
                      .arg(*preset)
            : topLevel ? tr("The top level: the scene %1, whose rect its entities measure "
                            "against.")
                             .arg(m_canvas->sceneId())
                       : tr("%n entities selected.", nullptr, int(paths.size())));
        m_idField->clear();
        m_classesField->clear();
        m_classesField->setToolTip({});
        m_activeBox->setChecked(false);
        m_visibleBox->setChecked(false);
        for (QDoubleSpinBox* box : {m_xBox, m_yBox, m_widthBox, m_heightBox}) {
            showLength(box, {}, box == m_xBox || box == m_yBox);
        }
        for (QComboBox* box : {m_xUnitBox, m_yUnitBox, m_widthUnitBox, m_heightUnitBox}) {
            showUnits(box, core::Unit::Px, {});
        }
        for (QDoubleSpinBox* box :
             {m_offsetXBox, m_offsetYBox, m_angleBox, m_pivotXBox, m_pivotYBox}) {
            showValue(box, 0);
        }
        for (QDoubleSpinBox* box : {m_scaleXBox, m_scaleYBox, m_parallaxXBox, m_parallaxYBox}) {
            showValue(box, 1);
        }
        for (QCheckBox* box :
             {m_flipXBox, m_flipYBox, m_sizeBox, m_pivotBox, m_fillBox, m_clipBox, m_parallaxBox}) {
            box->setChecked(false);
        }
        m_pivotGrid->setPoint(std::nullopt);
        m_assetField->setItem(std::nullopt);
        m_textSection->setVisible(false);
        m_videoSection->setVisible(false);
        m_audioSection->setVisible(false);
        m_sizeBox->setText(tr("Own"));  // as wide as with an entity, so nothing jumps
        m_tintButton->setColor(core::Color{});
        setRowVisible(m_sizeRow, true);
        setRowVisible(m_scaleRow, true);
        setRowVisible(m_imageRow, false);
        setRowVisible(m_offsetRow, false);
        setRowVisible(m_tilePivotRow, false);
        m_containerBox->setCurrentIndex(0);
        m_tileWidthBox->setVisible(false);
        m_tileHeightBox->setVisible(false);
        return;
    }

    QString summary = core::kindOf(*entity);
    if (const core::Image* image = entity->image ? &*entity->image : nullptr;
        image && image->isStandalone()) {
        summary += QStringLiteral(" · %1").arg(image->asset);
    }
    else if (image) {
        summary += QStringLiteral(" · %1 · %2")
                       .arg(image->sheet, image->isAnimation() ? image->animation : image->sprite);
    }
    m_summary->setText(summary);
    // Keep what is being typed; anything else shows the model.
    if (!(m_idField->hasFocus() && m_idField->isModified())) {
        m_idField->setText(entity->id);
        m_idField->setModified(false);
    }
    if (!(m_classesField->hasFocus() && m_classesField->isModified())) {
        m_classesField->setText(entity->classes.join(u' '));
        m_classesField->setModified(false);
    }
    m_activeBox->setChecked(entity->active);
    m_visibleBox->setChecked(entity->visible);
    // On an axis in pixels, the position; on the others, where its offset is measured from.
    const core::Frame frame = frameOf(path);
    for (int axis = 0; axis < 2; ++axis) {
        const core::Length& at = entity->at.axis(axis);
        const double pos = axis == 0 ? entity->transform.pos.x : entity->transform.pos.y;
        const bool px = at.unit == core::Unit::Px;
        showLength(axis == 0 ? m_xBox : m_yBox, px ? core::Length::px(pos) : at, true);
        showUnits(axis == 0 ? m_xUnitBox : m_yUnitBox, at.unit, frame);
        QDoubleSpinBox* offset = axis == 0 ? m_offsetXBox : m_offsetYBox;
        showValue(offset, px ? 0 : pos);
        offset->setEnabled(!px);
    }
    setRowVisible(m_offsetRow, !entity->at.isPx());
    showValue(m_angleBox, entity->transform.rot);
    showValue(m_scaleXBox, entity->transform.scale.x);
    showValue(m_scaleYBox, entity->transform.scale.y);
    m_flipXBox->setChecked(entity->transform.flipX);
    m_flipYBox->setChecked(entity->transform.flipY);

    // The size: its own, or its image's, shown but not editable.
    const core::ImageMetricsFn metrics = m_canvas->imageMetrics();
    const std::optional<core::SizeF> size = core::entitySize(*entity, frame, metrics);
    m_sizeBox->setText(entity->image ? tr("Own") : tr("Rect"));
    m_sizeBox->setToolTip(entity->image ? tr("A size of its own; otherwise its sprite's")
                                        : tr("A rect, for a region, a fill or children to "
                                             "measure against"));
    m_sizeBox->setChecked(entity->size.has_value());
    const core::Extent extent =
        entity->size.value_or(core::Extent::px(size.value_or(core::SizeF{})));
    showLength(m_widthBox, extent.width, false);
    showLength(m_heightBox, extent.height, false);
    showUnits(m_widthUnitBox, extent.width.unit, frame);
    showUnits(m_heightUnitBox, extent.height.unit, frame);
    for (QWidget* field : std::initializer_list<QWidget*>{m_widthBox, m_heightBox, m_widthUnitBox,
                                                          m_heightUnitBox}) {
        field->setEnabled(entity->size.has_value());
    }
    setRowVisible(m_scaleRow, !entity->size || entity->transform.scale != core::Point{1, 1});

    const core::Point pivot = core::entityPivot(*entity, metrics);
    m_pivotBox->setChecked(entity->pivot.has_value());
    m_pivotGrid->setPoint(pivot);
    showValue(m_pivotXBox, pivot.x);
    showValue(m_pivotYBox, pivot.y);
    m_pivotXBox->setEnabled(entity->pivot.has_value() || !entity->image);
    m_pivotYBox->setEnabled(entity->pivot.has_value() || !entity->image);

    m_assetField->setItem(assetOf(*entity));
    setRowVisible(m_imageRow, entity->image.has_value());
    refreshText(*entity);
    refreshVideo(*entity);
    refreshAudio(*entity);
    if (entity->image) {
        m_imageModeBox->setCurrentIndex(m_imageModeBox->findData(int(entity->image->mode)));
    }
    m_fillBox->setChecked(entity->fill.has_value());
    m_fillButton->setColor(entity->fill.value_or(core::Color{}));
    m_tintButton->setColor(entity->tint);
    m_tintModeBox->setCurrentIndex(m_tintModeBox->findData(int(entity->tintMode)));
    m_clipBox->setChecked(entity->clip);
    const std::optional<core::Container>& container = entity->container;
    m_containerBox->setCurrentIndex(
        m_containerBox->findData(container ? int(container->type) : -1));
    m_tileWidthBox->setVisible(container.has_value());
    m_tileHeightBox->setVisible(container.has_value());
    setRowVisible(m_tilePivotRow, container.has_value());
    const core::Container shown = container.value_or(core::Container{});
    showValue(m_tileWidthBox, shown.tileSize.width);
    showValue(m_tileHeightBox, shown.tileSize.height);
    m_tilePivotGrid->setPoint(shown.tilePivot);
    showValue(m_tilePivotXBox, shown.tilePivot.x);
    showValue(m_tilePivotYBox, shown.tilePivot.y);
    m_spaceBox->setCurrentIndex(m_spaceBox->findData(entity->space ? int(*entity->space) : -1));
    m_parallaxBox->setChecked(entity->parallax.has_value());
    showValue(m_parallaxXBox, entity->parallax ? entity->parallax->x : 1);
    showValue(m_parallaxYBox, entity->parallax ? entity->parallax->y : 1);
    m_parallaxXBox->setEnabled(entity->parallax.has_value());
    m_parallaxYBox->setEnabled(entity->parallax.has_value());

    // Each class's declaration, as a tooltip.
    const auto& registry = m_document->project().classes;
    QStringList lines;
    for (const QString& name : entity->classes) {
        if (!registry || !registry->classes.contains(name)) {
            lines << tr("<b>.%1</b>: not declared").arg(name);
            continue;
        }
        const QString doc = registry->classes.value(name).doc;
        lines << (doc.isEmpty() ? QStringLiteral("<b>.%1</b>").arg(name)
                                : QStringLiteral("<b>.%1</b>: %2").arg(name, doc.toHtmlEscaped()));
    }
    m_classesField->setToolTip(lines.join(QStringLiteral("<br>")));

    // What its preset locked is read-only in a scene.
    if (m_canvas->enforcesLocks()) {
        for (const auto& entry : core::enumNames(core::Lock{})) {
            if (entity->locks.testFlag(entry.value)) {
                for (QWidget* field : lockedFields(entry.value)) {
                    field->setEnabled(false);
                }
            }
        }
    }
}

QList<QWidget*> InspectorPanel::lockedFields(core::Lock lock) const
{
    switch (lock) {
        case core::Lock::PosX:
            return {m_xBox, m_xUnitBox, m_offsetXBox};
        case core::Lock::PosY:
            return {m_yBox, m_yUnitBox, m_offsetYBox};
        case core::Lock::Angle:
            return {m_angleBox};
        case core::Lock::ScaleX:
            return {m_scaleXBox};
        case core::Lock::ScaleY:
            return {m_scaleYBox};
        case core::Lock::Flip:
            return {m_flipXBox, m_flipYBox};
        case core::Lock::Width:
            return {m_widthBox, m_widthUnitBox, m_sizeBox};
        case core::Lock::Height:
            return {m_heightBox, m_heightUnitBox, m_sizeBox};
        case core::Lock::Pivot:
            return {m_pivotBox, m_pivotGrid, m_pivotXBox, m_pivotYBox};
        case core::Lock::Asset:
            return {m_assetField,        m_imageModeBox,     m_fontField,       m_textEdit,
                    m_textKeyField,      m_textSizeBox,      m_textColorButton, m_textAlignGrid,
                    m_textWrapBox,       m_lineHeightBox,    m_audioField,      m_volumeBox,
                    m_audioLoopBox,      m_audioAutoplayBox, m_positionalBox,   m_radiusBox,
                    m_removeAudioButton, m_videoField,       m_videoLoopBox,    m_videoAutoplayBox,
                    m_videoModeBox};
        case core::Lock::Fill:
            return {m_fillBox, m_fillButton};
        case core::Lock::Tint:
            return {m_tintButton, m_tintModeBox};
        case core::Lock::Clip:
            return {m_clipBox};
        case core::Lock::Container:
            return {m_containerBox,  m_tileWidthBox,  m_tileHeightBox,
                    m_tilePivotGrid, m_tilePivotXBox, m_tilePivotYBox};
        case core::Lock::Space:
            return {m_spaceBox};
        case core::Lock::Parallax:
            return {m_parallaxBox, m_parallaxXBox, m_parallaxYBox};
    }
    return {};
}

QString InspectorPanel::lockLabel(core::Lock lock)
{
    switch (lock) {
        case core::Lock::PosX:
            return tr("position x");
        case core::Lock::PosY:
            return tr("position y");
        case core::Lock::Angle:
            return tr("angle");
        case core::Lock::ScaleX:
            return tr("scale w");
        case core::Lock::ScaleY:
            return tr("scale h");
        case core::Lock::Flip:
            return tr("flip");
        case core::Lock::Width:
            return tr("rect w");
        case core::Lock::Height:
            return tr("rect h");
        case core::Lock::Pivot:
            return tr("pivot");
        case core::Lock::Asset:
            return tr("asset");
        case core::Lock::Fill:
            return tr("fill");
        case core::Lock::Tint:
            return tr("tint");
        case core::Lock::Clip:
            return tr("clip");
        case core::Lock::Container:
            return tr("container");
        case core::Lock::Space:
            return tr("space");
        case core::Lock::Parallax:
            return tr("parallax");
    }
    return {};
}

void InspectorPanel::refreshLocks(const Entity* entity)
{
    const bool editable = entity && m_canvas && m_canvas->isEditable();
    QStringList locked;
    for (const auto& entry : core::enumNames(core::Lock{})) {
        const bool on = entity && entity->locks.testFlag(entry.value);
        m_lockButtons.value(entry.value)->setChecked(on);
        m_editableButtons.value(entry.value)->setChecked(!on);
        m_lockButtons.value(entry.value)->setEnabled(editable);
        m_editableButtons.value(entry.value)->setEnabled(editable);
        if (on) {
            locked << lockLabel(entry.value);
        }
    }
    // In a scene, why some fields are read-only.
    const bool note = !locked.isEmpty() && m_canvas && m_canvas->enforcesLocks();
    m_lockNote->setVisible(note);
    if (note) {
        m_lockNote->setText(tr("Locked by its preset: %1.").arg(locked.join(QStringLiteral(", "))));
    }
}

void InspectorPanel::setLocked(core::Lock lock, bool locked)
{
    EntityPath path;
    const Entity* entity = selected(&path);
    // Only a preset defines its locks.
    if (!entity || m_canvas->enforcesLocks() || entity->locks.testFlag(lock) == locked) {
        return;
    }
    Entity changed = *entity;
    changed.locks.setFlag(lock, locked);
    push(path, std::move(changed),
         (locked ? tr("Lock the %2 of %1") : tr("Unlock the %2 of %1"))
             .arg(core::describe(*entity), lockLabel(lock)));
}

void InspectorPanel::refreshClasses()
{
    // Declared or in use: the class registry.
    QStringList names;
    if (m_document) {
        names = m_document->classUses().keys();
        if (const auto& registry = m_document->project().classes) {
            for (const QString& name : registry->classes.keys()) {
                if (!names.contains(name)) {
                    names << name;
                }
            }
        }
        names.sort();
    }
    m_classNames->setStringList(names);
    refresh();
}

void InspectorPanel::showMessage(const QString& message)
{
    editor::showMessage(m_message, message);
}

void InspectorPanel::setActive(bool active)
{
    EntityPath path;
    if (const Entity* entity = selected(&path); entity && entity->active != active) {
        Entity changed = *entity;
        changed.active = active;
        push(path, std::move(changed),
             (active ? tr("Activate %1") : tr("Deactivate %1")).arg(core::describe(*entity)));
    }
}

void InspectorPanel::setVisibleFlag(bool visible)
{
    EntityPath path;
    if (const Entity* entity = selected(&path); entity && entity->visible != visible) {
        Entity changed = *entity;
        changed.visible = visible;
        push(path, std::move(changed),
             (visible ? tr("Show %1") : tr("Hide %1")).arg(core::describe(*entity)));
    }
}

void InspectorPanel::setFlipX(bool flip)
{
    EntityPath path;
    if (const Entity* entity = selected(&path); entity && entity->transform.flipX != flip) {
        Entity changed = *entity;
        changed.transform.flipX = flip;
        push(path, std::move(changed),
             (flip ? tr("Flip %1 horizontally") : tr("Unflip %1 horizontally"))
                 .arg(core::describe(*entity)));
    }
}

void InspectorPanel::setFlipY(bool flip)
{
    EntityPath path;
    if (const Entity* entity = selected(&path); entity && entity->transform.flipY != flip) {
        Entity changed = *entity;
        changed.transform.flipY = flip;
        push(path, std::move(changed),
             (flip ? tr("Flip %1 vertically") : tr("Unflip %1 vertically"))
                 .arg(core::describe(*entity)));
    }
}

const Entity* InspectorPanel::selected(EntityPath* path) const
{
    if (!m_canvas || !m_canvas->scene()) {
        return nullptr;
    }
    const QList<EntityPath> paths = m_canvas->selectedPaths();
    if (paths.size() != 1) {
        return nullptr;
    }
    if (path) {
        *path = paths.first();
    }
    return core::entityAt(*m_canvas->scene(), paths.first());
}

core::Frame InspectorPanel::frameOf(const EntityPath& path) const
{
    return m_canvas->scene() ? core::frameIn(*m_canvas->scene(), path.first(path.size() - 1),
                                             m_canvas->imageMetrics())
                             : core::Frame{};
}

void InspectorPanel::push(const EntityPath& path, Entity entity, const QString& text)
{
    showMessage({});
    m_idField->setModified(false);
    m_classesField->setModified(false);
    // What the entity's preset locked stays, in a scene; a field shows it again.
    const QList<std::pair<EntityPath, Entity>> changes = m_canvas->keepingLocks({{path, entity}});
    if (changes.isEmpty()) {
        refresh();
        return;
    }
    m_document->commands().push(std::make_unique<core::EditEntitiesCommand>(
        *m_document, m_canvas->sceneId(), changes, text));
}

void InspectorPanel::completeClass(const QString& text)
{
    const qsizetype cursor = m_classesField->cursorPosition();
    const QString prefix = text.mid(tokenStart(text, cursor), cursor - tokenStart(text, cursor));
    if (prefix.isEmpty()) {
        m_completer->popup()->hide();
        return;
    }
    m_completer->setCompletionPrefix(prefix);
    if (m_completer->completionCount() > 0) {
        m_completer->complete();
    }
    else {
        m_completer->popup()->hide();
    }
}

void InspectorPanel::insertClass(const QString& name)
{
    const QString text = m_classesField->text();
    const qsizetype cursor = m_classesField->cursorPosition();
    const qsizetype start = tokenStart(text, cursor);
    const QString completed = text.left(start) + name + u' ';
    m_classesField->setText(completed + text.mid(cursor).trimmed());
    m_classesField->setCursorPosition(int(completed.size()));
    m_classesField->setModified(true);
}

}  // namespace shoal::editor
