////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/placement/snap_to_floor_service.hpp"

#include "assets/asset_registry.hpp"
#include "core/release_features.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/document/document_command_edits.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/viewport_local_visibility_service.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_hierarchy_visibility.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr float kSnapPositionEpsilon = 0.000001f;

[[nodiscard]] bool finite_vector(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] bool nearly_same_position(QVec3 left, QVec3 right) {
  return std::abs(left.x - right.x) <= kSnapPositionEpsilon &&
         std::abs(left.y - right.y) <= kSnapPositionEpsilon &&
         std::abs(left.z - right.z) <= kSnapPositionEpsilon;
}

[[nodiscard]] bool descendant_or_same(const SceneDocument &scene,
                                      EntityId entity, EntityId root) {
  EntityId current = entity;
  while (current != kInvalidEntityId) {
    if (current == root) {
      return true;
    }
    current = scene.parent(current);
  }
  return false;
}

[[nodiscard]] bool has_selected_candidate_ancestor(
    const SceneDocument &scene, EntityId entity,
    const std::unordered_set<EntityId> &candidates) {
  EntityId current = scene.parent(entity);
  while (current != kInvalidEntityId) {
    if (candidates.contains(current)) {
      return true;
    }
    current = scene.parent(current);
  }
  return false;
}

[[nodiscard]] std::vector<EntityId> object_snap_candidate_roots(
    const SceneDocument &scene, const SceneInstanceEditState &edit_state) {
  std::unordered_set<EntityId> candidates;
  for (const EntityId entity : scene.selected_entities()) {
    const bool active_member =
        edit_state.active &&
        scene.containing_instance_definition(entity) == edit_state.definition;
    if (active_member || scene.mesh(entity) != nullptr ||
        scene.instance_occurrence(entity) != nullptr) {
      candidates.insert(entity);
    }
  }

  std::vector<EntityId> roots;
  roots.reserve(candidates.size());
  for (const EntityId candidate : candidates) {
    if (!has_selected_candidate_ancestor(scene, candidate, candidates)) {
      roots.push_back(candidate);
    }
  }
  std::ranges::sort(roots);
  return roots;
}

void include_bounds(std::optional<SceneBounds> &target,
                    const SceneBounds &source) {
  if (!target.has_value()) {
    target = source;
    return;
  }
  include_point(*target, source.min);
  include_point(*target, source.max);
}

/// Stores one promoted object source and its preflighted placement facts.
struct ObjectSnapTarget final {
  EntityId entity = kInvalidEntityId;
  TransformComponent before;
  SceneBounds world_bounds;
  QMat4 display_from_scene;
};

/// Stores an all-or-nothing promoted source batch and receiver exclusions.
struct ObjectSnapSelection final {
  bool ok = false;
  std::string message;
  std::vector<ObjectSnapTarget> targets;
  std::vector<SceneSurfaceParticipantId> exclusions;
};

[[nodiscard]] bool source_participant_matches(
    const SceneDocument &scene, const SceneSurfaceParticipant &participant,
    EntityId source, const SceneInstanceEditState &edit_state,
    bool bounds_only) {
  if (edit_state.active &&
      scene.containing_instance_definition(source) == edit_state.definition) {
    const bool canonical_member =
        participant.id.definition_entity == edit_state.definition &&
        descendant_or_same(scene, participant.id.geometry_entity, source);
    return canonical_member &&
           (!bounds_only ||
            participant.id.display_entity == edit_state.occurrence);
  }
  // Every non-edit source moves its complete scene hierarchy. Match both
  // ordinary descendant meshes and formal occurrences nested under the root;
  // an occurrence is a leaf by scene invariant, so this also remains exact
  // for a directly selected occurrence.
  return descendant_or_same(scene, participant.id.display_entity, source);
}

