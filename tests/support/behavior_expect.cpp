////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "behavior_expect.hpp"

#include "assets/asset_registry.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_bounds.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace quader::test {
namespace {

QVec3 center(const BehaviorBounds& bounds) {
    return (bounds.min + bounds.max) * 0.5f;
}

void expect_vec3_near(QVec3 actual, QVec3 expected, float epsilon) {
  EXPECT_NEAR(actual.x, expected.x, epsilon);
  EXPECT_NEAR(actual.y, expected.y, epsilon);
  EXPECT_NEAR(actual.z, expected.z, epsilon);
}

void expect_transform_near(const TransformComponent &actual,
                           const TransformComponent &expected, float epsilon) {
  expect_vec3_near(actual.position, expected.position, epsilon);
  expect_vec3_near(actual.rotation_euler, expected.rotation_euler, epsilon);
  expect_vec3_near(actual.scale, expected.scale, epsilon);
}

void expect_transform_near(const QuaderTransform &actual,
                           const TransformComponent &expected, float epsilon) {
  expect_vec3_near(actual.position, expected.position, epsilon);
  expect_vec3_near(actual.rotation_euler, expected.rotation_euler, epsilon);
  expect_vec3_near(actual.scale, expected.scale, epsilon);
}

void expect_transform_near(const QuaderTransform &actual,
                           const QuaderTransform &expected, float epsilon) {
  expect_vec3_near(actual.position, expected.position, epsilon);
  expect_vec3_near(actual.rotation_euler, expected.rotation_euler, epsilon);
  expect_vec3_near(actual.scale, expected.scale, epsilon);
}

ModelingSelectionMode modeling_mode_from_api(QuaderSelectionMode mode) {
  switch (mode) {
  case QuaderSelectionMode::Vertex:
    return ModelingSelectionMode::Vertex;
  case QuaderSelectionMode::Edge:
    return ModelingSelectionMode::Edge;
  case QuaderSelectionMode::Face:
    return ModelingSelectionMode::Face;
  case QuaderSelectionMode::Object:
    return ModelingSelectionMode::Object;
  case QuaderSelectionMode::Group:
    return ModelingSelectionMode::Group;
  }
  return ModelingSelectionMode::Object;
}

std::size_t count_for_layer(
    const std::unordered_map<ViewportOverlayLayer, std::size_t> &counts,
    ViewportOverlayLayer layer) {
  const auto found = counts.find(layer);
  return found == counts.end() ? 0u : found->second;
}

std::optional<MeshSnapshot> find_mesh_snapshot(const BehaviorSnapshot &snapshot,
                                               AssetId mesh) {
  const auto found = std::find_if(
      snapshot.meshes.begin(), snapshot.meshes.end(),
      [&](const MeshSnapshot &value) { return value.mesh == mesh; });
  if (found == snapshot.meshes.end()) {
    return std::nullopt;
  }
  return *found;
}

std::optional<EntitySnapshot>
find_entity_snapshot(const BehaviorSnapshot &snapshot, std::string_view alias) {
  const auto found = std::find_if(
      snapshot.entities.begin(), snapshot.entities.end(),
      [&](const EntitySnapshot &value) { return value.alias == alias; });
  if (found == snapshot.entities.end()) {
    return std::nullopt;
  }
  return *found;
}

std::optional<QuaderEntitySnapshot>
find_entity_named(const BehaviorScenario &scenario, std::string_view name) {
  const std::vector<QuaderEntitySnapshot> entities =
      scenario.api().query().entities();
  const auto found = std::find_if(
      entities.begin(), entities.end(),
      [&](const QuaderEntitySnapshot &value) { return value.name == name; });
  if (found == entities.end()) {
    return std::nullopt;
  }
  return *found;
}

std::optional<QuaderEntitySnapshot>
find_entity_alias(const BehaviorScenario &scenario, std::string_view alias) {
  return scenario.api().query().entity(scenario.entity(alias));
}

EntityId scene_root_entity(const BehaviorScenario &scenario) {
  for (const QuaderEntitySnapshot &snapshot :
       scenario.api().query().entities()) {
    if (snapshot.is_scene_root) {
      return snapshot.id;
    }
  }
  return kInvalidEntityId;
}

void include_point(BehaviorBounds &bounds, QVec3 point) {
  bounds.min.x = std::min(bounds.min.x, point.x);
  bounds.min.y = std::min(bounds.min.y, point.y);
  bounds.min.z = std::min(bounds.min.z, point.z);
  bounds.max.x = std::max(bounds.max.x, point.x);
  bounds.max.y = std::max(bounds.max.y, point.y);
  bounds.max.z = std::max(bounds.max.z, point.z);
}

} // namespace

