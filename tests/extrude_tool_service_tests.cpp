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
#include "viewport/viewport_workspace_service.hpp"

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

TEST_F(ExtrudeToolServiceTests, ActivationStartsAtZeroWithoutChangingGeometryOrHistory) {
  const auto strip = create_strip();
  select_strip(strip);
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  auto &tool = session_.extrude();
  EXPECT_DOUBLE_EQ(ExtrudeSessionDefaults{}.amount, 0.0);
  ASSERT_TRUE(tool.activate().ok);
  EXPECT_TRUE(tool.active());
  EXPECT_TRUE(tool.preview_active());
  EXPECT_TRUE(tool.adjustable());
  EXPECT_FALSE(tool.dragging());
  EXPECT_DOUBLE_EQ(tool.amount(), 0.0);
  EXPECT_EQ(session_.modeling().selection_summary().selected_face_count, 2U);
  EXPECT_EQ(session_.history().undo_count(), steps);
  expect_mesh(strip, original);
}

TEST_F(ExtrudeToolServiceTests, PendingSettingsApplyOnceAndRememberTheAmount) {
  const auto strip = create_strip();
  select_strip(strip);
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_mode(modeling::FaceExtrudeMode::Individual).ok);
  ASSERT_TRUE(tool.set_amount(2.15).ok);
  EXPECT_EQ(session_.history().undo_count(), steps);
  EXPECT_NEAR(maximum_height(strip), 2.15 / 32.0, 0.00001);
  const auto pending = mesh(strip);
  ASSERT_TRUE(tool.commit_preview().ok);
  EXPECT_FALSE(tool.preview_active());
  EXPECT_FALSE(tool.adjustable());
  EXPECT_FALSE(tool.handle_state().adjustment_available);
  EXPECT_DOUBLE_EQ(tool.handle_state().amount_world, 0.0);
  EXPECT_DOUBLE_EQ(tool.amount(), 2.15);
  EXPECT_EQ(tool.mode(), modeling::FaceExtrudeMode::Individual);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  EXPECT_FALSE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.commit_preview().ok);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  expect_mesh(strip, pending);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, original);
  ASSERT_TRUE(session_.history().redo().ok);
  expect_mesh(strip, pending);
}

TEST_F(ExtrudeToolServiceTests, RepeatedActivationNeverAcceptsOrStacksThePendingStep) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(editor_.commands().execute(quader_command::kToolsActivateExtrude).ok());
  ASSERT_TRUE(tool.set_amount(2.15).ok);
  const auto pending = mesh(strip);
  ASSERT_TRUE(editor_.commands().execute(quader_command::kToolsActivateExtrude).ok());
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.activate_preview().ok);
  ASSERT_TRUE(tool.extrude().ok);
  EXPECT_TRUE(tool.preview_active());
  EXPECT_DOUBLE_EQ(tool.amount(), 2.15);
  EXPECT_EQ(session_.history().undo_count(), steps);
  expect_mesh(strip, pending);
  ASSERT_TRUE(tool.activate(modeling::FaceExtrudeMode::Individual).ok);
  EXPECT_EQ(tool.mode(), modeling::FaceExtrudeMode::Individual);
  EXPECT_DOUBLE_EQ(tool.amount(), 2.15);
  EXPECT_NEAR(maximum_height(strip), 2.15 / 32.0, 0.00001);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(editor_.tools().cancel_modal_tool().ok());
  expect_mesh(strip, original);
  EXPECT_DOUBLE_EQ(tool.amount(), 2.15);
  EXPECT_EQ(session_.history().undo_count(), steps);
}

