////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/session/scene_session.hpp"
#include "scene/light_properties.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/qt_environment_settings_popup.hpp"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_color_swatch_button.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QSlider>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <string_view>
#include <utility>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace quader {
namespace {

LocalizationService inspector_localization() {
  LocalizationService localization;
  EXPECT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
      "en-US"));
  return localization;
}

QTreeWidgetItem *find_top_level_item(QTreeWidget &tree,
                                     const QString &stable_id) {
  for (int index = 0; index < tree.topLevelItemCount(); ++index) {
    QTreeWidgetItem *item = tree.topLevelItem(index);
    if (item != nullptr &&
        item->data(0, Qt::UserRole).toString() == stable_id) {
      return item;
    }
  }
  return nullptr;
}

QtMaterialPresentationItem material_item(QuaderMaterialReference reference,
                                          QString stable_id, QString name,
                                          bool editable) {
  return {
      .reference = reference,
      .stable_id = std::move(stable_id),
      .name = std::move(name),
      .fallback_color = QColor::fromRgb(93, 137, 181),
      .available = true,
      .editable = editable,
  };
}

void process_inspector_events() {
  QCoreApplication::processEvents();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void expect_inspector_color(QVec3 actual, QVec3 expected) {
  EXPECT_FLOAT_EQ(actual.x, expected.x);
  EXPECT_FLOAT_EQ(actual.y, expected.y);
  EXPECT_FLOAT_EQ(actual.z, expected.z);
}

/// Runs the real retained panel against a public-API selected-light scene.
template <typename Panel>
struct InspectorScenario {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = inspector_localization();
  UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };
  Panel panel;

  InspectorScenario() {
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.resize(360, 780);
    panel.show();
  }

  void refresh() {
    panel.refresh(context);
    process_inspector_events();
  }

  QDoubleSpinBox *range_field() {
    return panel.findChild<QDoubleSpinBox *>(
        QStringLiteral("inspector.light.range"));
  }
};

using InspectorLightScenario = InspectorScenario<InspectorPanel>;
using EnvironmentSettingsScenario = InspectorScenario<QtEnvironmentSettingsPopup>;

TEST(InspectorMaterialTests, LightControlsRetainTemperatureEdits) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-light-controls";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  InspectorLightScenario scenario;
  QuaderCreatePointLightArgs args;
  args.select_created = true;
  args.light.intensity = 5.0f;
  args.light.light_unit = QuaderLightUnit::Candela;
  args.light.distance_attenuation = -1.0f;
  const auto created = scenario.editor.scene().create_point_light(args);
  ASSERT_TRUE(created.ok()) << created.error_message();
  scenario.refresh();
  const auto number = [&scenario](const char *name) {
    return scenario.panel.findChild<QDoubleSpinBox *>(QString::fromLatin1(name));
  };
  const auto combo = [&scenario](const char *name) {
    return scenario.panel.findChild<QComboBox *>(QString::fromLatin1(name));
  };
  const auto activate = [](QComboBox *field, int value) {
    ASSERT_NE(field, nullptr);
    const int index = field->findData(value);
    ASSERT_GE(index, 0);
    field->setCurrentIndex(index);
    field->activated(index);
  };
  auto *falloff = number("inspector.light.distance_attenuation");
  ASSERT_NE(falloff, nullptr);
  EXPECT_DOUBLE_EQ(falloff->value(), -1.0);
  falloff->setValue(1.5);
  falloff->editingFinished();
  scenario.refresh();
  EXPECT_FLOAT_EQ(scenario.session.scene().light(created.value().id)
                      ->distance_attenuation, 1.5f);

  QPointer<QDoubleSpinBox> temperature = number("inspector.light.color_temperature_kelvin");
  QPointer<QPushButton> color = scenario.panel.findChild<QPushButton *>(
      QStringLiteral("inspector.light.color"));
  QPointer<QComboBox> preset = combo("inspector.light.source_preset");
  ASSERT_NE(temperature, nullptr);
  ASSERT_NE(color, nullptr);
  ASSERT_NE(preset, nullptr);
  EXPECT_FALSE(temperature->isEnabled());
  EXPECT_TRUE(color->isEnabled());
  activate(preset, static_cast<int>(LightSourcePreset::Halogen80W));
  scenario.refresh();
  ASSERT_NE(temperature, nullptr);
  ASSERT_NE(preset, nullptr);
  EXPECT_DOUBLE_EQ(temperature->value(), 2900.0);
  EXPECT_TRUE(temperature->isEnabled());
  EXPECT_FALSE(color->isEnabled());
  EXPECT_EQ(scenario.session.scene().light(created.value().id)->light_unit,
            LightUnit::Candela);
  EXPECT_TRUE(scenario.panel.findChildren<QSlider *>().empty());
  ASSERT_NE(dynamic_cast<QtDoubleSpinBox *>(temperature.data()), nullptr);
  const auto temperature_history = scenario.session.history().undo_count();
  temperature->setValue(12000.0);
  temperature->setValue(temperature->maximum());
  scenario.refresh();
  ASSERT_NE(temperature, nullptr);
  EXPECT_DOUBLE_EQ(temperature->value(), 15000.0);
  EXPECT_EQ(preset->currentData().toInt(), static_cast<int>(LightSourcePreset::Custom));
  const LightComponent *light = scenario.session.scene().light(created.value().id);
  const QVec3 expected_color = light_color_temperature_rgb(15000.0f);
  expect_inspector_color(light->color, expected_color);
  const QVec3 display = light_linear_to_srgb(expected_color);
  EXPECT_EQ(static_cast<QtColorSwatchButton *>(color.data())->swatch_color(),
            QColor::fromRgbF(display.x, display.y, display.z));
  temperature->editingFinished();
  scenario.refresh();
  ASSERT_NE(temperature, nullptr);
  EXPECT_EQ(scenario.session.history().undo_count(), temperature_history + 1u);
  ASSERT_TRUE(scenario.editor.history().undo().ok());
  scenario.refresh();
  ASSERT_NE(temperature, nullptr);
  EXPECT_DOUBLE_EQ(temperature->value(), 2900.0);
  ASSERT_TRUE(scenario.editor.history().redo().ok());
  scenario.refresh();
  ASSERT_NE(temperature, nullptr);
  EXPECT_DOUBLE_EQ(temperature->value(), 15000.0);
  activate(combo("inspector.light.color_mode"), static_cast<int>(LightColorMode::Color));
  scenario.refresh();
  ASSERT_NE(temperature, nullptr);
  EXPECT_FALSE(temperature->isEnabled());
  EXPECT_TRUE(color->isEnabled());
  expect_inspector_color(scenario.session.scene().light(created.value().id)->color,
                          expected_color);

  auto *intensity = number("inspector.light.intensity");
  ASSERT_NE(intensity, nullptr);
  intensity->setValue(48.0);
  intensity->editingFinished();
  activate(combo("inspector.light.type"), static_cast<int>(LightType::Directional));
  scenario.refresh();
  EXPECT_EQ(combo("inspector.light.light_unit"), nullptr);
  EXPECT_EQ(number("inspector.light.brightness"), nullptr);
  EXPECT_EQ(number("inspector.light.candela"), nullptr);
  ASSERT_NE(number("inspector.light.intensity"), nullptr);
  EXPECT_DOUBLE_EQ(number("inspector.light.intensity")->value(), 48.0);
  EXPECT_EQ(scenario.panel.findChild<QSlider *>(
                QStringLiteral("inspector.light.intensity.slider")), nullptr);
  EXPECT_EQ(number("inspector.light.range"), nullptr);
}

