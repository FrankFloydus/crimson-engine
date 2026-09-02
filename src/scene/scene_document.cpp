////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/scene_document.hpp"

#include "scene/document/scene_entity_state.hpp"
#include "scene/scene_transform.hpp"
#include "scene/scene_texture_mapping.hpp"

#include <stduuid/uuid.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <string_view>
#include <utility>

namespace quader {

struct SceneModelingProjectionCheckpoint::State final {
  struct ParticipantRecord final {
    EntityId entity = kInvalidEntityId;
    bool existed = false;
    EntityKind kind = EntityKind::Generic;
    std::optional<MeshComponent> mesh;
    std::optional<CurveComponent> curve;
    std::optional<SelectionComponent> selection;
    std::optional<SceneEntityState> structure;
    std::optional<HierarchyComponent> hierarchy;
    std::uint64_t mesh_binding_mutation_count = 0u;
    std::uint64_t curve_binding_mutation_count = 0u;
    std::uint64_t selection_mutation_count = 0u;
    std::uint64_t structure_mutation_count = 0u;
  };

  const SceneDocument *owner = nullptr;
  SceneModelingProjectionDomain domains =
      SceneModelingProjectionDomain::None;
  std::vector<ParticipantRecord> participants;
  EntityId next_id = 1u;
  EditorObjectId next_editor_object_id = 1u;
  EntityId active_selection = kInvalidEntityId;
  std::uint64_t allocation_serial = 0u;
  std::uint64_t mutation_touch_serial = 0u;
  std::uint64_t unscoped_mutation_serial = 0u;
  std::uint64_t revision = 0u;
  std::uint64_t content_revision = 0u;
  std::uint64_t selection_revision = 0u;
  std::uint64_t hierarchy_presentation_revision = 0u;
  std::uint64_t visibility_revision = 0u;
  std::uint64_t transform_revision = 0u;
  std::uint64_t texture_mapping_revision = 0u;
  std::uint64_t mesh_binding_revision = 0u;
  std::uint64_t curve_binding_revision = 0u;
  std::uint64_t material_binding_revision = 0u;
  std::uint64_t render_component_revision = 0u;
  std::uint64_t layer_revision = 0u;
};

struct SceneModelingProjectionMutationFence::State final {
  struct Record final {
    EntityId entity = kInvalidEntityId;
    std::uint64_t allocation_serial = 0u;
    std::uint64_t mesh_bindings = 0u;
    std::uint64_t curve_bindings = 0u;
    std::uint64_t selection = 0u;
    std::uint64_t structure = 0u;
  };

  const SceneDocument *owner = nullptr;
  SceneModelingProjectionDomain domains =
      SceneModelingProjectionDomain::None;
  std::vector<Record> records;
};

struct SceneScalarCompensationCheckpoint::State final {
  struct EntityAllocationReceipt final {
    EntityId entity = kInvalidEntityId;
    EditorObjectId editor_object_id = kInvalidEditorObjectId;
    std::uint64_t serial = 0u;
    std::uint64_t previous_serial = 0u;
  };

  struct LayerAllocationReceipt final {
    SceneLayerId layer = kInvalidSceneLayerId;
    std::uint64_t serial = 0u;
    std::uint64_t previous_serial = 0u;
  };

  struct MutationBaseline final {
    bool record_existed = false;
    SceneModelingProjectionDomain domains =
        SceneModelingProjectionDomain::None;
    std::uint64_t mesh_bindings = 0u;
    std::uint64_t curve_bindings = 0u;
    std::uint64_t selection = 0u;
    std::uint64_t structure = 0u;
  };

  struct Boundary final {
    EntityId next_id = 1u;
    EditorObjectId next_editor_object_id = 1u;
    SceneLayerId next_layer_id = 1u;
    std::uint64_t allocation_serial = 0u;
    std::uint64_t layer_allocation_serial = 0u;
    std::uint64_t mutation_touch_serial = 0u;
    std::uint64_t unscoped_mutation_serial = 0u;
    std::uint64_t revision = 0u;
    std::uint64_t content_revision = 0u;
    std::uint64_t selection_revision = 0u;
    std::uint64_t hierarchy_presentation_revision = 0u;
    std::uint64_t visibility_revision = 0u;
    std::uint64_t transform_revision = 0u;
    std::uint64_t texture_mapping_revision = 0u;
    std::uint64_t mesh_binding_revision = 0u;
    std::uint64_t curve_binding_revision = 0u;
    std::uint64_t material_binding_revision = 0u;
    std::uint64_t render_component_revision = 0u;
    std::uint64_t layer_revision = 0u;
  };

  SceneDocument *owner = nullptr;
  std::uint64_t token = 0u;
  std::size_t depth = 0u;
  EntityId next_id = 1u;
  EditorObjectId next_editor_object_id = 1u;
  SceneLayerId next_layer_id = 1u;
  std::uint64_t allocation_serial = 0u;
  std::uint64_t layer_allocation_serial = 0u;
  std::uint64_t mutation_touch_serial = 0u;
  std::uint64_t unscoped_mutation_serial = 0u;
  std::uint64_t revision = 0u;
  std::uint64_t content_revision = 0u;
  std::uint64_t selection_revision = 0u;
  std::uint64_t hierarchy_presentation_revision = 0u;
  std::uint64_t visibility_revision = 0u;
  std::uint64_t transform_revision = 0u;
  std::uint64_t texture_mapping_revision = 0u;
  std::uint64_t mesh_binding_revision = 0u;
  std::uint64_t curve_binding_revision = 0u;
  std::uint64_t material_binding_revision = 0u;
  std::uint64_t render_component_revision = 0u;
  std::uint64_t layer_revision = 0u;
  std::vector<EntityAllocationReceipt> entity_allocations;
  std::vector<LayerAllocationReceipt> layer_allocations;
  std::unordered_map<EntityId, MutationBaseline> mutation_baselines;
  Boundary acknowledged_boundary;
  bool acknowledged = false;
  bool inverse_started = false;
  bool invalid_inverse_mutation = false;
  bool abandoned = false;
};

struct SceneDocument::ModelingProjectionRestorePreparation final {
  std::vector<EntityId> created;
  std::unordered_set<EntityId> participant_ids;
  std::unordered_set<EntityId> originally_existing;
  std::unordered_set<EntityId> removal_set;
};

namespace {

std::uint32_t entity_key(entt::entity entity);
void remove_child(std::vector<EntityId> &children, EntityId child);

[[nodiscard]] constexpr bool has_projection_domain(
    SceneModelingProjectionDomain domains,
    SceneModelingProjectionDomain requested) noexcept {
  return (domains & requested) != SceneModelingProjectionDomain::None;
}

template <typename Component>
void restore_optional_component(entt::registry &registry,
                                entt::entity entity,
                                const std::optional<Component> &component) {
  if (component.has_value()) {
    registry.emplace_or_replace<Component>(entity, *component);
  } else {
    registry.remove<Component>(entity);
  }
}

void increment_binding_reference(
    std::unordered_map<AssetId, std::size_t> &reference_counts,
    AssetId asset) {
  ++reference_counts[asset];
}

void decrement_binding_reference(
    std::unordered_map<AssetId, std::size_t> &reference_counts,
    AssetId asset) {
  const auto found = reference_counts.find(asset);
  if (found == reference_counts.end()) {
    return;
  }
  if (found->second <= 1u) {
    reference_counts.erase(found);
  } else {
    --found->second;
  }
}

void replace_binding_reference(
    std::unordered_map<AssetId, std::size_t> &reference_counts,
    std::optional<AssetId> current, std::optional<AssetId> replacement) {
  if (current == replacement) {
    return;
  }
  if (current.has_value()) {
    decrement_binding_reference(reference_counts, *current);
  }
  if (replacement.has_value()) {
    increment_binding_reference(reference_counts, *replacement);
  }
}

} // namespace

SceneModelingProjectionMutationFence::
    SceneModelingProjectionMutationFence() = default;
SceneModelingProjectionMutationFence::
    ~SceneModelingProjectionMutationFence() = default;
SceneModelingProjectionMutationFence::SceneModelingProjectionMutationFence(
    SceneModelingProjectionMutationFence &&) noexcept = default;
SceneModelingProjectionMutationFence &
SceneModelingProjectionMutationFence::operator=(
    SceneModelingProjectionMutationFence &&) noexcept = default;

SceneScalarCompensationCheckpoint::SceneScalarCompensationCheckpoint() =
    default;

SceneScalarCompensationCheckpoint::~SceneScalarCompensationCheckpoint() {
  reset();
}

SceneScalarCompensationCheckpoint::SceneScalarCompensationCheckpoint(
    SceneScalarCompensationCheckpoint &&other) noexcept
    : state_(std::move(other.state_)) {}

SceneScalarCompensationCheckpoint &
SceneScalarCompensationCheckpoint::operator=(
    SceneScalarCompensationCheckpoint &&other) noexcept {
  if (this == &other) {
    return *this;
  }
  reset();
  state_ = std::move(other.state_);
  return *this;
}

bool SceneScalarCompensationCheckpoint::valid() const noexcept {
  return state_ != nullptr;
}

void SceneScalarCompensationCheckpoint::reset() noexcept {
  if (state_ != nullptr && state_->owner != nullptr) {
    state_->owner->abandon_scalar_compensation_checkpoint(state_);
  }
  state_.reset();
}

bool SceneModelingProjectionMutationFence::valid() const noexcept {
  return state_ != nullptr;
}

std::size_t SceneModelingProjectionMutationFence::size() const noexcept {
  return state_ == nullptr ? 0u : state_->records.size();
}

bool SceneModelingProjectionMutationFence::matches_current(
    const SceneDocument &owner,
    SceneModelingProjectionMutationFenceWork *reported_work) const {
  SceneModelingProjectionMutationFenceWork work;
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
        owner.modeling_projection_mutations_by_entity_.find(record.entity);
    const SceneDocument::ModelingProjectionMutationCounters counters =
        current == owner.modeling_projection_mutations_by_entity_.end()
            ? SceneDocument::ModelingProjectionMutationCounters{}
            : current->second;
    const auto current_allocation =
        owner.last_entity_allocation_serial_.find(record.entity);
    const std::uint64_t allocation_serial =
        current_allocation == owner.last_entity_allocation_serial_.end()
            ? 0u
            : current_allocation->second;
    if ((has_projection_domain(
             state_->domains,
             SceneModelingProjectionDomain::MeshBindings) &&
         counters.mesh_bindings != record.mesh_bindings) ||
        (has_projection_domain(
             state_->domains,
             SceneModelingProjectionDomain::CurveBindings) &&
         counters.curve_bindings != record.curve_bindings) ||
        (has_projection_domain(state_->domains,
                               SceneModelingProjectionDomain::Selection) &&
         counters.selection != record.selection) ||
        (has_projection_domain(state_->domains,
                               SceneModelingProjectionDomain::Structure) &&
         counters.structure != record.structure) ||
        allocation_serial != record.allocation_serial) {
      return report(false);
    }
  }
  return report(true);
}

bool SceneModelingProjectionMutationFence::acknowledge_current(
    const SceneDocument &owner,
    std::span<const EntityId> newly_created_participants,
    SceneModelingProjectionMutationFenceWork *reported_work) {
  SceneModelingProjectionMutationFenceWork work{
      .input_reference_count = newly_created_participants.size(),
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

  std::vector<EntityId> participants;
  participants.reserve(state_->records.size() +
                       newly_created_participants.size());
  for (const State::Record &record : state_->records) {
    participants.push_back(record.entity);
  }
  participants.insert(participants.end(), newly_created_participants.begin(),
                      newly_created_participants.end());
  std::ranges::sort(participants);
  participants.erase(
      std::remove(participants.begin(), participants.end(), kInvalidEntityId),
      participants.end());
  participants.erase(std::unique(participants.begin(), participants.end()),
                     participants.end());

  std::vector<State::Record> records;
  records.reserve(participants.size());
  for (const EntityId entity : participants) {
    ++work.mutation_counter_lookup_count;
    const auto current =
        owner.modeling_projection_mutations_by_entity_.find(entity);
    const SceneDocument::ModelingProjectionMutationCounters counters =
        current == owner.modeling_projection_mutations_by_entity_.end()
            ? SceneDocument::ModelingProjectionMutationCounters{}
            : current->second;
    const auto allocation = owner.last_entity_allocation_serial_.find(entity);
    records.push_back({
        .entity = entity,
        .allocation_serial =
            allocation == owner.last_entity_allocation_serial_.end()
                ? 0u
                : allocation->second,
        .mesh_bindings = counters.mesh_bindings,
        .curve_bindings = counters.curve_bindings,
        .selection = counters.selection,
        .structure = counters.structure,
    });
  }
  work.participant_record_count = records.size();
  state_->records = std::move(records);
  work_ = work;
  return report(true);
}

const SceneHierarchyProjectionRecord *
SceneHierarchyProjectionSnapshot::find(EntityId entity) const noexcept {
  const auto found = index_by_entity_.find(entity);
  if (found == index_by_entity_.end() || found->second >= records_.size()) {
    return nullptr;
  }
  return &records_[found->second];
}

SceneModelingProjectionMutationFence
SceneDocument::capture_modeling_projection_mutation_fence(
    std::span<const EntityId> participants,
    SceneModelingProjectionDomain domains) const {
  SceneModelingProjectionMutationFence fence;
  fence.work_.input_reference_count = participants.size();
  constexpr std::uint8_t kKnownDomains =
      static_cast<std::uint8_t>(
          SceneModelingProjectionDomain::MeshBindings) |
      static_cast<std::uint8_t>(
          SceneModelingProjectionDomain::CurveBindings) |
      static_cast<std::uint8_t>(SceneModelingProjectionDomain::Selection) |
      static_cast<std::uint8_t>(SceneModelingProjectionDomain::Structure);
  const std::uint8_t requested_domains =
      static_cast<std::uint8_t>(domains);
  if (requested_domains == 0u ||
      (requested_domains & static_cast<std::uint8_t>(~kKnownDomains)) != 0u) {
    return fence;
  }

  std::vector<EntityId> ordered(participants.begin(), participants.end());
  std::ranges::sort(ordered);
  ordered.erase(
      std::remove(ordered.begin(), ordered.end(), kInvalidEntityId),
      ordered.end());
  ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());

  auto state =
      std::make_unique<SceneModelingProjectionMutationFence::State>();
  state->owner = this;
  state->domains = domains;
  state->records.reserve(ordered.size());
  for (const EntityId entity : ordered) {
    ++fence.work_.mutation_counter_lookup_count;
    const auto current =
        modeling_projection_mutations_by_entity_.find(entity);
    const ModelingProjectionMutationCounters counters =
        current == modeling_projection_mutations_by_entity_.end()
            ? ModelingProjectionMutationCounters{}
            : current->second;
    const auto allocation = last_entity_allocation_serial_.find(entity);
    state->records.push_back({
        .entity = entity,
        .allocation_serial =
            allocation == last_entity_allocation_serial_.end()
                ? 0u
                : allocation->second,
        .mesh_bindings = counters.mesh_bindings,
        .curve_bindings = counters.curve_bindings,
        .selection = counters.selection,
        .structure = counters.structure,
    });
  }
  fence.work_.participant_record_count = state->records.size();
  fence.state_ = std::move(state);
  return fence;
}

SceneScalarCompensationCheckpoint
SceneDocument::capture_scalar_compensation_checkpoint() {
  SceneScalarCompensationCheckpoint checkpoint;
  if (next_scalar_compensation_token_ ==
      std::numeric_limits<std::uint64_t>::max()) {
    return checkpoint;
  }

  auto state = std::make_shared<SceneScalarCompensationCheckpoint::State>();
  state->owner = this;
  state->token = next_scalar_compensation_token_ + 1u;
  state->depth = scalar_compensation_stack_.size() + 1u;
  state->next_id = next_id_;
  state->next_editor_object_id = next_editor_object_id_;
  state->next_layer_id = next_layer_id_;
  state->allocation_serial = entity_allocation_serial_;
  state->layer_allocation_serial = layer_allocation_serial_;
  state->mutation_touch_serial =
      modeling_projection_mutation_touch_serial_;
  state->unscoped_mutation_serial =
      unscoped_modeling_projection_mutation_serial_;
  state->revision = revision_;
  state->content_revision = content_revision_;
  state->selection_revision = selection_revision_;
  state->hierarchy_presentation_revision = hierarchy_presentation_revision_;
  state->visibility_revision = visibility_revision_;
  state->transform_revision = transform_revision_;
  state->texture_mapping_revision = texture_mapping_revision_;
  state->mesh_binding_revision = mesh_binding_revision_;
  state->curve_binding_revision = curve_binding_revision_;
  state->material_binding_revision = material_binding_revision_;
  state->render_component_revision = render_component_revision_;
  state->layer_revision = layer_revision_;

  scalar_compensation_stack_.push_back(state);
  next_scalar_compensation_token_ = state->token;
  checkpoint.state_ = std::move(state);
  return checkpoint;
}

bool SceneDocument::acknowledge_scalar_compensation_checkpoint(
    SceneScalarCompensationCheckpoint &checkpoint) noexcept {
  if (!checkpoint.valid() || checkpoint.state_->owner != this ||
      checkpoint.state_->depth != scalar_compensation_stack_.size() ||
      scalar_compensation_stack_.empty() ||
      scalar_compensation_stack_.back() != checkpoint.state_ ||
      checkpoint.state_->acknowledged || checkpoint.state_->inverse_started) {
    return false;
  }

  SceneScalarCompensationCheckpoint::State::Boundary &boundary =
      checkpoint.state_->acknowledged_boundary;
  boundary.next_id = next_id_;
  boundary.next_editor_object_id = next_editor_object_id_;
  boundary.next_layer_id = next_layer_id_;
  boundary.allocation_serial = entity_allocation_serial_;
  boundary.layer_allocation_serial = layer_allocation_serial_;
  boundary.mutation_touch_serial =
      modeling_projection_mutation_touch_serial_;
  boundary.unscoped_mutation_serial =
      unscoped_modeling_projection_mutation_serial_;
  boundary.revision = revision_;
  boundary.content_revision = content_revision_;
  boundary.selection_revision = selection_revision_;
  boundary.hierarchy_presentation_revision =
      hierarchy_presentation_revision_;
  boundary.visibility_revision = visibility_revision_;
  boundary.transform_revision = transform_revision_;
  boundary.texture_mapping_revision = texture_mapping_revision_;
  boundary.mesh_binding_revision = mesh_binding_revision_;
  boundary.curve_binding_revision = curve_binding_revision_;
  boundary.material_binding_revision = material_binding_revision_;
  boundary.render_component_revision = render_component_revision_;
  boundary.layer_revision = layer_revision_;
  checkpoint.state_->acknowledged = true;
  return true;
}

