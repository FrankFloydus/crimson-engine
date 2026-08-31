////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_host_publication_transaction.hpp"

#include <algorithm>
#include <exception>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

thread_local std::unordered_set<const SceneDocument *>
    active_publication_scenes;
thread_local std::unordered_set<const AssetRegistry *>
    active_publication_asset_registries;
thread_local std::unordered_set<const ModelingObjectBindingRegistry *>
    active_publication_binding_registries;
thread_local std::unordered_set<const modeling::ModelingSession *>
    active_publication_modeling_sessions;

[[nodiscard]] bool acquire_publication_owners(
    const SceneDocument *scene, const AssetRegistry *assets,
    const ModelingObjectBindingRegistry *bindings,
    const modeling::ModelingSession *session) noexcept {
  if (active_publication_scenes.contains(scene) ||
      active_publication_asset_registries.contains(assets) ||
      (bindings != nullptr &&
       active_publication_binding_registries.contains(bindings)) ||
      (session != nullptr &&
       active_publication_modeling_sessions.contains(session))) {
    return false;
  }
  bool scene_inserted = false;
  bool assets_inserted = false;
  bool bindings_inserted = false;
  bool session_inserted = false;
  try {
    scene_inserted = active_publication_scenes.insert(scene).second;
    assets_inserted =
        active_publication_asset_registries.insert(assets).second;
    bindings_inserted =
        bindings == nullptr ||
        active_publication_binding_registries.insert(bindings).second;
    session_inserted =
        session == nullptr ||
        active_publication_modeling_sessions.insert(session).second;
  } catch (...) {
    // The flags below identify every successful insertion that must be
    // unwound before construction reports the owner lease unavailable.
  }
  if (!scene_inserted || !assets_inserted || !bindings_inserted ||
      !session_inserted) {
    if (session != nullptr && session_inserted) {
      active_publication_modeling_sessions.erase(session);
    }
    if (bindings != nullptr && bindings_inserted) {
      active_publication_binding_registries.erase(bindings);
    }
    if (assets_inserted) {
      active_publication_asset_registries.erase(assets);
    }
    if (scene_inserted) {
      active_publication_scenes.erase(scene);
    }
    return false;
  }
  return true;
}

void release_publication_owners(
    const SceneDocument *scene, const AssetRegistry *assets,
    const ModelingObjectBindingRegistry *bindings,
    const modeling::ModelingSession *session) noexcept {
  active_publication_scenes.erase(scene);
  active_publication_asset_registries.erase(assets);
  if (bindings != nullptr) {
    active_publication_binding_registries.erase(bindings);
  }
  if (session != nullptr) {
    active_publication_modeling_sessions.erase(session);
  }
}

class PublicationConstructionGuard final {
public:
  PublicationConstructionGuard(
      const SceneDocument &scene, const AssetRegistry &assets,
      const ModelingObjectBindingRegistry *bindings,
      const modeling::ModelingSession *session, bool &owns_guard) noexcept
      : scene_(&scene), assets_(&assets), bindings_(bindings),
        session_(session), owns_guard_(&owns_guard) {}

  ~PublicationConstructionGuard() {
    if (!dismissed_ && owns_guard_ != nullptr && *owns_guard_) {
      release_publication_owners(scene_, assets_, bindings_, session_);
      *owns_guard_ = false;
    }
  }

  void dismiss() noexcept { dismissed_ = true; }

private:
  const SceneDocument *scene_ = nullptr;
  const AssetRegistry *assets_ = nullptr;
  const ModelingObjectBindingRegistry *bindings_ = nullptr;
  const modeling::ModelingSession *session_ = nullptr;
  bool *owns_guard_ = nullptr;
  bool dismissed_ = false;
};

} // namespace

