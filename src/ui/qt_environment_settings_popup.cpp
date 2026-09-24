////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_environment_settings_popup.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "core/release_features.hpp"
#include "core/scene_environment_catalog.hpp"
#include "scene/light_properties.hpp"
#include "scene/scene_document.hpp"
#include "ui/qt_color_swatch_button.hpp"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_continuous_edit_tracker.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleValidator>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPointer>
#include <QScreen>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {
constexpr float kRadToDeg = 57.29577951308232f;
constexpr float kDegToRad = 0.017453292519943295f;
QString qt_text(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}
enum class EnvironmentColorProperty { Environment, Fog };

/// Keeps small atmospheric coefficients readable without rounding owner values.
class EnvironmentFogSpinBox final : public QtDoubleSpinBox {
public:
  explicit EnvironmentFogSpinBox(QWidget *parent) : QtDoubleSpinBox(parent) {
    QObject::connect(lineEdit(), &QLineEdit::textEdited, this,
                     [this]() { draft_edited_ = true; });
    const auto settled = [this]() { draft_edited_ = false; };
    QObject::connect(this, &QDoubleSpinBox::valueChanged, this, settled);
    QObject::connect(this, &QDoubleSpinBox::editingFinished, this, settled);
  }

protected:
  QString textFromValue(double value) const override {
    if (value != 0.0 && std::abs(value) < 0.0001) {
      return locale().toString(value, 'g', 4);
    }
    return QtDoubleSpinBox::textFromValue(value);
  }

  double valueFromText(const QString &text) const override {
    if (!specialValueText().isEmpty() && text == specialValueText()) {
      return minimum();
    }
    if (!draft_edited_ && text == textFromValue(value())) {
      return value();
    }
    bool valid = false;
    const double parsed = locale().toDouble(text, &valid);
    return valid ? parsed : QtDoubleSpinBox::valueFromText(text);
  }

  QValidator::State validate(QString &text, int &position) const override {
    if (!specialValueText().isEmpty() && text == specialValueText()) {
      return QValidator::Acceptable;
    }
    QDoubleValidator validator(minimum(), maximum(), decimals());
    validator.setLocale(locale());
    validator.setNotation(QDoubleValidator::ScientificNotation);
    return validator.validate(text, position);
  }

private:
  bool draft_edited_ = false;
};

} // namespace

struct QtEnvironmentSettingsPopup::Impl {
  struct Section {
    QTreeWidgetItem *item = nullptr;
    QWidget *editor_parent = nullptr;
  };

  explicit Impl(QtEnvironmentSettingsPopup &panel) : panel_(panel) {
    panel_.setObjectName(QStringLiteral("environmentSettings"));
    panel_.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    panel_.setFrameShape(QFrame::StyledPanel);
    auto *layout = new QVBoxLayout(&panel_);
    layout->setContentsMargins(4, 4, 4, 4);
    tree_ = new QTreeWidget(&panel_);
    tree_->setObjectName(QStringLiteral("environmentSettings.tree"));
    tree_->setColumnCount(2);
    tree_->setSelectionMode(QAbstractItemView::NoSelection);
    tree_->setAlternatingRowColors(true);
    tree_->setRootIsDecorated(true);
    tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree_->header()->setMinimumSectionSize(64);
    tree_->header()->resizeSection(0, 170);
    layout->addWidget(tree_);
    panel_.setFocusProxy(tree_);
    QObject::connect(tree_, &QTreeWidget::itemCollapsed, &panel_,
                     [this](QTreeWidgetItem *item) {
                       if (rebuilding_ || item == nullptr) {
                         return;
                       }
                       const auto generation = runtime_binding_generation_;
                       QPointer<QtEnvironmentSettingsPopup> guard(&panel_);
                       settle_interaction();
                       if (!guard ||
                           generation != runtime_binding_generation_) {
                         return;
                       }
                       // A hidden editor must not remain the current row: later
                       // field/locale synchronization would scroll it into view
                       // and reopen its ancestors.
                       tree_->setCurrentItem(item);
                       tree_->setFocus(Qt::OtherFocusReason);
                     });
  }

  ~Impl() {
    // Child focus signals must not access Impl after its destruction.
    rebuilding_ = true;
    delete tree_;
  }

  QString localized(std::string_view key, std::string_view fallback) const {
    return localization_ ? qt_text(localization_->text(key, fallback))
                         : qt_text(fallback);
  }
  QString stable_name(std::string_view id) const {
    return QStringLiteral("environmentSettings.") + qt_text(id);
  }

