////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "render/overlay/collision_shape_overlay_cache.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/render_scene_snapshot.hpp"
#include "scene/scene_document.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

using namespace quader;

namespace {

MeshAsset collision_overlay_triangle() {
  MeshAsset mesh;
  mesh.name = "Collision Overlay Triangle";
  auto &geometry = mesh.edit_geometry();
  geometry.vertices = {
      MeshVertex{.position = {0.0f, 0.0f, 0.0f}},
      MeshVertex{.position = {1.0f, 0.0f, 0.0f}},
      MeshVertex{.position = {0.0f, 1.0f, 0.0f}},
  };
  geometry.indices = {0u, 1u, 2u};
  geometry.primitives = {{.index_offset = 0u, .index_count = 3u}};
  return mesh;
}

/** Owns one visible mesh and the retained extraction boundary under test. */
class CollisionShapeOverlayTests : public testing::Test {
protected:
  void SetUp() override {
    mesh_id_ = assets_.add_mesh(collision_overlay_triangle());
    entity_ = document_.create_entity("Collision Mesh", EntityKind::Mesh);
    component_.mesh = mesh_id_;
    component_.collision_type = MeshCollisionType::Mesh;
    component_.display_collision = true;
    ASSERT_TRUE(document_.set_mesh(entity_, component_));
  }

  RenderOverlayPersistentSnapshotUpdate update(
      bool visible = true,
      ViewportSettingColor color = {0.0f, 0.6f, 0.7f, 0.42f},
      const ViewportLocalVisibilitySnapshot *local = nullptr) {
    return cache_.update_persistent(document_, assets_, false, 0u, {}, {},
                                    local, false, {}, {}, false, visible, color);
  }