TEST(InspectorMaterialTests, AllLightShapesUseUncappedNumericIntensityAndRetainControls) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-simple-light-intensity";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  InspectorLightScenario scenario;
  QuaderCreatePointLightArgs args;
  args.select_created = true;
  const auto created = scenario.editor.scene().create_point_light(args);
  ASSERT_TRUE(created.ok()) << created.error_message();

  for (const QuaderLightType shape : {QuaderLightType::Directional,
                                      QuaderLightType::Point,
                                      QuaderLightType::Spot}) {
    SCOPED_TRACE(static_cast<int>(shape));
    auto legacy = to_api_light_component(
        *scenario.session.scene().light(created.value().id));
    legacy.type = shape;
    legacy.intensity = 128.0f;
    legacy.light_unit = QuaderLightUnit::Candela;
    ASSERT_TRUE(scenario.editor.scene().set_light_component(created.value().id, legacy).ok());
    const std::size_t legacy_undo_count = scenario.session.history().undo_count();
    scenario.refresh();
    QPointer<QDoubleSpinBox> intensity = scenario.panel.findChild<QDoubleSpinBox *>(
        QStringLiteral("inspector.light.intensity"));
    ASSERT_NE(intensity, nullptr);
    EXPECT_EQ(scenario.panel.findChild<QSlider *>(
                  QStringLiteral("inspector.light.intensity.slider")), nullptr);
    EXPECT_DOUBLE_EQ(intensity->value(), 128.0);
    EXPECT_DOUBLE_EQ(intensity->minimum(), 0.0);
    EXPECT_DOUBLE_EQ(intensity->maximum(), std::numeric_limits<float>::max());
    EXPECT_DOUBLE_EQ(intensity->singleStep(), 0.01);
    EXPECT_EQ(scenario.panel.findChild<QComboBox *>(
                  QStringLiteral("inspector.light.light_unit")), nullptr);
    EXPECT_EQ(scenario.panel.findChild<QDoubleSpinBox *>(
                  QStringLiteral("inspector.light.brightness")), nullptr);
    EXPECT_EQ(scenario.panel.findChild<QDoubleSpinBox *>(
                  QStringLiteral("inspector.light.candela")), nullptr);
    auto *tree = scenario.panel.findChild<QTreeWidget *>(
        QStringLiteral("inspector.tree"));
    ASSERT_NE(tree, nullptr);
    QTreeWidgetItem *section = find_top_level_item(*tree, QStringLiteral("light"));
    ASSERT_NE(section, nullptr);
    for (int index = 0; index < section->childCount(); ++index) {
      EXPECT_FALSE(section->child(index)->toolTip(0).isEmpty())
          << section->child(index)->text(0).toStdString();
    }
    QTreeWidgetItem *intensity_row = nullptr;
    for (int index = 0; index < section->childCount(); ++index) {
      if (section->child(index)->data(0, Qt::UserRole).toString() ==
          QStringLiteral("inspector.light.intensity")) {
        intensity_row = section->child(index);
        break;
      }
    }
    ASSERT_NE(intensity_row, nullptr);
    EXPECT_EQ(intensity_row->text(0), QStringLiteral("Intensity"));
    auto *preset = scenario.panel.findChild<QComboBox *>(
        QStringLiteral("inspector.light.source_preset"));
    ASSERT_NE(preset, nullptr);
    EXPECT_EQ(preset->toolTip(), QString::fromStdString(
        scenario.localization.text("ui.inspector.source_type_tooltip", "")));
    intensity->editingFinished();
    scenario.refresh();
    EXPECT_EQ(scenario.session.history().undo_count(), legacy_undo_count);
    EXPECT_FLOAT_EQ(scenario.session.scene().light(created.value().id)->intensity, 128.0f);

    // Historical unit metadata neither changes display values nor invalidates rows.
    legacy.light_unit = QuaderLightUnit::Lumens;
    ASSERT_TRUE(scenario.editor.scene().set_light_component(created.value().id, legacy).ok());
    scenario.refresh();
    ASSERT_NE(intensity, nullptr);
    EXPECT_DOUBLE_EQ(intensity->value(), 128.0);
    for (const float value : {150000.0f, 100000000.0f, 0.0f, 0.25f, 16.0f}) {
      SCOPED_TRACE(value);
      const float before = scenario.session.scene().light(created.value().id)->intensity;
      intensity->setValue(value);
      intensity->editingFinished();
      scenario.refresh();
      ASSERT_NE(intensity, nullptr);
      EXPECT_DOUBLE_EQ(intensity->value(), static_cast<double>(value));
      EXPECT_FLOAT_EQ(scenario.session.scene().light(created.value().id)->intensity, value);
      EXPECT_EQ(scenario.session.scene().light(created.value().id)->light_unit,
                LightUnit::Lumens);
      const std::size_t edited_undo_count = scenario.session.history().undo_count();
      intensity->editingFinished();
      scenario.refresh();
      EXPECT_EQ(scenario.session.history().undo_count(), edited_undo_count);
      ASSERT_TRUE(scenario.editor.history().undo().ok());
      scenario.refresh();
      ASSERT_NE(intensity, nullptr);
      EXPECT_DOUBLE_EQ(intensity->value(), static_cast<double>(before));
      EXPECT_FLOAT_EQ(scenario.session.scene().light(created.value().id)->intensity, before);
      ASSERT_TRUE(scenario.editor.history().redo().ok());
      scenario.refresh();
      ASSERT_NE(intensity, nullptr);
      EXPECT_DOUBLE_EQ(intensity->value(), static_cast<double>(value));
      EXPECT_FLOAT_EQ(scenario.session.scene().light(created.value().id)->intensity, value);
    }
    EXPECT_EQ(scenario.panel.findChild<QDoubleSpinBox *>(
                  QStringLiteral("inspector.light.intensity")), intensity.data());
  }
}

