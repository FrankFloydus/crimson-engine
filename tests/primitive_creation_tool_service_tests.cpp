////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/session/scene_session.hpp"
#include "scene/scene_document.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace quader {
namespace {

/// Provides isolated modeling and history owners for primitive lifecycle tests.
struct PrimitiveCreationHarness {
  SceneDocument scene;
  AssetRegistry assets;
  EditorHistoryService history;
  ModelingSessionService modeling{scene, assets};
  PrimitiveCreationToolService tool{history, modeling};
};

ModelingBoxPreview valid_primitive_preview(float center_x = 0.0f) {
  ModelingBoxPreview preview;
  preview.ok = true;
  preview.valid = true;
  preview.drag_state = ModelingBoxDragState::Height;
  preview.corners = {
      QVec3{center_x - 1.0f, 0.0f, -1.0f},
      QVec3{center_x - 1.0f, 0.0f, 1.0f},
      QVec3{center_x + 1.0f, 0.0f, 1.0f},
      QVec3{center_x + 1.0f, 0.0f, -1.0f},
      QVec3{center_x - 1.0f, 2.0f, -1.0f},
      QVec3{center_x - 1.0f, 2.0f, 1.0f},
      QVec3{center_x + 1.0f, 2.0f, 1.0f},
      QVec3{center_x + 1.0f, 2.0f, -1.0f},
  };
  preview.corner_count = preview.corners.size();
  return preview;
}

const ModelingObjectRecord *find_object(
    const std::vector<ModelingObjectRecord> &objects, std::string_view name) {
  const auto found =
      std::find_if(objects.begin(), objects.end(), [name](const auto &object) {
        return object.name == name;
      });
  return found != objects.end() ? &*found : nullptr;
}

std::size_t compiled_index_count(const ModelingSessionService &modeling,
                                 std::string_view name) {
  const std::vector<ModelingObjectRecord> objects = modeling.objects();
  const ModelingObjectRecord *const object = find_object(objects, name);
  if (object == nullptr) {
    ADD_FAILURE() << "Missing modeling object named " << name;
    return 0U;
  }
  const std::optional<MeshAsset> mesh =
      modeling.compile_object_mesh(object->modeling_object_id);
  if (!mesh.has_value()) {
    ADD_FAILURE() << "Could not compile modeling object named " << name;
    return 0U;
  }
  return mesh->geometry().indices.size();
}

TEST(PrimitiveCreationToolServiceTests,
     LiveSettingsUpdateNewestShapeAndApplyAsOneHistoryTransaction) {
  PrimitiveCreationHarness harness;

  const CommandResult activated =
      harness.tool.activate(PrimitiveCreationKind::Box);
  ASSERT_TRUE(activated.ok) << activated.message;
  EXPECT_TRUE(harness.tool.active());
  EXPECT_EQ(harness.tool.active_tool_id(), kCreatePrimitiveToolId);

  const CommandResult first = harness.tool.accept_preview(
      valid_primitive_preview(), "Low Resolution Box");
  ASSERT_TRUE(first.ok) << first.message;
  EXPECT_TRUE(harness.tool.active());
  EXPECT_EQ(harness.tool.shape_count(), 1U);
  EXPECT_FALSE(harness.history.can_undo());
  EXPECT_EQ(compiled_index_count(harness.modeling, "Low Resolution Box"),
            36U);

  const CommandResult updated =
      harness.tool.set_setting(kBoxSubdivisionsXSetting, 2.0);
  ASSERT_TRUE(updated.ok) << updated.message;
  ASSERT_TRUE(updated.changed);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Low Resolution Box"),
            60U);
  const CommandResult second = harness.tool.accept_preview(
      valid_primitive_preview(4.0f), "High Resolution Box");
  ASSERT_TRUE(second.ok) << second.message;
  EXPECT_TRUE(harness.tool.active());
  EXPECT_EQ(harness.tool.shape_count(), 2U);
  EXPECT_FALSE(harness.history.can_undo());

  // The first shape updates live, then freezes when creating the second.
  EXPECT_EQ(compiled_index_count(harness.modeling, "Low Resolution Box"),
            60U);
  EXPECT_EQ(compiled_index_count(harness.modeling, "High Resolution Box"),
            60U);

