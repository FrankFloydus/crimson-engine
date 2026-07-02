////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/scene_render_bridge.hpp"

#include "diagnostics/profile.hpp"

#include <utility>

namespace quader {

SceneRenderBridge::~SceneRenderBridge() {
    shutdown();
}

void SceneRenderBridge::initialize(filament::Engine& engine,
        filament::Scene& scene,
        filament::Material& shadedMeshMaterial,
        filament::Material& renderedMeshMaterial,
        filament::Material& overlayMaterial,
        filament::Material& overlayLineMaterial,
        filament::Material& selectedObjectLineMaterial,
        filament::Material& overlayPointMaterial,
        std::filesystem::path resourceRoot) {
    shutdown();
    engine_ = &engine;
    scene_ = &scene;
    textureBinding_.initialize(engine, std::move(resourceRoot));
    materialBinding_.initialize(shadedMeshMaterial, renderedMeshMaterial, textureBinding_);
    meshFactory_.initialize(engine, scene, materialBinding_);
    meshCache_.initialize(meshFactory_);
    toolPreviewCache_.initialize(meshFactory_, materialBinding_);
    overlayProxyFactory_.initialize(engine, scene, overlayMaterial, overlayLineMaterial, overlayPointMaterial);
    overlayCache_.initialize(overlayProxyFactory_);
    sceneWireframeOverlayCache_.initialize(engine, scene, selectedObjectLineMaterial);
    selectedObjectOverlayCache_.initialize(engine, scene, overlayMaterial, selectedObjectLineMaterial);
    syncService_.initialize(engine, scene, meshCache_, materialBinding_);
}

void SceneRenderBridge::shutdown() {
    if (!engine_) {
        return;
    }

    toolPreviewCache_.shutdown();
    syncService_.shutdown();
    selectedObjectOverlayCache_.shutdown();
    sceneWireframeOverlayCache_.shutdown();
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
    QDR_PROFILE_FUNCTION();
    if (!engine_ || !scene_ || !syncService_.initialized()) {
        return;
    }
    (void)syncService_.sync(document, assets);
}

void SceneRenderBridge::syncSelectedObjectOverlay(
        const SceneDocument& document,
        const AssetRegistry& assets,
        bool visible) {
    QDR_PROFILE_FUNCTION();
    if (!engine_ || !scene_ || !selectedObjectOverlayCache_.initialized()) {
        return;
    }
    selectedObjectOverlayCache_.sync(document, assets, visible);
}

void SceneRenderBridge::syncSceneWireframeOverlay(
        const SceneDocument& document,
        const AssetRegistry& assets,
        std::uint8_t layerMask) {
    QDR_PROFILE_FUNCTION();
    if (!engine_ || !scene_ || !sceneWireframeOverlayCache_.initialized()) {
        return;
    }
    sceneWireframeOverlayCache_.sync(document, assets, layerMask);
}

void SceneRenderBridge::setToolPreviewMeshes(std::vector<PreviewMesh> meshes, const AssetRegistry& assets) {
    QDR_PROFILE_FUNCTION();
    if (!engine_ || !scene_ || !toolPreviewCache_.initialized()) {
        return;
    }
    (void)toolPreviewCache_.setMeshes(meshes, assets);
}

void SceneRenderBridge::setOverlayFrame(ViewportOverlayFrame frame, ViewportOverlayCamera camera) {
    QDR_PROFILE_FUNCTION();
    (void)overlayCache_.setOverlayFrame(std::move(frame), camera);
}

void SceneRenderBridge::setOverlayFrame(
        std::size_t viewportIndex,
        std::uint8_t layerMask,
        ViewportOverlayFrame frame,
        ViewportOverlayCamera camera) {
    QDR_PROFILE_FUNCTION();
    (void)overlayCache_.setOverlayFrame(viewportIndex, layerMask, std::move(frame), camera);
}

} // namespace quader
