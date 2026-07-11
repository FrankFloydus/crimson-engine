////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_mesh_arrangement_catalog.hpp"
#include "editor/session/project_session.hpp"
#include "scene/scene_bounds.hpp"
#include "viewport/viewport_input_types.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <optional>
#include <string_view>

namespace {

constexpr float kEpsilon = 0.001f;

/// Owns an isolated editor session for native mesh arrangement behavior tests.
struct ArrangementHarness {
  quader::ProjectSession session;
  quader::QuaderEditorApi editor;

  ArrangementHarness()
      : editor(quader::QuaderApiSessionInternalAccess::from_project_session(
            session)) {}
};

quader::QuaderModelingObjectRef
create_box(ArrangementHarness &harness, std::string_view name,
           quader::QuaderBounds local_bounds,
           quader::QuaderTransform transform = {}) {
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
      harness.editor.modeling().create_box(local_bounds, std::string(name));
  EXPECT_TRUE(created.ok()) << created.error_message();
  if (created.ok()) {
    const quader::QuaderApiVoidResult moved =
        harness.editor.scene().set_transform(created.value().entity, transform);
    EXPECT_TRUE(moved.ok()) << moved.error_message();
  }
  return created.value();
}

quader::SceneBounds bounds(const ArrangementHarness &harness,
                           quader::EntityId entity) {
  const std::optional<quader::SceneBounds> result = quader::entity_world_bounds(
      harness.session.scene(), harness.session.assets(), entity);
  EXPECT_TRUE(result.has_value());
  return result.value_or(quader::SceneBounds{});
}

float center_x(const quader::SceneBounds &value) {
  return quader::bounds_center(value).x;
}

void expect_transform_near(const quader::TransformComponent &actual,
                           const quader::TransformComponent &expected) {
  EXPECT_NEAR(actual.position.x, expected.position.x, kEpsilon);
  EXPECT_NEAR(actual.position.y, expected.position.y, kEpsilon);
  EXPECT_NEAR(actual.position.z, expected.position.z, kEpsilon);
  EXPECT_NEAR(actual.rotation_euler.x, expected.rotation_euler.x, kEpsilon);
  EXPECT_NEAR(actual.rotation_euler.y, expected.rotation_euler.y, kEpsilon);
  EXPECT_NEAR(actual.rotation_euler.z, expected.rotation_euler.z, kEpsilon);
  EXPECT_NEAR(actual.scale.x, expected.scale.x, kEpsilon);
  EXPECT_NEAR(actual.scale.y, expected.scale.y, kEpsilon);
  EXPECT_NEAR(actual.scale.z, expected.scale.z, kEpsilon);
  EXPECT_EQ(actual.custom_pivot, expected.custom_pivot);
}

TEST(MeshArrangementTests,
     AlignsWorldAabbMinimumCenterAndMaximumToTheActiveMesh) {
  ArrangementHarness harness;
  const quader::QuaderModelingObjectRef active =
      create_box(harness, "Active",
                 {.min = {-1.0f, -2.0f, -1.5f}, .max = {1.0f, 2.0f, 1.5f}},
                 {.position = {8.0f, 3.0f, 5.0f},
                  .rotation_euler = {0.2f, -0.3f, 0.45f},
                  .scale = {1.5f, 0.75f, 2.0f}});
  const quader::QuaderModelingObjectRef other =
      create_box(harness, "Other",
                 {.min = {-2.0f, -1.0f, -3.0f}, .max = {2.0f, 1.0f, 3.0f}},
                 {.position = {-4.0f, 11.0f, -7.0f},
                  .rotation_euler = {-0.1f, 0.25f, -0.35f},
                  .scale = {0.8f, 1.2f, 0.6f}});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({active.entity, other.entity}, active.entity)
                  .ok());

  const quader::TransformComponent active_before =
      *harness.session.scene().transform(active.entity);
  const std::optional<quader::QuaderMeshAsset> topology_before =
      harness.editor.query().compiled_object_mesh(other);
  ASSERT_TRUE(topology_before.has_value());

