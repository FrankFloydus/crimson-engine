////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/scene_render_extraction.hpp"

#include "assets/curve_asset_payload.hpp"

#include "assets/asset_registry.hpp"
#include "diagnostics/profile.hpp"
#include "render/material_binding_service.hpp"
#include "render/render_signature_builder.hpp"
#include "render/overlay/curve_overlay_geometry.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/tool_preview_renderable_cache.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_mesh_occurrence_projection.hpp"
#include "scene/scene_texture_mapping.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

bool local_visibility_allows(
    const ViewportLocalVisibilitySnapshot *local_visibility,
    EntityId entity) {
  return local_visibility == nullptr || local_visibility->allows(entity);
}

RenderMaterialAssetSnapshot build_render_material_asset_snapshot(
    const AssetRegistry &assets, MaterialReference reference,
    const RenderProjectMaterialResolver *resolver);

RenderTextureAssetSnapshot build_render_texture_asset_snapshot(
    const AssetRegistry &assets, std::string_view slot, AssetId texture_id) {
  RenderTextureAssetSnapshot snapshot{
      .slot = std::string(slot),
      .texture_id = texture_id,
  };
  if (const TextureAsset *texture = assets.texture(texture_id)) {
    snapshot.texture = *texture;
    snapshot.present = true;
  }
  return snapshot;
}

RenderMaterialAssetSnapshot build_render_material_asset_snapshot(
    const AssetRegistry &assets, AssetId material_id) {
  RenderMaterialAssetSnapshot snapshot{
      .reference = MaterialReference(material_id),
      .material_id = material_id,
  };
  const MaterialDocument *material = assets.material(material_id);
  if (!material) {
    return snapshot;
  }

  snapshot.document = *material;
  snapshot.revision = material->revision.value;
  snapshot.present = true;
  for (const MaterialTextureSlotDescriptor &descriptor :
       material_texture_slot_descriptors()) {
    const std::optional<AssetId> texture_id =
        material_texture_slot_asset(*material, descriptor.id);
    if (!texture_id.has_value()) {
      continue;
    }
    snapshot.textures.push_back(build_render_texture_asset_snapshot(
        assets, descriptor.id, *texture_id));
  }
  return snapshot;
}

void add_render_material_asset_signature(
    RenderSignatureBuilder &signature,
    const RenderMaterialAssetSnapshot &material) {
  signature.add_value(static_cast<std::uint64_t>(material.reference.kind()));
  if (material.reference.project_owned()) {
    signature.add_string(material.reference.project_asset().str());
    signature.add_value(material.project_catalog_revision);
  } else {
    signature.add_value(material.material_id);
  }
  if (!material.present) {
    signature.add_value(0);
    return;
  }

  signature.add_value(material.revision);
  for (const MaterialTextureSlotDescriptor &descriptor :
       material_texture_slot_descriptors()) {
    const RenderTextureAssetSnapshot *texture =
        material.texture_for_slot(descriptor.id);
    if (texture == nullptr) {
      continue;
    }
    signature.add_string(descriptor.id);
    if (texture->project_owned()) {
      signature.add_string(texture->project_texture_id.str())
          .add_value(texture->content_revision);
    } else {
      signature.add_value(texture->texture_id)
          .add_value(texture->texture.revision.value);
    }
    signature.add_bool(texture->present);
  }
}

bool same_mesh_component(const MeshComponent &left,
                         const MeshComponent &right) {
  return left.mesh == right.mesh && left.editor_only == right.editor_only &&
         left.cast_shadows == right.cast_shadows &&
         left.double_sided == right.double_sided &&
         left.generate_collisions == right.generate_collisions &&
         left.collision_type == right.collision_type &&
         left.display_collision == right.display_collision &&
         left.body_type == right.body_type;
}

RenderSceneSnapshotDirtyState scene_snapshot_dirty_state(
    const RenderSceneSnapshot &snapshot, const RenderSceneSnapshot *previous) {
  if (previous == nullptr ||
      !render_source_dependencies_match(
          *previous, snapshot, RenderSourceDependency::SceneBinding)) {
    return {};
  }

  const RenderSourceDependencyMask changed =
      render_source_changed_dependencies(
          *previous, snapshot, kRenderSourceAllDependencies);
  return {
      .scene_changed =
          changed.contains(RenderSourceDependency::SceneRevision),
      .selection_changed =
          changed.contains(RenderSourceDependency::SelectionRevision),
      .visibility_changed =
          changed.contains(RenderSourceDependency::VisibilityRevision),
      .local_visibility_changed =
          changed.contains(RenderSourceDependency::LocalVisibilityRevision),
      .transforms_changed =
          changed.contains(RenderSourceDependency::TransformRevision),
      .texture_mappings_changed =
          changed.contains(RenderSourceDependency::TextureMappingRevision),
      .mesh_bindings_changed =
          changed.contains(RenderSourceDependency::MeshBindingRevision),
      .render_components_changed =
          changed.contains(RenderSourceDependency::RenderComponentRevision),
      .assets_changed =
          changed.contains(RenderSourceDependency::AssetRevision),
      .project_materials_changed =
          changed.contains(RenderSourceDependency::ProjectMaterialRevision),
  };
}

const RenderSceneMeshRecord *previous_mesh_record(
    const RenderSceneSnapshot *previous, EntityId entity_id) {
  if (previous == nullptr) {
    return nullptr;
  }
  const auto found = std::ranges::lower_bound(
      previous->meshes, entity_id, {}, &RenderSceneMeshRecord::entity_id);
  return found == previous->meshes.end() || found->entity_id != entity_id
             ? nullptr
             : &*found;
}

const RenderSceneLightRecord *previous_light_record(
    const RenderSceneSnapshot *previous, EntityId entity_id) {
  if (previous == nullptr) {
    return nullptr;
  }
  const auto found = std::ranges::lower_bound(
      previous->lights, entity_id, {}, &RenderSceneLightRecord::entity_id);
  return found == previous->lights.end() || found->entity_id != entity_id
             ? nullptr
             : &*found;
}

void mark_mesh_dirty(RenderSceneMeshRecord &record,
                     const RenderSceneMeshRecord *previous) {
  if (previous == nullptr) {
    return;
  }

  record.mesh_dirty = previous->mesh.mesh_id != record.mesh.mesh_id ||
                      previous->mesh.mesh_revision !=
                          record.mesh.mesh_revision ||
                      previous->mesh.present != record.mesh.present;
  record.material_dirty =
      previous->material_reference != record.material_reference ||
      previous->material_revision != record.material_revision;
  record.transform_dirty =
      previous->transform_revision != record.transform_revision;
  record.texture_mapping_dirty = previous->sampling_from_local.values !=
                                  record.sampling_from_local.values;
  record.binding_dirty =
      !same_mesh_component(previous->mesh_component, record.mesh_component);
}

void mark_light_dirty(RenderSceneLightRecord &record,
                      const RenderSceneLightRecord *previous) {
  if (previous == nullptr) {
    return;
  }

  record.light_dirty = previous->rebuild_revision != record.rebuild_revision;
  record.transform_dirty =
      previous->transform_revision != record.transform_revision;
}

[[nodiscard]] bool can_reuse_previous_render_records(
    const RenderSceneSnapshot &snapshot, const RenderSceneSnapshot *previous) {
  return previous != nullptr && render_source_dependencies_match(
                                    *previous, snapshot,
                                    kRenderSceneSnapshotSourceDependencies);
}

void reuse_previous_render_records(RenderSceneSnapshot &snapshot,
                                   const RenderSceneSnapshot &previous) {
  snapshot.meshes = previous.meshes;
  snapshot.instance_primitives = previous.instance_primitives;
  snapshot.lights = previous.lights;
  snapshot.live_mesh_entities = previous.live_mesh_entities;
  snapshot.live_light_entities = previous.live_light_entities;
  snapshot.reused_previous_records = true;
  for (RenderSceneMeshRecord &mesh : snapshot.meshes) {
    mesh.mesh_dirty = false;
    mesh.material_dirty = false;
    mesh.transform_dirty = false;
    mesh.texture_mapping_dirty = false;
    mesh.binding_dirty = false;
  }
  for (RenderSceneLightRecord &light : snapshot.lights) {
    light.light_dirty = false;
    light.transform_dirty = false;
  }
}

} // namespace