[[nodiscard]] ObjectSnapSelection collect_object_snap_selection(
    SceneDocument &scene, const AssetRegistry &assets,
    ModelingSessionService &modeling, SceneInstanceService &instances,
    SnapToFloorToolStatePort &native_tools,
    const ViewportLocalVisibilityService &local_visibility,
    const SceneSurfaceQuerySnapshot &surface) {
  if (modeling.selection_mode() != ModelingSelectionMode::Object) {
    return {.message = "Snap Meshes to Floor only works in object mode."};
  }
  if (native_tools.has_pending_tool() ||
      modeling.can_cancel_active_tool_or_preview()) {
    return {.message = "Commit or cancel the active tool or preview first."};
  }

  const SceneInstanceEditState edit_state = instances.instance_edit_state();
  const std::vector<EntityId> candidate_roots =
      object_snap_candidate_roots(scene, edit_state);
  if (candidate_roots.empty()) {
    return {.message = "Select one or more mesh objects first."};
  }

  ObjectSnapSelection result;
  const std::optional<QMat4> active_display_from_scene =
      edit_state.active ? instances.active_edit_display_from_scene()
                        : std::optional<QMat4>{QMat4{}};
  if (!active_display_from_scene.has_value()) {
    return {.message = "The active instance placement is not invertible."};
  }
  for (const EntityId source : candidate_roots) {
    const TransformComponent *transform = scene.transform(source);
    const bool active_member =
        edit_state.active &&
        scene.containing_instance_definition(source) == edit_state.definition;
    const bool source_visible =
        active_member
            ? scene.is_effectively_visible(edit_state.occurrence) &&
                  scene_hierarchy_segment_is_visible(
                      scene, source, edit_state.definition)
            : scene.is_effectively_visible(source);
    const EntityId display_entity =
        active_member ? edit_state.occurrence : source;
    if (transform == nullptr || scene.is_effectively_locked(source) ||
        !source_visible || !local_visibility.allows(display_entity)) {
      return {.message =
                  "Every snapped mesh must be visible, unlocked, and "
                  "transformable."};
    }
    if (const MeshComponent *component = scene.mesh(source);
        component != nullptr && assets.mesh(component->mesh) == nullptr) {
      return {.message = "A selected mesh asset is unavailable."};
    }

    std::optional<SceneBounds> bounds;
    for (const SceneSurfaceParticipant &participant :
         surface.participants()) {
      if (source_participant_matches(scene, participant, source, edit_state,
                                     true)) {
        include_bounds(bounds, participant.world_bounds);
      }
      if (source_participant_matches(scene, participant, source, edit_state,
                                     false)) {
        result.exclusions.push_back(participant.id);
      }
    }
    if (!bounds.has_value()) {
      return {.message =
                  "Every snapped mesh must have visible surface geometry."};
    }
    result.targets.push_back({
        .entity = source,
        .before = *transform,
        .world_bounds = *bounds,
        .display_from_scene = active_member ? *active_display_from_scene
                                            : QMat4{},
    });
  }
  if (result.targets.empty()) {
    return {.message = "Select one or more top-level mesh objects first."};
  }
  std::ranges::sort(
      result.exclusions,
      [](const SceneSurfaceParticipantId &left,
         const SceneSurfaceParticipantId &right) {
        return scene_surface_participant_id_less(left, right);
      });
  result.exclusions.erase(
      std::unique(result.exclusions.begin(), result.exclusions.end()),
      result.exclusions.end());
  result.ok = true;
  return result;
}

[[nodiscard]] std::optional<DocumentTransformEdit> translated_object_edit(
    const SceneDocument &scene, const ObjectSnapTarget &target,
    float world_y_delta) {
  const EntityId parent = scene.parent(target.entity);
  const QMat4 scene_parent_world =
      parent == kInvalidEntityId ? QMat4{}
                                 : scene.world_transform_matrix(parent);
  const QMat4 display_parent_world = scene_transform_multiply(
      target.display_from_scene,
      scene_compensated_parent_matrix(scene_parent_world, target.before));
  const std::optional<QMat4> parent_from_display =
      try_scene_matrix_inverse(display_parent_world);
  if (!parent_from_display.has_value()) {
    return std::nullopt;
  }
  const QVec3 local_delta = transform_vector(
      *parent_from_display, {0.0f, world_y_delta, 0.0f});
  if (!finite_vector(local_delta)) {
    return std::nullopt;
  }
  TransformComponent after = target.before;
  after.position += local_delta;
  if (!scene_transform_is_finite(after)) {
    return std::nullopt;
  }
  return DocumentTransformEdit{
      .entity = target.entity,
      .before = target.before,
      .after = after,
  };
}

