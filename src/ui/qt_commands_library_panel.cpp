////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_commands_library_panel.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "core/public_command_targets.hpp"
#include "editor/metadata/command_presentation_catalog.hpp"
#include "editor/metadata/release_feature_requirements.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "ui/qt_application_style.hpp"
#include "ui/qt_modeling_operation_catalog.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/retained_ui_presenter.hpp"

#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStringList>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTextLayout>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

QString qtext(std::string_view value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString library_command_label(const LocalizationService *localization,
                              QuaderCommandId command, std::string_view fallback) {
  std::string_view label;
  std::string_view key;
  if (command.string() == quader_command::kSelectEdgeLoops.string()) {
    label = "Select Loop";
    key = "ui.commands_library.select_loop";
  } else if (command.string() == quader_command::kSelectEdgeRing.string()) {
    label = "Select Ring";
    key = "ui.commands_library.select_ring";
  }
  if (!key.empty()) {
    return localization != nullptr
        ? QString::fromStdString(localization->text(key, label)) : qtext(label);
  }
  return localization != nullptr
      ? qt_modeling_operation_label(*localization, command, fallback) : qtext(fallback);
}

struct LibraryCommandTooltip final {
  QuaderCommandId command_id;
  std::string_view fallback;
};

constexpr LibraryCommandTooltip kLibraryCommandTooltips[]{
    {quader_command::kSelectEdgeLoops,
     "Select a continuous loop through the selected edges."},
    {quader_command::kSelectEdgeRing,
     "Select a ring of edges across neighboring quads."},
    {quader_command::kModelingCopyPivotFromActive,
     "Give the selected meshes the active mesh's pivot offset."},
    {quader_command::kModelingSetOriginToCenter,
     "Move each mesh's origin to its surface center without moving the mesh."},
    {quader_command::kModelingSetOriginToPivot,
     "Move the mesh origins to the selection pivot without moving the meshes."},
    {quader_command::kModelingSnapMeshesToFloor,
     "Place the selected meshes on the surface below them."},
    {quader_command::kModelingBakeTransforms,
     "Reset location, rotation and scale without changing how the meshes look."},
    {quader_command::kModelingBakeLocation,
     "Reset location to zero while keeping the meshes in place."},
    {quader_command::kModelingBakeRotation,
     "Reset rotation to zero while keeping the meshes facing the same way."},
    {quader_command::kModelingBakeScale,
     "Reset scale to one while keeping the meshes the same size."},
    {quader_command::kToolsActivateAlign,
     "Align or evenly space the selected objects."},
    {quader_command::kToolsActivateLinearArray,
     "Create evenly spaced copies along a line."},
    {quader_command::kToolsActivateRadialArray,
     "Arrange copies around a center point."},
    {quader_command::kToolsActivatePathArray,
     "Arrange copies of an object along the selected curve."},
    {quader_command::kModelingRemoveDoubles,
     "Weld overlapping vertices in the selected meshes."},
    {quader_command::kToolsActivateExtrude,
     "Extend the selected faces to add connected geometry."},
    {quader_command::kToolsActivateExtrudeIndividual,
     "Extrude each selected face separately."},
    {quader_command::kToolsActivateExtrudeAlongNormals,
     "Extend the selected faces along the directions they face."},
    {quader_command::kToolsActivateCut,
     "Draw a cutting plane through the selection and choose which side to keep."},
    {quader_command::kModelingCombineMesh,
     "Join the selected meshes into one object."},
    {quader_command::kModelingCreateOuterHull,
     "Give the selected open meshes an adjustable thickness."},
    {quader_command::kModelingHalfCut,
     "Split the selected meshes along a local axis."},
    {quader_command::kModelingFlipHorizontal,
     "Mirror each selected mesh left to right around its own center."},
    {quader_command::kModelingFlipVertical,
     "Mirror each selected mesh top to bottom around its own center."},
    {quader_command::kModelingInvertMesh,
     "Reverse which side of each selected mesh faces outward."},
    {quader_command::kModelingConnectVertices,
     "Add edges between selected vertices that share a face."},
    {quader_command::kModelingBevelVertices,
     "Cut off the selected corners to create new faces."},
    {quader_command::kModelingSnapVerticesToActive,
     "Move the selected vertices onto the active vertex without joining them."},
    {quader_command::kModelingSnapVerticesToFloor,
     "Place the selected vertices on the surface below them."},
    {quader_command::kModelingRadialAlignVertices,
     "Space the selected vertices evenly in a circular pattern."},
    {quader_command::kModelingMergeVertices,
     "Weld the selected vertices onto the active vertex."},
    {quader_command::kModelingMergeVerticesAtCenter,
     "Weld the selected vertices into one point at their center."},
    {quader_command::kModelingMergeByDistance,
     "Weld selected vertices that are closer than the chosen distance."},
    {quader_command::kModelingDissolveVertices,
     "Remove the selected vertices while keeping the surrounding surface closed."},
    {quader_command::kToolsActivateInsertEdgeLoop,
     "Insert a loop of edges through a strip of quads."},
    {quader_command::kModelingConnectEdges,
     "Add cuts between the midpoints of selected edges."},
    {quader_command::kModelingSplitEdges,
     "Separate the faces along selected edges without moving them."},
    {quader_command::kModelingSplitEdgesMidpoint,
     "Split each selected edge at its midpoint."},
    {quader_command::kModelingFillHole,
     "Close the hole inside the selected edge boundary."},
    {quader_command::kModelingPreviewBridgeEdges,
     "Join two open edge boundaries with a strip of faces."},
    {quader_command::kModelingBevelEdges,
     "Replace sharp edges with an adjustable flat or rounded bevel."},
    {quader_command::kModelingSnapEdgesToActive,
     "Move the selected edges onto the active edge without joining them."},
    {quader_command::kModelingRadialAlignEdges,
     "Arrange the selected edge boundary in a circular pattern."},
    {quader_command::kModelingMergeEdges,
     "Weld the selected edges onto the active edge."},
    {quader_command::kModelingDefaultEdgeNormals,
     "Restore the selected edges' default shading."},
    {quader_command::kModelingHardenEdgeNormals,
     "Create a sharp shading break along the selected edges."},
    {quader_command::kModelingSoftenEdgeNormals,
     "Blend the shading across the selected edges."},
    {quader_command::kModelingDissolveEdges,
     "Remove the selected edges and join the faces beside them."},
    {quader_command::kModelingCollapseEdges,
     "Merge the selected edge vertices into one point at their center."},
    {quader_command::kModelingExtractFaces,
     "Move the selected faces into a new mesh."},
    {quader_command::kModelingDetachFaces,
     "Disconnect the selected faces from the rest of the mesh."},
    {quader_command::kModelingCombineFaces,
     "Join neighboring selected faces into one face."},
    {quader_command::kModelingPreviewBridgeFaces,
     "Connect two face regions with a bridge of new faces."},
    {quader_command::kModelingSliceQuad,
     "Divide the selected quads into an adjustable grid."},
    {quader_command::kModelingThickenFaces,
     "Give the selected faces an adjustable thickness."},
    {quader_command::kModelingRadialAlignFaces,
     "Arrange the selected face boundary in a circular pattern."},
    {quader_command::kModelingInvertFaces,
     "Reverse which side of the selected faces points outward."},
    {quader_command::kModelingCollapseFaces,
     "Merge the selected face vertices into one point at their center."},
    {quader_command::kModelingUvPeel,
     "Unwrap strips of quads using selected edges as texture guides."},
    {quader_command::kModelingTextureProjectCube,
     "Map the texture from six sides, like wrapping a box."},
    {quader_command::kModelingTextureProjectCylinder,
     "Wrap the texture around a cylinder shape."},
    {quader_command::kModelingTextureProjectSphere,
     "Wrap the texture around a sphere shape."},
    {quader_command::kModelingTextureAlignToGrid,
     "Align the texture to the world grid and reset its scale, rotation and offset."},
    {quader_command::kModelingTextureAlignToFace,
     "Lay the texture flat against each selected face."},
    {quader_command::kModelingTextureAlignToView,
     "Project the texture from the current viewing direction."},
    {quader_command::kModelingTextureScaleXUp,
     "Make the texture twice as wide."},
    {quader_command::kModelingTextureScaleXDown,
     "Make the texture half as wide."},
    {quader_command::kModelingTextureScaleYUp,
     "Make the texture twice as tall."},
    {quader_command::kModelingTextureScaleYDown,
     "Make the texture half as tall."},
    {quader_command::kModelingTextureShiftLeft,
     "Move the texture left by one pixel."},
    {quader_command::kModelingTextureShiftRight,
     "Move the texture right by one pixel."},
    {quader_command::kModelingTextureShiftUp,
     "Move the texture up by one pixel."},
    {quader_command::kModelingTextureShiftDown,
     "Move the texture down by one pixel."},
    {quader_command::kModelingTextureRotateClockwise,
     "Rotate the texture clockwise by the current rotation step."},
    {quader_command::kModelingTextureRotateCounterClockwise,
     "Rotate the texture counterclockwise by the current rotation step."},
    {quader_command::kModelingTextureFlipTextureHorizontal,
     "Mirror the texture across its width."},
    {quader_command::kModelingTextureFlipTextureVertical,
     "Mirror the texture across its height."},
    {quader_command::kModelingTextureFlipFaceHorizontal,
     "Mirror the face's texture layout horizontally within its current bounds."},
    {quader_command::kModelingTextureFlipFaceVertical,
     "Mirror the face's texture layout vertically within its current bounds."},
    {quader_command::kModelingTextureFitBoth,
     "Fit one full texture tile across the selected faces."},
    {quader_command::kModelingTextureFitX,
     "Fit one texture tile across the faces' width."},
    {quader_command::kModelingTextureFitY,
     "Fit one texture tile across the faces' height."},
    {quader_command::kModelingTextureJustifyLeft,
     "Align the texture with the left edge of the faces."},
    {quader_command::kModelingTextureJustifyRight,
     "Align the texture with the right edge of the faces."},
    {quader_command::kModelingTextureJustifyTop,
     "Align the texture with the top edge of the faces."},
    {quader_command::kModelingTextureJustifyBottom,
     "Align the texture with the bottom edge of the faces."},
    {quader_command::kModelingTextureJustifyCenter,
     "Center the texture within the selected faces."},
};

QString library_command_tooltip(const LocalizationService *localization,
                                QuaderCommandId command) {
  const auto found = std::find_if(std::begin(kLibraryCommandTooltips),
      std::end(kLibraryCommandTooltips), [command](const LibraryCommandTooltip &entry) {
        return entry.command_id.string() == command.string();
      });
  if (found == std::end(kLibraryCommandTooltips)) return {};
  const std::string key = "ui.commands_library.tooltip." + std::string(command.string());
  return localization != nullptr
      ? QString::fromStdString(localization->text(key, found->fallback))
      : qtext(found->fallback);
}

struct LibraryOperationState final {
  bool active = false;
  std::string command_id;
};

LibraryOperationState library_operation_state(QuaderEditorApi &editor) {
  const auto settings = editor.tools().active_settings();
  if (settings.preview_active) {
    struct PreviewCommand final {
      std::string_view tool_id;
      QuaderCommandId command_id;
    };
    constexpr PreviewCommand kPreviewCommands[]{
        {"edge_bevel", quader_command::kModelingBevelEdges},
        {"slice_quad", quader_command::kModelingSliceQuad},
        {"merge_by_distance", quader_command::kModelingMergeByDistance},
        {"half_cut", quader_command::kModelingHalfCut},
    };
    for (const auto &preview : kPreviewCommands) {
      if (settings.tool_id == preview.tool_id) {
        return {true, std::string(preview.command_id.string())};
      }
    }
    if (settings.tool_id == "thicken") {
      const auto command = editor.selection().summary().mode == QuaderSelectionMode::Object
          ? quader_command::kModelingCreateOuterHull
          : quader_command::kModelingThickenFaces;
      return {true, std::string(command.string())};
    }
    if (settings.tool_id == "bridge") {
      const auto command = editor.selection().summary().mode == QuaderSelectionMode::Face
          ? quader_command::kModelingPreviewBridgeFaces
          : quader_command::kModelingPreviewBridgeEdges;
      return {true, std::string(command.string())};
    }
  }
  const auto tool_id = editor.tools().active_tool();
  if (tool_id == "move" || tool_id == "rotate" || tool_id == "scale") {
    return {settings.preview_active, {}};
  }
  const auto *tool = find_viewport_tool_descriptor(tool_id);
  if (tool != nullptr && tool->kind == ToolKind::Selection) {
    return {settings.preview_active, {}};
  }
  if (tool_id.empty()) return {settings.preview_active, {}};
  if (tool_id == "extrude") {
    for (const auto &control : settings.controls) {
      if (control.id != "extrude.type") continue;
      if (control.choice_value == "individual") {
        return {true, std::string(quader_command::kToolsActivateExtrudeIndividual.string())};
      }
      if (control.choice_value == "along_normals") {
        return {true, std::string(quader_command::kToolsActivateExtrudeAlongNormals.string())};
      }
    }
  }
  return {true, quader_tool_activation_command_target_id(tool_id)};
}

/** Keeps native button painting while wrapping long labels in narrow docks. */
class QtCommandsLibraryButton final : public QPushButton {
public:
  explicit QtCommandsLibraryButton(QWidget *parent) : QPushButton(parent) {
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setAutoDefault(false);
    setProperty(kQtMutedDisabledButtonTextProperty, true);
  }

  void set_label(QString label) {
    if (label_ == label) return;
    label_ = std::move(label);
    setAccessibleName(label_);
    update_wrapping();
    updateGeometry();
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    metrics_ = metrics;
    update_wrapping();
    updateGeometry();
  }

  [[nodiscard]] QSize minimumSizeHint() const override {
    return {metrics_.px(80), content_height(fontMetrics().height())};
  }

  [[nodiscard]] QSize sizeHint() const override {
    return {metrics_.px(240), heightForWidth(metrics_.px(240))};
  }

  [[nodiscard]] int heightForWidth(int width) const override {
    const auto rows = wrapped_lines(width);
    return content_height(std::max(1, static_cast<int>(rows.size())) * fontMetrics().height());
  }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QPushButton::resizeEvent(event);
    update_wrapping();
  }

  void changeEvent(QEvent *event) override {
    QPushButton::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange ||
        event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
      update_wrapping();
      updateGeometry();
    }
  }

