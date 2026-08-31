////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime/modeling_object_binding_registry.hpp"

#include "scene/scene_document.hpp"

#include <algorithm>
#include <utility>

namespace quader {

struct ModelingObjectBindingMutationFence::State final {
  struct Record final {
    int modeling_object_id = kInvalidModelingObjectId;
    std::uint64_t mutation_count = 0u;
  };

  const ModelingObjectBindingRegistry *owner = nullptr;
  std::vector<Record> records;
};

namespace {

[[nodiscard]] constexpr std::uint64_t sdk_object_key(modeling::ObjectId object) {
  return (static_cast<std::uint64_t>(object.generation) << 32U) |
         static_cast<std::uint64_t>(object.index);
}

[[nodiscard]] ModelingObjectSceneIdentity
capture_scene_identity(const SceneDocument &scene, EntityId entity) {
  ModelingObjectSceneIdentity identity;
  const std::optional<SceneEntityState> state =
      capture_scene_entity_state(scene, entity);
  if (!state.has_value()) {
    return identity;
  }
  static_cast<SceneEntityState &>(identity) = *state;
  return identity;
}

} // namespace

ModelingObjectBindingMutationFence::ModelingObjectBindingMutationFence() =
    default;
ModelingObjectBindingMutationFence::~ModelingObjectBindingMutationFence() =
    default;
ModelingObjectBindingMutationFence::ModelingObjectBindingMutationFence(
    ModelingObjectBindingMutationFence &&) noexcept = default;
ModelingObjectBindingMutationFence &
ModelingObjectBindingMutationFence::operator=(
    ModelingObjectBindingMutationFence &&) noexcept = default;

bool ModelingObjectBindingMutationFence::valid() const noexcept {
  return state_ != nullptr;
}

std::size_t ModelingObjectBindingMutationFence::size() const noexcept {
  return state_ == nullptr ? 0u : state_->records.size();
}

bool ModelingObjectBindingMutationFence::matches_current(
    const ModelingObjectBindingRegistry &owner,
    ModelingObjectBindingMutationFenceWork *reported_work) const {
  ModelingObjectBindingMutationFenceWork work;
  const auto report = [&](bool result) {
    if (reported_work != nullptr) {
      *reported_work = work;
    }
    return result;
  };
  if (!valid() || state_->owner != &owner) {
    return report(false);
  }

  work.participant_record_count = state_->records.size();
  for (const State::Record &record : state_->records) {
    ++work.mutation_counter_lookup_count;
    const auto current =
        owner.mutation_count_by_object_.find(record.modeling_object_id);
    const std::uint64_t mutation_count =
        current == owner.mutation_count_by_object_.end() ? 0u
                                                         : current->second;
    if (mutation_count != record.mutation_count) {
      return report(false);
    }
  }
  return report(true);
}

bool ModelingObjectBindingMutationFence::acknowledge_current(
    const ModelingObjectBindingRegistry &owner,
    std::span<const int> newly_created_modeling_object_ids,
    ModelingObjectBindingMutationFenceWork *reported_work) {
  ModelingObjectBindingMutationFenceWork work{
      .input_reference_count = newly_created_modeling_object_ids.size(),
  };
  const auto report = [&](bool result) {
    if (reported_work != nullptr) {
      *reported_work = work;
    }
    return result;
  };
  if (!valid() || state_->owner != &owner) {
    return report(false);
  }

  std::vector<int> modeling_object_ids;
  modeling_object_ids.reserve(state_->records.size() +
                              newly_created_modeling_object_ids.size());
  for (const State::Record &record : state_->records) {
    modeling_object_ids.push_back(record.modeling_object_id);
  }
  modeling_object_ids.insert(modeling_object_ids.end(),
                             newly_created_modeling_object_ids.begin(),
                             newly_created_modeling_object_ids.end());
  std::erase(modeling_object_ids, kInvalidModelingObjectId);
  std::ranges::sort(modeling_object_ids);
  modeling_object_ids.erase(
      std::unique(modeling_object_ids.begin(), modeling_object_ids.end()),
      modeling_object_ids.end());

  std::vector<State::Record> records;
  records.reserve(modeling_object_ids.size());
  for (const int modeling_object_id : modeling_object_ids) {
    ++work.mutation_counter_lookup_count;
    const auto current =
        owner.mutation_count_by_object_.find(modeling_object_id);
    records.push_back({
        .modeling_object_id = modeling_object_id,
        .mutation_count =
            current == owner.mutation_count_by_object_.end() ? 0u
                                                             : current->second,
    });
  }
  work.participant_record_count = records.size();
  state_->records = std::move(records);
  work_ = work;
  return report(true);
}

std::size_t ModelingObjectBindingRegistry::size() const {
  std::unordered_set<int> records;
  records.reserve(entities_by_object_.size() + sdk_objects_by_object_.size() +
                  scene_identity_by_object_.size());
  for (const auto &[modeling_object_id, entity] : entities_by_object_) {
    (void)entity;
    records.insert(modeling_object_id);
  }
  for (const auto &[modeling_object_id, object] : sdk_objects_by_object_) {
    (void)object;
    records.insert(modeling_object_id);
  }
  for (const auto &[modeling_object_id, identity] :
       scene_identity_by_object_) {
    (void)identity;
    records.insert(modeling_object_id);
  }
  return records.size();
}

bool ModelingObjectBindingRegistry::empty() const {
  return entities_by_object_.empty() && sdk_objects_by_object_.empty() &&
         scene_identity_by_object_.empty();
}

std::uint64_t ModelingObjectBindingRegistry::revision() const {
  return revision_;
}

std::optional<EntityId>
ModelingObjectBindingRegistry::entity_for_modeling_object(
    int modeling_object_id) const {
  const auto it = entities_by_object_.find(modeling_object_id);
  return it == entities_by_object_.end() ? std::nullopt
                                         : std::optional<EntityId>{it->second};
}

std::optional<int>
ModelingObjectBindingRegistry::modeling_object_for_entity(EntityId entity) const {
  const auto it = objects_by_entity_.find(entity);
  return it == objects_by_entity_.end() ? std::nullopt
                                        : std::optional<int>{it->second};
}

std::vector<std::pair<int, EntityId>>
ModelingObjectBindingRegistry::object_entity_bindings_by_modeling_object() const {
  std::vector<std::pair<int, EntityId>> bindings(entities_by_object_.begin(),
                                                 entities_by_object_.end());
  std::sort(bindings.begin(), bindings.end(),
            [](const auto &lhs, const auto &rhs) {
              return lhs.first < rhs.first;
            });
  return bindings;
}

std::optional<modeling::ObjectId>
ModelingObjectBindingRegistry::sdk_object_for_modeling_object(
    int modeling_object_id) const {
  const auto it = sdk_objects_by_object_.find(modeling_object_id);
  return it == sdk_objects_by_object_.end()
             ? std::nullopt
             : std::optional<modeling::ObjectId>{it->second};
}

std::optional<int> ModelingObjectBindingRegistry::modeling_object_for_sdk_object(
    modeling::ObjectId object) const {
  const auto it = objects_by_sdk_object_.find(sdk_object_key(object));
  return it == objects_by_sdk_object_.end() ? std::nullopt
                                            : std::optional<int>{it->second};
}

const std::map<int, modeling::ObjectId> &
ModelingObjectBindingRegistry::sdk_object_bindings_by_modeling_object() const {
  return sdk_objects_by_object_;
}

std::optional<ModelingObjectSceneIdentity>
ModelingObjectBindingRegistry::scene_identity_for_modeling_object(
    int modeling_object_id) const {
  const auto it = scene_identity_by_object_.find(modeling_object_id);
  return it == scene_identity_by_object_.end()
             ? std::nullopt
             : std::optional<ModelingObjectSceneIdentity>{it->second};
}

std::unordered_map<int, ModelingObjectSceneIdentity>
ModelingObjectBindingRegistry::capture_scene_identities_for_modeling_objects(
    const SceneDocument &scene,
    const std::unordered_set<int> &modeling_object_ids) const {
  std::unordered_map<int, ModelingObjectSceneIdentity> identities;
  identities.reserve(modeling_object_ids.size());
  for (const int modeling_object_id : modeling_object_ids) {
    const auto entity = entities_by_object_.find(modeling_object_id);
    if (entity != entities_by_object_.end() && scene.exists(entity->second)) {
      identities.emplace(modeling_object_id,
                         capture_scene_identity(scene, entity->second));
      continue;
    }
    const auto identity = scene_identity_by_object_.find(modeling_object_id);
    if (identity != scene_identity_by_object_.end()) {
      identities.emplace(modeling_object_id, identity->second);
    }
  }
  return identities;
}

std::vector<ModelingObjectSceneIdentity>
ModelingObjectBindingRegistry::capture_scene_identities_for_modeling_objects(
    const SceneDocument &scene, std::span<const int> modeling_object_ids) const {
  std::vector<ModelingObjectSceneIdentity> identities;
  identities.reserve(modeling_object_ids.size());
  for (const int modeling_object_id : modeling_object_ids) {
    const auto entity = entities_by_object_.find(modeling_object_id);
    if (entity != entities_by_object_.end() && scene.exists(entity->second)) {
      identities.push_back(capture_scene_identity(scene, entity->second));
      continue;
    }
    const auto identity = scene_identity_by_object_.find(modeling_object_id);
    if (identity != scene_identity_by_object_.end()) {
      identities.push_back(identity->second);
    }
  }
  return identities;
}

std::vector<ModelingObjectRecord>
ModelingObjectBindingRegistry::records(const SceneDocument &scene) const {
  std::vector<ModelingObjectRecord> records;
  records.reserve(entities_by_object_.size());
  for (const auto &[modeling_object_id, entity] : entities_by_object_) {
    if (!scene.exists(entity)) {
      continue;
    }
    ModelingObjectRecord record;
    record.modeling_object_id = modeling_object_id;
    record.entity = entity;
    if (const MeshComponent *mesh = scene.mesh(entity)) {
      record.mesh = mesh->mesh;
    }
    if (const NameComponent *name = scene.name(entity)) {
      record.name = name->name;
    }
    records.push_back(std::move(record));
  }
  std::sort(records.begin(), records.end(),
            [](const auto &lhs, const auto &rhs) {
              return lhs.entity < rhs.entity;
            });
  return records;
}

ModelingObjectBindingMutationFence
ModelingObjectBindingRegistry::capture_mutation_fence(
    std::span<const int> modeling_object_ids) const {
  ModelingObjectBindingMutationFence fence;
  fence.work_.input_reference_count = modeling_object_ids.size();
  std::vector<int> ordered(modeling_object_ids.begin(),
                           modeling_object_ids.end());
  std::erase(ordered, kInvalidModelingObjectId);
  std::ranges::sort(ordered);
  ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());