TEST_F(ExtrudeToolServiceTests, PendingCancelRestoresSelectionAndPreservesRedo) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  const auto original = mesh(strip);
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_amount(1.0).ok);
  ASSERT_TRUE(tool.commit_preview().ok);
  const auto accepted = mesh(strip);
  ASSERT_TRUE(tool.cancel().ok);
  ASSERT_TRUE(session_.history().undo().ok);
  const auto undo_steps = session_.history().undo_count();
  const auto redo_steps = session_.history().redo_count();
  const auto history_revision = session_.history().revision();
  ASSERT_TRUE(editor_.commands().execute(quader_command::kToolsActivateExtrude).ok());
  ASSERT_TRUE(tool.set_options({modeling::FaceExtrudeMode::Individual, 2.15}).ok);
  ASSERT_TRUE(editor_.tools().cancel_active_tool_preview().ok());
  EXPECT_FALSE(tool.active());
  EXPECT_FALSE(tool.preview_active());
  EXPECT_DOUBLE_EQ(tool.amount(), 2.15);
  EXPECT_TRUE(session_.modeling().tool_preview_meshes().empty());
  expect_mesh(strip, original);
  EXPECT_EQ(session_.modeling().selection_summary().selected_face_count, 2U);
  EXPECT_EQ(session_.history().undo_count(), undo_steps);
  EXPECT_EQ(session_.history().redo_count(), redo_steps);
  EXPECT_EQ(session_.history().revision(), history_revision);
  ASSERT_TRUE(session_.history().redo().ok);
  expect_mesh(strip, accepted);
}

TEST_F(ExtrudeToolServiceTests, SequentialAppliedAmountsStartFromCurrentCapsAndUndoSeparately) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  ASSERT_TRUE(tool.commit_preview().ok);
  const auto first = mesh(strip);
  EXPECT_DOUBLE_EQ(tool.amount(), 2.0);
  ASSERT_TRUE(tool.set_amount(3.0).ok);
  EXPECT_TRUE(tool.preview_active());
  EXPECT_NEAR(maximum_height(strip), 5.0 / 32.0, 0.00001);
  EXPECT_GT(mesh(strip).indices.size(), first.indices.size());
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  ASSERT_TRUE(tool.commit_preview().ok);
  const auto second = mesh(strip);
  EXPECT_DOUBLE_EQ(tool.amount(), 3.0);
  EXPECT_EQ(session_.history().undo_count(), steps + 2);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, first);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, original);
  ASSERT_TRUE(session_.history().redo().ok);
  expect_mesh(strip, first);
  ASSERT_TRUE(session_.history().redo().ok);
  expect_mesh(strip, second);
  EXPECT_FALSE(tool.adjustable());
  EXPECT_EQ(session_.modeling().selection_summary().selected_face_count, 2U);
}

TEST_F(ExtrudeToolServiceTests, CancelingTheNextOperationCannotRewriteTheAppliedStep) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  ASSERT_TRUE(tool.commit_preview().ok);
  const auto accepted = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.set_options({modeling::FaceExtrudeMode::Individual, 4.0}).ok);
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.update_drag(6.0).ok);
  ASSERT_TRUE(tool.cancel_drag().ok);
  EXPECT_TRUE(tool.active());
  EXPECT_FALSE(tool.preview_active());
  EXPECT_FALSE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), 4.0);
  expect_mesh(strip, accepted);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(tool.set_amount(3.0).ok);
  EXPECT_NEAR(maximum_height(strip), 5.0 / 32.0, 0.00001);
  EXPECT_EQ(session_.history().undo_count(), steps);
}

TEST_F(ExtrudeToolServiceTests, DragAcceptanceCreatesOneStepAndRemembersItsAmount) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.activate_preview().ok);
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  EXPECT_TRUE(tool.adjusting());
  ASSERT_TRUE(tool.update_drag(3.25).ok);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(tool.end_drag().ok);
  const auto accepted = mesh(strip);
  EXPECT_FALSE(tool.preview_active());
  EXPECT_FALSE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), 3.25);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  EXPECT_NEAR(maximum_height(strip), 3.25 / 32.0, 0.00001);
  ASSERT_TRUE(tool.end_drag().ok);
  ASSERT_TRUE(tool.cancel().ok);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  expect_mesh(strip, accepted);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, original);
}