bool SceneDocument::begin_scalar_compensation_restore(
    SceneScalarCompensationCheckpoint &checkpoint) noexcept {
  if (!checkpoint.valid() || checkpoint.state_->owner != this ||
      checkpoint.state_->depth != scalar_compensation_stack_.size() ||
      scalar_compensation_stack_.empty() ||
      scalar_compensation_stack_.back() != checkpoint.state_ ||
      !checkpoint.state_->acknowledged || checkpoint.state_->inverse_started) {
    return false;
  }

  const SceneScalarCompensationCheckpoint::State::Boundary &boundary =
      checkpoint.state_->acknowledged_boundary;
  if (next_id_ != boundary.next_id ||
      next_editor_object_id_ != boundary.next_editor_object_id ||
      next_layer_id_ != boundary.next_layer_id ||
      entity_allocation_serial_ != boundary.allocation_serial ||
      layer_allocation_serial_ != boundary.layer_allocation_serial ||
      modeling_projection_mutation_touch_serial_ !=
          boundary.mutation_touch_serial ||
      unscoped_modeling_projection_mutation_serial_ !=
          boundary.unscoped_mutation_serial ||
      revision_ != boundary.revision ||
      content_revision_ != boundary.content_revision ||
      selection_revision_ != boundary.selection_revision ||
      hierarchy_presentation_revision_ !=
          boundary.hierarchy_presentation_revision ||
      visibility_revision_ != boundary.visibility_revision ||
      transform_revision_ != boundary.transform_revision ||
      texture_mapping_revision_ != boundary.texture_mapping_revision ||
      mesh_binding_revision_ != boundary.mesh_binding_revision ||
      curve_binding_revision_ != boundary.curve_binding_revision ||
      material_binding_revision_ != boundary.material_binding_revision ||
      render_component_revision_ != boundary.render_component_revision ||
      layer_revision_ != boundary.layer_revision) {
    return false;
  }

  checkpoint.state_->inverse_started = true;
  return true;
}

bool SceneDocument::restore_scalar_compensation_checkpoint(
    SceneScalarCompensationCheckpoint &checkpoint) {
  if (!checkpoint.valid() || checkpoint.state_->owner != this ||
      checkpoint.state_->depth != scalar_compensation_stack_.size() ||
      scalar_compensation_stack_.empty() ||
      scalar_compensation_stack_.back() != checkpoint.state_ ||
      !checkpoint.state_->inverse_started ||
      checkpoint.state_->invalid_inverse_mutation ||
      entity_allocation_serial_ < checkpoint.state_->allocation_serial ||
      layer_allocation_serial_ <
          checkpoint.state_->layer_allocation_serial ||
      unscoped_modeling_projection_mutation_serial_ <
          checkpoint.state_->unscoped_mutation_serial ||
      modeling_projection_mutation_touch_serial_ <
          checkpoint.state_->mutation_touch_serial) {
    return false;
  }

  const SceneScalarCompensationCheckpoint::State &state = *checkpoint.state_;
  if (state.acknowledged_boundary.unscoped_mutation_serial <
          state.unscoped_mutation_serial ||
      unscoped_modeling_projection_mutation_serial_ <
          state.acknowledged_boundary.unscoped_mutation_serial) {
    return false;
  }
  const std::uint64_t forward_unscoped_mutation_delta =
      state.acknowledged_boundary.unscoped_mutation_serial -
      state.unscoped_mutation_serial;
  const std::uint64_t inverse_unscoped_mutation_delta =
      unscoped_modeling_projection_mutation_serial_ -
      state.acknowledged_boundary.unscoped_mutation_serial;
  if (inverse_unscoped_mutation_delta !=
      forward_unscoped_mutation_delta) {
    return false;
  }
  const std::uint64_t mutation_touch_delta =
      modeling_projection_mutation_touch_serial_ - state.mutation_touch_serial;
  std::uint64_t enrolled_mutation_delta = 0u;
  const std::uint64_t unscoped_mutation_delta =
      unscoped_modeling_projection_mutation_serial_ -
      state.unscoped_mutation_serial;
  if (unscoped_mutation_delta > mutation_touch_delta) {
    return false;
  }
  enrolled_mutation_delta = unscoped_mutation_delta;
  const auto add_mutation_delta = [&](std::uint64_t current,
                                      std::uint64_t baseline) {
    if (current < baseline) {
      return false;
    }
    const std::uint64_t delta = current - baseline;
    if (delta > mutation_touch_delta ||
        enrolled_mutation_delta > mutation_touch_delta - delta) {
      return false;
    }
    enrolled_mutation_delta += delta;
    return true;
  };
  for (const auto &[entity, baseline] : state.mutation_baselines) {
    const auto current = modeling_projection_mutations_by_entity_.find(entity);
    const ModelingProjectionMutationCounters counters =
        current == modeling_projection_mutations_by_entity_.end()
            ? ModelingProjectionMutationCounters{}
            : current->second;
    if (!add_mutation_delta(counters.mesh_bindings,
                            baseline.mesh_bindings) ||
        !add_mutation_delta(counters.curve_bindings,
                            baseline.curve_bindings) ||
        !add_mutation_delta(counters.selection, baseline.selection) ||
        !add_mutation_delta(counters.structure, baseline.structure)) {
      return false;
    }
  }
  if (enrolled_mutation_delta != mutation_touch_delta) {
    return false;
  }

  for (const auto &allocation : state.entity_allocations) {
    const auto current_serial =
        last_entity_allocation_serial_.find(allocation.entity);
    const bool receipt_is_current =
        current_serial != last_entity_allocation_serial_.end() &&
        current_serial->second == allocation.serial;
    if (receipt_is_current && allocation.entity >= state.next_id &&
        exists(allocation.entity)) {
      return false;
    }
    if (allocation.editor_object_id >= state.next_editor_object_id &&
        entity_by_editor_object_id_.contains(allocation.editor_object_id)) {
      return false;
    }
  }
  for (const auto &allocation : state.layer_allocations) {
    const auto current_serial =
        last_layer_allocation_serial_.find(allocation.layer);
    if (current_serial != last_layer_allocation_serial_.end() &&
        current_serial->second == allocation.serial &&
        allocation.layer >= state.next_layer_id &&
        layers_.contains(allocation.layer)) {
      return false;
    }
  }

  for (auto allocation = state.entity_allocations.rbegin();
       allocation != state.entity_allocations.rend(); ++allocation) {
    const auto current =
        last_entity_allocation_serial_.find(allocation->entity);
    if (current == last_entity_allocation_serial_.end() ||
        current->second != allocation->serial) {
      continue;
    }
    if (allocation->previous_serial == 0u) {
      last_entity_allocation_serial_.erase(current);
    } else {
      current->second = allocation->previous_serial;
    }
  }
  for (auto allocation = state.layer_allocations.rbegin();
       allocation != state.layer_allocations.rend(); ++allocation) {
    const auto current = last_layer_allocation_serial_.find(allocation->layer);
    if (current == last_layer_allocation_serial_.end() ||
        current->second != allocation->serial) {
      continue;
    }
    if (allocation->previous_serial == 0u) {
      last_layer_allocation_serial_.erase(current);
    } else {
      current->second = allocation->previous_serial;
    }
  }

  next_id_ = state.next_id;
  next_editor_object_id_ = state.next_editor_object_id;
  next_layer_id_ = state.next_layer_id;
  entity_allocation_serial_ = state.allocation_serial;
  layer_allocation_serial_ = state.layer_allocation_serial;
  revision_ = state.revision;
  content_revision_ = state.content_revision;
  selection_revision_ = state.selection_revision;
  hierarchy_presentation_revision_ = state.hierarchy_presentation_revision;
  visibility_revision_ = state.visibility_revision;
  transform_revision_ = state.transform_revision;
  texture_mapping_revision_ = state.texture_mapping_revision;
  mesh_binding_revision_ = state.mesh_binding_revision;
  curve_binding_revision_ = state.curve_binding_revision;
  material_binding_revision_ = state.material_binding_revision;
  render_component_revision_ = state.render_component_revision;
  layer_revision_ = state.layer_revision;
  for (const auto &[entity, baseline] : state.mutation_baselines) {
    if (!baseline.record_existed) {
      modeling_projection_mutations_by_entity_.erase(entity);
      continue;
    }
    modeling_projection_mutations_by_entity_[entity] = {
        .mesh_bindings = baseline.mesh_bindings,
        .curve_bindings = baseline.curve_bindings,
        .selection = baseline.selection,
        .structure = baseline.structure,
    };
  }
  modeling_projection_mutation_touch_serial_ = state.mutation_touch_serial;
  unscoped_modeling_projection_mutation_serial_ =
      state.unscoped_mutation_serial;
  (void)close_scalar_compensation_checkpoint(state);
  checkpoint.state_.reset();
  invalidate_spatial_projection_caches_after_revision_restore();
  return true;
}

bool SceneDocument::discard_scalar_compensation_checkpoint(
    SceneScalarCompensationCheckpoint &checkpoint) noexcept {
  if (!checkpoint.valid() || checkpoint.state_->owner != this ||
      !close_scalar_compensation_checkpoint(*checkpoint.state_)) {
    return false;
  }
  checkpoint.state_.reset();
  return true;
}

SceneModelingProjectionCheckpoint
SceneDocument::capture_modeling_projection_checkpoint(
    std::span<const EntityId> participants,
    SceneModelingProjectionDomain domains) const {
  SceneModelingProjectionCheckpoint checkpoint;
  checkpoint.work_.participant_reference_count = participants.size();

  std::vector<EntityId> ordered_participants(participants.begin(),
                                             participants.end());
  std::ranges::sort(ordered_participants);
  ordered_participants.erase(
      std::remove(ordered_participants.begin(), ordered_participants.end(),
                  kInvalidEntityId),
      ordered_participants.end());
  ordered_participants.erase(
      std::unique(ordered_participants.begin(), ordered_participants.end()),
      ordered_participants.end());
  checkpoint.work_.participant_record_count = ordered_participants.size();

  auto state = std::make_shared<SceneModelingProjectionCheckpoint::State>();
  state->owner = this;
  state->domains = domains;
  state->next_id = next_id_;
  state->next_editor_object_id = next_editor_object_id_;
  state->allocation_serial = entity_allocation_serial_;
  state->mutation_touch_serial =
      modeling_projection_mutation_touch_serial_;
  state->unscoped_mutation_serial =
      unscoped_modeling_projection_mutation_serial_;
  state->revision = revision_;
  state->content_revision = content_revision_;
  state->selection_revision = selection_revision_;
  state->hierarchy_presentation_revision =
      hierarchy_presentation_revision_;
  state->visibility_revision = visibility_revision_;
  state->transform_revision = transform_revision_;
  state->texture_mapping_revision = texture_mapping_revision_;
  state->mesh_binding_revision = mesh_binding_revision_;
  state->curve_binding_revision = curve_binding_revision_;
  state->material_binding_revision = material_binding_revision_;
  state->render_component_revision = render_component_revision_;
  state->layer_revision = layer_revision_;
  if (has_projection_domain(domains,
                            SceneModelingProjectionDomain::Selection)) {
    state->active_selection = active_selection_;
  }

  std::unordered_set<EntityId> participant_set(
      ordered_participants.begin(), ordered_participants.end());
  state->participants.reserve(ordered_participants.size());
  for (const EntityId entity : ordered_participants) {
    ++checkpoint.work_.live_entity_lookup_count;
    SceneModelingProjectionCheckpoint::State::ParticipantRecord record{
        .entity = entity,
        .existed = exists(entity),
    };
    if (const auto mutations =
            modeling_projection_mutations_by_entity_.find(entity);
        mutations != modeling_projection_mutations_by_entity_.end()) {
      record.mesh_binding_mutation_count =
          mutations->second.mesh_bindings;
      record.curve_binding_mutation_count =
          mutations->second.curve_bindings;
      record.selection_mutation_count = mutations->second.selection;
      record.structure_mutation_count = mutations->second.structure;
    }
    if (!record.existed) {
      state->participants.push_back(std::move(record));
      continue;
    }

    ++checkpoint.work_.live_entity_record_count;
    record.kind = kind(entity);
    if (has_projection_domain(
            domains, SceneModelingProjectionDomain::MeshBindings)) {
      if (const MeshComponent *component = mesh(entity)) {
        record.mesh = *component;
      }
      ++checkpoint.work_.mesh_binding_record_count;
    }
    if (has_projection_domain(
            domains, SceneModelingProjectionDomain::CurveBindings)) {
      if (const CurveComponent *component = curve(entity)) {
        record.curve = *component;
      }
      ++checkpoint.work_.curve_binding_record_count;
    }
    if (has_projection_domain(domains,
                              SceneModelingProjectionDomain::Selection)) {
      if (const SelectionComponent *component = selection(entity)) {
        record.selection = *component;
      }
      ++checkpoint.work_.selection_record_count;
    }
    if (has_projection_domain(domains,
                              SceneModelingProjectionDomain::Structure)) {
      const std::optional<SceneEntityState> captured =
          capture_scene_entity_state(*this, entity);
      const HierarchyComponent *captured_hierarchy = hierarchy(entity);
      if (!captured.has_value() || captured_hierarchy == nullptr) {
        return checkpoint;
      }
      // Structural rollback is intentionally bounded. Every descendant edge
      // of a participant must therefore be part of the caller's union.
      if (std::ranges::any_of(
              captured_hierarchy->children,
              [&](EntityId child) { return !participant_set.contains(child); })) {
        return checkpoint;
      }
      // Recreating a destroyed entity is exact only when its optional modeling
      // binding domains and selected state were explicitly requested.
      if ((captured->mesh.has_value() &&
           !has_projection_domain(
               domains, SceneModelingProjectionDomain::MeshBindings)) ||
          (captured->curve.has_value() &&
           !has_projection_domain(
               domains, SceneModelingProjectionDomain::CurveBindings)) ||
          (captured->selection.selected &&
           !has_projection_domain(domains,
                                  SceneModelingProjectionDomain::Selection))) {
        return checkpoint;
      }
      record.structure = *captured;
      record.hierarchy = *captured_hierarchy;
      ++checkpoint.work_.structure_record_count;
    }
    state->participants.push_back(std::move(record));
  }

  checkpoint.state_ = std::move(state);
  return checkpoint;
}

bool SceneDocument::can_restore_modeling_projection_checkpoint(
    const SceneModelingProjectionCheckpoint &checkpoint,
    std::span<const EntityId> created_participants) const {
  return preflight_modeling_projection_restore(
      checkpoint, nullptr, created_participants, false, nullptr, nullptr);
}

bool SceneDocument::
    can_restore_modeling_projection_checkpoint_preserving_concurrent_changes(
        const SceneModelingProjectionCheckpoint &checkpoint,
        const SceneModelingProjectionMutationFence &acknowledged_fence,
        std::span<const EntityId> created_participants) const {
  return preflight_modeling_projection_restore(
      checkpoint, &acknowledged_fence, created_participants, true, nullptr,
      nullptr);
}

