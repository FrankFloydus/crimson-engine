////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_face_texture_controls.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_settings_control_widgets.hpp"
#include "ui/qt_status_message.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QCheckBox>
#include <QEvent>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QTimer>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace quader {
namespace {

using namespace qt_settings_detail;

// Coalesce numeric edits while preserving their captured target revision.
constexpr int kNumberDispatchIntervalMs = 16;

QString qtext(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString localized_text(const LocalizationService *localization,
                       std::string_view key, std::string_view fallback) {
  return localization != nullptr
             ? QString::fromStdString(localization->text(key, fallback))
             : qtext(fallback);
}

void present_failure(QuaderEditorApi *editor, const char *action,
                     const QuaderApiVoidResult &result) {
  if (result.ok()) {
    return;
  }
  LOG_ERROR("{} failed: {}", action,
            result.error_message().empty() ? "Command failed."
                                           : result.error_message());
  if (editor != nullptr) {
    (void)editor->ui().notify({
        .level = QuaderNotificationLevel::Error,
        .message = result.error_message().empty() ? "Command failed."
                                                : result.error_message(),
    });
  }
}

void present_inline_message(QtStatusMessage &label, const QString &message) {
  label.present(message, false);
  if (label.isHidden() == !message.isEmpty()) {
    label.setVisible(!message.isEmpty());
  }
}

std::string next_texture_edit_merge_key() {
  static std::atomic<std::uint64_t> next_id{1};
  return "texture-settings:" +
         std::to_string(next_id.fetch_add(1, std::memory_order_relaxed));
}

/// Static presentation metadata for one API-owned face texture property.
struct TextureControlDescription {
  QuaderFaceTextureField field;
  std::string_view id;
  std::string_view localization_key;
  std::string_view fallback;
  double step;
  double step_origin;
  std::string_view tooltip;
};

constexpr std::array<TextureControlDescription, 5> kTextureControls{{
    {QuaderFaceTextureField::ScaleX, "scale_x",
     "tool_settings.texture_scale_x", "Scale U", 0.01, 1.0,
     "Texture width (U) scale; its screen direction follows the mapped texture. Larger magnitudes enlarge the texture; negative values mirror it. Authored UVs and material tiling also affect visible density."},
    {QuaderFaceTextureField::ScaleY, "scale_y",
     "tool_settings.texture_scale_y", "Scale V", 0.01, 1.0,
     "Texture height (V) scale; its screen direction follows the mapped texture. Larger magnitudes enlarge the texture; negative values mirror it. Authored UVs and material tiling also affect visible density."},
    {QuaderFaceTextureField::ShiftX, "shift_x",
     "tool_settings.texture_shift_x", "Shift U (px)", 1.0, 0.0,
     "Shift along the texture image's width axis (U), in image pixels. Its screen direction follows the mapped texture. Numeric shifts require one selected face."},
    {QuaderFaceTextureField::ShiftY, "shift_y",
     "tool_settings.texture_shift_y", "Shift V (px)", 1.0, 0.0,
     "Shift along the texture image's height axis (V), in image pixels. Its screen direction follows the mapped texture. Numeric shifts require one selected face."},
    {QuaderFaceTextureField::Rotate, "rotate",
     "tool_settings.texture_rotate", "Rotation (\xC2\xB0)", 1.0, 0.0,
     "Texture rotation in degrees in the face's planar projection. Selecting the face again preserves its angle."},
}};

/// Retained presentation only; each button dispatches the same public action
/// used by the corresponding command, without interpreting UVs in the panel.
struct TextureActionDescription {
  QuaderFaceTextureAction action;
  QuaderCommandId command;
  std::string_view id;
  std::string_view caption;
  std::string_view tooltip;
  std::string_view accessible_name{};
};

constexpr std::array<TextureActionDescription, 17> kTextureActions{{
    {QuaderFaceTextureAction::AlignToGrid,
     quader_command::kModelingTextureAlignToGrid,
     "align_to_grid", "Grid",
     "Project on world axes and reset scale to 0.125 map units per texel, rotation to zero, and shifts to zero."},
    {QuaderFaceTextureAction::AlignToFace,
     quader_command::kModelingTextureAlignToFace,
     "align_to_face", "Face",
     "Project on the face plane while retaining scale, rotation, and shifts. The texture origin moves to a face corner."},
    {QuaderFaceTextureAction::AlignToView,
     quader_command::kModelingTextureAlignToView,
     "align_to_view", "View",
     "Project using the captured view axes while retaining scale, rotation, and shifts. Angled surfaces can appear stretched."},
    {QuaderFaceTextureAction::RotateClockwise,
     quader_command::kModelingTextureRotateClockwise,
     "rotate_clockwise", "\xE2\x86\xBB",
     "Rotate the texture image clockwise by the current rotation step, viewed from the face front."},
    {QuaderFaceTextureAction::RotateCounterClockwise,
     quader_command::kModelingTextureRotateCounterClockwise,
     "rotate_counter_clockwise", "\xE2\x86\xBA",
     "Rotate the texture image counterclockwise by the current rotation step, viewed from the face front."},
    {QuaderFaceTextureAction::FlipTextureHorizontal,
     quader_command::kModelingTextureFlipTextureHorizontal,
     "flip_texture_horizontal", "U",
     "Mirror the texture along its width axis (U) about its chart origin. Keep the current shifts.",
     "Flip Texture U"},
    {QuaderFaceTextureAction::FlipTextureVertical,
     quader_command::kModelingTextureFlipTextureVertical,
     "flip_texture_vertical", "V",
     "Mirror the texture along its height axis (V) about its chart origin. Keep the current shifts.",
     "Flip Texture V"},
    {QuaderFaceTextureAction::FlipFaceHorizontal,
     quader_command::kModelingTextureFlipFaceHorizontal,
     "flip_face_horizontal", "U",
     "Mirror the face UVs along U within their existing bounds. Treat as One uses combined UV bounds across the selection. Geometry and scalar texture settings stay unchanged.",
     "Flip Face UVs U"},
    {QuaderFaceTextureAction::FlipFaceVertical,
     quader_command::kModelingTextureFlipFaceVertical,
     "flip_face_vertical", "V",
     "Mirror the face UVs along V within their existing bounds. Treat as One uses combined UV bounds across the selection. Geometry and scalar texture settings stay unchanged.",
     "Flip Face UVs V"},
    {QuaderFaceTextureAction::FitBoth,
     quader_command::kModelingTextureFitBoth,
     "fit_both", "Both",
     "Scale and shift both texture axes to fit one tile to the face bounds. Treat as One uses shared projection and combined bounds."},
    {QuaderFaceTextureAction::FitX,
     quader_command::kModelingTextureFitX,
     "fit_x", "U",
     "Scale and shift the texture width axis (U) to fit one tile. Keep the V mapping. Treat as One uses a shared projection.",
     "Fit Texture U"},
    {QuaderFaceTextureAction::FitY,
     quader_command::kModelingTextureFitY,
     "fit_y", "V",
     "Scale and shift the texture height axis (V) to fit one tile. Keep the U mapping. Treat as One uses a shared projection.",
     "Fit Texture V"},
    {QuaderFaceTextureAction::JustifyLeft,
     quader_command::kModelingTextureJustifyLeft,
     "justify_left", "\xE2\x86\x90",
     "Shift the texture to the left bound without scaling each face. Treat as One can change density through its shared projection."},
    {QuaderFaceTextureAction::JustifyRight,
     quader_command::kModelingTextureJustifyRight,
     "justify_right", "\xE2\x86\x92",
     "Shift the texture to the right bound without scaling each face. Treat as One can change density through its shared projection."},
    {QuaderFaceTextureAction::JustifyTop,
     quader_command::kModelingTextureJustifyTop,
     "justify_top", "\xE2\x86\x91",
     "Shift the texture to the top bound without scaling each face. Treat as One can change density through its shared projection."},
    {QuaderFaceTextureAction::JustifyBottom,
     quader_command::kModelingTextureJustifyBottom,
     "justify_bottom", "\xE2\x86\x93",
     "Shift the texture to the bottom bound without scaling each face. Treat as One can change density through its shared projection."},
    {QuaderFaceTextureAction::JustifyCenter,
     quader_command::kModelingTextureJustifyCenter,
     "justify_center", "\xE2\x8A\x99",
     "Center the texture within the face bounds without scaling. Treat as One can change density through its shared projection."},
}};

/// Groups consecutive texture actions into one labeled horizontal row.
struct TextureActionGroup {
  std::string_view id;
  std::string_view label;
  std::size_t count;
};

constexpr std::array<TextureActionGroup, 6> kTextureActionGroups{{
    {"align", "Align", 3},
    {"rotate", "Rotate", 2},
    {"flip_texture", "Flip Texture", 2},
    {"flip_face_uvs", "Flip Face UVs", 2},
    {"fit", "Fit", 3},
    {"justify", "Justify", 5},
}};

} // namespace

/// Retains face texture presentation; all mutations belong to the modeling API.
struct QtFaceTextureControls::Impl : public QObject {
  /// Retained input and pending gesture state; the modeling API owns face data.
  struct TextureControlWidget {
    QuaderFaceTextureField field = QuaderFaceTextureField::ScaleX;
    QtMixedNumberSpinBox *spin = nullptr;
    QLabel *label = nullptr;
    QTimer *dispatch_timer = nullptr;
    std::optional<double> pending_value;
    std::optional<double> last_requested_value;
    std::optional<QuaderFaceTextureRevision> gesture_revision;
    std::uint64_t selection_token = 0;
    std::string merge_key;
    bool editing = false;
    bool invalid_value = false;
    bool defer_drag_publication = false;
  };