TEST_F(ExtrudeToolServiceTests, ReleasedDragKeepsItsCandidateEditableUntilApply) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.update_drag(2.0).ok);
  ASSERT_TRUE(tool.release_drag().ok);
  EXPECT_FALSE(tool.dragging());
  EXPECT_TRUE(tool.preview_active());
  EXPECT_DOUBLE_EQ(tool.amount(), 2.0);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(tool.set_mode(modeling::FaceExtrudeMode::Individual).ok);
  ASSERT_TRUE(tool.set_amount(2.15).ok);
  const auto pending = mesh(strip);
  ASSERT_TRUE(editor_.tools().commit_active_tool_preview().ok());
  EXPECT_FALSE(tool.preview_active());
  EXPECT_DOUBLE_EQ(tool.amount(), 2.15);
  EXPECT_EQ(tool.mode(), modeling::FaceExtrudeMode::Individual);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  expect_mesh(strip, pending);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, original);
  ASSERT_TRUE(session_.history().redo().ok);
  expect_mesh(strip, pending);
}

TEST_F(ExtrudeToolServiceTests, EmptyActivationAndSelectionDoNotCreateGeometry) {
  const auto strip = create_strip();
  ASSERT_TRUE(editor_.selection().clear().ok());
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate(modeling::FaceExtrudeMode::AlongNormals).ok);
  EXPECT_EQ(session_.modeling().selection_mode(), ModelingSelectionMode::Face);
  EXPECT_FALSE(tool.has_selected_faces());
  EXPECT_FALSE(tool.preview_active());
  EXPECT_DOUBLE_EQ(tool.amount(), 0.0);
  select_strip(strip);
  ASSERT_TRUE(tool.refresh().ok);
  EXPECT_TRUE(tool.active());
  expect_mesh(strip, original);
  ASSERT_TRUE(tool.extrude().ok);
  EXPECT_TRUE(tool.preview_active());
  EXPECT_DOUBLE_EQ(tool.amount(), 0.0);
  expect_mesh(strip, original);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  EXPECT_NEAR(maximum_height(strip), 2.0 / 32.0, 0.00001);
  EXPECT_EQ(session_.history().undo_count(), steps);
}

TEST_F(ExtrudeToolServiceTests, ReturningPendingAmountToZeroRestoresOnlyItsOwnSource) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  ASSERT_TRUE(tool.commit_preview().ok);
  const auto first = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.set_amount(3.0).ok);
  ASSERT_TRUE(tool.set_amount(0.0).ok);
  EXPECT_TRUE(tool.preview_active());
  EXPECT_TRUE(tool.adjustable());
  expect_mesh(strip, first);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(tool.set_amount(1.25).ok);
  EXPECT_NEAR(maximum_height(strip), 3.25 / 32.0, 0.00001);
  ASSERT_TRUE(tool.commit_preview().ok);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, first);
}

TEST_F(ExtrudeToolServiceTests, ZeroApplyClosesThePreviewWithoutCreatingHistory) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  const auto revision = session_.history().revision();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  ASSERT_TRUE(tool.set_amount(0.0).ok);
  ASSERT_TRUE(tool.commit_preview().ok);
  EXPECT_FALSE(tool.preview_active());
  EXPECT_FALSE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), 0.0);
  expect_mesh(strip, original);
  EXPECT_EQ(session_.history().undo_count(), steps);
  EXPECT_EQ(session_.history().revision(), revision);
}

TEST_F(ExtrudeToolServiceTests, SelectionChangesCannotReuseAnAppliedSource) {
  const auto strip = create_strip();
  const auto other = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  ASSERT_TRUE(tool.commit_preview().ok);
  const auto first = mesh(strip);
  const auto second_source = mesh(other);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(editor_.selection().clear().ok());
  select_strip(other);
  ASSERT_TRUE(tool.refresh().ok);
  EXPECT_TRUE(tool.preview_active());
  EXPECT_TRUE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), 2.0);
  expect_mesh(strip, first);
  EXPECT_NEAR(maximum_height(other), 2.0 / 32.0, 0.00001);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(tool.set_amount(3.0).ok);
  expect_mesh(strip, first);
  EXPECT_NEAR(maximum_height(other), 3.0 / 32.0, 0.00001);
  ASSERT_TRUE(tool.commit_preview().ok);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, first);
  expect_mesh(other, second_source);
}