  auto state = std::make_unique<ModelingObjectBindingMutationFence::State>();
  state->owner = this;
  state->records.reserve(ordered.size());
  for (const int modeling_object_id : ordered) {
    ++fence.work_.mutation_counter_lookup_count;
    const auto current = mutation_count_by_object_.find(modeling_object_id);
    state->records.push_back({
        .modeling_object_id = modeling_object_id,
        .mutation_count = current == mutation_count_by_object_.end()
                              ? 0u
                              : current->second,
    });
  }
  fence.work_.participant_record_count = state->records.size();
  fence.state_ = std::move(state);
  return fence;
}

ModelingObjectBindingProjectionCheckpoint
ModelingObjectBindingRegistry::capture_projection_checkpoint(
    std::span<const int> modeling_object_ids) const {
  ModelingObjectBindingProjectionCheckpoint checkpoint;
  checkpoint.valid_ = true;
  checkpoint.owner_ = this;
  checkpoint.revision_ = revision_;
  checkpoint.work_.participant_reference_count = modeling_object_ids.size();
  std::vector<int> ordered_ids(modeling_object_ids.begin(),
                               modeling_object_ids.end());
  std::ranges::sort(ordered_ids);
  ordered_ids.erase(std::unique(ordered_ids.begin(), ordered_ids.end()),
                    ordered_ids.end());
  ordered_ids.erase(std::remove(ordered_ids.begin(), ordered_ids.end(),
                                kInvalidModelingObjectId),
                    ordered_ids.end());
  checkpoint.records_.reserve(ordered_ids.size());
  for (const int modeling_object_id : ordered_ids) {
    ModelingObjectBindingProjectionCheckpoint::Record record{
        .modeling_object_id = modeling_object_id,
    };
    checkpoint.work_.native_state_lookup_count += 4u;
    if (const auto entity = entities_by_object_.find(modeling_object_id);
        entity != entities_by_object_.end()) {
      record.entity = entity->second;
      ++checkpoint.work_.entity_edge_record_count;
    }
    if (const auto sdk = sdk_objects_by_object_.find(modeling_object_id);
        sdk != sdk_objects_by_object_.end()) {
      record.sdk_object = sdk->second;
      ++checkpoint.work_.sdk_edge_record_count;
    }
    if (const auto identity =
            scene_identity_by_object_.find(modeling_object_id);
        identity != scene_identity_by_object_.end()) {
      record.scene_identity = identity->second;
      ++checkpoint.work_.tombstone_record_count;
    }
    if (const auto mutation =
            mutation_count_by_object_.find(modeling_object_id);
        mutation != mutation_count_by_object_.end()) {
      record.mutation_count = mutation->second;
    }
    checkpoint.records_.push_back(std::move(record));
  }
  checkpoint.work_.record_count = checkpoint.records_.size();
  return checkpoint;
}