  explicit Impl(QtFaceTextureControls &panel) : QObject(&panel), panel_(panel) {
    panel_.setObjectName(QStringLiteral("face_texture.controls"));
    panel_.setMinimumWidth(240);
    auto *layout = new QVBoxLayout(&panel_);
    layout->setObjectName(QStringLiteral("face_texture.controls.layout"));
    set_scalable_margins(*layout, QMargins(4, 0, 4, 0));
    set_scalable_spacing(*layout, 4);
    layout->setAlignment(Qt::AlignTop);
    texture_section_ = new QtSettingsSection(
        QStringLiteral("face_texture.section.texture"), &panel_);
    build_texture_controls();
    texture_section_->content()->installEventFilter(this);
    layout->addWidget(texture_section_);
    edit_texture_section_ = new QtSettingsSection(
        QStringLiteral("face_texture.section.edit_texture"), &panel_);
    build_texture_actions();
    layout->addWidget(edit_texture_section_);
    refresh_localized_text();
    refresh_texture_settings();
  }

  ~Impl() override { discard_texture_dispatches(); }

  void bind(QuaderEditorApi &editor) {
    unbind();
    editor_ = &editor;
    refresh();
  }

  void unbind() {
    ++binding_generation_;
    editor_ = nullptr;
    texture_synchronizing_ = false;
    discard_texture_dispatches();
    texture_settings_.reset();
    texture_revision_.reset();
    refresh_texture_settings();
  }