  ASSERT_TRUE(harness.editor.modeling()
                  .align_selected_meshes(quader::QuaderTransformAxis::X,
                                         quader::QuaderMeshAlignment::Center)
                  .ok());
  EXPECT_NEAR(quader::bounds_center(bounds(harness, active.entity)).x,
              quader::bounds_center(bounds(harness, other.entity)).x, kEpsilon);
  ASSERT_TRUE(harness.editor.modeling()
                  .align_selected_meshes(quader::QuaderTransformAxis::Y,
                                         quader::QuaderMeshAlignment::Minimum)
                  .ok());
  EXPECT_NEAR(bounds(harness, active.entity).min.y,
              bounds(harness, other.entity).min.y, kEpsilon);
  ASSERT_TRUE(harness.editor.modeling()
                  .align_selected_meshes(quader::QuaderTransformAxis::Z,
                                         quader::QuaderMeshAlignment::Maximum)
                  .ok());
  EXPECT_NEAR(bounds(harness, active.entity).max.z,
              bounds(harness, other.entity).max.z, kEpsilon);

  expect_transform_near(*harness.session.scene().transform(active.entity),
                        active_before);
  EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 2u);
  EXPECT_EQ(harness.editor.query().history().undo_label, "Align Meshes");
  const std::optional<quader::QuaderMeshAsset> topology_after =
      harness.editor.query().compiled_object_mesh(other);
  ASSERT_TRUE(topology_after.has_value());
  EXPECT_EQ(topology_after->vertices.size(), topology_before->vertices.size());
  EXPECT_EQ(topology_after->indices.size(), topology_before->indices.size());

  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_GT(std::abs(bounds(harness, active.entity).max.z -
                     bounds(harness, other.entity).max.z),
            0.1f);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  EXPECT_NEAR(bounds(harness, active.entity).max.z,
              bounds(harness, other.entity).max.z, kEpsilon);
}

TEST(MeshArrangementTests,
     DistributesCentersAndSignedGapsWhileKeepingOuterMeshesFixed) {
  ArrangementHarness harness;
  const std::array<float, 4> widths{4.0f, 2.0f, 5.0f, 4.0f};
  const std::array<float, 4> positions{0.0f, 0.5f, 2.5f, 3.0f};
  std::array<quader::QuaderModelingObjectRef, 4> objects;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    const float half = widths[index] * 0.5f;
    objects[index] =
        create_box(harness, "Distributed",
                   {.min = {-half, -1.0f, -1.0f}, .max = {half, 1.0f, 1.0f}},
                   {.position = {positions[index], 0.0f, 0.0f}});
  }
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({objects[0].entity, objects[1].entity,
                                   objects[2].entity, objects[3].entity},
                                  objects[0].entity)
                  .ok());
  const quader::TransformComponent first_before =
      *harness.session.scene().transform(objects.front().entity);
  const quader::TransformComponent last_before =
      *harness.session.scene().transform(objects.back().entity);

  const quader::QuaderApiVoidResult centers =
      harness.editor.modeling().distribute_selected_meshes(
          quader::QuaderTransformAxis::X,
          quader::QuaderMeshDistribution::Centers);
  ASSERT_TRUE(centers.ok()) << centers.error_message();
  ASSERT_TRUE(centers.changed());
  const float center_step =
      (center_x(bounds(harness, objects.back().entity)) -
       center_x(bounds(harness, objects.front().entity))) /
      3.0f;
  for (std::size_t index = 1; index + 1 < objects.size(); ++index) {
    EXPECT_NEAR(center_x(bounds(harness, objects[index].entity)),
                center_x(bounds(harness, objects.front().entity)) +
                    center_step * static_cast<float>(index),
                kEpsilon);
  }
  ASSERT_TRUE(harness.editor.history().undo().ok());

  const quader::QuaderApiVoidResult gaps =
      harness.editor.modeling().distribute_selected_meshes(
          quader::QuaderTransformAxis::X, quader::QuaderMeshDistribution::Gaps);
  ASSERT_TRUE(gaps.ok()) << gaps.error_message();
  ASSERT_TRUE(gaps.changed());
  std::array<float, 3> actual_gaps;
  for (std::size_t index = 0; index < actual_gaps.size(); ++index) {
    actual_gaps[index] = bounds(harness, objects[index + 1].entity).min.x -
                         bounds(harness, objects[index].entity).max.x;
  }
  EXPECT_LT(actual_gaps.front(), 0.0f);
  EXPECT_NEAR(actual_gaps[0], actual_gaps[1], kEpsilon);
  EXPECT_NEAR(actual_gaps[1], actual_gaps[2], kEpsilon);
  expect_transform_near(
      *harness.session.scene().transform(objects.front().entity), first_before);
  expect_transform_near(
      *harness.session.scene().transform(objects.back().entity), last_before);
}

