////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/array/linear_array_tool_service.hpp"
#include "core/release_features.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "diagnostics/profile.hpp"
#include "editor/array/linked_instance_array_command.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"
#include "scene/scene_transform_pivot.hpp"

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

bool same_point(QVec3 left, QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

MaterialReference entity_material(const SceneDocument &scene,
                                  EntityId entity) {
  const MaterialComponent *material = scene.material(entity);
  return material != nullptr ? material->material : MaterialReference{};
}

MaterialAssetRevision embedded_material_revision(
    const AssetRegistry &assets, MaterialReference material) {
  return material.embedded()
             ? assets.material_revision(material.embedded_asset())
             : MaterialAssetRevision{};
}

QVec3 constrained_endpoint(QVec3 locked_endpoint, QVec3 endpoint,
                           LinearArrayAxisConstraint constraint) {
  switch (constraint) {
  case LinearArrayAxisConstraint::X:
    return {endpoint.x, locked_endpoint.y, locked_endpoint.z};
  case LinearArrayAxisConstraint::Y:
    return {locked_endpoint.x, endpoint.y, locked_endpoint.z};
  case LinearArrayAxisConstraint::Z:
    return {locked_endpoint.x, locked_endpoint.y, endpoint.z};
  case LinearArrayAxisConstraint::Free:
    break;
  }
  return endpoint;
}

QVec3 snapped_endpoint(QVec3 locked_endpoint, QVec3 endpoint,
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
  return constrained_endpoint(locked_endpoint, endpoint, constraint);
}

} // namespace

LinearArrayToolService::LinearArrayToolService(
    SceneDocument &scene, AssetRegistry &assets, EditorHistoryService &history,
    ModelingSessionService &modeling, EditorSelectionService &selection,
    SceneInstanceService &instances, LinearArraySessionDefaults *defaults)
    : scene_(scene), assets_(assets), history_(history), modeling_(modeling),
      selection_(selection), instances_(instances),
      defaults_(defaults != nullptr ? *defaults : local_defaults_) {}

void LinearArrayToolService::clear_preview_cache() {
  cached_preview_revision_ = 0;
  cached_preview_ = {};
}

std::uint64_t LinearArrayToolService::issue_revision() {
  return ++revision_counter_;
}