  void set_localization(const LocalizationService &localization) {
    localization_ = &localization;
    texture_revision_.reset();
    localized_text_dirty_ = true;
    refresh();
  }

  void refresh() {
    refresh_localized_text();
    refresh_texture_settings();
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    if (ui_scale_metrics_.revision() == metrics.revision() &&
        qFuzzyCompare(static_cast<qreal>(ui_scale_metrics_.factor()),
                      static_cast<qreal>(metrics.factor()))) {
      return;
    }
    ui_scale_metrics_ = metrics;
    apply_scalable_layout_metrics(panel_, metrics);
    apply_scalable_widget_metrics(panel_, metrics);
    refresh_control_metrics();
    schedule_control_metrics();
    panel_.updateGeometry();
  }

  [[nodiscard]] bool numeric_interaction_active() const {
    return std::any_of(texture_controls_.begin(), texture_controls_.end(),
                       [](const TextureControlWidget &binding) {
                         return binding.editing || binding.pending_value.has_value();
                       });
  }

private:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (texture_section_ != nullptr && watched == texture_section_->content() &&
        (event->type() == QEvent::Resize ||
         event->type() == QEvent::LayoutRequest)) {
      schedule_control_metrics();
    }
    return QObject::eventFilter(watched, event);
  }

  void refresh_control_metrics() {
    int numeric_label_width = 0;
    for (const TextureControlWidget &binding : texture_controls_) {
      numeric_label_width = std::max(numeric_label_width,
          binding.label->fontMetrics().horizontalAdvance(binding.label->text()));
    }
    numeric_label_width =
        std::min(numeric_label_width, ui_scale_metrics_.px(72));
    QWidget *numeric_content = texture_section_->content();
    auto *numeric_layout = qobject_cast<QGridLayout *>(numeric_content->layout());
    if (numeric_layout != nullptr && numeric_content->width() > 0) {
      int first_field_width = 0;
      int second_field_width = 0;
      for (std::size_t index = 0; index < 4; ++index) {
        int &field_width = index % 2 == 0 ? first_field_width : second_field_width;
        field_width = std::max(field_width,
                               texture_controls_[index].spin->minimumWidth());
      }
      const QMargins margins = numeric_layout->contentsMargins();
      const int available_label_width =
          (numeric_content->width() - margins.left() - margins.right() -
           3 * numeric_layout->horizontalSpacing() - first_field_width -
           second_field_width) /
          2;
      // Compact numeric fields reserve their readable value and hover arrows.
      // Wrap labels first when a narrow dock cannot fit both complete labels.
      numeric_label_width = std::min(
          numeric_label_width, std::max(1, available_label_width));
    }
    int action_label_width = ui_scale_metrics_.px(96);
    for (const QLabel *label : texture_action_group_labels_) {
      action_label_width = std::max(action_label_width,
          label->fontMetrics().horizontalAdvance(label->text()));
    }
    action_label_width =
        std::min(action_label_width, ui_scale_metrics_.px(144));
    const auto align_label = [](QLabel *label, int label_width) {
      if (label->minimumWidth() != label_width ||
          label->maximumWidth() != label_width) {
        label->setFixedWidth(label_width);
      }
    };
    for (const TextureControlWidget &binding : texture_controls_) {
      align_label(binding.label, numeric_label_width);
    }
    if (numeric_layout != nullptr) {
      std::array<int, 3> row_heights{};
      for (std::size_t index = 0; index < texture_controls_.size(); ++index) {
        const TextureControlWidget &binding = texture_controls_[index];
        row_heights[index / 2] = std::max(
            row_heights[index / 2],
            std::max(binding.label->heightForWidth(numeric_label_width),
                     binding.spin->minimumSizeHint().height()));
      }
      bool row_metrics_changed = false;
      const QMargins margins = numeric_layout->contentsMargins();
      int minimum_height = margins.top() + margins.bottom() +
                           2 * numeric_layout->verticalSpacing();
      for (std::size_t row = 0; row < row_heights.size(); ++row) {
        const int grid_row = static_cast<int>(row);
        if (numeric_layout->rowMinimumHeight(grid_row) != row_heights[row]) {
          numeric_layout->setRowMinimumHeight(grid_row, row_heights[row]);
          row_metrics_changed = true;
        }
        minimum_height += row_heights[row];
      }
      if (numeric_content->minimumHeight() != minimum_height) {
        numeric_content->setMinimumHeight(minimum_height);
        row_metrics_changed = true;
      }
      if (row_metrics_changed) {
        // Publish the wrapped rows through the retained section to the scroll
        // area's natural-height fit instead of enlarging labels in old slots.
        texture_section_->layout()->invalidate();
        texture_section_->updateGeometry();
        panel_.layout()->invalidate();
        panel_.updateGeometry();
      }
    }
    for (QLabel *label : texture_action_group_labels_) {
      align_label(label, action_label_width);
    }
    for (QPushButton *button : texture_action_buttons_) {
      const int width = button->fontMetrics().horizontalAdvance(button->text()) +
                        ui_scale_metrics_.px(12);
      if (button->minimumWidth() != width) {
        button->setMinimumWidth(width);
      }
    }
  }

  void schedule_control_metrics() {
    if (control_metrics_scheduled_) {
      return;
    }
    control_metrics_scheduled_ = true;
    // Application-font propagation completes after the scale listener returns.
    QTimer::singleShot(0, this, [this]() {
      control_metrics_scheduled_ = false;
      refresh_control_metrics();
    });
  }

