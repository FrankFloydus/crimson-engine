////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/array/radial_array_tool_service.hpp"
#include "core/release_features.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "diagnostics/profile.hpp"
#include "editor/array/linked_instance_array_command.hpp"
#include "editor/commands/scene_structural_lifecycle_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/radial_array_layout.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/selection/checked_selection_publication_command.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"
#include "scene/scene_transform_pivot.hpp"

#include <algorithm>
#include <array>
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
    SceneInstanceService &instances,
    SceneStructuralLifecycleService &structural_lifecycle,
    RadialArraySessionDefaults *defaults)
    : scene_(scene), assets_(assets), history_(history), modeling_(modeling),
      selection_(selection), instances_(instances),
      structural_lifecycle_(structural_lifecycle),
      defaults_(defaults != nullptr ? *defaults : local_defaults_) {}

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
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    return false;
  }
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
                              : defaults_.degrees;
}

std::uint32_t RadialArrayToolService::copies() const {
  return session_.has_value() ? session_->copies : defaults_.copies;
}

std::uint32_t RadialArrayToolService::repeats() const {
  return session_.has_value() ? session_->repeats
                              : defaults_.repeats;
}

double RadialArrayToolService::repeat_spacing() const {
  return session_.has_value() ? session_->repeat_spacing
                              : defaults_.repeat_spacing.value_or(1.0);
}

