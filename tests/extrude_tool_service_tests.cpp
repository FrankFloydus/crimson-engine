////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "core/undo_redo_manager.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "support/curve_modeling_test_support.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace quader {
namespace {

/// Exercises the scene-owned tool using public document and selection setup.
class ExtrudeToolServiceTests : public testing::Test {
protected:
  ExtrudeToolServiceTests()
      : defaults_(std::make_shared<ExtrudeSessionDefaults>()),
        session_(nullptr, defaults_),
        editor_(QuaderApiSessionInternalAccess::from_scene_session(session_)) {}

  QuaderModelingObjectRef create_strip(bool sloped = false) {
    quader_poly::Document document;
    const float step = sloped ? -1.0F : 0.0F;
    const std::array vertices{
        quader_poly::add_vertex(document, {0.0F, 0.0F, 0.0F}),
        quader_poly::add_vertex(document, {1.0F, 0.0F, step}),
        quader_poly::add_vertex(document, {2.0F, 0.0F, 2.0F * step}),
        quader_poly::add_vertex(document, {0.0F, 1.0F, 0.0F}),
        quader_poly::add_vertex(document, {1.0F, 1.0F, step}),
        quader_poly::add_vertex(document, {2.0F, 1.0F, 2.0F * step})};
    (void)quader_poly::add_face(
        document, std::array{vertices[0], vertices[1], vertices[4], vertices[3]});
    (void)quader_poly::add_face(
        document, std::array{vertices[1], vertices[2], vertices[5], vertices[4]});
    const auto created = editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Extrusion strip"});
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.ok() ? created.value() : QuaderModelingObjectRef{};
  }

  void select_strip(QuaderModelingObjectRef strip) {
    if (session_.modeling().selection_mode() != ModelingSelectionMode::Face) {
      ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Face).ok());
    }
    ASSERT_TRUE(editor_.selection().select_faces(strip, {1, 2}).ok());
  }

  QuaderMeshAsset mesh(QuaderModelingObjectRef object) {
    const auto compiled = editor_.query().compiled_object_mesh(object);
    EXPECT_TRUE(compiled.has_value());
    return compiled.value_or(QuaderMeshAsset{});
  }

  void expect_mesh(QuaderModelingObjectRef object,
                   const QuaderMeshAsset &expected) {
    test::curve::expect_same_mesh_geometry(mesh(object), expected);
  }

  float maximum_height(QuaderModelingObjectRef object) {
    float result = -std::numeric_limits<float>::infinity();
    for (const auto &vertex : mesh(object).vertices) {
      result = std::max(result, vertex.position.z);
    }
    return result;
  }

  std::shared_ptr<ExtrudeSessionDefaults> defaults_;
  SceneSession session_;
  QuaderEditorApi editor_;
};

TEST_F(ExtrudeToolServiceTests, ActivationExtrudesAndAdjustmentsKeepOneUndoStep) {
  const auto strip = create_strip();
  select_strip(strip);
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  EXPECT_TRUE(tool.active());
  EXPECT_TRUE(tool.adjustable());
  EXPECT_EQ(session_.modeling().selection_summary().selected_face_count, 2U);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  EXPECT_NEAR(maximum_height(strip), 1.0 / 32.0, 0.00001);
  ASSERT_TRUE(tool.set_mode(modeling::FaceExtrudeMode::Individual).ok);
  ASSERT_TRUE(tool.set_amount(2.15).ok);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  EXPECT_NEAR(maximum_height(strip), 2.15 / 32.0, 0.00001);
  const auto final = mesh(strip);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, original);
  ASSERT_TRUE(session_.history().redo().ok);
  expect_mesh(strip, final);
  EXPECT_FALSE(tool.adjustable());
}

TEST_F(ExtrudeToolServiceTests, RepeatedExtrusionsAreSeparateUndoSteps) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  const auto original = mesh(strip);
  ASSERT_TRUE(tool.activate().ok);
  const auto first = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.activate().ok);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  EXPECT_NEAR(maximum_height(strip), 2.0 / 32.0, 0.00001);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, first);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, original);
}

