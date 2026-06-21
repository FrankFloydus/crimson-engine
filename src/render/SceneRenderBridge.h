#pragma once

#include "assets/AssetRegistry.h"
#include "core/Types.h"
#include "render/MaterialBindingService.h"
#include "render/MeshRenderableCache.h"
#include "render/MeshRenderableFactory.h"
#include "render/SceneRenderSyncService.h"
#include "render/TextureBindingService.h"
#include "render/ToolPreviewRenderableCache.h"
#include "render/ViewportOverlayFrame.h"
#include "render/overlay/OverlayProxyFactory.h"
#include "render/overlay/OverlayRenderableCache.h"
#include "scene/SceneDocument.h"

#include <filament/Engine.h>
#include <filament/Material.h>
#include <filament/Scene.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace quader {

class SceneRenderBridge {
public:
    using PreviewMesh = ToolPreviewMesh;

    ~SceneRenderBridge();

    void initialize(filament::Engine& engine,
            filament::Scene& scene,
            filament::Material& meshMaterial,
            filament::Material& overlayMaterial,
            filament::Material& overlayLineMaterial,
            filament::Material& overlayPointMaterial,
            std::filesystem::path resourceRoot);
    void shutdown();
    void sync(const SceneDocument& document, const AssetRegistry& assets);
    void setToolPreviewMeshes(std::vector<PreviewMesh> meshes, const AssetRegistry& assets);
    void setOverlayFrame(ViewportOverlayFrame frame, ViewportOverlayCamera camera);
    void setOverlayFrame(std::size_t viewportIndex,
            std::uint8_t layerMask,
            ViewportOverlayFrame frame,
            ViewportOverlayCamera camera);

private:
    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    TextureBindingService textureBinding_;
    MaterialBindingService materialBinding_;
    MeshRenderableFactory meshFactory_;
    MeshRenderableCache meshCache_;
    ToolPreviewRenderableCache toolPreviewCache_;
    OverlayProxyFactory overlayProxyFactory_;
    OverlayRenderableCache overlayCache_;
    SceneRenderSyncService syncService_;
};

} // namespace quader
