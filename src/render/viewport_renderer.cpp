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

void apply_neutral_overlay_exposure(filament::Camera *camera) {
  if (!camera) {
    return;
  }
  camera->setExposure(kViewportOverlayCameraAperture,
                      kViewportOverlayCameraShutterSpeed,
                      kViewportOverlayCameraSensitivity);
}

ViewportSettingColor
viewport_clear_color(const SceneEnvironmentSettings &environment,
                     bool rendered_mode) {
  if (rendered_mode) {
    return {};
  }
  return {
      environment.clear_color.x,
      environment.clear_color.y,
      environment.clear_color.z,
      1.0f,
  };
}

filament::ColorGrading *
build_color_grading(filament::Engine &engine,
                    const filament::ToneMapper &tone_mapper) {
  return filament::ColorGrading::Builder()
      .quality(filament::ColorGrading::QualityLevel::MEDIUM)
      .toneMapper(&tone_mapper)
      .build(engine);
}

filament::ColorGrading *build_color_grading(filament::Engine &engine,
                                            SceneToneMapping tone_mapping) {
  switch (tone_mapping) {
  case SceneToneMapping::Linear: {
    const filament::LinearToneMapper tone_mapper;
    return build_color_grading(engine, tone_mapper);
  }
  case SceneToneMapping::AcesLegacy: {
    const filament::ACESLegacyToneMapper tone_mapper;
    return build_color_grading(engine, tone_mapper);
  }
  case SceneToneMapping::Aces: {
    const filament::ACESToneMapper tone_mapper;
    return build_color_grading(engine, tone_mapper);
  }
  case SceneToneMapping::Filmic: {
    const filament::FilmicToneMapper tone_mapper;
    return build_color_grading(engine, tone_mapper);
  }
  case SceneToneMapping::DisplayRange: {
    const filament::DisplayRangeToneMapper tone_mapper;
    return build_color_grading(engine, tone_mapper);
  }
  }
  const filament::ACESToneMapper tone_mapper;
  return build_color_grading(engine, tone_mapper);
}

TextureSampleColorSpace
scene_texture_sample_color_space(bool post_processing_enabled) {
  return post_processing_enabled ? TextureSampleColorSpace::DisplaySrgb
                                 : TextureSampleColorSpace::LinearSrgb;
}

} // namespace

ViewportRenderer::~ViewportRenderer() {
    shutdown();
}

void ViewportRenderer::initialize(filament::Engine &engine,
                                  filament::Scene &scene,
                                  TextureRegistry &texture_registry,
                                  filament::Material &ground_grid_material,
                                  std::uint8_t grid_layer_mask,
                                  ViewportRenderSurface surface) {
  engine_ = &engine;
  scene_ = &scene;
  texture_registry_ = &texture_registry;
  surface_ = surface;
  const std::uint8_t resolved_grid_layer_mask =
      grid_layer_mask == 0u ? viewport_grid_layer_mask(0) : grid_layer_mask;
  render_pass_coordinator_.initialize(resolved_grid_layer_mask);
  if (surface_ == ViewportRenderSurface::Texture) {
    render_target_.initialize(engine, texture_registry);
    overlay_render_target_.initialize(engine, texture_registry);
  }
  LOG_INFO("Creating viewport Filament view");
  view_ = engine_->createView();
  view_->setName("Main Viewport");
  view_->setScene(scene_);
  view_->setPostProcessingEnabled(false);
  view_->setShadowingEnabled(true);
  view_->setShadowType(filament::ShadowType::PCF);
  overlay_view_ = engine_->createView();
  overlay_view_->setName("Main Viewport Overlays");
  overlay_view_->setScene(scene_);
  overlay_view_->setPostProcessingEnabled(false);
  overlay_view_->setAntiAliasing(filament::AntiAliasing::NONE);
  overlay_view_->setDithering(filament::Dithering::NONE);
  overlay_view_->setShadowingEnabled(false);
  overlay_view_->setBlendMode(filament::View::BlendMode::TRANSLUCENT);
  update_visible_layers();

  LOG_INFO("Creating viewport camera");
  camera_entity_ = utils::EntityManager::get().create();
  camera_ = engine_->createCamera(camera_entity_);
  overlay_camera_entity_ = utils::EntityManager::get().create();
  overlay_camera_ = engine_->createCamera(overlay_camera_entity_);
  apply_neutral_overlay_exposure(overlay_camera_);
  view_->setCamera(camera_);
  overlay_view_->setCamera(overlay_camera_);
  grid_renderer_.initialize(engine, scene, ground_grid_material,
                            resolved_grid_layer_mask);
  LOG_INFO("Creating initial viewport render target");
  resize(640, 480);
}