const RenderTextureAssetSnapshot *
RenderMaterialAssetSnapshot::texture_for_slot(std::string_view slot) const {
  if (reference.project_owned()) {
    const auto found =
        std::ranges::find_if(textures, [slot](const auto &texture) {
          return texture.slot == slot;
        });
    return found == textures.end() ? nullptr : &*found;
  }
  const std::optional<AssetId> texture_id =
      material_texture_slot_asset(document, slot);
  if (!texture_id.has_value()) {
    return nullptr;
  }
  const auto found =
      std::ranges::find_if(textures, [texture_id](const auto &texture) {
        return texture.texture_id == *texture_id;
      });
  return found == textures.end() ? nullptr : &*found;
}

const RenderMaterialAssetSnapshot *
RenderMaterialBindingSnapshot::material_for_slot(std::uint32_t slot) const {
  const auto override_material = std::ranges::lower_bound(
      material_slots, slot, {},
      [](const auto &entry) { return entry.first; });
  if (override_material != material_slots.end() &&
      override_material->first == slot) {
    return &override_material->second;
  }
  return present ? &primary_material : nullptr;
}

RenderMeshAssetSnapshot render_mesh_asset_snapshot(AssetId mesh_id,
                                                   const MeshAsset &asset) {
  const std::shared_ptr<const MeshGeometryPayload> geometry =
      asset.geometry_payload();
  RenderMeshAssetSnapshot snapshot{
      .mesh_id = mesh_id,
      .geometry_payload = geometry,
      .geometry_token = asset.geometry_token(),
      .geometry_content_id = asset.geometry_content_id(),
  };
  snapshot.mesh_revision = asset.revision.value;
  snapshot.vertices = geometry->vertices;
  snapshot.indices = geometry->indices;
  snapshot.primitives = geometry->primitives;
  snapshot.present = true;
  snapshot.has_stable_cpu_topology = asset.has_stable_cpu_topology;
  return snapshot;
}

std::vector<MeshPrimitiveSpan> normalized_render_mesh_primitive_spans(
    const RenderMeshAssetSnapshot &mesh) {
  std::vector<MeshPrimitiveSpan> spans;
  spans.reserve(mesh.primitives.empty() ? 1u : mesh.primitives.size());
  for (const MeshPrimitiveSpan &primitive : mesh.primitives) {
    const std::uint64_t offset = primitive.index_offset;
    const std::uint64_t count = primitive.index_count;
    if (count == 0u || offset >= mesh.indices.size() ||
        offset + count > mesh.indices.size()) {
      continue;
    }
    spans.push_back(primitive);
  }
  if (spans.empty() && !mesh.indices.empty()) {
    spans.push_back(MeshPrimitiveSpan{
        .index_offset = 0,
        .index_count = static_cast<std::uint32_t>(mesh.indices.size()),
        .material_slot = 0,
    });
  }
  return spans;
}

std::vector<MeshPrimitiveSpan> coalesced_render_mesh_primitive_spans(
    const RenderMeshAssetSnapshot &mesh) {
  const std::vector<MeshPrimitiveSpan> normalized =
      normalized_render_mesh_primitive_spans(mesh);
  std::vector<MeshPrimitiveSpan> spans;
  spans.reserve(normalized.size());
  for (const MeshPrimitiveSpan &primitive : normalized) {
    if (!spans.empty()) {
      MeshPrimitiveSpan &previous = spans.back();
      const std::uint64_t previous_end =
          static_cast<std::uint64_t>(previous.index_offset) +
          static_cast<std::uint64_t>(previous.index_count);
      const std::uint64_t merged_count =
          static_cast<std::uint64_t>(previous.index_count) +
          static_cast<std::uint64_t>(primitive.index_count);
      if (previous.material_slot == primitive.material_slot &&
          previous_end == primitive.index_offset &&
          merged_count <= static_cast<std::uint64_t>(
                              std::numeric_limits<std::uint32_t>::max())) {
        previous.index_count = static_cast<std::uint32_t>(merged_count);
        continue;
      }
    }
    spans.push_back(primitive);
  }
  return spans;
}

RenderMaterialBindingSnapshot build_render_material_binding_snapshot(
    const AssetRegistry &assets, const MaterialComponent *material,
    const RenderProjectMaterialResolver *project_material_resolver) {
  RenderMaterialBindingSnapshot snapshot;
  if (!material) {
    return snapshot;
  }

  snapshot.component = *material;
  snapshot.primary_material = build_render_material_asset_snapshot(
      assets, material->material, project_material_resolver);
  snapshot.present = true;

  std::vector<std::uint32_t> slots;
  slots.reserve(material->material_slots.size());
  for (const auto &[slot, material_id] : material->material_slots) {
    (void)material_id;
    slots.push_back(slot);
  }
  std::ranges::sort(slots);
  snapshot.material_slots.reserve(slots.size());
  for (const std::uint32_t slot : slots) {
    snapshot.material_slots.emplace_back(
        slot, build_render_material_asset_snapshot(
                  assets, material->material_slots.at(slot),
                  project_material_resolver));
  }

  snapshot.revision = render_material_binding_signature(snapshot);
  return snapshot;
}

std::uint64_t render_material_binding_signature(
    const RenderMaterialBindingSnapshot &material) {
  if (!material.present) {
    return 0;
  }

  RenderSignatureBuilder signature;
  add_render_material_asset_signature(signature, material.primary_material);
  signature.add_size(material.material_slots.size());
  for (const auto &[slot, material_snapshot] : material.material_slots) {
    signature.add_value(slot);
    add_render_material_asset_signature(signature, material_snapshot);
  }
  return signature.value();
}

std::uint64_t render_material_payload_signature(
    const RenderMaterialAssetSnapshot *material);
RenderInstanceBounds mesh_instance_bounds(
    const RenderMeshAssetSnapshot &mesh);

namespace {

bool same_float_payload(float left, float right) {
  return std::bit_cast<std::uint32_t>(left) ==
         std::bit_cast<std::uint32_t>(right);
}

bool same_vec2_payload(QVec2 left, QVec2 right) {
  return same_float_payload(left.x, right.x) &&
         same_float_payload(left.y, right.y);
}

bool same_vec3_payload(QVec3 left, QVec3 right) {
  return same_float_payload(left.x, right.x) &&
         same_float_payload(left.y, right.y) &&
         same_float_payload(left.z, right.z);
}

bool same_render_texture_payload(
    const RenderTextureAssetSnapshot &left,
    const RenderTextureAssetSnapshot &right) {
  const std::uint32_t left_width =
      left.width != 0u ? left.width : left.texture.width;
  const std::uint32_t right_width =
      right.width != 0u ? right.width : right.texture.width;
  const std::uint32_t left_height =
      left.height != 0u ? left.height : left.texture.height;
  const std::uint32_t right_height =
      right.height != 0u ? right.height : right.texture.height;
  return left.slot == right.slot && left.present == right.present &&
         left.content_revision == right.content_revision &&
         left.texture.revision.value == right.texture.revision.value &&
         left.texture.usage == right.texture.usage &&
         left.texture.color_space == right.texture.color_space &&
         left_width == right_width && left_height == right_height &&
         left.texture.source_path.generic_string() ==
             right.texture.source_path.generic_string() &&
         left.encoded_file_bytes.get() == right.encoded_file_bytes.get() &&
         left.decoded_rgba8.get() == right.decoded_rgba8.get();
}

bool same_render_material_payload(
    const RenderMaterialAssetSnapshot *left,
    const RenderMaterialAssetSnapshot *right) {
  const bool left_present = left != nullptr && left->present;
  const bool right_present = right != nullptr && right->present;
  if (!left_present || !right_present) {
    return left_present == right_present;
  }
  if (!same_vec3_payload(left->document.base_color,
                         right->document.base_color) ||
      !same_float_payload(left->document.roughness,
                          right->document.roughness) ||
      !same_float_payload(left->document.metallic,
                          right->document.metallic) ||
      !same_float_payload(left->normal_strength, right->normal_strength) ||
      left->directx_normal != right->directx_normal ||
      !same_vec2_payload(left->texture_transform.center,
                         right->texture_transform.center) ||
      !same_vec2_payload(left->texture_transform.scale,
                         right->texture_transform.scale) ||
      !same_vec2_payload(left->texture_transform.offset,
                         right->texture_transform.offset) ||
      !same_float_payload(left->texture_transform.rotation_degrees,
                          right->texture_transform.rotation_degrees) ||
      left->revision != right->revision ||
      left->textures.size() != right->textures.size()) {
    return false;
  }
  for (std::size_t index = 0u; index < left->textures.size(); ++index) {
    if (!same_render_texture_payload(left->textures[index],
                                     right->textures[index])) {
      return false;
    }
  }
  return true;
}

/// Summarizes one primitive's material facts for instance compatibility.
struct OrdinaryMeshPrimitiveCompatibilityDescriptor final {
  MeshPrimitiveSpan primitive;
  std::uint64_t material_payload_signature = 0u;
  const RenderMaterialAssetSnapshot *material = nullptr;
};

/// Summarizes one ordinary mesh for stable instance-group matching.
struct OrdinaryMeshCompatibilityDescriptor final {
  const MeshGeometryPayload *geometry_payload = nullptr;
  MeshGeometryContentId geometry_content_id;
  std::vector<OrdinaryMeshPrimitiveCompatibilityDescriptor> primitives;
  std::uint64_t signature = 0u;
  bool cast_shadows = false;
  bool double_sided = false;
  bool eligible = false;
};

OrdinaryMeshCompatibilityDescriptor
ordinary_mesh_compatibility_descriptor(
    const RenderSceneMeshRecord &mesh) {
  OrdinaryMeshCompatibilityDescriptor descriptor{
      .geometry_payload = mesh.mesh.geometry_payload.get(),
      .geometry_content_id = mesh.mesh.geometry_content_id,
      .cast_shadows = mesh.cast_shadows(),
      .double_sided = mesh.double_sided(),
      .eligible = mesh.renderable() &&
                  mesh.mesh.geometry_content_id.value != 0u &&
                  mesh.mesh.geometry_payload != nullptr,
  };
  if (!mesh.renderable()) {
    return descriptor;
  }

  const std::vector<MeshPrimitiveSpan> primitives =
      coalesced_render_mesh_primitive_spans(mesh.mesh);
  descriptor.primitives.reserve(primitives.size());
  RenderSignatureBuilder signature;
  if (descriptor.eligible) {
    signature
        .add_value(mesh.mesh.geometry_content_id.value)
        .add_value(static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(
                descriptor.geometry_payload)))
        .add_bool(descriptor.cast_shadows)
        .add_bool(descriptor.double_sided)
        .add_size(primitives.size());
  }
  for (const MeshPrimitiveSpan &primitive : primitives) {
    const RenderMaterialAssetSnapshot *material =
        mesh.material.material_for_slot(primitive.material_slot);
    const std::uint64_t material_signature =
        render_material_payload_signature(material);
    descriptor.primitives.push_back({
        .primitive = primitive,
        .material_payload_signature = material_signature,
        .material = material,
    });
    if (descriptor.eligible) {
      signature.add_value(primitive.index_offset)
          .add_value(primitive.index_count)
          .add_value(primitive.material_slot)
          .add_value(material_signature);
    }
  }
  if (descriptor.eligible) {
    descriptor.signature = signature.value();
  }
  return descriptor;
}