CommandExpect& CommandExpect::ok() {
    EXPECT_TRUE(status_.ok) << status_.message;
    return *this;
}

CommandExpect &CommandExpect::ok_changed() {
  ok();
  EXPECT_TRUE(status_.changed) << status_.message;
  return *this;
}

CommandExpect& CommandExpect::unchanged() {
    ok();
    EXPECT_FALSE(status_.changed) << status_.message;
    return *this;
}

CommandExpect& CommandExpect::failed() {
    EXPECT_FALSE(status_.ok);
    return *this;
}

CommandExpect &CommandExpect::message_contains(std::string_view text) {
  EXPECT_NE(status_.message.find(std::string(text)), std::string::npos)
      << status_.message;
  return *this;
}

SceneExpect &SceneExpect::entity_count(std::size_t count) {
  EXPECT_EQ(scenario_.snapshot().entities.size(), count);
  return *this;
}

SceneExpect &SceneExpect::has_alias(std::string_view alias) {
  EXPECT_TRUE(find_entity_alias(scenario_, alias).has_value()) << alias;
  return *this;
}

SceneExpect &SceneExpect::missing_alias(std::string_view alias) {
  EXPECT_FALSE(find_entity_alias(scenario_, alias).has_value()) << alias;
  return *this;
}

SceneExpect &SceneExpect::named_exists(std::string_view name) {
  EXPECT_TRUE(find_entity_named(scenario_, name).has_value()) << name;
  return *this;
}

SceneExpect &SceneExpect::named_missing(std::string_view name) {
  EXPECT_FALSE(find_entity_named(scenario_, name).has_value()) << name;
  return *this;
}

SceneExpect &SceneExpect::alias_name(std::string_view alias,
                                     std::string_view name) {
  const std::optional<QuaderEntitySnapshot> snapshot =
      find_entity_alias(scenario_, alias);
  EXPECT_TRUE(snapshot.has_value()) << alias;
  if (!snapshot.has_value()) {
    return *this;
  }
  EXPECT_EQ(snapshot->name, name);
  return *this;
}

SceneExpect &SceneExpect::alias_kind(std::string_view alias,
                                     QuaderEntityKind kind) {
  const std::optional<QuaderEntitySnapshot> snapshot =
      find_entity_alias(scenario_, alias);
  EXPECT_TRUE(snapshot.has_value()) << alias;
  if (!snapshot.has_value()) {
    return *this;
  }
  EXPECT_EQ(snapshot->kind, kind);
  return *this;
}

SceneExpect &SceneExpect::parent(std::string_view alias,
                                 std::string_view parent_alias) {
  const std::optional<QuaderEntitySnapshot> snapshot =
      find_entity_alias(scenario_, alias);
  EXPECT_TRUE(snapshot.has_value()) << alias;
  if (!snapshot.has_value()) {
    return *this;
  }
  EXPECT_EQ(snapshot->parent, scenario_.entity(parent_alias));
  return *this;
}

