////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/metadata/host_contract.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_tool_toolbar.hpp"
#include "viewport/viewport_input_types.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QRadioButton>
#include <QSlider>

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <ranges>
#include <string_view>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace quader {
namespace {

TEST(PathArrayUiCatalogTests,
     ToolDescriptorCommandAndInputIdentityStayInOneCatalog) {
  const ToolDescriptor *descriptor =
      find_viewport_tool_descriptor(ViewportToolId::PathArray);
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->id, "path_array");
  EXPECT_EQ(descriptor->label, "Path Array");
  EXPECT_TRUE(descriptor->shortcut.empty());
  EXPECT_EQ(descriptor->kind, ToolKind::Modal);
  EXPECT_EQ(descriptor->menu_group, ToolMenuGroup::Modeling);
  EXPECT_TRUE(descriptor->owns_viewport);
  EXPECT_TRUE(descriptor->blocks_viewport_focus_shortcuts);
  EXPECT_FALSE(descriptor->allows_selection_fallback);
  EXPECT_TRUE(descriptor->editor_owned_native_lifecycle);

  EXPECT_EQ(find_viewport_tool_descriptor("path_array"), descriptor);
  const ViewportTool *tool = find_viewport_tool(ViewportToolId::PathArray);
  ASSERT_NE(tool, nullptr);
  EXPECT_EQ(tool->id(), descriptor->id);
  EXPECT_EQ(viewport_tool_id(ViewportToolId::PathArray), descriptor->id);
  EXPECT_EQ(quader_command::kToolsActivatePathArray.string(),
            "tools.activate.path_array");
  EXPECT_TRUE(std::ranges::any_of(
      quader_command::typed_command_ids(), [](QuaderCommandId command) {
        return command.string() ==
               quader_command::kToolsActivatePathArray.string();
      }));

  const quader_host_contract::HostToolDescriptor *host_tool =
      quader_host_contract::find_tool_descriptor("path_array");
  ASSERT_NE(host_tool, nullptr);
  EXPECT_EQ(host_tool->label, descriptor->label);
  EXPECT_TRUE(host_tool->primary_shortcut.empty());

  const auto modal_group = std::ranges::find_if(
      quader_host_contract::tool_group_catalog(), [](const auto &group) {
        return group.id == "modal_modeling_tools";
      });
  ASSERT_NE(modal_group, quader_host_contract::tool_group_catalog().end());
  EXPECT_NE(std::ranges::find(modal_group->tool_ids, "path_array"),
            modal_group->tool_ids.end());

  const auto descriptors = viewport_tool_descriptors();
  const auto radial_descriptor = std::ranges::find_if(
      descriptors, [](const ToolDescriptor &candidate) {
        return candidate.tool == ViewportToolId::RadialArray;
      });
  const auto path_descriptor = std::ranges::find_if(
      descriptors, [](const ToolDescriptor &candidate) {
        return candidate.tool == ViewportToolId::PathArray;
      });
  const auto curve_descriptor = std::ranges::find_if(
      descriptors, [](const ToolDescriptor &candidate) {
        return candidate.tool == ViewportToolId::Curve;
      });
  ASSERT_NE(radial_descriptor, descriptors.end());
  ASSERT_NE(path_descriptor, descriptors.end());
  ASSERT_NE(curve_descriptor, descriptors.end());
  EXPECT_EQ(std::next(radial_descriptor), path_descriptor);
  EXPECT_EQ(std::next(path_descriptor), curve_descriptor);

  const auto tools = viewport_tool_catalog();
  const auto radial_tool = std::ranges::find_if(
      tools, [](const ViewportTool *candidate) {
        return candidate->id() == "radial_array";
      });
  const auto path_tool = std::ranges::find_if(
      tools, [](const ViewportTool *candidate) {
        return candidate->id() == "path_array";
      });
  const auto curve_tool = std::ranges::find_if(
      tools, [](const ViewportTool *candidate) {
        return candidate->id() == "curve";
      });
  ASSERT_NE(radial_tool, tools.end());
  ASSERT_NE(path_tool, tools.end());
  ASSERT_NE(curve_tool, tools.end());
  EXPECT_EQ(std::next(radial_tool), path_tool);
  EXPECT_EQ(std::next(path_tool), curve_tool);
}