  [[nodiscard]] bool binding_is_current(QuaderEditorApi *editor,
                                        std::uint64_t generation) const {
    return editor_ == editor && binding_generation_ == generation;
  }

  void refresh_texture_settings(bool force = false) {
    if (editor_ == nullptr) {
      discard_texture_dispatches();
      texture_settings_.reset();
      texture_revision_.reset();
      if (!texture_section_->isHidden()) {
        texture_section_->hide();
      }
      if (!edit_texture_section_->isHidden()) {
        edit_texture_section_->hide();
      }
      return;
    }

    QuaderEditorApi *const editor = editor_;
    const std::uint64_t generation = binding_generation_;
    QuaderFaceTextureRevision revision =
        editor_->modeling().face_texture_settings_revision();
    const std::uint64_t keybindings_revision =
        editor_->preferences().keybindings_revision();
    const double rotation_step = editor_->preferences().rotation_snap_degrees();
    // Do not query selected faces, allocate descriptions, or touch Qt controls
    // on an unchanged viewport frame.
    if (!force && texture_revision_.has_value() &&
        *texture_revision_ == revision &&
        texture_keybindings_revision_ == keybindings_revision &&
        texture_rotation_step_ == rotation_step) {
      return;
    }
    QuaderFaceTextureSettings settings =
        editor_->modeling().face_texture_settings();
    if (!binding_is_current(editor, generation)) {
      return;
    }
    // Pending edits belong to the exact context returned by the settings query.
    revision = editor_->modeling().face_texture_settings_revision();
    const bool selection_changed =
        !texture_settings_.has_value() ||
        texture_settings_->selection_token != settings.selection_token;
    texture_synchronizing_ = true;
    for (TextureControlWidget &binding : texture_controls_) {
      if (selection_changed ||
          (binding.gesture_revision.has_value() &&
           *binding.gesture_revision != revision)) {
        discard_texture_dispatch(binding);
      }
    }
    const bool has_faces = settings.selected_face_count > 0;
    if (texture_section_->isHidden()) {
      texture_section_->setVisible(true);
    }
    if (edit_texture_section_->isHidden()) {
      edit_texture_section_->setVisible(true);
    }

    QString disabled_reason;
    if (has_faces && !settings.editable) {
      disabled_reason =
          settings.block_reason == QuaderFaceTextureBlockReason::PreviewActive
              ? localized_text(
                    localization_, "tool_settings.texture_preview_active",
                    "Finish or cancel the active preview to edit textures.")
              : localized_text(
                    localization_, "tool_settings.texture_target_unavailable",
                    "The selected faces are unavailable, locked, hidden, or "
                    "outside the instance edit scope.");
    }
    if (texture_disabled_reason_->text() != disabled_reason) {
      texture_disabled_reason_->setText(disabled_reason);
    }
    if (texture_disabled_reason_->isHidden() == !disabled_reason.isEmpty()) {
      texture_disabled_reason_->setVisible(!disabled_reason.isEmpty());
    }
    present_texture_action_message(disabled_reason);

    const bool single_face = settings.selected_face_count == 1;
    const std::array<std::optional<double>, 5> values{
        settings.scale_x, settings.scale_y,
        single_face ? std::optional<double>(settings.shift_x) : std::nullopt,
        single_face ? std::optional<double>(settings.shift_y) : std::nullopt,
        settings.rotation_mixed ? std::nullopt
                                : std::optional<double>(settings.rotation_degrees)};
    for (std::size_t index = 0; index < texture_controls_.size(); ++index) {
      TextureControlWidget &binding = texture_controls_[index];
      const bool shift = binding.field == QuaderFaceTextureField::ShiftX ||
                         binding.field == QuaderFaceTextureField::ShiftY;
      const bool enabled =
          has_faces && settings.editable && (!shift || single_face);
      if (binding.spin->isReadOnly() != !has_faces) {
        binding.spin->setReadOnly(!has_faces);
      }
      if (binding.spin->isEnabled() != enabled) {
        binding.spin->setEnabled(enabled);
      }
      if (binding.label->isEnabled() != enabled) {
        binding.label->setEnabled(enabled);
      }
      if (!binding.editing) {
        binding.spin->present(has_faces ? values[index] : std::nullopt,
                              has_faces ? QStringLiteral("--")
                                        : QStringLiteral("\u2014"));
      }
    }
    for (QPushButton *button : texture_action_buttons_) {
      if (button->isEnabled() != settings.editable) {
        button->setEnabled(settings.editable);
      }
    }
    if (texture_treat_as_one_->isEnabled() != has_faces) {
      texture_treat_as_one_->setEnabled(has_faces);
    }
    if (texture_treat_as_one_->isChecked() != settings.treat_as_one) {
      const QSignalBlocker blocker(texture_treat_as_one_);
      texture_treat_as_one_->setChecked(settings.treat_as_one);
    }
    refresh_texture_action_tooltips(rotation_step);
    texture_settings_ = std::move(settings);
    texture_revision_ = revision;
    texture_keybindings_revision_ = keybindings_revision;
    texture_rotation_step_ = rotation_step;
    texture_synchronizing_ = false;
  }

