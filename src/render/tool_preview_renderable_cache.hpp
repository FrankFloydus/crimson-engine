////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "core/types.hpp"
#include "math/types.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/mesh_renderable_proxy.hpp"

#include <cstdint>
#include <vector>

namespace quader {

class MaterialBindingService;

/**
 * Represents a Tool Preview Mesh value used by the render extraction and Filament resource layer.
 */
struct ToolPreviewMesh {
  EntityId source_entity = kInvalidEntityId;
  AssetId material = kInvalidAssetId;
  MeshAsset mesh;
  QMat4 transform;
};

/**
 * Caches Tool Preview Renderable Cache derived state for the render extraction and Filament resource layer.
 */
class ToolPreviewRenderableCache {
public:
    ToolPreviewRenderableCache() = default;
    ToolPreviewRenderableCache(const ToolPreviewRenderableCache&) = delete;
    ToolPreviewRenderableCache& operator=(const ToolPreviewRenderableCache&) = delete;
    ToolPreviewRenderableCache(ToolPreviewRenderableCache&&) = delete;
    ToolPreviewRenderableCache& operator=(ToolPreviewRenderableCache&&) = delete;
    ~ToolPreviewRenderableCache();

    void initialize(MeshRenderableFactory &factory,
                    MaterialBindingService &material_binding);
    void shutdown();

    bool initialized() const {
      return factory_ != nullptr && material_binding_ != nullptr;
    }

    bool set_meshes(const std::vector<ToolPreviewMesh> &meshes,
                    const AssetRegistry &assets);
    void clear();

    std::uint64_t signature() const { return mesh_signature_; }

    std::size_t proxy_count() const { return mesh_proxies_.size(); }

  private:
    static std::uint64_t
    preview_mesh_signature(const ToolPreviewMesh &preview_mesh,
                           const AssetRegistry &assets,
                           const MaterialBindingService &material_binding);
    static std::uint64_t
    preview_meshes_signature(const std::vector<ToolPreviewMesh> &preview_meshes,
                             const AssetRegistry &assets,
                             const MaterialBindingService &material_binding);

    MeshRenderableFactory* factory_ = nullptr;
    MaterialBindingService *material_binding_ = nullptr;
    std::vector<MeshRenderableProxy> mesh_proxies_;
    std::uint64_t mesh_signature_ = 0;
};

} // namespace quader
