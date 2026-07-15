////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_tool_settings_window.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "ui/qt_tool_settings_number_slider.hpp"

#include <QApplication>
#include <QByteArray>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

// Geometry previews can be substantially slower than raw pointer delivery.
// Keep visual controls immediate while limiting editor mutations to one batch
// per display-scale interval; release always flushes the latest value.
constexpr int kLiveNumberDispatchIntervalMs = 16;
constexpr std::string_view kPrimitiveTypeSettingId = "primitive.type";

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
         left.value == right.value;
}

bool same_control_structure(const QuaderToolSettingControl &left,
                            const QuaderToolSettingControl &right) {
  if (left.kind != right.kind || left.id != right.id ||
      left.label != right.label ||
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
  return true;
}

bool same_target_structure(const QuaderToolSettingsTarget &left,
                           const QuaderToolSettingsTarget &right) {
  if (left.active != right.active || left.tool_id != right.tool_id ||
      left.title != right.title ||
      left.controls.size() != right.controls.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.controls.size(); ++index) {
    if (!same_control_structure(left.controls[index], right.controls[index])) {
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

void update_slider_steps(QSlider &slider,
                         const QtToolSettingsNumberSliderMapping &mapping,
                         double value, double logical_step) {
  const int single_step = qt_tool_settings_number_slider_step(
      mapping, value, logical_step);
  slider.setSingleStep(single_step);
  slider.setPageStep(single_step <= std::numeric_limits<int>::max() / 10
                         ? single_step * 10
                         : std::numeric_limits<int>::max());
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

void log_failure(const char *action, const QuaderApiVoidResult &result) {
  if (result.ok()) {
    return;
  }
  LOG_ERROR("{} failed: {}", action,
            result.error_message().empty() ? "Command failed."
                                           : result.error_message());
}

void clear_layout(QLayout &layout) {
  while (QLayoutItem *item = layout.takeAt(0)) {
    if (QWidget *widget = item->widget()) {
      delete widget;
    }
    delete item;
  }
}

} // namespace

/// Owns retained controls for the active tool's descriptor-driven settings.
struct QtToolSettingsWindow::Impl {
  /// Associates one choice button with its stable option and numeric value.
  struct ChoiceWidget {
    QRadioButton *button = nullptr;
    std::string option_id;
    double value = 0.0;
  };

  /// Holds the widget variant used to edit one typed tool setting control.
  struct ControlWidget {
    QuaderToolSettingControlKind kind =
        QuaderToolSettingControlKind::Toggle;
    std::string id;
    QCheckBox *toggle = nullptr;
    QSpinBox *integer = nullptr;
    QDoubleSpinBox *floating = nullptr;
    QSlider *number_slider = nullptr;
    QComboBox *dropdown = nullptr;
    QtToolSettingsNumberSliderMapping number_slider_mapping;
    QTimer *number_dispatch_timer = nullptr;
    std::optional<double> pending_number_value;
    std::function<void(double)> number_dispatch;
    std::vector<ChoiceWidget> choices;
  };

  explicit Impl(QtToolSettingsWindow &window) : window_(window) {
    window_.setObjectName(QStringLiteral("floating.tool_settings"));
    window_.setAttribute(Qt::WA_DeleteOnClose, false);
    window_.resize(340, 220);

    auto *root_layout = new QVBoxLayout(&window_);

    target_title_ = new QLabel(&window_);
    target_title_->setObjectName(QStringLiteral("tool_settings.target_title"));
    root_layout->addWidget(target_title_);

    auto *title_separator = new QFrame(&window_);
    title_separator->setObjectName(
        QStringLiteral("tool_settings.title_separator"));
    title_separator->setFrameShape(QFrame::HLine);
    root_layout->addWidget(title_separator);

    scroll_area_ = new QScrollArea(&window_);
    scroll_area_->setObjectName(QStringLiteral("tool_settings.scroll"));
    scroll_area_->setWidgetResizable(true);
    controls_container_ = new QWidget(scroll_area_);
    controls_container_->setObjectName(
        QStringLiteral("tool_settings.controls"));
    controls_layout_ = new QVBoxLayout(controls_container_);
    scroll_area_->setWidget(controls_container_);
    root_layout->addWidget(scroll_area_, 1);

    actions_container_ = new QWidget(&window_);
    actions_container_->setObjectName(QStringLiteral("tool_settings.actions"));
    auto *actions_layout = new QHBoxLayout(actions_container_);
    actions_layout->addStretch(1);

    apply_button_ = new QPushButton(actions_container_);
    apply_button_->setObjectName(QStringLiteral("tool_settings.apply"));
    actions_layout->addWidget(apply_button_);

    cancel_button_ = new QPushButton(actions_container_);
    cancel_button_->setObjectName(QStringLiteral("tool_settings.cancel"));
    actions_layout->addWidget(cancel_button_);
    root_layout->addWidget(actions_container_);
    refresh_localized_text();

    QObject::connect(apply_button_, &QPushButton::clicked, &window_, [this]() {
      if (editor_ == nullptr) {
        return;
      }
      const QuaderApiVoidResult result =
          editor_->tools().commit_active_tool_preview();
      log_failure("Tool preview apply", result);
      schedule_refresh();
    });
    QObject::connect(cancel_button_, &QPushButton::clicked, &window_,
                     [this]() {
                       if (editor_ == nullptr) {
                         return;
                       }
                       const QuaderApiVoidResult result =
                           editor_->tools().cancel_active_tool_preview();
                       log_failure("Tool preview cancel", result);
                       schedule_refresh();
                     });

    actions_container_->hide();
    window_.hide();
  }

  void bind(QuaderEditorApi &editor) {
    editor_ = &editor;
    last_target_.reset();
    refresh();
  }

  void unbind() {
    editor_ = nullptr;
    last_target_.reset();
    clear_controls();
    window_.hide();
  }

  void set_localization(const LocalizationService &localization) {
    localization_ = &localization;
    // Descriptor controls retain their visible text, so force a rebuild when
    // the selected locale changes instead of updating only the window title.
    last_target_.reset();
    refresh_localized_text();
    refresh();
  }

  void refresh() {
    refresh_localized_text();
    if (editor_ == nullptr) {
      window_.hide();
      return;
    }

    const QuaderToolSettingsTarget target = editor_->tools().active_settings();
    if (!target.active) {
      if (last_target_.has_value()) {
        clear_controls();
        last_target_.reset();
      }
      window_.hide();
      return;
    }

    if (!last_target_.has_value() ||
        !same_target_structure(*last_target_, target)) {
      rebuild(target);
    } else {
      synchronize_values(target);
    }
    last_target_ = target;

    if (window_.isHidden()) {
      window_.show();
    }
  }

private:
  void schedule_refresh() {
    if (refresh_scheduled_) {
      return;
    }
    refresh_scheduled_ = true;
    QTimer::singleShot(0, &window_, [this]() {
      refresh_scheduled_ = false;
      refresh();
    });
  }

  void clear_controls() {
    controls_.clear();
    clear_layout(*controls_layout_);
    target_title_->clear();
    actions_container_->hide();
  }

  void queue_number_dispatch(std::size_t control_index, double value) {
    if (control_index >= controls_.size()) {
      return;
    }
    ControlWidget &binding = controls_[control_index];
    binding.pending_number_value = value;
    if (binding.number_dispatch_timer != nullptr &&
        !binding.number_dispatch_timer->isActive()) {
      binding.number_dispatch_timer->start();
    }
  }

  void flush_number_dispatch(std::size_t control_index) {
    if (control_index >= controls_.size()) {
      return;
    }
    ControlWidget &binding = controls_[control_index];
    if (!binding.pending_number_value.has_value() ||
        !binding.number_dispatch) {
      return;
    }
    const double value = *binding.pending_number_value;
    binding.pending_number_value.reset();
    if (binding.number_dispatch_timer != nullptr) {
      binding.number_dispatch_timer->stop();
    }
    binding.number_dispatch(value);
  }

  void dispatch_number(std::size_t control_index, double value,
                       bool coalesce) {
    if (control_index >= controls_.size()) {
      return;
    }
    ControlWidget &binding = controls_[control_index];
    if (coalesce) {
      queue_number_dispatch(control_index, value);
      return;
    }
    binding.pending_number_value.reset();
    if (binding.number_dispatch_timer != nullptr) {
      binding.number_dispatch_timer->stop();
    }
    if (binding.number_dispatch) {
      binding.number_dispatch(value);
    }
  }

  void refresh_localized_text() {
    window_.setWindowTitle(localized_text(
        localization_, "ui.panel.tool_settings", "Tool Settings"));
    apply_button_->setText(
        localized_text(localization_, "ui.action.apply", "Apply"));
    cancel_button_->setText(
        localized_text(localization_, "ui.action.cancel", "Cancel"));
    if (last_target_.has_value()) {
      target_title_->setText(
          localized_target_title(localization_, *last_target_));
    }
  }

  void rebuild(const QuaderToolSettingsTarget &target) {
    clear_controls();
    target_title_->setText(localized_target_title(localization_, target));

    for (std::size_t index = 0; index < target.controls.size(); ++index) {
      add_control(target, target.controls[index], index);
    }
    controls_layout_->addStretch(1);
    synchronize_values(target);
  }

  void add_control(const QuaderToolSettingsTarget &target,
                   const QuaderToolSettingControl &control,
                   std::size_t control_index) {
    ControlWidget binding{
        .kind = control.kind,
        .id = control.id,
    };
    const QString object_name =
        control_object_name(control.id, control_index);
    const QString label =
        localized_descriptor_label(localization_, control.id, control.label);

    switch (control.kind) {
    case QuaderToolSettingControlKind::Toggle: {
      auto *toggle = new QCheckBox(label, controls_container_);
      toggle->setObjectName(object_name);
      toggle->setAccessibleName(label);
      QObject::connect(toggle, &QCheckBox::toggled, &window_,
                       [this, tool_id = target.tool_id,
                        control_id = control.id](bool checked) {
                         if (editor_ == nullptr || synchronizing_) {
                           return;
                         }
                         const QuaderApiVoidResult result =
                             editor_->tools().set_setting(tool_id, control_id,
                                                          checked);
                         log_failure("Tool settings update", result);
                         if (!result.ok()) {
                           schedule_refresh();
                         }
                       });
      controls_layout_->addWidget(toggle);
      binding.toggle = toggle;
      break;
    }
    case QuaderToolSettingControlKind::Number: {
      auto *row = new QWidget(controls_container_);
      row->setObjectName(object_name + QStringLiteral(".row"));
      auto *row_layout = new QHBoxLayout(row);
      auto *field_label = new QLabel(label, row);
      field_label->setObjectName(object_name + QStringLiteral(".label"));
      row_layout->addWidget(field_label);

      const bool integer_control = uses_integer_spin_box(control);
      const QtToolSettingsNumberSliderMapping slider_mapping =
          qt_tool_settings_number_slider_mapping(
              control.min_value, control.max_value, integer_control,
              control.step);
      auto *slider =
          new QtToolSettingsNumberSlider(Qt::Horizontal, row);
      slider->setObjectName(object_name + QStringLiteral(".slider"));
      slider->setAccessibleName(label);
      slider->setRange(slider_mapping.slider_minimum,
                       slider_mapping.slider_maximum);
      slider->setTracking(true);
      row_layout->addWidget(slider, 1);
      binding.number_slider = slider;
      binding.number_slider_mapping = slider_mapping;

      const auto send_number_to_editor =
          [this, tool_id = target.tool_id,
           control_id = control.id](double value) {
            if (editor_ == nullptr) {
              return;
            }
            const QuaderApiVoidResult result =
                editor_->tools().set_setting(tool_id, control_id, value);
            log_failure("Tool settings update", result);
            if (!result.ok()) {
              schedule_refresh();
            }
          };
      auto *dispatch_timer = new QTimer(row);
      dispatch_timer->setSingleShot(true);
      dispatch_timer->setInterval(kLiveNumberDispatchIntervalMs);
      binding.number_dispatch_timer = dispatch_timer;
      binding.number_dispatch = send_number_to_editor;
      QObject::connect(dispatch_timer, &QTimer::timeout, &window_,
                       [this, control_index]() {
                         flush_number_dispatch(control_index);
                       });
      QObject::connect(slider, &QSlider::sliderReleased, &window_,
                       [this, control_index]() {
                         flush_number_dispatch(control_index);
                       });

      if (integer_control) {
        auto *spin = new QSpinBox(row);
        spin->setObjectName(object_name);
        spin->setAccessibleName(label);
        spin->setRange(slider_mapping.slider_minimum,
                       slider_mapping.slider_maximum);
        spin->setSingleStep(
            std::max(1, static_cast<int>(std::llround(control.step))));
        slider->setSingleStep(spin->singleStep());
        QObject::connect(
            slider, &QSlider::valueChanged, &window_,
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
              dispatch_number(control_index, static_cast<double>(value),
                              slider->isSliderDown());
            });
        QObject::connect(spin, &QSpinBox::valueChanged, &window_,
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
                           dispatch_number(control_index,
                                           static_cast<double>(value), false);
                         });
        row_layout->addWidget(spin);
        field_label->setBuddy(spin);
        binding.integer = spin;
      } else {
        auto *spin = new QDoubleSpinBox(row);
        spin->setObjectName(object_name);
        spin->setAccessibleName(label);
        const double minimum = std::min(control.min_value, control.max_value);
        const double maximum = std::max(control.min_value, control.max_value);
        spin->setRange(minimum, maximum);
        spin->setSingleStep(std::isfinite(control.step) && control.step > 0.0
                                ? control.step
                                : 0.001);
        // The legacy ImGui slider displayed four decimal places. Preserve at
        // least that precision so descriptor endpoints such as 0.0625 remain
        // representable by the paired stock Fusion spin box.
        spin->setDecimals(
            std::max(4, decimal_places_for_step(control.step)));
        QObject::connect(
            slider, &QSlider::valueChanged, &window_,
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
              dispatch_number(control_index, spin->value(),
                              slider->isSliderDown());
            });
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, &window_,
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
                           dispatch_number(control_index, value, false);
                         });
        row_layout->addWidget(spin);
        field_label->setBuddy(spin);
        binding.floating = spin;
      }
      controls_layout_->addWidget(row);
      break;
    }
    case QuaderToolSettingControlKind::Choice: {
      if (control.id == kPrimitiveTypeSettingId) {
        auto *row = new QWidget(controls_container_);
        row->setObjectName(object_name + QStringLiteral(".row"));
        auto *row_layout = new QHBoxLayout(row);
        auto *field_label = new QLabel(label, row);
        field_label->setObjectName(object_name + QStringLiteral(".label"));
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
        }
        field_label->setBuddy(dropdown);
        row_layout->addWidget(dropdown, 1);
        QObject::connect(
            dropdown, &QComboBox::currentIndexChanged, &window_,
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
              log_failure("Tool settings update", result);
              // The chosen primitive changes the descriptor itself. Refresh
              // after signal delivery so the old control tree can be safely
              // replaced with the fields for the newly selected kind.
              schedule_refresh();
            });
        controls_layout_->addWidget(row);
        binding.dropdown = dropdown;
        break;
      }

      auto *group = new QGroupBox(label, controls_container_);
      group->setObjectName(object_name);
      auto *choice_layout = new QVBoxLayout(group);
      auto *button_group = new QButtonGroup(group);
      button_group->setObjectName(object_name + QStringLiteral(".group"));
      button_group->setExclusive(true);
      for (std::size_t option_index = 0;
           option_index < control.choices.size(); ++option_index) {
        const QuaderToolSettingChoiceOption &option =
            control.choices[option_index];
        const QString option_label = localized_descriptor_label(
            localization_, option.id, option.label);
        auto *radio = new QRadioButton(option_label, group);
        radio->setObjectName(
            option_object_name(object_name, option.id, option_index));
        radio->setAccessibleName(option_label);
        button_group->addButton(radio);
        choice_layout->addWidget(radio);
        QObject::connect(radio, &QRadioButton::toggled, &window_,
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
                           log_failure("Tool settings update", result);
                           if (!result.ok()) {
                             schedule_refresh();
                           }
                         });
        binding.choices.push_back(
            ChoiceWidget{radio, option.id, option.value});
      }
      controls_layout_->addWidget(group);
      break;
    }
    case QuaderToolSettingControlKind::Action: {
      auto *button = new QPushButton(label, controls_container_);
      button->setObjectName(object_name);
      button->setAccessibleName(label);
      QObject::connect(button, &QPushButton::clicked, &window_,
                       [this, tool_id = target.tool_id,
                        action_id = control.id]() {
                         if (editor_ == nullptr) {
                           return;
                         }
                         const QuaderApiVoidResult result =
                             editor_->tools().execute_action(tool_id,
                                                             action_id);
                         log_failure("Tool settings action", result);
                         schedule_refresh();
                       });
      controls_layout_->addWidget(button);
      break;
    }
    }

    controls_.push_back(std::move(binding));
  }

  void synchronize_values(const QuaderToolSettingsTarget &target) {
    synchronizing_ = true;
    target_title_->setText(localized_target_title(localization_, target));
    apply_button_->setVisible(target.apply_visible);
    apply_button_->setEnabled(target.can_apply);
    cancel_button_->setVisible(target.can_cancel);
    cancel_button_->setEnabled(target.can_cancel);
    actions_container_->setVisible(target.apply_visible || target.can_cancel);

    const std::size_t count = std::min(controls_.size(), target.controls.size());
    for (std::size_t index = 0; index < count; ++index) {
      const QuaderToolSettingControl &control = target.controls[index];
      ControlWidget &binding = controls_[index];
      const bool integer_editing =
          widget_or_descendant_has_focus(binding.integer);
      const bool floating_editing =
          widget_or_descendant_has_focus(binding.floating);
      const bool slider_editing =
          binding.number_slider != nullptr &&
          binding.number_slider->isSliderDown();

      if (binding.number_slider != nullptr) {
        const bool interaction_active =
            slider_editing || integer_editing || floating_editing;
        const bool integer_control = binding.integer != nullptr;
        const QtToolSettingsNumberSliderMapping desired_mapping =
            qt_tool_settings_number_slider_mapping(
                control.min_value, control.max_value, integer_control,
                control.step);
        if (!interaction_active &&
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

        if (!interaction_active) {
          if (binding.integer != nullptr) {
            binding.integer->setSingleStep(std::max(
                1, static_cast<int>(std::llround(std::clamp(
                       control.step, 1.0,
                       static_cast<double>(
                           std::numeric_limits<int>::max()))))));
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

      if (binding.toggle != nullptr) {
        const QSignalBlocker blocker(binding.toggle);
        binding.toggle->setChecked(control.bool_value);
      }
      if (binding.integer != nullptr && !integer_editing && !slider_editing) {
        const QSignalBlocker blocker(binding.integer);
        binding.integer->setValue(
            static_cast<int>(std::llround(control.number_value)));
      }
      if (binding.floating != nullptr && !floating_editing &&
          !slider_editing) {
        const QSignalBlocker blocker(binding.floating);
        binding.floating->setValue(control.number_value);
      }
      if (binding.number_slider != nullptr &&
          !binding.number_slider->isSliderDown()) {
        const QSignalBlocker blocker(binding.number_slider);
        binding.number_slider->setValue(
            qt_tool_settings_number_slider_position(
                binding.number_slider_mapping, control.number_value));
      }
      if (binding.number_slider != nullptr) {
        const double current_number_value =
            slider_editing && binding.integer != nullptr
                ? static_cast<double>(binding.integer->value())
                : slider_editing && binding.floating != nullptr
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
      for (std::size_t option_index = 0;
           option_index < binding.choices.size() &&
           option_index < control.choices.size();
           ++option_index) {
        ChoiceWidget &choice = binding.choices[option_index];
        const QSignalBlocker blocker(choice.button);
        choice.button->setChecked(
            choice_matches(control.choice_value,
                           control.choices[option_index]));
      }
    }
    synchronizing_ = false;
  }

  QtToolSettingsWindow &window_;
  const LocalizationService *localization_ = nullptr;
  QuaderEditorApi *editor_ = nullptr;
  QLabel *target_title_ = nullptr;
  QScrollArea *scroll_area_ = nullptr;
  QWidget *controls_container_ = nullptr;
  QVBoxLayout *controls_layout_ = nullptr;
  QWidget *actions_container_ = nullptr;
  QPushButton *apply_button_ = nullptr;
  QPushButton *cancel_button_ = nullptr;
  std::vector<ControlWidget> controls_;
  std::optional<QuaderToolSettingsTarget> last_target_;
  bool synchronizing_ = false;
  bool refresh_scheduled_ = false;
};

QtToolSettingsWindow::QtToolSettingsWindow(QWidget *parent)
    : QWidget(parent, Qt::Tool),
      impl_(std::make_unique<Impl>(*this)) {}

QtToolSettingsWindow::~QtToolSettingsWindow() = default;

void QtToolSettingsWindow::bind(QuaderEditorApi &editor) {
  impl_->bind(editor);
}

void QtToolSettingsWindow::unbind() { impl_->unbind(); }

void QtToolSettingsWindow::set_localization(
    const LocalizationService &localization) {
  impl_->set_localization(localization);
}

void QtToolSettingsWindow::refresh() { impl_->refresh(); }

} // namespace quader