bool SceneDocument::preflight_modeling_projection_restore(
    const SceneModelingProjectionCheckpoint &checkpoint,
    const SceneModelingProjectionMutationFence *acknowledged_fence,
    std::span<const EntityId> created_participants,
    bool preserve_concurrent_changes,
    ModelingProjectionRestorePreparation *preparation,
    SceneModelingProjectionRestoreWork *reported_work) const {
  SceneModelingProjectionRestoreWork work;
  const auto report = [&](bool result) {
    if (reported_work != nullptr) {
      *reported_work = work;
    }
    return result;
  };
  if (!checkpoint.valid() || checkpoint.state_->owner != this) {
    return report(false);
  }

  const SceneModelingProjectionCheckpoint::State &state = *checkpoint.state_;
  work.participant_record_count = state.participants.size();

  std::vector<EntityId> created(created_participants.begin(),
                                created_participants.end());
  std::ranges::sort(created);
  created.erase(std::remove(created.begin(), created.end(),
                            kInvalidEntityId),
                created.end());
  created.erase(std::unique(created.begin(), created.end()), created.end());

  if (preserve_concurrent_changes) {
    if (acknowledged_fence == nullptr || !acknowledged_fence->valid() ||
        acknowledged_fence->state_->owner != this ||
        acknowledged_fence->state_->domains != state.domains ||
        !acknowledged_fence->matches_current(*this)) {
      return report(false);
    }
    std::vector<EntityId> expected_participants;
    expected_participants.reserve(state.participants.size() + created.size());
    for (const auto &record : state.participants) {
      expected_participants.push_back(record.entity);
    }
    expected_participants.insert(expected_participants.end(), created.begin(),
                                 created.end());
    std::ranges::sort(expected_participants);
    expected_participants.erase(
        std::unique(expected_participants.begin(), expected_participants.end()),
        expected_participants.end());
    if (expected_participants.size() !=
        acknowledged_fence->state_->records.size()) {
      return report(false);
    }
    for (std::size_t index = 0u; index < expected_participants.size(); ++index) {
      if (expected_participants[index] !=
          acknowledged_fence->state_->records[index].entity) {
        return report(false);
      }
    }
    if (entity_allocation_serial_ < state.allocation_serial) {
      return report(false);
    }
    if (next_id_ < state.next_id ||
        next_editor_object_id_ < state.next_editor_object_id ||
        modeling_projection_mutation_touch_serial_ <
            state.mutation_touch_serial ||
        unscoped_modeling_projection_mutation_serial_ <
            state.unscoped_mutation_serial ||
        revision_ < state.revision ||
        content_revision_ < state.content_revision ||
        selection_revision_ < state.selection_revision ||
        hierarchy_presentation_revision_ <
            state.hierarchy_presentation_revision ||
        visibility_revision_ < state.visibility_revision ||
        transform_revision_ < state.transform_revision ||
        texture_mapping_revision_ < state.texture_mapping_revision ||
        mesh_binding_revision_ < state.mesh_binding_revision ||
        curve_binding_revision_ < state.curve_binding_revision ||
        material_binding_revision_ < state.material_binding_revision ||
        render_component_revision_ < state.render_component_revision ||
        layer_revision_ < state.layer_revision) {
      return report(false);
    }
    for (const auto &record : state.participants) {
      const auto current =
          modeling_projection_mutations_by_entity_.find(record.entity);
      const ModelingProjectionMutationCounters counters =
          current == modeling_projection_mutations_by_entity_.end()
              ? ModelingProjectionMutationCounters{}
              : current->second;
      if ((has_projection_domain(
               state.domains,
               SceneModelingProjectionDomain::MeshBindings) &&
           counters.mesh_bindings < record.mesh_binding_mutation_count) ||
          (has_projection_domain(
               state.domains,
               SceneModelingProjectionDomain::CurveBindings) &&
           counters.curve_bindings < record.curve_binding_mutation_count) ||
          (has_projection_domain(state.domains,
                                 SceneModelingProjectionDomain::Selection) &&
           counters.selection < record.selection_mutation_count) ||
          (has_projection_domain(state.domains,
                                 SceneModelingProjectionDomain::Structure) &&
           counters.structure < record.structure_mutation_count)) {
        return report(false);
      }
    }
    for (const EntityId entity : created) {
      const auto allocation = last_entity_allocation_serial_.find(entity);
      if (allocation == last_entity_allocation_serial_.end() ||
          allocation->second <= state.allocation_serial ||
          allocation->second > entity_allocation_serial_) {
        return report(false);
      }
      if (exists(entity) &&
          ((mesh(entity) != nullptr &&
            !has_projection_domain(
                state.domains,
                SceneModelingProjectionDomain::MeshBindings)) ||
           (curve(entity) != nullptr &&
            !has_projection_domain(
                state.domains,
                SceneModelingProjectionDomain::CurveBindings)) ||
           (is_selected(entity) &&
            !has_projection_domain(state.domains,
                                   SceneModelingProjectionDomain::Selection)))) {
        return report(false);
      }
    }
  } else {
    // This is the allocation-lease preflight. It catches an unrelated entity
    // creation in O(1) and rejects before component, hierarchy, or ID mutation.
    if (entity_allocation_serial_ < state.allocation_serial ||
        entity_allocation_serial_ - state.allocation_serial != created.size()) {
      return report(false);
    }
    for (const EntityId entity : created) {
      const auto allocation = last_entity_allocation_serial_.find(entity);
      if (allocation == last_entity_allocation_serial_.end() ||
          allocation->second <= state.allocation_serial ||
          allocation->second > entity_allocation_serial_) {
        return report(false);
      }
    }

    if (modeling_projection_mutation_touch_serial_ <
            state.mutation_touch_serial ||
        unscoped_modeling_projection_mutation_serial_ !=
            state.unscoped_mutation_serial) {
      return report(false);
    }
    const std::uint64_t mutation_touch_delta =
        modeling_projection_mutation_touch_serial_ -
        state.mutation_touch_serial;
    std::uint64_t enrolled_mutation_touch_delta = 0u;
    const auto add_mutation_delta = [&](std::uint64_t current,
                                        std::uint64_t captured) {
      if (current < captured) {
        return false;
      }
      const std::uint64_t delta = current - captured;
      if (delta > mutation_touch_delta ||
          enrolled_mutation_touch_delta > mutation_touch_delta - delta) {
        return false;
      }
      enrolled_mutation_touch_delta += delta;
      return true;
    };
    std::unordered_set<EntityId> captured_participant_ids;
    captured_participant_ids.reserve(state.participants.size());
    for (const auto &record : state.participants) {
      captured_participant_ids.insert(record.entity);
      const auto current =
          modeling_projection_mutations_by_entity_.find(record.entity);
      const ModelingProjectionMutationCounters counters =
          current == modeling_projection_mutations_by_entity_.end()
              ? ModelingProjectionMutationCounters{}
              : current->second;
      if ((has_projection_domain(
               state.domains,
               SceneModelingProjectionDomain::MeshBindings) &&
           !add_mutation_delta(counters.mesh_bindings,
                               record.mesh_binding_mutation_count)) ||
          (has_projection_domain(
               state.domains,
               SceneModelingProjectionDomain::CurveBindings) &&
           !add_mutation_delta(counters.curve_bindings,
                               record.curve_binding_mutation_count)) ||
          (has_projection_domain(state.domains,
                                 SceneModelingProjectionDomain::Selection) &&
           !add_mutation_delta(counters.selection,
                               record.selection_mutation_count)) ||
          (has_projection_domain(state.domains,
                                 SceneModelingProjectionDomain::Structure) &&
           !add_mutation_delta(counters.structure,
                               record.structure_mutation_count))) {
        return report(false);
      }
    }
    for (const EntityId entity : created) {
      if (captured_participant_ids.contains(entity)) {
        continue;
      }
      const auto current =
          modeling_projection_mutations_by_entity_.find(entity);
      const ModelingProjectionMutationCounters counters =
          current == modeling_projection_mutations_by_entity_.end()
              ? ModelingProjectionMutationCounters{}
              : current->second;
      const auto allocation_baseline =
          last_entity_allocation_mutation_baselines_.find(entity);
      const auto allocation = last_entity_allocation_serial_.find(entity);
      if (allocation_baseline ==
              last_entity_allocation_mutation_baselines_.end() ||
          allocation == last_entity_allocation_serial_.end() ||
          allocation_baseline->second.allocation_serial !=
              allocation->second) {
        return report(false);
      }
      const ModelingProjectionMutationCounters &baseline =
          allocation_baseline->second.counters;
      // An explicit creation receipt owns the complete provisional entity,
      // independent of the payload domains captured for pre-existing
      // participants. A history transition can recreate a previously deleted
      // identity, so its counters begin at the allocation-time baseline rather
      // than necessarily at zero.
      if (!add_mutation_delta(counters.mesh_bindings,
                              baseline.mesh_bindings) ||
          !add_mutation_delta(counters.curve_bindings,
                              baseline.curve_bindings) ||
          !add_mutation_delta(counters.selection, baseline.selection) ||
          !add_mutation_delta(counters.structure, baseline.structure)) {
        return report(false);
      }
    }
    if (enrolled_mutation_touch_delta != mutation_touch_delta) {
      return report(false);
    }
  }

  std::unordered_set<EntityId> participant_ids;
  participant_ids.reserve(state.participants.size() + created.size());
  std::unordered_set<EntityId> originally_existing;
  originally_existing.reserve(state.participants.size());
  for (const auto &record : state.participants) {
    participant_ids.insert(record.entity);
    if (record.existed) {
      originally_existing.insert(record.entity);
    }
  }
  participant_ids.insert(created.begin(), created.end());

  std::unordered_set<EntityId> removal_set;
  removal_set.reserve(created.size());
  for (const EntityId entity : created) {
    if (!originally_existing.contains(entity)) {
      removal_set.insert(entity);
    }
  }

  // Validate every point lookup and structural boundary before mutating. A
  // failed restore therefore leaves both forward indexes and components exact.
  for (const auto &record : state.participants) {
    ++work.entity_lookup_count;
    if (record.existed && !exists(record.entity) &&
        !has_projection_domain(state.domains,
                               SceneModelingProjectionDomain::Structure)) {
      return report(false);
    }
    if (!record.existed && exists(record.entity) &&
        !removal_set.contains(record.entity)) {
      return report(false);
    }
    if (!record.structure.has_value()) {
      continue;
    }
    const SceneEntityState &structure = *record.structure;
    if (structure.parent != kInvalidEntityId &&
        !exists(structure.parent) &&
        !originally_existing.contains(structure.parent)) {
      return report(false);
    }
    if (const auto uuid_owner = entity_by_uuid_.find(structure.uuid);
        uuid_owner != entity_by_uuid_.end() &&
        uuid_owner->second != record.entity &&
        !participant_ids.contains(uuid_owner->second)) {
      return report(false);
    }
    if (const auto editor_owner =
            entity_by_editor_object_id_.find(structure.editor_object_id);
        editor_owner != entity_by_editor_object_id_.end() &&
        editor_owner->second != record.entity &&
        !participant_ids.contains(editor_owner->second)) {
      return report(false);
    }
    if (exists(record.entity)) {
      const HierarchyComponent *current_hierarchy = hierarchy(record.entity);
      if (current_hierarchy == nullptr ||
          std::ranges::any_of(
              current_hierarchy->children,
              [&](EntityId child) { return !participant_ids.contains(child); })) {
        return report(false);
      }
    }
  }
  for (const EntityId entity : removal_set) {
    ++work.entity_lookup_count;
    if (!exists(entity)) {
      continue;
    }
    const HierarchyComponent *current_hierarchy = hierarchy(entity);
    if (current_hierarchy == nullptr ||
        std::ranges::any_of(current_hierarchy->children, [&](EntityId child) {
          return !removal_set.contains(child);
        }) ||
        instance_definition(entity) != nullptr) {
      return report(false);
    }
  }
  if (has_projection_domain(state.domains,
                            SceneModelingProjectionDomain::Structure)) {
    std::unordered_map<EntityId, std::vector<std::size_t>> boundary_ordinals;
    for (const auto &record : state.participants) {
      if (!record.structure.has_value()) {
        continue;
      }
      const SceneEntityState &structure = *record.structure;
      if (structure.parent != kInvalidEntityId &&
          !participant_ids.contains(structure.parent) &&
          structure.sibling_ordinal.has_value()) {
        boundary_ordinals[structure.parent].push_back(
            *structure.sibling_ordinal);
      }
    }
    for (auto &[parent_id, ordinals] : boundary_ordinals) {
      const HierarchyComponent *parent_hierarchy = hierarchy(parent_id);
      if (parent_hierarchy == nullptr) {
        return report(false);
      }
      const std::size_t retained_child_count =
          static_cast<std::size_t>(std::ranges::count_if(
              parent_hierarchy->children, [&](EntityId child) {
                return !participant_ids.contains(child);
              }));
      std::ranges::sort(ordinals);
      for (std::size_t index = 0u; index < ordinals.size(); ++index) {
        if (ordinals[index] > retained_child_count + index) {
          return report(false);
        }
      }
    }
  }

  if (preparation != nullptr) {
    preparation->created = std::move(created);
    preparation->participant_ids = std::move(participant_ids);
    preparation->originally_existing = std::move(originally_existing);
    preparation->removal_set = std::move(removal_set);
  }
  return report(true);
}

bool SceneDocument::restore_modeling_projection_checkpoint(
    const SceneModelingProjectionCheckpoint &checkpoint,
    std::span<const EntityId> created_participants,
    SceneModelingProjectionRestoreWork *reported_work) {
  return restore_modeling_projection_checkpoint_impl(
      checkpoint, nullptr, created_participants, false, reported_work);
}

bool SceneDocument::
    restore_modeling_projection_checkpoint_preserving_concurrent_changes(
        const SceneModelingProjectionCheckpoint &checkpoint,
        const SceneModelingProjectionMutationFence &acknowledged_fence,
        std::span<const EntityId> created_participants,
        SceneModelingProjectionRestoreWork *reported_work) {
  return restore_modeling_projection_checkpoint_impl(
      checkpoint, &acknowledged_fence, created_participants, true,
      reported_work);
}

bool SceneDocument::restore_modeling_projection_checkpoint_impl(
    const SceneModelingProjectionCheckpoint &checkpoint,
    const SceneModelingProjectionMutationFence *acknowledged_fence,
    std::span<const EntityId> created_participants,
    bool preserve_concurrent_changes,
    SceneModelingProjectionRestoreWork *reported_work) {
  SceneModelingProjectionRestoreWork work;
  ModelingProjectionRestorePreparation preparation;
  const auto report = [&](bool result) {
    if (reported_work != nullptr) {
      *reported_work = work;
    }
    return result;
  };
  if (!preflight_modeling_projection_restore(
          checkpoint, acknowledged_fence, created_participants,
          preserve_concurrent_changes, &preparation, &work)) {
    return report(false);
  }

  const SceneModelingProjectionCheckpoint::State &state = *checkpoint.state_;
  const std::vector<EntityId> &created = preparation.created;
  const std::unordered_set<EntityId> &participant_ids =
      preparation.participant_ids;
  const std::unordered_set<EntityId> &removal_set = preparation.removal_set;
  std::vector<EntityId> changed_participants;
  if (preserve_concurrent_changes) {
    std::unordered_set<EntityId> changed;
    changed.reserve(participant_ids.size());
    changed_participants.reserve(participant_ids.size());
    for (const auto &record : state.participants) {
      const auto current =
          modeling_projection_mutations_by_entity_.find(record.entity);
      const ModelingProjectionMutationCounters counters =
          current == modeling_projection_mutations_by_entity_.end()
              ? ModelingProjectionMutationCounters{}
              : current->second;
      const bool counter_changed =
          (has_projection_domain(
               state.domains,
               SceneModelingProjectionDomain::MeshBindings) &&
           counters.mesh_bindings > record.mesh_binding_mutation_count) ||
          (has_projection_domain(
               state.domains,
               SceneModelingProjectionDomain::CurveBindings) &&
           counters.curve_bindings > record.curve_binding_mutation_count) ||
          (has_projection_domain(state.domains,
                                 SceneModelingProjectionDomain::Selection) &&
           counters.selection > record.selection_mutation_count) ||
          (has_projection_domain(state.domains,
                                 SceneModelingProjectionDomain::Structure) &&
           counters.structure > record.structure_mutation_count);
      if ((counter_changed || exists(record.entity) != record.existed) &&
          changed.insert(record.entity).second) {
        changed_participants.push_back(record.entity);
      }
    }
    for (const EntityId entity : created) {
      if (removal_set.contains(entity) && exists(entity) &&
          changed.insert(entity).second) {
        changed_participants.push_back(entity);
      }
    }
  }

  // Remove only explicitly tracked new participants. This is intentionally a
  // point operation and never calls entities(), entity_tree_order(), or a full
  // registry view.
  for (auto it = created.rbegin(); it != created.rend(); ++it) {
    const EntityId entity_id = *it;
    if (!removal_set.contains(entity_id) || !exists(entity_id)) {
      continue;
    }
    const EntityId parent_id = parent(entity_id);
    if (HierarchyComponent *parent_hierarchy = mutable_hierarchy(parent_id)) {
      remove_child(parent_hierarchy->children, entity_id);
    }
    const entt::entity entity = require_entity(entity_id);
    if (const EditorObjectIdentityComponent *identity =
            editor_object_identity(entity_id)) {
      entity_by_editor_object_id_.erase(identity->id);
    }
    if (const EntityUuidComponent *identity = entity_uuid(entity_id)) {
      entity_by_uuid_.erase(identity->uuid);
    }
    if (const MeshComponent *component =
            registry_.try_get<MeshComponent>(entity)) {
      decrement_binding_reference(mesh_binding_reference_counts_,
                                  component->mesh);
    }
    if (const CurveComponent *component =
            registry_.try_get<CurveComponent>(entity)) {
      decrement_binding_reference(curve_binding_reference_counts_,
                                  component->curve);
    }
    registry_.destroy(entity);
    entities_.erase(entity_id);
    ids_by_entity_.erase(entity_key(entity));
    ++work.created_entity_removal_count;
  }

  const bool restore_structure = has_projection_domain(
      state.domains, SceneModelingProjectionDomain::Structure);
  if (restore_structure) {
    // Release participant-owned identity endpoints before restoring them so a
    // bounded swap among enrolled participants remains exact.
    for (const auto &record : state.participants) {
      if (!record.structure.has_value() || !exists(record.entity)) {
        continue;
      }
      if (const EntityUuidComponent *identity = entity_uuid(record.entity)) {
        entity_by_uuid_.erase(identity->uuid);
      }
      if (const EditorObjectIdentityComponent *identity =
              editor_object_identity(record.entity)) {
        entity_by_editor_object_id_.erase(identity->id);
      }
    }

    // Create missing pre-existing participants without consulting the public
    // allocator. The captured UUID/editor identity already owns this lease.
    for (const auto &record : state.participants) {
      if (!record.existed || exists(record.entity) ||
          !record.structure.has_value()) {
        continue;
      }
      const SceneEntityState &structure = *record.structure;
      const entt::entity entity = registry_.create();
      registry_.emplace<EntityIdentityComponent>(
          entity, EntityIdentityComponent{.id = record.entity});
      registry_.emplace<EntityUuidComponent>(
          entity, EntityUuidComponent{.uuid = structure.uuid});
      registry_.emplace<EditorObjectIdentityComponent>(
          entity,
          EditorObjectIdentityComponent{.id = structure.editor_object_id});
      registry_.emplace<NameComponent>(
          entity, NameComponent{.name = structure.name});
      registry_.emplace<TransformComponent>(entity, structure.transform);
      registry_.emplace<EntityKindComponent>(
          entity, EntityKindComponent{.kind = structure.kind});
      registry_.emplace<HierarchyComponent>(entity, HierarchyComponent{});
      registry_.emplace<VisibilityComponent>(entity, structure.visibility);
      registry_.emplace<LockComponent>(entity, structure.lock);
      registry_.emplace<SelectionComponent>(entity, SelectionComponent{});
      entities_.emplace(record.entity, entity);
      ids_by_entity_.emplace(entity_key(entity), record.entity);
      ++work.recreated_entity_count;
    }

    for (const auto &record : state.participants) {
      if (!record.structure.has_value() || !exists(record.entity)) {
        continue;
      }
      const SceneEntityState &structure = *record.structure;
      const entt::entity entity = require_entity(record.entity);
      registry_.emplace_or_replace<EntityUuidComponent>(
          entity, EntityUuidComponent{.uuid = structure.uuid});
      registry_.emplace_or_replace<EditorObjectIdentityComponent>(
          entity,
          EditorObjectIdentityComponent{.id = structure.editor_object_id});
      registry_.emplace_or_replace<NameComponent>(
          entity, NameComponent{.name = structure.name});
      registry_.emplace_or_replace<TransformComponent>(entity,
                                                       structure.transform);
      registry_.emplace_or_replace<EntityKindComponent>(
          entity, EntityKindComponent{.kind = structure.kind});
      registry_.emplace_or_replace<VisibilityComponent>(entity,
                                                        structure.visibility);
      registry_.emplace_or_replace<LockComponent>(entity, structure.lock);
      restore_optional_component(registry_, entity,
                                 structure.modeling_object);
      restore_optional_component(registry_, entity, structure.material);
      restore_optional_component(registry_, entity,
                                 structure.texture_mapping);
      restore_optional_component(registry_, entity, structure.light);
      restore_optional_component(registry_, entity,
                                 structure.instance_definition);
      restore_optional_component(registry_, entity,
                                 structure.instance_occurrence);
      restore_optional_component(registry_, entity,
                                 structure.scene_settings);
      if (structure.layer_id == kInvalidSceneLayerId) {
        registry_.remove<SceneLayerMembershipComponent>(entity);
      } else {
        registry_.emplace_or_replace<SceneLayerMembershipComponent>(
            entity,
            SceneLayerMembershipComponent{.layer_id = structure.layer_id});
      }
      entity_by_uuid_[structure.uuid] = record.entity;
      entity_by_editor_object_id_[structure.editor_object_id] = record.entity;
      ++work.structure_write_count;
    }

    std::unordered_map<EntityId,
                       std::vector<std::pair<std::size_t, EntityId>>>
        boundary_children;
    for (const auto &record : state.participants) {
      if (!record.structure.has_value() || !record.hierarchy.has_value() ||
          !exists(record.entity)) {
        continue;
      }
      const EntityId current_parent = parent(record.entity);
      if (current_parent != kInvalidEntityId &&
          !participant_ids.contains(current_parent)) {
        if (HierarchyComponent *parent_hierarchy =
                mutable_hierarchy(current_parent)) {
          remove_child(parent_hierarchy->children, record.entity);
          ++work.boundary_hierarchy_write_count;
        }
      }
      registry_.emplace_or_replace<HierarchyComponent>(
          require_entity(record.entity), *record.hierarchy);
      const SceneEntityState &structure = *record.structure;
      if (structure.parent != kInvalidEntityId &&
          !participant_ids.contains(structure.parent) &&
          structure.sibling_ordinal.has_value()) {
        boundary_children[structure.parent].emplace_back(
            *structure.sibling_ordinal, record.entity);
      }
    }
    for (auto &[parent_id, insertions] : boundary_children) {
      HierarchyComponent &parent_hierarchy =
          registry_.get<HierarchyComponent>(require_entity(parent_id));
      std::erase_if(parent_hierarchy.children, [&](EntityId child) {
        return participant_ids.contains(child);
      });
      std::ranges::sort(insertions);
      for (const auto &[ordinal, child] : insertions) {
        parent_hierarchy.children.insert(
            parent_hierarchy.children.begin() +
                static_cast<std::ptrdiff_t>(ordinal),
            child);
      }
      ++work.boundary_hierarchy_write_count;
    }
  }

  bool restored_curve_material_surface_changed = false;
  for (const auto &record : state.participants) {
    if (!record.existed || !exists(record.entity)) {
      continue;
    }
    const entt::entity entity = require_entity(record.entity);
    if (has_projection_domain(
            state.domains, SceneModelingProjectionDomain::MeshBindings)) {
      const MeshComponent *current = registry_.try_get<MeshComponent>(entity);
      replace_binding_reference(
          mesh_binding_reference_counts_,
          current == nullptr ? std::nullopt
                             : std::optional<AssetId>{current->mesh},
          record.mesh.has_value()
              ? std::optional<AssetId>{record.mesh->mesh}
              : std::nullopt);
      restore_optional_component(registry_, entity, record.mesh);
      ++work.mesh_binding_write_count;
    }
    if (has_projection_domain(
            state.domains, SceneModelingProjectionDomain::CurveBindings)) {
      const CurveComponent *current = registry_.try_get<CurveComponent>(entity);
      const bool current_is_material_surface =
          current != nullptr && current->solidify;
      const bool restored_is_material_surface =
          record.curve.has_value() && record.curve->solidify;
      restored_curve_material_surface_changed =
          restored_curve_material_surface_changed ||
          current_is_material_surface != restored_is_material_surface;
      replace_binding_reference(
          curve_binding_reference_counts_,
          current == nullptr ? std::nullopt
                             : std::optional<AssetId>{current->curve},
          record.curve.has_value()
              ? std::optional<AssetId>{record.curve->curve}
              : std::nullopt);
      restore_optional_component(registry_, entity, record.curve);
      ++work.curve_binding_write_count;
    }
    if (has_projection_domain(
            state.domains, SceneModelingProjectionDomain::MeshBindings) ||
        has_projection_domain(
            state.domains, SceneModelingProjectionDomain::CurveBindings)) {
      registry_.emplace_or_replace<EntityKindComponent>(
          entity, EntityKindComponent{.kind = record.kind});
    }
    if (has_projection_domain(state.domains,
                              SceneModelingProjectionDomain::Selection)) {
      restore_optional_component(registry_, entity, record.selection);
      ++work.selection_write_count;
    }
  }

  if (has_projection_domain(state.domains,
                            SceneModelingProjectionDomain::Selection)) {
    if (!preserve_concurrent_changes ||
        active_selection_ == kInvalidEntityId ||
        participant_ids.contains(active_selection_)) {
      active_selection_ = state.active_selection;
    }
  }
  if (preserve_concurrent_changes) {
    for (const EntityId entity : changed_participants) {
      const SceneModelingProjectionDomain restored_domains =
          removal_set.contains(entity)
              ? state.domains | SceneModelingProjectionDomain::Structure
              : state.domains;
      note_modeling_projection_mutation(entity, restored_domains);
    }
    if (!changed_participants.empty()) {
      const bool restore_mesh = has_projection_domain(
          state.domains, SceneModelingProjectionDomain::MeshBindings);
      const bool restore_curve = has_projection_domain(
          state.domains, SceneModelingProjectionDomain::CurveBindings);
      const bool restore_selection = has_projection_domain(
          state.domains, SceneModelingProjectionDomain::Selection);
      const bool restore_structure =
          has_projection_domain(state.domains,
                                SceneModelingProjectionDomain::Structure) ||
          work.created_entity_removal_count != 0u;
      ++revision_;
      ++content_revision_;
      if (restore_selection) {
        ++selection_revision_;
      }
      if (restore_mesh) {
        ++mesh_binding_revision_;
      }
      if (restore_curve) {
        ++curve_binding_revision_;
      }
      if (restored_curve_material_surface_changed) {
        ++material_binding_revision_;
      }
      if (restore_structure) {
        ++visibility_revision_;
        ++transform_revision_;
        ++texture_mapping_revision_;
        ++material_binding_revision_;
        ++layer_revision_;
      }
      if (restore_mesh || restore_curve || restore_structure) {
        ++hierarchy_presentation_revision_;
        ++render_component_revision_;
      }
    }
  } else {
    for (const EntityId entity : removal_set) {
      const auto allocation_baseline =
          last_entity_allocation_mutation_baselines_.find(entity);
      const auto allocation = last_entity_allocation_serial_.find(entity);
      if (allocation_baseline !=
              last_entity_allocation_mutation_baselines_.end() &&
          allocation != last_entity_allocation_serial_.end() &&
          allocation_baseline->second.allocation_serial ==
              allocation->second &&
          allocation_baseline->second.record_existed) {
        modeling_projection_mutations_by_entity_[entity] =
            allocation_baseline->second.counters;
      } else {
        modeling_projection_mutations_by_entity_.erase(entity);
      }
    }
    next_id_ = state.next_id;
    next_editor_object_id_ = state.next_editor_object_id;
    revision_ = state.revision;
    content_revision_ = state.content_revision;
    selection_revision_ = state.selection_revision;
    hierarchy_presentation_revision_ = state.hierarchy_presentation_revision;
    visibility_revision_ = state.visibility_revision;
    transform_revision_ = state.transform_revision;
    texture_mapping_revision_ = state.texture_mapping_revision;
    mesh_binding_revision_ = state.mesh_binding_revision;
    curve_binding_revision_ = state.curve_binding_revision;
    material_binding_revision_ = state.material_binding_revision;
    render_component_revision_ = state.render_component_revision;
    layer_revision_ = state.layer_revision;
  }
  invalidate_spatial_projection_caches_after_revision_restore();
  return report(true);
}

