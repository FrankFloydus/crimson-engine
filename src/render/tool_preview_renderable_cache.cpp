////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/tool_preview_renderable_cache.hpp"

#include "diagnostics/profile.hpp"
#include "render/material_binding_service.hpp"
#include "render/render_layers.hpp"
#include "render/render_signature_builder.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <numeric>
#include <span>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

std::size_t
preview_transform_count(const RenderToolPreviewMeshRecord &preview_mesh) {
  return preview_mesh.instance_transforms.empty()
             ? 1u
             : preview_mesh.instance_transforms.size();
}

const QMat4 &preview_transform(const RenderToolPreviewMeshRecord &preview_mesh,
                               std::size_t index) {
  return preview_mesh.instance_transforms.empty()
             ? preview_mesh.transform
             : preview_mesh.instance_transforms[index];
}

void copy_preview_transforms(std::vector<QMat4> &destination,
                             const RenderToolPreviewMeshRecord &preview_mesh) {
  const std::size_t count = preview_transform_count(preview_mesh);
  destination.resize(count);
  for (std::size_t index = 0u; index < count; ++index) {
    destination[index] = preview_transform(preview_mesh, index);
  }
}

std::span<const QVec3>
preview_dynamic_positions(const RenderToolPreviewMeshRecord &preview_mesh) {
  return preview_mesh.dynamic_position_payload
             ? std::span<const QVec3>(*preview_mesh.dynamic_position_payload)
             : std::span<const QVec3>{};
}

void update_bounds(MeshRenderableFactory &factory,
                   const MeshRenderableProxy &proxy,
                   const RenderMeshAssetSnapshot &mesh,
                   std::span<const QVec3> positions) {
  if (positions.empty()) {
    factory.update_bounds(proxy, mesh);
  } else {
    factory.update_bounds(proxy, positions);
  }
}

bool same_float_bits(float left, float right) {
  return std::bit_cast<std::uint32_t>(left) ==
         std::bit_cast<std::uint32_t>(right);
}

bool same_vec3(const QVec3 left, const QVec3 right) {
  return same_float_bits(left.x, right.x) &&
         same_float_bits(left.y, right.y) &&
         same_float_bits(left.z, right.z);
}

bool same_mat4(const QMat4 &left, const QMat4 &right) {
  return std::equal(left.values.begin(), left.values.end(),
                    right.values.begin(), same_float_bits);
}

bool same_geometry_payload(
    const std::shared_ptr<const MeshGeometryPayload> &left,
    const std::shared_ptr<const MeshGeometryPayload> &right,
    bool include_positions) {
  if (left.get() == right.get()) {
    return true;
  }
  if (!left || !right || left->vertices.size() != right->vertices.size() ||
      left->indices != right->indices ||
      left->primitives != right->primitives) {
    return false;
  }
  for (std::size_t index = 0u; index < left->vertices.size(); ++index) {
    const MeshVertex &a = left->vertices[index];
    const MeshVertex &b = right->vertices[index];
    if ((include_positions && !same_vec3(a.position, b.position)) ||
        a.source_vertex_id != b.source_vertex_id ||
        !same_vec3(a.normal, b.normal) ||
        !same_float_bits(a.uv.x, b.uv.x) ||
        !same_float_bits(a.uv.y, b.uv.y) || a.rgba != b.rgba) {
      return false;
    }
  }
  return true;
}

} // namespace

ToolPreviewRenderableCache::~ToolPreviewRenderableCache() {
  shutdown();
}

void ToolPreviewRenderableCache::initialize(
    MeshRenderableFactory &factory, MaterialBindingService &material_binding) {
  factory_ = &factory;
  material_binding_ = &material_binding;
}

void ToolPreviewRenderableCache::shutdown() {
  if (!factory_) {
    return;
  }
  clear();
  factory_ = nullptr;
  material_binding_ = nullptr;
}