TEST_F(ExtrudeToolServiceTests, SharedOptionsSeedTheNextActivationUntilANewAppLifetime) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_options({modeling::FaceExtrudeMode::Individual, 2.15}).ok);
  ASSERT_TRUE(tool.commit_preview().ok);
  const auto accepted = mesh(strip);
  ASSERT_TRUE(tool.cancel().ok);
  expect_mesh(strip, accepted);
  SceneSession second(nullptr, defaults_);
  EXPECT_EQ(second.extrude().mode(), modeling::FaceExtrudeMode::Individual);
  EXPECT_DOUBLE_EQ(second.extrude().amount(), 2.15);
  ASSERT_TRUE(tool.activate().ok);
  EXPECT_EQ(tool.mode(), modeling::FaceExtrudeMode::Individual);
  EXPECT_DOUBLE_EQ(tool.amount(), 2.15);
  EXPECT_NEAR(maximum_height(strip), 4.3 / 32.0, 0.00001);
  ASSERT_TRUE(tool.cancel().ok);
  expect_mesh(strip, accepted);
  SceneSession fresh;
  EXPECT_EQ(fresh.extrude().mode(), modeling::FaceExtrudeMode::Region);
  EXPECT_DOUBLE_EQ(fresh.extrude().amount(), 0.0);
}

TEST_F(ExtrudeToolServiceTests, SwitchingToolsAcceptsPendingExtrusionAsOneUndoStep) {
  const auto strip = create_strip();
  select_strip(strip);
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(editor_.tools().activate(ViewportToolId::Extrude).ok());
  ASSERT_TRUE(editor_.tools().set_setting("extrude", "extrude.amount", 32.0).ok());
  const auto pending = mesh(strip);

  ASSERT_TRUE(editor_.tools().can_activate(ViewportToolId::Move));
  const auto switched = editor_.tools().activate(ViewportToolId::Move);
  ASSERT_TRUE(switched.ok()) << switched.error_message();
  EXPECT_EQ(editor_.tools().active_tool(), "move");
  EXPECT_FALSE(session_.extrude().active());
  EXPECT_FALSE(session_.extrude().preview_active());
  EXPECT_EQ(session_.history().undo_count(), steps + 1U);
  expect_mesh(strip, pending);

  ASSERT_TRUE(editor_.tools().cancel_modal_tool().ok());
  expect_mesh(strip, pending);
  EXPECT_EQ(session_.history().undo_count(), steps + 1U);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_mesh(strip, original);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_mesh(strip, pending);
}