TEST_F(ExtrudeToolServiceTests, EmptyActivationAndSelectionDoNotCreateGeometry) {
  const auto strip = create_strip();
  ASSERT_TRUE(editor_.selection().clear().ok());
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  EXPECT_EQ(session_.modeling().selection_mode(), ModelingSelectionMode::Face);
  EXPECT_FALSE(tool.has_selected_faces());
  select_strip(strip);
  ASSERT_TRUE(tool.refresh().ok);
  EXPECT_TRUE(tool.active());
  EXPECT_TRUE(tool.has_selected_faces());
  expect_mesh(strip, original);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(tool.extrude().ok);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
}

TEST_F(ExtrudeToolServiceTests, ZeroSettingsRestoreTheOriginalAndRemainEditable) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.set_amount(0.0).ok);
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.activate().ok);
  EXPECT_EQ(session_.history().undo_count(), steps);
  expect_mesh(strip, original);
  ASSERT_TRUE(tool.set_amount(1.0).ok);
  ASSERT_TRUE(tool.extrude().ok);
  ASSERT_TRUE(tool.set_amount(0.0).ok);
  expect_mesh(strip, original);
  EXPECT_TRUE(tool.adjustable());
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  EXPECT_NEAR(maximum_height(strip), 2.0 / 32.0, 0.00001);
}

TEST_F(ExtrudeToolServiceTests, SelectionAndHistoryChangesRetireAdjustments) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  const auto first = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(editor_.selection().clear().ok());
  ASSERT_TRUE(tool.set_amount(4.0).ok);
  EXPECT_FALSE(tool.adjustable());
  expect_mesh(strip, first);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(session_.history().undo().ok);
  ASSERT_TRUE(session_.history().redo().ok);
  ASSERT_TRUE(tool.set_amount(3.0).ok);
  expect_mesh(strip, first);
  EXPECT_EQ(session_.history().undo_count(), steps);
}

TEST_F(ExtrudeToolServiceTests, SharedDefaultsSurviveToolAndSceneChanges) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_options({modeling::FaceExtrudeMode::Individual, 2.15}).ok);
  const auto committed = mesh(strip);
  ASSERT_TRUE(tool.cancel().ok);
  expect_mesh(strip, committed);
  SceneSession second(nullptr, defaults_);
  EXPECT_EQ(second.extrude().mode(), modeling::FaceExtrudeMode::Individual);
  EXPECT_DOUBLE_EQ(second.extrude().amount(), 2.15);
  EXPECT_DOUBLE_EQ(ExtrudeSessionDefaults{}.amount, 1.0);
  ASSERT_TRUE(tool.activate().ok);
  EXPECT_NEAR(maximum_height(strip), 4.30 / 32.0, 0.00001);
}

TEST_F(ExtrudeToolServiceTests, DragCancelRestoresGeometryHistoryAndSettings) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  const auto committed = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.begin_drag().ok);
  EXPECT_DOUBLE_EQ(tool.amount(), 0.0);
  expect_mesh(strip, committed);
  ASSERT_TRUE(tool.update_drag_world_amount(0.5).ok);
  EXPECT_DOUBLE_EQ(tool.amount(), 16.0);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(tool.cancel_drag().ok);
  EXPECT_TRUE(tool.active());
  EXPECT_FALSE(tool.dragging());
  EXPECT_TRUE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), 1.0);
  expect_mesh(strip, committed);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  EXPECT_EQ(session_.history().undo_count(), steps);
  EXPECT_NEAR(maximum_height(strip), 2.0 / 32.0, 0.00001);
}

TEST_F(ExtrudeToolServiceTests, CompletedNewDragKeepsASeparateUndoStep) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  const auto first = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.begin_drag().ok);
  EXPECT_FALSE(tool.adjusting());
  EXPECT_DOUBLE_EQ(tool.amount(), 0.0);
  ASSERT_TRUE(tool.update_drag(3.0).ok);
  ASSERT_TRUE(tool.end_drag().ok);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  EXPECT_NEAR(maximum_height(strip), 4.0 / 32.0, 0.00001);
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  EXPECT_NEAR(maximum_height(strip), 3.0 / 32.0, 0.00001);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, first);
}

