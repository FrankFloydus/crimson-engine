////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/array/linear_array_tool_service.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace quader {
namespace {

constexpr float kLinearArrayEpsilon = 0.00001f;

bool finite_vector(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

bool near_same_point(QVec3 left, QVec3 right) {
  return length_squared(left - right) <=
         kLinearArrayEpsilon * kLinearArrayEpsilon;
}

bool same_matrix(const QMat4 &left, const QMat4 &right) {
  for (std::size_t index = 0; index < left.values.size(); ++index) {
    if (std::abs(left.values[index] - right.values[index]) >
        kLinearArrayEpsilon) {
      return false;
    }
  }
  return true;
}

AssetId entity_material(const SceneDocument &scene, EntityId entity) {
  const MaterialComponent *material = scene.material(entity);
  return material != nullptr ? material->material : kInvalidAssetId;
}

QVec3 constrained_endpoint(QVec3 source, QVec3 endpoint,
                           LinearArrayAxisConstraint constraint) {
  switch (constraint) {
  case LinearArrayAxisConstraint::X:
    return {endpoint.x, source.y, source.z};
  case LinearArrayAxisConstraint::Y:
    return {source.x, endpoint.y, source.z};
  case LinearArrayAxisConstraint::Z:
    return {source.x, source.y, endpoint.z};
  case LinearArrayAxisConstraint::Free:
    break;
  }
  return endpoint;
}

QVec3 snapped_endpoint(QVec3 source, QVec3 endpoint,
                       LinearArrayAxisConstraint constraint, double grid_size) {
  if (!std::isfinite(grid_size) || grid_size <= kLinearArrayEpsilon) {
    return endpoint;
  }
  const float grid = static_cast<float>(grid_size);
  const auto snapped = [grid](float value) {
    return std::round(value / grid) * grid;
  };
  switch (constraint) {
  case LinearArrayAxisConstraint::X:
    endpoint.x = snapped(endpoint.x);
    break;
  case LinearArrayAxisConstraint::Y:
    endpoint.y = snapped(endpoint.y);
    break;
  case LinearArrayAxisConstraint::Z:
    endpoint.z = snapped(endpoint.z);
    break;
  case LinearArrayAxisConstraint::Free:
    endpoint = {snapped(endpoint.x), snapped(endpoint.y), snapped(endpoint.z)};
    break;
  }
  return constrained_endpoint(source, endpoint, constraint);
}

} // namespace

LinearArrayToolService::LinearArrayToolService(
    SceneDocument &scene, AssetRegistry &assets, EditorHistoryService &history,
    ModelingSessionService &modeling, EditorSelectionService &selection)
    : scene_(scene), assets_(assets), history_(history), modeling_(modeling),
      selection_(selection) {}

bool LinearArrayToolService::valid_source_selection(EntityId &source) const {
  source = kInvalidEntityId;
  if (selection_.mode() != ModelingSelectionMode::Object) {
    return false;
  }
  const std::vector<EntityId> selected = scene_.selected_entities();
  if (selected.size() != 1u || scene_.primary_selection() != selected.front()) {
    return false;
  }
  source = selected.front();
  const ModelingObjectComponent *modeling = scene_.modeling_object(source);
  const MeshComponent *mesh = scene_.mesh(source);
  const TransformComponent *transform = scene_.transform(source);
  return scene_.exists(source) && modeling != nullptr && mesh != nullptr &&
         transform != nullptr &&
         modeling->object_id != kInvalidModelingObjectId &&
         modeling_.modeling_object_for_entity(source) == modeling->object_id &&
         assets_.mesh(mesh->mesh) != nullptr &&
         scene_.is_effectively_visible(source) &&
         !scene_.is_effectively_locked(source);
}

bool LinearArrayToolService::can_activate() const {
  EntityId source = kInvalidEntityId;
  return valid_source_selection(source);
}

bool LinearArrayToolService::active() const {
  return session_.has_value() && session_source_is_valid(*session_);
}

std::uint32_t LinearArrayToolService::count() const {
  return session_.has_value() ? session_->count : kLinearArrayDefaultCount;
}

bool LinearArrayToolService::session_source_is_valid(
    const Session &session) const {
  if (!scene_.exists(session.source_entity) ||
      scene_.selection_revision() != session.selection_revision ||
      scene_.uuid(session.source_entity) != session.source_uuid ||
      scene_.parent(session.source_entity) != session.source_parent ||
      !scene_.is_effectively_visible(session.source_entity) ||
      scene_.is_effectively_locked(session.source_entity)) {
    return false;
  }
  const std::vector<EntityId> selected = scene_.selected_entities();
  const ModelingObjectComponent *modeling =
      scene_.modeling_object(session.source_entity);
  const MeshComponent *mesh = scene_.mesh(session.source_entity);
  const MeshAsset *mesh_asset =
      mesh != nullptr ? assets_.mesh(mesh->mesh) : nullptr;
  return selection_.mode() == ModelingSelectionMode::Object &&
         selected.size() == 1u && selected.front() == session.source_entity &&
         scene_.primary_selection() == session.source_entity &&
         modeling != nullptr &&
         modeling->object_id == session.source_modeling_object_id &&
         mesh != nullptr && mesh->mesh == session.source_mesh &&
         mesh_asset != nullptr &&
         mesh_asset->revision == session.source_mesh_revision &&
         entity_material(scene_, session.source_entity) ==
             session.source_material &&
         same_matrix(scene_.world_transform_matrix(session.source_entity),
                     session.source_world_transform);
}

CommandResult LinearArrayToolService::activate(double fallback_grid_size) {
  EntityId source = kInvalidEntityId;
  if (!valid_source_selection(source)) {
    return command_invalid_result(
        "Select exactly one visible, unlocked mesh in object mode.");
  }
  const ModelingObjectComponent *modeling = scene_.modeling_object(source);
  const MeshComponent *mesh = scene_.mesh(source);
  const MeshAsset *mesh_asset =
      mesh != nullptr ? assets_.mesh(mesh->mesh) : nullptr;
  if (modeling == nullptr || mesh == nullptr || mesh_asset == nullptr) {
    return command_invalid_result("Linear Array source mesh was not found.");
  }

  const QMat4 source_world_transform = scene_.world_transform_matrix(source);
  const QVec3 source_world = transform_point(source_world_transform, {});
  float adjacent_step = 0.0f;
  if (const std::optional<SceneBounds> bounds =
          entity_world_bounds(scene_, assets_, source)) {
    adjacent_step = bounds->max.x - bounds->min.x;
  }
  if (!std::isfinite(adjacent_step) || adjacent_step <= kLinearArrayEpsilon) {
    adjacent_step =
        std::isfinite(fallback_grid_size) &&
                fallback_grid_size > kLinearArrayEpsilon
            ? static_cast<float>(fallback_grid_size)
            : 1.0f;
  }

  session_ = Session{
      .source_entity = source,
      .source_modeling_object_id = modeling->object_id,
      .source_uuid = scene_.uuid(source),
      .source_parent = scene_.parent(source),
      .source_mesh = mesh->mesh,
      .source_material = entity_material(scene_, source),
      .source_mesh_revision = mesh_asset->revision,
      .source_world_transform = source_world_transform,
      .source_world = source_world,
      .endpoint_world =
          source_world +
          QVec3{adjacent_step *
                    static_cast<float>(kLinearArrayDefaultCount - 1u),
                0.0f, 0.0f},
      .count = kLinearArrayDefaultCount,
      .constraint = LinearArrayAxisConstraint::Free,
      .selection_revision = scene_.selection_revision(),
      .revision = 1u,
      .dragging = false,
  };
  return command_changed_result();
}

CommandResult LinearArrayToolService::refresh() {
  if (!session_.has_value()) {
    return command_unchanged_result();
  }
  if (session_source_is_valid(*session_)) {
    return command_unchanged_result();
  }
  session_.reset();
  return {
      .ok = true,
      .changed = true,
      .message = "Linear Array canceled because its source changed.",
  };
}

CommandResult LinearArrayToolService::cancel() {
  if (!session_.has_value()) {
    return command_unchanged_result();
  }
  session_.reset();
  return command_changed_result();
}

CommandResult LinearArrayToolService::apply() {
  if (!session_.has_value()) {
    return command_unchanged_result();
  }
  if (!session_source_is_valid(*session_)) {
    session_.reset();
    return command_invalid_result(
        "Linear Array source changed before the operation was applied.");
  }
  const Session committed = *session_;
  session_.reset();
  return history_.execute(std::make_unique<LinearArrayHistoryCommand>(
      modeling_, ModelingLinearArrayRequest{
                     .source_entity = committed.source_entity,
                     .source_modeling_object_id =
                         committed.source_modeling_object_id,
                     .endpoint_world = committed.endpoint_world,
                     .count = committed.count,
                 }));
}

CommandResult LinearArrayToolService::set_count(std::uint32_t count) {
  if (!session_.has_value()) {
    return command_invalid_result("Linear Array tool is not active.");
  }
  if (count < kLinearArrayMinimumCount || count > kLinearArrayMaximumCount) {
    return command_invalid_result(
        "Linear Array count must be between 2 and 128.");
  }
  if (session_->count == count) {
    return command_unchanged_result();
  }
  session_->count = count;
  ++session_->revision;
  return command_changed_result();
}

CommandResult LinearArrayToolService::set_axis_constraint(
    LinearArrayAxisConstraint constraint) {
  if (!session_.has_value()) {
    return command_invalid_result("Linear Array tool is not active.");
  }
  const LinearArrayAxisConstraint next =
      session_->constraint == constraint ? LinearArrayAxisConstraint::Free
                                         : constraint;
  if (session_->constraint == next) {
    return command_unchanged_result();
  }
  session_->constraint = next;
  session_->endpoint_world = constrained_endpoint(
      session_->source_world, session_->endpoint_world, next);
  ++session_->revision;
  return command_changed_result();
}

CommandResult LinearArrayToolService::set_endpoint(QVec3 world_position,
                                                   bool snap_to_grid,
                                                   double grid_size) {
  if (!session_.has_value()) {
    return command_invalid_result("Linear Array tool is not active.");
  }
  if (!finite_vector(world_position)) {
    return command_invalid_result("Linear Array endpoint must be finite.");
  }
  QVec3 endpoint = constrained_endpoint(
      session_->source_world, world_position, session_->constraint);
  if (snap_to_grid) {
    endpoint = snapped_endpoint(session_->source_world, endpoint,
                                session_->constraint, grid_size);
  }
  if (near_same_point(session_->endpoint_world, endpoint)) {
    return command_unchanged_result();
  }
  session_->endpoint_world = endpoint;
  ++session_->revision;
  return command_changed_result();
}

void LinearArrayToolService::begin_endpoint_drag() {
  if (session_.has_value() && !session_->dragging) {
    session_->dragging = true;
    ++session_->revision;
  }
}

void LinearArrayToolService::end_endpoint_drag() {
  if (session_.has_value() && session_->dragging) {
    session_->dragging = false;
    ++session_->revision;
  }
}

LinearArrayHandleState LinearArrayToolService::handle_state() const {
  if (!session_.has_value() || !session_source_is_valid(*session_)) {
    return {};
  }
  return {
      .visible = true,
      .dragging = session_->dragging,
      .source_world = session_->source_world,
      .endpoint_world = session_->endpoint_world,
      .constraint = session_->constraint,
      .revision = session_->revision,
  };
}

QMat4 LinearArrayToolService::with_world_origin(QMat4 transform,
                                                QVec3 world_origin) {
  transform.values[12] = world_origin.x;
  transform.values[13] = world_origin.y;
  transform.values[14] = world_origin.z;
  return transform;
}

LinearArrayPreviewState LinearArrayToolService::preview_state() const {
  if (!session_.has_value() || !session_source_is_valid(*session_)) {
    return {};
  }
  LinearArrayPreviewState preview{
      .active = true,
      .source_entity = session_->source_entity,
      .mesh = session_->source_mesh,
      .material = session_->source_material,
      .revision = session_->revision,
  };
  preview.copy_world_transforms.reserve(session_->count - 1u);
  const QVec3 span = session_->endpoint_world - session_->source_world;
  for (std::uint32_t index = 1u; index < session_->count; ++index) {
    const float factor = static_cast<float>(index) /
                         static_cast<float>(session_->count - 1u);
    preview.copy_world_transforms.push_back(with_world_origin(
        session_->source_world_transform,
        session_->source_world + span * factor));
  }
  return preview;
}

} // namespace quader
