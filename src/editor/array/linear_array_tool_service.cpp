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
#include <cstdint>
#include <memory>
#include <utility>

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
  return left.values == right.values;
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

void LinearArrayToolService::clear_preview_cache() {
  cached_preview_revision_ = 0;
  cached_preview_ = {};
}

std::uint64_t LinearArrayToolService::issue_revision() {
  return ++revision_counter_;
}

std::uint32_t
LinearArrayToolService::maximum_count_for(std::size_t source_count) {
  if (source_count == 0u ||
      source_count > kLinearArrayMaximumTotalElements /
                         kLinearArrayMinimumCount) {
    return 0u;
  }
  return static_cast<std::uint32_t>(std::min<std::size_t>(
      kLinearArrayMaximumCount,
      kLinearArrayMaximumTotalElements / source_count));
}

bool LinearArrayToolService::valid_source_selection(
    std::vector<EntityId> &ordered_sources, EntityId &primary_source) const {
  ordered_sources.clear();
  primary_source = kInvalidEntityId;
  if (selection_.mode() != ModelingSelectionMode::Object) {
    return false;
  }
  ordered_sources = scene_.selected_entities();
  if (ordered_sources.empty()) {
    return false;
  }
  primary_source = scene_.primary_selection();
  const auto primary = std::find(ordered_sources.begin(),
                                 ordered_sources.end(), primary_source);
  if (primary == ordered_sources.end()) {
    return false;
  }

  for (const EntityId source : ordered_sources) {
    const ModelingObjectComponent *modeling = scene_.modeling_object(source);
    const MeshComponent *mesh = scene_.mesh(source);
    const TransformComponent *transform = scene_.transform(source);
    if (!scene_.exists(source) || modeling == nullptr || mesh == nullptr ||
        transform == nullptr ||
        modeling->object_id == kInvalidModelingObjectId ||
        modeling_.modeling_object_for_entity(source) != modeling->object_id ||
        assets_.mesh(mesh->mesh) == nullptr ||
        !scene_.is_effectively_visible(source) ||
        scene_.is_effectively_locked(source)) {
      ordered_sources.clear();
      primary_source = kInvalidEntityId;
      return false;
    }
  }

  ordered_sources.erase(primary);
  ordered_sources.push_back(primary_source);
  return maximum_count_for(ordered_sources.size()) >=
         kLinearArrayMinimumCount;
}

bool LinearArrayToolService::can_activate() const {
  std::vector<EntityId> sources;
  EntityId primary = kInvalidEntityId;
  return valid_source_selection(sources, primary);
}

bool LinearArrayToolService::session_sources_are_valid(
    const Session &session) const {
  const std::uint64_t scene_revision = scene_.revision();
  const std::uint64_t asset_revision = assets_.revision();
  const std::uint64_t object_map_revision =
      modeling_.modeling_object_map_revision();
  const int selection_mode = static_cast<int>(selection_.mode());
  if (validated_session_generation_ == session.generation &&
      validated_scene_revision_ == scene_revision &&
      validated_asset_revision_ == asset_revision &&
      validated_object_map_revision_ == object_map_revision &&
      validated_selection_mode_ == selection_mode) {
    return cached_session_sources_valid_;
  }

  cached_session_sources_valid_ =
      compute_session_sources_are_valid(session);
  validated_session_generation_ = session.generation;
  validated_scene_revision_ = scene_revision;
  validated_asset_revision_ = asset_revision;
  validated_object_map_revision_ = object_map_revision;
  validated_selection_mode_ = selection_mode;
  return cached_session_sources_valid_;
}

bool LinearArrayToolService::compute_session_sources_are_valid(
    const Session &session) const {
  if (selection_.mode() != ModelingSelectionMode::Object ||
      scene_.selection_revision() != session.selection_revision ||
      scene_.primary_selection() != session.primary_source) {
    return false;
  }

  const std::vector<EntityId> selected = scene_.selected_entities();
  std::vector<EntityId> captured;
  captured.reserve(session.sources.size());
  for (const SourceSnapshot &source : session.sources) {
    captured.push_back(source.entity);
  }
  std::sort(captured.begin(), captured.end());
  if (selected != captured) {
    return false;
  }

  for (const SourceSnapshot &source : session.sources) {
    if (!scene_.exists(source.entity) ||
        scene_.uuid(source.entity) != source.uuid ||
        scene_.parent(source.entity) != source.parent ||
        !scene_.is_effectively_visible(source.entity) ||
        scene_.is_effectively_locked(source.entity)) {
      return false;
    }
    const ModelingObjectComponent *modeling =
        scene_.modeling_object(source.entity);
    const MeshComponent *mesh = scene_.mesh(source.entity);
    const MeshAsset *mesh_asset =
        mesh != nullptr ? assets_.mesh(mesh->mesh) : nullptr;
    if (modeling == nullptr || modeling->object_id != source.modeling_object_id ||
        modeling_.modeling_object_for_entity(source.entity) !=
            source.modeling_object_id ||
        mesh == nullptr || mesh->mesh != source.mesh || mesh_asset == nullptr ||
        mesh_asset->revision != source.mesh_revision ||
        entity_material(scene_, source.entity) != source.material ||
        assets_.material_revision(source.material) !=
            source.material_revision ||
        !same_matrix(scene_.world_transform_matrix(source.entity),
                     source.world_transform)) {
      return false;
    }
  }
  return true;
}