void ViewportRenderer::shutdown() {
    if (!engine_) {
        return;
    }

    render_target_.shutdown();
    overlay_render_target_.shutdown();
    grid_renderer_.shutdown();
    if (view_) {
        view_->setColorGrading(nullptr);
    }
    destroy_color_grading();
    if (overlay_view_) {
      overlay_view_->setCamera(nullptr);
      overlay_view_->setScene(nullptr);
      engine_->destroy(overlay_view_);
      overlay_view_ = nullptr;
    }
    if (view_) {
        view_->setCamera(nullptr);
        view_->setScene(nullptr);
        engine_->destroy(view_);
        view_ = nullptr;
    }
    if (overlay_camera_) {
      engine_->destroyCameraComponent(overlay_camera_entity_);
      overlay_camera_ = nullptr;
    }
    if (camera_) {
      engine_->destroyCameraComponent(camera_entity_);
      camera_ = nullptr;
    }
    if (overlay_camera_entity_) {
      utils::EntityManager::get().destroy(overlay_camera_entity_);
      overlay_camera_entity_.clear();
    }
    if (camera_entity_) {
      utils::EntityManager::get().destroy(camera_entity_);
      camera_entity_.clear();
    }

    engine_ = nullptr;
    scene_ = nullptr;
    texture_registry_ = nullptr;
}

void ViewportRenderer::resize(std::uint32_t width, std::uint32_t height) {
    QDR_PROFILE_FUNCTION();
    width_ = std::max(1u, width);
    height_ = std::max(1u, height);
    if (surface_ == ViewportRenderSurface::Texture) {
      render_target_.resize(width_, height_, view_,
                            scene_texture_sample_color_space_);
      overlay_render_target_.resize(
          width_, height_, overlay_view_,
          TextureSampleColorSpace::DisplaySrgbPremultipliedAlpha);
      return;
    }

    if (view_) {
        view_->setRenderTarget(nullptr);
        view_->setViewport({0, 0, width_, height_});
    }
    if (overlay_view_) {
      overlay_view_->setRenderTarget(nullptr);
      overlay_view_->setViewport({0, 0, width_, height_});
    }
}

float ViewportRenderer::texture_max_u() const {
  if (surface_ != ViewportRenderSurface::Texture) {
    return 1.0f;
  }
  return std::clamp(
      static_cast<float>(width_) /
          static_cast<float>(std::max(1u, render_target_.width())),
      0.0f, 1.0f);
}

float ViewportRenderer::texture_max_v() const {
  if (surface_ != ViewportRenderSurface::Texture) {
    return 1.0f;
  }
  return std::clamp(
      static_cast<float>(height_) /
          static_cast<float>(std::max(1u, render_target_.height())),
      0.0f, 1.0f);
}

void ViewportRenderer::update_camera(
    const ViewportCameraController &controller) {
  QDR_PROFILE_FUNCTION();
  const auto pose = controller.pose();
  const auto apply_look_at = [&](filament::Camera *camera) {
    if (camera) {
      camera->lookAt({pose.eye.x, pose.eye.y, pose.eye.z},
                     {pose.target.x, pose.target.y, pose.target.z},
                     {pose.up.x, pose.up.y, pose.up.z});
    }
  };
  apply_look_at(camera_);
  apply_look_at(overlay_camera_);
  const double aspect =
      static_cast<double>(width()) / static_cast<double>(height());
  const auto apply_projection = [&](filament::Camera *camera) {
    if (!camera) {
      return;
    }
    if (controller.state().projection == ViewportProjection::Orthographic) {
      const double half_height =
          std::max(0.0001f, controller.state().orthographic_size * 0.5f);
      const double half_width = half_height * aspect;
      camera->setProjection(filament::Camera::Projection::ORTHO, -half_width,
                            half_width, -half_height, half_height,
                            controller.state().near_plane,
                            controller.state().far_plane);
    } else {
      camera->setProjection(controller.state().fov_degrees, aspect,
                            controller.state().near_plane,
                            controller.state().far_plane);
    }
  };
  apply_projection(camera_);
  apply_projection(overlay_camera_);
  apply_neutral_overlay_exposure(overlay_camera_);
}