SceneExpect &SceneExpect::no_parent(std::string_view alias) {
  const std::optional<QuaderEntitySnapshot> snapshot =
      find_entity_alias(scenario_, alias);
  EXPECT_TRUE(snapshot.has_value()) << alias;
  if (!snapshot.has_value()) {
    return *this;
  }
  EXPECT_EQ(snapshot->parent, scene_root_entity(scenario_));
  return *this;
}

SceneExpect &SceneExpect::child_count(std::string_view alias,
                                      std::size_t count) {
  const EntityId parent = scenario_.entity(alias);
  std::size_t actual = 0;
  for (const QuaderEntitySnapshot &snapshot :
       scenario_.api().query().entities()) {
    if (snapshot.parent == parent) {
      ++actual;
    }
  }
  EXPECT_EQ(actual, count) << alias;
  return *this;
}

SceneExpect &
SceneExpect::selected_aliases(std::initializer_list<std::string_view> aliases) {
  std::vector<EntityId> expected;
  expected.reserve(aliases.size());
  for (const std::string_view alias : aliases) {
    expected.push_back(scenario_.entity(alias));
  }
  std::sort(expected.begin(), expected.end());

  std::vector<EntityId> actual;
  for (const QuaderEntitySnapshot &snapshot :
       scenario_.api().query().entities()) {
    if (snapshot.selected) {
      actual.push_back(snapshot.id);
    }
  }
  std::sort(actual.begin(), actual.end());
  EXPECT_EQ(actual, expected);
  return *this;
}

SceneExpect &SceneExpect::active_alias(std::string_view alias) {
  const EntityId expected = scenario_.entity(alias);
  bool found = false;
  for (const QuaderEntitySnapshot &snapshot :
       scenario_.api().query().entities()) {
    if (snapshot.active) {
      found = true;
      EXPECT_EQ(snapshot.id, expected);
    }
  }
  EXPECT_TRUE(found) << alias;
  return *this;
}

SceneExpect &SceneExpect::selected_alias_is(std::string_view alias) {
  return active_alias(alias);
}

SceneExpect& SceneExpect::visible(std::string_view alias, bool expected) {
  const std::optional<QuaderEntitySnapshot> snapshot =
      find_entity_alias(scenario_, alias);
  EXPECT_TRUE(snapshot.has_value()) << alias;
  if (!snapshot.has_value()) {
    return *this;
  }
    EXPECT_EQ(snapshot->visible, expected);
    return *this;
}

SceneExpect& SceneExpect::locked(std::string_view alias, bool expected) {
  const std::optional<QuaderEntitySnapshot> snapshot =
      find_entity_alias(scenario_, alias);
  EXPECT_TRUE(snapshot.has_value()) << alias;
  if (!snapshot.has_value()) {
    return *this;
  }
    EXPECT_EQ(snapshot->locked, expected);
    return *this;
}

SceneExpect &SceneExpect::has_mesh(std::string_view alias, AssetId mesh) {
  const std::optional<QuaderEntitySnapshot> snapshot =
      find_entity_alias(scenario_, alias);
  EXPECT_TRUE(snapshot.has_value()) << alias;
  if (!snapshot.has_value()) {
    return *this;
  }
  EXPECT_TRUE(snapshot->has_mesh) << alias;
  EXPECT_EQ(snapshot->mesh, mesh) << alias;
  return *this;
}

SceneExpect &SceneExpect::has_material(std::string_view alias,
                                       AssetId material) {
  const std::optional<QuaderEntitySnapshot> snapshot =
      find_entity_alias(scenario_, alias);
  EXPECT_TRUE(snapshot.has_value()) << alias;
  if (!snapshot.has_value()) {
    return *this;
  }
  EXPECT_TRUE(snapshot->has_material) << alias;
  EXPECT_EQ(snapshot->material, material) << alias;
  return *this;
}

SceneExpect &SceneExpect::world_position_near(std::string_view alias,
                                              QVec3 expected, float epsilon) {
  expect_vec3_near(scenario_.world_position(scenario_.entity(alias)), expected,
                   epsilon);
  return *this;
}