ModelingHostProjectionCheckpoint::ModelingHostProjectionCheckpoint(
    SceneDocument &scene, AssetRegistry &assets,
    ModelingObjectBindingRegistry *bindings,
    ModelingHostProjectionCheckpointOptions options)
    : scene_(&scene), assets_(&assets), bindings_(bindings),
      captures_scene_(options.capture_scene),
      captures_assets_(options.capture_assets),
      captures_bindings_(options.capture_bindings),
      captures_mutation_fence_(options.capture_mutation_fence),
      preserves_concurrent_owner_changes_(
          options.preserve_concurrent_owner_changes) {
  if (preserves_concurrent_owner_changes_ && !captures_mutation_fence_) {
    return;
  }
  if (captures_scene_) {
    scene_checkpoint_.emplace(scene.capture_modeling_projection_checkpoint(
        options.scene_entities, options.scene_domains));
    if (!scene_checkpoint_->valid()) {
      return;
    }
    const SceneModelingProjectionCheckpointWork &scene_work =
        scene_checkpoint_->work();
    work_.scene_entity_records = scene_work.live_entity_record_count;
    work_.hierarchy_records = scene_work.structure_record_count;
  }
  if (captures_assets_) {
    asset_checkpoint_.emplace(
        assets.capture_modeling_projection_checkpoint(options.asset_ids));
    if (!asset_checkpoint_->valid()) {
      return;
    }
    work_.asset_records = asset_checkpoint_->participant_count();
  }
  if (captures_bindings_) {
    if (bindings == nullptr) {
      return;
    }
    binding_checkpoint_.emplace(
        bindings->capture_projection_checkpoint(options.modeling_object_ids));
    if (!binding_checkpoint_->valid()) {
      return;
    }
    work_.binding_records = binding_checkpoint_->work().record_count;
  }
  if (captures_mutation_fence_) {
    if (captures_scene_) {
      scene_mutation_fence_.emplace(
          scene.capture_modeling_projection_mutation_fence(
              options.scene_entities, options.scene_domains));
      if (!scene_mutation_fence_->valid()) {
        return;
      }
    }
    if (captures_assets_) {
      asset_mutation_fence_.emplace(
          assets.capture_modeling_projection_mutation_fence(
              options.asset_ids));
      if (!asset_mutation_fence_->valid()) {
        return;
      }
    }
    if (captures_bindings_) {
      if (bindings == nullptr) {
        return;
      }
      binding_mutation_fence_.emplace(
          bindings->capture_mutation_fence(options.modeling_object_ids));
      if (!binding_mutation_fence_->valid()) {
        return;
      }
    }
  }
  valid_ = true;
}

ModelingHostProjectionCheckpoint::ModelingHostProjectionCheckpoint(
    ModelingHostProjectionCheckpoint &&other) noexcept {
  *this = std::move(other);
}

ModelingHostProjectionCheckpoint &
ModelingHostProjectionCheckpoint::operator=(
    ModelingHostProjectionCheckpoint &&other) noexcept {
  if (this == &other) {
    return *this;
  }
  if (valid_) {
    // Replacing a live rollback capability would silently abandon its owner
    // lease. Callers must resolve/reset it before assigning another baseline.
    std::terminate();
  }
  scene_ = std::exchange(other.scene_, nullptr);
  assets_ = std::exchange(other.assets_, nullptr);
  bindings_ = std::exchange(other.bindings_, nullptr);
  captures_scene_ = std::exchange(other.captures_scene_, false);
  captures_assets_ = std::exchange(other.captures_assets_, false);
  captures_bindings_ = std::exchange(other.captures_bindings_, false);
  captures_mutation_fence_ =
      std::exchange(other.captures_mutation_fence_, false);
  preserves_concurrent_owner_changes_ =
      std::exchange(other.preserves_concurrent_owner_changes_, false);
  scene_checkpoint_ = std::move(other.scene_checkpoint_);
  asset_checkpoint_ = std::move(other.asset_checkpoint_);
  binding_checkpoint_ = std::move(other.binding_checkpoint_);
  scene_mutation_fence_ = std::move(other.scene_mutation_fence_);
  asset_mutation_fence_ = std::move(other.asset_mutation_fence_);
  binding_mutation_fence_ = std::move(other.binding_mutation_fence_);
  valid_ = std::exchange(other.valid_, false);
  work_ = std::exchange(other.work_, ModelingHostPublicationWork{});
  other.scene_checkpoint_.reset();
  other.asset_checkpoint_.reset();
  other.binding_checkpoint_.reset();
  other.scene_mutation_fence_.reset();
  other.asset_mutation_fence_.reset();
  other.binding_mutation_fence_.reset();
  return *this;
}

bool ModelingHostProjectionCheckpoint::valid() const noexcept {
  return valid_;
}

const ModelingHostPublicationWork &
ModelingHostProjectionCheckpoint::work() const noexcept {
  return work_;
}

