////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/array/path_array_tool_service.hpp"
#include "core/release_features.hpp"
#include "editor/metadata/release_feature_requirements.hpp"

#include "assets/asset_registry.hpp"
#include "assets/curve_asset.hpp"
#include "core/i_undoable_command.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/document/document_command_edits.hpp"
#include "editor/commands/document/scene_texture_mapping_plan.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/commands/scene_structural_lifecycle_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/checked_selection_publication_command.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_hierarchy_visibility.hpp"
#include "scene/scene_texture_mapping.hpp"
#include "scene/scene_transform.hpp"
#include "scene/scene_transform_pivot.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

constexpr double kPathArrayDistanceEpsilon = 0.000000001;

bool same_matrix(const QMat4 &left, const QMat4 &right) {
  return left.values == right.values;
}

bool near_matrix(const QMat4 &left, const QMat4 &right) {
  for (std::size_t index = 0U; index < left.values.size(); ++index) {
    const float tolerance = 0.00001F *
        std::max({1.0F, std::abs(left.values[index]),
                  std::abs(right.values[index])});
    if (std::abs(left.values[index] - right.values[index]) > tolerance) {
      return false;
    }
  }
  return true;
}

bool same_vector(QVec3 left, QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool same_transform(const TransformComponent &left,
                    const TransformComponent &right) {
  return same_vector(left.position, right.position) &&
         same_vector(left.rotation_euler, right.rotation_euler) &&
         same_vector(left.scale, right.scale) &&
         left.custom_pivot == right.custom_pivot &&
         left.parenting_offset.values == right.parenting_offset.values;
}

bool finite_vector(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

QVec3 componentwise_minimum(QVec3 left, QVec3 right) {
  return {std::min(left.x, right.x), std::min(left.y, right.y),
          std::min(left.z, right.z)};
}

QVec3 componentwise_maximum(QVec3 left, QVec3 right) {
  return {std::max(left.x, right.x), std::max(left.y, right.y),
          std::max(left.z, right.z)};
}

std::optional<QMat4> source_local_variation_frame(
    const SceneDocument &scene, EntityId source,
    const TransformComponent &local_transform) {
  const EntityId parent = scene.parent(source);
  const QMat4 parent_world =
      parent == kInvalidEntityId ? scene_transform_identity_matrix()
                                 : scene.world_transform_matrix(parent);
  if (!scene_matrix_is_finite(parent_world) ||
      !scene_transform_is_finite(local_transform)) {
    return std::nullopt;
  }
  const glm::mat3 parent_linear{to_glm(
      scene_compensated_parent_matrix(parent_world, local_transform))};
  const glm::mat3 local_rotation = glm::mat3_cast(
      to_glm(quat_from_euler_radians(local_transform.rotation_euler)));
  const QMat4 frame = from_glm(glm::mat4{parent_linear * local_rotation});
  return scene_matrix_is_finite(frame) ? std::optional<QMat4>{frame}
                                       : std::nullopt;
}

std::uint64_t path_array_variation_seed(std::string_view path_uuid,
                                        std::string_view source_uuid) {
  constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
  constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
  std::uint64_t hash = kFnvOffset;
  const auto append = [&](std::string_view value) {
    for (const unsigned char byte : value) {
      hash ^= static_cast<std::uint64_t>(byte);
      hash *= kFnvPrime;
    }
    hash ^= 0xffU;
    hash *= kFnvPrime;
  };
  append("quader.path_array.variation.v1");
  append(path_uuid);
  append(source_uuid);
  return path_array_layout_detail::splitmix64(hash);
}

modeling::Transform3 sdk_transform(const QMat4 &value) {
  const auto &matrix = value.values;
  return {
      .x_axis = {matrix[0], matrix[1], matrix[2]},
      .y_axis = {matrix[4], matrix[5], matrix[6]},
      .z_axis = {matrix[8], matrix[9], matrix[10]},
      .origin = {matrix[12], matrix[13], matrix[14]},
  };
}

QVec3 native_vector(modeling::Vec3 value) {
  return {value.x, value.y, value.z};
}

MaterialComponent entity_material(const SceneDocument &scene,
                                  EntityId entity) {
  const MaterialComponent *material = scene.material(entity);
  return material != nullptr ? *material : MaterialComponent{};
}

void append_material_revision(
    const AssetRegistry &assets, MaterialReference material,
    std::vector<PathArrayMaterialRevisionKey> &revisions) {
  if (!material.embedded()) {
    return;
  }
  revisions.push_back({
      .asset = material.embedded_asset(),
      .revision = assets.material_revision(material.embedded_asset()),
  });
}

std::vector<PathArrayMaterialRevisionKey>
material_revision_keys(const AssetRegistry &assets,
                       const MaterialComponent *material) {
  std::vector<PathArrayMaterialRevisionKey> revisions;
  if (material == nullptr) {
    return revisions;
  }
  revisions.reserve(material->material_slots.size() + 1U);
  append_material_revision(assets, material->material, revisions);
  for (const auto &[slot, reference] : material->material_slots) {
    (void)slot;
    append_material_revision(assets, reference, revisions);
  }
  std::ranges::sort(revisions, [](const auto &left, const auto &right) {
    return left.asset < right.asset;
  });
  revisions.erase(
      std::unique(revisions.begin(), revisions.end(),
                  [](const auto &left, const auto &right) {
                    return left.asset == right.asset;
                  }),
      revisions.end());
  return revisions;
}

std::uint64_t preview_layout_key(std::uint64_t kind, EntityId source,
                                 EntityId visibility) {
  constexpr std::uint64_t kMix = 0x9e3779b97f4a7c15ULL;
  std::uint64_t value = kind + kMix;
  value ^= static_cast<std::uint64_t>(source) + kMix + (value << 6U) +
           (value >> 2U);
  value ^= static_cast<std::uint64_t>(visibility) + kMix + (value << 6U) +
           (value >> 2U);
  return value;
}

std::pair<QVec3, QVec3> marker_bounds_in_root_space(
    const SceneDocument &scene, const AssetRegistry &assets, EntityId root,
    const QMat4 &root_inverse) {
  const std::optional<SceneBounds> world =
      entity_hierarchy_world_bounds(scene, assets, root);
  if (!world.has_value()) {
    return {{}, {}};
  }
  const std::array<QVec3, 8U> corners{{
      {world->min.x, world->min.y, world->min.z},
      {world->max.x, world->min.y, world->min.z},
      {world->min.x, world->max.y, world->min.z},
      {world->max.x, world->max.y, world->min.z},
      {world->min.x, world->min.y, world->max.z},
      {world->max.x, world->min.y, world->max.z},
      {world->min.x, world->max.y, world->max.z},
      {world->max.x, world->max.y, world->max.z},
  }};
  QVec3 minimum = transform_point(root_inverse, corners.front());
  QVec3 maximum = minimum;
  for (std::size_t index = 1U; index < corners.size(); ++index) {
    const QVec3 local = transform_point(root_inverse, corners[index]);
    minimum = {std::min(minimum.x, local.x), std::min(minimum.y, local.y),
               std::min(minimum.z, local.z)};
    maximum = {std::max(maximum.x, local.x), std::max(maximum.y, local.y),
               std::max(maximum.z, local.z)};
  }
  return {minimum, maximum};
}

/**
 * Anchors the history item to the exact Curve selection captured at activation.
 *
 * The command is inserted before structural duplication. It therefore runs
 * last on undo and first on redo; both directions intentionally publish the
 * same baseline before duplicate commands rebuild their own selection chain.
 */
class PathArrayCurveSelectionMementoCommand final : public IUndoableCommand {
public:
  PathArrayCurveSelectionMementoCommand(
      EditorSelectionService &selection,
      EditorSelectionBoxBaseline baseline)
      : selection_(selection), baseline_(std::move(baseline)) {}

  [[nodiscard]] const std::string &label() const override { return label_; }
  CommandResult undo() override { return apply(); }
  CommandResult redo() override { return apply(); }

private:
  CommandResult apply_unchecked(const EditorSelectionBoxBaseline &baseline) {
    bool changed = false;
    ModelingCommandResult mode = selection_.set_mode(baseline.mode);
    if (!mode.ok) {
      return {.ok = false,
              .changed = mode.changed,
              .message = std::move(mode.message)};
    }
    changed = changed || mode.changed;
    DocumentCommandResult scene = selection_.synchronize_scene_selection(
        baseline.scene_selection, baseline.active_entity);
    if (!scene.ok) {
      return {.ok = false,
              .changed = changed || scene.changed,
              .message = std::move(scene.message)};
    }
    changed = changed || scene.changed;
    ModelingCommandResult sub =
        selection_.restore_registered_sub_selection_baseline(
            baseline.sub_selection);
    if (!sub.ok) {
      return {.ok = false,
              .changed = changed || sub.changed,
              .message = std::move(sub.message)};
    }
    return changed || sub.changed ? command_changed_result()
                                  : command_unchanged_result();
  }

  CommandResult apply() {
    const EditorSelectionBoxBaseline before =
        selection_.capture_box_selection_baseline();
    CommandResult result = apply_unchecked(baseline_);
    if (result.ok) {
      return result;
    }
    const CommandResult restored = apply_unchecked(before);
    if (!restored.ok) {
      result.changed = true;
      if (!result.message.empty()) {
        result.message += " ";
      }
      result.message +=
          "Path Array selection memento rollback also failed.";
    }
    return result;
  }

  EditorSelectionService &selection_;
  EditorSelectionBoxBaseline baseline_;
  std::string label_ = "Path Array";
};

} // namespace

PathArrayToolService::PathArrayToolService(
    SceneDocument &scene, AssetRegistry &assets, EditorHistoryService &history,
    ModelingSessionService &modeling, EditorSelectionService &selection,
    DocumentCommandService &document_commands,
    SceneStructuralLifecycleService &structural_lifecycle,
    PathArraySessionDefaults &defaults)
    : scene_(scene), assets_(assets), history_(history), modeling_(modeling),
      selection_(selection), document_commands_(document_commands),
      structural_lifecycle_(structural_lifecycle), defaults_(defaults) {}

std::uint64_t PathArrayToolService::issue_revision() {
  return ++revision_counter_;
}

void PathArrayToolService::issue_tool_settings_revision() {
  ++tool_settings_revision_;
}

PathArraySessionDefaults &PathArrayToolService::editable_defaults() {
  if (!session_.has_value() || !session_->source.has_value()) {
    return defaults_;
  }
  if (!pending_defaults_.has_value()) {
    pending_defaults_ = defaults_;
  }
  return *pending_defaults_;
}

void PathArrayToolService::publish_pending_defaults() const {
  if (pending_defaults_.has_value()) {
    defaults_ = *pending_defaults_;
    pending_defaults_.reset();
  }
}

void PathArrayToolService::invalidate_preview() {
  cached_preview_attempted_ = false;
  cached_preview_current_valid_ = false;
}

void PathArrayToolService::clear_preview() {
  invalidate_preview();
  cached_preview_ = {};
  cached_layout_backbone_key_.reset();
  cached_layout_backbone_.reset();
}

double PathArrayToolService::fit_distance(double path_length, bool closed,
                                          std::uint32_t count) {
  if (!std::isfinite(path_length) || path_length <= 0.0 ||
      count < kPathArrayMinimumCount) {
    return 0.0;
  }
  const double intervals =
      static_cast<double>(closed ? count : count - 1U);
  return path_length / intervals;
}

std::optional<PathArrayToolService::PathSnapshot>
PathArrayToolService::capture_selected_path() const {
  if (selection_.mode() != ModelingSelectionMode::Object) {
    return std::nullopt;
  }
  const std::vector<EntityId> selected = scene_.selected_entities();
  if (selected.size() != 1U || scene_.primary_selection() != selected.front()) {
    return std::nullopt;
  }
  const EntityId entity = selected.front();
  const CurveComponent *component = scene_.curve(entity);
  const TransformComponent *transform = scene_.transform(entity);
  const ModelingObjectComponent *modeling = scene_.modeling_object(entity);
  const CurveAsset *asset =
      component != nullptr ? assets_.curve(component->curve) : nullptr;
  if (!scene_.exists(entity) || scene_.kind(entity) != EntityKind::Curve ||
      component == nullptr || transform == nullptr || modeling == nullptr ||
      modeling->object_id == kInvalidModelingObjectId || asset == nullptr ||
      asset->document == nullptr || !scene_.is_effectively_visible(entity) ||
      scene_.is_effectively_locked(entity) ||
      scene_.is_instance_definition_entity(entity) ||
      scene_.is_instance_definition_member(entity) ||
      modeling_.modeling_object_for_entity(entity) != modeling->object_id) {
    return std::nullopt;
  }

  const QMat4 world = scene_.world_transform_matrix(entity);
  modeling::Result<modeling::CurveArcLengthPath> built =
      modeling::CurveArcLengthPath::build(*asset->document,
                                          sdk_transform(world));
  if (!built.ok() || !std::isfinite(built.value().total_length()) ||
      built.value().total_length() <= kPathArrayDistanceEpsilon) {
    return std::nullopt;
  }
  modeling::CurveArcLengthPath path = std::move(built).value();
  std::vector<PathArrayPathStation> layout_stations;
  const std::span<const modeling::CurveArcLengthStation> sampled =
      path.stations();
  layout_stations.reserve(sampled.size());
  for (const modeling::CurveArcLengthStation &station : sampled) {
    layout_stations.push_back({
        .distance = station.distance,
        .position_world = native_vector(station.evaluation.position),
        .tangent_world = native_vector(station.evaluation.tangent),
    });
  }
  return PathSnapshot{
      .entity = entity,
      .uuid = scene_.uuid(entity),
      .parent = scene_.parent(entity),
      .modeling_object_id = modeling->object_id,
      .curve = component->curve,
      .curve_revision = assets_.curve_revision(component->curve),
      .curve_content_revision = asset->document->content_revision(),
      .world_transform = world,
      .path = std::move(path),
      .layout_stations = std::move(layout_stations),
  };
}

bool PathArrayToolService::path_is_valid(const PathSnapshot &path) const {
  const TransformComponent *transform = scene_.transform(path.entity);
  const ModelingObjectComponent *modeling =
      scene_.modeling_object(path.entity);
  const CurveComponent *component = scene_.curve(path.entity);
  const CurveAsset *asset =
      component != nullptr ? assets_.curve(component->curve) : nullptr;
  return scene_.exists(path.entity) && transform != nullptr &&
         modeling != nullptr && component != nullptr && asset != nullptr &&
         asset->document != nullptr && scene_.uuid(path.entity) == path.uuid &&
         scene_.parent(path.entity) == path.parent &&
         modeling->object_id == path.modeling_object_id &&
         modeling_.modeling_object_for_entity(path.entity) ==
             path.modeling_object_id &&
         component->curve == path.curve &&
         assets_.curve_revision(path.curve) == path.curve_revision &&
         asset->document->content_revision() == path.curve_content_revision &&
         same_matrix(scene_.world_transform_matrix(path.entity),
                     path.world_transform) &&
         scene_.is_effectively_visible(path.entity) &&
         !scene_.is_effectively_locked(path.entity);
}

bool PathArrayToolService::active() const {
  if (!session_.has_value()) {
    return false;
  }
  const std::uint64_t scene_revision = scene_.revision();
  const std::uint64_t asset_revision = assets_.revision();
  const std::uint64_t object_map_revision =
      modeling_.modeling_object_map_revision();
  const std::uint64_t modeling_content_revision = modeling_.content_revision();
  if (cached_path_generation_ == session_->path_generation &&
      cached_path_scene_revision_ == scene_revision &&
      cached_path_asset_revision_ == asset_revision &&
      cached_path_object_map_revision_ == object_map_revision &&
      cached_path_modeling_content_revision_ == modeling_content_revision) {
    return cached_path_valid_;
  }
  cached_path_valid_ = path_is_valid(session_->path);
  cached_path_generation_ = session_->path_generation;
  cached_path_scene_revision_ = scene_revision;
  cached_path_asset_revision_ = asset_revision;
  cached_path_object_map_revision_ = object_map_revision;
  cached_path_modeling_content_revision_ = modeling_content_revision;
  return cached_path_valid_;
}

bool PathArrayToolService::has_source() const {
  return active() && session_->source.has_value();
}

bool PathArrayToolService::preview_valid() const {
  (void)preview_state();
  return cached_preview_current_valid_;
}

bool PathArrayToolService::can_activate() const {
  if (!release_allows_tool(kPathArrayToolId)) {
    return false;
  }
  QDR_PROFILE_SCOPE("Path Array can activate");
  const std::uint64_t scene_revision = scene_.revision();
  const std::uint64_t asset_revision = assets_.revision();
  const std::uint64_t object_map_revision =
      modeling_.modeling_object_map_revision();
  const std::uint64_t modeling_content_revision = modeling_.content_revision();
  const int selection_mode = static_cast<int>(selection_.mode());
  if (cached_can_activate_initialized_ &&
      cached_can_activate_scene_revision_ == scene_revision &&
      cached_can_activate_asset_revision_ == asset_revision &&
      cached_can_activate_object_map_revision_ == object_map_revision &&
      cached_can_activate_modeling_content_revision_ ==
          modeling_content_revision &&
      cached_can_activate_selection_mode_ == selection_mode) {
    return cached_can_activate_;
  }
  cached_can_activate_ = capture_selected_path().has_value();
  cached_can_activate_initialized_ = true;
  cached_can_activate_scene_revision_ = scene_revision;
  cached_can_activate_asset_revision_ = asset_revision;
  cached_can_activate_object_map_revision_ = object_map_revision;
  cached_can_activate_modeling_content_revision_ = modeling_content_revision;
  cached_can_activate_selection_mode_ = selection_mode;
  return cached_can_activate_;
}

PathArrayToolStage PathArrayToolService::stage() const {
  if (!active()) {
    return PathArrayToolStage::Inactive;
  }
  return session_->source.has_value() ? PathArrayToolStage::Previewing
                                      : PathArrayToolStage::AwaitingSource;
}

EntityId PathArrayToolService::path_entity() const {
  return active() ? session_->path.entity : kInvalidEntityId;
}

EntityId PathArrayToolService::source_root() const {
  return has_source() ? session_->source->root : kInvalidEntityId;
}

std::optional<EntityId>
PathArrayToolService::resolve_source_candidate(EntityId candidate) const {
  if (!active() || candidate == kInvalidEntityId ||
      candidate == session_->path.entity || !scene_.exists(candidate) ||
      scene_.is_instance_definition_entity(candidate) ||
      scene_.is_instance_definition_member(candidate) ||
      scene_.transform(candidate) == nullptr ||
      !scene_.is_effectively_visible(candidate) ||
      scene_.is_effectively_locked(candidate)) {
    return std::nullopt;
  }
  const bool source_kind = scene_.mesh(candidate) != nullptr ||
                           scene_.instance_occurrence(candidate) != nullptr ||
                           (scene_.kind(candidate) == EntityKind::Empty &&
                            scene_.has_children(candidate));
  if (!source_kind ||
      scene_.is_descendant_of(candidate, session_->path.entity)) {
    return std::nullopt;
  }
  const std::vector<EntityId> hierarchy = scene_.descendants(candidate, true);
  if (std::ranges::find(hierarchy, session_->path.entity) != hierarchy.end() ||
      std::ranges::any_of(hierarchy, [&](EntityId entity) {
        return scene_.is_instance_definition_entity(entity) ||
               scene_.is_instance_definition_member(entity);
      })) {
    return std::nullopt;
  }
  return candidate;
}

std::uint32_t PathArrayToolService::count() const {
  return session_.has_value() ? session_->count : defaults_.count;
}

double PathArrayToolService::distance() const {
  return session_.has_value() ? session_->distance
                              : defaults_.remembered_distance.value_or(0.0);
}

double PathArrayToolService::maximum_distance() const {
  return session_.has_value()
             ? std::max(session_->path.path.total_length(), session_->distance)
             : 0.0;
}

bool PathArrayToolService::follow_path() const {
  return session_.has_value() ? session_->follow_path
                              : defaults_.follow_path;
}

bool PathArrayToolService::show_bounding_box_dimensions() const {
  return defaults_.show_bounding_box_dimensions;
}

PathArraySignedAxis PathArrayToolService::forward_axis() const {
  return session_.has_value() ? session_->forward_axis
                              : defaults_.forward_axis;
}

QVec3 PathArrayToolService::rotation_offset_degrees() const {
  return session_.has_value() ? session_->variation.rotation_offset_degrees
                              : defaults_.rotation_offset_degrees;
}

QVec3 PathArrayToolService::rotation_random_min_degrees() const {
  return session_.has_value()
             ? session_->variation.rotation_random_min_degrees
             : defaults_.rotation_random_min_degrees;
}

QVec3 PathArrayToolService::rotation_random_max_degrees() const {
  return session_.has_value()
             ? session_->variation.rotation_random_max_degrees
             : defaults_.rotation_random_max_degrees;
}

QVec3 PathArrayToolService::scale_offset_factors() const {
  return session_.has_value() ? session_->variation.scale_offset_factors
                              : defaults_.scale_offset_factors;
}

QVec3 PathArrayToolService::scale_random_min_factors() const {
  return session_.has_value()
             ? session_->variation.scale_random_min_factors
             : defaults_.scale_random_min_factors;
}

QVec3 PathArrayToolService::scale_random_max_factors() const {
  return session_.has_value()
             ? session_->variation.scale_random_max_factors
             : defaults_.scale_random_max_factors;
}

std::uint32_t PathArrayToolService::maximum_count() const {
  if (!session_.has_value() || !session_->source.has_value()) {
    return kPathArrayMaximumCount;
  }
  return maximum_count_for(*session_->source);
}

CommandResult PathArrayToolService::activate() {
  if (!release_allows_tool(kPathArrayToolId)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  std::optional<PathSnapshot> path = capture_selected_path();
  if (!path.has_value()) {
    return command_invalid_result(
        "Select one visible, unlocked, nondegenerate Curve in object mode.");
  }
  const double fitted = fit_distance(path->path.total_length(),
                                     path->path.closed(),
                                     defaults_.count);
  const double initial_distance =
      defaults_.remembered_distance.value_or(fitted);
  Session next{
      .path = std::move(*path),
      .path_selection = selection_.capture_box_selection_baseline(),
      .count = defaults_.count,
      .distance = initial_distance,
      .follow_path = defaults_.follow_path,
      .forward_axis = defaults_.forward_axis,
      .variation = {
          .rotation_offset_degrees = defaults_.rotation_offset_degrees,
          .rotation_random_min_degrees = defaults_.rotation_random_min_degrees,
          .rotation_random_max_degrees = defaults_.rotation_random_max_degrees,
          .scale_offset_factors = defaults_.scale_offset_factors,
          .scale_random_min_factors = defaults_.scale_random_min_factors,
          .scale_random_max_factors = defaults_.scale_random_max_factors,
      },
  };
  next.path_generation = issue_revision();
  next.layout_revision = next.path_generation;
  if (!defaults_.remembered_distance.has_value()) {
    defaults_.remembered_distance = initial_distance;
  }
  session_ = std::move(next);
  pending_defaults_.reset();
  cached_path_generation_ = 0U;
  clear_preview();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult PathArrayToolService::refresh() {
  if (!session_.has_value()) {
    return command_unchanged_result();
  }
  if (!active()) {
    const EditorSelectionBoxBaseline baseline = session_->path_selection;
    const CommandResult restored = apply_selection_baseline(baseline);
    if (!restored.ok) {
      clear_preview();
      return {
          .ok = false,
          .changed = restored.changed,
          .message =
              "Path Array could not restore its activation selection while "
              "canceling after the driving Curve changed. " +
              restored.message,
      };
    }
    session_.reset();
    pending_defaults_.reset();
    cached_path_generation_ = 0U;
    clear_preview();
    issue_tool_settings_revision();
    return {
        .ok = true,
        .changed = true,
        .message = "Path Array canceled because its driving Curve changed.",
    };
  }

  if (!session_->source.has_value()) {
    const std::vector<EntityId> selected = scene_.selected_entities();
    if (selected.size() == 1U && selected.front() != session_->path.entity &&
        resolve_source_candidate(selected.front()).has_value()) {
      return select_source(selected.front());
    }
    return command_unchanged_result();
  }

  if (source_is_current(*session_->source)) {
    return command_unchanged_result();
  }
  std::optional<SourceSnapshot> current = std::move(cached_changed_source_);
  cached_changed_source_.reset();
  if (!current.has_value()) {
    session_->source.reset();
    pending_defaults_.reset();
    session_->source_generation = issue_revision();
    session_->layout_revision = issue_revision();
    clear_preview();
    issue_tool_settings_revision();
    return {
        .ok = true,
        .changed = true,
        .message = "Path Array source changed and must be selected again.",
    };
  }
  session_->source = std::move(current);
  session_->source_generation = issue_revision();
  const std::uint32_t allowed = maximum_count();
  if (allowed < kPathArrayMinimumCount) {
    session_->source.reset();
    session_->source_generation = issue_revision();
    session_->layout_revision = issue_revision();
    clear_preview();
    issue_tool_settings_revision();
    return command_invalid_result(
        "Path Array source hierarchy exceeds the preview capacity.");
  }
  session_->count = std::min(session_->count, allowed);
  session_->layout_revision = issue_revision();
  invalidate_preview();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult PathArrayToolService::cancel() {
  if (!session_.has_value()) {
    return command_unchanged_result();
  }
  if (pending_defaults_.has_value()) {
    (void)preview_state();
  }
  const EditorSelectionBoxBaseline baseline = session_->path_selection;
  const CommandResult restored = apply_selection_baseline(baseline);
  if (!restored.ok) {
    return restored;
  }
  session_.reset();
  pending_defaults_.reset();
  cached_path_generation_ = 0U;
  clear_preview();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult PathArrayToolService::select_source(EntityId source) {
  if (!release_allows_tool(kPathArrayToolId)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  const std::optional<EntityId> resolved = resolve_source_candidate(source);
  if (!resolved.has_value()) {
    return command_invalid_result(
        "Select one visible, unlocked mesh or group outside the driving Curve.");
  }
  std::optional<SourceSnapshot> captured = capture_source(*resolved);
  if (!captured.has_value()) {
    return command_invalid_result(
        "Path Array source hierarchy could not be captured.");
  }
  const std::uint32_t allowed = maximum_count_for(*captured);
  if (allowed < kPathArrayMinimumCount) {
    return command_invalid_result(
        "Path Array source hierarchy exceeds the array capacity.");
  }

  const std::uint32_t next_count = std::min(session_->count, allowed);
  const bool source_changed =
      !session_->source.has_value() ||
      !same_source_snapshot(*session_->source, *captured);
  const std::uint64_t candidate_source_generation =
      source_changed ? issue_revision() : session_->source_generation;
  Session candidate{
      .path = session_->path,
      .path_selection = session_->path_selection,
      .source = std::move(captured),
      .count = next_count,
      .distance = session_->distance,
      .follow_path = session_->follow_path,
      .forward_axis = session_->forward_axis,
      .variation = session_->variation,
      .path_generation = session_->path_generation,
      .source_generation = candidate_source_generation,
      .layout_revision = session_->layout_revision,
  };
  candidate.variation.seed = path_array_variation_seed(
      candidate.path.uuid, candidate.source->uuid);
  const std::optional<PathArrayLayoutResult> candidate_layout =
      build_layout(candidate);
  if (!candidate_layout.has_value() ||
      !build_preview_mesh_mappings(candidate, *candidate_layout).has_value()) {
    return command_invalid_result(
        "Path Array source cannot produce a valid preview with the current "
        "settings.");
  }

  const DocumentCommandResult selected =
      selection_.synchronize_scene_selection(
          std::span<const EntityId>(&*resolved, 1U), *resolved);
  if (!selected.ok) {
    return CommandResult{.ok = false,
                         .changed = selected.changed,
                         .message = selected.message};
  }
  const bool settings_changed = next_count != session_->count;
  if (source_changed) {
    session_->source = std::move(candidate.source);
    session_->source_generation = candidate_source_generation;
    session_->variation.seed = candidate.variation.seed;
  }
  session_->count = next_count;
  if (!source_changed && !settings_changed && !selected.changed) {
    return command_unchanged_result();
  }
  if (source_changed || settings_changed) {
    session_->layout_revision = issue_revision();
    invalidate_preview();
  }
  if (settings_changed || source_changed) {
    issue_tool_settings_revision();
  }
  return command_changed_result();
}

CommandResult PathArrayToolService::clear_source() {
  if (!release_allows_tool(kPathArrayToolId)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_unchanged_result();
  }
  if (pending_defaults_.has_value()) {
    (void)preview_state();
  }
  const DocumentCommandResult cleared = selection_.synchronize_scene_selection(
      std::span<const EntityId>{}, kInvalidEntityId);
  if (!cleared.ok) {
    return CommandResult{.ok = false,
                         .changed = cleared.changed,
                         .message = cleared.message};
  }
  if (!session_->source.has_value()) {
    return cleared.changed ? command_changed_result()
                           : command_unchanged_result();
  }
  session_->source.reset();
  pending_defaults_.reset();
  session_->source_generation = issue_revision();
  session_->layout_revision = issue_revision();
  clear_preview();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult PathArrayToolService::set_count(std::uint32_t count) {
  if (!release_allows_tool(kPathArrayToolId)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_invalid_result("Path Array tool is not active.");
  }
  if (count < kPathArrayMinimumCount || count > maximum_count()) {
    return command_invalid_result(
        "Path Array count exceeds the current hierarchy limit.");
  }
  if (session_->count == count) {
    return command_unchanged_result();
  }
  session_->count = count;
  editable_defaults().count = count;
  session_->layout_revision = issue_revision();
  invalidate_preview();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult PathArrayToolService::set_distance(double distance) {
  if (!release_allows_tool(kPathArrayToolId)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_invalid_result("Path Array tool is not active.");
  }
  if (!std::isfinite(distance) || distance < 0.0) {
    return command_invalid_result(
        "Path Array distance must be finite and nonnegative.");
  }
  if (session_->distance == distance) {
    return command_unchanged_result();
  }
  session_->distance = distance;
  editable_defaults().remembered_distance = distance;
  session_->layout_revision = issue_revision();
  invalidate_preview();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult PathArrayToolService::set_follow_path(bool follow) {
  if (!release_allows_tool(kPathArrayToolId)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_invalid_result("Path Array tool is not active.");
  }
  if (session_->follow_path == follow) {
    return command_unchanged_result();
  }
  session_->follow_path = follow;
  editable_defaults().follow_path = follow;
  session_->layout_revision = issue_revision();
  invalidate_preview();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult PathArrayToolService::set_show_bounding_box_dimensions(bool show) {
  if (!release_allows_tool(kPathArrayToolId)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_invalid_result("Path Array tool is not active.");
  }
  if (defaults_.show_bounding_box_dimensions == show) {
    return command_unchanged_result();
  }
  defaults_.show_bounding_box_dimensions = show;
  if (pending_defaults_.has_value()) {
    pending_defaults_->show_bounding_box_dimensions = show;
  }
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult
PathArrayToolService::set_forward_axis(PathArraySignedAxis axis) {
  if (!release_allows_tool(kPathArrayToolId)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_invalid_result("Path Array tool is not active.");
  }
  if (axis < PathArraySignedAxis::PositiveX ||
      axis > PathArraySignedAxis::NegativeZ) {
    return command_invalid_result("Path Array forward axis is invalid.");
  }
  if (session_->forward_axis == axis) {
    return command_unchanged_result();
  }
  session_->forward_axis = axis;
  editable_defaults().forward_axis = axis;
  session_->layout_revision = issue_revision();
  invalidate_preview();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult
PathArrayToolService::set_variation(PathArrayVariationSettings variation) {
  if (!session_.has_value()) {
    return command_invalid_result("Path Array tool is not active.");
  }
  variation.seed = session_->variation.seed;
  const std::string validation_error =
      path_array_variation_validation_error(variation);
  if (!validation_error.empty()) {
    return command_invalid_result(validation_error);
  }
  if (session_->variation == variation) {
    return command_unchanged_result();
  }
  session_->variation = variation;
  PathArraySessionDefaults &remembered = editable_defaults();
  remembered.rotation_offset_degrees = variation.rotation_offset_degrees;
  remembered.rotation_random_min_degrees = variation.rotation_random_min_degrees;
  remembered.rotation_random_max_degrees = variation.rotation_random_max_degrees;
  remembered.scale_offset_factors = variation.scale_offset_factors;
  remembered.scale_random_min_factors = variation.scale_random_min_factors;
  remembered.scale_random_max_factors = variation.scale_random_max_factors;
  session_->layout_revision = issue_revision();
  invalidate_preview();
  issue_tool_settings_revision();
  return command_changed_result();
}

CommandResult
PathArrayToolService::set_rotation_offset_degrees(QVec3 value) {
  if (!release_allows_tool(kPathArrayToolId)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  PathArrayVariationSettings variation =
      session_.has_value() ? session_->variation : PathArrayVariationSettings{};
  variation.rotation_offset_degrees = value;
  return set_variation(variation);
}

CommandResult
PathArrayToolService::set_rotation_random_min_degrees(QVec3 value) {
  if (!release_allows_tool(kPathArrayToolId)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  PathArrayVariationSettings variation =
      session_.has_value() ? session_->variation : PathArrayVariationSettings{};
  variation.rotation_random_min_degrees = value;
  variation.rotation_random_max_degrees = componentwise_maximum(
      variation.rotation_random_max_degrees, value);
  return set_variation(variation);
}

CommandResult
PathArrayToolService::set_rotation_random_max_degrees(QVec3 value) {
  if (!release_allows_tool(kPathArrayToolId)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  PathArrayVariationSettings variation =
      session_.has_value() ? session_->variation : PathArrayVariationSettings{};
  variation.rotation_random_max_degrees = value;
  variation.rotation_random_min_degrees = componentwise_minimum(
      variation.rotation_random_min_degrees, value);
  return set_variation(variation);
}

CommandResult PathArrayToolService::set_scale_offset_factors(QVec3 value) {
  if (!release_allows_tool(kPathArrayToolId)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  PathArrayVariationSettings variation =
      session_.has_value() ? session_->variation : PathArrayVariationSettings{};
  variation.scale_offset_factors = value;
  return set_variation(variation);
}

CommandResult
PathArrayToolService::set_scale_random_min_factors(QVec3 value) {
  if (!release_allows_tool(kPathArrayToolId)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  PathArrayVariationSettings variation =
      session_.has_value() ? session_->variation : PathArrayVariationSettings{};
  variation.scale_random_min_factors = value;
  variation.scale_random_max_factors =
      componentwise_maximum(variation.scale_random_max_factors, value);
  return set_variation(variation);
}

CommandResult
PathArrayToolService::set_scale_random_max_factors(QVec3 value) {
  if (!release_allows_tool(kPathArrayToolId)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  PathArrayVariationSettings variation =
      session_.has_value() ? session_->variation : PathArrayVariationSettings{};
  variation.scale_random_max_factors = value;
  variation.scale_random_min_factors =
      componentwise_minimum(variation.scale_random_min_factors, value);
  return set_variation(variation);
}

CommandResult PathArrayToolService::reset_settings() {
  if (!release_allows_tool(kPathArrayToolId)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value()) {
    return command_invalid_result("Path Array tool is not active.");
  }
  const std::uint32_t reset_count =
      std::min(kPathArrayDefaultCount, maximum_count());
  const double reset_distance =
      fit_distance(session_->path.path.total_length(),
                   session_->path.path.closed(), reset_count);
  PathArrayVariationSettings reset_variation;
  reset_variation.seed = session_->variation.seed;
  const bool changed = session_->count != reset_count ||
                       defaults_.count != kPathArrayDefaultCount ||
                       session_->distance != reset_distance ||
                       !session_->follow_path ||
                       session_->forward_axis != kPathArrayDefaultForwardAxis ||
                       session_->variation != reset_variation;
  if (!changed) {
    return set_show_bounding_box_dimensions(true);
  }
  editable_defaults() = {.remembered_distance = reset_distance};
  defaults_.show_bounding_box_dimensions = true;
  session_->count = reset_count;
  session_->distance = reset_distance;
  session_->follow_path = kPathArrayDefaultFollowPath;
  session_->forward_axis = kPathArrayDefaultForwardAxis;
  session_->variation = reset_variation;
  session_->layout_revision = issue_revision();
  invalidate_preview();
  issue_tool_settings_revision();
  return command_changed_result();
}

std::optional<PathArrayToolService::SourceSnapshot>
PathArrayToolService::capture_source(EntityId source) const {
  if (!resolve_source_candidate(source).has_value()) {
    return std::nullopt;
  }
  const TransformComponent *root_transform = scene_.transform(source);
  const std::optional<QVec3> pivot =
      resolve_entity_transform_visible_pivot(scene_, assets_, source);
  const QMat4 root_world = scene_.world_transform_matrix(source);
  const std::optional<QMat4> root_inverse =
      try_scene_matrix_inverse(root_world);
  const std::optional<QMat4> variation_frame =
      root_transform != nullptr
          ? source_local_variation_frame(scene_, source, *root_transform)
          : std::nullopt;
  if (root_transform == nullptr || !pivot.has_value() ||
      !finite_vector(*pivot) || !root_inverse.has_value() ||
      !variation_frame.has_value()) {
    return std::nullopt;
  }

  SourceSnapshot result{
      .root = source,
      .uuid = scene_.uuid(source),
      .parent = scene_.parent(source),
      .local_transform = *root_transform,
      .world_transform = root_world,
      .variation_frame_world = *variation_frame,
      .visible_pivot_world = *pivot,
  };

  const auto append_fingerprint = [&](EntityId entity) -> bool {
    const TransformComponent *transform = scene_.transform(entity);
    if (!scene_.exists(entity) || transform == nullptr) {
      return false;
    }
    HierarchyNodeFingerprint fingerprint{
        .entity = entity,
        .uuid = scene_.uuid(entity),
        .parent = scene_.parent(entity),
        .kind = scene_.kind(entity),
        .transform = *transform,
        .visibility = scene_.visibility(entity) != nullptr
                          ? *scene_.visibility(entity)
                          : VisibilityComponent{},
        .lock = scene_.lock(entity) != nullptr ? *scene_.lock(entity)
                                                : LockComponent{},
        .layer_id = scene_.entity_layer(entity),
    };
    if (const ModelingObjectComponent *component =
            scene_.modeling_object(entity)) {
      fingerprint.modeling_object = *component;
    }
    if (const MeshComponent *component = scene_.mesh(entity)) {
      fingerprint.mesh = *component;
      fingerprint.mesh_revision = assets_.mesh_revision(component->mesh);
    }
    if (const CurveComponent *component = scene_.curve(entity)) {
      fingerprint.curve = *component;
      fingerprint.curve_revision = assets_.curve_revision(component->curve);
    }
    if (const MaterialComponent *component = scene_.material(entity)) {
      fingerprint.material = *component;
      fingerprint.material_revisions =
          material_revision_keys(assets_, component);
    }
    if (const TextureMappingComponent *component =
            scene_.texture_mapping(entity)) {
      fingerprint.texture_mapping = *component;
    }
    if (const LightComponent *component = scene_.light(entity)) {
      fingerprint.light = *component;
    }
    if (const InstanceDefinitionComponent *component =
            scene_.instance_definition(entity)) {
      fingerprint.instance_definition = *component;
    }
    if (const InstanceOccurrenceComponent *component =
            scene_.instance_occurrence(entity)) {
      fingerprint.instance_occurrence = *component;
    }
    result.hierarchy.push_back(std::move(fingerprint));
    return true;
  };

  const std::vector<EntityId> hierarchy = scene_.descendants(source, true);
  result.hierarchy_node_count = hierarchy.size();
  result.suppression.entities.reserve(hierarchy.size());
  std::unordered_set<EntityId> captured_definition_fingerprints;
  for (const EntityId entity : hierarchy) {
    if (entity == session_->path.entity ||
        scene_.is_instance_definition_entity(entity) ||
        scene_.is_instance_definition_member(entity) ||
        !append_fingerprint(entity)) {
      return std::nullopt;
    }
    result.suppression.entities.push_back(entity);
    const bool visible = scene_.is_effectively_visible(entity);

    const EntityKind kind = scene_.kind(entity);
    if (visible && (kind == EntityKind::Empty ||
                    kind == EntityKind::Generic)) {
      MarkerParticipant marker{
          .source_entity = entity,
          .member_from_root = scene_transform_multiply(
              *root_inverse, scene_.world_transform_matrix(entity)),
      };
      if (kind == EntityKind::Empty && scene_.has_children(entity)) {
        const std::optional<QMat4> entity_inverse = try_scene_matrix_inverse(
            scene_.world_transform_matrix(entity));
        if (!entity_inverse.has_value()) {
          return std::nullopt;
        }
        const auto [minimum, maximum] = marker_bounds_in_root_space(
            scene_, assets_, entity, *entity_inverse);
        if (!finite_vector(minimum) || !finite_vector(maximum)) {
          return std::nullopt;
        }
        marker.local_min = minimum;
        marker.local_max = maximum;
      }
      result.markers.push_back(std::move(marker));
    }

    if (const InstanceOccurrenceComponent *occurrence =
            scene_.instance_occurrence(entity)) {
      if (scene_.kind(entity) != EntityKind::Instance ||
          scene_.has_children(entity) ||
          !scene_.is_instance_definition_entity(occurrence->definition)) {
        return std::nullopt;
      }
      const std::optional<QMat4> definition_inverse =
          try_scene_matrix_inverse(
              scene_.world_transform_matrix(occurrence->definition));
      if (!definition_inverse.has_value()) {
        return std::nullopt;
      }
      if (captured_definition_fingerprints.insert(occurrence->definition)
              .second &&
          !append_fingerprint(occurrence->definition)) {
        return std::nullopt;
      }
      for (const EntityId member :
           scene_.descendants(occurrence->definition, false)) {
        if (scene_.containing_instance_definition(member) !=
            occurrence->definition) {
          continue;
        }
        if (captured_definition_fingerprints.insert(member).second &&
            !append_fingerprint(member)) {
          return std::nullopt;
        }
        const MeshComponent *mesh = scene_.mesh(member);
        if (mesh == nullptr ||
            !scene_hierarchy_segment_is_visible(
                scene_, member, occurrence->definition)) {
          continue;
        }
        const MeshAsset *asset = assets_.mesh(mesh->mesh);
        if (asset == nullptr) {
          return std::nullopt;
        }
        result.suppression.occurrences.push_back({
            .source_entity = member,
            .occurrence = entity,
        });
        if (!visible) {
          continue;
        }
        const QMat4 member_from_root = scene_transform_multiply(
            *root_inverse,
            scene_transform_multiply(
                scene_.world_transform_matrix(entity),
                scene_transform_multiply(
                    *definition_inverse,
                    scene_.world_transform_matrix(member))));
        result.meshes.push_back({
            .source_entity = member,
            .visibility_entity = entity,
            .material = entity_material(scene_, member),
            .mesh = *asset,
            .member_from_root = member_from_root,
            .double_sided = mesh->double_sided,
            .cast_shadows = mesh->cast_shadows,
        });
      }
      continue;
    }

    if (const MeshComponent *mesh = scene_.mesh(entity)) {
      const MeshAsset *asset = assets_.mesh(mesh->mesh);
      if (asset == nullptr) {
        return std::nullopt;
      }
      if (visible) {
        result.meshes.push_back({
            .source_entity = entity,
            .visibility_entity = entity,
            .material = entity_material(scene_, entity),
            .mesh = *asset,
            .member_from_root = scene_transform_multiply(
                *root_inverse, scene_.world_transform_matrix(entity)),
            .double_sided = mesh->double_sided,
            .cast_shadows = mesh->cast_shadows,
        });
      }
    }
    if (const CurveComponent *curve = scene_.curve(entity)) {
      const CurveAsset *asset = assets_.curve(curve->curve);
      if (asset == nullptr || asset->document == nullptr) {
        return std::nullopt;
      }
      if (visible) {
        result.curves.push_back({
            .source_entity = entity,
            .visibility_entity = entity,
            .curve = curve->curve,
            .material = entity_material(scene_, entity),
            .solidify = curve->solidify,
            .solidify_resolution = curve->solidify_resolution,
            .solidify_radius = curve->solidify_radius,
            .member_from_root = scene_transform_multiply(
                *root_inverse, scene_.world_transform_matrix(entity)),
        });
      }
    }
    if (const LightComponent *light = scene_.light(entity)) {
      if (visible) {
        ++result.light_count;
        if (light->casts_shadows) {
          ++result.shadow_light_count;
        }
        result.lights.push_back({
            .source_entity = entity,
            .visibility_entity = entity,
            .light = *light,
            .member_from_root = scene_transform_multiply(
                *root_inverse, scene_.world_transform_matrix(entity)),
        });
      }
    }
  }
  result.visible_preview_participant_count =
      result.meshes.size() + result.curves.size() + result.lights.size() +
      result.markers.size();
  if (result.hierarchy_node_count == 0U ||
      maximum_count_for(result) < kPathArrayMinimumCount) {
    return std::nullopt;
  }
  return result;
}

std::uint32_t
PathArrayToolService::maximum_count_for(const SourceSnapshot &source) {
  std::size_t maximum = kPathArrayMaximumCount;
  const auto constrain = [&maximum](std::size_t capacity, std::size_t cost) {
    if (cost != 0U) {
      maximum = std::min(maximum, capacity / cost);
    }
  };
  constrain(kPathArrayMaximumHierarchyElements, source.hierarchy_node_count);
  constrain(kPathArrayMaximumPreviewParticipants,
            source.visible_preview_participant_count);
  constrain(kPathArrayMaximumLights, source.light_count);
  constrain(kPathArrayMaximumShadowLights, source.shadow_light_count);
  return static_cast<std::uint32_t>(maximum);
}

bool PathArrayToolService::source_is_current(
    const SourceSnapshot &source) const {
  const std::uint64_t scene_revision = scene_.revision();
  const std::uint64_t asset_revision = assets_.revision();
  const std::uint64_t object_map_revision =
      modeling_.modeling_object_map_revision();
  const std::uint64_t modeling_content_revision = modeling_.content_revision();
  if (session_.has_value() &&
      cached_source_generation_ == session_->source_generation &&
      cached_source_scene_revision_ == scene_revision &&
      cached_source_asset_revision_ == asset_revision &&
      cached_source_object_map_revision_ == object_map_revision &&
      cached_source_modeling_content_revision_ == modeling_content_revision) {
    return cached_source_valid_;
  }
  std::optional<SourceSnapshot> current = capture_source(source.root);
  cached_source_valid_ =
      current.has_value() && same_source_snapshot(source, *current);
  if (cached_source_valid_) {
    cached_changed_source_.reset();
  } else {
    cached_changed_source_ = std::move(current);
  }
  cached_source_generation_ =
      session_.has_value() ? session_->source_generation : 0U;
  cached_source_scene_revision_ = scene_revision;
  cached_source_asset_revision_ = asset_revision;
  cached_source_object_map_revision_ = object_map_revision;
  cached_source_modeling_content_revision_ = modeling_content_revision;
  return cached_source_valid_;
}

bool PathArrayToolService::same_source_snapshot(
    const SourceSnapshot &left, const SourceSnapshot &right) {
  const auto same_optional_modeling = [](const auto &first,
                                         const auto &second) {
    if (first.has_value() != second.has_value()) {
      return false;
    }
    return !first.has_value() ||
           (first->object_id == second->object_id &&
            first->material_slot == second->material_slot &&
            first->sdk_owned == second->sdk_owned &&
            first->local_frame_version == second->local_frame_version);
  };
  const auto same_node = [&](const HierarchyNodeFingerprint &first,
                             const HierarchyNodeFingerprint &second) {
    const bool same_instance_definition =
        first.instance_definition.has_value() ==
            second.instance_definition.has_value() &&
        (!first.instance_definition.has_value() ||
         first.instance_definition->internal ==
             second.instance_definition->internal);
    return first.entity == second.entity && first.uuid == second.uuid &&
           first.parent == second.parent && first.kind == second.kind &&
           same_transform(first.transform, second.transform) &&
           first.visibility.visible == second.visibility.visible &&
           first.visibility.selectable == second.visibility.selectable &&
           first.lock.locked == second.lock.locked &&
           first.layer_id == second.layer_id &&
           same_optional_modeling(first.modeling_object,
                                  second.modeling_object) &&
           first.mesh == second.mesh && first.curve == second.curve &&
           first.material == second.material &&
           first.texture_mapping == second.texture_mapping &&
           first.light == second.light && same_instance_definition &&
           first.instance_occurrence == second.instance_occurrence &&
           first.mesh_revision == second.mesh_revision &&
           first.curve_revision == second.curve_revision &&
           first.material_revisions == second.material_revisions;
  };
  const auto same_mesh = [](const MeshParticipant &first,
                            const MeshParticipant &second) {
    return first.source_entity == second.source_entity &&
           first.visibility_entity == second.visibility_entity &&
           first.material == second.material &&
           first.mesh.revision == second.mesh.revision &&
           same_matrix(first.member_from_root, second.member_from_root) &&
           first.double_sided == second.double_sided &&
           first.cast_shadows == second.cast_shadows;
  };
  const auto same_curve = [](const CurveParticipant &first,
                             const CurveParticipant &second) {
    return first.source_entity == second.source_entity &&
           first.visibility_entity == second.visibility_entity &&
           first.curve == second.curve &&
           first.material == second.material &&
           first.solidify == second.solidify &&
           first.solidify_resolution == second.solidify_resolution &&
           first.solidify_radius == second.solidify_radius &&
           same_matrix(first.member_from_root, second.member_from_root);
  };
  const auto same_light = [](const LightParticipant &first,
                             const LightParticipant &second) {
    return first.source_entity == second.source_entity &&
           first.visibility_entity == second.visibility_entity &&
           first.light == second.light &&
           same_matrix(first.member_from_root, second.member_from_root);
  };
  const auto same_marker = [](const MarkerParticipant &first,
                              const MarkerParticipant &second) {
    return first.source_entity == second.source_entity &&
           same_vector(first.local_min, second.local_min) &&
           same_vector(first.local_max, second.local_max) &&
           same_matrix(first.member_from_root, second.member_from_root);
  };
  return left.root == right.root && left.uuid == right.uuid &&
         left.parent == right.parent &&
         same_transform(left.local_transform, right.local_transform) &&
         same_matrix(left.world_transform, right.world_transform) &&
         same_matrix(left.variation_frame_world,
                     right.variation_frame_world) &&
         same_vector(left.visible_pivot_world, right.visible_pivot_world) &&
         left.hierarchy_node_count == right.hierarchy_node_count &&
         left.visible_preview_participant_count ==
             right.visible_preview_participant_count &&
         left.light_count == right.light_count &&
         left.shadow_light_count == right.shadow_light_count &&
         left.hierarchy.size() == right.hierarchy.size() &&
         std::equal(left.hierarchy.begin(), left.hierarchy.end(),
                    right.hierarchy.begin(), same_node) &&
         left.meshes.size() == right.meshes.size() &&
         std::equal(left.meshes.begin(), left.meshes.end(),
                    right.meshes.begin(), same_mesh) &&
         left.curves.size() == right.curves.size() &&
         std::equal(left.curves.begin(), left.curves.end(),
                    right.curves.begin(), same_curve) &&
         left.lights.size() == right.lights.size() &&
         std::equal(left.lights.begin(), left.lights.end(),
                    right.lights.begin(), same_light) &&
         left.markers.size() == right.markers.size() &&
         std::equal(left.markers.begin(), left.markers.end(),
                    right.markers.begin(), same_marker) &&
         left.suppression.entities == right.suppression.entities &&
         left.suppression.occurrences == right.suppression.occurrences;
}

const PathArrayLayoutBackbone &
PathArrayToolService::layout_backbone(const Session &session) const {
  const LayoutBackboneCacheKey key{
      .path_generation = session.path_generation,
      .source_generation = session.source_generation,
      .follow_path = session.follow_path,
      .forward_axis = session.follow_path ? session.forward_axis
                                          : kPathArrayDefaultForwardAxis,
  };
  if (!cached_layout_backbone_key_.has_value() ||
      *cached_layout_backbone_key_ != key ||
      !cached_layout_backbone_.has_value()) {
    PathArrayLayoutBackbone prepared = prepare_path_array_layout_backbone({
        .path_stations = session.path.layout_stations,
        .source_world_transform = session.source->world_transform,
        .source_variation_frame_world =
            session.source->variation_frame_world,
        .source_pivot_world = session.source->visible_pivot_world,
        .follow_path = session.follow_path,
        .signed_axis = session.forward_axis,
        .closed = session.path.path.closed(),
    });
    cached_layout_backbone_key_ = key;
    cached_layout_backbone_ = std::move(prepared);
    ++layout_work_stats_.backbone_build_count;
  }
  return *cached_layout_backbone_;
}

std::optional<PathArrayLayoutResult>
PathArrayToolService::build_layout(const Session &session) const {
  if (session.path.layout_stations.size() < 2U ||
      !session.source.has_value()) {
    return std::nullopt;
  }
  const PathArrayLayoutBackbone &backbone = layout_backbone(session);
  ++layout_work_stats_.layout_evaluation_count;
  PathArrayLayoutResult result = build_path_array_layout(backbone, {
      .count = session.count,
      .distance = session.distance,
      .variation = session.variation,
  });
  if (!result.ok || result.world_transforms.size() != session.count) {
    return std::nullopt;
  }
  const EntityId parent = session.source->parent;
  if (parent != kInvalidEntityId && !scene_.exists(parent)) {
    return std::nullopt;
  }
  const QMat4 parent_world =
      parent == kInvalidEntityId ? scene_transform_identity_matrix()
                                 : scene_.world_transform_matrix(parent);
  for (const QMat4 &world : result.world_transforms) {
    const std::optional<TransformComponent> local =
        try_scene_local_transform_preserving_world(
            parent_world, world, session.source->local_transform);
    if (!local.has_value() ||
        !near_matrix(scene_transform_multiply(
                         parent_world,
                         scene_local_transform_matrix(*local)),
                     world)) {
      return std::nullopt;
    }
  }
  return result;
}

std::optional<std::vector<std::vector<QMat4>>>
PathArrayToolService::build_preview_mesh_mappings(
    const Session &session, const PathArrayLayoutResult &layout) const {
  if (!session.source.has_value()) {
    return std::nullopt;
  }
  std::vector<std::vector<QMat4>> mappings(session.source->meshes.size());
  if (mappings.empty()) {
    return mappings;
  }

  const EntityId root = session.source->root;
  const TextureLockSettings texture_locks =
      document_commands_.texture_lock_settings();
  if (!cached_texture_plan_attempted_ ||
      cached_texture_plan_source_generation_ != session.source_generation ||
      cached_texture_plan_texture_lock_ != texture_locks.texture_lock ||
      cached_texture_plan_texture_lock_scale_ !=
          texture_locks.texture_lock_scale) {
    std::optional<SceneTextureMappingPlan> captured =
        SceneTextureMappingPlan::capture(
            scene_, std::span<const EntityId>(&root, 1U), texture_locks);
    cached_texture_plan_source_generation_ = session.source_generation;
    cached_texture_plan_texture_lock_ = texture_locks.texture_lock;
    cached_texture_plan_texture_lock_scale_ =
        texture_locks.texture_lock_scale;
    cached_texture_plan_attempted_ = true;
    cached_texture_mapping_plan_ =
        captured.has_value()
            ? std::make_shared<SceneTextureMappingPlan>(
                  std::move(*captured))
            : nullptr;
  }
  if (cached_texture_mapping_plan_ == nullptr) {
    return std::nullopt;
  }
  const SceneTextureMappingPlan &plan = *cached_texture_mapping_plan_;
  const EntityId parent = session.source->parent;
  if (parent != kInvalidEntityId && !scene_.exists(parent)) {
    return std::nullopt;
  }
  const QMat4 parent_world =
      parent == kInvalidEntityId ? scene_transform_identity_matrix()
                                 : scene_.world_transform_matrix(parent);
  for (std::vector<QMat4> &participant_mappings : mappings) {
    participant_mappings.reserve(layout.world_transforms.size());
  }
  std::vector<std::size_t> mapping_edit_indices(
      session.source->meshes.size(), std::numeric_limits<std::size_t>::max());
  bool mapping_edit_indices_initialized = false;

  for (const QMat4 &root_world : layout.world_transforms) {
    const std::optional<TransformComponent> root_after =
        try_scene_local_transform_preserving_world(
            parent_world, root_world, session.source->local_transform);
    if (!root_after.has_value() ||
        !near_matrix(scene_transform_multiply(
                         parent_world,
                         scene_local_transform_matrix(*root_after)),
                     root_world)) {
      return std::nullopt;
    }
    const DocumentTransformEdit root_edit{
        .entity = root,
        .before = session.source->local_transform,
        .after = *root_after,
    };
    const std::optional<std::vector<DocumentTextureMappingEdit>> evaluated =
        plan.evaluate(std::span<const DocumentTransformEdit>(&root_edit, 1U),
                      SceneTextureTransformKind::Combined);
    if (!evaluated.has_value()) {
      return std::nullopt;
    }
    if (!mapping_edit_indices_initialized) {
      for (std::size_t index = 0U; index < session.source->meshes.size();
           ++index) {
        const EntityId mapping_entity =
            session.source->meshes[index].visibility_entity;
        const auto found = std::ranges::find(
            *evaluated, mapping_entity, &DocumentTextureMappingEdit::entity);
        if (found == evaluated->end()) {
          return std::nullopt;
        }
        mapping_edit_indices[index] =
            static_cast<std::size_t>(found - evaluated->begin());
      }
      mapping_edit_indices_initialized = true;
    }

    for (std::size_t index = 0U; index < session.source->meshes.size();
         ++index) {
      const MeshParticipant &participant = session.source->meshes[index];
      const EntityId mapping_entity = participant.visibility_entity;
      const std::size_t mapping_index = mapping_edit_indices[index];
      if (mapping_index >= evaluated->size() ||
          (*evaluated)[mapping_index].entity != mapping_entity) {
        return std::nullopt;
      }
      const std::optional<TextureMappingComponent> &after =
          (*evaluated)[mapping_index].after;
      const QMat4 planned = after.has_value()
                                ? after->sampling_from_local
                                : QMat4{};
      if (participant.visibility_entity == participant.source_entity) {
        if (!texture_mapping_is_valid({planned})) {
          return std::nullopt;
        }
        mappings[index].push_back(planned);
        continue;
      }

      const InstanceOccurrenceComponent *occurrence =
          scene_.instance_occurrence(participant.visibility_entity);
      if (occurrence == nullptr ||
          scene_.containing_instance_definition(participant.source_entity) !=
              occurrence->definition) {
        return std::nullopt;
      }
      const std::optional<QMat4> definition_from_world =
          try_scene_matrix_inverse(
              scene_.world_transform_matrix(occurrence->definition));
      if (!definition_from_world.has_value()) {
        return std::nullopt;
      }
      const QMat4 definition_from_member = scene_transform_multiply(
          *definition_from_world,
          scene_.world_transform_matrix(participant.source_entity));
      const std::optional<QMat4> member_from_definition =
          try_scene_matrix_inverse(definition_from_member);
      if (!member_from_definition.has_value()) {
        return std::nullopt;
      }
      const TextureMappingComponent *member_mapping =
          scene_.texture_mapping(participant.source_entity);
      const QMat4 direct = member_mapping != nullptr
                               ? member_mapping->sampling_from_local
                               : QMat4{};
      const QMat4 effective = scene_transform_multiply(
          scene_transform_multiply(direct, *member_from_definition),
          scene_transform_multiply(planned, definition_from_member));
      if (!texture_mapping_is_valid({effective})) {
        return std::nullopt;
      }
      mappings[index].push_back(effective);
    }
  }
  return mappings;
}

const PathArrayPreviewState &PathArrayToolService::preview_state() const {
  if (!session_.has_value() || !session_->source.has_value()) {
    cached_preview_current_valid_ = false;
    return cached_preview_;
  }
  const TextureLockSettings texture_locks =
      document_commands_.texture_lock_settings();
  const std::uint64_t scene_revision = scene_.revision();
  const std::uint64_t asset_revision = assets_.revision();
  const std::uint64_t object_map_revision =
      modeling_.modeling_object_map_revision();
  const std::uint64_t modeling_content_revision = modeling_.content_revision();
  if (cached_preview_attempted_ &&
      cached_preview_attempt_layout_revision_ == session_->layout_revision &&
      cached_preview_attempt_scene_revision_ == scene_revision &&
      cached_preview_attempt_asset_revision_ == asset_revision &&
      cached_preview_attempt_object_map_revision_ == object_map_revision &&
      cached_preview_attempt_modeling_content_revision_ ==
          modeling_content_revision &&
      cached_preview_attempt_texture_lock_ == texture_locks.texture_lock &&
      cached_preview_attempt_texture_lock_scale_ ==
          texture_locks.texture_lock_scale) {
    return cached_preview_;
  }
  cached_preview_attempted_ = true;
  cached_preview_attempt_layout_revision_ = session_->layout_revision;
  cached_preview_attempt_scene_revision_ = scene_revision;
  cached_preview_attempt_asset_revision_ = asset_revision;
  cached_preview_attempt_object_map_revision_ = object_map_revision;
  cached_preview_attempt_modeling_content_revision_ =
      modeling_content_revision;
  cached_preview_attempt_texture_lock_ = texture_locks.texture_lock;
  cached_preview_attempt_texture_lock_scale_ =
      texture_locks.texture_lock_scale;
  cached_preview_current_valid_ = false;
  if (!active() || !source_is_current(*session_->source)) {
    return cached_preview_;
  }

  std::optional<PathArrayLayoutResult> layout = build_layout(*session_);
  if (!layout.has_value()) {
    return cached_preview_;
  }
  const std::optional<std::vector<std::vector<QMat4>>> mesh_mappings =
      build_preview_mesh_mappings(*session_, *layout);
  if (!mesh_mappings.has_value()) {
    return cached_preview_;
  }
  const SourceSnapshot &source = *session_->source;
  const std::uint64_t publication_revision =
      ++preview_publication_revision_;
  PathArrayPreviewState preview{
      .active = true,
      .path_entity = session_->path.entity,
      .source_root = source.root,
      .effective_distance = layout->effective_distance,
      .total_path_length = layout->total_path_length,
      .root_world_transforms = layout->world_transforms,
      .revision = publication_revision,
  };
  preview.scene.revision = publication_revision;
  preview.scene.suppression = source.suppression;
  preview.scene.meshes.reserve(source.meshes.size());
  for (std::size_t participant_index = 0U;
       participant_index < source.meshes.size(); ++participant_index) {
    const MeshParticipant &participant = source.meshes[participant_index];
    std::vector<QMat4> transforms;
    transforms.reserve(layout->world_transforms.size());
    for (const QMat4 &root_world : layout->world_transforms) {
      transforms.push_back(scene_transform_multiply(
          root_world, participant.member_from_root));
    }
    const ModelingObjectComponent *modeling =
        scene_.modeling_object(participant.source_entity);
    ModelingToolPreviewMesh mesh{
        .source_object_id = modeling != nullptr
                                ? modeling->object_id
                                : kInvalidModelingObjectId,
        .source_entity = participant.source_entity,
        .visibility_entity = participant.visibility_entity,
        .material = participant.material,
        .mesh = participant.mesh,
        .transform = transforms.empty() ? QMat4{} : transforms.front(),
        .instance_transforms = std::move(transforms),
        .instance_sampling_from_local = (*mesh_mappings)[participant_index],
        .double_sided = participant.double_sided,
        .cast_shadows = participant.cast_shadows,
        .suppress_source = true,
        .layout_key = preview_layout_key(1U, participant.source_entity,
                                         participant.visibility_entity),
    };
    preview.scene.meshes.push_back(std::move(mesh));
  }
  preview.scene.curves.reserve(source.curves.size());
  for (const CurveParticipant &participant : source.curves) {
    ModelingToolPreviewCurve curve{
        .source_entity = participant.source_entity,
        .visibility_entity = participant.visibility_entity,
        .curve = participant.curve,
        .material = participant.material,
        .solidify = participant.solidify,
        .solidify_resolution = participant.solidify_resolution,
        .solidify_radius = participant.solidify_radius,
        .suppress_source = true,
        .layout_key = preview_layout_key(2U, participant.source_entity,
                                         participant.visibility_entity),
    };
    curve.instance_transforms.reserve(layout->world_transforms.size());
    for (const QMat4 &root_world : layout->world_transforms) {
      curve.instance_transforms.push_back(scene_transform_multiply(
          root_world, participant.member_from_root));
    }
    preview.scene.curves.push_back(std::move(curve));
  }
  preview.scene.lights.reserve(source.lights.size());
  for (const LightParticipant &participant : source.lights) {
    ModelingToolPreviewLight light{
        .source_entity = participant.source_entity,
        .visibility_entity = participant.visibility_entity,
        .light = participant.light,
        .suppress_source = true,
        .layout_key = preview_layout_key(3U, participant.source_entity,
                                         participant.visibility_entity),
    };
    light.instance_transforms.reserve(layout->world_transforms.size());
    for (const QMat4 &root_world : layout->world_transforms) {
      light.instance_transforms.push_back(scene_transform_multiply(
          root_world, participant.member_from_root));
    }
    preview.scene.lights.push_back(std::move(light));
  }
  preview.scene.group_markers.reserve(source.markers.size());
  for (const MarkerParticipant &participant : source.markers) {
    std::vector<QMat4> transforms;
    transforms.reserve(layout->world_transforms.size());
    for (const QMat4 &root_world : layout->world_transforms) {
      transforms.push_back(scene_transform_multiply(
          root_world, participant.member_from_root));
    }
    preview.scene.group_markers.push_back({
        .source_entity = participant.source_entity,
        .local_min = participant.local_min,
        .local_max = participant.local_max,
        .instance_transforms = std::move(transforms),
        .layout_key = preview_layout_key(
            4U, participant.source_entity, participant.source_entity),
    });
  }
  cached_preview_ = std::move(preview);
  cached_preview_current_valid_ = true;
  publish_pending_defaults();
  return cached_preview_;
}

bool PathArrayToolService::blocks_document_mutation(EntityId entity) const {
  if (!active() || committing_) {
    return false;
  }
  const auto affects_entity_placement = [&](EntityId pinned) {
    return entity == pinned ||
           (scene_.exists(entity) && scene_.is_descendant_of(pinned, entity));
  };
  if (affects_entity_placement(session_->path.entity)) {
    return true;
  }
  if (!session_->source.has_value()) {
    return false;
  }
  return affects_entity_placement(session_->source->root) ||
         std::ranges::any_of(
             session_->source->hierarchy,
             [entity](const HierarchyNodeFingerprint &fingerprint) {
               return fingerprint.entity == entity;
             });
}

CommandResult PathArrayToolService::apply_selection_baseline(
    const EditorSelectionBoxBaseline &baseline) {
  PathArrayCurveSelectionMementoCommand command(selection_, baseline);
  return command.redo();
}

CommandResult PathArrayToolService::restore_selection_after_failed_apply(
    const EditorSelectionBoxBaseline &selection, CommandResult failure) {
  const CommandResult restored = apply_selection_baseline(selection);
  if (!restored.ok) {
    failure.changed = true;
    if (!failure.message.empty()) {
      failure.message += " ";
    }
    failure.message +=
        "Path Array could not restore the exact selection after failure.";
  }
  return failure;
}

CommandResult PathArrayToolService::apply() {
  if (!release_allows_tool(kPathArrayToolId)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!session_.has_value() || !session_->source.has_value()) {
    return command_invalid_result(
        "Select one mesh or group before applying Path Array.");
  }
  if (!active() || !source_is_current(*session_->source)) {
    return command_invalid_result(
        "Path Array Curve or source changed before the operation was applied.");
  }
  std::optional<PathArrayLayoutResult> layout = build_layout(*session_);
  if (!layout.has_value()) {
    return command_invalid_result("Path Array layout is no longer valid.");
  }

  const EditorSelectionBoxBaseline selection_before_apply =
      selection_.capture_box_selection_baseline();
  const EditorSelectionBoxBaseline curve_selection = session_->path_selection;
  const EntityId source_root = session_->source->root;
  const std::uint32_t count = session_->count;
  struct CommitGuard final {
    bool &committing;
    explicit CommitGuard(bool &value) : committing(value) {
      committing = true;
    }
    ~CommitGuard() { committing = false; }
  } commit_guard(committing_);

  EditorTransaction transaction = history_.begin_transaction("Path Array");
  if (!transaction.active()) {
    return command_invalid_result(
        "Cannot apply Path Array while another editor transaction is active.");
  }
  const auto rollback = [&](CommandResult failure) {
    if (transaction.active()) {
      const CommandResult canceled = transaction.cancel();
      if (!canceled.ok) {
        failure.changed = true;
        if (!failure.message.empty()) {
          failure.message += " ";
        }
        failure.message += "Path Array transaction rollback failed.";
      }
    }
    return restore_selection_after_failed_apply(selection_before_apply,
                                                std::move(failure));
  };

  std::vector<SceneStructuralDuplicateBatch> batches(count - 1U);
  for (SceneStructuralDuplicateBatch &batch : batches) {
    batch.roots.push_back(source_root);
  }
  const ModelingSelectionMode result_mode =
      scene_.kind(source_root) == EntityKind::Empty
          ? ModelingSelectionMode::Group
          : ModelingSelectionMode::Object;
  SceneStructuralDuplicateBatchResult duplicated =
      structural_lifecycle_.duplicate_hierarchy_batches_in_transaction(
          transaction, batches, result_mode, "Path Array", false);
  if (!duplicated.ok || !duplicated.changed ||
      duplicated.batches.size() != batches.size()) {
    return rollback(CommandResult{
        .ok = false,
        .changed = duplicated.changed,
        .message = duplicated.message.empty()
                       ? "Path Array hierarchy duplication failed."
                       : std::move(duplicated.message),
    });
  }

  const CommandResult recorded_front =
      transaction.record_executed_before_existing(
          std::make_unique<PathArrayCurveSelectionMementoCommand>(
              selection_, curve_selection));
  if (!recorded_front.ok) {
    return rollback(recorded_front);
  }

  std::vector<EntityId> roots;
  roots.reserve(count);
  roots.push_back(source_root);
  for (const SceneStructuralDuplicateResult &batch : duplicated.batches) {
    if (batch.clones.size() != 1U ||
        batch.clones.front().source != source_root ||
        batch.clones.front().duplicate == kInvalidEntityId) {
      return rollback(command_invalid_result(
          "Path Array duplicate mapping was incomplete."));
    }
    roots.push_back(batch.clones.front().duplicate);
  }
  if (roots.size() != layout->world_transforms.size()) {
    return rollback(command_invalid_result(
        "Path Array produced an incomplete transform layout."));
  }

  std::vector<DocumentTransformEdit> transforms;
  transforms.reserve(roots.size());
  for (std::size_t index = 0U; index < roots.size(); ++index) {
    const EntityId root = roots[index];
    const TransformComponent *before = scene_.transform(root);
    const EntityId parent = scene_.parent(root);
    if (before == nullptr ||
        (parent != kInvalidEntityId && !scene_.exists(parent))) {
      return rollback(command_invalid_result(
          "Path Array root placement changed during commit."));
    }
    const QMat4 parent_world =
        parent == kInvalidEntityId
            ? scene_transform_identity_matrix()
            : scene_.world_transform_matrix(parent);
    const std::optional<TransformComponent> after =
        try_scene_local_transform_preserving_world(
            parent_world, layout->world_transforms[index], *before);
    if (!after.has_value() ||
        !near_matrix(scene_transform_multiply(
                         parent_world,
                         scene_local_transform_matrix(*after)),
                     layout->world_transforms[index])) {
      return rollback(command_invalid_result(
          "Path Array root transform could not be represented."));
    }
    transforms.push_back({
        .entity = root,
        .before = *before,
        .after = *after,
    });
  }
  const DocumentCommandResult transformed = document_commands_.set_transforms(
      std::move(transforms), "Path Array", SceneTextureTransformKind::Combined);
  if (!transformed.ok) {
    return rollback(CommandResult{.ok = false,
                                  .changed = transformed.changed,
                                  .message = transformed.message});
  }

  auto ending_selection =
      std::make_unique<PathArrayCurveSelectionMementoCommand>(selection_,
                                                              curve_selection);
  const CommandResult restored_curve = ending_selection->redo();
  if (!restored_curve.ok) {
    return rollback(restored_curve);
  }
  const CommandResult recorded_end =
      transaction.record_executed(std::move(ending_selection));
  if (!recorded_end.ok) {
    return rollback(recorded_end);
  }
  const CommandResult committed = transaction.commit();
  if (!committed.ok || !committed.changed) {
    return restore_selection_after_failed_apply(selection_before_apply,
                                                committed);
  }

  publish_pending_defaults();
  session_.reset();
  cached_path_generation_ = 0U;
  clear_preview();
  issue_tool_settings_revision();
  return committed;
}

} // namespace quader