bool ordinary_mesh_descriptors_are_compatible(
    const OrdinaryMeshCompatibilityDescriptor &left,
    const OrdinaryMeshCompatibilityDescriptor &right) {
  if (!left.eligible || !right.eligible ||
      left.geometry_content_id != right.geometry_content_id ||
      left.geometry_payload != right.geometry_payload ||
      left.cast_shadows != right.cast_shadows ||
      left.double_sided != right.double_sided ||
      left.primitives.size() != right.primitives.size()) {
    return false;
  }
  for (std::size_t index = 0u; index < left.primitives.size(); ++index) {
    const OrdinaryMeshPrimitiveCompatibilityDescriptor &left_primitive =
        left.primitives[index];
    const OrdinaryMeshPrimitiveCompatibilityDescriptor &right_primitive =
        right.primitives[index];
    if (left_primitive.primitive != right_primitive.primitive ||
        left_primitive.material_payload_signature !=
            right_primitive.material_payload_signature ||
        !same_render_material_payload(left_primitive.material,
                                      right_primitive.material)) {
      return false;
    }
  }
  return true;
}

/// Preserves one representative and its compatible meshes in source order.
struct OrdinaryMeshCompatibilityGroup final {
  std::size_t representative = 0u;
  std::vector<std::size_t> additional_members;
};

std::vector<OrdinaryMeshCompatibilityGroup>
stable_group_ordinary_meshes(
    std::span<const OrdinaryMeshCompatibilityDescriptor> descriptors) {
  std::vector<OrdinaryMeshCompatibilityGroup> groups;
  groups.reserve(descriptors.size());
  std::unordered_multimap<std::uint64_t, std::size_t> groups_by_signature;
  groups_by_signature.reserve(descriptors.size());

  for (std::size_t mesh_index = 0u; mesh_index < descriptors.size();
       ++mesh_index) {
    const OrdinaryMeshCompatibilityDescriptor &descriptor =
        descriptors[mesh_index];
    std::optional<std::size_t> compatible_group;
    if (descriptor.eligible) {
      const auto [bucket_begin, bucket_end] =
          groups_by_signature.equal_range(descriptor.signature);
      for (auto candidate = bucket_begin; candidate != bucket_end;
           ++candidate) {
        const std::size_t group_index = candidate->second;
        if (ordinary_mesh_descriptors_are_compatible(
                descriptors[groups[group_index].representative],
                descriptor)) {
          compatible_group = group_index;
          break;
        }
      }
    }

    if (compatible_group.has_value()) {
      groups[*compatible_group].additional_members.push_back(mesh_index);
      continue;
    }

    const std::size_t group_index = groups.size();
    groups.push_back({.representative = mesh_index});
    if (descriptor.eligible) {
      groups_by_signature.emplace(descriptor.signature, group_index);
    }
  }
  return groups;
}

void append_ordinary_instance_primitive_records(
    RenderSceneSnapshot &snapshot, const RenderSceneMeshRecord &source,
    const OrdinaryMeshCompatibilityDescriptor &compatibility,
    std::span<const RenderSceneInstanceOccurrenceRecord> occurrences);

void add_material_reference_signature(
    RenderSignatureBuilder &signature, MaterialReference reference) {
  signature.add_value(static_cast<std::uint64_t>(reference.kind()));
  if (reference.project_owned()) {
    signature.add_string(reference.project_asset().str());
  } else {
    signature.add_value(reference.embedded_asset());
  }
}

std::uint64_t
preview_material_component_signature(const MaterialComponent &material) {
  RenderSignatureBuilder signature;
  add_material_reference_signature(signature, material.material);
  std::vector<std::pair<std::uint32_t, MaterialReference>> slots;
  slots.reserve(material.material_slots.size());
  for (const auto &[slot, reference] : material.material_slots) {
    slots.emplace_back(slot, reference);
  }
  std::ranges::sort(
      slots, {}, [](const auto &entry) { return entry.first; });
  signature.add_size(slots.size());
  for (const auto &[slot, reference] : slots) {
    signature.add_value(slot);
    add_material_reference_signature(signature, reference);
  }
  return signature.value();
}

} // namespace

std::uint64_t instance_occurrence_token(EntityId definition,
                                        EntityId member,
                                        EntityId occurrence);
bool selected(const SceneDocument &document, EntityId entity);
void append_instance_primitive_records(
    RenderSceneSnapshot &snapshot, const RenderSceneMeshRecord &source,
    EntityId definition, EntityId member, const QMat4 &member_from_definition,
    std::span<const RenderSceneInstanceOccurrenceRecord> occurrences);
RenderSceneMeshRecord make_render_scene_mesh_record(
    const SceneDocument &document, const AssetRegistry &assets,
    EntityId entity_id, const MeshComponent &component,
    const RenderProjectMaterialResolver *project_material_resolver,
    const RenderSceneSnapshot *previous);