bool ModelingObjectBindingRegistry::can_restore_projection_checkpoint(
    const ModelingObjectBindingProjectionCheckpoint &checkpoint) const {
  return can_restore_projection_checkpoint_impl(checkpoint, nullptr, false);
}

bool ModelingObjectBindingRegistry::
    can_restore_projection_checkpoint_preserving_concurrent_changes(
        const ModelingObjectBindingProjectionCheckpoint &checkpoint,
        const ModelingObjectBindingMutationFence &acknowledged_fence) const {
  return can_restore_projection_checkpoint_impl(checkpoint,
                                                &acknowledged_fence, true);
}

bool ModelingObjectBindingRegistry::can_restore_projection_checkpoint_impl(
    const ModelingObjectBindingProjectionCheckpoint &checkpoint,
    const ModelingObjectBindingMutationFence *acknowledged_fence,
    bool preserve_concurrent_changes) const {
  if (!checkpoint.valid() || checkpoint.owner_ != this) {
    return false;
  }

  std::unordered_set<int> restored_ids;
  restored_ids.reserve(checkpoint.records_.size());
  for (const auto &record : checkpoint.records_) {
    restored_ids.insert(record.modeling_object_id);
  }

  if (preserve_concurrent_changes) {
    if (acknowledged_fence == nullptr || !acknowledged_fence->valid() ||
        acknowledged_fence->state_->owner != this ||
        !acknowledged_fence->matches_current(*this) ||
        revision_ < checkpoint.revision_ ||
        acknowledged_fence->state_->records.size() !=
            checkpoint.records_.size()) {
      return false;
    }
    for (std::size_t index = 0u; index < checkpoint.records_.size(); ++index) {
      if (checkpoint.records_[index].modeling_object_id !=
          acknowledged_fence->state_->records[index].modeling_object_id) {
        return false;
      }
      const auto current = mutation_count_by_object_.find(
          checkpoint.records_[index].modeling_object_id);
      if ((current == mutation_count_by_object_.end() ? 0u : current->second) <
          checkpoint.records_[index].mutation_count) {
        return false;
      }
    }
  } else {
    // A bounded revision rewind is truthful only while every mutation since
    // capture belongs to an enrolled native identity. Per-object mutation
    // counters prove that closure in O(participant count).
    if (revision_ < checkpoint.revision_) {
      return false;
    }
    const std::uint64_t revision_delta = revision_ - checkpoint.revision_;
    std::uint64_t enrolled_mutation_delta = 0u;
    for (const auto &record : checkpoint.records_) {
      const auto current =
          mutation_count_by_object_.find(record.modeling_object_id);
      const std::uint64_t current_count =
          current == mutation_count_by_object_.end() ? 0u : current->second;
      if (current_count < record.mutation_count) {
        return false;
      }
      const std::uint64_t object_delta =
          current_count - record.mutation_count;
      if (object_delta > revision_delta ||
          enrolled_mutation_delta > revision_delta - object_delta) {
        return false;
      }
      enrolled_mutation_delta += object_delta;
    }
    if (enrolled_mutation_delta != revision_delta) {
      return false;
    }
  }

  // Refuse to erase an edge owned by a caller that was not enrolled in the
  // bounded checkpoint. Such a conflict means the publication scope was
  // incomplete and cannot be rolled back truthfully.
  for (const auto &record : checkpoint.records_) {
    if (record.entity.has_value()) {
      if (const auto owner = objects_by_entity_.find(*record.entity);
          owner != objects_by_entity_.end() &&
          owner->second != record.modeling_object_id &&
          !restored_ids.contains(owner->second)) {
        return false;
      }
    }
    if (record.sdk_object.has_value()) {
      if (const auto owner =
              objects_by_sdk_object_.find(sdk_object_key(*record.sdk_object));
          owner != objects_by_sdk_object_.end() &&
          owner->second != record.modeling_object_id &&
          !restored_ids.contains(owner->second)) {
        return false;
      }
    }
  }

  return true;
}