TEST(PathArrayUiCatalogTests,
     GenericChoiceDescriptorDefaultsToRadioAndSupportsDisabledDropdown) {
  const QuaderToolSettingControl default_control;
  EXPECT_TRUE(default_control.enabled);
  EXPECT_EQ(default_control.number_interaction,
            QuaderToolSettingNumberInteraction::Continuous);
  EXPECT_EQ(default_control.choice_presentation,
            QuaderToolSettingChoicePresentation::Radio);

  const QuaderToolSettingControl forward_axis{
      .kind = QuaderToolSettingControlKind::Choice,
      .id = "path_array.forward_axis",
      .label = "Forward Axis",
      .enabled = false,
      .choice_value = "positive_x",
      .choices = {{.id = "positive_x", .label = "+X", .value = 0.0}},
      .choice_presentation =
          QuaderToolSettingChoicePresentation::Dropdown,
  };
  EXPECT_FALSE(forward_axis.enabled);
  EXPECT_EQ(forward_axis.choice_presentation,
            QuaderToolSettingChoicePresentation::Dropdown);
}

TEST(PathArrayUiCatalogTests,
     ArrayCopyDescriptorsRequestGenericStepwiseNumberInteraction) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto source = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Array Slider Source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());

  const auto expect_interaction = [&editor](
                                      ViewportToolId tool,
                                      std::string_view stepped_control_id,
                                      std::initializer_list<std::string_view>
                                          continuous_control_ids) {
    ASSERT_TRUE(editor.tools().activate(tool).ok());
    const QuaderToolSettingsTarget target = editor.tools().active_settings();
    const auto stepped = std::ranges::find_if(
        target.controls, [stepped_control_id](const auto &control) {
          return control.id == stepped_control_id;
        });
    ASSERT_NE(stepped, target.controls.end());
    EXPECT_EQ(stepped->number_interaction,
              QuaderToolSettingNumberInteraction::Stepped);
    for (const std::string_view continuous_control_id :
         continuous_control_ids) {
      const auto continuous = std::ranges::find_if(
          target.controls, [continuous_control_id](const auto &control) {
            return control.id == continuous_control_id;
          });
      ASSERT_NE(continuous, target.controls.end());
      EXPECT_EQ(continuous->number_interaction,
                QuaderToolSettingNumberInteraction::Continuous);
    }
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  };

  expect_interaction(ViewportToolId::LinearArray, "linear_array.count", {});
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  expect_interaction(ViewportToolId::RadialArray, "radial_array.copies",
                     {"radial_array.degrees", "radial_array.repeats"});

  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0F, 0.0F, 0.0F}},
                 {.position = {5.0F, 0.0F, 0.0F}}},
      .name = "Array Slider Path",
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
  expect_interaction(ViewportToolId::PathArray, "path_array.count",
                     {"path_array.distance"});
}

TEST(PathArrayUiCatalogTests,
     ChangingCopyCountDoesNotChangeDistanceValueOrRange) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0F, 0.0F, 0.0F}},
                 {.position = {12.0F, 0.0F, 0.0F}}},
      .name = "Path Array Independent Distance",
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::PathArray).ok());

  const QuaderToolSettingsTarget before = editor.tools().active_settings();
  const auto before_distance = std::ranges::find_if(
      before.controls, [](const QuaderToolSettingControl &control) {
        return control.id == "path_array.distance";
      });
  ASSERT_NE(before_distance, before.controls.end());
  const double distance_value = before_distance->number_value;
  const double distance_minimum = before_distance->min_value;
  const double distance_maximum = before_distance->max_value;

  ASSERT_TRUE(editor.tools()
                  .set_setting("path_array", "path_array.count", 128.0)
                  .ok());
  const QuaderToolSettingsTarget after = editor.tools().active_settings();
  const auto after_distance = std::ranges::find_if(
      after.controls, [](const QuaderToolSettingControl &control) {
        return control.id == "path_array.distance";
      });
  ASSERT_NE(after_distance, after.controls.end());
  EXPECT_DOUBLE_EQ(after_distance->number_value, distance_value);
  EXPECT_DOUBLE_EQ(after_distance->min_value, distance_minimum);
  EXPECT_DOUBLE_EQ(after_distance->max_value, distance_maximum);
}