bool ModelingHostProjectionCheckpoint::
    mutations_match_acknowledged_fence() const {
  if (!valid_) {
    return false;
  }
  if (!captures_mutation_fence_) {
    return true;
  }
  if (captures_scene_ &&
      (scene_ == nullptr || !scene_mutation_fence_.has_value() ||
       !scene_mutation_fence_->matches_current(*scene_))) {
    return false;
  }
  if (captures_assets_ &&
      (assets_ == nullptr || !asset_mutation_fence_.has_value() ||
       !asset_mutation_fence_->matches_current(*assets_))) {
    return false;
  }
  return !captures_bindings_ ||
         (bindings_ != nullptr && binding_mutation_fence_.has_value() &&
          binding_mutation_fence_->matches_current(*bindings_));
}

bool ModelingHostProjectionCheckpoint::acknowledge_current_mutations(
    std::span<const EntityId> newly_created_entities,
    std::span<const AssetId> newly_created_assets,
    std::span<const int> newly_created_modeling_object_ids) {
  if (!valid_ || !captures_mutation_fence_) {
    return false;
  }
  if (captures_scene_ &&
      (scene_ == nullptr || !scene_mutation_fence_.has_value() ||
       !scene_mutation_fence_->acknowledge_current(
           *scene_, newly_created_entities))) {
    return false;
  }
  if (captures_assets_ &&
      (assets_ == nullptr || !asset_mutation_fence_.has_value() ||
       !asset_mutation_fence_->acknowledge_current(
           *assets_, newly_created_assets))) {
    return false;
  }
  return !captures_bindings_ ||
         (bindings_ != nullptr && binding_mutation_fence_.has_value() &&
          binding_mutation_fence_->acknowledge_current(
              *bindings_, newly_created_modeling_object_ids));
}

bool ModelingHostProjectionCheckpoint::can_restore(
    std::span<const EntityId> created_entities,
    std::span<const AssetId> created_assets,
    ModelingHostProjectionRestoreMode mode) const {
  if (!valid_ || !mutations_match_acknowledged_fence()) {
    return false;
  }
  const bool require_preserving_restore =
      mode == ModelingHostProjectionRestoreMode::PreserveConcurrent;
  if (require_preserving_restore &&
      !preserves_concurrent_owner_changes_) {
    return false;
  }
  if (captures_scene_ &&
      (scene_ == nullptr || !scene_checkpoint_.has_value())) {
    return false;
  }
  if (captures_scene_) {
    const bool scene_preserving =
        preserves_concurrent_owner_changes_ &&
        scene_mutation_fence_.has_value() &&
        scene_->can_restore_modeling_projection_checkpoint_preserving_concurrent_changes(
            *scene_checkpoint_, *scene_mutation_fence_, created_entities);
    const bool scene_ready =
        require_preserving_restore
            ? scene_preserving
            : scene_->can_restore_modeling_projection_checkpoint(
                  *scene_checkpoint_, created_entities) ||
                  scene_preserving;
    if (!scene_ready) {
      return false;
    }
  }
  if (captures_assets_ &&
      (assets_ == nullptr || !asset_checkpoint_.has_value())) {
    return false;
  }
  if (captures_assets_) {
    const bool assets_preserving =
        preserves_concurrent_owner_changes_ &&
        asset_mutation_fence_.has_value() &&
        assets_->can_restore_modeling_projection_checkpoint_preserving_concurrent_changes(
            *asset_checkpoint_, *asset_mutation_fence_, created_assets);
    const bool assets_ready =
        require_preserving_restore
            ? assets_preserving
            : assets_->can_restore_modeling_projection_checkpoint(
                  *asset_checkpoint_, created_assets) ||
                  assets_preserving;
    if (!assets_ready) {
      return false;
    }
  }
  if (!captures_bindings_) {
    return true;
  }
  if (bindings_ == nullptr || !binding_checkpoint_.has_value()) {
    return false;
  }
  const bool bindings_preserving =
      preserves_concurrent_owner_changes_ &&
      binding_mutation_fence_.has_value() &&
      bindings_->can_restore_projection_checkpoint_preserving_concurrent_changes(
          *binding_checkpoint_, *binding_mutation_fence_);
  return require_preserving_restore
             ? bindings_preserving
             : bindings_->can_restore_projection_checkpoint(
                   *binding_checkpoint_) ||
                   bindings_preserving;
}