private:
  [[nodiscard]] int content_height(int text_height) const {
    QStyleOptionButton option;
    initStyleOption(&option);
    if (qt_application_style_active(&option)) {
      return std::max(metrics_.px(22), text_height + metrics_.px(6));
    }
    return std::max(metrics_.px(28), style()->sizeFromContents(
        QStyle::CT_PushButton, &option, {1, text_height}, this).height());
  }

  [[nodiscard]] QStringList wrapped_lines(int width) const {
    const int gutter = std::max(metrics_.px(16),
        2 * style()->pixelMetric(QStyle::PM_ButtonMargin, nullptr, this) +
        2 * style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, this));
    QTextLayout text_layout(label_, font());
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    text_layout.setTextOption(option);
    QStringList rows;
    text_layout.beginLayout();
    for (;;) {
      auto line = text_layout.createLine();
      if (!line.isValid()) break;
      line.setLineWidth(std::max(1, width - gutter));
      rows.push_back(label_.sliced(line.textStart(), line.textLength()).trimmed());
    }
    text_layout.endLayout();
    return rows;
  }

  void update_wrapping() {
    const QString wrapped = wrapped_lines(width()).join(QLatin1Char('\n'));
    if (text() != wrapped) setText(wrapped);
  }

  QString label_;
  QtUiScaleMetrics metrics_;
};