TEST_F(ExtrudeToolServiceTests, FailedDragBeginRetainsPreviousEditableExtrusion) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  const auto accepted = mesh(strip);
  const auto steps = session_.history().undo_count();
  for (const bool adjusting : {false, true}) {
    SCOPED_TRACE(adjusting);
    session_.modeling().set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::AfterSdkMutation);
    const auto failed = adjusting ? tool.begin_adjust_drag() : tool.begin_drag();
    session_.modeling().set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(tool.dragging());
    EXPECT_FALSE(tool.can_update_drag());
    EXPECT_TRUE(tool.adjustable());
    EXPECT_DOUBLE_EQ(tool.amount(), 1.0);
    EXPECT_EQ(session_.history().undo_count(), steps);
    expect_mesh(strip, accepted);
  }
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  EXPECT_EQ(session_.history().undo_count(), steps);
  EXPECT_NEAR(maximum_height(strip), 2.0 / 32.0, 0.00001);
}

TEST_F(ExtrudeToolServiceTests, DragCommitCreatesOneAdjustableUndoStep) {
  const auto strip = create_strip();
  ASSERT_TRUE(editor_.selection().clear().ok());
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  select_strip(strip);
  ASSERT_TRUE(tool.active());
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.begin_drag().ok);
  ASSERT_TRUE(tool.update_drag(1.25).ok);
  ASSERT_TRUE(tool.update_drag(2.15).ok);
  ASSERT_TRUE(tool.end_drag().ok);
  EXPECT_TRUE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), 2.15);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  ASSERT_TRUE(tool.set_mode(modeling::FaceExtrudeMode::Individual).ok);
  ASSERT_TRUE(tool.set_amount(3.0).ok);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  const auto final = mesh(strip);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, original);
  ASSERT_TRUE(session_.history().redo().ok);
  expect_mesh(strip, final);
}

TEST_F(ExtrudeToolServiceTests, InvalidSettingsPreserveAcceptedGeometryAndHistory) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  const auto committed = mesh(strip);
  const auto steps = session_.history().undo_count();
  EXPECT_FALSE(tool.set_amount(std::numeric_limits<double>::quiet_NaN()).ok);
  EXPECT_FALSE(tool.set_amount(std::numeric_limits<double>::infinity()).ok);
  EXPECT_FALSE(tool.set_amount(std::numeric_limits<double>::max()).ok);
  EXPECT_FALSE(tool.set_mode(static_cast<modeling::FaceExtrudeMode>(255)).ok);
  EXPECT_DOUBLE_EQ(tool.amount(), 1.0);
  EXPECT_TRUE(tool.adjustable());
  expect_mesh(strip, committed);
  EXPECT_EQ(session_.history().undo_count(), steps);
}

TEST_F(ExtrudeToolServiceTests, UnchangedFramesDoNotCaptureOrRegenerateGeometry) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.handle_state().active);
  const auto work = tool.work_stats_for_tests();
  const auto revision = tool.tool_settings_revision();
  for (int frame = 0; frame != 20; ++frame) {
    EXPECT_TRUE(tool.refresh().ok);
    EXPECT_TRUE(tool.handle_state().active);
    EXPECT_EQ(tool.tool_settings_revision(), revision);
  }
  EXPECT_EQ(tool.work_stats_for_tests().source_capture_count,
            work.source_capture_count);
  EXPECT_EQ(tool.work_stats_for_tests().geometry_update_count,
            work.geometry_update_count);
}

TEST_F(ExtrudeToolServiceTests, FailedAdjustmentPreservesItsAcceptedSettings) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  const auto committed = mesh(strip);
  const auto steps = session_.history().undo_count();
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  const auto failed = tool.set_amount(3.0);
  EXPECT_FALSE(failed.ok);
  EXPECT_DOUBLE_EQ(tool.amount(), 1.0);
  expect_mesh(strip, committed);
  EXPECT_EQ(session_.history().undo_count(), steps);
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  EXPECT_EQ(session_.history().undo_count(), steps);
  EXPECT_NEAR(maximum_height(strip), 2.0 / 32.0, 0.00001);
}