  void refresh(QuaderEditorApi &editor, const SceneDocument &document,
               LocalizationService *localization) {
    const auto next_binding = editor.query().document_binding();
    if ((document_binding_ && *document_binding_ != next_binding) ||
        (document_ && document_ != &document) ||
        (editor_ && editor_ != &editor)) {
      abandon_runtime();
    }
    document_binding_ = next_binding;
    editor_ = &editor;
    document_ = &document;
    localization_ = localization;
    const auto revision = document.scene_settings_revision();
    const auto locale_revision = localization ? localization->revision() : 0;
    // Do not query/copy settings or touch widgets on unchanged frames.
    if (!force_refresh_ && presented_revision_ == revision &&
        presented_localization_ == localization &&
        presented_locale_revision_ == locale_revision) {
      return;
    }
    if (continuous_edit_active_ || active_color_dialog_) {
      return;
    }
    synchronize(revision);
  }

  void synchronize(std::uint64_t revision) {
    if (!editor_) {
      return;
    }
    presented_settings_ = editor_->query().scene_settings();
    std::array<std::pair<QTreeWidgetItem *, bool>, 4> expansion{};
    std::size_t index = 0;
    for (const auto &[id, item] : sections_) {
      expansion[index++] = {item, item->isExpanded()};
    }
    rebuilding_ = true;
    panel_.setWindowTitle(release_has_feature(ReleaseFeature::Environment)
                              ? localized("ui.inspector.environment", "Environment")
                              : localized("ui.inspector.camera", "Camera"));
    tree_->setHeaderLabels({localized("ui.inspector.property", "Property"),
                            localized("ui.inspector.value", "Value")});
    build_scene_settings_sections();
    // Persistent editor geometry updates can expand ancestors internally.
    // Retain the user's disclosure choice across the complete synchronization.
    for (const auto &[item, expanded] : expansion) {
      if (item != nullptr && item->isExpanded() != expanded) {
        item->setExpanded(expanded);
      }
    }
    rebuilding_ = false;
    presented_revision_ = revision;
    presented_localization_ = localization_;
    presented_locale_revision_ = localization_ ? localization_->revision() : 0;
    force_refresh_ = false;
    ++presentation_update_count_;
  }

  void abandon_runtime() noexcept {
    ++runtime_binding_generation_;
    rebuilding_ = true;
    editor_ = nullptr;
    document_ = nullptr;
    document_binding_.reset();
    localization_ = nullptr;
    for (QDoubleSpinBox *spin : panel_.findChildren<QDoubleSpinBox *>()) {
      qt_install_numeric_spin_box(*spin).abandon_interaction();
    }
    active_continuous_edit_.reset();
    continuous_edit_active_ = false;
    active_continuous_id_.clear();
    if (active_color_dialog_) {
      active_color_dialog_->reject();
    }
    active_color_dialog_ = nullptr;
    panel_.hide();
    force_refresh_ = true;
    rebuilding_ = false;
  }

  bool binding_is_current() {
    if (!editor_ || !document_binding_) {
      return false;
    }
    if (editor_->query().document_binding() != *document_binding_) {
      abandon_runtime();
      return false;
    }
    return true;
  }

  void settle_interaction() {
    if (rebuilding_) {
      return;
    }
    const auto generation = runtime_binding_generation_;
    QPointer<QtEnvironmentSettingsPopup> guard(&panel_);
    if (QWidget *focus = panel_.focusWidget();
        focus && panel_.isAncestorOf(focus)) {
      focus->clearFocus();
    }
    if (!guard || generation != runtime_binding_generation_) {
      return;
    }
    active_continuous_edit_.reset();
    continuous_edit_active_ = false;
    active_continuous_id_.clear();
    force_refresh_ = true;
  }

  Section add_section(std::string stable_id, const QString &title) {
    auto it = sections_.find(stable_id);
    if (it == sections_.end()) {
      auto *item = new QTreeWidgetItem(tree_);
      item->setData(0, Qt::UserRole, qt_text(stable_id));
      item->setFlags(Qt::ItemIsEnabled);
      item->setFirstColumnSpanned(true);
      item->setExpanded(true);
      it = sections_.emplace(stable_id, item).first;
    }
    if (it->second->text(0) != title) {
      it->second->setText(0, title);
    }
    return {it->second, tree_->viewport()};
  }

  void add_row(Section &section, const std::string &id, const QString &label,
               QWidget *field) {
    auto it = rows_.find(id);
    if (it == rows_.end()) {
      auto *item = new QTreeWidgetItem(section.item);
      item->setData(0, Qt::UserRole, stable_name(id));
      item->setFlags(Qt::ItemIsEnabled);
      field->setMinimumWidth(0);
      field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
      tree_->setItemWidget(item, 1, field);
      it = rows_.emplace(id, item).first;
    }
    if (it->second->text(0) != label) {
      it->second->setText(0, label);
    }
  }
  void set_field_enabled(const std::string &id, bool enabled) {
    QWidget *field = tree_->itemWidget(rows_.at(id), 1);
    if (field->isEnabled() != enabled) {
      field->setEnabled(enabled);
    }
  }

