////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/tool_preview_renderable_cache.hpp"

#include "render/material_binding_service.hpp"
#include "render/render_layers.hpp"
#include "render/render_signature_builder.hpp"

namespace quader {
namespace {

std::vector<QMat4>
preview_transforms(const RenderToolPreviewMeshRecord &preview_mesh) {
  return preview_mesh.instance_transforms.empty()
             ? std::vector<QMat4>{preview_mesh.transform}
             : preview_mesh.instance_transforms;
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
  if (!factory_ || !material_binding_) {
    return false;
  }

  const std::uint64_t next_signature = preview_meshes_signature(meshes);
  if (next_signature == mesh_signature_) {
    return false;
  }

  bool transforms_only = meshes.size() == preview_groups_.size();
  if (transforms_only) {
    for (std::size_t index = 0; index < meshes.size(); ++index) {
      const std::vector<QMat4> transforms = preview_transforms(meshes[index]);
      if (preview_geometry_signature(meshes[index]) !=
              preview_groups_[index].geometry_signature ||
          transforms.size() != preview_groups_[index].transforms.size()) {
        transforms_only = false;
        break;
      }
    }
  }
  if (transforms_only) {
    for (std::size_t index = 0; index < meshes.size(); ++index) {
      PreviewGroup &group = preview_groups_[index];
      group.transforms = preview_transforms(meshes[index]);
      factory_->update_transform(group.prototype, group.transforms.front());
      for (std::size_t instance = 0; instance < group.instances.size();
           ++instance) {
        factory_->update_transform(group.instances[instance],
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
    std::vector<QMat4> transforms = preview_transforms(preview_mesh);
    if (transforms.empty()) {
      continue;
    }
    const RenderMeshAssetSnapshot mesh_snapshot =
        render_mesh_asset_snapshot(kInvalidAssetId, preview_mesh.mesh);
    MeshRenderableProxy prototype = factory_->create(
        preview_mesh.source_entity, mesh_snapshot, &preview_mesh.material,
        transforms.front(), false, true, kViewportToolPreviewLayerMask,
        true);
    PreviewGroup group{
        .geometry_signature = preview_geometry_signature(preview_mesh),
        .transforms = std::move(transforms),
        .prototype = std::move(prototype),
    };
    group.instances.reserve(group.transforms.size() - 1u);
    for (std::size_t index = 1u; index < group.transforms.size(); ++index) {
      group.instances.push_back(factory_->create_shared_instance(
          preview_mesh.source_entity, mesh_snapshot, group.prototype,
          group.transforms[index], kViewportToolPreviewLayerMask));
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
        factory_->destroy(group.prototype);
      }
    }
    preview_groups_.clear();
    mesh_signature_ = 0;
}

std::size_t ToolPreviewRenderableCache::proxy_count() const {
  std::size_t count = 0u;
  for (const PreviewGroup &group : preview_groups_) {
    count += 1u + group.instances.size();
  }
  return count;
}

std::uint64_t ToolPreviewRenderableCache::preview_geometry_signature(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  RenderSignatureBuilder signature;
  signature.add_value(preview_mesh.source_entity)
      .add_value(preview_mesh.material_id)
      .add_value(preview_mesh.material_revision);
  return MeshRenderableFactory::mesh_asset_signature(signature.value(),
                                                     preview_mesh.mesh);
}

std::uint64_t ToolPreviewRenderableCache::preview_meshes_signature(
    const std::vector<RenderToolPreviewMeshRecord> &preview_meshes) {
  RenderSignatureBuilder signature;
  signature.add_size(preview_meshes.size());
  for (const RenderToolPreviewMeshRecord &preview_mesh : preview_meshes) {
    signature.add_value(preview_geometry_signature(preview_mesh));
    const std::vector<QMat4> transforms = preview_transforms(preview_mesh);
    signature.add_size(transforms.size());
    for (const QMat4 &transform : transforms) {
      signature.add_mat4(transform);
    }
  }
  return signature.value();
}

} // namespace quader
