////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_mesh_arrangement_catalog.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/transform/mesh_arrangement_service.hpp"
#include "scene/scene_bounds.hpp"
#include "viewport/viewport_input_types.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>

namespace {

constexpr float kEpsilon = 0.001f;

/// Owns an isolated editor session for native mesh arrangement behavior tests.
struct ArrangementHarness {
  quader::SceneSession session;
  quader::QuaderEditorApi editor;

  ArrangementHarness()
      : editor(quader::QuaderApiSessionInternalAccess::from_scene_session(
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
  EXPECT_EQ(actual.parenting_offset.values, expected.parenting_offset.values);
}

quader::EntityId create_instance(ArrangementHarness &harness,
                                 quader::QuaderModelingObjectRef source,
                                 quader::QuaderTransform transform = {}) {
  EXPECT_TRUE(harness.editor.selection().select_object(source).ok());
  const auto created =
      harness.editor.modeling().create_instance_from_selection();
  EXPECT_TRUE(created.ok()) << created.error_message();
  const quader::EntityId occurrence =
      harness.session.scene().primary_selection();
  EXPECT_NE(harness.session.scene().instance_occurrence(occurrence), nullptr);
  EXPECT_TRUE(harness.editor.scene().set_transform(occurrence, transform).ok());
  return occurrence;
}

void expect_mesh_unchanged(const quader::QuaderMeshAsset &actual,
                           const quader::QuaderMeshAsset &expected) {
  EXPECT_EQ(actual.id, expected.id);
  EXPECT_EQ(actual.revision, expected.revision);
  EXPECT_EQ(actual.indices, expected.indices);
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  for (std::size_t index = 0; index < actual.vertices.size(); ++index) {
    EXPECT_FLOAT_EQ(actual.vertices[index].position.x,
                    expected.vertices[index].position.x);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.y,
                    expected.vertices[index].position.y);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.z,
                    expected.vertices[index].position.z);
    EXPECT_FLOAT_EQ(actual.vertices[index].normal.x,
                    expected.vertices[index].normal.x);
    EXPECT_FLOAT_EQ(actual.vertices[index].normal.y,
                    expected.vertices[index].normal.y);
    EXPECT_FLOAT_EQ(actual.vertices[index].normal.z,
                    expected.vertices[index].normal.z);
    EXPECT_FLOAT_EQ(actual.vertices[index].uv.x, expected.vertices[index].uv.x);
    EXPECT_FLOAT_EQ(actual.vertices[index].uv.y, expected.vertices[index].uv.y);
    EXPECT_EQ(actual.vertices[index].uv_carrier,
              expected.vertices[index].uv_carrier);
  }
}

TEST(MeshArrangementTests,
     AlignsWorldAabbMinimumCenterAndMaximumToTheOriginalSelectionBounds) {
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
  const auto active_bounds = bounds(harness, active.entity);
  const auto other_bounds = bounds(harness, other.entity);
  const float reference_x =
      (std::min(active_bounds.min.x, other_bounds.min.x) +
       std::max(active_bounds.max.x, other_bounds.max.x)) * 0.5f;
  const float reference_y = std::min(active_bounds.min.y, other_bounds.min.y);
  const float reference_z = std::max(active_bounds.max.z, other_bounds.max.z);
  const std::optional<quader::QuaderMeshAsset> topology_before =
      harness.editor.query().compiled_object_mesh(other);
  ASSERT_TRUE(topology_before.has_value());

  ASSERT_TRUE(harness.editor.modeling()
                  .align_selected_meshes(quader::QuaderTransformAxis::X,
                                         quader::QuaderMeshAlignment::Center)
                  .ok());
  EXPECT_NEAR(center_x(bounds(harness, active.entity)), reference_x, kEpsilon);
  EXPECT_NEAR(center_x(bounds(harness, other.entity)), reference_x, kEpsilon);
  ASSERT_TRUE(harness.editor.modeling()
                  .align_selected_meshes(quader::QuaderTransformAxis::Y,
                                         quader::QuaderMeshAlignment::Minimum)
                  .ok());
  EXPECT_NEAR(bounds(harness, active.entity).min.y, reference_y, kEpsilon);
  EXPECT_NEAR(bounds(harness, other.entity).min.y, reference_y, kEpsilon);
  ASSERT_TRUE(harness.editor.modeling()
                  .align_selected_meshes(quader::QuaderTransformAxis::Z,
                                         quader::QuaderMeshAlignment::Maximum)
                  .ok());
  EXPECT_NEAR(bounds(harness, active.entity).max.z, reference_z, kEpsilon);
  EXPECT_NEAR(bounds(harness, other.entity).max.z, reference_z, kEpsilon);

  auto preserved_channels = active_before;
  preserved_channels.position =
      harness.session.scene().transform(active.entity)->position;
  expect_transform_near(*harness.session.scene().transform(active.entity),
                        preserved_channels);
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
     LegacyDistributionModesShareSignedGapsAndKeepOuterMeshesFixed) {
  ArrangementHarness harness;
  const std::array<float, 4> widths{4.0f, 2.0f, 5.0f, 4.0f};
  const std::array<float, 4> positions{0.0f, 0.5f, 2.5f, 4.0f};
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
  std::array<quader::TransformComponent, 4> distributed;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    distributed[index] = *harness.session.scene().transform(objects[index].entity);
    if (index + 1u < objects.size()) {
      EXPECT_NEAR(bounds(harness, objects[index + 1u].entity).min.x -
                      bounds(harness, objects[index].entity).max.x,
                  -7.0f / 3.0f, kEpsilon);
    }
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
  for (std::size_t index = 0; index < objects.size(); ++index) {
    expect_transform_near(*harness.session.scene().transform(objects[index].entity),
                          distributed[index]);
  }
  expect_transform_near(
      *harness.session.scene().transform(objects.front().entity), first_before);
  expect_transform_near(
      *harness.session.scene().transform(objects.back().entity), last_before);
}

TEST(MeshArrangementTests,
     AlignmentReferenceIgnoresActiveObjectAndSelectionOrder) {
  ArrangementHarness harness;
  const std::array<float, 3> widths{2.0f, 6.0f, 3.5f};
  const std::array<float, 3> centers{-9.25f, -2.0f, 15.5f};
  std::array<quader::QuaderModelingObjectRef, 3> objects;
  std::array<quader::TransformComponent, 3> original_transforms;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    const float half_width = widths[index] * 0.5f;
    objects[index] = create_box(
        harness, "Selection Reference",
        {.min = {-half_width, -1.0f, -1.0f},
         .max = {half_width, 1.0f, 1.0f}},
        {.position = {centers[index], 3.0f, -4.0f}});
    original_transforms[index] =
        *harness.session.scene().transform(objects[index].entity);
  }
  harness.session.history().clear();
  const std::array<std::array<std::size_t, 3>, 3> orders{{
      {0u, 1u, 2u}, {2u, 1u, 0u}, {1u, 0u, 2u}}};
  const std::array<quader::QuaderMeshAlignment, 3> modes{
      quader::QuaderMeshAlignment::Minimum,
      quader::QuaderMeshAlignment::Center,
      quader::QuaderMeshAlignment::Maximum};
  // Original world interval [-10.25,17.25] defines all three references.
  const std::array<float, 3> references{-10.25f, 3.5f, 17.25f};
  for (std::size_t mode = 0; mode < modes.size(); ++mode) {
    for (std::size_t variant = 0; variant < orders.size(); ++variant) {
      SCOPED_TRACE(mode);
      SCOPED_TRACE(variant);
      const auto &order = orders[variant];
      ASSERT_TRUE(harness.editor.selection().select_objects(
          {objects[order[0]].entity, objects[order[1]].entity,
           objects[order[2]].entity}, objects[variant].entity).ok());
      harness.session.history().clear();
      const auto aligned = harness.editor.modeling().align_selected_meshes(
          quader::QuaderTransformAxis::X, modes[mode]);
      ASSERT_TRUE(aligned.ok()) << aligned.error_message();
      ASSERT_TRUE(aligned.changed());
      for (const auto object : objects) {
        const auto actual = bounds(harness, object.entity);
        const float coordinate = mode == 0u ? actual.min.x
            : mode == 1u ? center_x(actual) : actual.max.x;
        EXPECT_NEAR(coordinate, references[mode], kEpsilon);
        EXPECT_NEAR(quader::bounds_center(actual).y, 3.0f, kEpsilon);
        EXPECT_NEAR(quader::bounds_center(actual).z, -4.0f, kEpsilon);
      }
      EXPECT_EQ(harness.session.scene().primary_selection(), objects[variant].entity);
      EXPECT_EQ(harness.session.history().undo_count(), 1u);
      ASSERT_TRUE(harness.editor.history().undo().ok());
      for (std::size_t index = 0; index < objects.size(); ++index) {
        expect_transform_near(
            *harness.session.scene().transform(objects[index].entity),
            original_transforms[index]);
      }
    }
  }
}