void LinearArrayToolService::issue_tool_settings_revision() {
  ++tool_settings_revision_counter_;
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
    std::vector<EntityId> &ordered_sources, EntityId &primary_source,
    bool &instance_occurrences) const {
  ordered_sources.clear();
  primary_source = kInvalidEntityId;
  instance_occurrences = false;
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

  instance_occurrences =
      scene_.instance_occurrence(ordered_sources.front()) != nullptr;

  for (const EntityId source : ordered_sources) {
    const TransformComponent *transform = scene_.transform(source);
    const bool active_definition_member =
        instances_.is_in_active_definition_scope(source);
    if (!scene_.exists(source) || transform == nullptr ||
        (!active_definition_member && !scene_.is_effectively_visible(source)) ||
        scene_.is_effectively_locked(source)) {
      ordered_sources.clear();
      primary_source = kInvalidEntityId;
      return false;
    }
    if (instance_occurrences) {
      EntityId definition = kInvalidEntityId;
      std::vector<InstanceArrayPreviewMeshSnapshot> meshes;
      if (!capture_instance_array_preview_meshes(
              scene_, assets_, source, definition, meshes)) {
        ordered_sources.clear();
        primary_source = kInvalidEntityId;
        return false;
      }
      continue;
    }

    const ModelingObjectComponent *modeling = scene_.modeling_object(source);
    const MeshComponent *mesh = scene_.mesh(source);
    if (modeling == nullptr || mesh == nullptr ||
        scene_.instance_occurrence(source) != nullptr ||
        modeling->object_id == kInvalidModelingObjectId ||
        modeling_.modeling_object_for_entity(source) != modeling->object_id ||
        assets_.mesh(mesh->mesh) == nullptr) {
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
  if (!release_has_feature(ReleaseFeature::LinearArray)) {
    return false;
  }
  QDR_PROFILE_SCOPE("Linear array can activate");
  const std::uint64_t scene_revision = scene_.revision();
  const std::uint64_t layer_revision = scene_.layer_revision();
  const std::uint64_t asset_revision = assets_.revision();
  const std::uint64_t object_map_revision =
      modeling_.modeling_object_map_revision();
  const std::uint64_t instance_edit_revision = instances_.instance_edit_revision();
  const int selection_mode = static_cast<int>(selection_.mode());
  if (can_activate_cache_valid_ &&
      can_activate_scene_revision_ == scene_revision &&
      can_activate_layer_revision_ == layer_revision &&
      can_activate_asset_revision_ == asset_revision &&
      can_activate_object_map_revision_ == object_map_revision &&
      can_activate_instance_edit_revision_ == instance_edit_revision &&
      can_activate_selection_mode_ == selection_mode) {
    return can_activate_cached_value_;
  }

  {
    QDR_PROFILE_SCOPE("Linear array can activate cache miss");
    can_activate_cached_value_ = can_activate_preflight();
  }
  can_activate_scene_revision_ = scene_revision;
  can_activate_layer_revision_ = layer_revision;
  can_activate_asset_revision_ = asset_revision;
  can_activate_object_map_revision_ = object_map_revision;
  can_activate_instance_edit_revision_ = instance_edit_revision;
  can_activate_selection_mode_ = selection_mode;
  can_activate_cache_valid_ = true;
  return can_activate_cached_value_;
}

bool LinearArrayToolService::can_activate_preflight() const {
  QDR_PROFILE_SCOPE("Linear array can activate structural preflight");
  if (selection_.mode() != ModelingSelectionMode::Object) {
    return false;
  }
  const std::vector<EntityId> sources = scene_.selected_entities();
  if (maximum_count_for(sources.size()) < kLinearArrayMinimumCount) {
    return false;
  }
  const EntityId primary = scene_.primary_selection();
  if (primary == kInvalidEntityId ||
      std::find(sources.begin(), sources.end(), primary) == sources.end()) {
    return false;
  }

  const bool instance_occurrences =
      scene_.instance_occurrence(sources.front()) != nullptr;
  return std::all_of(sources.begin(), sources.end(), [&](EntityId source) {
    const bool source_is_occurrence =
        scene_.instance_occurrence(source) != nullptr;
    if (source_is_occurrence != instance_occurrences) {
      return false;
    }
    const bool active_definition_member =
        instances_.is_in_active_definition_scope(source);
    if (!scene_.exists(source) || scene_.transform(source) == nullptr ||
        (!active_definition_member && !scene_.is_effectively_visible(source)) ||
        scene_.is_effectively_locked(source)) {
      return false;
    }
    if (source_is_occurrence) {
      return true;
    }
    const ModelingObjectComponent *modeling = scene_.modeling_object(source);
    const MeshComponent *mesh = scene_.mesh(source);
    return modeling != nullptr && mesh != nullptr &&
           modeling->object_id != kInvalidModelingObjectId &&
           modeling_.modeling_object_for_entity(source) == modeling->object_id &&
           assets_.mesh(mesh->mesh) != nullptr;
  });
}

std::optional<QVec3>
LinearArrayToolService::source_visible_pivot(EntityId entity) const {
  const std::optional<QVec3> scene_pivot =
      resolve_entity_transform_visible_pivot(scene_, assets_, entity);
  const std::optional<QMat4> effective_world =
      instances_.effective_member_world_transform(entity);
  if (!effective_world.has_value()) {
    return scene_pivot;
  }
  if (scene_pivot.has_value()) {
    const std::optional<QMat4> scene_world_inverse =
        try_scene_matrix_inverse(scene_.world_transform_matrix(entity));
    if (scene_world_inverse.has_value()) {
      const QVec3 effective_pivot = transform_point(
          *effective_world, transform_point(*scene_world_inverse, *scene_pivot));
      if (finite_vector(effective_pivot)) {
        return effective_pivot;
      }
    }
  }
  const TransformComponent *transform = scene_.transform(entity);
  if (transform != nullptr && transform->custom_pivot) {
    const QVec3 effective_origin = transform_point(*effective_world, {});
    return finite_vector(effective_origin) ? std::optional<QVec3>{effective_origin}
                                           : std::nullopt;
  }
  const MeshComponent *component = scene_.mesh(entity);
  const MeshAsset *mesh =
      component != nullptr ? assets_.mesh(component->mesh) : nullptr;
  if (mesh == nullptr) {
    return std::nullopt;
  }
  const std::optional<SceneBounds> bounds =
      world_bounds_from_local_mesh_bounds(*effective_world,
                                          local_mesh_bounds(*mesh));
  const QVec3 effective_pivot =
      bounds.has_value() ? bounds_center(*bounds)
                         : transform_point(*effective_world, {});
  return finite_vector(effective_pivot)
             ? std::optional<QVec3>{effective_pivot}
             : std::nullopt;
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
    const bool active_definition_member =
        instances_.is_in_active_definition_scope(source.entity);
    if (!scene_.exists(source.entity) ||
        scene_.uuid(source.entity) != source.uuid ||
        scene_.parent(source.entity) != source.parent ||
        (!active_definition_member &&
         !scene_.is_effectively_visible(source.entity)) ||
        scene_.is_effectively_locked(source.entity)) {
      return false;
    }
    const std::optional<QVec3> visible_pivot =
        source_visible_pivot(source.entity);
    if (!visible_pivot.has_value() ||
        !same_point(*visible_pivot, source.visible_pivot_world)) {
      return false;
    }
    if (session.instance_occurrences) {
      EntityId definition = kInvalidEntityId;
      std::vector<InstanceArrayPreviewMeshSnapshot> meshes;
      if (!capture_instance_array_preview_meshes(
              scene_, assets_, source.entity, definition, meshes) ||
          definition != source.instance_definition ||
          meshes.size() != source.instance_meshes.size() ||
          !same_matrix(scene_.world_transform_matrix(source.entity),
                       source.world_transform)) {
        return false;
      }
      for (std::size_t index = 0; index < meshes.size(); ++index) {
        const InstanceArrayPreviewMeshSnapshot &current = meshes[index];
        const InstanceArrayPreviewMeshSnapshot &captured =
            source.instance_meshes[index];
        if (current.member != captured.member ||
            current.mesh != captured.mesh ||
            current.material != captured.material ||
            current.mesh_revision != captured.mesh_revision ||
            current.material_revision != captured.material_revision ||
            !same_matrix(current.member_from_definition,
                         captured.member_from_definition)) {
          return false;
        }
      }
      continue;
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
        embedded_material_revision(assets_, source.material) !=
            source.material_revision ||
        !same_matrix(instances_
                         .effective_member_world_transform(source.entity)
                         .value_or(scene_.world_transform_matrix(source.entity)),
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
  return session_.has_value() ? session_->count : defaults_.count;
}

bool LinearArrayToolService::show_bounding_box_dimensions() const {
  return defaults_.show_bounding_box_dimensions;
}

std::uint32_t LinearArrayToolService::maximum_count() const {
  return session_.has_value() ? maximum_count_for(session_->sources.size())
                              : kLinearArrayMaximumCount;
}

CommandResult LinearArrayToolService::activate(double fallback_grid_size) {
  if (!release_has_feature(ReleaseFeature::LinearArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  std::vector<EntityId> ordered_sources;
  EntityId primary_source = kInvalidEntityId;
  bool instance_occurrences = false;
  if (!valid_source_selection(ordered_sources, primary_source,
                              instance_occurrences)) {
    return command_invalid_result(
        "Select visible, unlocked meshes or instance occurrences in object "
        "mode; mixed arrays are not supported.");
  }

  Session next;
  next.generation = issue_revision();
  next.revision = next.generation;
  next.primary_source = primary_source;
  next.instance_occurrences = instance_occurrences;
  next.selection_revision = scene_.selection_revision();
  next.count = std::min(defaults_.count,
                        maximum_count_for(ordered_sources.size()));
  next.sources.reserve(ordered_sources.size());
  for (const EntityId entity : ordered_sources) {
    const std::optional<QVec3> visible_pivot = source_visible_pivot(entity);
    if (!visible_pivot.has_value()) {
      return command_invalid_result(
          "Linear Array source has no valid visible pivot.");
    }
    if (instance_occurrences) {
      EntityId definition = kInvalidEntityId;
      std::vector<InstanceArrayPreviewMeshSnapshot> instance_meshes;
      if (!capture_instance_array_preview_meshes(
              scene_, assets_, entity, definition, instance_meshes)) {
        return command_invalid_result(
            "Linear Array instance source was not found.");
      }
      next.sources.push_back(SourceSnapshot{
          .entity = entity,
          .modeling_object_id = kInvalidModelingObjectId,
          .uuid = scene_.uuid(entity),
          .parent = scene_.parent(entity),
          .world_transform = scene_.world_transform_matrix(entity),
          .visible_pivot_world = *visible_pivot,
          .instance_definition = definition,
          .instance_meshes = std::move(instance_meshes),
      });
      continue;
    }
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
            embedded_material_revision(assets_,
                                       entity_material(scene_, entity)),
        .world_transform =
            instances_.effective_member_world_transform(entity).value_or(
                scene_.world_transform_matrix(entity)),
        .visible_pivot_world = *visible_pivot,
    });
  }

  next.anchor_world = next.sources.back().visible_pivot_world;
  float adjacent_step = 0.0f;
  if (const std::optional<SceneBounds> bounds = modeling_.selection_bounds()) {
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
  issue_tool_settings_revision();
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
  issue_tool_settings_revision();
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
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult LinearArrayToolService::apply() {
  if (!release_has_feature(ReleaseFeature::LinearArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_unchanged_result();
  }
  if (!session_sources_are_valid(*session_)) {
    return command_invalid_result(
        "Linear Array sources changed before the operation was applied.");
  }

  const Session committed = *session_;
  const auto finish = [this](CommandResult result) {
    if (result.ok) {
      session_.reset();
      clear_preview_cache();
      issue_tool_settings_revision();
    } else if (!result.changed) {
      // A successful command rollback restores the same sources but can advance
      // their publication revisions. Retain the draft against those restored
      // revisions so a retry does not mistake our own rollback for an edit.
      session_->selection_revision = scene_.selection_revision();
      for (SourceSnapshot &source : session_->sources) {
        if (const MeshAsset *mesh = assets_.mesh(source.mesh)) {
          source.mesh_revision = mesh->revision;
        }
      }
      validated_session_generation_ = 0;
      clear_preview_cache();
      issue_tool_settings_revision();
    }
    return result;
  };
  if (committed.instance_occurrences) {
    std::vector<LinkedInstanceArraySource> sources;
    sources.reserve(committed.sources.size());
    const TextureLockSettings texture_locks = modeling_.texture_lock_settings();
    const QVec3 span_world =
        committed.endpoint_world - committed.anchor_world;
    for (const SourceSnapshot &source : committed.sources) {
      LinearArrayLayoutResult layout = build_linear_array_layout({
          .source_world_transform = source.world_transform,
          .span_world = span_world,
          .count = committed.count,
      });
      if (!layout.ok) {
        return command_invalid_result(layout.message);
      }
      const auto &mapping_plan = source.texture_mapping_cache.resolve(
          scene_, source.entity, texture_locks, source.world_transform);
      auto mappings = mapping_plan.has_value()
          ? mapping_plan->evaluate(layout.generated_world_transforms) : std::nullopt;
      if (!mappings.has_value()) {
        return command_invalid_result("Linear Array texture placement cannot be represented.");
      }
      sources.push_back(LinkedInstanceArraySource{
          .source = source.entity,
          .generated_world_transforms =
              std::move(layout.generated_world_transforms),
          .generated_sampling_from_local = std::move(*mappings),
      });
    }
    return finish(history_.execute(std::make_unique<LinkedInstanceArrayCommand>(
        scene_, "Linear Array", std::move(sources))));
  }

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
  return finish(history_.execute(std::make_unique<LinearArrayHistoryCommand>(
      modeling_, std::move(request))));
}

CommandResult LinearArrayToolService::set_count(std::uint32_t count) {
  if (!release_has_feature(ReleaseFeature::LinearArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
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
  defaults_.count = count;
  session_->revision = issue_revision();
  clear_preview_cache();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult LinearArrayToolService::set_show_bounding_box_dimensions(
    bool enabled) {
  if (!release_has_feature(ReleaseFeature::LinearArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_invalid_result("Linear Array tool is not active.");
  }
  if (defaults_.show_bounding_box_dimensions == enabled) {
    return command_unchanged_result();
  }
  defaults_.show_bounding_box_dimensions = enabled;
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult LinearArrayToolService::reset_settings() {
  if (!release_has_feature(ReleaseFeature::LinearArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_invalid_result("Linear Array tool is not active.");
  }
  const std::uint32_t default_count =
      std::min(kLinearArrayDefaultCount, maximum_count());
  const bool geometry_changed = session_->count != default_count;
  if (!geometry_changed && defaults_ == LinearArraySessionDefaults{}) {
    return command_unchanged_result();
  }
  session_->count = default_count;
  defaults_ = {};
  if (geometry_changed) {
    session_->revision = issue_revision();
    clear_preview_cache();
  }
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult LinearArrayToolService::adjust_count(int delta) {
  if (!release_has_feature(ReleaseFeature::LinearArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
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
  if (!release_has_feature(ReleaseFeature::LinearArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
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
  session_->revision = issue_revision();
  issue_tool_settings_revision();
  clear_preview_cache();
  return command_changed_result();
}

CommandResult LinearArrayToolService::set_endpoint(QVec3 world_position,
                                                   bool snap_to_grid,
                                                   double grid_size,
                                                   LinearArrayEndpointMotion motion) {
  if (!release_has_feature(ReleaseFeature::LinearArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_invalid_result("Linear Array tool is not active.");
  }
  if (!finite_vector(world_position)) {
    return command_invalid_result("Linear Array endpoint must be finite.");
  }
  QVec3 endpoint = constrained_endpoint(
      session_->endpoint_world, world_position, session_->constraint);
  if (snap_to_grid) {
    endpoint = snapped_endpoint(session_->endpoint_world, endpoint,
                                session_->constraint, grid_size);
  }
  if (session_->constraint == LinearArrayAxisConstraint::Free) {
    if (motion == LinearArrayEndpointMotion::Horizontal) {
      endpoint.y = session_->endpoint_world.y;
    } else if (motion == LinearArrayEndpointMotion::Vertical) {
      endpoint.x = session_->endpoint_world.x;
      endpoint.z = session_->endpoint_world.z;
    }
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
  if (!release_has_feature(ReleaseFeature::LinearArray)) {
    return;
  }
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
  const TextureLockSettings texture_locks = modeling_.texture_lock_settings();
  if (cached_preview_revision_ == session_->revision &&
      cached_preview_scene_revision_ == scene_.revision() &&
      cached_preview_texture_locks_.texture_lock == texture_locks.texture_lock &&
      cached_preview_texture_locks_.texture_lock_scale == texture_locks.texture_lock_scale &&
      cached_preview_.active) {
    return cached_preview_;
  }

  LinearArrayPreviewState preview{
      .active = true,
      .revision = ++preview_generation_counter_,
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
    const auto &mapping_plan = source.texture_mapping_cache.resolve(
        scene_, source.entity, texture_locks, source.world_transform);
    auto mappings = mapping_plan.has_value()
        ? mapping_plan->evaluate(layout.generated_world_transforms)
        : std::nullopt;
    if (!mappings.has_value()) {
      // Keep an unrepresentable placement visible so acceptance can report its
      // existing transform error without destroying the editable preview.
      const auto *direct = scene_.texture_mapping(source.entity);
      mappings = std::vector<QMat4>(layout.generated_world_transforms.size(),
          direct != nullptr ? direct->sampling_from_local : QMat4{});
    }
    if (session_->instance_occurrences) {
      for (const InstanceArrayPreviewMeshSnapshot &mesh :
           source.instance_meshes) {
        std::vector<QMat4> member_world;
        member_world.reserve(layout.generated_world_transforms.size());
        for (const QMat4 &occurrence_world :
             layout.generated_world_transforms) {
          member_world.push_back(scene_transform_multiply(
              occurrence_world, mesh.member_from_definition));
        }
        const auto member_mappings = array_member_sampling(
            scene_, mesh.member, source.entity, *mappings, true);
        if (!member_mappings.has_value()) {
          cached_preview_revision_ = 0;
          cached_preview_ = {};
          return cached_preview_;
        }
        preview.sources.push_back({
            .source_entity = mesh.member,
            .visibility_entity = source.entity,
            .material_entity = mesh.member,
            .mesh = mesh.mesh,
            .material = mesh.material,
            .generated_world_transforms = std::move(member_world),
            .generated_sampling_from_local = *member_mappings,
        });
      }
      continue;
    }
    const auto scope = instances_.instance_edit_scope();
    const EntityId occurrence = scope.active() &&
            scene_.containing_instance_definition(source.entity) == scope.definition()
        ? scope.occurrence() : kInvalidEntityId;
    const auto member_mappings = array_member_sampling(
        scene_, source.entity, occurrence, *mappings, false,
        layout.generated_world_transforms);
    if (!member_mappings.has_value()) {
      cached_preview_revision_ = 0;
      cached_preview_ = {};
      return cached_preview_;
    }
    preview.sources.push_back({
        .source_entity = source.entity,
        .visibility_entity = occurrence == kInvalidEntityId ? source.entity : occurrence,
        .material_entity = source.entity,
        .mesh = source.mesh,
        .material = source.material,
        .generated_world_transforms =
            std::move(layout.generated_world_transforms),
        .generated_sampling_from_local = *member_mappings,
    });
  }
  cached_preview_revision_ = session_->revision;
  cached_preview_scene_revision_ = scene_.revision();
  cached_preview_texture_locks_ = texture_locks;
  cached_preview_ = std::move(preview);
  return cached_preview_;
}

} // namespace quader