TEST(MeshArrangementTests,
     ParentedAlignmentPreservesLocalRotationScalePivotAndOtherWorldAxes) {
  ArrangementHarness harness;
  quader::QuaderCreateEntityArgs parent_args;
  parent_args.name = "Transform Parent";
  parent_args.kind = quader::QuaderEntityKind::Empty;
  parent_args.transform = {
      .position = {3.0f, -2.0f, 4.0f},
      .rotation_euler = {0.25f, -0.4f, 0.3f},
      .scale = {1.5f, 0.8f, 2.0f},
  };
  const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
      harness.editor.scene().create_entity(parent_args);
  ASSERT_TRUE(parent.ok()) << parent.error_message();

  const quader::QuaderModelingObjectRef active =
      create_box(harness, "Parented Active",
                 {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                 {.position = {-2.0f, 5.0f, 7.0f}});
  const quader::QuaderModelingObjectRef other =
      create_box(harness, "Parented Other",
                 {.min = {-2.0f, -0.5f, -1.5f}, .max = {2.0f, 0.5f, 1.5f}},
                 {.position = {8.0f, -6.0f, -3.0f},
                  .rotation_euler = {-0.2f, 0.35f, 0.15f},
                  .scale = {0.7f, 1.4f, 0.9f}});
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(other.entity, parent.value().id, true)
                  .ok());
  quader::TransformComponent marked =
      *harness.session.scene().transform(other.entity);
  marked.custom_pivot = true;
  harness.session.scene().set_transform(other.entity, marked);
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({active.entity, other.entity}, active.entity)
                  .ok());

  const quader::TransformComponent before =
      *harness.session.scene().transform(other.entity);
  const quader::SceneBounds world_before = bounds(harness, other.entity);
  const quader::QuaderApiVoidResult aligned =
      harness.editor.modeling().align_selected_meshes(
          quader::QuaderTransformAxis::Y, quader::QuaderMeshAlignment::Maximum);
  ASSERT_TRUE(aligned.ok()) << aligned.error_message();
  ASSERT_TRUE(aligned.changed());

  const quader::TransformComponent after =
      *harness.session.scene().transform(other.entity);
  EXPECT_FLOAT_EQ(after.rotation_euler.x, before.rotation_euler.x);
  EXPECT_FLOAT_EQ(after.rotation_euler.y, before.rotation_euler.y);
  EXPECT_FLOAT_EQ(after.rotation_euler.z, before.rotation_euler.z);
  EXPECT_FLOAT_EQ(after.scale.x, before.scale.x);
  EXPECT_FLOAT_EQ(after.scale.y, before.scale.y);
  EXPECT_FLOAT_EQ(after.scale.z, before.scale.z);
  EXPECT_TRUE(after.custom_pivot);
  const quader::SceneBounds world_after = bounds(harness, other.entity);
  EXPECT_NEAR(world_after.max.y, bounds(harness, active.entity).max.y,
              kEpsilon);
  EXPECT_NEAR(quader::bounds_center(world_after).x,
              quader::bounds_center(world_before).x, kEpsilon);
  EXPECT_NEAR(quader::bounds_center(world_after).z,
              quader::bounds_center(world_before).z, kEpsilon);
}

