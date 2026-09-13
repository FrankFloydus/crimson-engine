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
#include "ui/qt_semantic_axis_colors.hpp"
#include "ui/qt_tool_settings_drag_spinner.hpp"
#include "ui/qt_tool_settings_number_slider.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/ui_panel.hpp"


#include <QApplication>
#include <QByteArray>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QFont>
#include <QFocusEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMargins>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
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
constexpr int kNumberDispatchIntervalMs = 50;

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
  if (localization != nullptr && !target.tool_id.empty()) {
    std::string key = "tool_settings.target.";
    key.append(target.tool_id);
    return QString::fromStdString(localization->text(key, target.title));
  }
  if (!target.title.empty()) {
    return qtext(target.title);
  }
  return localized_text(localization, "ui.panel.tool_settings",
                        "Tool Settings");
}

QString localized_target_description(
    const LocalizationService *localization,
    const QuaderToolSettingsTarget &target) {
  QString description = qtext(target.description);
  if (localization != nullptr && !target.tool_id.empty()) {
    std::string key = "tool_settings.description.";
    key.append(target.tool_id);
    description = QString::fromStdString(
        localization->text(key, target.description));
  }
  if (!target.status_message.empty()) {
    description.append(QStringLiteral("\n\n"));
    description.append(qtext(target.status_message));
  }
  return description;
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
      left.title != right.title || left.description != right.description ||
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

void update_slider_steps(QtToolSettingsNumberSlider &slider,
                         const QtToolSettingsNumberSliderMapping &mapping,
                         double value, double logical_step) {
  const int single_step = qt_tool_settings_number_slider_step(
      mapping, value, logical_step);
  slider.setSingleStep(single_step);
  if (slider.stepwise_interaction()) {
    slider.setPageStep(single_step);
    slider.setTickInterval(single_step);
    slider.setTickPosition(QSlider::TicksBelow);
  } else {
    slider.setPageStep(single_step <= std::numeric_limits<int>::max() / 10
                           ? single_step * 10
                           : std::numeric_limits<int>::max());
    slider.setTickInterval(0);
    slider.setTickPosition(QSlider::NoTicks);
  }
  slider.setTracking(true);
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
    QGroupBox *label_group = nullptr;
    std::string presented_label;
    std::string presented_label_key;
    QCheckBox *toggle = nullptr;
    QSpinBox *integer = nullptr;
    QDoubleSpinBox *floating = nullptr;
    QtMixedNumberSpinBox *spinbox_only = nullptr;
    bool invalid_number_draft = false;
    QtToolSettingsDragSpinner *number_drag_spinner = nullptr;
    QtToolSettingsNumberSlider *number_slider = nullptr;
    QComboBox *dropdown = nullptr;
    QtToolSettingsNumberSliderMapping number_slider_mapping;
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
    panel_.setObjectName(QString::fromLatin1(
        ui_panel_stable_id(UIPanelId::ToolSettings)));
    // This widget is the content of a user-resizable/restored dock. Keep the
    // constraint in logical Qt units so a live interface-scale change cannot
    // override the user's saved dock width. First-run dock sizing is scaled by
    // UISystem before restoreState() instead.
    panel_.setMinimumWidth(240);

    auto *root_layout = new QVBoxLayout(&panel_);
    root_layout->setObjectName(QStringLiteral("tool_settings.layout"));
    set_scalable_margins(*root_layout, QMargins(8, 8, 8, 8));
    set_scalable_spacing(*root_layout, 8);

    scroll_area_ = new QScrollArea(&panel_);
    scroll_area_->setObjectName(QStringLiteral("tool_settings.scroll"));
    scroll_area_->setFrameShape(QFrame::NoFrame);
    scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_area_->setWidgetResizable(true);
    panel_.setFocusProxy(scroll_area_);

    auto *scroll_content = new QWidget(scroll_area_);
    scroll_content->setObjectName(QStringLiteral("tool_settings.content"));
    auto *content_layout = new QVBoxLayout(scroll_content);
    set_scalable_margins(*content_layout, QMargins(0, 0, 0, 0));
    set_scalable_spacing(*content_layout, 8);

    target_title_ = new QLabel(scroll_content);
    target_title_->setObjectName(QStringLiteral("tool_settings.target_title"));
    target_title_->setWordWrap(true);
    QFont target_font = target_title_->font();
    target_font.setBold(true);
    target_title_->setFont(target_font);
    content_layout->addWidget(target_title_);

    title_separator_ = new QFrame(scroll_content);
    title_separator_->setObjectName(
        QStringLiteral("tool_settings.title_separator"));
    title_separator_->setFrameShape(QFrame::HLine);
    content_layout->addWidget(title_separator_);

    description_section_ = new QtSettingsSection(
        QStringLiteral("tool_settings.section.description"),
        scroll_content);
    auto *description_layout =
        new QVBoxLayout(description_section_->content());
    set_scalable_margins(*description_layout, QMargins(8, 6, 8, 6));
    description_label_ =
        new QLabel(description_section_->content());
    description_label_->setObjectName(
        QStringLiteral("tool_settings.description"));
    description_label_->setWordWrap(true);
    description_label_->setTextInteractionFlags(
        Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    description_layout->addWidget(description_label_);
    content_layout->addWidget(description_section_);

    settings_section_ = new QtSettingsSection(
        QStringLiteral("tool_settings.section.settings"), scroll_content);
    controls_container_ = settings_section_->content();
    controls_container_->setObjectName(QStringLiteral(
        "tool_settings.section.settings.content"));
    controls_layout_ = new QVBoxLayout(controls_container_);
    set_scalable_margins(*controls_layout_, QMargins(8, 6, 8, 6));
    set_scalable_spacing(*controls_layout_, 6);
    content_layout->addWidget(settings_section_);
    content_layout->addStretch(1);
    scroll_area_->setWidget(scroll_content);
    root_layout->addWidget(scroll_area_, 1);

    actions_container_ = new QWidget(&panel_);
    actions_container_->setObjectName(QStringLiteral("tool_settings.actions"));
    auto *actions_layout = new QVBoxLayout(actions_container_);
    set_scalable_margins(*actions_layout, QMargins(0, 0, 0, 0));
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

    reset_button_ = new QPushButton(actions_container_);
    reset_button_->setObjectName(
        QStringLiteral("tool_settings.reset_defaults"));
    reset_button_->setFocusPolicy(Qt::TabFocus);
    reset_button_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    actions_layout->addWidget(reset_button_);
    root_layout->addWidget(actions_container_);
    refresh_localized_text();

    QObject::connect(reset_button_, &QPushButton::pressed, &panel_, [this]() {
      discard_pending_number_dispatches();
    });
    QObject::connect(reset_button_, &QPushButton::clicked, &panel_, [this]() {
      // Reset is authoritative: a stale delayed edit must never overwrite it.
      discard_pending_number_dispatches();
      reset_button_->setFocus(Qt::MouseFocusReason);
      if (editor_ == nullptr) {
        return;
      }
      const QuaderApiVoidResult result =
          editor_->tools().reset_active_settings();
      present_failure(editor_, "Reset tool settings", result);
      schedule_refresh();
    });
    QObject::connect(apply_button_, &QPushButton::clicked, &panel_, [this]() {
      if (editor_ == nullptr) {
        discard_pending_number_dispatches();
        return;
      }
      if (has_invalid_number_draft()) {
        return;
      }
      number_dispatch_failed_ = false;
      const std::uint64_t generation = binding_generation_;
      const std::uint64_t tool_revision = editor_->tools().active_tool_revision();
      flush_pending_number_dispatches();
      if (number_dispatch_failed_ || editor_ == nullptr ||
          generation != binding_generation_ ||
          editor_->tools().active_tool_revision() != tool_revision) {
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
                       if (editor_ == nullptr) {
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
    // the selected locale changes instead of updating only the dock title.
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

    const QuaderToolSettingsTarget target = editor_->tools().active_settings();
    if (!target.active) {
      if (!inactive_presented_) {
        present_inactive_target();
      }
      return;
    }

    if (!last_target_.has_value() ||
        !same_target_structure(*last_target_, target)) {
      rebuild(target);
    } else {
      synchronize_values(target);
    }
    last_target_ = target;
  }

  [[nodiscard]] bool has_active_target() const {
    return last_target_.has_value() && last_target_->active;
  }

  [[nodiscard]] bool numeric_interaction_active() const {
    return std::any_of(
        controls_.begin(), controls_.end(), [](const ControlWidget &binding) {
          const bool vector_pending = std::any_of(
              binding.vector3_components.begin(),
              binding.vector3_components.end(),
              [](const Vector3ComponentWidget &component) {
                return component.pending_value.has_value();
              });
          return binding.pending_number_value.has_value() || vector_pending ||
                 (binding.number_drag_spinner != nullptr &&
                   (binding.number_drag_spinner->is_dragging() ||
                    binding.number_drag_spinner->is_editing())) ||
                 (binding.number_slider != nullptr &&
                  binding.number_slider->isSliderDown());
        });
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    if (ui_scale_metrics_.revision() == metrics.revision() &&
        qFuzzyCompare(static_cast<qreal>(ui_scale_metrics_.factor()),
                      static_cast<qreal>(metrics.factor()))) {
      return;
    }
    ui_scale_metrics_ = metrics;
    apply_current_ui_scale();
  }

private:
  void apply_current_ui_scale() {
    apply_scalable_layout_metrics(panel_, ui_scale_metrics_);
    apply_scalable_widget_metrics(panel_, ui_scale_metrics_);
    QFont target_font = QApplication::font(target_title_);
    target_font.setBold(true);
    target_title_->setFont(target_font);
    panel_.updateGeometry();
    if (scroll_area_->widget() != nullptr) {
      scroll_area_->widget()->updateGeometry();
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
    empty_settings_label_ = nullptr;
    clear_layout(*controls_layout_);
    target_title_->clear();
    description_label_->clear();
    reset_button_->setEnabled(false);
    preview_actions_container_->hide();
    apply_button_->hide();
    cancel_button_->hide();
  }

  void add_empty_settings_message(const QString &text) {
    empty_settings_label_ = new QLabel(text, controls_container_);
    empty_settings_label_->setObjectName(
        QStringLiteral("tool_settings.settings_empty"));
    empty_settings_label_->setWordWrap(true);
    controls_layout_->addWidget(empty_settings_label_);
  }

  void set_tool_sections_visible(bool visible) {
    if (description_section_->isHidden() == visible) {
      description_section_->setVisible(visible);
    }
    if (settings_section_->isHidden() == visible) {
      settings_section_->setVisible(visible);
    }
  }

  void synchronize_target_heading_visibility() {
    constexpr bool kVisible = true;
    if (target_title_->isHidden() == kVisible) {
      target_title_->setVisible(kVisible);
    }
    if (title_separator_->isHidden() == kVisible) {
      title_separator_->setVisible(kVisible);
    }
  }

  void present_inactive_target() {
    clear_controls();
    last_target_.reset();
    inactive_presented_ = true;
    set_tool_sections_visible(false);
    synchronize_target_heading_visibility();
    target_title_->setText(localized_text(
        localization_, "tool_settings.no_active.title", "No Active Tool"));
    description_label_->setText(localized_text(
        localization_, "tool_settings.no_active.description",
        "Activate a tool to view its description and settings."));
    add_empty_settings_message(localized_text(
        localization_, "tool_settings.no_active.settings",
        "No tool settings are available."));
    controls_layout_->addStretch(1);
    apply_current_ui_scale();
  }

  void begin_number_continuous_edit(std::size_t control_index) {
    if (control_index >= controls_.size()) {
      return;
    }
    ControlWidget &binding = controls_[control_index];
    if (!binding.continuous_number_merge_key.empty()) {
      return;
    }
    // Finish a preceding arrow, wheel, or keyboard step before a new
    // continuous gesture establishes its own merge identity.
    flush_number_dispatch(control_index);
    binding.continuous_number_merge_key =
        next_tool_setting_edit_merge_key(binding.id);
  }

  void finish_number_continuous_edit(std::size_t control_index) {
    if (control_index >= controls_.size()) {
      return;
    }
    flush_number_dispatch(control_index);
    controls_[control_index].continuous_number_merge_key.clear();
  }

  void begin_vector3_continuous_edit(std::size_t control_index,
                                     std::size_t component_index) {
    if (control_index >= controls_.size() || component_index >= 3U) {
      return;
    }
    Vector3ComponentWidget &component =
        controls_[control_index].vector3_components[component_index];
    if (!component.continuous_merge_key.empty()) {
      return;
    }
    flush_vector3_dispatch(control_index, component_index);
    component.continuous_merge_key =
        next_tool_setting_edit_merge_key(component.id);
  }

  void finish_vector3_continuous_edit(std::size_t control_index,
                                      std::size_t component_index) {
    if (control_index >= controls_.size() || component_index >= 3U) {
      return;
    }
    flush_vector3_dispatch(control_index, component_index);
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
        flush_number_dispatch(control_index);
      }
    }
    if (!binding.pending_number_value.has_value()) {
      if (editor_ == nullptr || !last_target_.has_value() ||
          !last_target_->active) {
        return;
      }
      binding.pending_number_lifecycle = NumberDispatchLifecycle{
          .binding_generation = binding_generation_,
          .settings_revision = editor_->tools().active_settings_revision(),
          .active_tool_revision = editor_->tools().active_tool_revision(),
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
          (binding.number_drag_spinner != nullptr &&
           binding.number_drag_spinner->is_dragging()) ||
          (binding.number_slider != nullptr &&
           binding.number_slider->isSliderDown());
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
          !last_target_->active) {
        return;
      }
      component.pending_lifecycle = NumberDispatchLifecycle{
          .binding_generation = binding_generation_,
          .settings_revision = editor_->tools().active_settings_revision(),
          .active_tool_revision = editor_->tools().active_tool_revision(),
          .tool_id = last_target_->tool_id,
          .merge_key = component.continuous_merge_key,
          .preview_active = last_target_->preview_active,
      };
    }
    component.pending_value = value;
    if (component.dispatch_timer != nullptr) {
      component.dispatch_timer->start();
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
    if (binding_generation_ != lifecycle.binding_generation ||
        editor_->tools().active_settings_revision() !=
            lifecycle.settings_revision ||
        editor_->tools().active_tool_revision() !=
            lifecycle.active_tool_revision) {
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
    if (binding_generation_ != lifecycle.binding_generation ||
        editor_->tools().active_settings_revision() !=
            lifecycle.settings_revision ||
        editor_->tools().active_tool_revision() !=
            lifecycle.active_tool_revision ||
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
        editor_->tools().active_settings_revision();
    const std::uint64_t active_tool_revision =
        editor_->tools().active_tool_revision();
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
    const std::uint64_t tool_revision = editor_->tools().active_tool_revision();
    const auto dispatch = binding.number_dispatch;
    dispatch(
        value, {.merge_key = std::move(merge_key)});
    if (editor_ == nullptr || generation != binding_generation_ ||
        editor_->tools().active_tool_revision() != tool_revision) {
      number_dispatch_failed_ = true;
      return;
    }
    // A successful owner write advances the settings revision. Other values
    // queued in the same UI lifecycle remain valid and must still flush.
    rebind_pending_number_dispatches_to_current_revision();
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
    component.dispatch(value, {.merge_key = std::move(merge_key)});
    rebind_pending_number_dispatches_to_current_revision();
  }

  void flush_pending_number_dispatches() {
    const std::uint64_t generation = binding_generation_;
    for (std::size_t index = 0; index < controls_.size(); ++index) {
      flush_number_dispatch(index);
      if (generation != binding_generation_) {
        return;
      }
      for (std::size_t component = 0; component < 3U; ++component) {
        flush_vector3_dispatch(index, component);
      }
    }
  }

  void discard_pending_number_dispatch(std::size_t control_index) {
    if (control_index >= controls_.size()) {
      return;
    }
    ControlWidget &binding = controls_[control_index];
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
    panel_.setWindowTitle(localized_text(
        localization_, "ui.panel.tool_settings", "Tool Settings"));
    description_section_->set_title(localized_text(
        localization_, "tool_settings.section.description", "Description"));
    settings_section_->set_title(localized_text(
        localization_, "tool_settings.section.settings", "Settings"));
    reset_button_->setText(localized_text(
        localization_, "tool_settings.reset_defaults",
        "Reset to Tool Defaults"));
    apply_button_->setText(
        localized_text(localization_, "ui.action.apply", "Apply"));
    cancel_button_->setText(
        localized_text(localization_, "ui.action.cancel", "Cancel"));
    if (last_target_.has_value()) {
      target_title_->setText(
          localized_target_title(localization_, *last_target_));
      description_label_->setText(
          localized_target_description(localization_, *last_target_));
      if (empty_settings_label_ != nullptr) {
        empty_settings_label_->setText(localized_text(
            localization_, "tool_settings.no_settings",
            "This tool has no adjustable settings."));
      }
    } else if (inactive_presented_) {
      target_title_->setText(localized_text(
          localization_, "tool_settings.no_active.title", "No Active Tool"));
      description_label_->setText(localized_text(
          localization_, "tool_settings.no_active.description",
          "Activate a tool to view its description and settings."));
      if (empty_settings_label_ != nullptr) {
        empty_settings_label_->setText(localized_text(
            localization_, "tool_settings.no_active.settings",
            "No tool settings are available."));
      }
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
      };

      auto *layout = new QVBoxLayout(section->content());
      set_scalable_margins(*layout, QMargins(8, 6, 8, 6));
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
    set_tool_sections_visible(true);
    synchronize_target_heading_visibility();
    target_title_->setText(localized_target_title(localization_, target));
    description_label_->setText(
        localized_target_description(localization_, target));

    build_control_groups(target);
    for (std::size_t index = 0; index < target.controls.size(); ++index) {
      const QuaderToolSettingControl &control = target.controls[index];
      ControlGroupWidget *group = find_control_group(control.group_id);
      add_control(target, control, index,
                  group != nullptr ? group->section->content()
                                   : controls_container_,
                  group != nullptr ? *group->layout : *controls_layout_);
    }
    if (target.controls.empty()) {
      add_empty_settings_message(localized_text(
          localization_, "tool_settings.no_settings",
          "This tool has no adjustable settings."));
    }
    controls_layout_->addStretch(1);
    synchronize_values(target);
    apply_current_ui_scale();
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

    switch (control.kind) {
    case QuaderToolSettingControlKind::Toggle: {
      auto *toggle = new QCheckBox(label, control_parent);
      toggle->setObjectName(object_name);
      toggle->setAccessibleName(label);
      QObject::connect(toggle, &QCheckBox::toggled, &panel_,
                       [this, tool_id = target.tool_id,
                        control_id = control.id](bool checked) {
                         if (editor_ == nullptr || synchronizing_) {
                           return;
                         }
                         const QuaderApiVoidResult result =
                             editor_->tools().set_setting(tool_id, control_id,
                                                          checked);
                         present_failure(editor_, "Tool settings update", result);
                         if (!result.ok()) {
                           invalidate_control_value(control_id);
                           schedule_refresh();
                         }
                       });
      control_layout.addWidget(toggle);
      binding.container = toggle;
      binding.label_button = toggle;
      binding.toggle = toggle;
      break;
    }
    case QuaderToolSettingControlKind::Number: {
      auto *row = new QWidget(control_parent);
      row->setObjectName(object_name + QStringLiteral(".row"));
      auto *row_layout = new QGridLayout(row);
      row_layout->setColumnStretch(0, 1);
      auto *field_label = new QLabel(label, row);
      field_label->setObjectName(object_name + QStringLiteral(".label"));
      field_label->setWordWrap(true);
      binding.field_label = field_label;
      row_layout->addWidget(field_label, 0, 0, 1, 2);

      const bool integer_control = uses_integer_spin_box(control);
      const QtToolSettingsNumberSliderMapping slider_mapping =
          qt_tool_settings_number_slider_mapping(
              control.min_value, control.max_value, integer_control,
              control.step);
      const auto send_number_to_editor =
          [this, tool_id = target.tool_id,
           control_id = control.id](
              double value, QuaderToolSettingEditOptions options) {
            if (editor_ == nullptr) {
              return;
            }
            const QuaderApiVoidResult result =
                editor_->tools().set_setting(tool_id, control_id, value,
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
      binding.number_dispatch_timer = dispatch_timer;
      binding.number_dispatch = send_number_to_editor;
      QObject::connect(dispatch_timer, &QTimer::timeout, &panel_,
                       [this, control_index]() {
                         flush_number_dispatch(control_index);
                       });

      if (control.number_presentation ==
          QuaderToolSettingNumberPresentation::SpinBoxOnly) {
        auto *spin = new QtMixedNumberSpinBox(0.0, row);
        spin->setObjectName(object_name);
        spin->setAccessibleName(label);
        spin->setRange(control.min_value, control.max_value);
        spin->setSingleStep(control.step);
        spin->present(control.number_value);
        spin->edited = [this, control_index,
                        positive_only = control.number_positive_only](
                           std::optional<double> value) {
          if (editor_ == nullptr || synchronizing_ ||
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
                             flush_number_dispatch(control_index);
                           }
                         });
        row_layout->addWidget(spin, 1, 0, 1, 2);
        field_label->setBuddy(spin);
        binding.floating = spin;
        binding.spinbox_only = spin;
        control_layout.addWidget(row);
        binding.container = row;
        break;
      }

      const bool use_drag_spinner =
          integer_control &&
          control.number_presentation ==
              QuaderToolSettingNumberPresentation::DragSpinner;
      if (use_drag_spinner) {
        auto *spinner = new QtToolSettingsDragSpinner(row);
        spinner->setObjectName(object_name + QStringLiteral(".spinner"));
        spinner->spin_box()->setObjectName(object_name);
        spinner->set_accessible_label(label);
        spinner->set_range(slider_mapping.slider_minimum,
                           slider_mapping.slider_maximum);
        spinner->set_single_step(integer_single_step(control.step));
        spinner->spin_box()->setKeyboardTracking(false);
        spinner->set_value(qt_tool_settings_number_slider_position(
            slider_mapping, control.number_value));
        spinner->value_changed = [this, control_index](int value) {
          if (editor_ == nullptr || synchronizing_) {
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
        row_layout->addWidget(spinner, 1, 0, 1, 2, Qt::AlignLeft);
        field_label->setBuddy(spinner->spin_box());
        binding.integer = spinner->spin_box();
        binding.number_drag_spinner = spinner;
        binding.number_slider_mapping = slider_mapping;
        control_layout.addWidget(row);
        binding.container = row;
        break;
      }

      auto *slider =
          new QtToolSettingsNumberSlider(Qt::Horizontal, row);
      slider->setObjectName(object_name + QStringLiteral(".slider"));
      slider->setAccessibleName(label);
      slider->setRange(slider_mapping.slider_minimum,
                       slider_mapping.slider_maximum);
      slider->set_stepwise_interaction(
          control.number_interaction ==
          QuaderToolSettingNumberInteraction::Stepped);
      slider->setTracking(true);
      row_layout->addWidget(slider, 1, 0);
      binding.number_slider = slider;
      binding.number_slider_mapping = slider_mapping;
      QObject::connect(slider, &QSlider::sliderPressed, &panel_,
                       [this, control_index]() {
                         begin_number_continuous_edit(control_index);
                       });
      QObject::connect(slider, &QSlider::sliderReleased, &panel_,
                       [this, control_index]() {
                         finish_number_continuous_edit(control_index);
                       });

      if (integer_control) {
        auto *spin = new QSpinBox(row);
        spin->setObjectName(object_name);
        spin->setAccessibleName(label);
        spin->setRange(slider_mapping.slider_minimum,
                       slider_mapping.slider_maximum);
        spin->setSingleStep(integer_single_step(control.step));
        spin->setKeyboardTracking(false);
        slider->setSingleStep(spin->singleStep());
        QObject::connect(
            slider, &QSlider::valueChanged, &panel_,
            [this, spin, slider, control_index](int position) {
              if (editor_ == nullptr || synchronizing_) {
                return;
              }
              const QtToolSettingsNumberSliderMapping &mapping =
                  controls_[control_index].number_slider_mapping;
              const int value = static_cast<int>(std::llround(
                  qt_tool_settings_number_slider_value(mapping, position)));
              {
                const QSignalBlocker spin_blocker(spin);
                spin->setValue(value);
                            }
                            update_slider_steps(*slider, mapping, value,
                                                spin->singleStep());
                            queue_number_dispatch(control_index,
                                                  static_cast<double>(value));
            });
        QObject::connect(spin, &QSpinBox::valueChanged, &panel_,
                         [this, spin, slider, control_index](int value) {
                           if (editor_ == nullptr || synchronizing_) {
                             return;
                           }
                           const QtToolSettingsNumberSliderMapping &mapping =
                               controls_[control_index]
                                   .number_slider_mapping;
                           {
                             const QSignalBlocker slider_blocker(slider);
                             slider->setValue(
                                 qt_tool_settings_number_slider_position(
                                     mapping,
                                     static_cast<double>(value)));
                           }
                           update_slider_steps(*slider, mapping, value,
                                               spin->singleStep());
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
        row_layout->addWidget(spin, 1, 1);
        field_label->setBuddy(spin);
        binding.integer = spin;
      } else {
        auto *spin = new QtDoubleSpinBox(row);
        spin->setObjectName(object_name);
        spin->setAccessibleName(label);
        const double minimum = std::min(control.min_value, control.max_value);
        const double maximum = std::max(control.min_value, control.max_value);
        spin->setRange(minimum, maximum);
        spin->setSingleStep(std::isfinite(control.step) && control.step > 0.0
                                ? control.step
                                : 0.001);
        spin->setKeyboardTracking(false);
        // The legacy ImGui slider displayed four decimal places. Preserve at
        // least that precision so descriptor endpoints such as 0.0625 remain
        // representable by the paired stock Fusion spin box.
        spin->setDecimals(
            std::max(4, decimal_places_for_step(control.step)));
        QObject::connect(
            slider, &QSlider::valueChanged, &panel_,
            [this, spin, slider, control_index](int position) {
              if (editor_ == nullptr || synchronizing_) {
                return;
              }
              const QtToolSettingsNumberSliderMapping &mapping =
                  controls_[control_index].number_slider_mapping;
              const double value = qt_tool_settings_number_slider_value(
                  mapping, position);
              {
                const QSignalBlocker spin_blocker(spin);
                spin->setValue(value);
              }
              update_slider_steps(*slider, mapping, spin->value(),
                                  spin->singleStep());
              queue_number_dispatch(control_index, spin->value());
            });
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, &panel_,
                         [this, spin, slider, control_index](double value) {
                           if (editor_ == nullptr || synchronizing_) {
                             return;
                           }
                           const QtToolSettingsNumberSliderMapping &mapping =
                               controls_[control_index]
                                   .number_slider_mapping;
                           {
                             const QSignalBlocker slider_blocker(slider);
                             slider->setValue(
                                 qt_tool_settings_number_slider_position(
                                     mapping, value));
                            }
                            update_slider_steps(*slider, mapping, value,
                                                spin->singleStep());
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
        row_layout->addWidget(spin, 1, 1);
        field_label->setBuddy(spin);
        binding.floating = spin;
      }
      control_layout.addWidget(row);
      binding.container = row;
      break;
    }
    case QuaderToolSettingControlKind::Vector3Number: {
      auto *row = new QWidget(control_parent);
      row->setObjectName(object_name + QStringLiteral(".row"));
      auto *row_layout = new QVBoxLayout(row);
      set_scalable_margins(*row_layout, QMargins(0, 0, 0, 0));
      set_scalable_spacing(*row_layout, 3);

      auto *field_label = new QLabel(label, row);
      field_label->setObjectName(object_name + QStringLiteral(".label"));
      field_label->setWordWrap(true);
      binding.field_label = field_label;
      row_layout->addWidget(field_label);

      auto *fields = new QWidget(row);
      fields->setObjectName(object_name + QStringLiteral(".fields"));
      fields->setMinimumWidth(0);
      fields->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
      auto *fields_layout = new QGridLayout(fields);
      set_scalable_margins(*fields_layout, QMargins(0, 0, 0, 0));
      set_scalable_grid_horizontal_spacing(*fields_layout, 4);
      constexpr std::array<const char *, 3> kAxisNames{{"X", "Y", "Z"}};
      constexpr std::array<quader_editor_core::ViewAxis, 3> kAxes{{
          quader_editor_core::ViewAxis::X,
          quader_editor_core::ViewAxis::Y,
          quader_editor_core::ViewAxis::Z,
      }};

      for (std::size_t axis = 0; axis < 3U; ++axis) {
        const QuaderToolSettingNumberComponent &component =
            control.vector3_components[axis];
        const int label_column = static_cast<int>(axis * 2U);
        const int field_column = label_column + 1;
        const QString component_name = control_object_name(
            component.id, control_index * 3U + axis);
        auto *axis_label =
            new QLabel(QString::fromLatin1(kAxisNames[axis]), fields);
        axis_label->setObjectName(component_name +
                                  QStringLiteral(".axis_label"));
        axis_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        apply_qt_semantic_axis_label_color(*axis_label, kAxes[axis]);
        fields_layout->addWidget(axis_label, 0, label_column);

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
        spin->setMinimumWidth(0);
        spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        spin->setValue(component.value);
        spin->setEnabled(component.enabled);
        fields_layout->setColumnStretch(field_column, 1);
        fields_layout->addWidget(spin, 0, field_column);
        axis_label->setBuddy(spin);

        auto *dispatch_timer = new QTimer(spin);
        dispatch_timer->setSingleShot(true);
        dispatch_timer->setInterval(kNumberDispatchIntervalMs);
        Vector3ComponentWidget &component_binding =
            binding.vector3_components[axis];
        component_binding.id = component.id;
        component_binding.spin = spin;
        component_binding.dispatch_timer = dispatch_timer;
        component_binding.enabled = component.enabled;
        component_binding.dispatch =
            [this, tool_id = target.tool_id,
             component_id = component.id](
                double value, QuaderToolSettingEditOptions options) {
              if (editor_ == nullptr) {
                return;
              }
              const QuaderApiVoidResult result = editor_->tools().set_setting(
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
                           if (editor_ == nullptr || synchronizing_) {
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
      row_layout->addWidget(fields);
      control_layout.addWidget(row);
      binding.container = row;
      break;
    }
    case QuaderToolSettingControlKind::Choice: {
      if (control.choice_presentation ==
          QuaderToolSettingChoicePresentation::Buttons) {
        auto *row = new QWidget(control_parent);
        row->setObjectName(object_name);
        auto *row_layout = new QVBoxLayout(row);
        set_scalable_margins(*row_layout, QMargins(0, 0, 0, 0));
        set_scalable_spacing(*row_layout, 4);
        auto *field_label = new QLabel(label, row);
        field_label->setObjectName(object_name + QStringLiteral(".label"));
        binding.field_label = field_label;
        row_layout->addWidget(field_label);
        auto *buttons_layout = new QHBoxLayout();
        set_scalable_margins(*buttons_layout, QMargins(0, 0, 0, 0));
        set_scalable_spacing(*buttons_layout, 4);
        row_layout->addLayout(buttons_layout);
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
          auto *button = new QPushButton(option_label, row);
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
                if (!checked || editor_ == nullptr || synchronizing_) {
                  return;
                }
                flush_pending_number_dispatches();
                const QuaderApiVoidResult result = editor_->tools().set_setting(
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
        control_layout.addWidget(row);
        binding.container = row;
        break;
      }
      if (control.choice_presentation ==
          QuaderToolSettingChoicePresentation::Dropdown) {
        auto *row = new QWidget(control_parent);
        row->setObjectName(object_name + QStringLiteral(".row"));
        auto *row_layout = new QHBoxLayout(row);
        auto *field_label = new QLabel(label, row);
        field_label->setObjectName(object_name + QStringLiteral(".label"));
        binding.field_label = field_label;
        row_layout->addWidget(field_label);

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
        row_layout->addWidget(dropdown, 1);
        QObject::connect(
            dropdown, &QComboBox::currentIndexChanged, &panel_,
            [this, dropdown, tool_id = target.tool_id,
             control_id = control.id](int index) {
              if (editor_ == nullptr || synchronizing_ || index < 0) {
                return;
              }
              const QByteArray option_id =
                  dropdown->itemData(index).toString().toUtf8();
              const QuaderApiVoidResult result = editor_->tools().set_setting(
                  tool_id, control_id,
                  std::string_view(option_id.constData(),
                                   static_cast<std::size_t>(option_id.size())));
              present_failure(editor_, "Tool settings update", result);
              // The chosen primitive changes the descriptor itself. Refresh
              // after signal delivery so the old control tree can be safely
              // replaced with the fields for the newly selected kind.
              schedule_refresh();
            });
        control_layout.addWidget(row);
        binding.container = row;
        binding.dropdown = dropdown;
        break;
      }

      auto *group = new QGroupBox(label, control_parent);
      group->setObjectName(object_name);
      auto *choice_layout = new QVBoxLayout(group);
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
                           if (!checked || editor_ == nullptr ||
                               synchronizing_) {
                             return;
                           }
                           const QuaderApiVoidResult result =
                               editor_->tools().set_setting(tool_id, control_id,
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
      control_layout.addWidget(group);
      binding.container = group;
      binding.label_group = group;
      break;
    }
    case QuaderToolSettingControlKind::Action: {
      auto *button = new QPushButton(label, control_parent);
      button->setObjectName(object_name);
      button->setAccessibleName(label);
      QObject::connect(button, &QPushButton::clicked, &panel_,
                       [this, tool_id = target.tool_id,
                        action_id = control.id]() {
                         if (editor_ == nullptr) {
                           discard_pending_number_dispatches();
                           return;
                         }
                         // Actions consume the presented settings. Commit any
                         // coalesced numeric edit first so the owner observes
                         // the latest value rather than the last timer tick.
                         flush_pending_number_dispatches();
                         const QuaderApiVoidResult result =
                             editor_->tools().execute_action(tool_id,
                                                             action_id);
                         present_failure(editor_, "Tool settings action", result);
                         schedule_refresh();
                       });
      control_layout.addWidget(button);
      binding.container = button;
      binding.label_button = button;
      break;
    }
    }

    if (binding.container != nullptr && !control.tooltip.empty()) {
      const QString tooltip = localized_descriptor_label(
          localization_, control.id + ".tooltip", control.tooltip);
      binding.container->setToolTip(tooltip);
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
    }
    if (binding.label_button != nullptr &&
        binding.label_button->text() != label) {
      binding.label_button->setText(label);
    }
    if (binding.label_group != nullptr &&
        binding.label_group->title() != label) {
      binding.label_group->setTitle(label);
    }
    const auto update_accessible_name = [](QWidget *widget,
                                          const QString &name) {
      if (widget != nullptr && widget->accessibleName() != name) {
        widget->setAccessibleName(name);
      }
    };
    update_accessible_name(binding.label_button, label);
    update_accessible_name(binding.integer, label);
    update_accessible_name(binding.floating, label);
    update_accessible_name(binding.number_slider, label);
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
    if (target_title_->text() != target_title) {
      target_title_->setText(target_title);
    }
    const QString description =
        localized_target_description(localization_, target);
    if (description_label_->text() != description) {
      description_label_->setText(description);
    }
    if (reset_button_->isEnabled() != target.can_reset) {
      reset_button_->setEnabled(target.can_reset);
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
          widget_or_descendant_has_focus(binding.integer);
      const bool floating_editing =
          widget_or_descendant_has_focus(binding.floating);
      const bool slider_editing =
          binding.number_slider != nullptr &&
          binding.number_slider->isSliderDown();
      const bool drag_spinner_editing =
          binding.number_drag_spinner != nullptr &&
          (binding.number_drag_spinner->is_dragging() ||
           binding.number_drag_spinner->is_editing());
      const bool number_dispatch_pending =
          binding.pending_number_value.has_value();
      const bool number_interaction_active =
          slider_editing || drag_spinner_editing || integer_editing ||
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
            widget_or_descendant_has_focus(component_binding.spin) ||
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

      if (binding.number_slider != nullptr) {
        const bool integer_control = binding.integer != nullptr;
        const QtToolSettingsNumberSliderMapping desired_mapping =
            qt_tool_settings_number_slider_mapping(
                control.min_value, control.max_value, integer_control,
                control.step);
        if (!number_interaction_active &&
            desired_mapping != binding.number_slider_mapping) {
          {
            const QSignalBlocker blocker(binding.number_slider);
            binding.number_slider->setRange(desired_mapping.slider_minimum,
                                             desired_mapping.slider_maximum);
          }
          if (binding.integer != nullptr) {
            const QSignalBlocker blocker(binding.integer);
            binding.integer->setRange(desired_mapping.slider_minimum,
                                      desired_mapping.slider_maximum);
          }
          if (binding.floating != nullptr) {
            const QSignalBlocker blocker(binding.floating);
            binding.floating->setRange(desired_mapping.value_minimum,
                                       desired_mapping.value_maximum);
          }
          binding.number_slider_mapping = desired_mapping;
        }

        if (!number_interaction_active) {
          if (binding.integer != nullptr) {
            binding.integer->setSingleStep(integer_single_step(control.step));
          }
          if (binding.floating != nullptr) {
            binding.floating->setSingleStep(
                std::isfinite(control.step) && control.step > 0.0
                    ? control.step
                    : 0.001);
            binding.floating->setDecimals(
                std::max(4, decimal_places_for_step(control.step)));
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
        binding.number_slider_mapping = desired_mapping;
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
          !slider_editing && !number_dispatch_pending) {
        const QSignalBlocker blocker(binding.integer);
        binding.integer->setValue(
            static_cast<int>(std::llround(control.number_value)));
      }
      if (binding.floating != nullptr && !floating_editing &&
          !slider_editing && !number_dispatch_pending) {
        const QSignalBlocker blocker(binding.floating);
        binding.floating->setValue(control.number_value);
      }
      if (binding.number_slider != nullptr &&
          !slider_editing && !integer_editing && !floating_editing &&
          !number_dispatch_pending) {
        const QSignalBlocker blocker(binding.number_slider);
        binding.number_slider->setValue(
            qt_tool_settings_number_slider_position(
                binding.number_slider_mapping, control.number_value));
      }
      if (binding.number_slider != nullptr) {
        const double current_number_value =
            number_interaction_active && binding.integer != nullptr
                ? static_cast<double>(binding.integer->value())
                : number_interaction_active && binding.floating != nullptr
                      ? binding.floating->value()
                      : control.number_value;
        const double logical_step =
            binding.integer != nullptr
                ? static_cast<double>(binding.integer->singleStep())
                : binding.floating != nullptr
                      ? binding.floating->singleStep()
                      : control.step;
        update_slider_steps(*binding.number_slider,
                            binding.number_slider_mapping,
                            current_number_value, logical_step);
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
        binding.presented_choice_value = control.choice_value;
      }
    }
    synchronizing_ = false;
  }

  QtToolSettingsPanel &panel_;
  QtUiScaleMetrics ui_scale_metrics_;
  const LocalizationService *localization_ = nullptr;
  QuaderEditorApi *editor_ = nullptr;
  std::uint64_t binding_generation_ = 0;
  bool number_dispatch_failed_ = false;
  QLabel *target_title_ = nullptr;
  QFrame *title_separator_ = nullptr;
  QScrollArea *scroll_area_ = nullptr;
  QtSettingsSection *description_section_ = nullptr;
  QLabel *description_label_ = nullptr;
  QtSettingsSection *settings_section_ = nullptr;
  QWidget *controls_container_ = nullptr;
  QVBoxLayout *controls_layout_ = nullptr;
  QLabel *empty_settings_label_ = nullptr;
  QWidget *actions_container_ = nullptr;
  QWidget *preview_actions_container_ = nullptr;
  QPushButton *reset_button_ = nullptr;
  QPushButton *apply_button_ = nullptr;
  QPushButton *cancel_button_ = nullptr;
  std::vector<ControlWidget> controls_;
  std::vector<ControlGroupWidget> control_groups_;
  std::unordered_map<std::string, bool> control_group_expansion_;
  std::optional<QuaderToolSettingsTarget> last_target_;
  bool synchronizing_ = false;
  bool refresh_scheduled_ = false;
  bool inactive_presented_ = false;
  bool localized_text_dirty_ = true;
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

bool QtToolSettingsPanel::numeric_interaction_active() const {
  return impl_->numeric_interaction_active();
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
  for (const QWidget *top_level : QApplication::topLevelWidgets()) {
    if (top_level == nullptr || top_level->windowHandle() != target_window) {
      continue;
    }
    if (dynamic_cast<const QtToolSettingsPanel *>(top_level) != nullptr) {
      return true;
    }
    const auto *dock = qobject_cast<const QDockWidget *>(top_level);
    return dock != nullptr &&
           dynamic_cast<const QtToolSettingsPanel *>(dock->widget()) !=
               nullptr;
  }
  return false;
}

bool qt_object_belongs_to_tool_settings_panel(
    const QObject *target, const QPoint &global_position) {
  const auto *target_window = qobject_cast<const QWindow *>(target);
  if (target_window == nullptr ||
      qobject_cast<QApplication *>(QCoreApplication::instance()) == nullptr) {
    return false;
  }
  for (const QWidget *top_level : QApplication::topLevelWidgets()) {
    if (top_level == nullptr || top_level->windowHandle() != target_window) {
      continue;
    }
    const auto descendants = top_level->findChildren<QWidget *>();
    if (std::any_of(
            descendants.begin(), descendants.end(),
            [&global_position](const QWidget *widget) {
              const auto *panel =
                  dynamic_cast<const QtToolSettingsPanel *>(widget);
              if (panel == nullptr) {
                return false;
              }
              const QPoint local = panel->mapFromGlobal(global_position);
              return panel->rect().contains(local) &&
                     panel->visibleRegion().contains(local);
            })) {
      return true;
    }
  }
  return false;
}

} // namespace quader