TEST(EnvironmentSettingsPopupTests, ToneMappingOffersAgxAndDisplaysLegacyValuesWithoutMutation) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-tone-mapping";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  EnvironmentSettingsScenario scenario;
  scenario.refresh();
  const auto tone_combo = [&]() {
    return scenario.panel.findChild<QComboBox *>(
        QStringLiteral("environmentSettings.post_process.tone_mapping"));
  };
  QComboBox *tone = tone_combo();
  ASSERT_NE(tone, nullptr);
  EXPECT_EQ(tone->count(), 4);
  EXPECT_EQ(tone->currentData().toInt(), static_cast<int>(SceneToneMapping::Linear));
  EXPECT_GE(tone->findData(static_cast<int>(SceneToneMapping::Linear)), 0);
  EXPECT_GE(tone->findData(static_cast<int>(SceneToneMapping::Aces)), 0);
  EXPECT_GE(tone->findData(static_cast<int>(SceneToneMapping::Filmic)), 0);
  const int agx_index = tone->findData(static_cast<int>(SceneToneMapping::Agx));
  ASSERT_GE(agx_index, 0);
  EXPECT_EQ(tone->itemText(agx_index), QStringLiteral("AgX"));
  EXPECT_EQ(tone->findData(static_cast<int>(SceneToneMapping::AcesLegacy)), -1);
  EXPECT_EQ(tone->findData(static_cast<int>(SceneToneMapping::DisplayRange)), -1);
  tone->setCurrentIndex(agx_index);
  tone->activated(agx_index);
  EXPECT_EQ(scenario.session.scene().scene_settings().post_process.tone_mapping,
            SceneToneMapping::Agx);
  ASSERT_TRUE(scenario.editor.history().undo().ok());
  scenario.refresh();
  ASSERT_NE(tone_combo(), nullptr);
  EXPECT_EQ(tone_combo()->currentData().toInt(), static_cast<int>(SceneToneMapping::Linear));
  ASSERT_TRUE(scenario.editor.history().redo().ok());
  scenario.refresh();
  ASSERT_NE(tone_combo(), nullptr);
  EXPECT_EQ(tone_combo()->currentData().toInt(), static_cast<int>(SceneToneMapping::Agx));

  for (const SceneToneMapping legacy : {SceneToneMapping::AcesLegacy,
                                        SceneToneMapping::DisplayRange}) {
    auto settings = scenario.session.scene().scene_settings();
    settings.post_process.tone_mapping = legacy;
    ASSERT_TRUE(scenario.editor.scene().set_scene_settings(
        to_api_scene_settings(settings)).ok());
    scenario.session.mark_clean();
    const std::size_t undo_count = scenario.session.history().undo_count();
    scenario.refresh();
    tone = tone_combo();
    ASSERT_NE(tone, nullptr);
    EXPECT_EQ(tone->currentData().toInt(), static_cast<int>(SceneToneMapping::Aces));
    EXPECT_EQ(scenario.session.scene().scene_settings().post_process.tone_mapping,
              legacy);
    EXPECT_EQ(scenario.session.history().undo_count(), undo_count);
    EXPECT_FALSE(scenario.session.dirty());
    scenario.refresh();
    EXPECT_EQ(scenario.session.history().undo_count(), undo_count);
    EXPECT_EQ(scenario.session.scene().scene_settings().post_process.tone_mapping,
              legacy);
    const int aces_index = tone->currentIndex();
    tone->activated(aces_index);
    EXPECT_EQ(scenario.session.scene().scene_settings().post_process.tone_mapping,
              SceneToneMapping::Aces);
    EXPECT_TRUE(scenario.session.dirty());
    ASSERT_TRUE(scenario.editor.history().undo().ok());
    EXPECT_EQ(scenario.session.scene().scene_settings().post_process.tone_mapping,
              legacy);
  }
}

TEST(InspectorMaterialTests, LightColorDialogPreviewsCancelsAndCommitsOneUndoStep) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-light-live-color";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  InspectorLightScenario scenario;
  QuaderCreatePointLightArgs args;
  args.select_created = true;
  args.light.color = {0.2f, 0.3f, 0.4f};
  const auto created = scenario.editor.scene().create_point_light(args);
  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(scenario.editor.scene().rename(created.value().id, "Redo marker").ok());
  ASSERT_TRUE(scenario.editor.history().undo().ok());
  scenario.refresh();
  const LightComponent baseline = *scenario.session.scene().light(created.value().id);
  const std::string initial_name =
      scenario.session.scene().name(created.value().id)->name;
  const std::size_t undo_count = scenario.session.history().undo_count();
  const std::size_t redo_count = scenario.session.history().redo_count();
  ASSERT_GT(redo_count, 0u);
  const std::string redo_label = *scenario.session.history().redo_label();
  const auto run_dialog = [&](bool cancel) {
    QPointer<QPushButton> color = scenario.panel.findChild<QPushButton *>(
        QStringLiteral("inspector.light.color"));
    ASSERT_NE(color, nullptr);
    QTimer::singleShot(0, &scenario.panel, [&]() {
      auto *dialog = scenario.panel.findChild<QColorDialog *>(
          QStringLiteral("inspector.light.color.dialog"));
      EXPECT_NE(dialog, nullptr);
      if (dialog == nullptr) {
        scenario.panel.abandon_runtime();
        return;
      }
      dialog->setCurrentColor(QColor::fromRgbF(0.8, 0.2, 0.1));
      scenario.refresh();
      EXPECT_NE(color, nullptr);
      // The dialog's integer channel controls normalize the requested RGB.
      const QColor displayed = dialog->currentColor();
      const QVec3 expected = light_srgb_to_linear(
          {static_cast<float>(displayed.redF()),
           static_cast<float>(displayed.greenF()),
           static_cast<float>(displayed.blueF())});
      const QVec3 current = scenario.session.scene().light(created.value().id)->color;
      EXPECT_NEAR(current.x, expected.x, 0.0001);
      EXPECT_NEAR(current.y, expected.y, 0.0001);
      EXPECT_NEAR(current.z, expected.z, 0.0001);
      EXPECT_FALSE(scenario.editor.history().can_redo());
      EXPECT_EQ(scenario.session.history().undo_count(), undo_count);
      EXPECT_EQ(scenario.session.history().redo_count(), redo_count);
      EXPECT_NE(scenario.session.history().redo_label(), nullptr);
      if (scenario.session.history().redo_label() != nullptr) {
        EXPECT_EQ(*scenario.session.history().redo_label(), redo_label);
      }
      dialog->setCurrentColor(QColor::fromRgbF(0.1, 0.9, 0.5));
      if (cancel) {
        dialog->reject();
      } else {
        dialog->accept();
      }
    });
    color->click();
    scenario.refresh();
  };
  run_dialog(true);
  EXPECT_EQ(*scenario.session.scene().light(created.value().id), baseline);
  EXPECT_TRUE(scenario.editor.history().can_redo());
  EXPECT_EQ(scenario.session.history().redo_count(), redo_count);
  ASSERT_TRUE(scenario.editor.history().redo().ok());
  EXPECT_EQ(scenario.session.scene().name(created.value().id)->name, "Redo marker");
  ASSERT_TRUE(scenario.editor.history().undo().ok());
  EXPECT_EQ(scenario.session.scene().name(created.value().id)->name, initial_name);
  EXPECT_EQ(scenario.session.history().redo_count(), redo_count);
  run_dialog(false);
  const LightComponent accepted = *scenario.session.scene().light(created.value().id);
  EXPECT_NE(accepted.color.x, baseline.color.x);
  EXPECT_FALSE(scenario.editor.history().can_redo());
  ASSERT_TRUE(scenario.editor.history().undo().ok());
  EXPECT_EQ(*scenario.session.scene().light(created.value().id), baseline);
  ASSERT_TRUE(scenario.editor.history().redo().ok());
  EXPECT_EQ(*scenario.session.scene().light(created.value().id), accepted);
}