bool ModelingObjectBindingRegistry::restore_projection_checkpoint(
    const ModelingObjectBindingProjectionCheckpoint &checkpoint) {
  return restore_projection_checkpoint_impl(checkpoint, nullptr, false);
}

bool ModelingObjectBindingRegistry::
    restore_projection_checkpoint_preserving_concurrent_changes(
        const ModelingObjectBindingProjectionCheckpoint &checkpoint,
        const ModelingObjectBindingMutationFence &acknowledged_fence) {
  return restore_projection_checkpoint_impl(checkpoint, &acknowledged_fence,
                                            true);
}

bool ModelingObjectBindingRegistry::restore_projection_checkpoint_impl(
    const ModelingObjectBindingProjectionCheckpoint &checkpoint,
    const ModelingObjectBindingMutationFence *acknowledged_fence,
    bool preserve_concurrent_changes) {
  if (!can_restore_projection_checkpoint_impl(
          checkpoint, acknowledged_fence, preserve_concurrent_changes)) {
    return false;
  }

  std::vector<int> changed_ids;
  if (preserve_concurrent_changes) {
    changed_ids.reserve(checkpoint.records_.size());
    for (const auto &record : checkpoint.records_) {
      const auto current =
          mutation_count_by_object_.find(record.modeling_object_id);
      const std::uint64_t current_count =
          current == mutation_count_by_object_.end() ? 0u : current->second;
      if (current_count > record.mutation_count) {
        changed_ids.push_back(record.modeling_object_id);
      }
    }
  }

  std::unordered_set<int> restored_ids;
  restored_ids.reserve(checkpoint.records_.size());
  for (const auto &record : checkpoint.records_) {
    restored_ids.insert(record.modeling_object_id);
  }

  for (const int modeling_object_id : restored_ids) {
    if (const auto entity = entities_by_object_.find(modeling_object_id);
        entity != entities_by_object_.end()) {
      objects_by_entity_.erase(entity->second);
      entities_by_object_.erase(entity);
    }
    if (const auto sdk = sdk_objects_by_object_.find(modeling_object_id);
        sdk != sdk_objects_by_object_.end()) {
      objects_by_sdk_object_.erase(sdk_object_key(sdk->second));
      sdk_objects_by_object_.erase(sdk);
    }
    scene_identity_by_object_.erase(modeling_object_id);
  }

  for (const auto &record : checkpoint.records_) {
    if (record.entity.has_value()) {
      entities_by_object_.emplace(record.modeling_object_id, *record.entity);
      objects_by_entity_.emplace(*record.entity, record.modeling_object_id);
    }
    if (record.sdk_object.has_value()) {
      sdk_objects_by_object_.emplace(record.modeling_object_id,
                                     *record.sdk_object);
      objects_by_sdk_object_.emplace(sdk_object_key(*record.sdk_object),
                                     record.modeling_object_id);
    }
    if (record.scene_identity.has_value()) {
      scene_identity_by_object_.emplace(record.modeling_object_id,
                                        *record.scene_identity);
    }
    if (!preserve_concurrent_changes) {
      if (record.mutation_count == 0u) {
        mutation_count_by_object_.erase(record.modeling_object_id);
      } else {
        mutation_count_by_object_[record.modeling_object_id] =
            record.mutation_count;
      }
    }
  }
  if (preserve_concurrent_changes) {
    for (const int modeling_object_id : changed_ids) {
      mark_object_mutated(modeling_object_id);
    }
  } else {
    revision_ = checkpoint.revision_;
  }
  return true;
}