namespace {

std::uint32_t entity_key(entt::entity entity) {
  return static_cast<std::uint32_t>(entt::to_integral(entity));
}

void remove_child(std::vector<EntityId> &children, EntityId child) {
  children.erase(std::remove(children.begin(), children.end(), child),
                 children.end());
}

std::mt19937 &entity_uuid_random_engine() {
  thread_local std::mt19937 engine = [] {
    std::random_device random_device;
    std::array<std::uint32_t, 8> seed_data{};
    for (std::uint32_t &value : seed_data) {
      value = random_device();
    }
    std::seed_seq seed(seed_data.begin(), seed_data.end());
    return std::mt19937(seed);
  }();
  return engine;
}

std::string generate_unique_entity_uuid(
    const std::unordered_map<std::string, EntityId> &existing) {
  uuids::uuid_random_generator generator{entity_uuid_random_engine()};
  for (;;) {
    const std::string uuid = uuids::to_string(generator());
    if (!uuid.empty() && !existing.contains(uuid)) {
      return uuid;
    }
  }
}

bool valid_entity_uuid(std::string_view uuid) {
  return uuids::uuid::from_string(uuid).has_value();
}

bool same_transform(const TransformComponent &left,
                    const TransformComponent &right) {
  return left.position.x == right.position.x &&
         left.position.y == right.position.y &&
         left.position.z == right.position.z &&
         left.rotation_euler.x == right.rotation_euler.x &&
         left.rotation_euler.y == right.rotation_euler.y &&
         left.rotation_euler.z == right.rotation_euler.z &&
         left.scale.x == right.scale.x && left.scale.y == right.scale.y &&
         left.scale.z == right.scale.z &&
         left.custom_pivot == right.custom_pivot;
}

bool same_modeling_object(const ModelingObjectComponent &left,
                          const ModelingObjectComponent &right) {
  return left.object_id == right.object_id &&
         left.material_slot == right.material_slot &&
         left.sdk_owned == right.sdk_owned &&
         left.local_frame_version == right.local_frame_version;
}

bool valid_layer_color(QVec3 color) {
  return std::isfinite(color.x) && std::isfinite(color.y) &&
         std::isfinite(color.z) && color.x >= 0.0f && color.x <= 1.0f &&
         color.y >= 0.0f && color.y <= 1.0f && color.z >= 0.0f &&
         color.z <= 1.0f;
}

bool valid_layer_name(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  std::size_t code_points = 0u;
  for (std::size_t index = 0u; index < name.size();) {
    const auto lead = static_cast<unsigned char>(name[index]);
    std::size_t length = 0u;
    std::uint32_t value = 0u;
    if (lead <= 0x7fu) {
      length = 1u;
      value = lead;
    } else if (lead >= 0xc2u && lead <= 0xdfu) {
      length = 2u;
      value = lead & 0x1fu;
    } else if (lead >= 0xe0u && lead <= 0xefu) {
      length = 3u;
      value = lead & 0x0fu;
    } else if (lead >= 0xf0u && lead <= 0xf4u) {
      length = 4u;
      value = lead & 0x07u;
    } else {
      return false;
    }
    if (index + length > name.size()) {
      return false;
    }
    for (std::size_t offset = 1u; offset < length; ++offset) {
      const auto continuation =
          static_cast<unsigned char>(name[index + offset]);
      if ((continuation & 0xc0u) != 0x80u) {
        return false;
      }
      value = (value << 6u) | (continuation & 0x3fu);
    }
    if ((length == 3u && value < 0x800u) ||
        (length == 4u && value < 0x10000u) ||
        (value >= 0xd800u && value <= 0xdfffu) || value > 0x10ffffu) {
      return false;
    }
    index += length;
    if (++code_points > 127u) {
      return false;
    }
  }
  return true;
}

void normalize_layer_state(SceneLayerState &state) {
  std::sort(state.layers.begin(), state.layers.end(),
            [](const SceneLayerRecord &left, const SceneLayerRecord &right) {
              return left.id < right.id;
            });
  std::sort(state.assignments.begin(), state.assignments.end(),
            [](const SceneLayerAssignment &left,
               const SceneLayerAssignment &right) {
              return left.entity < right.entity;
            });
}

bool is_supported_instance_member_subtree(const SceneDocument &scene,
                                          EntityId root) {
  for (const EntityId entity : scene.descendants(root, true)) {
    if (scene.is_instance_definition_entity(entity) ||
        scene.instance_occurrence(entity) != nullptr ||
        scene.curve(entity) != nullptr ||
        scene.light(entity) != nullptr) {
      return false;
    }
    switch (scene.kind(entity)) {
    case EntityKind::Mesh:
      if (scene.mesh(entity) == nullptr) {
        return false;
      }
      break;
    case EntityKind::Empty:
      if (scene.mesh(entity) != nullptr ||
          scene.modeling_object(entity) != nullptr) {
        return false;
      }
      break;
    case EntityKind::Light:
    case EntityKind::Generic:
    case EntityKind::Instance:
    case EntityKind::Curve:
      return false;
    }
  }
  return true;
}

bool destruction_preserves_instance_graph(
    const SceneDocument &scene,
    const std::unordered_set<EntityId> &destroyed_entities) {
  const std::vector<EntityId> entities = scene.entities();
  std::unordered_map<EntityId, std::size_t> occurrence_counts;
  occurrence_counts.reserve(entities.size());
  for (const EntityId entity : entities) {
    const InstanceOccurrenceComponent *occurrence =
        scene.instance_occurrence(entity);
    if (occurrence == nullptr) {
      continue;
    }
    ++occurrence_counts[occurrence->definition];
    if (destroyed_entities.contains(occurrence->definition) &&
        !destroyed_entities.contains(entity)) {
      return false;
    }
  }

  for (const EntityId entity : entities) {
    if (!scene.is_instance_definition_entity(entity)) {
      continue;
    }
    const bool definition_destroyed = destroyed_entities.contains(entity);
    if (definition_destroyed) {
      continue;
    }

    const std::vector<EntityId> members = scene.children(entity);
    if (occurrence_counts.contains(entity) &&
        std::none_of(members.begin(), members.end(),
                     [&](EntityId member) {
                       return !destroyed_entities.contains(member);
                     })) {
      return false;
    }
  }
  return true;
}

} // namespace

void SceneDocument::set_editor_object_id_allocator(
    EditorObjectIdAllocator allocator) {
  editor_object_id_allocator_ = std::move(allocator);
}

EntityId SceneDocument::create_entity(std::string name, EntityKind kind,
                                      EditorObjectId editor_object_id) {
  const EntityId id = next_id_;
  const EditorObjectId object_id = allocate_editor_object_id(editor_object_id);
  if (object_id == kInvalidEditorObjectId) {
    return kInvalidEntityId;
  }
  ++next_id_;
  const entt::entity entity = registry_.create();
  registry_.emplace<EntityIdentityComponent>(entity,
                                             EntityIdentityComponent{.id = id});
  const std::string uuid = generate_unique_entity_uuid(entity_by_uuid_);
  registry_.emplace<EntityUuidComponent>(entity,
                                         EntityUuidComponent{.uuid = uuid});
  registry_.emplace<EditorObjectIdentityComponent>(
      entity, EditorObjectIdentityComponent{.id = object_id});
  registry_.emplace<NameComponent>(entity,
                                   NameComponent{.name = std::move(name)});
  registry_.emplace<TransformComponent>(entity, TransformComponent{});
  registry_.emplace<EntityKindComponent>(entity,
                                         EntityKindComponent{.kind = kind});
  registry_.emplace<HierarchyComponent>(entity, HierarchyComponent{});
  registry_.emplace<VisibilityComponent>(entity, VisibilityComponent{});
  registry_.emplace<LockComponent>(entity, LockComponent{});
  registry_.emplace<SelectionComponent>(entity, SelectionComponent{});
  entities_.emplace(id, entity);
  ids_by_entity_.emplace(entity_key(entity), id);
  entity_by_uuid_.emplace(uuid, id);
  entity_by_editor_object_id_.emplace(object_id, id);
  const auto previous_allocation = last_entity_allocation_serial_.find(id);
  const std::uint64_t previous_allocation_serial =
      previous_allocation == last_entity_allocation_serial_.end()
          ? 0u
          : previous_allocation->second;
  const auto mutation_baseline =
      modeling_projection_mutations_by_entity_.find(id);
  const std::uint64_t allocation_serial = ++entity_allocation_serial_;
  last_entity_allocation_serial_[id] = allocation_serial;
  last_entity_allocation_mutation_baselines_[id] = {
      .allocation_serial = allocation_serial,
      .counters = mutation_baseline ==
                          modeling_projection_mutations_by_entity_.end()
                      ? ModelingProjectionMutationCounters{}
                      : mutation_baseline->second,
      .record_existed = mutation_baseline !=
                        modeling_projection_mutations_by_entity_.end(),
  };
  note_scalar_compensation_entity_allocation(id, object_id,
                                             allocation_serial,
                                             previous_allocation_serial);
  ++hierarchy_presentation_revision_;
  ++visibility_revision_;
  ++transform_revision_;
  ++render_component_revision_;
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return id;
}

EntityId SceneDocument::create_entity_with_id(EntityId id, std::string name,
                                              EntityKind kind,
                                              EditorObjectId editor_object_id) {
  if (id == kInvalidEntityId) {
    return create_entity(std::move(name), kind, editor_object_id);
  }
  if (exists(id)) {
    return kInvalidEntityId;
  }
  const EditorObjectId object_id = allocate_editor_object_id(editor_object_id);
  if (object_id == kInvalidEditorObjectId) {
    return kInvalidEntityId;
  }
  next_id_ = std::max(next_id_, id + 1u);
  const entt::entity entity = registry_.create();
  registry_.emplace<EntityIdentityComponent>(entity,
                                             EntityIdentityComponent{.id = id});
  const std::string uuid = generate_unique_entity_uuid(entity_by_uuid_);
  registry_.emplace<EntityUuidComponent>(entity,
                                         EntityUuidComponent{.uuid = uuid});
  registry_.emplace<EditorObjectIdentityComponent>(
      entity, EditorObjectIdentityComponent{.id = object_id});
  registry_.emplace<NameComponent>(entity,
                                   NameComponent{.name = std::move(name)});
  registry_.emplace<TransformComponent>(entity, TransformComponent{});
  registry_.emplace<EntityKindComponent>(entity,
                                         EntityKindComponent{.kind = kind});
  registry_.emplace<HierarchyComponent>(entity, HierarchyComponent{});
  registry_.emplace<VisibilityComponent>(entity, VisibilityComponent{});
  registry_.emplace<LockComponent>(entity, LockComponent{});
  registry_.emplace<SelectionComponent>(entity, SelectionComponent{});
  entities_.emplace(id, entity);
  ids_by_entity_.emplace(entity_key(entity), id);
  entity_by_uuid_.emplace(uuid, id);
  entity_by_editor_object_id_.emplace(object_id, id);
  const auto previous_allocation = last_entity_allocation_serial_.find(id);
  const std::uint64_t previous_allocation_serial =
      previous_allocation == last_entity_allocation_serial_.end()
          ? 0u
          : previous_allocation->second;
  const auto mutation_baseline =
      modeling_projection_mutations_by_entity_.find(id);
  const std::uint64_t allocation_serial = ++entity_allocation_serial_;
  last_entity_allocation_serial_[id] = allocation_serial;
  last_entity_allocation_mutation_baselines_[id] = {
      .allocation_serial = allocation_serial,
      .counters = mutation_baseline ==
                          modeling_projection_mutations_by_entity_.end()
                      ? ModelingProjectionMutationCounters{}
                      : mutation_baseline->second,
      .record_existed = mutation_baseline !=
                        modeling_projection_mutations_by_entity_.end(),
  };
  note_scalar_compensation_entity_allocation(id, object_id,
                                             allocation_serial,
                                             previous_allocation_serial);
  ++hierarchy_presentation_revision_;
  ++transform_revision_;
  ++visibility_revision_;
  ++render_component_revision_;
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return id;
}

EntityId SceneDocument::create_empty(std::string name,
                                     EditorObjectId editor_object_id) {
  return create_entity(std::move(name), EntityKind::Empty, editor_object_id);
}

bool SceneDocument::destroy_entity(EntityId id) {
  return destroy_entities(std::span<const EntityId>(&id, 1u));
}

