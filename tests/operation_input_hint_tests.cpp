////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "core/localization_service.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "editor/input/quader_input_binding.hpp"
#include "editor/metadata/operation_input_hint_catalog.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "support/command_behavior_fixture.hpp"
#include "ui/qt_operation_input_hints.hpp"
#include "ui/qt_application_theme.hpp"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QStatusBar>
#include <QStyle>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <string_view>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace quader {
namespace {

bool contains_command(const std::vector<QuaderOperationInputHint> &hints,
                      QuaderCommandId command) {
  return std::ranges::any_of(hints, [&](const auto &hint) {
    if (hint.command.string() == command.string()) return true;
    for (std::size_t index = 0; index < hint.related_command_count; ++index) {
      if (hint.related_commands[index].string() == command.string()) return true;
    }
    return false;
  });
}

bool contains_gesture(const std::vector<QuaderOperationInputHint> &hints,
                      QuaderOperationInputGesture gesture) {
  return std::ranges::any_of(hints, [&](const auto &hint) { return hint.gesture == gesture; });
}

bool contains_badge(const QtOperationInputHints &widget, const QString &badge) {
  return std::ranges::any_of(widget.hints(), [&](const auto &hint) { return hint.badge == badge; });
}

LocalizationService localization_for(std::string_view locale = "en-US") {
  LocalizationService localization;
  EXPECT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", std::string(locale)));
  return localization;
}

void prepare_component_context(QuaderEditorApi &editor) {
  ASSERT_TRUE(editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Component hint context").ok());
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
}

TEST(OperationInputHintTests, PrimitiveHintsFollowShapePhaseAndHoveredControl) {
  using Shape = QuaderPrimitiveCreationShape;
  using namespace quader_command;
  for (const auto shape : {Shape::Box, Shape::Cylinder, Shape::Cone, Shape::Sphere, Shape::Torus}) {
    const auto pending = primitive_creation_input_hints(shape, false);
    EXPECT_FALSE(contains_command(pending, kToolsApplyPrimitiveCreation));
    EXPECT_TRUE(contains_command(pending, kToolsCancelPrimitiveCreation));
    EXPECT_TRUE(contains_command(pending, kToolsFlipPrimitiveFaces));
    EXPECT_EQ(contains_command(pending, kToolsTogglePrimitiveCaps),
              shape == Shape::Cylinder || shape == Shape::Cone);
    EXPECT_EQ(contains_command(pending, kToolsTogglePrimitiveHemisphere), shape == Shape::Sphere);
    EXPECT_FALSE(contains_gesture(pending, QuaderOperationInputGesture::PrimaryDrag));
    EXPECT_FALSE(contains_gesture(pending, QuaderOperationInputGesture::Wheel));
    EXPECT_TRUE(contains_command(primitive_creation_input_hints(shape, true), kToolsApplyPrimitiveCreation));
    QuaderOperationInputContext context;
    context.pointer_dragging = true;
    EXPECT_FALSE(contains_command(primitive_creation_input_hints(shape, true, context), kToolsApplyPrimitiveCreation));
    context.pointer_dragging = false;
    context.primitive_control = QuaderInputPrimitiveControl::Sides;
    const auto controls = primitive_creation_input_hints(shape, true, context);
    EXPECT_TRUE(contains_gesture(controls, QuaderOperationInputGesture::Wheel));
    EXPECT_TRUE(contains_gesture(controls, QuaderOperationInputGesture::ControlPrimaryClick));
  }
}

TEST(OperationInputHintTests, IdleAndNavigationReplaceRatherThanAppendToolHints) {
  auto standalone = create_standalone_editor();
  auto &tools = standalone.editor().tools();
  QuaderOperationInputContext context;
  for (bool held : {false, true}) {
    context.shift = held;
    context.control = held;
    const auto idle = tools.active_input_hints(context);
    ASSERT_EQ(idle.size(), 3u);
    EXPECT_EQ(idle[0].label_fallback, "Orbit");
    EXPECT_EQ(idle[1].label_fallback, "Pan");
    EXPECT_EQ(idle[2].label_fallback, "Fly Navigation");
  }
  ASSERT_TRUE(tools.activate(ViewportToolId::Move).ok());
  EXPECT_EQ(tools.active_input_hints(context).size(), 3u);
  ASSERT_TRUE(tools.activate(ViewportToolId::CreatePrimitive).ok());
  context.navigation = QuaderInputNavigation::Fly;
  const auto fly = tools.active_input_hints(context);
  ASSERT_EQ(fly.size(), 3u);
  EXPECT_EQ(fly[0].related_command_count, 3u);
  EXPECT_TRUE(contains_command(fly, QuaderCommandId{"viewport.navigation.fly_left"}));
  EXPECT_TRUE(contains_command(fly, QuaderCommandId{"viewport.navigation.reset_fly_speed"}));
  EXPECT_FALSE(contains_command(fly, quader_command::kToolsCancelPrimitiveCreation));
  EXPECT_FALSE(contains_command(fly, QuaderCommandId{"viewport.navigation.cancel"}));
  context.navigation = QuaderInputNavigation::Orbit;
  ASSERT_EQ(tools.active_input_hints(context).size(), 1u);
  context.navigation = QuaderInputNavigation::Pan;
  EXPECT_TRUE(tools.active_input_hints(context).empty());
  context.navigation = QuaderInputNavigation::Zoom;
  EXPECT_EQ(tools.active_input_hints(context).size(), 1u);
  context.viewport_available = false;
  EXPECT_TRUE(tools.active_input_hints(context).empty());
  context = {};
  context.orthographic = true;
  context.orbit_available = false;
  const auto orthographic = viewport_navigation_input_hints(context);
  ASSERT_EQ(orthographic.size(), 2u);
  EXPECT_FALSE(contains_command(orthographic, QuaderCommandId{"viewport.navigation.orbit"}));
  EXPECT_EQ(orthographic.back().label_fallback, "Pan");
}

TEST(OperationInputHintTests, SelectionUsesCapturedOperationAndNeverAdvertisesBoxEscape) {
  QuaderOperationInputContext context;
  context.selection_gesture = QuaderInputSelectionGesture::Box;
  context.selection_operation = QuaderInputSelectionOperation::Add;
  context.control = true;
  auto hints = selection_gesture_input_hints(context);
  ASSERT_EQ(hints.size(), 2u);
  EXPECT_EQ(hints[0].label_fallback, "Add");
  EXPECT_EQ(hints[1].label_fallback, "Contained");
  EXPECT_FALSE(contains_command(hints, QuaderCommandId{"viewport.cancel_active_tool"}));
  EXPECT_FALSE(contains_gesture(hints, QuaderOperationInputGesture::Control));
  context.selection_crossing = true;
  EXPECT_EQ(selection_gesture_input_hints(context)[1].label_fallback, "Crossing");
  context.selection_gesture = QuaderInputSelectionGesture::Lasso;
  EXPECT_TRUE(contains_command(selection_gesture_input_hints(context), QuaderCommandId{"viewport.cancel_active_tool"}));
  context.selection_gesture = QuaderInputSelectionGesture::Paint;
  EXPECT_TRUE(selection_gesture_input_hints(context)[0].active);
  context.selection_gesture = QuaderInputSelectionGesture::EdgeRing;
  context.selection_toggle = true;
  EXPECT_EQ(selection_gesture_input_hints(context)[0].label_fallback, "Toggle Ring");
  auto standalone = create_standalone_editor();
  ASSERT_TRUE(standalone.editor().tools().activate(ViewportToolId::LassoSelect).ok());
  hints = standalone.editor().tools().active_input_hints();
  EXPECT_EQ(hints.size(), 2u);
  EXPECT_TRUE(contains_gesture(hints, QuaderOperationInputGesture::Shift));
  EXPECT_FALSE(contains_gesture(hints, QuaderOperationInputGesture::PrimaryDrag));
}

TEST(OperationInputHintTests, PaintSelectionShowsLiveModifiersWithoutBrushSettings) {
  auto standalone = create_standalone_editor();
  auto &tools = standalone.editor().tools();
  ASSERT_TRUE(tools.activate(ViewportToolId::PaintSelect).ok());
  QuaderOperationInputContext context;
  const auto initial = tools.active_input_hints_revision(context);
  const auto idle = tools.active_input_hints(context);
  ASSERT_EQ(idle.size(), 3u);
  EXPECT_EQ(idle[0].gesture, QuaderOperationInputGesture::PrimaryDrag);
  EXPECT_EQ(idle[0].label_fallback, "Paint");
  EXPECT_TRUE(contains_gesture(idle, QuaderOperationInputGesture::Shift));
  EXPECT_TRUE(contains_gesture(idle, QuaderOperationInputGesture::Control));
  for (int frame = 0; frame < 20; ++frame) {
    EXPECT_EQ(tools.active_input_hints_revision(context), initial);
  }
  context.selection_gesture = QuaderInputSelectionGesture::Paint;
  context.selection_operation = QuaderInputSelectionOperation::Remove;
  context.control = true;
  const auto painting = tools.active_input_hints(context);
  ASSERT_EQ(painting.size(), 3u);
  EXPECT_EQ(painting[0].label_fallback, "Remove");
  EXPECT_FALSE(painting[1].active);
  EXPECT_TRUE(painting[2].active);
  const auto changed = tools.active_input_hints_revision(context);
  EXPECT_GT(changed, initial);
  EXPECT_EQ(tools.active_input_hints_revision(context), changed);
}

TEST(OperationInputHintTests, OwnerRevisionTracksPrimitiveKindFootprintAndCancel) {
  const auto *contract = test::find_command_behavior_contract(quader_command::kToolsTogglePrimitiveCaps.string());
  ASSERT_NE(contract, nullptr);
  auto fixture = test::CommandBehaviorFixture::create(*contract);
  auto &tools = fixture->editor().tools();
  const auto initial = tools.active_input_hints_revision();
  for (int frame = 0; frame < 20; ++frame) EXPECT_EQ(tools.active_input_hints_revision(), initial);
  ASSERT_TRUE(tools.set_setting("create_primitive", "primitive.type", "sphere").ok());
  EXPECT_GT(tools.active_input_hints_revision(), initial);
  EXPECT_TRUE(contains_command(tools.active_input_hints(), quader_command::kToolsTogglePrimitiveHemisphere));
  const auto changed = tools.active_input_hints_revision();
  fixture->begin_primitive_footprint();
  EXPECT_GT(tools.active_input_hints_revision(), changed);
  EXPECT_FALSE(contains_command(tools.active_input_hints(), quader_command::kToolsApplyPrimitiveCreation));
  ASSERT_TRUE(tools.cancel_active_tool_preview().ok());
  EXPECT_EQ(tools.active_input_hints().size(), 3u);
  QuaderOperationInputContext context;
  const auto idle = tools.active_input_hints_revision(context);
  context.navigation = QuaderInputNavigation::Fly;
  EXPECT_GT(tools.active_input_hints_revision(context), idle);
  const auto flying = tools.active_input_hints_revision(context);
  EXPECT_EQ(tools.active_input_hints_revision(context), flying);
  context.navigation = QuaderInputNavigation::None;
  EXPECT_GT(tools.active_input_hints_revision(context), flying);
}

TEST(OperationInputHintTests,
     PrimitiveHintRevisionRetainsSemanticPhaseAcrossFootprintGeometryUpdates) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  auto &tools = editor.tools();
  auto &modeling = session.modeling();
  ASSERT_TRUE(tools.begin_primitive_creation(QuaderPrimitiveCreationShape::Box).ok());
  const ModelingBoxToolEvent start{
      .screen_position = {400.0f, 300.0f},
      .ray = {.origin = {0.0f, 4.0f, 0.0f},
              .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
  };
  const auto begun = modeling.begin_box_footprint(start);
  ASSERT_TRUE(begun.ok) << begun.message;
  ASSERT_EQ(begun.drag_state, ModelingBoxDragState::Footprint);
  QuaderOperationInputContext context;
  const auto footprint_revision = tools.active_input_hints_revision(context);
  const auto footprint_hints = tools.active_input_hints(context);
  ASSERT_FALSE(footprint_hints.empty());
  EXPECT_FALSE(contains_command(footprint_hints, quader_command::kToolsApplyPrimitiveCreation));
  const auto same_hint = [](const QuaderOperationInputHint &left,
                            const QuaderOperationInputHint &right) {
    return left.gesture == right.gesture &&
           left.command.string() == right.command.string() &&
           left.label_key == right.label_key &&
           left.label_fallback == right.label_fallback &&
           left.priority == right.priority && left.active == right.active &&
           left.related_command_count == right.related_command_count &&
           left.detail == right.detail &&
           std::ranges::equal(left.related_commands, right.related_commands,
                              [](QuaderCommandId left_command, QuaderCommandId right_command) {
                                return left_command.string() == right_command.string();
                              });
  };

  auto finish = start;
  for (const float extent : {2.0f, 3.0f, 4.0f, 5.0f}) {
    SCOPED_TRACE(extent);
    const auto preview_revision = modeling.tool_preview_source_revision();
    finish.ray.origin.x = extent;
    finish.ray.origin.z = extent + 1.0f;
    const auto preview = modeling.update_box_footprint(finish);
    ASSERT_TRUE(preview.ok) << preview.message;
    ASSERT_TRUE(preview.valid);
    EXPECT_EQ(preview.drag_state, ModelingBoxDragState::Footprint);
    EXPECT_FLOAT_EQ(preview.footprint_size.x, extent);
    EXPECT_FLOAT_EQ(preview.footprint_size.y, extent + 1.0f);
    EXPECT_GT(modeling.tool_preview_source_revision(), preview_revision);
    EXPECT_EQ(tools.active_input_hints_revision(context), footprint_revision);
    EXPECT_TRUE(std::ranges::equal(tools.active_input_hints(context),
                                  footprint_hints, same_hint));
  }

  context.control = true;
  const auto modified_revision = tools.active_input_hints_revision(context);
  EXPECT_GT(modified_revision, footprint_revision);
  EXPECT_EQ(tools.active_input_hints_revision(context), modified_revision);
  context.control = false;
  const auto unmodified_revision = tools.active_input_hints_revision(context);
  EXPECT_GT(unmodified_revision, modified_revision);
  ASSERT_TRUE(modeling.begin_box_height(finish).valid);
  ASSERT_TRUE(tools.accept_active_primitive("Hint phase box").ok());
  ASSERT_TRUE(tools.active_settings().can_apply);
  const auto accepted_revision = tools.active_input_hints_revision(context);
  EXPECT_GT(accepted_revision, unmodified_revision);
  EXPECT_TRUE(contains_command(tools.active_input_hints(context),
                               quader_command::kToolsApplyPrimitiveCreation));

  ASSERT_TRUE(tools.set_setting("create_primitive", "primitive.type", "sphere").ok());
  EXPECT_GT(tools.active_input_hints_revision(context), accepted_revision);
  EXPECT_TRUE(contains_command(tools.active_input_hints(context),
                               quader_command::kToolsTogglePrimitiveHemisphere));
}

TEST(OperationInputHintTests, UvPeelOnlyAdvertisesApplyForValidPreview) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  prepare_component_context(editor);
  ASSERT_FALSE(::testing::Test::HasFatalFailure());
  ASSERT_TRUE(editor.commands().execute(quader_command::kModelingUvPeel).ok());
  EXPECT_EQ(editor.tools().active_tool(), "uv_peel");
  const auto hints = editor.tools().active_input_hints();
  EXPECT_FALSE(contains_gesture(hints, QuaderOperationInputGesture::PrimaryClick));
  EXPECT_FALSE(contains_command(hints, QuaderCommandId{"viewport.modal_tool.finalize"}));
  EXPECT_TRUE(contains_command(hints, QuaderCommandId{"viewport.cancel_active_tool"}));
  EXPECT_TRUE(contains_command(viewport_tool_input_hints("uv_peel", {.can_apply = true}), QuaderCommandId{"viewport.modal_tool.finalize"}));
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_EQ(editor.tools().active_input_hints().size(), 3u);
}

TEST(OperationInputHintTests, ExtrudeOwnerPhasesReplaceModeChoicesWithLiveModifiers) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto box = editor.modeling().create_box({.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Hint Source");
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(box.value(), 1).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
  auto hints = editor.tools().active_input_hints();
  EXPECT_TRUE(contains_command(hints, quader_command::kToolsActivateExtrudeIndividual));
  EXPECT_FALSE(contains_command(hints, quader_command::kToolsActivateExtrude));
  EXPECT_FALSE(contains_gesture(hints, QuaderOperationInputGesture::PrimaryClick));
  const auto waiting = editor.tools().active_input_hints_revision();
  ASSERT_TRUE(session.extrude().begin_drag().ok);
  EXPECT_GT(editor.tools().active_input_hints_revision(), waiting);
  hints = editor.tools().active_input_hints();
  EXPECT_TRUE(contains_gesture(hints, QuaderOperationInputGesture::Shift));
  EXPECT_TRUE(contains_gesture(hints, QuaderOperationInputGesture::Control));
  EXPECT_TRUE(contains_command(hints, QuaderCommandId{"viewport.extrude.constraint_z"}));
  EXPECT_FALSE(contains_command(hints, QuaderCommandId{"viewport.modal_tool.finalize"}));
  EXPECT_FALSE(contains_command(hints, quader_command::kToolsActivateExtrudeIndividual));
  ASSERT_TRUE(session.extrude().release_drag().ok);
  EXPECT_TRUE(contains_command(editor.tools().active_input_hints(), QuaderCommandId{"viewport.modal_tool.finalize"}));
  ASSERT_TRUE(session.extrude().set_mode(modeling::FaceExtrudeMode::Individual).ok);
  ASSERT_TRUE(session.extrude().begin_drag().ok);
  EXPECT_FALSE(contains_command(editor.tools().active_input_hints(), QuaderCommandId{"viewport.extrude.constraint_x"}));
}

TEST(OperationInputHintTests, LinearArrayVerticalHintTracksOwnerConstraintAndHeldAlt) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Array Hint Source").ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::LinearArray).ok());
  QuaderOperationInputContext context;
  context.linear_array_endpoint_hovered = true;
  const auto initial = editor.tools().active_input_hints_revision(context);
  const auto hints = editor.tools().active_input_hints(context);
  const auto vertical = std::ranges::find_if(hints, [](const auto &hint) {
    return hint.gesture == QuaderOperationInputGesture::Alt;
  });
  ASSERT_NE(vertical, hints.end());
  for (const char *locale : {"en-US", "it-IT"}) {
    EXPECT_NE(localization_for(locale).text(vertical->label_key, "missing"), "missing");
  }
  for (int frame = 0; frame < 20; ++frame) {
    EXPECT_EQ(editor.tools().active_input_hints_revision(context), initial);
  }
  context.alt = true;
  const auto held = editor.tools().active_input_hints_revision(context);
  EXPECT_GT(held, initial);
  const auto held_hints = editor.tools().active_input_hints(context);
  EXPECT_TRUE(std::ranges::any_of(held_hints, [](const auto &hint) {
    return hint.gesture == QuaderOperationInputGesture::Alt && hint.active;
  }));
  for (const auto axis : {LinearArrayAxisConstraint::X,
                          LinearArrayAxisConstraint::Y,
                          LinearArrayAxisConstraint::Z}) {
    ASSERT_TRUE(session.linear_array().set_axis_constraint(axis).ok);
    EXPECT_GT(editor.tools().active_input_hints_revision(context), held);
    EXPECT_FALSE(contains_gesture(editor.tools().active_input_hints(context),
                                 QuaderOperationInputGesture::Alt));
  }
  ASSERT_TRUE(session.linear_array().set_axis_constraint(LinearArrayAxisConstraint::Free).ok);
  EXPECT_TRUE(contains_gesture(editor.tools().active_input_hints(context),
                              QuaderOperationInputGesture::Alt));
  context.navigation = QuaderInputNavigation::Fly;
  EXPECT_FALSE(contains_gesture(editor.tools().active_input_hints(context),
                               QuaderOperationInputGesture::Alt));
}

TEST(OperationInputHintTests, ModelingPhasesDoNotAdvertiseUnavailableApplyOrWrongModifiers) {
  const auto finalize = QuaderCommandId{"viewport.modal_tool.finalize"};
  for (const auto tool : {"knife", "poly", "cut", "mirror"}) {
    EXPECT_FALSE(contains_command(viewport_tool_input_hints(tool), finalize));
    EXPECT_TRUE(contains_command(viewport_tool_input_hints(tool, {.can_apply = true}), finalize));
    EXPECT_FALSE(contains_command(viewport_tool_input_hints(tool, {.can_apply = true, .dragging = true}), finalize));
  }
  EXPECT_FALSE(contains_gesture(viewport_tool_input_hints("knife"), QuaderOperationInputGesture::Shift));
  EXPECT_TRUE(contains_gesture(viewport_tool_input_hints("knife", {.active_stroke = true}), QuaderOperationInputGesture::Shift));
  const auto closed = viewport_tool_input_hints("poly", {.can_apply = true, .active_stroke = true, .closed = true});
  EXPECT_FALSE(contains_gesture(closed, QuaderOperationInputGesture::Shift));
  EXPECT_FALSE(contains_gesture(closed, QuaderOperationInputGesture::Control));
  EXPECT_FALSE(contains_gesture(viewport_tool_input_hints("cut"), QuaderOperationInputGesture::Control));
  EXPECT_FALSE(contains_gesture(viewport_tool_input_hints("mirror"), QuaderOperationInputGesture::Shift));
  EXPECT_FALSE(contains_gesture(viewport_tool_input_hints("insert_edge_loop"), QuaderOperationInputGesture::PrimaryDrag));
  QuaderOperationInputContext context;
  context.grid_snap_available = false;
  EXPECT_TRUE(contains_gesture(viewport_tool_input_hints("insert_edge_loop", {}, context), QuaderOperationInputGesture::Control));
  EXPECT_TRUE(contains_gesture(viewport_tool_input_hints("extrude", {.dragging = true}, context), QuaderOperationInputGesture::Control));
  const auto preview = viewport_tool_input_hints("operation", {.can_apply = true});
  EXPECT_TRUE(contains_command(preview, quader_command::kModelingApplyActivePreview));
  EXPECT_TRUE(contains_command(preview, quader_command::kModelingCancelActivePreview));
  EXPECT_FALSE(contains_command(preview, finalize));
}

TEST(OperationInputHintTests, InsertEdgeLoopAdvertisesCtrlSnapIndependentlyOfGlobalToggles) {
  auto standalone = create_standalone_editor();
  auto &tools = standalone.editor().tools();
  prepare_component_context(standalone.editor());
  ASSERT_FALSE(::testing::Test::HasFatalFailure());
  ASSERT_TRUE(tools.activate(ViewportToolId::InsertEdgeLoop).ok());
  for (const bool available : {false, true}) {
    for (const bool enabled : {false, true}) {
      QuaderOperationInputContext context;
      context.grid_snap_available = available;
      context.grid_snap_enabled = enabled;
      for (const bool control : {false, true, false}) {
        context.control = control;
        const auto hints = tools.active_input_hints(context);
        const auto snap = std::ranges::find_if(hints, [](const auto &hint) {
          return hint.gesture == QuaderOperationInputGesture::Control;
        });
        ASSERT_NE(snap, hints.end());
        EXPECT_EQ(snap->label_key, "operation.hints.insert_edge_loop.grid_snap");
        EXPECT_EQ(snap->label_fallback, "Grid Snap");
        EXPECT_EQ(snap->active, control);
        for (const char *locale : {"en-US", "it-IT"}) {
          EXPECT_NE(localization_for(locale).text(snap->label_key, "missing"), "missing");
        }
        const auto revision = tools.active_input_hints_revision(context);
        for (int frame = 0; frame < 12; ++frame) {
          EXPECT_EQ(tools.active_input_hints_revision(context), revision);
        }
      }
    }
  }
}

TEST(OperationInputHintTests, AuxiliaryContextReplacesIdleWithoutInventingModifiers) {
  auto standalone = create_standalone_editor();
  auto &tools = standalone.editor().tools();
  QuaderOperationInputContext context;
  context.point_light_range_hovered = true;
  ASSERT_EQ(tools.active_input_hints(context).size(), 1u);
  EXPECT_TRUE(contains_gesture(tools.active_input_hints(context), QuaderOperationInputGesture::PrimaryDrag));
  context.point_light_range_dragging = true;
  EXPECT_TRUE(contains_command(tools.active_input_hints(context), QuaderCommandId{"viewport.cancel_active_tool"}));
  context = {};
  context.material_transfer = QuaderInputMaterialTransfer::ContinueTexture;
  ASSERT_EQ(tools.active_input_hints(context).size(), 1u);
  EXPECT_TRUE(contains_command(tools.active_input_hints(context), QuaderCommandId{"viewport.material.transfer_face_texture"}));
  context.navigation = QuaderInputNavigation::Fly;
  EXPECT_EQ(tools.active_input_hints(context).size(), 3u);
}

TEST(OperationInputHintTests, CatalogContextsHaveTranslationsInBothLocales) {
  std::vector<QuaderOperationInputHint> hints;
  const auto append = [&](std::vector<QuaderOperationInputHint> values) {
    hints.insert(hints.end(), values.begin(), values.end());
  };
  for (const auto tool : {"select", "lasso_select", "paint_select", "extrude", "insert_edge_loop", "knife", "cut", "poly", "mirror", "uv_peel"}) {
    append(viewport_tool_input_hints(tool));
    append(viewport_tool_input_hints(tool, {.can_apply = true, .dragging = true, .active_stroke = true, .extrusion_pending = true}));
  }
  for (const auto navigation : {QuaderInputNavigation::None, QuaderInputNavigation::Orbit, QuaderInputNavigation::Pan, QuaderInputNavigation::Fly, QuaderInputNavigation::Zoom}) {
    append(viewport_navigation_input_hints({.navigation = navigation}));
  }
  append(primitive_creation_input_hints(QuaderPrimitiveCreationShape::Sphere, true));
  for (const char *locale : {"en-US", "it-IT"}) {
    const auto localization = localization_for(locale);
    for (const auto &hint : hints) EXPECT_NE(localization.text(hint.label_key, "missing"), "missing") << hint.label_key;
  }
}

/// Supplies Qt services without activating or showing a native window.
class QtOperationInputHintTests : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
    QApplication::setStyle(QStringLiteral("Fusion"));
    auto application_font = application_->font();
    if (application_font.pointSizeF() < 10.0) {
      application_font.setPointSizeF(10.0);
      application_->setFont(application_font);
    }
  }

  void prepare_widget(QtOperationInputHints &widget) {
    widget.setAttribute(Qt::WA_DontShowOnScreen);
    widget.ensurePolished();
    widget.show();
    QApplication::processEvents();
  }

  QImage render_widget(QtOperationInputHints &widget) {
    QApplication::processEvents();
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(widget.palette().color(QPalette::Window));
    widget.render(&image);
    return image;
  }

  void capture(QtOperationInputHints &widget, const QString &name) {
    const auto directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
    if (!directory.isEmpty()) {
      ASSERT_TRUE(QDir().mkpath(directory));
      ASSERT_TRUE(render_widget(widget).save(QDir(directory).filePath(name + QStringLiteral(".png"))));
    }
  }

  int argc_ = 1;
  char name_[24] = "operation-input-hints";
  char *argv_[2] = {name_, nullptr};
  std::unique_ptr<QApplication> application_;
};