bool ModelingObjectBindingRegistry::bind(int modeling_object_id,
                                         EntityId entity) {
  if (modeling_object_id == kInvalidModelingObjectId ||
      entity == kInvalidEntityId) {
    return false;
  }
  const auto current_entity = entities_by_object_.find(modeling_object_id);
  const auto current_object = objects_by_entity_.find(entity);
  if (current_entity != entities_by_object_.end()) {
    const auto reverse = objects_by_entity_.find(current_entity->second);
    if (reverse == objects_by_entity_.end() ||
        reverse->second != modeling_object_id) {
      return false;
    }
  }
  if (current_object != objects_by_entity_.end()) {
    const auto forward = entities_by_object_.find(current_object->second);
    if (forward == entities_by_object_.end() || forward->second != entity ||
        current_object->second != modeling_object_id) {
      return false;
    }
  }
  if (current_entity != entities_by_object_.end() &&
      current_entity->second == entity &&
      current_object != objects_by_entity_.end() &&
      current_object->second == modeling_object_id) {
    return true;
  }
  if (current_entity != entities_by_object_.end()) {
    objects_by_entity_.erase(current_entity->second);
  }
  entities_by_object_[modeling_object_id] = entity;
  objects_by_entity_[entity] = modeling_object_id;
  mark_object_mutated(modeling_object_id);
  return true;
}

