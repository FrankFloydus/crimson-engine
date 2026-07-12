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
#include "scene/scene_document.hpp"
#include "render/tool_preview_renderable_cache.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace quader {
namespace {

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
  signature.add_value(material.material_id);
  if (!material.present) {
    signature.add_value(0);
    return;
  }

  signature.add_value(material.revision);
  for (const MaterialTextureSlotDescriptor &descriptor :
       material_texture_slot_descriptors()) {
    const std::optional<AssetId> texture_id =
        material_texture_slot_asset(material.document, descriptor.id);
    if (!texture_id.has_value()) {
      continue;
    }
    signature.add_string(descriptor.id).add_value(*texture_id);
    const RenderTextureAssetSnapshot *texture =
        material.texture_for_slot(descriptor.id);
    signature.add_value(texture && texture->present ? texture->texture.revision.value
                                                    : 0u);
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
  if (previous == nullptr) {
    return {};
  }

  return {
      .scene_changed = previous->scene_revision != snapshot.scene_revision,
      .selection_changed =
          previous->selection_revision != snapshot.selection_revision,
      .visibility_changed =
          previous->visibility_revision != snapshot.visibility_revision,
      .transforms_changed =
          previous->transform_revision != snapshot.transform_revision,
      .mesh_bindings_changed =
          previous->mesh_binding_revision != snapshot.mesh_binding_revision,
      .render_components_changed = previous->render_component_revision !=
                                   snapshot.render_component_revision,
      .assets_changed = previous->asset_revision != snapshot.asset_revision,
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
      previous->material_id != record.material_id ||
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
         previous->scene_revision == snapshot.scene_revision &&
         previous->selection_revision == snapshot.selection_revision &&
         previous->visibility_revision == snapshot.visibility_revision &&
         previous->transform_revision == snapshot.transform_revision &&
         previous->mesh_binding_revision == snapshot.mesh_binding_revision &&
         previous->render_component_revision ==
             snapshot.render_component_revision &&
         previous->asset_revision == snapshot.asset_revision;
}

void reuse_previous_render_records(RenderSceneSnapshot &snapshot,
                                   const RenderSceneSnapshot &previous) {
  snapshot.meshes = previous.meshes;
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
  RenderMeshAssetSnapshot snapshot{
      .mesh_id = mesh_id,
  };
  snapshot.mesh_revision = asset.revision.value;
  snapshot.vertices = asset.vertices;
  snapshot.indices = asset.indices;
  snapshot.primitives = asset.primitives;
  snapshot.present = true;
  snapshot.has_stable_cpu_topology = asset.has_stable_cpu_topology;
  snapshot.has_authored_polygon_document = asset.has_authored_polygon_document;
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
    const AssetRegistry &assets, const MaterialComponent *material) {
  RenderMaterialBindingSnapshot snapshot;
  if (!material) {
    return snapshot;
  }

  snapshot.component = *material;
  snapshot.primary_material =
      build_render_material_asset_snapshot(assets, material->material);
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
                  assets, material->material_slots.at(slot)));
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

RenderSceneSnapshot build_render_scene_snapshot(
    const SceneDocument &document, const AssetRegistry &assets,
    RenderSceneSnapshotBuildOptions options) {
  QDR_PROFILE_FUNCTION();
  RenderSceneSnapshot snapshot;
  snapshot.frame_index = options.frame_index;
  snapshot.scene_revision = document.revision();
  snapshot.selection_revision = document.selection_revision();
  snapshot.visibility_revision = document.visibility_revision();
  snapshot.transform_revision = document.transform_revision();
  snapshot.mesh_binding_revision = document.mesh_binding_revision();
  snapshot.render_component_revision = document.render_component_revision();
  snapshot.asset_revision = assets.revision();
  snapshot.dirty = scene_snapshot_dirty_state(snapshot, options.previous);
  if (can_reuse_previous_render_records(snapshot, options.previous)) {
    reuse_previous_render_records(snapshot, *options.previous);
    return snapshot;
  }

  for (const EntityId entity_id : document.entities()) {
    QDR_PROFILE_SCOPE("Scene render extract entity");
    if (!document.is_effectively_visible(entity_id)) {
      continue;
    }

    const QMat4 world_transform = document.world_transform_matrix(entity_id);

    if (const MeshComponent *component = document.mesh(entity_id)) {
      const MaterialComponent *scene_material =
          document.material(entity_id);
      const RenderMaterialBindingSnapshot material =
          build_render_material_binding_snapshot(assets, scene_material);
      const MeshAsset *asset = assets.mesh(component->mesh);
      RenderSceneMeshRecord entry{
          .entity_id = entity_id,
          .mesh = asset ? render_mesh_asset_snapshot(component->mesh, *asset)
                        : RenderMeshAssetSnapshot{.mesh_id = component->mesh},
          .mesh_component = *component,
          .material = material,
          .material_id = scene_material ? scene_material->material
                                            : kInvalidAssetId,
          .material_revision = material.revision,
          .world_transform = world_transform,
          .transform_revision =
              RenderSignatureBuilder::light_transform_signature(
                  world_transform),
      };
      mark_mesh_dirty(entry, previous_mesh_record(options.previous, entity_id));
      if (entry.renderable()) {
        snapshot.live_mesh_entities.insert(entity_id);
      }
      snapshot.meshes.push_back(std::move(entry));
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
      mark_light_dirty(entry,
                       previous_light_record(options.previous, entity_id));
      snapshot.lights.push_back(entry);
    }
  }

  return snapshot;
}

bool try_refresh_render_scene_snapshot_transforms(
    RenderSceneSnapshot &snapshot, const SceneDocument &document,
    const AssetRegistry &assets, std::uint64_t frame_index) {
  QDR_PROFILE_FUNCTION();
  if (snapshot.asset_revision != assets.revision() ||
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

  snapshot.frame_index = frame_index;
  snapshot.scene_revision = document.revision();
  snapshot.selection_revision = document.selection_revision();
  snapshot.visibility_revision = document.visibility_revision();
  snapshot.transform_revision = document.transform_revision();
  snapshot.mesh_binding_revision = document.mesh_binding_revision();
  snapshot.render_component_revision = document.render_component_revision();
  snapshot.asset_revision = assets.revision();
  snapshot.dirty = {
      .scene_changed = previous_scene_revision != snapshot.scene_revision,
      .selection_changed =
          previous_selection_revision != snapshot.selection_revision,
      .visibility_changed =
          previous_visibility_revision != snapshot.visibility_revision,
      .transforms_changed =
          previous_transform_revision != snapshot.transform_revision,
      .mesh_bindings_changed =
          previous_mesh_binding_revision != snapshot.mesh_binding_revision,
      .render_components_changed = previous_render_component_revision !=
                                   snapshot.render_component_revision,
      .assets_changed = previous_asset_revision != snapshot.asset_revision,
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
    const AssetRegistry &assets) {
  return snapshot.scene_revision == document.revision() &&
         snapshot.selection_revision == document.selection_revision() &&
         snapshot.visibility_revision == document.visibility_revision() &&
         snapshot.transform_revision == document.transform_revision() &&
         snapshot.mesh_binding_revision == document.mesh_binding_revision() &&
         snapshot.render_component_revision ==
             document.render_component_revision() &&
         snapshot.asset_revision == assets.revision();
}

SceneRenderExtractionPlan build_scene_render_extraction_plan(
    const SceneDocument &document, const AssetRegistry &assets) {
  return build_render_scene_snapshot(document, assets);
}

std::vector<RenderToolPreviewMeshRecord> build_render_tool_preview_snapshot(
    const std::vector<ToolPreviewMesh> &meshes, const AssetRegistry &assets) {
  std::vector<RenderToolPreviewMeshRecord> records;
  records.reserve(meshes.size());
  for (const ToolPreviewMesh &mesh : meshes) {
    const MaterialComponent material{.material = mesh.material};
    RenderMaterialBindingSnapshot material_snapshot =
        build_render_material_binding_snapshot(assets, &material);
    records.push_back(RenderToolPreviewMeshRecord{
        .source_entity = mesh.source_entity,
        .material_id = mesh.material,
        .mesh = mesh.mesh,
        .material = std::move(material_snapshot),
        .material_revision = 0u,
        .transform = mesh.transform,
        .instance_transforms = mesh.instance_transforms,
    });
    records.back().material_revision = records.back().material.revision;
  }
  return records;
}

} // namespace quader