TEST(EnvironmentSettingsPopupTests, EnvironmentColorDialogSurvivesSelectionChangeAndCancelsExplicitly) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-environment-live-color";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  EnvironmentSettingsScenario scenario;
  const auto selected = scenario.editor.scene().create_empty();
  ASSERT_TRUE(selected.ok());
  scenario.refresh();
  const QVec3 baseline = scenario.session.scene().scene_settings().environment.clear_color;
  auto *color = scenario.panel.findChild<QPushButton *>(
      QStringLiteral("environmentSettings.environment.clear_color"));
  ASSERT_NE(color, nullptr);
  QTimer::singleShot(0, &scenario.panel, [&]() {
    auto *dialog = scenario.panel.findChild<QColorDialog *>(
        QStringLiteral("environmentSettings.environment.clear_color.dialog"));
    EXPECT_NE(dialog, nullptr);
    if (dialog == nullptr) {
      scenario.panel.abandon_runtime();
      return;
    }
    dialog->setCurrentColor(QColor::fromRgbF(0.7, 0.2, 0.3));
    scenario.refresh();
    EXPECT_NE(scenario.session.scene().scene_settings().environment.clear_color.x,
              baseline.x);
    EXPECT_NE(scenario.panel.findChild<QPushButton *>(
                  QStringLiteral("environmentSettings.environment.clear_color")), nullptr);
    EXPECT_TRUE(scenario.editor.scene().select({}).ok());
    scenario.refresh();
    EXPECT_TRUE(dialog->isVisible());
    EXPECT_NE(scenario.session.scene().scene_settings().environment.clear_color.x, baseline.x);
    dialog->reject();
  });
  color->click();
  expect_inspector_color(
      scenario.session.scene().scene_settings().environment.clear_color,
      baseline);
  EXPECT_TRUE(scenario.session.scene().selected_entities().empty());
}

TEST(EnvironmentSettingsPopupTests, FogSectionEditsRetainGesturesAndCollapseAcrossLocaleChanges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-fog-controls";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  apply_qt_obsidian_application_theme(QApplication::style()->standardPalette());
  application.setQuitOnLastWindowClosed(false);
  EnvironmentSettingsScenario scenario;
  scenario.refresh();
  auto *tree = scenario.panel.findChild<QTreeWidget *>(QStringLiteral("environmentSettings.tree"));
  ASSERT_NE(tree, nullptr);
  auto *fog = find_top_level_item(*tree, QStringLiteral("fog"));
  ASSERT_NE(fog, nullptr);
  EXPECT_TRUE(fog->isExpanded());
  EXPECT_EQ(fog->text(0), QStringLiteral("Fog"));
  EXPECT_EQ(fog->childCount(), 11);
  EXPECT_EQ(tree->indexOfTopLevelItem(fog),
            tree->indexOfTopLevelItem(find_top_level_item(*tree, QStringLiteral("environment"))) + 1);
  EXPECT_EQ(tree->indexOfTopLevelItem(find_top_level_item(*tree, QStringLiteral("camera"))),
            tree->indexOfTopLevelItem(fog) + 1);
  const auto number = [&](const char *id) {
    return scenario.panel.findChild<QDoubleSpinBox *>(
        QStringLiteral("environmentSettings.fog.") + QString::fromLatin1(id));
  };
  for (const char *id : {"density", "distance", "cut_off_distance", "maximum_opacity",
                         "height", "height_falloff", "in_scattering_size", "in_scattering_start"}) {
    auto *field = number(id);
    ASSERT_NE(field, nullptr) << id;
    EXPECT_FALSE(field->isEnabled()) << id;
    EXPECT_FALSE(field->toolTip().isEmpty()) << id;
  }
  EXPECT_EQ(number("cut_off_distance")->text(), QStringLiteral("Unlimited"));
  EXPECT_EQ(number("in_scattering_size")->text(), QStringLiteral("Disabled"));
  auto *enabled = scenario.panel.findChild<QCheckBox *>(QStringLiteral("environmentSettings.fog.enabled"));
  ASSERT_NE(enabled, nullptr);
  EXPECT_TRUE(enabled->isEnabled());
  EXPECT_FALSE(enabled->isChecked());
  fog->setExpanded(false);
  enabled->setChecked(true);
  scenario.refresh();
  fog = find_top_level_item(*tree, QStringLiteral("fog"));
  ASSERT_NE(fog, nullptr);
  EXPECT_FALSE(fog->isExpanded());
  EXPECT_TRUE(scenario.session.scene().scene_settings().fog.enabled);
  fog->setExpanded(true);
  EXPECT_TRUE(number("density")->isEnabled());
  EXPECT_FALSE(number("in_scattering_start")->isEnabled());
  const auto sentinel_settings = scenario.session.scene().scene_settings().fog;
  const auto sentinel_history = scenario.session.history().undo_count();
  const auto sentinel_revision = scenario.session.scene().revision();
  for (const char *id : {"cut_off_distance", "in_scattering_size"}) {
    auto *field = number(id);
    const QString sentinel = field->text();
    field->interpretText();
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(field, &enter);
    field->clearFocus();
    EXPECT_EQ(field->text(), sentinel);
  }
  scenario.refresh();
  EXPECT_EQ(scenario.session.scene().scene_settings().fog, sentinel_settings);
  EXPECT_FLOAT_EQ(scenario.session.scene().scene_settings().fog.in_scattering_size, -1.0f);
  EXPECT_EQ(scenario.session.history().undo_count(), sentinel_history);
  EXPECT_EQ(scenario.session.scene().revision(), sentinel_revision);

  QPointer<QDoubleSpinBox> density = number("density");
  density->setToolTip(QStringLiteral("retained fog field"));
  const auto revision = scenario.session.scene().revision();
  const auto history = scenario.session.history().undo_count();
  scenario.refresh();
  ASSERT_NE(density, nullptr);
  EXPECT_EQ(number("density"), density.data());
  EXPECT_EQ(density->toolTip(), QStringLiteral("retained fog field"));
  EXPECT_EQ(scenario.session.scene().revision(), revision);
  density->setValue(0.02);
  scenario.refresh();
  ASSERT_NE(density, nullptr);
  EXPECT_EQ(number("density"), density.data());
  density->setValue(0.032);
  scenario.refresh();
  ASSERT_NE(density, nullptr);
  EXPECT_EQ(scenario.session.history().undo_count(), history + 1u);
  density->editingFinished();
  scenario.refresh();
  EXPECT_FLOAT_EQ(scenario.session.scene().scene_settings().fog.density, 0.032f);
  ASSERT_TRUE(scenario.editor.history().undo().ok());
  scenario.refresh();
  EXPECT_FLOAT_EQ(scenario.session.scene().scene_settings().fog.density, 0.1f);
  ASSERT_TRUE(scenario.editor.history().redo().ok());
  scenario.refresh();
  EXPECT_NEAR(number("density")->value(), 0.032, 0.000001);

  struct FogNumberEdit {
    const char *id;
    float SceneFogSettings::*member;
    float value;
  };
  for (const FogNumberEdit &edit : {
           FogNumberEdit{"distance", &SceneFogSettings::distance, 2.5f},
           FogNumberEdit{"cut_off_distance", &SceneFogSettings::cut_off_distance, 150.0f},
           FogNumberEdit{"maximum_opacity", &SceneFogSettings::maximum_opacity, 0.65f},
           FogNumberEdit{"height", &SceneFogSettings::height, -3.0f},
           FogNumberEdit{"height_falloff", &SceneFogSettings::height_falloff, 0.035f},
           FogNumberEdit{"in_scattering_size", &SceneFogSettings::in_scattering_size, 32.0f},
           FogNumberEdit{"in_scattering_start", &SceneFogSettings::in_scattering_start, 5.0f}}) {
    auto *field = number(edit.id);
    ASSERT_NE(field, nullptr);
    EXPECT_TRUE(field->isEnabled()) << edit.id;
    field->setValue(edit.value);
    if (std::string_view(edit.id) == "in_scattering_size") {
      EXPECT_TRUE(number("in_scattering_start")->isEnabled());
    }
    field->editingFinished();
    scenario.refresh();
    EXPECT_FLOAT_EQ(scenario.session.scene().scene_settings().fog.*(edit.member), edit.value);
  }
  auto *scattering_size = number("in_scattering_size");
  scattering_size->setValue(0.0);
  EXPECT_FALSE(number("in_scattering_start")->isEnabled());
  scattering_size->editingFinished();
  scenario.refresh();
  EXPECT_FLOAT_EQ(scenario.session.scene().scene_settings().fog.in_scattering_size, 0.0f);
  EXPECT_EQ(number("in_scattering_size")->text(), QStringLiteral("Disabled"));
  ASSERT_TRUE(scenario.editor.history().undo().ok());
  scenario.refresh();
  EXPECT_FLOAT_EQ(scenario.session.scene().scene_settings().fog.in_scattering_size, 32.0f);
  EXPECT_TRUE(number("in_scattering_start")->isEnabled());
  auto *environment = scenario.panel.findChild<QCheckBox *>(
      QStringLiteral("environmentSettings.fog.color_from_environment"));
  ASSERT_NE(environment, nullptr);
  environment->setChecked(true);
  scenario.refresh();
  EXPECT_TRUE(scenario.session.scene().scene_settings().fog.color_from_environment);

  auto settings = to_api_scene_settings(scenario.session.scene().scene_settings());
  settings.fog.density = 0.000001234567f;
  ASSERT_TRUE(scenario.editor.scene().set_scene_settings(settings).ok());
  scenario.refresh();
  const auto precise_history = scenario.session.history().undo_count();
  auto *precise = number("density");
  ASSERT_NE(precise, nullptr);
  EXPECT_NEAR(precise->value(), settings.fog.density, 0.000000000001);
  bool display_valid = false;
  EXPECT_GT(precise->locale().toDouble(precise->text(), &display_valid), 0.0);
  EXPECT_TRUE(display_valid);
  EXPECT_TRUE(precise->text().contains(QLatin1Char('e'), Qt::CaseInsensitive));
  precise->interpretText();
  precise->editingFinished();
  scenario.refresh();
  EXPECT_FLOAT_EQ(scenario.session.scene().scene_settings().fog.density, settings.fog.density);
  EXPECT_EQ(scenario.session.history().undo_count(), precise_history);
  precise = number("density");
  auto *density_text = precise->findChild<QLineEdit *>();
  ASSERT_NE(density_text, nullptr);
  const QString typed_density = precise->locale().toString(0.0000025, 'g', 4);
  density_text->setText(typed_density);
  density_text->textEdited(typed_density);
  precise->interpretText();
  precise->editingFinished();
  scenario.refresh();
  EXPECT_FLOAT_EQ(scenario.session.scene().scene_settings().fog.density, 0.0000025f);
  EXPECT_GT(number("density")->locale().toDouble(number("density")->text()), 0.0);

  fog = find_top_level_item(*tree, QStringLiteral("fog"));
  ASSERT_NE(fog, nullptr);
  fog->setExpanded(false);
  ASSERT_FALSE(fog->isExpanded());
  ASSERT_TRUE(scenario.localization.select_locale("it-IT"));
  scenario.refresh();
  fog = find_top_level_item(*tree, QStringLiteral("fog"));
  ASSERT_NE(fog, nullptr);
  EXPECT_FALSE(fog->isExpanded());
  EXPECT_EQ(fog->text(0), QStringLiteral("Nebbia"));
  EXPECT_TRUE(number("density")->toolTip().contains(QStringLiteral("Estinzione")));

  const QString screenshot = qEnvironmentVariable("QUADER_FOG_INSPECTOR_SCREENSHOT");
  if (!screenshot.isEmpty()) {
    ASSERT_TRUE(scenario.localization.select_locale("en-US"));
    scenario.refresh();
    for (int row = 0; row < tree->topLevelItemCount(); ++row) {
      auto *section = tree->topLevelItem(row);
      section->setExpanded(section->data(0, Qt::UserRole).toString() == QStringLiteral("fog"));
    }
    process_inspector_events();
    EXPECT_TRUE(scenario.panel.grab().save(screenshot));
  }
}