TEST_F(ExtrudeToolServiceTests, PaneFocusRetainsPendingWorkUntilAnotherPaneAcquiresItsTool) {
  const auto strip = create_strip();
  select_strip(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(session_.viewport_workspace().set_layout(ViewportLayoutId::FourQuad));
  ASSERT_TRUE(editor_.tools().activate(ViewportToolId::Extrude).ok());
  ASSERT_TRUE(editor_.tools().set_setting("extrude", "extrude.amount", 16.0).ok());
  const auto pending = mesh(strip);
  ASSERT_TRUE(session_.viewport_workspace().record_selected_tool(
      ViewportPaneId::Top, ViewportToolId::Rotate, true));

  ASSERT_TRUE(session_.viewport_workspace().focus_pane(ViewportPaneId::Top));
  EXPECT_TRUE(session_.extrude().preview_active());
  EXPECT_EQ(session_.history().undo_count(), steps);
  expect_mesh(strip, pending);

  const auto acquired = editor_.tools().acquire_pane_tool(ViewportPaneId::Top);
  ASSERT_TRUE(acquired.ok()) << acquired.error_message();
  EXPECT_EQ(editor_.tools().active_tool(), "rotate");
  EXPECT_TRUE(editor_.tools().pane_owns_executing_tool(ViewportPaneId::Top));
  EXPECT_EQ(session_.history().undo_count(), steps + 1U);
  EXPECT_FALSE(session_.extrude().preview_active());
  expect_mesh(strip, pending);
}

TEST_F(ExtrudeToolServiceTests, UnavailableActivationDoesNotAcceptPendingExtrusion) {
  const auto strip = create_strip();
  select_strip(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(editor_.tools().activate(ViewportToolId::Extrude).ok());
  ASSERT_TRUE(editor_.tools().set_setting("extrude", "extrude.amount", 16.0).ok());
  const auto pending = mesh(strip);

  EXPECT_FALSE(editor_.tools().activate("missing_tool").ok());
  EXPECT_FALSE(editor_.tools().begin_primitive_creation(
      static_cast<QuaderPrimitiveCreationShape>(-1)).ok());
  EXPECT_TRUE(session_.extrude().preview_active());
  EXPECT_EQ(editor_.tools().active_tool(), "extrude");
  EXPECT_EQ(session_.history().undo_count(), steps);
  expect_mesh(strip, pending);
}

TEST_F(ExtrudeToolServiceTests, SharedDefaultsRememberAcceptedAmountsWithoutChangingPendingGeometry) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  SceneSession second(nullptr, defaults_);
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_options({modeling::FaceExtrudeMode::Individual, 2.15}).ok);
  const auto pending = mesh(strip);
  const auto second_revision = second.extrude().tool_settings_revision();
  EXPECT_EQ(second.extrude().mode(), modeling::FaceExtrudeMode::Individual);
  EXPECT_DOUBLE_EQ(second.extrude().amount(), 2.15);
  ASSERT_TRUE(second.extrude().set_amount(4.0).ok);
  EXPECT_GT(second.extrude().tool_settings_revision(), second_revision);
  EXPECT_DOUBLE_EQ(tool.amount(), 2.15);
  expect_mesh(strip, pending);
  ASSERT_TRUE(tool.commit_preview().ok);
  EXPECT_DOUBLE_EQ(tool.amount(), 2.15);
  EXPECT_DOUBLE_EQ(second.extrude().amount(), 2.15);
  ASSERT_TRUE(second.extrude().activate().ok);
  EXPECT_DOUBLE_EQ(second.extrude().amount(), 2.15);
  EXPECT_FALSE(second.extrude().preview_active());
  EXPECT_EQ(second.extrude().mode(), modeling::FaceExtrudeMode::Individual);
}

TEST_F(ExtrudeToolServiceTests, FreshActivationUsesAnInactiveAmountWithoutCommittingIt) {
  const auto strip = create_strip();
  select_strip(strip);
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.set_options({modeling::FaceExtrudeMode::Individual, 2.15}).ok);
  ASSERT_TRUE(tool.activate().ok);
  EXPECT_TRUE(tool.preview_active());
  EXPECT_EQ(tool.mode(), modeling::FaceExtrudeMode::Individual);
  EXPECT_DOUBLE_EQ(tool.amount(), 2.15);
  EXPECT_NEAR(maximum_height(strip), 2.15 / 32.0, 0.00001);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(tool.cancel().ok);
  expect_mesh(strip, original);
  EXPECT_DOUBLE_EQ(tool.amount(), 2.15);
}

TEST_F(ExtrudeToolServiceTests, DragCancelRestoresGeometryHistoryAndZeroAmount) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.update_drag_world_amount(0.5).ok);
  EXPECT_DOUBLE_EQ(tool.amount(), 16.0);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(tool.cancel_drag().ok);
  EXPECT_TRUE(tool.active());
  EXPECT_FALSE(tool.dragging());
  EXPECT_FALSE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), 0.0);
  expect_mesh(strip, original);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  EXPECT_NEAR(maximum_height(strip), 2.0 / 32.0, 0.00001);
  EXPECT_EQ(session_.history().undo_count(), steps);
}

TEST_F(ExtrudeToolServiceTests, NewDragAfterApplyStartsAtTheCurrentCapAndZeroDisplacement) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  ASSERT_TRUE(tool.commit_preview().ok);
  const auto first = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.begin_drag().ok);
  EXPECT_FALSE(tool.adjusting());
  EXPECT_DOUBLE_EQ(tool.amount(), 0.0);
  expect_mesh(strip, first);
  ASSERT_TRUE(tool.update_drag(3.0).ok);
  ASSERT_TRUE(tool.end_drag().ok);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  EXPECT_NEAR(maximum_height(strip), 5.0 / 32.0, 0.00001);
  EXPECT_DOUBLE_EQ(tool.amount(), 3.0);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, first);
}