ToolPreviewSubmissionResult ToolPreviewRenderableCache::set_meshes(
    const std::vector<RenderToolPreviewMeshRecord> &meshes) {
  QDR_PROFILE_FUNCTION();
  if (!factory_ || !factory_->initialized() || !material_binding_) {
    return ToolPreviewSubmissionResult::Failed;
  }

  const std::uint64_t next_signature = preview_meshes_signature(meshes);
  // The aggregate hash is only a cheap rejection gate. Exact state, with
  // pointer-first geometry checks, prevents a collision from suppressing a
  // real submission while keeping repeated dense ticks O(group count).
  if (next_signature == mesh_signature_ &&
      submitted_meshes_equal(submitted_meshes_, meshes)) {
    return ToolPreviewSubmissionResult::Unchanged;
  }

  if (meshes.empty()) {
    QDR_PROFILE_SCOPE("Tool preview retain current and warm generations");
    if (current_generation_) {
      hide_generation(*current_generation_);
    }
    mesh_signature_ = next_signature;
    submitted_meshes_ = capture_submission(meshes);
    return ToolPreviewSubmissionResult::Published;
  }

  std::vector<std::size_t> candidate_indices;
  if (current_generation_) {
    QDR_PROFILE_SCOPE("Tool preview match current generation");
    if (prepare_generation(*current_generation_, meshes, candidate_indices)) {
      publish_generation_subset(*current_generation_,
                                std::move(candidate_indices));
      mesh_signature_ = next_signature;
      submitted_meshes_ = capture_submission(meshes);
      material_uniforms_dirty_ =
          !active_material_uniforms_are_current(*current_generation_);
      return ToolPreviewSubmissionResult::Published;
    }
  }

  if (warm_generation_) {
    QDR_PROFILE_SCOPE("Tool preview match warm generation");
    if (prepare_generation(*warm_generation_, meshes, candidate_indices)) {
      // The candidate is made visible first. No frame can observe a source
      // hidden without either the old or new preview surface present.
      publish_generation_subset(*warm_generation_,
                                std::move(candidate_indices));
      if (current_generation_) {
        hide_generation(*current_generation_);
      }
      current_generation_.swap(warm_generation_);
      mesh_signature_ = next_signature;
      submitted_meshes_ = capture_submission(meshes);
      material_uniforms_dirty_ =
          !active_material_uniforms_are_current(*current_generation_);
      return ToolPreviewSubmissionResult::Published;
    }
  }

  std::unique_ptr<PreviewGeneration> candidate;
  bool candidate_complete = false;
  {
    QDR_PROFILE_SCOPE("Tool preview build hidden candidate generation");
    candidate = build_generation(meshes, candidate_complete);
  }
  if (!candidate_complete) {
    // Successful pieces of a failed candidate are detached and queued. The
    // submission path never destroys a Filament proxy synchronously.
    retire_generation(std::move(candidate));
    return ToolPreviewSubmissionResult::Failed;
  }

  candidate_indices.resize(candidate->groups.size());
  std::iota(candidate_indices.begin(), candidate_indices.end(), 0u);
  {
    QDR_PROFILE_SCOPE("Tool preview publish candidate generation");
    publish_generation_subset(*candidate, std::move(candidate_indices));
    if (current_generation_) {
      hide_generation(*current_generation_);
    }
  }

  // Keep the immediate predecessor intact. Only an older third generation is
  // moved, in O(1), to an explicitly budgeted idle-destruction queue.
  if (warm_generation_) {
    QDR_PROFILE_SCOPE("Tool preview retire older warm generation");
    retire_generation(std::move(warm_generation_));
  }
  warm_generation_ = std::move(current_generation_);
  current_generation_ = std::move(candidate);
  mesh_signature_ = next_signature;
  submitted_meshes_ = capture_submission(meshes);
  material_uniforms_dirty_ =
      !active_material_uniforms_are_current(*current_generation_);
  return ToolPreviewSubmissionResult::Published;
}

