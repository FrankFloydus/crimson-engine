////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/selected_mesh_outline_renderer.hpp"

#include "core/color_space.hpp"
#include "diagnostics/profile.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/overlay/viewport_persistent_overlay_style.hpp"
#include "render/render_layers.hpp"
#include "render/render_signature_builder.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <backend/DriverEnums.h>
#include <filament/Box.h>
#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/Options.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/RenderTarget.h>
#include <filament/Scene.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace quader {
namespace {

filament::math::float4 outline_color(std::uint32_t packed) {
  return {srgb_to_linear(static_cast<float>(packed & 0xffu) / 255.0f),
          srgb_to_linear(static_cast<float>((packed >> 8u) & 0xffu) / 255.0f),
          srgb_to_linear(static_cast<float>((packed >> 16u) & 0xffu) / 255.0f),
          static_cast<float>((packed >> 24u) & 0xffu) / 255.0f};
}

} // namespace

/** Mask scene geometry is external; this owner retains only per-pane GPU state. */
struct SelectedMeshOutlineRenderer::Impl final {
  struct MaskTarget final {
    filament::View *view = nullptr;
    filament::Texture *color = nullptr;
    filament::Texture *depth = nullptr;
    filament::RenderTarget *target = nullptr;
  };

  filament::Engine *engine = nullptr;
  filament::Scene *main_scene = nullptr;
  filament::Camera *camera = nullptr;
  utils::Entity camera_entity;
  std::array<MaskTarget, 2u> masks;
  filament::VertexBuffer *vertices = nullptr;
  filament::IndexBuffer *indices = nullptr;
  std::array<filament::MaterialInstance *, 2u> materials{};
  std::array<utils::Entity, 2u> entities;
  std::uint8_t pane_layer_mask = 0u;
  std::uint8_t applied_layer_mask = 0u;
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;
  std::uint32_t target_width = 0u;
  std::uint32_t target_height = 0u;
  std::uint32_t guard_pixels = 4u;
  bool visible = true;
  bool ordinary_geometry = false;
  bool active_geometry = false;
  bool has_camera = false;
  bool has_style = false;
  ViewportCameraState camera_state;
  ViewportCameraPose camera_pose;
  std::uint64_t camera_signature = 0u;
  ViewportPersistentOverlayStyleSnapshot style;

  void update_visibility() {
    if (!engine) return;
    const std::uint8_t requested = visible && (ordinary_geometry || active_geometry) &&
            masks[0].target && masks[1].target && has_camera
        ? pane_layer_mask : 0u;
    if (requested == applied_layer_mask) return;
    auto &renderables = engine->getRenderableManager();
    for (const auto entity : entities) {
      if (entity) renderables.setLayerMask(renderables.getInstance(entity),
                                            kViewportAllLayerMask, requested);
    }
    applied_layer_mask = requested;
  }

  void destroy_targets() {
    if (!engine) return;
    for (auto &mask : masks) {
      if (mask.view) mask.view->setRenderTarget(nullptr);
      if (mask.target) engine->destroy(mask.target);
      if (mask.color) engine->destroy(mask.color);
      if (mask.depth) engine->destroy(mask.depth);
      mask.target = nullptr;
      mask.color = nullptr;
      mask.depth = nullptr;
    }
    target_width = 0u;
    target_height = 0u;
    update_visibility();
  }

  void apply_camera() {
    if (!camera || !has_camera || !width || !height) return;
    camera->lookAt({camera_pose.eye.x, camera_pose.eye.y, camera_pose.eye.z},
                    {camera_pose.target.x, camera_pose.target.y, camera_pose.target.z},
                    {camera_pose.up.x, camera_pose.up.y, camera_pose.up.z});
    const double expanded_width = static_cast<double>(width) + 2.0 * guard_pixels;
    const double expanded_height = static_cast<double>(height) + 2.0 * guard_pixels;
    const bool orthographic = camera_state.projection == ViewportProjection::Orthographic;
    const double half_height = orthographic
        ? std::max(0.0001, static_cast<double>(camera_state.orthographic_size) * 0.5)
        : camera_state.near_plane * std::tan(camera_state.fov_degrees *
                                               std::numbers::pi / 360.0);
    const double horizontal_extent = half_height * expanded_width / height;
    const double vertical_extent = half_height * expanded_height / height;
    camera->setProjection(orthographic ? filament::Camera::Projection::ORTHO
                                     : filament::Camera::Projection::PERSPECTIVE,
                           -horizontal_extent, horizontal_extent,
                           -vertical_extent, vertical_extent,
                           camera_state.near_plane, camera_state.far_plane);
    camera->setExposure(16.0f, 1.0f / 125.0f, 100.0f);
  }