TEST(EnvironmentSettingsPopupTests, FogColorPreviewUsesLinearColorAndCancelPreservesHistory) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-fog-live-color";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  EnvironmentSettingsScenario scenario;
  auto settings = to_api_scene_settings(scenario.session.scene().scene_settings());
  settings.fog.enabled = true;
  settings.fog.color = {0.2f, 0.3f, 0.4f};
  ASSERT_TRUE(scenario.editor.scene().set_scene_settings(settings).ok());
  scenario.refresh();
  scenario.session.mark_clean();
  const auto baseline = scenario.session.scene().scene_settings().fog;
  const auto history = scenario.session.history().undo_count();
  const auto run_dialog = [&](bool cancel) {
    QPointer<QPushButton> button = scenario.panel.findChild<QPushButton *>(
        QStringLiteral("environmentSettings.fog.color"));
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(button->isEnabled());
    QTimer::singleShot(0, &scenario.panel, [&]() {
      auto *dialog = scenario.panel.findChild<QColorDialog *>(
          QStringLiteral("environmentSettings.fog.color.dialog"));
      EXPECT_NE(dialog, nullptr);
      if (dialog == nullptr) {
        scenario.panel.abandon_runtime();
        return;
      }
      dialog->setCurrentColor(QColor::fromRgbF(0.75, 0.25, 0.5));
      const QColor displayed = dialog->currentColor();
      const QVec3 expected = light_srgb_to_linear(
          {static_cast<float>(displayed.redF()), static_cast<float>(displayed.greenF()),
           static_cast<float>(displayed.blueF())});
      scenario.refresh();
      EXPECT_NE(button, nullptr);
      const QVec3 current = scenario.session.scene().scene_settings().fog.color;
      EXPECT_NEAR(current.x, expected.x, 0.0001);
      EXPECT_NEAR(current.y, expected.y, 0.0001);
      EXPECT_NEAR(current.z, expected.z, 0.0001);
      EXPECT_EQ(scenario.session.history().undo_count(), history);
      dialog->setCurrentColor(QColor::fromRgbF(0.1, 0.8, 0.6));
      if (cancel) {
        dialog->reject();
      } else {
        dialog->accept();
      }
    });
    button->click();
    scenario.refresh();
  };
  run_dialog(true);
  EXPECT_EQ(scenario.session.scene().scene_settings().fog, baseline);
  EXPECT_EQ(scenario.session.history().undo_count(), history);
  EXPECT_FALSE(scenario.session.dirty());
  run_dialog(false);
  const auto accepted = scenario.session.scene().scene_settings().fog;
  EXPECT_NE(accepted.color.x, baseline.color.x);
  EXPECT_EQ(scenario.session.history().undo_count(), history + 1u);
  EXPECT_TRUE(scenario.session.dirty());
  ASSERT_TRUE(scenario.editor.history().undo().ok());
  EXPECT_EQ(scenario.session.scene().scene_settings().fog, baseline);
  ASSERT_TRUE(scenario.editor.history().redo().ok());
  EXPECT_EQ(scenario.session.scene().scene_settings().fog, accepted);
}