bool SceneDocument::destroy_entities(std::span<const EntityId> ids) {
  std::unordered_set<EntityId> destroy_set;
  for (const EntityId id : ids) {
    if (is_scene_root(id)) {
      continue;
    }
    const std::vector<EntityId> descendants_to_destroy = descendants(id, true);
    destroy_set.insert(descendants_to_destroy.begin(),
                       descendants_to_destroy.end());
  }
  if (destroy_set.empty()) {
    return false;
  }

  std::vector<EntityId> to_destroy;
  to_destroy.reserve(destroy_set.size());
  for (const EntityId entity_id : entity_tree_order()) {
    if (destroy_set.contains(entity_id)) {
      to_destroy.push_back(entity_id);
    }
  }
  if (to_destroy.empty()) {
    return false;
  }

  if (!destruction_preserves_instance_graph(*this, destroy_set)) {
    return false;
  }

  const bool active_destroyed = destroy_set.contains(active_selection_);
  const bool layer_membership_destroyed =
      std::any_of(to_destroy.begin(), to_destroy.end(),
                  [this](EntityId entity) {
                    return entity_layer(entity) != kInvalidSceneLayerId;
                  });
  const bool material_binding_destroyed =
      std::any_of(to_destroy.begin(), to_destroy.end(),
                  [this](EntityId entity) {
                    const CurveComponent *curve_component = curve(entity);
                    return material(entity) != nullptr ||
                           (curve_component != nullptr &&
                            curve_component->solidify);
                  });
  const bool curve_binding_destroyed =
      std::any_of(to_destroy.begin(), to_destroy.end(),
                  [this](EntityId entity) { return curve(entity) != nullptr; });

  std::unordered_set<EntityId> surviving_parents;
  surviving_parents.reserve(to_destroy.size());
  for (const EntityId entity_id : to_destroy) {
    const EntityId parent_id = parent(entity_id);
    if (parent_id != kInvalidEntityId &&
        !destroy_set.contains(parent_id)) {
      surviving_parents.insert(parent_id);
    }
  }
  for (const EntityId parent_id : surviving_parents) {
    if (HierarchyComponent *parent_hierarchy =
            mutable_hierarchy(parent_id)) {
      std::erase_if(parent_hierarchy->children, [&](EntityId child) {
        return destroy_set.contains(child);
      });
    }
  }

  for (auto it = to_destroy.rbegin(); it != to_destroy.rend(); ++it) {
    const EntityId entity_id = *it;
    const entt::entity entity = require_entity(entity_id);
    if (entity == entt::null) {
      continue;
    }
    if (const EditorObjectIdentityComponent *identity =
            editor_object_identity(entity_id)) {
      entity_by_editor_object_id_.erase(identity->id);
    }
    if (const EntityUuidComponent *identity = entity_uuid(entity_id)) {
      entity_by_uuid_.erase(identity->uuid);
    }
    if (const MeshComponent *component =
            registry_.try_get<MeshComponent>(entity)) {
      decrement_binding_reference(mesh_binding_reference_counts_,
                                  component->mesh);
    }
    if (const CurveComponent *component =
            registry_.try_get<CurveComponent>(entity)) {
      decrement_binding_reference(curve_binding_reference_counts_,
                                  component->curve);
    }
    registry_.destroy(entity);
    entities_.erase(entity_id);
    ids_by_entity_.erase(entity_key(entity));
  }

  if (active_destroyed || !is_selected(active_selection_)) {
    active_selection_ = first_selected_entity();
  }
  ++hierarchy_presentation_revision_;
  ++selection_revision_;
  ++visibility_revision_;
  ++transform_revision_;
  ++mesh_binding_revision_;
  if (curve_binding_destroyed) {
    ++curve_binding_revision_;
  }
  if (material_binding_destroyed) {
    ++material_binding_revision_;
  }
  ++render_component_revision_;
  if (layer_membership_destroyed) {
    ++layer_revision_;
  }
  mark_dirty(to_destroy, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::exists(EntityId id) const {
  return require_entity(id) != entt::null;
}

bool SceneDocument::rename_entity(EntityId id, std::string name) {
  if (NameComponent *component = mutable_name(id)) {
    if (component->name == name) {
      return true;
    }
    component->name = std::move(name);
    ++hierarchy_presentation_revision_;
    mark_dirty(id, SceneModelingProjectionDomain::Structure);
    return true;
  }
  return false;
}

bool SceneDocument::set_entity_uuid(EntityId id, std::string uuid) {
  if (uuid.empty() || !valid_entity_uuid(uuid)) {
    return false;
  }
  const entt::entity entity = require_entity(id);
  if (entity == entt::null) {
    return false;
  }
  if (const auto existing = entity_by_uuid_.find(uuid);
      existing != entity_by_uuid_.end() && existing->second != id) {
    return false;
  }

  EntityUuidComponent *identity =
      registry_.try_get<EntityUuidComponent>(entity);
  if (identity != nullptr && identity->uuid == uuid) {
    return true;
  }
  if (identity != nullptr && !identity->uuid.empty()) {
    entity_by_uuid_.erase(identity->uuid);
    identity->uuid = std::move(uuid);
  } else {
    registry_.emplace_or_replace<EntityUuidComponent>(
        entity, EntityUuidComponent{.uuid = std::move(uuid)});
    identity = registry_.try_get<EntityUuidComponent>(entity);
  }
  if (identity != nullptr) {
    entity_by_uuid_[identity->uuid] = id;
  }
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::set_editor_object_id(EntityId id,
                                         EditorObjectId editor_object_id) {
  if (editor_object_id == kInvalidEditorObjectId) {
    return false;
  }
  const entt::entity entity = require_entity(id);
  if (entity == entt::null) {
    return false;
  }
  if (const auto existing = entity_by_editor_object_id_.find(editor_object_id);
      existing != entity_by_editor_object_id_.end() && existing->second != id) {
    return false;
  }

  EditorObjectIdentityComponent *identity =
      registry_.try_get<EditorObjectIdentityComponent>(entity);
  if (identity != nullptr && identity->id == editor_object_id) {
    return true;
  }
  if (editor_object_id_allocator_) {
    const EditorObjectId reserved =
        editor_object_id_allocator_(editor_object_id);
    if (reserved != editor_object_id) {
      return false;
    }
  }
  if (identity != nullptr && identity->id != kInvalidEditorObjectId) {
    entity_by_editor_object_id_.erase(identity->id);
    identity->id = editor_object_id;
  } else {
    registry_.emplace_or_replace<EditorObjectIdentityComponent>(
        entity, EditorObjectIdentityComponent{.id = editor_object_id});
  }
  entity_by_editor_object_id_[editor_object_id] = id;
  next_editor_object_id_ =
      std::max(next_editor_object_id_, editor_object_id + 1u);
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::set_entity_kind(EntityId id, EntityKind kind) {
  if (curve(id) != nullptr && kind != EntityKind::Curve) {
    return false;
  }
  if (texture_mapping(id) != nullptr && kind != EntityKind::Mesh &&
      kind != EntityKind::Instance) {
    return false;
  }
  if (instance_occurrence(id) != nullptr && kind != EntityKind::Instance) {
    return false;
  }
  if (is_instance_definition_entity(id) && kind != EntityKind::Empty &&
      kind != EntityKind::Generic) {
    return false;
  }
  if (is_instance_definition_member(id) && kind != EntityKind::Mesh &&
      kind != EntityKind::Empty) {
    return false;
  }
  if (kind == EntityKind::Instance && instance_occurrence(id) == nullptr) {
    return false;
  }
  if (EntityKindComponent *component = mutable_entity_kind(id)) {
    if (component->kind == kind) {
      return true;
    }
    component->kind = kind;
    ++hierarchy_presentation_revision_;
    mark_dirty(id, SceneModelingProjectionDomain::Structure);
    return true;
  }
  return false;
}

bool SceneDocument::set_visible(EntityId id, bool visible) {
  if (VisibilityComponent *component = mutable_visibility(id)) {
    if (component->visible == visible) {
      return true;
    }
    component->visible = visible;
    ++hierarchy_presentation_revision_;
    ++visibility_revision_;
    mark_dirty(id, SceneModelingProjectionDomain::Structure);
    return true;
  }
  return false;
}

bool SceneDocument::set_selectable(EntityId id, bool selectable) {
  if (VisibilityComponent *component = mutable_visibility(id)) {
    if (component->selectable == selectable) {
      return true;
    }
    component->selectable = selectable;
    ++visibility_revision_;
    mark_dirty(id, SceneModelingProjectionDomain::Structure);
    return true;
  }
  return false;
}

bool SceneDocument::set_locked(EntityId id, bool locked) {
  if (LockComponent *component = mutable_lock(id)) {
    if (component->locked == locked) {
      return true;
    }
    component->locked = locked;
    ++hierarchy_presentation_revision_;
    mark_dirty(id, SceneModelingProjectionDomain::Structure);
    return true;
  }
  return false;
}

bool SceneDocument::set_selected(EntityId id, bool selected) {
  if (SelectionComponent *component = mutable_selection(id)) {
    const bool was_selected = component->selected;
    const EntityId previous_active_selection = active_selection_;
    component->selected = selected;
    if (selected) {
      active_selection_ = id;
    } else if (active_selection_ == id) {
      active_selection_ = first_selected_entity();
    }
    if (was_selected != selected ||
        previous_active_selection != active_selection_) {
      ++selection_revision_;
      const std::array touched{id, previous_active_selection,
                               active_selection_};
      mark_transient_dirty(touched,
                           SceneModelingProjectionDomain::Selection);
    }
    return true;
  }
  return false;
}

bool SceneDocument::set_active_selection(EntityId id) {
  if (!is_selected(id)) {
    return false;
  }
  if (active_selection_ != id) {
    const EntityId previous_active_selection = active_selection_;
    active_selection_ = id;
    ++selection_revision_;
    const std::array touched{id, previous_active_selection};
    mark_transient_dirty(touched,
                         SceneModelingProjectionDomain::Selection);
  }
  return true;
}

bool SceneDocument::replace_selection(std::span<const EntityId> selected,
                                      EntityId active) {
  const EntityId previous_active_selection = active_selection_;
  std::unordered_set<EntityId> desired;
  desired.reserve(selected.size());
  EntityId fallback_active = kInvalidEntityId;
  for (const EntityId entity : selected) {
    if (entity != kInvalidEntityId && exists(entity) &&
        desired.insert(entity).second) {
      fallback_active = entity;
    }
  }
  const EntityId resolved_active =
      active != kInvalidEntityId && desired.contains(active)
          ? active
          : fallback_active;

  bool changed = active_selection_ != resolved_active;
  const std::vector<EntityId> current = selected_entities();
  for (const EntityId entity : current) {
    if (desired.contains(entity)) {
      continue;
    }
    if (SelectionComponent *component = mutable_selection(entity)) {
      component->selected = false;
      changed = true;
    }
  }
  for (const EntityId entity : desired) {
    SelectionComponent *component = mutable_selection(entity);
    if (component != nullptr && !component->selected) {
      component->selected = true;
      changed = true;
    }
  }

  active_selection_ = resolved_active;
  if (changed) {
    ++selection_revision_;
    std::vector<EntityId> touched = current;
    touched.insert(touched.end(), desired.begin(), desired.end());
    touched.push_back(previous_active_selection);
    touched.push_back(resolved_active);
    mark_transient_dirty(touched,
                         SceneModelingProjectionDomain::Selection);
  }
  return true;
}

void SceneDocument::clear_selection() {
  (void)replace_selection({}, kInvalidEntityId);
}

bool SceneDocument::set_parent(EntityId child, EntityId new_parent,
                               bool preserve_world_transform) {
  if (child == kInvalidEntityId || require_entity(child) == entt::null) {
    return false;
  }
  if (is_scene_root(child) && new_parent != kInvalidEntityId) {
    return false;
  }
  if (new_parent != kInvalidEntityId &&
      require_entity(new_parent) == entt::null) {
    return false;
  }
  if (child == new_parent || is_descendant_of(new_parent, child)) {
    return false;
  }
  if (instance_occurrence(new_parent) != nullptr) {
    return false;
  }
  if (instance_occurrence(child) != nullptr &&
      containing_instance_definition(new_parent) != kInvalidEntityId) {
    return false;
  }
  if (is_instance_definition_entity(child) &&
      containing_instance_definition(new_parent) != kInvalidEntityId) {
    return false;
  }

  const EntityId old_definition = containing_instance_definition(child);
  const EntityId new_definition =
      containing_instance_definition(new_parent);
  if (old_definition != kInvalidEntityId && old_definition != child &&
      new_definition != old_definition &&
      !instance_occurrences_for_definition(old_definition).empty()) {
    const bool extracts_redundant_top_level_member =
        new_definition == kInvalidEntityId && parent(child) == old_definition &&
        children(old_definition).size() > 1u;
    if (!extracts_redundant_top_level_member) {
      return false;
    }
  }
  if (new_definition != kInvalidEntityId && new_definition != child &&
      old_definition != new_definition &&
      !is_supported_instance_member_subtree(*this, child)) {
    return false;
  }

  HierarchyComponent *child_hierarchy = mutable_hierarchy(child);
  if (!child_hierarchy) {
    return false;
  }

  const EntityId old_parent = child_hierarchy->parent;
  if (old_parent == new_parent) {
    return true;
  }

  const QMat4 child_world = preserve_world_transform
                                ? world_transform_matrix(child)
                                : local_transform_matrix(child);
  std::optional<TransformComponent> preserved_local_transform;
  if (preserve_world_transform) {
    const TransformComponent *current_transform = transform(child);
    const QMat4 parent_world = new_parent == kInvalidEntityId
                                   ? scene_transform_identity_matrix()
                                   : world_transform_matrix(new_parent);
    if (current_transform == nullptr) {
      return false;
    }
    preserved_local_transform = try_scene_local_transform_preserving_world(
        parent_world, child_world, *current_transform);
    if (!preserved_local_transform.has_value()) {
      return false;
    }
  }

  if (HierarchyComponent *old_parent_hierarchy =
          mutable_hierarchy(old_parent)) {
    remove_child(old_parent_hierarchy->children, child);
  }

  child_hierarchy->parent = new_parent;
  if (HierarchyComponent *new_parent_hierarchy =
          mutable_hierarchy(new_parent)) {
    if (std::find(new_parent_hierarchy->children.begin(),
                  new_parent_hierarchy->children.end(),
                  child) == new_parent_hierarchy->children.end()) {
      new_parent_hierarchy->children.push_back(child);
    }
  }

  if (preserve_world_transform) {
    if (TransformComponent *local_transform = mutable_transform(child)) {
      *local_transform = *preserved_local_transform;
    }
  }

  bool layer_membership_removed = false;
  if (new_definition != kInvalidEntityId && new_definition != child &&
      old_definition != new_definition) {
    for (const EntityId entity : descendants(child, true)) {
      const entt::entity handle = require_entity(entity);
      if (handle != entt::null &&
          registry_.remove<SceneLayerMembershipComponent>(handle) > 0u) {
        layer_membership_removed = true;
      }
    }
  }

  ++visibility_revision_;
  ++transform_revision_;
  if (layer_membership_removed) {
    ++layer_revision_;
    ++render_component_revision_;
  }
  ++hierarchy_presentation_revision_;
  mark_dirty(child, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::set_sibling_ordinal(EntityId entity,
                                        std::size_t ordinal) {
  const EntityId parent_id = parent(entity);
  HierarchyComponent *parent_hierarchy = mutable_hierarchy(parent_id);
  if (entity == kInvalidEntityId || parent_hierarchy == nullptr) {
    return false;
  }

  std::vector<EntityId> &siblings = parent_hierarchy->children;
  const auto current = std::ranges::find(siblings, entity);
  if (current == siblings.end() || ordinal >= siblings.size()) {
    return false;
  }
  const std::size_t current_ordinal =
      static_cast<std::size_t>(std::distance(siblings.begin(), current));
  if (current_ordinal == ordinal) {
    return true;
  }

  const EntityId moved = *current;
  siblings.erase(current);
  siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(ordinal),
                  moved);
  ++hierarchy_presentation_revision_;
  mark_dirty(entity, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::set_parents(
    std::span<const SceneParentAssignment> assignments) {
  if (assignments.empty()) {
    return true;
  }
  std::unordered_map<EntityId, EntityId> replacements;
  replacements.reserve(assignments.size());
  for (const SceneParentAssignment &assignment : assignments) {
    if (assignment.child == kInvalidEntityId ||
        !exists(assignment.child) ||
        (assignment.parent != kInvalidEntityId &&
         !exists(assignment.parent)) ||
        !replacements.emplace(assignment.child, assignment.parent).second) {
      return false;
    }
  }

  const std::vector<EntityId> tree_order = entity_tree_order();
  std::vector<SceneParentAssignment> hierarchy;
  hierarchy.reserve(tree_order.size());
  for (const EntityId entity : tree_order) {
    const auto replacement = replacements.find(entity);
    hierarchy.push_back(SceneParentAssignment{
        .child = entity,
        .parent = replacement == replacements.end()
                      ? parent(entity)
                      : replacement->second,
    });
  }
  return replace_hierarchy(hierarchy);
}

bool SceneDocument::replace_hierarchy(
    std::span<const SceneParentAssignment> assignments) {
  if (assignments.size() != entities_.size()) {
    return false;
  }

  std::unordered_map<EntityId, EntityId> parents;
  parents.reserve(assignments.size());
  std::unordered_map<EntityId, std::vector<EntityId>> children;
  children.reserve(assignments.size());

  for (const SceneParentAssignment &assignment : assignments) {
    if (assignment.child == kInvalidEntityId ||
        require_entity(assignment.child) == entt::null ||
        !parents.emplace(assignment.child, assignment.parent).second ||
        assignment.child == assignment.parent ||
        (assignment.parent != kInvalidEntityId &&
         require_entity(assignment.parent) == entt::null) ||
        (is_scene_root(assignment.child) &&
         assignment.parent != kInvalidEntityId) ||
        instance_definition(assignment.child) != nullptr ||
        instance_occurrence(assignment.child) != nullptr) {
      return false;
    }
    if (assignment.parent != kInvalidEntityId) {
      children[assignment.parent].push_back(assignment.child);
    }
  }
  if (parents.size() != entities_.size()) {
    return false;
  }

  std::unordered_map<EntityId, std::uint8_t> visit_state;
  visit_state.reserve(assignments.size());
  std::vector<EntityId> chain;
  chain.reserve(32u);
  for (const SceneParentAssignment &assignment : assignments) {
    EntityId current = assignment.child;
    chain.clear();
    while (current != kInvalidEntityId) {
      std::uint8_t &state = visit_state[current];
      if (state == 2u) {
        break;
      }
      if (state == 1u) {
        return false;
      }
      state = 1u;
      chain.push_back(current);
      current = parents.at(current);
    }
    for (const EntityId entity : chain) {
      visit_state[entity] = 2u;
    }
  }

  bool changed = false;
  for (const SceneParentAssignment &assignment : assignments) {
    const HierarchyComponent *current = hierarchy(assignment.child);
    const auto child_it = children.find(assignment.child);
    const bool children_changed =
        current == nullptr ||
        (child_it == children.end() ? !current->children.empty()
                                    : current->children != child_it->second);
    if (current == nullptr || current->parent != assignment.parent ||
        children_changed) {
      changed = true;
      break;
    }
  }
  if (!changed) {
    return true;
  }

  std::vector<EntityId> changed_entities;
  changed_entities.reserve(assignments.size());
  for (const SceneParentAssignment &assignment : assignments) {
    HierarchyComponent *component = mutable_hierarchy(assignment.child);
    if (component == nullptr) {
      return false;
    }
    component->parent = assignment.parent;
    const auto child_it = children.find(assignment.child);
    component->children =
        child_it == children.end() ? std::vector<EntityId>{}
                                   : std::move(child_it->second);
    changed_entities.push_back(assignment.child);
  }

  ++visibility_revision_;
  ++transform_revision_;
  ++hierarchy_presentation_revision_;
  mark_dirty(changed_entities, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::set_transform(EntityId id, TransformComponent transform) {
  if (!scene_transform_is_finite(transform)) {
    return false;
  }
  const entt::entity entity = require_entity(id);
  if (entity == entt::null) {
    return false;
  }
  if (const TransformComponent *current =
          registry_.try_get<TransformComponent>(entity);
      current != nullptr && same_transform(*current, transform)) {
    return true;
  }
  registry_.emplace_or_replace<TransformComponent>(entity, transform);
  ++transform_revision_;
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return true;
}

void SceneDocument::set_transforms(
    std::span<const SceneTransformAssignment> transforms) {
  bool changed = false;
  std::vector<EntityId> changed_entities;
  changed_entities.reserve(transforms.size());
  for (const SceneTransformAssignment &assignment : transforms) {
    if (!scene_transform_is_finite(assignment.transform)) {
      continue;
    }
    const entt::entity entity = require_entity(assignment.entity);
    if (entity == entt::null) {
      continue;
    }
    if (const TransformComponent *current =
            registry_.try_get<TransformComponent>(entity);
        current != nullptr && same_transform(*current, assignment.transform)) {
      continue;
    }
    registry_.emplace_or_replace<TransformComponent>(entity,
                                                     assignment.transform);
    changed = true;
    changed_entities.push_back(assignment.entity);
  }
  if (changed) {
    ++transform_revision_;
    mark_dirty(changed_entities,
               SceneModelingProjectionDomain::Structure);
  }
}

bool SceneDocument::set_modeling_object(EntityId id,
                                        ModelingObjectComponent object) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || instance_occurrence(id) != nullptr ||
      is_instance_definition_entity(id)) {
    return false;
  }
  if (const ModelingObjectComponent *current =
          registry_.try_get<ModelingObjectComponent>(entity);
      current != nullptr && same_modeling_object(*current, object)) {
    return true;
  }
  registry_.emplace_or_replace<ModelingObjectComponent>(entity, object);
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::clear_modeling_object(EntityId id) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null ||
      !registry_.all_of<ModelingObjectComponent>(entity)) {
    return false;
  }
  registry_.remove<ModelingObjectComponent>(entity);
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::set_texture_mapping(EntityId id,
                                       TextureMappingComponent mapping) {
  const SceneTextureMappingAssignment assignment{.entity = id,
                                                .mapping = mapping};
  return set_transforms_and_texture_mappings({}, {&assignment, 1u});
}

bool SceneDocument::clear_texture_mapping(EntityId id) {
  const SceneTextureMappingAssignment assignment{.entity = id,
                                                .mapping = std::nullopt};
  return set_transforms_and_texture_mappings({}, {&assignment, 1u});
}

bool SceneDocument::set_transforms_and_texture_mappings(
    std::span<const SceneTransformAssignment> transforms,
    std::span<const SceneTextureMappingAssignment> mappings) {
  // Validate the entire publication before touching the registry or revisions.
  for (const SceneTransformAssignment &assignment : transforms) {
    if (!exists(assignment.entity) ||
        !scene_transform_is_finite(assignment.transform)) {
      return false;
    }
  }
  for (const SceneTextureMappingAssignment &assignment : mappings) {
    if (!exists(assignment.entity) ||
        (assignment.mapping.has_value() &&
         ((kind(assignment.entity) != EntityKind::Mesh &&
           instance_occurrence(assignment.entity) == nullptr) ||
          !texture_mapping_is_valid(*assignment.mapping)))) {
      return false;
    }
  }

  bool transforms_changed = false;
  bool mappings_changed = false;
  std::vector<EntityId> changed_entities;
  changed_entities.reserve(transforms.size() + mappings.size());
  for (const SceneTransformAssignment &assignment : transforms) {
    const entt::entity entity = require_entity(assignment.entity);
    const TransformComponent *current =
        registry_.try_get<TransformComponent>(entity);
    if (current != nullptr && same_transform(*current, assignment.transform)) {
      continue;
    }
    registry_.emplace_or_replace<TransformComponent>(entity,
                                                     assignment.transform);
    transforms_changed = true;
    changed_entities.push_back(assignment.entity);
  }
  for (const SceneTextureMappingAssignment &assignment : mappings) {
    const entt::entity entity = require_entity(assignment.entity);
    const TextureMappingComponent *current =
        registry_.try_get<TextureMappingComponent>(entity);
    if (!assignment.mapping.has_value()) {
      if (current != nullptr) {
        registry_.remove<TextureMappingComponent>(entity);
        mappings_changed = true;
        changed_entities.push_back(assignment.entity);
      }
    } else if (current == nullptr || *current != *assignment.mapping) {
      registry_.emplace_or_replace<TextureMappingComponent>(entity,
                                                            *assignment.mapping);
      mappings_changed = true;
      changed_entities.push_back(assignment.entity);
    }
  }
  if (transforms_changed) {
    ++transform_revision_;
  }
  if (mappings_changed) {
    ++texture_mapping_revision_;
  }
  if (transforms_changed || mappings_changed) {
    mark_dirty(changed_entities,
               SceneModelingProjectionDomain::Structure);
  }
  return true;
}

bool SceneDocument::set_instance_definition(EntityId id) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || is_scene_root(id) ||
      instance_occurrence(id) != nullptr ||
      containing_instance_definition(parent(id)) != kInvalidEntityId ||
      mesh(id) != nullptr || curve(id) != nullptr ||
      modeling_object(id) != nullptr ||
      material(id) != nullptr || light(id) != nullptr ||
      texture_mapping(id) != nullptr) {
    return false;
  }
  for (const EntityId child : children(id)) {
    if (!is_supported_instance_member_subtree(*this, child)) {
      return false;
    }
  }
  if (registry_.all_of<InstanceDefinitionComponent>(entity)) {
    return true;
  }
  registry_.emplace<InstanceDefinitionComponent>(entity);
  bool layer_membership_removed = false;
  for (const EntityId member : descendants(id, true)) {
    const entt::entity handle = require_entity(member);
    if (handle != entt::null &&
        registry_.remove<SceneLayerMembershipComponent>(handle) > 0u) {
      layer_membership_removed = true;
    }
  }
  if (layer_membership_removed) {
    ++layer_revision_;
    ++visibility_revision_;
  }
  ++hierarchy_presentation_revision_;
  ++render_component_revision_;
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::clear_instance_definition(EntityId id) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null ||
      !registry_.all_of<InstanceDefinitionComponent>(entity) ||
      !instance_occurrences_for_definition(id).empty()) {
    return false;
  }
  registry_.remove<InstanceDefinitionComponent>(entity);
  ++hierarchy_presentation_revision_;
  ++render_component_revision_;
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::set_instance_occurrence(
    EntityId id, InstanceOccurrenceComponent occurrence) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || occurrence.definition == kInvalidEntityId ||
      occurrence.definition == id ||
      !is_instance_definition_entity(occurrence.definition) ||
      is_instance_definition_entity(id) || has_children(id) ||
      containing_instance_definition(id) != kInvalidEntityId ||
      mesh(id) != nullptr || curve(id) != nullptr ||
      modeling_object(id) != nullptr ||
      material(id) != nullptr || light(id) != nullptr) {
    return false;
  }
  if (const InstanceOccurrenceComponent *current = instance_occurrence(id);
      current != nullptr && *current == occurrence &&
      kind(id) == EntityKind::Instance) {
    return true;
  }
  registry_.emplace_or_replace<InstanceOccurrenceComponent>(entity,
                                                             occurrence);
  registry_.emplace_or_replace<EntityKindComponent>(
      entity, EntityKindComponent{.kind = EntityKind::Instance});
  // Definition retargeting changes the recursive material-use projection even
  // when the entity was already an instance occurrence.
  ++hierarchy_presentation_revision_;
  ++render_component_revision_;
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::clear_instance_occurrence(EntityId id) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null ||
      !registry_.all_of<InstanceOccurrenceComponent>(entity)) {
    return false;
  }
  registry_.remove<InstanceOccurrenceComponent>(entity);
  if (registry_.remove<TextureMappingComponent>(entity) > 0u) {
    ++texture_mapping_revision_;
  }
  registry_.emplace_or_replace<EntityKindComponent>(
      entity, EntityKindComponent{.kind = EntityKind::Generic});
  ++hierarchy_presentation_revision_;
  ++render_component_revision_;
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::set_mesh(EntityId id, MeshComponent mesh) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || instance_occurrence(id) != nullptr ||
      is_instance_definition_entity(id) || curve(id) != nullptr) {
    return false;
  }
  if (const MeshComponent *current = registry_.try_get<MeshComponent>(entity);
      current != nullptr && *current == mesh &&
      kind(id) == EntityKind::Mesh) {
    return true;
  }
  const MeshComponent *current = registry_.try_get<MeshComponent>(entity);
  replace_binding_reference(
      mesh_binding_reference_counts_,
      current == nullptr ? std::nullopt
                         : std::optional<AssetId>{current->mesh},
      mesh.mesh);
  const bool kind_changed = kind(id) != EntityKind::Mesh;
  registry_.emplace_or_replace<MeshComponent>(entity, mesh);
  registry_.emplace_or_replace<EntityKindComponent>(
      entity, EntityKindComponent{.kind = EntityKind::Mesh});
  if (kind_changed) {
    ++hierarchy_presentation_revision_;
  }
  ++mesh_binding_revision_;
  ++render_component_revision_;
  mark_dirty(id, SceneModelingProjectionDomain::MeshBindings);
  return true;
}

bool SceneDocument::clear_mesh(EntityId id) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || !registry_.all_of<MeshComponent>(entity)) {
    return false;
  }
  decrement_binding_reference(
      mesh_binding_reference_counts_, registry_.get<MeshComponent>(entity).mesh);
  registry_.remove<MeshComponent>(entity);
  const bool kind_changed = kind(id) == EntityKind::Mesh;
  if (kind_changed) {
    registry_.emplace_or_replace<EntityKindComponent>(
        entity, EntityKindComponent{.kind = EntityKind::Generic});
    ++hierarchy_presentation_revision_;
  }
  ++mesh_binding_revision_;
  ++render_component_revision_;
  mark_dirty(id, SceneModelingProjectionDomain::MeshBindings);
  return true;
}