  SceneDocument document_;
  AssetRegistry assets_;
  RenderOverlaySnapshotCache cache_;
  AssetId mesh_id_ = kInvalidAssetId;
  EntityId entity_ = kInvalidEntityId;
  MeshComponent component_;
};

TEST_F(CollisionShapeOverlayTests, SelectionChangesStyleWithoutRebuildingCollisionGeometry) {
  const auto first = update();
  ASSERT_NE(first.collision, nullptr);
  ASSERT_EQ(first.collision->meshes.size(), 1u);
  EXPECT_TRUE(first.collision_changed);
  const auto *placements = first.collision->meshes.data();
  const auto payload = first.collision->meshes.front().collision_payload;
  for (int frame = 0; frame < 5; ++frame) {
    EXPECT_FALSE(update().collision_changed);
  }
  ASSERT_TRUE(document_.set_selected(entity_, true));
  const auto selected = update();
  EXPECT_TRUE(selected.collision_changed);
  EXPECT_TRUE(selected.collision->meshes.front().selected);
  EXPECT_EQ(selected.collision->meshes.front().collision_payload, payload);
  ASSERT_TRUE(document_.set_selected(entity_, false));
  const auto deselected = update();
  EXPECT_TRUE(deselected.collision_changed);
  EXPECT_FALSE(deselected.collision->meshes.front().selected);
  EXPECT_EQ(deselected.collision->meshes.data(), placements);
  EXPECT_EQ(deselected.collision->meshes.front().collision_payload, payload);
  EXPECT_EQ(cache_.stats().collision_build_count, 1u);
  EXPECT_EQ(cache_.stats().collision_transform_refresh_count, 0u);
}

TEST_F(CollisionShapeOverlayTests, GodotColorsKeepFacesFaintAndSelectedWiresBrighter) {
  const ViewportSettingColor color{0.0f, 0.6f, 0.7f, 0.42f};
  const auto fill = collision_shape_fill_color(color);
  const auto wire = collision_shape_wire_color(color, false);
  const auto selected_wire = collision_shape_wire_color(color, true);
  for (const auto resolved : {fill, wire, selected_wire}) {
    EXPECT_FLOAT_EQ(resolved.r, color.r);
    EXPECT_FLOAT_EQ(resolved.g, color.g);
    EXPECT_FLOAT_EQ(resolved.b, color.b);
  }
  EXPECT_FLOAT_EQ(fill.a, 0.02625f);
  EXPECT_FLOAT_EQ(wire.a, 0.168f);
  EXPECT_FLOAT_EQ(selected_wire.a, 0.672f);
  EXPECT_FLOAT_EQ(collision_shape_fill_color({0.2f, 0.3f, 0.4f, 0.0f}).a, 0.0f);
  EXPECT_FLOAT_EQ(collision_shape_wire_color({0.2f, 0.3f, 0.4f, 0.0f}, true).a, 0.0f);
}

TEST_F(CollisionShapeOverlayTests, CollisionWiresDeduplicateEdgesAndKeepTriangulation) {
  const QVec3 a{0.0f, 0.0f, 0.0f};
  const QVec3 b{1.0f, 0.0f, 0.0f};
  const QVec3 c{1.0f, 1.0f, 0.0f};
  const QVec3 d{0.0f, 1.0f, 0.0f};
  MeshCollisionGeometryPayload geometry;
  geometry.faces = {a, b, c, a, c, d, c, b, a};
  const auto wires = build_collision_shape_wire_geometry(geometry);
  ASSERT_NE(wires, nullptr);
  ASSERT_EQ(wires->segments.size(), 5u);
  const auto same_point = [](QVec3 left, QVec3 right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
  };
  for (const MeshWireSegment expected :
       {MeshWireSegment{a, b}, {b, c}, {c, d}, {d, a}, {a, c}}) {
    EXPECT_EQ(std::count_if(wires->segments.begin(), wires->segments.end(),
                           [&](const MeshWireSegment &edge) {
      return (same_point(edge.start, expected.start) &&
              same_point(edge.end, expected.end)) ||
             (same_point(edge.start, expected.end) &&
              same_point(edge.end, expected.start));
    }), 1);
  }

  // Wire extraction consumes the collision positions without another snap.
  const QVec3 near_a{0.00001f, 0.0f, 0.0f};
  geometry.faces.insert(geometry.faces.end(), {a, c, near_a});
  const auto close_wires = build_collision_shape_wire_geometry(geometry);
  ASSERT_NE(close_wires, nullptr);
  EXPECT_EQ(close_wires->segments.size(), 7u);
  EXPECT_TRUE(std::any_of(close_wires->segments.begin(), close_wires->segments.end(),
                         [&](const MeshWireSegment &edge) {
    return same_point(edge.start, near_a) || same_point(edge.end, near_a);
  }));
  const auto empty = build_collision_shape_wire_geometry({});
  EXPECT_TRUE(empty == nullptr || empty->segments.empty());
}

TEST_F(CollisionShapeOverlayTests, ColorAndTransformsReusePinnedGeometry) {
  const auto initial = update();
  ASSERT_EQ(initial.collision->meshes.size(), 1u);
  const auto payload = initial.collision->meshes.front().collision_payload;
  const auto *placements = initial.collision->meshes.data();
  const ViewportSettingColor color{0.8f, 0.2f, 0.1f, 0.73f};
  const auto recolored = update(true, color);
  EXPECT_TRUE(recolored.collision_changed);
  EXPECT_EQ(recolored.collision->meshes.data(), placements);
  EXPECT_FLOAT_EQ(recolored.collision->color.a, 0.73f);
  EXPECT_EQ(cache_.stats().collision_build_count, 1u);
  ASSERT_TRUE(document_.set_transform(
      entity_, TransformComponent{.position = {5.0f, 2.0f, -3.0f}}));
  const auto moved = update(true, color);
  ASSERT_EQ(moved.collision->meshes.size(), 1u);
  EXPECT_TRUE(moved.collision_changed);
  EXPECT_EQ(moved.collision->meshes.data(), placements);
  EXPECT_EQ(moved.collision->meshes.front().collision_payload, payload);
  EXPECT_FLOAT_EQ(moved.collision->meshes.front().world_transform.values[12], 5.0f);
  EXPECT_EQ(cache_.stats().collision_build_count, 1u);
  EXPECT_EQ(cache_.stats().collision_transform_refresh_count, 1u);

  ASSERT_TRUE(assets_.update_mesh(mesh_id_, [](MeshAsset &mesh) {
    mesh.edit_geometry().vertices[1].position.x = 2.0f;
  }));
  const auto edited = update(true, color);
  ASSERT_EQ(edited.collision->meshes.size(), 1u);
  EXPECT_NE(edited.collision->meshes.front().collision_payload, payload);
  EXPECT_EQ(cache_.stats().collision_build_count, 2u);
  EXPECT_FLOAT_EQ(payload->faces[1].x, 1.0f);
  EXPECT_FLOAT_EQ(edited.collision->meshes.front().collision_payload->faces[1].x, 2.0f);
}

TEST_F(CollisionShapeOverlayTests, EligibilityRespectsBothFlagsAndSupportedTypes) {
  ASSERT_EQ(update().collision->meshes.size(), 1u);
  component_.generate_collisions = false;
  ASSERT_TRUE(document_.set_mesh(entity_, component_));
  EXPECT_TRUE(update().collision->meshes.empty());
  component_.generate_collisions = true;
  component_.display_collision = false;
  ASSERT_TRUE(document_.set_mesh(entity_, component_));
  EXPECT_TRUE(update().collision->meshes.empty());
  component_.display_collision = true;
  component_.collision_type = MeshCollisionType::Box;
  ASSERT_TRUE(document_.set_mesh(entity_, component_));
  EXPECT_TRUE(update().collision->meshes.empty());
  EXPECT_EQ(document_.mesh(entity_)->collision_type, MeshCollisionType::Box);
  component_.collision_type = MeshCollisionType::Mesh;
  component_.body_type = MeshPhysicsBodyType::RigidBody;
  ASSERT_TRUE(document_.set_mesh(entity_, component_));
  EXPECT_TRUE(update().collision->meshes.empty());
  component_.body_type = MeshPhysicsBodyType::StaticBody;
  ASSERT_TRUE(document_.set_mesh(entity_, component_));
  ASSERT_EQ(update().collision->meshes.size(), 1u);
  EXPECT_TRUE(update(false).collision->meshes.empty());
  ASSERT_EQ(update().collision->meshes.size(), 1u);
}

TEST_F(CollisionShapeOverlayTests, VisibilityUsesHierarchyLayerAndLocalFilter) {
  const auto layer = document_.create_layer("Collision Layer", {1.0f, 0.0f, 0.0f});
  ASSERT_TRUE(document_.set_entity_layer(entity_, layer));
  ASSERT_TRUE(document_.set_layer_visible(layer, false));
  EXPECT_TRUE(update().collision->meshes.empty());
  ASSERT_TRUE(document_.set_layer_visible(layer, true));
  ASSERT_EQ(update().collision->meshes.size(), 1u);
  const EntityId parent = document_.create_entity("Parent", EntityKind::Empty);
  ASSERT_TRUE(document_.set_parent(entity_, parent, false));
  ASSERT_TRUE(document_.set_visible(parent, false));
  EXPECT_TRUE(update().collision->meshes.empty());
  ASSERT_TRUE(document_.set_visible(parent, true));
  ASSERT_EQ(update().collision->meshes.size(), 1u);
  const EntityId other = document_.create_entity("Other", EntityKind::Empty);
  ViewportWorkspaceService workspace;
  const std::array<ViewportLocalVisibilityEntityId, 2> baseline{entity_, other};
  const std::array<ViewportLocalVisibilityEntityId, 1> members{other};
  ASSERT_TRUE(workspace.enter_local_visibility(baseline, members));
  auto local = workspace.local_visibility_snapshot();
  EXPECT_TRUE(update(true, {}, &local).collision->meshes.empty());
  ASSERT_TRUE(workspace.exit_local_visibility());
  local = workspace.local_visibility_snapshot();
  EXPECT_EQ(update(true, {}, &local).collision->meshes.size(), 1u);
}

TEST_F(CollisionShapeOverlayTests, FormalOccurrencesSharePayloadAndSuppressPrecisely) {
  const EntityId definition = document_.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document_.set_instance_definition(definition));
  ASSERT_TRUE(document_.set_parent(entity_, definition, false));
  ASSERT_TRUE(document_.set_visible(definition, false));
  const EntityId first = document_.create_entity("First", EntityKind::Instance);
  const EntityId second = document_.create_entity("Second", EntityKind::Instance);
  ASSERT_TRUE(document_.set_instance_occurrence(first, {.definition = definition}));
  ASSERT_TRUE(document_.set_instance_occurrence(second, {.definition = definition}));
  const RenderOverlayCollisionSnapshot snapshot = *update().collision;
  ASSERT_EQ(snapshot.meshes.size(), 2u);
  EXPECT_EQ(snapshot.meshes[0].collision_payload, snapshot.meshes[1].collision_payload);
  EXPECT_EQ(snapshot.meshes[0].member, entity_);
  EXPECT_EQ(snapshot.meshes[0].definition, definition);
  RenderToolPreviewSuppressionSnapshot suppression;
  suppression.occurrences.push_back({.source_entity = entity_, .occurrence = first});
  suppression.signature = 42u;
  const auto filtered = filter_render_overlay_collision_snapshot(snapshot, suppression);
  ASSERT_EQ(filtered.meshes.size(), 1u);
  EXPECT_EQ(filtered.meshes.front().occurrence, second);
  EXPECT_NE(filtered.local_visibility_revision, snapshot.local_visibility_revision);
  // Canceling a preview restores both placements and the original revision.
  const auto restored = filter_render_overlay_collision_snapshot(snapshot, {});
  EXPECT_EQ(restored.meshes.size(), 2u);
  EXPECT_EQ(restored.local_visibility_revision, snapshot.local_visibility_revision);
  suppression.entities = {definition};
  EXPECT_TRUE(filter_render_overlay_collision_snapshot(snapshot, suppression).meshes.empty());
}