TEST_F(ExtrudeToolServiceTests, FailedDragBeginPreservesTheAppliedStepAndAllowsRetry) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_amount(1.0).ok);
  ASSERT_TRUE(tool.commit_preview().ok);
  const auto accepted = mesh(strip);
  const auto steps = session_.history().undo_count();
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  const auto failed = tool.begin_drag();
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(tool.dragging());
  EXPECT_FALSE(tool.can_update_drag());
  EXPECT_FALSE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), 1.0);
  EXPECT_EQ(session_.history().undo_count(), steps);
  expect_mesh(strip, accepted);
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  EXPECT_NEAR(maximum_height(strip), 3.0 / 32.0, 0.00001);
  EXPECT_EQ(session_.history().undo_count(), steps);
}

TEST_F(ExtrudeToolServiceTests, InvalidSettingsPreserveTheLastValidPendingCandidate) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_amount(1.25).ok);
  const auto pending = mesh(strip);
  const auto steps = session_.history().undo_count();
  EXPECT_FALSE(tool.set_amount(std::numeric_limits<double>::quiet_NaN()).ok);
  EXPECT_FALSE(tool.set_amount(std::numeric_limits<double>::infinity()).ok);
  EXPECT_FALSE(tool.set_amount(std::numeric_limits<double>::max()).ok);
  EXPECT_FALSE(tool.set_mode(static_cast<modeling::FaceExtrudeMode>(255)).ok);
  EXPECT_DOUBLE_EQ(tool.amount(), 1.25);
  EXPECT_TRUE(tool.adjustable());
  expect_mesh(strip, pending);
  EXPECT_EQ(session_.history().undo_count(), steps);
}

TEST_F(ExtrudeToolServiceTests, UnchangedPendingAndAppliedFramesDoNotRegenerateGeometry) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  for (const bool applied : {false, true}) {
    SCOPED_TRACE(applied);
    if (applied) {
      ASSERT_TRUE(tool.commit_preview().ok);
    }
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
}

TEST_F(ExtrudeToolServiceTests, FailedPendingUpdateRestoresOnlyTheCurrentOperationSource) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_amount(1.0).ok);
  ASSERT_TRUE(tool.commit_preview().ok);
  const auto accepted = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.set_amount(4.0).ok);
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  const auto failed = tool.set_amount(6.0);
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  ASSERT_FALSE(failed.ok);
  ASSERT_TRUE(tool.refresh().ok);
  EXPECT_FALSE(tool.preview_active());
  EXPECT_FALSE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), 4.0);
  expect_mesh(strip, accepted);
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  EXPECT_NEAR(maximum_height(strip), 3.0 / 32.0, 0.00001);
  EXPECT_EQ(session_.history().undo_count(), steps);
}

TEST_F(ExtrudeToolServiceTests, FailedActivationPreservesGeometryAndPriorTool) {
  const auto strip = create_strip();
  select_strip(strip);
  ASSERT_TRUE(session_.extrude().set_options(
      {modeling::FaceExtrudeMode::Individual, 2.15}).ok);
  ASSERT_TRUE(session_.modeling().activate_viewport_tool(
      ModelingViewportTool::Move).ok);
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  EXPECT_FALSE(session_.extrude().activate().ok);
  EXPECT_FALSE(session_.extrude().active());
  EXPECT_EQ(session_.extrude().mode(), modeling::FaceExtrudeMode::Individual);
  EXPECT_DOUBLE_EQ(session_.extrude().amount(), 2.15);
  EXPECT_EQ(session_.modeling().tool_status().active_tool,
            ModelingViewportTool::Move);
  expect_mesh(strip, original);
  EXPECT_EQ(session_.history().undo_count(), steps);
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
}