SceneExpect &SceneExpect::selected_bounds_center_near(QVec3 expected,
                                                      float epsilon) {
  const BehaviorSnapshot current = scenario_.snapshot();
  EXPECT_TRUE(current.selected_bounds.has_value());
  if (current.selected_bounds.has_value()) {
    expect_vec3_near(center(*current.selected_bounds), expected, epsilon);
  }
  return *this;
}

SceneExpect &SceneExpect::entity_transform_near(std::string_view alias,
                                                TransformComponent expected,
                                                float epsilon) {
  const std::optional<QuaderEntitySnapshot> snapshot =
      find_entity_alias(scenario_, alias);
  EXPECT_TRUE(snapshot.has_value()) << alias;
  if (!snapshot.has_value()) {
    return *this;
  }
  expect_transform_near(snapshot->transform, expected, epsilon);
  return *this;
}

SceneExpect &SceneExpect::entity_transform_near(std::string_view alias,
                                                QuaderTransform expected,
                                                float epsilon) {
  const std::optional<QuaderEntitySnapshot> snapshot =
      find_entity_alias(scenario_, alias);
  EXPECT_TRUE(snapshot.has_value()) << alias;
  if (!snapshot.has_value()) {
    return *this;
  }
  expect_transform_near(snapshot->transform, expected, epsilon);
  return *this;
}

AssetExpect &AssetExpect::texture_count(std::size_t count) {
  EXPECT_EQ(scenario_.api().query().assets().texture_count, count);
  return *this;
}

SelectionExpect& SelectionExpect::mode(ModelingSelectionMode mode) {
    EXPECT_EQ(scenario_.snapshot().selection.mode, mode);
    return *this;
}

SelectionExpect& SelectionExpect::mode(QuaderSelectionMode mode) {
  return this->mode(modeling_mode_from_api(mode));
}

SelectionExpect &SelectionExpect::object_count(std::uint32_t count) {
  EXPECT_EQ(scenario_.snapshot().selection.selected_object_count, count);
  return *this;
}

SelectionExpect &SelectionExpect::vertex_count(std::uint32_t count) {
  EXPECT_EQ(scenario_.snapshot().selection.selected_vertex_count, count);
  return *this;
}

SelectionExpect &SelectionExpect::edge_count(std::uint32_t count) {
  EXPECT_EQ(scenario_.snapshot().selection.selected_edge_count, count);
  return *this;
}

SelectionExpect &SelectionExpect::face_count(std::uint32_t count) {
  EXPECT_EQ(scenario_.snapshot().selection.selected_face_count, count);
  return *this;
}

SelectionExpect &SelectionExpect::active_alias(std::string_view alias) {
  EXPECT_EQ(scenario_.snapshot().selection.active_object_id,
            scenario_.modeling_object(alias));
  return *this;
}

SelectionExpect &SelectionExpect::active_vertex(int vertex) {
  EXPECT_EQ(scenario_.snapshot().selection.active_vertex_id, vertex);
  return *this;
}

SelectionExpect &SelectionExpect::active_edge(int a, int b) {
  const ModelingSelectionSummary selection = scenario_.snapshot().selection;
  const bool same_order =
      selection.active_edge_a == a && selection.active_edge_b == b;
  const bool reversed =
      selection.active_edge_a == b && selection.active_edge_b == a;
  EXPECT_TRUE(same_order || reversed)
      << "active edge was (" << selection.active_edge_a << ", "
      << selection.active_edge_b << ")";
  return *this;
}

SelectionExpect &SelectionExpect::active_face(int face) {
  EXPECT_EQ(scenario_.snapshot().selection.active_face_id, face);
  return *this;
}

SelectionExpect &
SelectionExpect::revision_changed_from(const ModelingSelectionSummary &before) {
  const ModelingSelectionSummary current = scenario_.snapshot().selection;
  EXPECT_TRUE(current.content_revision != before.content_revision ||
              current.selection_revision != before.selection_revision);
  return *this;
}