  const CommandResult newest_updated =
      harness.tool.set_setting(kBoxSubdivisionsXSetting, 3.0);
  ASSERT_TRUE(newest_updated.ok) << newest_updated.message;
  ASSERT_TRUE(newest_updated.changed);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Low Resolution Box"),
            60U);
  const std::size_t newest_index_count =
      compiled_index_count(harness.modeling, "High Resolution Box");
  EXPECT_NE(newest_index_count, 60U);

  const CommandResult applied = harness.tool.apply();
  ASSERT_TRUE(applied.ok) << applied.message;
  EXPECT_FALSE(harness.tool.active());
  EXPECT_EQ(harness.modeling.objects().size(), 2U);
  ASSERT_TRUE(harness.history.can_undo());
  ASSERT_NE(harness.history.undo_label(), nullptr);
  EXPECT_EQ(*harness.history.undo_label(), "Create Primitives");

  const CommandResult undone = harness.history.undo();
  ASSERT_TRUE(undone.ok) << undone.message;
  EXPECT_TRUE(harness.modeling.objects().empty());
  EXPECT_FALSE(harness.history.can_undo());
  EXPECT_TRUE(harness.history.can_redo());

  const CommandResult redone = harness.history.redo();
  ASSERT_TRUE(redone.ok) << redone.message;
  EXPECT_EQ(harness.modeling.objects().size(), 2U);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Low Resolution Box"),
            60U);
  EXPECT_EQ(compiled_index_count(harness.modeling, "High Resolution Box"),
            newest_index_count);
}

TEST(PrimitiveCreationToolServiceTests,
     CancelRollsBackEveryAcceptedShapeToThePreToolState) {
  PrimitiveCreationHarness harness;
  const ModelingCreateBoxResult baseline =
      harness.modeling.create_box_from_bounds({
          .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                     .max = {1.0f, 1.0f, 1.0f}},
          .name = "Baseline",
      });
  ASSERT_TRUE(baseline.ok) << baseline.message;
  const std::size_t baseline_mesh_count = harness.assets.mesh_ids().size();

  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Cylinder).ok);
  ASSERT_TRUE(harness.tool.set_setting(kCylinderSideCountSetting, 3.0).ok);
  ASSERT_TRUE(harness.tool.set_setting(kCylinderCapsSetting, 0.0).ok);
  const CommandResult first = harness.tool.accept_preview(
      valid_primitive_preview(4.0f), "Open Triangle Cylinder");
  ASSERT_TRUE(first.ok) << first.message;
  EXPECT_EQ(compiled_index_count(harness.modeling, "Open Triangle Cylinder"),
            18U);

  ASSERT_TRUE(harness.tool.set_setting(kCylinderSideCountSetting, 8.0).ok);
  ASSERT_TRUE(harness.tool.set_setting(kCylinderCapsSetting, 1.0).ok);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Open Triangle Cylinder"),
            84U);
  const CommandResult second = harness.tool.accept_preview(
      valid_primitive_preview(8.0f), "Capped Octagonal Cylinder");
  ASSERT_TRUE(second.ok) << second.message;
  EXPECT_TRUE(harness.tool.active());
  EXPECT_EQ(harness.tool.shape_count(), 2U);
  EXPECT_EQ(compiled_index_count(harness.modeling, "Open Triangle Cylinder"),
            84U);
  EXPECT_EQ(
      compiled_index_count(harness.modeling, "Capped Octagonal Cylinder"),
      84U);
  const std::vector<ModelingObjectRecord> objects_before_cancel =
      harness.modeling.objects();
  ASSERT_EQ(objects_before_cancel.size(), 3U);
  const ModelingObjectRecord *const first_transient =
      find_object(objects_before_cancel, "Open Triangle Cylinder");
  const ModelingObjectRecord *const second_transient =
      find_object(objects_before_cancel, "Capped Octagonal Cylinder");
  ASSERT_NE(first_transient, nullptr);
  ASSERT_NE(second_transient, nullptr);
  const int first_transient_id = first_transient->modeling_object_id;
  const int second_transient_id = second_transient->modeling_object_id;
  EXPECT_NE(first_transient_id, second_transient_id);

  const CommandResult canceled = harness.tool.cancel();
  ASSERT_TRUE(canceled.ok) << canceled.message;
  EXPECT_TRUE(canceled.changed);
  EXPECT_FALSE(harness.tool.active());
  EXPECT_EQ(harness.tool.shape_count(), 0U);
  ASSERT_EQ(harness.modeling.objects().size(), 1U);
  EXPECT_EQ(harness.assets.mesh_ids().size(), baseline_mesh_count);
  EXPECT_EQ(harness.modeling.objects().front().name, "Baseline");
  EXPECT_EQ(harness.modeling.objects().front().modeling_object_id,
            baseline.object.modeling_object_id);
  EXPECT_FALSE(harness.history.can_undo());

  const ModelingSelectionSummary selection =
      harness.modeling.selection_summary();
  EXPECT_EQ(selection.selected_object_count, 1U);
  EXPECT_EQ(selection.active_object_id, baseline.object.modeling_object_id);

  const ModelingCreateBoxResult replacement =
      harness.modeling.create_box_from_bounds({
          .bounds = {.min = {11.0f, -1.0f, -1.0f},
                     .max = {13.0f, 1.0f, 1.0f}},
          .name = "Replacement After Primitive Cancel",
      });
  ASSERT_TRUE(replacement.ok) << replacement.message;
  EXPECT_GT(replacement.object.modeling_object_id, first_transient_id);
  EXPECT_GT(replacement.object.modeling_object_id, second_transient_id);
}