  bool ensure_targets() {
    if (!engine || !width || !height) return false;
    const auto requested_width = width + 2u * guard_pixels;
    const auto requested_height = height + 2u * guard_pixels;
    if (target_width == requested_width && target_height == requested_height &&
        masks[0].target && masks[1].target) return true;
    destroy_targets();
    using Texture = filament::Texture;
    for (auto &mask : masks) {
      mask.color = Texture::Builder().width(requested_width).height(requested_height)
          .levels(1u).format(Texture::InternalFormat::RGBA8)
          .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
          .build(*engine);
      mask.depth = Texture::Builder().width(requested_width).height(requested_height)
          .levels(1u).format(Texture::InternalFormat::DEPTH32F)
          .usage(Texture::Usage::DEPTH_ATTACHMENT | Texture::Usage::SAMPLEABLE)
          .build(*engine);
      if (!mask.color || !mask.depth) { destroy_targets(); return false; }
      mask.target = filament::RenderTarget::Builder()
          .texture(filament::RenderTarget::AttachmentPoint::COLOR, mask.color)
          .texture(filament::RenderTarget::AttachmentPoint::DEPTH, mask.depth)
          .build(*engine);
      if (!mask.target) { destroy_targets(); return false; }
      mask.view->setRenderTarget(mask.target);
      mask.view->setViewport({0, 0, requested_width, requested_height});
    }
    const filament::TextureSampler sampler(filament::TextureSampler::MinFilter::NEAREST,
        filament::TextureSampler::MagFilter::NEAREST,
        filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);
    for (auto *material : materials) {
      material->setParameter("ordinaryMask", masks[0].color, sampler);
      material->setParameter("activeMask", masks[1].color, sampler);
      material->setParameter("ordinaryDepth", masks[0].depth, sampler);
      material->setParameter("activeDepth", masks[1].depth, sampler);
      material->setParameter("guardPixels", static_cast<float>(guard_pixels));
    }
    target_width = requested_width;
    target_height = requested_height;
    apply_camera();
    update_visibility();
    return true;
  }

  void shutdown() {
    if (!engine) return;
    // Detach both passes before destroying any material referenced by them.
    for (auto entity : entities) {
      if (entity && main_scene) main_scene->remove(entity);
      if (entity) engine->getRenderableManager().destroy(entity);
    }
    for (auto &entity : entities) {
      if (entity) utils::EntityManager::get().destroy(entity);
      entity.clear();
    }
    applied_layer_mask = 0u;
    for (auto *&material : materials) {
      if (material) engine->destroy(material);
      material = nullptr;
    }
    destroy_targets();
    for (auto &mask : masks) {
      if (mask.view) engine->destroy(mask.view);
      mask.view = nullptr;
    }
    if (vertices) engine->destroy(vertices);
    if (indices) engine->destroy(indices);
    vertices = nullptr;
    indices = nullptr;
    if (camera_entity) {
      engine->destroyCameraComponent(camera_entity);
      utils::EntityManager::get().destroy(camera_entity);
      camera_entity.clear();
    }
    camera = nullptr;
    engine = nullptr;
    main_scene = nullptr;
    width = height = 0u;
    camera_signature = 0u;
    has_camera = has_style = false;
    ordinary_geometry = active_geometry = false;
  }
};

SelectedMeshOutlineRenderer::SelectedMeshOutlineRenderer()
    : impl_(std::make_unique<Impl>()) {}
SelectedMeshOutlineRenderer::~SelectedMeshOutlineRenderer() { shutdown(); }