TEST_F(QtOperationInputHintTests, RebindingUnassignmentAndLocaleRefreshWithoutIdleProjection) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  auto localization = localization_for();
  QWidget parent;
  QtOperationInputHints widget(&parent);
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Shift+F")));
  const auto revision = widget.projection_revision();
  const auto *items = widget.hints().data();
  for (int frame = 0; frame < 20; ++frame) {
    widget.refresh(editor.tools(), editor.commands(), localization);
  }
  EXPECT_EQ(widget.projection_revision(), revision);
  EXPECT_EQ(widget.hints().data(), items);
  EXPECT_FALSE(parent.isVisible());

  ASSERT_TRUE(editor.commands().set_shortcut(quader_command::kToolsFlipPrimitiveFaces, "Ctrl+K D").ok());
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("Shift+F")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Ctrl+K D")));
  ASSERT_TRUE(editor.commands().remove_shortcut(quader_command::kToolsFlipPrimitiveFaces).ok());
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("Ctrl+K D")));
  ASSERT_TRUE(editor.commands().reset_shortcut(quader_command::kToolsFlipPrimitiveFaces).ok());
  ASSERT_TRUE(localization.select_locale("it-IT"));
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_TRUE(std::ranges::any_of(widget.hints(), [](const auto &hint) {
    return hint.badge == QStringLiteral("Shift+F") && hint.label == QStringLiteral("Inverti facce");
  }));
}

