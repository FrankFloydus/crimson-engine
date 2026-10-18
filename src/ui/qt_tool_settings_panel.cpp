////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_tool_settings_panel.hpp"

#include "ui/qt_settings_control_widgets.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_panel_section_header.hpp"
#include "ui/qt_semantic_axis_colors.hpp"
#include "ui/qt_spin_box.hpp"
#include "ui/qt_tool_settings_drag_spinner.hpp"
#include "ui/qt_tool_settings_number_slider.hpp"
#include "ui/qt_ui_scale_metrics.hpp"


#include <QApplication>
#include <QByteArray>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFrame>
#include <QFontMetrics>
#include <QFocusEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMargins>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QWindow>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {

using namespace qt_settings_detail;

// Geometry previews can be substantially slower than raw pointer delivery.
// Pointer drags publish the latest value at this bounded cadence; keyboard,
// wheel, and spin edits use it as a trailing coalescing window.
constexpr int kNumberDispatchIntervalMs = 16;
constexpr int kMinimumControlLabelWidth = 88;
constexpr int kMaximumControlLabelWidth = 220;
constexpr int kControlColumnSpacing = 8;

/// Font propagation may arrive after the host applies its scaled metrics.
class QtToolSettingLabel final : public QLabel {
public:
  using QLabel::QLabel;
  std::function<void()> metrics_changed;

protected:
  void changeEvent(QEvent *event) override {
    QLabel::changeEvent(event);
    if ((event->type() == QEvent::FontChange ||
         event->type() == QEvent::StyleChange) && metrics_changed) {
      metrics_changed();
    }
  }
};