bool ModelingHostProjectionCheckpoint::restore(
    std::span<const EntityId> created_entities,
    std::span<const AssetId> created_assets,
    ModelingHostProjectionRestoreMode mode) noexcept {
  enum class RestoreMode { Skip, Exact, PreserveConcurrent };
  RestoreMode scene_mode = RestoreMode::Skip;
  RestoreMode asset_mode = RestoreMode::Skip;
  RestoreMode binding_mode = RestoreMode::Skip;
  try {
    if (!valid_ || !mutations_match_acknowledged_fence()) {
      return false;
    }
    const bool require_preserving_restore =
        mode == ModelingHostProjectionRestoreMode::PreserveConcurrent;
    if (require_preserving_restore &&
        !preserves_concurrent_owner_changes_) {
      return false;
    }
    if (captures_scene_) {
      if (scene_ == nullptr || !scene_checkpoint_.has_value()) {
        return false;
      }
      if (!require_preserving_restore &&
          scene_->can_restore_modeling_projection_checkpoint(
              *scene_checkpoint_, created_entities)) {
        scene_mode = RestoreMode::Exact;
      } else if (preserves_concurrent_owner_changes_ &&
                 scene_mutation_fence_.has_value() &&
                 scene_->can_restore_modeling_projection_checkpoint_preserving_concurrent_changes(
                     *scene_checkpoint_, *scene_mutation_fence_,
                     created_entities)) {
        scene_mode = RestoreMode::PreserveConcurrent;
      } else {
        return false;
      }
    }
    if (captures_assets_) {
      if (assets_ == nullptr || !asset_checkpoint_.has_value()) {
        return false;
      }
      if (!require_preserving_restore &&
          assets_->can_restore_modeling_projection_checkpoint(
              *asset_checkpoint_, created_assets)) {
        asset_mode = RestoreMode::Exact;
      } else if (preserves_concurrent_owner_changes_ &&
                 asset_mutation_fence_.has_value() &&
                 assets_->can_restore_modeling_projection_checkpoint_preserving_concurrent_changes(
                     *asset_checkpoint_, *asset_mutation_fence_,
                     created_assets)) {
        asset_mode = RestoreMode::PreserveConcurrent;
      } else {
        return false;
      }
    }
    if (captures_bindings_) {
      if (bindings_ == nullptr || !binding_checkpoint_.has_value()) {
        return false;
      }
      if (!require_preserving_restore &&
          bindings_->can_restore_projection_checkpoint(
              *binding_checkpoint_)) {
        binding_mode = RestoreMode::Exact;
      } else if (preserves_concurrent_owner_changes_ &&
                 binding_mutation_fence_.has_value() &&
                 bindings_->can_restore_projection_checkpoint_preserving_concurrent_changes(
                     *binding_checkpoint_, *binding_mutation_fence_)) {
        binding_mode = RestoreMode::PreserveConcurrent;
      } else {
        return false;
      }
    }
  } catch (...) {
    return false;
  }
  // Owner checkpoints are rollback capabilities, not reusable snapshots. A
  // second restore could rewind owner revisions across unrelated later work.
  valid_ = false;
  try {
    const bool scene_restored =
        scene_mode == RestoreMode::Skip ||
        (scene_mode == RestoreMode::Exact
             ? scene_->restore_modeling_projection_checkpoint(
                   *scene_checkpoint_, created_entities)
             : scene_->restore_modeling_projection_checkpoint_preserving_concurrent_changes(
                   *scene_checkpoint_, *scene_mutation_fence_,
                   created_entities));
    if (!scene_restored) {
      std::terminate();
    }
    const bool assets_restored =
        asset_mode == RestoreMode::Skip ||
        (asset_mode == RestoreMode::Exact
             ? assets_->restore_modeling_projection_checkpoint(
                   *asset_checkpoint_, created_assets)
             : assets_->restore_modeling_projection_checkpoint_preserving_concurrent_changes(
                   *asset_checkpoint_, *asset_mutation_fence_,
                   created_assets));
    if (!assets_restored) {
      std::terminate();
    }
    const bool bindings_restored =
        binding_mode == RestoreMode::Skip ||
        (binding_mode == RestoreMode::Exact
             ? bindings_->restore_projection_checkpoint(
                   *binding_checkpoint_)
             : bindings_->restore_projection_checkpoint_preserving_concurrent_changes(
                   *binding_checkpoint_, *binding_mutation_fence_));
    if (!bindings_restored) {
      std::terminate();
    }
  } catch (...) {
    // Every owner was preflighted before the first mutation. A failure in the
    // synchronous apply phase would otherwise expose a cross-owner split.
    std::terminate();
  }
  return true;
}