bool ModelingObjectBindingRegistry::bind_sdk_object(
    int modeling_object_id, modeling::ObjectId object) {
  if (modeling_object_id == kInvalidModelingObjectId || !object.valid()) {
    return false;
  }
  const std::uint64_t key = sdk_object_key(object);
  const auto current_sdk = sdk_objects_by_object_.find(modeling_object_id);
  const auto current_native = objects_by_sdk_object_.find(key);
  if (current_sdk != sdk_objects_by_object_.end()) {
    const auto reverse =
        objects_by_sdk_object_.find(sdk_object_key(current_sdk->second));
    if (reverse == objects_by_sdk_object_.end() ||
        reverse->second != modeling_object_id) {
      return false;
    }
  }
  if (current_native != objects_by_sdk_object_.end()) {
    const auto forward = sdk_objects_by_object_.find(current_native->second);
    if (forward == sdk_objects_by_object_.end() ||
        forward->second != object ||
        current_native->second != modeling_object_id) {
      return false;
    }
  }
  if (current_sdk != sdk_objects_by_object_.end() &&
      current_sdk->second == object &&
      current_native != objects_by_sdk_object_.end() &&
      current_native->second == modeling_object_id) {
    return true;
  }
  if (current_sdk != sdk_objects_by_object_.end()) {
    objects_by_sdk_object_.erase(sdk_object_key(current_sdk->second));
  }
  sdk_objects_by_object_[modeling_object_id] = object;
  objects_by_sdk_object_[key] = modeling_object_id;
  mark_object_mutated(modeling_object_id);
  return true;
}

void ModelingObjectBindingRegistry::remember_scene_entity(
    int modeling_object_id, const SceneDocument &scene, EntityId entity) {
  if (modeling_object_id == kInvalidModelingObjectId ||
      entity == kInvalidEntityId || !scene.exists(entity)) {
    return;
  }
  scene_identity_by_object_[modeling_object_id] =
      capture_scene_identity(scene, entity);
  mark_object_mutated(modeling_object_id);
}

void ModelingObjectBindingRegistry::restore_scene_identities(
    std::unordered_map<int, ModelingObjectSceneIdentity> identities) {
  std::unordered_set<int> changed_ids;
  changed_ids.reserve(scene_identity_by_object_.size() + identities.size());
  for (const auto &[modeling_object_id, identity] :
       scene_identity_by_object_) {
    (void)identity;
    changed_ids.insert(modeling_object_id);
  }
  for (const auto &[modeling_object_id, identity] : identities) {
    (void)identity;
    changed_ids.insert(modeling_object_id);
  }
  scene_identity_by_object_ = std::move(identities);
  for (const int modeling_object_id : changed_ids) {
    mark_object_mutated(modeling_object_id);
  }
}