  void build_texture_actions() {
    auto *layout = new QGridLayout(edit_texture_section_->content());
    set_scalable_margins(*layout, QMargins(6, 6, 6, 6));
    set_scalable_grid_spacing(*layout, 6, 4);
    layout->setAlignment(Qt::AlignTop);
    layout->setColumnStretch(1, 1);
    std::size_t action_index = 0;
    for (std::size_t group_index = 0;
         group_index < kTextureActionGroups.size(); ++group_index) {
      const TextureActionGroup &group = kTextureActionGroups[group_index];
      auto *label = new QLabel(edit_texture_section_->content());
      label->setObjectName(QStringLiteral("face_texture.edit_texture.group.%1")
                               .arg(qtext(group.id)));
      label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
      label->setWordWrap(true);
      texture_action_group_labels_[group_index] = label;
      layout->addWidget(label, static_cast<int>(group_index), 0);
      auto *buttons = new QHBoxLayout;
      set_scalable_margins(*buttons, QMargins(0, 0, 0, 0));
      set_scalable_spacing(*buttons, 4);
      for (std::size_t offset = 0; offset < group.count;
           ++offset, ++action_index) {
        const TextureActionDescription &description =
            kTextureActions[action_index];
        auto *button = new QPushButton(edit_texture_section_->content());
        button->setObjectName(QStringLiteral("face_texture.edit_texture.%1")
                                  .arg(qtext(description.id)));
        button->setAutoDefault(false);
        // Validate the draft before mouse focus-out can replace invalid text
        // with the spin box's previous value. Keyboard traversal stays native.
        button->setFocusPolicy(Qt::TabFocus);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        button->setMinimumWidth(0);
        buttons->addWidget(button, 1);
        texture_action_buttons_[action_index] = button;
        QObject::connect(button, &QPushButton::clicked, &panel_,
                         [this, action_index]() {
                           dispatch_texture_action(action_index);
                         });
      }
      layout->addLayout(buttons, static_cast<int>(group_index), 1);
    }
    texture_treat_as_one_ = new QCheckBox(edit_texture_section_->content());
    texture_treat_as_one_->setObjectName(
        QStringLiteral("face_texture.edit_texture.treat_as_one"));
    texture_treat_as_one_->setFocusPolicy(Qt::TabFocus);
    layout->addWidget(texture_treat_as_one_,
                      static_cast<int>(kTextureActionGroups.size()), 1);
    QObject::connect(texture_treat_as_one_, &QCheckBox::toggled, &panel_,
                     [this](bool enabled) {
                       if (editor_ == nullptr || texture_synchronizing_) {
                         return;
                       }
                       QuaderEditorApi *const editor = editor_;
                       const std::uint64_t generation = binding_generation_;
                       const QuaderFaceTextureRevision before =
                           editor_->modeling().face_texture_settings_revision();
                       const QuaderApiVoidResult result =
                           editor_->modeling().set_face_texture_treat_as_one(
                               enabled);
                       if (!binding_is_current(editor, generation)) {
                         return;
                       }
                       present_failure(editor_, "Treat selected faces as one", result);
                       if (!binding_is_current(editor, generation)) {
                         return;
                       }
                       if (result.ok()) {
                         // This session option does not commit numeric drafts.
                         // Rebind only drafts from the same unchanged targets.
                         const QuaderFaceTextureRevision after =
                             editor_->modeling().face_texture_settings_revision();
                         for (TextureControlWidget &binding : texture_controls_) {
                           if (binding.gesture_revision == before) {
                             binding.gesture_revision = after;
                           }
                         }
                       }
                       refresh_texture_settings(true);
                     });
    texture_action_message_ = new QtStatusMessage(edit_texture_section_->content());
    texture_action_message_->setObjectName(
        QStringLiteral("face_texture.edit_texture.message"));
    texture_action_message_->setWordWrap(true);
    texture_action_message_->hide();
    layout->addWidget(texture_action_message_,
                      static_cast<int>(kTextureActionGroups.size()) + 1, 0, 1, 2);
  }

  void present_texture_action_message(const QString &message) {
    present_inline_message(*texture_action_message_, message);
  }

  void refresh_texture_action_tooltips(double rotation_step) {
    for (std::size_t index = 0; index < kTextureActions.size(); ++index) {
      const TextureActionDescription &description = kTextureActions[index];
      QPushButton *button = texture_action_buttons_[index];
      QString tooltip = button->accessibleName();
      if (description.action == QuaderFaceTextureAction::RotateClockwise ||
          description.action == QuaderFaceTextureAction::RotateCounterClockwise) {
        tooltip.append(QStringLiteral(" (%1\u00B0)").arg(rotation_step, 0, 'g', 6));
      }
      const std::string shortcut =
          editor_->commands().shortcut_for(description.command);
      if (!shortcut.empty()) {
        tooltip.append(QStringLiteral(" [%1]").arg(qtext(shortcut)));
      }
      tooltip.append(QStringLiteral("\n") + localized_text(
          localization_, std::string("tool_settings.edit_texture.") +
                             std::string(description.id) + ".tooltip",
          description.tooltip));
      if (button->toolTip() != tooltip) {
        button->setToolTip(tooltip);
      }
    }
  }