TEST(PrimitiveCreationToolServiceTests,
     PerShapeFailureRollsBackAcceptedShapesAndExitsTheTool) {
  PrimitiveCreationHarness harness;
  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Sphere).ok);
  ASSERT_TRUE(
      harness.tool.set_setting(kSphereRadialSegmentsSetting, 8.0).ok);
  ASSERT_TRUE(harness.tool.set_setting(kSphereRingsSetting, 4.0).ok);
  ASSERT_TRUE(harness.tool.set_setting(kSphereHemisphereSetting, 1.0).ok);

  const CommandResult accepted = harness.tool.accept_preview(
      valid_primitive_preview(), "Accepted Hemisphere");
  ASSERT_TRUE(accepted.ok) << accepted.message;
  ASSERT_TRUE(harness.tool.active());
  ASSERT_EQ(harness.tool.shape_count(), 1U);
  ASSERT_EQ(harness.modeling.objects().size(), 1U);
  EXPECT_FALSE(harness.history.can_undo());

  ModelingBoxPreview invalid = valid_primitive_preview(4.0f);
  invalid.corner_count = invalid.corners.size() - 1U;
  const CommandResult failed =
      harness.tool.accept_preview(invalid, "Invalid Sphere");
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.message.empty());
  EXPECT_FALSE(harness.tool.active());
  EXPECT_EQ(harness.tool.shape_count(), 0U);
  EXPECT_TRUE(harness.modeling.objects().empty());
  EXPECT_FALSE(harness.history.can_undo());
  EXPECT_FALSE(harness.history.can_redo());
  EXPECT_EQ(harness.modeling.selection_summary().selected_object_count, 0U);
}

TEST(PrimitiveCreationToolServiceTests,
     LivePrimitiveMorphsUntilSelectionChangesThenFreezes) {
  PrimitiveCreationHarness harness;
  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Box).ok);

  const CommandResult accepted = harness.tool.accept_preview(
      valid_primitive_preview(), "Live Primitive");
  ASSERT_TRUE(accepted.ok) << accepted.message;
  const std::vector<ModelingObjectRecord> before_morph =
      harness.modeling.objects();
  ASSERT_EQ(before_morph.size(), 1U);
  const int live_id = before_morph.front().modeling_object_id;
  EXPECT_EQ(compiled_index_count(harness.modeling, "Live Primitive"), 36U);

  const CommandResult morphed =
      harness.tool.set_kind(PrimitiveCreationKind::Cylinder);
  ASSERT_TRUE(morphed.ok) << morphed.message;
  ASSERT_TRUE(morphed.changed);
  const std::size_t morphed_index_count =
      compiled_index_count(harness.modeling, "Live Primitive");
  EXPECT_NE(morphed_index_count, 36U);
  const std::vector<ModelingObjectRecord> after_morph =
      harness.modeling.objects();
  ASSERT_EQ(after_morph.size(), 1U);
  EXPECT_EQ(after_morph.front().modeling_object_id, live_id);

  ASSERT_TRUE(harness.modeling.clear_selection().ok);
  const CommandResult future_setting =
      harness.tool.set_setting(kCylinderSideCountSetting, 8.0);
  ASSERT_TRUE(future_setting.ok) << future_setting.message;
  EXPECT_EQ(compiled_index_count(harness.modeling, "Live Primitive"),
            morphed_index_count);

  ASSERT_TRUE(harness.tool.cancel().ok);
  ASSERT_TRUE(harness.tool.activate().ok);
  ASSERT_TRUE(harness.tool.active_kind().has_value());
  EXPECT_EQ(*harness.tool.active_kind(), PrimitiveCreationKind::Cylinder);
  EXPECT_EQ(harness.tool.cylinder_settings().side_count, 8);
  EXPECT_TRUE(harness.tool.cancel().ok);
}

