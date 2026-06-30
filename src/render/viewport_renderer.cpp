////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/viewport_renderer.hpp"

#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "render/render_layers.hpp"

#include <filament/ColorGrading.h>
#include <filament/Options.h>
#include <filament/ToneMapper.h>
#include <filament/Viewport.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <cstdint>

namespace quader {
namespace {

constexpr float kViewportOverlayCameraAperture = 16.0f;
constexpr float kViewportOverlayCameraShutterSpeed = 1.0f / 125.0f;
constexpr float kViewportOverlayCameraSensitivity = 100.0f;

void applyNeutralOverlayExposure(filament::Camera* camera) {
    if (!camera) {
        return;
    }
    camera->setExposure(
            kViewportOverlayCameraAperture,
            kViewportOverlayCameraShutterSpeed,
            kViewportOverlayCameraSensitivity);
}

ViewportSettingColor viewportClearColor(const SceneEnvironmentSettings& environment, bool renderedMode) {
    if (renderedMode) {
        return {};
    }
    return {
            environment.clearColor.x,
            environment.clearColor.y,
            environment.clearColor.z,
            1.0f,
    };
}

filament::ColorGrading* buildColorGrading(
        filament::Engine& engine,
        const filament::ToneMapper& toneMapper) {
    return filament::ColorGrading::Builder()
            .quality(filament::ColorGrading::QualityLevel::MEDIUM)
            .toneMapper(&toneMapper)
            .build(engine);
}

filament::ColorGrading* buildColorGrading(
        filament::Engine& engine,
        SceneToneMapping toneMapping) {
    switch (toneMapping) {
    case SceneToneMapping::Linear: {
        const filament::LinearToneMapper toneMapper;
        return buildColorGrading(engine, toneMapper);
    }
    case SceneToneMapping::AcesLegacy: {
        const filament::ACESLegacyToneMapper toneMapper;
        return buildColorGrading(engine, toneMapper);
    }
    case SceneToneMapping::Aces: {
        const filament::ACESToneMapper toneMapper;
        return buildColorGrading(engine, toneMapper);
    }
    case SceneToneMapping::Filmic: {
        const filament::FilmicToneMapper toneMapper;
        return buildColorGrading(engine, toneMapper);
    }
    case SceneToneMapping::DisplayRange: {
        const filament::DisplayRangeToneMapper toneMapper;
        return buildColorGrading(engine, toneMapper);
    }
    }
    const filament::ACESToneMapper toneMapper;
    return buildColorGrading(engine, toneMapper);
}

} // namespace

ViewportRenderer::~ViewportRenderer() {
    shutdown();
}

void ViewportRenderer::initialize(filament::Engine& engine,
        filament::Scene& scene,
        TextureRegistry& textureRegistry,
        filament::Material& groundGridMaterial,
        std::uint8_t gridLayerMask,
        ViewportRenderSurface surface) {
    engine_ = &engine;
    scene_ = &scene;
    textureRegistry_ = &textureRegistry;
    surface_ = surface;
    const std::uint8_t resolvedGridLayerMask = gridLayerMask == 0u ? viewportGridLayerMask(0) : gridLayerMask;
    renderPassCoordinator_.initialize(resolvedGridLayerMask);
    if (surface_ == ViewportRenderSurface::Texture) {
        renderTarget_.initialize(engine, textureRegistry);
        overlayRenderTarget_.initialize(engine, textureRegistry);
    }
    LOG_INFO("Creating viewport Filament view");
    view_ = engine_->createView();
    view_->setName("Main Viewport");
    view_->setScene(scene_);
    view_->setPostProcessingEnabled(false);
    view_->setShadowingEnabled(true);
    view_->setShadowType(filament::ShadowType::PCF);
    overlayView_ = engine_->createView();
    overlayView_->setName("Main Viewport Overlays");
    overlayView_->setScene(scene_);
    overlayView_->setPostProcessingEnabled(false);
    overlayView_->setAntiAliasing(filament::AntiAliasing::NONE);
    overlayView_->setDithering(filament::Dithering::NONE);
    overlayView_->setShadowingEnabled(false);
    overlayView_->setBlendMode(filament::View::BlendMode::TRANSLUCENT);
    updateVisibleLayers();

    LOG_INFO("Creating viewport camera");
    cameraEntity_ = utils::EntityManager::get().create();
    camera_ = engine_->createCamera(cameraEntity_);
    overlayCameraEntity_ = utils::EntityManager::get().create();
    overlayCamera_ = engine_->createCamera(overlayCameraEntity_);
    applyNeutralOverlayExposure(overlayCamera_);
    view_->setCamera(camera_);
    overlayView_->setCamera(overlayCamera_);
    gridRenderer_.initialize(engine, scene, groundGridMaterial, resolvedGridLayerMask);
    LOG_INFO("Creating initial viewport render target");
    resize(640, 480);
}

void ViewportRenderer::shutdown() {
    if (!engine_) {
        return;
    }

    renderTarget_.shutdown();
    overlayRenderTarget_.shutdown();
    gridRenderer_.shutdown();
    if (view_) {
        view_->setColorGrading(nullptr);
    }
    destroyColorGrading();
    if (overlayView_) {
        overlayView_->setCamera(nullptr);
        overlayView_->setScene(nullptr);
        engine_->destroy(overlayView_);
        overlayView_ = nullptr;
    }
    if (view_) {
        view_->setCamera(nullptr);
        view_->setScene(nullptr);
        engine_->destroy(view_);
        view_ = nullptr;
    }
    if (overlayCamera_) {
        engine_->destroyCameraComponent(overlayCameraEntity_);
        overlayCamera_ = nullptr;
    }
    if (camera_) {
        engine_->destroyCameraComponent(cameraEntity_);
        camera_ = nullptr;
    }
    if (overlayCameraEntity_) {
        utils::EntityManager::get().destroy(overlayCameraEntity_);
        overlayCameraEntity_.clear();
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
    QDR_PROFILE_FUNCTION();
    width_ = std::max(1u, width);
    height_ = std::max(1u, height);
    if (surface_ == ViewportRenderSurface::Texture) {
        renderTarget_.resize(width_, height_, view_, TextureSampleColorSpace::LinearSrgb);
        overlayRenderTarget_.resize(
                width_,
                height_,
                overlayView_,
                TextureSampleColorSpace::DisplaySrgbPremultipliedAlpha);
        return;
    }

    if (view_) {
        view_->setRenderTarget(nullptr);
        view_->setViewport({0, 0, width_, height_});
    }
    if (overlayView_) {
        overlayView_->setRenderTarget(nullptr);
        overlayView_->setViewport({0, 0, width_, height_});
    }
}

void ViewportRenderer::updateCamera(const ViewportCameraController& controller) {
    QDR_PROFILE_FUNCTION();
    const auto pose = controller.pose();
    const auto applyLookAt = [&](filament::Camera* camera) {
        if (camera) {
            camera->lookAt(
                    {pose.eye.x, pose.eye.y, pose.eye.z},
                    {pose.target.x, pose.target.y, pose.target.z},
                    {pose.up.x, pose.up.y, pose.up.z});
        }
    };
    applyLookAt(camera_);
    applyLookAt(overlayCamera_);
    const double aspect = static_cast<double>(width()) / static_cast<double>(height());
    const auto applyProjection = [&](filament::Camera* camera) {
        if (!camera) {
            return;
        }
        if (controller.state().projection == ViewportProjection::Orthographic) {
            const double halfHeight = std::max(0.0001f, controller.state().orthographicSize * 0.5f);
            const double halfWidth = halfHeight * aspect;
            camera->setProjection(filament::Camera::Projection::ORTHO,
                    -halfWidth, halfWidth,
                    -halfHeight, halfHeight,
                    cameraNearPlane_, cameraFarPlane_);
        } else {
            camera->setProjection(controller.state().fovDegrees, aspect,
                    cameraNearPlane_, cameraFarPlane_);
        }
    };
    applyProjection(camera_);
    applyProjection(overlayCamera_);
    applyNeutralOverlayExposure(overlayCamera_);
}

void ViewportRenderer::applySceneSettings(const SceneSettingsComponent& settings, bool renderedMode) {
    QDR_PROFILE_FUNCTION();
    if (!view_ || !camera_) {
        return;
    }

    const bool postProcessingEnabled = renderedMode && settings.postProcess.enabled;
    cameraNearPlane_ = std::max(settings.camera.nearPlane, 0.001f);
    cameraFarPlane_ = std::max(settings.camera.farPlane, cameraNearPlane_ + 0.001f);
    view_->setPostProcessingEnabled(postProcessingEnabled);
    view_->setAntiAliasing(
            postProcessingEnabled && settings.postProcess.antiAliasing
                    ? filament::AntiAliasing::FXAA
                    : filament::AntiAliasing::NONE);
    view_->setDithering(
            postProcessingEnabled && settings.postProcess.dithering
                    ? filament::Dithering::TEMPORAL
                    : filament::Dithering::NONE);
    if (overlayView_) {
        overlayView_->setPostProcessingEnabled(false);
        overlayView_->setAntiAliasing(filament::AntiAliasing::NONE);
        overlayView_->setDithering(filament::Dithering::NONE);
        overlayView_->setShadowingEnabled(false);
        overlayView_->setColorGrading(nullptr);
    }
    applyToneMapping(settings.postProcess.toneMapping, postProcessingEnabled);
    renderPassCoordinator_.setBackgroundColor(viewportClearColor(settings.environment, renderedMode));

    filament::View::AmbientOcclusionOptions ao;
    ao.enabled = postProcessingEnabled && settings.postProcess.ambientOcclusion;
    ao.intensity = std::max(settings.postProcess.ambientOcclusionIntensity, 0.0f);
    ao.radius = std::max(settings.postProcess.ambientOcclusionRadius, 0.0f);
    ao.quality = filament::QualityLevel::MEDIUM;
    ao.upsampling = filament::QualityLevel::MEDIUM;
    view_->setAmbientOcclusionOptions(ao);

    filament::View::BloomOptions bloom;
    bloom.enabled = postProcessingEnabled && settings.postProcess.bloom;
    bloom.strength = std::clamp(settings.postProcess.bloomStrength, 0.0f, 1.0f);
    view_->setBloomOptions(bloom);

    camera_->setExposure(
            std::max(settings.camera.aperture, 0.01f),
            std::max(settings.camera.shutterSpeed, 0.000001f),
            std::max(settings.camera.sensitivity, 1.0f));
    applyNeutralOverlayExposure(overlayCamera_);
}

void ViewportRenderer::applyToneMapping(SceneToneMapping toneMapping, bool enabled) {
    if (!engine_ || !view_) {
        return;
    }
    if (!enabled) {
        view_->setColorGrading(nullptr);
        return;
    }
    if (colorGrading_ != nullptr && hasColorGradingToneMapping_ && colorGradingToneMapping_ == toneMapping) {
        view_->setColorGrading(colorGrading_);
        return;
    }

    view_->setColorGrading(nullptr);
    destroyColorGrading();
    colorGrading_ = buildColorGrading(*engine_, toneMapping);
    if (colorGrading_ == nullptr) {
        hasColorGradingToneMapping_ = false;
        return;
    }
    colorGradingToneMapping_ = toneMapping;
    hasColorGradingToneMapping_ = true;
    view_->setColorGrading(colorGrading_);
}

void ViewportRenderer::destroyColorGrading() {
    if (!engine_ || colorGrading_ == nullptr) {
        colorGrading_ = nullptr;
        hasColorGradingToneMapping_ = false;
        return;
    }
    engine_->destroy(colorGrading_);
    colorGrading_ = nullptr;
    hasColorGradingToneMapping_ = false;
}

void ViewportRenderer::updateGroundGrid(
        const ViewportCameraController& controller,
        float gridWorldSpacing,
        const ViewportSettings& settings,
        bool visible) {
    QDR_PROFILE_FUNCTION();
    if (!engine_ || !scene_) {
        return;
    }

    renderPassCoordinator_.setGridVisible(visible);
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

void ViewportRenderer::setSceneLayerMask(std::uint8_t layerMask) {
    if (!renderPassCoordinator_.setSceneLayerMask(layerMask)) {
        return;
    }
    updateVisibleLayers();
}

void ViewportRenderer::setShadowingEnabled(bool enabled) {
    if (view_) {
        view_->setShadowingEnabled(enabled);
    }
    if (overlayView_) {
        overlayView_->setShadowingEnabled(false);
    }
}

void ViewportRenderer::render(filament::Renderer& renderer) {
    QDR_PROFILE_FUNCTION();
    renderPassCoordinator_.render(
            renderer,
            view_,
            overlayView_,
            surface_ == ViewportRenderSurface::Texture);
}

void ViewportRenderer::updateVisibleLayers() {
    if (!view_ && !overlayView_) {
        return;
    }
    renderPassCoordinator_.applyVisibleLayers(view_, overlayView_);
}

} // namespace quader
