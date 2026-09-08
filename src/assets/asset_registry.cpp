////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "assets/mesh_asset_payload.hpp"

#include <quader/modeling/neutral/polygon/document_texture.hpp>
#include <quader/modeling/curves/curve_document.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <exception>
#include <optional>
#include <ranges>
#include <unordered_set>
#include <utility>

namespace quader {

AssetRegistry::AssetRegistry() : owner_thread_(std::this_thread::get_id()) {}

AssetRegistry::AssetRegistry(const AssetRegistry &other)
    : next_id_(other.next_id_), revision_(other.revision_),
      modeling_mutation_count_(other.modeling_mutation_count_),
      unscoped_mutation_count_(other.unscoped_mutation_count_),
      mutation_count_by_asset_(other.mutation_count_by_asset_),
      asset_allocation_serial_(other.asset_allocation_serial_),
      last_asset_allocation_serial_(other.last_asset_allocation_serial_),
      material_catalog_revision_(other.material_catalog_revision_),
      mesh_material_usage_revision_(other.mesh_material_usage_revision_),
      meshes_(other.meshes_), deferred_meshes_(other.deferred_meshes_),
      mesh_geometry_pool_(other.mesh_geometry_pool_),
      authored_topology_pool_(other.authored_topology_pool_),
      materials_(other.materials_), curves_(other.curves_),
      textures_(other.textures_),
      owner_thread_(std::this_thread::get_id()) {}

AssetRegistry &AssetRegistry::operator=(const AssetRegistry &other) {
  if (this == &other) {
    return *this;
  }
  assert_owner_thread();
  next_id_ = other.next_id_;
  revision_ = other.revision_;
  modeling_mutation_count_ = other.modeling_mutation_count_;
  unscoped_mutation_count_ = other.unscoped_mutation_count_;
  mutation_count_by_asset_ = other.mutation_count_by_asset_;
  asset_allocation_serial_ = other.asset_allocation_serial_;
  last_asset_allocation_serial_ = other.last_asset_allocation_serial_;
  material_catalog_revision_ = other.material_catalog_revision_;
  mesh_material_usage_revision_ = other.mesh_material_usage_revision_;
  meshes_ = other.meshes_;
  deferred_meshes_ = other.deferred_meshes_;
  mesh_geometry_pool_ = other.mesh_geometry_pool_;
  authored_topology_pool_ = other.authored_topology_pool_;
  materials_ = other.materials_;
  curves_ = other.curves_;
  textures_ = other.textures_;
  owner_thread_ = std::this_thread::get_id();
  return *this;
}

AssetRegistry::AssetRegistry(AssetRegistry &&other) noexcept
    : next_id_(other.next_id_), revision_(other.revision_),
      modeling_mutation_count_(other.modeling_mutation_count_),
      unscoped_mutation_count_(other.unscoped_mutation_count_),
      mutation_count_by_asset_(std::move(other.mutation_count_by_asset_)),
      asset_allocation_serial_(other.asset_allocation_serial_),
      last_asset_allocation_serial_(
          std::move(other.last_asset_allocation_serial_)),
      material_catalog_revision_(other.material_catalog_revision_),
      mesh_material_usage_revision_(other.mesh_material_usage_revision_),
      meshes_(std::move(other.meshes_)),
      deferred_meshes_(std::move(other.deferred_meshes_)),
      mesh_geometry_pool_(std::move(other.mesh_geometry_pool_)),
      authored_topology_pool_(std::move(other.authored_topology_pool_)),
      materials_(std::move(other.materials_)),
      curves_(std::move(other.curves_)),
      textures_(std::move(other.textures_)),
      owner_thread_(std::this_thread::get_id()) {}

AssetRegistry &AssetRegistry::operator=(AssetRegistry &&other) noexcept {
  if (this == &other) {
    return *this;
  }
  next_id_ = other.next_id_;
  revision_ = other.revision_;
  modeling_mutation_count_ = other.modeling_mutation_count_;
  unscoped_mutation_count_ = other.unscoped_mutation_count_;
  mutation_count_by_asset_ = std::move(other.mutation_count_by_asset_);
  asset_allocation_serial_ = other.asset_allocation_serial_;
  last_asset_allocation_serial_ =
      std::move(other.last_asset_allocation_serial_);
  material_catalog_revision_ = other.material_catalog_revision_;
  mesh_material_usage_revision_ = other.mesh_material_usage_revision_;
  meshes_ = std::move(other.meshes_);
  deferred_meshes_ = std::move(other.deferred_meshes_);
  mesh_geometry_pool_ = std::move(other.mesh_geometry_pool_);
  authored_topology_pool_ = std::move(other.authored_topology_pool_);
  materials_ = std::move(other.materials_);
  curves_ = std::move(other.curves_);
  textures_ = std::move(other.textures_);
  owner_thread_ = std::this_thread::get_id();
  return *this;
}

bool AssetRegistry::on_owner_thread() const noexcept {
  return owner_thread_ == std::this_thread::get_id();
}

void AssetRegistry::assert_owner_thread() const {
  assert(on_owner_thread() &&
         "AssetRegistry mutation/materialization must run on its owner thread");
}

ModelingAssetProjectionCheckpoint
AssetRegistry::capture_modeling_projection_checkpoint(
    std::span<const AssetId> modeling_asset_ids) const {
  assert_owner_thread();
  ModelingAssetProjectionCheckpoint checkpoint;
  for (const AssetId id : modeling_asset_ids) {
    if (id != kInvalidAssetId && deferred_meshes_.contains(id)) {
      return checkpoint;
    }
  }
  checkpoint.valid_ = true;
  checkpoint.origin_ = this;
  checkpoint.next_id_ = next_id_;
  checkpoint.revision_ = revision_;
  checkpoint.mesh_material_usage_revision_ = mesh_material_usage_revision_;
  checkpoint.modeling_mutation_count_ = modeling_mutation_count_;
  checkpoint.unscoped_mutation_count_ = unscoped_mutation_count_;
  checkpoint.allocation_serial_ = asset_allocation_serial_;
  for (const AssetId id : modeling_asset_ids) {
    if (id == kInvalidAssetId ||
        checkpoint.mutation_count_by_asset_.contains(id)) {
      continue;
    }
    const auto mutation = mutation_count_by_asset_.find(id);
    checkpoint.mutation_count_by_asset_.emplace(
        id, mutation != mutation_count_by_asset_.end() ? mutation->second : 0u);
  }
  checkpoint.meshes_ = meshes_.capture_targeted(modeling_asset_ids);
  checkpoint.curves_ = curves_.capture_targeted(modeling_asset_ids);
  return checkpoint;
}

ModelingAssetProjectionMutationFence::ModelingAssetProjectionMutationFence(
    ModelingAssetProjectionMutationFence &&other) noexcept
    : origin_(std::exchange(other.origin_, nullptr)),
      records_(std::move(other.records_)),
      work_(std::exchange(
          other.work_, ModelingAssetProjectionMutationFenceWork{})) {}

ModelingAssetProjectionMutationFence &
ModelingAssetProjectionMutationFence::operator=(
    ModelingAssetProjectionMutationFence &&other) noexcept {
  if (this == &other) {
    return *this;
  }
  origin_ = std::exchange(other.origin_, nullptr);
  records_ = std::move(other.records_);
  work_ = std::exchange(other.work_,
                        ModelingAssetProjectionMutationFenceWork{});
  return *this;
}

bool ModelingAssetProjectionMutationFence::matches_current(
    const AssetRegistry &assets,
    ModelingAssetProjectionMutationFenceWork *reported_work) const {
  ModelingAssetProjectionMutationFenceWork current_work{
      .participant_reference_count = records_.size(),
      .participant_record_count = records_.size(),
  };
  const auto report = [&](bool matches) {
    if (reported_work != nullptr) {
      *reported_work = current_work;
    }
    return matches;
  };
  if (!valid() || origin_ != &assets) {
    return report(false);
  }
  assets.assert_owner_thread();
  for (const Record &record : records_) {
    ++current_work.mutation_counter_lookup_count;
    const auto mutation = assets.mutation_count_by_asset_.find(record.asset);
    const std::uint64_t count =
        mutation != assets.mutation_count_by_asset_.end()
            ? mutation->second
            : 0u;
    if (count != record.mutation_count) {
      return report(false);
    }
  }
  return report(true);
}

bool ModelingAssetProjectionMutationFence::acknowledge_current(
    const AssetRegistry &assets, std::span<const AssetId> newly_created,
    ModelingAssetProjectionMutationFenceWork *reported_work) {
  ModelingAssetProjectionMutationFenceWork current_work{
      .participant_reference_count = records_.size() + newly_created.size(),
  };
  const auto report = [&](bool acknowledged) {
    if (reported_work != nullptr) {
      *reported_work = current_work;
    }
    return acknowledged;
  };
  if (!valid() || origin_ != &assets) {
    return report(false);
  }
  assets.assert_owner_thread();
  std::vector<AssetId> participants;
  participants.reserve(records_.size() + newly_created.size());
  for (const Record &record : records_) {
    participants.push_back(record.asset);
  }
  participants.insert(participants.end(), newly_created.begin(),
                      newly_created.end());
  std::erase(participants, kInvalidAssetId);
  std::ranges::sort(participants);
  participants.erase(
      std::unique(participants.begin(), participants.end()),
      participants.end());

  std::vector<Record> acknowledged;
  acknowledged.reserve(participants.size());
  for (const AssetId asset : participants) {
    ++current_work.mutation_counter_lookup_count;
    const auto mutation = assets.mutation_count_by_asset_.find(asset);
    acknowledged.push_back({
        .asset = asset,
        .mutation_count =
            mutation != assets.mutation_count_by_asset_.end()
                ? mutation->second
                : 0u,
    });
  }
  current_work.participant_record_count = acknowledged.size();
  records_ = std::move(acknowledged);
  work_ = current_work;
  return report(true);
}

ModelingAssetProjectionMutationFence
AssetRegistry::capture_modeling_projection_mutation_fence(
    std::span<const AssetId> modeling_asset_ids) const {
  assert_owner_thread();
  ModelingAssetProjectionMutationFence fence;
  fence.origin_ = this;
  fence.work_.participant_reference_count = modeling_asset_ids.size();
  std::vector<AssetId> participants(modeling_asset_ids.begin(),
                                    modeling_asset_ids.end());
  std::erase(participants, kInvalidAssetId);
  std::ranges::sort(participants);
  participants.erase(
      std::unique(participants.begin(), participants.end()),
      participants.end());
  fence.records_.reserve(participants.size());
  for (const AssetId asset : participants) {
    ++fence.work_.mutation_counter_lookup_count;
    const auto mutation = mutation_count_by_asset_.find(asset);
    fence.records_.push_back({
        .asset = asset,
        .mutation_count = mutation != mutation_count_by_asset_.end()
                              ? mutation->second
                              : 0u,
    });
  }
  fence.work_.participant_record_count = fence.records_.size();
  return fence;
}

std::optional<AssetRegistry::ModelingAssetProjectionRestorePlan>
AssetRegistry::prepare_modeling_projection_restore(
    const ModelingAssetProjectionCheckpoint &checkpoint,
    std::span<const AssetId> created_modeling_asset_ids) const {
  assert_owner_thread();
  if (!checkpoint.valid() || checkpoint.origin_ != this) {
    return std::nullopt;
  }
  if (next_id_ < checkpoint.next_id_ ||
      revision_ < checkpoint.revision_ ||
      modeling_mutation_count_ < checkpoint.modeling_mutation_count_ ||
      asset_allocation_serial_ < checkpoint.allocation_serial_ ||
      unscoped_mutation_count_ != checkpoint.unscoped_mutation_count_) {
    return std::nullopt;
  }

  ModelingAssetProjectionRestorePlan plan;
  plan.created_asset_ids.assign(created_modeling_asset_ids.begin(),
                                created_modeling_asset_ids.end());
  std::erase(plan.created_asset_ids, kInvalidAssetId);
  std::ranges::sort(plan.created_asset_ids);
  plan.created_asset_ids.erase(
      std::unique(plan.created_asset_ids.begin(),
                  plan.created_asset_ids.end()),
      plan.created_asset_ids.end());

  const std::uint64_t allocation_delta =
      asset_allocation_serial_ - checkpoint.allocation_serial_;
  if (plan.created_asset_ids.size() != allocation_delta) {
    return std::nullopt;
  }
  for (const AssetId id : plan.created_asset_ids) {
    const auto allocation = last_asset_allocation_serial_.find(id);
    if (allocation == last_asset_allocation_serial_.end() ||
        allocation->second <= checkpoint.allocation_serial_ ||
        allocation->second > asset_allocation_serial_) {
      return std::nullopt;
    }
    if (meshes_.contains(id)) {
      plan.created_mesh_ids.push_back(id);
    }
    if (curves_.contains(id)) {
      plan.created_curve_ids.push_back(id);
    }
  }

  std::unordered_set<AssetId> enrolled_assets;
  enrolled_assets.reserve(checkpoint.mutation_count_by_asset_.size() +
                          plan.created_asset_ids.size());
  for (const auto &[id, count] : checkpoint.mutation_count_by_asset_) {
    (void)count;
    enrolled_assets.insert(id);
  }
  enrolled_assets.insert(plan.created_asset_ids.begin(),
                         plan.created_asset_ids.end());
  plan.enrolled_asset_ids.assign(enrolled_assets.begin(),
                                 enrolled_assets.end());
  std::ranges::sort(plan.enrolled_asset_ids);
  const std::uint64_t owner_delta =
      modeling_mutation_count_ - checkpoint.modeling_mutation_count_;
  std::uint64_t enrolled_delta = 0u;
  for (const AssetId id : plan.enrolled_asset_ids) {
    const auto captured = checkpoint.mutation_count_by_asset_.find(id);
    const std::uint64_t captured_count =
        captured != checkpoint.mutation_count_by_asset_.end()
            ? captured->second
            : 0u;
    const auto current = mutation_count_by_asset_.find(id);
    const std::uint64_t current_count =
        current != mutation_count_by_asset_.end() ? current->second : 0u;
    if (current_count < captured_count) {
      return std::nullopt;
    }
    const std::uint64_t delta = current_count - captured_count;
    if (enrolled_delta > owner_delta ||
        delta > owner_delta - enrolled_delta) {
      return std::nullopt;
    }
    enrolled_delta += delta;
  }
  if (enrolled_delta != owner_delta) {
    return std::nullopt;
  }
  return plan;
}

bool AssetRegistry::can_restore_modeling_projection_checkpoint(
    const ModelingAssetProjectionCheckpoint &checkpoint,
    std::span<const AssetId> created_modeling_asset_ids) const {
  return prepare_modeling_projection_restore(checkpoint,
                                              created_modeling_asset_ids)
      .has_value();
}

bool AssetRegistry::restore_modeling_projection_checkpoint(
    const ModelingAssetProjectionCheckpoint &checkpoint,
    std::span<const AssetId> created_modeling_asset_ids) {
  std::optional<ModelingAssetProjectionRestorePlan> plan =
      prepare_modeling_projection_restore(checkpoint,
                                          created_modeling_asset_ids);
  if (!plan.has_value()) {
    return false;
  }
  apply_modeling_projection_restore(checkpoint, *plan);
  return true;
}

std::optional<AssetRegistry::ConcurrentModelingAssetProjectionRestorePlan>
AssetRegistry::
    prepare_modeling_projection_restore_preserving_concurrent_changes(
        const ModelingAssetProjectionCheckpoint &checkpoint,
        const ModelingAssetProjectionMutationFence &acknowledged_fence,
        std::span<const AssetId> created_modeling_asset_ids) const {
  assert_owner_thread();
  if (!checkpoint.valid() || checkpoint.origin_ != this ||
      !acknowledged_fence.valid() || acknowledged_fence.origin_ != this ||
      !acknowledged_fence.matches_current(*this) ||
      next_id_ < checkpoint.next_id_ || revision_ < checkpoint.revision_ ||
      modeling_mutation_count_ < checkpoint.modeling_mutation_count_ ||
      unscoped_mutation_count_ < checkpoint.unscoped_mutation_count_ ||
      asset_allocation_serial_ < checkpoint.allocation_serial_) {
    return std::nullopt;
  }

  std::vector<AssetId> created(created_modeling_asset_ids.begin(),
                               created_modeling_asset_ids.end());
  std::erase(created, kInvalidAssetId);
  std::ranges::sort(created);
  created.erase(std::unique(created.begin(), created.end()), created.end());

  // The fence must cover the complete payload checkpoint plus every explicit
  // provisional allocation. This prevents a permissive restore from being
  // used with a partial acknowledgement set.
  std::vector<AssetId> expected_fence_participants;
  expected_fence_participants.reserve(
      checkpoint.mutation_count_by_asset_.size() + created.size());
  for (const auto &[asset, mutation_count] :
       checkpoint.mutation_count_by_asset_) {
    (void)mutation_count;
    expected_fence_participants.push_back(asset);
  }
  expected_fence_participants.insert(expected_fence_participants.end(),
                                     created.begin(), created.end());
  std::ranges::sort(expected_fence_participants);
  expected_fence_participants.erase(
      std::unique(expected_fence_participants.begin(),
                  expected_fence_participants.end()),
      expected_fence_participants.end());
  std::vector<AssetId> acknowledged_participants;
  acknowledged_participants.reserve(acknowledged_fence.records_.size());
  for (const ModelingAssetProjectionMutationFence::Record &record :
       acknowledged_fence.records_) {
    acknowledged_participants.push_back(record.asset);
  }
  if (acknowledged_participants != expected_fence_participants) {
    return std::nullopt;
  }

  ConcurrentModelingAssetProjectionRestorePlan plan;
  for (const AssetId asset : created) {
    const auto allocation = last_asset_allocation_serial_.find(asset);
    if (allocation == last_asset_allocation_serial_.end() ||
        allocation->second <= checkpoint.allocation_serial_ ||
        allocation->second > asset_allocation_serial_) {
      return std::nullopt;
    }
    const auto captured_mesh = checkpoint.meshes_.documents.find(asset);
    const auto captured_curve = checkpoint.curves_.documents.find(asset);
    if ((captured_mesh != checkpoint.meshes_.documents.end() &&
         captured_mesh->second.has_value()) ||
        (captured_curve != checkpoint.curves_.documents.end() &&
         captured_curve->second.has_value()) ||
        materials_.contains(asset) || textures_.contains(asset)) {
      return std::nullopt;
    }
    const bool mesh_exists = meshes_.contains(asset);
    const bool curve_exists = curves_.contains(asset);
    if (mesh_exists && curve_exists) {
      return std::nullopt;
    }
    if (mesh_exists) {
      plan.created_mesh_ids.push_back(asset);
      plan.mesh_material_usage_dirty = true;
    }
    if (curve_exists) {
      plan.created_curve_ids.push_back(asset);
    }
  }

  plan.restore_asset_ids.reserve(checkpoint.mutation_count_by_asset_.size());
  for (const auto &[asset, captured_count] :
       checkpoint.mutation_count_by_asset_) {
    const auto current = mutation_count_by_asset_.find(asset);
    const std::uint64_t current_count =
        current != mutation_count_by_asset_.end() ? current->second : 0u;
    if (current_count < captured_count) {
      return std::nullopt;
    }
    if (current_count == captured_count) {
      continue;
    }
    plan.restore_asset_ids.push_back(asset);
    const auto captured_mesh = checkpoint.meshes_.documents.find(asset);
    if ((captured_mesh != checkpoint.meshes_.documents.end() &&
         captured_mesh->second.has_value()) ||
        meshes_.contains(asset)) {
      plan.mesh_material_usage_dirty = true;
    }
  }
  plan.changed_asset_ids = plan.restore_asset_ids;
  plan.changed_asset_ids.insert(plan.changed_asset_ids.end(), created.begin(),
                                created.end());
  std::ranges::sort(plan.changed_asset_ids);
  plan.changed_asset_ids.erase(
      std::unique(plan.changed_asset_ids.begin(),
                  plan.changed_asset_ids.end()),
      plan.changed_asset_ids.end());
  return plan;
}

bool AssetRegistry::
    can_restore_modeling_projection_checkpoint_preserving_concurrent_changes(
        const ModelingAssetProjectionCheckpoint &checkpoint,
        const ModelingAssetProjectionMutationFence &acknowledged_fence,
        std::span<const AssetId> created_modeling_asset_ids) const {
  return prepare_modeling_projection_restore_preserving_concurrent_changes(
             checkpoint, acknowledged_fence, created_modeling_asset_ids)
      .has_value();
}

bool AssetRegistry::
    restore_modeling_projection_checkpoint_preserving_concurrent_changes(
        const ModelingAssetProjectionCheckpoint &checkpoint,
        const ModelingAssetProjectionMutationFence &acknowledged_fence,
        std::span<const AssetId> created_modeling_asset_ids) {
  std::optional<ConcurrentModelingAssetProjectionRestorePlan> concurrent =
      prepare_modeling_projection_restore_preserving_concurrent_changes(
          checkpoint, acknowledged_fence, created_modeling_asset_ids);
  if (!concurrent.has_value()) {
    return false;
  }
  apply_modeling_projection_restore_preserving_concurrent_changes(
      checkpoint, *concurrent);
  return true;
}

void AssetRegistry::apply_modeling_projection_restore(
    const ModelingAssetProjectionCheckpoint &checkpoint,
    const ModelingAssetProjectionRestorePlan &plan) {
  const bool meshes_restored =
      meshes_.restore_targeted(checkpoint.meshes_, plan.created_mesh_ids);
  const bool curves_restored =
      curves_.restore_targeted(checkpoint.curves_, plan.created_curve_ids);
  assert(meshes_restored && curves_restored);
  (void)meshes_restored;
  (void)curves_restored;
  for (const AssetId id : plan.created_mesh_ids) {
    deferred_meshes_.erase(id);
  }
  for (const auto &[id, mesh] : checkpoint.meshes_.documents) {
    (void)mesh;
    deferred_meshes_.erase(id);
  }
  for (const AssetId id : plan.enrolled_asset_ids) {
    const auto captured = checkpoint.mutation_count_by_asset_.find(id);
    if (captured == checkpoint.mutation_count_by_asset_.end() ||
        captured->second == 0u) {
      mutation_count_by_asset_.erase(id);
    } else {
      const auto current = mutation_count_by_asset_.find(id);
      assert(current != mutation_count_by_asset_.end());
      current->second = captured->second;
    }
  }
  next_id_ = checkpoint.next_id_;
  revision_ = checkpoint.revision_;
  mesh_material_usage_revision_ = checkpoint.mesh_material_usage_revision_;
  modeling_mutation_count_ = checkpoint.modeling_mutation_count_;
}

void AssetRegistry::
    apply_modeling_projection_restore_preserving_concurrent_changes(
        const ModelingAssetProjectionCheckpoint &checkpoint,
        const ConcurrentModelingAssetProjectionRestorePlan &plan) {
  const bool meshes_restored = meshes_.restore_targeted_preserving_revision(
      checkpoint.meshes_, plan.restore_asset_ids, plan.created_mesh_ids);
  const bool curves_restored = curves_.restore_targeted_preserving_revision(
      checkpoint.curves_, plan.restore_asset_ids, plan.created_curve_ids);
  assert(meshes_restored && curves_restored);
  (void)meshes_restored;
  (void)curves_restored;
  for (const AssetId asset : plan.created_mesh_ids) {
    deferred_meshes_.erase(asset);
  }
  for (const AssetId asset : plan.restore_asset_ids) {
    deferred_meshes_.erase(asset);
  }
  if (!plan.changed_asset_ids.empty()) {
    mark_modeling_assets_dirty(plan.changed_asset_ids,
                               plan.mesh_material_usage_dirty);
  }
}

quader_poly::Document &MeshAsset::edit_authored_polygon_document() {
  auto editable = authored_polygon_document
                      ? std::make_shared<quader_poly::Document>(
                            *authored_polygon_document)
                      : std::make_shared<quader_poly::Document>();
  authored_polygon_document = editable;
  has_authored_polygon_document = true;
  invalidate_wire_geometry_payload();
  return *editable;
}

void MeshAsset::replace_authored_polygon_document(
    quader_poly::Document document) {
  authored_polygon_document =
      std::make_shared<const quader_poly::Document>(std::move(document));
  has_authored_polygon_document = true;
  invalidate_wire_geometry_payload();
}

AssetId AssetRegistry::add_mesh(MeshAsset mesh) {
  assert_owner_thread();
  if (next_id_ == kInvalidAssetId || !validate_mesh(mesh).ok) {
    return kInvalidAssetId;
  }
  const AssetId id = next_id_++;
  mesh = isolate_mesh_asset_payload(std::move(mesh));
  intern_mesh_payload(mesh);
  meshes_.add(id, std::move(mesh));
  note_asset_allocation(id);
  const std::array<AssetId, 1> changed{id};
  mark_modeling_assets_dirty(changed, true);
  return id;
}

AssetId AssetRegistry::add_mesh(AssetId id, MeshAsset mesh) {
  assert_owner_thread();
  if (id == kInvalidAssetId) {
    return add_mesh(std::move(mesh));
  }
  if (meshes_.contains(id) || !validate_mesh(mesh).ok) {
    return kInvalidAssetId;
  }
  next_id_ = std::max(next_id_, id + 1u);
  mesh = isolate_mesh_asset_payload(std::move(mesh));
  intern_mesh_payload(mesh);
  meshes_.add(id, std::move(mesh));
  note_asset_allocation(id);
  const std::array<AssetId, 1> changed{id};
  mark_modeling_assets_dirty(changed, true);
  return id;
}

AssetId AssetRegistry::add_curve(CurveAsset curve) {
  assert_owner_thread();
  if (next_id_ == kInvalidAssetId) {
    return kInvalidAssetId;
  }
  const AssetId id = next_id_;
  return add_curve(id, std::move(curve));
}

AssetId AssetRegistry::add_curve(AssetId id, CurveAsset curve) {
  assert_owner_thread();
  if (id == kInvalidAssetId) {
    return add_curve(std::move(curve));
  }
  if (curves_.contains(id) || !validate_curve(curve).ok) {
    return kInvalidAssetId;
  }
  // Pin an independent wrapper: a caller may retain a mutable CurveDocument
  // alias, while the authored storage itself remains shared and immutable.
  curve.document =
      std::make_shared<const modeling::CurveDocument>(*curve.document);
  if (curves_.add(id, std::move(curve)) == kInvalidAssetId) {
    return kInvalidAssetId;
  }
  note_asset_allocation(id);
  next_id_ = std::max(next_id_, id + 1u);
  const std::array<AssetId, 1> changed{id};
  mark_modeling_assets_dirty(changed, false);
  return id;
}

bool AssetRegistry::remove_curve(AssetId id) {
  assert_owner_thread();
  if (!curves_.remove(id)) {
    return false;
  }
  const std::array<AssetId, 1> changed{id};
  mark_modeling_assets_dirty(changed, false);
  return true;
}

bool AssetRegistry::replace_curve(AssetId id, CurveAsset curve) {
  assert_owner_thread();
  if (!curves_.contains(id) || !validate_curve(curve).ok) {
    return false;
  }
  curve.document =
      std::make_shared<const modeling::CurveDocument>(*curve.document);
  if (!curves_.replace(id, std::move(curve))) {
    return false;
  }
  const std::array<AssetId, 1> changed{id};
  mark_modeling_assets_dirty(changed, false);
  return true;
}

AssetRegistryValidationResult
AssetRegistry::validate_curve(const CurveAsset &curve) const {
  if (!curve.document) {
    return {.ok = false, .message = "Curve asset has no authored document."};
  }
  if (curve.document->point_count() < 2u) {
    return {.ok = false,
            .message = "A published curve requires at least two anchors."};
  }
  const auto validation = curve.document->validate();
  if (!validation.ok()) {
    return {.ok = false, .message = validation.error().message};
  }
  return {};
}

const CurveAsset *AssetRegistry::curve(AssetId id) const {
  return curves_.get(id);
}

bool AssetRegistry::has_curve(AssetId id) const { return curves_.contains(id); }

CurveAssetRevision AssetRegistry::curve_revision(AssetId id) const {
  return curves_.revision(id);
}

AssetId AssetRegistry::add_mesh_payload_alias(
    AssetId id, AssetId payload_source, std::string name,
    bool has_stable_cpu_topology) {
  assert_owner_thread();
  if (id == kInvalidAssetId || meshes_.contains(id)) {
    return kInvalidAssetId;
  }
  const MeshAsset *source = mesh(payload_source);
  if (source == nullptr) {
    return kInvalidAssetId;
  }

  MeshAsset alias = *source;
  alias.name = std::move(name);
  alias.has_stable_cpu_topology = has_stable_cpu_topology;
  if (meshes_.add(id, std::move(alias)) == kInvalidAssetId) {
    return kInvalidAssetId;
  }
  note_asset_allocation(id);
  next_id_ = std::max(next_id_, id + 1u);
  const std::array<AssetId, 1> changed{id};
  mark_modeling_assets_dirty(changed, true);
  return id;
}

AssetId AssetRegistry::add_deferred_mesh(
    AssetId id, std::string name, bool has_stable_cpu_topology,
    bool has_authored_polygon_document, std::function<MeshAsset()> loader,
    std::function<std::shared_ptr<const std::vector<std::byte>>()>
        encoded_geometry_loader) {
  assert_owner_thread();
  if (id == kInvalidAssetId || meshes_.contains(id) || !loader) {
    return kInvalidAssetId;
  }
  MeshAsset descriptor;
  descriptor.name = std::move(name);
  descriptor.has_stable_cpu_topology = has_stable_cpu_topology;
  if (meshes_.add(id, std::move(descriptor)) == kInvalidAssetId) {
    return kInvalidAssetId;
  }
  deferred_meshes_.emplace(
      id, DeferredMesh{
              .has_stable_cpu_topology = has_stable_cpu_topology,
              .has_authored_polygon_document =
                  has_authored_polygon_document,
              .loader = std::move(loader),
              .encoded_geometry_loader = std::move(encoded_geometry_loader),
          });
  note_asset_allocation(id);
  next_id_ = std::max(next_id_, id + 1u);
  const std::array<AssetId, 1> changed{id};
  mark_modeling_assets_dirty(changed, true);
  return id;
}

AssetId AssetRegistry::add_material(MaterialDocument material) {
  assert_owner_thread();
  if (next_id_ == kInvalidAssetId || !validate_material(material).ok) {
    return kInvalidAssetId;
  }
  const AssetId id = next_id_++;
  normalize_material_texture_slots(material);
  materials_.add(id, std::move(material));
  note_asset_allocation(id);
  mark_material_dirty();
  return id;
}

AssetId AssetRegistry::add_material(AssetId id, MaterialDocument material) {
  assert_owner_thread();
  if (id == kInvalidAssetId) {
    return add_material(std::move(material));
  }
  if (materials_.contains(id) || !validate_material(material).ok) {
    return kInvalidAssetId;
  }
  next_id_ = std::max(next_id_, id + 1u);
  normalize_material_texture_slots(material);
  materials_.add(id, std::move(material));
  note_asset_allocation(id);
  mark_material_dirty();
  return id;
}

AssetId AssetRegistry::add_texture(TextureAsset texture) {
  assert_owner_thread();
  if (next_id_ == kInvalidAssetId) {
    return kInvalidAssetId;
  }
  const AssetId id = next_id_++;
  textures_.add(id, std::move(texture));
  note_asset_allocation(id);
  mark_dirty();
  return id;
}

AssetId AssetRegistry::add_texture(AssetId id, TextureAsset texture) {
  assert_owner_thread();
  if (id == kInvalidAssetId) {
    return add_texture(std::move(texture));
  }
  if (textures_.contains(id)) {
    return kInvalidAssetId;
  }
  const bool restores_material = material_references_texture(id);
  next_id_ = std::max(next_id_, id + 1u);
  textures_.add(id, std::move(texture));
  note_asset_allocation(id);
  if (restores_material) {
    mark_material_dirty();
  } else {
    mark_dirty();
  }
  return id;
}

bool AssetRegistry::remove_mesh(AssetId id) {
  assert_owner_thread();
  if (!meshes_.remove(id)) {
    return false;
  }
  deferred_meshes_.erase(id);
  const std::array<AssetId, 1> changed{id};
  mark_modeling_assets_dirty(changed, true);
  return true;
}

bool AssetRegistry::remove_material(AssetId id) {
  assert_owner_thread();
  if (!materials_.remove(id)) {
    return false;
  }
  mark_material_dirty();
  return true;
}

bool AssetRegistry::remove_texture(AssetId id) {
  assert_owner_thread();
  const bool invalidates_material = material_references_texture(id);
  if (!textures_.remove(id)) {
    return false;
  }
  if (invalidates_material) {
    mark_material_dirty();
  } else {
    mark_dirty();
  }
  return true;
}

bool AssetRegistry::replace_mesh(AssetId id, MeshAsset mesh) {
  MeshAssetReplacement replacement{.id = id, .mesh = std::move(mesh)};
  return replace_meshes(
      std::span<MeshAssetReplacement>(&replacement, 1u));
}

bool AssetRegistry::replace_meshes(
    std::span<MeshAssetReplacement> replacements) {
  assert_owner_thread();
  if (replacements.empty()) {
    return true;
  }

  std::unordered_set<AssetId> unique_ids;
  unique_ids.reserve(replacements.size());
  for (const MeshAssetReplacement &replacement : replacements) {
    if (replacement.id == kInvalidAssetId ||
        !meshes_.contains(replacement.id) ||
        !unique_ids.insert(replacement.id).second ||
        !validate_mesh(replacement.mesh).ok) {
      return false;
    }
  }

  // No owner state changes before the complete batch has passed validation.
  // Preparation can then share immutable payloads without copying geometry.
  for (MeshAssetReplacement &replacement : replacements) {
    replacement.mesh =
        isolate_mesh_asset_payload(std::move(replacement.mesh));
    intern_mesh_payload(replacement.mesh);
  }

  for (MeshAssetReplacement &replacement : replacements) {
    const bool replaced =
        meshes_.replace(replacement.id, std::move(replacement.mesh));
    assert(replaced && "prevalidated mesh replacement disappeared");
    if (!replaced) {
      return false;
    }
    deferred_meshes_.erase(replacement.id);
  }
  std::vector<AssetId> changed_ids;
  changed_ids.reserve(replacements.size());
  for (const MeshAssetReplacement &replacement : replacements) {
    changed_ids.push_back(replacement.id);
  }
  mark_modeling_assets_dirty(changed_ids, true);
  return true;
}

bool AssetRegistry::replace_material(AssetId id, MaterialDocument material) {
  assert_owner_thread();
  if (!validate_material(material).ok) {
    return false;
  }
  normalize_material_texture_slots(material);
  if (!materials_.replace(id, std::move(material))) {
    return false;
  }
  mark_material_dirty();
  return true;
}

bool AssetRegistry::replace_texture(AssetId id, TextureAsset texture) {
  assert_owner_thread();
  const bool affects_material = material_references_texture(id);
  if (!textures_.replace(id, std::move(texture))) {
    return false;
  }
  if (affects_material) {
    mark_material_dirty();
  } else {
    mark_dirty();
  }
  return true;
}

bool AssetRegistry::update_mesh(
    AssetId id, const std::function<void(MeshAsset &)> &update) {
  assert_owner_thread();
  const MeshAsset *existing = mesh(id);
  if (existing == nullptr) {
    return false;
  }
  MeshAsset updated = clone_mesh_asset_payload(*existing);
  update(updated);
  if (!validate_mesh(updated).ok) {
    return false;
  }
  updated = isolate_mesh_asset_payload(std::move(updated));
  intern_mesh_payload(updated);
  if (!meshes_.replace(id, std::move(updated))) {
    return false;
  }
  const std::array<AssetId, 1> changed{id};
  mark_modeling_assets_dirty(changed, true);
  return true;
}

bool AssetRegistry::update_material(
    AssetId id, const std::function<void(MaterialDocument &)> &update) {
  assert_owner_thread();
  const MaterialDocument *existing = materials_.get(id);
  if (existing == nullptr) {
    return false;
  }
  MaterialDocument updated = *existing;
  update(updated);
  if (!validate_material(updated).ok) {
    return false;
  }
  normalize_material_texture_slots(updated);
  if (!materials_.replace(id, std::move(updated))) {
    return false;
  }
  mark_material_dirty();
  return true;
}

AssetRegistryMutationResult
AssetRegistry::set_material_texture(AssetId material, std::string_view slot,
                                    AssetId texture) {
  assert_owner_thread();
  if (slot.empty()) {
    return {.ok = false,
            .changed = false,
            .message = "Material texture slot is empty"};
  }
  const std::optional<std::string> texture_slot =
      canonical_material_texture_slot(slot);
  if (!texture_slot.has_value()) {
    return {.ok = false,
            .changed = false,
            .message = "Unsupported material texture slot"};
  }
  if (texture == kInvalidAssetId || textures_.get(texture) == nullptr) {
    return {.ok = false,
            .changed = false,
            .message = "Texture asset does not exist"};
  }

  const MaterialDocument *existing = materials_.get(material);
  if (existing == nullptr) {
    return {.ok = false,
            .changed = false,
            .message = "Material asset does not exist"};
  }
  if (const auto current = existing->texture_slots.find(*texture_slot);
      current != existing->texture_slots.end() && current->second == texture) {
    return {.ok = true, .changed = false, .message = "Material unchanged"};
  }

  MaterialDocument updated = *existing;
  set_material_texture_slot(updated, *texture_slot, texture);
  const AssetRegistryValidationResult validation = validate_material(updated);
  if (!validation.ok) {
    return {.ok = false, .changed = false, .message = validation.message};
  }
  if (!replace_material(material, std::move(updated))) {
    return {.ok = false,
            .changed = false,
            .message = "Material asset does not exist"};
  }
  return {.ok = true, .changed = true, .message = "Material updated"};
}

AssetRegistryValidationResult
AssetRegistry::validate_mesh(const MeshAsset &mesh) const {
  const auto finite_vec2 = [](QVec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
  };
  const auto finite_vec3 = [](QVec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
  };

  const MeshGeometryPayload &geometry = mesh.geometry();
  for (const MeshVertex &vertex : geometry.vertices) {
    if (!finite_vec3(vertex.position) || !finite_vec3(vertex.normal) ||
        !finite_vec2(vertex.uv) ||
        !finite_vec3(vertex.uv_geometric_normal) ||
        std::ranges::any_of(vertex.uv_carrier, [](float value) {
          return !std::isfinite(value);
        })) {
      return {.ok = false, .message = "Mesh vertex data must be finite"};
    }
  }
  if (geometry.indices.size() % 3u != 0u) {
    return {.ok = false,
            .message = "Mesh indices must describe complete triangles"};
  }
  for (const std::uint32_t index : geometry.indices) {
    if (index >= geometry.vertices.size()) {
      return {.ok = false,
              .message = "Mesh index references a missing vertex"};
    }
  }

  if (mesh.has_authored_polygon_document !=
      static_cast<bool>(mesh.authored_polygon_document)) {
    return {.ok = false,
            .message = "Mesh authored polygon document state is inconsistent"};
  }
  if (mesh.authored_polygon_document) {
    for (const quader_poly::Vertex &vertex :
         mesh.authored_polygon_document->vertices) {
      if (!finite_vec3(vertex.position)) {
        return {.ok = false,
                .message =
                    "Mesh authored polygon vertex data must be finite"};
      }
    }
    for (const quader_poly::Face &face :
         mesh.authored_polygon_document->faces) {
      if (!quader_poly::valid_face_texture_transform(face.texture_transform)) {
        return {.ok = false,
                .message = "Mesh authored face texture transform is invalid"};
      }
      for (const QVec2 uv : face.uvs) {
        if (!finite_vec2(uv)) {
          return {.ok = false,
                  .message = "Mesh authored polygon UV data must be finite"};
        }
      }
    }
  }
  return {};
}

AssetRegistryValidationResult
AssetRegistry::validate_material(const MaterialDocument &material) const {
  if (!is_valid_material_diffuse_model(material.diffuse_model)) {
    return {.ok = false, .message = "Material diffuse model is unsupported"};
  }
  if (!std::isfinite(material.base_color.x) ||
      !std::isfinite(material.base_color.y) ||
      !std::isfinite(material.base_color.z) ||
      !std::isfinite(material.roughness) ||
      !std::isfinite(material.metallic)) {
    return {.ok = false, .message = "Material values must be finite"};
  }
  for (const auto &[slot, texture] : material.texture_slots) {
    if (slot.empty()) {
      return {.ok = false, .message = "Material texture slot is empty"};
    }
    if (!material_texture_slot_supported(slot)) {
      return {.ok = false, .message = "Unsupported material texture slot"};
    }
    if (texture == kInvalidAssetId || textures_.get(texture) == nullptr) {
      return {.ok = false, .message = "Texture asset does not exist"};
    }
  }
  return {};
}

bool AssetRegistry::update_texture(
    AssetId id, const std::function<void(TextureAsset &)> &update) {
  assert_owner_thread();
  const bool affects_material = material_references_texture(id);
  if (!textures_.update(id, update)) {
    return false;
  }
  if (affects_material) {
    mark_material_dirty();
  } else {
    mark_dirty();
  }
  return true;
}

const MeshAsset* AssetRegistry::mesh(AssetId id) const {
  assert_owner_thread();
  const auto deferred = deferred_meshes_.find(id);
  if (deferred == deferred_meshes_.end()) {
    return meshes_.get(id);
  }
  const MeshAsset *descriptor = meshes_.get(id);
  if (descriptor == nullptr) {
    deferred_meshes_.erase(deferred);
    return nullptr;
  }

  MeshAsset materialized = deferred->second.loader();
  materialized.name = descriptor->name;
  materialized.has_stable_cpu_topology =
      deferred->second.has_stable_cpu_topology;
  if (materialized.has_authored_polygon_document !=
      deferred->second.has_authored_polygon_document) {
    return nullptr;
  }
  if (!validate_mesh(materialized).ok) {
    return nullptr;
  }
  materialized = isolate_mesh_asset_payload(std::move(materialized));
  intern_mesh_payload(materialized);
  const MeshAssetRevision revision = descriptor->revision;
  MeshAsset *stored = const_cast<MeshAsset *>(descriptor);
  *stored = std::move(materialized);
  stored->id = id;
  stored->revision = revision;
  deferred_meshes_.erase(deferred);
  return stored;
}

bool AssetRegistry::has_mesh(AssetId id) const { return meshes_.contains(id); }

bool AssetRegistry::mesh_materialized(AssetId id) const {
  return meshes_.contains(id) && !deferred_meshes_.contains(id);
}

std::optional<MeshAssetDescriptor>
AssetRegistry::mesh_descriptor(AssetId id) const {
  const MeshAsset *stored = meshes_.get(id);
  if (stored == nullptr) {
    return std::nullopt;
  }
  const auto deferred = deferred_meshes_.find(id);
  return MeshAssetDescriptor{
      .id = id,
      .revision = stored->revision,
      .name = stored->name,
      .has_stable_cpu_topology =
          deferred == deferred_meshes_.end()
              ? stored->has_stable_cpu_topology
              : deferred->second.has_stable_cpu_topology,
      .has_authored_polygon_document =
          deferred == deferred_meshes_.end()
              ? stored->has_authored_polygon_document
              : deferred->second.has_authored_polygon_document,
      .materialized = deferred == deferred_meshes_.end(),
  };
}

MeshAssetWriteSourceResult
AssetRegistry::mesh_write_source(AssetId id) const {
  assert_owner_thread();
  const std::optional<MeshAssetDescriptor> descriptor = mesh_descriptor(id);
  if (!descriptor.has_value()) {
    return {.ok = false, .message = "Mesh asset does not exist."};
  }

  if (const auto deferred = deferred_meshes_.find(id);
      deferred != deferred_meshes_.end() &&
      deferred->second.encoded_geometry_loader) {
    try {
      std::shared_ptr<const std::vector<std::byte>> encoded =
          deferred->second.encoded_geometry_loader();
      if (!encoded || encoded->empty()) {
        return {.ok = false,
                .message = "Deferred mesh encoded geometry is empty."};
      }
      return {
          .ok = true,
          .source = {.descriptor = *descriptor,
                     .encoded_geometry = std::move(encoded)},
      };
    } catch (const std::exception &exception) {
      return {.ok = false, .message = exception.what()};
    } catch (...) {
      return {.ok = false,
              .message = "Deferred mesh encoded geometry could not be read."};
    }
  }

  try {
    const MeshAsset *materialized = mesh(id);
    if (materialized == nullptr) {
      return {.ok = false,
              .message = "Mesh asset could not be materialized for save."};
    }
    return {
        .ok = true,
        .source = {
            .descriptor = MeshAssetDescriptor{
                .id = id,
                .revision = materialized->revision,
                .name = materialized->name,
                .has_stable_cpu_topology =
                    materialized->has_stable_cpu_topology,
                .has_authored_polygon_document =
                    materialized->has_authored_polygon_document,
                .materialized = true,
            },
            .geometry = materialized->geometry_payload(),
            .authored_polygon_document =
                materialized->authored_polygon_document,
        },
    };
  } catch (const std::exception &exception) {
    return {.ok = false, .message = exception.what()};
  } catch (...) {
    return {.ok = false,
            .message = "Mesh asset could not be materialized for save."};
  }
}

const MaterialDocument* AssetRegistry::material(AssetId id) const {
    return materials_.get(id);
}

const TextureAsset* AssetRegistry::texture(AssetId id) const {
    return textures_.get(id);
}

MeshAssetRevision AssetRegistry::mesh_revision(AssetId id) const {
  return meshes_.revision(id);
}

MaterialAssetRevision AssetRegistry::material_revision(AssetId id) const {
  return materials_.revision(id);
}

TextureAssetRevision AssetRegistry::texture_revision(AssetId id) const {
  return textures_.revision(id);
}

void AssetRegistry::intern_mesh_payload(MeshAsset &mesh) const {
  assert_owner_thread();
  std::shared_ptr<const MeshGeometryPayload> geometry = mesh.geometry_payload();
  const std::uint64_t content_id = geometry->content_id().value;
  auto &geometry_candidates = mesh_geometry_pool_[content_id];
  bool geometry_present = false;
  for (auto candidate = geometry_candidates.begin();
       candidate != geometry_candidates.end();) {
    std::shared_ptr<const MeshGeometryPayload> existing = candidate->lock();
    if (!existing) {
      candidate = geometry_candidates.erase(candidate);
      continue;
    }
    if (existing.get() != geometry.get() &&
        mesh_geometry_payload_equal(*existing, *geometry)) {
      mesh.share_geometry(existing);
      geometry = std::move(existing);
      geometry_present = true;
      break;
    }
    if (existing.get() == geometry.get()) {
      geometry_present = true;
      break;
    }
    ++candidate;
  }
  if (!geometry_present) {
    geometry_candidates.emplace_back(geometry);
  }

  if (!mesh.has_authored_polygon_document ||
      !mesh.authored_polygon_document) {
    return;
  }
  std::shared_ptr<const quader_poly::Document> authored =
      mesh.authored_polygon_document;
  const std::uint64_t authored_content_id =
      mesh_asset_payload_detail::polygon_document_hash(*authored);
  auto &authored_candidates =
      authored_topology_pool_[authored_content_id];
  bool authored_present = false;
  for (auto candidate = authored_candidates.begin();
       candidate != authored_candidates.end();) {
    std::shared_ptr<const quader_poly::Document> existing = candidate->lock();
    if (!existing) {
      candidate = authored_candidates.erase(candidate);
      continue;
    }
    if (existing.get() == authored.get()) {
      authored_present = true;
      break;
    }
    if (mesh_asset_payload_detail::same_polygon_document(*existing,
                                                         *authored)) {
      mesh.share_authored_polygon_document(existing);
      authored = std::move(existing);
      authored_present = true;
      break;
    }
    ++candidate;
  }
  if (!authored_present) {
    authored_candidates.emplace_back(authored);
  }
}

void AssetRegistry::advance_revision_for_replacement(
    std::uint64_t previous_revision) {
  assert_owner_thread();
  ++unscoped_mutation_count_;
  meshes_.restamp_after(MeshAssetRevision{previous_revision});
  curves_.restamp_after(CurveAssetRevision{previous_revision});
  materials_.restamp_after(MaterialAssetRevision{previous_revision});
  textures_.restamp_after(TextureAssetRevision{previous_revision});
  revision_ = std::max(revision_, previous_revision) + 1u;
  material_catalog_revision_ =
      std::max(material_catalog_revision_, previous_revision) + 1u;
  mesh_material_usage_revision_ =
      std::max(mesh_material_usage_revision_, previous_revision) + 1u;
}

void AssetRegistry::mark_dirty() {
  assert_owner_thread();
  ++unscoped_mutation_count_;
  ++revision_;
}

void AssetRegistry::mark_material_dirty() {
  assert_owner_thread();
  ++material_catalog_revision_;
  mark_dirty();
}

void AssetRegistry::mark_mesh_material_usage_dirty() {
  assert_owner_thread();
  ++mesh_material_usage_revision_;
  mark_dirty();
}

void AssetRegistry::note_asset_allocation(AssetId asset_id) {
  assert_owner_thread();
  assert(asset_id != kInvalidAssetId);
  last_asset_allocation_serial_[asset_id] = ++asset_allocation_serial_;
}

void AssetRegistry::mark_modeling_assets_dirty(
    std::span<const AssetId> asset_ids,
    bool mesh_material_usage_dirty) {
  assert_owner_thread();
  std::unordered_set<AssetId> unique_ids;
  unique_ids.reserve(asset_ids.size());
  for (const AssetId id : asset_ids) {
    if (id == kInvalidAssetId || !unique_ids.insert(id).second) {
      continue;
    }
    ++mutation_count_by_asset_[id];
    ++modeling_mutation_count_;
  }
  if (mesh_material_usage_dirty) {
    ++mesh_material_usage_revision_;
  }
  ++revision_;
}

bool AssetRegistry::material_references_texture(AssetId texture) const {
  if (texture == kInvalidAssetId) {
    return false;
  }
  for (const AssetId material_id : materials_.ids()) {
    const MaterialDocument *material = materials_.get(material_id);
    if (material == nullptr) {
      continue;
    }
    if (std::ranges::any_of(
            material->texture_slots,
            [texture](const auto &slot) { return slot.second == texture; })) {
      return true;
    }
  }
  return false;
}

} // namespace quader
