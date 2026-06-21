#pragma once

#include "core/Types.h"
#include "render/LightRenderableCache.h"
#include "scene/SceneDocument.h"

#include <filament/Engine.h>
#include <filament/Scene.h>

#include <cstdint>

namespace quader {

class AssetRegistry;
class MaterialBindingService;
class MeshRenderableCache;

class SceneRenderSyncService {
public:
    ~SceneRenderSyncService();

    void initialize(filament::Engine& engine,
            filament::Scene& scene,
            MeshRenderableCache& meshCache,
            MaterialBindingService& materialBinding);
    void shutdown();

    bool initialized() const {
        return engine_ != nullptr && scene_ != nullptr && meshCache_ != nullptr && materialBinding_ != nullptr;
    }

    bool sync(const SceneDocument& document, const AssetRegistry& assets);
    bool renderAssetRevisionsChanged(const SceneDocument& document, const AssetRegistry& assets) const;

private:
    void rebuild(const SceneDocument& document, const AssetRegistry& assets);

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    MeshRenderableCache* meshCache_ = nullptr;
    MaterialBindingService* materialBinding_ = nullptr;
    std::uint64_t syncedRevision_ = 0;
    std::uint64_t syncedAssetRevision_ = 0;
    bool hasSynced_ = false;
    LightRenderableCache lightCache_;
};

} // namespace quader