  QCheckBox *add_checkbox(Section &section, std::string id,
                          const QString &label, bool checked,
                          std::function<void(bool)> changed) {
    auto *box = panel_.findChild<QCheckBox *>(stable_name(id));
    if (!box) {
      box = new QCheckBox(section.editor_parent);
      box->setObjectName(stable_name(id));
      QObject::connect(box, &QCheckBox::toggled, &panel_,
                       [this, changed = std::move(changed)](bool value) {
                         if (!rebuilding_) {
                           changed(value);
                         }
                       });
    }
    add_row(section, id, label, box);
    if (box->isChecked() != checked) {
      const QSignalBlocker blocker(box);
      box->setChecked(checked);
    }
    return box;
  }

  template <class Enum>
  QComboBox *
  add_enum_combo(Section &section, std::string id, const QString &label,
                 Enum value,
                 const std::vector<std::pair<Enum, QString>> &options,
                 std::function<void(Enum)> changed) {
    auto *combo = panel_.findChild<QComboBox *>(stable_name(id));
    if (!combo) {
      combo = new QComboBox(section.editor_parent);
      combo->setObjectName(stable_name(id));
      for (const auto &[option, text] : options) {
        combo->addItem(text, static_cast<int>(option));
      }
      QObject::connect(
          combo, &QComboBox::activated, &panel_,
          [this, combo, changed = std::move(changed)](int index) {
            if (!rebuilding_ && index >= 0) {
              changed(static_cast<Enum>(combo->itemData(index).toInt()));
            }
          });
    }
    const QSignalBlocker blocker(combo);
    for (int i = 0; i < static_cast<int>(options.size()); ++i) {
      if (combo->itemText(i) != options[i].second) {
        combo->setItemText(i, options[i].second);
      }
    }
    combo->setCurrentIndex(combo->findData(static_cast<int>(value)));
    add_row(section, id, label, combo);
    return combo;
  }

  void add_environment_preset(Section &section, const std::string &current) {
    const std::string id = "environment.environment_preset";
    auto *combo = panel_.findChild<QComboBox *>(stable_name(id));
    if (!combo) {
      combo = new QComboBox(section.editor_parent);
      combo->setObjectName(stable_name(id));
      for (const auto &descriptor : scene_environment_catalog()) {
        combo->addItem(
            localized(descriptor.label_key, descriptor.fallback_label),
            qt_text(descriptor.id));
      }
      QObject::connect(
          combo, &QComboBox::activated, &panel_, [this, combo](int index) {
            if (rebuilding_ || index < 0) {
              return;
            }
            const std::string value =
                combo->itemData(index).toString().toUtf8().toStdString();
            apply_scene_settings([value](QuaderSceneSettings &settings) {
              settings.environment.environment_id = value;
            });
          });
    }
    const QSignalBlocker blocker(combo);
    int i = 0;
    for (const auto &descriptor : scene_environment_catalog()) {
      const QString label =
          localized(descriptor.label_key, descriptor.fallback_label);
      if (combo->itemText(i) != label) {
        combo->setItemText(i, label);
      }
      ++i;
    }
    combo->setCurrentIndex(combo->findData(qt_text(current)));
    add_row(section, id, localized("ui.inspector.environment_preset", "Preset"),
            combo);
  }

  std::string begin_number_edit(const std::string &id) {
    continuous_edit_active_ = true;
    active_continuous_id_ = id;
    return active_continuous_edit_.ensure_merge_key(id, kInvalidEntityId, {});
  }
  void finish_number_edit(const std::string &id) {
    if (rebuilding_ || active_continuous_id_ != id) {
      return;
    }
    active_continuous_edit_.reset();
    continuous_edit_active_ = false;
    active_continuous_id_.clear();
    force_refresh_ = true;
  }

  QDoubleSpinBox *
  add_continuous_number(Section &section, std::string id, const QString &label,
                        double value, double minimum, double maximum,
                        double step, int decimals,
                        std::function<void(double, std::string)> changed,
                        bool fog_number = false) {
    auto *spin = panel_.findChild<QDoubleSpinBox *>(stable_name(id));
    if (!spin) {
      spin = fog_number ? static_cast<QDoubleSpinBox *>(
                              new EnvironmentFogSpinBox(section.editor_parent))
                        : new QtDoubleSpinBox(section.editor_parent);
      spin->setObjectName(stable_name(id));
      // Formatting rounds the presentation only, never the stored owner value.
      spin->setDecimals(std::max(12, decimals));
      spin->setSingleStep(step);
      spin->setKeyboardTracking(true);
      QObject::connect(spin, &QDoubleSpinBox::valueChanged, &panel_,
                       [this, id, changed = std::move(changed)](double next) {
                         if (!rebuilding_) {
                           changed(next, begin_number_edit(id));
                         }
                       });
      const auto finish = [this, id]() { finish_number_edit(id); };
      QObject::connect(spin, &QDoubleSpinBox::editingFinished, &panel_, finish);
      qt_install_numeric_spin_box(*spin).interaction_canceled = finish;
    }
    const QSignalBlocker blocker(spin);
    // Negative fog scattering is the historical Disabled sentinel, shown as 0.
    const double displayed = fog_number ? std::max(minimum, value) : value;
    const double lower = std::min(minimum, displayed);
    const double upper = std::max(maximum, displayed);
    if (spin->minimum() != lower || spin->maximum() != upper) {
      spin->setRange(lower, upper);
    }
    if (spin->value() != displayed) {
      spin->setValue(displayed);
    }
    add_row(section, id, label, spin);
    return spin;
  }