RenderSceneSnapshot build_render_scene_snapshot(
    const SceneDocument &document, const AssetRegistry &assets,
    RenderSceneSnapshotBuildOptions options) {
  QDR_PROFILE_FUNCTION();
  RenderSceneSnapshot snapshot;
  snapshot.frame_index = options.frame_index;
  const std::uint64_t project_material_revision =
      options.project_material_revision != 0u
          ? options.project_material_revision
          : options.project_material_resolver != nullptr
                ? options.project_material_resolver->revision()
                : 0u;
  static_cast<RenderSourceStamp &>(snapshot) = capture_render_source_stamp(
      document, assets,
      {
          .scene_binding = options.scene_binding,
          .local_visibility = options.local_visibility,
          .project_material_revision = project_material_revision,
          .instance_edit_scope = options.instance_edit_scope,
      });
  const RenderSceneSnapshot *const previous =
      options.previous != nullptr &&
              render_source_dependencies_match(
                  *options.previous, snapshot,
                  RenderSourceDependency::SceneBinding)
          ? options.previous
          : nullptr;
  snapshot.dirty = scene_snapshot_dirty_state(snapshot, previous);
  if (can_reuse_previous_render_records(snapshot, previous)) {
    reuse_previous_render_records(snapshot, *previous);
    return snapshot;
  }

  const std::shared_ptr<const SceneMeshOccurrenceProjectionSnapshot>
      mesh_projection = document.mesh_occurrence_projection();
  std::vector<RenderSceneMeshRecord> ordinary_meshes;
  ordinary_meshes.reserve(
      mesh_projection->ordinary_placement_indices().size());
  for (const std::size_t index :
       mesh_projection->ordinary_placement_indices()) {
    const SceneMeshOccurrenceProjectionRecord *placement =
        mesh_projection->record(index);
    if (placement == nullptr ||
        !local_visibility_allows(options.local_visibility,
                                 placement->display_entity)) {
      continue;
    }
    const MeshComponent *component =
        document.mesh(placement->geometry_entity);
    if (component != nullptr) {
      ordinary_meshes.push_back(make_render_scene_mesh_record(
          document, assets, placement->geometry_entity, *component,
          options.project_material_resolver, previous));
    }
  }

  for (const EntityId entity_id : document.entities()) {
    QDR_PROFILE_SCOPE("Scene render extract entity");
    if (!document.is_effectively_visible(entity_id) ||
        !local_visibility_allows(options.local_visibility, entity_id)) {
      continue;
    }

    // Definition storage and occurrence leaves contribute meshes only through
    // the canonical placement projection; they do not publish direct lights.
    if (document.is_instance_definition_entity(entity_id) ||
        document.is_instance_definition_member(entity_id) ||
        document.instance_occurrence(entity_id) != nullptr) {
      continue;
    }
    const QMat4 world_transform = document.world_transform_matrix(entity_id);

    if (const LightComponent *component = document.light(entity_id)) {
      snapshot.live_light_entities.insert(entity_id);
      RenderSceneLightRecord entry{
          .entity_id = entity_id,
          .light = *component,
          .world_transform = world_transform,
          .rebuild_revision =
              RenderSignatureBuilder::light_signature(*component),
          .transform_revision =
              RenderSignatureBuilder::light_transform_signature(
                  world_transform),
      };
      mark_light_dirty(entry, previous_light_record(previous, entity_id));
      snapshot.lights.push_back(entry);
    }
  }

  // Preserve the legacy single-mesh cache for unique payloads, but route two
  // or more fully compatible ordinary objects through explicit GPU instancing.
  // Compatibility is payload-based and deliberately excludes scene/asset IDs.
  // Build each exact descriptor once. The hash only selects candidates; exact
  // equality keeps collisions from merging incompatible render payloads.
  std::vector<OrdinaryMeshCompatibilityDescriptor>
      ordinary_compatibility;
  ordinary_compatibility.reserve(ordinary_meshes.size());
  for (const RenderSceneMeshRecord &mesh : ordinary_meshes) {
    ordinary_compatibility.push_back(
        ordinary_mesh_compatibility_descriptor(mesh));
  }
  const std::vector<OrdinaryMeshCompatibilityGroup> ordinary_groups =
      stable_group_ordinary_meshes(ordinary_compatibility);
  for (const OrdinaryMeshCompatibilityGroup &group : ordinary_groups) {
    const std::size_t first = group.representative;
    if (group.additional_members.empty() &&
        !options.instance_edit_scope.active()) {
      RenderSceneMeshRecord &record = ordinary_meshes[first];
      if (record.renderable()) {
        snapshot.live_mesh_entities.insert(record.entity_id);
      }
      snapshot.meshes.push_back(std::move(record));
      continue;
    }

    std::vector<RenderSceneInstanceOccurrenceRecord> occurrences;
    occurrences.reserve(group.additional_members.size() + 1u);
    const auto append_occurrence = [&](std::size_t index) {
      RenderSceneMeshRecord &record = ordinary_meshes[index];
      occurrences.push_back({
          .entity_id = record.entity_id,
          .occurrence = instance_occurrence_token(
              kInvalidEntityId, record.entity_id, record.entity_id),
          .world_transform = record.world_transform,
          .sampling_from_local = record.sampling_from_local,
          .selected = selected(document, record.entity_id),
          .dimmed = options.instance_edit_scope.active(),
      });
    };
    append_occurrence(first);
    for (const std::size_t member : group.additional_members) {
      append_occurrence(member);
    }
    append_ordinary_instance_primitive_records(
        snapshot, ordinary_meshes[first], ordinary_compatibility[first],
        occurrences);
  }

  // Geometry groups and participant transforms come from the scene-owned
  // occurrence projection. Material and texture sampling remain render facts.
  for (const SceneMeshOccurrenceGeometryGroup &group :
       mesh_projection->formal_geometry_groups()) {
    const MeshComponent *component = document.mesh(group.geometry_entity);
    if (component == nullptr) {
      continue;
    }
    RenderSceneMeshRecord source = make_render_scene_mesh_record(
        document, assets, group.geometry_entity, *component,
        options.project_material_resolver, nullptr);
    std::vector<RenderSceneInstanceOccurrenceRecord> occurrences;
    occurrences.reserve(group.participant_indices.size());
    for (const std::size_t index : group.participant_indices) {
      const SceneMeshOccurrenceProjectionRecord *placement =
          mesh_projection->record(index);
      if (placement == nullptr ||
          !local_visibility_allows(options.local_visibility,
                                   placement->display_entity)) {
        continue;
      }
      occurrences.push_back({
          .entity_id = placement->display_entity,
          .occurrence = instance_occurrence_token(
              group.definition_entity, group.geometry_entity,
              placement->display_entity),
          .world_transform = placement->world_from_geometry,
          .sampling_from_local =
              effective_texture_sampling_from_local(
                  document, group.geometry_entity, placement->display_entity)
                  .value_or(QMat4{}),
          .selected = selected(document, placement->display_entity),
          .dimmed = options.instance_edit_scope.active() &&
                    placement->display_entity !=
                        options.instance_edit_scope.occurrence,
      });
    }
    if (!occurrences.empty()) {
      append_instance_primitive_records(
          snapshot, source, group.definition_entity, group.geometry_entity,
          group.definition_from_geometry, occurrences);
    }
  }

  return snapshot;
}

RenderInstanceBounds mesh_instance_bounds(
    const RenderMeshAssetSnapshot &mesh) {
  if (mesh.vertices.empty()) {
    return {};
  }
  RenderInstanceBounds bounds{
      .min = {std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max()},
      .max = {std::numeric_limits<float>::lowest(),
              std::numeric_limits<float>::lowest(),
              std::numeric_limits<float>::lowest()},
      .present = true,
  };
  for (const MeshVertex &vertex : mesh.vertices) {
    bounds.min.x = std::min(bounds.min.x, vertex.position.x);
    bounds.min.y = std::min(bounds.min.y, vertex.position.y);
    bounds.min.z = std::min(bounds.min.z, vertex.position.z);
    bounds.max.x = std::max(bounds.max.x, vertex.position.x);
    bounds.max.y = std::max(bounds.max.y, vertex.position.y);
    bounds.max.z = std::max(bounds.max.z, vertex.position.z);
  }
  return bounds;
}

std::uint64_t render_material_payload_signature(
    const RenderMaterialAssetSnapshot *material) {
  if (material == nullptr || !material->present) {
    return 0u;
  }
  RenderSignatureBuilder signature;
  signature.add_vec3(material->document.base_color)
      .add_float(material->document.roughness)
      .add_float(material->document.metallic)
      .add_float(material->normal_strength)
      .add_bool(material->directx_normal)
      .add_vec2(material->texture_transform.center)
      .add_vec2(material->texture_transform.scale)
      .add_vec2(material->texture_transform.offset)
      .add_float(material->texture_transform.rotation_degrees)
      .add_value(material->revision)
      .add_size(material->textures.size());
  for (const RenderTextureAssetSnapshot &texture : material->textures) {
    signature.add_string(texture.slot)
        .add_bool(texture.present)
        .add_value(texture.content_revision)
        .add_value(texture.texture.revision.value)
        .add_value(static_cast<std::uint64_t>(texture.texture.usage))
        .add_value(static_cast<std::uint64_t>(texture.texture.color_space))
        .add_value(texture.width != 0u ? texture.width : texture.texture.width)
        .add_value(texture.height != 0u ? texture.height
                                       : texture.texture.height)
        .add_string(texture.texture.source_path.generic_string())
        .add_value(reinterpret_cast<std::uintptr_t>(
            texture.encoded_file_bytes.get()))
        .add_value(reinterpret_cast<std::uintptr_t>(
            texture.decoded_rgba8.get()));
  }
  return signature.value();
}