[[nodiscard]] ModelingCommandResult modeling_failure(std::string message) {
  return {.ok = false, .changed = false, .message = std::move(message)};
}

} // namespace

SnapToFloorService::SnapToFloorService(
    SceneDocument &scene, const AssetRegistry &assets,
    DocumentCommandService &document_commands, ModelingSessionService &modeling,
    SceneInstanceService &instances,
    SnapToFloorToolStatePort &native_tools,
    const ViewportLocalVisibilityService &local_visibility,
    SceneSurfaceQuery &surface_query)
    : scene_(scene), assets_(assets), document_commands_(document_commands),
      modeling_(modeling), instances_(instances), native_tools_(native_tools),
      local_visibility_(local_visibility), surface_query_(surface_query) {}

bool SnapToFloorService::can_snap_selected_meshes_to_floor() const {
  if (!release_has_feature(ReleaseFeature::SnapToFloor)) {
    return false;
  }
  if (modeling_.selection_mode() != ModelingSelectionMode::Object ||
      native_tools_.has_pending_tool() ||
      modeling_.can_cancel_active_tool_or_preview()) {
    return false;
  }
  const SceneInstanceEditState edit_state = instances_.instance_edit_state();
  const std::vector<EntityId> candidate_roots =
      object_snap_candidate_roots(scene_, edit_state);
  if (candidate_roots.empty()) {
    return false;
  }
  for (const EntityId entity : candidate_roots) {
    const bool active_member =
        edit_state.active &&
        scene_.containing_instance_definition(entity) == edit_state.definition;
    const bool source_visible =
        active_member
            ? scene_.is_effectively_visible(edit_state.occurrence) &&
                  scene_hierarchy_segment_is_visible(
                      scene_, entity, edit_state.definition)
            : scene_.is_effectively_visible(entity);
    const EntityId display_entity =
        active_member ? edit_state.occurrence : entity;
    const MeshComponent *mesh = scene_.mesh(entity);
    if (!scene_.exists(entity) || scene_.transform(entity) == nullptr ||
        scene_.is_effectively_locked(entity) || !source_visible ||
        !local_visibility_.allows(display_entity) ||
        (mesh != nullptr && assets_.mesh(mesh->mesh) == nullptr)) {
      return false;
    }
  }
  return true;
}

DocumentCommandResult
SnapToFloorService::snap_selected_meshes_to_floor() {
  if (!release_has_feature(ReleaseFeature::SnapToFloor)) {
    return document_command_invalid_request_result(release_feature_unavailable_message());
  }
  QDR_PROFILE_FUNCTION();
  const SceneSurfaceQuerySnapshot surface = surface_query_.snapshot();
  const ObjectSnapSelection selection = collect_object_snap_selection(
      scene_, assets_, modeling_, instances_, native_tools_,
      local_visibility_, surface);
  if (!selection.ok) {
    return document_command_invalid_request_result(selection.message);
  }

  std::vector<DocumentTransformEdit> edits;
  edits.reserve(selection.targets.size());
  SceneSurfaceQueryScratch query_scratch;
  for (const ObjectSnapTarget &target : selection.targets) {
    const QVec3 center = bounds_center(target.world_bounds);
    const QVec3 origin{center.x, target.world_bounds.min.y, center.z};
    const std::optional<SceneSurfaceHit> hit = surface.nearest_hit(
        {
            .world_origin = origin,
            .world_direction = {0.0f, -1.0f, 0.0f},
            .min_distance = 0.0f,
            .max_distance = std::numeric_limits<float>::infinity(),
            .excluded_participants = selection.exclusions,
        },
        query_scratch);
    const float destination_y = hit.has_value() ? hit->world_position.y : 0.0f;
    const float delta_y = destination_y - target.world_bounds.min.y;
    if (!std::isfinite(delta_y)) {
      return document_command_invalid_request_result(
          "A snap destination is not finite.");
    }
    if (std::abs(delta_y) <= kSnapPositionEpsilon) {
      continue;
    }
    const std::optional<DocumentTransformEdit> edit =
        translated_object_edit(scene_, target, delta_y);
    if (!edit.has_value()) {
      return document_command_invalid_request_result(
          "A selected mesh has a non-invertible parent transform.");
    }
    edits.push_back(*edit);
  }
  return document_commands_.set_transforms(std::move(edits),
                                           "Snap Meshes to Floor");
}