TEST_F(ExtrudeToolServiceTests, FailedActivationPreservesGeometryAndPriorTool) {
  const auto strip = create_strip();
  select_strip(strip);
  ASSERT_TRUE(session_.modeling().activate_viewport_tool(
      ModelingViewportTool::Move).ok);
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  EXPECT_FALSE(session_.extrude().activate().ok);
  EXPECT_FALSE(session_.extrude().active());
  EXPECT_EQ(session_.modeling().tool_status().active_tool,
            ModelingViewportTool::Move);
  expect_mesh(strip, original);
  EXPECT_EQ(session_.history().undo_count(), steps);
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
}

TEST_F(ExtrudeToolServiceTests, PresetActivationStartsANewExtrusionAndRemembersMode) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  const auto first = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.activate(modeling::FaceExtrudeMode::Individual).ok);
  EXPECT_EQ(tool.mode(), modeling::FaceExtrudeMode::Individual);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  EXPECT_NEAR(maximum_height(strip), 2.0 / 32.0, 0.00001);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, first);
}

TEST_F(ExtrudeToolServiceTests, EmptyPresetActivationOnlyArmsTheChosenMode) {
  const auto strip = create_strip();
  ASSERT_TRUE(editor_.selection().clear().ok());
  auto &tool = session_.extrude();
  const auto before = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.activate(modeling::FaceExtrudeMode::AlongNormals).ok);
  EXPECT_TRUE(tool.active());
  EXPECT_EQ(tool.mode(), modeling::FaceExtrudeMode::AlongNormals);
  expect_mesh(strip, before);
  EXPECT_EQ(session_.history().undo_count(), steps);
  select_strip(strip);
  ASSERT_TRUE(tool.extrude().ok);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
}

TEST_F(ExtrudeToolServiceTests, FailedPresetActivationPreservesPreviousAdjustment) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  const auto before = mesh(strip);
  const auto steps = session_.history().undo_count();
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  EXPECT_FALSE(tool.activate(modeling::FaceExtrudeMode::Individual).ok);
  EXPECT_TRUE(tool.active());
  EXPECT_TRUE(tool.adjustable());
  EXPECT_EQ(tool.mode(), modeling::FaceExtrudeMode::Region);
  EXPECT_DOUBLE_EQ(tool.amount(), 1.0);
  expect_mesh(strip, before);
  EXPECT_EQ(session_.history().undo_count(), steps);
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  EXPECT_EQ(session_.history().undo_count(), steps);
  EXPECT_NEAR(maximum_height(strip), 2.0 / 32.0, 0.00001);
}

TEST_F(ExtrudeToolServiceTests, AdjustmentDragStartsAtAcceptedAmountAndMergesHistory) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  const auto original = mesh(strip);
  ASSERT_TRUE(tool.activate().ok);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  EXPECT_TRUE(tool.adjusting());
  EXPECT_DOUBLE_EQ(tool.amount(), 1.0);
  ASSERT_TRUE(tool.update_drag(2.15).ok);
  ASSERT_TRUE(tool.end_drag().ok);
  EXPECT_FALSE(tool.adjusting());
  EXPECT_TRUE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), 2.15);
  EXPECT_EQ(session_.history().undo_count(), steps);
  EXPECT_NEAR(maximum_height(strip), 2.15 / 32.0, 0.00001);
  const auto after = mesh(strip);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, original);
  ASSERT_TRUE(session_.history().redo().ok);
  expect_mesh(strip, after);
}