TEST(PrimitiveCreationToolServiceTests,
     PublicDeselectAndConcurrentEditRemainValidAfterCancel) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const ModelingCreateBoxResult baseline =
      session.modeling().create_box_from_bounds({
          .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                     .max = {1.0f, 1.0f, 1.0f}},
          .name = "Concurrent Baseline",
      });
  ASSERT_TRUE(baseline.ok) << baseline.message;
  const std::size_t baseline_mesh_count = session.assets().mesh_ids().size();
  ASSERT_FALSE(editor.history().can_undo());

  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(4.0f),
                                  "Canceled Transient")
                  .ok);
  const std::vector<ModelingObjectRecord> active_objects =
      session.modeling().objects();
  const ModelingObjectRecord *const transient =
      find_object(active_objects, "Canceled Transient");
  ASSERT_NE(transient, nullptr);
  const QuaderApiVoidResult rejected_transient_edit =
      editor.scene().set_visible(transient->entity, false);
  EXPECT_FALSE(rejected_transient_edit.ok());
  EXPECT_TRUE(session.primitive_creation().active());
  EXPECT_FALSE(session.history().can_undo());
  const QuaderApiVoidResult rejected_modeling_preview =
      editor.modeling().begin_preview("half_cut");
  EXPECT_FALSE(rejected_modeling_preview.ok());
  EXPECT_TRUE(session.primitive_creation().active());
  EXPECT_FALSE(session.history().can_undo());
  const std::size_t transient_indices =
      compiled_index_count(session.modeling(), "Canceled Transient");

  ASSERT_TRUE(editor.selection().clear().ok());
  EXPECT_FALSE(session.history().can_undo());
  ASSERT_TRUE(editor.tools()
                  .set_setting("create_primitive",
                               "primitive.box.subdivisions_x", 3.0)
                  .ok());
  EXPECT_EQ(compiled_index_count(session.modeling(), "Canceled Transient"),
            transient_indices);

  ASSERT_TRUE(editor.scene().set_visible(baseline.object.entity, false).ok());
  ASSERT_TRUE(editor.history().can_undo());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  ASSERT_EQ(session.modeling().objects().size(), 1U);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count);
  EXPECT_EQ(session.modeling().objects().front().modeling_object_id,
            baseline.object.modeling_object_id);
  const std::optional<QuaderEntitySnapshot> canceled_baseline =
      editor.query().entity(baseline.object.entity);
  ASSERT_TRUE(canceled_baseline.has_value());
  EXPECT_FALSE(canceled_baseline->visible);
  EXPECT_EQ(session.modeling().selection_summary().active_object_id,
            baseline.object.modeling_object_id);

  ASSERT_TRUE(editor.history().undo().ok());
  const std::optional<QuaderEntitySnapshot> undone_visibility =
      editor.query().entity(baseline.object.entity);
  ASSERT_TRUE(undone_visibility.has_value());
  EXPECT_TRUE(undone_visibility->visible);
  ASSERT_TRUE(editor.history().redo().ok());
  const std::optional<QuaderEntitySnapshot> redone_visibility =
      editor.query().entity(baseline.object.entity);
  ASSERT_TRUE(redone_visibility.has_value());
  EXPECT_FALSE(redone_visibility->visible);
}