struct CommandsLibraryStamp final {
  QuaderSceneDocumentBinding binding;
  std::array<std::uint64_t, 7> revisions{};
  std::uint64_t context_revision = 0u;
  std::uint64_t tool_availability_revision = 0u;
  std::uint64_t active_tool_revision = 0u;
  ViewportPaneId pane = ViewportPaneId::Perspective;
  std::uint64_t pane_control_revision = 0u;
  std::uint64_t localization_revision = 0u;
  friend bool operator==(const CommandsLibraryStamp &,
                         const CommandsLibraryStamp &) = default;
};

} // namespace

class QtCommandsLibraryPanel::Impl final {
public:
  explicit Impl(QtCommandsLibraryPanel &owner) : owner_(owner) {
    owner_.setObjectName(QStringLiteral("commands_library.panel"));
    owner_.setBackgroundRole(QPalette::Base);
    owner_.setAutoFillBackground(true);
    auto *layout = new QVBoxLayout(&owner_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    scroll_ = new QScrollArea(&owner_);
    scroll_->setObjectName(QStringLiteral("commands_library.scroll"));
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->viewport()->setBackgroundRole(QPalette::Base);
    scroll_->viewport()->setAutoFillBackground(true);
    body_ = new QWidget(scroll_);
    body_->setObjectName(QStringLiteral("commands_library.body"));
    body_->setBackgroundRole(QPalette::Base);
    body_->setAutoFillBackground(true);
    rows_ = new QVBoxLayout(body_);
    rows_->setContentsMargins(8, 8, 8, 8);
    rows_->setSpacing(4);
    rows_->addStretch(1);
    scroll_->setWidget(body_);
    layout->addWidget(scroll_);
    owner_.setWindowTitle(QStringLiteral("Commands Library"));
  }

  struct Entry final {
    QuaderCommandId command_id;
    QtCommandsLibraryButton *button = nullptr;
  };

  void clear() {
    ++binding_generation_;
    rows_binding_.reset();
    for (const auto &entry : entries_) {
      rows_->removeWidget(entry.button);
      QObject::disconnect(entry.button, nullptr, &owner_, nullptr);
      entry.button->hide();
      entry.button->setObjectName(QString{});
      entry.button->setProperty("commandId", QVariant{});
      entry.button->deleteLater();
    }
    entries_.clear();
  }

  [[nodiscard]] CommandsLibraryStamp stamp() const {
    const auto revisions = editor_->api().revisions();
    const auto pane = editor_->viewports().focused_pane();
    return {
        .binding = editor_->query().document_binding(),
        .revisions = {revisions.project_revision, revisions.scene_revision,
            revisions.scene_layer_revision, revisions.scene_settings_revision,
            revisions.asset_revision, revisions.modeling_content_revision,
            revisions.modeling_selection_revision},
        .context_revision = editor_->mode().revision(),
        .tool_availability_revision = editor_->tools().availability_revision(),
        .active_tool_revision = editor_->tools().active_tool_revision(),
        .pane = pane,
        .pane_control_revision = editor_->viewports().control_revision(pane),
        .localization_revision = localization_ != nullptr ? localization_->revision() : 0u,
    };
  }

  void dispatch(QuaderCommandId command_id, std::uint64_t generation) {
    if (editor_ == nullptr || generation != binding_generation_) return;
    const auto *presented = presenter_.presented_stamp();
    if (presented == nullptr || editor_->query().document_binding() != presented->binding) {
      refresh();
      return;
    }
    const auto *presentation = find_command_presentation(command_id);
    const auto mode = editor_->selection().summary().mode;
    if (presentation == nullptr ||
        !has_command_presentation_surface(*presentation, CommandPresentationSurface::CommandsLibrary) ||
        !command_presentation_supports_selection_kind(*presentation, mode) ||
        !release_presents_command(command_id.string())) {
      refresh();
      return;
    }
    const auto operation = library_operation_state(*editor_);
    if (operation.active && operation.command_id != command_id.string()) {
      refresh();
      return;
    }
    const auto dispatched_command = operation.active
        ? quader_command::kToolsActivateSelect : command_id;
    ++work_stats_.descriptor_query_count;
    const auto descriptor = editor_->commands().descriptor(dispatched_command);
    if (!descriptor || !descriptor->enabled) {
      refresh();
      return;
    }
    const QPointer<QtCommandsLibraryPanel> guard(&owner_);
    QuaderEditorApi *const editor = editor_;
    (void)editor->commands().execute(dispatched_command);
    if (!guard || binding_generation_ != generation || editor_ != editor) return;
    refresh();
  }

  void refresh() {
    if (editor_ == nullptr) return;
    const auto next = stamp();
    if (!presenter_.needs_refresh(next)) return;
    const auto mode = editor_->selection().summary().mode;
    ++work_stats_.catalog_projection_count;
    std::vector<QuaderCommandId> commands;
    for (const auto &presentation : command_presentation_catalog()) {
      if (!editor_->commands().workspace_allows(presentation.command_id.string())) continue;
      if (has_command_presentation_surface(presentation, CommandPresentationSurface::CommandsLibrary) &&
          command_presentation_supports_selection_kind(presentation, mode) &&
          release_presents_command(presentation.command_id.string())) {
        commands.push_back(presentation.command_id);
      }
    }
    const bool rebuild = !rows_binding_ || *rows_binding_ != next.binding ||
        entries_.size() != commands.size() ||
        !std::equal(entries_.begin(), entries_.end(), commands.begin(), commands.end(),
            [](const Entry &entry, QuaderCommandId command) {
              return entry.command_id.string() == command.string();
            });
    if (rebuild) {
      clear();
      rows_binding_ = next.binding;
      for (const auto command : commands) {
        auto *button = new QtCommandsLibraryButton(body_);
        button->setObjectName(QStringLiteral("commands_library.command.%1").arg(qtext(command.string())));
        button->setProperty("commandId", qtext(command.string()));
        button->apply_ui_scale(metrics_);
        const auto generation = binding_generation_;
        QObject::connect(button, &QPushButton::clicked, &owner_,
            [this, command, generation] { dispatch(command, generation); });
        rows_->insertWidget(rows_->count() - 1, button);
        entries_.push_back({command, button});
      }
    }
    const auto operation = library_operation_state(*editor_);
    const auto exit_descriptor = operation.active
        ? editor_->commands().descriptor(quader_command::kToolsActivateSelect)
        : std::optional<QuaderCommandDescriptor>{};
    if (operation.active) ++work_stats_.descriptor_query_count;
    for (const auto &entry : entries_) {
      ++work_stats_.descriptor_query_count;
      const auto descriptor = editor_->commands().descriptor(entry.command_id);
      const auto label = descriptor ? std::string_view{descriptor->label} : entry.command_id.string();
      entry.button->set_label(library_command_label(localization_, entry.command_id, label));
      const auto tooltip = library_command_tooltip(localization_, entry.command_id);
      if (entry.button->toolTip() != tooltip) entry.button->setToolTip(tooltip);
      const bool enabled = operation.active
          ? operation.command_id == entry.command_id.string() &&
                exit_descriptor && exit_descriptor->enabled
          : descriptor && descriptor->enabled;
      if (entry.button->isEnabled() != enabled) entry.button->setEnabled(enabled);
    }
    const auto title = localization_ != nullptr
        ? QString::fromStdString(localization_->text("ui.panel.commands_library", "Commands Library"))
        : QStringLiteral("Commands Library");
    if (owner_.windowTitle() != title) owner_.setWindowTitle(title);
    ++work_stats_.presentation_count;
    presenter_.mark_presented(next);
  }

private:
  friend class QtCommandsLibraryPanel;

  QtCommandsLibraryPanel &owner_;
  QuaderEditorApi *editor_ = nullptr;
  const LocalizationService *localization_ = nullptr;
  QScrollArea *scroll_ = nullptr;
  QWidget *body_ = nullptr;
  QVBoxLayout *rows_ = nullptr;
  QtUiScaleMetrics metrics_;
  std::vector<Entry> entries_;
  std::optional<QuaderSceneDocumentBinding> rows_binding_;
  RetainedUiPresenter<CommandsLibraryStamp> presenter_;
  QtCommandsLibraryWorkStats work_stats_;
  std::uint64_t binding_generation_ = 0u;
};

QtCommandsLibraryPanel::QtCommandsLibraryPanel(QWidget *parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this)) {}