TEST(InspectorMaterialTests, DISABLED_PointLightRangeRefreshPerformance) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-light-performance";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  apply_qt_obsidian_application_theme(
      QApplication::style()->standardPalette());
  application.setQuitOnLastWindowClosed(false);

  InspectorLightScenario scenario;
  QuaderCreatePointLightArgs args;
  args.light.range = 2.0f;
  args.select_created = true;
  const auto created = scenario.editor.scene().create_point_light(args);
  ASSERT_TRUE(created.ok()) << created.error_message();
  scenario.refresh();

  constexpr int kWarmupSamples = 12;
  constexpr int kMeasuredSamples = 120;
  std::uint64_t material_revision = 0;
  for (const bool full_rebuild : {true, false}) {
    const auto edit = scenario.editor.scene().begin_point_light_range_edit(
        created.value().id);
    ASSERT_TRUE(edit.ok()) << edit.error_message();
    std::array<double, kMeasuredSamples> samples{};
    for (int index = 0; index < kWarmupSamples + kMeasuredSamples; ++index) {
      const auto update = scenario.editor.scene().update_point_light_range_edit(
          edit.value(), 2.0f + static_cast<float>(index + 1) * 0.025f);
      ASSERT_TRUE(update.ok()) << update.error_message();
      if (full_rebuild) {
        // Existing owner invalidation exercises the original full rebuild in
        // this same executable without adding a production benchmark switch.
        scenario.panel.set_material_presentation(
            {.revision = ++material_revision});
      }
      const auto start = std::chrono::steady_clock::now();
      scenario.refresh();
      const double elapsed_ms =
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - start)
              .count();
      if (index >= kWarmupSamples) {
        samples[static_cast<std::size_t>(index - kWarmupSamples)] = elapsed_ms;
      }
    }
    ASSERT_TRUE(scenario.editor.scene()
                    .finish_point_light_range_edit(edit.value(), true)
                    .ok());
    scenario.refresh();
    const double mean_ms =
        std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
    std::sort(samples.begin(), samples.end());
    std::cout << "Inspector point-light radius "
              << (full_rebuild ? "full-rebuild" : "normal-refresh")
              << ": samples=" << samples.size() << " mean_ms=" << mean_ms
              << " p95_ms=" << samples[samples.size() * 95 / 100] << '\n';
  }
}

TEST(InspectorMaterialTests,
     RetainsLightRangeFieldDuringPreviewCancelAndHistory) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-light-range-retained";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  InspectorLightScenario scenario;
  QuaderCreatePointLightArgs args;
  args.light.range = 2.0f;
  args.select_created = true;
  const auto created = scenario.editor.scene().create_point_light(args);
  ASSERT_TRUE(created.ok()) << created.error_message();
  scenario.refresh();
  QPointer<QDoubleSpinBox> field = scenario.range_field();
  ASSERT_NE(field, nullptr);
  int value_change_signals = 0;
  QObject::connect(field.data(), &QDoubleSpinBox::valueChanged,
                   [&value_change_signals](double) { ++value_change_signals; });

  const auto edit = scenario.editor.scene().begin_point_light_range_edit(
      created.value().id);
  ASSERT_TRUE(edit.ok()) << edit.error_message();
  for (const float range : {3.125f, 0.001f, 125.125f}) {
    ASSERT_TRUE(scenario.editor.scene()
                    .update_point_light_range_edit(edit.value(), range)
                    .ok());
    scenario.refresh();
    ASSERT_NE(field, nullptr)
        << "radius samples must retain the existing property widgets";
    EXPECT_EQ(scenario.range_field(), field.data());
    EXPECT_NEAR(field->value(), range, 0.00005);
  }
  ASSERT_TRUE(scenario.editor.scene()
                  .finish_point_light_range_edit(edit.value(), true)
                  .ok());
  scenario.refresh();
  ASSERT_NE(field, nullptr);
  EXPECT_DOUBLE_EQ(field->value(), 2.0);

  const auto committed = scenario.editor.scene().begin_point_light_range_edit(
      created.value().id);
  ASSERT_TRUE(committed.ok());
  ASSERT_TRUE(scenario.editor.scene()
                  .update_point_light_range_edit(committed.value(), 4.5f)
                  .ok());
  scenario.refresh();
  ASSERT_TRUE(scenario.editor.scene()
                  .finish_point_light_range_edit(committed.value())
                  .ok());
  scenario.refresh();
  ASSERT_NE(field, nullptr);
  EXPECT_DOUBLE_EQ(field->value(), 4.5);
  ASSERT_TRUE(scenario.editor.history().undo().ok());
  scenario.refresh();
  ASSERT_NE(field, nullptr);
  EXPECT_DOUBLE_EQ(field->value(), 2.0);
  ASSERT_TRUE(scenario.editor.history().redo().ok());
  scenario.refresh();
  ASSERT_NE(field, nullptr);
  EXPECT_DOUBLE_EQ(field->value(), 4.5);
  EXPECT_EQ(value_change_signals, 0)
      << "owner refreshes must not emit Inspector mutations";

  scenario.refresh();
  EXPECT_EQ(scenario.range_field(), field.data());
}

TEST(InspectorMaterialTests,
     LightRangeRetentionInvalidatesOtherPropertiesSelectionAndLocale) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-light-range-invalidation";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  InspectorLightScenario scenario;
  QuaderCreatePointLightArgs args;
  args.light.range = 2.0f;
  args.select_created = true;
  const auto created = scenario.editor.scene().create_point_light(args);
  ASSERT_TRUE(created.ok());
  scenario.refresh();
  ASSERT_NE(scenario.range_field(), nullptr);

  // A range sample coalesced with another scene edit must not mask that edit.
  const auto edit = scenario.editor.scene().begin_point_light_range_edit(
      created.value().id);
  ASSERT_TRUE(edit.ok());
  ASSERT_TRUE(scenario.editor.scene()
                  .update_point_light_range_edit(edit.value(), 3.0f)
                  .ok());
  ASSERT_TRUE(scenario.editor.scene().rename(created.value().id, "Renamed Light")
                  .ok());
  scenario.refresh();
  ASSERT_NE(scenario.range_field(), nullptr);
  EXPECT_DOUBLE_EQ(scenario.range_field()->value(), 3.0);
  auto *name = scenario.panel.findChild<QLineEdit *>(
      QStringLiteral("inspector.general.name"));
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->text(), QStringLiteral("Renamed Light"));
  ASSERT_TRUE(scenario.editor.scene()
                  .finish_point_light_range_edit(edit.value(), true)
                  .ok());
  scenario.refresh();

  ASSERT_NE(scenario.range_field(), nullptr);
  EXPECT_DOUBLE_EQ(scenario.range_field()->value(), 2.0);
  auto light = to_api_light_component(
      *scenario.session.scene().light(created.value().id));
  light.intensity = 3.0f;
  ASSERT_TRUE(scenario.editor.scene()
                  .set_light_component(created.value().id, light)
                  .ok());
  scenario.refresh();
  auto *intensity = scenario.panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("inspector.light.intensity"));
  ASSERT_NE(intensity, nullptr);
  EXPECT_DOUBLE_EQ(intensity->value(), 3.0);

  light.type = QuaderLightType::Directional;
  ASSERT_TRUE(scenario.editor.scene()
                  .set_light_component(created.value().id, light)
                  .ok());
  scenario.refresh();
  EXPECT_EQ(scenario.range_field(), nullptr);
  light.type = QuaderLightType::Point;
  ASSERT_TRUE(scenario.editor.scene()
                  .set_light_component(created.value().id, light)
                  .ok());
  scenario.refresh();
  ASSERT_NE(scenario.range_field(), nullptr);
  EXPECT_DOUBLE_EQ(scenario.range_field()->value(), 2.0);

  ASSERT_TRUE(scenario.localization.select_locale("it-IT"));
  scenario.refresh();
  auto *tree = scenario.panel.findChild<QTreeWidget *>(
      QStringLiteral("inspector.tree"));
  ASSERT_NE(tree, nullptr);
  EXPECT_EQ(tree->headerItem()->text(0),
            QString::fromStdString(std::string(scenario.localization.text(
                "ui.inspector.property", "Property"))));

  scenario.panel.set_material_presentation(
      {.revision = 1u, .section_visible = true});
  scenario.refresh();
  EXPECT_NE(find_top_level_item(*tree, QStringLiteral("material")), nullptr);

  args.light.range = 8.0f;
  const auto second = scenario.editor.scene().create_point_light(args);
  ASSERT_TRUE(second.ok());
  scenario.refresh();
  ASSERT_NE(scenario.range_field(), nullptr);
  EXPECT_DOUBLE_EQ(scenario.range_field()->value(), 8.0);

  scenario.panel.abandon_runtime();
  scenario.refresh();
  ASSERT_NE(scenario.range_field(), nullptr);
  EXPECT_DOUBLE_EQ(scenario.range_field()->value(), 8.0);
}

