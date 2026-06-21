#pragma once

#include "assets/AssetRegistry.h"
#include "core/Types.h"
#include "math/Types.h"
#include "render/MeshRenderableFactory.h"
#include "render/MeshRenderableProxy.h"

#include <cstdint>
#include <vector>

namespace quader {

class MaterialBindingService;

struct ToolPreviewMesh {
    EntityId sourceEntity = InvalidEntityId;
    AssetId material = InvalidAssetId;
    MeshAsset mesh;
    Mat4 transform;
};

class ToolPreviewRenderableCache {
public:
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