std::unique_ptr<ToolPreviewRenderableCache::PreviewGeneration>
ToolPreviewRenderableCache::build_generation(
    const std::vector<RenderToolPreviewMeshRecord> &meshes, bool &complete) {
  complete = false;
  auto generation = std::make_unique<PreviewGeneration>();
  generation->groups.reserve(meshes.size());
  generation->scene_entities.reserve(meshes.size() * 3u);

  const auto retain_entity = [&](const MeshRenderableProxy &proxy) {
    if (proxy.entity) {
      generation->scene_entities.push_back(proxy.entity);
    }
  };

  for (const RenderToolPreviewMeshRecord &preview_mesh : meshes) {
    if (!preview_mesh.renderable()) {
      if (preview_mesh.suppress_source) {
        return generation;
      }
      continue;
    }

    std::vector<QMat4> transforms;
    copy_preview_transforms(transforms, preview_mesh);
    if (transforms.empty()) {
      if (preview_mesh.suppress_source) {
        return generation;
      }
      continue;
    }

    const RenderMeshAssetSnapshot mesh_snapshot =
        render_mesh_asset_snapshot(kInvalidAssetId, preview_mesh.mesh);
    const std::span<const QVec3> positions =
        preview_dynamic_positions(preview_mesh);
    MeshRenderableProxy rendered_prototype = factory_->create(
        preview_mesh.source_entity, mesh_snapshot, &preview_mesh.material,
        transforms.front(), false, true, 0u, false, false,
        kViewportDefaultRenderableChannel, preview_mesh.dynamic_positions,
        positions);
    if (!rendered_prototype.entity) {
      return generation;
    }
    retain_entity(rendered_prototype);

    MeshRenderableProxy shaded_prototype = factory_->create_shared_geometry(
        preview_mesh.source_entity, mesh_snapshot, &preview_mesh.material,
        rendered_prototype, transforms.front(), true, 0u, true);
    const auto make_group = [&](MeshRenderableProxy rendered,
                                MeshRenderableProxy shaded = {},
                                MeshRenderableProxy overlay_depth = {}) {
      return PreviewGroup{
          .source_entity = preview_mesh.source_entity,
          .geometry_signature = preview_geometry_signature(preview_mesh),
          .topology_signature = preview_topology_signature(preview_mesh),
          .material_signature = preview_material_signature(preview_mesh),
          .material_uniform_signature = 0u,
          .layout_key = preview_mesh.layout_key,
          .position_revision = preview_mesh.position_revision,
          .topology_payload = preview_mesh.mesh.geometry_payload(),
          .transforms = std::move(transforms),
          .dynamic_positions = preview_mesh.dynamic_positions,
          .visible = false,
          .material_uniforms_initialized = false,
          .rendered_prototype = std::move(rendered),
          .shaded_prototype = std::move(shaded),
          .overlay_depth_prototype = std::move(overlay_depth),
      };
    };
    if (!shaded_prototype.entity) {
      generation->groups.push_back(
          make_group(std::move(rendered_prototype)));
      return generation;
    }
    retain_entity(shaded_prototype);
    MeshRenderableProxy overlay_depth_prototype =
        factory_->create_shared_geometry(
            preview_mesh.source_entity, mesh_snapshot, &preview_mesh.material,
            rendered_prototype, transforms.front(), true, 0u, true, true,
            kViewportOverlayDepthChannel);
    if (!overlay_depth_prototype.entity) {
      generation->groups.push_back(make_group(
          std::move(rendered_prototype), std::move(shaded_prototype)));
      return generation;
    }
    retain_entity(overlay_depth_prototype);
    PreviewGroup group = make_group(
        std::move(rendered_prototype), std::move(shaded_prototype),
        std::move(overlay_depth_prototype));
    if (!positions.empty()) {
      factory_->update_bounds(group.shaded_prototype, positions);
      factory_->update_bounds(group.overlay_depth_prototype, positions);
    }
    group.rendered_instances.reserve(group.transforms.size() - 1u);
    group.shaded_instances.reserve(group.transforms.size() - 1u);
    group.overlay_depth_instances.reserve(group.transforms.size() - 1u);
    for (std::size_t index = 1u; index < group.transforms.size(); ++index) {
      MeshRenderableProxy rendered_instance = factory_->create_shared_instance(
          preview_mesh.source_entity, mesh_snapshot,
          group.rendered_prototype, group.transforms[index], 0u,
          kViewportDefaultRenderableChannel, true);
      if (!rendered_instance.entity) {
        generation->groups.push_back(std::move(group));
        return generation;
      }
      retain_entity(rendered_instance);
      group.rendered_instances.push_back(std::move(rendered_instance));

      MeshRenderableProxy shaded_instance = factory_->create_shared_instance(
          preview_mesh.source_entity, mesh_snapshot, group.shaded_prototype,
          group.transforms[index], 0u, kViewportDefaultRenderableChannel,
          false);
      if (!shaded_instance.entity) {
        generation->groups.push_back(std::move(group));
        return generation;
      }
      retain_entity(shaded_instance);
      group.shaded_instances.push_back(std::move(shaded_instance));

      MeshRenderableProxy overlay_depth_instance =
          factory_->create_shared_instance(
              preview_mesh.source_entity, mesh_snapshot,
              group.overlay_depth_prototype, group.transforms[index], 0u,
              kViewportOverlayDepthChannel, false);
      if (!overlay_depth_instance.entity) {
        generation->groups.push_back(std::move(group));
        return generation;
      }
      retain_entity(overlay_depth_instance);
      group.overlay_depth_instances.push_back(
          std::move(overlay_depth_instance));
      if (!positions.empty()) {
        factory_->update_bounds(group.rendered_instances.back(), positions);
        factory_->update_bounds(group.shaded_instances.back(), positions);
        factory_->update_bounds(group.overlay_depth_instances.back(),
                                positions);
      }
    }
    generation->groups.push_back(std::move(group));
  }
  complete = true;
  return generation;
}

