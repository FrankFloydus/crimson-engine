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

  clear();
  mesh_signature_ = next_signature;
  mesh_proxies_.reserve(meshes.size());
  for (const RenderToolPreviewMeshRecord &preview_mesh : meshes) {
    if (!preview_mesh.renderable()) {
      continue;
    }
    const RenderMeshAssetSnapshot mesh_snapshot =
        render_mesh_asset_snapshot(kInvalidAssetId, preview_mesh.mesh);
    mesh_proxies_.push_back(factory_->create(
        preview_mesh.source_entity, mesh_snapshot, &preview_mesh.material,
        preview_mesh.transform, false, true, kViewportToolPreviewLayerMask,
        true));
  }
  return true;
}

void ToolPreviewRenderableCache::clear() {
    if (factory_) {
      for (MeshRenderableProxy &proxy : mesh_proxies_) {
        factory_->destroy(proxy);
      }
    }
    mesh_proxies_.clear();
    mesh_signature_ = 0;
}

std::uint64_t ToolPreviewRenderableCache::preview_mesh_signature(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  RenderSignatureBuilder signature;
  signature.add_value(preview_mesh.source_entity)
      .add_value(preview_mesh.material_id)
      .add_value(preview_mesh.material_revision)
      .add_mat4(preview_mesh.transform);
  return MeshRenderableFactory::mesh_asset_signature(signature.value(),
                                                     preview_mesh.mesh);
}

std::uint64_t ToolPreviewRenderableCache::preview_meshes_signature(
    const std::vector<RenderToolPreviewMeshRecord> &preview_meshes) {
  RenderSignatureBuilder signature;
  signature.add_size(preview_meshes.size());
  for (const RenderToolPreviewMeshRecord &preview_mesh : preview_meshes) {
    signature.add_value(preview_mesh_signature(preview_mesh));
  }
  return signature.value();
}

} // namespace quader
