////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/array/radial_array_tool_service.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "diagnostics/profile.hpp"
#include "editor/array/linked_instance_array_command.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/radial_array_layout.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"
#include "scene/scene_transform_pivot.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace quader {
namespace {

constexpr float kRadialArrayToolEpsilon = 0.00001f;

bool finite_vector(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
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

double sanitized_grid_size(double grid_size) {
  return std::isfinite(grid_size) && grid_size > kRadialArrayToolEpsilon
             ? grid_size
             : 1.0;
}

} // namespace

RadialArrayToolService::RadialArrayToolService(
    SceneDocument &scene, AssetRegistry &assets, EditorHistoryService &history,
    ModelingSessionService &modeling, EditorSelectionService &selection,
    SceneInstanceService &instances)
    : scene_(scene), assets_(assets), history_(history), modeling_(modeling),
      selection_(selection), instances_(instances) {}

void RadialArrayToolService::clear_preview_cache() {
  cached_preview_revision_ = 0;
  cached_preview_ = {};
}

std::uint64_t RadialArrayToolService::issue_revision() {
  return ++revision_counter_;
}

void RadialArrayToolService::issue_tool_settings_revision() {
  ++tool_settings_revision_counter_;
}

bool RadialArrayToolService::valid_source_selection(
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
  if (std::find(ordered_sources.begin(), ordered_sources.end(),
                primary_source) == ordered_sources.end()) {
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

  ordered_sources.erase(
      std::find(ordered_sources.begin(), ordered_sources.end(),
                primary_source));
  ordered_sources.push_back(primary_source);
  return maximum_copies_for(ordered_sources.size(),
                            kRadialArrayDefaultRepeats) >=
         kRadialArrayMinimumCopies;
}

bool RadialArrayToolService::can_activate() const {
  QDR_PROFILE_SCOPE("Radial array can activate");
  const std::uint64_t scene_revision = scene_.revision();
  const int selection_mode = static_cast<int>(selection_.mode());
  if (can_activate_cache_valid_ &&
      can_activate_scene_revision_ == scene_revision &&
      can_activate_selection_mode_ == selection_mode) {
    return can_activate_cached_value_;
  }

  {
    QDR_PROFILE_SCOPE("Radial array can activate cache miss");
    can_activate_cached_value_ = can_activate_preflight();
  }
  can_activate_scene_revision_ = scene_revision;
  can_activate_selection_mode_ = selection_mode;
  can_activate_cache_valid_ = true;
  return can_activate_cached_value_;
}

bool RadialArrayToolService::can_activate_preflight() const {
  QDR_PROFILE_SCOPE("Radial array can activate structural preflight");
  if (selection_.mode() != ModelingSelectionMode::Object) {
    return false;
  }
  const std::vector<EntityId> sources = scene_.selected_entities();
  if (maximum_copies_for(sources.size(), kRadialArrayDefaultRepeats) <
      kRadialArrayMinimumCopies) {
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
    return source_is_occurrence ||
           (scene_.modeling_object(source) != nullptr &&
            scene_.mesh(source) != nullptr);
  });
}

bool RadialArrayToolService::session_sources_are_valid(
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

bool RadialArrayToolService::compute_session_sources_are_valid(
    const Session &session) const {
  if (selection_.mode() != ModelingSelectionMode::Object ||
      scene_.selection_revision() != session.selection_revision ||
      scene_.primary_selection() != session.primary_source) {
    return false;
  }
  std::vector<EntityId> selected = scene_.selected_entities();
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

bool RadialArrayToolService::active() const {
  return session_.has_value() && session_sources_are_valid(*session_);
}

bool RadialArrayToolService::center_locked() const {
  return active() && session_->center_locked;
}

double RadialArrayToolService::degrees() const {
  return session_.has_value() ? session_->degrees
                              : kRadialArrayDefaultDegrees;
}

std::uint32_t RadialArrayToolService::copies() const {
  return session_.has_value() ? session_->copies : kRadialArrayDefaultCopies;
}

std::uint32_t RadialArrayToolService::repeats() const {
  return session_.has_value() ? session_->repeats
                              : kRadialArrayDefaultRepeats;
}

double RadialArrayToolService::repeat_spacing() const {
  return session_.has_value() ? session_->repeat_spacing : 1.0;
}

std::uint32_t RadialArrayToolService::maximum_copies_for(
    std::size_t source_count, std::uint32_t repeats) {
  if (source_count == 0u || repeats == 0u ||
      source_count > kRadialArrayMaximumTotalElements / repeats) {
    return 0u;
  }
  const std::size_t maximum =
      kRadialArrayMaximumTotalElements / (source_count * repeats);
  return static_cast<std::uint32_t>(
      std::min<std::size_t>(kRadialArrayMaximumCopies, maximum));
}

std::uint32_t RadialArrayToolService::maximum_repeats_for(
    std::size_t source_count, std::uint32_t copies) {
  if (source_count == 0u || copies == 0u ||
      source_count > kRadialArrayMaximumTotalElements / copies) {
    return 0u;
  }
  const std::size_t maximum =
      kRadialArrayMaximumTotalElements / (source_count * copies);
  return static_cast<std::uint32_t>(
      std::min<std::size_t>(kRadialArrayMaximumRepeats, maximum));
}

double RadialArrayToolService::automatic_repeat_spacing(
    const Session &session) {
  if (session.center_locked && !session.sources.empty()) {
    const SourceSnapshot &primary = session.sources.back();
    const QVec3 delta = primary.visible_pivot_world - session.center_world;
    const QVec3 planar =
        delta - session.plane_normal_world *
                    dot(delta, session.plane_normal_world);
    const double radius = static_cast<double>(length(planar));
    if (std::isfinite(radius) && radius > kRadialArrayToolEpsilon) {
      return radius;
    }
  }
  return session.fallback_grid_size;
}

std::uint32_t RadialArrayToolService::maximum_copies() const {
  return session_.has_value()
             ? maximum_copies_for(session_->sources.size(), session_->repeats)
             : kRadialArrayMaximumCopies;
}

std::uint32_t RadialArrayToolService::maximum_repeats() const {
  return session_.has_value()
             ? maximum_repeats_for(session_->sources.size(), session_->copies)
             : kRadialArrayMaximumRepeats;
}

std::optional<QVec3>
RadialArrayToolService::source_visible_pivot(EntityId entity) const {
  const std::optional<QMat4> effective_world =
      instances_.effective_member_world_transform(entity);
  if (!effective_world.has_value()) {
    return resolve_entity_transform_visible_pivot(scene_, assets_, entity);
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
  return bounds.has_value() ? std::optional<QVec3>{bounds_center(*bounds)}
                            : std::optional<QVec3>{
                                  transform_point(*effective_world, {})};
}

CommandResult RadialArrayToolService::activate(double fallback_grid_size) {
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
  next.fallback_grid_size = sanitized_grid_size(fallback_grid_size);
  next.repeat_spacing = next.fallback_grid_size;
  next.copies = std::min(kRadialArrayDefaultCopies,
                         maximum_copies_for(ordered_sources.size(),
                                            kRadialArrayDefaultRepeats));
  next.sources.reserve(ordered_sources.size());
  for (const EntityId entity : ordered_sources) {
    const std::optional<QVec3> visible_pivot =
        source_visible_pivot(entity);
    if (instance_occurrences) {
      EntityId definition = kInvalidEntityId;
      std::vector<InstanceArrayPreviewMeshSnapshot> instance_meshes;
      if (!visible_pivot.has_value() ||
          !capture_instance_array_preview_meshes(
              scene_, assets_, entity, definition, instance_meshes)) {
        return command_invalid_result(
            "Radial Array instance source was not found.");
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
    if (modeling == nullptr || mesh == nullptr || mesh_asset == nullptr ||
        !visible_pivot.has_value()) {
      return command_invalid_result("Radial Array source mesh was not found.");
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
  session_ = std::move(next);
  clear_preview_cache();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::refresh() {
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
      .message = "Radial Array canceled because its sources changed.",
  };
}

CommandResult RadialArrayToolService::cancel() {
  if (!session_.has_value()) {
    return command_unchanged_result();
  }
  session_.reset();
  clear_preview_cache();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::apply() {
  if (!session_.has_value()) {
    return command_unchanged_result();
  }
  if (!session_sources_are_valid(*session_)) {
    session_.reset();
    clear_preview_cache();
    issue_tool_settings_revision();
    return command_invalid_result(
        "Radial Array sources changed before the operation was applied.");
  }
  if (!session_->center_locked) {
    return command_invalid_result("Choose a Radial Array center first.");
  }

  const Session committed = *session_;
  session_.reset();
  clear_preview_cache();
  issue_tool_settings_revision();
  if (committed.instance_occurrences) {
    std::vector<LinkedInstanceArraySource> sources;
    sources.reserve(committed.sources.size());
    for (const SourceSnapshot &source : committed.sources) {
      RadialArrayLayoutResult layout = build_radial_array_layout({
          .source_world_transform = source.world_transform,
          .source_pivot_world = source.visible_pivot_world,
          .center_world = committed.center_world,
          .plane_normal_world = committed.plane_normal_world,
          .degrees = committed.degrees,
          .copies = committed.copies,
          .repeats = committed.repeats,
          .repeat_spacing = committed.repeat_spacing,
      });
      if (!layout.ok) {
        return command_invalid_result(layout.message);
      }
      sources.push_back(LinkedInstanceArraySource{
          .source = source.entity,
          .generated_world_transforms =
              std::move(layout.generated_world_transforms),
      });
    }
    return history_.execute(std::make_unique<LinkedInstanceArrayCommand>(
        scene_, "Radial Array", std::move(sources)));
  }

  ModelingRadialArrayRequest request{
      .center_world = committed.center_world,
      .plane_normal_world = committed.plane_normal_world,
      .degrees = committed.degrees,
      .copies = committed.copies,
      .repeats = committed.repeats,
      .repeat_spacing = committed.repeat_spacing,
  };
  request.sources.reserve(committed.sources.size());
  for (const SourceSnapshot &source : committed.sources) {
    request.sources.push_back({
        .entity = source.entity,
        .modeling_object_id = source.modeling_object_id,
    });
  }
  return history_.execute(std::make_unique<RadialArrayHistoryCommand>(
      modeling_, std::move(request)));
}

CommandResult RadialArrayToolService::update_center_candidate(
    QVec3 world_position, QVec3 world_normal, double fallback_grid_size) {
  if (!session_.has_value()) {
    return command_invalid_result("Radial Array tool is not active.");
  }
  if (session_->center_locked) {
    return command_unchanged_result();
  }
  if (!finite_vector(world_position) || !finite_vector(world_normal) ||
      length_squared(world_normal) <=
          kRadialArrayToolEpsilon * kRadialArrayToolEpsilon) {
    return command_invalid_result("Radial Array center placement is invalid.");
  }
  world_normal = normalize(world_normal);
  session_->fallback_grid_size = sanitized_grid_size(fallback_grid_size);
  const bool unchanged = session_->candidate_valid &&
                         length_squared(session_->center_world -
                                        world_position) <=
                             kRadialArrayToolEpsilon * kRadialArrayToolEpsilon &&
                         length_squared(session_->plane_normal_world -
                                        world_normal) <=
                             kRadialArrayToolEpsilon * kRadialArrayToolEpsilon;
  if (unchanged) {
    return command_unchanged_result();
  }
  session_->candidate_valid = true;
  session_->center_world = world_position;
  session_->plane_normal_world = world_normal;
  session_->revision = issue_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::clear_center_candidate() {
  if (!session_.has_value() || session_->center_locked ||
      !session_->candidate_valid) {
    return command_unchanged_result();
  }
  session_->candidate_valid = false;
  session_->revision = issue_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::lock_center() {
  if (!session_.has_value()) {
    return command_invalid_result("Radial Array tool is not active.");
  }
  if (session_->center_locked) {
    return command_unchanged_result();
  }
  if (!session_->candidate_valid) {
    return command_invalid_result("Choose a valid Radial Array center.");
  }
  session_->center_locked = true;
  if (!session_->repeat_spacing_edited) {
    session_->repeat_spacing = automatic_repeat_spacing(*session_);
  }
  session_->revision = issue_revision();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::set_degrees(double degrees) {
  if (!session_.has_value()) {
    return command_invalid_result("Radial Array tool is not active.");
  }
  if (!std::isfinite(degrees) || degrees < kRadialArrayMinimumDegrees ||
      degrees > kRadialArrayMaximumDegrees) {
    return command_invalid_result(
        "Radial Array degrees must be between 0 and 360.");
  }
  if (session_->degrees == degrees) {
    return command_unchanged_result();
  }
  session_->degrees = degrees;
  session_->revision = issue_revision();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::set_copies(std::uint32_t copies) {
  if (!session_.has_value()) {
    return command_invalid_result("Radial Array tool is not active.");
  }
  const std::uint32_t maximum = maximum_copies();
  if (copies < kRadialArrayMinimumCopies || copies > maximum) {
    return command_invalid_result(
        "Radial Array copy count exceeds the current array limit.");
  }
  if (session_->copies == copies) {
    return command_unchanged_result();
  }
  session_->copies = copies;
  session_->revision = issue_revision();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::adjust_copies(int delta) {
  if (!session_.has_value()) {
    return command_invalid_result("Radial Array tool is not active.");
  }
  const std::int64_t adjusted =
      static_cast<std::int64_t>(session_->copies) +
      static_cast<std::int64_t>(delta);
  const std::uint32_t clamped = static_cast<std::uint32_t>(std::clamp(
      adjusted, static_cast<std::int64_t>(kRadialArrayMinimumCopies),
      static_cast<std::int64_t>(maximum_copies())));
  return set_copies(clamped);
}

CommandResult RadialArrayToolService::set_repeats(std::uint32_t repeats) {
  if (!session_.has_value()) {
    return command_invalid_result("Radial Array tool is not active.");
  }
  const std::uint32_t maximum = maximum_repeats();
  if (repeats < kRadialArrayMinimumRepeats || repeats > maximum) {
    return command_invalid_result(
        "Radial Array repeat count exceeds the current array limit.");
  }
  if (session_->repeats == repeats) {
    return command_unchanged_result();
  }
  session_->repeats = repeats;
  session_->revision = issue_revision();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::set_repeat_spacing(double spacing) {
  if (!session_.has_value()) {
    return command_invalid_result("Radial Array tool is not active.");
  }
  if (!std::isfinite(spacing) ||
      spacing < kRadialArrayMinimumRepeatSpacing ||
      spacing > kRadialArrayMaximumRepeatSpacing) {
    return command_invalid_result(
        "Radial Array repeat spacing must be between 0 and 16384.");
  }
  session_->repeat_spacing_edited = true;
  if (session_->repeat_spacing == spacing) {
    return command_unchanged_result();
  }
  session_->repeat_spacing = spacing;
  session_->revision = issue_revision();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::reset_settings() {
  if (!session_.has_value()) {
    return command_invalid_result("Radial Array tool is not active.");
  }
  const std::uint32_t default_repeats = kRadialArrayDefaultRepeats;
  const std::uint32_t default_copies = std::min(
      kRadialArrayDefaultCopies,
      maximum_copies_for(session_->sources.size(), default_repeats));
  const double default_spacing = automatic_repeat_spacing(*session_);
  const bool changed =
      session_->degrees != kRadialArrayDefaultDegrees ||
      session_->copies != default_copies ||
      session_->repeats != default_repeats ||
      session_->repeat_spacing != default_spacing ||
      session_->repeat_spacing_edited;
  if (!changed) {
    return command_unchanged_result();
  }
  session_->degrees = kRadialArrayDefaultDegrees;
  session_->copies = default_copies;
  session_->repeats = default_repeats;
  session_->repeat_spacing = default_spacing;
  session_->repeat_spacing_edited = false;
  session_->revision = issue_revision();
  issue_tool_settings_revision();
  return command_changed_result();
}

RadialArrayCenterState RadialArrayToolService::center_state() const {
  if (!session_.has_value() || !session_sources_are_valid(*session_) ||
      !session_->candidate_valid) {
    return {};
  }
  return {
      .visible = true,
      .locked = session_->center_locked,
      .world_position = session_->center_world,
      .world_normal = session_->plane_normal_world,
      .revision = session_->revision,
  };
}

const RadialArrayPreviewState &RadialArrayToolService::preview_state() const {
  if (!session_.has_value() || !session_sources_are_valid(*session_) ||
      !session_->center_locked) {
    cached_preview_revision_ = 0;
    cached_preview_ = {};
    return cached_preview_;
  }
  if (cached_preview_revision_ == session_->revision &&
      cached_preview_.active) {
    return cached_preview_;
  }
  RadialArrayPreviewState preview{
      .active = true,
      .revision = session_->revision,
  };
  preview.sources.reserve(session_->sources.size());
  for (const SourceSnapshot &source : session_->sources) {
    const RadialArrayLayoutResult layout = build_radial_array_layout({
        .source_world_transform = source.world_transform,
        .source_pivot_world = source.visible_pivot_world,
        .center_world = session_->center_world,
        .plane_normal_world = session_->plane_normal_world,
        .degrees = session_->degrees,
        .copies = session_->copies,
        .repeats = session_->repeats,
        .repeat_spacing = session_->repeat_spacing,
    });
    if (!layout.ok) {
      cached_preview_revision_ = 0;
      cached_preview_ = {};
      return cached_preview_;
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
        preview.sources.push_back({
            .source_entity = source.entity,
            .material_entity = mesh.member,
            .mesh = mesh.mesh,
            .material = mesh.material,
            .generated_world_transforms = std::move(member_world),
        });
      }
      continue;
    }
    preview.sources.push_back({
        .source_entity = source.entity,
        .material_entity = source.entity,
        .mesh = source.mesh,
        .material = source.material,
        .generated_world_transforms = layout.generated_world_transforms,
    });
  }
  cached_preview_revision_ = session_->revision;
  cached_preview_ = std::move(preview);
  return cached_preview_;
}

} // namespace quader