bool LinearArrayToolService::active() const {
  return session_.has_value() && session_sources_are_valid(*session_);
}

std::uint32_t LinearArrayToolService::count() const {
  return session_.has_value() ? session_->count : kLinearArrayDefaultCount;
}

std::uint32_t LinearArrayToolService::maximum_count() const {
  return session_.has_value() ? maximum_count_for(session_->sources.size())
                              : kLinearArrayMaximumCount;
}

CommandResult LinearArrayToolService::activate(double fallback_grid_size) {
  std::vector<EntityId> ordered_sources;
  EntityId primary_source = kInvalidEntityId;
  if (!valid_source_selection(ordered_sources, primary_source)) {
    return command_invalid_result(
        "Select one or more visible, unlocked meshes in object mode.");
  }

  Session next;
  next.generation = issue_revision();
  next.revision = next.generation;
  next.primary_source = primary_source;
  next.selection_revision = scene_.selection_revision();
  next.count = std::min(kLinearArrayDefaultCount,
                        maximum_count_for(ordered_sources.size()));
  next.sources.reserve(ordered_sources.size());
  for (const EntityId entity : ordered_sources) {
    const ModelingObjectComponent *modeling = scene_.modeling_object(entity);
    const MeshComponent *mesh = scene_.mesh(entity);
    const MeshAsset *mesh_asset =
        mesh != nullptr ? assets_.mesh(mesh->mesh) : nullptr;
    if (modeling == nullptr || mesh == nullptr || mesh_asset == nullptr) {
      return command_invalid_result("Linear Array source mesh was not found.");
    }
    next.sources.push_back(SourceSnapshot{
        .entity = entity,
        .modeling_object_id = modeling->object_id,
        .uuid = scene_.uuid(entity),
        .parent = scene_.parent(entity),
        .mesh = mesh->mesh,
        .material = entity_material(scene_, entity),
        .mesh_revision = mesh_asset->revision,
        .material_revision =
            assets_.material_revision(entity_material(scene_, entity)),
        .world_transform = scene_.world_transform_matrix(entity),
    });
  }

  next.anchor_world = transform_point(next.sources.back().world_transform, {});
  float adjacent_step = 0.0f;
  if (const std::optional<SceneBounds> bounds =
          selected_world_bounds(scene_, assets_)) {
    adjacent_step = bounds->max.x - bounds->min.x;
  }
  if (!std::isfinite(adjacent_step) || adjacent_step <= kLinearArrayEpsilon) {
    adjacent_step =
        std::isfinite(fallback_grid_size) &&
                fallback_grid_size > kLinearArrayEpsilon
            ? static_cast<float>(fallback_grid_size)
            : 1.0f;
  }
  next.endpoint_world =
      next.anchor_world +
      QVec3{adjacent_step * static_cast<float>(next.count - 1u), 0.0f, 0.0f};
  session_ = std::move(next);
  clear_preview_cache();
  return command_changed_result();
}

CommandResult LinearArrayToolService::refresh() {
  if (!session_.has_value()) {
    return command_unchanged_result();
  }
  if (session_sources_are_valid(*session_)) {
    return command_unchanged_result();
  }
  session_.reset();
  clear_preview_cache();
  return {
      .ok = true,
      .changed = true,
      .message = "Linear Array canceled because its sources changed.",
  };
}

CommandResult LinearArrayToolService::cancel() {
  if (!session_.has_value()) {
    return command_unchanged_result();
  }
  session_.reset();
  clear_preview_cache();
  return command_changed_result();
}