bool SnapToFloorService::can_snap_selected_vertices_to_floor() const {
  if (!release_has_feature(ReleaseFeature::SnapToFloor)) {
    return false;
  }
  const ModelingSelectionSummary summary = modeling_.selection_summary();
  return modeling_.selection_mode() == ModelingSelectionMode::Vertex &&
         summary.selected_vertex_count != 0u &&
         !instances_.instance_edit_state().active &&
         !native_tools_.blocks_modeling_content_mutation() &&
         !modeling_.can_cancel_active_tool_or_preview();
}

ModelingCommandResult SnapToFloorService::snap_selected_vertices_to_floor() {
  if (!release_has_feature(ReleaseFeature::SnapToFloor)) {
    return modeling_failure(release_feature_unavailable_message());
  }
  QDR_PROFILE_FUNCTION();
  if (instances_.instance_edit_state().active) {
    return modeling_failure(
        "Snap Vertices to Floor is unavailable while editing an instance.");
  }
  if (native_tools_.blocks_modeling_content_mutation() ||
      modeling_.can_cancel_active_tool_or_preview()) {
    return modeling_failure("Commit or cancel the active tool or preview first.");
  }
  const ModelingSelectedVerticesSnapshot selected =
      modeling_.selected_vertices_snapshot();
  if (!selected.ok) {
    return modeling_failure(selected.message);
  }

  const SceneSurfaceQuerySnapshot surface = surface_query_.snapshot();
  std::vector<SceneSurfaceParticipantId> exclusions;
  for (const SceneSurfaceParticipant &participant : surface.participants()) {
    if (std::ranges::any_of(
            selected.objects,
            [&participant](const ModelingSelectedVertexObjectSnapshot &object) {
              return participant.id.geometry_entity == object.entity;
            })) {
      exclusions.push_back(participant.id);
    }
  }
  std::ranges::sort(
      exclusions,
      [](const SceneSurfaceParticipantId &left,
         const SceneSurfaceParticipantId &right) {
        return scene_surface_participant_id_less(left, right);
      });
  exclusions.erase(std::unique(exclusions.begin(), exclusions.end()),
                   exclusions.end());
  std::vector<ModelingResolvedVertexPositionBatch> batches;
  batches.reserve(selected.objects.size());
  SceneSurfaceQueryScratch query_scratch;
  for (const ModelingSelectedVertexObjectSnapshot &object : selected.objects) {
    ModelingResolvedVertexPositionBatch batch{
        .modeling_object_id = object.modeling_object_id,
    };
    batch.positions.reserve(object.vertices.size());
    for (const ModelingSelectedVertexSnapshot &vertex : object.vertices) {
      const QVec3 world =
          transform_point(object.world_from_object, vertex.local_position);
      if (!finite_vector(world)) {
        return modeling_failure("A selected vertex position is not finite.");
      }
      const std::optional<SceneSurfaceHit> hit = surface.nearest_hit(
          {
              .world_origin = world,
              .world_direction = {0.0f, -1.0f, 0.0f},
              .min_distance = 0.0f,
              .max_distance = std::numeric_limits<float>::infinity(),
              .excluded_participants = exclusions,
          },
          query_scratch);
      const QVec3 destination_world{
          world.x, hit.has_value() ? hit->world_position.y : 0.0f, world.z};
      const QVec3 destination_local =
          transform_point(object.object_from_world, destination_world);
      if (!finite_vector(destination_local)) {
        return modeling_failure(
            "A resolved vertex position is not finite.");
      }
      if (nearly_same_position(destination_local, vertex.local_position)) {
        continue;
      }
      batch.positions.push_back({
          .vertex = vertex.vertex,
          .position = {destination_local.x, destination_local.y,
                       destination_local.z},
      });
    }
    if (!batch.positions.empty()) {
      batches.push_back(std::move(batch));
    }
  }
  return modeling_.apply_resolved_vertex_positions(batches);
}

SceneSurfaceQueryStats
SnapToFloorService::surface_query_stats() const noexcept {
  return surface_query_.stats();
}

void SnapToFloorService::invalidate_surface_cache() noexcept {
  surface_query_.invalidate();
}

} // namespace quader