QtCommandsLibraryPanel::~QtCommandsLibraryPanel() = default;

void QtCommandsLibraryPanel::bind(QuaderEditorApi &editor) {
  if (impl_->editor_ != &editor) {
    unbind();
    impl_->editor_ = &editor;
  }
  refresh();
}

void QtCommandsLibraryPanel::unbind() {
  impl_->editor_ = nullptr;
  ++impl_->binding_generation_;
  impl_->presenter_.invalidate();
  impl_->clear();
}

void QtCommandsLibraryPanel::set_localization(const LocalizationService &localization) {
  if (impl_->localization_ != &localization) impl_->presenter_.invalidate();
  impl_->localization_ = &localization;
  refresh();
}

void QtCommandsLibraryPanel::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  impl_->metrics_ = metrics;
  impl_->rows_->setContentsMargins(metrics.px(8), metrics.px(8), metrics.px(8), metrics.px(8));
  impl_->rows_->setSpacing(metrics.px(4));
  for (const auto &entry : impl_->entries_) entry.button->apply_ui_scale(metrics);
  updateGeometry();
}

void QtCommandsLibraryPanel::refresh() { impl_->refresh(); }

const QtCommandsLibraryWorkStats &QtCommandsLibraryPanel::work_stats() const {
  return impl_->work_stats_;
}

QSize QtCommandsLibraryPanel::sizeHint() const { return impl_->metrics_.size({320, 360}); }

QSize QtCommandsLibraryPanel::minimumSizeHint() const { return impl_->metrics_.size({160, 120}); }

} // namespace quader
