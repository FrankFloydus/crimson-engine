////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_tool_settings_panel.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "ui/qt_tool_settings_number_slider.hpp"
#include "ui/ui_panel.hpp"

#include <QApplication>
#include <QByteArray>
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
#include <utility>
#include <vector>

namespace quader {
namespace {

// Geometry previews can be substantially slower than raw pointer delivery.
// Pointer drags publish the latest value at this bounded cadence; keyboard,
// wheel, and spin edits use it as a trailing coalescing window.
constexpr int kNumberDispatchIntervalMs = 50;
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

QString localized_target_description(
    const LocalizationService *localization,
    const QuaderToolSettingsTarget &target) {
  if (localization != nullptr && !target.tool_id.empty()) {
    std::string key = "tool_settings.description.";
    key.append(target.tool_id);
    return QString::fromStdString(
        localization->text(key, target.description));
  }
  return qtext(target.description);
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
      left.title != right.title || left.description != right.description ||
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

/// Keeps one disclosure section expanded/collapsed without rebuilding it.
class QtToolSettingsSection final : public QWidget {
public:
  explicit QtToolSettingsSection(QString stable_name,
                                 QWidget *parent = nullptr)
      : QWidget(parent) {
    setObjectName(stable_name);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    header_ = new QToolButton(this);
    header_->setObjectName(stable_name + QStringLiteral(".header"));
    header_->setAutoRaise(true);
    header_->setCheckable(true);
    header_->setChecked(true);
    header_->setArrowType(Qt::DownArrow);
    header_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(header_);

    content_ = new QFrame(this);
    content_->setObjectName(stable_name + QStringLiteral(".content"));
    content_->setFrameShape(QFrame::StyledPanel);
    content_->setFrameShadow(QFrame::Plain);
    layout->addWidget(content_);

    QObject::connect(header_, &QToolButton::toggled, this,
                     [this](bool expanded) {
                       header_->setArrowType(expanded ? Qt::DownArrow
                                                       : Qt::RightArrow);
                       content_->setVisible(expanded);
                     });
  }

  void set_title(const QString &title) {
    if (header_->text() != title) {
      header_->setText(title);
    }
    if (header_->accessibleName() != title) {
      header_->setAccessibleName(title);
    }
  }

  [[nodiscard]] QWidget *content() const { return content_; }

private:
  QToolButton *header_ = nullptr;
  QFrame *content_ = nullptr;
};

/// A mixed value is presentation state, never a sentinel texture coordinate.
class QtTextureNumberSpinBox final : public QDoubleSpinBox {
public:
  explicit QtTextureNumberSpinBox(double step_origin,
                                  QWidget *parent = nullptr)
      : QDoubleSpinBox(parent), step_origin_(step_origin) {
    setKeyboardTracking(false);
    setDecimals(6);
    setRange(-std::numeric_limits<float>::max(),
             std::numeric_limits<float>::max());
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setMinimumWidth(88);
    QObject::connect(this, &QDoubleSpinBox::valueChanged, this,
                     [this](double value) {
                       const bool was_mixed = mixed_;
                       mixed_ = false;
                       if (was_mixed) {
                         lineEdit()->setText(
                             QDoubleSpinBox::textFromValue(value));
                       }
                       if (edited) {
                         edited(value);
                       }
                     });
    QObject::connect(lineEdit(), &QLineEdit::textEdited, this,
                     [this](const QString &text) {
                       QString candidate = text;
                       int position = lineEdit()->cursorPosition();
                       if (QDoubleSpinBox::validate(candidate, position) ==
                               QValidator::Acceptable &&
                           text != QStringLiteral("--")) {
                         mixed_ = false;
                         if (edited) {
                           edited(QDoubleSpinBox::valueFromText(text));
                         }
                       } else if (edited) {
                         edited(std::nullopt);
                       }
                     });
  }

  void present(std::optional<double> value) {
    const QSignalBlocker spin_blocker(this);
    const QSignalBlocker text_blocker(lineEdit());
    mixed_ = !value.has_value();
    if (value.has_value() && this->value() != *value) {
      setValue(*value);
    }
    const QString desired = mixed_ ? QStringLiteral("--")
                                   : QDoubleSpinBox::textFromValue(this->value());
    if (lineEdit()->text() != desired) {
      lineEdit()->setText(desired);
    }
  }

  std::function<void(std::optional<double>)> edited;

protected:
  QString textFromValue(double value) const override {
    return mixed_ ? QStringLiteral("--")
                  : QDoubleSpinBox::textFromValue(value);
  }

  double valueFromText(const QString &text) const override {
    return text == QStringLiteral("--")
               ? value()
               : QDoubleSpinBox::valueFromText(text);
  }

  QValidator::State validate(QString &text, int &position) const override {
    return text == QStringLiteral("--")
               ? QValidator::Intermediate
               : QDoubleSpinBox::validate(text, position);
  }

  void focusInEvent(QFocusEvent *event) override {
    QDoubleSpinBox::focusInEvent(event);
    if (mixed_) {
      selectAll();
    }
  }

  void stepBy(int steps) override {
    if (mixed_) {
      // Arrows on an indeterminate field start from its neutral value.
      mixed_ = false;
      const QSignalBlocker blocker(this);
      setValue(step_origin_);
    }
    QDoubleSpinBox::stepBy(steps);
  }

private:
  double step_origin_ = 0.0;
  bool mixed_ = false;
};

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
};

constexpr std::array<TextureControlDescription, 5> kTextureControls{{
    {QuaderFaceTextureField::ScaleX, "scale_x",
     "tool_settings.texture_scale_x", "Scale X", 0.1, 1.0},
    {QuaderFaceTextureField::ScaleY, "scale_y",
     "tool_settings.texture_scale_y", "Scale Y", 0.1, 1.0},
    {QuaderFaceTextureField::ShiftX, "shift_x",
     "tool_settings.texture_shift_x", "Shift X", 1.0, 0.0},
    {QuaderFaceTextureField::ShiftY, "shift_y",
     "tool_settings.texture_shift_y", "Shift Y", 1.0, 0.0},
    {QuaderFaceTextureField::Rotate, "rotate",
     "tool_settings.texture_rotate", "Rotate (\xC2\xB0)", 1.0, 0.0},
}};

/// Retained presentation only; each button dispatches the same public action
/// used by the corresponding command, without interpreting UVs in the panel.
struct TextureActionDescription {
  QuaderFaceTextureAction action;
  QuaderCommandId command;
  std::string_view id;
  std::string_view caption;
};

constexpr std::array<TextureActionDescription, 25> kTextureActions{{
    {QuaderFaceTextureAction::AlignToGrid,
     quader_command::kModelingTextureAlignToGrid,
     "align_to_grid", "Grid"},
    {QuaderFaceTextureAction::AlignToFace,
     quader_command::kModelingTextureAlignToFace,
     "align_to_face", "Face"},
    {QuaderFaceTextureAction::AlignToView,
     quader_command::kModelingTextureAlignToView,
     "align_to_view", "View"},
    {QuaderFaceTextureAction::ScaleXUp,
     quader_command::kModelingTextureScaleXUp,
     "scale_x_up", "X +"},
    {QuaderFaceTextureAction::ScaleXDown,
     quader_command::kModelingTextureScaleXDown,
     "scale_x_down", "X -"},
    {QuaderFaceTextureAction::ScaleYUp,
     quader_command::kModelingTextureScaleYUp,
     "scale_y_up", "Y +"},
    {QuaderFaceTextureAction::ScaleYDown,
     quader_command::kModelingTextureScaleYDown,
     "scale_y_down", "Y -"},
    {QuaderFaceTextureAction::ShiftLeft,
     quader_command::kModelingTextureShiftLeft,
     "shift_left", "Left"},
    {QuaderFaceTextureAction::ShiftRight,
     quader_command::kModelingTextureShiftRight,
     "shift_right", "Right"},
    {QuaderFaceTextureAction::ShiftUp,
     quader_command::kModelingTextureShiftUp,
     "shift_up", "Up"},
    {QuaderFaceTextureAction::ShiftDown,
     quader_command::kModelingTextureShiftDown,
     "shift_down", "Down"},
    {QuaderFaceTextureAction::RotateClockwise,
     quader_command::kModelingTextureRotateClockwise,
     "rotate_clockwise", "CW"},
    {QuaderFaceTextureAction::RotateCounterClockwise,
     quader_command::kModelingTextureRotateCounterClockwise,
     "rotate_counter_clockwise", "CCW"},
    {QuaderFaceTextureAction::FlipTextureHorizontal,
     quader_command::kModelingTextureFlipTextureHorizontal,
     "flip_texture_horizontal", "Flip Tex H"},
    {QuaderFaceTextureAction::FlipTextureVertical,
     quader_command::kModelingTextureFlipTextureVertical,
     "flip_texture_vertical", "Flip Tex V"},
    {QuaderFaceTextureAction::FlipFaceHorizontal,
     quader_command::kModelingTextureFlipFaceHorizontal,
     "flip_face_horizontal", "Flip Face H"},
    {QuaderFaceTextureAction::FlipFaceVertical,
     quader_command::kModelingTextureFlipFaceVertical,
     "flip_face_vertical", "Flip Face V"},
    {QuaderFaceTextureAction::FitBoth,
     quader_command::kModelingTextureFitBoth,
     "fit_both", "Both"},
    {QuaderFaceTextureAction::FitX,
     quader_command::kModelingTextureFitX,
     "fit_x", "X"},
    {QuaderFaceTextureAction::FitY,
     quader_command::kModelingTextureFitY,
     "fit_y", "Y"},
    {QuaderFaceTextureAction::JustifyLeft,
     quader_command::kModelingTextureJustifyLeft,
     "justify_left", "Left"},
    {QuaderFaceTextureAction::JustifyRight,
     quader_command::kModelingTextureJustifyRight,
     "justify_right", "Right"},
    {QuaderFaceTextureAction::JustifyTop,
     quader_command::kModelingTextureJustifyTop,
     "justify_top", "Top"},
    {QuaderFaceTextureAction::JustifyBottom,
     quader_command::kModelingTextureJustifyBottom,
     "justify_bottom", "Bottom"},
    {QuaderFaceTextureAction::JustifyCenter,
     quader_command::kModelingTextureJustifyCenter,
     "justify_center", "Center"},
}};

/// Groups consecutive texture actions into one compact retained button grid.
struct TextureActionGroup {
  std::string_view id;
  std::string_view label;
  std::size_t count;
  int columns;
};

constexpr std::array<TextureActionGroup, 6> kTextureActionGroups{{
    {"align", "Align", 3, 3},
    {"scale", "Scale", 4, 2},
    {"shift", "Shift", 4, 2},
    {"rotate", "Rotate", 6, 2},
    {"fit", "Fit", 3, 3},
    {"justify", "Justify", 5, 2},
}};

} // namespace

/// Owns retained controls for the active tool's descriptor-driven settings.
struct QtToolSettingsPanel::Impl {
  /// Binds a delayed numeric write to the owner lifecycle that queued it.
  struct NumberDispatchLifecycle {
    std::uint64_t settings_revision = 0;
    std::uint64_t active_tool_revision = 0;
    std::string tool_id;
    bool preview_active = false;
  };

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
    std::optional<NumberDispatchLifecycle> pending_number_lifecycle;
    std::function<void(double)> number_dispatch;
    std::vector<ChoiceWidget> choices;
  };