SelectionExpect &
SelectionExpect::revision_same_as(const ModelingSelectionSummary &before) {
  const ModelingSelectionSummary current = scenario_.snapshot().selection;
  EXPECT_EQ(current.content_revision, before.content_revision);
  EXPECT_EQ(current.selection_revision, before.selection_revision);
  return *this;
}

MeshExpect::MeshExpect(const BehaviorScenario& scenario, std::string_view alias) {
    const BehaviorSnapshot snapshot = scenario.snapshot();
    const std::optional<EntitySnapshot> entity =
        find_entity_snapshot(snapshot, alias);
    if (entity.has_value()) {
      mesh_ = find_mesh_snapshot(snapshot, entity->mesh);
    }
    EXPECT_TRUE(mesh_.has_value()) << "Missing mesh snapshot for alias " << alias;
}

MeshExpect &MeshExpect::vertex_count(std::size_t count) {
  if (mesh_.has_value()) {
    EXPECT_EQ(mesh_->vertex_count, count);
  }
  return *this;
}

MeshExpect &MeshExpect::index_count(std::size_t count) {
  if (mesh_.has_value()) {
    EXPECT_EQ(mesh_->index_count, count);
  }
  return *this;
}

MeshExpect &MeshExpect::vertices_greater_than(const MeshSnapshot &before) {
  if (mesh_.has_value()) {
    EXPECT_GT(mesh_->vertex_count, before.vertex_count);
  }
  return *this;
}

MeshExpect &MeshExpect::vertices_differ_from(const MeshSnapshot &before,
                                             float epsilon) {
  if (mesh_.has_value()) {
    EXPECT_EQ(mesh_->positions.size(), before.positions.size());
    if (mesh_->positions.size() != before.positions.size()) {
      return *this;
    }
    bool differs = false;
    for (std::size_t index = 0; index < mesh_->positions.size(); ++index) {
      const QVec3 delta = mesh_->positions[index] - before.positions[index];
      if (std::abs(delta.x) > epsilon || std::abs(delta.y) > epsilon ||
          std::abs(delta.z) > epsilon) {
        differs = true;
        break;
      }
    }
    EXPECT_TRUE(differs);
  }
  return *this;
}

MeshExpect &MeshExpect::bounds_min_near(QVec3 expected, float epsilon) {
  if (mesh_.has_value()) {
    expect_vec3_near(mesh_->bounds.min, expected, epsilon);
  }
  return *this;
}

MeshExpect &MeshExpect::bounds_max_near(QVec3 expected, float epsilon) {
  if (mesh_.has_value()) {
    expect_vec3_near(mesh_->bounds.max, expected, epsilon);
  }
  return *this;
}

MeshExpect &MeshExpect::bounds_center_near(QVec3 expected, float epsilon) {
  if (mesh_.has_value()) {
    expect_vec3_near(center(mesh_->bounds), expected, epsilon);
  }
  return *this;
}

OverlayExpect &OverlayExpect::empty() {
  const ViewportOverlayFrame frame = scenario_.selection_overlay_frame();
  EXPECT_TRUE(frame.lines.empty());
  EXPECT_TRUE(frame.points.empty());
  EXPECT_TRUE(frame.triangles.empty());
  return *this;
}

OverlayExpect &OverlayExpect::has_line(ViewportOverlayLayer layer) {
  EXPECT_GT(count_for_layer(scenario_.snapshot().overlay.lines_by_layer, layer),
            0u);
  return *this;
}

OverlayExpect &OverlayExpect::line_count(ViewportOverlayLayer layer,
                                         std::size_t count) {
  EXPECT_EQ(count_for_layer(scenario_.snapshot().overlay.lines_by_layer, layer),
            count);
  return *this;
}