TEST_F(QtOperationInputHintTests, IdleAndNavigationReplaceRatherThanAppendHints) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  auto localization = localization_for();
  QtOperationInputHints widget;
  QuaderOperationInputContext context;
  widget.refresh(editor.tools(), editor.commands(), localization, context);
  ASSERT_EQ(widget.hints().size(), 3u);
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("MMB Drag")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Shift + MMB Drag")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("RMB Drag")));
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("LMB")));
  const auto idle = widget.hints();
  context.navigation = QuaderInputNavigation::Fly;
  widget.refresh(editor.tools(), editor.commands(), localization, context);
  ASSERT_EQ(widget.hints().size(), 3u);
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("Esc")));
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("RMB Drag")));
  EXPECT_TRUE(std::ranges::any_of(widget.hints(), [](const auto &hint) {
    return hint.label == QStringLiteral("Move") && hint.badge.contains(QStringLiteral("W")) &&
           hint.badge.contains(QStringLiteral("A")) && hint.badge.contains(QStringLiteral("S")) &&
           hint.badge.contains(QStringLiteral("D"));
  }));
  const auto revision = widget.projection_revision();
  for (int frame = 0; frame < 20; ++frame) {
    widget.refresh(editor.tools(), editor.commands(), localization, context);
  }
  EXPECT_EQ(widget.projection_revision(), revision);
  context.navigation = QuaderInputNavigation::None;
  widget.refresh(editor.tools(), editor.commands(), localization, context);
  EXPECT_EQ(widget.hints(), idle);
  context.navigation = QuaderInputNavigation::Pan;
  widget.refresh(editor.tools(), editor.commands(), localization, context);
  EXPECT_TRUE(widget.hints().empty());
  context.navigation = QuaderInputNavigation::Orbit;
  widget.refresh(editor.tools(), editor.commands(), localization, context);
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("Esc")));
}