TEST_F(ExtrudeToolServiceTests,
       RepeatedAdjustmentDragsMergeUntilTheNextExtrudeCommand) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(editor_.commands()
                  .execute(quader_command::kToolsActivateExtrude)
                  .ok());

  for (const double amount : {2.15, 0.0, -1.25, 3.25}) {
    SCOPED_TRACE(amount);
    const double accepted_amount = tool.amount();
    ASSERT_TRUE(tool.begin_adjust_drag().ok);
    EXPECT_TRUE(tool.adjusting());
    EXPECT_DOUBLE_EQ(tool.amount(), accepted_amount);
    ASSERT_TRUE(tool.update_drag(amount).ok);
    ASSERT_TRUE(tool.end_drag().ok);
    EXPECT_TRUE(tool.adjustable());
    EXPECT_DOUBLE_EQ(tool.amount(), amount);
    EXPECT_EQ(session_.history().undo_count(), steps + 1);
    EXPECT_EQ(session_.modeling().selection_summary().selected_face_count, 2U);
    if (amount == 0.0) {
      expect_mesh(strip, original);
    }
  }
  const auto first = mesh(strip);
  EXPECT_NEAR(maximum_height(strip), 3.25 / 32.0, 0.00001);

  ASSERT_TRUE(editor_.commands()
                  .execute(quader_command::kToolsActivateExtrude)
                  .ok());
  EXPECT_DOUBLE_EQ(tool.amount(), 3.25);
  EXPECT_NEAR(maximum_height(strip), 6.5 / 32.0, 0.00001);
  for (const double amount : {0.0, -2.0, 1.75}) {
    SCOPED_TRACE(amount);
    ASSERT_TRUE(tool.begin_adjust_drag().ok);
    ASSERT_TRUE(tool.update_drag(amount).ok);
    ASSERT_TRUE(tool.end_drag().ok);
    EXPECT_TRUE(tool.adjustable());
    EXPECT_DOUBLE_EQ(tool.amount(), amount);
    EXPECT_EQ(session_.history().undo_count(), steps + 2);
    if (amount == 0.0) {
      expect_mesh(strip, first);
    }
  }
  const auto second = mesh(strip);
  EXPECT_NEAR(maximum_height(strip), 5.0 / 32.0, 0.00001);

  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.update_drag(0.0).ok);
  ASSERT_TRUE(tool.update_drag(-3.0).ok);
  ASSERT_TRUE(tool.cancel_drag().ok);
  EXPECT_TRUE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), 1.75);
  EXPECT_EQ(session_.history().undo_count(), steps + 2);
  expect_mesh(strip, second);

  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, first);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, original);
  ASSERT_TRUE(session_.history().redo().ok);
  expect_mesh(strip, first);
  ASSERT_TRUE(session_.history().redo().ok);
  expect_mesh(strip, second);
  EXPECT_EQ(session_.history().undo_count(), steps + 2);
  EXPECT_EQ(session_.modeling().selection_summary().selected_face_count, 2U);
}

TEST_F(ExtrudeToolServiceTests, AdjustmentCancelRestoresGeometryAmountAndEligibility) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_amount(2.15).ok);
  const auto accepted = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.update_drag(4.0).ok);
  ASSERT_TRUE(tool.cancel_drag().ok);
  EXPECT_TRUE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), 2.15);
  expect_mesh(strip, accepted);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(tool.set_amount(3.0).ok);
  EXPECT_EQ(session_.history().undo_count(), steps);
  EXPECT_NEAR(maximum_height(strip), 3.0 / 32.0, 0.00001);
}

TEST_F(ExtrudeToolServiceTests, UnchangedAdjustmentReleaseKeepsHistoryAndSettings) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  const auto accepted = mesh(strip);
  const auto steps = session_.history().undo_count();
  const auto history_revision = session_.history().revision();
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.update_drag(2.0).ok);
  ASSERT_TRUE(tool.update_drag(1.0).ok);
  ASSERT_TRUE(tool.end_drag().ok);
  EXPECT_TRUE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), 1.0);
  EXPECT_EQ(session_.history().undo_count(), steps);
  EXPECT_EQ(session_.history().revision(), history_revision);
  expect_mesh(strip, accepted);
}