QString qtext(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString localized_text(const LocalizationService *localization,
                       std::string_view key, std::string_view fallback) {
  return localization != nullptr
             ? QString::fromStdString(localization->text(key, fallback))
             : qtext(fallback);
}

QString localized_target_title(const LocalizationService *localization,
                               const QuaderToolSettingsTarget &target) {
  if (!target.title_key.empty()) {
    return localized_text(localization, target.title_key, target.title);
  }
  if (localization != nullptr && !target.tool_id.empty()) {
    std::string key = "tool_settings.target.";
    key.append(target.tool_id);
    return QString::fromStdString(localization->text(key, target.title));
  }
  if (!target.title.empty()) {
    return qtext(target.title);
  }
  return localized_text(localization, "ui.viewport.settings", "Settings");
}

QString target_title_button_text(QString title) {
  return title.replace(QLatin1Char('&'), QStringLiteral("&&"));
}

QString localized_descriptor_label(const LocalizationService *localization,
                                   std::string_view id,
                                   std::string_view fallback) {
  if (localization != nullptr && !id.empty()) {
    return QString::fromStdString(localization->text(id, fallback));
  }
  return qtext(fallback);
}

QString control_object_name(std::string_view id, std::size_t index) {
  if (!id.empty()) {
    return QStringLiteral("tool_settings.control.%1").arg(qtext(id));
  }
  return QStringLiteral("tool_settings.control.unnamed_%1")
      .arg(static_cast<qulonglong>(index));
}

QString option_object_name(QStringView control_name, std::string_view id,
                           std::size_t index) {
  const QString suffix = id.empty()
                             ? QStringLiteral("unnamed_%1")
                                   .arg(static_cast<qulonglong>(index))
                             : qtext(id);
  return QStringLiteral("%1.option.%2").arg(control_name, suffix);
}

bool nearly_integer(double value) {
  return std::isfinite(value) &&
         std::abs(value - std::round(value)) <= 0.000001;
}

bool uses_integer_spin_box(const QuaderToolSettingControl &control) {
  constexpr double kIntMinimum =
      static_cast<double>(std::numeric_limits<int>::min());
  constexpr double kIntMaximum =
      static_cast<double>(std::numeric_limits<int>::max());
  return nearly_integer(control.step) && control.step > 0.0 &&
         nearly_integer(control.min_value) &&
         nearly_integer(control.max_value) &&
         control.min_value >= kIntMinimum && control.max_value <= kIntMaximum;
}

QString group_object_name(std::string_view id, std::size_t index) {
  if (!id.empty()) {
    return QStringLiteral("tool_settings.group.%1").arg(qtext(id));
  }
  return QStringLiteral("tool_settings.group.unnamed_%1")
      .arg(static_cast<qulonglong>(index));
}

int integer_single_step(double step) {
  if (!std::isfinite(step) || step <= 0.0) {
    return 1;
  }
  return std::max(
      1, static_cast<int>(std::llround(std::min(
             step, static_cast<double>(std::numeric_limits<int>::max())))));
}

int decimal_places_for_step(double step) {
  if (!std::isfinite(step) || step <= 0.0) {
    return 3;
  }
  double scaled = step;
  for (int decimals = 0; decimals < 8; ++decimals) {
    if (nearly_integer(scaled)) {
      return decimals;
    }
    scaled *= 10.0;
  }
  return 8;
}

int number_decimals(const QuaderToolSettingControl &control) {
  return control.number_decimals > 0
             ? std::clamp(control.number_decimals, 1, 17)
             : std::max(4, decimal_places_for_step(control.step));
}

bool same_choice_structure(const QuaderToolSettingChoiceOption &left,
                           const QuaderToolSettingChoiceOption &right) {
  return left.id == right.id && left.label == right.label &&
         left.tooltip == right.tooltip && left.value == right.value;
}

bool same_vector_component_structure(
    const QuaderToolSettingNumberComponent &left,
    const QuaderToolSettingNumberComponent &right) {
  return left.id == right.id;
}

bool same_group_structure(const QuaderToolSettingGroup &left,
                          const QuaderToolSettingGroup &right) {
  return left.id == right.id && left.label == right.label &&
         left.expanded_by_default == right.expanded_by_default;
}

bool same_control_structure(const QuaderToolSettingControl &left,
                            const QuaderToolSettingControl &right) {
  if (left.kind != right.kind || left.id != right.id ||
      left.group_id != right.group_id ||
      left.tooltip != right.tooltip ||
      left.number_interaction != right.number_interaction ||
      left.number_presentation != right.number_presentation ||
      left.number_positive_only != right.number_positive_only ||
      left.number_grid_scale != right.number_grid_scale ||
      left.number_drag_sensitivity != right.number_drag_sensitivity ||
      left.choice_presentation != right.choice_presentation ||
      left.choices.size() != right.choices.size()) {
    return false;
  }
  if (left.kind == QuaderToolSettingControlKind::Number &&
      uses_integer_spin_box(left) != uses_integer_spin_box(right)) {
    return false;
  }
  for (std::size_t index = 0; index < left.choices.size(); ++index) {
    if (!same_choice_structure(left.choices[index], right.choices[index])) {
      return false;
    }
  }
  for (std::size_t index = 0; index < left.vector3_components.size();
       ++index) {
    if (!same_vector_component_structure(left.vector3_components[index],
                                         right.vector3_components[index])) {
      return false;
    }
  }
  return true;
}

bool same_target_structure(const QuaderToolSettingsTarget &left,
                           const QuaderToolSettingsTarget &right) {
  if (left.active != right.active || left.tool_id != right.tool_id ||
      left.title != right.title || left.title_key != right.title_key ||
      left.controls.size() != right.controls.size() ||
      left.groups.size() != right.groups.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.controls.size(); ++index) {
    if (!same_control_structure(left.controls[index], right.controls[index])) {
      return false;
    }
  }
  for (std::size_t index = 0; index < left.groups.size(); ++index) {
    if (!same_group_structure(left.groups[index], right.groups[index])) {
      return false;
    }
  }
  return true;
}

bool widget_or_descendant_has_focus(const QWidget *widget) {
  const QWidget *focus = QApplication::focusWidget();
  return widget != nullptr && focus != nullptr &&
         (focus == widget || widget->isAncestorOf(focus));
}

bool choice_matches(std::string_view selected,
                    const QuaderToolSettingChoiceOption &option) {
  if (selected == option.id) {
    return true;
  }
  if (selected.empty()) {
    return false;
  }
  std::string text(selected);
  char *end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  return end != text.c_str() && end != nullptr && *end == '\0' &&
         std::isfinite(value) &&
         std::abs(value - option.value) <= 0.000001;
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

void clear_layout(QLayout &layout) {
  while (QLayoutItem *item = layout.takeAt(0)) {
    if (QWidget *widget = item->widget()) {
      delete widget;
    }
    delete item;
  }
}

std::string next_tool_setting_edit_merge_key(std::string_view control_id) {
  static std::atomic<std::uint64_t> next_id{1};
  std::string result = "tool-settings:";
  result.append(control_id);
  result.push_back(':');
  result.append(
      std::to_string(next_id.fetch_add(1, std::memory_order_relaxed)));
  return result;
}

} // namespace

/// Owns retained controls for the active tool's descriptor-driven settings.
struct QtToolSettingsPanel::Impl {
  /// Binds a delayed numeric write to the owner lifecycle that queued it.
  struct NumberDispatchLifecycle {
    std::uint64_t binding_generation = 0;
    std::uint64_t settings_revision = 0;
    std::uint64_t active_tool_revision = 0;
    std::uint64_t presentation_serial = 0;
    std::string tool_id;
    std::string merge_key;
    bool preview_active = false;
  };

  /// Associates one choice button with its stable option and numeric value.
  struct ChoiceWidget {
    QAbstractButton *button = nullptr;
    std::string option_id;
    double value = 0.0;
  };

  /// Retains one independently dispatched component of an XYZ numeric row.
  struct Vector3ComponentWidget {
    std::string id;
    QDoubleSpinBox *spin = nullptr;
    QtNumericSpinBoxController *numeric_controller = nullptr;
    QTimer *dispatch_timer = nullptr;
    std::optional<double> pending_value;
    std::optional<NumberDispatchLifecycle> pending_lifecycle;
    std::function<void(double, QuaderToolSettingEditOptions)> dispatch;
    std::string continuous_merge_key;
    bool enabled = true;
  };

  /// Holds the widget variant used to edit one typed tool setting control.
  struct ControlWidget {
    QuaderToolSettingControlKind kind =
        QuaderToolSettingControlKind::Toggle;
    std::string id;
    QWidget *container = nullptr;
    QLabel *field_label = nullptr;
    QAbstractButton *label_button = nullptr;
    std::string presented_label;
    std::string presented_label_key;
    QCheckBox *toggle = nullptr;
    QSpinBox *integer = nullptr;
    QDoubleSpinBox *floating = nullptr;
    QtNumericSpinBoxController *numeric_controller = nullptr;
    QtMixedNumberSpinBox *spinbox_only = nullptr;
    bool invalid_number_draft = false;
    QtToolSettingsDragSpinner *number_drag_spinner = nullptr;
    QComboBox *dropdown = nullptr;
    QTimer *number_dispatch_timer = nullptr;
    std::optional<double> pending_number_value;
    std::optional<NumberDispatchLifecycle> pending_number_lifecycle;
    std::function<void(double, QuaderToolSettingEditOptions)> number_dispatch;
    std::string continuous_number_merge_key;
    std::vector<ChoiceWidget> choices;
    std::optional<std::string> presented_choice_value;
    std::optional<bool> presented_toggle_value;
    std::array<Vector3ComponentWidget, 3> vector3_components;
  };

  /// Maps one descriptor disclosure group to its retained section and layout.
  struct ControlGroupWidget {
    std::string id;
    QtSettingsSection *section = nullptr;
    QVBoxLayout *layout = nullptr;
  };

  explicit Impl(QtToolSettingsPanel &panel) : panel_(panel) {
    panel_.setObjectName(QStringLiteral("viewport.settings_overlay"));
    panel_.setAutoFillBackground(true);
    panel_.setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    auto *root_layout = new QVBoxLayout(&panel_);
    root_layout->setObjectName(QStringLiteral("tool_settings.layout"));
    root_layout->setSizeConstraint(QLayout::SetNoConstraint);
    set_scalable_margins(*root_layout, QMargins(0, 0, 0, 0));
    set_scalable_spacing(*root_layout, 4);

    target_title_ = new QtPanelSectionHeader(&panel_);
    target_title_->setObjectName(QStringLiteral("tool_settings.target_title"));
    target_title_->setChecked(expanded_);
    target_title_->setArrowType(Qt::DownArrow);
    target_title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    // Mouse collapse settles drafts before focus-out can interpret them.
    target_title_->setFocusPolicy(Qt::TabFocus);
    root_layout->addWidget(target_title_);
    panel_.setFocusProxy(target_title_);
    QObject::connect(target_title_, &QToolButton::toggled, &panel_,
                     [this](bool expanded) { set_expanded(expanded); });

    scroll_area_ = new QScrollArea(&panel_);
    scroll_area_->setObjectName(QStringLiteral("tool_settings.scroll"));
    scroll_area_->setFrameShape(QFrame::NoFrame);
    scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setMinimumSize(0, 0);
    scroll_area_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

    controls_container_ = new QWidget(scroll_area_);
    controls_container_->setObjectName(QStringLiteral("tool_settings.content"));
    controls_layout_ = new QVBoxLayout(controls_container_);
    set_scalable_margins(*controls_layout_, QMargins(6, 2, 6, 6));
    set_scalable_spacing(*controls_layout_, 6);
    controls_layout_->setAlignment(Qt::AlignTop);
    scroll_area_->setWidget(controls_container_);
    root_layout->addWidget(scroll_area_, 1);

    actions_container_ = new QWidget(&panel_);
    actions_container_->setObjectName(QStringLiteral("tool_settings.actions"));
    auto *actions_layout = new QVBoxLayout(actions_container_);
    set_scalable_margins(*actions_layout, QMargins(6, 0, 6, 6));
    set_scalable_spacing(*actions_layout, 6);

    preview_actions_container_ = new QWidget(actions_container_);
    preview_actions_container_->setObjectName(
        QStringLiteral("tool_settings.preview_actions"));
    auto *preview_actions_layout =
        new QHBoxLayout(preview_actions_container_);
    set_scalable_margins(*preview_actions_layout, QMargins(0, 0, 0, 0));
    preview_actions_layout->addStretch(1);

    apply_button_ = new QPushButton(preview_actions_container_);
    apply_button_->setObjectName(QStringLiteral("tool_settings.apply"));
    apply_button_->setFocusPolicy(Qt::TabFocus);
    preview_actions_layout->addWidget(apply_button_);

    cancel_button_ = new QPushButton(preview_actions_container_);
    cancel_button_->setObjectName(QStringLiteral("tool_settings.cancel"));
    // Keep mouse focus on the numeric editor until pressed has invalidated its
    // delayed value. clicked restores normal button focus after that boundary.
    cancel_button_->setFocusPolicy(Qt::TabFocus);
    preview_actions_layout->addWidget(cancel_button_);
    actions_layout->addWidget(preview_actions_container_);

    root_layout->addWidget(actions_container_);
    refresh_localized_text();

    QObject::connect(apply_button_, &QPushButton::clicked, &panel_, [this]() {
      if (editor_ == nullptr ||
          !prepare_lifecycle_intent(*editor_,
                                   QuaderNativeToolSettingsIntent::Apply)) {
        return;
      }
      const QuaderApiVoidResult result =
          editor_->tools().commit_active_tool_preview();
      present_failure(editor_, "Tool preview apply", result);
      schedule_refresh();
    });
    QObject::connect(cancel_button_, &QPushButton::clicked, &panel_,
                     [this]() {
                       // Cancel owns the previous authoritative preview state.
                       discard_pending_number_dispatches();
                       cancel_button_->setFocus(Qt::MouseFocusReason);
                       if (editor_ == nullptr ||
                           !prepare_lifecycle_intent(
                               *editor_, QuaderNativeToolSettingsIntent::Cancel)) {
                         return;
                       }
                       const QuaderApiVoidResult result =
                           editor_->tools().cancel_active_tool_preview();
                       present_failure(editor_, "Tool preview cancel", result);
                       schedule_refresh();
                     });
    QObject::connect(cancel_button_, &QPushButton::pressed, &panel_, [this]() {
      discard_pending_number_dispatches();
    });
    present_inactive_target();
  }

  ~Impl() { clear_controls(); }

  void bind(QuaderEditorApi &editor) {
    ++binding_generation_;
    discard_pending_number_dispatches();
    editor_ = &editor;
    last_target_.reset();
    refresh();
  }

  void unbind() {
    ++binding_generation_;
    discard_pending_number_dispatches();
    editor_ = nullptr;
    last_target_.reset();
    present_inactive_target();
  }

  void set_localization(const LocalizationService &localization) {
    localization_ = &localization;
    // Descriptor controls retain their visible text, so force a rebuild when
    // the selected locale changes instead of updating only the header title.
    last_target_.reset();
    localized_text_dirty_ = true;
    refresh_localized_text();
    refresh();
  }

  void refresh() {
    refresh_localized_text();
    if (editor_ == nullptr) {
      if (!inactive_presented_) {
        present_inactive_target();
      }
      return;
    }

    const std::uint64_t generation = binding_generation_;
    const auto pane = settings_pane();
    const bool pane_changed = pane != target_pane_;
    if (pane_changed) {
      // A queued edit belongs to its original operation presentation. A new
      // origin must not receive a delayed draft from the previous operation.
      discard_pending_number_dispatches();
    }
    target_pane_ = pane;
    const QuaderToolSettingsTarget target = editor_->tools().settings_for_pane(pane);
    if (generation != binding_generation_ || editor_ == nullptr) {
      return;
    }
    if (!target.active ||
        std::none_of(target.controls.begin(), target.controls.end(),
                     [](const QuaderToolSettingControl &control) {
                       return control.kind != QuaderToolSettingControlKind::Action ||
                              !control.choices.empty();
                     })) {
      if (!inactive_presented_) {
        present_inactive_target();
      }
      return;
    }

    const std::uint64_t presentation_serial =
        editor_->tools().settings_presentation_serial(target_pane_);
    const bool new_activation =
        inactive_presented_ || pane_changed ||
        target_presentation_serial_ != presentation_serial ||
        (last_target_.has_value() && last_target_->tool_id != target.tool_id);
    if (!last_target_.has_value() || pane_changed ||
        target_presentation_serial_ != presentation_serial ||
        !same_target_structure(*last_target_, target)) {
      rebuild(target);
    } else {
      synchronize_values(target);
    }
    last_target_ = target;
    target_presentation_serial_ = presentation_serial;
    target_tool_revision_ = editor_->tools().selected_tool_revision(target_pane_);
    if (new_activation) {
      set_expanded(true);
    }
    notify_presentation_changed();
  }

  [[nodiscard]] bool has_active_target() const {
    return last_target_.has_value() && last_target_->active &&
           !last_target_->controls.empty();
  }

  [[nodiscard]] std::string_view target_id() const {
    return last_target_.has_value() ? std::string_view(last_target_->tool_id)
                                    : std::string_view{};
  }

  [[nodiscard]] bool expanded() const { return expanded_; }

  void set_expanded(bool expanded) {
    if (expanded_ == expanded) {
      return;
    }
    if (!expanded) {
      settle_numeric_edits();
    }
    expanded_ = expanded;
    const QSignalBlocker blocker(target_title_);
    target_title_->setChecked(expanded);
    target_title_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    synchronize_body_visibility();
    notify_presentation_changed();
  }

  [[nodiscard]] QSize preferred_size() const {
    const QMargins margins = panel_.layout()->contentsMargins();
    const int width = expanded_
        ? std::max(ui_scale_metrics_.px(320),
                   controls_layout_->minimumSize().width() +
                       margins.left() + margins.right())
        : ui_scale_metrics_.px(160);
    int height = std::max(target_title_->minimumHeight(),
                          target_title_->sizeHint().height()) +
                 margins.top() + margins.bottom();
    if (expanded_ && !inactive_presented_) {
      const int controls_height = controls_layout_->hasHeightForWidth()
          ? controls_layout_->heightForWidth(
                width - margins.left() - margins.right())
          : controls_layout_->sizeHint().height();
      height += controls_height + panel_.layout()->spacing();
      if (!preview_actions_container_->isHidden()) {
        height += actions_container_->sizeHint().height() +
                  panel_.layout()->spacing();
      }
    }
    return QSize(width, height);
  }

  [[nodiscard]] bool numeric_interaction_active() const {
    return std::any_of(
        controls_.begin(), controls_.end(), [](const ControlWidget &binding) {
          const bool vector_pending = std::any_of(
              binding.vector3_components.begin(),
              binding.vector3_components.end(),
              [](const Vector3ComponentWidget &component) {
                return component.pending_value.has_value() ||
                       (component.numeric_controller != nullptr &&
                        component.numeric_controller->is_dragging());
              });
          return binding.pending_number_value.has_value() || vector_pending ||
                 (binding.numeric_controller != nullptr &&
                  binding.numeric_controller->is_dragging()) ||
                 (binding.number_drag_spinner != nullptr &&
                   (binding.number_drag_spinner->is_dragging() ||
                    binding.number_drag_spinner->is_editing()));
        });
  }

  [[nodiscard]] bool prepare_lifecycle_intent(
      QuaderEditorApi &editor, QuaderNativeToolSettingsIntent intent) {
    if (editor_ != &editor || !presentation_is_current() ||
        !editor_->tools().pane_owns_executing_tool(target_pane_) ||
        (intent == QuaderNativeToolSettingsIntent::Cancel && !last_target_->can_cancel) ||
        (intent == QuaderNativeToolSettingsIntent::Apply && !last_target_->apply_visible)) {
      discard_pending_number_dispatches();
      return false;
    }
    if (intent == QuaderNativeToolSettingsIntent::Cancel) {
      discard_pending_number_dispatches();
      return true;
    }
    if (intent != QuaderNativeToolSettingsIntent::Apply ||
        has_invalid_number_draft()) {
      return false;
    }
    number_dispatch_failed_ = false;
    const std::uint64_t generation = binding_generation_;
    const std::uint64_t controls_generation = control_generation_;
    const std::uint64_t tool_revision = editor_->tools().selected_tool_revision(target_pane_);
    const auto still_current = [&] {
      return editor_ == &editor && generation == binding_generation_ &&
             controls_generation == control_generation_ &&
             presentation_is_current() &&
             editor_->tools().selected_tool_revision(target_pane_) == tool_revision;
    };
    for (QWidget *widget = QApplication::focusWidget();
         widget != nullptr && panel_.isAncestorOf(widget);
         widget = widget->parentWidget()) {
      if (auto *spin = dynamic_cast<QAbstractSpinBox *>(widget)) {
        if (!spin->hasAcceptableInput()) {
          return false;
        }
        spin->interpretText();
        break;
      }
    }
    if (!still_current() || has_invalid_number_draft()) {
      return false;
    }
    // interpretText queues the final sample; it does not emit editingFinished
    // when a command filter consumes the completion key before the control.
    flush_pending_number_dispatches();
    if (number_dispatch_failed_ || !still_current()) {
      return false;
    }
    // A fresh amount can start a preview during this flush. Observe it before
    // Apply completes it so even a whole step between frame refreshes publishes
    // its authoritative reset to a still-focused field.
    refresh();
    return !number_dispatch_failed_ && still_current();
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    if (ui_scale_metrics_.revision() == metrics.revision() &&
        qFuzzyCompare(static_cast<qreal>(ui_scale_metrics_.factor()),
                      static_cast<qreal>(metrics.factor()))) {
      return;
    }
    ui_scale_metrics_ = metrics;
    apply_current_ui_scale();
    notify_presentation_changed();
  }

private:
  [[nodiscard]] ViewportPaneId settings_pane() const {
    const auto &origin = editor_->tools().active_settings_presentation_origin();
    return origin.serial != 0u ? origin.pane : editor_->viewports().focused_pane();
  }

  [[nodiscard]] bool presentation_is_current() const {
    return editor_ != nullptr && has_active_target() &&
           settings_pane() == target_pane_ &&
           editor_->tools().selected_tool_revision(target_pane_) == target_tool_revision_ &&
           editor_->tools().settings_presentation_serial(target_pane_) ==
               target_presentation_serial_;
  }

  /// Only an accepted mutation may acquire the pane's executing owner. Refresh
  /// the retained source fence after that acquisition, not on focus or hover.
  void observe_settings_mutation(const QuaderApiVoidResult &result,
                                 std::uint64_t generation,
                                 std::uint64_t controls_generation,
                                 ViewportPaneId pane) {
    if (!result.ok() || editor_ == nullptr || generation != binding_generation_ ||
        controls_generation != control_generation_ || pane != target_pane_ ||
        settings_pane() != pane) {
      return;
    }
    target_presentation_serial_ = editor_->tools().settings_presentation_serial(pane);
    target_tool_revision_ = editor_->tools().selected_tool_revision(pane);
    for (auto &binding : controls_) {
      if (binding.pending_number_lifecycle) {
        binding.pending_number_lifecycle->presentation_serial = target_presentation_serial_;
      }
      for (auto &component : binding.vector3_components) {
        if (component.pending_lifecycle) {
          component.pending_lifecycle->presentation_serial = target_presentation_serial_;
        }
      }
    }
  }

  template <class Value>
  QuaderApiVoidResult set_pane_setting(std::string_view tool_id,
                                      std::string_view control_id, Value value) {
    const auto generation = binding_generation_;
    const auto controls_generation = control_generation_;
    const auto pane = target_pane_;
    auto result = editor_->tools().set_setting(pane, tool_id, control_id, value);
    observe_settings_mutation(result, generation, controls_generation, pane);
    return result;
  }

  QuaderApiVoidResult set_pane_setting(std::string_view tool_id,
                                      std::string_view control_id, double value,
                                      QuaderToolSettingEditOptions options) {
    const auto generation = binding_generation_;
    const auto controls_generation = control_generation_;
    const auto pane = target_pane_;
    auto result = editor_->tools().set_setting(pane, tool_id, control_id, value,
                                              std::move(options));
    observe_settings_mutation(result, generation, controls_generation, pane);
    return result;
  }

  QuaderApiVoidResult execute_pane_action(std::string_view tool_id,
                                         std::string_view action_id) {
    const auto generation = binding_generation_;
    const auto controls_generation = control_generation_;
    const auto pane = target_pane_;
    auto result = editor_->tools().execute_action(pane, tool_id, action_id);
    observe_settings_mutation(result, generation, controls_generation, pane);
    return result;
  }

  void notify_presentation_changed() {
    // The UI owner already revision-gates refresh. Avoid redundant host work
    // during retained value synchronization and repeated local refreshes.
    if (controls_layout_ != nullptr) {
      controls_layout_->activate();
    }
    const QSize size = preferred_size();
    const bool available = has_active_target();
    if (last_presentation_size_ == size &&
        last_presentation_available_ == available) {
      return;
    }
    last_presentation_size_ = size;
    last_presentation_available_ = available;
    panel_.updateGeometry();
    if (panel_.presentation_changed) {
      panel_.presentation_changed();
    }
  }

  void settle_numeric_edits() {
    const std::uint64_t generation = binding_generation_;
    const std::uint64_t controls_generation = control_generation_;
    // Reset invalid drafts before focus-out; otherwise Qt could reinterpret
    // intermediate input and dispatch it while the settings body is hiding.
    for (std::size_t index = 0; index < controls_.size(); ++index) {
      ControlWidget &binding = controls_[index];
      if (!last_target_.has_value() || index >= last_target_->controls.size()) {
        break;
      }
      const QuaderToolSettingControl &control = last_target_->controls[index];
      if (binding.invalid_number_draft) {
        discard_pending_number_dispatch(index);
        if (binding.spinbox_only != nullptr) {
          binding.spinbox_only->present(control.number_value);
        }
      }
      const auto restore_invalid = [](auto *spin, auto value) {
        if (spin != nullptr && !spin->hasAcceptableInput()) {
          const QSignalBlocker blocker(spin);
          spin->setValue(value);
        }
      };
      restore_invalid(binding.integer,
                      static_cast<int>(std::llround(control.number_value)));
      restore_invalid(binding.floating, control.number_value);
      for (std::size_t axis = 0; axis < 3U; ++axis) {
        restore_invalid(binding.vector3_components[axis].spin,
                        control.vector3_components[axis].value);
      }
    }
    if (QWidget *focus = QApplication::focusWidget();
        focus != nullptr && panel_.isAncestorOf(focus)) {
      focus->clearFocus();
    }
    if (generation != binding_generation_ ||
        controls_generation != control_generation_) {
      return;
    }
    flush_pending_number_dispatches();
    if (generation != binding_generation_ ||
        controls_generation != control_generation_) {
      return;
    }
    // Hidden controls must never retain a timer or a continuous edit identity.
    discard_pending_number_dispatches();
    refresh();
  }

  void apply_current_ui_scale() {
    apply_scalable_layout_metrics(panel_, ui_scale_metrics_);
    apply_scalable_widget_metrics(panel_, ui_scale_metrics_);
    control_label_width_dirty_ = true;
    synchronize_control_label_width();
    panel_.updateGeometry();
    if (scroll_area_->widget() != nullptr) {
      scroll_area_->widget()->updateGeometry();
    }
  }

  /// Measure labels only when their text, target or interface scale changes.
  void synchronize_control_label_width() {
    if (!control_label_width_dirty_) {
      return;
    }
    control_label_width_dirty_ = false;
    int width = ui_scale_metrics_.px(kMinimumControlLabelWidth);
    int width_limit = ui_scale_metrics_.px(kMaximumControlLabelWidth);
    const QMargins panel_margins = panel_.layout()->contentsMargins();
    for (const ControlWidget &control : controls_) {
      if (control.field_label != nullptr) {
        const QLabel &label = *control.field_label;
        width = std::max(width, label.fontMetrics().horizontalAdvance(label.text()));
      }
      auto *row_layout =
          dynamic_cast<QGridLayout *>(control.container->layout());
      if (row_layout == nullptr) {
        continue;
      }
      const QWidget *field = row_layout->itemAtPosition(0, 1)->widget();
      int field_width =
          std::max(field->minimumWidth(), field->minimumSizeHint().width());
      if (field->layout() != nullptr) {
        // Reserve composite editors' actual child minima before widening labels.
        field_width = std::max(field_width, field->layout()->minimumSize().width());
      }
      int margins = panel_margins.left() + panel_margins.right();
      for (const QWidget *parent = control.container->parentWidget();
           parent != nullptr; parent = parent->parentWidget()) {
        const QMargins content_margins = parent->contentsMargins();
        margins += content_margins.left() + content_margins.right();
        if (parent->layout() != nullptr) {
          const QMargins layout_margins = parent->layout()->contentsMargins();
          margins += layout_margins.left() + layout_margins.right();
        }
        if (parent == controls_container_) {
          break;
        }
      }
      width_limit = std::min(width_limit,
          ui_scale_metrics_.px(320) - margins -
              row_layout->horizontalSpacing() - field_width);
    }
    // Ordinary labels fit on one line; unusually long translations wrap before
    // they displace the numeric and composite editors from a compact overlay.
    width = std::min(width,
        std::max(ui_scale_metrics_.px(kMinimumControlLabelWidth), width_limit));
    for (const ControlWidget &control : controls_) {
      if (control.kind == QuaderToolSettingControlKind::Vector3Number &&
          control.field_label != nullptr &&
          control.vector3_components[0].spin != nullptr) {
        // The group name follows the first axis, as in the Inspector. Measure
        // after font propagation so scaling never pins a label to the old font.
        const int top_inset = std::max(
            0, (control.vector3_components[0].spin->sizeHint().height() -
                control.field_label->fontMetrics().height()) / 2);
        control.field_label->setContentsMargins(0, top_inset, 0, 0);
      }
      if (auto *row_layout =
              dynamic_cast<QGridLayout *>(control.container->layout())) {
        QWidget *label_cell = row_layout->itemAtPosition(0, 0)->widget();
        if (label_cell->minimumWidth() != width ||
            label_cell->maximumWidth() != width) {
          label_cell->setFixedWidth(width);
        }
        if (row_layout->columnMinimumWidth(0) != width) {
          row_layout->setColumnMinimumWidth(0, width);
        }
      }
    }
  }

  void schedule_refresh() {
    if (refresh_scheduled_) {
      return;
    }
    refresh_scheduled_ = true;
    QTimer::singleShot(0, &panel_, [this]() {
      refresh_scheduled_ = false;
      refresh();
    });
  }

  void clear_controls() {
    ++control_generation_;
    control_label_width_dirty_ = true;
    const bool was_synchronizing = synchronizing_;
    synchronizing_ = true;
    // Stop old control-owned timers before deleting their rows and callbacks.
    discard_pending_number_dispatches();
    for (ControlWidget &binding : controls_) {
      if (binding.number_drag_spinner == nullptr) {
        continue;
      }
      binding.number_drag_spinner->value_changed = {};
      binding.number_drag_spinner->editing_started = {};
      binding.number_drag_spinner->editing_canceled = {};
      binding.number_drag_spinner->editing_finished = {};
      binding.number_drag_spinner->drag_started = {};
      binding.number_drag_spinner->drag_finished = {};
    }
    controls_.clear();
    control_groups_.clear();
    clear_layout(*controls_layout_);
    target_title_->setText({});
    preview_actions_container_->hide();
    apply_button_->hide();
    cancel_button_->hide();
    synchronizing_ = was_synchronizing;
  }

  void synchronize_body_visibility() {
    const bool visible = expanded_ && !inactive_presented_;
    if (scroll_area_->isHidden() == visible) {
      scroll_area_->setVisible(visible);
    }
    const bool actions_visible = visible && !preview_actions_container_->isHidden();
    if (actions_container_->isHidden() == actions_visible) {
      actions_container_->setVisible(actions_visible);
    }
  }

  void present_inactive_target() {
    clear_controls();
    last_target_.reset();
    inactive_presented_ = true;
    synchronize_body_visibility();
    panel_.hide();
    notify_presentation_changed();
  }

  void begin_number_continuous_edit(std::size_t control_index) {
    if (control_index >= controls_.size()) {
      return;
    }
    if (!controls_[control_index].continuous_number_merge_key.empty()) {
      return;
    }
    // Finish a preceding arrow, wheel, or keyboard step before a new
    // continuous gesture establishes its own merge identity.
    const std::uint64_t controls_generation = control_generation_;
    flush_number_dispatch(control_index);
    if (controls_generation != control_generation_ ||
        !presentation_is_current()) {
      return;
    }
    ControlWidget &binding = controls_[control_index];
    binding.continuous_number_merge_key =
        next_tool_setting_edit_merge_key(binding.id);
  }

  void finish_number_continuous_edit(std::size_t control_index) {
    if (control_index >= controls_.size()) {
      return;
    }
    const std::uint64_t controls_generation = control_generation_;
    flush_number_dispatch(control_index);
    if (controls_generation != control_generation_) {
      return;
    }
    controls_[control_index].continuous_number_merge_key.clear();
  }

  /// Share one history identity from the first scrub sample through release.
  void bind_number_interaction(QAbstractSpinBox &spin, ControlWidget &binding,
                               std::size_t control_index,
                               const QuaderToolSettingControl &control) {
    auto &controller = qt_install_numeric_spin_box(spin);
    controller.set_drag_snap_scale(control.number_grid_scale);
    controller.set_drag_sensitivity(control.number_drag_sensitivity);
    binding.numeric_controller = &controller;
    controller.drag_started = [this, control_index]() {
      begin_number_continuous_edit(control_index);
    };
    controller.editing_started = controller.drag_started;
    controller.editing_canceled = [this, control_index]() {
      if (control_index >= controls_.size()) {
        return;
      }
      ControlWidget &current = controls_[control_index];
      const double restored_value = current.floating != nullptr
          ? current.floating->value() : current.integer->value();
      std::string merge_key = std::move(current.continuous_number_merge_key);
      discard_pending_number_dispatch(control_index);
      current.continuous_number_merge_key = std::move(merge_key);
      // Mixed fields can preview text while their native scalar is unchanged.
      // Replace the draft with the restored scalar under the same edit identity.
      queue_number_dispatch(control_index, restored_value);
      schedule_refresh();
    };
    controller.interaction_canceled = [this, control_index]() {
      finish_number_continuous_edit(control_index);
    };
  }

  void begin_vector3_continuous_edit(std::size_t control_index,
                                     std::size_t component_index) {
    if (control_index >= controls_.size() || component_index >= 3U) {
      return;
    }
    if (!controls_[control_index].vector3_components[component_index]
             .continuous_merge_key.empty()) {
      return;
    }
    const std::uint64_t controls_generation = control_generation_;
    flush_vector3_dispatch(control_index, component_index);
    if (controls_generation != control_generation_ ||
        !presentation_is_current()) {
      return;
    }
    Vector3ComponentWidget &component =
        controls_[control_index].vector3_components[component_index];
    component.continuous_merge_key =
        next_tool_setting_edit_merge_key(component.id);
  }

  void finish_vector3_continuous_edit(std::size_t control_index,
                                      std::size_t component_index) {
    if (control_index >= controls_.size() || component_index >= 3U) {
      return;
    }
    const std::uint64_t controls_generation = control_generation_;
    flush_vector3_dispatch(control_index, component_index);
    if (controls_generation != control_generation_) {
      return;
    }
    controls_[control_index]
        .vector3_components[component_index]
        .continuous_merge_key.clear();
  }

  void queue_number_dispatch(std::size_t control_index, double value) {
    if (control_index >= controls_.size()) {
      return;
    }
    ControlWidget &binding = controls_[control_index];
    if (binding.pending_number_value.has_value() &&
        binding.number_drag_spinner != nullptr) {
      const std::string_view pending_merge_key =
          binding.pending_number_lifecycle.has_value()
              ? std::string_view(
                    binding.pending_number_lifecycle->merge_key)
              : std::string_view{};
      if (binding.continuous_number_merge_key.empty() ||
          pending_merge_key != binding.continuous_number_merge_key) {
        // Discrete values are delayed for preview pacing, but never replace a
        // preceding discrete history step. Continuous values may replace the
        // pending sample because all dispatched samples share one merge key.
        const std::uint64_t controls_generation = control_generation_;
        flush_number_dispatch(control_index);
        if (controls_generation != control_generation_ ||
            !presentation_is_current()) {
          return;
        }
      }
    }
    if (!binding.pending_number_value.has_value()) {
      if (editor_ == nullptr || !last_target_.has_value() ||
          !last_target_->active ||
          editor_->tools().settings_presentation_serial(target_pane_) !=
              target_presentation_serial_) {
        return;
      }
      binding.pending_number_lifecycle = NumberDispatchLifecycle{
          .binding_generation = binding_generation_,
          .settings_revision = editor_->tools().settings_revision(target_pane_),
          .active_tool_revision = editor_->tools().selected_tool_revision(target_pane_),
          .presentation_serial = target_presentation_serial_,
          .tool_id = last_target_->tool_id,
          .merge_key = binding.continuous_number_merge_key,
          .preview_active = last_target_->preview_active,
      };
    }
    binding.pending_number_value = value;
    if (binding.number_dispatch_timer != nullptr) {
      const bool text_edit = binding.number_drag_spinner != nullptr &&
                             binding.number_drag_spinner->is_editing();
      if (text_edit) {
        // Native text entry remains an atomic edit: Enter/focus-out commits
        // the final value, while Escape can still discard it regardless of
        // how long the user pauses between keystrokes.
        binding.number_dispatch_timer->stop();
        return;
      }
      const bool pointer_drag =
          (binding.numeric_controller != nullptr &&
           binding.numeric_controller->is_dragging()) ||
          (binding.number_drag_spinner != nullptr &&
           binding.number_drag_spinner->is_dragging());
      // Pointer drags publish the latest value at a bounded cadence so live
      // geometry remains visible without compiling once per mouse event.
      // Keyboard, wheel, and spin edits retain one trailing dispatch window.
      if (!pointer_drag || !binding.number_dispatch_timer->isActive()) {
        binding.number_dispatch_timer->start();
      }
    }
  }

  void queue_vector3_dispatch(std::size_t control_index,
                              std::size_t component_index, double value) {
    if (control_index >= controls_.size() || component_index >= 3U) {
      return;
    }
    Vector3ComponentWidget &component =
        controls_[control_index].vector3_components[component_index];
    if (!component.pending_value.has_value()) {
      if (editor_ == nullptr || !last_target_.has_value() ||
          !last_target_->active ||
          editor_->tools().settings_presentation_serial(target_pane_) !=
              target_presentation_serial_) {
        return;
      }
      component.pending_lifecycle = NumberDispatchLifecycle{
          .binding_generation = binding_generation_,
          .settings_revision = editor_->tools().settings_revision(target_pane_),
          .active_tool_revision = editor_->tools().selected_tool_revision(target_pane_),
          .presentation_serial = target_presentation_serial_,
          .tool_id = last_target_->tool_id,
          .merge_key = component.continuous_merge_key,
          .preview_active = last_target_->preview_active,
      };
    }
    component.pending_value = value;
    if (component.dispatch_timer != nullptr) {
      const bool pointer_drag = component.numeric_controller != nullptr &&
                                component.numeric_controller->is_dragging();
      if (!pointer_drag || !component.dispatch_timer->isActive()) {
        component.dispatch_timer->start();
      }
    }
  }

  [[nodiscard]] bool number_dispatch_lifecycle_is_current(
      const ControlWidget &binding) const {
    if (editor_ == nullptr ||
        !binding.pending_number_lifecycle.has_value()) {
      return false;
    }
    const NumberDispatchLifecycle &lifecycle =
        *binding.pending_number_lifecycle;
    if (settings_pane() != target_pane_ ||
        binding_generation_ != lifecycle.binding_generation ||
        editor_->tools().settings_revision(target_pane_) !=
            lifecycle.settings_revision ||
        editor_->tools().selected_tool_revision(target_pane_) !=
            lifecycle.active_tool_revision ||
        editor_->tools().settings_presentation_serial(target_pane_) !=
            lifecycle.presentation_serial) {
      return false;
    }

    // The revisions above are the owner contract for descriptor and tool
    // identity. Reuse the retained descriptor captured for this gesture rather
    // than rebuilding the broad settings snapshot on every drag dispatch; Path
    // Array's snapshot may need to validate and publish geometry.
    if (!last_target_.has_value() || !last_target_->active ||
        last_target_->tool_id != lifecycle.tool_id ||
        last_target_->preview_active != lifecycle.preview_active) {
      return false;
    }
    return std::any_of(
        last_target_->controls.begin(), last_target_->controls.end(),
        [&binding](const QuaderToolSettingControl &control) {
          return control.kind == QuaderToolSettingControlKind::Number &&
                 control.id == binding.id;
        });
  }

  [[nodiscard]] bool vector3_dispatch_lifecycle_is_current(
      std::size_t control_index, std::size_t component_index,
      const Vector3ComponentWidget &component) const {
    if (editor_ == nullptr || !component.pending_lifecycle.has_value() ||
        !last_target_.has_value() || !last_target_->active ||
        control_index >= last_target_->controls.size() ||
        component_index >= 3U) {
      return false;
    }
    const NumberDispatchLifecycle &lifecycle =
        *component.pending_lifecycle;
    if (settings_pane() != target_pane_ ||
        binding_generation_ != lifecycle.binding_generation ||
        editor_->tools().settings_revision(target_pane_) !=
            lifecycle.settings_revision ||
        editor_->tools().selected_tool_revision(target_pane_) !=
            lifecycle.active_tool_revision ||
        editor_->tools().settings_presentation_serial(target_pane_) !=
            lifecycle.presentation_serial ||
        last_target_->tool_id != lifecycle.tool_id ||
        last_target_->preview_active != lifecycle.preview_active) {
      return false;
    }
    const QuaderToolSettingControl &control =
        last_target_->controls[control_index];
    return control.kind == QuaderToolSettingControlKind::Vector3Number &&
           control.vector3_components[component_index].id == component.id;
  }

  void rebind_pending_number_dispatches_to_current_revision() {
    if (editor_ == nullptr) {
      return;
    }
    const std::uint64_t settings_revision =
        editor_->tools().settings_revision(target_pane_);
    const std::uint64_t active_tool_revision =
        editor_->tools().selected_tool_revision(target_pane_);
    for (ControlWidget &binding : controls_) {
      if (binding.pending_number_lifecycle.has_value()) {
        binding.pending_number_lifecycle->settings_revision =
            settings_revision;
        binding.pending_number_lifecycle->active_tool_revision =
            active_tool_revision;
      }
      for (Vector3ComponentWidget &component :
           binding.vector3_components) {
        if (!component.pending_lifecycle.has_value()) {
          continue;
        }
        component.pending_lifecycle->settings_revision = settings_revision;
        component.pending_lifecycle->active_tool_revision =
            active_tool_revision;
      }
    }
  }

  void flush_number_dispatch(std::size_t control_index) {
    if (control_index >= controls_.size()) {
      return;
    }
    ControlWidget &binding = controls_[control_index];
    if (!binding.pending_number_value.has_value()) {
      return;
    }
    const bool lifecycle_current =
        number_dispatch_lifecycle_is_current(binding);
    const double value = *binding.pending_number_value;
    std::string merge_key =
        binding.pending_number_lifecycle.has_value()
            ? std::move(binding.pending_number_lifecycle->merge_key)
            : std::string{};
    binding.pending_number_value.reset();
    binding.pending_number_lifecycle.reset();
    if (binding.number_dispatch_timer != nullptr) {
      binding.number_dispatch_timer->stop();
    }
    if (!lifecycle_current || !binding.number_dispatch) {
      number_dispatch_failed_ = true;
      schedule_refresh();
      return;
    }
    const std::uint64_t generation = binding_generation_;
    const std::uint64_t controls_generation = control_generation_;
    const std::uint64_t tool_revision = editor_->tools().selected_tool_revision(target_pane_);
    const std::uint64_t settings_revision =
        editor_->tools().settings_revision(target_pane_);
    const auto dispatch = binding.number_dispatch;
    dispatch(
        value, {.merge_key = std::move(merge_key)});
    if (editor_ == nullptr || generation != binding_generation_ ||
        controls_generation != control_generation_ ||
        !presentation_is_current() ||
        editor_->tools().selected_tool_revision(target_pane_) != tool_revision) {
      number_dispatch_failed_ = true;
      return;
    }
    // A successful owner write advances the settings revision. Other values
    // queued in the same UI lifecycle remain valid and must still flush.
    rebind_pending_number_dispatches_to_current_revision();
    // A later queued sample can be clamped to an already accepted bound, or
    // rejected, without advancing the revision. Settle that draft as well.
    const bool differs_from_presented_owner =
        last_target_.has_value() &&
        control_index < last_target_->controls.size() &&
        value != last_target_->controls[control_index].number_value;
    if (editor_->tools().settings_revision(target_pane_) != settings_revision ||
        differs_from_presented_owner) {
      synchronize_accepted_number(control_index, value);
    }
  }

  /// Present owner normalization immediately, including during a live scrub.
  void synchronize_accepted_number(std::size_t control_index,
                                    double requested_value) {
    const QPointer<QtToolSettingsPanel> panel_guard(&panel_);
    QuaderEditorApi *const editor = editor_;
    const auto generation = binding_generation_;
    const auto controls_generation = control_generation_;
    const auto binding_is_current = [&]() {
      return panel_guard && editor_ == editor &&
             binding_generation_ == generation &&
             control_generation_ == controls_generation &&
             presentation_is_current();
    };
    const auto target = editor->tools().settings_for_pane(target_pane_);
    if (!binding_is_current()) {
      return;
    }
    if (!last_target_.has_value() ||
        !same_target_structure(*last_target_, target) ||
        control_index >= target.controls.size() ||
        control_index >= controls_.size()) {
      schedule_refresh();
      return;
    }
    synchronize_values(target);
    if (!binding_is_current()) {
      return;
    }
    const auto &control = target.controls[control_index];
    auto &binding = controls_[control_index];
    if (!binding.pending_number_value.has_value() &&
        control.kind == QuaderToolSettingControlKind::Number &&
        control.number_value != requested_value) {
      if (binding.floating != nullptr) {
        const QSignalBlocker blocker(binding.floating);
        binding.floating->setDecimals(number_decimals(control));
        binding.floating->setRange(control.min_value, control.max_value);
        binding.floating->setValue(control.number_value);
      } else if (binding.integer != nullptr) {
        const QSignalBlocker blocker(binding.integer);
        binding.integer->setValue(
            static_cast<int>(std::llround(control.number_value)));
      }
      if (binding.numeric_controller != nullptr) {
        binding.numeric_controller->synchronize_drag_value();
      }
    }
    last_target_ = target;
    notify_presentation_changed();
  }

  void flush_vector3_dispatch(std::size_t control_index,
                              std::size_t component_index) {
    if (control_index >= controls_.size() || component_index >= 3U) {
      return;
    }
    Vector3ComponentWidget &component =
        controls_[control_index].vector3_components[component_index];
    if (!component.pending_value.has_value()) {
      return;
    }
    const bool lifecycle_current = vector3_dispatch_lifecycle_is_current(
        control_index, component_index, component);
    const double value = *component.pending_value;
    std::string merge_key = component.pending_lifecycle.has_value()
                                ? std::move(
                                      component.pending_lifecycle->merge_key)
                                : std::string{};
    component.pending_value.reset();
    component.pending_lifecycle.reset();
    if (component.dispatch_timer != nullptr) {
      component.dispatch_timer->stop();
    }
    if (!lifecycle_current || !component.dispatch) {
      schedule_refresh();
      return;
    }
    const std::uint64_t generation = binding_generation_;
    const std::uint64_t controls_generation = control_generation_;
    const std::uint64_t tool_revision = editor_->tools().selected_tool_revision(target_pane_);
    const auto dispatch = component.dispatch;
    dispatch(value, {.merge_key = std::move(merge_key)});
    if (editor_ == nullptr || generation != binding_generation_ ||
        controls_generation != control_generation_ ||
        !presentation_is_current() ||
        editor_->tools().selected_tool_revision(target_pane_) != tool_revision) {
      number_dispatch_failed_ = true;
      return;
    }
    rebind_pending_number_dispatches_to_current_revision();
  }

  void flush_pending_number_dispatches() {
    const std::uint64_t generation = binding_generation_;
    const std::uint64_t controls_generation = control_generation_;
    for (std::size_t index = 0; index < controls_.size(); ++index) {
      flush_number_dispatch(index);
      if (generation != binding_generation_ ||
          controls_generation != control_generation_ ||
          !presentation_is_current()) {
        return;
      }
      for (std::size_t component = 0; component < 3U; ++component) {
        flush_vector3_dispatch(index, component);
        if (generation != binding_generation_ ||
            controls_generation != control_generation_ ||
            !presentation_is_current()) {
          return;
        }
      }
    }
  }

  void discard_pending_number_dispatch(std::size_t control_index) {
    if (control_index >= controls_.size()) {
      return;
    }
    ControlWidget &binding = controls_[control_index];
    if (binding.numeric_controller != nullptr) {
      binding.numeric_controller->abandon_interaction();
    }
    binding.pending_number_value.reset();
    binding.pending_number_lifecycle.reset();
    binding.continuous_number_merge_key.clear();
    binding.invalid_number_draft = false;
    if (binding.spinbox_only != nullptr) {
      binding.spinbox_only->present(binding.spinbox_only->value());
    }
    if (binding.number_dispatch_timer != nullptr) {
      binding.number_dispatch_timer->stop();
    }
    for (Vector3ComponentWidget &component : binding.vector3_components) {
      if (component.numeric_controller != nullptr) {
        component.numeric_controller->abandon_interaction();
      }
      component.pending_value.reset();
      component.pending_lifecycle.reset();
      component.continuous_merge_key.clear();
      if (component.dispatch_timer != nullptr) {
        component.dispatch_timer->stop();
      }
    }
  }

  void discard_pending_number_dispatches() {
    for (std::size_t index = 0; index < controls_.size(); ++index) {
      discard_pending_number_dispatch(index);
    }
  }

  [[nodiscard]] bool has_invalid_number_draft() const {
    return std::any_of(controls_.begin(), controls_.end(),
                       [](const ControlWidget &binding) {
                         return binding.invalid_number_draft;
                       });
  }

  void refresh_localized_text() {
    if (!localized_text_dirty_) {
      return;
    }
    localized_text_dirty_ = false;
    apply_button_->setText(
        localized_text(localization_, "ui.action.apply", "Apply"));
    cancel_button_->setText(
        localized_text(localization_, "ui.action.cancel", "Cancel"));
    if (last_target_.has_value()) {
      const QString title = localized_target_title(localization_, *last_target_);
      target_title_->setText(target_title_button_text(title));
      target_title_->setAccessibleName(title);
      panel_.setWindowTitle(title);
    }
  }

  [[nodiscard]] static std::string control_group_state_key(
      std::string_view tool_id, std::string_view group_id) {
    std::string key(tool_id);
    key.push_back('\n');
    key.append(group_id);
    return key;
  }

  [[nodiscard]] ControlGroupWidget *find_control_group(
      std::string_view id) {
    const auto found = std::find_if(
        control_groups_.begin(), control_groups_.end(),
        [id](const ControlGroupWidget &group) { return group.id == id; });
    return found == control_groups_.end() ? nullptr : &*found;
  }

  void build_control_groups(const QuaderToolSettingsTarget &target) {
    for (std::size_t index = 0; index < target.groups.size(); ++index) {
      const QuaderToolSettingGroup &group = target.groups[index];
      if (group.id.empty() || find_control_group(group.id) != nullptr ||
          std::none_of(target.controls.begin(), target.controls.end(),
                       [&group](const QuaderToolSettingControl &control) {
                         return control.group_id == group.id;
                       })) {
        continue;
      }

      auto *section = new QtSettingsSection(
          group_object_name(group.id, index), controls_container_);
      section->set_title(localized_descriptor_label(
          localization_, group.id, group.label));
      const std::string state_key =
          control_group_state_key(target.tool_id, group.id);
      const auto remembered = control_group_expansion_.find(state_key);
      section->set_expanded(remembered != control_group_expansion_.end()
                                ? remembered->second
                                : group.expanded_by_default);
      section->expansion_changed = [this, state_key](bool expanded) {
        control_group_expansion_[state_key] = expanded;
        notify_presentation_changed();
      };

      auto *layout = new QVBoxLayout(section->content());
      set_scalable_margins(*layout, QMargins(6, 6, 6, 6));
      set_scalable_spacing(*layout, 6);
      controls_layout_->addWidget(section);
      control_groups_.push_back(ControlGroupWidget{
          .id = group.id,
          .section = section,
          .layout = layout,
      });
    }
  }

  void rebuild(const QuaderToolSettingsTarget &target) {
    clear_controls();
    inactive_presented_ = false;
    synchronize_body_visibility();
    build_control_groups(target);
    for (std::size_t index = 0; index < target.controls.size(); ++index) {
      const QuaderToolSettingControl &control = target.controls[index];
      ControlGroupWidget *group = find_control_group(control.group_id);
      add_control(target, control, index,
                  group != nullptr ? group->section->content()
                                   : controls_container_,
                  group != nullptr ? *group->layout : *controls_layout_);
    }
    synchronize_values(target);
    apply_current_ui_scale();
    reveal_rebuilt_controls();
  }

  void reveal_rebuilt_controls() {
    // Qt queues the initial show of widgets added to an already visible parent.
    // Reveal rebuilt rows before measuring, or the host caches an empty-body
    // height. Showing sections preserves their deliberately collapsed content.
    for (const ControlGroupWidget &group : control_groups_) {
      group.section->show();
    }
    for (const ControlWidget &control : controls_) {
      if (control.container != nullptr) {
        control.container->show();
      }
    }
  }

  void add_control(const QuaderToolSettingsTarget &target,
                   const QuaderToolSettingControl &control,
                   std::size_t control_index, QWidget *control_parent,
                   QVBoxLayout &control_layout) {
    ControlWidget binding{
        .kind = control.kind,
        .id = control.id,
    };
    const QString object_name =
        control_object_name(control.id, control_index);
    const QString label =
        localized_descriptor_label(
            localization_,
            control.label_key.empty() ? control.id : control.label_key,
            control.label);

    auto *row = new QWidget(control_parent);
    row->setObjectName(object_name + QStringLiteral(".row"));
    auto *row_layout = new QGridLayout(row);
    set_scalable_margins(*row_layout, QMargins(0, 0, 0, 0));
    set_scalable_grid_spacing(*row_layout, kControlColumnSpacing, 3);
    row_layout->setColumnStretch(1, 1);
    row_layout->setColumnMinimumWidth(
        0, ui_scale_metrics_.px(kMinimumControlLabelWidth));
    QLabel *field_label = nullptr;
    if (control.kind != QuaderToolSettingControlKind::Action ||
        !control.choices.empty()) {
      auto *setting_label = new QtToolSettingLabel(label, row);
      setting_label->metrics_changed = [this]() {
        control_label_width_dirty_ = true;
        schedule_refresh();
      };
      field_label = setting_label;
      field_label->setObjectName(object_name + QStringLiteral(".label"));
      field_label->setWordWrap(true);
      field_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
      field_label->setFixedWidth(ui_scale_metrics_.px(kMinimumControlLabelWidth));
      binding.field_label = field_label;
      row_layout->addWidget(field_label, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    } else {
      // A real cell preserves the same gap as the labeled rows without making
      // their height-for-width measurement include the inter-column spacing.
      auto *label_spacer = new QWidget(row);
      label_spacer->setObjectName(object_name + QStringLiteral(".label_spacer"));
      label_spacer->setFixedWidth(ui_scale_metrics_.px(kMinimumControlLabelWidth));
      row_layout->addWidget(label_spacer, 0, 0);
    }
    binding.container = row;

    switch (control.kind) {
    case QuaderToolSettingControlKind::Toggle: {
      auto *toggle = new QCheckBox(row);
      toggle->setObjectName(object_name);
      toggle->setAccessibleName(label);
      toggle->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
      QObject::connect(toggle, &QCheckBox::toggled, &panel_,
                       [this, tool_id = target.tool_id,
                        control_id = control.id](bool checked) {
                         if (!presentation_is_current() || synchronizing_) {
                           return;
                         }
                         const QuaderApiVoidResult result =
                             set_pane_setting(tool_id, control_id,
                                                          checked);
                         present_failure(editor_, "Tool settings update", result);
                         if (!result.ok()) {
                           invalidate_control_value(control_id);
                           schedule_refresh();
                         }
                       });
      row_layout->addWidget(toggle, 0, 1, Qt::AlignLeft | Qt::AlignVCenter);
      field_label->setBuddy(toggle);
      binding.toggle = toggle;
      break;
    }
    case QuaderToolSettingControlKind::Number: {
      const bool integer_control = uses_integer_spin_box(control);
      const QtToolSettingsNumberSliderMapping slider_mapping =
          qt_tool_settings_number_slider_mapping(
              control.min_value, control.max_value, integer_control,
              control.step);
      const auto send_number_to_editor =
          [this, tool_id = target.tool_id,
           control_id = control.id](
              double value, QuaderToolSettingEditOptions options) {
            if (!presentation_is_current()) {
              return;
            }
            const QuaderApiVoidResult result =
                set_pane_setting(tool_id, control_id, value,
                                             std::move(options));
            present_failure(editor_, "Tool settings update", result);
            if (!result.ok()) {
              number_dispatch_failed_ = true;
              schedule_refresh();
            }
          };
      auto *dispatch_timer = new QTimer(row);
      dispatch_timer->setSingleShot(true);
      dispatch_timer->setInterval(kNumberDispatchIntervalMs);
      dispatch_timer->setTimerType(Qt::PreciseTimer);
      binding.number_dispatch_timer = dispatch_timer;
      binding.number_dispatch = send_number_to_editor;
      QObject::connect(dispatch_timer, &QTimer::timeout, &panel_,
                       [this, control_index]() {
                         flush_number_dispatch(control_index);
                       });

      if (control.number_presentation ==
          QuaderToolSettingNumberPresentation::SpinBoxOnly) {
        auto *spin = new QtMixedNumberSpinBox(0.0, row);
        bind_number_interaction(*spin, binding, control_index, control);
        spin->setObjectName(object_name);
        spin->setAccessibleName(label);
        spin->setDecimals(number_decimals(control));
        spin->setRange(control.min_value, control.max_value);
        spin->setSingleStep(control.step);
        spin->present(control.number_value);
        spin->edited = [this, control_index,
                        positive_only = control.number_positive_only](
                           std::optional<double> value) {
          if (!presentation_is_current() || synchronizing_ ||
              control_index >= controls_.size()) {
            return;
          }
          ControlWidget &current = controls_[control_index];
          current.invalid_number_draft =
              !value.has_value() || !std::isfinite(*value) ||
              (positive_only && *value <= 0.0);
          if (current.invalid_number_draft) {
            queue_number_dispatch(control_index,
                                  current.floating->value());
            current.pending_number_value.reset();
            current.number_dispatch_timer->stop();
          } else {
            queue_number_dispatch(control_index, *value);
          }
          if (last_target_.has_value()) {
            apply_button_->setEnabled(last_target_->can_apply &&
                                      !has_invalid_number_draft());
          }
        };
        QObject::connect(spin, &QDoubleSpinBox::editingFinished, &panel_,
                         [this, control_index]() {
                           if (!synchronizing_ &&
                               control_index < controls_.size() &&
                               !controls_[control_index].invalid_number_draft) {
                             finish_number_continuous_edit(control_index);
                           }
                         });
        row_layout->addWidget(spin, 0, 1);
        field_label->setBuddy(spin);
        binding.floating = spin;
        binding.spinbox_only = spin;
        break;
      }

      const bool use_drag_spinner =
          integer_control &&
          control.number_presentation ==
              QuaderToolSettingNumberPresentation::DragSpinner;
      if (use_drag_spinner) {
        auto *spinner = new QtToolSettingsDragSpinner(row);
        // The standalone spinner is compact; a form field fills its column.
        spinner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        spinner->setObjectName(object_name + QStringLiteral(".spinner"));
        spinner->spin_box()->setObjectName(object_name);
        spinner->set_accessible_label(label);
        spinner->set_range(slider_mapping.slider_minimum,
                           slider_mapping.slider_maximum);
        spinner->set_single_step(integer_single_step(control.step));
        auto &controller = qt_install_numeric_spin_box(*spinner->spin_box());
        controller.set_drag_snap_scale(control.number_grid_scale);
        controller.set_drag_sensitivity(control.number_drag_sensitivity);
        spinner->spin_box()->setKeyboardTracking(false);
        spinner->set_value(qt_tool_settings_number_slider_position(
            slider_mapping, control.number_value));
        spinner->value_changed = [this, control_index](int value) {
          if (!presentation_is_current() || synchronizing_) {
            return;
          }
          queue_number_dispatch(control_index, static_cast<double>(value));
        };
        spinner->editing_started = [this, control_index]() {
          begin_number_continuous_edit(control_index);
        };
        spinner->editing_canceled = [this, control_index]() {
          discard_pending_number_dispatch(control_index);
          schedule_refresh();
        };
        spinner->editing_finished = [this, control_index]() {
          finish_number_continuous_edit(control_index);
        };
        spinner->drag_started = [this, control_index]() {
          begin_number_continuous_edit(control_index);
        };
        spinner->drag_finished = [this, control_index]() {
          finish_number_continuous_edit(control_index);
        };
        row_layout->addWidget(spinner, 0, 1);
        field_label->setBuddy(spinner->spin_box());
        binding.integer = spinner->spin_box();
        binding.number_drag_spinner = spinner;
        break;
      }

      if (integer_control) {
        auto *spin = new QtSpinBox(row);
        bind_number_interaction(*spin, binding, control_index, control);
        spin->setObjectName(object_name);
        spin->setAccessibleName(label);
        spin->setRange(slider_mapping.slider_minimum,
                       slider_mapping.slider_maximum);
        spin->setSingleStep(integer_single_step(control.step));
        spin->setKeyboardTracking(false);
        QObject::connect(spin, &QSpinBox::valueChanged, &panel_,
                         [this, control_index](int value) {
                           if (!presentation_is_current() || synchronizing_) {
                             return;
                           }
                           queue_number_dispatch(control_index,
                                                 static_cast<double>(value));
                         });
        QObject::connect(spin, &QSpinBox::editingFinished, &panel_,
                         [this, control_index]() {
                           finish_number_continuous_edit(control_index);
                         });
        if (QLineEdit *editor = spin->findChild<QLineEdit *>();
            editor != nullptr) {
          QObject::connect(editor, &QLineEdit::textEdited, &panel_,
                           [this, control_index](const QString &) {
                             begin_number_continuous_edit(control_index);
                           });
        }
        row_layout->addWidget(spin, 0, 1);
        field_label->setBuddy(spin);
        binding.integer = spin;
      } else {
        auto *spin = new QtDoubleSpinBox(row);
        bind_number_interaction(*spin, binding, control_index, control);
        spin->setObjectName(object_name);
        spin->setAccessibleName(label);
        const double minimum = std::min(control.min_value, control.max_value);
        const double maximum = std::max(control.min_value, control.max_value);
        // Preserve owner bounds before Qt rounds the supplied range.
        spin->setDecimals(number_decimals(control));
        spin->setRange(minimum, maximum);
        spin->setSingleStep(std::isfinite(control.step) && control.step > 0.0
                                ? control.step
                                : 0.001);
        spin->setKeyboardTracking(false);
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, &panel_,
                         [this, control_index](double value) {
                           if (!presentation_is_current() || synchronizing_) {
                             return;
                           }
                           queue_number_dispatch(control_index, value);
                         });
        QObject::connect(spin, &QDoubleSpinBox::editingFinished, &panel_,
                         [this, control_index]() {
                           finish_number_continuous_edit(control_index);
                         });
        if (QLineEdit *editor = spin->findChild<QLineEdit *>();
            editor != nullptr) {
          QObject::connect(editor, &QLineEdit::textEdited, &panel_,
                           [this, control_index](const QString &) {
                             begin_number_continuous_edit(control_index);
                           });
        }
        row_layout->addWidget(spin, 0, 1);
        field_label->setBuddy(spin);
        binding.floating = spin;
      }
      break;
    }
    case QuaderToolSettingControlKind::Vector3Number: {
      auto *fields = new QWidget(row);
      fields->setObjectName(object_name + QStringLiteral(".fields"));
      fields->setMinimumWidth(0);
      // Retain the child editors' readable minima when the form allocates its
      // field column. Each editor still ignores its potentially unbounded hint.
      fields->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
      auto *fields_layout = new QGridLayout(fields);
      set_scalable_margins(*fields_layout, QMargins(0, 0, 0, 0));
      set_scalable_grid_spacing(*fields_layout, 6, 3);
      fields_layout->setColumnStretch(1, 1);
      field_label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
      row_layout->setAlignment(field_label, Qt::AlignLeft | Qt::AlignTop);
      constexpr std::array<const char *, 3> kAxisNames{{"X", "Y", "Z"}};
      constexpr std::array<quader_editor_core::ViewAxis, 3> kAxes{{
          quader_editor_core::ViewAxis::X,
          quader_editor_core::ViewAxis::Y,
          quader_editor_core::ViewAxis::Z,
      }};

      for (std::size_t axis = 0; axis < 3U; ++axis) {
        const QuaderToolSettingNumberComponent &component =
            control.vector3_components[axis];
        const int axis_row = static_cast<int>(axis);
        const QString component_name = control_object_name(
            component.id, control_index * 3U + axis);
        auto *axis_label =
            new QLabel(QString::fromLatin1(kAxisNames[axis]), fields);
        axis_label->setObjectName(component_name +
                                  QStringLiteral(".axis_label"));
        axis_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        apply_qt_semantic_axis_label_color(*axis_label, kAxes[axis]);
        fields_layout->addWidget(axis_label, axis_row, 0);

        auto *spin = new QtDoubleSpinBox(fields);
        spin->setObjectName(component_name);
        spin->setAccessibleName(
            QStringLiteral("%1 %2").arg(label,
                                         QString::fromLatin1(kAxisNames[axis])));
        spin->setDecimals(
            std::max(4, decimal_places_for_step(component.step)));
        spin->setRange(std::min(component.min_value, component.max_value),
                       std::max(component.min_value, component.max_value));
        spin->setSingleStep(std::isfinite(component.step) &&
                                    component.step > 0.0
                                ? component.step
                                : 0.001);
        spin->setKeyboardTracking(false);
        set_scalable_minimum_width(*spin, 88);
        spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        spin->setValue(component.value);
        spin->setEnabled(component.enabled);
        fields_layout->addWidget(spin, axis_row, 1);
        axis_label->setBuddy(spin);

        auto *dispatch_timer = new QTimer(spin);
        dispatch_timer->setSingleShot(true);
        dispatch_timer->setInterval(kNumberDispatchIntervalMs);
        dispatch_timer->setTimerType(Qt::PreciseTimer);
        Vector3ComponentWidget &component_binding =
            binding.vector3_components[axis];
        component_binding.id = component.id;
        component_binding.spin = spin;
        auto &numeric_controller = qt_install_numeric_spin_box(*spin);
        component_binding.numeric_controller = &numeric_controller;
        numeric_controller.drag_started = [this, control_index, axis]() {
          begin_vector3_continuous_edit(control_index, axis);
        };
        numeric_controller.editing_started = numeric_controller.drag_started;
        numeric_controller.interaction_canceled = [this, control_index, axis]() {
          finish_vector3_continuous_edit(control_index, axis);
        };
        component_binding.dispatch_timer = dispatch_timer;
        component_binding.enabled = component.enabled;
        component_binding.dispatch =
            [this, tool_id = target.tool_id,
             component_id = component.id](
                double value, QuaderToolSettingEditOptions options) {
              if (!presentation_is_current()) {
                return;
              }
              const QuaderApiVoidResult result = set_pane_setting(
                  tool_id, component_id, value, std::move(options));
              present_failure(editor_, "Tool settings update", result);
              if (!result.ok()) {
                schedule_refresh();
              }
            };
        QObject::connect(dispatch_timer, &QTimer::timeout, &panel_,
                         [this, control_index, axis]() {
                           flush_vector3_dispatch(control_index, axis);
                         });
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, &panel_,
                         [this, control_index, axis](double value) {
                           if (!presentation_is_current() || synchronizing_) {
                             return;
                           }
                           queue_vector3_dispatch(control_index, axis, value);
                         });
        QObject::connect(spin, &QDoubleSpinBox::editingFinished, &panel_,
                         [this, control_index, axis]() {
                           finish_vector3_continuous_edit(control_index,
                                                          axis);
                         });
        if (QLineEdit *editor = spin->findChild<QLineEdit *>();
            editor != nullptr) {
          QObject::connect(editor, &QLineEdit::textEdited, &panel_,
                           [this, control_index,
                            axis](const QString &) {
                             begin_vector3_continuous_edit(control_index,
                                                           axis);
                           });
        }
      }
      row_layout->addWidget(fields, 0, 1);
      field_label->setBuddy(binding.vector3_components[0].spin);
      break;
    }
    case QuaderToolSettingControlKind::Choice: {
      if (control.choice_presentation ==
          QuaderToolSettingChoicePresentation::Buttons) {
        auto *buttons = new QWidget(row);
        buttons->setObjectName(object_name);
        auto *buttons_layout = new QHBoxLayout(buttons);
        set_scalable_margins(*buttons_layout, QMargins(0, 0, 0, 0));
        set_scalable_spacing(*buttons_layout, 4);
        row_layout->addWidget(buttons, 0, 1);
        auto *button_group = new QButtonGroup(row);
        button_group->setObjectName(object_name + QStringLiteral(".group"));
        button_group->setExclusive(true);
        for (std::size_t option_index = 0;
             option_index < control.choices.size(); ++option_index) {
          const QuaderToolSettingChoiceOption &option =
              control.choices[option_index];
          const std::string option_key = control.id + "." + option.id;
          const QString option_label = localized_descriptor_label(
              localization_, option_key, option.label);
          auto *button = new QPushButton(option_label, buttons);
          button->setObjectName(
              option_object_name(object_name, option.id, option_index));
          button->setAccessibleName(option_label);
          button->setCheckable(true);
          if (!option.tooltip.empty()) {
            button->setToolTip(localized_descriptor_label(
                localization_, option_key + ".tooltip", option.tooltip));
          }
          button_group->addButton(button);
          buttons_layout->addWidget(button, 1);
          QObject::connect(
              button, &QPushButton::toggled, &panel_,
              [this, tool_id = target.tool_id, control_id = control.id,
               option_id = option.id](bool checked) {
                if (!checked || !presentation_is_current() || synchronizing_) {
                  return;
                }
                const std::uint64_t generation = binding_generation_;
                flush_pending_number_dispatches();
                if (generation != binding_generation_ ||
                    !presentation_is_current()) {
                  return;
                }
                const QuaderApiVoidResult result = set_pane_setting(
                    tool_id, control_id, std::string_view(option_id));
                present_failure(editor_, "Tool settings update", result);
                if (!result.ok()) {
                  invalidate_control_value(control_id);
                  schedule_refresh();
                }
              });
          binding.choices.push_back(
              ChoiceWidget{button, option.id, option.value});
        }
        if (!binding.choices.empty()) {
          field_label->setBuddy(binding.choices.front().button);
        }
        break;
      }
      if (control.choice_presentation ==
          QuaderToolSettingChoicePresentation::Dropdown) {
        auto *dropdown = new QComboBox(row);
        dropdown->setObjectName(object_name);
        dropdown->setAccessibleName(label);
        for (const QuaderToolSettingChoiceOption &option : control.choices) {
          std::string option_localization_key(control.id);
          option_localization_key.push_back('.');
          option_localization_key.append(option.id);
          dropdown->addItem(localized_descriptor_label(
                                localization_, option_localization_key,
                                option.label),
                            qtext(option.id));
          if (!option.tooltip.empty()) {
            dropdown->setItemData(
                dropdown->count() - 1,
                localized_descriptor_label(localization_,
                    option_localization_key + ".tooltip", option.tooltip),
                Qt::ToolTipRole);
          }
        }
        field_label->setBuddy(dropdown);
        row_layout->addWidget(dropdown, 0, 1);
        QObject::connect(
            dropdown, &QComboBox::currentIndexChanged, &panel_,
            [this, dropdown, tool_id = target.tool_id,
             control_id = control.id](int index) {
              if (!presentation_is_current() || synchronizing_ || index < 0) {
                return;
              }
              const QByteArray option_id =
                  dropdown->itemData(index).toString().toUtf8();
              const QuaderApiVoidResult result = set_pane_setting(
                  tool_id, control_id,
                  std::string_view(option_id.constData(),
                                   static_cast<std::size_t>(option_id.size())));
              present_failure(editor_, "Tool settings update", result);
              // The chosen primitive changes the descriptor itself. Refresh
              // after signal delivery so the old control tree can be safely
              // replaced with the fields for the newly selected kind.
              schedule_refresh();
            });
        binding.dropdown = dropdown;
        break;
      }

      auto *group = new QWidget(row);
      group->setObjectName(object_name);
      auto *choice_layout = new QVBoxLayout(group);
      set_scalable_margins(*choice_layout, QMargins(0, 0, 0, 0));
      set_scalable_spacing(*choice_layout, 3);
      auto *button_group = new QButtonGroup(group);
      button_group->setObjectName(object_name + QStringLiteral(".group"));
      button_group->setExclusive(true);
      for (std::size_t option_index = 0;
           option_index < control.choices.size(); ++option_index) {
        const QuaderToolSettingChoiceOption &option =
            control.choices[option_index];
        std::string option_localization_key = "tool_settings.";
        option_localization_key.append(target.tool_id);
        option_localization_key.push_back('.');
        option_localization_key.append(option.id);
        const QString option_label = localized_descriptor_label(
            localization_, option_localization_key, option.label);
        auto *radio = new QRadioButton(option_label, group);
        radio->setObjectName(
            option_object_name(object_name, option.id, option_index));
        radio->setAccessibleName(option_label);
        if (!option.tooltip.empty()) {
          radio->setToolTip(localized_descriptor_label(
              localization_, control.id + "." + option.id + ".tooltip",
              option.tooltip));
        }
        button_group->addButton(radio);
        choice_layout->addWidget(radio);
        QObject::connect(radio, &QRadioButton::toggled, &panel_,
                         [this, tool_id = target.tool_id,
                          control_id = control.id,
                          value = option.value](bool checked) {
                           if (!checked || !presentation_is_current() ||
                               synchronizing_) {
                             return;
                           }
                           const QuaderApiVoidResult result =
                               set_pane_setting(tool_id, control_id,
                                                            value);
                           present_failure(editor_, "Tool settings update", result);
                           if (!result.ok()) {
                             invalidate_control_value(control_id);
                             schedule_refresh();
                           }
                         });
        binding.choices.push_back(
            ChoiceWidget{radio, option.id, option.value});
      }
      row_layout->addWidget(group, 0, 1);
      if (!binding.choices.empty()) {
        field_label->setBuddy(binding.choices.front().button);
      }
      break;
    }
    case QuaderToolSettingControlKind::Action: {
      const auto connect_action =
          [this, tool_id = target.tool_id](QPushButton &button,
                                          std::string action_id) {
        QObject::connect(&button, &QPushButton::clicked, &panel_,
                       [this, tool_id, action_id = std::move(action_id)]() {
                         if (!presentation_is_current()) {
                           discard_pending_number_dispatches();
                           return;
                         }
                         // Actions consume the presented settings. Commit any
                         // coalesced numeric edit first so the owner observes
                         // the latest value rather than the last timer tick.
                         const std::uint64_t generation = binding_generation_;
                         flush_pending_number_dispatches();
                         if (generation != binding_generation_ ||
                             !presentation_is_current()) {
                           return;
                         }
                         const QuaderApiVoidResult result =
                             execute_pane_action(tool_id,
                                                             action_id);
                         present_failure(editor_, "Tool settings action", result);
                         schedule_refresh();
                       });
      };
      if (!control.choices.empty()) {
        auto *buttons = new QWidget(row);
        buttons->setObjectName(object_name);
        auto *buttons_layout = new QHBoxLayout(buttons);
        set_scalable_margins(*buttons_layout, QMargins(0, 0, 0, 0));
        set_scalable_spacing(*buttons_layout, 4);
        row_layout->addWidget(buttons, 0, 1);
        for (std::size_t option_index = 0;
             option_index < control.choices.size(); ++option_index) {
          const auto &option = control.choices[option_index];
          const std::string action_id = control.id + "." + option.id;
          const std::string option_key = control.label_key.empty()
                                             ? action_id
                                             : control.label_key + "." + option.id;
          const QString option_label = localized_descriptor_label(
              localization_, option_key, option.label);
          auto *button = new QPushButton(option_label, buttons);
          button->setObjectName(
              option_object_name(object_name, option.id, option_index));
          button->setAccessibleName(option_label);
          if (!option.tooltip.empty()) {
            button->setToolTip(localized_descriptor_label(
                localization_, option_key + ".tooltip", option.tooltip));
          }
          buttons_layout->addWidget(button, 1);
          connect_action(*button, action_id);
          if (option_index == 0) {
            field_label->setBuddy(button);
          }
        }
        break;
      }
      auto *button = new QPushButton(label, row);
      button->setObjectName(object_name);
      button->setAccessibleName(label);
      connect_action(*button, control.id);
      row_layout->addWidget(button, 0, 1);
      binding.label_button = button;
      break;
    }
    }
    if (binding.floating != nullptr) {
      set_scalable_minimum_width(*binding.floating, 80);
      binding.floating->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    if (binding.integer != nullptr) {
      set_scalable_minimum_width(*binding.integer, 80);
      binding.integer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    control_layout.addWidget(row);

    if (binding.container != nullptr && !control.tooltip.empty()) {
      const QString tooltip = localized_descriptor_label(
          localization_, control.id + ".tooltip", control.tooltip);
      binding.container->setToolTip(tooltip);
      if (binding.toggle != nullptr) {
        binding.toggle->setToolTip(tooltip);
      }
      if (binding.spinbox_only != nullptr) {
        binding.spinbox_only->setToolTip(tooltip);
      }
      if (binding.field_label != nullptr) {
        binding.field_label->setToolTip(tooltip);
      }
    }
    binding.presented_label = control.label;
    binding.presented_label_key = control.label_key;
    controls_.push_back(std::move(binding));
  }

  /// Updates descriptor text without rebuilding editors or revisiting idle labels.
  void synchronize_control_label(ControlWidget &binding,
                                 const QuaderToolSettingControl &control) {
    if (binding.presented_label == control.label &&
        binding.presented_label_key == control.label_key) {
      return;
    }
    const QString label = localized_descriptor_label(
        localization_,
        control.label_key.empty() ? control.id : control.label_key,
        control.label);
    if (binding.field_label != nullptr &&
        binding.field_label->text() != label) {
      binding.field_label->setText(label);
      control_label_width_dirty_ = true;
    }
    if (binding.label_button != nullptr &&
        binding.label_button->text() != label) {
      binding.label_button->setText(label);
    }
    const auto update_accessible_name = [](QWidget *widget,
                                          const QString &name) {
      if (widget != nullptr && widget->accessibleName() != name) {
        widget->setAccessibleName(name);
      }
    };
    update_accessible_name(binding.label_button, label);
    update_accessible_name(binding.toggle, label);
    update_accessible_name(binding.integer, label);
    update_accessible_name(binding.floating, label);
    update_accessible_name(binding.dropdown, label);
    if (binding.number_drag_spinner != nullptr &&
        binding.number_drag_spinner->accessibleName() != label) {
      binding.number_drag_spinner->set_accessible_label(label);
    }
    constexpr std::array<const char *, 3> kAxisNames{{"X", "Y", "Z"}};
    for (std::size_t axis = 0; axis < binding.vector3_components.size(); ++axis) {
      if (binding.vector3_components[axis].spin != nullptr) {
        update_accessible_name(binding.vector3_components[axis].spin,
            QStringLiteral("%1 %2").arg(
                label, QString::fromLatin1(kAxisNames[axis])));
      }
    }
    binding.presented_label = control.label;
    binding.presented_label_key = control.label_key;
  }

  void invalidate_control_value(std::string_view control_id) {
    for (ControlWidget &binding : controls_) {
      if (binding.id == control_id) {
        binding.presented_choice_value.reset();
        binding.presented_toggle_value.reset();
        break;
      }
    }
  }

  void synchronize_values(const QuaderToolSettingsTarget &target) {
    synchronizing_ = true;
    const bool preview_completed = last_target_.has_value() &&
                                   last_target_->preview_active &&
                                   !target.preview_active;
    if (preview_completed) {
      // Completing a preview ends its drafts even if Apply retained field focus.
      // Present owner resets now; later typing starts a new numeric interaction.
      discard_pending_number_dispatches();
    }
    for (std::size_t index = 0; index < controls_.size(); ++index) {
      ControlWidget &binding = controls_[index];
      if (binding.spinbox_only != nullptr &&
          binding.pending_number_lifecycle.has_value() &&
          !number_dispatch_lifecycle_is_current(binding)) {
        discard_pending_number_dispatch(index);
        binding.spinbox_only->present(target.controls[index].number_value);
      }
    }
    const QString target_title =
        localized_target_title(localization_, target);
    const QString button_title = target_title_button_text(target_title);
    if (target_title_->text() != button_title) {
      target_title_->setText(button_title);
      target_title_->setAccessibleName(target_title);
      panel_.setWindowTitle(target_title);
    }
    if (apply_button_->isHidden() == target.apply_visible) {
      apply_button_->setVisible(target.apply_visible);
    }
    const bool can_apply = target.can_apply && !has_invalid_number_draft();
    if (apply_button_->isEnabled() != can_apply) {
      apply_button_->setEnabled(can_apply);
    }
    if (cancel_button_->isHidden() == target.can_cancel) {
      cancel_button_->setVisible(target.can_cancel);
    }
    if (cancel_button_->isEnabled() != target.can_cancel) {
      cancel_button_->setEnabled(target.can_cancel);
    }
    const bool preview_actions_visible =
        target.apply_visible || target.can_cancel;
    if (preview_actions_container_->isHidden() ==
        preview_actions_visible) {
      preview_actions_container_->setVisible(preview_actions_visible);
    }
    synchronize_body_visibility();

    const std::size_t count = std::min(controls_.size(), target.controls.size());
    for (std::size_t index = 0; index < count; ++index) {
      const QuaderToolSettingControl &control = target.controls[index];
      ControlWidget &binding = controls_[index];
      synchronize_control_label(binding, control);
      if (binding.container != nullptr &&
          binding.container->isEnabled() != control.enabled) {
        binding.container->setEnabled(control.enabled);
      }
      const bool integer_editing =
          !preview_completed &&
          (widget_or_descendant_has_focus(binding.integer) ||
           (binding.integer != nullptr && binding.numeric_controller != nullptr &&
            binding.numeric_controller->is_dragging()));
      const bool floating_editing =
          !preview_completed &&
          (widget_or_descendant_has_focus(binding.floating) ||
           (binding.floating != nullptr && binding.numeric_controller != nullptr &&
            binding.numeric_controller->is_dragging()));
      const bool drag_spinner_editing =
          binding.number_drag_spinner != nullptr &&
          (binding.number_drag_spinner->is_dragging() ||
           binding.number_drag_spinner->is_editing());
      const bool number_dispatch_pending =
          binding.pending_number_value.has_value();
      const bool number_interaction_active =
          drag_spinner_editing || integer_editing ||
          floating_editing ||
          number_dispatch_pending;

      for (std::size_t axis = 0; axis < 3U; ++axis) {
        Vector3ComponentWidget &component_binding =
            binding.vector3_components[axis];
        if (component_binding.spin == nullptr) {
          continue;
        }
        const QuaderToolSettingNumberComponent &component =
            control.vector3_components[axis];
        if (component_binding.enabled != component.enabled) {
          component_binding.spin->setEnabled(component.enabled);
          component_binding.enabled = component.enabled;
        }
        const bool component_editing =
            (!preview_completed &&
             (widget_or_descendant_has_focus(component_binding.spin) ||
              (component_binding.numeric_controller != nullptr &&
               component_binding.numeric_controller->is_dragging()))) ||
            component_binding.pending_value.has_value();
        if (component_editing) {
          continue;
        }
        const double minimum =
            std::min(component.min_value, component.max_value);
        const double maximum =
            std::max(component.min_value, component.max_value);
        const double single_step =
            std::isfinite(component.step) && component.step > 0.0
                ? component.step
                : 0.001;
        const int decimals =
            std::max(4, decimal_places_for_step(component.step));
        const QSignalBlocker blocker(component_binding.spin);
        if (component_binding.spin->decimals() != decimals) {
          component_binding.spin->setDecimals(decimals);
        }
        if (component_binding.spin->minimum() != minimum ||
            component_binding.spin->maximum() != maximum) {
          component_binding.spin->setRange(minimum, maximum);
        }
        if (component_binding.spin->singleStep() != single_step) {
          component_binding.spin->setSingleStep(single_step);
        }
        if (component_binding.spin->value() != component.value) {
          component_binding.spin->setValue(component.value);
        }
      }

      if (!number_interaction_active) {
        if (binding.integer != nullptr && binding.number_drag_spinner == nullptr) {
          const auto range = qt_tool_settings_number_slider_mapping(
              control.min_value, control.max_value, true, control.step);
          const QSignalBlocker blocker(binding.integer);
          if (binding.integer->minimum() != range.slider_minimum ||
              binding.integer->maximum() != range.slider_maximum) {
            binding.integer->setRange(range.slider_minimum, range.slider_maximum);
          }
          const int step = integer_single_step(control.step);
          if (binding.integer->singleStep() != step) {
            binding.integer->setSingleStep(step);
          }
        }
        if (binding.floating != nullptr && binding.spinbox_only == nullptr) {
          const QSignalBlocker blocker(binding.floating);
          const double minimum = std::min(control.min_value, control.max_value);
          const double maximum = std::max(control.min_value, control.max_value);
          const int decimals = number_decimals(control);
          if (binding.floating->decimals() != decimals) {
            binding.floating->setDecimals(decimals);
          }
          if (binding.floating->minimum() != minimum ||
              binding.floating->maximum() != maximum) {
            binding.floating->setRange(minimum, maximum);
          }
          const double step = std::isfinite(control.step) && control.step > 0.0
                                  ? control.step : 0.001;
          if (binding.floating->singleStep() != step) {
            binding.floating->setSingleStep(step);
          }
        }
      }

      if (binding.number_drag_spinner != nullptr &&
          !number_interaction_active) {
        const QtToolSettingsNumberSliderMapping desired_mapping =
            qt_tool_settings_number_slider_mapping(
                control.min_value, control.max_value, true, control.step);
        binding.number_drag_spinner->set_range(desired_mapping.slider_minimum,
                                               desired_mapping.slider_maximum);
        binding.number_drag_spinner->set_single_step(
            integer_single_step(control.step));
        binding.number_drag_spinner->set_value(
            qt_tool_settings_number_slider_position(desired_mapping,
                                                    control.number_value));
      }

      if (binding.toggle != nullptr &&
          binding.presented_toggle_value != control.bool_value) {
        if (binding.toggle->isChecked() != control.bool_value) {
          const QSignalBlocker blocker(binding.toggle);
          binding.toggle->setChecked(control.bool_value);
        }
        binding.presented_toggle_value = control.bool_value;
      }
      if (binding.integer != nullptr &&
          binding.number_drag_spinner == nullptr && !integer_editing &&
          !number_dispatch_pending) {
        const QSignalBlocker blocker(binding.integer);
        binding.integer->setValue(
            static_cast<int>(std::llround(control.number_value)));
      }
      if (binding.floating != nullptr && !floating_editing &&
          !number_dispatch_pending) {
        const QSignalBlocker blocker(binding.floating);
        binding.floating->setValue(control.number_value);
      }
      if (binding.dropdown != nullptr) {
        const QSignalBlocker blocker(binding.dropdown);
        int selected_index = -1;
        for (std::size_t option_index = 0;
             option_index < control.choices.size(); ++option_index) {
          if (choice_matches(control.choice_value,
                             control.choices[option_index])) {
            selected_index = static_cast<int>(option_index);
            break;
          }
        }
        binding.dropdown->setCurrentIndex(selected_index);
      }
      if (!binding.choices.empty() &&
          binding.presented_choice_value != control.choice_value) {
        QButtonGroup *group = binding.choices.front().button->group();
        const bool exclusive = group != nullptr && group->exclusive();
        if (exclusive) group->setExclusive(false);
        for (std::size_t option_index = 0;
             option_index < binding.choices.size() &&
             option_index < control.choices.size();
             ++option_index) {
          ChoiceWidget &choice = binding.choices[option_index];
          const bool checked = choice_matches(
              control.choice_value, control.choices[option_index]);
          if (choice.button->isChecked() != checked) {
            const QSignalBlocker blocker(choice.button);
            choice.button->setChecked(checked);
          }
        }
        if (exclusive) group->setExclusive(true);
        binding.presented_choice_value = control.choice_value;
      }
    }
    synchronize_control_label_width();
    synchronizing_ = false;
  }

  QtToolSettingsPanel &panel_;
  QtUiScaleMetrics ui_scale_metrics_;
  const LocalizationService *localization_ = nullptr;
  QuaderEditorApi *editor_ = nullptr;
  std::uint64_t binding_generation_ = 0;
  std::uint64_t target_presentation_serial_ = 0;
  ViewportPaneId target_pane_ = ViewportPaneId::Perspective;
  std::uint64_t target_tool_revision_ = 0;
  std::uint64_t control_generation_ = 0;
  bool number_dispatch_failed_ = false;
  QtPanelSectionHeader *target_title_ = nullptr;
  QScrollArea *scroll_area_ = nullptr;

  QWidget *controls_container_ = nullptr;
  QVBoxLayout *controls_layout_ = nullptr;
  QWidget *actions_container_ = nullptr;
  QWidget *preview_actions_container_ = nullptr;
  QPushButton *apply_button_ = nullptr;
  QPushButton *cancel_button_ = nullptr;
  std::vector<ControlWidget> controls_;
  std::vector<ControlGroupWidget> control_groups_;
  std::unordered_map<std::string, bool> control_group_expansion_;
  std::optional<QuaderToolSettingsTarget> last_target_;
  bool expanded_ = true;
  QSize last_presentation_size_;
  bool last_presentation_available_ = false;
  bool synchronizing_ = false;
  bool refresh_scheduled_ = false;
  bool inactive_presented_ = false;
  bool localized_text_dirty_ = true;
  bool control_label_width_dirty_ = true;
};

QtToolSettingsPanel::QtToolSettingsPanel(QWidget *parent)
    : QWidget(parent),
      impl_(std::make_unique<Impl>(*this)) {}

QtToolSettingsPanel::~QtToolSettingsPanel() = default;

void QtToolSettingsPanel::bind(QuaderEditorApi &editor) {
  impl_->bind(editor);
}

void QtToolSettingsPanel::unbind() { impl_->unbind(); }

void QtToolSettingsPanel::apply_ui_scale(
    const QtUiScaleMetrics &metrics) {
  impl_->apply_ui_scale(metrics);
}

void QtToolSettingsPanel::set_localization(
    const LocalizationService &localization) {
  impl_->set_localization(localization);
}

void QtToolSettingsPanel::refresh() { impl_->refresh(); }

bool QtToolSettingsPanel::has_active_target() const {
  return impl_->has_active_target();
}

bool QtToolSettingsPanel::has_settings() const {
  return impl_->has_active_target();
}

std::string_view QtToolSettingsPanel::target_id() const {
  return impl_->target_id();
}

bool QtToolSettingsPanel::expanded() const { return impl_->expanded(); }

void QtToolSettingsPanel::set_expanded(bool expanded) {
  impl_->set_expanded(expanded);
}

QSize QtToolSettingsPanel::sizeHint() const { return impl_->preferred_size(); }

QSize QtToolSettingsPanel::minimumSizeHint() const {
  const QSize preferred = impl_->preferred_size();
  return {preferred.width(), QWidget::minimumSizeHint().height()};
}

bool QtToolSettingsPanel::numeric_interaction_active() const {
  return impl_->numeric_interaction_active();
}

bool QtToolSettingsPanel::prepare_lifecycle_intent(
    QuaderEditorApi &editor, QuaderNativeToolSettingsIntent intent) {
  return impl_->prepare_lifecycle_intent(editor, intent);
}

bool qt_object_belongs_to_tool_settings_panel(const QObject *target) {
  for (const QObject *current = target; current != nullptr;
       current = current->parent()) {
    if (dynamic_cast<const QtToolSettingsPanel *>(current) != nullptr) {
      return true;
    }
  }

  const auto *target_window = qobject_cast<const QWindow *>(target);
  if (target_window == nullptr ||
      qobject_cast<QApplication *>(QCoreApplication::instance()) == nullptr) {
    return false;
  }
  // Native child and popup windows retain their owning QWidget. Walking that
  // widget's ancestry also covers a combo's separate popup container.
  const QWidget *widget = QWidget::find(target_window->winId());
  return widget != nullptr &&
         qt_object_belongs_to_tool_settings_panel(widget);
}

bool qt_object_belongs_to_tool_settings_panel(
    const QObject *target, const QPoint &global_position) {
  const auto *target_window = qobject_cast<const QWindow *>(target);
  if (target_window == nullptr ||
      qobject_cast<QApplication *>(QCoreApplication::instance()) == nullptr) {
    return false;
  }
  const QWidget *window_widget = QWidget::find(target_window->winId());
  if (window_widget == nullptr || !window_widget->isVisible()) {
    return false;
  }
  const QPoint local = window_widget->mapFromGlobal(global_position);
  if (!window_widget->rect().contains(local)) {
    return false;
  }
  const QWidget *hit = window_widget->childAt(local);
  const QWidget *recipient = hit != nullptr ? hit : window_widget;
  // An ancestor's visible region excludes native children. Test the resolved
  // child instead so pointer delivery through that ancestor still owns the
  // overlay, without accepting hidden or obscured settings controls.
  return qt_object_belongs_to_tool_settings_panel(recipient) &&
         recipient->visibleRegion().contains(
             recipient->mapFromGlobal(global_position));
}

} // namespace quader