TEST_F(ExtrudeToolServiceTests, PendingDragReturningToItsStartStillNeedsOneApply) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_amount(1.0).ok);
  const auto pending = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.update_drag(2.0).ok);
  ASSERT_TRUE(tool.update_drag(1.0).ok);
  ASSERT_TRUE(tool.release_drag().ok);
  EXPECT_TRUE(tool.preview_active());
  EXPECT_DOUBLE_EQ(tool.amount(), 1.0);
  EXPECT_EQ(session_.history().undo_count(), steps);
  expect_mesh(strip, pending);
  ASSERT_TRUE(tool.commit_preview().ok);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  EXPECT_DOUBLE_EQ(tool.amount(), 1.0);
  expect_mesh(strip, pending);
}

TEST_F(ExtrudeToolServiceTests, ReturningToTheSourceDirectionRestoresThePendingCandidate) {
  const auto strip = create_strip(true);
  select_strip(strip);
  auto &tool = session_.extrude();
  const auto original = mesh(strip);
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_amount(1.0).ok);
  const auto pending = mesh(strip);
  const auto direction = tool.handle_state().adjustment_direction;
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.update_drag(2.0, QVec3{1.0F, 0.0F, 0.0F}).ok);
  ASSERT_TRUE(tool.update_drag(1.0, direction).ok);
  ASSERT_TRUE(tool.release_drag().ok);
  EXPECT_EQ(session_.history().undo_count(), steps);
  expect_mesh(strip, pending);
  ASSERT_TRUE(tool.commit_preview().ok);
  EXPECT_EQ(session_.history().undo_count(), steps + 1);
  ASSERT_TRUE(session_.history().undo().ok);
  expect_mesh(strip, original);
  ASSERT_TRUE(session_.history().redo().ok);
  expect_mesh(strip, pending);
}

TEST_F(ExtrudeToolServiceTests, NegativePendingAmountKeepsTheSourceDirectionAndCancelPivot) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  const auto original = mesh(strip);
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
  EXPECT_DOUBLE_EQ(tool.amount(), -2.0);
  EXPECT_NEAR(tool.handle_state().world_position.z, 0.0, 0.00001);
  expect_mesh(strip, original);
}

TEST_F(ExtrudeToolServiceTests, FailedDragUpdateRetiresItsPreviewWithoutCommitting) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.update_drag(1.0).ok);
  EXPECT_TRUE(tool.can_update_drag());
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  EXPECT_FALSE(tool.update_drag(2.0).ok);
  EXPECT_FALSE(tool.can_update_drag());
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  ASSERT_TRUE(tool.cancel_drag().ok);
  EXPECT_FALSE(tool.adjustable());
  EXPECT_DOUBLE_EQ(tool.amount(), 0.0);
  expect_mesh(strip, original);
  EXPECT_EQ(session_.history().undo_count(), steps);
}

TEST_F(ExtrudeToolServiceTests, FailedDragReleaseCannotRememberACompensatedSample) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.set_amount(2.0).ok);
  ASSERT_TRUE(tool.commit_preview().ok);
  const auto original = mesh(strip);
  const auto steps = session_.history().undo_count();
  ASSERT_TRUE(tool.begin_drag().ok);
  ASSERT_TRUE(tool.update_drag(3.0).ok);
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  EXPECT_FALSE(tool.update_drag(4.0).ok);
  session_.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  EXPECT_FALSE(tool.release_drag().ok);
  EXPECT_TRUE(tool.preview_active());
  EXPECT_TRUE(tool.dragging());
  EXPECT_EQ(session_.history().undo_count(), steps);
  ASSERT_TRUE(tool.cancel_drag().ok);
  EXPECT_FALSE(tool.preview_active());
  EXPECT_DOUBLE_EQ(tool.amount(), 2.0);
  expect_mesh(strip, original);
  EXPECT_EQ(session_.history().undo_count(), steps);
}