TEST_F(CollisionShapeOverlayTests, FillPolicyPreservesOcclusionAndEditingFeedbackOrder) {
  const auto state = OverlayRenderableBuilder::make_state(
      ViewportOverlayLayer::CollisionFill, ViewportOverlayGeometryKind::Triangles, 0u);
  EXPECT_TRUE(state.depth_culling);
  EXPECT_FALSE(state.depth_write);
  EXPECT_EQ(state.depth_func, OverlayDepthFunc::GreaterOrEqual);
  EXPECT_EQ(state.culling_mode, OverlayCullingMode::Back);
  EXPECT_EQ(state.transparency_mode, OverlayTransparencyMode::Default);
  EXPECT_FLOAT_EQ(state.polygon_offset_scale, 0.0f);
  EXPECT_FLOAT_EQ(state.polygon_offset_constant, 0.0f);
  const auto wire_order = static_cast<std::uint16_t>(
      viewport_overlay_draw_order(ViewportOverlayLayer::CollisionFill) * 64u + 1u);
  const auto wire_state = OverlayRenderableBuilder::make_state(
      ViewportOverlayLayer::CollisionFill, ViewportOverlayGeometryKind::Lines,
      wire_order);
  EXPECT_TRUE(wire_state.depth_culling);
  EXPECT_FALSE(wire_state.depth_write);
  EXPECT_EQ(wire_state.depth_func, OverlayDepthFunc::GreaterOrEqual);
  EXPECT_FLOAT_EQ(wire_state.polygon_offset_scale, 0.0f);
  EXPECT_FLOAT_EQ(wire_state.polygon_offset_constant, 0.0f);
  EXPECT_LT(wire_state.blend_order,
            viewport_overlay_draw_order(ViewportOverlayLayer::SelectedVisibleFace) * 64u);
  for (const auto layer : kViewportOverlayDrawOrder) {
    if (layer != ViewportOverlayLayer::CollisionFill) {
      EXPECT_LT(viewport_overlay_draw_order(ViewportOverlayLayer::CollisionFill),
                viewport_overlay_draw_order(layer));
    }
  }
}