  void build_texture_controls() {
    auto *layout = new QGridLayout(texture_section_->content());
    set_scalable_margins(*layout, QMargins(6, 6, 6, 6));
    set_scalable_grid_spacing(*layout, 6, 4);
    layout->setColumnStretch(1, 1);
    layout->setColumnStretch(3, 1);
    for (std::size_t index = 0; index < kTextureControls.size(); ++index) {
      const TextureControlDescription &description = kTextureControls[index];
      TextureControlWidget &binding = texture_controls_[index];
      binding.field = description.field;
      const QString name =
          QStringLiteral("face_texture.texture.%1").arg(qtext(description.id));
      binding.label = new QLabel(texture_section_->content());
      binding.label->setObjectName(name + QStringLiteral(".label"));
      binding.label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
      binding.label->setWordWrap(true);
      const bool scale = binding.field == QuaderFaceTextureField::ScaleX ||
                         binding.field == QuaderFaceTextureField::ScaleY;
      binding.spin = new QtMixedNumberSpinBox(
          description.step_origin, texture_section_->content(), scale);
      binding.spin->setObjectName(name);
      binding.spin->setSingleStep(description.step);
      set_scalable_minimum_width(*binding.spin, 64);
      binding.label->setBuddy(binding.spin);
      const int row = static_cast<int>(index / 2);
      const int column = static_cast<int>(index % 2) * 2;
      const bool rotation = binding.field == QuaderFaceTextureField::Rotate;
      layout->addWidget(binding.label, row, column);
      // Center fixed-height fields without capping a wrapped label's row.
      layout->addWidget(binding.spin, row, column + 1, 1, rotation ? 3 : 1,
                         Qt::AlignVCenter);
      binding.dispatch_timer = new QTimer(binding.spin);
      binding.dispatch_timer->setSingleShot(true);
      binding.dispatch_timer->setInterval(kNumberDispatchIntervalMs);
      binding.dispatch_timer->setTimerType(Qt::PreciseTimer);
      QObject::connect(binding.dispatch_timer, &QTimer::timeout, &panel_,
                       [this, index]() { flush_texture_dispatch(index); });
      binding.spin->edited = [this, index](std::optional<double> value) {
        queue_texture_dispatch(index, value);
      };
      auto &numeric = qt_install_numeric_spin_box(*binding.spin);
      numeric.can_commit_text = [this, index]() {
        return !texture_controls_[index].invalid_value;
      };
      if (scale) {
        numeric.set_drag_sensitivity(description.step);
      }
      numeric.drag_started = [this, index]() {
        TextureControlWidget &current = texture_controls_[index];
        if (editor_ == nullptr || texture_synchronizing_ ||
            !texture_settings_.has_value() || !texture_revision_.has_value() ||
            *texture_revision_ !=
                editor_->modeling().face_texture_settings_revision()) {
          discard_texture_dispatch(current);
          refresh_texture_settings(true);
          return;
        }
        // A mixed field has no single value to restore on cancellation. Keep
        // its draft local until release; ordinary numeric drags stay live.
        current.defer_drag_publication =
            (current.field == QuaderFaceTextureField::ScaleX &&
             !texture_settings_->scale_x.has_value()) ||
            (current.field == QuaderFaceTextureField::ScaleY &&
             !texture_settings_->scale_y.has_value()) ||
            (current.field == QuaderFaceTextureField::Rotate &&
             texture_settings_->rotation_mixed);
        if (!current.gesture_revision.has_value()) {
          current.gesture_revision = *texture_revision_;
          current.selection_token = texture_settings_->selection_token;
          current.merge_key = next_texture_edit_merge_key();
        }
        current.editing = true;
      };
      numeric.interaction_canceled = [this, index]() {
        if (texture_synchronizing_) {
          return;
        }
        const std::uint64_t generation = binding_generation_;
        if (!texture_controls_[index].defer_drag_publication) {
          // The control restored its original scalar through edited(). Flush
          // that final sample before releasing the existing merge identity.
          flush_texture_dispatch(index);
        }
        if (generation != binding_generation_) {
          return;
        }
        discard_texture_dispatch(texture_controls_[index]);
        refresh_texture_settings(true);
      };
      QObject::connect(binding.spin, &QDoubleSpinBox::editingFinished, &panel_,
                       [this, index]() {
                         if (texture_synchronizing_) {
                           return;
                         }
                         const std::uint64_t generation = binding_generation_;
                         flush_texture_dispatch(index);
                         if (generation != binding_generation_) {
                           return;
                         }
                         discard_texture_dispatch(texture_controls_[index]);
                         refresh_texture_settings(true);
                       });
    }
    texture_disabled_reason_ = new QLabel(texture_section_->content());
    texture_disabled_reason_->setObjectName(
        QStringLiteral("face_texture.texture.disabled_reason"));
    texture_disabled_reason_->setWordWrap(true);
    texture_disabled_reason_->hide();
    layout->addWidget(texture_disabled_reason_, 3, 0, 1, 4);
  }

  void queue_texture_dispatch(std::size_t index, std::optional<double> value) {
    if (editor_ == nullptr || texture_synchronizing_ ||
        !texture_settings_.has_value() || !texture_settings_->editable ||
        !texture_revision_.has_value()) {
      return;
    }
    TextureControlWidget &binding = texture_controls_[index];
    if (!binding.spin->isEnabled()) {
      return;
    }
    if (binding.defer_drag_publication &&
        qt_install_numeric_spin_box(*binding.spin).is_canceling()) {
      return;
    }
    binding.editing = true;
    if (!binding.gesture_revision.has_value()) {
      binding.gesture_revision = *texture_revision_;
      binding.selection_token = texture_settings_->selection_token;
      binding.merge_key = next_texture_edit_merge_key();
    }
    const bool scale = binding.field == QuaderFaceTextureField::ScaleX ||
                       binding.field == QuaderFaceTextureField::ScaleY;
    binding.invalid_value = !value.has_value() || !std::isfinite(*value) ||
                            (scale && *value == 0.0);
    if (binding.invalid_value) {
      binding.pending_value.reset();
      binding.last_requested_value.reset();
      binding.dispatch_timer->stop();
      return;
    }
    if (binding.last_requested_value == value) {
      return;
    }
    binding.last_requested_value = value;
    binding.pending_value = value;
    if (!binding.defer_drag_publication) {
      const bool dragging =
          qt_install_numeric_spin_box(*binding.spin).is_dragging();
      if (!dragging || !binding.dispatch_timer->isActive()) {
        binding.dispatch_timer->start();
      }
    }
  }