  QtColorSwatchButton *add_color_button(Section &section, std::string id,
                                        const QString &label, QVec3 value,
                                        EnvironmentColorProperty property) {
    const QColor initial = QColor::fromRgbF(std::clamp(value.x, 0.0f, 1.0f),
                                            std::clamp(value.y, 0.0f, 1.0f),
                                            std::clamp(value.z, 0.0f, 1.0f));
    auto *button = static_cast<QtColorSwatchButton *>(
        panel_.findChild<QPushButton *>(stable_name(id)));
    if (!button) {
      button = new QtColorSwatchButton(section.editor_parent);
      button->setObjectName(stable_name(id));
      QObject::connect(
          button, &QtColorSwatchButton::clicked, &panel_,
          [this, id, button, property]() {
            if (!binding_is_current()) {
              return;
            }
            const auto generation = runtime_binding_generation_;
            QPointer<QtEnvironmentSettingsPopup> guard(&panel_);
            settle_interaction();
            if (!guard || generation != runtime_binding_generation_ ||
                !editor_) {
              return;
            }
            const auto edit =
                property == EnvironmentColorProperty::Fog
                    ? editor_->scene().begin_fog_color_edit()
                    : editor_->scene().begin_environment_color_edit();
            if (!guard || generation != runtime_binding_generation_) {
              return;
            }
            if (!edit.ok()) {
              (void)editor_->ui().notify(
                  {.level = QuaderNotificationLevel::Error,
                   .message = edit.error_message()});
              return;
            }
            const QColor before = button->swatch_color();
            QPointer<QColorDialog> dialog = new QColorDialog(before, &panel_);
            dialog->setObjectName(stable_name(id + ".dialog"));
            dialog->setOption(QColorDialog::DontUseNativeDialog, true);
            active_color_dialog_ = dialog;
            QPointer<QtColorSwatchButton> guarded_button(button);
            QObject::connect(
                dialog.data(), &QColorDialog::currentColorChanged,
                dialog.data(),
                [this, dialog, guard, generation, guarded_button, property,
                 edit_ref = edit.value()](const QColor &color) {
                  if (!guard || generation != runtime_binding_generation_ ||
                      !editor_ || !color.isValid()) {
                    return;
                  }
                  QVec3 next{static_cast<float>(color.redF()),
                             static_cast<float>(color.greenF()),
                             static_cast<float>(color.blueF())};
                  if (property == EnvironmentColorProperty::Fog) {
                    next = light_srgb_to_linear(next);
                  }
                  const auto result =
                      property == EnvironmentColorProperty::Fog
                          ? editor_->scene().update_fog_color_edit(edit_ref,
                                                                   next)
                          : editor_->scene().update_environment_color_edit(
                                edit_ref, next);
                  if (!guard || generation != runtime_binding_generation_) {
                    return;
                  }
                  if (!result.ok()) {
                    if (dialog) {
                      dialog->reject();
                    }
                    return;
                  }
                  if (guarded_button) {
                    guarded_button->set_swatch_color(color);
                  }
                });
            const bool cancel = dialog->exec() != QDialog::Accepted;
            // The popup can be destroyed by a nested event. QObject ownership
            // then deletes the heap dialog and clears this local weak pointer.
            delete dialog.data();
            if (!guard) {
              return;
            }
            if (generation != runtime_binding_generation_ || !editor_) {
              return;
            }
            const auto finished =
                property == EnvironmentColorProperty::Fog
                    ? editor_->scene().finish_fog_color_edit(edit.value(),
                                                             cancel)
                    : editor_->scene().finish_environment_color_edit(
                          edit.value(), cancel);
            if (!guard || generation != runtime_binding_generation_) {
              return;
            }
            if (cancel && guarded_button) {
              guarded_button->set_swatch_color(before);
            }
            force_refresh_ = true;
            if (!finished.ok()) {
              (void)editor_->ui().notify(
                  {.level = QuaderNotificationLevel::Error,
                   .message = finished.error_message()});
            }
          });
    }
    button->setAccessibleName(label);
    if (button->swatch_color() != initial) {
      button->set_swatch_color(initial);
    }
    add_row(section, id, label, button);
    return button;
  }