TEST_F(CollisionShapeOverlayTests, GpuPlannerGatesUniformsAndRetainsLocalBounds) {
  RenderOverlayCollisionSnapshot snapshot = *update().collision;
  CollisionShapeOverlayPlanner planner;
  EXPECT_EQ(planner.prepare(snapshot, 8u).batches,
            RenderInstanceBatchUpdateKind::Rebuild);
  ASSERT_EQ(planner.plan().chunks.size(), 1u);
  EXPECT_EQ(planner.stats().prepare_count, 1u);
  ASSERT_EQ(planner.wire_plan().chunks.size(), 1u);
  const auto wire_key = planner.wire_plan().chunks.front().key.geometry_payload;
  const auto wires = planner.wire_geometry(wire_key);
  ASSERT_NE(wires, nullptr);
  EXPECT_EQ(wires->segments.size(), 3u);
  EXPECT_EQ(planner.stats().wire_geometry_build_count, 1u);
  const auto vertex_visits = planner.stats().local_bounds_vertex_visit_count;
  EXPECT_EQ(vertex_visits, 3u);
  const auto wire_preparations = planner.stats().wire_prepare_count;
  for (int frame = 0; frame < 5; ++frame) {
    EXPECT_EQ(planner.prepare(snapshot, 8u).wire_batches,
              RenderInstanceBatchUpdateKind::Unchanged);
  }
  EXPECT_EQ(planner.stats().wire_prepare_count, wire_preparations);
  ++snapshot.selection_revision;
  ++snapshot.scene_revision;
  snapshot.meshes.front().selected = true;
  const auto selected = planner.prepare(snapshot, 8u);
  EXPECT_EQ(selected.batches, RenderInstanceBatchUpdateKind::Unchanged);
  EXPECT_EQ(selected.wire_batches, RenderInstanceBatchUpdateKind::Rebuild);
  EXPECT_EQ(planner.wire_geometry(wire_key), wires);
  EXPECT_EQ(planner.stats().wire_geometry_build_count, 1u);
  const auto selected_wire_preparations = planner.stats().wire_prepare_count;
  snapshot.color.a = 0.19f;
  const auto recolored = planner.prepare(snapshot, 8u);
  EXPECT_TRUE(recolored.color_changed);
  EXPECT_EQ(recolored.batches, RenderInstanceBatchUpdateKind::Unchanged);
  EXPECT_EQ(recolored.wire_batches, RenderInstanceBatchUpdateKind::Unchanged);
  EXPECT_EQ(planner.stats().wire_prepare_count, selected_wire_preparations);
  EXPECT_EQ(planner.stats().prepare_count, 1u);
  EXPECT_EQ(planner.stats().source_record_visit_count, 1u);
  ++snapshot.transform_revision;
  snapshot.meshes.front().world_transform.values[12] = 9.0f;
  const auto moved = planner.prepare(snapshot, 8u);
  EXPECT_EQ(moved.batches, RenderInstanceBatchUpdateKind::TransformsOnly);
  EXPECT_EQ(moved.wire_batches, RenderInstanceBatchUpdateKind::TransformsOnly);
  EXPECT_EQ(planner.stats().local_bounds_vertex_visit_count, vertex_visits);
  EXPECT_FLOAT_EQ(planner.plan().chunks.front().world_transforms.front().values[12], 9.0f);
  EXPECT_FLOAT_EQ(planner.wire_plan().chunks.front().world_transforms.front().values[12], 9.0f);
  EXPECT_EQ(planner.wire_geometry(wire_key), wires);
  EXPECT_EQ(planner.stats().wire_geometry_build_count, 1u);
  snapshot.visible = false;
  const auto hidden = planner.prepare(snapshot, 8u);
  EXPECT_TRUE(hidden.visibility_changed);
  EXPECT_EQ(hidden.batches, RenderInstanceBatchUpdateKind::Unchanged);
  EXPECT_EQ(planner.plan().chunks.size(), 1u);
  EXPECT_EQ(planner.wire_plan().chunks.size(), 1u);
  EXPECT_EQ(planner.stats().local_bounds_vertex_visit_count, vertex_visits);
  EXPECT_EQ(planner.wire_geometry(wire_key), wires);
  EXPECT_EQ(planner.stats().wire_geometry_build_count, 1u);
  snapshot.visible = true;
  EXPECT_EQ(planner.prepare(snapshot, 8u).batches,
            RenderInstanceBatchUpdateKind::Unchanged);
  EXPECT_EQ(planner.stats().local_bounds_vertex_visit_count, vertex_visits);
}