void ViewportRenderer::apply_scene_settings(
    const SceneSettingsComponent &settings, bool rendered_mode) {
  QDR_PROFILE_FUNCTION();
  if (!view_ || !camera_) {
    return;
  }

  const bool post_processing_enabled =
      rendered_mode && settings.post_process.enabled;
  scene_texture_sample_color_space_ =
      scene_texture_sample_color_space(post_processing_enabled);
  if (surface_ == ViewportRenderSurface::Texture) {
    render_target_.set_sample_color_space(scene_texture_sample_color_space_);
  }
  view_->setPostProcessingEnabled(post_processing_enabled);
  view_->setAntiAliasing(post_processing_enabled &&
                                 settings.post_process.anti_aliasing
                             ? filament::AntiAliasing::FXAA
                             : filament::AntiAliasing::NONE);
  view_->setDithering(post_processing_enabled && settings.post_process.dithering
                          ? filament::Dithering::TEMPORAL
                          : filament::Dithering::NONE);
  if (overlay_view_) {
    overlay_view_->setPostProcessingEnabled(false);
    overlay_view_->setAntiAliasing(filament::AntiAliasing::NONE);
    overlay_view_->setDithering(filament::Dithering::NONE);
    overlay_view_->setShadowingEnabled(false);
    overlay_view_->setColorGrading(nullptr);
  }
  apply_tone_mapping(settings.post_process.tone_mapping,
                     post_processing_enabled);
  render_pass_coordinator_.set_background_color(
      viewport_clear_color(settings.environment, rendered_mode));

  filament::View::AmbientOcclusionOptions ao;
  ao.enabled =
      post_processing_enabled && settings.post_process.ambient_occlusion;
  ao.intensity =
      std::max(settings.post_process.ambient_occlusion_intensity, 0.0f);
  ao.radius = std::max(settings.post_process.ambient_occlusion_radius, 0.0f);
  ao.quality = filament::QualityLevel::MEDIUM;
  ao.upsampling = filament::QualityLevel::MEDIUM;
  view_->setAmbientOcclusionOptions(ao);

  filament::View::BloomOptions bloom;
  bloom.enabled = post_processing_enabled && settings.post_process.bloom;
  bloom.strength = std::clamp(settings.post_process.bloom_strength, 0.0f, 1.0f);
  view_->setBloomOptions(bloom);

  camera_->setExposure(std::max(settings.camera.aperture, 0.01f),
                       std::max(settings.camera.shutter_speed, 0.000001f),
                       std::max(settings.camera.sensitivity, 1.0f));
  apply_neutral_overlay_exposure(overlay_camera_);
}

void ViewportRenderer::apply_tone_mapping(SceneToneMapping tone_mapping,
                                          bool enabled) {
  if (!engine_ || !view_) {
    return;
  }
  if (!enabled) {
    view_->setColorGrading(nullptr);
    return;
  }
  if (color_grading_ != nullptr && has_color_grading_tone_mapping_ &&
      color_grading_tone_mapping_ == tone_mapping) {
    view_->setColorGrading(color_grading_);
    return;
  }

  view_->setColorGrading(nullptr);
  destroy_color_grading();
  color_grading_ = build_color_grading(*engine_, tone_mapping);
  if (color_grading_ == nullptr) {
    has_color_grading_tone_mapping_ = false;
    return;
  }
  color_grading_tone_mapping_ = tone_mapping;
  has_color_grading_tone_mapping_ = true;
  view_->setColorGrading(color_grading_);
}

void ViewportRenderer::destroy_color_grading() {
  if (!engine_ || color_grading_ == nullptr) {
    color_grading_ = nullptr;
    has_color_grading_tone_mapping_ = false;
    return;
  }
  engine_->destroy(color_grading_);
  color_grading_ = nullptr;
  has_color_grading_tone_mapping_ = false;
}

void ViewportRenderer::update_ground_grid(
    const ViewportCameraController &controller, float grid_world_spacing,
    const ViewportSettings &settings, bool visible) {
  QDR_PROFILE_FUNCTION();
  if (!engine_ || !scene_) {
    return;
  }

  render_pass_coordinator_.set_grid_visible(visible);
  update_visible_layers();
  grid_renderer_.update(controller, width(), height(), grid_world_spacing,
                        settings, visible);
}

void ViewportRenderer::set_overlay_visible(bool visible) {
  if (!render_pass_coordinator_.set_overlay_visible(visible)) {
    return;
  }
  update_visible_layers();
}

void ViewportRenderer::set_scene_visible(bool visible) {
  if (!render_pass_coordinator_.set_scene_visible(visible)) {
    return;
  }
  update_visible_layers();
}

void ViewportRenderer::set_scene_layer_mask(std::uint8_t layer_mask) {
  if (!render_pass_coordinator_.set_scene_layer_mask(layer_mask)) {
    return;
  }
  update_visible_layers();
}

void ViewportRenderer::set_shadowing_enabled(bool enabled) {
  if (view_) {
    view_->setShadowingEnabled(enabled);
  }
  if (overlay_view_) {
    overlay_view_->setShadowingEnabled(false);
  }
}

void ViewportRenderer::render(filament::Renderer& renderer) {
    QDR_PROFILE_FUNCTION();
    render_pass_coordinator_.render(renderer, view_, overlay_view_,
                                    surface_ == ViewportRenderSurface::Texture);
}

void ViewportRenderer::update_visible_layers() {
  if (!view_ && !overlay_view_) {
    return;
  }
  render_pass_coordinator_.apply_visible_layers(view_, overlay_view_);
}

} // namespace quader
