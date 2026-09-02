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
#include "editor/array/path_array_tool_service.hpp"
#include "editor/metadata/host_contract.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "ui/qt_tool_settings_drag_spinner.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_tool_toolbar.hpp"
#include "viewport/viewport_input_types.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>

#include <algorithm>
#include <array>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <limits>
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
  EXPECT_EQ(default_control.number_presentation,
            QuaderToolSettingNumberPresentation::SliderSpinBox);
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
     GenericVectorAndDisclosureDescriptorsHaveNeutralDefaults) {
  const QuaderToolSettingControl vector_control{
      .kind = QuaderToolSettingControlKind::Vector3Number,
      .id = "example.vector",
      .label = "Vector",
      .group_id = "example.group",
      .vector3_components = {{
          {.id = "example.vector.x", .value = 1.0},
          {.id = "example.vector.y", .value = 2.0},
          {.id = "example.vector.z", .value = 3.0},
      }},
  };
  EXPECT_EQ(vector_control.group_id, "example.group");
  EXPECT_EQ(vector_control.vector3_components[0].id, "example.vector.x");
  EXPECT_DOUBLE_EQ(vector_control.vector3_components[1].value, 2.0);

  const QuaderToolSettingGroup group{
      .id = "example.group", .label = "Example"};
  EXPECT_TRUE(group.expanded_by_default);
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
                                      QuaderToolSettingNumberPresentation
                                          stepped_presentation,
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
    EXPECT_EQ(stepped->number_presentation, stepped_presentation);
    for (const std::string_view continuous_control_id :
         continuous_control_ids) {
      const auto continuous = std::ranges::find_if(
          target.controls, [continuous_control_id](const auto &control) {
            return control.id == continuous_control_id;
          });
      ASSERT_NE(continuous, target.controls.end());
      EXPECT_EQ(continuous->number_interaction,
                QuaderToolSettingNumberInteraction::Continuous);
      EXPECT_EQ(continuous->number_presentation,
                QuaderToolSettingNumberPresentation::SliderSpinBox);
    }
    ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  };

  expect_interaction(ViewportToolId::LinearArray, "linear_array.count",
                     QuaderToolSettingNumberPresentation::SliderSpinBox, {});
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  expect_interaction(ViewportToolId::RadialArray, "radial_array.copies",
                     QuaderToolSettingNumberPresentation::SliderSpinBox,
                     {"radial_array.degrees", "radial_array.repeats"});

  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0F, 0.0F, 0.0F}},
                 {.position = {5.0F, 0.0F, 0.0F}}},
      .name = "Array Slider Path",
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
  expect_interaction(ViewportToolId::PathArray, "path_array.count",
                     QuaderToolSettingNumberPresentation::DragSpinner,
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
     PathArrayTransformSettingsUseCompactRowsInOrderedDisclosureGroups) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0F, 0.0F, 0.0F}},
                 {.position = {12.0F, 0.0F, 0.0F}}},
      .name = "Path Array Settings Groups",
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::PathArray).ok());

  const QuaderToolSettingsTarget target = editor.tools().active_settings();
  ASSERT_EQ(target.groups.size(), 3U);
  EXPECT_EQ(target.groups[0].id, "path_array.group.placement");
  EXPECT_EQ(target.groups[0].label, "Placement");
  EXPECT_TRUE(target.groups[0].expanded_by_default);
  EXPECT_EQ(target.groups[1].id, "path_array.group.rotation");
  EXPECT_EQ(target.groups[1].label, "Rotation");
  EXPECT_FALSE(target.groups[1].expanded_by_default);
  EXPECT_EQ(target.groups[2].id, "path_array.group.scale");
  EXPECT_EQ(target.groups[2].label, "Scale");
  EXPECT_FALSE(target.groups[2].expanded_by_default);

  ASSERT_EQ(target.controls.size(), 10U);
  for (const QuaderToolSettingControl &control : target.controls) {
    EXPECT_TRUE(std::ranges::any_of(
        target.groups, [&control](const QuaderToolSettingGroup &group) {
          return group.id == control.group_id;
        })) << control.id;
  }

  const auto expect_vector =
      [&target](std::string_view row_id, std::string_view group_id,
                std::array<std::string_view, 3> component_ids,
                double expected_value, double minimum, double maximum,
                double step) {
        const auto row = std::ranges::find_if(
            target.controls, [row_id](const QuaderToolSettingControl &control) {
              return control.id == row_id;
            });
        ASSERT_NE(row, target.controls.end());
        EXPECT_EQ(row->kind, QuaderToolSettingControlKind::Vector3Number);
        EXPECT_EQ(row->group_id, group_id);
        for (std::size_t index = 0; index < 3U; ++index) {
          const QuaderToolSettingNumberComponent &component =
              row->vector3_components[index];
          EXPECT_EQ(component.id, component_ids[index]);
          EXPECT_TRUE(component.enabled);
          EXPECT_DOUBLE_EQ(component.value, expected_value);
          EXPECT_DOUBLE_EQ(component.min_value, minimum);
          EXPECT_DOUBLE_EQ(component.max_value, maximum);
          EXPECT_DOUBLE_EQ(component.step, step);
        }
      };

  expect_vector("path_array.rotation_offset", "path_array.group.rotation",
                {kPathArrayRotationOffsetXSetting,
                 kPathArrayRotationOffsetYSetting,
                 kPathArrayRotationOffsetZSetting},
                0.0, kPathArrayRotationUiMinimumDegrees,
                kPathArrayRotationUiMaximumDegrees,
                kPathArrayRotationUiStepDegrees);
  expect_vector("path_array.rotation_random_min",
                "path_array.group.rotation",
                {kPathArrayRotationRandomMinXSetting,
                 kPathArrayRotationRandomMinYSetting,
                 kPathArrayRotationRandomMinZSetting},
                0.0, kPathArrayRotationUiMinimumDegrees,
                kPathArrayRotationUiMaximumDegrees,
                kPathArrayRotationUiStepDegrees);
  expect_vector("path_array.rotation_random_max",
                "path_array.group.rotation",
                {kPathArrayRotationRandomMaxXSetting,
                 kPathArrayRotationRandomMaxYSetting,
                 kPathArrayRotationRandomMaxZSetting},
                0.0, kPathArrayRotationUiMinimumDegrees,
                kPathArrayRotationUiMaximumDegrees,
                kPathArrayRotationUiStepDegrees);
  expect_vector("path_array.scale_offset", "path_array.group.scale",
                {kPathArrayScaleOffsetXSetting, kPathArrayScaleOffsetYSetting,
                 kPathArrayScaleOffsetZSetting},
                1.0, kPathArrayScaleUiMinimumFactor,
                kPathArrayScaleUiMaximumFactor, kPathArrayScaleUiStepFactor);
  expect_vector("path_array.scale_random_min", "path_array.group.scale",
                {kPathArrayScaleRandomMinXSetting,
                 kPathArrayScaleRandomMinYSetting,
                 kPathArrayScaleRandomMinZSetting},
                1.0, kPathArrayScaleUiMinimumFactor,
                kPathArrayScaleUiMaximumFactor, kPathArrayScaleUiStepFactor);
  expect_vector("path_array.scale_random_max", "path_array.group.scale",
                {kPathArrayScaleRandomMaxXSetting,
                 kPathArrayScaleRandomMaxYSetting,
                 kPathArrayScaleRandomMaxZSetting},
                1.0, kPathArrayScaleUiMinimumFactor,
                kPathArrayScaleUiMaximumFactor, kPathArrayScaleUiStepFactor);
}