TEST_F(CollisionShapeOverlayTests,
       MasterVisibilityPreservesMeshChoicesAndRetainedGeometry) {
  const EntityId hidden_entity =
      document_.create_entity("Individually Hidden Collision", EntityKind::Mesh);
  MeshComponent hidden_component = component_;
  hidden_component.display_collision = false;
  ASSERT_TRUE(document_.set_mesh(hidden_entity, hidden_component));
  const auto visible = update();
  ASSERT_EQ(visible.collision->meshes.size(), 1u);
  const auto payload = visible.collision->meshes.front().collision_payload;
  CollisionShapeOverlayPlanner planner;
  EXPECT_EQ(planner.prepare(*visible.collision, 8u).batches,
            RenderInstanceBatchUpdateKind::Rebuild);
  const auto geometry_key = planner.plan().chunks.front().key.geometry_payload;
  const auto vertex_visits = planner.stats().local_bounds_vertex_visit_count;
  const auto prepare_count = planner.stats().prepare_count;

  const auto disabled = update(false);
  EXPECT_FALSE(disabled.collision->visible);
  EXPECT_TRUE(disabled.collision->meshes.empty());
  EXPECT_EQ(planner.prepare(*disabled.collision, 8u).batches,
            RenderInstanceBatchUpdateKind::Unchanged);
  ASSERT_EQ(planner.plan().chunks.size(), 1u);
  EXPECT_EQ(planner.geometry(geometry_key), payload);
  EXPECT_EQ(planner.stats().prepare_count, prepare_count);
  EXPECT_FALSE(update(false).collision_changed);
  EXPECT_TRUE(document_.mesh(entity_)->display_collision);
  EXPECT_FALSE(document_.mesh(hidden_entity)->display_collision);

  const auto enabled = update();
  ASSERT_EQ(enabled.collision->meshes.size(), 1u);
  EXPECT_EQ(enabled.collision->meshes.front().entity, entity_);
  EXPECT_EQ(enabled.collision->meshes.front().collision_payload, payload);
  EXPECT_EQ(planner.prepare(*enabled.collision, 8u).batches,
            RenderInstanceBatchUpdateKind::Unchanged);
  EXPECT_EQ(planner.stats().local_bounds_vertex_visit_count, vertex_visits);
  EXPECT_FALSE(document_.mesh(hidden_entity)->display_collision);
}