TEST_F(ExtrudeToolServiceTests,
       ReturningToTheOriginalDirectionKeepsHistoryAndGeometry) {
  const auto strip = create_strip(true);
  select_strip(strip);
  auto &tool = session_.extrude();
  const auto original = mesh(strip);
  ASSERT_TRUE(editor_.commands()
                  .execute(quader_command::kToolsActivateExtrude)
                  .ok());
  const auto accepted = mesh(strip);
  const double accepted_amount = tool.amount();
  const auto original_direction = tool.handle_state().adjustment_direction;
  const auto steps = session_.history().undo_count();
  const auto history_revision = session_.history().revision();

  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.update_drag(2.0, QVec3{1.0F, 0.0F, 0.0F}).ok);
  ASSERT_TRUE(tool.update_drag(accepted_amount, original_direction).ok);
  ASSERT_TRUE(tool.end_drag().ok);
  EXPECT_TRUE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), accepted_amount);
  EXPECT_EQ(session_.history().undo_count(), steps);
  EXPECT_EQ(session_.history().revision(), history_revision);
  expect_mesh(strip, accepted);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, original);
  ASSERT_TRUE(session_.history().redo().ok);
  expect_mesh(strip, accepted);
}

TEST_F(ExtrudeToolServiceTests, AdjustmentDragToZeroRestoresSourceAndCanBeAdjustedAgain) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  const auto original = mesh(strip);
  ASSERT_TRUE(tool.activate().ok);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.update_drag(0.0).ok);
  ASSERT_TRUE(tool.end_drag().ok);
  EXPECT_TRUE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), 0.0);
  expect_mesh(strip, original);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.update_drag(2.0).ok);
  ASSERT_TRUE(tool.end_drag().ok);
  EXPECT_EQ(session_.history().undo_count(), steps);
  EXPECT_NEAR(maximum_height(strip), 2.0 / 32.0, 0.00001);
}

TEST_F(ExtrudeToolServiceTests, NegativeAdjustmentKeepsOriginalDirectionAndCurrentPivot) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_amount(-2.0).ok);
  const auto handle = tool.handle_state();
  EXPECT_TRUE(handle.adjustment_available);
  EXPECT_NEAR(handle.adjustment_origin.x, 1.0, 0.00001);
  EXPECT_NEAR(handle.adjustment_origin.y, 0.5, 0.00001);
  EXPECT_NEAR(handle.adjustment_origin.z, -2.0 / 32.0, 0.00001);
  EXPECT_NEAR(handle.adjustment_direction.z, 1.0, 0.00001);
  EXPECT_DOUBLE_EQ(handle.amount_world, -2.0 / 32.0);
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.update_drag(-3.0).ok);
  EXPECT_NEAR(tool.handle_state().adjustment_origin.z, -3.0 / 32.0, 0.00001);
  EXPECT_NEAR(tool.handle_state().adjustment_direction.z, 1.0, 0.00001);
  ASSERT_TRUE(tool.cancel_drag().ok);
  EXPECT_NEAR(tool.handle_state().adjustment_origin.z, -2.0 / 32.0, 0.00001);
}

TEST_F(ExtrudeToolServiceTests, FailedAdjustmentPreviewRestoresTheEditableResult) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  const auto accepted = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  EXPECT_TRUE(tool.can_update_drag());
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  EXPECT_FALSE(tool.update_drag(2.0).ok);
  EXPECT_FALSE(tool.can_update_drag());
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  ASSERT_TRUE(tool.cancel_drag().ok);
  EXPECT_TRUE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), 1.0);
  expect_mesh(strip, accepted);
  EXPECT_EQ(session_.history().undo_count(), steps);
}