  void apply_scene_settings(
      const std::function<void(QuaderSceneSettings &)> &mutation,
      std::string merge_key = {}) {
    if (!binding_is_current()) {
      return;
    }
    auto settings = editor_->query().scene_settings();
    mutation(settings);
    settings.environment.intensity =
        std::max(settings.environment.intensity, 0.0f);
    settings.environment.background_intensity =
        std::max(settings.environment.background_intensity, 0.0f);
    settings.camera.near_plane = std::max(settings.camera.near_plane, 0.001f);
    settings.camera.far_plane = std::max(settings.camera.far_plane,
                                         settings.camera.near_plane + 0.001f);
    const auto generation = runtime_binding_generation_;
    QPointer<QtEnvironmentSettingsPopup> guard(&panel_);
    const auto result = editor_->scene().set_scene_settings(
        settings, QuaderSceneEditOptions{.merge_key = std::move(merge_key)});
    if (!guard || generation != runtime_binding_generation_ ||
        !binding_is_current()) {
      return;
    }
    if (!result.ok()) {
      (void)editor_->ui().notify({.level = QuaderNotificationLevel::Error,
                                  .message = result.error_message()});
      return;
    }
    // A checkbox can change a dependent field's enabled state immediately.
    // Numeric gestures keep their field and draft intact until settlement.
    if (!continuous_edit_active_ && !active_color_dialog_) {
      synchronize(document_->scene_settings_revision());
    }
    if (active_continuous_id_ == "fog.in_scattering_size") {
      set_field_enabled("fog.in_scattering_start",
                        settings.fog.enabled &&
                            settings.fog.in_scattering_size > 0.0f);
    }
  }

  void build_fog_section(const QuaderSceneFogSettings &settings) {
    Section fog = add_section("fog", localized("ui.inspector.fog", "Fog"));
    auto *enabled = add_checkbox(
        fog, "fog.enabled", localized("ui.inspector.enabled", "Enabled"),
        settings.enabled, [this](bool value) {
          apply_scene_settings(
              [value](QuaderSceneSettings &next) { next.fog.enabled = value; });
        });
    enabled->setToolTip(localized("ui.inspector.fog.enabled.tooltip",
                                  "Show atmospheric fog in Rendered viewports. "
                                  "Other shading modes stay clear."));

    const auto add_fog_number = [this, &fog, &settings](
                                    std::string stable_id, const QString &label,
                                    float QuaderSceneFogSettings::*member,
                                    double minimum, double maximum, double step,
                                    const QString &tooltip) {
      auto *field = add_continuous_number(
          fog, std::move(stable_id), label, settings.*member, minimum, maximum,
          step, 50,
          [this, member](double value, std::string merge_key) {
            apply_scene_settings(
                [value, member](QuaderSceneSettings &next) {
                  // Return can emit the displayed zero for an unchanged
                  // Disabled sentinel. Preserve the owner's negative value.
                  if (member == &QuaderSceneFogSettings::in_scattering_size &&
                      value <= 0.0 && next.fog.in_scattering_size <= 0.0f) {
                    return;
                  }
                  next.fog.*member = static_cast<float>(value);
                },
                std::move(merge_key));
          },
          true);
      field->setEnabled(settings.enabled);
      field->setToolTip(tooltip);
      return field;
    };
    const double maximum = std::numeric_limits<float>::max();
    add_fog_number("fog.density",
                   localized("ui.inspector.fog.density", "Density (1/m)"),
                   &QuaderSceneFogSettings::density, 0.0, maximum, 0.001,
                   localized("ui.inspector.fog.density.tooltip",
                             "Extinction per meter at the fog height. Higher "
                             "values make the fog denser."));
    add_fog_number("fog.distance",
                   localized("ui.inspector.fog.distance", "Start (m)"),
                   &QuaderSceneFogSettings::distance, 0.0, maximum, 0.1,
                   localized("ui.inspector.fog.distance.tooltip",
                             "Distance from the camera before fog starts."));
    auto *cutoff = add_fog_number(
        "fog.cut_off_distance",
        localized("ui.inspector.fog.cut_off_distance", "Cutoff (m)"),
        &QuaderSceneFogSettings::cut_off_distance, 0.0, maximum, 1.0,
        localized("ui.inspector.fog.cut_off_distance.tooltip",
                  "Objects beyond this distance receive no fog. Zero means "
                  "unlimited, including the sky background."));
    cutoff->setSpecialValueText(
        localized("ui.inspector.fog.unlimited", "Unlimited"));
    add_fog_number(
        "fog.maximum_opacity",
        localized("ui.inspector.fog.maximum_opacity", "Max Opacity"),
        &QuaderSceneFogSettings::maximum_opacity, 0.0, 1.0, 0.01,
        localized(
            "ui.inspector.fog.maximum_opacity.tooltip",
            "Upper limit of fog opacity, from transparent (0) to opaque (1)."));
    add_fog_number("fog.height",
                   localized("ui.inspector.fog.height", "Height (m)"),
                   &QuaderSceneFogSettings::height, -maximum, maximum, 0.1,
                   localized("ui.inspector.fog.height.tooltip",
                             "World height of the fog layer, measured along "
                             "the vertical axis."));
    add_fog_number(
        "fog.height_falloff",
        localized("ui.inspector.fog.height_falloff", "Falloff (1/m)"),
        &QuaderSceneFogSettings::height_falloff, 0.0, maximum, 0.01,
        localized("ui.inspector.fog.height_falloff.tooltip",
                  "How quickly density decreases above the fog height. Zero "
                  "gives uniform density at every height."));
    auto *color = add_color_button(
        fog, "fog.color", localized("ui.inspector.fog.color", "Color / Tint"),
        light_linear_to_srgb(settings.color), EnvironmentColorProperty::Fog);
    color->setEnabled(settings.enabled);
    color->setToolTip(
        localized("ui.inspector.fog.color.tooltip",
                  "Fog color, or a tint applied to the sampled environment "
                  "when Environment Color is enabled."));
    auto *environment =
        add_checkbox(fog, "fog.color_from_environment",
                     localized("ui.inspector.fog.color_from_environment",
                               "Environment Color"),
                     settings.color_from_environment, [this](bool value) {
                       apply_scene_settings([value](QuaderSceneSettings &next) {
                         next.fog.color_from_environment = value;
                       });
                     });
    environment->setEnabled(settings.enabled);
    environment->setToolTip(localized(
        "ui.inspector.fog.color_from_environment.tooltip",
        "Sample the scene environment in the view direction and multiply it by "
        "the fog tint. Requires an enabled environment."));
    auto *scattering_size = add_fog_number(
        "fog.in_scattering_size",
        localized("ui.inspector.fog.in_scattering_size", "Sun Scattering"),
        &QuaderSceneFogSettings::in_scattering_size, 0.0, maximum, 1.0,
        localized("ui.inspector.fog.in_scattering_size.tooltip",
                  "Directional-light scattering; zero disables it. Try 10 to "
                  "100. Smaller positive values spread the glow wider. "
                  "Requires a directional light."));
    scattering_size->setSpecialValueText(
        localized("ui.inspector.fog.disabled", "Disabled"));
    auto *scattering_start = add_fog_number(
        "fog.in_scattering_start",
        localized("ui.inspector.fog.in_scattering_start", "Sun Start (m)"),
        &QuaderSceneFogSettings::in_scattering_start, 0.0, maximum, 0.1,
        localized("ui.inspector.fog.in_scattering_start.tooltip",
                  "Distance from the camera before directional-light "
                  "scattering starts."));
    scattering_start->setEnabled(settings.enabled &&
                                 settings.in_scattering_size > 0.0f);
  }

