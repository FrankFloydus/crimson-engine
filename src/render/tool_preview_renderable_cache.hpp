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
    EntityId sourceEntity = InvalidEntityId;
    AssetId material = InvalidAssetId;
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

    void initialize(MeshRenderableFactory& factory, MaterialBindingService& materialBinding);
    void shutdown();

    bool initialized() const {
        return factory_ != nullptr && materialBinding_ != nullptr;
    }

    bool setMeshes(const std::vector<ToolPreviewMesh>& meshes, const AssetRegistry& assets);
    void clear();

    std::uint64_t signature() const {
        return meshSignature_;
    }

    std::size_t proxyCount() const {
        return meshProxies_.size();
    }

private:
    static std::uint64_t previewMeshSignature(
            const ToolPreviewMesh& previewMesh,
            const AssetRegistry& assets,
            const MaterialBindingService& materialBinding);
    static std::uint64_t previewMeshesSignature(
            const std::vector<ToolPreviewMesh>& previewMeshes,
            const AssetRegistry& assets,
            const MaterialBindingService& materialBinding);

    MeshRenderableFactory* factory_ = nullptr;
    MaterialBindingService* materialBinding_ = nullptr;
    std::vector<MeshRenderableProxy> meshProxies_;
    std::uint64_t meshSignature_ = 0;
};

} // namespace quader