namespace {

void append_instance_primitive_record(
    RenderSceneSnapshot &snapshot, const RenderSceneMeshRecord &source,
    EntityId definition, EntityId member, const QMat4 &member_from_definition,
    const MeshPrimitiveSpan &primitive, std::uint64_t material_payload,
    const RenderInstanceBounds &local_bounds,
    std::span<const RenderSceneInstanceOccurrenceRecord> occurrences) {
  snapshot.instance_primitives.push_back({
      .definition_entity = definition,
      .member_entity = member,
      .member_from_definition = member_from_definition,
      .key = {.geometry_payload = static_cast<std::uint64_t>(
                  source.mesh.geometry_token),
              .material_payload = material_payload,
              .index_offset = primitive.index_offset,
              .index_count = primitive.index_count,
              .cast_shadows = source.cast_shadows(),
              .receive_shadows = true,
              .double_sided = source.double_sided()},
      .mesh = source.mesh,
      .primitive = primitive,
      .material = source.material,
      .local_bounds = local_bounds,
      .occurrences = {occurrences.begin(), occurrences.end()},
  });
}

void append_ordinary_instance_primitive_records(
    RenderSceneSnapshot &snapshot, const RenderSceneMeshRecord &source,
    const OrdinaryMeshCompatibilityDescriptor &compatibility,
    std::span<const RenderSceneInstanceOccurrenceRecord> occurrences) {
  if (!source.renderable() || occurrences.empty()) {
    return;
  }
  const RenderInstanceBounds local_bounds = mesh_instance_bounds(source.mesh);
  for (const OrdinaryMeshPrimitiveCompatibilityDescriptor &primitive :
       compatibility.primitives) {
    append_instance_primitive_record(
        snapshot, source, kInvalidEntityId, kInvalidEntityId, QMat4{},
        primitive.primitive, primitive.material_payload_signature,
        local_bounds, occurrences);
  }
}

} // namespace

std::uint64_t instance_occurrence_token(EntityId definition,
                                        EntityId member,
                                        EntityId occurrence) {
  return RenderSignatureBuilder{}
      .add_value(definition)
      .add_value(member)
      .add_value(occurrence)
      .value();
}

bool selected(const SceneDocument &document, EntityId entity) {
  const SelectionComponent *selection = document.selection(entity);
  return selection != nullptr && selection->selected;
}

void append_instance_primitive_records(
    RenderSceneSnapshot &snapshot, const RenderSceneMeshRecord &source,
    EntityId definition, EntityId member, const QMat4 &member_from_definition,
    std::span<const RenderSceneInstanceOccurrenceRecord> occurrences) {
  if (!source.renderable() || occurrences.empty()) {
    return;
  }
  const RenderInstanceBounds local_bounds = mesh_instance_bounds(source.mesh);
  for (const MeshPrimitiveSpan &primitive :
       coalesced_render_mesh_primitive_spans(source.mesh)) {
    const std::uint64_t material_payload =
        render_material_payload_signature(
            source.material.material_for_slot(primitive.material_slot));
    append_instance_primitive_record(
        snapshot, source, definition, member, member_from_definition,
        primitive, material_payload, local_bounds, occurrences);
  }
}

RenderSceneMeshRecord make_render_scene_mesh_record(
    const SceneDocument &document, const AssetRegistry &assets,
    EntityId entity_id, const MeshComponent &component,
    const RenderProjectMaterialResolver *project_material_resolver,
    const RenderSceneSnapshot *previous) {
  const MaterialComponent *scene_material = document.material(entity_id);
  const RenderMaterialBindingSnapshot material =
      build_render_material_binding_snapshot(
          assets, scene_material, project_material_resolver);
  const MeshAsset *asset = assets.mesh(component.mesh);
  const QMat4 world_transform = document.world_transform_matrix(entity_id);
  RenderSceneMeshRecord entry{
      .entity_id = entity_id,
      .mesh = asset ? render_mesh_asset_snapshot(component.mesh, *asset)
                    : RenderMeshAssetSnapshot{.mesh_id = component.mesh},
      .mesh_component = component,
      .material = material,
      .material_reference =
          scene_material ? scene_material->material : MaterialReference{},
      .material_id = scene_material
                         ? scene_material->material.embedded_asset()
                         : kInvalidAssetId,
      .material_revision = material.revision,
      .world_transform = world_transform,
      .sampling_from_local = effective_texture_sampling_from_local(
                                 document, entity_id)
                                 .value_or(QMat4{}),
      .transform_revision =
          RenderSignatureBuilder::light_transform_signature(world_transform),
  };
  mark_mesh_dirty(entry, previous_mesh_record(previous, entity_id));
  return entry;
}

namespace {

RenderTextureAssetSnapshot build_render_project_texture_snapshot(
    const RenderMaterialTextureEvaluationSnapshot &texture,
    std::string_view slot) {
  RenderTextureAssetSnapshot snapshot{
      .slot = std::string(slot),
      .project_texture_id = texture.project_texture_id,
      .content_revision = texture.content_revision,
      .encoded_file_bytes = texture.encoded_file_bytes,
      .decoded_rgba8 = texture.decoded_rgba8,
      .width = texture.width,
      .height = texture.height,
      .present = texture.project_texture_id.valid() && texture.present(),
  };
  snapshot.texture.color_space = texture.color_space;
  return snapshot;
}

RenderMaterialAssetSnapshot build_render_project_material_asset_snapshot(
    MaterialReference reference,
    const RenderProjectMaterialResolver *resolver) {
  RenderMaterialAssetSnapshot snapshot{
      .reference = reference,
  };
  if (!reference.project_owned() || resolver == nullptr) {
    return snapshot;
  }

  const std::optional<RenderProjectMaterialSnapshot> resolved =
      resolver->resolve(reference.project_asset());
  if (!resolved.has_value() ||
      resolved->evaluation.project_material_id != reference.project_asset()) {
    return snapshot;
  }

  const RenderStandardMaterialEvaluationSnapshot *standard =
      resolved->evaluation.standard_material();
  if (standard == nullptr) {
    return snapshot;
  }
  snapshot.document.base_color = standard->base_color;
  snapshot.document.roughness = standard->roughness;
  snapshot.document.metallic = standard->metallic;
  snapshot.normal_strength = standard->normal_strength;
  snapshot.directx_normal = standard->directx_normal;
  snapshot.texture_transform = standard->texture_transform;
  snapshot.revision = resolved->evaluation.content_revision;
  snapshot.project_catalog_revision = resolved->catalog_revision;
  snapshot.present = true;
  /// Maps each typed Standard sampler to the existing render binding slot.
  struct TextureBinding final {
    RenderStandardMaterialTextureSlot slot;
    std::string_view render_slot;
  };
  constexpr std::array<TextureBinding, 4> kTextureBindings{
      TextureBinding{RenderStandardMaterialTextureSlot::Albedo,
                     material_texture_slot::kBaseColor},
      TextureBinding{RenderStandardMaterialTextureSlot::Roughness,
                     material_texture_slot::kRoughness},
      TextureBinding{RenderStandardMaterialTextureSlot::Metallic,
                     material_texture_slot::kMetallic},
      TextureBinding{RenderStandardMaterialTextureSlot::Normal,
                     material_texture_slot::kNormal},
  };
  for (const TextureBinding &binding : kTextureBindings) {
    const RenderMaterialTextureEvaluationSnapshot &texture =
        standard->texture(binding.slot);
    if (texture.present()) {
      snapshot.textures.push_back(build_render_project_texture_snapshot(
          texture, binding.render_slot));
    }
  }
  return snapshot;
}

RenderMaterialAssetSnapshot build_render_material_asset_snapshot(
    const AssetRegistry &assets, MaterialReference reference,
    const RenderProjectMaterialResolver *resolver) {
  if (reference.project_owned()) {
    return build_render_project_material_asset_snapshot(reference, resolver);
  }
  return build_render_material_asset_snapshot(assets,
                                               reference.embedded_asset());
}

} // namespace