TEST(MeshArrangementTests,
     UnevenWidthsUseExactDefaultGapsIndependentOfModeGridAndActiveOrder) {
  ArrangementHarness harness;
  const std::array<float, 3> widths{1.25f, 2.5f, 3.75f};
  const std::array<float, 3> centers{-12.3f, -8.05f, 20.4f};
  std::array<quader::QuaderModelingObjectRef, 3> objects;
  std::array<quader::TransformComponent, 3> original_transforms;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    const float half_width = widths[index] * 0.5f;
    objects[index] = create_box(
        harness, "Fractional Distribution",
        {.min = {-half_width, -1.0f, -1.0f},
         .max = {half_width, 1.0f, 1.0f}},
        {.position = {centers[index], 3.0f, -4.0f}});
    original_transforms[index] =
        *harness.session.scene().transform(objects[index].entity);
  }
  harness.session.history().clear();
  const std::array<quader::QuaderMeshDistribution, 2> modes{
      quader::QuaderMeshDistribution::Centers,
      quader::QuaderMeshDistribution::Gaps};
  for (const auto mode : modes) {
    for (std::size_t active = 0; active < objects.size(); ++active) {
      SCOPED_TRACE(static_cast<int>(mode));
      SCOPED_TRACE(active);
      ASSERT_TRUE(harness.editor.selection().select_objects(
          {objects[2].entity, objects[active == 1u ? 1u : 0u].entity,
           objects[active == 1u ? 0u : 1u].entity}, objects[active].entity).ok());
      harness.session.history().clear();
      const auto distributed = harness.editor.modeling().distribute_selected_meshes(
          quader::QuaderTransformAxis::X, mode,
          {.grid_size = 64.0});
      ASSERT_TRUE(distributed.ok()) << distributed.error_message();
      ASSERT_TRUE(distributed.changed());
      EXPECT_NEAR(center_x(bounds(harness, objects[1].entity)), 3.425f, kEpsilon);
      EXPECT_NEAR(bounds(harness, objects[1].entity).min.x -
                      bounds(harness, objects[0].entity).max.x,
                  13.85f, kEpsilon);
      EXPECT_NEAR(bounds(harness, objects[2].entity).min.x -
                      bounds(harness, objects[1].entity).max.x,
                  13.85f, kEpsilon);
      expect_transform_near(*harness.session.scene().transform(objects[0].entity),
                            original_transforms[0]);
      expect_transform_near(*harness.session.scene().transform(objects[2].entity),
                            original_transforms[2]);
      for (std::size_t index = 0; index < objects.size(); ++index) {
        const auto actual = bounds(harness, objects[index].entity);
        EXPECT_NEAR(actual.max.x - actual.min.x, widths[index], kEpsilon);
        EXPECT_NEAR(quader::bounds_center(actual).y, 3.0f, kEpsilon);
        EXPECT_NEAR(quader::bounds_center(actual).z, -4.0f, kEpsilon);
      }
      EXPECT_EQ(harness.session.scene().primary_selection(), objects[active].entity);
      EXPECT_EQ(harness.session.history().undo_count(), 1u);
      const auto middle = *harness.session.scene().transform(objects[1].entity);
      const auto unchanged = harness.editor.modeling().distribute_selected_meshes(
          quader::QuaderTransformAxis::X, mode);
      ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
      EXPECT_FALSE(unchanged.changed());
      EXPECT_EQ(harness.session.history().undo_count(), 1u);
      ASSERT_TRUE(harness.editor.history().undo().ok());
      for (std::size_t index = 0; index < objects.size(); ++index) {
        expect_transform_near(
            *harness.session.scene().transform(objects[index].entity),
            original_transforms[index]);
      }
      ASSERT_TRUE(harness.editor.history().redo().ok());
      expect_transform_near(*harness.session.scene().transform(objects[1].entity),
                            middle);
      ASSERT_TRUE(harness.editor.history().undo().ok());
    }
  }
}