TEST_F(QtOperationInputHintTests, InsertEdgeLoopRowShowsAvailableAndHeldCtrlSnap) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  prepare_component_context(editor);
  ASSERT_FALSE(::testing::Test::HasFatalFailure());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::InsertEdgeLoop).ok());
  auto localization = localization_for();
  QtOperationInputHints widget;
  QuaderOperationInputContext context;
  context.grid_snap_available = false;
  for (const bool control : {false, true, false}) {
    context.control = control;
    widget.refresh(editor.tools(), editor.commands(), localization, context);
    const auto snap = std::ranges::find_if(widget.hints(), [](const auto &hint) {
      return hint.badge == QStringLiteral("Ctrl");
    });
    ASSERT_NE(snap, widget.hints().end());
    EXPECT_EQ(snap->label, QStringLiteral("Grid Snap"));
    EXPECT_EQ(snap->active, control);
    const auto revision = widget.projection_revision();
    for (int frame = 0; frame < 20; ++frame) {
      widget.refresh(editor.tools(), editor.commands(), localization, context);
    }
    EXPECT_EQ(widget.projection_revision(), revision);
  }
  ASSERT_TRUE(localization.select_locale("it-IT"));
  widget.refresh(editor.tools(), editor.commands(), localization, context);
  EXPECT_TRUE(std::ranges::any_of(widget.hints(), [](const auto &hint) {
    return hint.badge == QStringLiteral("Ctrl") &&
           hint.label == QStringLiteral("Snap alla griglia") && !hint.active;
  }));
}