bool try_refresh_render_scene_snapshot_curve_publication(
    RenderSceneSnapshot &snapshot, const SceneDocument &document,
    const AssetRegistry &assets, std::uint64_t frame_index,
    RenderSceneBindingKey scene_binding,
    std::uint64_t project_material_revision,
    RenderInstanceEditScope instance_edit_scope,
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  const RenderSourceStamp current = capture_render_source_stamp(
      document, assets,
      {
          .scene_binding = scene_binding,
          .local_visibility = local_visibility,
          .project_material_revision = project_material_revision,
          .instance_edit_scope = instance_edit_scope,
      });
  if (!render_source_dependencies_match(
          snapshot, current,
          kRenderSceneCurveRefreshStableDependencies) ||
      !render_source_dependencies_advanced_together(
          snapshot, current,
          kRenderSceneCurveRefreshAdvanceDependencies)) {
    return false;
  }

  QDR_PROFILE_SCOPE("Render scene curve-only snapshot refresh");
  const std::uint64_t previous_scene_revision = snapshot.scene_revision;
  snapshot.frame_index = frame_index;
  static_cast<RenderSourceStamp &>(snapshot) = current;
  snapshot.dirty = {
      .scene_changed = previous_scene_revision != snapshot.scene_revision,
      .selection_changed = false,
      .visibility_changed = false,
      .local_visibility_changed = false,
      .transforms_changed = false,
      .texture_mappings_changed = false,
      .mesh_bindings_changed = false,
      // Curve records live in RenderOverlaySnapshotCache. Main-scene mesh and
      // light records remain exact even though these broad public owner stamps
      // advanced.
      .render_components_changed = false,
      .assets_changed = false,
      .project_materials_changed = false,
  };
  // This is an in-place stamp refresh: the record vectors and their immutable
  // payload pins are deliberately left untouched, including their historical
  // per-record dirty annotations.
  snapshot.reused_previous_records = true;
  return true;
}

bool try_refresh_render_scene_snapshot_selection(
    RenderSceneSnapshot &snapshot, const SceneDocument &document,
    const AssetRegistry &assets, std::uint64_t frame_index,
    RenderSceneBindingKey scene_binding,
    std::uint64_t project_material_revision,
    RenderInstanceEditScope instance_edit_scope,
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  const RenderSourceStamp current = capture_render_source_stamp(
      document, assets,
      {
          .scene_binding = scene_binding,
          .local_visibility = local_visibility,
          .project_material_revision = project_material_revision,
          .instance_edit_scope = instance_edit_scope,
      });
  if (!render_source_dependencies_match(
          snapshot, current,
          kRenderSceneSelectionRefreshStableDependencies) ||
      render_source_dependencies_match(
          snapshot, current, RenderSourceDependency::SelectionRevision)) {
    return false;
  }

  QDR_PROFILE_SCOPE("Render scene selection-only snapshot refresh");
  const std::uint64_t previous_scene_revision = snapshot.scene_revision;
  const std::uint64_t previous_selection_revision =
      snapshot.selection_revision;

  snapshot.frame_index = frame_index;
  static_cast<RenderSourceStamp &>(snapshot) = current;
  snapshot.dirty = {
      .scene_changed = previous_scene_revision != snapshot.scene_revision,
      .selection_changed =
          previous_selection_revision != snapshot.selection_revision,
      .visibility_changed = false,
      .local_visibility_changed = false,
      .transforms_changed = false,
      .texture_mappings_changed = false,
      .mesh_bindings_changed = false,
      .render_components_changed = false,
      .assets_changed = false,
      .project_materials_changed = false,
  };
  snapshot.reused_previous_records = false;

  for (RenderSceneMeshRecord &record : snapshot.meshes) {
    record.mesh_dirty = false;
    record.material_dirty = false;
    record.transform_dirty = false;
    record.texture_mapping_dirty = false;
    record.binding_dirty = false;
  }
  for (RenderSceneInstancePrimitiveRecord &record :
       snapshot.instance_primitives) {
    for (RenderSceneInstanceOccurrenceRecord &occurrence :
         record.occurrences) {
      occurrence.selected = selected(document, occurrence.entity_id);
    }
  }
  for (RenderSceneLightRecord &record : snapshot.lights) {
    record.light_dirty = false;
    record.transform_dirty = false;
  }
  return true;
}

bool try_refresh_render_scene_snapshot_transforms(
    RenderSceneSnapshot &snapshot, const SceneDocument &document,
    const AssetRegistry &assets, std::uint64_t frame_index,
    RenderSceneBindingKey scene_binding,
    std::uint64_t project_material_revision,
    RenderInstanceEditScope instance_edit_scope,
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  QDR_PROFILE_FUNCTION();
  const RenderSourceStamp current = capture_render_source_stamp(
      document, assets,
      {
          .scene_binding = scene_binding,
          .local_visibility = local_visibility,
          .project_material_revision = project_material_revision,
          .instance_edit_scope = instance_edit_scope,
      });
  if (!render_source_dependencies_match(
          snapshot, current,
          kRenderSceneTransformRefreshStableDependencies) ||
      render_source_dependencies_match(
          snapshot, current,
          RenderSourceDependencyMask::from({
              RenderSourceDependency::TransformRevision,
              RenderSourceDependency::TextureMappingRevision,
          }))) {
    return false;
  }

  const std::uint64_t previous_scene_revision = snapshot.scene_revision;
  const std::uint64_t previous_selection_revision =
      snapshot.selection_revision;
  const std::uint64_t previous_visibility_revision =
      snapshot.visibility_revision;
  const std::uint64_t previous_transform_revision =
      snapshot.transform_revision;
  const std::uint64_t previous_texture_mapping_revision =
      snapshot.texture_mapping_revision;
  const std::uint64_t previous_mesh_binding_revision =
      snapshot.mesh_binding_revision;
  const std::uint64_t previous_render_component_revision =
      snapshot.render_component_revision;
  const std::uint64_t previous_asset_revision = snapshot.asset_revision;
  const std::uint64_t previous_project_material_revision =
      snapshot.project_material_revision;

  snapshot.frame_index = frame_index;
  static_cast<RenderSourceStamp &>(snapshot) = current;
  snapshot.dirty = {
      .scene_changed = previous_scene_revision != snapshot.scene_revision,
      .selection_changed =
          previous_selection_revision != snapshot.selection_revision,
      .visibility_changed =
          previous_visibility_revision != snapshot.visibility_revision,
      .local_visibility_changed = false,
      .transforms_changed =
          previous_transform_revision != snapshot.transform_revision,
      .texture_mappings_changed = previous_texture_mapping_revision !=
                                  snapshot.texture_mapping_revision,
      .mesh_bindings_changed =
          previous_mesh_binding_revision != snapshot.mesh_binding_revision,
      .render_components_changed = previous_render_component_revision !=
                                   snapshot.render_component_revision,
      .assets_changed = previous_asset_revision != snapshot.asset_revision,
      .project_materials_changed =
          previous_project_material_revision !=
          snapshot.project_material_revision,
  };
  snapshot.reused_previous_records = false;

  const std::shared_ptr<const SceneMeshOccurrenceProjectionSnapshot>
      mesh_projection = document.mesh_occurrence_projection();
  for (RenderSceneMeshRecord &record : snapshot.meshes) {
    const SceneMeshOccurrenceProjectionRecord *placement =
        mesh_projection->find({
            .display_entity = record.entity_id,
            .geometry_entity = record.entity_id,
        });
    if (placement == nullptr) {
      return false;
    }
    const QMat4 &world_transform = placement->world_from_geometry;
    const std::uint64_t transform_revision =
        RenderSignatureBuilder::light_transform_signature(world_transform);
    record.mesh_dirty = false;
    record.material_dirty = false;
    record.binding_dirty = false;
    record.transform_dirty =
        record.transform_revision != transform_revision;
    record.world_transform = world_transform;
    record.transform_revision = transform_revision;
    const QMat4 mapping = effective_texture_sampling_from_local(
                             document, record.entity_id).value_or(QMat4{});
    record.texture_mapping_dirty =
        record.sampling_from_local.values != mapping.values;
    record.sampling_from_local = mapping;
  }
  for (RenderSceneInstancePrimitiveRecord &record :
       snapshot.instance_primitives) {
    if (record.definition_entity != kInvalidEntityId) {
      const auto group = std::ranges::find_if(
          mesh_projection->formal_geometry_groups(),
          [&](const SceneMeshOccurrenceGeometryGroup &candidate) {
            return candidate.definition_entity == record.definition_entity &&
                   candidate.geometry_entity == record.member_entity;
          });
      if (group == mesh_projection->formal_geometry_groups().end()) {
        return false;
      }
      record.member_from_definition = group->definition_from_geometry;
    }
    for (RenderSceneInstanceOccurrenceRecord &occurrence :
         record.occurrences) {
      const EntityId geometry_entity =
          record.definition_entity != kInvalidEntityId
              ? record.member_entity
              : occurrence.entity_id;
      const SceneMeshOccurrenceProjectionRecord *placement =
          mesh_projection->find({
              .display_entity = occurrence.entity_id,
              .definition_entity = record.definition_entity,
              .geometry_entity = geometry_entity,
          });
      if (placement == nullptr) {
        return false;
      }
      occurrence.world_transform = placement->world_from_geometry;
      occurrence.sampling_from_local =
          record.definition_entity != kInvalidEntityId
              ? effective_texture_sampling_from_local(
                    document, record.member_entity, occurrence.entity_id)
                    .value_or(QMat4{})
              : effective_texture_sampling_from_local(
                    document, occurrence.entity_id).value_or(QMat4{});
    }
  }
  for (RenderSceneLightRecord &record : snapshot.lights) {
    const QMat4 world_transform =
        document.world_transform_matrix(record.entity_id);
    const std::uint64_t transform_revision =
        RenderSignatureBuilder::light_transform_signature(world_transform);
    record.light_dirty = false;
    record.transform_dirty =
        record.transform_revision != transform_revision;
    record.world_transform = world_transform;
    record.transform_revision = transform_revision;
  }
  return true;
}