TEST(MeshArrangementTests,
     SnappingSearchChoosesTheCoarsestFittingMovementGridAndIgnoresViewportGrid) {
  struct SnapSample {
    std::array<float, 3> widths;
    std::array<float, 3> centers;
    double expected_grid = 1.0;
    float expected_middle = 0.0f;
    float expected_gap = 0.0f;
  };
  const std::array<SnapSample, 3> samples{{
      {{2.0f, 2.0f, 2.0f}, {0.0f, 4.0f, 20.0f}, 2.0, 10.0f, 8.0f},
      {{2.0f, 2.0f, 2.0f}, {0.0f, 3.5f, 20.0f}, 0.5, 10.0f, 8.0f},
      {{2.0f, 4.0f, 6.0f}, {0.0f, 4.5f, 18.0f}, 0.5, 8.0f, 5.0f}}};
  for (const auto &sample : samples) {
    SCOPED_TRACE(sample.expected_grid);
    ArrangementHarness harness;
    std::array<quader::QuaderModelingObjectRef, 3> objects;
    for (std::size_t index = 0; index < objects.size(); ++index) {
      const float half_width = sample.widths[index] * 0.5f;
      objects[index] = create_box(
          harness, "Snapping Search",
          {.min = {-half_width, -1.0f, -1.0f},
           .max = {half_width, 1.0f, 1.0f}},
          {.position = {sample.centers[index], 0.0f, 0.0f}});
    }
    ASSERT_TRUE(harness.editor.selection().select_objects(
        {objects[0].entity, objects[1].entity, objects[2].entity},
        objects[1].entity).ok());
    harness.session.history().clear();
    quader::MeshArrangementService service(
        harness.session.scene(), harness.session.assets(), {.object_mode = true},
        harness.session.document_commands());
    const auto baseline = service.capture_baseline();
    ASSERT_TRUE(baseline.ok) << baseline.message;
    quader::MeshArrangementIntent intent;
    intent.axes[0] = quader::MeshArrangementMode::Distribute;
    const auto exact = quader::MeshArrangementService::plan(baseline, intent);
    ASSERT_TRUE(exact.ok) << exact.message;
    EXPECT_FALSE(exact.snapping_requested);
    EXPECT_FALSE(exact.used_grid_alignment);
    EXPECT_FALSE(exact.grid_alignment_unavailable);
    const quader::MeshArrangementSnapRequest snapping{
        .enabled = true,
        .candidate_grid_sizes = {64.0, 32.0, 16.0, 8.0, 4.0, 2.0,
                                 1.0, 0.5, 0.25, 0.125, 0.0625, 0.03125}};
    const auto snapped =
        quader::MeshArrangementService::plan(baseline, intent, snapping);
    ASSERT_TRUE(snapped.ok) << snapped.message;
    EXPECT_TRUE(snapped.snapping_requested);
    EXPECT_TRUE(snapped.used_grid_alignment);
    EXPECT_FALSE(snapped.grid_alignment_unavailable);
    ASSERT_TRUE(snapped.snap_grid_size.has_value());
    EXPECT_DOUBLE_EQ(*snapped.snap_grid_size, sample.expected_grid);
    ASSERT_EQ(snapped.edits.size(), 1u);
    EXPECT_EQ(snapped.edits.front().entity, objects[1].entity);
    EXPECT_NEAR(snapped.edits.front().after.position.x,
                sample.expected_middle, kEpsilon);
    EXPECT_NEAR(center_x(bounds(harness, objects[1].entity)),
                sample.centers[1], kEpsilon);
    EXPECT_EQ(harness.session.history().undo_count(), 0u);
    const auto first_before = *harness.session.scene().transform(objects[0].entity);
    const auto last_before = *harness.session.scene().transform(objects[2].entity);
    for (const auto preset : {0, 10}) {
      ASSERT_TRUE(harness.editor.viewports().set_grid_preset(preset).ok());
      const auto applied = harness.editor.modeling().distribute_selected_meshes(
          quader::QuaderTransformAxis::X, quader::QuaderMeshDistribution::Gaps,
          {.snap_to_grid = true,
           .grid_size = preset == 0 ? 64.0 : 0.03125});
      ASSERT_TRUE(applied.ok()) << applied.error_message();
      ASSERT_TRUE(applied.changed());
      EXPECT_NEAR(center_x(bounds(harness, objects[1].entity)),
                  sample.expected_middle, kEpsilon);
      EXPECT_NEAR(bounds(harness, objects[1].entity).min.x -
                      bounds(harness, objects[0].entity).max.x,
                  sample.expected_gap, kEpsilon);
      EXPECT_NEAR(bounds(harness, objects[2].entity).min.x -
                      bounds(harness, objects[1].entity).max.x,
                  sample.expected_gap, kEpsilon);
      expect_transform_near(*harness.session.scene().transform(objects[0].entity),
                            first_before);
      expect_transform_near(*harness.session.scene().transform(objects[2].entity),
                            last_before);
      EXPECT_EQ(harness.session.history().undo_count(), 1u);
      ASSERT_TRUE(harness.editor.history().undo().ok());
    }
  }
}

