////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/SceneRenderBridge.h"

#include <utility>

namespace quader {

SceneRenderBridge::~SceneRenderBridge() {
    shutdown();
}

void SceneRenderBridge::initialize(filament::Engine& engine,
        filament::Scene& scene,
        filament::Material& meshMaterial,
        filament::Material& overlayMaterial,
        filament::Material& overlayLineMaterial,
        filament::Material& overlayPointMaterial,
        std::filesystem::path resourceRoot) {
    engine_ = &engine;
    scene_ = &scene;
    textureBinding_.initialize(engine, std::move(resourceRoot));
    materialBinding_.initialize(meshMaterial, textureBinding_);
    meshFactory_.initialize(engine, scene, materialBinding_);
    meshCache_.initialize(meshFactory_);
    toolPreviewCache_.initialize(meshFactory_, materialBinding_);
    overlayProxyFactory_.initialize(engine, scene, overlayMaterial, overlayLineMaterial, overlayPointMaterial);
    overlayCache_.initialize(overlayProxyFactory_);
    syncService_.initialize(engine, scene, meshCache_, materialBinding_);
}

void SceneRenderBridge::shutdown() {
    if (!engine_) {
        return;
    }

    toolPreviewCache_.shutdown();
    syncService_.shutdown();
    meshCache_.shutdown();
    overlayCache_.shutdown();
    overlayProxyFactory_.shutdown();
    meshFactory_.shutdown();
    materialBinding_.shutdown();
    textureBinding_.shutdown();

    engine_ = nullptr;
    scene_ = nullptr;
}

void SceneRenderBridge::sync(const SceneDocument& document, const AssetRegistry& assets) {
    if (!engine_ || !scene_ || !syncService_.initialized()) {
        return;
    }
    (void)syncService_.sync(document, assets);
}

void SceneRenderBridge::setToolPreviewMeshes(std::vector<PreviewMesh> meshes, const AssetRegistry& assets) {
    if (!engine_ || !scene_ || !toolPreviewCache_.initialized()) {
        return;
    }
    (void)toolPreviewCache_.setMeshes(meshes, assets);
}

void SceneRenderBridge::setOverlayFrame(ViewportOverlayFrame frame, ViewportOverlayCamera camera) {
    (void)overlayCache_.setOverlayFrame(std::move(frame), camera);
}

void SceneRenderBridge::setOverlayFrame(
        std::size_t viewportIndex,
        std::uint8_t layerMask,
        ViewportOverlayFrame frame,
        ViewportOverlayCamera camera) {
    (void)overlayCache_.setOverlayFrame(viewportIndex, layerMask, std::move(frame), camera);
}

} // namespace quader