ModelingHostPublicationTransaction::ModelingHostPublicationTransaction(
    SceneDocument &scene, AssetRegistry &assets,
    ModelingObjectBindingRegistry &bindings,
    modeling::ModelingSession &session, RestoreHostState restore_host_state,
    ModelingHostPublicationTransactionOptions options)
    : ModelingHostPublicationTransaction(
          scene, assets, &bindings, &session, std::move(restore_host_state),
          options) {}

ModelingHostPublicationTransaction::ModelingHostPublicationTransaction(
    SceneDocument &scene, AssetRegistry &assets,
    RestoreHostState restore_host_state,
    ModelingHostPublicationTransactionOptions options)
    : ModelingHostPublicationTransaction(
          scene, assets, nullptr, nullptr, std::move(restore_host_state),
          options) {}

ModelingHostPublicationTransaction::ModelingHostPublicationTransaction(
    SceneDocument &scene, AssetRegistry &assets,
    ModelingObjectBindingRegistry *bindings,
    modeling::ModelingSession *session, RestoreHostState restore_host_state,
    ModelingHostPublicationTransactionOptions options)
    : scene_(scene), assets_(assets), bindings_(bindings), session_(session),
      restore_host_state_(std::move(restore_host_state)), options_(options),
      sdk_undo_mark_(session != nullptr ? session->undo_depth() : 0u) {
  owns_nesting_guard_ = acquire_publication_owners(
      &scene_, &assets_, bindings_, session_);
  if (!owns_nesting_guard_) {
    ready_ = false;
    active_ = false;
    return;
  }
  PublicationConstructionGuard construction_guard(
      scene_, assets_, bindings_, session_, owns_nesting_guard_);
  affected_scene_entities_.assign(options.affected_scene_entities.begin(),
                                  options.affected_scene_entities.end());
  affected_asset_ids_.assign(options.affected_asset_ids.begin(),
                             options.affected_asset_ids.end());
  affected_modeling_object_ids_.assign(
      options.affected_modeling_object_ids.begin(),
      options.affected_modeling_object_ids.end());
  affected_sdk_object_ids_.assign(options.affected_sdk_object_ids.begin(),
                                  options.affected_sdk_object_ids.end());
  options_.affected_scene_entities = affected_scene_entities_;
  options_.affected_asset_ids = affected_asset_ids_;
  options_.affected_modeling_object_ids = affected_modeling_object_ids_;
  options_.affected_sdk_object_ids = affected_sdk_object_ids_;
  const auto reject_unbounded_capture = [this]() {
    ready_ = false;
    active_ = false;
    release_nesting_guard();
  };
  if (session_ != nullptr &&
      options_.sdk_rollback == ModelingSdkRollbackPolicy::Snapshot &&
      session_->mutation_checkpoint_active()) {
    reject_unbounded_capture();
    return;
  }
  if (options_.checkpoint_scope ==
      ModelingHostPublicationCheckpointScope::Unspecified) {
    reject_unbounded_capture();
    return;
  }
  const ModelingHostPublicationCheckpointScope asset_scope =
      options_.asset_checkpoint_scope.value_or(options_.checkpoint_scope);
  const ModelingHostPublicationCheckpointScope scene_scope =
      options_.scene_checkpoint_scope.value_or(options_.checkpoint_scope);
  const bool missing_mutation_participants =
      options_.sdk_rollback == ModelingSdkRollbackPolicy::MutationCheckpoint &&
      options_.affected_sdk_object_ids.empty();
  const bool missing_binding_owner = options_.capture_bindings &&
                                     bindings_ == nullptr;
  const bool missing_binding_participants =
      options_.capture_bindings && bindings_ != nullptr &&
      options_.affected_modeling_object_ids.empty();
  if (asset_scope == ModelingHostPublicationCheckpointScope::Unspecified ||
      scene_scope == ModelingHostPublicationCheckpointScope::Unspecified ||
      missing_mutation_participants || missing_binding_owner ||
      missing_binding_participants) {
    reject_unbounded_capture();
    return;
  }
  std::vector<AssetId> affected_assets(options_.affected_asset_ids.begin(),
                                       options_.affected_asset_ids.end());
  if (affected_assets.empty() && options_.capture_assets) {
    affected_assets.reserve(options_.affected_scene_entities.size());
    for (const EntityId entity : options_.affected_scene_entities) {
      if (asset_scope ==
              ModelingHostPublicationCheckpointScope::MeshProjection ||
          asset_scope ==
              ModelingHostPublicationCheckpointScope::ModelingProjection) {
        if (const MeshComponent *mesh = scene.mesh(entity)) {
          affected_assets.push_back(mesh->mesh);
        }
      }
      if (asset_scope ==
              ModelingHostPublicationCheckpointScope::CurveProjection ||
          asset_scope ==
              ModelingHostPublicationCheckpointScope::ModelingProjection) {
        if (const CurveComponent *curve = scene.curve(entity)) {
          affected_assets.push_back(curve->curve);
        }
      }
    }
  }
  std::ranges::sort(affected_assets);
  affected_assets.erase(
      std::unique(affected_assets.begin(), affected_assets.end()),
      affected_assets.end());

  const bool projection_captures_assets = options_.capture_assets;
  const bool projection_captures_scene = options_.capture_scene;
  const bool projection_captures_bindings = options_.capture_bindings;
  if (projection_captures_assets || projection_captures_scene ||
      projection_captures_bindings) {
    SceneModelingProjectionDomain domains =
        SceneModelingProjectionDomain::None;
    if (projection_captures_scene && options_.capture_scene_entities) {
      if (scene_scope ==
              ModelingHostPublicationCheckpointScope::MeshProjection ||
          scene_scope ==
              ModelingHostPublicationCheckpointScope::ModelingProjection) {
        domains = domains | SceneModelingProjectionDomain::MeshBindings;
      }
      if (scene_scope ==
              ModelingHostPublicationCheckpointScope::CurveProjection ||
          scene_scope ==
              ModelingHostPublicationCheckpointScope::ModelingProjection) {
        domains = domains | SceneModelingProjectionDomain::CurveBindings;
      }
    }
    if (projection_captures_scene && options_.capture_scene_selection) {
      domains = domains | SceneModelingProjectionDomain::Selection;
    }
    if (projection_captures_scene && options_.capture_hierarchy) {
      domains = domains | SceneModelingProjectionDomain::Structure;
    }
    projection_checkpoint_.emplace(
        scene, assets, bindings_,
        ModelingHostProjectionCheckpointOptions{
            .capture_assets = projection_captures_assets,
            .capture_scene = projection_captures_scene,
            .capture_bindings = projection_captures_bindings,
            .scene_domains = domains,
            .scene_entities = options_.affected_scene_entities,
            .asset_ids = affected_assets,
            .modeling_object_ids = options_.affected_modeling_object_ids,
        });
    if (!projection_checkpoint_->valid()) {
      reject_unbounded_capture();
      return;
    }
    const ModelingHostPublicationWork &projection_work =
        projection_checkpoint_->work();
    work_.asset_records = projection_work.asset_records;
    work_.scene_entity_records = projection_work.scene_entity_records;
    work_.hierarchy_records = projection_work.hierarchy_records;
    work_.binding_records = projection_work.binding_records;
  }
  // Capture SDK lifecycle state last. A mutation checkpoint temporarily owns
  // the session redo branch, so no later host allocation may throw and orphan
  // that scope during constructor unwinding.
  if (session_ != nullptr &&
      options_.sdk_rollback == ModelingSdkRollbackPolicy::Snapshot) {
    sdk_snapshot_.emplace(*session_);
    work_.full_sdk_snapshot = true;
  } else if (options_.sdk_rollback ==
                 ModelingSdkRollbackPolicy::PreviewCheckpoint &&
             session_ != nullptr) {
    const modeling::PreviewTransactionSummary preview =
        session_->preview_summary();
    if (preview.active) {
      modeling::Result<modeling::SessionPreviewCheckpoint> checkpoint =
          session_->capture_preview_checkpoint(preview.id);
      if (checkpoint.ok()) {
        preview_checkpoint_.emplace(std::move(checkpoint).value());
      }
    }
    if (!preview_checkpoint_.has_value()) {
      // Preview publications never fall back to a whole-session snapshot.
      reject_unbounded_capture();
      return;
    }
  } else if (session_ != nullptr &&
             options_.sdk_rollback ==
                 ModelingSdkRollbackPolicy::MutationCheckpoint) {
    modeling::Result<modeling::SessionMutationCheckpoint> checkpoint =
        session_->capture_mutation_checkpoint(
            options_.affected_sdk_object_ids);
    if (!checkpoint.ok()) {
      reject_unbounded_capture();
      return;
    }
    mutation_checkpoint_.emplace(std::move(checkpoint).value());
    work_.sdk_participant_records =
        options_.affected_sdk_object_ids.size();
  }
  construction_guard.dismiss();
}