TEST(MeshArrangementTests,
     DistributionUsesStableEntityTieOrderAndUnchangedCallsAddNoHistory) {
  ArrangementHarness harness;
  std::array<quader::QuaderModelingObjectRef, 4> objects;
  const std::array<float, 4> positions{0.0f, 5.0f, 5.0f, 12.0f};
  for (std::size_t index = 0; index < objects.size(); ++index) {
    objects[index] =
        create_box(harness, "Tied Distribution",
                   {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                   {.position = {positions[index], 0.0f, 0.0f}});
  }
  ASSERT_LT(objects[1].entity, objects[2].entity);
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({objects[0].entity, objects[1].entity,
                                   objects[2].entity, objects[3].entity},
                                  objects[0].entity)
                  .ok());

  const quader::QuaderApiVoidResult first =
      harness.editor.modeling().distribute_selected_meshes(
          quader::QuaderTransformAxis::X,
          quader::QuaderMeshDistribution::Centers);
  ASSERT_TRUE(first.ok()) << first.error_message();
  EXPECT_TRUE(first.changed());
  EXPECT_NEAR(center_x(bounds(harness, objects[1].entity)), 4.0f, kEpsilon);
  EXPECT_NEAR(center_x(bounds(harness, objects[2].entity)), 8.0f, kEpsilon);

  const quader::QuaderApiVoidResult unchanged =
      harness.editor.modeling().distribute_selected_meshes(
          quader::QuaderTransformAxis::X,
          quader::QuaderMeshDistribution::Centers);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_NEAR(center_x(bounds(harness, objects[1].entity)), 5.0f, kEpsilon);
  EXPECT_NEAR(center_x(bounds(harness, objects[2].entity)), 5.0f, kEpsilon);

  const std::array<quader::TransformComponent, 4> before_invalid{
      *harness.session.scene().transform(objects[0].entity),
      *harness.session.scene().transform(objects[1].entity),
      *harness.session.scene().transform(objects[2].entity),
      *harness.session.scene().transform(objects[3].entity),
  };
  ASSERT_TRUE(harness.session.scene().set_visible(objects[2].entity, false));
  const quader::QuaderApiVoidResult invalid =
      harness.editor.modeling().distribute_selected_meshes(
          quader::QuaderTransformAxis::X, quader::QuaderMeshDistribution::Gaps);
  EXPECT_FALSE(invalid.ok());
  for (std::size_t index = 0; index < objects.size(); ++index) {
    expect_transform_near(
        *harness.session.scene().transform(objects[index].entity),
        before_invalid[index]);
  }
}

TEST(MeshArrangementTests,
     SnapsEachArrangementDeltaAndPromotesAnActiveNestedMeshToItsRoot) {
  ArrangementHarness harness;
  const quader::QuaderModelingObjectRef parent =
      create_box(harness, "Parent",
                 {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}});
  const quader::QuaderModelingObjectRef child =
      create_box(harness, "Child",
                 {.min = {-0.5f, -0.5f, -0.5f}, .max = {0.5f, 0.5f, 0.5f}},
                 {.position = {2.0f, 0.0f, 0.0f}});
  const quader::QuaderModelingObjectRef other =
      create_box(harness, "Other",
                 {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                 {.position = {5.3f, 0.0f, 0.0f}});
  ASSERT_TRUE(
      harness.editor.scene().reparent(child.entity, parent.entity).ok());
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({parent.entity, child.entity, other.entity},
                                  child.entity)
                  .ok());
  const quader::SceneBounds parent_before = bounds(harness, parent.entity);
  const quader::SceneBounds child_before = bounds(harness, child.entity);

  const quader::QuaderApiVoidResult snapped =
      harness.editor.modeling().align_selected_meshes(
          quader::QuaderTransformAxis::X, quader::QuaderMeshAlignment::Center,
          {.snap_to_grid = true, .grid_size = 0.25});
  ASSERT_TRUE(snapped.ok()) << snapped.error_message();
  ASSERT_TRUE(snapped.changed());
  EXPECT_NEAR(center_x(bounds(harness, parent.entity)), center_x(parent_before),
              kEpsilon);
  EXPECT_NEAR(center_x(bounds(harness, child.entity)), center_x(child_before),
              kEpsilon);
  EXPECT_NEAR(center_x(bounds(harness, other.entity)), 0.05f, kEpsilon);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  const quader::QuaderApiVoidResult exact =
      harness.editor.modeling().align_selected_meshes(
          quader::QuaderTransformAxis::X, quader::QuaderMeshAlignment::Center);
  ASSERT_TRUE(exact.ok()) << exact.error_message();
  EXPECT_NEAR(center_x(bounds(harness, other.entity)),
              center_x(bounds(harness, parent.entity)), kEpsilon);

  const quader::QuaderApiVoidResult invalid =
      harness.editor.modeling().align_selected_meshes(
          quader::QuaderTransformAxis::X, quader::QuaderMeshAlignment::Center,
          {.snap_to_grid = true, .grid_size = 0.0});
  EXPECT_FALSE(invalid.ok());
}