OverlayExpect &OverlayExpect::has_point(ViewportOverlayLayer layer) {
  EXPECT_GT(
      count_for_layer(scenario_.snapshot().overlay.points_by_layer, layer), 0u);
  return *this;
}

OverlayExpect &OverlayExpect::point_count(ViewportOverlayLayer layer,
                                          std::size_t count) {
  EXPECT_EQ(
      count_for_layer(scenario_.snapshot().overlay.points_by_layer, layer),
      count);
  return *this;
}

OverlayExpect &OverlayExpect::has_triangle(ViewportOverlayLayer layer) {
  EXPECT_GT(
      count_for_layer(scenario_.snapshot().overlay.triangles_by_layer, layer),
      0u);
  return *this;
}

OverlayExpect &OverlayExpect::triangle_count(ViewportOverlayLayer layer,
                                             std::size_t count) {
  EXPECT_EQ(
      count_for_layer(scenario_.snapshot().overlay.triangles_by_layer, layer),
      count);
  return *this;
}

OverlayExpect &
OverlayExpect::point_bounds_center_near(ViewportOverlayLayer layer,
                                        QVec3 expected, float epsilon) {
  const ViewportOverlayFrame frame = scenario_.selection_overlay_frame();
  std::optional<BehaviorBounds> bounds;
  for (const ViewportOverlayPoint &point : frame.points) {
    if (point.layer != layer) {
      continue;
    }
    if (!bounds.has_value()) {
      bounds = BehaviorBounds{.min = point.position, .max = point.position};
    } else {
      include_point(*bounds, point.position);
    }
  }
  EXPECT_TRUE(bounds.has_value());
  if (bounds.has_value()) {
    expect_vec3_near(center(*bounds), expected, epsilon);
  }
  return *this;
}

OverlayExpect &OverlayExpect::line_bounds_within(ViewportOverlayLayer layer,
                                                 QuaderBounds bounds,
                                                 float epsilon) {
  const ViewportOverlayFrame frame = scenario_.selection_overlay_frame();
  bool found = false;
  for (const ViewportOverlayLine &line : frame.lines) {
    if (line.layer != layer) {
      continue;
    }
    found = true;
    for (const QVec3 point : {line.start, line.end}) {
      EXPECT_GE(point.x, bounds.min.x - epsilon);
      EXPECT_GE(point.y, bounds.min.y - epsilon);
      EXPECT_GE(point.z, bounds.min.z - epsilon);
      EXPECT_LE(point.x, bounds.max.x + epsilon);
      EXPECT_LE(point.y, bounds.max.y + epsilon);
      EXPECT_LE(point.z, bounds.max.z + epsilon);
    }
  }
  EXPECT_TRUE(found);
  return *this;
}

OverlayExpect &OverlayExpect::triangle_bounds_within(ViewportOverlayLayer layer,
                                                     QuaderBounds bounds,
                                                     float epsilon) {
  const ViewportOverlayFrame frame = scenario_.selection_overlay_frame();
  bool found = false;
  for (const ViewportOverlayTriangle &triangle : frame.triangles) {
    if (triangle.layer != layer) {
      continue;
    }
    found = true;
    for (const QVec3 point : {triangle.a, triangle.b, triangle.c}) {
      EXPECT_GE(point.x, bounds.min.x - epsilon);
      EXPECT_GE(point.y, bounds.min.y - epsilon);
      EXPECT_GE(point.z, bounds.min.z - epsilon);
      EXPECT_LE(point.x, bounds.max.x + epsilon);
      EXPECT_LE(point.y, bounds.max.y + epsilon);
      EXPECT_LE(point.z, bounds.max.z + epsilon);
    }
  }
  EXPECT_TRUE(found);
  return *this;
}