ModelingHostPublicationTransaction::~ModelingHostPublicationTransaction() {
  if (active_ && !rollback()) {
    // A publication that cannot restore its owner checkpoints has no truthful
    // changed=false result. Treat this as an invariant failure instead of
    // letting partially published editor state escape the transaction.
    std::terminate();
  }
  release_nesting_guard();
}

void ModelingHostPublicationTransaction::release_nesting_guard() noexcept {
  if (owns_nesting_guard_) {
    release_publication_owners(&scene_, &assets_, bindings_, session_);
    owns_nesting_guard_ = false;
  }
}

std::size_t
ModelingHostPublicationTransaction::sdk_undo_mark() const noexcept {
  return sdk_undo_mark_;
}

bool ModelingHostPublicationTransaction::active() const noexcept {
  return active_;
}

bool ModelingHostPublicationTransaction::ready() const noexcept {
  return ready_;
}

const ModelingHostPublicationWork &
ModelingHostPublicationTransaction::work() const noexcept {
  return work_;
}

void ModelingHostPublicationTransaction::track_created_entity(EntityId entity) {
  if (active_ && entity != kInvalidEntityId) {
    created_entities_.push_back(entity);
  }
}

void ModelingHostPublicationTransaction::track_created_entities(
    std::span<const EntityId> entities) {
  if (active_) {
    created_entities_.insert(created_entities_.end(), entities.begin(),
                             entities.end());
  }
}