TEST(MeshArrangementTests,
     CatalogCommandsUseTheGlobalGridSnapPreferenceAndViewportPreset) {
  ArrangementHarness harness;
  bool grid_snap_enabled = true;
  harness.editor.preferences().set_runtime_hooks({
      .grid_snap_enabled = [&grid_snap_enabled] { return grid_snap_enabled; },
      .set_grid_snap_enabled =
          [&grid_snap_enabled](bool enabled) {
            const bool changed = grid_snap_enabled != enabled;
            grid_snap_enabled = enabled;
            return quader::QuaderApiVoidResult{.ok = true, .changed = changed};
          },
  });
  const quader::QuaderModelingObjectRef active =
      create_box(harness, "Command Active",
                 {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}});
  const quader::QuaderModelingObjectRef other =
      create_box(harness, "Command Other",
                 {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                 {.position = {0.3f, 0.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({active.entity, other.entity}, active.entity)
                  .ok());
  ASSERT_TRUE(harness.editor.viewports().set_grid_preset(4).ok());
  ASSERT_NEAR(quader::viewport_grid_world_size_for_preset(4), 0.25f, kEpsilon);

  ASSERT_EQ(quader::quader_mesh_arrangement_catalog().size(), 15u);
  for (const quader::QuaderMeshArrangementDescriptor &descriptor :
       quader::quader_mesh_arrangement_catalog()) {
    EXPECT_TRUE(
        harness.editor.commands().shortcut_for(descriptor.command_id).empty());
  }
  const quader::QuaderApiVoidResult result = harness.editor.commands().execute(
      quader::quader_command::kModelingAlignXCenter);
  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_NEAR(center_x(bounds(harness, other.entity)), 0.05f, kEpsilon);

  const quader::QuaderApiVoidResult disabled_snap =
      harness.editor.preferences().set_grid_snap_enabled(false);
  ASSERT_TRUE(disabled_snap.ok()) << disabled_snap.error_message();
  EXPECT_FALSE(harness.editor.preferences().grid_snap_enabled());
}

TEST(MeshArrangementTests, RejectsComponentModesAndInsufficientSelections) {
  ArrangementHarness harness;
  const quader::QuaderModelingObjectRef box =
      create_box(harness, "Only",
                 {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}});
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  EXPECT_FALSE(harness.editor.modeling().can_align_selected_meshes());
  EXPECT_FALSE(harness.editor.modeling().can_distribute_selected_meshes());
  EXPECT_FALSE(harness.editor.modeling()
                   .align_selected_meshes(quader::QuaderTransformAxis::X,
                                          quader::QuaderMeshAlignment::Center)
                   .ok());

  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Vertex)
                  .ok());
  EXPECT_FALSE(harness.editor.modeling()
                   .align_selected_meshes(quader::QuaderTransformAxis::X,
                                          quader::QuaderMeshAlignment::Center)
                   .ok());
  EXPECT_FALSE(
      harness.editor.modeling()
          .distribute_selected_meshes(quader::QuaderTransformAxis::X,
                                      quader::QuaderMeshDistribution::Centers)
          .ok());
}

} // namespace
