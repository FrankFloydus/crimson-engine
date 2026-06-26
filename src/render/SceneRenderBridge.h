////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
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

/**
 * Owns Scene Render Bridge behavior within the render extraction and Filament resource layer.
 */
class SceneRenderBridge {
public:
    using PreviewMesh = ToolPreviewMesh;

    SceneRenderBridge() = default;
    SceneRenderBridge(const SceneRenderBridge&) = delete;
    SceneRenderBridge& operator=(const SceneRenderBridge&) = delete;
    SceneRenderBridge(SceneRenderBridge&&) = delete;
    SceneRenderBridge& operator=(SceneRenderBridge&&) = delete;
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