bool ToolPreviewRenderableCache::prepare_generation(
    PreviewGeneration &generation,
    const std::vector<RenderToolPreviewMeshRecord> &meshes,
    std::vector<std::size_t> &candidate_indices) {
  QDR_PROFILE_FUNCTION();
  candidate_indices.clear();

  // Steady modal ticks preserve submission order and the active group set.
  // Validate that common case without allocating lookup tables or hashing
  // every retained inactive group.
  if (meshes.size() == generation.active_group_indices.size()) {
    bool ordered_active_match = true;
    for (std::size_t index = 0u; index < meshes.size(); ++index) {
      const std::size_t group_index =
          generation.active_group_indices[index];
      if (!meshes[index].renderable() ||
          group_index >= generation.groups.size() ||
          !group_matches_exact(generation.groups[group_index],
                               meshes[index]) ||
          (meshes[index].dynamic_positions &&
           !group_dynamic_update_is_valid(generation.groups[group_index],
                                          meshes[index]))) {
        ordered_active_match = false;
        break;
      }
    }
    if (ordered_active_match) {
      QDR_PROFILE_SCOPE("Tool preview ordered active fast path");
      candidate_indices = generation.active_group_indices;
      for (std::size_t index = 0u; index < meshes.size(); ++index) {
        update_group(generation.groups[candidate_indices[index]],
                     meshes[index]);
      }
      return true;
    }
  }

  std::vector<const RenderToolPreviewMeshRecord *> renderable_meshes;
  renderable_meshes.reserve(meshes.size());
  for (const RenderToolPreviewMeshRecord &mesh : meshes) {
    if (mesh.renderable()) {
      renderable_meshes.push_back(&mesh);
    } else if (mesh.suppress_source) {
      return false;
    }
  }
  if (renderable_meshes.size() > generation.groups.size()) {
    return false;
  }

  std::vector<bool> claimed(generation.groups.size(), false);
  std::unordered_multimap<EntityId, std::size_t> groups_by_source;
  groups_by_source.reserve(generation.groups.size());
  for (std::size_t index = 0u; index < generation.groups.size(); ++index) {
    groups_by_source.emplace(generation.groups[index].source_entity, index);
  }

  bool exact = true;
  for (const RenderToolPreviewMeshRecord *mesh : renderable_meshes) {
    bool matched = false;
    const auto [first, last] = groups_by_source.equal_range(mesh->source_entity);
    for (auto candidate = first; candidate != last; ++candidate) {
      const std::size_t index = candidate->second;
      if (!claimed[index] && group_matches_exact(generation.groups[index],
                                                 *mesh)) {
        claimed[index] = true;
        candidate_indices.push_back(index);
        matched = true;
        break;
      }
    }
    if (!matched) {
      exact = false;
      break;
    }
  }

  if (!exact) {
    // Structural reassignment changes the stable source/layout identity, so it
    // is allowed only when the complete generation is being reused. Subset
    // reuse remains exact and therefore preserves a dense warm generation.
    if (renderable_meshes.size() != generation.groups.size()) {
      return false;
    }
    candidate_indices.clear();
    std::fill(claimed.begin(), claimed.end(), false);
    std::unordered_multimap<std::uint64_t, std::size_t> groups_by_structure;
    groups_by_structure.reserve(generation.groups.size());
    for (std::size_t index = 0u; index < generation.groups.size(); ++index) {
      groups_by_structure.emplace(
          group_structural_reuse_signature(generation.groups[index]), index);
    }
    for (const RenderToolPreviewMeshRecord *mesh : renderable_meshes) {
      bool matched = false;
      const auto key = preview_structural_reuse_signature(*mesh);
      const auto [first, last] = groups_by_structure.equal_range(key);
      for (auto candidate = first; candidate != last; ++candidate) {
        const std::size_t index = candidate->second;
        if (!claimed[index] &&
            group_matches_structure(generation.groups[index], *mesh)) {
          claimed[index] = true;
          candidate_indices.push_back(index);
          matched = true;
          break;
        }
      }
      if (!matched) {
        return false;
      }
    }
  }

  for (std::size_t index = 0u; index < renderable_meshes.size(); ++index) {
    const PreviewGroup &group = generation.groups[candidate_indices[index]];
    if (renderable_meshes[index]->dynamic_positions &&
        !group_dynamic_update_is_valid(group, *renderable_meshes[index])) {
      return false;
    }
  }

  {
    QDR_PROFILE_SCOPE("Tool preview update reused candidate groups");
    for (std::size_t index = 0u; index < renderable_meshes.size(); ++index) {
      update_group(generation.groups[candidate_indices[index]],
                   *renderable_meshes[index]);
    }
  }
  return true;
}

void ToolPreviewRenderableCache::update_group(
    PreviewGroup &group,
    const RenderToolPreviewMeshRecord &preview_mesh) {
  const RenderMeshAssetSnapshot mesh_snapshot =
      render_mesh_asset_snapshot(kInvalidAssetId, preview_mesh.mesh);
  const std::span<const QVec3> positions =
      preview_dynamic_positions(preview_mesh);
  const bool identity_changed =
      group.source_entity != preview_mesh.source_entity ||
      group.layout_key != preview_mesh.layout_key;
  const std::uint64_t geometry_signature =
      preview_geometry_signature(preview_mesh);
  const std::shared_ptr<const MeshGeometryPayload> topology_payload =
      preview_mesh.mesh.geometry_payload();
  const bool positions_changed =
      preview_mesh.dynamic_positions &&
      (identity_changed ||
       group.position_revision != preview_mesh.position_revision ||
       group.geometry_signature != geometry_signature ||
       (!preview_mesh.dynamic_position_payload &&
        group.topology_payload.get() != topology_payload.get()));

  if (positions_changed) {
    // prepare_generation exhaustively validates the only guards that can make
    // update_dynamic_geometry return false. Factory lifetime is single-threaded
    // here, so no guard can change between validation and this mutation pass.
    const bool updated = factory_->update_dynamic_geometry(
        group.rendered_prototype, mesh_snapshot, positions);
    assert(updated);
    (void)updated;
  }
  if (positions_changed) {
    update_bounds(*factory_, group.shaded_prototype, mesh_snapshot, positions);
    update_bounds(*factory_, group.overlay_depth_prototype, mesh_snapshot,
                  positions);
    for (std::size_t index = 0u;
         index < group.rendered_instances.size(); ++index) {
      update_bounds(*factory_, group.rendered_instances[index], mesh_snapshot,
                    positions);
      update_bounds(*factory_, group.shaded_instances[index], mesh_snapshot,
                    positions);
      update_bounds(*factory_, group.overlay_depth_instances[index],
                    mesh_snapshot, positions);
    }
  }

  copy_preview_transforms(group.transforms, preview_mesh);
  factory_->update_transform(group.rendered_prototype,
                             group.transforms.front());
  factory_->update_transform(group.shaded_prototype,
                             group.transforms.front());
  factory_->update_transform(group.overlay_depth_prototype,
                             group.transforms.front());
  for (std::size_t index = 0u; index < group.rendered_instances.size();
       ++index) {
    factory_->update_transform(group.rendered_instances[index],
                               group.transforms[index + 1u]);
    factory_->update_transform(group.shaded_instances[index],
                               group.transforms[index + 1u]);
    factory_->update_transform(group.overlay_depth_instances[index],
                               group.transforms[index + 1u]);
  }

  const auto update_source = [&](MeshRenderableProxy &proxy) {
    proxy.source_entity = preview_mesh.source_entity;
  };
  update_source(group.rendered_prototype);
  update_source(group.shaded_prototype);
  update_source(group.overlay_depth_prototype);
  for (MeshRenderableProxy &proxy : group.rendered_instances) {
    update_source(proxy);
  }
  for (MeshRenderableProxy &proxy : group.shaded_instances) {
    update_source(proxy);
  }
  for (MeshRenderableProxy &proxy : group.overlay_depth_instances) {
    update_source(proxy);
  }

  group.source_entity = preview_mesh.source_entity;
  group.geometry_signature = geometry_signature;
  group.material_signature = preview_material_signature(preview_mesh);
  group.layout_key = preview_mesh.layout_key;
  group.position_revision = preview_mesh.position_revision;
  if (!group.topology_payload ||
      group.topology_payload.get() != topology_payload.get()) {
    group.topology_signature = preview_topology_signature(preview_mesh);
  }
  group.topology_payload = topology_payload;
  group.dynamic_positions = preview_mesh.dynamic_positions;
}