bool SceneDocument::set_curve(EntityId id, CurveComponent value) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || !curve_component_is_valid(value) ||
      is_scene_root(id) || mesh(id) != nullptr || light(id) != nullptr ||
      texture_mapping(id) != nullptr ||
      instance_occurrence(id) != nullptr ||
      containing_instance_definition(id) != kInvalidEntityId) {
    return false;
  }
  if (const CurveComponent *current = curve(id);
      current != nullptr && *current == value && kind(id) == EntityKind::Curve) {
    return true;
  }
  const CurveComponent *current = registry_.try_get<CurveComponent>(entity);
  const bool material_surface_changed =
      current == nullptr ? value.solidify : current->solidify != value.solidify;
  replace_binding_reference(
      curve_binding_reference_counts_,
      current == nullptr ? std::nullopt
                         : std::optional<AssetId>{current->curve},
      value.curve);
  const bool kind_changed = kind(id) != EntityKind::Curve;
  registry_.emplace_or_replace<CurveComponent>(entity, value);
  registry_.emplace_or_replace<EntityKindComponent>(
      entity, EntityKindComponent{.kind = EntityKind::Curve});
  if (kind_changed) {
    ++hierarchy_presentation_revision_;
  }
  ++curve_binding_revision_;
  if (material_surface_changed) {
    ++material_binding_revision_;
  }
  ++render_component_revision_;
  mark_dirty(id, SceneModelingProjectionDomain::CurveBindings);
  return true;
}

bool SceneDocument::clear_curve(EntityId id) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || !registry_.all_of<CurveComponent>(entity)) {
    return false;
  }
  const CurveComponent &current = registry_.get<CurveComponent>(entity);
  const bool material_surface_changed = current.solidify;
  decrement_binding_reference(curve_binding_reference_counts_, current.curve);
  registry_.remove<CurveComponent>(entity);
  registry_.emplace_or_replace<EntityKindComponent>(
      entity, EntityKindComponent{.kind = EntityKind::Generic});
  ++hierarchy_presentation_revision_;
  ++curve_binding_revision_;
  if (material_surface_changed) {
    ++material_binding_revision_;
  }
  ++render_component_revision_;
  mark_dirty(id, SceneModelingProjectionDomain::CurveBindings);
  return true;
}

bool SceneDocument::notify_curve_payload_changed(EntityId id) {
  if (curve(id) == nullptr) {
    return false;
  }
  ++curve_binding_revision_;
  ++render_component_revision_;
  mark_dirty(id, SceneModelingProjectionDomain::CurveBindings);
  return true;
}

bool SceneDocument::notify_mesh_payload_changed(EntityId id) {
  return notify_mesh_payloads_changed(std::span<const EntityId>(&id, 1u));
}

bool SceneDocument::can_notify_mesh_payloads_changed(
    std::span<const EntityId> ids) const {
  for (const EntityId id : ids) {
    const entt::entity entity = require_entity(id);
    if (entity == entt::null ||
        !registry_.all_of<MeshComponent>(entity)) {
      return false;
    }
  }
  return true;
}

bool SceneDocument::notify_mesh_payloads_changed(
    std::span<const EntityId> ids) {
  if (ids.empty()) {
    return true;
  }
  if (!can_notify_mesh_payloads_changed(ids)) {
    return false;
  }
  ++mesh_binding_revision_;
  ++render_component_revision_;
  mark_dirty(ids, SceneModelingProjectionDomain::MeshBindings);
  return true;
}

bool SceneDocument::set_material(EntityId id, MaterialComponent material) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || instance_occurrence(id) != nullptr ||
      is_instance_definition_entity(id)) {
    return false;
  }
  if (const MaterialComponent *current =
          registry_.try_get<MaterialComponent>(entity);
      current != nullptr && *current == material) {
    return true;
  }
  registry_.emplace_or_replace<MaterialComponent>(entity, material);
  ++material_binding_revision_;
  ++render_component_revision_;
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::clear_material(EntityId id) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || !registry_.all_of<MaterialComponent>(entity)) {
    return false;
  }
  registry_.remove<MaterialComponent>(entity);
  ++material_binding_revision_;
  ++render_component_revision_;
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::set_light(EntityId id, LightComponent light) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || instance_occurrence(id) != nullptr ||
      is_instance_definition_entity(id) ||
      is_instance_definition_member(id) || curve(id) != nullptr) {
    return false;
  }
  const LightComponent *current = registry_.try_get<LightComponent>(entity);
  if (current != nullptr && *current == light &&
      kind(id) == EntityKind::Light) {
    return true;
  }
  const bool kind_changed = kind(id) != EntityKind::Light;
  const bool active_kind_changed =
      current == nullptr && active_selection_ == id && is_selected(id);
  registry_.emplace_or_replace<LightComponent>(entity, light);
  registry_.emplace_or_replace<EntityKindComponent>(
      entity, EntityKindComponent{.kind = EntityKind::Light});
  if (kind_changed) {
    ++hierarchy_presentation_revision_;
  }
  ++render_component_revision_;
  if (active_kind_changed) {
    ++selection_revision_;
  }
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::clear_light(EntityId id) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || !registry_.all_of<LightComponent>(entity)) {
    return false;
  }
  registry_.remove<LightComponent>(entity);
  const bool kind_changed = kind(id) == EntityKind::Light;
  if (kind_changed) {
    registry_.emplace_or_replace<EntityKindComponent>(
        entity, EntityKindComponent{.kind = EntityKind::Generic});
    ++hierarchy_presentation_revision_;
  }
  ++render_component_revision_;
  if (kind_changed && active_selection_ == id && is_selected(id)) {
    ++selection_revision_;
  }
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::set_scene_settings(EntityId id,
                                       SceneSettingsComponent settings) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || curve(id) != nullptr) {
    return false;
  }
  const EntityId root = scene_root();
  if (root != kInvalidEntityId && root != id) {
    return false;
  }
  if (const SceneSettingsComponent *current = scene_settings(id);
      current != nullptr && *current == settings) {
    return true;
  }
  const bool hierarchy_presentation_changed =
      scene_settings(id) == nullptr || kind(id) != EntityKind::Empty;
  registry_.emplace_or_replace<SceneSettingsComponent>(entity,
                                                       std::move(settings));
  registry_.emplace_or_replace<EntityKindComponent>(
      entity, EntityKindComponent{.kind = EntityKind::Empty});
  if (hierarchy_presentation_changed) {
    ++hierarchy_presentation_revision_;
  }
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return true;
}

bool SceneDocument::clear_scene_settings(EntityId id) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null ||
      !registry_.all_of<SceneSettingsComponent>(entity)) {
    return false;
  }
  registry_.remove<SceneSettingsComponent>(entity);
  if (kind(id) == EntityKind::Empty) {
    registry_.emplace_or_replace<EntityKindComponent>(
        entity, EntityKindComponent{.kind = EntityKind::Generic});
  }
  ++hierarchy_presentation_revision_;
  mark_dirty(id, SceneModelingProjectionDomain::Structure);
  return true;
}

SceneLayerId SceneDocument::create_layer(std::string name,
                                         QVec3 wireframe_color) {
  if (name.empty()) {
    name = next_layer_name();
  }
  return create_layer_with_id(next_layer_id_, std::move(name), true,
                              wireframe_color);
}

SceneLayerId SceneDocument::create_layer_with_id(SceneLayerId id,
                                                 std::string name,
                                                 bool visible,
                                                 QVec3 wireframe_color,
                                                 bool locked) {
  if (id == kInvalidSceneLayerId ||
      id == std::numeric_limits<SceneLayerId>::max() ||
      !valid_layer_name(name) ||
      layers_.contains(id) || !valid_layer_color(wireframe_color)) {
    return kInvalidSceneLayerId;
  }
  SceneLayerState state = layer_state();
  state.layers.push_back(SceneLayerRecord{
      .id = id,
      .name = std::move(name),
      .visible = visible,
      .wireframe_color = wireframe_color,
      .locked = locked,
  });
  return replace_layer_state(std::move(state)) ? id : kInvalidSceneLayerId;
}

bool SceneDocument::rename_layer(SceneLayerId id, std::string name) {
  if (!valid_layer_name(name) || layer(id) == nullptr) {
    return false;
  }
  SceneLayerState state = layer_state();
  for (SceneLayerRecord &record : state.layers) {
    if (record.id == id) {
      record.name = std::move(name);
      return replace_layer_state(std::move(state));
    }
  }
  return false;
}

bool SceneDocument::set_layer_visible(SceneLayerId id, bool visible) {
  if (layer(id) == nullptr) {
    return false;
  }
  SceneLayerState state = layer_state();
  for (SceneLayerRecord &record : state.layers) {
    if (record.id == id) {
      record.visible = visible;
      return replace_layer_state(std::move(state));
    }
  }
  return false;
}

bool SceneDocument::set_layer_wireframe_color(SceneLayerId id, QVec3 color) {
  if (layer(id) == nullptr || !valid_layer_color(color)) {
    return false;
  }
  SceneLayerState state = layer_state();
  for (SceneLayerRecord &record : state.layers) {
    if (record.id == id) {
      record.wireframe_color = color;
      return replace_layer_state(std::move(state));
    }
  }
  return false;
}

bool SceneDocument::set_layer_locked(SceneLayerId id, bool locked) {
  if (layer(id) == nullptr) {
    return false;
  }
  SceneLayerState state = layer_state();
  for (SceneLayerRecord &record : state.layers) {
    if (record.id == id) {
      record.locked = locked;
      return replace_layer_state(std::move(state));
    }
  }
  return false;
}

bool SceneDocument::empty_layer(SceneLayerId id) {
  if (layer(id) == nullptr) {
    return false;
  }
  SceneLayerState state = layer_state();
  std::erase_if(state.assignments, [id](const SceneLayerAssignment &entry) {
    return entry.layer_id == id;
  });
  return replace_layer_state(std::move(state));
}

bool SceneDocument::delete_layer(SceneLayerId id) {
  return delete_layers(std::span<const SceneLayerId>(&id, 1u));
}

bool SceneDocument::delete_layers(std::span<const SceneLayerId> ids) {
  std::unordered_set<SceneLayerId> removed;
  for (const SceneLayerId id : ids) {
    if (id == kInvalidSceneLayerId || layer(id) == nullptr) {
      return false;
    }
    removed.insert(id);
  }
  if (removed.empty()) {
    return true;
  }
  SceneLayerState state = layer_state();
  std::erase_if(state.layers, [&removed](const SceneLayerRecord &record) {
    return removed.contains(record.id);
  });
  std::erase_if(state.assignments,
                [&removed](const SceneLayerAssignment &entry) {
                  return removed.contains(entry.layer_id);
                });
  return replace_layer_state(std::move(state));
}

bool SceneDocument::set_entity_layer(EntityId entity,
                                     SceneLayerId layer_id) {
  if (!exists(entity) ||
      (layer_id != kInvalidSceneLayerId &&
       (!is_layer_eligible(entity) || layer(layer_id) == nullptr))) {
    return false;
  }
  if (entity_layer(entity) == layer_id) {
    return true;
  }
  SceneLayerState state = layer_state();
  std::erase_if(state.assignments,
                [entity](const SceneLayerAssignment &entry) {
                  return entry.entity == entity;
                });
  if (layer_id != kInvalidSceneLayerId) {
    state.assignments.push_back(
        SceneLayerAssignment{.entity = entity, .layer_id = layer_id});
  }
  return replace_layer_state(std::move(state));
}