bool RadialArrayToolService::show_bounding_box_dimensions() const {
  return defaults_.show_bounding_box_dimensions;
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

bool RadialArrayToolService::has_valid_center() const {
  return active() && session_->candidate_valid;
}

RadialArrayMode RadialArrayToolService::mode() const {
  return session_.has_value() ? session_->mode : defaults_.mode;
}

bool RadialArrayToolService::create_instances() const {
  return session_.has_value() ? session_->create_instances
                              : defaults_.create_instances;
}

double RadialArrayToolService::automatic_repeat_spacing(
    const Session &session) {
  if (session.candidate_valid && !session.sources.empty()) {
    const SourceSnapshot &primary = session.sources.back();
    const QVec3 delta = primary.visible_pivot_world - session.center_world;
    const QVec3 planar =
        delta - session.plane_normal_world *
                    dot(delta, session.plane_normal_world);
    const double radius = static_cast<double>(length(planar));
    if (std::isfinite(radius) && radius > kRadialArrayToolEpsilon) {
      return std::min(radius, kRadialArrayMaximumRepeatSpacing);
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
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
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
  next.fallback_grid_size = sanitized_grid_size(fallback_grid_size);
  next.repeat_spacing =
      defaults_.repeat_spacing.value_or(next.fallback_grid_size);
  next.repeat_spacing_edited = defaults_.repeat_spacing.has_value();
  next.mode = defaults_.mode;
  next.degrees = defaults_.degrees;
  next.create_instances = defaults_.create_instances;
  // A source-capacity reduction affects only this operation, not authored
  // options used by subsequent operations with a smaller selection.
  next.copies = std::min(defaults_.copies,
                         maximum_copies_for(ordered_sources.size(),
                                            kRadialArrayDefaultRepeats));
  next.repeats = std::min(defaults_.repeats,
                         maximum_repeats_for(ordered_sources.size(),
                                             next.copies));
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
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_unchanged_result();
  }
  if (!session_sources_are_valid(*session_)) {
    return command_invalid_result(
        "Radial Array sources changed before the operation was applied.");
  }
  if (!session_->candidate_valid) {
    return command_invalid_result("Choose a Radial Array center first.");
  }

  const Session committed = *session_;
  CommandResult result;
  if (committed.instance_occurrences || committed.create_instances) {
    result = apply_instance_array(committed);
  } else {
    ModelingRadialArrayRequest request{
        .center_world = committed.center_world,
        .plane_normal_world = committed.plane_normal_world,
        .degrees = committed.degrees,
        .copies = committed.copies,
        .repeats = committed.repeats,
        .repeat_spacing = committed.repeat_spacing,
        .mode = committed.mode,
    };
    request.sources.reserve(committed.sources.size());
    for (const SourceSnapshot &source : committed.sources) {
      request.sources.push_back({
          .entity = source.entity,
          .modeling_object_id = source.modeling_object_id,
      });
    }
    result = history_.execute(std::make_unique<RadialArrayHistoryCommand>(
        modeling_, std::move(request)));
  }
  if (result.ok && result.changed) {
    session_.reset();
    clear_preview_cache();
    issue_tool_settings_revision();
  }
  return result;
}

CommandResult RadialArrayToolService::finish() {
  if (!session_) {
    return command_unchanged_result();
  }
  if (!session_sources_are_valid(*session_)) {
    return command_invalid_result(
        "Radial Array sources changed before the operation was applied.");
  }
  // Source validity and center presence are separate: a stale authored preview
  // must not be mistaken for an empty tool and silently discarded.
  return session_->candidate_valid ? apply() : cancel();
}

CommandResult RadialArrayToolService::apply_instance_array(
    const Session &session) {
  if (session.create_instances && !session.instance_occurrences &&
      std::ranges::any_of(session.sources, [this](const SourceSnapshot &source) {
        return scene_.has_children(source.entity);
      })) {
    // Independent arrays copy only selected meshes. A visible occurrence cannot
    // retain their ordinary children, so conversion must not absorb an unseen
    // subtree or change the authored hierarchy.
    return command_invalid_result(
        "Create Instances requires source meshes without child entities. "
        "Disable Create Instances to keep the existing hierarchy.");
  }
  std::vector<LinkedInstanceArraySource> sources;
  sources.reserve(session.sources.size());
  const TextureLockSettings texture_locks = modeling_.texture_lock_settings();
  for (const SourceSnapshot &source : session.sources) {
    RadialArrayLayoutResult layout = build_radial_array_layout({
        .source_world_transform = source.world_transform,
        .source_pivot_world = source.visible_pivot_world,
        .center_world = session.center_world,
        .plane_normal_world = session.plane_normal_world,
        .degrees = session.degrees,
        .copies = session.copies,
        .repeats = session.repeats,
        .repeat_spacing = session.repeat_spacing,
        .mode = session.mode,
    });
    if (!layout.ok) return command_invalid_result(layout.message);
    const auto &mapping_plan = source.texture_mapping_cache.resolve(
        scene_, source.entity, texture_locks, source.world_transform);
    auto mappings = mapping_plan.has_value()
        ? mapping_plan->evaluate(layout.generated_world_transforms) : std::nullopt;
    if (!mappings.has_value()) {
      return command_invalid_result("Radial Array texture placement cannot be represented.");
    }
    sources.push_back({source.entity,
                       std::move(layout.generated_world_transforms),
                       std::move(*mappings)});
  }

  SceneStructuralCompensationScope compensation =
      structural_lifecycle_.begin_compensation_scope();
  if (!compensation.valid()) {
    return command_invalid_result("Scene scalar compensation is unavailable.");
  }
  const DocumentSelectionState selection_before =
      document_command_capture_selection(scene_);
  const ModelingSessionMemento modeling_before =
      modeling_.capture_history_memento();
  std::optional<ModelingAssetProjectionCheckpoint> assets_before;
  std::vector<AssetId> created_mesh_assets;
  if (!session.create_instances) {
    std::vector<AssetId> source_mesh_assets;
    for (const SourceSnapshot &source : session.sources) {
      for (const InstanceArrayPreviewMeshSnapshot &mesh : source.instance_meshes) {
        source_mesh_assets.push_back(mesh.mesh);
      }
    }
    std::ranges::sort(source_mesh_assets);
    source_mesh_assets.erase(
        std::unique(source_mesh_assets.begin(), source_mesh_assets.end()),
        source_mesh_assets.end());
    assets_before = assets_.capture_modeling_projection_checkpoint(
        source_mesh_assets);
    if (!assets_before->valid()) {
      return command_invalid_result("Radial Array asset checkpoint is unavailable.");
    }
  }
  EditorTransaction transaction = history_.begin_transaction("Radial Array");
  if (!transaction.active()) {
    return command_invalid_result(
        "Cannot apply Radial Array while another transaction is active.");
  }
  const auto rollback = [&](CommandResult failure) {
    if (transaction.active() && transaction.empty() && !failure.changed) {
      // A rejected preflight published nothing. Replaying a full modeling
      // memento here would unnecessarily reproject untouched instance frames.
      (void)transaction.cancel();
      (void)compensation.release();
      failure.ok = false;
      return failure;
    }
    if (!compensation.seal_forward() || !compensation.open_inverse()) {
      (void)compensation.release();
      const CommandResult retained = transaction.active()
                                         ? transaction.commit()
                                         : command_unchanged_result();
      failure.ok = false;
      failure.changed = true;
      failure.message += "; Radial Array rollback boundary was invalidated";
      if (!retained.ok) failure.message += "; " + retained.message;
      return failure;
    }
    const CommandResult canceled = transaction.active()
                                       ? transaction.cancel()
                                       : command_unchanged_result();
    const ModelingCommandResult restored =
        modeling_.restore_history_memento_exact_for_failure_compensation(
            modeling_before);
    document_command_apply_selection(scene_, selection_before);
    const bool restored_selection = document_command_same_selection(
        selection_before, document_command_capture_selection(scene_));
    // An empty lease means preflight rejected or the inner publication already
    // compensated its allocations. Its monotonic allocation fence must not be
    // replayed as a new, empty exclusive lease.
    const bool restored_assets =
        canceled.ok && restored.ok &&
        (!assets_before.has_value() || created_mesh_assets.empty() ||
         assets_.restore_modeling_projection_checkpoint(*assets_before,
                                                        created_mesh_assets));
    const bool restored_all = restored_assets && restored_selection;
    const bool restored_scalars = restored_all && compensation.restore();
    failure.ok = false;
    failure.changed = !restored_scalars;
    if (!restored_scalars) {
      (void)compensation.release();
      failure.message += "; Radial Array rollback failed";
      if (!canceled.ok) failure.message += ": " + canceled.message;
      if (!restored.ok) failure.message += ": " + restored.message;
      if (!restored_assets) failure.message += "; asset compensation failed";
    }
    return failure;
  };

  if (!session.instance_occurrences) {
    for (std::size_t index = 0; index < sources.size(); ++index) {
      const SourceSnapshot &source = session.sources[index];
      const std::optional<QMat4> source_inverse =
          try_scene_matrix_inverse(source.world_transform);
      if (!source_inverse.has_value()) {
        return rollback(command_invalid_result(
            "Radial Array source transform cannot be inverted."));
      }
      const std::array<EntityId, 1> root{source.entity};
      const SceneInstanceMutationResult converted =
          instances_.create_instance_from_roots_in_transaction(transaction, root);
      if (!converted.ok || !converted.changed) {
        return rollback(converted.command_result());
      }
      sources[index].source = converted.occurrence;
      const QMat4 occurrence_world =
          scene_.world_transform_matrix(converted.occurrence);
      const auto occurrence_from_world = try_scene_matrix_inverse(occurrence_world);
      const auto *member_mapping = scene_.texture_mapping(source.entity);
      const auto inverse_member_mapping = try_scene_matrix_inverse(
          member_mapping != nullptr ? member_mapping->sampling_from_local : QMat4{});
      if (!occurrence_from_world.has_value() || !inverse_member_mapping.has_value()) {
        return rollback(command_invalid_result("Radial Array instance texture frame is invalid."));
      }
      const QMat4 occurrence_from_member = scene_transform_multiply(
          *occurrence_from_world, source.world_transform);
      const auto member_from_occurrence = try_scene_matrix_inverse(occurrence_from_member);
      if (!member_from_occurrence.has_value()) {
        return rollback(command_invalid_result("Radial Array member texture frame is invalid."));
      }
      // Conversion retains the member chart while recentering its occurrence.
      // Solve its correction from the already previewed effective mesh chart.
      for (QMat4 &mapping : sources[index].generated_sampling_from_local) {
        mapping = scene_transform_multiply(
            scene_transform_multiply(occurrence_from_member, *inverse_member_mapping),
            scene_transform_multiply(mapping, *member_from_occurrence));
      }
      // Conversion recenters the occurrence. Preserve each preview placement
      // delta, including its captured visible pivot and radial repeat offset.
      for (QMat4 &world : sources[index].generated_world_transforms) {
        world = scene_transform_multiply(
            scene_transform_multiply(world, *source_inverse), occurrence_world);
      }
    }
  }
  std::vector<EntityId> final_selection;
  final_selection.reserve(sources.size() * session.copies * session.repeats);
  for (const LinkedInstanceArraySource &source : sources) {
    final_selection.push_back(source.source);
  }
  if (session.create_instances) {
    auto command = std::make_unique<LinkedInstanceArrayCommand>(
        scene_, "Radial Array", std::move(sources));
    const auto *generated_command = command.get();
    const CommandResult duplicated = transaction.execute(
        with_checked_selection_publication(
            scene_, std::move(command),
            [this]() { return selection_.publish_scene_selection(); }));
    if (!duplicated.ok || !duplicated.changed) return rollback(duplicated);
    const std::vector<EntityId> &generated =
        generated_command->generated_occurrences();
    final_selection.insert(final_selection.end(), generated.begin(),
                            generated.end());
  } else {
    std::vector<SceneInstanceMaterializationRequest> requests;
    for (const LinkedInstanceArraySource &source : sources) {
      for (std::size_t index = 0; index < source.generated_world_transforms.size(); ++index) {
        requests.push_back({.occurrence = source.source,
                            .world_transform = source.generated_world_transforms[index],
                            .occurrence_sampling_from_local =
                                source.generated_sampling_from_local[index]});
      }
    }
    const SceneInstanceMutationResult materialized =
        instances_.materialize_instance_copies_in_transaction(transaction,
                                                              requests);
    created_mesh_assets = materialized.created_mesh_assets;
    if (!materialized.ok || !materialized.changed) {
      return rollback(materialized.command_result());
    }
    final_selection.insert(final_selection.end(), materialized.entities.begin(),
                            materialized.entities.end());
  }
  const CommandResult selected = selection_.set_inspection_scene_selection(
      final_selection, final_selection[session.sources.size() - 1u]);
  if (!selected.ok) return rollback(selected);
  if (!compensation.seal_forward()) {
    return rollback(command_invalid_result(
        "Radial Array scalar compensation boundary could not be acknowledged."));
  }
  const CommandResult committed = transaction.commit();
  if (!committed.ok || !committed.changed) return rollback(committed);
  if (!compensation.open_inverse() || !compensation.release()) {
    return {.ok = false, .changed = true,
            .message = "Scene changed while Radial Array history was committed."};
  }
  return committed;
}

CommandResult RadialArrayToolService::update_center_candidate(
    QVec3 world_position, QVec3 world_normal, double fallback_grid_size) {
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
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
  const double grid_size = sanitized_grid_size(fallback_grid_size);
  const bool unchanged = session_->candidate_valid &&
                         session_->fallback_grid_size == grid_size &&
                         length_squared(session_->center_world -
                                        world_position) <=
                             kRadialArrayToolEpsilon * kRadialArrayToolEpsilon &&
                         length_squared(session_->plane_normal_world -
                                        world_normal) <=
                             kRadialArrayToolEpsilon * kRadialArrayToolEpsilon;
  if (unchanged) {
    return command_unchanged_result();
  }
  const bool first_candidate = !session_->candidate_valid;
  const double previous_spacing = session_->repeat_spacing;
  session_->fallback_grid_size = grid_size;
  session_->candidate_valid = true;
  session_->center_world = world_position;
  session_->plane_normal_world = world_normal;
  if (!session_->repeat_spacing_edited) {
    session_->repeat_spacing = automatic_repeat_spacing(*session_);
  }
  session_->revision = issue_revision();
  if (first_candidate || previous_spacing != session_->repeat_spacing) {
    issue_tool_settings_revision();
  }
  return command_changed_result();
}

CommandResult RadialArrayToolService::clear_center_candidate() {
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value() || session_->center_locked ||
      !session_->candidate_valid) {
    return command_unchanged_result();
  }
  session_->candidate_valid = false;
  session_->revision = issue_revision();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::lock_center() {
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
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
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
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
  defaults_.degrees = degrees;
  session_->revision = issue_revision();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::set_mode(RadialArrayMode mode) {
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_invalid_result("Radial Array tool is not active.");
  }
  if (mode != RadialArrayMode::Total && mode != RadialArrayMode::Spacing) {
    return command_invalid_result("Radial Array distribution mode is invalid.");
  }
  if (session_->mode == mode) return command_unchanged_result();
  session_->mode = mode;
  defaults_.mode = mode;
  session_->revision = issue_revision();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::set_create_instances(bool enabled) {
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_invalid_result("Radial Array tool is not active.");
  }
  if (session_->create_instances == enabled) return command_unchanged_result();
  session_->create_instances = enabled;
  defaults_.create_instances = enabled;
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::set_copies(std::uint32_t copies) {
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
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
  defaults_.copies = copies;
  session_->revision = issue_revision();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::adjust_copies(int delta) {
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
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
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
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
  defaults_.repeats = repeats;
  session_->revision = issue_revision();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::adjust_repeats(int delta) {
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_invalid_result("Radial Array tool is not active.");
  }
  const std::int64_t adjusted =
      static_cast<std::int64_t>(session_->repeats) +
      static_cast<std::int64_t>(delta);
  const std::uint32_t clamped = static_cast<std::uint32_t>(std::clamp(
      adjusted, static_cast<std::int64_t>(kRadialArrayMinimumRepeats),
      static_cast<std::int64_t>(maximum_repeats())));
  return set_repeats(clamped);
}

CommandResult RadialArrayToolService::set_repeat_spacing(double spacing) {
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_invalid_result("Radial Array tool is not active.");
  }
  if (!std::isfinite(spacing) ||
      spacing < kRadialArrayMinimumRepeatSpacing ||
      spacing > kRadialArrayMaximumRepeatSpacing) {
    return command_invalid_result(
        "Radial Array repeat spacing must be between 0 and 16384.");
  }
  if (session_->repeat_spacing == spacing &&
      session_->repeat_spacing_edited) {
    return command_unchanged_result();
  }
  session_->repeat_spacing = spacing;
  session_->repeat_spacing_edited = true;
  defaults_.repeat_spacing = spacing;
  session_->revision = issue_revision();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::set_show_bounding_box_dimensions(
    bool enabled) {
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_invalid_result("Radial Array tool is not active.");
  }
  if (defaults_.show_bounding_box_dimensions == enabled) {
    return command_unchanged_result();
  }
  defaults_.show_bounding_box_dimensions = enabled;
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult RadialArrayToolService::reset_settings() {
  if (!release_has_feature(ReleaseFeature::RadialArray)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_invalid_result("Radial Array tool is not active.");
  }
  const std::uint32_t default_repeats = kRadialArrayDefaultRepeats;
  const std::uint32_t default_copies = std::min(
      kRadialArrayDefaultCopies,
      maximum_copies_for(session_->sources.size(), default_repeats));
  const double default_spacing = automatic_repeat_spacing(*session_);
  const bool geometry_changed =
      session_->degrees != kRadialArrayDefaultDegrees ||
      session_->copies != default_copies ||
      session_->repeats != default_repeats ||
      session_->repeat_spacing != default_spacing ||
      session_->repeat_spacing_edited ||
      session_->mode != RadialArrayMode::Total;
  const bool changed = geometry_changed || session_->create_instances ||
      defaults_ != RadialArraySessionDefaults{};
  if (!changed) {
    return command_unchanged_result();
  }
  session_->degrees = kRadialArrayDefaultDegrees;
  session_->copies = default_copies;
  session_->repeats = default_repeats;
  session_->repeat_spacing = default_spacing;
  session_->repeat_spacing_edited = false;
  session_->mode = RadialArrayMode::Total;
  session_->create_instances = false;
  defaults_ = {};
  if (geometry_changed) {
    session_->revision = issue_revision();
  }
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
      !session_->candidate_valid) {
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
  RadialArrayPreviewState preview{
      .active = true,
      .revision = ++preview_generation_counter_,
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
        .mode = session_->mode,
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
        .generated_world_transforms = layout.generated_world_transforms,
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
