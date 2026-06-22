////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/ViewportRenderer.h"

#include "core/Logger.h"
#include "render/RenderLayers.h"

#include <utils/EntityManager.h>

#include <cstdint>

namespace quader {

ViewportRenderer::~ViewportRenderer() {
    shutdown();
}

void ViewportRenderer::initialize(filament::Engine& engine,
        filament::Scene& scene,
        TextureRegistry& textureRegistry,
        filament::Material& groundGridMaterial,
        std::uint8_t gridLayerMask) {
    engine_ = &engine;
    scene_ = &scene;
    textureRegistry_ = &textureRegistry;
    const std::uint8_t resolvedGridLayerMask = gridLayerMask == 0u ? viewportGridLayerMask(0) : gridLayerMask;
    renderPassCoordinator_.initialize(resolvedGridLayerMask);
    renderTarget_.initialize(engine, textureRegistry);
    LOG_INFO("Creating viewport Filament view");
    view_ = engine_->createView();
    view_->setName("Main Viewport");
    view_->setScene(scene_);
    view_->setPostProcessingEnabled(false);
    updateVisibleLayers();

    LOG_INFO("Creating viewport camera");
    cameraEntity_ = utils::EntityManager::get().create();
    camera_ = engine_->createCamera(cameraEntity_);
    view_->setCamera(camera_);
    gridRenderer_.initialize(engine, scene, groundGridMaterial, resolvedGridLayerMask);
    LOG_INFO("Creating initial viewport render target");
    resize(640, 480);
}

void ViewportRenderer::shutdown() {
    if (!engine_) {
        return;
    }

    renderTarget_.shutdown();
    gridRenderer_.shutdown();
    if (view_) {
        view_->setCamera(nullptr);
        view_->setScene(nullptr);
        engine_->destroy(view_);
        view_ = nullptr;
    }
    if (camera_) {
        engine_->destroyCameraComponent(cameraEntity_);
        camera_ = nullptr;
    }
    if (cameraEntity_) {
        utils::EntityManager::get().destroy(cameraEntity_);
        cameraEntity_.clear();
    }

    engine_ = nullptr;
    scene_ = nullptr;
    textureRegistry_ = nullptr;
}

void ViewportRenderer::resize(std::uint32_t width, std::uint32_t height) {
    renderTarget_.resize(width, height, view_);
}

void ViewportRenderer::updateCamera(const ViewportCameraController& controller) {
    const auto pose = controller.pose();
    camera_->lookAt(
            {pose.eye.x, pose.eye.y, pose.eye.z},
            {pose.target.x, pose.target.y, pose.target.z},
            {pose.up.x, pose.up.y, pose.up.z});
    const double aspect = static_cast<double>(width()) / static_cast<double>(height());
    if (controller.state().projection == ViewportProjection::Orthographic) {
        const double halfHeight = std::max(0.0001f, controller.state().orthographicSize * 0.5f);
        const double halfWidth = halfHeight * aspect;
        camera_->setProjection(filament::Camera::Projection::ORTHO,
                -halfWidth, halfWidth,
                -halfHeight, halfHeight,
                controller.state().nearPlane, controller.state().farPlane);
    } else {
        camera_->setProjection(controller.state().fovDegrees, aspect,
                controller.state().nearPlane, controller.state().farPlane);
    }
}

void ViewportRenderer::updateGroundGrid(
        const ViewportCameraController& controller,
        float gridWorldSpacing,
        const ViewportSettings& settings,
        bool visible) {
    if (!engine_ || !scene_) {
        return;
    }

    renderPassCoordinator_.setGridVisible(visible);
    renderPassCoordinator_.setBackgroundColor(settings.color(ViewportColorSettingId::Background));
    updateVisibleLayers();
    gridRenderer_.update(controller, width(), height(), gridWorldSpacing, settings, visible);
}

void ViewportRenderer::setOverlayVisible(bool visible) {
    if (!renderPassCoordinator_.setOverlayVisible(visible)) {
        return;
    }
    updateVisibleLayers();
}

void ViewportRenderer::setSceneVisible(bool visible) {
    if (!renderPassCoordinator_.setSceneVisible(visible)) {
        return;
    }
    updateVisibleLayers();
}

void ViewportRenderer::render(filament::Renderer& renderer) {
    renderPassCoordinator_.render(renderer, view_, renderTarget_.renderTarget());
}

void ViewportRenderer::updateVisibleLayers() {
    if (!view_) {
        return;
    }
    renderPassCoordinator_.applyVisibleLayers(view_);
}

} // namespace quader