TEST_F(CollisionShapeOverlayTests, GpuPlannerSharesIdenticalSoupAndSeparatesHashCollisions) {
  RenderOverlayCollisionSnapshot snapshot = *update().collision;
  const auto original = snapshot.meshes.front().collision_payload;
  auto duplicate = snapshot.meshes.front();
  duplicate.collision_payload =
      std::make_shared<MeshCollisionGeometryPayload>(*original);
  duplicate.entity = entity_ + 100u;
  duplicate.world_transform.values[12] = 3.0f;
  snapshot.meshes.push_back(duplicate);
  CollisionShapeOverlayPlanner planner;
  EXPECT_EQ(planner.prepare(snapshot, 8u).batches,
            RenderInstanceBatchUpdateKind::Rebuild);
  ASSERT_EQ(planner.plan().chunks.size(), 1u);
  EXPECT_EQ(planner.plan().chunks.front().world_transforms.size(), 2u);
  EXPECT_EQ(planner.stats().local_bounds_vertex_visit_count, 3u);
  ASSERT_EQ(planner.wire_plan().chunks.size(), 1u);
  EXPECT_EQ(planner.wire_plan().chunks.front().world_transforms.size(), 2u);
  EXPECT_EQ(planner.stats().wire_geometry_build_count, 1u);
  const auto shared_wires = planner.wire_geometry(
      planner.wire_plan().chunks.front().key.geometry_payload);
  snapshot.meshes.back().selected = true;
  ++snapshot.selection_revision;
  const auto selected = planner.prepare(snapshot, 8u);
  EXPECT_EQ(selected.batches, RenderInstanceBatchUpdateKind::Unchanged);
  EXPECT_EQ(selected.wire_batches, RenderInstanceBatchUpdateKind::Rebuild);
  ASSERT_EQ(planner.wire_plan().chunks.size(), 2u);
  for (const auto &chunk : planner.wire_plan().chunks) {
    EXPECT_EQ(planner.wire_geometry(chunk.key.geometry_payload), shared_wires);
  }
  EXPECT_EQ(planner.stats().wire_geometry_build_count, 1u);

  auto distinct = std::make_shared<MeshCollisionGeometryPayload>(*original);
  distinct->faces[1].x = 7.0f;
  ASSERT_EQ(distinct->content_id, original->content_id);
  snapshot.meshes.back().collision_payload = distinct;
  ++snapshot.mesh_material_usage_revision;
  EXPECT_EQ(planner.prepare(snapshot, 8u).batches,
            RenderInstanceBatchUpdateKind::Rebuild);
  ASSERT_EQ(planner.plan().chunks.size(), 2u);
  const auto first_key = planner.plan().chunks[0].key.geometry_payload;
  const auto second_key = planner.plan().chunks[1].key.geometry_payload;
  EXPECT_NE(first_key, second_key);
  ASSERT_NE(planner.geometry(first_key), nullptr);
  ASSERT_NE(planner.geometry(second_key), nullptr);
  EXPECT_NE(planner.geometry(first_key)->faces[1].x,
            planner.geometry(second_key)->faces[1].x);
  EXPECT_EQ(planner.stats().wire_geometry_build_count, 2u);
  EXPECT_NE(planner.wire_geometry(first_key), planner.wire_geometry(second_key));
  std::weak_ptr<const MeshCollisionGeometryPayload> removed_payload = distinct;
  snapshot.meshes.pop_back();
  distinct.reset();
  ++snapshot.mesh_binding_revision;
  EXPECT_EQ(planner.prepare(snapshot, 8u).batches,
            RenderInstanceBatchUpdateKind::Rebuild);
  EXPECT_TRUE(removed_payload.expired());
  EXPECT_EQ(planner.plan().chunks.size(), 1u);
}

} // namespace