TEST(MeshArrangementTests,
     ThirdsSnappingFallbackRetainsExactEqualGapsAndOriginalEndpoints) {
  ArrangementHarness harness;
  const std::array<float, 4> centers{0.0f, 1.0f, 5.0f, 10.0f};
  std::array<quader::QuaderModelingObjectRef, 4> objects;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    objects[index] = create_box(
        harness, "Thirds Distribution",
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        {.position = {centers[index], 0.0f, 0.0f}});
  }
  ASSERT_TRUE(harness.editor.selection().select_objects(
      {objects[3].entity, objects[0].entity, objects[2].entity, objects[1].entity},
      objects[2].entity).ok());
  harness.session.history().clear();
  quader::MeshArrangementService service(
      harness.session.scene(), harness.session.assets(), {.object_mode = true},
      harness.session.document_commands());
  const auto baseline = service.capture_baseline();
  ASSERT_TRUE(baseline.ok) << baseline.message;
  quader::MeshArrangementIntent intent;
  intent.axes[0] = quader::MeshArrangementMode::Distribute;
  const auto fallback = quader::MeshArrangementService::plan(
      baseline, intent,
      {.enabled = true,
       .candidate_grid_sizes = {64.0, 32.0, 16.0, 8.0, 4.0, 2.0,
                                1.0, 0.5, 0.25, 0.125, 0.0625, 0.03125}});
  ASSERT_TRUE(fallback.ok) << fallback.message;
  EXPECT_TRUE(fallback.snapping_requested);
  EXPECT_FALSE(fallback.used_grid_alignment);
  EXPECT_TRUE(fallback.grid_alignment_unavailable);
  EXPECT_FALSE(fallback.snap_grid_size.has_value());
  EXPECT_FALSE(quader::mesh_arrangement_grid_alignment_warning().empty());
  ASSERT_EQ(fallback.edits.size(), 2u);
  const auto first_before = *harness.session.scene().transform(objects[0].entity);
  const auto last_before = *harness.session.scene().transform(objects[3].entity);
  const auto applied = harness.editor.modeling().distribute_selected_meshes(
      quader::QuaderTransformAxis::X, quader::QuaderMeshDistribution::Gaps,
      {.snap_to_grid = true, .grid_size = 64.0});
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());
  EXPECT_NEAR(center_x(bounds(harness, objects[1].entity)), 10.0f / 3.0f, kEpsilon);
  EXPECT_NEAR(center_x(bounds(harness, objects[2].entity)), 20.0f / 3.0f, kEpsilon);
  for (std::size_t index = 0; index + 1u < objects.size(); ++index) {
    EXPECT_NEAR(bounds(harness, objects[index + 1u].entity).min.x -
                    bounds(harness, objects[index].entity).max.x,
                4.0f / 3.0f, kEpsilon);
  }
  expect_transform_near(*harness.session.scene().transform(objects[0].entity),
                        first_before);
  expect_transform_near(*harness.session.scene().transform(objects[3].entity),
                        last_before);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
}

TEST(MeshArrangementTests,
     NonGridFractionalSnappingFallbackKeepsUnequalWidthsAndEqualGaps) {
  ArrangementHarness harness;
  const std::array<float, 3> widths{1.25f, 2.5f, 3.75f};
  const std::array<float, 3> centers{-12.3f, -8.05f, 20.4f};
  std::array<quader::QuaderModelingObjectRef, 3> objects;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    const float half_width = widths[index] * 0.5f;
    objects[index] = create_box(
        harness, "Non Grid Distribution",
        {.min = {-half_width, -1.0f, -1.0f},
         .max = {half_width, 1.0f, 1.0f}},
        {.position = {centers[index], 0.0f, 0.0f}});
  }
  ASSERT_TRUE(harness.editor.selection().select_objects(
      {objects[0].entity, objects[1].entity, objects[2].entity},
      objects[1].entity).ok());
  harness.session.history().clear();
  quader::MeshArrangementService service(
      harness.session.scene(), harness.session.assets(), {.object_mode = true},
      harness.session.document_commands());
  const auto baseline = service.capture_baseline();
  ASSERT_TRUE(baseline.ok) << baseline.message;
  quader::MeshArrangementIntent intent;
  intent.axes[0] = quader::MeshArrangementMode::Distribute;
  const auto fallback = quader::MeshArrangementService::plan(
      baseline, intent,
      {.enabled = true,
       .candidate_grid_sizes = {64.0, 32.0, 16.0, 8.0, 4.0, 2.0,
                                1.0, 0.5, 0.25, 0.125, 0.0625, 0.03125}});
  ASSERT_TRUE(fallback.ok) << fallback.message;
  EXPECT_FALSE(fallback.used_grid_alignment);
  EXPECT_TRUE(fallback.grid_alignment_unavailable);
  EXPECT_FALSE(fallback.snap_grid_size.has_value());
  const auto first_before = *harness.session.scene().transform(objects[0].entity);
  const auto last_before = *harness.session.scene().transform(objects[2].entity);
  const auto applied = harness.editor.modeling().distribute_selected_meshes(
      quader::QuaderTransformAxis::X, quader::QuaderMeshDistribution::Centers,
      {.snap_to_grid = true, .grid_size = 0.03125});
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());
  EXPECT_NEAR(center_x(bounds(harness, objects[1].entity)), 3.425f, kEpsilon);
  for (std::size_t index = 0; index + 1u < objects.size(); ++index) {
    EXPECT_NEAR(bounds(harness, objects[index + 1u].entity).min.x -
                    bounds(harness, objects[index].entity).max.x,
                13.85f, kEpsilon);
  }
  for (std::size_t index = 0; index < objects.size(); ++index) {
    const auto actual = bounds(harness, objects[index].entity);
    EXPECT_NEAR(actual.max.x - actual.min.x, widths[index], kEpsilon);
  }
  expect_transform_near(*harness.session.scene().transform(objects[0].entity),
                        first_before);
  expect_transform_near(*harness.session.scene().transform(objects[2].entity),
                        last_before);
}