void ToolPreviewRenderableCache::publish_generation_subset(
    PreviewGeneration &generation,
    std::vector<std::size_t> candidate_indices) {
  std::vector<bool> desired(generation.groups.size(), false);
  for (const std::size_t index : candidate_indices) {
    if (index < desired.size()) {
      desired[index] = true;
    }
  }

  // Candidate-first visibility swap: show replacements before hiding any
  // previously active group.
  for (const std::size_t index : candidate_indices) {
    if (index < generation.groups.size()) {
      set_group_visible(generation.groups[index], true);
    }
  }
  for (const std::size_t index : generation.active_group_indices) {
    if (index < generation.groups.size() && !desired[index]) {
      set_group_visible(generation.groups[index], false);
    }
  }
  generation.active_group_indices = std::move(candidate_indices);
}

void ToolPreviewRenderableCache::set_group_visible(PreviewGroup &group,
                                                   bool visible) {
  if (!factory_ || group.visible == visible) {
    return;
  }
  const std::uint8_t rendered_mask =
      visible ? kViewportRenderedSceneLayerMask : 0u;
  const std::uint8_t shaded_mask =
      visible ? kViewportShadedSceneLayerMask : 0u;
  const std::uint8_t depth_mask = visible ? kViewportOverlayLayerMask : 0u;
  factory_->update_layer_mask(group.rendered_prototype, rendered_mask);
  for (const MeshRenderableProxy &proxy : group.rendered_instances) {
    factory_->update_layer_mask(proxy, rendered_mask);
  }
  factory_->update_layer_mask(group.shaded_prototype, shaded_mask);
  for (const MeshRenderableProxy &proxy : group.shaded_instances) {
    factory_->update_layer_mask(proxy, shaded_mask);
  }
  factory_->update_layer_mask(group.overlay_depth_prototype, depth_mask);
  for (const MeshRenderableProxy &proxy : group.overlay_depth_instances) {
    factory_->update_layer_mask(proxy, depth_mask);
  }
  group.visible = visible;
}

void ToolPreviewRenderableCache::hide_generation(
    PreviewGeneration &generation) {
  for (const std::size_t index : generation.active_group_indices) {
    if (index < generation.groups.size()) {
      set_group_visible(generation.groups[index], false);
    }
  }
}

void ToolPreviewRenderableCache::update_surface_grid(
    float grid_world_spacing, const ViewportSettings &settings) {
  QDR_PROFILE_FUNCTION();
  if (!factory_) {
    return;
  }
  const std::uint64_t signature =
      mesh_surface_grid_signature(grid_world_spacing, settings);
  if (!material_uniforms_dirty_ &&
      material_uniform_signature_ == signature) {
    return;
  }

  {
    QDR_PROFILE_SCOPE("Tool preview material uniforms apply");
    if (current_generation_) {
      for (const std::size_t index :
           current_generation_->active_group_indices) {
        if (index >= current_generation_->groups.size()) {
          continue;
        }
        PreviewGroup &group = current_generation_->groups[index];
        if (!group.visible ||
            (group.material_uniforms_initialized &&
             group.material_uniform_signature == signature)) {
          continue;
        }
        factory_->update_surface_grid(group.rendered_prototype,
                                      grid_world_spacing, settings);
        factory_->update_surface_grid(group.shaded_prototype,
                                      grid_world_spacing, settings);
        factory_->update_shaded_lighting(group.shaded_prototype, settings);
        group.material_uniform_signature = signature;
        group.material_uniforms_initialized = true;
      }
    }
  }
  material_uniform_signature_ = signature;
  material_uniforms_dirty_ = false;
}