  /// Retained input and pending gesture state; the modeling API owns face data.
  struct TextureControlWidget {
    QuaderFaceTextureField field = QuaderFaceTextureField::ScaleX;
    QtTextureNumberSpinBox *spin = nullptr;
    QLabel *label = nullptr;
    QTimer *dispatch_timer = nullptr;
    std::optional<double> pending_value;
    std::optional<double> last_requested_value;
    std::optional<QuaderFaceTextureRevision> gesture_revision;
    std::uint64_t selection_token = 0;
    std::string merge_key;
    bool editing = false;
    bool invalid_value = false;
  };

  explicit Impl(QtToolSettingsPanel &panel) : panel_(panel) {
    panel_.setObjectName(QString::fromLatin1(
        ui_panel_stable_id(UIPanelId::ToolSettings)));
    panel_.setMinimumWidth(240);

    auto *root_layout = new QVBoxLayout(&panel_);
    root_layout->setObjectName(QStringLiteral("tool_settings.layout"));
    root_layout->setContentsMargins(8, 8, 8, 8);
    root_layout->setSpacing(8);

    scroll_area_ = new QScrollArea(&panel_);
    scroll_area_->setObjectName(QStringLiteral("tool_settings.scroll"));
    scroll_area_->setFrameShape(QFrame::NoFrame);
    scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_area_->setWidgetResizable(true);
    panel_.setFocusProxy(scroll_area_);

    auto *scroll_content = new QWidget(scroll_area_);
    scroll_content->setObjectName(QStringLiteral("tool_settings.content"));
    auto *content_layout = new QVBoxLayout(scroll_content);
    content_layout->setContentsMargins(0, 0, 0, 0);
    content_layout->setSpacing(8);

    target_title_ = new QLabel(scroll_content);
    target_title_->setObjectName(QStringLiteral("tool_settings.target_title"));
    target_title_->setWordWrap(true);
    QFont target_font = target_title_->font();
    target_font.setBold(true);
    target_title_->setFont(target_font);
    content_layout->addWidget(target_title_);

    auto *title_separator = new QFrame(scroll_content);
    title_separator->setObjectName(
        QStringLiteral("tool_settings.title_separator"));
    title_separator->setFrameShape(QFrame::HLine);
    content_layout->addWidget(title_separator);

    description_section_ = new QtToolSettingsSection(
        QStringLiteral("tool_settings.section.description"),
        scroll_content);
    auto *description_layout =
        new QVBoxLayout(description_section_->content());
    description_layout->setContentsMargins(8, 6, 8, 6);
    description_label_ =
        new QLabel(description_section_->content());
    description_label_->setObjectName(
        QStringLiteral("tool_settings.description"));
    description_label_->setWordWrap(true);
    description_label_->setTextInteractionFlags(
        Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    description_layout->addWidget(description_label_);
    content_layout->addWidget(description_section_);

    settings_section_ = new QtToolSettingsSection(
        QStringLiteral("tool_settings.section.settings"), scroll_content);
    controls_container_ = settings_section_->content();
    controls_container_->setObjectName(QStringLiteral(
        "tool_settings.section.settings.content"));
    controls_layout_ = new QVBoxLayout(controls_container_);
    controls_layout_->setContentsMargins(8, 6, 8, 6);
    controls_layout_->setSpacing(6);
    content_layout->addWidget(settings_section_);
    texture_section_ = new QtToolSettingsSection(
        QStringLiteral("tool_settings.section.texture"), scroll_content);
    build_texture_controls();
    texture_section_->hide();
    content_layout->addWidget(texture_section_);
    edit_texture_section_ = new QtToolSettingsSection(
        QStringLiteral("tool_settings.section.edit_texture"), scroll_content);
    build_texture_actions();
    edit_texture_section_->hide();
    content_layout->addWidget(edit_texture_section_);
    content_layout->addStretch(1);
    scroll_area_->setWidget(scroll_content);
    root_layout->addWidget(scroll_area_, 1);

    actions_container_ = new QWidget(&panel_);
    actions_container_->setObjectName(QStringLiteral("tool_settings.actions"));
    auto *actions_layout = new QVBoxLayout(actions_container_);
    actions_layout->setContentsMargins(0, 0, 0, 0);
    actions_layout->setSpacing(6);

    preview_actions_container_ = new QWidget(actions_container_);
    preview_actions_container_->setObjectName(
        QStringLiteral("tool_settings.preview_actions"));
    auto *preview_actions_layout =
        new QHBoxLayout(preview_actions_container_);
    preview_actions_layout->setContentsMargins(0, 0, 0, 0);
    preview_actions_layout->addStretch(1);

    apply_button_ = new QPushButton(preview_actions_container_);
    apply_button_->setObjectName(QStringLiteral("tool_settings.apply"));
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
      log_failure("Reset tool settings", result);
      schedule_refresh();
    });
    QObject::connect(apply_button_, &QPushButton::clicked, &panel_, [this]() {
      if (editor_ == nullptr) {
        discard_pending_number_dispatches();
        return;
      }
      flush_pending_number_dispatches();
      const QuaderApiVoidResult result =
          editor_->tools().commit_active_tool_preview();
      log_failure("Tool preview apply", result);
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
                       log_failure("Tool preview cancel", result);
                       schedule_refresh();
                     });
    QObject::connect(cancel_button_, &QPushButton::pressed, &panel_, [this]() {
      discard_pending_number_dispatches();
    });
    present_inactive_target();
  }

  void bind(QuaderEditorApi &editor) {
    discard_pending_number_dispatches();
    discard_texture_dispatches();
    editor_ = &editor;
    last_target_.reset();
    texture_revision_.reset();
    texture_settings_.reset();
    texture_action_error_.clear();
    refresh();
  }

  void unbind() {
    discard_pending_number_dispatches();
    discard_texture_dispatches();
    editor_ = nullptr;
    last_target_.reset();
    present_inactive_target();
    refresh_texture_settings();
  }

  void set_localization(const LocalizationService &localization) {
    localization_ = &localization;
    // Descriptor controls retain their visible text, so force a rebuild when
    // the selected locale changes instead of updating only the dock title.
    last_target_.reset();
    texture_revision_.reset();
    texture_action_error_.clear();
    localized_text_dirty_ = true;
    refresh_localized_text();
    refresh();
  }

  void refresh() {
    refresh_localized_text();
    refresh_texture_settings();
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
      texture_action_error_.clear();
      return;
    }

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
    // Initializing a new selection's rotation baseline can advance its context
    // revision. Pending edits belong to the context returned by this query.
    revision = editor_->modeling().face_texture_settings_revision();
    const bool selection_changed =
        !texture_settings_.has_value() ||
        texture_settings_->selection_token != settings.selection_token;
    if (selection_changed ||
        (texture_revision_.has_value() && *texture_revision_ != revision)) {
      texture_action_error_.clear();
    }

    texture_synchronizing_ = true;
    for (TextureControlWidget &binding : texture_controls_) {
      if (selection_changed ||
          (binding.gesture_revision.has_value() &&
           *binding.gesture_revision != revision)) {
        discard_texture_dispatch(binding);
      }
    }
    const bool visible = settings.selected_face_count > 0;
    if (texture_section_->isHidden() == visible) {
      texture_section_->setVisible(visible);
    }
    if (edit_texture_section_->isHidden() == visible) {
      edit_texture_section_->setVisible(visible);
    }

    QString disabled_reason;
    if (visible && !settings.editable) {
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
    present_texture_action_message(disabled_reason.isEmpty()
                                       ? texture_action_error_
                                       : disabled_reason);

    const bool single_face = settings.selected_face_count == 1;
    const std::array<std::optional<double>, 5> values{
        settings.scale_x, settings.scale_y,
        single_face ? std::optional<double>(settings.shift_x) : std::nullopt,
        single_face ? std::optional<double>(settings.shift_y) : std::nullopt,
        settings.rotation_degrees};
    for (std::size_t index = 0; index < texture_controls_.size(); ++index) {
      TextureControlWidget &binding = texture_controls_[index];
      const bool shift = binding.field == QuaderFaceTextureField::ShiftX ||
                         binding.field == QuaderFaceTextureField::ShiftY;
      const bool enabled = settings.editable && (!shift || single_face);
      if (binding.spin->isEnabled() != enabled) {
        binding.spin->setEnabled(enabled);
      }
      if (binding.label->isEnabled() != enabled) {
        binding.label->setEnabled(enabled);
      }
      if (!binding.editing) {
        binding.spin->present(values[index]);
      }
    }
    for (QPushButton *button : texture_action_buttons_) {
      if (button->isEnabled() != settings.editable) {
        button->setEnabled(settings.editable);
      }
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

  [[nodiscard]] bool has_active_target() const {
    return last_target_.has_value() && last_target_->active;
  }

  [[nodiscard]] bool numeric_interaction_active() const {
    if (std::any_of(texture_controls_.begin(), texture_controls_.end(),
                    [](const TextureControlWidget &binding) {
                      return binding.editing || binding.pending_value.has_value();
                    })) {
      return true;
    }
    return std::any_of(
        controls_.begin(), controls_.end(), [](const ControlWidget &binding) {
          return binding.pending_number_value.has_value() ||
                 (binding.number_slider != nullptr &&
                  binding.number_slider->isSliderDown());
        });
  }

private:
  void build_texture_actions() {
    auto *layout = new QVBoxLayout(edit_texture_section_->content());
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);
    std::size_t action_index = 0;
    for (std::size_t group_index = 0;
         group_index < kTextureActionGroups.size(); ++group_index) {
      const TextureActionGroup &group = kTextureActionGroups[group_index];
      auto *label = new QLabel(edit_texture_section_->content());
      label->setObjectName(QStringLiteral("tool_settings.edit_texture.group.%1")
                               .arg(qtext(group.id)));
      texture_action_group_labels_[group_index] = label;
      layout->addWidget(label);
      auto *buttons = new QGridLayout;
      buttons->setContentsMargins(0, 0, 0, 0);
      buttons->setHorizontalSpacing(4);
      buttons->setVerticalSpacing(4);
      for (std::size_t offset = 0; offset < group.count;
           ++offset, ++action_index) {
        const TextureActionDescription &description =
            kTextureActions[action_index];
        auto *button = new QPushButton(edit_texture_section_->content());
        button->setObjectName(QStringLiteral("tool_settings.edit_texture.%1")
                                  .arg(qtext(description.id)));
        button->setAutoDefault(false);
        // Validate the draft before mouse focus-out can replace invalid text
        // with the spin box's previous value. Keyboard traversal stays native.
        button->setFocusPolicy(Qt::TabFocus);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        button->setMinimumWidth(0);
        const int cell = static_cast<int>(offset);
        const int span = offset + 1 == group.count &&
                                 group.count % group.columns == 1
                             ? group.columns
                             : 1;
        buttons->addWidget(button, cell / group.columns, cell % group.columns,
                           1, span);
        texture_action_buttons_[action_index] = button;
        QObject::connect(button, &QPushButton::clicked, &panel_,
                         [this, action_index]() {
                           dispatch_texture_action(action_index);
                         });
      }
      for (int column = 0; column < group.columns; ++column) {
        buttons->setColumnStretch(column, 1);
      }
      layout->addLayout(buttons);
    }
    texture_treat_as_one_ = new QCheckBox(edit_texture_section_->content());
    texture_treat_as_one_->setObjectName(
        QStringLiteral("tool_settings.edit_texture.treat_as_one"));
    texture_treat_as_one_->setFocusPolicy(Qt::TabFocus);
    layout->addWidget(texture_treat_as_one_);
    QObject::connect(texture_treat_as_one_, &QCheckBox::toggled, &panel_,
                     [this](bool enabled) {
                       if (editor_ == nullptr || texture_synchronizing_) {
                         return;
                       }
                       const QuaderFaceTextureRevision before =
                           editor_->modeling().face_texture_settings_revision();
                       const QuaderApiVoidResult result =
                           editor_->modeling().set_face_texture_treat_as_one(
                               enabled);
                       log_failure("Treat selected faces as one", result);
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
    texture_action_message_ = new QLabel(edit_texture_section_->content());
    texture_action_message_->setObjectName(
        QStringLiteral("tool_settings.edit_texture.message"));
    texture_action_message_->setWordWrap(true);
    texture_action_message_->hide();
    layout->addWidget(texture_action_message_);
  }

  void present_texture_action_message(const QString &message) {
    if (texture_action_message_->text() != message) {
      texture_action_message_->setText(message);
    }
    if (texture_action_message_->isHidden() == !message.isEmpty()) {
      texture_action_message_->setVisible(!message.isEmpty());
    }
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
      if (button->toolTip() != tooltip) {
        button->setToolTip(tooltip);
      }
    }
  }

  void build_texture_controls() {
    auto *layout = new QGridLayout(texture_section_->content());
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(6);
    layout->setColumnStretch(1, 1);
    for (std::size_t index = 0; index < kTextureControls.size(); ++index) {
      const TextureControlDescription &description = kTextureControls[index];
      TextureControlWidget &binding = texture_controls_[index];
      binding.field = description.field;
      const QString name =
          QStringLiteral("tool_settings.texture.%1").arg(qtext(description.id));
      binding.label = new QLabel(texture_section_->content());
      binding.label->setObjectName(name + QStringLiteral(".label"));
      binding.label->setWordWrap(true);
      binding.spin = new QtTextureNumberSpinBox(description.step_origin,
                                               texture_section_->content());
      binding.spin->setObjectName(name);
      binding.spin->setSingleStep(description.step);
      binding.label->setBuddy(binding.spin);
      layout->addWidget(binding.label, static_cast<int>(index), 0);
      layout->addWidget(binding.spin, static_cast<int>(index), 1);
      binding.dispatch_timer = new QTimer(binding.spin);
      binding.dispatch_timer->setSingleShot(true);
      binding.dispatch_timer->setInterval(kNumberDispatchIntervalMs);
      QObject::connect(binding.dispatch_timer, &QTimer::timeout, &panel_,
                       [this, index]() { flush_texture_dispatch(index); });
      binding.spin->edited = [this, index](std::optional<double> value) {
        queue_texture_dispatch(index, value);
      };
      QObject::connect(binding.spin, &QDoubleSpinBox::editingFinished, &panel_,
                       [this, index]() {
                         if (texture_synchronizing_) {
                           return;
                         }
                         flush_texture_dispatch(index);
                         discard_texture_dispatch(texture_controls_[index]);
                         refresh_texture_settings(true);
                       });
    }
    texture_disabled_reason_ = new QLabel(texture_section_->content());
    texture_disabled_reason_->setObjectName(
        QStringLiteral("tool_settings.texture.disabled_reason"));
    texture_disabled_reason_->setWordWrap(true);
    texture_disabled_reason_->hide();
    layout->addWidget(texture_disabled_reason_,
                      static_cast<int>(kTextureControls.size()), 0, 1, 2);
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
    binding.dispatch_timer->start();
  }

  bool flush_texture_dispatch(std::size_t index) {
    TextureControlWidget &binding = texture_controls_[index];
    if (!binding.pending_value.has_value()) {
      return !binding.invalid_value;
    }
    binding.dispatch_timer->stop();
    if (editor_ == nullptr || !binding.gesture_revision.has_value() ||
        editor_->modeling().face_texture_settings_revision() !=
            *binding.gesture_revision) {
      discard_texture_dispatch(binding);
      schedule_texture_refresh();
      return false;
    }
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
    log_failure("Face texture settings update", result);
    if (result.ok()) {
      // Accepted corrections also dismiss a validation error when they restore
      // the already-stored value and therefore do not advance owner revisions.
      texture_action_error_.clear();
      present_texture_action_message({});
      const QuaderFaceTextureRevision revision =
          editor_->modeling().face_texture_settings_revision();
      for (TextureControlWidget &other : texture_controls_) {
        if (other.gesture_revision == previous_revision) {
          other.gesture_revision = revision;
        }
      }
    } else {
      texture_action_error_ = qtext(result.error_message());
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
      texture_action_error_ = localized_text(
          localization_, "tool_settings.edit_texture.stale_selection",
          "The face selection or texture settings changed. Review the current "
          "selection and try again.");
      present_texture_action_message(texture_action_error_);
      return false;
    }
    if (std::any_of(texture_controls_.begin(), texture_controls_.end(),
                    [](const TextureControlWidget &binding) {
                      return binding.invalid_value;
                    })) {
      texture_action_error_ = localized_text(
          localization_, "tool_settings.edit_texture.invalid_edit",
          "Finish a valid texture value before applying an action.");
      present_texture_action_message(texture_action_error_);
      return false;
    }
    for (std::size_t index = 0; index < texture_controls_.size(); ++index) {
      if (!flush_texture_dispatch(index)) {
        present_texture_action_message(texture_action_error_);
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
    const std::uint64_t selection_token = texture_settings_->selection_token;
    if (!flush_texture_inputs_for_action()) {
      return;
    }
    const QuaderApiVoidResult result =
        editor_->modeling().apply_face_texture_action(
            QuaderFaceTextureActionRequest{
                .action = kTextureActions[index].action,
                .selection_token = selection_token});
    log_failure("Face texture action", result);
    refresh_texture_settings(true);
    texture_action_error_ = result.ok() ? QString{} : qtext(result.error_message());
    present_texture_action_message(texture_action_error_);
    texture_action_buttons_[index]->setFocus(Qt::MouseFocusReason);
  }

  void discard_texture_dispatch(TextureControlWidget &binding) {
    binding.dispatch_timer->stop();
    binding.pending_value.reset();
    binding.last_requested_value.reset();
    binding.gesture_revision.reset();
    binding.selection_token = 0;
    binding.merge_key.clear();
    binding.editing = false;
    binding.invalid_value = false;
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
    controls_.clear();
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

  void present_inactive_target() {
    clear_controls();
    last_target_.reset();
    inactive_presented_ = true;
    target_title_->setText(localized_text(
        localization_, "tool_settings.no_active.title", "No Active Tool"));
    description_label_->setText(localized_text(
        localization_, "tool_settings.no_active.description",
        "Activate a tool to view its description and settings."));
    add_empty_settings_message(localized_text(
        localization_, "tool_settings.no_active.settings",
        "No tool settings are available."));
    controls_layout_->addStretch(1);
  }

  void queue_number_dispatch(std::size_t control_index, double value) {
    if (control_index >= controls_.size()) {
      return;
    }
    ControlWidget &binding = controls_[control_index];
    if (!binding.pending_number_value.has_value()) {
      if (editor_ == nullptr || !last_target_.has_value() ||
          !last_target_->active) {
        return;
      }
      binding.pending_number_lifecycle = NumberDispatchLifecycle{
          .settings_revision = editor_->tools().active_settings_revision(),
          .active_tool_revision = editor_->tools().active_tool_revision(),
          .tool_id = last_target_->tool_id,
          .preview_active = last_target_->preview_active,
      };
    }
    binding.pending_number_value = value;
    if (binding.number_dispatch_timer != nullptr) {
      const bool pointer_drag = binding.number_slider != nullptr &&
                                binding.number_slider->isSliderDown();
      // Pointer drags publish the latest value at a bounded cadence so live
      // geometry remains visible without compiling once per mouse event.
      // Keyboard, wheel, and spin edits retain one trailing dispatch window.
      if (!pointer_drag || !binding.number_dispatch_timer->isActive()) {
        binding.number_dispatch_timer->start();
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
    if (editor_->tools().active_settings_revision() !=
            lifecycle.settings_revision ||
        editor_->tools().active_tool_revision() !=
            lifecycle.active_tool_revision) {
      return false;
    }

    const QuaderToolSettingsTarget target = editor_->tools().active_settings();
    if (!target.active || target.tool_id != lifecycle.tool_id ||
        target.preview_active != lifecycle.preview_active) {
      return false;
    }
    return std::any_of(
        target.controls.begin(), target.controls.end(),
        [&binding](const QuaderToolSettingControl &control) {
          return control.kind == QuaderToolSettingControlKind::Number &&
                 control.id == binding.id;
        });
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
      if (!binding.pending_number_lifecycle.has_value()) {
        continue;
      }
      binding.pending_number_lifecycle->settings_revision = settings_revision;
      binding.pending_number_lifecycle->active_tool_revision =
          active_tool_revision;
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
    binding.pending_number_value.reset();
    binding.pending_number_lifecycle.reset();
    if (binding.number_dispatch_timer != nullptr) {
      binding.number_dispatch_timer->stop();
    }
    if (!lifecycle_current || !binding.number_dispatch) {
      schedule_refresh();
      return;
    }
    binding.number_dispatch(value);
    // A successful owner write advances the settings revision. Other values
    // queued in the same UI lifecycle remain valid and must still flush.
    rebind_pending_number_dispatches_to_current_revision();
  }

  void flush_pending_number_dispatches() {
    for (std::size_t index = 0; index < controls_.size(); ++index) {
      flush_number_dispatch(index);
    }
  }

  void discard_pending_number_dispatches() {
    for (ControlWidget &binding : controls_) {
      binding.pending_number_value.reset();
      binding.pending_number_lifecycle.reset();
      if (binding.number_dispatch_timer != nullptr) {
        binding.number_dispatch_timer->stop();
      }
    }
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
          quader_face_texture_action_label(description.action));
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
        "Use combined selected-face bounds for Fit, Justify, and Flip Face.");
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
    }
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

  void rebuild(const QuaderToolSettingsTarget &target) {
    clear_controls();
    inactive_presented_ = false;
    target_title_->setText(localized_target_title(localization_, target));
    description_label_->setText(
        localized_target_description(localization_, target));

    for (std::size_t index = 0; index < target.controls.size(); ++index) {
      add_control(target, target.controls[index], index);
    }
    if (target.controls.empty()) {
      add_empty_settings_message(localized_text(
          localization_, "tool_settings.no_settings",
          "This tool has no adjustable settings."));
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
      QObject::connect(toggle, &QCheckBox::toggled, &panel_,
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
      auto *row_layout = new QGridLayout(row);
      row_layout->setColumnStretch(0, 1);
      auto *field_label = new QLabel(label, row);
      field_label->setObjectName(object_name + QStringLiteral(".label"));
      field_label->setWordWrap(true);
      row_layout->addWidget(field_label, 0, 0, 1, 2);

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
      row_layout->addWidget(slider, 1, 0);
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
      dispatch_timer->setInterval(kNumberDispatchIntervalMs);
      binding.number_dispatch_timer = dispatch_timer;
      binding.number_dispatch = send_number_to_editor;
      QObject::connect(dispatch_timer, &QTimer::timeout, &panel_,
                       [this, control_index]() {
                         flush_number_dispatch(control_index);
                       });
      QObject::connect(slider, &QSlider::sliderReleased, &panel_,
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
                           flush_number_dispatch(control_index);
                         });
        row_layout->addWidget(spin, 1, 1);
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
                           flush_number_dispatch(control_index);
                         });
        row_layout->addWidget(spin, 1, 1);
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
      QObject::connect(button, &QPushButton::clicked, &panel_,
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
    if (apply_button_->isEnabled() != target.can_apply) {
      apply_button_->setEnabled(target.can_apply);
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
      const bool integer_editing =
          widget_or_descendant_has_focus(binding.integer);
      const bool floating_editing =
          widget_or_descendant_has_focus(binding.floating);
      const bool slider_editing =
          binding.number_slider != nullptr &&
          binding.number_slider->isSliderDown();
      const bool number_dispatch_pending =
          binding.pending_number_value.has_value();
      const bool number_interaction_active =
          slider_editing || integer_editing || floating_editing ||
          number_dispatch_pending;

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
      if (binding.integer != nullptr && !integer_editing && !slider_editing &&
          !number_dispatch_pending) {
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

  QtToolSettingsPanel &panel_;
  const LocalizationService *localization_ = nullptr;
  QuaderEditorApi *editor_ = nullptr;
  QLabel *target_title_ = nullptr;
  QScrollArea *scroll_area_ = nullptr;
  QtToolSettingsSection *description_section_ = nullptr;
  QLabel *description_label_ = nullptr;
  QtToolSettingsSection *settings_section_ = nullptr;
  QtToolSettingsSection *texture_section_ = nullptr;
  QLabel *texture_disabled_reason_ = nullptr;
  QtToolSettingsSection *edit_texture_section_ = nullptr;
  QLabel *texture_action_message_ = nullptr;
  QCheckBox *texture_treat_as_one_ = nullptr;
  QWidget *controls_container_ = nullptr;
  QVBoxLayout *controls_layout_ = nullptr;
  QLabel *empty_settings_label_ = nullptr;
  QWidget *actions_container_ = nullptr;
  QWidget *preview_actions_container_ = nullptr;
  QPushButton *reset_button_ = nullptr;
  QPushButton *apply_button_ = nullptr;
  QPushButton *cancel_button_ = nullptr;
  std::vector<ControlWidget> controls_;
  std::array<TextureControlWidget, 5> texture_controls_;
  std::array<QPushButton *, 25> texture_action_buttons_{};
  std::array<QLabel *, 6> texture_action_group_labels_{};
  std::optional<QuaderFaceTextureSettings> texture_settings_;
  std::optional<QuaderFaceTextureRevision> texture_revision_;
  std::uint64_t texture_keybindings_revision_ = 0;
  double texture_rotation_step_ = 0.0;
  QString texture_action_error_;
  std::optional<QuaderToolSettingsTarget> last_target_;
  bool synchronizing_ = false;
  bool refresh_scheduled_ = false;
  bool inactive_presented_ = false;
  bool texture_synchronizing_ = false;
  bool texture_refresh_scheduled_ = false;
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

void QtToolSettingsPanel::set_localization(
    const LocalizationService &localization) {
  impl_->set_localization(localization);
}

void QtToolSettingsPanel::refresh() { impl_->refresh(); }

void QtToolSettingsPanel::refresh_texture_settings() {
  impl_->refresh_texture_settings();
}

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