  bool flush_texture_dispatch(std::size_t index) {
    TextureControlWidget &binding = texture_controls_[index];
    if (!binding.pending_value.has_value()) {
      return !binding.invalid_value;
    }
    if (binding.defer_drag_publication &&
        qt_install_numeric_spin_box(*binding.spin).is_dragging()) {
      return true;
    }
    binding.dispatch_timer->stop();
    if (editor_ == nullptr || !binding.gesture_revision.has_value() ||
        editor_->modeling().face_texture_settings_revision() !=
            *binding.gesture_revision) {
      discard_texture_dispatch(binding);
      schedule_texture_refresh();
      return false;
    }
    QuaderEditorApi *const editor = editor_;
    const std::uint64_t generation = binding_generation_;
    const QuaderFaceTextureRevision previous_revision =
        *binding.gesture_revision;
    const double value = *binding.pending_value;
    binding.pending_value.reset();
    const QuaderApiVoidResult result =
        editor_->modeling().set_face_texture_setting(
            QuaderFaceTextureEdit{.field = binding.field,
                                  .value = value,
                                  .selection_token = binding.selection_token,
                                  .merge_key = binding.merge_key});
    if (!binding_is_current(editor, generation)) {
      return false;
    }
    present_failure(editor_, "Face texture settings update", result);
    if (!binding_is_current(editor, generation)) {
      return false;
    }
    if (result.ok()) {
      present_texture_action_message({});
      const QuaderFaceTextureRevision revision =
          editor_->modeling().face_texture_settings_revision();
      for (TextureControlWidget &other : texture_controls_) {
        if (other.gesture_revision == previous_revision) {
          other.gesture_revision = revision;
        }
      }
    } else {
      discard_texture_dispatch(binding);
    }
    schedule_texture_refresh();
    return result.ok();
  }

  bool flush_texture_inputs_for_action() {
    if (editor_ == nullptr || !texture_settings_.has_value() ||
        !texture_revision_.has_value() || !texture_settings_->editable) {
      return false;
    }
    const QuaderFaceTextureRevision current =
        editor_->modeling().face_texture_settings_revision();
    // A live numeric gesture may have published since the last presentation.
    // Its exact revision is the only permitted extension of the captured key.
    const bool current_capture =
        *texture_revision_ == current ||
        std::any_of(texture_controls_.begin(), texture_controls_.end(),
                    [this, &current](const TextureControlWidget &binding) {
                      return binding.gesture_revision == current &&
                             binding.selection_token ==
                                 texture_settings_->selection_token;
                    });
    const bool stale_draft = std::any_of(
        texture_controls_.begin(), texture_controls_.end(),
        [&current](const TextureControlWidget &binding) {
          return binding.editing && binding.gesture_revision != current;
        });
    if (!current_capture || stale_draft) {
      discard_texture_dispatches();
      refresh_texture_settings(true);
      const QString error = localized_text(
          localization_, "tool_settings.edit_texture.stale_selection",
          "The face selection or texture settings changed. Review the current "
          "selection and try again.");
      present_failure(editor_, "Face texture action",
                      {.ok = false, .message = error.toStdString()});
      return false;
    }
    if (std::any_of(texture_controls_.begin(), texture_controls_.end(),
                    [](const TextureControlWidget &binding) {
                      return binding.invalid_value;
                    })) {
      const QString error = localized_text(
          localization_, "tool_settings.edit_texture.invalid_edit",
          "Finish a valid texture value before applying an action.");
      present_failure(editor_, "Face texture action",
                      {.ok = false, .message = error.toStdString()});
      return false;
    }
    for (std::size_t index = 0; index < texture_controls_.size(); ++index) {
      if (!flush_texture_dispatch(index)) {
        return false;
      }
    }
    // An action is never part of the preceding numeric merge gesture. Present
    // accepted text before moving focus so Qt cannot publish the old draft.
    discard_texture_dispatches();
    refresh_texture_settings(true);
    return true;
  }

  void dispatch_texture_action(std::size_t index) {
    if (editor_ == nullptr || !texture_settings_.has_value()) {
      return;
    }
    QuaderEditorApi *const editor = editor_;
    const std::uint64_t generation = binding_generation_;
    const std::uint64_t selection_token = texture_settings_->selection_token;
    if (!flush_texture_inputs_for_action() ||
        !binding_is_current(editor, generation)) {
      return;
    }
    const QuaderApiVoidResult result =
        editor_->modeling().apply_face_texture_action(
            QuaderFaceTextureActionRequest{
                .action = kTextureActions[index].action,
                .selection_token = selection_token});
    if (!binding_is_current(editor, generation)) {
      return;
    }
    present_failure(editor_, "Face texture action", result);
    if (!binding_is_current(editor, generation)) {
      return;
    }
    refresh_texture_settings(true);
    if (binding_is_current(editor, generation)) {
      texture_action_buttons_[index]->setFocus(Qt::MouseFocusReason);
    }
  }

  void discard_texture_dispatch(TextureControlWidget &binding) {
    binding.dispatch_timer->stop();
    binding.spin->abandon_edit();
    binding.pending_value.reset();
    binding.last_requested_value.reset();
    binding.gesture_revision.reset();
    binding.selection_token = 0;
    binding.merge_key.clear();
    binding.editing = false;
    binding.invalid_value = false;
    binding.defer_drag_publication = false;
  }