TEST(MeshArrangementTests,
     CrossingExtentsKeepTheWideBoundaryOwnerAndRedistributeNarrowObjects) {
  ArrangementHarness harness;
  const std::array<float, 3> widths{2.0f, 20.0f, 2.0f};
  const std::array<float, 3> centers{0.0f, 5.0f, 10.0f};
  std::array<quader::QuaderModelingObjectRef, 3> objects;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    const float half_width = widths[index] * 0.5f;
    objects[index] = create_box(
        harness, "Crossing Extent",
        {.min = {-half_width, -1.0f, -1.0f},
         .max = {half_width, 1.0f, 1.0f}},
        {.position = {centers[index], 0.0f, 0.0f}});
  }
  ASSERT_TRUE(harness.editor.selection().select_objects(
      {objects[2].entity, objects[0].entity, objects[1].entity},
      objects[0].entity).ok());
  const auto wide_before = *harness.session.scene().transform(objects[1].entity);
  harness.session.history().clear();
  const auto distributed = harness.editor.modeling().distribute_selected_meshes(
      quader::QuaderTransformAxis::X, quader::QuaderMeshDistribution::Gaps);
  ASSERT_TRUE(distributed.ok()) << distributed.error_message();
  ASSERT_TRUE(distributed.changed());
  EXPECT_NEAR(center_x(bounds(harness, objects[0].entity)), -4.0f, kEpsilon);
  EXPECT_NEAR(center_x(bounds(harness, objects[2].entity)), 14.0f, kEpsilon);
  expect_transform_near(*harness.session.scene().transform(objects[1].entity),
                        wide_before);
  EXPECT_NEAR(bounds(harness, objects[1].entity).min.x, -5.0f, kEpsilon);
  EXPECT_NEAR(bounds(harness, objects[1].entity).max.x, 15.0f, kEpsilon);
  EXPECT_NEAR(bounds(harness, objects[1].entity).min.x -
                  bounds(harness, objects[0].entity).max.x, -2.0f, kEpsilon);
  EXPECT_NEAR(bounds(harness, objects[2].entity).min.x -
                  bounds(harness, objects[1].entity).max.x, -2.0f, kEpsilon);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_NEAR(center_x(bounds(harness, objects[0].entity)), 0.0f, kEpsilon);
  EXPECT_NEAR(center_x(bounds(harness, objects[2].entity)), 10.0f, kEpsilon);
}

TEST(MeshArrangementTests,
     UnsatisfiableCrossingExtentsRejectWithoutChangingTransformsOrHistory) {
  ArrangementHarness harness;
  const std::array<float, 4> widths{1.0f, 1.0f, 100.0f, 100.0f};
  const std::array<float, 4> centers{0.0f, 1.0f, 2.0f, 3.0f};
  std::array<quader::QuaderModelingObjectRef, 4> objects;
  std::array<quader::TransformComponent, 4> original_transforms;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    const float half_width = widths[index] * 0.5f;
    objects[index] = create_box(
        harness, "Unsatisfiable Extent",
        {.min = {-half_width, -1.0f, -1.0f},
         .max = {half_width, 1.0f, 1.0f}},
        {.position = {centers[index], 0.0f, 0.0f}});
    original_transforms[index] =
        *harness.session.scene().transform(objects[index].entity);
  }
  ASSERT_TRUE(harness.editor.selection().select_objects(
      {objects[0].entity, objects[1].entity, objects[2].entity, objects[3].entity},
      objects[2].entity).ok());
  harness.session.history().clear();
  const auto asset_revision = harness.session.assets().revision();
  const auto scene_revision = harness.session.scene().revision();
  for (const auto mode : {quader::QuaderMeshDistribution::Centers,
                          quader::QuaderMeshDistribution::Gaps}) {
    const auto rejected = harness.editor.modeling().distribute_selected_meshes(
        quader::QuaderTransformAxis::X, mode);
    EXPECT_FALSE(rejected.ok());
    EXPECT_FALSE(rejected.changed());
    EXPECT_FALSE(rejected.error_message().empty());
    for (std::size_t index = 0; index < objects.size(); ++index) {
      expect_transform_near(
          *harness.session.scene().transform(objects[index].entity),
          original_transforms[index]);
    }
    EXPECT_EQ(harness.session.scene().revision(), scene_revision);
    EXPECT_EQ(harness.session.assets().revision(), asset_revision);
    EXPECT_EQ(harness.session.history().undo_count(), 0u);
    EXPECT_EQ(harness.session.scene().primary_selection(), objects[2].entity);
    EXPECT_EQ(harness.session.scene().selected_entities().size(), 4u);
  }
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
     ArrangementKeepsExactFallbackAndPromotesSelectedChildrenToTheirRoot) {
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
  EXPECT_NEAR(center_x(bounds(harness, parent.entity)), 2.65f, kEpsilon);
  EXPECT_NEAR(center_x(bounds(harness, child.entity)) - center_x(child_before),
              2.65f - center_x(parent_before), kEpsilon);
  EXPECT_NEAR(center_x(bounds(harness, other.entity)), 2.65f, kEpsilon);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  const quader::QuaderApiVoidResult exact =
      harness.editor.modeling().align_selected_meshes(
          quader::QuaderTransformAxis::X, quader::QuaderMeshAlignment::Center);
  ASSERT_TRUE(exact.ok()) << exact.error_message();
  EXPECT_NEAR(center_x(bounds(harness, other.entity)),
              center_x(bounds(harness, parent.entity)), kEpsilon);

  const auto history_count = harness.session.history().undo_count();
  const quader::QuaderApiVoidResult ignored_options =
      harness.editor.modeling().align_selected_meshes(
          quader::QuaderTransformAxis::X, quader::QuaderMeshAlignment::Center,
          {.snap_to_grid = true, .grid_size = 0.0});
  ASSERT_TRUE(ignored_options.ok()) << ignored_options.error_message();
  EXPECT_FALSE(ignored_options.changed());
  EXPECT_EQ(harness.session.history().undo_count(), history_count);
}