TEST(PathArrayUiCatalogTests,
     PrimitiveChoiceUsesTheGenericDropdownAndToolbarOrderIsStable) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-path-array-ui-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  QtToolToolbar toolbar;
  const auto actions = toolbar.actions();
  const auto radial_action = std::ranges::find_if(
      actions, [](const QAction *action) {
        return action->objectName() == QStringLiteral("tool_radial_array");
      });
  const auto path_action = std::ranges::find_if(
      actions, [](const QAction *action) {
        return action->objectName() == QStringLiteral("tool_path_array");
      });
  const auto curve_action = std::ranges::find_if(
      actions, [](const QAction *action) {
        return action->objectName() == QStringLiteral("tool_curve");
      });
  ASSERT_NE(radial_action, actions.end());
  ASSERT_NE(path_action, actions.end());
  ASSERT_NE(curve_action, actions.end());
  EXPECT_EQ(std::next(radial_action), path_action);
  EXPECT_EQ(std::next(path_action), curve_action);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());

  const QuaderToolSettingsTarget target = editor.tools().active_settings();
  const auto primitive_type = std::ranges::find_if(
      target.controls, [](const QuaderToolSettingControl &control) {
        return control.id == "primitive.type";
      });
  ASSERT_NE(primitive_type, target.controls.end());
  EXPECT_EQ(primitive_type->choice_presentation,
            QuaderToolSettingChoicePresentation::Dropdown);

  QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  EXPECT_NE(panel.findChild<QComboBox *>(
                QStringLiteral("tool_settings.control.primitive.type")),
            nullptr);
  EXPECT_EQ(panel.findChild<QRadioButton *>(
                QStringLiteral("tool_settings.control.primitive.type")),
            nullptr);
  panel.unbind();
}

TEST(PathArrayUiCatalogTests,
     PathArrayForwardAxisDropdownTracksGenericEnabledState) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-path-array-enabled-ui-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0F, 0.0F, 0.0F}},
                 {.position = {5.0F, 0.0F, 0.0F}}},
      .name = "Path Array UI Driver",
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::PathArray).ok());
  ASSERT_TRUE(editor.tools()
                  .set_setting("path_array", "path_array.follow_path", false)
                  .ok());

  const QuaderToolSettingsTarget target = editor.tools().active_settings();
  ASSERT_EQ(target.tool_id, "path_array");
  ASSERT_EQ(target.controls.size(), 4U);
  const auto forward_axis = std::ranges::find_if(
      target.controls, [](const QuaderToolSettingControl &control) {
        return control.id == "path_array.forward_axis";
      });
  ASSERT_NE(forward_axis, target.controls.end());
  EXPECT_FALSE(forward_axis->enabled);
  EXPECT_EQ(forward_axis->choice_value, "positive_x");
  EXPECT_EQ(forward_axis->choices.size(), 6U);
  EXPECT_EQ(forward_axis->choice_presentation,
            QuaderToolSettingChoicePresentation::Dropdown);

  QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  auto *dropdown = panel.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.path_array.forward_axis"));
  auto *count_slider = panel.findChild<QSlider *>(
      QStringLiteral("tool_settings.control.path_array.count.slider"));
  auto *distance_slider = panel.findChild<QSlider *>(
      QStringLiteral("tool_settings.control.path_array.distance.slider"));
  ASSERT_NE(dropdown, nullptr);
  ASSERT_NE(count_slider, nullptr);
  ASSERT_NE(distance_slider, nullptr);
  EXPECT_FALSE(dropdown->isEnabled());
  EXPECT_EQ(dropdown->count(), 6);
  EXPECT_TRUE(count_slider->hasTracking());
  EXPECT_EQ(count_slider->singleStep(), 1);
  EXPECT_EQ(count_slider->pageStep(), 1);
  EXPECT_EQ(count_slider->tickInterval(), 1);
  EXPECT_EQ(count_slider->tickPosition(), QSlider::TicksBelow);
  EXPECT_TRUE(distance_slider->hasTracking());
  EXPECT_EQ(distance_slider->tickInterval(), 0);
  EXPECT_EQ(distance_slider->tickPosition(), QSlider::NoTicks);

  ASSERT_TRUE(editor.tools()
                  .set_setting("path_array", "path_array.follow_path", true)
                  .ok());
  panel.refresh();
  EXPECT_EQ(panel.findChild<QComboBox *>(
                QStringLiteral(
                    "tool_settings.control.path_array.forward_axis")),
            dropdown);
  EXPECT_TRUE(dropdown->isEnabled());
  panel.unbind();
}

TEST(PathArrayUiCatalogTests, ShippedLocalesCoverPathArrayPresentation) {
  const std::filesystem::path locale_root =
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales";
  LocalizationService localization;
  ASSERT_TRUE(localization.load(locale_root, "it-IT"));
  EXPECT_EQ(localization.text("viewport.tool.path_array.label", "Path Array"),
            "Serie lungo percorso");
  EXPECT_EQ(localization.text("path_array.forward_axis", "Forward Axis"),
            "Asse in avanti");
  EXPECT_EQ(localization.text("path_array.forward_axis.negative_z", "-Z"),
            "-Z");

  ASSERT_TRUE(localization.select_locale("en-US"));
  EXPECT_EQ(localization.text("viewport.tool.path_array.label", "Path Array"),
            "Path Array");
  EXPECT_EQ(localization.text("path_array.count", "Num Copies"),
            "Num Copies");
}

} // namespace
} // namespace quader