  void build_scene_settings_sections() {
    const QuaderSceneSettings *settings = &presented_settings_;

    if (release_has_feature(ReleaseFeature::Environment)) {
      Section environment = add_section(
          "environment", localized("ui.inspector.environment", "Environment"));
      add_environment_preset(environment, settings->environment.environment_id);
      add_checkbox(environment, "environment.enabled",
                   localized("ui.inspector.enabled", "Enabled"),
                   settings->environment.enabled, [this](bool enabled) {
                     apply_scene_settings([enabled](QuaderSceneSettings &next) {
                       next.environment.enabled = enabled;
                     });
                   });
      add_checkbox(environment, "environment.background",
                   localized("ui.inspector.background", "Background"),
                   settings->environment.show_background, [this](bool visible) {
                     apply_scene_settings([visible](QuaderSceneSettings &next) {
                       next.environment.show_background = visible;
                     });
                   });
      add_color_button(environment, "environment.clear_color",
                       localized("ui.inspector.clear_color", "Clear Color"),
                       settings->environment.clear_color,
                       EnvironmentColorProperty::Environment);
      add_continuous_number(environment, "environment.intensity",
                            localized("ui.inspector.intensity", "Intensity"),
                            settings->environment.intensity, 0.0, 16.0, 0.01, 3,
                            [this](double value, std::string merge_key) {
                              apply_scene_settings(
                                  [value](QuaderSceneSettings &next) {
                                    next.environment.intensity =
                                        static_cast<float>(value);
                                  },
                                  std::move(merge_key));
                            });
      add_continuous_number(
          environment, "environment.background_intensity",
          localized("ui.inspector.background_intensity", "Background Intensity"),
          settings->environment.background_intensity, 0.0, 16.0, 0.01, 3,
          [this](double value, std::string merge_key) {
            apply_scene_settings(
                [value](QuaderSceneSettings &next) {
                  next.environment.background_intensity =
                      static_cast<float>(value);
                },
                std::move(merge_key));
          });
      add_continuous_number(
          environment, "environment.rotation",
          localized("ui.inspector.rotation", "Rotation"),
          settings->environment.rotation_y * kRadToDeg, -180.0, 180.0, 1.0, 12,
          [this](double value, std::string merge_key) {
            apply_scene_settings(
                [value](QuaderSceneSettings &next) {
                  next.environment.rotation_y =
                      static_cast<float>(value) * kDegToRad;
                },
                std::move(merge_key));
          });

      build_fog_section(settings->fog);
    }

    if (release_has_feature(ReleaseFeature::CameraSettings)) {
      Section camera =
          add_section("camera", localized("ui.inspector.camera", "Camera"));
      add_continuous_number(
          camera, "camera.near", localized("ui.inspector.near_plane", "Near"),
          settings->camera.near_plane, 0.001, 10000.0, 0.001, 4,
          [this](double value, std::string merge_key) {
            apply_scene_settings(
                [value](QuaderSceneSettings &next) {
                  next.camera.near_plane = static_cast<float>(value);
                },
                std::move(merge_key));
          });
      add_continuous_number(
          camera, "camera.far", localized("ui.inspector.far_plane", "Far"),
          settings->camera.far_plane, 0.001, 1000000.0, 1.0, 2,
          [this](double value, std::string merge_key) {
            apply_scene_settings(
                [value](QuaderSceneSettings &next) {
                  next.camera.far_plane = static_cast<float>(value);
                },
                std::move(merge_key));
          });
    }

    if (!release_has_feature(ReleaseFeature::Environment)) {
      return;
    }
    Section post = add_section(
        "post_process", localized("ui.inspector.post_process", "Post Process"));
    add_checkbox(post, "post_process.enabled",
                 localized("ui.inspector.enabled", "Enabled"),
                 settings->post_process.enabled, [this](bool enabled) {
                   apply_scene_settings([enabled](QuaderSceneSettings &next) {
                     next.post_process.enabled = enabled;
                   });
                 });
    add_checkbox(post, "post_process.anti_aliasing",
                 localized("ui.inspector.anti_aliasing", "Anti-Aliasing"),
                 settings->post_process.anti_aliasing, [this](bool enabled) {
                   apply_scene_settings([enabled](QuaderSceneSettings &next) {
                     next.post_process.anti_aliasing = enabled;
                   });
                 });
    add_checkbox(post, "post_process.dithering",
                 localized("ui.inspector.dithering", "Dithering"),
                 settings->post_process.dithering, [this](bool enabled) {
                   apply_scene_settings([enabled](QuaderSceneSettings &next) {
                     next.post_process.dithering = enabled;
                   });
                 });
    const QuaderToneMapping tone_mapping = settings->post_process.tone_mapping;
    const QuaderToneMapping displayed_tone_mapping =
        tone_mapping == QuaderToneMapping::AcesLegacy ||
                tone_mapping == QuaderToneMapping::DisplayRange
            ? QuaderToneMapping::Aces
            : tone_mapping;
    add_enum_combo<QuaderToneMapping>(
        post, "post_process.tone_mapping",
        localized("ui.inspector.tone_mapping", "Tone Mapping"),
        displayed_tone_mapping,
        {
            {QuaderToneMapping::Linear,
             localized("ui.inspector.tone_mapping.linear", "Linear")},
            {QuaderToneMapping::Aces,
             localized("ui.inspector.tone_mapping.aces", "ACES")},
            {QuaderToneMapping::Agx,
             localized("ui.inspector.tone_mapping.agx", "AgX")},
            {QuaderToneMapping::Filmic,
             localized("ui.inspector.tone_mapping.filmic", "Filmic")},
        },
        [this](QuaderToneMapping value) {
          apply_scene_settings([value](QuaderSceneSettings &next) {
            next.post_process.tone_mapping = value;
          });
        });
    add_checkbox(
        post, "post_process.ambient_occlusion",
        localized("ui.inspector.ambient_occlusion", "Ambient Occlusion"),
        settings->post_process.ambient_occlusion, [this](bool enabled) {
          apply_scene_settings([enabled](QuaderSceneSettings &next) {
            next.post_process.ambient_occlusion = enabled;
          });
        });
    {
      add_continuous_number(
          post, "post_process.ambient_occlusion_intensity",
          localized("ui.inspector.ambient_occlusion_intensity", "AO Intensity"),
          settings->post_process.ambient_occlusion_intensity, 0.0, 8.0, 0.01, 3,
          [this](double value, std::string merge_key) {
            apply_scene_settings(
                [value](QuaderSceneSettings &next) {
                  next.post_process.ambient_occlusion_intensity =
                      static_cast<float>(value);
                },
                std::move(merge_key));
          });
      add_continuous_number(
          post, "post_process.ambient_occlusion_radius",
          localized("ui.inspector.ambient_occlusion_radius", "AO Radius"),
          settings->post_process.ambient_occlusion_radius, 0.0, 10.0, 0.01, 3,
          [this](double value, std::string merge_key) {
            apply_scene_settings(
                [value](QuaderSceneSettings &next) {
                  next.post_process.ambient_occlusion_radius =
                      static_cast<float>(value);
                },
                std::move(merge_key));
          });
    }
    set_field_enabled("post_process.ambient_occlusion_intensity",
                      settings->post_process.ambient_occlusion);
    set_field_enabled("post_process.ambient_occlusion_radius",
                      settings->post_process.ambient_occlusion);
    add_checkbox(post, "post_process.bloom",
                 localized("ui.inspector.bloom", "Bloom"),
                 settings->post_process.bloom, [this](bool enabled) {
                   apply_scene_settings([enabled](QuaderSceneSettings &next) {
                     next.post_process.bloom = enabled;
                   });
                 });
    add_continuous_number(
        post, "post_process.bloom_strength",
        localized("ui.inspector.bloom_strength", "Bloom Strength"),
        settings->post_process.bloom_strength, 0.0, 1.0, 0.01, 3,
        [this](double value, std::string merge_key) {
          apply_scene_settings(
              [value](QuaderSceneSettings &next) {
                next.post_process.bloom_strength = static_cast<float>(value);
              },
              std::move(merge_key));
        });
  }

private:
  friend class QtEnvironmentSettingsPopup;