TEST(MeshArrangementTests,
     CompatibilityCommandsStayHiddenAndUseExactAlignmentRegardlessOfGrid) {
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

  const auto keybinding_catalog = harness.editor.commands().keybinding_catalog();
  for (const quader::QuaderMeshArrangementDescriptor &descriptor :
       quader::quader_mesh_arrangement_catalog()) {
    EXPECT_FALSE(harness.editor.commands().descriptor(descriptor.command_id).has_value());
    EXPECT_TRUE(std::none_of(keybinding_catalog.begin(), keybinding_catalog.end(),
                            [&](const auto &entry) {
      return entry.target_id == descriptor.command_id.string() ||
             std::find(entry.target_ids.begin(), entry.target_ids.end(),
                       descriptor.command_id.string()) != entry.target_ids.end();
    }));
    EXPECT_TRUE(
        harness.editor.commands().shortcut_for(descriptor.command_id).empty());
  }
  const quader::QuaderApiVoidResult result = harness.editor.commands().execute(
      quader::quader_command::kModelingAlignXCenter);
  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_NEAR(center_x(bounds(harness, active.entity)), 0.15f, kEpsilon);
  EXPECT_NEAR(center_x(bounds(harness, other.entity)), 0.15f, kEpsilon);

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

  ASSERT_TRUE(harness.editor.mode().set(quader::QuaderEditorMode::Edit).ok());
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

TEST(MeshArrangementTests,
     AlignsMixedMeshAndInstanceSelectionToTheSelectionBounds) {
  ArrangementHarness harness;
  const auto source = create_box(
      harness, "Instance Source",
      {.min = {-2.0f, -1.0f, -0.5f}, .max = {2.0f, 1.0f, 0.5f}});
  const quader::EntityId occurrence = create_instance(
      harness, source, {.position = {8.0f, 3.0f, 5.0f},
                        .rotation_euler = {0.1f, 0.4f, -0.2f},
                        .scale = {1.5f, 0.75f, 2.0f}});
  const auto other = create_box(
      harness, "Ordinary Mesh",
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      {.position = {-6.0f, -4.0f, 2.0f}});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({occurrence, other.entity}, occurrence).ok());
  const auto source_before =
      harness.editor.query().compiled_object_mesh(source);
  ASSERT_TRUE(source_before.has_value());
  const auto source_transform = *harness.session.scene().transform(source.entity);
  const auto occurrence_transform =
      *harness.session.scene().transform(occurrence);
  const auto occurrence_component =
      *harness.session.scene().instance_occurrence(occurrence);
  const float reference = std::max(bounds(harness, occurrence).max.x,
                                   bounds(harness, other.entity).max.x);
  harness.session.history().clear();

  EXPECT_TRUE(harness.editor.modeling().can_align_selected_meshes());
  const auto command = harness.editor.commands().descriptor(
      quader::quader_command::kModelingAlignXMax);
  EXPECT_FALSE(command.has_value());
  const auto result = harness.editor.modeling().align_selected_meshes(
      quader::QuaderTransformAxis::X, quader::QuaderMeshAlignment::Maximum);
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.changed());
  EXPECT_NEAR(bounds(harness, other.entity).max.x,
              bounds(harness, occurrence).max.x, kEpsilon);
  EXPECT_NEAR(bounds(harness, occurrence).max.x, reference, kEpsilon);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  EXPECT_EQ(harness.session.scene().selected_entities().size(), 2u);
  EXPECT_EQ(harness.session.scene().primary_selection(), occurrence);
  expect_transform_near(*harness.session.scene().transform(occurrence),
                        occurrence_transform);
  expect_transform_near(*harness.session.scene().transform(source.entity),
                        source_transform);
  EXPECT_EQ(*harness.session.scene().instance_occurrence(occurrence),
            occurrence_component);
  const auto source_after = harness.editor.query().compiled_object_mesh(source);
  ASSERT_TRUE(source_after.has_value());
  expect_mesh_unchanged(*source_after, *source_before);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_NEAR(center_x(bounds(harness, other.entity)), -6.0f, kEpsilon);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  EXPECT_NEAR(bounds(harness, other.entity).max.x,
              bounds(harness, occurrence).max.x, kEpsilon);
}

TEST(MeshArrangementTests,
     DistributesInstanceGeometryGapsWithoutChangingSharedSources) {
  ArrangementHarness harness;
  const std::array<float, 3> widths{2.0f, 4.0f, 6.0f};
  const std::array<float, 3> centers{0.0f, 4.0f, 18.0f};
  std::array<quader::QuaderModelingObjectRef, 3> sources;
  std::array<quader::EntityId, 3> occurrences;
  std::array<quader::QuaderMeshAsset, 3> meshes;
  std::array<quader::TransformComponent, 3> transforms;
  std::array<quader::InstanceOccurrenceComponent, 3> components;
  const auto parent = harness.editor.scene().create_entity(
      {.name = "Compensated Parent", .kind = quader::QuaderEntityKind::Empty,
       .transform = {.position = {3.0f, -2.0f, 4.0f},
                     .rotation_euler = {0.25f, -0.4f, 0.3f},
                     .scale = {1.5f, 0.8f, 2.0f}}});
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  for (std::size_t index = 0; index < sources.size(); ++index) {
    sources[index] = create_box(
        harness, "Distribution Source",
        {.min = {-widths[index] * 0.5f, -1.0f, -1.0f},
         .max = {widths[index] * 0.5f, 1.0f, 1.0f}});
    occurrences[index] = create_instance(
        harness, sources[index], {.position = {centers[index], 0.0f, 0.0f}});
    if (index == 1u) {
      ASSERT_TRUE(harness.editor.scene()
                      .reparent(occurrences[index], parent.value().id, true).ok());
    }
    const auto mesh = harness.editor.query().compiled_object_mesh(sources[index]);
    ASSERT_TRUE(mesh.has_value());
    meshes[index] = *mesh;
    transforms[index] = *harness.session.scene().transform(occurrences[index]);
    components[index] =
        *harness.session.scene().instance_occurrence(occurrences[index]);
  }
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({occurrences[0], occurrences[1], occurrences[2]},
                                  occurrences[1]).ok());
  const auto asset_revision = harness.session.assets().revision();
  harness.session.history().clear();
  EXPECT_TRUE(harness.editor.modeling().can_distribute_selected_meshes());
  const auto result = harness.editor.modeling().distribute_selected_meshes(
      quader::QuaderTransformAxis::X, quader::QuaderMeshDistribution::Gaps);
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.changed());
  EXPECT_NEAR(center_x(bounds(harness, occurrences[1])), 8.0f, kEpsilon);
  EXPECT_NEAR(bounds(harness, occurrences[1]).min.x -
                  bounds(harness, occurrences[0]).max.x, 5.0f, kEpsilon);
  EXPECT_NEAR(bounds(harness, occurrences[2]).min.x -
                  bounds(harness, occurrences[1]).max.x, 5.0f, kEpsilon);
  EXPECT_EQ(harness.session.assets().revision(), asset_revision);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  for (std::size_t index = 0; index < sources.size(); ++index) {
    const auto after = harness.editor.query().compiled_object_mesh(sources[index]);
    ASSERT_TRUE(after.has_value());
    expect_mesh_unchanged(*after, meshes[index]);
    EXPECT_EQ(*harness.session.scene().instance_occurrence(occurrences[index]),
              components[index]);
  }
  expect_transform_near(*harness.session.scene().transform(occurrences.front()),
                        transforms.front());
  expect_transform_near(*harness.session.scene().transform(occurrences.back()),
                        transforms.back());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  for (std::size_t index = 0; index < occurrences.size(); ++index) {
    expect_transform_near(*harness.session.scene().transform(occurrences[index]),
                          transforms[index]);
  }
  ASSERT_TRUE(harness.editor.history().redo().ok());
  EXPECT_NEAR(center_x(bounds(harness, occurrences[1])), 8.0f, kEpsilon);
}