TEST(InspectorMaterialTests, AppliesInterfaceScaleToRetainedTreeMetrics) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-scale-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  InspectorPanel panel;
  auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("inspector.tree"));
  ASSERT_NE(tree, nullptr);

  panel.apply_ui_scale(QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(tree->header()->minimumSectionSize(), 128);

  panel.apply_ui_scale(QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(tree->header()->minimumSectionSize(), 64);
}

TEST(InspectorMaterialTests,
     RefreshesOnlyPaletteBakedMaterialActionIconsOnPaletteChanges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-material-palette-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  QPalette palette = QApplication::palette();
  palette.setColor(QPalette::ButtonText, QColor(211, 37, 53));
  palette.setColor(QPalette::Mid, QColor(113, 23, 41));
  QApplication::setPalette(palette);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto box = editor.modeling().create_box(
      QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
      "Inspector Palette Material");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = inspector_localization();
  UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };

  InspectorPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.set_material_presentation({
      .revision = 1u,
      .section_visible = true,
      .materials = {material_item(
          QuaderMaterialReference::project(ProjectAssetId::generate()),
          QStringLiteral("project:palette-material"),
          QStringLiteral("Palette Material"), true)},
  });
  panel.refresh(context);
  panel.show();
  QCoreApplication::processEvents();

  auto *preview = panel.findChild<QLabel *>(
      QStringLiteral("inspector.material.0.preview"));
  auto *edit = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.0.edit"));
  auto *replace = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.0.replace"));
  ASSERT_NE(preview, nullptr);
  ASSERT_NE(edit, nullptr);
  ASSERT_NE(replace, nullptr);
  const qint64 preview_key = preview->pixmap().cacheKey();
  const qint64 initial_edit_key = edit->icon().cacheKey();
  const qint64 initial_replace_key = replace->icon().cacheKey();

  palette.setColor(QPalette::ButtonText, QColor(29, 197, 83));
  palette.setColor(QPalette::Mid, QColor(17, 97, 47));
  QApplication::setPalette(palette);
  QCoreApplication::processEvents();
  const qint64 application_edit_key = edit->icon().cacheKey();
  const qint64 application_replace_key = replace->icon().cacheKey();
  EXPECT_NE(application_edit_key, initial_edit_key);
  EXPECT_NE(application_replace_key, initial_replace_key);
  EXPECT_EQ(preview->pixmap().cacheKey(), preview_key)
      << "palette changes must not rebuild material preview rasters";

  palette.setColor(QPalette::ButtonText, QColor(37, 89, 223));
  palette.setColor(QPalette::Mid, QColor(21, 47, 127));
  panel.setPalette(palette);
  EXPECT_NE(edit->icon().cacheKey(), application_edit_key);
  EXPECT_NE(replace->icon().cacheKey(), application_replace_key);
  EXPECT_EQ(preview->pixmap().cacheKey(), preview_key);
}

TEST(InspectorMaterialTests,
     SelectionModesPresentOnlyTheirApprovedPropertyMatrix) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-material-mode-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto first = editor.modeling().create_box(
      QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
      "Inspector Material First");
  const auto second = editor.modeling().create_box(
      QuaderBounds{.min = {2.0f, -1.0f, -1.0f},
                   .max = {4.0f, 1.0f, 1.0f}},
      "Inspector Material Second");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.selection().select_object(first.value()).ok());

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = inspector_localization();
  UIInspectorPreviewContext preview{.document = session.scene(),
                                    .assets = session.assets()};
  UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .inspector_preview = &preview,
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };

  InspectorPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  const auto set_presentation = [&panel](std::uint64_t revision) {
    panel.set_material_presentation({
        .revision = revision,
        .section_visible = true,
        .materials = {material_item(
            QuaderMaterialReference::project(ProjectAssetId::generate()),
            QStringLiteral("project:painted-metal"),
            QStringLiteral("Painted Metal"), true)},
    });
  };

  set_presentation(1u);
  panel.refresh(context);
  auto *tree = panel.findChild<QTreeWidget *>(
      QStringLiteral("inspector.tree"));
  ASSERT_NE(tree, nullptr);
  EXPECT_NE(find_top_level_item(*tree, QStringLiteral("general")), nullptr);
  auto *material =
      find_top_level_item(*tree, QStringLiteral("material"));
  ASSERT_NE(material, nullptr);
  EXPECT_TRUE(material->isExpanded());

  auto *material_preview = panel.findChild<QLabel *>(
      QStringLiteral("inspector.material.0.preview"));
  auto *material_edit = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.0.edit"));
  ASSERT_NE(material_preview, nullptr);
  ASSERT_NE(material_edit, nullptr);
  panel.apply_ui_scale(QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(material_preview->size(), QSize(112, 112));
  EXPECT_EQ(material_edit->size(), QSize(48, 48));
  EXPECT_EQ(material_edit->iconSize(), QSize(32, 32));
  panel.apply_ui_scale(QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(material_preview->size(), QSize(56, 56));
  EXPECT_EQ(material_edit->size(), QSize(24, 24));
  EXPECT_EQ(material_edit->iconSize(), QSize(16, 16));

  ASSERT_TRUE(editor.selection()
                  .select_objects({first.value().entity, second.value().entity},
                                  first.value().entity)
                  .ok());
  set_presentation(2u);
  panel.refresh(context);
  EXPECT_NE(find_top_level_item(*tree, QStringLiteral("general")), nullptr);
  EXPECT_NE(find_top_level_item(*tree, QStringLiteral("material")), nullptr);

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Group).ok());
  const auto group = editor.scene().group_selection("Inspector Material Group");
  ASSERT_TRUE(group.ok()) << group.error_message();
  ASSERT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Group);
  set_presentation(3u);
  panel.refresh(context);
  EXPECT_NE(find_top_level_item(*tree, QStringLiteral("general")), nullptr);
  EXPECT_NE(find_top_level_item(*tree, QStringLiteral("material")), nullptr);

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(first.value(), 1u).ok());
  ASSERT_EQ(editor.selection().summary().selected_face_count, 1u);
  set_presentation(4u);
  panel.refresh(context);
  ASSERT_EQ(tree->topLevelItemCount(), 1);
  EXPECT_NE(find_top_level_item(*tree, QStringLiteral("material")), nullptr);

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  set_presentation(5u);
  panel.refresh(context);
  EXPECT_EQ(tree->topLevelItemCount(), 0);

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Edge).ok());
  set_presentation(6u);
  panel.refresh(context);
  EXPECT_EQ(tree->topLevelItemCount(), 0);
}