bool SceneDocument::replace_layer_state(SceneLayerState state) {
  normalize_layer_state(state);

  std::unordered_set<SceneLayerId> layer_ids;
  std::unordered_set<std::string> layer_names;
  layer_ids.reserve(state.layers.size());
  layer_names.reserve(state.layers.size());
  for (const SceneLayerRecord &record : state.layers) {
    if (record.id == kInvalidSceneLayerId ||
        record.id == std::numeric_limits<SceneLayerId>::max() ||
        !valid_layer_name(record.name) ||
        !valid_layer_color(record.wireframe_color) ||
        !layer_ids.insert(record.id).second ||
        !layer_names.insert(record.name).second) {
      return false;
    }
  }

  std::unordered_set<EntityId> assigned_entities;
  assigned_entities.reserve(state.assignments.size());
  const std::vector<EntityId> scene_entities = entity_tree_order();
  std::unordered_map<EntityId, bool> inside_instance_definition;
  inside_instance_definition.reserve(scene_entities.size());
  for (const EntityId entity : scene_entities) {
    const EntityId parent_id = parent(entity);
    inside_instance_definition[entity] =
        parent_id != kInvalidEntityId &&
        (is_instance_definition_entity(parent_id) ||
         inside_instance_definition[parent_id]);
  }
  for (const SceneLayerAssignment &assignment : state.assignments) {
    if (!exists(assignment.entity) || is_scene_root(assignment.entity) ||
        is_instance_definition_entity(assignment.entity) ||
        inside_instance_definition[assignment.entity] ||
        !layer_ids.contains(assignment.layer_id) ||
        !assigned_entities.insert(assignment.entity).second) {
      return false;
    }
  }

  const SceneLayerState before = layer_state();
  if (before == state) {
    return true;
  }
  std::vector<SceneLayerId> allocated_layers;
  allocated_layers.reserve(state.layers.size());
  for (const SceneLayerRecord &record : state.layers) {
    if (!layers_.contains(record.id)) {
      allocated_layers.push_back(record.id);
    }
  }

  constexpr std::uint8_t kEffectivelyVisible = 1u << 0u;
  constexpr std::uint8_t kEffectivelyLocked = 1u << 1u;
  const auto effective_hierarchy_presentation_state =
      [&](std::span<const EntityId> tree_order) {
        std::vector<std::uint8_t> result;
        result.reserve(tree_order.size());
        std::unordered_map<EntityId, std::uint8_t> state_by_entity;
        state_by_entity.reserve(tree_order.size());
        for (const EntityId entity : tree_order) {
          const EntityId parent_id = parent(entity);
          bool visible =
              parent_id == kInvalidEntityId ||
              (state_by_entity[parent_id] & kEffectivelyVisible) != 0u;
          bool locked =
              parent_id != kInvalidEntityId &&
              (state_by_entity[parent_id] & kEffectivelyLocked) != 0u;
          if (const VisibilityComponent *component = visibility(entity);
              component != nullptr && !component->visible) {
            visible = false;
          }
          if (const LockComponent *component = lock(entity);
              component != nullptr && component->locked) {
            locked = true;
          }
          const SceneLayerId layer_id = entity_layer(entity);
          if (layer_id != kInvalidSceneLayerId) {
            const SceneLayerRecord *record = layer(layer_id);
            if (record == nullptr || !record->visible) {
              visible = false;
            }
            if (record == nullptr || record->locked) {
              locked = true;
            }
          }
          const std::uint8_t presentation_state =
              (visible ? kEffectivelyVisible : 0u) |
              (locked ? kEffectivelyLocked : 0u);
          state_by_entity.emplace(entity, presentation_state);
          result.push_back(presentation_state);
        }
        return result;
      };
  const std::vector<std::uint8_t> presentation_before =
      effective_hierarchy_presentation_state(scene_entities);

  layers_.clear();
  for (SceneLayerRecord &record : state.layers) {
    next_layer_id_ = std::max(next_layer_id_, record.id + 1u);
    layers_.emplace(record.id, std::move(record));
  }
  for (const SceneLayerId layer : allocated_layers) {
    const auto previous_allocation = last_layer_allocation_serial_.find(layer);
    const std::uint64_t previous_allocation_serial =
        previous_allocation == last_layer_allocation_serial_.end()
            ? 0u
            : previous_allocation->second;
    const std::uint64_t allocation_serial = ++layer_allocation_serial_;
    last_layer_allocation_serial_[layer] = allocation_serial;
    note_scalar_compensation_layer_allocation(
        layer, allocation_serial, previous_allocation_serial);
  }

  std::unordered_map<EntityId, SceneLayerId> assignment_by_entity;
  assignment_by_entity.reserve(state.assignments.size());
  for (const SceneLayerAssignment &assignment : state.assignments) {
    assignment_by_entity.emplace(assignment.entity, assignment.layer_id);
  }
  for (const EntityId entity : scene_entities) {
    const entt::entity handle = require_entity(entity);
    const auto assigned = assignment_by_entity.find(entity);
    if (assigned == assignment_by_entity.end()) {
      registry_.remove<SceneLayerMembershipComponent>(handle);
      continue;
    }
    registry_.emplace_or_replace<SceneLayerMembershipComponent>(
        handle, SceneLayerMembershipComponent{.layer_id = assigned->second});
  }

  const std::vector<std::uint8_t> presentation_after =
      effective_hierarchy_presentation_state(scene_entities);
  bool visibility_changed = false;
  for (std::size_t index = 0u; index < presentation_before.size(); ++index) {
    if ((presentation_before[index] & kEffectivelyVisible) !=
        (presentation_after[index] & kEffectivelyVisible)) {
      visibility_changed = true;
      break;
    }
  }
  if (visibility_changed) {
    ++visibility_revision_;
  }
  if (presentation_before != presentation_after) {
    ++hierarchy_presentation_revision_;
  }
  ++layer_revision_;
  ++render_component_revision_;
  mark_dirty(scene_entities, SceneModelingProjectionDomain::Structure);
  return true;
}

const SceneLayerRecord *SceneDocument::layer(SceneLayerId id) const {
  const auto found = layers_.find(id);
  return found == layers_.end() ? nullptr : &found->second;
}

std::vector<SceneLayerRecord> SceneDocument::layers() const {
  std::vector<SceneLayerRecord> records;
  records.reserve(layers_.size());
  for (const auto &[id, record] : layers_) {
    (void)id;
    records.push_back(record);
  }
  std::sort(records.begin(), records.end(),
            [](const SceneLayerRecord &left, const SceneLayerRecord &right) {
              return left.id < right.id;
            });
  return records;
}

std::vector<EntityId> SceneDocument::layer_members(SceneLayerId id) const {
  std::vector<EntityId> members;
  if (layer(id) == nullptr) {
    return members;
  }
  for (const EntityId entity : entities()) {
    if (entity_layer(entity) == id) {
      members.push_back(entity);
    }
  }
  return members;
}

SceneLayerId SceneDocument::entity_layer(EntityId entity) const {
  const SceneLayerMembershipComponent *membership = layer_membership(entity);
  return membership == nullptr ? kInvalidSceneLayerId : membership->layer_id;
}

SceneLayerState SceneDocument::layer_state() const {
  SceneLayerState state;
  state.layers = layers();
  for (const EntityId entity : entities()) {
    const SceneLayerId layer_id = entity_layer(entity);
    if (layer_id != kInvalidSceneLayerId) {
      state.assignments.push_back(
          SceneLayerAssignment{.entity = entity, .layer_id = layer_id});
    }
  }
  return state;
}

std::string SceneDocument::next_layer_name() const {
  std::unordered_set<std::string> names;
  names.reserve(layers_.size());
  for (const auto &[id, record] : layers_) {
    (void)id;
    names.insert(record.name);
  }
  if (!names.contains("Layer")) {
    return "Layer";
  }
  for (std::uint64_t suffix = 1u;; ++suffix) {
    std::string candidate = "Layer_" + std::to_string(suffix);
    if (!names.contains(candidate)) {
      return candidate;
    }
  }
}

bool SceneDocument::is_layer_eligible(EntityId entity) const {
  return entity != kInvalidEntityId && exists(entity) &&
         !is_scene_root(entity) && !is_instance_definition_entity(entity) &&
         !is_instance_definition_member(entity);
}

std::vector<EntityId> SceneDocument::layer_eligible_entities(
    std::span<const EntityId> roots) const {
  std::unordered_set<EntityId> requested_roots;
  requested_roots.reserve(roots.size());
  for (const EntityId root : roots) {
    if (exists(root)) {
      requested_roots.insert(root);
    }
  }

  std::vector<EntityId> eligible;
  std::unordered_map<EntityId, bool> inside_requested_subtree;
  std::unordered_map<EntityId, bool> inside_instance_definition;
  const std::vector<EntityId> tree_order = entity_tree_order();
  eligible.reserve(tree_order.size());
  inside_requested_subtree.reserve(tree_order.size());
  inside_instance_definition.reserve(tree_order.size());
  for (const EntityId entity : tree_order) {
    const EntityId parent_id = parent(entity);
    const bool requested =
        requested_roots.contains(entity) ||
        (parent_id != kInvalidEntityId &&
         inside_requested_subtree[parent_id]);
    inside_requested_subtree.emplace(entity, requested);
    const bool definition_member =
        parent_id != kInvalidEntityId &&
        (is_instance_definition_entity(parent_id) ||
         inside_instance_definition[parent_id]);
    inside_instance_definition.emplace(entity, definition_member);
    if (requested && !is_scene_root(entity) &&
        !is_instance_definition_entity(entity) && !definition_member) {
      eligible.push_back(entity);
    }
  }
  return eligible;
}

NameComponent *SceneDocument::mutable_name(EntityId id) {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<NameComponent>(entity);
}

TransformComponent *SceneDocument::mutable_transform(EntityId id) {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<TransformComponent>(entity);
}

EntityKindComponent *SceneDocument::mutable_entity_kind(EntityId id) {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<EntityKindComponent>(entity);
}

HierarchyComponent *SceneDocument::mutable_hierarchy(EntityId id) {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<HierarchyComponent>(entity);
}

VisibilityComponent *SceneDocument::mutable_visibility(EntityId id) {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<VisibilityComponent>(entity);
}

LockComponent *SceneDocument::mutable_lock(EntityId id) {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<LockComponent>(entity);
}

SelectionComponent *SceneDocument::mutable_selection(EntityId id) {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<SelectionComponent>(entity);
}

const NameComponent* SceneDocument::name(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<NameComponent>(entity);
}

const EntityUuidComponent *SceneDocument::entity_uuid(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<EntityUuidComponent>(entity);
}

const EditorObjectIdentityComponent *
SceneDocument::editor_object_identity(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null
             ? nullptr
             : registry_.try_get<EditorObjectIdentityComponent>(entity);
}

const TransformComponent* SceneDocument::transform(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<TransformComponent>(entity);
}

const EntityKindComponent *SceneDocument::entity_kind(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<EntityKindComponent>(entity);
}

const HierarchyComponent* SceneDocument::hierarchy(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<HierarchyComponent>(entity);
}

const VisibilityComponent* SceneDocument::visibility(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<VisibilityComponent>(entity);
}

const LockComponent* SceneDocument::lock(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<LockComponent>(entity);
}

const SelectionComponent* SceneDocument::selection(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<SelectionComponent>(entity);
}

const ModelingObjectComponent *
SceneDocument::modeling_object(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null
             ? nullptr
             : registry_.try_get<ModelingObjectComponent>(entity);
}

const InstanceDefinitionComponent *
SceneDocument::instance_definition(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null
             ? nullptr
             : registry_.try_get<InstanceDefinitionComponent>(entity);
}

const InstanceOccurrenceComponent *
SceneDocument::instance_occurrence(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null
             ? nullptr
             : registry_.try_get<InstanceOccurrenceComponent>(entity);
}

const MeshComponent* SceneDocument::mesh(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<MeshComponent>(entity);
}

const MaterialComponent* SceneDocument::material(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<MaterialComponent>(entity);
}

const TextureMappingComponent *
SceneDocument::texture_mapping(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null
             ? nullptr
             : registry_.try_get<TextureMappingComponent>(entity);
}

const CurveComponent *SceneDocument::curve(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                            : registry_.try_get<CurveComponent>(entity);
}

std::size_t SceneDocument::mesh_binding_reference_count(
    AssetId mesh, SceneBindingReferenceCountWork *work) const {
  if (work != nullptr) {
    *work = SceneBindingReferenceCountWork{.owner_index_lookup_count = 1u};
  }
  const auto found = mesh_binding_reference_counts_.find(mesh);
  return found == mesh_binding_reference_counts_.end() ? 0u : found->second;
}

std::size_t SceneDocument::curve_binding_reference_count(
    AssetId curve, SceneBindingReferenceCountWork *work) const {
  if (work != nullptr) {
    *work = SceneBindingReferenceCountWork{.owner_index_lookup_count = 1u};
  }
  const auto found = curve_binding_reference_counts_.find(curve);
  return found == curve_binding_reference_counts_.end() ? 0u : found->second;
}

const LightComponent* SceneDocument::light(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<LightComponent>(entity);
}

const SceneSettingsComponent *SceneDocument::scene_settings(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null
             ? nullptr
             : registry_.try_get<SceneSettingsComponent>(entity);
}

const SceneLayerMembershipComponent *
SceneDocument::layer_membership(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null
             ? nullptr
             : registry_.try_get<SceneLayerMembershipComponent>(entity);
}