CommandResult LinearArrayToolService::apply() {
  if (!session_.has_value()) {
    return command_unchanged_result();
  }
  if (!session_sources_are_valid(*session_)) {
    session_.reset();
    clear_preview_cache();
    return command_invalid_result(
        "Linear Array sources changed before the operation was applied.");
  }

  const Session committed = *session_;
  session_.reset();
  clear_preview_cache();
  ModelingLinearArrayRequest request{
      .span_world = committed.endpoint_world - committed.anchor_world,
      .count = committed.count,
  };
  request.sources.reserve(committed.sources.size());
  for (const SourceSnapshot &source : committed.sources) {
    request.sources.push_back({
        .entity = source.entity,
        .modeling_object_id = source.modeling_object_id,
    });
  }
  return history_.execute(std::make_unique<LinearArrayHistoryCommand>(
      modeling_, std::move(request)));
}

CommandResult LinearArrayToolService::set_count(std::uint32_t count) {
  if (!session_.has_value()) {
    return command_invalid_result("Linear Array tool is not active.");
  }
  if (count < kLinearArrayMinimumCount || count > maximum_count()) {
    return command_invalid_result(
        "Linear Array count exceeds the current array limit.");
  }
  if (session_->count == count) {
    return command_unchanged_result();
  }
  session_->count = count;
  session_->revision = issue_revision();
  clear_preview_cache();
  return command_changed_result();
}

CommandResult LinearArrayToolService::adjust_count(int delta) {
  if (!session_.has_value()) {
    return command_invalid_result("Linear Array tool is not active.");
  }
  const std::int64_t adjusted = static_cast<std::int64_t>(session_->count) +
                                static_cast<std::int64_t>(delta);
  const std::uint32_t clamped = static_cast<std::uint32_t>(std::clamp(
      adjusted, static_cast<std::int64_t>(kLinearArrayMinimumCount),
      static_cast<std::int64_t>(maximum_count())));
  return set_count(clamped);
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
      session_->anchor_world, session_->endpoint_world, next);
  session_->revision = issue_revision();
  clear_preview_cache();
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
      session_->anchor_world, world_position, session_->constraint);
  if (snap_to_grid) {
    endpoint = snapped_endpoint(session_->anchor_world, endpoint,
                                session_->constraint, grid_size);
  }
  if (near_same_point(session_->endpoint_world, endpoint)) {
    return command_unchanged_result();
  }
  session_->endpoint_world = endpoint;
  session_->revision = issue_revision();
  clear_preview_cache();
  return command_changed_result();
}

void LinearArrayToolService::begin_endpoint_drag() {
  if (session_.has_value() && !session_->dragging) {
    session_->dragging = true;
    session_->revision = issue_revision();
    clear_preview_cache();
  }
}

void LinearArrayToolService::end_endpoint_drag() {
  if (session_.has_value() && session_->dragging) {
    session_->dragging = false;
    session_->revision = issue_revision();
    clear_preview_cache();
  }
}

LinearArrayHandleState LinearArrayToolService::handle_state() const {
  if (!session_.has_value() || !session_sources_are_valid(*session_)) {
    return {};
  }
  return {
      .visible = true,
      .dragging = session_->dragging,
      .source_world = session_->anchor_world,
      .endpoint_world = session_->endpoint_world,
      .constraint = session_->constraint,
      .revision = session_->revision,
  };
}

const LinearArrayPreviewState &LinearArrayToolService::preview_state() const {
  if (!session_.has_value() || !session_sources_are_valid(*session_)) {
    cached_preview_revision_ = 0;
    cached_preview_ = {};
    return cached_preview_;
  }
  if (cached_preview_revision_ == session_->revision &&
      cached_preview_.active) {
    return cached_preview_;
  }

  LinearArrayPreviewState preview{
      .active = true,
      .revision = session_->revision,
  };
  preview.sources.reserve(session_->sources.size());
  const QVec3 span_world = session_->endpoint_world - session_->anchor_world;
  for (const SourceSnapshot &source : session_->sources) {
    LinearArrayLayoutResult layout = build_linear_array_layout({
        .source_world_transform = source.world_transform,
        .span_world = span_world,
        .count = session_->count,
    });
    if (!layout.ok) {
      cached_preview_revision_ = 0;
      cached_preview_ = {};
      return cached_preview_;
    }
    preview.sources.push_back({
        .source_entity = source.entity,
        .mesh = source.mesh,
        .material = source.material,
        .generated_world_transforms =
            std::move(layout.generated_world_transforms),
    });
  }
  cached_preview_revision_ = session_->revision;
  cached_preview_ = std::move(preview);
  return cached_preview_;
}

} // namespace quader