TEST_F(QtOperationInputHintTests, LocalOverridesSuppressFallbackAndRetainTheirSourceRevision) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  auto localization = localization_for();
  QtOperationInputHints widget;
  using Gesture = QuaderOperationInputGesture;
  std::array hints{QuaderOperationInputHint{Gesture::Shift, {}, "test.precision", "Precision", 90, false}};
  widget.refresh(editor.tools(), editor.commands(), localization);
  const auto idle = widget.hints();
  widget.refresh_snapshot(hints, 1, &hints, editor.commands(), localization);
  ASSERT_EQ(widget.hints().size(), 1u);
  EXPECT_EQ(widget.hints()[0].label, QStringLiteral("Precision"));
  const auto revision = widget.projection_revision();
  const auto *retained = widget.hints().data();
  for (int frame = 0; frame < 20; ++frame) {
    widget.refresh_snapshot(hints, 1, &hints, editor.commands(), localization);
  }
  EXPECT_EQ(widget.projection_revision(), revision);
  EXPECT_EQ(widget.hints().data(), retained);
  hints[0].active = true;
  widget.refresh_snapshot(hints, 2, &hints, editor.commands(), localization);
  EXPECT_TRUE(widget.hints()[0].active);
  EXPECT_GT(widget.projection_revision(), revision);
  widget.refresh_snapshot({}, 3, &hints, editor.commands(), localization);
  EXPECT_TRUE(widget.hints().empty());
  EXPECT_TRUE(widget.isHidden());
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_EQ(widget.hints(), idle);
  widget.invalidate();
  EXPECT_TRUE(widget.hints().empty());
  EXPECT_TRUE(widget.visible_hint_indices().empty());
}