  QtEnvironmentSettingsPopup &panel_;
  QTreeWidget *tree_ = nullptr;
  QuaderEditorApi *editor_ = nullptr;
  std::optional<QuaderSceneDocumentBinding> document_binding_;
  const SceneDocument *document_ = nullptr;
  LocalizationService *localization_ = nullptr;
  QtUiScaleMetrics ui_scale_metrics_;
  QuaderSceneSettings presented_settings_;
  std::unordered_map<std::string, QTreeWidgetItem *> sections_;
  std::unordered_map<std::string, QTreeWidgetItem *> rows_;
  UIContinuousEditTracker active_continuous_edit_;
  QPointer<QColorDialog> active_color_dialog_;
  std::string active_continuous_id_;
  const LocalizationService *presented_localization_ = nullptr;
  std::uint64_t presented_locale_revision_ = 0;
  std::uint64_t runtime_binding_generation_ = 0;
  std::uint64_t presented_revision_ = 0;
  std::uint64_t presentation_update_count_ = 0;
  bool rebuilding_ = false;
  bool force_refresh_ = true;
  bool continuous_edit_active_ = false;
};

QtEnvironmentSettingsPopup::QtEnvironmentSettingsPopup(QWidget *parent)
    : QFrame(parent), impl_(std::make_unique<Impl>(*this)) {}