bool SelectedMeshOutlineRenderer::initialize(filament::Engine &engine,
    filament::Scene &main_scene, filament::Scene &mask_scene,
    filament::Material &composite_material, std::uint8_t pane_layer_mask) {
  shutdown();
  auto &state = *impl_;
  // Viewport sizing can precede initialization. The next resize must create
  // targets even when it repeats those pre-initialization dimensions.
  state.width = state.height = 0u;
  state.engine = &engine;
  state.main_scene = &main_scene;
  state.pane_layer_mask = pane_layer_mask;
  state.camera_entity = utils::EntityManager::get().create();
  state.camera = engine.createCamera(state.camera_entity);
  if (!state.camera) { shutdown(); return false; }
  for (std::size_t index = 0u; index < state.masks.size(); ++index) {
    auto *view = engine.createView();
    if (!view) { shutdown(); return false; }
    state.masks[index].view = view;
    view->setName(index == 0u ? "Ordinary selected mesh occupancy" : "Active selected mesh occupancy");
    view->setScene(&mask_scene);
    view->setCamera(state.camera);
    view->setVisibleLayers(kViewportAllLayerMask, static_cast<std::uint8_t>(1u << index));
    view->setPostProcessingEnabled(false);
    view->setAntiAliasing(filament::AntiAliasing::NONE);
    view->setDithering(filament::Dithering::NONE);
    view->setMultiSampleAntiAliasingOptions({.enabled = false});
    view->setTemporalAntiAliasingOptions({.enabled = false});
    view->setDynamicResolutionOptions({.enabled = false});
    view->setShadowingEnabled(false);
    view->setFogOptions({.enabled = false});
    // Translucent views render through a color-only final blit, which would
    // leave the sampleable custom depth attachment without the mask geometry.
    view->setBlendMode(filament::View::BlendMode::OPAQUE);
  }
  const std::array<filament::math::float3, 3u> positions{{
      {-1.0f, -1.0f, 0.0f}, {3.0f, -1.0f, 0.0f}, {-1.0f, 3.0f, 0.0f}}};
  const std::array<std::uint16_t, 3u> triangles{{0u, 1u, 2u}};
  state.vertices = filament::VertexBuffer::Builder().vertexCount(3u).bufferCount(1u)
      .attribute(filament::VertexAttribute::POSITION, 0u,
                 filament::backend::ElementType::FLOAT3, 0u, sizeof(filament::math::float3))
      .build(engine);
  state.indices = filament::IndexBuffer::Builder().indexCount(3u)
      .bufferType(filament::IndexBuffer::IndexType::USHORT).build(engine);
  if (!state.vertices || !state.indices) { shutdown(); return false; }
  state.vertices->setBufferAt(engine, 0u,
      FilamentBufferDescriptorFactory::bytes(positions.data(), sizeof(positions)));
  state.indices->setBuffer(engine,
      FilamentBufferDescriptorFactory::bytes(triangles.data(), sizeof(triangles)));
  filament::Box bounds;
  bounds.set({-1.0f, -1.0f, -0.001f}, {3.0f, 3.0f, 0.001f});
  const auto order = static_cast<std::uint16_t>(
      viewport_overlay_draw_order(ViewportOverlayLayer::SelectedEdge) * 64u);
  for (std::size_t index = 0u; index < state.entities.size(); ++index) {
    auto *material = composite_material.createInstance();
    state.materials[index] = material;
    if (!material) { shutdown(); return false; }
    material->setDepthWrite(false);
    material->setDepthCulling(true);
    material->setDepthFunc(index == 0u ? filament::MaterialInstance::DepthFunc::GE
                                     : filament::MaterialInstance::DepthFunc::L);
    material->setCullingMode(filament::MaterialInstance::CullingMode::NONE);
    material->setTransparencyMode(filament::MaterialInstance::TransparencyMode::DEFAULT);
    material->setParameter("alphaScale", index == 0u ? 1.0f : kOverlayOccludedSilhouetteAlphaMultiplier);
    state.entities[index] = utils::EntityManager::get().create();
    const auto result = filament::RenderableManager::Builder(1u)
        .geometry(0u, filament::RenderableManager::PrimitiveType::TRIANGLES,
                   state.vertices, state.indices)
        .material(0u, material).boundingBox(bounds).culling(false)
        .castShadows(false).receiveShadows(false)
        .channel(kViewportDefaultRenderableChannel).priority(4u)
        .blendOrder(0u, static_cast<std::uint16_t>(order + index))
        .globalBlendOrderEnabled(0u, true).layerMask(kViewportAllLayerMask, 0u)
        .build(engine, state.entities[index]);
    if (result != filament::RenderableManager::Builder::Success) {
      shutdown(); return false;
    }
    main_scene.addEntity(state.entities[index]);
  }
  set_style(ViewportPersistentOverlayStyleSnapshot{});
  return true;
}