TEST(PathArrayUiCatalogTests,
     NumericComponentDispatchUpdatesOneOwnerAxisAndResetRestoresNeutralValues) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0F, 0.0F, 0.0F}},
                 {.position = {12.0F, 0.0F, 0.0F}}},
      .name = "Path Array Component Dispatch",
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::PathArray).ok());

  const auto set = [&editor](std::string_view id, double value) {
    return editor.tools().set_setting("path_array", id, value);
  };
  ASSERT_TRUE(set(kPathArrayRotationOffsetXSetting, 3.0).ok());
  ASSERT_TRUE(set(kPathArrayRotationOffsetYSetting, -4.0).ok());
  ASSERT_TRUE(set(kPathArrayRotationOffsetZSetting, 5.0).ok());
  ASSERT_TRUE(set(kPathArrayRotationRandomMaxXSetting, 10.0).ok());
  ASSERT_TRUE(set(kPathArrayRotationRandomMaxYSetting, 20.0).ok());
  ASSERT_TRUE(set(kPathArrayRotationRandomMaxZSetting, 30.0).ok());
  ASSERT_TRUE(set(kPathArrayRotationRandomMinXSetting, -10.0).ok());
  ASSERT_TRUE(set(kPathArrayRotationRandomMinYSetting, -20.0).ok());
  ASSERT_TRUE(set(kPathArrayRotationRandomMinZSetting, -30.0).ok());
  ASSERT_TRUE(set(kPathArrayScaleOffsetXSetting, 0.9).ok());
  ASSERT_TRUE(set(kPathArrayScaleOffsetYSetting, 1.1).ok());
  ASSERT_TRUE(set(kPathArrayScaleOffsetZSetting, 1.2).ok());
  ASSERT_TRUE(set(kPathArrayScaleRandomMaxXSetting, 2.0).ok());
  ASSERT_TRUE(set(kPathArrayScaleRandomMaxYSetting, 3.0).ok());
  ASSERT_TRUE(set(kPathArrayScaleRandomMaxZSetting, 4.0).ok());
  ASSERT_TRUE(set(kPathArrayScaleRandomMinXSetting, 0.5).ok());
  ASSERT_TRUE(set(kPathArrayScaleRandomMinYSetting, 0.6).ok());
  ASSERT_TRUE(set(kPathArrayScaleRandomMinZSetting, 0.7).ok());

  const PathArrayToolService &owner = session.path_array();
  const auto expect_vector = [](QVec3 actual, QVec3 expected) {
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.z, expected.z);
  };
  expect_vector(owner.rotation_offset_degrees(), {3.0F, -4.0F, 5.0F});
  expect_vector(owner.rotation_random_min_degrees(),
                {-10.0F, -20.0F, -30.0F});
  expect_vector(owner.rotation_random_max_degrees(),
                {10.0F, 20.0F, 30.0F});
  expect_vector(owner.scale_offset_factors(), {0.9F, 1.1F, 1.2F});
  expect_vector(owner.scale_random_min_factors(), {0.5F, 0.6F, 0.7F});
  expect_vector(owner.scale_random_max_factors(), {2.0F, 3.0F, 4.0F});

  ASSERT_TRUE(set(kPathArrayRotationRandomMinXSetting, 11.0).ok());
  EXPECT_EQ(owner.rotation_random_min_degrees().x, 11.0F);
  EXPECT_EQ(owner.rotation_random_max_degrees().x, 11.0F);
  ASSERT_TRUE(set(kPathArrayScaleRandomMinXSetting, 2.1).ok());
  EXPECT_EQ(owner.scale_random_min_factors().x, 2.1F);
  EXPECT_EQ(owner.scale_random_max_factors().x, 2.1F);
  EXPECT_FALSE(set(kPathArrayScaleOffsetXSetting, 0.0).ok());
  EXPECT_EQ(owner.scale_offset_factors().x, 0.9F);
  EXPECT_FALSE(set(kPathArrayRotationOffsetXSetting,
                   std::numeric_limits<double>::quiet_NaN())
                   .ok());
  EXPECT_FALSE(set("path_array.variation.unknown", 1.0).ok());

  ASSERT_TRUE(editor.tools().reset_active_settings().ok());
  expect_vector(owner.rotation_offset_degrees(), {});
  expect_vector(owner.rotation_random_min_degrees(), {});
  expect_vector(owner.rotation_random_max_degrees(), {});
  expect_vector(owner.scale_offset_factors(), {1.0F, 1.0F, 1.0F});
  expect_vector(owner.scale_random_min_factors(), {1.0F, 1.0F, 1.0F});
  expect_vector(owner.scale_random_max_factors(), {1.0F, 1.0F, 1.0F});
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
  ASSERT_GE(target.controls.size(), 4U);
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
  auto *count_spinner = dynamic_cast<QtToolSettingsDragSpinner *>(
      panel.findChild<QWidget *>(QStringLiteral(
          "tool_settings.control.path_array.count.spinner")));
  auto *count_editor = panel.findChild<QSpinBox *>(
      QStringLiteral("tool_settings.control.path_array.count"));
  auto *distance_slider = panel.findChild<QSlider *>(
      QStringLiteral("tool_settings.control.path_array.distance.slider"));
  ASSERT_NE(dropdown, nullptr);
  ASSERT_NE(count_spinner, nullptr);
  ASSERT_NE(count_editor, nullptr);
  ASSERT_NE(distance_slider, nullptr);
  EXPECT_FALSE(dropdown->isEnabled());
  EXPECT_EQ(dropdown->count(), 6);
  EXPECT_EQ(count_spinner->spin_box(), count_editor);
  EXPECT_EQ(count_spinner->single_step(), 1);
  EXPECT_EQ(panel.findChild<QSlider *>(QStringLiteral(
                "tool_settings.control.path_array.count.slider")),
            nullptr);
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
  EXPECT_EQ(localization.text("path_array.group.placement", "Placement"),
            "Posizionamento");
  EXPECT_EQ(localization.text("path_array.group.rotation", "Rotation"),
            "Rotazione");
  EXPECT_EQ(localization.text("path_array.group.scale", "Scale"), "Scala");
  EXPECT_EQ(localization.text("path_array.rotation_offset",
                              "Offset per Copy"),
            "Offset per copia");
  EXPECT_EQ(localization.text("path_array.rotation_random_min",
                              "Random Minimum"),
            "Minimo casuale");
  EXPECT_EQ(localization.text("path_array.scale_offset",
                              "Scale Factor per Copy"),
            "Fattore scala per copia");
  EXPECT_EQ(localization.text("path_array.scale_random_max",
                              "Random Maximum"),
            "Massimo casuale");

  ASSERT_TRUE(localization.select_locale("en-US"));
  EXPECT_EQ(localization.text("viewport.tool.path_array.label", "Path Array"),
            "Path Array");
  EXPECT_EQ(localization.text("path_array.count", "Num Copies"),
            "Num Copies");
  EXPECT_EQ(localization.text("path_array.group.placement", "Placement"),
            "Placement");
  EXPECT_EQ(localization.text("path_array.rotation_offset",
                              "Offset per Copy"),
            "Offset per Copy");
  EXPECT_EQ(localization.text("path_array.scale_offset",
                              "Scale Factor per Copy"),
            "Scale Factor per Copy");
  EXPECT_EQ(localization.text("path_array.scale_random_max",
                              "Random Maximum"),
            "Random Maximum");
}

} // namespace
} // namespace quader