TEST_F(ExtrudeToolServiceTests, RegionAdjustmentAxisChangeUsesOriginalSourcePivot) {
  const auto strip = create_strip(true);
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.set_amount(0.0).ok);
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.begin_drag(QVec3{0.0F, 0.0F, 1.0F}).ok);
  ASSERT_TRUE(tool.update_drag(1.0).ok);
  ASSERT_TRUE(tool.end_drag().ok);
  EXPECT_NEAR(tool.handle_state().adjustment_origin.z,
              -1.0 + 1.0 / 32.0, 0.00001);
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.update_drag(2.0, QVec3{1.0F, 0.0F, 0.0F}).ok);
  const auto preview = tool.handle_state();
  EXPECT_NEAR(preview.adjustment_origin.x, 1.0 + 2.0 / 32.0, 0.00001);
  EXPECT_NEAR(preview.adjustment_origin.y, 0.5, 0.00001);
  EXPECT_NEAR(preview.adjustment_origin.z, -1.0, 0.00001);
  ASSERT_TRUE(tool.end_drag().ok);
  const auto committed = tool.handle_state();
  EXPECT_NEAR(committed.adjustment_origin.x, preview.adjustment_origin.x,
              0.00001);
  EXPECT_NEAR(committed.adjustment_origin.z, preview.adjustment_origin.z,
              0.00001);
  EXPECT_FLOAT_EQ(committed.world_direction.x, 1.0F);
  EXPECT_FLOAT_EQ(committed.world_direction.y, 0.0F);
  EXPECT_FLOAT_EQ(committed.world_direction.z, 0.0F);
}

TEST_F(ExtrudeToolServiceTests, AdjustableRegionHandleFollowsCommittedAxisDirection) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(editor_.commands().execute(quader_command::kToolsActivateExtrude).ok());
  EXPECT_FLOAT_EQ(tool.handle_state().world_direction.z, 1.0F);
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.update_drag(2.0, QVec3{1.0F, 0.0F, 0.0F}).ok);
  ASSERT_TRUE(tool.end_drag().ok);
  const auto committed = tool.handle_state();
  ASSERT_TRUE(committed.adjustment_available);
  EXPECT_FLOAT_EQ(committed.world_direction.x, 1.0F);
  EXPECT_FLOAT_EQ(committed.world_direction.y, 0.0F);
  EXPECT_FLOAT_EQ(committed.world_direction.z, 0.0F);
  EXPECT_FLOAT_EQ(length_squared(committed.world_direction -
                                 committed.adjustment_direction), 0.0F);
  EXPECT_NEAR(committed.world_position.x, 1.0 + 2.0 / 32.0, 0.00001);
  EXPECT_NEAR(committed.world_position.z, 0.0, 0.00001);

  tool.retire_adjustment();
  const auto next_operation = tool.handle_state();
  EXPECT_FALSE(next_operation.adjustment_available);
  EXPECT_FLOAT_EQ(next_operation.world_direction.x, 0.0F);
  EXPECT_FLOAT_EQ(next_operation.world_direction.y, 0.0F);
  EXPECT_FLOAT_EQ(next_operation.world_direction.z, 1.0F);
}