TEST(PrimitiveCreationToolServiceTests,
     ApplyHistoryOrdersConcurrentEditsBeforeThePrimitiveDelta) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const ModelingCreateBoxResult baseline =
      session.modeling().create_box_from_bounds({
          .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                     .max = {1.0f, 1.0f, 1.0f}},
          .name = "Apply Baseline",
      });
  ASSERT_TRUE(baseline.ok) << baseline.message;
  const std::size_t baseline_mesh_count = session.assets().mesh_ids().size();

  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(4.0f),
                                  "Applied Primitive")
                  .ok);
  ASSERT_TRUE(editor.selection().clear().ok());
  ASSERT_FALSE(session.history().can_undo());
  ASSERT_TRUE(editor.scene().set_visible(baseline.object.entity, false).ok());
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  const std::vector<ModelingObjectRecord> applied_objects =
      session.modeling().objects();
  ASSERT_EQ(applied_objects.size(), 2U);
  const ModelingObjectRecord *const applied_primitive =
      find_object(applied_objects, "Applied Primitive");
  ASSERT_NE(applied_primitive, nullptr);
  const AssetId applied_mesh = applied_primitive->mesh;
  ASSERT_NE(applied_mesh, kInvalidAssetId);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 1U);

  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_EQ(session.modeling().objects().size(), 1U);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count);
  EXPECT_EQ(session.modeling().objects().front().modeling_object_id,
            baseline.object.modeling_object_id);
  const std::optional<QuaderEntitySnapshot> after_primitive_undo =
      editor.query().entity(baseline.object.entity);
  ASSERT_TRUE(after_primitive_undo.has_value());
  EXPECT_FALSE(after_primitive_undo->visible);

  ASSERT_TRUE(editor.history().undo().ok());
  const std::optional<QuaderEntitySnapshot> after_visibility_undo =
      editor.query().entity(baseline.object.entity);
  ASSERT_TRUE(after_visibility_undo.has_value());
  EXPECT_TRUE(after_visibility_undo->visible);

  ASSERT_TRUE(editor.history().redo().ok());
  ASSERT_TRUE(editor.history().redo().ok());
  const std::vector<ModelingObjectRecord> redone_objects =
      session.modeling().objects();
  ASSERT_EQ(redone_objects.size(), 2U);
  const ModelingObjectRecord *const redone_primitive =
      find_object(redone_objects, "Applied Primitive");
  ASSERT_NE(redone_primitive, nullptr);
  EXPECT_EQ(redone_primitive->mesh, applied_mesh);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 1U);
  const std::optional<QuaderEntitySnapshot> after_redo =
      editor.query().entity(baseline.object.entity);
  ASSERT_TRUE(after_redo.has_value());
  EXPECT_FALSE(after_redo->visible);

  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count);
  ASSERT_TRUE(editor.history().redo().ok());
  const std::vector<ModelingObjectRecord> repeated_redo_objects =
      session.modeling().objects();
  const ModelingObjectRecord *const repeated_redo_primitive =
      find_object(repeated_redo_objects, "Applied Primitive");
  ASSERT_NE(repeated_redo_primitive, nullptr);
  EXPECT_EQ(repeated_redo_primitive->mesh, applied_mesh);
  EXPECT_EQ(session.assets().mesh_ids().size(), baseline_mesh_count + 1U);
}

TEST(PrimitiveCreationToolServiceTests,
     TransientPublicDeselectPreservesTheExistingRedoBranch) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  QuaderCreateEntityArgs args;
  args.name = "Redo Sentinel";
  const QuaderApiResult<QuaderEntityRef> sentinel =
      editor.scene().create_entity(args);
  ASSERT_TRUE(sentinel.ok()) << sentinel.error_message();
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(editor.history().can_redo());

  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(valid_primitive_preview(),
                                  "Redo Branch Transient")
                  .ok);
  ASSERT_TRUE(editor.selection().clear().ok());
  EXPECT_TRUE(session.history().can_redo());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());

  EXPECT_TRUE(session.modeling().objects().empty());
  ASSERT_TRUE(editor.history().can_redo());
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_TRUE(editor.query().entity(sentinel.value().id).has_value());
}

TEST(PrimitiveCreationToolServiceTests,
     SettingsAreFrozenWhileTheCurrentFootprintIsBeingDrawn) {
  PrimitiveCreationHarness harness;
  ASSERT_TRUE(harness.tool.activate(PrimitiveCreationKind::Box).ok);
  const ModelingBoxPreview footprint =
      harness.modeling.begin_box_footprint({
          .screen_position = {400.0f, 300.0f},
          .ray = {.origin = {0.0f, 4.0f, 0.0f},
                  .direction = {0.0f, -1.0f, 0.0f}},
          .grid_size = 1.0,
      });
  ASSERT_TRUE(footprint.ok) << footprint.message;
  ASSERT_EQ(footprint.drag_state, ModelingBoxDragState::Footprint);

  const CommandResult changed_during_draw =
      harness.tool.set_setting(kBoxSubdivisionsXSetting, 4.0);
  EXPECT_FALSE(changed_during_draw.ok);
  EXPECT_FALSE(changed_during_draw.changed);
  EXPECT_EQ(harness.tool.box_settings().subdivisions_x, 1);

  EXPECT_TRUE(harness.tool.cancel().ok);
}

} // namespace
} // namespace quader
