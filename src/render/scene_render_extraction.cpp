////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/scene_render_extraction.hpp"

#include "assets/asset_registry.hpp"
#include "diagnostics/profile.hpp"
#include "render/material_binding_service.hpp"
#include "render/render_signature_builder.hpp"
#include "render/tool_preview_renderable_cache.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_hierarchy_visibility.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace quader {
namespace {

bool local_visibility_allows(
    const ViewportLocalVisibilitySnapshot *local_visibility,
    EntityId entity) {
  return local_visibility == nullptr || local_visibility->allows(entity);
}

std::uint64_t local_visibility_revision(
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  return local_visibility != nullptr ? local_visibility->revision() : 0u;
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
      previous->scene_binding != snapshot.scene_binding) {
    return {};
  }

  return {
      .scene_changed = previous->scene_revision != snapshot.scene_revision,
      .selection_changed =
          previous->selection_revision != snapshot.selection_revision,
      .visibility_changed =
          previous->visibility_revision != snapshot.visibility_revision,
      .local_visibility_changed = previous->local_visibility_revision !=
                                  snapshot.local_visibility_revision,
      .transforms_changed =
          previous->transform_revision != snapshot.transform_revision,
      .mesh_bindings_changed =
          previous->mesh_binding_revision != snapshot.mesh_binding_revision,
      .render_components_changed = previous->render_component_revision !=
                                   snapshot.render_component_revision,
      .assets_changed = previous->asset_revision != snapshot.asset_revision,
      .project_materials_changed = previous->project_material_revision !=
                                   snapshot.project_material_revision,
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
  return previous != nullptr &&
         previous->scene_binding == snapshot.scene_binding &&
         previous->scene_revision == snapshot.scene_revision &&
         previous->selection_revision == snapshot.selection_revision &&
         previous->visibility_revision == snapshot.visibility_revision &&
         previous->local_visibility_revision ==
             snapshot.local_visibility_revision &&
         previous->transform_revision == snapshot.transform_revision &&
         previous->mesh_binding_revision == snapshot.mesh_binding_revision &&
         previous->render_component_revision ==
             snapshot.render_component_revision &&
         previous->asset_revision == snapshot.asset_revision &&
         previous->project_material_revision ==
             snapshot.project_material_revision &&
         previous->instance_edit_scope == snapshot.instance_edit_scope;
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
  const auto override_material = std::ranges::find_if(
      material_slots, [slot](const auto &entry) { return entry.first == slot; });
  if (override_material != material_slots.end()) {
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

bool ordinary_meshes_are_instance_compatible(
    const RenderSceneMeshRecord &left,
    const RenderSceneMeshRecord &right);
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
  snapshot.scene_binding = options.scene_binding;
  snapshot.scene_revision = document.revision();
  snapshot.selection_revision = document.selection_revision();
  snapshot.visibility_revision = document.visibility_revision();
  snapshot.local_visibility_revision =
      local_visibility_revision(options.local_visibility);
  snapshot.transform_revision = document.transform_revision();
  snapshot.mesh_binding_revision = document.mesh_binding_revision();
  snapshot.render_component_revision = document.render_component_revision();
  snapshot.asset_revision = assets.revision();
  snapshot.project_material_revision =
      options.project_material_revision != 0u
          ? options.project_material_revision
          : options.project_material_resolver != nullptr
                ? options.project_material_resolver->revision()
                : 0u;
  snapshot.instance_edit_scope = options.instance_edit_scope;
  const RenderSceneSnapshot *const previous =
      options.previous != nullptr &&
              options.previous->scene_binding == snapshot.scene_binding
          ? options.previous
          : nullptr;
  snapshot.dirty = scene_snapshot_dirty_state(snapshot, previous);
  if (can_reuse_previous_render_records(snapshot, previous)) {
    reuse_previous_render_records(snapshot, *previous);
    return snapshot;
  }

  std::vector<RenderSceneMeshRecord> ordinary_meshes;
  for (const EntityId entity_id : document.entities()) {
    QDR_PROFILE_SCOPE("Scene render extract entity");
    if (!document.is_effectively_visible(entity_id) ||
        !local_visibility_allows(options.local_visibility, entity_id)) {
      continue;
    }

    // Definition roots/members are internal canonical data, and occurrence
    // leaves are rendered only through the explicit instance plan below.
    if (document.is_instance_definition_entity(entity_id) ||
        document.is_instance_definition_member(entity_id) ||
        document.instance_occurrence(entity_id) != nullptr) {
      continue;
    }

    const QMat4 world_transform = document.world_transform_matrix(entity_id);

    if (const MeshComponent *component = document.mesh(entity_id)) {
      ordinary_meshes.push_back(make_render_scene_mesh_record(
          document, assets, entity_id, *component,
          options.project_material_resolver, previous));
    }

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
  std::vector<bool> batched_ordinary(ordinary_meshes.size(), false);
  for (std::size_t first = 0u; first < ordinary_meshes.size(); ++first) {
    if (batched_ordinary[first]) {
      continue;
    }
    std::vector<std::size_t> compatible{first};
    for (std::size_t candidate = first + 1u;
         candidate < ordinary_meshes.size(); ++candidate) {
      if (!batched_ordinary[candidate] &&
          ordinary_meshes_are_instance_compatible(ordinary_meshes[first],
                                                   ordinary_meshes[candidate])) {
        compatible.push_back(candidate);
      }
    }
    if (compatible.size() < 2u && !options.instance_edit_scope.active()) {
      RenderSceneMeshRecord &record = ordinary_meshes[first];
      if (record.renderable()) {
        snapshot.live_mesh_entities.insert(record.entity_id);
      }
      snapshot.meshes.push_back(std::move(record));
      continue;
    }
    std::vector<RenderSceneInstanceOccurrenceRecord> occurrences;
    occurrences.reserve(compatible.size());
    for (const std::size_t index : compatible) {
      RenderSceneMeshRecord &record = ordinary_meshes[index];
      batched_ordinary[index] = true;
      occurrences.push_back({
          .entity_id = record.entity_id,
          .occurrence = instance_occurrence_token(
              kInvalidEntityId, record.entity_id, record.entity_id),
          .world_transform = record.world_transform,
          .selected = selected(document, record.entity_id),
          .dimmed = options.instance_edit_scope.active(),
      });
    }
    append_instance_primitive_records(
        snapshot, ordinary_meshes[first], kInvalidEntityId,
        kInvalidEntityId, QMat4{}, occurrences);
  }

  // Expand every visible occurrence from its hidden canonical definition.
  // The canonical member-to-definition transform is shared by all placements;
  // per-frame occurrence transforms remain the only mutable GPU payload.
  for (const EntityId definition : document.entities()) {
    if (!document.is_instance_definition_entity(definition)) {
      continue;
    }
    const std::optional<QMat4> definition_inverse =
        try_scene_matrix_inverse(document.world_transform_matrix(definition));
    if (!definition_inverse.has_value()) {
      continue;
    }
    std::vector<EntityId> visible_occurrences;
    for (const EntityId occurrence :
         document.instance_occurrences_for_definition(definition)) {
      if (document.is_effectively_visible(occurrence) &&
          local_visibility_allows(options.local_visibility, occurrence)) {
        visible_occurrences.push_back(occurrence);
      }
    }
    if (visible_occurrences.empty()) {
      continue;
    }
    for (const EntityId member : document.descendants(definition, false)) {
      if (document.containing_instance_definition(member) != definition ||
          !scene_hierarchy_segment_is_visible(document, member, definition)) {
        continue;
      }
      const MeshComponent *component = document.mesh(member);
      if (component == nullptr) {
        continue;
      }
      RenderSceneMeshRecord source = make_render_scene_mesh_record(
          document, assets, member, *component,
          options.project_material_resolver, nullptr);
      const QMat4 member_from_definition = scene_transform_multiply(
          *definition_inverse, document.world_transform_matrix(member));
      std::vector<RenderSceneInstanceOccurrenceRecord> occurrences;
      occurrences.reserve(visible_occurrences.size());
      for (const EntityId occurrence : visible_occurrences) {
        occurrences.push_back({
            .entity_id = occurrence,
            .occurrence =
                instance_occurrence_token(definition, member, occurrence),
            .world_transform = scene_transform_multiply(
                document.world_transform_matrix(occurrence),
                member_from_definition),
            .selected = selected(document, occurrence),
            .dimmed = options.instance_edit_scope.active() &&
                       occurrence != options.instance_edit_scope.occurrence,
        });
      }
      append_instance_primitive_records(
          snapshot, source, definition, member, member_from_definition,
          occurrences);
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

std::vector<std::uint64_t> primitive_material_payloads(
    const RenderSceneMeshRecord &mesh) {
  std::vector<std::uint64_t> payloads;
  const std::vector<MeshPrimitiveSpan> primitives =
      coalesced_render_mesh_primitive_spans(mesh.mesh);
  payloads.reserve(primitives.size());
  for (const MeshPrimitiveSpan &primitive : primitives) {
    payloads.push_back(render_material_payload_signature(
        mesh.material.material_for_slot(primitive.material_slot)));
  }
  return payloads;
}

bool ordinary_meshes_are_instance_compatible(
    const RenderSceneMeshRecord &left,
    const RenderSceneMeshRecord &right) {
  if (!left.renderable() || !right.renderable() ||
      left.mesh.geometry_content_id.value == 0u ||
      left.mesh.geometry_content_id != right.mesh.geometry_content_id ||
      left.mesh.geometry_payload.get() != right.mesh.geometry_payload.get() ||
      left.cast_shadows() != right.cast_shadows() ||
      left.double_sided() != right.double_sided()) {
    return false;
  }
  const std::vector<MeshPrimitiveSpan> left_primitives =
      coalesced_render_mesh_primitive_spans(left.mesh);
  const std::vector<MeshPrimitiveSpan> right_primitives =
      coalesced_render_mesh_primitive_spans(right.mesh);
  return left_primitives == right_primitives &&
         primitive_material_payloads(left) ==
             primitive_material_payloads(right);
}

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

bool try_refresh_render_scene_snapshot_selection(
    RenderSceneSnapshot &snapshot, const SceneDocument &document,
    const AssetRegistry &assets, std::uint64_t frame_index,
    RenderSceneBindingKey scene_binding,
    std::uint64_t project_material_revision,
    RenderInstanceEditScope instance_edit_scope,
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  if (snapshot.scene_binding != scene_binding ||
      snapshot.asset_revision != assets.revision() ||
      snapshot.project_material_revision != project_material_revision ||
      snapshot.local_visibility_revision !=
          local_visibility_revision(local_visibility) ||
      snapshot.instance_edit_scope != instance_edit_scope ||
      snapshot.visibility_revision != document.visibility_revision() ||
      snapshot.transform_revision != document.transform_revision() ||
      snapshot.mesh_binding_revision != document.mesh_binding_revision() ||
      snapshot.render_component_revision !=
          document.render_component_revision() ||
      snapshot.selection_revision == document.selection_revision()) {
    return false;
  }

  QDR_PROFILE_SCOPE("Render scene selection-only snapshot refresh");
  const std::uint64_t previous_scene_revision = snapshot.scene_revision;
  const std::uint64_t previous_selection_revision =
      snapshot.selection_revision;

  snapshot.frame_index = frame_index;
  snapshot.scene_revision = document.revision();
  snapshot.selection_revision = document.selection_revision();
  snapshot.dirty = {
      .scene_changed = previous_scene_revision != snapshot.scene_revision,
      .selection_changed =
          previous_selection_revision != snapshot.selection_revision,
      .visibility_changed = false,
      .local_visibility_changed = false,
      .transforms_changed = false,
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
  if (snapshot.scene_binding != scene_binding ||
      snapshot.asset_revision != assets.revision() ||
      snapshot.project_material_revision != project_material_revision ||
      snapshot.local_visibility_revision !=
          local_visibility_revision(local_visibility) ||
      snapshot.instance_edit_scope != instance_edit_scope ||
      snapshot.selection_revision != document.selection_revision() ||
      snapshot.visibility_revision != document.visibility_revision() ||
      snapshot.mesh_binding_revision != document.mesh_binding_revision() ||
      snapshot.render_component_revision !=
          document.render_component_revision() ||
      snapshot.transform_revision == document.transform_revision()) {
    return false;
  }

  const std::uint64_t previous_scene_revision = snapshot.scene_revision;
  const std::uint64_t previous_selection_revision =
      snapshot.selection_revision;
  const std::uint64_t previous_visibility_revision =
      snapshot.visibility_revision;
  const std::uint64_t previous_transform_revision =
      snapshot.transform_revision;
  const std::uint64_t previous_mesh_binding_revision =
      snapshot.mesh_binding_revision;
  const std::uint64_t previous_render_component_revision =
      snapshot.render_component_revision;
  const std::uint64_t previous_asset_revision = snapshot.asset_revision;
  const std::uint64_t previous_project_material_revision =
      snapshot.project_material_revision;

  snapshot.frame_index = frame_index;
  snapshot.scene_revision = document.revision();
  snapshot.selection_revision = document.selection_revision();
  snapshot.visibility_revision = document.visibility_revision();
  snapshot.local_visibility_revision =
      local_visibility_revision(local_visibility);
  snapshot.transform_revision = document.transform_revision();
  snapshot.mesh_binding_revision = document.mesh_binding_revision();
  snapshot.render_component_revision = document.render_component_revision();
  snapshot.asset_revision = assets.revision();
  snapshot.project_material_revision = project_material_revision;
  snapshot.instance_edit_scope = instance_edit_scope;
  snapshot.dirty = {
      .scene_changed = previous_scene_revision != snapshot.scene_revision,
      .selection_changed =
          previous_selection_revision != snapshot.selection_revision,
      .visibility_changed =
          previous_visibility_revision != snapshot.visibility_revision,
      .local_visibility_changed = false,
      .transforms_changed =
          previous_transform_revision != snapshot.transform_revision,
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

  for (RenderSceneMeshRecord &record : snapshot.meshes) {
    const QMat4 world_transform =
        document.world_transform_matrix(record.entity_id);
    const std::uint64_t transform_revision =
        RenderSignatureBuilder::light_transform_signature(world_transform);
    record.mesh_dirty = false;
    record.material_dirty = false;
    record.binding_dirty = false;
    record.transform_dirty =
        record.transform_revision != transform_revision;
    record.world_transform = world_transform;
    record.transform_revision = transform_revision;
  }
  for (RenderSceneInstancePrimitiveRecord &record :
       snapshot.instance_primitives) {
    if (record.definition_entity != kInvalidEntityId) {
      const std::optional<QMat4> definition_inverse = try_scene_matrix_inverse(
          document.world_transform_matrix(record.definition_entity));
      if (!definition_inverse.has_value()) {
        return false;
      }
      record.member_from_definition = scene_transform_multiply(
          *definition_inverse,
          document.world_transform_matrix(record.member_entity));
    }
    for (RenderSceneInstanceOccurrenceRecord &occurrence :
         record.occurrences) {
      occurrence.world_transform =
          record.definition_entity != kInvalidEntityId
              ? scene_transform_multiply(
                    document.world_transform_matrix(occurrence.entity_id),
                    record.member_from_definition)
              : document.world_transform_matrix(occurrence.entity_id);
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
  return snapshot.scene_binding == scene_binding &&
         snapshot.scene_revision == document.revision() &&
         snapshot.selection_revision == document.selection_revision() &&
         snapshot.visibility_revision == document.visibility_revision() &&
         snapshot.local_visibility_revision ==
             local_visibility_revision(local_visibility) &&
         snapshot.transform_revision == document.transform_revision() &&
         snapshot.mesh_binding_revision == document.mesh_binding_revision() &&
         snapshot.render_component_revision ==
             document.render_component_revision() &&
         snapshot.asset_revision == assets.revision() &&
         snapshot.project_material_revision == project_material_revision &&
         snapshot.instance_edit_scope == instance_edit_scope;
}

SceneRenderExtractionPlan build_scene_render_extraction_plan(
    const SceneDocument &document, const AssetRegistry &assets) {
  return build_render_scene_snapshot(document, assets);
}

std::vector<RenderToolPreviewMeshRecord> build_render_tool_preview_snapshot(
    const std::vector<ToolPreviewMesh> &meshes, const AssetRegistry &assets,
    const ViewportLocalVisibilitySnapshot *local_visibility,
    const RenderProjectMaterialResolver *project_material_resolver) {
  QDR_PROFILE_FUNCTION();
  std::vector<RenderToolPreviewMeshRecord> records;
  records.reserve(meshes.size());
  std::vector<std::pair<MaterialComponent, RenderMaterialBindingSnapshot>>
      material_cache;
  material_cache.reserve(4U);
  for (const ToolPreviewMesh &mesh : meshes) {
    const EntityId visibility_entity =
        mesh.visibility_entity == kInvalidEntityId ? mesh.source_entity
                                                    : mesh.visibility_entity;
    if (mesh.source_entity != kInvalidEntityId &&
        !local_visibility_allows(local_visibility, visibility_entity)) {
      continue;
    }
    auto cached_material = std::ranges::find(
        material_cache, mesh.material,
        [](const auto &entry) -> const MaterialComponent & {
          return entry.first;
        });
    if (cached_material == material_cache.end()) {
      QDR_PROFILE_SCOPE("Tool preview build unique material snapshot");
      material_cache.emplace_back(
          mesh.material,
          build_render_material_binding_snapshot(
              assets, &mesh.material, project_material_resolver));
      cached_material = std::prev(material_cache.end());
    }
    RenderMaterialBindingSnapshot material_snapshot =
        cached_material->second;
    records.push_back(RenderToolPreviewMeshRecord{
        .source_object_id = mesh.source_object_id,
        .source_entity = mesh.source_entity,
        .visibility_entity = visibility_entity,
        .material_reference = mesh.material.material,
        .material_id = mesh.material.material.embedded_asset(),
        .mesh = mesh.mesh,
        .material = std::move(material_snapshot),
        .material_revision = 0u,
        .transform = mesh.transform,
        .instance_transforms = mesh.instance_transforms,
        .dynamic_positions = mesh.dynamic_positions,
        .suppress_source = mesh.suppress_source,
        .layout_key = mesh.layout_key,
        .position_revision = mesh.position_revision,
        .dynamic_position_payload = mesh.dynamic_position_payload,
    });
    records.back().material_revision = records.back().material.revision;
  }
  return records;
}

} // namespace quader
