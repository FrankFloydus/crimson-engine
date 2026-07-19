////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/tool_preview_renderable_cache.hpp"

#include "render/material_binding_service.hpp"
#include "diagnostics/profile.hpp"
#include "render/render_layers.hpp"
#include "render/render_signature_builder.hpp"

#include <algorithm>

namespace quader {
namespace {

std::size_t
preview_transform_count(const RenderToolPreviewMeshRecord &preview_mesh) {
  return preview_mesh.instance_transforms.empty()
             ? 1U
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
  for (std::size_t index = 0; index < count; ++index) {
    destination[index] = preview_transform(preview_mesh, index);
  }
}

std::span<const QVec3>
preview_dynamic_positions(const RenderToolPreviewMeshRecord &preview_mesh) {
  return preview_mesh.dynamic_position_payload
             ? std::span<const QVec3>(*preview_mesh.dynamic_position_payload)
             : std::span<const QVec3>{};
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

bool ToolPreviewRenderableCache::set_meshes(
    const std::vector<RenderToolPreviewMeshRecord> &meshes) {
  QDR_PROFILE_FUNCTION();
  if (!factory_ || !material_binding_) {
    return false;
  }

  const std::uint64_t next_signature = preview_meshes_signature(meshes);
  if (next_signature == mesh_signature_) {
    return false;
  }

  if (meshes.empty() && !preview_groups_.empty() &&
      std::ranges::all_of(preview_groups_, &PreviewGroup::dynamic_positions)) {
    QDR_PROFILE_SCOPE("Tool preview retain dynamic groups");
    set_preview_groups_visible(false);
    mesh_signature_ = next_signature;
    return true;
  }

  bool dynamic_positions_only = meshes.size() == preview_groups_.size();
  if (dynamic_positions_only) {
    for (std::size_t index = 0; index < meshes.size(); ++index) {
      if (!meshes[index].dynamic_positions ||
          !preview_groups_[index].dynamic_positions ||
          meshes[index].layout_key == 0u ||
          meshes[index].layout_key != preview_groups_[index].layout_key ||
          preview_topology_signature(meshes[index]) !=
              preview_groups_[index].topology_signature ||
          preview_transform_count(meshes[index]) !=
              preview_groups_[index].transforms.size()) {
        dynamic_positions_only = false;
        break;
      }
    }
  }
  if (dynamic_positions_only) {
    bool updated = true;
    for (std::size_t index = 0; index < meshes.size(); ++index) {
      PreviewGroup &group = preview_groups_[index];
      const RenderMeshAssetSnapshot mesh_snapshot =
          render_mesh_asset_snapshot(kInvalidAssetId, meshes[index].mesh);
      const std::span<const QVec3> positions =
          preview_dynamic_positions(meshes[index]);
      if (!factory_->update_dynamic_geometry(group.prototype, mesh_snapshot,
                                             positions)) {
        updated = false;
        break;
      }
      if (positions.empty()) {
        factory_->update_bounds(group.overlay_depth_prototype, mesh_snapshot);
      } else {
        factory_->update_bounds(group.overlay_depth_prototype, positions);
      }
      group.geometry_signature = preview_geometry_signature(meshes[index]);
      copy_preview_transforms(group.transforms, meshes[index]);
      factory_->update_transform(group.prototype, group.transforms.front());
      factory_->update_transform(group.overlay_depth_prototype,
                                 group.transforms.front());
      for (std::size_t instance = 0; instance < group.instances.size();
           ++instance) {
        if (positions.empty()) {
          factory_->update_bounds(group.instances[instance], mesh_snapshot);
        } else {
          factory_->update_bounds(group.instances[instance], positions);
        }
        factory_->update_transform(group.instances[instance],
                                   group.transforms[instance + 1u]);
        if (positions.empty()) {
          factory_->update_bounds(group.overlay_depth_instances[instance],
                                  mesh_snapshot);
        } else {
          factory_->update_bounds(group.overlay_depth_instances[instance],
                                  positions);
        }
        factory_->update_transform(group.overlay_depth_instances[instance],
                                   group.transforms[instance + 1u]);
      }
    }
    if (updated) {
      set_preview_groups_visible(true);
      mesh_signature_ = next_signature;
      return true;
    }
  }

  bool transforms_only = meshes.size() == preview_groups_.size();
  if (transforms_only) {
    for (std::size_t index = 0; index < meshes.size(); ++index) {
      if (preview_geometry_signature(meshes[index]) !=
              preview_groups_[index].geometry_signature ||
          preview_transform_count(meshes[index]) !=
              preview_groups_[index].transforms.size()) {
        transforms_only = false;
        break;
      }
    }
  }
  if (transforms_only) {
    for (std::size_t index = 0; index < meshes.size(); ++index) {
      PreviewGroup &group = preview_groups_[index];
      copy_preview_transforms(group.transforms, meshes[index]);
      factory_->update_transform(group.prototype, group.transforms.front());
      factory_->update_transform(group.overlay_depth_prototype,
                                 group.transforms.front());
      for (std::size_t instance = 0; instance < group.instances.size();
           ++instance) {
        factory_->update_transform(group.instances[instance],
                                   group.transforms[instance + 1u]);
        factory_->update_transform(group.overlay_depth_instances[instance],
                                   group.transforms[instance + 1u]);
      }
    }
    mesh_signature_ = next_signature;
    return true;
  }

  clear();
  mesh_signature_ = next_signature;
  preview_groups_.reserve(meshes.size());
  for (const RenderToolPreviewMeshRecord &preview_mesh : meshes) {
    if (!preview_mesh.renderable()) {
      continue;
    }
    std::vector<QMat4> transforms;
    copy_preview_transforms(transforms, preview_mesh);
    if (transforms.empty()) {
      continue;
    }
    const RenderMeshAssetSnapshot mesh_snapshot =
        render_mesh_asset_snapshot(kInvalidAssetId, preview_mesh.mesh);
    MeshRenderableProxy prototype = factory_->create(
        preview_mesh.source_entity, mesh_snapshot, &preview_mesh.material,
        transforms.front(), false, true, kViewportToolPreviewLayerMask,
        true, false, kViewportDefaultRenderableChannel,
        preview_mesh.dynamic_positions,
        preview_dynamic_positions(preview_mesh));
    MeshRenderableProxy overlay_depth_prototype =
        factory_->create_shared_geometry(
            preview_mesh.source_entity, mesh_snapshot, &preview_mesh.material,
            prototype, transforms.front(), true, kViewportOverlayLayerMask,
            true, true, kViewportOverlayDepthChannel);
    const std::span<const QVec3> positions =
        preview_dynamic_positions(preview_mesh);
    if (!positions.empty()) {
      factory_->update_bounds(overlay_depth_prototype, positions);
    }
    PreviewGroup group{
        .geometry_signature = preview_geometry_signature(preview_mesh),
        .topology_signature = preview_topology_signature(preview_mesh),
        .layout_key = preview_mesh.layout_key,
        .transforms = std::move(transforms),
        .dynamic_positions = preview_mesh.dynamic_positions,
        .prototype = std::move(prototype),
        .overlay_depth_prototype = std::move(overlay_depth_prototype),
    };
    group.instances.reserve(group.transforms.size() - 1u);
    group.overlay_depth_instances.reserve(group.transforms.size() - 1u);
    for (std::size_t index = 1u; index < group.transforms.size(); ++index) {
      group.instances.push_back(factory_->create_shared_instance(
          preview_mesh.source_entity, mesh_snapshot, group.prototype,
          group.transforms[index], kViewportToolPreviewLayerMask));
      group.overlay_depth_instances.push_back(
          factory_->create_shared_instance(
              preview_mesh.source_entity, mesh_snapshot,
              group.overlay_depth_prototype, group.transforms[index],
              kViewportOverlayLayerMask, kViewportOverlayDepthChannel));
      if (!positions.empty()) {
        factory_->update_bounds(group.instances.back(), positions);
        factory_->update_bounds(group.overlay_depth_instances.back(),
                                positions);
      }
    }
    preview_groups_.push_back(std::move(group));
  }
  return true;
}

void ToolPreviewRenderableCache::update_shaded_lighting(
    const ViewportSettings &settings) {
  if (!factory_) {
    return;
  }
  for (const PreviewGroup &group : preview_groups_) {
    factory_->update_shaded_lighting(group.prototype, settings);
  }
}

void ToolPreviewRenderableCache::clear() {
    if (factory_) {
      for (PreviewGroup &group : preview_groups_) {
        for (MeshRenderableProxy &instance : group.instances) {
          factory_->destroy(instance);
        }
        for (MeshRenderableProxy &instance : group.overlay_depth_instances) {
          factory_->destroy(instance);
        }
        factory_->destroy(group.overlay_depth_prototype);
        factory_->destroy(group.prototype);
      }
    }
  preview_groups_.clear();
  mesh_signature_ = 0;
  preview_groups_visible_ = true;
}

void ToolPreviewRenderableCache::set_preview_groups_visible(bool visible) {
  if (!factory_ || preview_groups_visible_ == visible) {
    return;
  }
  const std::uint8_t layer_mask =
      visible ? kViewportToolPreviewLayerMask : 0u;
  for (const PreviewGroup &group : preview_groups_) {
    factory_->update_layer_mask(group.prototype, layer_mask);
    for (const MeshRenderableProxy &instance : group.instances) {
      factory_->update_layer_mask(instance, layer_mask);
    }
    const std::uint8_t overlay_depth_layer_mask =
        visible ? kViewportOverlayLayerMask : 0u;
    factory_->update_layer_mask(group.overlay_depth_prototype,
                                overlay_depth_layer_mask);
    for (const MeshRenderableProxy &instance :
         group.overlay_depth_instances) {
      factory_->update_layer_mask(instance, overlay_depth_layer_mask);
    }
  }
  preview_groups_visible_ = visible;
}

std::size_t ToolPreviewRenderableCache::proxy_count() const {
  std::size_t count = 0u;
  for (const PreviewGroup &group : preview_groups_) {
    count += 2u + group.instances.size() +
             group.overlay_depth_instances.size();
  }
  return count;
}

std::uint64_t ToolPreviewRenderableCache::preview_geometry_signature(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  RenderSignatureBuilder signature;
  signature.add_value(preview_mesh.source_entity)
      .add_value(preview_mesh.material_id)
      .add_value(preview_mesh.material_revision);
  if (preview_mesh.dynamic_positions && preview_mesh.layout_key != 0u) {
    return signature.add_value(preview_mesh.layout_key)
        .add_value(preview_mesh.position_revision)
        .value();
  }
  return MeshRenderableFactory::mesh_asset_signature(signature.value(),
                                                     preview_mesh.mesh);
}

std::uint64_t ToolPreviewRenderableCache::preview_topology_signature(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  RenderSignatureBuilder signature;
  signature.add_value(preview_mesh.source_entity)
      .add_value(preview_mesh.material_id)
      .add_value(preview_mesh.material_revision)
      .add_value(preview_mesh.dynamic_positions)
      .add_value(preview_mesh.suppress_source);
  const MeshGeometryPayload &geometry = preview_mesh.mesh.geometry();
  signature.add_size(geometry.vertices.size());
  signature.add_size(geometry.indices.size());
  if (preview_mesh.dynamic_positions && preview_mesh.layout_key != 0u) {
    return signature.add_value(preview_mesh.layout_key).value();
  }
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

std::uint64_t ToolPreviewRenderableCache::preview_meshes_signature(
    const std::vector<RenderToolPreviewMeshRecord> &preview_meshes) {
  RenderSignatureBuilder signature;
  signature.add_size(preview_meshes.size());
  for (const RenderToolPreviewMeshRecord &preview_mesh : preview_meshes) {
    if (preview_mesh.dynamic_positions && preview_mesh.layout_key != 0u) {
      signature.add_value(preview_mesh.source_entity)
          .add_value(preview_mesh.material_id)
          .add_value(preview_mesh.material_revision)
          .add_value(preview_mesh.layout_key)
          .add_value(preview_mesh.position_revision)
          .add_value(preview_mesh.suppress_source);
    } else {
      signature.add_value(preview_geometry_signature(preview_mesh))
          .add_value(preview_mesh.dynamic_positions)
          .add_value(preview_mesh.suppress_source);
    }
    const std::size_t transform_count = preview_transform_count(preview_mesh);
    signature.add_size(transform_count);
    for (std::size_t index = 0; index < transform_count; ++index) {
      signature.add_mat4(preview_transform(preview_mesh, index));
    }
  }
  return signature.value();
}

} // namespace quader