void ModelingHostPublicationTransaction::track_created_asset(AssetId asset) {
  if (active_ && asset != kInvalidAssetId) {
    created_assets_.push_back(asset);
  }
}

void ModelingHostPublicationTransaction::track_created_assets(
    std::span<const AssetId> assets) {
  if (active_) {
    for (const AssetId asset : assets) {
      track_created_asset(asset);
    }
  }
}

void ModelingHostPublicationTransaction::track_started_preview(
    modeling::PreviewId preview_id) {
  if (active_ && preview_id.valid()) {
    started_preview_ = preview_id;
  }
}

bool ModelingHostPublicationTransaction::rollback() noexcept {
  if (!active_) {
    return rollback_succeeded_;
  }
  bool native_preflighted = true;
  bool sdk_preflighted = true;
  try {
    if ((!created_entities_.empty() && !options_.capture_scene) ||
        (!created_assets_.empty() && !options_.capture_assets)) {
      native_preflighted = false;
    }
    if (projection_checkpoint_.has_value()) {
      native_preflighted = native_preflighted &&
                           projection_checkpoint_->can_restore(
                               created_entities_, created_assets_);
    }
  } catch (...) {
    native_preflighted = false;
  }
  try {
    if (session_ != nullptr &&
        options_.sdk_rollback == ModelingSdkRollbackPolicy::Snapshot) {
      sdk_preflighted = !session_->mutation_checkpoint_active();
    }
    if (session_ != nullptr && options_.sdk_rollback ==
                                   ModelingSdkRollbackPolicy::MutationCheckpoint &&
        mutation_checkpoint_.has_value()) {
      sdk_preflighted =
          session_->preflight_mutation_checkpoint(*mutation_checkpoint_).ok();
    }
    if (session_ != nullptr &&
        options_.sdk_rollback ==
            ModelingSdkRollbackPolicy::PreviewCheckpoint &&
        preview_checkpoint_.has_value()) {
      sdk_preflighted =
          session_->preflight_preview_checkpoint(*preview_checkpoint_).ok();
    }
    if (session_ != nullptr &&
        options_.sdk_rollback ==
            ModelingSdkRollbackPolicy::CancelStartedPreview &&
        started_preview_.has_value()) {
      const modeling::PreviewTransactionSummary current_preview =
          session_->preview_summary();
      sdk_preflighted = current_preview.active &&
                        current_preview.id == *started_preview_;
    }
  } catch (...) {
    sdk_preflighted = false;
  }
  if (!sdk_preflighted && mutation_checkpoint_.has_value()) {
    // Keep the current branch but consume the temporary redo/checkpoint lease.
    // This emergency path deliberately skips scope validation because normal
    // commit/restore both rejected it already.
    const modeling::Result<modeling::OperationResult> discarded =
        session_->discard_mutation_checkpoint(
            std::move(*mutation_checkpoint_));
    if (!discarded.ok()) {
      std::terminate();
    }
  }
  if (!native_preflighted && sdk_preflighted &&
      mutation_checkpoint_.has_value()) {
    // Native owners remain untouched. Preserve the current SDK branch while
    // consuming its temporary redo/checkpoint lease.
    const modeling::Result<modeling::OperationResult> retained =
        session_->commit_mutation_checkpoint(std::move(*mutation_checkpoint_));
    if (!retained.ok()) {
      std::terminate();
    }
  }
  // Explicit rollback is one-shot. A failed restore is reported to the caller
  // and will not trigger a second destructor attempt against partial state.
  active_ = false;
  if (!native_preflighted || !sdk_preflighted) {
    rollback_succeeded_ = false;
    release_nesting_guard();
    return false;
  }
  bool restored = true;
  try {
    if (session_ != nullptr &&
        options_.sdk_rollback == ModelingSdkRollbackPolicy::Snapshot &&
        sdk_snapshot_.has_value()) {
      session_->restore_snapshot_preserving_identity_high_water(*sdk_snapshot_);
    } else if (session_ != nullptr && options_.sdk_rollback ==
                   ModelingSdkRollbackPolicy::PreviewCheckpoint &&
               preview_checkpoint_.has_value()) {
      const modeling::Result<modeling::OperationResult> preview_restored =
          session_->restore_preview_checkpoint(*preview_checkpoint_);
      if (!preview_restored.ok()) {
        // Aggregate preflight already succeeded. Continuing into native
        // restore after an SDK apply refusal would split the owners.
        std::terminate();
      }
    } else if (session_ != nullptr && options_.sdk_rollback ==
                   ModelingSdkRollbackPolicy::PreviewCheckpoint &&
               sdk_snapshot_.has_value()) {
      session_->restore_snapshot_preserving_identity_high_water(*sdk_snapshot_);
    } else if (session_ != nullptr && options_.sdk_rollback ==
                    ModelingSdkRollbackPolicy::MutationCheckpoint &&
               mutation_checkpoint_.has_value()) {
      modeling::Result<modeling::OperationResult> sdk_restored =
          session_->restore_mutation_checkpoint(
              std::move(*mutation_checkpoint_));
      if (!sdk_restored.ok()) {
        std::terminate();
      }
    } else if (session_ != nullptr && options_.sdk_rollback ==
                    ModelingSdkRollbackPolicy::CancelStartedPreview &&
               started_preview_.has_value()) {
      const modeling::Result<modeling::OperationResult> canceled =
          session_->cancel_preview(*started_preview_);
      if (!canceled.ok()) {
        std::terminate();
      }
    }
    if (projection_checkpoint_.has_value()) {
      restored = projection_checkpoint_->restore(created_entities_,
                                                  created_assets_) &&
                 restored;
    }
    if (restore_host_state_) {
      restore_host_state_.restore();
    }
  } catch (...) {
    restored = false;
  }
  rollback_succeeded_ = restored;
  release_nesting_guard();
  return rollback_succeeded_;
}

bool ModelingHostPublicationTransaction::commit() noexcept {
  if (!active_ || !ready_) {
    return false;
  }
  if (session_ != nullptr && options_.sdk_rollback ==
                                 ModelingSdkRollbackPolicy::MutationCheckpoint &&
      mutation_checkpoint_.has_value()) {
    try {
      modeling::Result<modeling::OperationResult> sdk_committed =
          session_->commit_mutation_checkpoint(
              std::move(*mutation_checkpoint_));
      if (!sdk_committed.ok()) {
        [[maybe_unused]] const bool compensated = rollback();
        return false;
      }
    } catch (...) {
      [[maybe_unused]] const bool compensated = rollback();
      return false;
    }
  }
  active_ = false;
  rollback_succeeded_ = true;
  release_nesting_guard();
  return true;
}

} // namespace quader