std::vector<EntityId> SceneDocument::entities() const {
    std::vector<EntityId> ids;
    ids.reserve(entities_.size());
    for (const auto& [id, entity] : entities_) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<EntityId> SceneDocument::curve_entities() const {
  std::vector<EntityId> ids;
  const auto curves =
      registry_.view<CurveComponent, EntityIdentityComponent>();
  ids.reserve(curves.size_hint());
  for (auto [entity, curve, identity] : curves.each()) {
    (void)entity;
    (void)curve;
    ids.push_back(identity.id);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

EntityId SceneDocument::scene_root() const {
  EntityId root = kInvalidEntityId;
  for (auto [entity, settings, identity] :
       registry_.view<SceneSettingsComponent, EntityIdentityComponent>()
           .each()) {
    (void)settings;
    if (root == kInvalidEntityId || identity.id < root) {
      root = identity.id;
    }
  }
  return exists(root) ? root : kInvalidEntityId;
}

bool SceneDocument::is_scene_root(EntityId id) const {
  return id != kInvalidEntityId && scene_settings(id) != nullptr;
}

std::vector<EntityId> SceneDocument::root_entities() const {
  std::vector<EntityId> roots;
  const std::vector<EntityId> ids = entities();
  roots.reserve(ids.size());
  const EntityId root = scene_root();
  if (root != kInvalidEntityId && parent(root) == kInvalidEntityId) {
    roots.push_back(root);
  }
  for (const EntityId id : ids) {
    if (id != root && parent(id) == kInvalidEntityId) {
      roots.push_back(id);
    }
  }
  return roots;
}

std::vector<EntityId> SceneDocument::children(EntityId id) const {
    if (const HierarchyComponent* component = hierarchy(id)) {
        return component->children;
    }
    return {};
}

std::optional<std::size_t>
SceneDocument::sibling_ordinal(EntityId entity) const {
  const EntityId parent_id = parent(entity);
  const HierarchyComponent *parent_hierarchy = hierarchy(parent_id);
  if (entity == kInvalidEntityId || parent_hierarchy == nullptr) {
    return std::nullopt;
  }
  const auto sibling =
      std::ranges::find(parent_hierarchy->children, entity);
  if (sibling == parent_hierarchy->children.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(
      std::distance(parent_hierarchy->children.begin(), sibling));
}

std::vector<EntityId> SceneDocument::descendants(EntityId id,
                                                 bool include_self) const {
  if (require_entity(id) == entt::null) {
    return {};
  }

  std::vector<EntityId> result;
  result.reserve(16u);
  if (include_self) {
    result.push_back(id);
  }

  std::vector<EntityId> pending;
  if (const HierarchyComponent *root_hierarchy = hierarchy(id)) {
    pending.reserve(root_hierarchy->children.size());
    for (auto child = root_hierarchy->children.rbegin();
         child != root_hierarchy->children.rend(); ++child) {
      pending.push_back(*child);
    }
  }

  while (!pending.empty()) {
    const EntityId entity = pending.back();
    pending.pop_back();
    result.push_back(entity);
    if (const HierarchyComponent *component = hierarchy(entity)) {
      for (auto child = component->children.rbegin();
           child != component->children.rend(); ++child) {
        pending.push_back(*child);
      }
    }
  }
  return result;
}

std::vector<EntityId> SceneDocument::entity_tree_order() const {
  std::vector<EntityId> ids;
  ids.reserve(entities_.size());
  const std::vector<EntityId> roots = root_entities();
  std::vector<EntityId> pending;
  pending.reserve(entities_.size());
  for (auto root = roots.rbegin(); root != roots.rend(); ++root) {
    pending.push_back(*root);
  }
  while (!pending.empty()) {
    const EntityId entity = pending.back();
    pending.pop_back();
    ids.push_back(entity);
    if (const HierarchyComponent *component = hierarchy(entity)) {
      for (auto child = component->children.rbegin();
           child != component->children.rend(); ++child) {
        pending.push_back(*child);
      }
    }
  }
  return ids;
}

std::shared_ptr<const SceneHierarchyProjectionSnapshot>
SceneDocument::hierarchy_projection() const {
  const HierarchyProjectionCacheKey key{
      .restore_epoch = spatial_projection_restore_epoch_,
      .hierarchy_presentation_revision = hierarchy_presentation_revision_,
      .transform_revision = transform_revision_,
  };
  if (hierarchy_projection_cache_ != nullptr &&
      hierarchy_projection_cache_key_ == key) {
    ++spatial_projection_cache_stats_.hierarchy_cache_hit_count;
    return hierarchy_projection_cache_;
  }

  SceneHierarchyProjectionWorkStats work;
  auto snapshot = std::make_shared<SceneHierarchyProjectionSnapshot>();
  const std::vector<EntityId> tree_order = entity_tree_order();
  snapshot->records_.reserve(tree_order.size());
  snapshot->index_by_entity_.reserve(tree_order.size());

  for (const EntityId entity : tree_order) {
    ++work.entity_visit_count;
    SceneHierarchyProjectionRecord record;
    record.entity = entity;
    record.parent = parent(entity);
    record.world_transform = local_transform_matrix(entity);

    const SceneHierarchyProjectionRecord *parent_state = nullptr;
    if (record.parent != kInvalidEntityId) {
      ++work.parent_state_lookup_count;
      parent_state = snapshot->find(record.parent);
    }
    if (parent_state != nullptr) {
      record.depth = parent_state->depth + 1u;
      record.world_transform = scene_transform_multiply(
          parent_state->world_transform, record.world_transform);
      ++work.world_transform_multiply_count;
      record.effectively_visible = parent_state->effectively_visible;
      record.effectively_locked = parent_state->effectively_locked;
      record.containing_instance_definition =
          parent_state->containing_instance_definition;
    }

    if (const VisibilityComponent *component = visibility(entity);
        component != nullptr && !component->visible) {
      record.effectively_visible = false;
    }
    if (const LockComponent *component = lock(entity);
        component != nullptr && component->locked) {
      record.effectively_locked = true;
    }
    const SceneLayerId layer_id = entity_layer(entity);
    if (layer_id != kInvalidSceneLayerId) {
      const SceneLayerRecord *layer_record = layer(layer_id);
      if (layer_record == nullptr || !layer_record->visible) {
        record.effectively_visible = false;
      }
      if (layer_record == nullptr || layer_record->locked) {
        record.effectively_locked = true;
      }
    }
    if (is_instance_definition_entity(entity)) {
      record.containing_instance_definition = entity;
    }

    snapshot->index_by_entity_.emplace(entity, snapshot->records_.size());
    snapshot->records_.push_back(std::move(record));
  }

  hierarchy_projection_cache_key_ = key;
  hierarchy_projection_cached_work_ = work;
  hierarchy_projection_cache_ = std::move(snapshot);
  ++spatial_projection_cache_stats_.hierarchy_build_count;
  return hierarchy_projection_cache_;
}

SceneHierarchyProjectionSnapshot
SceneDocument::hierarchy_projection_snapshot(
    SceneHierarchyProjectionWorkStats *work_stats) const {
  const std::shared_ptr<const SceneHierarchyProjectionSnapshot> projection =
      hierarchy_projection();
  if (work_stats != nullptr) {
    *work_stats = hierarchy_projection_cached_work_;
  }
  return projection != nullptr ? *projection
                               : SceneHierarchyProjectionSnapshot{};
}

std::vector<EntityRecord> SceneDocument::entity_records() const {
  std::vector<EntityRecord> records;
  const std::vector<EntityId> ids = entities();
  const std::shared_ptr<const SceneHierarchyProjectionSnapshot> projection =
      hierarchy_projection();
  const EntityId active = primary_selection();
  records.reserve(ids.size());

  for (const EntityId id : ids) {
    records.push_back(
        make_entity_record(id, 0, active, projection->find(id)));
  }

  return records;
}

std::vector<EntityRecord> SceneDocument::entity_records_tree_order() const {
  std::vector<EntityRecord> records;
  records.reserve(entities_.size());
  const std::shared_ptr<const SceneHierarchyProjectionSnapshot> projection =
      hierarchy_projection();
  const EntityId active = primary_selection();
  for (const SceneHierarchyProjectionRecord &state : projection->records()) {
    if (state.containing_instance_definition != kInvalidEntityId) {
      continue;
    }
    records.push_back(make_entity_record(
        state.entity, static_cast<int>(state.depth), active, &state));
  }
  return records;
}

std::vector<EntityRecord>
SceneDocument::entity_records_for_instance_definition(
    EntityId definition) const {
  std::vector<EntityRecord> records;
  if (!is_instance_definition_entity(definition)) {
    return records;
  }

  const EntityId active = primary_selection();
  const std::shared_ptr<const SceneHierarchyProjectionSnapshot> projection =
      hierarchy_projection();
  const std::vector<EntityId> members = descendants(definition, false);
  records.reserve(members.size());
  std::function<void(EntityId, int, bool, bool)> append_member =
      [&](EntityId member, int depth, bool parent_visible,
          bool parent_locked) {
        EntityRecord record = make_entity_record(
            member, depth, active, projection->find(member));
        if (depth == 0) {
          record.parent = kInvalidEntityId;
        }
        record.effectively_visible = parent_visible && record.visible;
        record.effectively_locked = parent_locked || record.locked;
        records.push_back(record);
        for (const EntityId child : children(member)) {
          append_member(child, depth + 1, record.effectively_visible,
                        record.effectively_locked);
        }
      };
  for (const EntityId member : children(definition)) {
    append_member(member, 0, true, false);
  }
  return records;
}

std::vector<EntityId> SceneDocument::selected_entities() const {
  std::vector<EntityId> ids;
  for (auto [entity, selection, identity] :
       registry_.view<SelectionComponent, EntityIdentityComponent>().each()) {
    if (selection.selected) {
      ids.push_back(identity.id);
    }
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

EntityId SceneDocument::primary_selection() const {
  if (is_selected(active_selection_)) {
    return active_selection_;
  }
  return first_selected_entity();
}

EntityId SceneDocument::parent(EntityId id) const {
    if (const HierarchyComponent* component = hierarchy(id)) {
        return component->parent;
    }
    return kInvalidEntityId;
}

std::string SceneDocument::uuid(EntityId id) const {
  const EntityUuidComponent *identity = entity_uuid(id);
  return identity == nullptr ? std::string{} : identity->uuid;
}

EntityId SceneDocument::entity_for_uuid(std::string_view uuid) const {
  const auto existing = entity_by_uuid_.find(std::string(uuid));
  if (existing == entity_by_uuid_.end() || !exists(existing->second)) {
    return kInvalidEntityId;
  }
  return existing->second;
}

EditorObjectId SceneDocument::editor_object_id(EntityId id) const {
  const EditorObjectIdentityComponent *identity = editor_object_identity(id);
  return identity == nullptr ? kInvalidEditorObjectId : identity->id;
}

EntityId SceneDocument::entity_for_editor_object_id(
    EditorObjectId editor_object_id) const {
  const auto existing = entity_by_editor_object_id_.find(editor_object_id);
  if (existing == entity_by_editor_object_id_.end() ||
      !exists(existing->second)) {
    return kInvalidEntityId;
  }
  return existing->second;
}

bool SceneDocument::has_children(EntityId id) const {
  if (const HierarchyComponent *component = hierarchy(id)) {
    return !component->children.empty();
  }
  return false;
}

bool SceneDocument::is_descendant_of(EntityId entity,
                                     EntityId possible_ancestor) const {
  if (entity == kInvalidEntityId || possible_ancestor == kInvalidEntityId) {
    return false;
  }
  EntityId current = parent(entity);
  std::size_t guard = entities_.size() + 1u;
  while (current != kInvalidEntityId && guard-- > 0u) {
    if (current == possible_ancestor) {
      return true;
    }
    current = parent(current);
  }
  return false;
}

bool SceneDocument::is_instance_definition_entity(EntityId entity) const {
  return instance_definition(entity) != nullptr;
}

EntityId SceneDocument::containing_instance_definition(EntityId entity) const {
  if (entity == kInvalidEntityId || !exists(entity)) {
    return kInvalidEntityId;
  }
  EntityId current = entity;
  std::size_t guard = entities_.size() + 1u;
  while (current != kInvalidEntityId && guard-- > 0u) {
    if (is_instance_definition_entity(current)) {
      return current;
    }
    current = parent(current);
  }
  return kInvalidEntityId;
}

bool SceneDocument::is_instance_definition_member(EntityId entity) const {
  const EntityId definition = containing_instance_definition(entity);
  return definition != kInvalidEntityId && definition != entity;
}

std::vector<EntityId> SceneDocument::instance_occurrences_for_definition(
    EntityId definition) const {
  std::vector<EntityId> result;
  if (!is_instance_definition_entity(definition)) {
    return result;
  }
  for (const EntityId entity : entities()) {
    const InstanceOccurrenceComponent *occurrence =
        instance_occurrence(entity);
    if (occurrence != nullptr && occurrence->definition == definition) {
      result.push_back(entity);
    }
  }
  return result;
}

EntityKind SceneDocument::kind(EntityId id) const {
  if (const EntityKindComponent *component = entity_kind(id)) {
    return component->kind;
  }
    return EntityKind::Generic;
}

bool SceneDocument::is_effectively_visible(EntityId id) const {
  if (require_entity(id) == entt::null) {
    return false;
  }
  EntityId current = id;
  std::size_t guard = entities_.size() + 1u;
  while (current != kInvalidEntityId && guard-- > 0u) {
    if (const VisibilityComponent *component = visibility(current)) {
      if (!component->visible) {
        return false;
      }
    }
    const SceneLayerId layer_id = entity_layer(current);
    if (layer_id != kInvalidSceneLayerId) {
      const SceneLayerRecord *record = layer(layer_id);
      if (record == nullptr || !record->visible) {
        return false;
      }
    }
    current = parent(current);
  }
  return true;
}

bool SceneDocument::is_effectively_locked(EntityId id) const {
  if (require_entity(id) == entt::null) {
    return false;
  }
  EntityId current = id;
  std::size_t guard = entities_.size() + 1u;
  while (current != kInvalidEntityId && guard-- > 0u) {
    if (const LockComponent *component = lock(current)) {
      if (component->locked) {
        return true;
      }
    }
    const SceneLayerId layer_id = entity_layer(current);
    if (layer_id != kInvalidSceneLayerId) {
      const SceneLayerRecord *record = layer(layer_id);
      if (record == nullptr || record->locked) {
        return true;
      }
    }
    current = parent(current);
  }
  return false;
}

QMat4 SceneDocument::local_transform_matrix(EntityId id) const {
  if (const TransformComponent *component = transform(id)) {
    return compose_transform(component->spatial_transform());
  }
  return scene_transform_identity_matrix();
}

QMat4 SceneDocument::world_transform_matrix(EntityId id) const {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null) {
    return scene_transform_identity_matrix();
  }

  const TransformComponent *local_transform =
      registry_.try_get<TransformComponent>(entity);
  const QMat4 local_matrix =
      local_transform != nullptr
          ? compose_transform(local_transform->spatial_transform())
          : scene_transform_identity_matrix();
  const HierarchyComponent *hierarchy =
      registry_.try_get<HierarchyComponent>(entity);
  const EntityId parent_id =
      hierarchy != nullptr ? hierarchy->parent : kInvalidEntityId;
  if (parent_id == kInvalidEntityId) {
    return local_matrix;
  }
  return scene_transform_multiply(world_transform_matrix(parent_id),
                                  local_matrix);
}

void SceneDocument::advance_revision_for_replacement(
    std::uint64_t previous_revision) {
  note_unscoped_modeling_projection_mutation();
  revision_ = std::max(revision_ + 1u, previous_revision + 1u);
  content_revision_ =
      std::max(content_revision_ + 1u, previous_revision + 1u);
  selection_revision_ = std::max(selection_revision_ + 1u, revision_);
  visibility_revision_ = std::max(visibility_revision_ + 1u, revision_);
  transform_revision_ = std::max(transform_revision_ + 1u, revision_);
  texture_mapping_revision_ =
      std::max(texture_mapping_revision_ + 1u, revision_);
  mesh_binding_revision_ = std::max(mesh_binding_revision_ + 1u, revision_);
  curve_binding_revision_ = std::max(curve_binding_revision_ + 1u, revision_);
  material_binding_revision_ =
      std::max(material_binding_revision_ + 1u, revision_);
  render_component_revision_ =
      std::max(render_component_revision_ + 1u, revision_);
  layer_revision_ = std::max(layer_revision_ + 1u, revision_);
  hierarchy_presentation_revision_ =
      std::max(hierarchy_presentation_revision_ + 1u, revision_);
}

entt::entity SceneDocument::entt_entity(EntityId id) const {
  return require_entity(id);
}

EntityId SceneDocument::entity_id(entt::entity entity) const {
  if (entity == entt::null || !registry_.valid(entity)) {
    return kInvalidEntityId;
  }
  const auto it = ids_by_entity_.find(entity_key(entity));
  return it == ids_by_entity_.end() ? kInvalidEntityId : it->second;
}

void SceneDocument::mark_dirty() {
  note_unscoped_modeling_projection_mutation();
  ++revision_;
  ++content_revision_;
}

void SceneDocument::mark_transient_dirty() {
  note_unscoped_modeling_projection_mutation();
  ++revision_;
}

void SceneDocument::mark_dirty(EntityId entity,
                               SceneModelingProjectionDomain domain) {
  note_modeling_projection_mutation(entity, domain);
  ++revision_;
  ++content_revision_;
}

void SceneDocument::mark_dirty(
    std::span<const EntityId> entities,
    SceneModelingProjectionDomain domain) {
  note_modeling_projection_mutations(entities, domain);
  ++revision_;
  ++content_revision_;
}

void SceneDocument::mark_transient_dirty(
    EntityId entity, SceneModelingProjectionDomain domain) {
  note_modeling_projection_mutation(entity, domain);
  ++revision_;
}

void SceneDocument::mark_transient_dirty(
    std::span<const EntityId> entities,
    SceneModelingProjectionDomain domain) {
  note_modeling_projection_mutations(entities, domain);
  ++revision_;
}

void SceneDocument::note_modeling_projection_mutation(
    EntityId entity, SceneModelingProjectionDomain domain) {
  if (entity == kInvalidEntityId ||
      domain == SceneModelingProjectionDomain::None) {
    note_unscoped_modeling_projection_mutation();
    return;
  }
  if (!scalar_compensation_stack_.empty()) {
    const auto existing =
        modeling_projection_mutations_by_entity_.find(entity);
    const bool counter_record_existed =
        existing != modeling_projection_mutations_by_entity_.end();
    const ModelingProjectionMutationCounters before =
        counter_record_existed ? existing->second
                               : ModelingProjectionMutationCounters{};
    note_scalar_compensation_mutation(entity, before,
                                      counter_record_existed, domain);
  }
  ModelingProjectionMutationCounters &counters =
      modeling_projection_mutations_by_entity_[entity];
  if (has_projection_domain(domain,
                            SceneModelingProjectionDomain::MeshBindings)) {
    ++counters.mesh_bindings;
    ++modeling_projection_mutation_touch_serial_;
  }
  if (has_projection_domain(domain,
                            SceneModelingProjectionDomain::CurveBindings)) {
    ++counters.curve_bindings;
    ++modeling_projection_mutation_touch_serial_;
  }
  if (has_projection_domain(domain,
                            SceneModelingProjectionDomain::Selection)) {
    ++counters.selection;
    ++modeling_projection_mutation_touch_serial_;
  }
  if (has_projection_domain(domain,
                            SceneModelingProjectionDomain::Structure)) {
    ++counters.structure;
    ++modeling_projection_mutation_touch_serial_;
  }
}

void SceneDocument::note_modeling_projection_mutations(
    std::span<const EntityId> entities,
    SceneModelingProjectionDomain domain) {
  if (domain == SceneModelingProjectionDomain::None) {
    note_unscoped_modeling_projection_mutation();
    return;
  }

  bool recorded_entity = false;
  for (const EntityId entity : entities) {
    if (entity == kInvalidEntityId) {
      continue;
    }
    note_modeling_projection_mutation(entity, domain);
    recorded_entity = true;
  }
  if (!recorded_entity) {
    note_unscoped_modeling_projection_mutation();
  }
}

void SceneDocument::note_unscoped_modeling_projection_mutation() {
  ++modeling_projection_mutation_touch_serial_;
  ++unscoped_modeling_projection_mutation_serial_;
}

void SceneDocument::note_scalar_compensation_entity_allocation(
    EntityId entity, EditorObjectId editor_object_id,
    std::uint64_t allocation_serial,
    std::uint64_t previous_allocation_serial) {
  for (const auto &state :
       scalar_compensation_stack_) {
    state->entity_allocations.push_back({
        .entity = entity,
        .editor_object_id = editor_object_id,
        .serial = allocation_serial,
        .previous_serial = previous_allocation_serial,
    });
  }
}

void SceneDocument::note_scalar_compensation_layer_allocation(
    SceneLayerId layer, std::uint64_t allocation_serial,
    std::uint64_t previous_allocation_serial) {
  for (const auto &state :
       scalar_compensation_stack_) {
    state->layer_allocations.push_back({
        .layer = layer,
        .serial = allocation_serial,
        .previous_serial = previous_allocation_serial,
    });
  }
}

void SceneDocument::note_scalar_compensation_mutation(
    EntityId entity, const ModelingProjectionMutationCounters &counters,
    bool counter_record_existed, SceneModelingProjectionDomain domain) {
  for (const auto &state : scalar_compensation_stack_) {
    const auto baseline = state->mutation_baselines.find(entity);
    if (state->acknowledged) {
      if (baseline == state->mutation_baselines.end() ||
          (baseline->second.domains & domain) != domain) {
        state->invalid_inverse_mutation = true;
      }
      continue;
    }
    auto [inserted, was_inserted] = state->mutation_baselines.try_emplace(
        entity,
        SceneScalarCompensationCheckpoint::State::MutationBaseline{
            .record_existed = counter_record_existed,
            .domains = domain,
            .mesh_bindings = counters.mesh_bindings,
            .curve_bindings = counters.curve_bindings,
            .selection = counters.selection,
            .structure = counters.structure,
        });
    if (!was_inserted) {
      inserted->second.domains = inserted->second.domains | domain;
    }
  }
}

bool SceneDocument::close_scalar_compensation_checkpoint(
    const SceneScalarCompensationCheckpoint::State &state) noexcept {
  if (state.owner != this || scalar_compensation_stack_.empty() ||
      state.depth != scalar_compensation_stack_.size() ||
      scalar_compensation_stack_.back().get() != &state) {
    return false;
  }
  scalar_compensation_stack_.pop_back();
  collapse_abandoned_scalar_compensation_checkpoints();
  return true;
}

void SceneDocument::abandon_scalar_compensation_checkpoint(
    const std::shared_ptr<SceneScalarCompensationCheckpoint::State> &state)
    noexcept {
  if (state == nullptr || state->owner != this) {
    return;
  }
  state->abandoned = true;
  collapse_abandoned_scalar_compensation_checkpoints();
}

void SceneDocument::collapse_abandoned_scalar_compensation_checkpoints()
    noexcept {
  while (!scalar_compensation_stack_.empty() &&
         scalar_compensation_stack_.back()->abandoned) {
    scalar_compensation_stack_.pop_back();
  }
}

void SceneDocument::
invalidate_spatial_projection_caches_after_revision_restore() {
  ++spatial_projection_restore_epoch_;
  hierarchy_projection_cache_key_.reset();
  hierarchy_projection_cache_.reset();
  mesh_occurrence_projection_cache_key_.reset();
  mesh_occurrence_projection_cache_.reset();
}

EditorObjectId
SceneDocument::allocate_editor_object_id(EditorObjectId requested) {
  if (requested != kInvalidEditorObjectId) {
    if (entity_by_editor_object_id_.contains(requested)) {
      return kInvalidEditorObjectId;
    }
    if (editor_object_id_allocator_) {
      const EditorObjectId allocated = editor_object_id_allocator_(requested);
      if (allocated != requested) {
        return kInvalidEditorObjectId;
      }
    }
    next_editor_object_id_ = std::max(next_editor_object_id_, requested + 1u);
    return requested;
  }
  if (editor_object_id_allocator_) {
    const std::size_t guard = entity_by_editor_object_id_.size() + 2u;
    for (std::size_t attempt = 0; attempt < guard; ++attempt) {
      const EditorObjectId allocated =
          editor_object_id_allocator_(kInvalidEditorObjectId);
      if (allocated != kInvalidEditorObjectId &&
          !entity_by_editor_object_id_.contains(allocated)) {
        next_editor_object_id_ =
            std::max(next_editor_object_id_, allocated + 1u);
        return allocated;
      }
    }
    return kInvalidEditorObjectId;
  }
  while (entity_by_editor_object_id_.contains(next_editor_object_id_)) {
    ++next_editor_object_id_;
  }
  return next_editor_object_id_++;
}

entt::entity SceneDocument::require_entity(EntityId id) const {
  const auto it = entities_.find(id);
  if (it == entities_.end() || it->second == entt::null ||
      !registry_.valid(it->second)) {
    return entt::null;
  }
  return it->second;
}

bool SceneDocument::is_selected(EntityId id) const {
  if (id == kInvalidEntityId) {
    return false;
  }
  if (const SelectionComponent *component = selection(id)) {
    return component->selected;
  }
  return false;
}

EntityId SceneDocument::first_selected_entity() const {
  const std::vector<EntityId> selected = selected_entities();
  return selected.empty() ? kInvalidEntityId : selected.front();
}

EntityRecord SceneDocument::make_entity_record(EntityId id, int depth,
                                               EntityId active,
                                               const SceneHierarchyProjectionRecord *
                                                   projection) const {
  EntityRecord record;
  record.id = id;
  record.uuid = uuid(id);
  record.editor_object_id = editor_object_id(id);
  record.kind = kind(id);
  record.parent = parent(id);
  record.depth = depth;
  if (const NameComponent *component = name(id)) {
    record.name = component->name;
  }
  if (const VisibilityComponent *component = visibility(id)) {
    record.visible = component->visible;
  }
  record.effectively_visible =
      projection != nullptr ? projection->effectively_visible
                            : is_effectively_visible(id);
  if (const LockComponent *component = lock(id)) {
    record.locked = component->locked;
  }
  record.effectively_locked =
      projection != nullptr ? projection->effectively_locked
                            : is_effectively_locked(id);
  if (const SelectionComponent *component = selection(id)) {
    record.selected = component->selected;
  }
  record.active = id == active;
  record.has_children = has_children(id);
  record.has_mesh = mesh(id) != nullptr;
  record.has_curve = curve(id) != nullptr;
  record.has_light = light(id) != nullptr;
  record.is_scene_root = is_scene_root(id);
  record.has_scene_settings = scene_settings(id) != nullptr;
  record.has_modeling_object = modeling_object(id) != nullptr;
  record.is_instance_definition = is_instance_definition_entity(id);
  record.has_instance_occurrence = instance_occurrence(id) != nullptr;
  record.layer_id = entity_layer(id);
  return record;
}

} // namespace quader