TEST(MeshArrangementTests,
     CommandsArrangeLightAndEmptyOriginsAndPreserveLightProperties) {
  ArrangementHarness harness;
  const auto point = harness.editor.scene().create_point_light(
      {.name = "Point", .transform = {.position = {0.0f, 2.0f, 4.0f}}});
  const auto directional = harness.editor.scene().create_directional_light(
      {.name = "Directional", .transform = {.position = {2.0f, 8.0f, 9.0f}}});
  const auto empty = harness.editor.scene().create_entity(
      {.name = "Empty", .kind = quader::QuaderEntityKind::Empty,
       .transform = {.position = {12.0f, -3.0f, 7.0f}}});
  ASSERT_TRUE(point.ok()) << point.error_message();
  ASSERT_TRUE(directional.ok()) << directional.error_message();
  ASSERT_TRUE(empty.ok()) << empty.error_message();
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({point.value().id, directional.value().id,
                                   empty.value().id}, empty.value().id).ok());
  const auto point_before = *harness.session.scene().light(point.value().id);
  const auto directional_before =
      *harness.session.scene().light(directional.value().id);
  harness.session.history().clear();
  const auto command = harness.editor.commands().descriptor(
      quader::quader_command::kModelingDistributeCentersX);
  EXPECT_FALSE(command.has_value());
  const auto distributed = harness.editor.commands().execute(
      quader::quader_command::kModelingDistributeCentersX);
  ASSERT_TRUE(distributed.ok()) << distributed.error_message();
  ASSERT_TRUE(distributed.changed());
  EXPECT_NEAR(center_x(bounds(harness, directional.value().id)), 6.0f, kEpsilon);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_NEAR(center_x(bounds(harness, directional.value().id)), 2.0f, kEpsilon);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  EXPECT_NEAR(center_x(bounds(harness, directional.value().id)), 6.0f, kEpsilon);

  const auto aligned = harness.editor.commands().execute(
      quader::quader_command::kModelingAlignYCenter);
  ASSERT_TRUE(aligned.ok()) << aligned.error_message();
  ASSERT_TRUE(aligned.changed());
  EXPECT_NEAR(quader::bounds_center(bounds(harness, point.value().id)).y,
              2.5f, kEpsilon);
  EXPECT_NEAR(quader::bounds_center(bounds(harness, directional.value().id)).y,
              2.5f, kEpsilon);
  EXPECT_NEAR(quader::bounds_center(bounds(harness, empty.value().id)).y,
              2.5f, kEpsilon);
  EXPECT_NEAR(center_x(bounds(harness, directional.value().id)), 6.0f, kEpsilon);
  EXPECT_EQ(*harness.session.scene().light(point.value().id), point_before);
  EXPECT_EQ(*harness.session.scene().light(directional.value().id),
            directional_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), empty.value().id);
}

TEST(MeshArrangementTests,
     SpatialRootPromotionMovesSelectedChildrenOnlyThroughTheirParent) {
  ArrangementHarness harness;
  const auto parent = harness.editor.scene().create_entity(
      {.name = "Spatial Parent", .kind = quader::QuaderEntityKind::Empty,
       .transform = {.position = {1.0f, 2.0f, 3.0f},
                     .rotation_euler = {0.2f, -0.3f, 0.4f},
                     .scale = {1.5f, 0.8f, 2.0f}}});
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  const auto child = harness.editor.scene().create_point_light(
      {.name = "Child", .parent = parent.value().id,
       .transform = {.position = {2.0f, 5.0f, 1.0f}}});
  const auto active = harness.editor.scene().create_point_light(
      {.name = "Reference", .transform = {.position = {8.0f, -4.0f, 2.0f}}});
  ASSERT_TRUE(child.ok()) << child.error_message();
  ASSERT_TRUE(active.ok()) << active.error_message();
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({parent.value().id, child.value().id,
                                   active.value().id}, active.value().id).ok());
  const auto child_transform = *harness.session.scene().transform(child.value().id);
  const auto child_before = quader::bounds_center(bounds(harness, child.value().id));
  const auto parent_before = quader::bounds_center(bounds(harness, parent.value().id));
  harness.session.history().clear();
  const auto aligned = harness.editor.modeling().align_selected_meshes(
      quader::QuaderTransformAxis::X, quader::QuaderMeshAlignment::Center);
  ASSERT_TRUE(aligned.ok()) << aligned.error_message();
  ASSERT_TRUE(aligned.changed());
  const auto parent_after = quader::bounds_center(bounds(harness, parent.value().id));
  const auto child_after = quader::bounds_center(bounds(harness, child.value().id));
  const float reference = (child_before.x + 8.0f) * 0.5f;
  EXPECT_NEAR(child_after.x, reference, kEpsilon);
  EXPECT_NEAR(parent_after.x - parent_before.x,
              reference - child_before.x, kEpsilon);
  EXPECT_NEAR(child_after.x - child_before.x,
              parent_after.x - parent_before.x, kEpsilon);
  EXPECT_NEAR(child_after.y, child_before.y, kEpsilon);
  EXPECT_NEAR(child_after.z, child_before.z, kEpsilon);
  expect_transform_near(*harness.session.scene().transform(child.value().id),
                        child_transform);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_NEAR(center_x(bounds(harness, parent.value().id)), parent_before.x, kEpsilon);
}