TEST_F(ExtrudeToolServiceTests, RegionAxisChangesUseTheCurrentOperationSourcePivot) {
  const auto strip = create_strip(true);
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.begin_adjust_drag(QVec3{0.0F, 0.0F, 1.0F}).ok);
  ASSERT_TRUE(tool.update_drag(1.0).ok);
  ASSERT_TRUE(tool.release_drag().ok);
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
  EXPECT_FALSE(committed.adjustment_available);
  EXPECT_DOUBLE_EQ(committed.amount_world, 0.0);
  EXPECT_NEAR(committed.world_position.x, preview.world_position.x, 0.00001);
  EXPECT_NEAR(committed.world_position.z, preview.world_position.z, 0.00001);
}

TEST_F(ExtrudeToolServiceTests, AppliedAxisConstraintDoesNotCarryIntoTheNextOperation) {
  const auto strip = create_strip();
  select_strip(strip);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate().ok);
  ASSERT_TRUE(tool.begin_adjust_drag().ok);
  ASSERT_TRUE(tool.update_drag(2.0, QVec3{1.0F, 0.0F, 0.0F}).ok);
  EXPECT_FLOAT_EQ(tool.handle_state().world_direction.x, 1.0F);
  ASSERT_TRUE(tool.end_drag().ok);
  const auto idle = tool.handle_state();
  EXPECT_FALSE(idle.adjustment_available);
  EXPECT_NEAR(idle.world_position.x, 1.0 + 2.0 / 32.0, 0.00001);
  EXPECT_FLOAT_EQ(idle.world_direction.x, 0.0F);
  EXPECT_FLOAT_EQ(idle.world_direction.z, 1.0F);
  ASSERT_TRUE(tool.set_amount(3.0).ok);
  EXPECT_FLOAT_EQ(tool.handle_state().world_direction.x, 0.0F);
  EXPECT_FLOAT_EQ(tool.handle_state().world_direction.z, 1.0F);
  EXPECT_NEAR(maximum_height(strip), 3.0 / 32.0, 0.00001);
}

TEST_F(ExtrudeToolServiceTests, OpposingFaceNormalsUseModeAppropriatePendingDiskDirection) {
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
  const auto source = session_.modeling().capture_face_extrusion_source();
  ASSERT_TRUE(source.ok);
  EXPECT_NEAR(std::abs(source.direction_world.x), 1.0, 0.00001);
  auto &tool = session_.extrude();
  ASSERT_TRUE(tool.activate(modeling::FaceExtrudeMode::Individual).ok);
  const auto initial = tool.handle_state();
  EXPECT_FLOAT_EQ(initial.world_direction.x, 0.0F);
  EXPECT_FLOAT_EQ(initial.world_direction.y, 1.0F);
  EXPECT_FLOAT_EQ(initial.world_direction.z, 0.0F);
  ASSERT_TRUE(tool.set_amount(1.0).ok);
  const auto pending = tool.handle_state();
  EXPECT_FLOAT_EQ(pending.world_direction.y, 1.0F);
  EXPECT_FLOAT_EQ(pending.world_position.x, initial.world_position.x);
  EXPECT_FLOAT_EQ(pending.world_position.y, initial.world_position.y);
  EXPECT_FLOAT_EQ(pending.world_position.z, initial.world_position.z);
  ASSERT_TRUE(tool.set_mode(modeling::FaceExtrudeMode::Region).ok);
  const auto region = tool.handle_state();
  ASSERT_TRUE(region.adjustment_available);
  EXPECT_FLOAT_EQ(length_squared(region.world_direction -
                                 region.adjustment_direction), 0.0F);
  EXPECT_NEAR(std::abs(region.world_direction.x), 1.0, 0.00001);
  EXPECT_FLOAT_EQ(region.world_direction.y, 0.0F);
  for (const auto mode : {modeling::FaceExtrudeMode::AlongNormals,
                          modeling::FaceExtrudeMode::Individual}) {
    ASSERT_TRUE(tool.set_mode(mode).ok);
    EXPECT_FLOAT_EQ(tool.handle_state().world_direction.x, 0.0F);
    EXPECT_FLOAT_EQ(tool.handle_state().world_direction.y, 1.0F);
    EXPECT_FLOAT_EQ(tool.handle_state().world_direction.z, 0.0F);
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