TEST_F(ExtrudeToolServiceTests, OpposingFaceNormalsUseModeAppropriateDiskDirection) {
  quader_poly::Document document;
  const std::array vertices{
      quader_poly::add_vertex(document, {0.0F, 0.0F, 0.0F}),
      quader_poly::add_vertex(document, {0.0F, 1.0F, 0.0F}),
      quader_poly::add_vertex(document, {0.0F, 1.0F, 1.0F}),
      quader_poly::add_vertex(document, {0.0F, 0.0F, 1.0F}),
      quader_poly::add_vertex(document, {2.0F, 0.0F, 0.0F}),
      quader_poly::add_vertex(document, {2.0F, 0.0F, 1.0F}),
      quader_poly::add_vertex(document, {2.0F, 1.0F, 1.0F}),
      quader_poly::add_vertex(document, {2.0F, 1.0F, 0.0F})};
  (void)quader_poly::add_face(document,
      std::array{vertices[0], vertices[1], vertices[2], vertices[3]});
  (void)quader_poly::add_face(document,
      std::array{vertices[4], vertices[5], vertices[6], vertices[7]});
  const auto created = editor_.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
      {.name = "Opposing extrusion disks"});
  ASSERT_TRUE(created.ok());
  select_strip(created.value());
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.set_options({modeling::FaceExtrudeMode::Individual, 0.0}).ok);
  ASSERT_TRUE(tool.activate().ok);
  const auto idle = tool.handle_state();
  EXPECT_FLOAT_EQ(idle.world_direction.x, 0.0F);
  EXPECT_FLOAT_EQ(idle.world_direction.y, 1.0F);
  EXPECT_FLOAT_EQ(idle.world_direction.z, 0.0F);
  const auto source = session_.modeling().capture_face_extrusion_source();
  ASSERT_TRUE(source.ok);
  EXPECT_NEAR(std::abs(source.direction_world.x), 1.0, 0.00001);
  ASSERT_TRUE(tool.begin_drag().ok);
  ASSERT_TRUE(tool.update_drag(1.0).ok);
  const auto preview = tool.handle_state();
  EXPECT_FLOAT_EQ(preview.world_direction.y, 1.0F);
  EXPECT_FLOAT_EQ(preview.world_direction.x, 0.0F);
  EXPECT_FLOAT_EQ(preview.world_position.x, idle.world_position.x);
  EXPECT_FLOAT_EQ(preview.world_position.y, idle.world_position.y);
  EXPECT_FLOAT_EQ(preview.world_position.z, idle.world_position.z);
  ASSERT_TRUE(tool.cancel_drag().ok);
  ASSERT_TRUE(tool.set_options({modeling::FaceExtrudeMode::Region, 1.0}).ok);
  ASSERT_TRUE(editor_.commands().execute(quader_command::kToolsActivateExtrude).ok());
  const auto region = tool.handle_state();
  ASSERT_TRUE(region.adjustment_available);
  EXPECT_FLOAT_EQ(length_squared(region.world_direction -
                                 region.adjustment_direction), 0.0F);
  EXPECT_NEAR(std::abs(region.world_direction.x), 1.0, 0.00001);
  EXPECT_FLOAT_EQ(region.world_direction.y, 0.0F);
  for (const auto mode : {modeling::FaceExtrudeMode::AlongNormals,
                          modeling::FaceExtrudeMode::Individual}) {
    ASSERT_TRUE(tool.set_mode(mode).ok);
    const auto per_normal = tool.handle_state();
    EXPECT_FLOAT_EQ(per_normal.world_direction.x, 0.0F);
    EXPECT_FLOAT_EQ(per_normal.world_direction.y, 1.0F);
    EXPECT_FLOAT_EQ(per_normal.world_direction.z, 0.0F);
  }
}

/// Minimal command proving the history fence without coupling it to geometry.
class ExtrusionFenceTestCommand final : public IUndoableCommand {
public:
  const std::string &label() const override { return label_; }
  CommandResult undo() override { return command_changed_result(); }
  CommandResult redo() override { return command_changed_result(); }
private:
  std::string label_ = "Fence test";
};

TEST(ExtrudeHistoryEligibilityTests, UndoRedoInvalidatesAnOtherwiseMatchingHead) {
  EditorHistoryService history;
  ASSERT_TRUE(history.execute(std::make_unique<ExtrusionFenceTestCommand>(),
                              {.merge_key = "extrusion"}).ok);
  const auto revision = history.revision();
  EXPECT_TRUE(history.can_merge_with_latest("extrusion", revision));
  EXPECT_FALSE(history.can_merge_with_latest("different", revision));
  ASSERT_TRUE(history.undo().ok);
  EXPECT_FALSE(history.can_merge_with_latest("extrusion", revision));
  ASSERT_TRUE(history.redo().ok);
  EXPECT_FALSE(history.can_merge_with_latest("extrusion", revision));
  EXPECT_TRUE(history.can_merge_with_latest("extrusion", history.revision()));
  auto transaction = history.begin_transaction("Provisional edit");
  EXPECT_FALSE(history.can_merge_with_latest("extrusion", history.revision()));
  ASSERT_TRUE(transaction.cancel().ok);
  history.clear();
  EXPECT_FALSE(history.can_merge_with_latest("extrusion", history.revision()));
}

} // namespace
} // namespace quader