bool render_scene_snapshot_matches_sources(
    const RenderSceneSnapshot &snapshot, const SceneDocument &document,
    const AssetRegistry &assets, RenderSceneBindingKey scene_binding,
    std::uint64_t project_material_revision,
    RenderInstanceEditScope instance_edit_scope,
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  const RenderSourceStamp current = capture_render_source_stamp(
      document, assets,
      {
          .scene_binding = scene_binding,
          .local_visibility = local_visibility,
          .project_material_revision = project_material_revision,
          .instance_edit_scope = instance_edit_scope,
      });
  return render_source_dependencies_match(
      snapshot, current, kRenderSceneSnapshotSourceDependencies);
}

SceneRenderExtractionPlan build_scene_render_extraction_plan(
    const SceneDocument &document, const AssetRegistry &assets) {
  return build_render_scene_snapshot(document, assets);
}

std::vector<RenderToolPreviewMeshRecord> build_render_tool_preview_snapshot(
    std::vector<ToolPreviewMesh> meshes, const AssetRegistry &assets,
    const ViewportLocalVisibilitySnapshot *local_visibility,
    const RenderProjectMaterialResolver *project_material_resolver) {
  QDR_PROFILE_FUNCTION();
  std::vector<RenderToolPreviewMeshRecord> records;
  records.reserve(meshes.size());
  /// Retains an exact material component and its projected render snapshot.
  struct PreviewMaterialCacheEntry final {
    MaterialComponent component;
    RenderMaterialBindingSnapshot snapshot;
  };
  std::vector<PreviewMaterialCacheEntry> material_cache;
  material_cache.reserve(
      std::min(meshes.size(), std::size_t{64u}));
  std::unordered_multimap<std::uint64_t, std::size_t>
      material_cache_by_signature;
  material_cache_by_signature.reserve(meshes.size());
  for (ToolPreviewMesh &mesh : meshes) {
    const EntityId visibility_entity =
        mesh.visibility_entity == kInvalidEntityId ? mesh.source_entity
                                                    : mesh.visibility_entity;
    if (mesh.source_entity != kInvalidEntityId &&
        !local_visibility_allows(local_visibility, visibility_entity)) {
      continue;
    }

    const std::uint64_t material_signature =
        preview_material_component_signature(mesh.material);
    std::optional<std::size_t> cached_material;
    const auto [bucket_begin, bucket_end] =
        material_cache_by_signature.equal_range(material_signature);
    for (auto candidate = bucket_begin; candidate != bucket_end;
         ++candidate) {
      if (material_cache[candidate->second].component == mesh.material) {
        cached_material = candidate->second;
        break;
      }
    }
    if (!cached_material.has_value()) {
      QDR_PROFILE_SCOPE("Tool preview build unique material snapshot");
      cached_material = material_cache.size();
      material_cache.push_back({
          .component = mesh.material,
          .snapshot = build_render_material_binding_snapshot(
              assets, &mesh.material, project_material_resolver),
      });
      material_cache_by_signature.emplace(material_signature,
                                          *cached_material);
    }
    RenderMaterialBindingSnapshot material_snapshot =
        material_cache[*cached_material].snapshot;
    records.push_back(RenderToolPreviewMeshRecord{
        .source_object_id = mesh.source_object_id,
        .source_entity = mesh.source_entity,
        .visibility_entity = visibility_entity,
        .material_reference = mesh.material.material,
        .material_id = mesh.material.material.embedded_asset(),
        .mesh = std::move(mesh.mesh),
        .material = std::move(material_snapshot),
        .material_revision = 0u,
        .transform = mesh.transform,
        .instance_transforms = std::move(mesh.instance_transforms),
        .sampling_from_local = mesh.sampling_from_local,
        .instance_sampling_from_local =
            std::move(mesh.instance_sampling_from_local),
        .double_sided = mesh.double_sided,
        .cast_shadows = mesh.cast_shadows,
        .dynamic_positions = mesh.dynamic_positions,
        .suppress_source = mesh.suppress_source,
        .layout_key = mesh.layout_key,
        .position_revision = mesh.position_revision,
        .dynamic_position_payload =
            std::move(mesh.dynamic_position_payload),
    });
    records.back().material_revision = records.back().material.revision;
  }
  return records;
}

RenderToolPreviewSceneSnapshot build_render_tool_preview_scene_snapshot(
    ToolPreviewScene preview, const AssetRegistry &assets,
    const ViewportLocalVisibilitySnapshot *local_visibility,
    const RenderProjectMaterialResolver *project_material_resolver) {
  RenderToolPreviewSceneSnapshot snapshot;
  snapshot.revision = preview.revision;
  for (const ToolPreviewMesh &mesh : preview.meshes) {
    const std::size_t transform_count =
        mesh.instance_transforms.empty() ? 1u
                                         : mesh.instance_transforms.size();
    if (!mesh.instance_sampling_from_local.empty() &&
        mesh.instance_sampling_from_local.size() != transform_count) {
      snapshot.complete = false;
    }
  }
  snapshot.meshes = build_render_tool_preview_snapshot(
      std::move(preview.meshes), assets, local_visibility,
      project_material_resolver);

  snapshot.curves.reserve(preview.curves.size());
  for (ToolPreviewCurve &curve : preview.curves) {
    const EntityId visibility_entity =
        curve.visibility_entity == kInvalidEntityId ? curve.source_entity
                                                    : curve.visibility_entity;
    if (curve.source_entity != kInvalidEntityId &&
        !local_visibility_allows(local_visibility, visibility_entity)) {
      continue;
    }
    const CurveAsset *asset = assets.curve(curve.curve);
    if (asset == nullptr || asset->document == nullptr ||
        curve.instance_transforms.empty()) {
      snapshot.complete = false;
      continue;
    }
    std::shared_ptr<const CurveSamplePayload> samples =
        curve_sample_payload(*asset);
    if (samples == nullptr || !samples->error_message.empty()) {
      snapshot.complete = false;
      continue;
    }
    snapshot.curves.push_back({
        .source_entity = curve.source_entity,
        .visibility_entity = visibility_entity,
        .curve = curve.curve,
        .curve_revision = asset->revision.value,
        .sample_payload = std::move(samples),
        .instance_transforms = std::move(curve.instance_transforms),
        .suppress_source = curve.suppress_source,
        .layout_key = curve.layout_key,
    });
  }

  snapshot.lights.reserve(preview.lights.size());
  for (ToolPreviewLight &light : preview.lights) {
    const EntityId visibility_entity =
        light.visibility_entity == kInvalidEntityId ? light.source_entity
                                                    : light.visibility_entity;
    if (light.source_entity != kInvalidEntityId &&
        !local_visibility_allows(local_visibility, visibility_entity)) {
      continue;
    }
    if (light.instance_transforms.empty()) {
      snapshot.complete = false;
      continue;
    }
    snapshot.lights.push_back({
        .source_entity = light.source_entity,
        .visibility_entity = visibility_entity,
        .light = std::move(light.light),
        .instance_transforms = std::move(light.instance_transforms),
        .suppress_source = light.suppress_source,
        .layout_key = light.layout_key,
    });
  }

  snapshot.group_markers.reserve(preview.group_markers.size());
  for (ToolPreviewGroupMarker &marker : preview.group_markers) {
    if (marker.source_entity != kInvalidEntityId &&
        !local_visibility_allows(local_visibility, marker.source_entity)) {
      continue;
    }
    if (marker.instance_transforms.empty()) {
      snapshot.complete = false;
      continue;
    }
    snapshot.group_markers.push_back({
        .source_entity = marker.source_entity,
        .local_min = marker.local_min,
        .local_max = marker.local_max,
        .instance_transforms = std::move(marker.instance_transforms),
        .layout_key = marker.layout_key,
    });
  }

  std::vector<EntityId> &entities = preview.suppressed_entities;
  std::vector<RenderInstancePreviewSuppression> &occurrences =
      preview.occurrence_suppressions;
  const auto append_suppression = [&](EntityId source, EntityId visibility) {
    if (source == kInvalidEntityId) {
      return;
    }
    if (visibility != kInvalidEntityId && visibility != source) {
      occurrences.push_back({.source_entity = source,
                             .occurrence = visibility});
    } else {
      entities.push_back(source);
    }
  };
  for (const RenderToolPreviewMeshRecord &mesh : snapshot.meshes) {
    if (mesh.suppress_source) {
      append_suppression(mesh.source_entity, mesh.visibility_entity);
    }
  }
  for (const RenderToolPreviewCurveRecord &curve : snapshot.curves) {
    if (curve.suppress_source) {
      append_suppression(curve.source_entity, curve.visibility_entity);
    }
  }
  for (const RenderToolPreviewLightRecord &light : snapshot.lights) {
    if (light.suppress_source) {
      append_suppression(light.source_entity, light.visibility_entity);
    }
  }
  std::erase(entities, kInvalidEntityId);
  std::ranges::sort(entities);
  entities.erase(std::ranges::unique(entities).begin(), entities.end());
  std::erase_if(occurrences, [](const auto &suppression) {
    return suppression.source_entity == kInvalidEntityId ||
           suppression.occurrence == kInvalidEntityId;
  });
  std::ranges::sort(
      occurrences, {}, [](const RenderInstancePreviewSuppression &value) {
        return std::pair{value.occurrence, value.source_entity};
      });
  occurrences.erase(std::ranges::unique(occurrences).begin(),
                    occurrences.end());
  RenderSignatureBuilder suppression_signature;
  suppression_signature.add_size(entities.size());
  for (const EntityId entity : entities) {
    suppression_signature.add_value(entity);
  }
  suppression_signature.add_size(occurrences.size());
  for (const RenderInstancePreviewSuppression &occurrence : occurrences) {
    suppression_signature.add_value(occurrence.source_entity)
        .add_value(occurrence.occurrence);
  }
  snapshot.suppression = {
      .entities = std::move(entities),
      .occurrences = std::move(occurrences),
      .signature = suppression_signature.value(),
  };
  return snapshot;
}