void ToolPreviewRenderableCache::retire_generation(
    std::unique_ptr<PreviewGeneration> generation) {
  if (!generation) {
    return;
  }
  // Retired generations have already been hidden (or were built at layer 0).
  // Keep their prebuilt entity list so retirement needs one Filament call,
  // independent of proxy count; resource destruction remains idle-budgeted.
  if (!generation->detached && !generation->scene_entities.empty()) {
    factory_->remove_from_scene(
        std::span<const utils::Entity>(generation->scene_entities));
  }
  generation->detached = true;
  retired_generations_.push_back(std::move(generation));
}

bool ToolPreviewRenderableCache::destroy_one_proxy(
    PreviewGeneration &generation) {
  if (!factory_) {
    return false;
  }
  PreviewGroup &group = generation.groups.back();
  const auto destroy_proxy = [&](MeshRenderableProxy &proxy) {
    if (generation.detached) {
      factory_->destroy_detached(proxy);
    } else {
      factory_->destroy(proxy);
    }
  };
  const auto destroy_back = [&](std::vector<MeshRenderableProxy> &proxies) {
    if (proxies.empty()) {
      return false;
    }
    destroy_proxy(proxies.back());
    proxies.pop_back();
    return true;
  };
  if (destroy_back(group.overlay_depth_instances) ||
      destroy_back(group.shaded_instances) ||
      destroy_back(group.rendered_instances)) {
    return true;
  }
  if (group.overlay_depth_prototype.entity) {
    destroy_proxy(group.overlay_depth_prototype);
    return true;
  }
  if (group.shaded_prototype.entity) {
    destroy_proxy(group.shaded_prototype);
    return true;
  }
  if (group.rendered_prototype.entity) {
    destroy_proxy(group.rendered_prototype);
    return true;
  }
  return false;
}

void ToolPreviewRenderableCache::drain_retired_destroy_queue(
    std::size_t max_proxy_count) {
  QDR_PROFILE_FUNCTION();
  if (!factory_ || max_proxy_count == 0u) {
    return;
  }
  std::size_t destroyed = 0u;
  while (destroyed < max_proxy_count && !retired_generations_.empty()) {
    PreviewGeneration &generation = *retired_generations_.back();
    while (!generation.groups.empty() &&
           group_proxy_count(generation.groups.back()) == 0u) {
      generation.groups.pop_back();
    }
    if (generation.groups.empty()) {
      retired_generations_.pop_back();
      continue;
    }
    QDR_PROFILE_SCOPE("Tool preview destroy retired proxy");
    if (destroy_one_proxy(generation)) {
      ++destroyed;
    }
  }
}

void ToolPreviewRenderableCache::destroy_group(PreviewGroup &group,
                                               bool detached) {
  if (!factory_) {
    return;
  }
  const auto destroy_proxy = [&](MeshRenderableProxy &proxy) {
    if (detached) {
      factory_->destroy_detached(proxy);
    } else {
      factory_->destroy(proxy);
    }
  };
  for (MeshRenderableProxy &proxy : group.rendered_instances) {
    destroy_proxy(proxy);
  }
  for (MeshRenderableProxy &proxy : group.shaded_instances) {
    destroy_proxy(proxy);
  }
  for (MeshRenderableProxy &proxy : group.overlay_depth_instances) {
    destroy_proxy(proxy);
  }
  destroy_proxy(group.overlay_depth_prototype);
  destroy_proxy(group.shaded_prototype);
  destroy_proxy(group.rendered_prototype);
}

void ToolPreviewRenderableCache::clear() {
  const auto destroy_generation = [&](std::unique_ptr<PreviewGeneration> &gen) {
    if (!gen || !factory_) {
      gen.reset();
      return;
    }
    for (PreviewGroup &group : gen->groups) {
      destroy_group(group, gen->detached);
    }
    gen.reset();
  };
  destroy_generation(current_generation_);
  destroy_generation(warm_generation_);
  for (std::unique_ptr<PreviewGeneration> &generation :
       retired_generations_) {
    destroy_generation(generation);
  }
  retired_generations_.clear();
  submitted_meshes_.clear();
  mesh_signature_ = 0u;
  material_uniform_signature_ = 0u;
  material_uniforms_dirty_ = true;
}

std::size_t ToolPreviewRenderableCache::proxy_count() const {
  const auto generation_count = [](const PreviewGeneration *generation) {
    std::size_t count = 0u;
    if (generation) {
      for (const PreviewGroup &group : generation->groups) {
        count += group_proxy_count(group);
      }
    }
    return count;
  };
  std::size_t count = generation_count(current_generation_.get()) +
                      generation_count(warm_generation_.get());
  for (const std::unique_ptr<PreviewGeneration> &generation :
       retired_generations_) {
    count += generation_count(generation.get());
  }
  return count;
}