TEST(MeshArrangementTests,
     ContainerBoundsIncludeLightsAlongsideGeometryAndExcludeGroupPivots) {
  ArrangementHarness harness;
  const auto light_group = harness.editor.scene().create_entity(
      {.name = "Light Group", .kind = quader::QuaderEntityKind::Empty});
  const auto mixed_group = harness.editor.scene().create_entity(
      {.name = "Mixed Group", .kind = quader::QuaderEntityKind::Empty});
  ASSERT_TRUE(light_group.ok()) << light_group.error_message();
  ASSERT_TRUE(mixed_group.ok()) << mixed_group.error_message();
  const auto first = harness.editor.scene().create_point_light(
      {.name = "First", .parent = light_group.value().id,
       .transform = {.position = {5.0f, 0.0f, 0.0f}}});
  const auto second = harness.editor.scene().create_point_light(
      {.name = "Second", .parent = light_group.value().id,
       .transform = {.position = {15.0f, 0.0f, 0.0f}}});
  const auto mixed_light = harness.editor.scene().create_point_light(
      {.name = "Mixed Light", .parent = mixed_group.value().id,
       .transform = {.position = {15.0f, 0.0f, 0.0f}}});
  const auto active = harness.editor.scene().create_point_light(
      {.name = "Reference", .transform = {.position = {20.0f, 0.0f, 0.0f}}});
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(mixed_light.ok()) << mixed_light.error_message();
  ASSERT_TRUE(active.ok()) << active.error_message();
  const auto mesh = create_box(
      harness, "Mixed Mesh",
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      {.position = {5.0f, 0.0f, 0.0f}});
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(mesh.entity, mixed_group.value().id, true).ok());
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({light_group.value().id, mixed_group.value().id,
                                   active.value().id}, active.value().id).ok());
  harness.session.history().clear();
  const auto aligned = harness.editor.modeling().align_selected_meshes(
      quader::QuaderTransformAxis::X, quader::QuaderMeshAlignment::Center);
  ASSERT_TRUE(aligned.ok()) << aligned.error_message();
  ASSERT_TRUE(aligned.changed());
  EXPECT_NEAR(center_x(bounds(harness, first.value().id)), 7.0f, kEpsilon);
  EXPECT_NEAR(center_x(bounds(harness, second.value().id)), 17.0f, kEpsilon);
  EXPECT_NEAR(bounds(harness, mesh.entity).min.x, 6.5f, kEpsilon);
  EXPECT_NEAR(center_x(bounds(harness, mixed_light.value().id)), 17.5f, kEpsilon);
  EXPECT_NEAR(center_x(bounds(harness, active.value().id)), 12.0f, kEpsilon);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_NEAR(center_x(bounds(harness, first.value().id)), 5.0f, kEpsilon);
  EXPECT_NEAR(center_x(bounds(harness, mesh.entity)), 5.0f, kEpsilon);
}

TEST(MeshArrangementTests,
     InvalidMixedSelectionsRejectAtomicallyAndRefreshAvailability) {
  ArrangementHarness harness;
  const auto source = create_box(
      harness, "Locked Instance Source",
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}});
  const auto occurrence = create_instance(
      harness, source, {.position = {4.0f, 0.0f, 0.0f}});
  const auto light = harness.editor.scene().create_point_light(
      {.name = "Locked Light", .transform = {.position = {8.0f, 0.0f, 0.0f}}});
  const auto mesh = create_box(
      harness, "Ordinary",
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}});
  ASSERT_TRUE(light.ok()) << light.error_message();
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({occurrence, light.value().id, mesh.entity},
                                  occurrence).ok());
  EXPECT_TRUE(harness.editor.modeling().can_align_selected_meshes());
  EXPECT_TRUE(harness.editor.modeling().can_distribute_selected_meshes());
  const auto instance_before = *harness.session.scene().transform(occurrence);
  const auto mesh_before = *harness.session.scene().transform(mesh.entity);
  const auto light_before = *harness.session.scene().transform(light.value().id);
  harness.session.history().clear();
  ASSERT_TRUE(harness.session.scene().set_locked(light.value().id, true));
  EXPECT_FALSE(harness.editor.modeling().can_align_selected_meshes());
  EXPECT_FALSE(harness.editor.modeling().can_distribute_selected_meshes());
  const auto locked = harness.editor.modeling().align_selected_meshes(
      quader::QuaderTransformAxis::X, quader::QuaderMeshAlignment::Center);
  EXPECT_FALSE(locked.ok());
  ASSERT_TRUE(harness.session.scene().set_locked(light.value().id, false));
  EXPECT_TRUE(harness.editor.modeling().can_distribute_selected_meshes());
  ASSERT_TRUE(harness.session.scene().set_visible(occurrence, false));
  EXPECT_FALSE(harness.editor.modeling().can_distribute_selected_meshes());
  const auto hidden = harness.editor.modeling().distribute_selected_meshes(
      quader::QuaderTransformAxis::X, quader::QuaderMeshDistribution::Centers);
  EXPECT_FALSE(hidden.ok());
  expect_transform_near(*harness.session.scene().transform(occurrence), instance_before);
  expect_transform_near(*harness.session.scene().transform(mesh.entity), mesh_before);
  expect_transform_near(*harness.session.scene().transform(light.value().id), light_before);
  EXPECT_EQ(harness.session.history().undo_count(), 0u);
}

} // namespace