OverlayExpect &OverlayExpect::finite_coordinates(float max_abs_coordinate) {
  const ViewportOverlayFrame frame = scenario_.selection_overlay_frame();
  auto expect_finite = [&](QVec3 point) {
    EXPECT_TRUE(std::isfinite(point.x));
    EXPECT_TRUE(std::isfinite(point.y));
    EXPECT_TRUE(std::isfinite(point.z));
    EXPECT_LE(std::abs(point.x), max_abs_coordinate);
    EXPECT_LE(std::abs(point.y), max_abs_coordinate);
    EXPECT_LE(std::abs(point.z), max_abs_coordinate);
  };
  for (const ViewportOverlayLine &line : frame.lines) {
    expect_finite(line.start);
    expect_finite(line.end);
  }
  for (const ViewportOverlayTriangle &triangle : frame.triangles) {
    expect_finite(triangle.a);
    expect_finite(triangle.b);
    expect_finite(triangle.c);
  }
  for (const ViewportOverlayPoint &point : frame.points) {
    expect_finite(point.position);
  }
  return *this;
}

ToolStatusExpect &ToolStatusExpect::active_tool(ModelingViewportTool tool) {
  EXPECT_EQ(scenario_.snapshot().tool_status.active_tool, tool);
  return *this;
}

ToolStatusExpect &ToolStatusExpect::modal_owns_viewport(bool expected) {
  EXPECT_EQ(scenario_.snapshot().tool_status.modal_tool_owns_viewport,
            expected);
  return *this;
}

ToolStatusExpect &ToolStatusExpect::cut_active(bool expected) {
  EXPECT_EQ(scenario_.snapshot().tool_status.cut_active, expected);
  return *this;
}

ToolStatusExpect &ToolStatusExpect::mirror_active(bool expected) {
  EXPECT_EQ(scenario_.snapshot().tool_status.mirror_active, expected);
  return *this;
}

ToolStatusExpect &ToolStatusExpect::poly_active(bool expected) {
  EXPECT_EQ(scenario_.snapshot().tool_status.poly_active, expected);
  return *this;
}

UndoRedoExpect &UndoRedoExpect::can_undo(bool expected) {
  EXPECT_EQ(scenario_.api().history().can_undo(), expected);
  return *this;
}

UndoRedoExpect &UndoRedoExpect::can_redo(bool expected) {
  EXPECT_EQ(scenario_.api().history().can_redo(), expected);
  return *this;
}

UndoRedoExpect &UndoRedoExpect::undo_label(std::string_view expected) {
  EXPECT_EQ(scenario_.api().query().history().undo_label, expected);
  return *this;
}

UndoRedoExpect &UndoRedoExpect::redo_label(std::string_view expected) {
  EXPECT_EQ(scenario_.api().query().history().redo_label, expected);
  return *this;
}

SnapshotExpect &SnapshotExpect::selected_bounds_center_near(QVec3 expected,
                                                            float epsilon) {
  EXPECT_TRUE(snapshot_.selected_bounds.has_value()) << snapshot_.label;
  if (snapshot_.selected_bounds.has_value()) {
    expect_vec3_near(center(*snapshot_.selected_bounds), expected, epsilon);
  }
  return *this;
}

SnapshotExpect &SnapshotExpect::entity_count(std::size_t count) {
  EXPECT_EQ(snapshot_.entities.size(), count) << snapshot_.label;
  return *this;
}

SnapshotExpect &SnapshotExpect::mesh_vertex_count(std::string_view alias,
                                                  std::size_t count) {
  const auto entity = std::find_if(
      snapshot_.entities.begin(), snapshot_.entities.end(),
      [&](const EntitySnapshot &value) { return value.alias == alias; });
  EXPECT_NE(entity, snapshot_.entities.end()) << alias;
  if (entity == snapshot_.entities.end()) {
    return *this;
  }

  const std::optional<MeshSnapshot> mesh =
      find_mesh_snapshot(snapshot_, entity->mesh);
  EXPECT_TRUE(mesh.has_value()) << alias;
  if (mesh.has_value()) {
    EXPECT_EQ(mesh->vertex_count, count) << alias;
  }
  return *this;
}

} // namespace quader::test