QtEnvironmentSettingsPopup::~QtEnvironmentSettingsPopup() {
  impl_->abandon_runtime();
}
void QtEnvironmentSettingsPopup::refresh(UIContext &context) {
  refresh(context.intent.editor, context.scene.document, &context.localization);
}
void QtEnvironmentSettingsPopup::refresh(QuaderEditorApi &editor,
                                         const SceneDocument &document,
                                         LocalizationService *localization) {
  impl_->refresh(editor, document, localization);
}
void QtEnvironmentSettingsPopup::apply_ui_scale(
    const QtUiScaleMetrics &metrics) {
  if (impl_->ui_scale_metrics_.revision() == metrics.revision() &&
      qFuzzyCompare(impl_->ui_scale_metrics_.factor(), metrics.factor())) {
    return;
  }
  impl_->ui_scale_metrics_ = metrics;
  impl_->tree_->header()->setMinimumSectionSize(metrics.px(64));
  impl_->tree_->header()->resizeSection(0, metrics.px(170));
}
void QtEnvironmentSettingsPopup::show_below(QWidget &anchor) {
  if (!impl_->editor_ || !impl_->document_) {
    return;
  }
  impl_->refresh(*impl_->editor_, *impl_->document_, impl_->localization_);
  QPoint position = anchor.mapToGlobal(QPoint(0, anchor.height()));
  QScreen *screen = QGuiApplication::screenAt(position);
  if (!screen) {
    screen = anchor.screen();
  }
  const QRect available =
      screen ? screen->availableGeometry() : QRect(position, QSize(480, 600));
  resize(std::min(impl_->ui_scale_metrics_.px(460), available.width()),
         std::min(impl_->ui_scale_metrics_.px(600), available.height()));
  position.setX(
      std::clamp(position.x(), available.left(),
                 std::max(available.left(), available.right() - width() + 1)));
  if (position.y() + height() - 1 > available.bottom()) {
    position.setY(anchor.mapToGlobal(QPoint(0, 0)).y() - height());
  }
  position.setY(
      std::clamp(position.y(), available.top(),
                 std::max(available.top(), available.bottom() - height() + 1)));
  move(position);
  show();
  raise();
  impl_->tree_->setFocus(Qt::PopupFocusReason);
}
void QtEnvironmentSettingsPopup::abandon_runtime() noexcept {
  impl_->abandon_runtime();
}
std::uint64_t QtEnvironmentSettingsPopup::presentation_update_count() const {
  return impl_->presentation_update_count_;
}
void QtEnvironmentSettingsPopup::keyPressEvent(QKeyEvent *event) {
  if (event && event->key() == Qt::Key_Escape) {
    hide();
    event->accept();
    return;
  }
  QFrame::keyPressEvent(event);
}
void QtEnvironmentSettingsPopup::hideEvent(QHideEvent *event) {
  QPointer<QtEnvironmentSettingsPopup> guard(this);
  impl_->settle_interaction();
  if (guard) {
    QFrame::hideEvent(event);
  }
}
} // namespace quader