ViewportOverlayFrame build_render_tool_preview_overlay_frame(
    const RenderToolPreviewSceneSnapshot &preview) {
  ViewportOverlayFrame frame;
  for (const RenderToolPreviewCurveRecord &curve : preview.curves) {
    if (curve.sample_payload == nullptr) {
      continue;
    }
    for (const QMat4 &world_transform : curve.instance_transforms) {
      append_curve_overlay_stroke(
          frame, *curve.sample_payload, world_transform,
          ViewportOverlayLine{
              .layer = ViewportOverlayLayer::ToolPreviewLine,
              .color = kOverlayCurveColor,
              .width_pixels = kOverlayCurveLineWidthPixels,
              .style_role = ViewportOverlayStyleRole::ExplicitColor,
              .source = ViewportOverlaySource::Tool,
          });
    }
  }

  constexpr float kMarkerRadius = 0.28f;
  constexpr float kDirectionLength = 0.85f;
  for (const RenderToolPreviewLightRecord &light : preview.lights) {
    for (const QMat4 &world_transform : light.instance_transforms) {
      const QVec3 position = transform_point(world_transform, {});
      if (light.light.type == LightType::Point) {
        frame.icons.push_back({
            .layer = ViewportOverlayLayer::PointLightGizmo,
            .kind = ViewportOverlayIconKind::PointLight,
            .position = position,
            .color = rgba8(255, 255, 255, 255),
            .size_pixels = kOverlayPointLightGizmoSizePixels,
            .source = ViewportOverlaySource::Tool,
        });
        continue;
      }
      frame.points.push_back({
          .layer = ViewportOverlayLayer::ToolHandle,
          .position = position,
          .color = kOverlayToolHandleColor,
          .size_pixels = kOverlayLightMarkerPointSizePixels,
          .style_role = ViewportOverlayStyleRole::ToolHandle,
          .source = ViewportOverlaySource::Tool,
          .outline_growth_pixels = kOverlayLightMarkerOutlineGrowthPixels,
      });
      QVec3 direction =
          transform_vector(world_transform, {0.0f, -1.0f, 0.0f});
      if (length(direction) <= 0.000001f) {
        direction = {0.0f, -1.0f, 0.0f};
      } else {
        direction = normalize(direction);
      }
      const QVec3 tip = position + direction * kDirectionLength;
      frame.lines.push_back({
          .layer = ViewportOverlayLayer::ToolPreviewLine,
          .start = position,
          .end = tip,
          .color = kOverlayToolHandleColor,
          .width_pixels = kOverlayToolPreviewLineWidthPixels,
          .style_role = ViewportOverlayStyleRole::ToolPreviewLine,
          .source = ViewportOverlaySource::Tool,
      });
      if (light.light.type == LightType::Directional) {
        QVec3 tick = cross(direction, {0.0f, 1.0f, 0.0f});
        if (length(tick) <= 0.000001f) {
          tick = {1.0f, 0.0f, 0.0f};
        } else {
          tick = normalize(tick);
        }
        tick = tick * kMarkerRadius;
        frame.lines.push_back({
            .layer = ViewportOverlayLayer::ToolPreviewLine,
            .start = tip - tick,
            .end = tip + tick,
            .color = kOverlayToolHandleColor,
            .width_pixels = kOverlayToolPreviewLineWidthPixels,
            .style_role = ViewportOverlayStyleRole::ToolPreviewLine,
            .source = ViewportOverlaySource::Tool,
        });
      }
    }
  }

  constexpr std::array<std::array<std::size_t, 2>, 12> kBoundsEdges{{
      {{0u, 1u}}, {{1u, 2u}}, {{2u, 3u}}, {{3u, 0u}},
      {{4u, 5u}}, {{5u, 6u}}, {{6u, 7u}}, {{7u, 4u}},
      {{0u, 4u}}, {{1u, 5u}}, {{2u, 6u}}, {{3u, 7u}},
  }};
  for (const RenderToolPreviewGroupMarkerRecord &marker :
       preview.group_markers) {
    const std::array<QVec3, 8> local{{
        {marker.local_min.x, marker.local_min.y, marker.local_min.z},
        {marker.local_max.x, marker.local_min.y, marker.local_min.z},
        {marker.local_max.x, marker.local_max.y, marker.local_min.z},
        {marker.local_min.x, marker.local_max.y, marker.local_min.z},
        {marker.local_min.x, marker.local_min.y, marker.local_max.z},
        {marker.local_max.x, marker.local_min.y, marker.local_max.z},
        {marker.local_max.x, marker.local_max.y, marker.local_max.z},
        {marker.local_min.x, marker.local_max.y, marker.local_max.z},
    }};
    for (const QMat4 &world_transform : marker.instance_transforms) {
      frame.points.push_back({
          .layer = ViewportOverlayLayer::GroupBounds,
          .position = transform_point(world_transform, {}),
          .color = kOverlayGroupBoundsColor,
          .size_pixels = kOverlayDrawnPlanePointSizePixels,
          .style_role = ViewportOverlayStyleRole::GroupBounds,
          .source = ViewportOverlaySource::Tool,
      });
      for (const auto &edge : kBoundsEdges) {
        frame.lines.push_back({
            .layer = ViewportOverlayLayer::GroupBounds,
            .start = transform_point(world_transform, local[edge[0]]),
            .end = transform_point(world_transform, local[edge[1]]),
            .color = kOverlayGroupBoundsColor,
            .width_pixels = kOverlayGroupBoundsLineWidthPixels,
            .style_role = ViewportOverlayStyleRole::GroupBounds,
            .source = ViewportOverlaySource::Tool,
        });
      }
    }
  }
  return frame;
}

} // namespace quader