TEST_F(QtOperationInputHintTests, GroupedBindingsCollapseWheelPairsAndPreserveOverrides) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  auto localization = localization_for();
  QtOperationInputHints widget;
  const QuaderCommandId increase{"viewport.radial_array.increase_copies"};
  const QuaderCommandId decrease{"viewport.radial_array.decrease_copies"};
  std::array hints{QuaderOperationInputHint{
      QuaderOperationInputGesture::Keybinding, increase, "test.copies", "Copies",
      70, false, {decrease}, 1}};
  widget.refresh_snapshot(hints, 1, &hints, editor.commands(), localization);
  ASSERT_EQ(widget.hints().size(), 1u);
  EXPECT_EQ(widget.hints()[0].badge, QStringLiteral("Shift + Wheel"));
  ASSERT_TRUE(editor.commands().set_shortcut(increase, "Ctrl+Alt+WheelUp").ok());
  widget.refresh_snapshot(hints, 1, &hints, editor.commands(), localization);
  EXPECT_EQ(widget.hints()[0].badge,
            QStringLiteral("Ctrl + Alt + Wheel Up / Shift + Wheel Down"));
  ASSERT_TRUE(editor.commands().remove_shortcut(decrease).ok());
  widget.refresh_snapshot(hints, 1, &hints, editor.commands(), localization);
  EXPECT_EQ(widget.hints()[0].badge, QStringLiteral("Ctrl + Alt + Wheel Up"));
  ASSERT_TRUE(editor.commands().remove_shortcut(increase).ok());
  widget.refresh_snapshot(hints, 1, &hints, editor.commands(), localization);
  EXPECT_TRUE(widget.hints().empty());
}