std::size_t ToolPreviewRenderableCache::group_proxy_count(
    const PreviewGroup &group) {
  const auto live_count = [](const std::vector<MeshRenderableProxy> &proxies) {
    return static_cast<std::size_t>(std::ranges::count_if(
        proxies, [](const MeshRenderableProxy &proxy) {
          return static_cast<bool>(proxy.entity);
        }));
  };
  return static_cast<std::size_t>(group.rendered_prototype.entity ? 1u : 0u) +
         static_cast<std::size_t>(group.shaded_prototype.entity ? 1u : 0u) +
         static_cast<std::size_t>(
             group.overlay_depth_prototype.entity ? 1u : 0u) +
         live_count(group.rendered_instances) +
         live_count(group.shaded_instances) +
         live_count(group.overlay_depth_instances);
}

std::uint64_t ToolPreviewRenderableCache::preview_material_signature(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  RenderSignatureBuilder signature(
      render_material_binding_signature(preview_mesh.material));
  signature.add_value(preview_mesh.material_revision)
      .add_value(static_cast<std::uint64_t>(
          preview_mesh.material_reference.kind()));
  if (preview_mesh.material_reference.project_owned()) {
    signature.add_string(preview_mesh.material_reference.project_asset().str());
  } else {
    signature.add_value(preview_mesh.material_reference.embedded_asset());
  }
  return signature.value();
}

std::uint64_t ToolPreviewRenderableCache::preview_geometry_signature(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  RenderSignatureBuilder signature;
  signature.add_value(preview_mesh.mesh.geometry_content_id().value)
      .add_value(preview_mesh.mesh.revision.value);
  if (preview_mesh.dynamic_positions) {
    signature.add_value(preview_mesh.layout_key)
        .add_value(preview_mesh.position_revision);
  }
  return signature.value();
}

std::uint64_t ToolPreviewRenderableCache::preview_topology_signature(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  const MeshGeometryPayload &geometry = preview_mesh.mesh.geometry();
  RenderSignatureBuilder signature;
  signature.add_size(geometry.vertices.size())
      .add_size(geometry.indices.size());
  for (const MeshVertex &vertex : geometry.vertices) {
    signature.add_value(vertex.source_vertex_id)
        .add_vec3(vertex.normal)
        .add_vec2(vertex.uv)
        .add_value(vertex.rgba);
  }
  for (const std::uint32_t index : geometry.indices) {
    signature.add_value(index);
  }
  signature.add_size(geometry.primitives.size());
  for (const MeshPrimitiveSpan primitive : geometry.primitives) {
    signature.add_value(primitive.index_offset)
        .add_value(primitive.index_count)
        .add_value(primitive.material_slot);
  }
  return signature.value();
}

std::uint64_t ToolPreviewRenderableCache::preview_structural_reuse_signature(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  return RenderSignatureBuilder{}
      .add_value(preview_topology_signature(preview_mesh))
      .add_value(preview_material_signature(preview_mesh))
      .add_bool(preview_mesh.dynamic_positions)
      .add_size(preview_transform_count(preview_mesh))
      .value();
}

std::uint64_t ToolPreviewRenderableCache::group_structural_reuse_signature(
    const PreviewGroup &group) {
  return RenderSignatureBuilder{}
      .add_value(group.topology_signature)
      .add_value(group.material_signature)
      .add_bool(group.dynamic_positions)
      .add_size(group.transforms.size())
      .value();
}

bool ToolPreviewRenderableCache::group_matches_exact(
    const PreviewGroup &group,
    const RenderToolPreviewMeshRecord &preview_mesh) {
  if (group.source_entity != preview_mesh.source_entity ||
      group.material_signature != preview_material_signature(preview_mesh) ||
      group.dynamic_positions != preview_mesh.dynamic_positions ||
      group.transforms.size() != preview_transform_count(preview_mesh)) {
    return false;
  }
  if (!preview_mesh.dynamic_positions) {
    if (group.geometry_signature != preview_geometry_signature(preview_mesh)) {
      return false;
    }
    const std::shared_ptr<const MeshGeometryPayload> geometry_payload =
        preview_mesh.mesh.geometry_payload();
    return same_geometry_payload(group.topology_payload, geometry_payload,
                                 true);
  }
  const bool layout_compatible =
      group.layout_key == preview_mesh.layout_key || group.layout_key == 0u ||
      preview_mesh.layout_key == 0u;
  const std::shared_ptr<const MeshGeometryPayload> topology_payload =
      preview_mesh.mesh.geometry_payload();
  if (group.layout_key != 0u &&
      group.layout_key == preview_mesh.layout_key &&
      group.topology_payload.get() == topology_payload.get()) {
    return true;
  }
  return layout_compatible &&
         group.topology_signature == preview_topology_signature(preview_mesh) &&
         group_topology_equals(group, preview_mesh) &&
         group.dynamic_positions;
}

bool ToolPreviewRenderableCache::group_matches_structure(
    const PreviewGroup &group,
    const RenderToolPreviewMeshRecord &preview_mesh) {
  return group.dynamic_positions && preview_mesh.dynamic_positions &&
         group.topology_signature == preview_topology_signature(preview_mesh) &&
         group_topology_equals(group, preview_mesh) &&
         group.material_signature == preview_material_signature(preview_mesh) &&
         group.transforms.size() == preview_transform_count(preview_mesh);
}