void SelectedMeshOutlineRenderer::shutdown() { impl_->shutdown(); }
bool SelectedMeshOutlineRenderer::initialized() const {
  return impl_->engine && impl_->materials[0] && impl_->materials[1];
}
void SelectedMeshOutlineRenderer::resize(std::uint32_t width, std::uint32_t height) {
  auto &state = *impl_;
  if (state.width == width && state.height == height) {
    state.ensure_targets();
    return;
  }
  state.width = width;
  state.height = height;
  state.camera_signature = 0u;
  if (width && height) state.ensure_targets();
  else state.destroy_targets();
}
void SelectedMeshOutlineRenderer::update_camera(const ViewportCameraController &controller) {
  auto &state = *impl_;
  const auto pose = controller.pose();
  const auto &camera = controller.state();
  const auto signature = RenderSignatureBuilder{}.add_vec3(pose.eye).add_vec3(pose.target)
      .add_vec3(pose.up).add_value(static_cast<std::uint32_t>(camera.projection))
      .add_float(camera.orthographic_size).add_float(camera.fov_degrees)
      .add_float(camera.near_plane).add_float(camera.far_plane)
      .add_value(state.width).add_value(state.height).add_value(state.guard_pixels).value();
  if (state.has_camera && state.camera_signature == signature) return;
  state.camera_signature = signature;
  state.camera_state = camera;
  state.camera_pose = pose;
  state.has_camera = true;
  state.apply_camera();
  state.update_visibility();
}
void SelectedMeshOutlineRenderer::set_style(const ViewportPersistentOverlayStyleSnapshot &style) {
  auto &state = *impl_;
  if (state.has_style && viewport_persistent_overlay_wire_styles_match(state.style, style)) return;
  state.style = style;
  state.has_style = true;
  const float width = std::max(1.0f, style.selection_edge_line_width_pixels);
  const float feather = kOverlayLineFeatherPixels * std::max(0.01f, style.raster_scale);
  const auto guard = static_cast<std::uint32_t>(std::ceil((width + feather) * 0.5f + 2.0f));
  for (auto *material : state.materials) {
    if (!material) continue;
    material->setParameter("ordinaryColor", outline_color(style.selection_wire_color));
    material->setParameter("activeColor", outline_color(style.active_selection_wire_color));
    material->setParameter("lineWidth", width);
    material->setParameter("featherPixels", feather);
  }
  if (guard != state.guard_pixels) {
    state.guard_pixels = guard;
    state.ensure_targets();
  }
}
void SelectedMeshOutlineRenderer::set_visible(bool visible) {
  impl_->visible = visible;
  impl_->update_visibility();
}
void SelectedMeshOutlineRenderer::set_geometry_available(bool ordinary, bool active) {
  impl_->ordinary_geometry = ordinary;
  impl_->active_geometry = active;
  impl_->update_visibility();
}
void SelectedMeshOutlineRenderer::render_masks(filament::Renderer &renderer) {
  QDR_PROFILE_FUNCTION();
  auto &state = *impl_;
  if (!initialized() || !state.visible || !state.has_camera ||
      (!state.ordinary_geometry && !state.active_geometry) ||
      !state.masks[0].target || !state.masks[1].target) return;
  const auto clear = renderer.getClearOptions();
  renderer.setClearOptions({.clearColor = {0.0f, 0.0f, 0.0f, 0.0f},
                            .clear = true, .discard = false});
  for (const auto &mask : state.masks) renderer.render(mask.view);
  renderer.setClearOptions(clear);
}

} // namespace quader