  void discard_texture_dispatches() {
    for (TextureControlWidget &binding : texture_controls_) {
      discard_texture_dispatch(binding);
    }
  }

  void schedule_texture_refresh() {
    if (texture_refresh_scheduled_) {
      return;
    }
    texture_refresh_scheduled_ = true;
    QTimer::singleShot(0, &panel_, [this]() {
      texture_refresh_scheduled_ = false;
      refresh_texture_settings();
    });
  }

  void refresh_localized_text() {
    if (!localized_text_dirty_) {
      return;
    }
    localized_text_dirty_ = false;
    texture_section_->set_title(localized_text(
        localization_, "tool_settings.texture_section", "Texture Settings"));
    edit_texture_section_->set_title(localized_text(
        localization_, "tool_settings.edit_texture_section", "Edit Texture"));
    for (std::size_t index = 0; index < kTextureActionGroups.size(); ++index) {
      const TextureActionGroup &group = kTextureActionGroups[index];
      const QString label = localized_text(
          localization_, std::string("tool_settings.edit_texture.group.") +
                             std::string(group.id),
          group.label);
      if (texture_action_group_labels_[index]->text() != label) {
        texture_action_group_labels_[index]->setText(label);
      }
    }
    for (std::size_t index = 0; index < kTextureActions.size(); ++index) {
      const TextureActionDescription &description = kTextureActions[index];
      QPushButton *button = texture_action_buttons_[index];
      const QString caption = localized_text(
          localization_, std::string("tool_settings.edit_texture.") +
                             std::string(description.id),
          description.caption);
      const QString label = localized_text(
          localization_, std::string("command.modeling.texture.") +
                             std::string(description.id),
          description.accessible_name.empty()
              ? quader_face_texture_action_label(description.action)
              : description.accessible_name);
      if (button->text() != caption) {
        button->setText(caption);
      }
      if (button->accessibleName() != label) {
        button->setAccessibleName(label);
      }
    }
    const QString treat_as_one = localized_text(
        localization_, "tool_settings.edit_texture.treat_as_one", "Treat as One");
    if (texture_treat_as_one_->text() != treat_as_one) {
      texture_treat_as_one_->setText(treat_as_one);
      texture_treat_as_one_->setAccessibleName(treat_as_one);
    }
    const QString treat_as_one_tooltip = localized_text(
        localization_, "tool_settings.edit_texture.treat_as_one_tooltip",
        "Use shared projection and combined bounds for Fit and Justify. "
        "Flip Face UVs mirrors within the combined existing UV bounds. "
        "Other actions remain relative to each face.");
    if (texture_treat_as_one_->toolTip() != treat_as_one_tooltip) {
      texture_treat_as_one_->setToolTip(treat_as_one_tooltip);
    }
    for (std::size_t index = 0; index < kTextureControls.size(); ++index) {
      const TextureControlDescription &description = kTextureControls[index];
      TextureControlWidget &binding = texture_controls_[index];
      const QString label = localized_text(localization_,
                                            description.localization_key,
                                            description.fallback);
      if (binding.label->text() != label) {
        binding.label->setText(label);
      }
      if (binding.spin->accessibleName() != label) {
        binding.spin->setAccessibleName(label);
      }
      const QString tooltip = localized_text(
          localization_, std::string(description.localization_key) + ".tooltip",
          description.tooltip);
      if (binding.spin->toolTip() != tooltip) {
        binding.spin->setToolTip(tooltip);
        binding.label->setToolTip(tooltip);
      }
    }
    refresh_control_metrics();
    schedule_control_metrics();
  }

  QtFaceTextureControls &panel_;
  QtUiScaleMetrics ui_scale_metrics_;
  const LocalizationService *localization_ = nullptr;
  QuaderEditorApi *editor_ = nullptr;
  std::uint64_t binding_generation_ = 0;
  QtSettingsSection *texture_section_ = nullptr;
  QLabel *texture_disabled_reason_ = nullptr;
  QtSettingsSection *edit_texture_section_ = nullptr;
  QtStatusMessage *texture_action_message_ = nullptr;
  QCheckBox *texture_treat_as_one_ = nullptr;
  std::array<TextureControlWidget, 5> texture_controls_;
  std::array<QPushButton *, 17> texture_action_buttons_{};
  std::array<QLabel *, 6> texture_action_group_labels_{};
  std::optional<QuaderFaceTextureSettings> texture_settings_;
  std::optional<QuaderFaceTextureRevision> texture_revision_;
  std::uint64_t texture_keybindings_revision_ = 0;
  double texture_rotation_step_ = 0.0;
  bool texture_synchronizing_ = false;
  bool texture_refresh_scheduled_ = false;
  bool localized_text_dirty_ = true;
  bool control_metrics_scheduled_ = false;
};

QtFaceTextureControls::QtFaceTextureControls(QWidget *parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this)) {}

QtFaceTextureControls::~QtFaceTextureControls() = default;

void QtFaceTextureControls::bind(QuaderEditorApi &editor) { impl_->bind(editor); }

void QtFaceTextureControls::unbind() { impl_->unbind(); }

void QtFaceTextureControls::abandon_runtime_binding() { impl_->unbind(); }

void QtFaceTextureControls::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  impl_->apply_ui_scale(metrics);
}

void QtFaceTextureControls::set_localization(const LocalizationService &localization) {
  impl_->set_localization(localization);
}

void QtFaceTextureControls::refresh() { impl_->refresh(); }

bool QtFaceTextureControls::numeric_interaction_active() const {
  return impl_->numeric_interaction_active();
}

} // namespace quader