TEST_F(QtOperationInputHintTests, DisabledCommandsAreOmittedAndIntrinsicGesturesStayDistinct) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  auto localization = localization_for();
  using Gesture = QuaderOperationInputGesture;
  std::array hints{
      QuaderOperationInputHint{Gesture::Command, quader_command::kToolsApplyPrimitiveCreation,
                              "test.apply", "Apply"},
      QuaderOperationInputHint{Gesture::PrimaryClick, {}, "test.click", "Click"},
      QuaderOperationInputHint{Gesture::PrimaryDrag, {}, "test.drag", "Drag"},
      QuaderOperationInputHint{Gesture::PrimaryDoubleClick, {}, "test.double", "Double"},
      QuaderOperationInputHint{Gesture::Information, {}, "test.mode", "Crossing"}};
  QtOperationInputHints widget;
  widget.refresh_snapshot(hints, 1, &hints, editor.commands(), localization);
  ASSERT_EQ(widget.hints().size(), 4u);
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("LMB")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("LMB Drag")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Double LMB")));
  EXPECT_TRUE(widget.hints().back().badge.isEmpty());
  EXPECT_EQ(widget.hints().back().label, QStringLiteral("Crossing"));
  EXPECT_FALSE(widget.accessibleName().contains(QStringLiteral("Apply")));
}

TEST_F(QtOperationInputHintTests, FixedNavigationModifierSurvivesHeldBindingRemapping) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  auto localization = localization_for();
  using Gesture = QuaderOperationInputGesture;
  const QuaderCommandId quick_pivot{"viewport.quick_pivot"};
  const QuaderCommandId orbit_snap{"viewport.navigation.orbit_snap"};
  std::array hints{
      QuaderOperationInputHint{Gesture::KeybindingHold, quick_pivot, "test.pivot", "Edit Pivot"},
      QuaderOperationInputHint{Gesture::KeybindingModifier, orbit_snap, "test.orbit", "Snap View"}};
  QtOperationInputHints widget;
  widget.refresh_snapshot(hints, 1, &hints, editor.commands(), localization);
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("D held")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Alt")));
  EXPECT_FALSE(editor.commands().set_shortcut(orbit_snap, "F19").ok());
  ASSERT_TRUE(editor.commands().set_shortcut(quick_pivot, "F20").ok());
  widget.refresh_snapshot(hints, 1, &hints, editor.commands(), localization);
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("F20 held")));
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("F19")));
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("Alt")));
}

TEST_F(QtOperationInputHintTests, DynamicTargetNamesParticipateInRetainedPresentation) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  auto localization = localization_for();
  std::array hints{QuaderOperationInputHint{QuaderOperationInputGesture::Information,
                                          {}, "test.parent", "Parent to"}};
  hints[0].detail = "Box";
  QtOperationInputHints widget;
  widget.refresh_snapshot(hints, 1, &hints, editor.commands(), localization);
  ASSERT_EQ(widget.hints().size(), 1u);
  EXPECT_EQ(widget.hints()[0].label, QStringLiteral("Parent to Box"));
  EXPECT_EQ(widget.accessibleName(), QStringLiteral("Parent to Box"));
  hints[0].detail = "Group";
  widget.refresh_snapshot(hints, 2, &hints, editor.commands(), localization);
  EXPECT_EQ(widget.hints()[0].label, QStringLiteral("Parent to Group"));
  EXPECT_EQ(widget.toolTip(), QStringLiteral("Parent to Group"));
}

TEST_F(QtOperationInputHintTests, NarrowRowsKeepWholePriorityGroupsAndHeldModifierWins) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  auto localization = localization_for();
  using Gesture = QuaderOperationInputGesture;
  std::array hints{
      QuaderOperationInputHint{Gesture::Shift, {}, "test.equal", "Same Length", 10},
      QuaderOperationInputHint{Gesture::Shift, {}, "test.equal", "Same Length", 20},
      QuaderOperationInputHint{Gesture::Shift, {}, "test.equal", "Same Length", 90}};
  QtOperationInputHints widget;
  widget.refresh_snapshot(std::span(hints).last(1), 1, &hints, editor.commands(), localization);
  const int one_width = widget.sizeHint().width();
  prepare_widget(widget);
  widget.resize(one_width, widget.sizeHint().height());
  widget.refresh_snapshot(hints, 2, &hints, editor.commands(), localization);
  QApplication::processEvents();
  EXPECT_EQ(widget.visible_hint_indices(), (std::vector<std::size_t>{2}));
  hints[0].active = true;
  widget.refresh_snapshot(hints, 3, &hints, editor.commands(), localization);
  EXPECT_EQ(widget.visible_hint_indices(), (std::vector<std::size_t>{0}));
  widget.resize(one_width - 1, widget.height());
  QApplication::processEvents();
  EXPECT_TRUE(widget.visible_hint_indices().empty());
  EXPECT_FALSE(widget.hasHeightForWidth());
  widget.resize(widget.sizeHint());
  QApplication::processEvents();
  EXPECT_EQ(widget.visible_hint_indices(), (std::vector<std::size_t>{0, 1, 2}));
}

