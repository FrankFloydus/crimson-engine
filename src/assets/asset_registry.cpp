////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "assets/mesh_asset_payload.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <exception>
#include <optional>
#include <unordered_set>
#include <utility>

namespace quader {

AssetRegistry::AssetRegistry() : owner_thread_(std::this_thread::get_id()) {}

AssetRegistry::AssetRegistry(const AssetRegistry &other)
    : next_id_(other.next_id_), revision_(other.revision_),
      material_catalog_revision_(other.material_catalog_revision_),
      mesh_material_usage_revision_(other.mesh_material_usage_revision_),
      meshes_(other.meshes_), deferred_meshes_(other.deferred_meshes_),
      mesh_geometry_pool_(other.mesh_geometry_pool_),
      authored_topology_pool_(other.authored_topology_pool_),
      materials_(other.materials_), textures_(other.textures_),
      owner_thread_(std::this_thread::get_id()) {}

AssetRegistry &AssetRegistry::operator=(const AssetRegistry &other) {
  if (this == &other) {
    return *this;
  }
  assert_owner_thread();
  next_id_ = other.next_id_;
  revision_ = other.revision_;
  material_catalog_revision_ = other.material_catalog_revision_;
  mesh_material_usage_revision_ = other.mesh_material_usage_revision_;
  meshes_ = other.meshes_;
  deferred_meshes_ = other.deferred_meshes_;
  mesh_geometry_pool_ = other.mesh_geometry_pool_;
  authored_topology_pool_ = other.authored_topology_pool_;
  materials_ = other.materials_;
  textures_ = other.textures_;
  owner_thread_ = std::this_thread::get_id();
  return *this;
}

AssetRegistry::AssetRegistry(AssetRegistry &&other) noexcept
    : next_id_(other.next_id_), revision_(other.revision_),
      material_catalog_revision_(other.material_catalog_revision_),
      mesh_material_usage_revision_(other.mesh_material_usage_revision_),
      meshes_(std::move(other.meshes_)),
      deferred_meshes_(std::move(other.deferred_meshes_)),
      mesh_geometry_pool_(std::move(other.mesh_geometry_pool_)),
      authored_topology_pool_(std::move(other.authored_topology_pool_)),
      materials_(std::move(other.materials_)),
      textures_(std::move(other.textures_)),
      owner_thread_(std::this_thread::get_id()) {}

AssetRegistry &AssetRegistry::operator=(AssetRegistry &&other) noexcept {
  if (this == &other) {
    return *this;
  }
  next_id_ = other.next_id_;
  revision_ = other.revision_;
  material_catalog_revision_ = other.material_catalog_revision_;
  mesh_material_usage_revision_ = other.mesh_material_usage_revision_;
  meshes_ = std::move(other.meshes_);
  deferred_meshes_ = std::move(other.deferred_meshes_);
  mesh_geometry_pool_ = std::move(other.mesh_geometry_pool_);
  authored_topology_pool_ = std::move(other.authored_topology_pool_);
  materials_ = std::move(other.materials_);
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

AssetRegistryProjectionCheckpoint
AssetRegistry::capture_projection_checkpoint() const {
  assert_owner_thread();
  return AssetRegistryProjectionCheckpoint(
      std::make_shared<const AssetRegistry>(*this));
}

bool AssetRegistry::restore_projection_checkpoint(
    const AssetRegistryProjectionCheckpoint &checkpoint) {
  assert_owner_thread();
  if (!checkpoint.valid()) {
    return false;
  }
  *this = *checkpoint.snapshot_;
  return true;
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
  mark_mesh_material_usage_dirty();
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
  mark_mesh_material_usage_dirty();
  return id;
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
  next_id_ = std::max(next_id_, id + 1u);
  mark_mesh_material_usage_dirty();
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
  next_id_ = std::max(next_id_, id + 1u);
  mark_mesh_material_usage_dirty();
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
  mark_mesh_material_usage_dirty();
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
  mark_mesh_material_usage_dirty();
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
  mark_mesh_material_usage_dirty();
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
        !finite_vec2(vertex.uv)) {
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
  meshes_.restamp_after(MeshAssetRevision{previous_revision});
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