TEST(InspectorMaterialTests,
     MaterialRowsDeduplicateUseSquareControlsAndDispatchTypedCallbacks) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-material-row-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto box = editor.modeling().create_box(
      QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
      "Inspector Material Rows");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = inspector_localization();
  UIInspectorPreviewContext preview{.document = session.scene(),
                                    .assets = session.assets()};
  UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .inspector_preview = &preview,
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };

  const QuaderMaterialReference editable_reference =
      QuaderMaterialReference::project(ProjectAssetId::generate());
  const QuaderMaterialReference embedded_reference =
      QuaderMaterialReference::embedded(201u);
  const QuaderMaterialReference graph_reference =
      QuaderMaterialReference::project(ProjectAssetId::generate());
  const QuaderMaterialReference missing_reference =
      QuaderMaterialReference::project(ProjectAssetId::generate());
  QtMaterialPresentationItem missing_material = material_item(
      missing_reference, QStringLiteral("project:missing"), {}, true);
  missing_material.available = false;
  std::optional<QuaderMaterialReference> edited;
  std::optional<QuaderMaterialReference> replaced;
  int edit_callback_version = 0;

  InspectorPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.set_material_presentation({
      .revision = 1u,
      .section_visible = true,
      .materials = {
          material_item(editable_reference, QStringLiteral("project:standard"),
                        QStringLiteral("Editable Material"), true),
          material_item(editable_reference,
                        QStringLiteral("project:standard-duplicate"),
                        QStringLiteral("Duplicate Material"), true),
          material_item(embedded_reference, QStringLiteral("embedded:201"),
                        QStringLiteral("Embedded Material"), true),
          material_item(graph_reference, QStringLiteral("project:graph"),
                        QStringLiteral("Graph Material"), false),
          missing_material,
      },
      .edit_material = [&edited, &edit_callback_version](
                           QuaderMaterialReference reference) {
        edited = reference;
        edit_callback_version = 1;
      },
      .replace_material = [&replaced](QuaderMaterialReference reference) {
        replaced = reference;
      },
  });
  panel.refresh(context);

  auto *tree = panel.findChild<QTreeWidget *>(
      QStringLiteral("inspector.tree"));
  ASSERT_NE(tree, nullptr);
  auto *material =
      find_top_level_item(*tree, QStringLiteral("material"));
  ASSERT_NE(material, nullptr);
  EXPECT_TRUE(material->isExpanded());
  EXPECT_EQ(material->childCount(), 4)
      << "owner snapshots remain protected from duplicate presentation rows";
  ASSERT_TRUE(material->child(0)->isFirstColumnSpanned());
  ASSERT_TRUE(material->child(1)->isFirstColumnSpanned());
  ASSERT_TRUE(material->child(2)->isFirstColumnSpanned());
  ASSERT_TRUE(material->child(3)->isFirstColumnSpanned());

  auto *preview_label = panel.findChild<QLabel *>(
      QStringLiteral("inspector.material.0.preview"));
  auto *edit_button = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.0.edit"));
  auto *replace_button = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.0.replace"));
  auto *embedded_edit = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.1.edit"));
  auto *graph_edit = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.2.edit"));
  auto *missing_edit = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.3.edit"));
  auto *missing_replace = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.3.replace"));
  ASSERT_NE(preview_label, nullptr);
  ASSERT_NE(edit_button, nullptr);
  ASSERT_NE(replace_button, nullptr);
  ASSERT_NE(embedded_edit, nullptr);
  ASSERT_NE(graph_edit, nullptr);
  ASSERT_NE(missing_edit, nullptr);
  ASSERT_NE(missing_replace, nullptr);
  auto *name_label = panel.findChild<QLabel *>(
      QStringLiteral("inspector.material.0.name"));
  ASSERT_NE(name_label, nullptr);
  EXPECT_FALSE(name_label->toolTip().isEmpty());
  EXPECT_FALSE(preview_label->toolTip().isEmpty());
  EXPECT_EQ(preview_label->width(), 56);
  EXPECT_EQ(preview_label->height(), 56);
  EXPECT_FALSE(preview_label->pixmap().isNull());
  EXPECT_EQ(edit_button->width(), edit_button->height());
  EXPECT_EQ(replace_button->width(), replace_button->height());
  const QList<QSize> edit_icon_sizes = edit_button->icon().availableSizes(
      QIcon::Normal, QIcon::Off);
  EXPECT_TRUE(edit_icon_sizes.contains(QSize(16, 16)));
  EXPECT_TRUE(edit_icon_sizes.contains(QSize(384, 384)))
      << "programmatic Inspector glyphs retain a true high-resolution source";
  EXPECT_TRUE(edit_button->isEnabled());
  EXPECT_TRUE(replace_button->isEnabled());
  EXPECT_FALSE(embedded_edit->isEnabled());
  EXPECT_FALSE(graph_edit->isEnabled());
  EXPECT_FALSE(missing_edit->isEnabled());
  EXPECT_TRUE(missing_replace->isEnabled());

  const qint64 preview_cache_key = preview_label->pixmap().cacheKey();
  QEvent device_pixel_ratio_event(QEvent::DevicePixelRatioChange);
  QCoreApplication::sendEvent(&panel, &device_pixel_ratio_event);
  EXPECT_EQ(preview_label->pixmap().cacheKey(), preview_cache_key)
      << "an unchanged effective DPR must not rebuild the retained raster";
  QEvent screen_change_event(QEvent::ScreenChangeInternal);
  QCoreApplication::sendEvent(&panel, &screen_change_event);
  EXPECT_EQ(preview_label->pixmap().cacheKey(), preview_cache_key)
      << "screen notifications re-evaluate DPR behind the same cache gate";

  edit_button->click();
  replace_button->click();
  ASSERT_TRUE(edited.has_value());
  ASSERT_TRUE(replaced.has_value());
  EXPECT_EQ(*edited, editable_reference);
  EXPECT_EQ(*replaced, editable_reference);
  EXPECT_EQ(edit_callback_version, 1);

  // An unchanged presentation revision must not rebuild or enumerate data.
  panel.set_material_presentation({
      .revision = 1u,
      .section_visible = true,
      .materials = {material_item(
          editable_reference, QStringLiteral("project:standard"),
          QStringLiteral("Should Not Rebuild"), true)},
      .edit_material = [&edited, &edit_callback_version](
                           QuaderMaterialReference reference) {
        edited = reference;
        edit_callback_version = 2;
      },
      .replace_material = [&replaced](QuaderMaterialReference reference) {
        replaced = reference;
      },
  });
  panel.refresh(context);
  auto *name = panel.findChild<QLabel *>(
      QStringLiteral("inspector.material.0.name"));
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->accessibleName(), QStringLiteral("Editable Material"));
  edit_button->click();
  EXPECT_EQ(edit_callback_version, 2);
}

} // namespace
} // namespace quader