bool ModelingObjectBindingRegistry::restore_sdk_object_bindings(
    std::map<int, modeling::ObjectId> bindings) {
  std::unordered_map<std::uint64_t, int> reverse;
  reverse.reserve(bindings.size());
  for (const auto &[modeling_object_id, object] : bindings) {
    if (modeling_object_id == kInvalidModelingObjectId || !object.valid() ||
        !reverse.emplace(sdk_object_key(object), modeling_object_id).second) {
      return false;
    }
  }
  if (sdk_objects_by_object_ == bindings &&
      objects_by_sdk_object_ == reverse) {
    return true;
  }
  std::unordered_set<int> changed_ids;
  changed_ids.reserve(sdk_objects_by_object_.size() + bindings.size());
  for (const auto &[modeling_object_id, object] : sdk_objects_by_object_) {
    (void)object;
    changed_ids.insert(modeling_object_id);
  }
  for (const auto &[modeling_object_id, object] : bindings) {
    (void)object;
    changed_ids.insert(modeling_object_id);
  }
  sdk_objects_by_object_ = std::move(bindings);
  objects_by_sdk_object_ = std::move(reverse);
  for (const int modeling_object_id : changed_ids) {
    mark_object_mutated(modeling_object_id);
  }
  return true;
}

void ModelingObjectBindingRegistry::unbind_object(int modeling_object_id) {
  const auto it = entities_by_object_.find(modeling_object_id);
  if (it == entities_by_object_.end()) {
    return;
  }
  objects_by_entity_.erase(it->second);
  entities_by_object_.erase(it);
  mark_object_mutated(modeling_object_id);
}

void ModelingObjectBindingRegistry::unbind_sdk_object(
    int modeling_object_id) {
  const auto it = sdk_objects_by_object_.find(modeling_object_id);
  if (it == sdk_objects_by_object_.end()) {
    return;
  }
  objects_by_sdk_object_.erase(sdk_object_key(it->second));
  sdk_objects_by_object_.erase(it);
  mark_object_mutated(modeling_object_id);
}

void ModelingObjectBindingRegistry::clear_sdk_object_bindings() {
  if (sdk_objects_by_object_.empty() && objects_by_sdk_object_.empty()) {
    return;
  }
  std::vector<int> changed_ids;
  changed_ids.reserve(sdk_objects_by_object_.size());
  for (const auto &[modeling_object_id, object] : sdk_objects_by_object_) {
    (void)object;
    changed_ids.push_back(modeling_object_id);
  }
  sdk_objects_by_object_.clear();
  objects_by_sdk_object_.clear();
  for (const int modeling_object_id : changed_ids) {
    mark_object_mutated(modeling_object_id);
  }
}

void ModelingObjectBindingRegistry::clear() {
  std::unordered_set<int> changed_ids;
  changed_ids.reserve(entities_by_object_.size() + sdk_objects_by_object_.size() +
                      scene_identity_by_object_.size());
  for (const auto &[modeling_object_id, entity] : entities_by_object_) {
    (void)entity;
    changed_ids.insert(modeling_object_id);
  }
  for (const auto &[modeling_object_id, object] : sdk_objects_by_object_) {
    (void)object;
    changed_ids.insert(modeling_object_id);
  }
  for (const auto &[modeling_object_id, identity] :
       scene_identity_by_object_) {
    (void)identity;
    changed_ids.insert(modeling_object_id);
  }
  entities_by_object_.clear();
  objects_by_entity_.clear();
  sdk_objects_by_object_.clear();
  objects_by_sdk_object_.clear();
  scene_identity_by_object_.clear();
  for (const int modeling_object_id : changed_ids) {
    mark_object_mutated(modeling_object_id);
  }
}

void ModelingObjectBindingRegistry::mark_object_mutated(
    int modeling_object_id) {
  ++revision_;
  ++mutation_count_by_object_[modeling_object_id];
}

} // namespace quader