TEST_F(QtOperationInputHintTests, NativeRowRespectsRtlPaletteAndReversibleScaling) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  auto localization = localization_for();
  using Gesture = QuaderOperationInputGesture;
  std::array hints{
      QuaderOperationInputHint{Gesture::Shift, {}, "test.precision", "Precision", 90, true},
      QuaderOperationInputHint{Gesture::Control, {}, "test.snap", "Snap", 80},
      QuaderOperationInputHint{Gesture::ControlShift, {}, "test.fine", "Fine Snap", 60},
      QuaderOperationInputHint{Gesture::EscapeOrRightClick, {}, "test.cancel", "Cancel Value", 95},
      QuaderOperationInputHint{Gesture::ControlPrimaryDrag, {}, "test.drag", "Move", 70},
      QuaderOperationInputHint{Gesture::MiddleDoubleClick, {}, "test.middle", "Frame", 65},
      QuaderOperationInputHint{Gesture::Wheel, {}, "test.wheel", "Zoom", 50}};
  QtOperationInputHints widget;
  for (const auto *path : {":/quader/icons/ph_mouse-left-click-fill.png",
                           ":/quader/icons/ph_mouse-middle-click-fill.png",
                           ":/quader/icons/ph_mouse-right-click-fill.png"}) {
    ASSERT_FALSE(QImage(QString::fromLatin1(path)).isNull());
  }
  widget.setPalette(qt_obsidian_application_palette(application_->palette()));
  widget.refresh_snapshot(hints, 1, &hints, editor.commands(), localization);
  prepare_widget(widget);
  const QFont baseline = widget.font();
  const auto baseline_size = widget.sizeHint();
  const auto revision = widget.projection_revision();
  for (const float scale : {1.0f, 2.0f, 1.0f}) {
    widget.apply_ui_scale(QtUiScaleMetrics(scale));
    QFont scaled = baseline;
    if (baseline.pointSizeF() > 0) {
      scaled.setPointSizeF(baseline.pointSizeF() * scale);
    } else {
      scaled.setPixelSize(qRound(baseline.pixelSize() * scale));
    }
    widget.setFont(scaled);
    for (const int width : {320, 1200}) {
      widget.resize(qRound(width * scale), widget.sizeHint().height());
      QApplication::processEvents();
      EXPECT_EQ(widget.height(), widget.sizeHint().height());
      EXPECT_FALSE(widget.hasHeightForWidth());
      capture(widget, QStringLiteral("input-hints-%1-%2").arg(width).arg(qRound(scale * 100)));
    }
  }
  EXPECT_EQ(widget.sizeHint(), baseline_size);
  EXPECT_EQ(widget.projection_revision(), revision);
  widget.resize(widget.sizeHint());
  const auto ltr = render_widget(widget);
  widget.setLayoutDirection(Qt::RightToLeft);
  const auto rtl = render_widget(widget);
  EXPECT_NE(ltr, rtl);
  EXPECT_EQ(widget.visible_hint_indices().size(), hints.size());
  capture(widget, QStringLiteral("input-hints-rtl-100"));
  QPalette contrast;
  for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    contrast.setColor(group, QPalette::Window, Qt::black);
    contrast.setColor(group, QPalette::Button, Qt::black);
    contrast.setColor(group, QPalette::WindowText, Qt::white);
    contrast.setColor(group, QPalette::ButtonText, Qt::white);
    contrast.setColor(group, QPalette::Text, Qt::white);
    contrast.setColor(group, QPalette::Midlight, Qt::white);
    contrast.setColor(group, QPalette::Highlight, Qt::yellow);
    contrast.setColor(group, QPalette::HighlightedText, Qt::black);
  }
  widget.setPalette(contrast);
  const auto contrasted = render_widget(widget);
  EXPECT_NE(contrasted, rtl);
  int highlighted_pixels = 0;
  for (int y = 0; y < contrasted.height(); ++y) {
    for (int x = 0; x < contrasted.width(); ++x) {
      highlighted_pixels += contrasted.pixelColor(x, y) == QColor(Qt::yellow) ? 1 : 0;
    }
  }
  EXPECT_GT(highlighted_pixels, 20);
  capture(widget, QStringLiteral("input-hints-high-contrast"));
  widget.setEnabled(false);
  capture(widget, QStringLiteral("input-hints-disabled"));
  widget.setEnabled(true);
  QPalette light = QApplication::style()->standardPalette();
  for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    light.setColor(group, QPalette::Window, Qt::white);
    light.setColor(group, QPalette::WindowText, Qt::black);
    light.setColor(group, QPalette::Button, Qt::white);
    light.setColor(group, QPalette::ButtonText, Qt::black);
    light.setColor(group, QPalette::Text, Qt::black);
  }
  widget.setPalette(light);
  capture(widget, QStringLiteral("input-hints-native-light"));
}

TEST_F(QtOperationInputHintTests, SameFacadeSceneRebindRetiresPreviousHints) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto created = editor.project().create(std::filesystem::path(directory.path().toStdWString()));
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(editor.tools().set_setting("create_primitive", "primitive.type", "sphere").ok());
  auto localization = localization_for();
  QtOperationInputHints widget;
  auto *const tools = &editor.tools();
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_TRUE(contains_badge(widget, QStringLiteral("H")));
  const auto revision = widget.projection_revision();
  ASSERT_TRUE(editor.scene_documents().new_scene().ok());
  EXPECT_EQ(&editor.tools(), tools);
  widget.refresh(editor.tools(), editor.commands(), localization);
  EXPECT_GT(widget.projection_revision(), revision);
  ASSERT_EQ(widget.hints().size(), 3u);
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("H")));
  EXPECT_FALSE(contains_badge(widget, QStringLiteral("LMB")));
}

} // namespace
} // namespace quader