bool ToolPreviewRenderableCache::group_topology_equals(
    const PreviewGroup &group,
    const RenderToolPreviewMeshRecord &preview_mesh) {
  return same_geometry_payload(group.topology_payload,
                               preview_mesh.mesh.geometry_payload(), false);
}

bool ToolPreviewRenderableCache::group_dynamic_update_is_valid(
    const PreviewGroup &group,
    const RenderToolPreviewMeshRecord &preview_mesh) const {
  const MeshGeometryPayload &geometry = preview_mesh.mesh.geometry();
  const std::span<const QVec3> positions =
      preview_dynamic_positions(preview_mesh);
  // This mirrors every guard in MeshRenderableFactory::update_dynamic_geometry.
  // Complete generations separately guarantee sibling/vector shape. With the
  // factory confined to the render thread, these guards remain true throughout
  // prepare_generation's following non-failing mutation pass.
  return factory_ != nullptr && factory_->initialized() &&
         group.rendered_prototype.dynamic_geometry &&
         group.rendered_prototype.vertex_buffer != nullptr &&
         group.rendered_prototype.vertex_count == geometry.vertices.size() &&
         group.rendered_prototype.index_count == geometry.indices.size() &&
         (positions.empty() || positions.size() == geometry.vertices.size());
}

bool ToolPreviewRenderableCache::submitted_meshes_equal(
    const std::vector<PreviewSubmissionState> &submitted,
    const std::vector<RenderToolPreviewMeshRecord> &meshes) {
  if (submitted.size() != meshes.size()) {
    return false;
  }
  for (std::size_t index = 0u; index < meshes.size(); ++index) {
    const PreviewSubmissionState &left = submitted[index];
    const RenderToolPreviewMeshRecord &right = meshes[index];
    if (left.source_entity != right.source_entity ||
        left.visibility_entity != right.visibility_entity ||
        left.material_reference != right.material_reference ||
        left.material_signature != preview_material_signature(right) ||
        left.material_revision != right.material_revision ||
        left.mesh_revision != right.mesh.revision.value ||
        left.dynamic_positions != right.dynamic_positions ||
        left.suppress_source != right.suppress_source ||
        !same_geometry_payload(left.geometry_payload,
                               right.mesh.geometry_payload(), true) ||
        !same_mat4(left.transform, right.transform) ||
        left.instance_transforms.size() !=
            right.instance_transforms.size() ||
        !std::equal(left.instance_transforms.begin(),
                    left.instance_transforms.end(),
                    right.instance_transforms.begin(), same_mat4)) {
      return false;
    }
    if (right.dynamic_positions &&
        (left.layout_key != right.layout_key ||
         left.position_revision != right.position_revision)) {
      return false;
    }
  }
  return true;
}

std::vector<ToolPreviewRenderableCache::PreviewSubmissionState>
ToolPreviewRenderableCache::capture_submission(
    const std::vector<RenderToolPreviewMeshRecord> &meshes) {
  std::vector<PreviewSubmissionState> captured;
  captured.reserve(meshes.size());
  for (const RenderToolPreviewMeshRecord &mesh : meshes) {
    captured.push_back({
        .source_entity = mesh.source_entity,
        .visibility_entity = mesh.visibility_entity,
        .material_reference = mesh.material_reference,
        .material_signature = preview_material_signature(mesh),
        .material_revision = mesh.material_revision,
        .mesh_revision = mesh.mesh.revision.value,
        .layout_key = mesh.layout_key,
        .position_revision = mesh.position_revision,
        .geometry_payload = mesh.mesh.geometry_payload(),
        .transform = mesh.transform,
        .instance_transforms = mesh.instance_transforms,
        .dynamic_positions = mesh.dynamic_positions,
        .suppress_source = mesh.suppress_source,
    });
  }
  return captured;
}

bool ToolPreviewRenderableCache::active_material_uniforms_are_current(
    const PreviewGeneration &generation) const {
  for (const std::size_t index : generation.active_group_indices) {
    if (index >= generation.groups.size()) {
      continue;
    }
    const PreviewGroup &group = generation.groups[index];
    if (group.visible &&
        (!group.material_uniforms_initialized ||
         group.material_uniform_signature != material_uniform_signature_)) {
      return false;
    }
  }
  return true;
}

std::uint64_t ToolPreviewRenderableCache::preview_meshes_signature(
    const std::vector<RenderToolPreviewMeshRecord> &preview_meshes) {
  RenderSignatureBuilder signature;
  signature.add_size(preview_meshes.size());
  for (const RenderToolPreviewMeshRecord &preview_mesh : preview_meshes) {
    signature.add_value(preview_mesh.source_entity)
        .add_value(preview_mesh.visibility_entity)
        .add_value(preview_material_signature(preview_mesh))
        .add_value(preview_geometry_signature(preview_mesh))
        .add_bool(preview_mesh.dynamic_positions)
        .add_bool(preview_mesh.suppress_source);
    const std::size_t transform_count = preview_transform_count(preview_mesh);
    signature.add_size(transform_count);
    for (std::size_t index = 0u; index < transform_count; ++index) {
      signature.add_mat4(preview_transform(preview_mesh, index));
    }
  }
  return signature.value();
}

} // namespace quader
