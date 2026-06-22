#pragma once

#include "assets/AssetRegistry.h"
#include "core/Types.h"
#include "math/Types.h"
#include "render/MeshRenderableProxy.h"
#include "scene/SceneDocument.h"

#include <filament/Engine.h>
#include <filament/Scene.h>

#include <cstdint>

namespace quader {

class MaterialBindingService;

class MeshRenderableFactory {
public:
    void initialize(filament::Engine& engine, filament::Scene& scene, MaterialBindingService& materialBinding);
    void shutdown();

    bool initialized() const {
        return engine_ != nullptr && scene_ != nullptr && materialBinding_ != nullptr;
    }

    MeshRenderableProxy create(EntityId entityId,
            AssetId meshId,
            const MeshAsset& mesh,
            AssetId materialId,
            const MaterialDocument* material,
            const AssetRegistry& assets,
            const Mat4& worldTransform,
            bool castShadows,
            bool doubleSided,
            std::uint64_t materialRevision);
    void updateTransform(const MeshRenderableProxy& proxy, const Mat4& worldTransform);
    void destroy(MeshRenderableProxy& proxy);

    static std::uint64_t meshAssetSignature(std::uint64_t hash, const MeshAsset& mesh);

private:
    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    MaterialBindingService* materialBinding_ = nullptr;
};

} // namespace quader
