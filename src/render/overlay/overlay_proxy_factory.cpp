////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_proxy_factory.hpp"

#include "diagnostics/profile.hpp"
#include "core/color_space.hpp"
#include "render/overlay/overlay_point_mesh_builder.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/png_texture_loader.hpp"
#include "render/render_layers.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <backend/DriverEnums.h>
#include <filament/Box.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/VertexBuffer.h>
#include <utils/Entity.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

filament::TextureSampler shared_position_sampler() {
  return {filament::TextureSampler::MinFilter::NEAREST,
          filament::TextureSampler::MagFilter::NEAREST,
          filament::TextureSampler::WrapMode::CLAMP_TO_EDGE};
}

std::vector<filament::MaterialInstance *> proxy_materials(
    const OverlayRenderableProxy &proxy) {
  std::vector<filament::MaterialInstance *> materials{
          proxy.material_instance, proxy.occluded_material_instance,
          proxy.component_point_materials[0u], proxy.component_point_materials[1u],
          proxy.component_point_materials[2u]};
  materials.insert(materials.end(), proxy.face_fill_materials.begin(),
                   proxy.face_fill_materials.end());
  return materials;
}

filament::backend::PixelBufferDescriptor shared_position_pixels(
    std::vector<OverlayFloat4> positions) {
  auto storage = FilamentBufferDescriptorFactory::vector(std::move(positions));
  filament::backend::PixelBufferDescriptor pixels(
      storage.buffer, storage.size, filament::backend::PixelDataFormat::RGBA,
      filament::backend::PixelDataType::FLOAT, storage.getCallback(), storage.getUser());
  storage.setCallback(nullptr);
  return pixels;
}

/// Static ribbon controls stay separate from preview-updated endpoints.
struct RetainedEditWireVertex {
  OverlayFloat4 position;
  OverlayFloat4 control;
  std::uint32_t color = 0u;
};

// The shared viewport scale is sanitized to at most 15x. Keep one native texel
// available per displayed pixel at that upper bound, then let Filament's mip chain
// handle every smaller presentation without depending on a fixed-DPI asset.
constexpr std::uint32_t kPointLightIconTextureExtent = 1024u;
static_assert(static_cast<float>(kPointLightIconTextureExtent) >=
              kOverlayPointLightGizmoSizePixels *
                  kViewportPixelScaleMaximum);

struct IconColor {
  float red = 0.0f;
  float green = 0.0f;
  float blue = 0.0f;
  float alpha = 0.0f;
};

float unit_clamp(float value) { return std::clamp(value, 0.0f, 1.0f); }

float antialiased_coverage(float signed_distance) {
  constexpr float kPixelWidth =
      2.0f / static_cast<float>(kPointLightIconTextureExtent);
  return unit_clamp(0.5f - signed_distance / kPixelWidth);
}

float rounded_box_signed_distance(float x, float y, float center_x,
                                  float center_y, float half_width,
                                  float half_height, float radius) {
  const float qx = std::abs(x - center_x) - half_width + radius;
  const float qy = std::abs(y - center_y) - half_height + radius;
  const float outside_x = std::max(qx, 0.0f);
  const float outside_y = std::max(qy, 0.0f);
  return std::hypot(outside_x, outside_y) +
         std::min(std::max(qx, qy), 0.0f) - radius;
}

float bulb_half_width(float y) {
  constexpr float kBulbCenterY = -0.31f;
  constexpr float kBulbRadius = 0.54f;
  const float circle_y = y - kBulbCenterY;
  const float circle_width =
      std::sqrt(std::max(kBulbRadius * kBulbRadius - circle_y * circle_y,
                         0.0f));

  float taper_width = 0.0f;
  if (y >= 0.03f && y <= 0.42f) {
    const float taper = (y - 0.03f) / (0.42f - 0.03f);
    taper_width = 0.40f + (0.21f - 0.40f) * taper;
  }
  return std::max(circle_width, taper_width);
}

float bulb_signed_distance(float x, float y) {
  constexpr float kTop = -0.85f;
  constexpr float kBottom = 0.42f;
  const float horizontal = std::abs(x) - bulb_half_width(y);
  const float vertical = std::max(kTop - y, y - kBottom);
  return std::max(horizontal, vertical);
}

void composite_icon_color(IconColor &destination, IconColor source,
                          float coverage) {
  const float source_alpha = unit_clamp(source.alpha * coverage);
  const float result_alpha =
      source_alpha + destination.alpha * (1.0f - source_alpha);
  if (result_alpha <= 0.0f) {
    return;
  }
  const float retained = destination.alpha * (1.0f - source_alpha);
  destination.red =
      (source.red * source_alpha + destination.red * retained) / result_alpha;
  destination.green = (source.green * source_alpha +
                       destination.green * retained) /
                      result_alpha;
  destination.blue = (source.blue * source_alpha +
                      destination.blue * retained) /
                     result_alpha;
  destination.alpha = result_alpha;
}

std::byte icon_channel_byte(float value) {
  return static_cast<std::byte>(
      static_cast<std::uint8_t>(std::lround(unit_clamp(value) * 255.0f)));
}

std::vector<std::byte> make_point_light_icon_rgba() {
  constexpr std::size_t kChannelCount = 4u;
  const std::size_t pixel_count =
      static_cast<std::size_t>(kPointLightIconTextureExtent) *
      kPointLightIconTextureExtent;
  std::vector<std::byte> pixels(pixel_count * kChannelCount);

  for (std::uint32_t row = 0u; row < kPointLightIconTextureExtent; ++row) {
    const float y =
        (static_cast<float>(row) + 0.5f) * 2.0f /
            static_cast<float>(kPointLightIconTextureExtent) -
        1.0f;
    for (std::uint32_t column = 0u; column < kPointLightIconTextureExtent;
         ++column) {
      const float x =
          (static_cast<float>(column) + 0.5f) * 2.0f /
              static_cast<float>(kPointLightIconTextureExtent) -
          1.0f;
      IconColor color;

      const float body_distance = bulb_signed_distance(x, y);
      composite_icon_color(color, {0.10f, 0.07f, 0.025f, 1.0f},
                           antialiased_coverage(body_distance - 0.025f));

      const float body_coverage = antialiased_coverage(body_distance);
      const float edge =
          unit_clamp(std::abs(x) / std::max(bulb_half_width(y), 0.001f));
      const float lower_warmth = unit_clamp((y + 0.15f) / 0.57f);
      const IconColor bulb_color{
          1.0f,
          0.86f - edge * 0.20f - lower_warmth * 0.08f,
          0.12f - edge * 0.08f,
          1.0f,
      };
      composite_icon_color(color, bulb_color, body_coverage);

      const float highlight_x = (x + 0.20f) / 0.18f;
      const float highlight_y = (y + 0.48f) / 0.25f;
      const float highlight = unit_clamp(
          1.0f - std::hypot(highlight_x, highlight_y));
      composite_icon_color(color, {1.0f, 1.0f, 0.88f, 0.72f},
                           highlight * body_coverage);

      const float base_distance =
          rounded_box_signed_distance(x, y, 0.0f, 0.60f, 0.245f, 0.25f,
                                      0.075f);
      composite_icon_color(color, {0.055f, 0.05f, 0.045f, 1.0f},
                           antialiased_coverage(base_distance - 0.018f));
      composite_icon_color(color, {0.15f, 0.145f, 0.14f, 1.0f},
                           antialiased_coverage(base_distance));

      constexpr float kBandCenters[] = {0.43f, 0.56f, 0.69f};
      for (float band_center : kBandCenters) {
        const float band_distance = rounded_box_signed_distance(
            x, y, 0.0f, band_center, 0.215f, 0.052f, 0.032f);
        composite_icon_color(color, {0.035f, 0.03f, 0.028f, 1.0f},
                             antialiased_coverage(band_distance - 0.012f));
        const float band_luminance =
            0.72f + 0.20f * unit_clamp(1.0f - std::abs(x) / 0.215f);
        composite_icon_color(
            color,
            {band_luminance, band_luminance, band_luminance * 0.96f, 1.0f},
            antialiased_coverage(band_distance));
      }

      const std::size_t offset =
          (static_cast<std::size_t>(row) * kPointLightIconTextureExtent +
           column) *
          kChannelCount;
      pixels[offset] = icon_channel_byte(color.red);
      pixels[offset + 1u] = icon_channel_byte(color.green);
      pixels[offset + 2u] = icon_channel_byte(color.blue);
      pixels[offset + 3u] = icon_channel_byte(color.alpha);
    }
  }
  return pixels;
}

std::uint32_t to_overlay_entity_id(utils::Entity entity) {
  return static_cast<std::uint32_t>(utils::Entity::smuggle(entity));
}

utils::Entity to_filament_entity(std::uint32_t entity_id) {
  return utils::Entity::import(static_cast<std::int32_t>(entity_id));
}

filament::Box overlay_mesh_bounds() {
  filament::Box box;
  const filament::math::float3 half_extent{500000.0f, 500000.0f, 500000.0f};
  box.set(-half_extent, half_extent);
  return box;
}

void destroy_overlay_proxy_resources(filament::Engine &engine,
                                     filament::Scene *scene,
                                     OverlayRenderableProxy &proxy,
                                     bool remove_from_scene);

/**
 * Cleans up partially constructed overlay resources until ownership is handed to the proxy.
 */
class OverlayProxyCreationGuard {
public:
    OverlayProxyCreationGuard(filament::Engine& engine, filament::Scene& scene, OverlayRenderableProxy& proxy)
        : engine_(engine)
        , scene_(scene)
        , proxy_(proxy) {}

    OverlayProxyCreationGuard(const OverlayProxyCreationGuard&) = delete;
    OverlayProxyCreationGuard& operator=(const OverlayProxyCreationGuard&) = delete;
    OverlayProxyCreationGuard(OverlayProxyCreationGuard&&) = delete;
    OverlayProxyCreationGuard& operator=(OverlayProxyCreationGuard&&) = delete;

    ~OverlayProxyCreationGuard() {
        if (active_) {
          destroy_overlay_proxy_resources(engine_, &scene_, proxy_,
                                          scene_added_);
        }
    }

    void mark_scene_added() { scene_added_ = true; }

    void release() {
        active_ = false;
    }

private:
    filament::Engine& engine_;
    filament::Scene& scene_;
    OverlayRenderableProxy& proxy_;
    bool scene_added_ = false;
    bool active_ = true;
};

void publish_overlay_proxy(filament::Scene &scene, OverlayRenderableProxy &proxy,
                           std::vector<OverlayRenderableProxy> &proxies,
                           OverlayProxyCreationGuard &creation_guard) {
  const auto entity = to_filament_entity(proxy.entity_id);
  scene.addEntity(entity);
  creation_guard.mark_scene_added();
  proxies.push_back(std::move(proxy));
  creation_guard.release();
}

filament::MaterialInstance::DepthFunc
to_filament_depth_func(OverlayDepthFunc depth_func) {
  switch (depth_func) {
  case OverlayDepthFunc::Always:
    return filament::MaterialInstance::DepthFunc::A;
  case OverlayDepthFunc::Equal:
    return filament::MaterialInstance::DepthFunc::E;
  case OverlayDepthFunc::Less:
    return filament::MaterialInstance::DepthFunc::L;
  case OverlayDepthFunc::GreaterOrEqual:
    return filament::MaterialInstance::DepthFunc::GE;
  }
  return filament::MaterialInstance::DepthFunc::A;
}

filament::MaterialInstance::CullingMode
to_filament_culling_mode(OverlayCullingMode culling_mode) {
  switch (culling_mode) {
  case OverlayCullingMode::None:
    return filament::MaterialInstance::CullingMode::NONE;
  case OverlayCullingMode::Back:
    return filament::MaterialInstance::CullingMode::BACK;
  }
  return filament::MaterialInstance::CullingMode::NONE;
}

filament::MaterialInstance::TransparencyMode
to_filament_transparency_mode(OverlayTransparencyMode transparency_mode) {
  switch (transparency_mode) {
  case OverlayTransparencyMode::Default:
    return filament::MaterialInstance::TransparencyMode::DEFAULT;
  case OverlayTransparencyMode::TwoPassesOneSide:
    return filament::MaterialInstance::TransparencyMode::TWO_PASSES_ONE_SIDE;
  case OverlayTransparencyMode::TwoPassesTwoSides:
    return filament::MaterialInstance::TransparencyMode::TWO_PASSES_TWO_SIDES;
  }
  return filament::MaterialInstance::TransparencyMode::DEFAULT;
}

std::pair<std::uint16_t, std::uint16_t>
line_blend_orders(const OverlayLineBatch &batch, bool visible_mask_supported) {
  const std::uint16_t visible = batch.state.blend_order;
  const std::uint16_t occluded = batch.occluded_state
                                   ? batch.occluded_state->blend_order
                                   : visible;
  // Without stencil, draw visible contours last so hidden contours cannot
  // cover an opaque active outline. Translucent overlap remains a fallback
  // limitation; Vulkan instead excludes hidden coverage through stencil.
  return !visible_mask_supported && batch.occluded_state
             ? std::pair{occluded, visible}
             : std::pair{visible, occluded};
}

void destroy_overlay_proxy_resources(filament::Engine &engine,
                                     filament::Scene *scene,
                                     OverlayRenderableProxy &proxy,
                                     bool remove_from_scene) {
  if (proxy.entity_id != 0u) {
    const utils::Entity entity = to_filament_entity(proxy.entity_id);
    if (remove_from_scene && scene != nullptr) {
      scene->remove(entity);
    }
    engine.getRenderableManager().destroy(entity);
  }
  if (proxy.material_instance) {
    engine.destroy(proxy.material_instance);
    proxy.material_instance = nullptr;
  }
  if (proxy.occluded_material_instance) {
    engine.destroy(proxy.occluded_material_instance);
    proxy.occluded_material_instance = nullptr;
  }
  for (auto *&material : proxy.component_point_materials) {
    if (material != nullptr) {
      engine.destroy(material);
      material = nullptr;
    }
  }
  for (auto *material : proxy.face_fill_materials) {
    if (material != nullptr) engine.destroy(material);
  }
  proxy.face_fill_materials.clear();
  proxy.face_fill_colors.clear();
  proxy.face_fill_triangle_colors.clear();
  if (proxy.vertex_buffer) {
    engine.destroy(proxy.vertex_buffer);
    proxy.vertex_buffer = nullptr;
  }
  if (proxy.index_buffer) {
    engine.destroy(proxy.index_buffer);
    proxy.index_buffer = nullptr;
  }
  if (proxy.entity_id != 0u) {
    utils::EntityManager::get().destroy(to_filament_entity(proxy.entity_id));
    proxy.entity_id = 0u;
  }
  proxy.visibility_point_indices.clear();
  proxy.point_visibility_stream.clear();
  proxy.component_point_appearance.clear();
}

template <typename Vertex, typename ConfigureAttributes,
          typename ConfigureMaterial,
          typename ConfigureOccludedMaterial = std::nullptr_t>
void append_overlay_proxy(filament::Engine &engine, filament::Scene &scene,
                          filament::Material &material,
                          const OverlayMesh<Vertex> &mesh,
                          OverlayRenderableProxyKind kind,
                          const OverlayRenderState &state,
                          std::uint8_t layer_mask,
                          std::vector<OverlayRenderableProxy> &proxies,
                          ConfigureAttributes configure_attributes,
                          ConfigureMaterial configure_material,
                          const OverlayRenderState *occluded_state = nullptr,
                          ConfigureOccludedMaterial configure_occluded_material =
                              nullptr) {
  if (mesh.vertices.empty() || mesh.indices.empty()) {
    return;
  }

  OverlayRenderableProxy proxy;
  proxy.kind = kind;
  proxy.vertex_count = static_cast<std::uint32_t>(mesh.vertices.size());
  proxy.index_count = static_cast<std::uint32_t>(mesh.indices.size());
  proxy.position_stream_bindings = mesh.position_stream_bindings;
  if constexpr (std::is_same_v<Vertex, OverlayFaceFillVertex>) {
    proxy.position_stream3.reserve(mesh.vertices.size());
    for (const Vertex &vertex : mesh.vertices) {
      proxy.position_stream3.push_back(vertex.position);
    }
  } else if constexpr (std::is_same_v<Vertex, OverlayLineVertex>) {
    proxy.primary_position_stream4.reserve(mesh.vertices.size());
    proxy.secondary_position_stream4.reserve(mesh.vertices.size());
    for (const Vertex &vertex : mesh.vertices) {
      proxy.primary_position_stream4.push_back(vertex.start);
      proxy.secondary_position_stream4.push_back(vertex.end);
    }
  } else if constexpr (std::is_same_v<Vertex, OverlayPointVertex>) {
    proxy.primary_position_stream4.reserve(mesh.vertices.size());
    proxy.point_visibility_stream.assign(mesh.vertices.size(), 1.0f);
    for (const Vertex &vertex : mesh.vertices) {
      proxy.primary_position_stream4.push_back(vertex.center);
    }
  }
  const utils::Entity entity = utils::EntityManager::get().create();
  proxy.entity_id = to_overlay_entity_id(entity);
  OverlayProxyCreationGuard creation_guard(engine, scene, proxy);
  proxy.material_instance = material.createInstance();
  configure_material(*proxy.material_instance);
  if constexpr (!std::is_same_v<ConfigureOccludedMaterial, std::nullptr_t>) {
    if (occluded_state != nullptr) {
      proxy.occluded_material_instance = material.createInstance();
      configure_occluded_material(*proxy.occluded_material_instance);
    }
  }

  filament::VertexBuffer::Builder vertex_builder;
  std::uint8_t buffer_count = 1u;
  if (kind == OverlayRenderableProxyKind::Point) {
    buffer_count = 3u;
  } else if (!proxy.position_stream3.empty() ||
             !proxy.primary_position_stream4.empty()) {
    buffer_count = proxy.secondary_position_stream4.empty() ? 2u : 3u;
  }
  vertex_builder.vertexCount(static_cast<std::uint32_t>(mesh.vertices.size()))
      .bufferCount(buffer_count);
  configure_attributes(vertex_builder);
  proxy.vertex_buffer = vertex_builder.build(engine);
  proxy.vertex_buffer->setBufferAt(
      engine, 0, FilamentBufferDescriptorFactory::vector(mesh.vertices));
  if (!proxy.position_stream3.empty()) {
    proxy.vertex_buffer->setBufferAt(
        engine, 1,
        FilamentBufferDescriptorFactory::vector(proxy.position_stream3));
  } else if (!proxy.primary_position_stream4.empty()) {
    proxy.vertex_buffer->setBufferAt(
        engine, 1, FilamentBufferDescriptorFactory::vector(
                       proxy.primary_position_stream4));
  }
  if (!proxy.point_visibility_stream.empty()) {
    proxy.vertex_buffer->setBufferAt(
        engine, 2,
        FilamentBufferDescriptorFactory::vector(proxy.point_visibility_stream));
  } else if (!proxy.secondary_position_stream4.empty()) {
    proxy.vertex_buffer->setBufferAt(
        engine, 2, FilamentBufferDescriptorFactory::vector(
                       proxy.secondary_position_stream4));
  }

  proxy.index_buffer =
      filament::IndexBuffer::Builder()
          .indexCount(static_cast<std::uint32_t>(mesh.indices.size()))
          .bufferType(filament::IndexBuffer::IndexType::UINT)
          .build(engine);
  proxy.index_buffer->setBuffer(
      engine, FilamentBufferDescriptorFactory::vector(mesh.indices));

  filament::RenderableManager::Builder renderable_builder(
      proxy.occluded_material_instance != nullptr ? 2u : 1u);
  renderable_builder
      .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES,
                proxy.vertex_buffer, proxy.index_buffer)
      .material(0, proxy.material_instance)
      .blendOrder(0, state.blend_order)
      .globalBlendOrderEnabled(0, true)
      .layerMask(kViewportAllLayerMask, layer_mask)
      .boundingBox(overlay_mesh_bounds())
      .culling(false)
      .castShadows(false)
      .receiveShadows(false);
  if (proxy.occluded_material_instance != nullptr) {
    renderable_builder
        .geometry(1u, filament::RenderableManager::PrimitiveType::TRIANGLES,
                  proxy.vertex_buffer, proxy.index_buffer)
        .material(1u, proxy.occluded_material_instance)
        .blendOrder(1u, occluded_state->blend_order)
        .globalBlendOrderEnabled(1u, true);
  }
  renderable_builder.build(engine, entity);
  publish_overlay_proxy(scene, proxy, proxies, creation_guard);
}

template <typename Vertex>
bool proxy_can_update_mesh(const OverlayRenderableProxy &proxy,
                           OverlayRenderableProxyKind kind,
                           const OverlayMesh<Vertex> &mesh) {
  return !mesh.empty() && proxy.kind == kind &&
         proxy.edit_wire_slot_capacity == 0u &&
         proxy.face_fill_colors.empty() &&
         proxy.vertex_buffer != nullptr && proxy.index_buffer != nullptr &&
         proxy.material_instance != nullptr &&
         proxy.vertex_count == mesh.vertices.size() &&
         proxy.index_count == mesh.indices.size();
}

void update_proxy_layer_mask(filament::Engine &engine,
                             const OverlayRenderableProxy &proxy,
                             std::uint8_t layer_mask) {
  if (proxy.entity_id == 0u) {
    return;
  }
  filament::RenderableManager &renderables = engine.getRenderableManager();
  const filament::RenderableManager::Instance instance =
      renderables.getInstance(to_filament_entity(proxy.entity_id));
  if (!instance) {
    return;
  }
  renderables.setLayerMask(instance, kViewportAllLayerMask, layer_mask);
}

template <typename Vertex>
void refresh_proxy_position_streams(OverlayRenderableProxy &proxy,
                                    const OverlayMesh<Vertex> &mesh) {
  proxy.position_stream_bindings = mesh.position_stream_bindings;
  proxy.position_stream3.clear();
  proxy.primary_position_stream4.clear();
  proxy.secondary_position_stream4.clear();
  if constexpr (std::is_same_v<Vertex, OverlayFaceFillVertex>) {
    proxy.position_stream3.reserve(mesh.vertices.size());
    for (const Vertex &vertex : mesh.vertices) {
      proxy.position_stream3.push_back(vertex.position);
    }
  } else if constexpr (std::is_same_v<Vertex, OverlayLineVertex>) {
    proxy.primary_position_stream4.reserve(mesh.vertices.size());
    proxy.secondary_position_stream4.reserve(mesh.vertices.size());
    for (const Vertex &vertex : mesh.vertices) {
      proxy.primary_position_stream4.push_back(vertex.start);
      proxy.secondary_position_stream4.push_back(vertex.end);
    }
  } else if constexpr (std::is_same_v<Vertex, OverlayPointVertex>) {
    proxy.primary_position_stream4.reserve(mesh.vertices.size());
    for (const Vertex &vertex : mesh.vertices) {
      proxy.primary_position_stream4.push_back(vertex.center);
    }
  }
}

bool upload_proxy_position_streams(filament::Engine &engine,
                                   OverlayRenderableProxy &proxy) {
  if (proxy.vertex_buffer == nullptr) {
    return false;
  }
  if (!proxy.position_stream3.empty()) {
    proxy.vertex_buffer->setBufferAt(
        engine, 1,
        FilamentBufferDescriptorFactory::vector(proxy.position_stream3));
  } else if (!proxy.primary_position_stream4.empty()) {
    proxy.vertex_buffer->setBufferAt(
        engine, 1, FilamentBufferDescriptorFactory::vector(
                       proxy.primary_position_stream4));
  }
  if (!proxy.secondary_position_stream4.empty()) {
    proxy.vertex_buffer->setBufferAt(
        engine, 2, FilamentBufferDescriptorFactory::vector(
                       proxy.secondary_position_stream4));
  }
  return !proxy.position_stream3.empty() ||
         !proxy.primary_position_stream4.empty();
}

template <typename Vertex>
void update_proxy_vertex_buffer(filament::Engine &engine,
                                OverlayRenderableProxy &proxy,
                                const OverlayMesh<Vertex> &mesh) {
  proxy.vertex_buffer->setBufferAt(
      engine, 0, FilamentBufferDescriptorFactory::vector(mesh.vertices));
  refresh_proxy_position_streams(proxy, mesh);
  (void)upload_proxy_position_streams(engine, proxy);
}

template <typename Vertex>
void update_proxy_buffers(filament::Engine &engine,
                          OverlayRenderableProxy &proxy,
                          const OverlayMesh<Vertex> &mesh,
                          std::uint8_t layer_mask) {
  update_proxy_vertex_buffer(engine, proxy, mesh);
  proxy.index_buffer->setBuffer(
      engine, FilamentBufferDescriptorFactory::vector(mesh.indices));
  update_proxy_layer_mask(engine, proxy, layer_mask);
}

template <typename Vertex>
void update_canonical_quad_proxy_vertices(filament::Engine &engine,
                                          OverlayRenderableProxy &proxy,
                                          const OverlayMesh<Vertex> &mesh,
                                          std::uint8_t layer_mask) {
  // OverlayLineMeshBuilder emits the same six indices for every four-vertex
  // line quad. proxy_can_update_mesh has already verified the kind and both
  // counts, so the existing index payload is identical. World endpoints and
  // color remain encoded in the vertices; clipping, depth and width are shader
  // state and built-in view uniforms.
  update_proxy_vertex_buffer(engine, proxy, mesh);
  update_proxy_layer_mask(engine, proxy, layer_mask);
}

} // namespace

/// Keeps candidate host storage independent of every live overlay stream.
struct OverlayProxyFactory::RetainedEditWireUpdate final {
  /// Coalesces adjacent changed slots into one upload per attribute stream.
  struct Range final {
    std::uint32_t first_vertex = 0u;
    std::vector<OverlayFloat4> starts;
    std::vector<OverlayFloat4> ends;
    filament::backend::BufferDescriptor static_upload;
    filament::backend::BufferDescriptor start_upload;
    filament::backend::BufferDescriptor end_upload;
  };
  filament::VertexBuffer *expected_vertices = nullptr;
  filament::IndexBuffer *expected_indices = nullptr;
  std::uint32_t expected_entity = 0u;
  std::uint64_t expected_revision = 0u;
  std::uint32_t slot_capacity = 0u;
  std::uint32_t index_count = 0u;
  std::vector<Range> ranges;
  std::vector<OverlayPositionStreamBinding> bindings;
  filament::backend::BufferDescriptor index_upload;
  bool indices_changed = false;
  bool consolidated = false;
  bool consumed = false;
};

std::shared_ptr<OverlayProxyFactory::RetainedEditWireUpdate>
OverlayProxyFactory::prepare_retained_edit_wire_update(
    const OverlayRenderableProxy *existing,
    std::span<const ViewportOverlayLine> slots,
    std::span<const std::uint32_t> dirty_slots,
    std::span<const std::uint32_t> active_indices,
    std::span<const OverlayPositionStreamBinding> active_bindings,
    bool indices_changed) {
  QDR_PROFILE_FUNCTION();
  constexpr std::size_t kMaximumSlots =
      std::numeric_limits<std::uint32_t>::max() /
      (4u * sizeof(RetainedEditWireVertex));
  if (slots.empty() || slots.size() > kMaximumSlots ||
      active_indices.empty() || active_indices.size() % 6u != 0u ||
      active_indices.size() > slots.size() * 6u) {
    return {};
  }
  const auto vertex_count = static_cast<std::uint32_t>(slots.size() * 4u);
  if (existing != nullptr &&
      (existing->kind != OverlayRenderableProxyKind::EditWire ||
       existing->edit_wire_slot_capacity != slots.size() ||
       existing->vertex_count != vertex_count ||
       existing->vertex_buffer == nullptr || existing->index_buffer == nullptr ||
       existing->material_instance == nullptr || existing->entity_id == 0u ||
       existing->primary_position_stream4.size() != vertex_count ||
       existing->secondary_position_stream4.size() != vertex_count ||
       (!indices_changed && existing->index_count != active_indices.size()))) {
    return {};
  }
  constexpr std::array<std::uint32_t, 6u> kQuadIndices{0u, 1u, 2u, 0u, 2u, 3u};
  for (std::size_t index = 0u; index < active_indices.size(); index += 6u) {
    const auto first = active_indices[index];
    if (first % 4u != 0u || first >= vertex_count) {
      return {};
    }
    for (std::size_t corner = 0u; corner < kQuadIndices.size(); ++corner) {
      if (active_indices[index + corner] != first + kQuadIndices[corner]) {
        return {};
      }
    }
  }
  for (std::size_t index = 0u; index < dirty_slots.size(); ++index) {
    if (dirty_slots[index] >= slots.size() ||
        (index != 0u && dirty_slots[index - 1u] >= dirty_slots[index])) {
      return {};
    }
  }
  const auto finite_position = [](QVec3 point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
  };
  if (existing == nullptr) {
    for (const auto &line : slots) {
      if (!finite_position(line.start) || !finite_position(line.end)) return {};
    }
  } else {
    for (const auto slot : dirty_slots) {
      if (!finite_position(slots[slot].start) || !finite_position(slots[slot].end)) return {};
    }
  }
  for (const auto &binding : active_bindings) {
    if (binding.object_id < 0 || binding.vertex_id < 0 ||
        binding.first_vertex % 4u != 0u || binding.first_vertex >= vertex_count ||
        binding.vertex_count != 4u ||
        (binding.stream != OverlayPositionStream::Primary &&
         binding.stream != OverlayPositionStream::Secondary)) {
      return {};
    }
    const auto &line = slots[binding.first_vertex / 4u];
    const bool primary = binding.stream == OverlayPositionStream::Primary;
    const auto position = primary ? line.start : line.end;
    if (binding.object_id != line.element.object_id ||
        binding.vertex_id != (primary ? line.element.edge_a : line.element.edge_b) ||
        binding.base_position.x != position.x || binding.base_position.y != position.y ||
        binding.base_position.z != position.z || binding.base_position.w != 1.0f) {
      return {};
    }
  }

  auto prepared = std::make_shared<RetainedEditWireUpdate>();
  prepared->slot_capacity = static_cast<std::uint32_t>(slots.size());
  prepared->index_count = static_cast<std::uint32_t>(active_indices.size());
  prepared->indices_changed = existing == nullptr || indices_changed;
  prepared->bindings.assign(active_bindings.begin(), active_bindings.end());
  if (existing != nullptr) {
    prepared->expected_vertices = existing->vertex_buffer;
    prepared->expected_indices = existing->index_buffer;
    prepared->expected_entity = existing->entity_id;
    prepared->expected_revision = existing->edit_wire_update_revision;
  }
  if (prepared->indices_changed) {
    prepared->index_upload = FilamentBufferDescriptorFactory::span(active_indices);
  }
  const auto append_range = [&](std::uint32_t first_slot, std::uint32_t end_slot,
                                bool preserve_unchanged = false) {
    auto &range = prepared->ranges.emplace_back();
    range.first_vertex = first_slot * 4u;
    const std::size_t count = static_cast<std::size_t>(end_slot - first_slot) * 4u;
    std::vector<RetainedEditWireVertex> vertices;
    vertices.reserve(count);
    range.starts.reserve(count);
    range.ends.reserve(count);
    constexpr std::array<OverlayFloat4, 4u> kControls{{
        {-1.0f, -1.0f, 0.0f, 0.0f}, {-1.0f, 1.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 1.0f, 0.0f}, {1.0f, -1.0f, 0.0f, 0.0f}}};
    auto dirty = std::lower_bound(dirty_slots.begin(), dirty_slots.end(), first_slot);
    for (std::uint32_t slot = first_slot; slot < end_slot; ++slot) {
      const auto &line = slots[slot];
      const auto color = rgba8_srgb_to_linear(line.color);
      const bool changed = !preserve_unchanged ||
                           (dirty != dirty_slots.end() && *dirty == slot);
      if (dirty != dirty_slots.end() && *dirty == slot) ++dirty;
      for (std::size_t corner = 0u; corner < kControls.size(); ++corner) {
        vertices.push_back({{0.0f, 0.0f, 0.0f, 1.0f}, kControls[corner], color});
        const std::size_t vertex = static_cast<std::size_t>(slot) * 4u + corner;
        // A consolidated gap may currently carry preview positions. Keep its
        // exact accepted endpoints rather than resetting it to the frame base.
        range.starts.push_back(changed
            ? OverlayFloat4{line.start.x, line.start.y, line.start.z, 1.0f}
            : existing->primary_position_stream4[vertex]);
        range.ends.push_back(changed
            ? OverlayFloat4{line.end.x, line.end.y, line.end.z, 1.0f}
            : existing->secondary_position_stream4[vertex]);
      }
    }
    range.static_upload = FilamentBufferDescriptorFactory::vector(std::move(vertices));
    range.start_upload = FilamentBufferDescriptorFactory::vector(range.starts);
    range.end_upload = FilamentBufferDescriptorFactory::vector(range.ends);
  };
  if (existing == nullptr) {
    append_range(0u, prepared->slot_capacity);
  } else {
    constexpr std::size_t kMaximumPartialRanges = 64u;
    std::size_t range_count = 0u;
    for (std::size_t index = 0u; index < dirty_slots.size(); ++index) {
      range_count += index == 0u || dirty_slots[index - 1u] + 1u != dirty_slots[index];
    }
    if (range_count > kMaximumPartialRanges) {
      // Each range emits three backend commands. A committed deformation can
      // dirty tens of thousands of isolated edges; stage one bounded submission
      // instead of overflowing Filament's fixed command arena before beginFrame.
      prepared->consolidated = true;
      append_range(dirty_slots.front(), dirty_slots.back() + 1u, true);
    } else {
      for (std::size_t index = 0u; index < dirty_slots.size();) {
        const auto first = dirty_slots[index++];
        auto end = first + 1u;
        while (index < dirty_slots.size() && dirty_slots[index] == end) {
          ++index;
          ++end;
        }
        append_range(first, end);
      }
    }
  }
  return prepared;
}

OverlayRenderableProxy OverlayProxyFactory::create_retained_edit_wire_proxy(
    const OverlayEditWireBatch &presentation, std::uint8_t layer_mask,
    std::shared_ptr<RetainedEditWireUpdate> prepared) {
  QDR_PROFILE_FUNCTION();
  OverlayRenderableProxy proxy;
  if (!initialized() || !prepared || prepared->consumed ||
      prepared->expected_vertices != nullptr || prepared->ranges.size() != 1u ||
      prepared->slot_capacity == 0u || prepared->index_count == 0u) {
    return proxy;
  }
  std::vector<OverlayRenderableProxy> proxies;
  proxies.reserve(1u);
  const auto entity = utils::EntityManager::get().create();
  proxy.entity_id = to_overlay_entity_id(entity);
  OverlayProxyCreationGuard guard(*engine_, *scene_, proxy);
  proxy.kind = OverlayRenderableProxyKind::EditWire;
  proxy.vertex_count = prepared->slot_capacity * 4u;
  proxy.index_count = prepared->index_count;
  proxy.edit_wire_slot_capacity = prepared->slot_capacity;
  proxy.edit_wire_update_revision = 1u;
  proxy.material_instance = edit_wire_line_material_->createInstance();
  if (proxy.material_instance == nullptr) return {};
  configure_edit_wire_material(*proxy.material_instance, presentation);
  proxy.vertex_buffer = filament::VertexBuffer::Builder()
      .vertexCount(proxy.vertex_count).bufferCount(3u)
      .attribute(filament::VertexAttribute::POSITION, 0u, AttributeType::FLOAT4,
                 offsetof(RetainedEditWireVertex, position), sizeof(RetainedEditWireVertex))
      .attribute(filament::VertexAttribute::CUSTOM0, 1u, AttributeType::FLOAT4,
                 0u, sizeof(OverlayFloat4))
      .attribute(filament::VertexAttribute::CUSTOM1, 2u, AttributeType::FLOAT4,
                 0u, sizeof(OverlayFloat4))
      .attribute(filament::VertexAttribute::CUSTOM2, 0u, AttributeType::FLOAT4,
                 offsetof(RetainedEditWireVertex, control), sizeof(RetainedEditWireVertex))
      .attribute(filament::VertexAttribute::COLOR, 0u, AttributeType::UBYTE4,
                 offsetof(RetainedEditWireVertex, color), sizeof(RetainedEditWireVertex))
      .normalized(filament::VertexAttribute::COLOR).build(*engine_);
  if (proxy.vertex_buffer == nullptr) return {};
  proxy.index_buffer = filament::IndexBuffer::Builder()
      .indexCount(proxy.edit_wire_slot_capacity * 6u)
      .bufferType(filament::IndexBuffer::IndexType::UINT).build(*engine_);
  if (proxy.index_buffer == nullptr) return {};
  auto &range = prepared->ranges.front();
  proxy.vertex_buffer->setBufferAt(*engine_, 0u, std::move(range.static_upload));
  proxy.vertex_buffer->setBufferAt(*engine_, 1u, std::move(range.start_upload));
  proxy.vertex_buffer->setBufferAt(*engine_, 2u, std::move(range.end_upload));
  proxy.index_buffer->setBuffer(*engine_, std::move(prepared->index_upload));
  prepared->consumed = true;
  const auto built = filament::RenderableManager::Builder(1u)
      .geometry(0u, filament::RenderableManager::PrimitiveType::TRIANGLES,
                proxy.vertex_buffer, proxy.index_buffer, 0u, proxy.index_count)
      .material(0u, proxy.material_instance)
      .blendOrder(0u, presentation.state.blend_order)
      .globalBlendOrderEnabled(0u, true)
      .layerMask(kViewportAllLayerMask, layer_mask)
      .boundingBox(overlay_mesh_bounds()).culling(false)
      .castShadows(false).receiveShadows(false).build(*engine_, entity);
  if (built != filament::RenderableManager::Builder::Success) return {};
  proxy.primary_position_stream4 = std::move(range.starts);
  proxy.secondary_position_stream4 = std::move(range.ends);
  proxy.position_stream_bindings = std::move(prepared->bindings);
  publish_overlay_proxy(*scene_, proxy, proxies, guard);
  ++upload_stats_.geometry_creation_count;
  ++upload_stats_.static_upload_count;
  upload_stats_.position_upload_count += 2u;
  ++upload_stats_.index_upload_count;
  upload_stats_.retained_edit_wire_uploaded_slot_count += proxy.edit_wire_slot_capacity;
  return std::move(proxies.front());
}

bool OverlayProxyFactory::update_retained_edit_wire_proxy(
    OverlayRenderableProxy &proxy, const OverlayEditWireBatch &presentation,
    std::uint8_t layer_mask, std::shared_ptr<RetainedEditWireUpdate> prepared) {
  QDR_PROFILE_FUNCTION();
  if (!initialized() || !prepared || prepared->consumed ||
      proxy.kind != OverlayRenderableProxyKind::EditWire ||
      proxy.vertex_buffer != prepared->expected_vertices ||
      proxy.index_buffer != prepared->expected_indices ||
      proxy.entity_id != prepared->expected_entity ||
      proxy.edit_wire_update_revision != prepared->expected_revision ||
      proxy.edit_wire_slot_capacity != prepared->slot_capacity ||
      proxy.material_instance == nullptr ||
      proxy.primary_position_stream4.size() != prepared->slot_capacity * 4u ||
      proxy.secondary_position_stream4.size() != prepared->slot_capacity * 4u) {
    return false;
  }
  auto &renderables = engine_->getRenderableManager();
  const auto instance = renderables.getInstance(to_filament_entity(proxy.entity_id));
  if (!instance || renderables.getPrimitiveCount(instance) != 1u) return false;
  if (!clear_shared_positions(proxy)) return false;
  prepared->consumed = true;
  for (auto &range : prepared->ranges) {
    std::copy(range.starts.begin(), range.starts.end(),
              proxy.primary_position_stream4.begin() + range.first_vertex);
    std::copy(range.ends.begin(), range.ends.end(),
              proxy.secondary_position_stream4.begin() + range.first_vertex);
    proxy.vertex_buffer->setBufferAt(*engine_, 0u, std::move(range.static_upload),
        range.first_vertex * sizeof(RetainedEditWireVertex));
    proxy.vertex_buffer->setBufferAt(*engine_, 1u, std::move(range.start_upload),
        range.first_vertex * sizeof(OverlayFloat4));
    proxy.vertex_buffer->setBufferAt(*engine_, 2u, std::move(range.end_upload),
        range.first_vertex * sizeof(OverlayFloat4));
    upload_stats_.retained_edit_wire_uploaded_slot_count += range.starts.size() / 4u;
  }
  if (prepared->indices_changed) {
    proxy.index_buffer->setBuffer(*engine_, std::move(prepared->index_upload));
    ++upload_stats_.index_upload_count;
  }
  if (proxy.index_count != prepared->index_count) {
    renderables.setGeometryAt(instance, 0u,
        filament::RenderableManager::PrimitiveType::TRIANGLES,
        proxy.vertex_buffer, proxy.index_buffer, 0u, prepared->index_count);
    proxy.index_count = prepared->index_count;
  }
  proxy.position_stream_bindings = std::move(prepared->bindings);
  ++proxy.edit_wire_update_revision;
  configure_edit_wire_material(*proxy.material_instance, presentation);
  renderables.setBlendOrderAt(instance, 0u, presentation.state.blend_order);
  renderables.setLayerMask(instance, kViewportAllLayerMask, layer_mask);
  upload_stats_.static_upload_count += prepared->ranges.size();
  upload_stats_.position_upload_count += prepared->ranges.size() * 2u;
  upload_stats_.retained_edit_wire_consolidated_upload_count += prepared->consolidated ? 1u : 0u;
  return true;
}

void OverlayProxyFactory::initialize(
    filament::Engine &engine, filament::Scene &scene,
    filament::Material &overlay_material,
    filament::Material &overlay_line_material,
    filament::Material &edit_wire_line_material,
    filament::Material &overlay_point_material,
    filament::Material &overlay_icon_material,
    const std::filesystem::path &resource_root) {
  shutdown();
  engine_ = &engine;
  scene_ = &scene;
  overlay_material_ = &overlay_material;
  overlay_line_material_ = &overlay_line_material;
  edit_wire_line_material_ = &edit_wire_line_material;
  overlay_point_material_ = &overlay_point_material;
  overlay_icon_material_ = &overlay_icon_material;
  shared_position_fallback_texture_ = filament::Texture::Builder()
      .width(1u).height(1u).levels(1u)
      .sampler(filament::Texture::Sampler::SAMPLER_2D)
      .format(filament::Texture::InternalFormat::RGBA8).build(engine);
  const unsigned char fallback[] = {0u, 0u, 0u, 255u};
  shared_position_fallback_texture_->setImage(engine, 0u,
      FilamentBufferDescriptorFactory::rgba_pixels(fallback, sizeof(fallback)));
  for (auto *material : {overlay_material_, overlay_line_material_,
                        edit_wire_line_material_, overlay_point_material_}) {
    if (material->hasParameter("sharedPositionsEnabled") &&
        material->hasParameter("sharedPositions")) {
      material->setDefaultParameter("sharedPositionsEnabled", false);
      material->setDefaultParameter("sharedPositions",
          shared_position_fallback_texture_, shared_position_sampler());
    }
  }
  load_icon_textures(resource_root);
}

void OverlayProxyFactory::shutdown() {
  destroy_icon_textures();
  if (engine_ != nullptr) {
    for (auto *material : {overlay_material_, overlay_line_material_,
                          edit_wire_line_material_, overlay_point_material_}) {
      if (material != nullptr && material->hasParameter("sharedPositions")) {
        material->setDefaultParameter("sharedPositions",
            static_cast<filament::Texture *>(nullptr), shared_position_sampler());
      }
    }
    if (shared_position_texture_ != nullptr) engine_->destroy(shared_position_texture_);
    if (shared_position_fallback_texture_ != nullptr) {
      engine_->destroy(shared_position_fallback_texture_);
    }
  }
  shared_position_texture_ = nullptr;
  shared_position_fallback_texture_ = nullptr;
  shared_position_materials_.clear();
  shared_position_count_ = 0u;
  engine_ = nullptr;
  scene_ = nullptr;
  overlay_material_ = nullptr;
  overlay_line_material_ = nullptr;
  edit_wire_line_material_ = nullptr;
  overlay_point_material_ = nullptr;
  overlay_icon_material_ = nullptr;
}

std::vector<OverlayRenderableProxy>
OverlayProxyFactory::create_layer_proxies(const OverlayLayerBuildPlan &plan,
                                          std::uint8_t layer_mask) {
  std::vector<OverlayRenderableProxy> proxies;
  if (!initialized()) {
    return proxies;
  }

  proxies.reserve(planned_loaded_proxy_count(plan));
  for (const OverlayFaceFillBatch &batch : plan.face_fill_batches) {
    create_triangle_proxy(batch, layer_mask, proxies);
  }
  for (const OverlayLineBatch &batch : plan.line_batches) {
    create_line_proxy(batch, layer_mask, proxies);
  }
  for (const OverlayEditWireBatch &batch : plan.edit_wire_batches) {
    create_edit_wire_proxy(batch, layer_mask, proxies);
  }
  for (const OverlayPointBatch &batch : plan.point_batches) {
    create_point_proxy(batch, layer_mask, proxies);
  }
  for (const OverlayIconBatch &batch : plan.icon_batches) {
    create_icon_proxy(batch, layer_mask, proxies);
  }
  for (const auto &proxy : proxies) {
    ++upload_stats_.geometry_creation_count;
    ++upload_stats_.static_upload_count;
    ++upload_stats_.index_upload_count;
    upload_stats_.position_upload_count +=
        proxy.secondary_position_stream4.empty() ? 1u : 2u;
    if (!proxy.point_visibility_stream.empty()) {
      ++upload_stats_.visibility_upload_count;
    }
  }
  return proxies;
}

bool OverlayProxyFactory::update_layer_proxies(
    std::vector<OverlayRenderableProxy> &proxies,
    const OverlayLayerBuildPlan &plan, std::uint8_t layer_mask) {
  if (!initialized() || proxies.size() != planned_loaded_proxy_count(plan)) {
    return false;
  }

  std::size_t index = 0;
  for (const OverlayFaceFillBatch &batch : plan.face_fill_batches) {
    if (batch.mesh.empty()) {
      continue;
    }
    if (index >= proxies.size() ||
        !proxy_can_update_mesh(proxies[index++],
                               OverlayRenderableProxyKind::Triangle,
                               batch.mesh)) {
      return false;
    }
  }
  for (const OverlayLineBatch &batch : plan.line_batches) {
    if (batch.mesh.empty()) {
      continue;
    }
    if (index >= proxies.size() ||
        (proxies[index].occluded_material_instance != nullptr) !=
            batch.occluded_state.has_value() ||
        !proxy_can_update_mesh(proxies[index++],
                               OverlayRenderableProxyKind::Line, batch.mesh)) {
      return false;
    }
  }
  for (const OverlayEditWireBatch &batch : plan.edit_wire_batches) {
    if (batch.mesh.empty()) {
      continue;
    }
    if (index >= proxies.size() ||
        !proxy_can_update_mesh(proxies[index++],
                               OverlayRenderableProxyKind::EditWire,
                               batch.mesh)) {
      return false;
    }
  }
  for (const OverlayPointBatch &batch : plan.point_batches) {
    if (batch.mesh.empty()) {
      continue;
    }
    if (index >= proxies.size() ||
        !proxy_can_update_mesh(proxies[index++],
                               OverlayRenderableProxyKind::Point, batch.mesh)) {
      return false;
    }
  }
  for (const OverlayIconBatch &batch : plan.icon_batches) {
    if (batch.mesh.empty() || texture_for_icon(batch.icon) == nullptr) {
      continue;
    }
    if (index >= proxies.size() ||
        !proxy_can_update_mesh(proxies[index++],
                               OverlayRenderableProxyKind::Icon, batch.mesh)) {
      return false;
    }
  }

  for (auto &proxy : proxies) {
    if (!clear_shared_positions(proxy)) return false;
  }
  index = 0;
  for (const OverlayFaceFillBatch &batch : plan.face_fill_batches) {
    if (!batch.mesh.empty()) {
      update_triangle_proxy(proxies[index++], batch, layer_mask);
    }
  }
  for (const OverlayLineBatch &batch : plan.line_batches) {
    if (!batch.mesh.empty()) {
      update_line_proxy(proxies[index++], batch, layer_mask);
    }
  }
  for (const OverlayEditWireBatch &batch : plan.edit_wire_batches) {
    if (!batch.mesh.empty()) {
      update_edit_wire_proxy(proxies[index++], batch, layer_mask);
    }
  }
  for (const OverlayPointBatch &batch : plan.point_batches) {
    if (!batch.mesh.empty()) {
      update_point_proxy(proxies[index++], batch, layer_mask);
    }
  }
  for (const OverlayIconBatch &batch : plan.icon_batches) {
    if (!batch.mesh.empty() && texture_for_icon(batch.icon) != nullptr) {
      update_icon_proxy(proxies[index++], batch, layer_mask);
    }
  }
  for (const auto &proxy : proxies) {
    ++upload_stats_.static_upload_count;
    upload_stats_.position_upload_count +=
        proxy.secondary_position_stream4.empty() ? 1u : 2u;
    if (proxy.kind == OverlayRenderableProxyKind::Triangle ||
        proxy.kind == OverlayRenderableProxyKind::Icon) {
      ++upload_stats_.index_upload_count;
    }
  }
  return true;
}

bool OverlayProxyFactory::update_position_streams(
    OverlayRenderableProxy &proxy) {
  if (proxy.shared_positions_bound) return clear_shared_positions(proxy);
  if (engine_ == nullptr || !upload_proxy_position_streams(*engine_, proxy)) {
    return false;
  }
  upload_stats_.position_upload_count +=
      proxy.secondary_position_stream4.empty() ? 1u : 2u;
  if (proxy.edit_wire_slot_capacity != 0u) {
    ++proxy.edit_wire_update_revision;
  }
  return true;
}

OverlayRenderableProxy OverlayProxyFactory::create_retained_face_proxy(
    std::span<const ViewportOverlayTriangle> source_triangles,
    std::span<const std::uint32_t> triangle_colors,
    std::span<const std::uint32_t> indices,
    std::span<const OverlayRetainedFaceDraw> draws,
    std::uint8_t layer_mask) {
  QDR_PROFILE_SCOPE("Overlay create retained face arena");
  OverlayRenderableProxy proxy;
  if (!initialized() || source_triangles.empty() || indices.empty() || draws.empty() ||
      triangle_colors.size() != source_triangles.size() ||
      source_triangles.size() > std::numeric_limits<std::uint32_t>::max() / 3u ||
      indices.size() > source_triangles.size() * 3u) return proxy;
  proxy.vertex_count = static_cast<std::uint32_t>(source_triangles.size() * 3u);
  for (const auto index : indices) if (index >= proxy.vertex_count) return {};
  for (const auto &draw : draws) {
    if (static_cast<std::size_t>(draw.first_index) + draw.index_count > indices.size()) return {};
  }
  proxy.kind = OverlayRenderableProxyKind::Triangle;
  proxy.index_count = static_cast<std::uint32_t>(indices.size());
  proxy.position_stream3.reserve(proxy.vertex_count);
  proxy.position_stream_bindings.reserve(proxy.vertex_count);
  proxy.face_fill_colors.reserve(proxy.vertex_count);
  proxy.face_fill_triangle_colors.assign(triangle_colors.begin(), triangle_colors.end());
  for (std::size_t triangle_index = 0u; triangle_index < source_triangles.size(); ++triangle_index) {
    const auto &triangle = source_triangles[triangle_index];
    const std::array positions{triangle.a, triangle.b, triangle.c};
    const std::array vertex_ids{triangle.element.vertex_id, triangle.element.edge_a,
                                triangle.element.edge_b};
    const auto color = rgba8_srgb_to_linear(triangle_colors[triangle_index]);
    for (std::size_t corner = 0u; corner < positions.size(); ++corner) {
      const auto position = positions[corner];
      const auto vertex = static_cast<std::uint32_t>(proxy.position_stream3.size());
      proxy.position_stream3.push_back({position.x, position.y, position.z});
      proxy.face_fill_colors.push_back(color);
      proxy.position_stream_bindings.push_back({
          .object_id = triangle.element.object_id, .vertex_id = vertex_ids[corner],
          .first_vertex = vertex, .vertex_count = 1u,
          .stream = OverlayPositionStream::Primary,
          .base_position = {position.x, position.y, position.z, 1.0f}});
    }
  }
  const auto entity = utils::EntityManager::get().create();
  proxy.entity_id = to_overlay_entity_id(entity);
  OverlayProxyCreationGuard guard(*engine_, *scene_, proxy);
  proxy.vertex_buffer = filament::VertexBuffer::Builder()
      .vertexCount(proxy.vertex_count).bufferCount(2u)
      .attribute(filament::VertexAttribute::POSITION, 1u, AttributeType::FLOAT3,
                 0u, sizeof(OverlayFloat3))
      .attribute(filament::VertexAttribute::COLOR, 0u, AttributeType::UBYTE4,
                 0u, sizeof(std::uint32_t))
      .normalized(filament::VertexAttribute::COLOR).build(*engine_);
  proxy.index_buffer = filament::IndexBuffer::Builder().indexCount(proxy.vertex_count)
      .bufferType(filament::IndexBuffer::IndexType::UINT).build(*engine_);
  if (proxy.vertex_buffer == nullptr || proxy.index_buffer == nullptr) return {};
  proxy.vertex_buffer->setBufferAt(*engine_, 0u,
      FilamentBufferDescriptorFactory::vector(proxy.face_fill_colors));
  proxy.vertex_buffer->setBufferAt(*engine_, 1u,
      FilamentBufferDescriptorFactory::vector(proxy.position_stream3));
  proxy.index_buffer->setBuffer(*engine_, FilamentBufferDescriptorFactory::span(indices));
  proxy.face_fill_materials.reserve(draws.size() - 1u);
  filament::RenderableManager::Builder builder(draws.size());
  for (std::size_t pass = 0u; pass < draws.size(); ++pass) {
    auto *material = overlay_material_->createInstance();
    if (material == nullptr) return {};
    if (pass == 0u) proxy.material_instance = material;
    else proxy.face_fill_materials.push_back(material);
    configure_overlay_material(*material, draws[pass].state);
    material->setParameter("useUniformColor", false);
    builder.geometry(pass, filament::RenderableManager::PrimitiveType::TRIANGLES,
        proxy.vertex_buffer, proxy.index_buffer, draws[pass].first_index, draws[pass].index_count)
        .material(pass, material).blendOrder(pass, draws[pass].state.blend_order)
        .globalBlendOrderEnabled(pass, true);
  }
  builder.layerMask(kViewportAllLayerMask, layer_mask).boundingBox(overlay_mesh_bounds())
      .culling(false).castShadows(false).receiveShadows(false).build(*engine_, entity);
  scene_->addEntity(entity);
  guard.mark_scene_added();
  guard.release();
  ++upload_stats_.geometry_creation_count;
  ++upload_stats_.position_upload_count;
  ++upload_stats_.appearance_upload_count;
  ++upload_stats_.index_upload_count;
  return proxy;
}

bool OverlayProxyFactory::update_retained_face_proxy(
    OverlayRenderableProxy &proxy,
    std::span<const std::uint32_t> triangle_colors,
    std::span<const std::uint32_t> indices,
    std::span<const OverlayRetainedFaceDraw> draws,
    std::uint8_t layer_mask, bool indices_changed) {
  QDR_PROFILE_SCOPE("Overlay update retained face membership");
  if (!initialized() || proxy.face_fill_colors.empty() ||
      proxy.vertex_buffer == nullptr || proxy.index_buffer == nullptr ||
      triangle_colors.size() * 3u != proxy.vertex_count ||
      triangle_colors.size() != proxy.face_fill_triangle_colors.size() ||
      draws.size() != proxy.face_fill_materials.size() + 1u ||
      indices.size() > proxy.vertex_count) return false;
  auto &renderables = engine_->getRenderableManager();
  const auto instance = renderables.getInstance(to_filament_entity(proxy.entity_id));
  if (!instance || renderables.getPrimitiveCount(instance) != draws.size()) return false;
  for (const auto index : indices) if (index >= proxy.vertex_count) return false;
  for (const auto &draw : draws) {
    if (static_cast<std::size_t>(draw.first_index) + draw.index_count > indices.size()) return false;
  }
  if (indices_changed && !clear_shared_positions(proxy)) return false;
  std::size_t first_changed = proxy.face_fill_colors.size();
  std::size_t end_changed = 0u;
  for (std::size_t triangle = 0u; triangle < triangle_colors.size(); ++triangle) {
    if (proxy.face_fill_triangle_colors[triangle] == triangle_colors[triangle]) continue;
    proxy.face_fill_triangle_colors[triangle] = triangle_colors[triangle];
    const auto color = rgba8_srgb_to_linear(triangle_colors[triangle]);
    const auto first = triangle * 3u;
    if (proxy.face_fill_colors[first] == color) continue;
    std::fill_n(proxy.face_fill_colors.begin() + first, 3u, color);
    first_changed = std::min(first_changed, first);
    end_changed = first + 3u;
  }
  if (first_changed < end_changed) {
    proxy.vertex_buffer->setBufferAt(*engine_, 0u,
        FilamentBufferDescriptorFactory::span(std::span<const std::uint32_t>(proxy.face_fill_colors)
            .subspan(first_changed, end_changed - first_changed)),
        static_cast<std::uint32_t>(first_changed * sizeof(std::uint32_t)));
    ++upload_stats_.appearance_upload_count;
  }
  if (indices_changed && !indices.empty()) {
    proxy.index_buffer->setBuffer(*engine_, FilamentBufferDescriptorFactory::span(indices));
    ++upload_stats_.index_upload_count;
  }
  for (std::size_t pass = 0u; pass < draws.size(); ++pass) {
    auto *material = pass == 0u ? proxy.material_instance : proxy.face_fill_materials[pass - 1u];
    configure_overlay_material(*material, draws[pass].state);
    renderables.setGeometryAt(instance, pass, filament::RenderableManager::PrimitiveType::TRIANGLES,
        proxy.vertex_buffer, proxy.index_buffer, draws[pass].first_index, draws[pass].index_count);
    renderables.setBlendOrderAt(instance, pass, draws[pass].state.blend_order);
  }
  proxy.index_count = static_cast<std::uint32_t>(indices.size());
  renderables.setLayerMask(instance, kViewportAllLayerMask, layer_mask);
  return true;
}

bool OverlayProxyFactory::upload_shared_positions(
    std::span<const OverlayFloat3> positions) {
  QDR_PROFILE_FUNCTION();
  if (!initialized() || positions.empty()) return false;
  // Float attributes represent every index exactly within this bounded square.
  constexpr std::size_t kMaximumTextureExtent = 4096u;
  const auto extent = std::min(kMaximumTextureExtent,
      filament::Texture::getMaxTextureSize(*engine_, filament::Texture::Sampler::SAMPLER_2D));
  if (extent == 0u || positions.size() > extent * extent ||
      !filament::Texture::isTextureFormatSupported(
          *engine_, filament::Texture::InternalFormat::RGBA32F)) return false;
  const auto width = static_cast<std::uint32_t>(extent);
  const auto height = static_cast<std::uint32_t>((positions.size() + extent - 1u) / extent);
  std::vector<OverlayFloat4> pixels(static_cast<std::size_t>(width) * height);
  for (std::size_t index = 0u; index < positions.size(); ++index) {
    const auto position = positions[index];
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z)) return false;
    pixels[index] = {position.x, position.y, position.z, 1.0f};
  }
  if (shared_position_texture_ == nullptr ||
      shared_position_texture_->getHeight() < height) {
    auto *texture = filament::Texture::Builder()
        .width(width).height(height).levels(1u)
        .sampler(filament::Texture::Sampler::SAMPLER_2D)
        .format(filament::Texture::InternalFormat::RGBA32F).build(*engine_);
    if (texture == nullptr) return false;
    for (auto *material : shared_position_materials_) {
      material->setParameter("sharedPositions", texture, shared_position_sampler());
    }
    if (shared_position_texture_ != nullptr) engine_->destroy(shared_position_texture_);
    shared_position_texture_ = texture;
  }
  shared_position_texture_->setImage(*engine_, 0u, 0u, 0u, width, height,
      shared_position_pixels(std::move(pixels)));
  shared_position_count_ = positions.size();
  ++upload_stats_.shared_position_upload_count;
  return true;
}

bool OverlayProxyFactory::bind_shared_positions(
    OverlayRenderableProxy &proxy,
    std::span<const std::uint32_t> primary_indices,
    std::span<const std::uint32_t> secondary_indices) {
  QDR_PROFILE_FUNCTION();
  if (!initialized() || shared_position_texture_ == nullptr ||
      proxy.vertex_buffer == nullptr || proxy.material_instance == nullptr ||
      primary_indices.size() != proxy.vertex_count || primary_indices.empty() ||
      secondary_indices.size() != proxy.secondary_position_stream4.size() ||
      (proxy.position_stream3.size() != primary_indices.size() &&
       proxy.primary_position_stream4.size() != primary_indices.size())) return false;
  for (auto *material : proxy_materials(proxy)) {
    if (material != nullptr &&
        (!material->getMaterial()->hasParameter("sharedPositionsEnabled") ||
         !material->getMaterial()->hasParameter("sharedPositions"))) return false;
  }
  for (const auto indices : {primary_indices, secondary_indices}) {
    for (const auto index : indices) {
      if (index >= shared_position_count_) return false;
    }
  }
  filament::backend::BufferDescriptor primary_upload;
  filament::backend::BufferDescriptor secondary_upload;
  if (!proxy.position_stream3.empty()) {
    std::vector<OverlayFloat3> indices(primary_indices.size());
    for (std::size_t index = 0u; index < indices.size(); ++index) {
      indices[index].x = static_cast<float>(primary_indices[index]);
    }
    primary_upload = FilamentBufferDescriptorFactory::vector(std::move(indices));
  } else {
    std::vector<OverlayFloat4> indices(primary_indices.size());
    for (std::size_t index = 0u; index < indices.size(); ++index) {
      indices[index] = {static_cast<float>(primary_indices[index]), 0.0f, 0.0f, 1.0f};
    }
    primary_upload = FilamentBufferDescriptorFactory::vector(std::move(indices));
  }
  if (!secondary_indices.empty()) {
    std::vector<OverlayFloat4> indices(secondary_indices.size());
    for (std::size_t index = 0u; index < indices.size(); ++index) {
      indices[index] = {static_cast<float>(secondary_indices[index]), 0.0f, 0.0f, 1.0f};
    }
    secondary_upload = FilamentBufferDescriptorFactory::vector(std::move(indices));
  }
  // Register all consumers before publishing, so a later texture growth can
  // rebind every pass without retaining pointers to movable proxy wrappers.
  if (!proxy.shared_positions_bound) {
    shared_position_materials_.reserve(shared_position_materials_.size() +
                                       5u + proxy.face_fill_materials.size());
    for (auto *material : proxy_materials(proxy)) {
      if (material != nullptr) shared_position_materials_.push_back(material);
    }
  }
  proxy.vertex_buffer->setBufferAt(*engine_, 1u, std::move(primary_upload));
  if (!secondary_indices.empty()) {
    proxy.vertex_buffer->setBufferAt(*engine_, 2u, std::move(secondary_upload));
  }
  for (auto *material : proxy_materials(proxy)) {
    if (material != nullptr) {
      material->setParameter("sharedPositions", shared_position_texture_, shared_position_sampler());
      material->setParameter("sharedPositionsEnabled", true);
    }
  }
  proxy.shared_positions_bound = true;
  ++upload_stats_.shared_position_bind_count;
  upload_stats_.shared_position_binding_upload_count += secondary_indices.empty() ? 1u : 2u;
  if (proxy.edit_wire_slot_capacity != 0u) ++proxy.edit_wire_update_revision;
  return true;
}

bool OverlayProxyFactory::clear_shared_positions(OverlayRenderableProxy &proxy) {
  if (!proxy.shared_positions_bound) return true;
  if (engine_ == nullptr || !upload_proxy_position_streams(*engine_, proxy)) return false;
  for (auto *material : proxy_materials(proxy)) {
    if (material != nullptr) {
      material->setParameter("sharedPositionsEnabled", false);
      material->setParameter("sharedPositions", shared_position_fallback_texture_,
          shared_position_sampler());
      std::erase(shared_position_materials_, material);
    }
  }
  proxy.shared_positions_bound = false;
  upload_stats_.position_upload_count += proxy.secondary_position_stream4.empty() ? 1u : 2u;
  if (proxy.edit_wire_slot_capacity != 0u) ++proxy.edit_wire_update_revision;
  return true;
}

std::vector<OverlayRenderableProxy>
OverlayProxyFactory::create_component_point_proxies(
    std::span<const ViewportOverlayPoint *const> points,
    std::span<const std::size_t> frame_indices,
    const ViewportOverlayCamera &camera, std::uint16_t blend_order_bias,
    std::uint8_t layer_mask) {
  QDR_PROFILE_FUNCTION();
  std::vector<OverlayRenderableProxy> proxies;
  if (!initialized() || points.empty() || points.size() != frame_indices.size() ||
      points.size() > std::numeric_limits<std::uint32_t>::max() / 6u ||
      std::any_of(points.begin(), points.end(), [](const auto *point) {
        return point == nullptr ||
            (point->layer != ViewportOverlayLayer::UnselectedVertex &&
             point->layer != ViewportOverlayLayer::SelectedVertex);
      })) {
    return proxies;
  }

  OverlayPointMesh mesh;
  mesh.vertices.reserve(points.size() * 4u);
  mesh.indices.reserve(points.size() * 6u);
  mesh.position_stream_bindings.reserve(points.size());
  for (const auto *point : points) {
    OverlayPointMeshBuilder::append_point_quad(
        mesh, point->position, kOverlayVertexOutlineColor, &point->element);
  }
  OverlayRenderableProxy proxy;
  proxy.kind = OverlayRenderableProxyKind::Point;
  proxy.vertex_count = static_cast<std::uint32_t>(mesh.vertices.size());
  proxy.index_count = static_cast<std::uint32_t>(mesh.indices.size());
  proxy.position_stream_bindings = std::move(mesh.position_stream_bindings);
  proxy.primary_position_stream4.reserve(mesh.vertices.size());
  for (const auto &vertex : mesh.vertices) {
    proxy.primary_position_stream4.push_back(vertex.center);
  }
  proxy.point_visibility_stream.assign(mesh.vertices.size(), 1.0f);
  const auto entity = utils::EntityManager::get().create();
  proxy.entity_id = to_overlay_entity_id(entity);
  OverlayProxyCreationGuard creation_guard(*engine_, *scene_, proxy);
  proxy.material_instance = overlay_point_material_->createInstance();
  for (auto *&material : proxy.component_point_materials) {
    material = overlay_point_material_->createInstance();
  }
  proxy.vertex_buffer = filament::VertexBuffer::Builder()
      .vertexCount(proxy.vertex_count).bufferCount(4u)
      .attribute(filament::VertexAttribute::POSITION, 0u, AttributeType::FLOAT4,
                 offsetof(OverlayPointVertex, position), sizeof(OverlayPointVertex))
      .attribute(filament::VertexAttribute::UV0, 0u, AttributeType::FLOAT2,
                 offsetof(OverlayPointVertex, uv), sizeof(OverlayPointVertex))
      .attribute(filament::VertexAttribute::CUSTOM0, 1u, AttributeType::FLOAT4,
                 0u, sizeof(OverlayFloat4))
      .attribute(filament::VertexAttribute::CUSTOM1, 2u, AttributeType::FLOAT,
                 0u, sizeof(float))
      .attribute(filament::VertexAttribute::COLOR, 3u, AttributeType::UBYTE4,
                 offsetof(OverlayComponentPointAppearance, color),
                 sizeof(OverlayComponentPointAppearance))
      .normalized(filament::VertexAttribute::COLOR)
      .attribute(filament::VertexAttribute::CUSTOM2, 3u, AttributeType::FLOAT4,
                 offsetof(OverlayComponentPointAppearance, control),
                 sizeof(OverlayComponentPointAppearance))
      .attribute(filament::VertexAttribute::CUSTOM3, 0u, AttributeType::UBYTE4,
                 offsetof(OverlayPointVertex, color), sizeof(OverlayPointVertex))
      .normalized(filament::VertexAttribute::CUSTOM3)
      .build(*engine_);
  proxy.vertex_buffer->setBufferAt(
      *engine_, 0u, FilamentBufferDescriptorFactory::vector(mesh.vertices));
  proxy.vertex_buffer->setBufferAt(
      *engine_, 1u, FilamentBufferDescriptorFactory::vector(proxy.primary_position_stream4));
  proxy.vertex_buffer->setBufferAt(
      *engine_, 2u, FilamentBufferDescriptorFactory::vector(proxy.point_visibility_stream));
  proxy.index_buffer = filament::IndexBuffer::Builder()
      .indexCount(proxy.index_count)
      .bufferType(filament::IndexBuffer::IndexType::UINT).build(*engine_);
  proxy.index_buffer->setBuffer(
      *engine_, FilamentBufferDescriptorFactory::vector(mesh.indices));
  if (!update_component_point_appearance(
          proxy, points, frame_indices, camera, blend_order_bias, layer_mask)) {
    return proxies;
  }
  filament::RenderableManager::Builder builder(4u);
  builder.layerMask(kViewportAllLayerMask, layer_mask)
      .boundingBox(overlay_mesh_bounds()).culling(false)
      .castShadows(false).receiveShadows(false);
  for (std::size_t pass = 0u; pass < 4u; ++pass) {
    const auto layer = pass < 2u ? ViewportOverlayLayer::UnselectedVertex
                                : ViewportOverlayLayer::SelectedVertex;
    auto *material = pass == 0u ? proxy.material_instance
                               : proxy.component_point_materials[pass - 1u];
    const auto order = static_cast<std::uint16_t>(
        viewport_overlay_draw_order(layer) * 64u + blend_order_bias + pass % 2u);
    builder.geometry(pass, filament::RenderableManager::PrimitiveType::TRIANGLES,
                     proxy.vertex_buffer, proxy.index_buffer)
        .material(pass, material).blendOrder(pass, order)
        .globalBlendOrderEnabled(pass, true);
  }
  builder.build(*engine_, entity);
  publish_overlay_proxy(*scene_, proxy, proxies, creation_guard);
  ++upload_stats_.geometry_creation_count;
  ++upload_stats_.static_upload_count;
  ++upload_stats_.position_upload_count;
  ++upload_stats_.index_upload_count;
  ++upload_stats_.visibility_upload_count;
  return proxies;
}

bool OverlayProxyFactory::update_component_point_appearance(
    OverlayRenderableProxy &proxy,
    std::span<const ViewportOverlayPoint *const> points,
    std::span<const std::size_t> frame_indices,
    const ViewportOverlayCamera &camera, std::uint16_t blend_order_bias,
    std::uint8_t layer_mask) {
  QDR_PROFILE_FUNCTION();
  if (!initialized() || proxy.vertex_buffer == nullptr ||
      proxy.component_point_materials.back() == nullptr ||
      points.size() != frame_indices.size() ||
      points.size() * 4u != proxy.vertex_count) {
    return false;
  }
  bool changed = proxy.component_point_appearance.size() != proxy.vertex_count;
  proxy.component_point_appearance.resize(proxy.vertex_count);
  for (std::size_t index = 0u; index < points.size(); ++index) {
    const auto &point = *points[index];
    const OverlayComponentPointAppearance appearance{
        .color = rgba8_srgb_to_linear(point.color),
        .control = {
            viewport_overlay_point_size_pixels(point.layer, point.size_pixels),
            point.outline_growth_pixels.value_or(
                viewport_overlay_point_outline_growth_pixels(point.layer)),
            point.layer == ViewportOverlayLayer::SelectedVertex ? 1.0f : 0.0f,
            0.0f},
    };
    const auto &previous = proxy.component_point_appearance[index * 4u];
    if (changed || previous.color != appearance.color ||
        previous.control.x != appearance.control.x ||
        previous.control.y != appearance.control.y ||
        previous.control.z != appearance.control.z) {
      std::fill_n(proxy.component_point_appearance.begin() + index * 4u, 4u, appearance);
      changed = true;
    }
  }
  if (changed) {
    proxy.vertex_buffer->setBufferAt(
        *engine_, 3u, FilamentBufferDescriptorFactory::vector(proxy.component_point_appearance));
    ++upload_stats_.appearance_upload_count;
  }
  proxy.visibility_point_indices.assign(frame_indices.begin(), frame_indices.end());
  auto &renderables = engine_->getRenderableManager();
  const auto instance = renderables.getInstance(to_filament_entity(proxy.entity_id));
  for (std::size_t pass = 0u; pass < 4u; ++pass) {
    const auto layer = pass < 2u ? ViewportOverlayLayer::UnselectedVertex
                                : ViewportOverlayLayer::SelectedVertex;
    const auto order = static_cast<std::uint16_t>(
        viewport_overlay_draw_order(layer) * 64u + blend_order_bias + pass % 2u);
    auto *material = pass == 0u ? proxy.material_instance
                               : proxy.component_point_materials[pass - 1u];
    configure_overlay_material(*material, OverlayRenderableBuilder::make_state(
        layer, ViewportOverlayGeometryKind::Points, order));
    material->setParameter("componentPass", static_cast<float>(pass));
    material->setParameter("innerRadius", 0.0f);
    material->setParameter("pointSize", 1.0f);
    material->setParameter("nearPlane", camera.near_plane);
    material->setParameter("depthBias", viewport_overlay_depth_bias(
        layer, ViewportOverlayGeometryKind::Points));
    if (instance) {
      renderables.setBlendOrderAt(instance, pass, order);
    }
  }
  update_proxy_layer_mask(*engine_, proxy, layer_mask);
  return true;
}

bool OverlayProxyFactory::update_point_visibility(
    OverlayRenderableProxy &proxy,
    std::span<const std::uint8_t> frame_visibility) {
  QDR_PROFILE_FUNCTION();
  if (engine_ == nullptr || proxy.vertex_buffer == nullptr ||
      proxy.kind != OverlayRenderableProxyKind::Point ||
      proxy.visibility_point_indices.empty() ||
      proxy.point_visibility_stream.size() != proxy.vertex_count) {
    return false;
  }

  constexpr std::size_t kVerticesPerPoint = 4u;
  bool changed = false;
  const std::size_t point_count =
      proxy.point_visibility_stream.size() / kVerticesPerPoint;
  for (std::size_t point_index = 0u; point_index < point_count; ++point_index) {
    float visibility = 1.0f;
    if (point_index < proxy.visibility_point_indices.size()) {
      const std::size_t frame_index =
          proxy.visibility_point_indices[point_index];
      if (frame_index < frame_visibility.size()) {
        visibility = frame_visibility[frame_index] != 0u ? 1.0f : 0.0f;
      }
    }
    const std::size_t vertex_offset = point_index * kVerticesPerPoint;
    if (proxy.point_visibility_stream[vertex_offset] == visibility) {
      continue;
    }
    std::fill_n(proxy.point_visibility_stream.begin() + vertex_offset,
                kVerticesPerPoint, visibility);
    changed = true;
  }
  if (changed) {
    proxy.vertex_buffer->setBufferAt(
        *engine_, 2,
        FilamentBufferDescriptorFactory::vector(proxy.point_visibility_stream));
    ++upload_stats_.visibility_upload_count;
  }
  return changed;
}

void OverlayProxyFactory::update_camera_state(
    const std::vector<OverlayRenderableProxy> &proxies,
    const ViewportOverlayCamera &camera) {
  QDR_PROFILE_FUNCTION();
  if (!initialized()) {
    return;
  }
  for (const OverlayRenderableProxy &proxy : proxies) {
    filament::MaterialInstance *material = proxy.material_instance;
    if (material == nullptr) {
      continue;
    }
    switch (proxy.kind) {
    case OverlayRenderableProxyKind::Line:
      if (proxy.occluded_material_instance != nullptr) {
        proxy.occluded_material_instance->setParameter("nearPlane",
                                                        camera.near_plane);
      }
      [[fallthrough]];
    case OverlayRenderableProxyKind::Point:
      material->setParameter("nearPlane", camera.near_plane);
      for (auto *component_material : proxy.component_point_materials) {
        if (component_material != nullptr) {
          component_material->setParameter("nearPlane", camera.near_plane);
        }
      }
      break;
    case OverlayRenderableProxyKind::EditWire:
      material->setParameter("nearPlane", camera.near_plane);
      material->setParameter("farPlane", camera.far_plane);
      material->setParameter(
          "orthographicProjection",
          camera.projection == ViewportOverlayProjection::Orthographic ? 1.0f
                                                                      : 0.0f);
      break;
    case OverlayRenderableProxyKind::Unknown:
    case OverlayRenderableProxyKind::Triangle:
    case OverlayRenderableProxyKind::Icon:
      break;
    }
  }
}

void OverlayProxyFactory::set_layer_mask(
    const std::vector<OverlayRenderableProxy> &proxies,
    std::uint8_t layer_mask) {
  if (!engine_) {
    return;
  }
  for (const OverlayRenderableProxy &proxy : proxies) {
    update_proxy_layer_mask(*engine_, proxy, layer_mask);
  }
}

void OverlayProxyFactory::destroy(OverlayRenderableProxy& proxy) {
    if (!engine_) {
        return;
    }

    if (proxy.shared_positions_bound) {
      for (auto *material : proxy_materials(proxy)) {
        if (material != nullptr) std::erase(shared_position_materials_, material);
      }
      proxy.shared_positions_bound = false;
    }
    destroy_overlay_proxy_resources(*engine_, scene_, proxy, true);
}

std::size_t
OverlayProxyFactory::planned_proxy_count(const OverlayLayerBuildPlan &plan) {
  std::size_t count = 0;
  for (const OverlayFaceFillBatch &batch : plan.face_fill_batches) {
    count += batch.mesh.empty() ? 0u : 1u;
  }
  for (const OverlayLineBatch &batch : plan.line_batches) {
    count += batch.mesh.empty() ? 0u : 1u;
  }
  for (const OverlayEditWireBatch &batch : plan.edit_wire_batches) {
    count += batch.mesh.empty() ? 0u : 1u;
  }
  for (const OverlayPointBatch &batch : plan.point_batches) {
    count += batch.mesh.empty() ? 0u : 1u;
  }
  for (const OverlayIconBatch &batch : plan.icon_batches) {
    count += batch.mesh.empty() ? 0u : 1u;
  }
  return count;
}

void OverlayProxyFactory::create_triangle_proxy(
    const OverlayFaceFillBatch &batch, std::uint8_t layer_mask,
    std::vector<OverlayRenderableProxy> &proxies) {
  if (batch.mesh.empty()) {
    return;
  }

  append_overlay_proxy(
      *engine_, *scene_, *overlay_material_, batch.mesh,
      OverlayRenderableProxyKind::Triangle, batch.state,
      layer_mask, proxies,
      [](filament::VertexBuffer::Builder &builder) {
        builder
            .attribute(filament::VertexAttribute::POSITION, 1,
                       AttributeType::FLOAT3,
                       0, sizeof(OverlayFloat3))
            .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2,
                       offsetof(OverlayFaceFillVertex, uv),
                       sizeof(OverlayFaceFillVertex))
            .attribute(filament::VertexAttribute::COLOR, 0,
                       AttributeType::UBYTE4,
                       offsetof(OverlayFaceFillVertex, color),
                       sizeof(OverlayFaceFillVertex))
            .normalized(filament::VertexAttribute::COLOR);
      },
      [this, &batch](filament::MaterialInstance &material) {
        configure_overlay_material(material, batch.state);
      });
}

void OverlayProxyFactory::create_line_proxy(
    const OverlayLineBatch &batch, std::uint8_t layer_mask,
    std::vector<OverlayRenderableProxy> &proxies) {
  if (batch.mesh.empty()) {
    return;
  }

  const auto [visible_order, occluded_order] = line_blend_orders(
      batch, engine_->getBackend() == filament::Engine::Backend::VULKAN);
  OverlayRenderState visible_state = batch.state;
  visible_state.blend_order = visible_order;
  std::optional<OverlayRenderState> occluded_state = batch.occluded_state;
  if (occluded_state) {
    occluded_state->blend_order = occluded_order;
  }

  append_overlay_proxy(
      *engine_, *scene_, *overlay_line_material_, batch.mesh,
      OverlayRenderableProxyKind::Line, visible_state,
      layer_mask, proxies,
      [](filament::VertexBuffer::Builder &builder) {
        builder
            .attribute(filament::VertexAttribute::POSITION, 0,
                       AttributeType::FLOAT4,
                       offsetof(OverlayLineVertex, position),
                       sizeof(OverlayLineVertex))
            .attribute(filament::VertexAttribute::CUSTOM0, 1,
                       AttributeType::FLOAT4,
                       0, sizeof(OverlayFloat4))
            .attribute(filament::VertexAttribute::CUSTOM1, 2,
                       AttributeType::FLOAT4,
                       0, sizeof(OverlayFloat4))
            .attribute(filament::VertexAttribute::CUSTOM2, 0,
                       AttributeType::FLOAT4,
                       offsetof(OverlayLineVertex, control),
                       sizeof(OverlayLineVertex))
            .attribute(
                filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4,
                offsetof(OverlayLineVertex, color), sizeof(OverlayLineVertex))
            .normalized(filament::VertexAttribute::COLOR);
      },
      [this, &batch](filament::MaterialInstance &material) {
        configure_overlay_material(material, batch.state);
        material.setParameter("lineWidth", batch.width_pixels);
        material.setParameter("featherPixels", kOverlayLineFeatherPixels);
        material.setParameter("nearPlane", batch.near_plane);
        material.setParameter("depthBias", batch.depth_bias);
        material.setParameter("alphaMultiplier", 1.0f);
      },
      occluded_state ? &*occluded_state : nullptr,
      [this, &batch](filament::MaterialInstance &material) {
        configure_overlay_material(material, *batch.occluded_state);
        material.setParameter("lineWidth", batch.width_pixels);
        material.setParameter("featherPixels", kOverlayLineFeatherPixels);
        material.setParameter("nearPlane", batch.near_plane);
        material.setParameter("depthBias", batch.depth_bias);
        material.setParameter("alphaMultiplier",
                              batch.occluded_alpha_multiplier);
      });
}

void OverlayProxyFactory::create_edit_wire_proxy(
    const OverlayEditWireBatch &batch, std::uint8_t layer_mask,
    std::vector<OverlayRenderableProxy> &proxies) {
  if (batch.mesh.empty()) {
    return;
  }

  append_overlay_proxy(
      *engine_, *scene_, *edit_wire_line_material_, batch.mesh,
      OverlayRenderableProxyKind::EditWire, batch.state,
      layer_mask, proxies,
      [](filament::VertexBuffer::Builder &builder) {
        builder
            .attribute(filament::VertexAttribute::POSITION, 0,
                       AttributeType::FLOAT4,
                       offsetof(OverlayLineVertex, position),
                       sizeof(OverlayLineVertex))
            .attribute(filament::VertexAttribute::CUSTOM0, 1,
                       AttributeType::FLOAT4,
                       0, sizeof(OverlayFloat4))
            .attribute(filament::VertexAttribute::CUSTOM1, 2,
                       AttributeType::FLOAT4,
                       0, sizeof(OverlayFloat4))
            .attribute(filament::VertexAttribute::CUSTOM2, 0,
                       AttributeType::FLOAT4,
                       offsetof(OverlayLineVertex, control),
                       sizeof(OverlayLineVertex))
            .attribute(
                filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4,
                offsetof(OverlayLineVertex, color), sizeof(OverlayLineVertex))
            .normalized(filament::VertexAttribute::COLOR);
      },
      [this, &batch](filament::MaterialInstance &material) {
        configure_edit_wire_material(material, batch);
      });
}

void OverlayProxyFactory::create_point_proxy(
    const OverlayPointBatch &batch, std::uint8_t layer_mask,
    std::vector<OverlayRenderableProxy> &proxies) {
  if (batch.mesh.empty()) {
    return;
  }

  append_overlay_proxy(
      *engine_, *scene_, *overlay_point_material_, batch.mesh,
      OverlayRenderableProxyKind::Point, batch.state,
      layer_mask, proxies,
      [](filament::VertexBuffer::Builder &builder) {
        builder
            .attribute(filament::VertexAttribute::POSITION, 0,
                       AttributeType::FLOAT4,
                       offsetof(OverlayPointVertex, position),
                       sizeof(OverlayPointVertex))
            .attribute(filament::VertexAttribute::CUSTOM0, 1,
                       AttributeType::FLOAT4,
                       0, sizeof(OverlayFloat4))
            .attribute(filament::VertexAttribute::CUSTOM1, 2,
                       AttributeType::FLOAT, 0, sizeof(float))
            .attribute(filament::VertexAttribute::CUSTOM2, 0,
                       AttributeType::FLOAT4, offsetof(OverlayPointVertex, position),
                       sizeof(OverlayPointVertex))
            .attribute(filament::VertexAttribute::CUSTOM3, 0,
                       AttributeType::UBYTE4, offsetof(OverlayPointVertex, color),
                       sizeof(OverlayPointVertex))
            .normalized(filament::VertexAttribute::CUSTOM3)
            .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2,
                       offsetof(OverlayPointVertex, uv),
                       sizeof(OverlayPointVertex))
            .attribute(
                filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4,
                offsetof(OverlayPointVertex, color), sizeof(OverlayPointVertex))
            .normalized(filament::VertexAttribute::COLOR);
      },
      [this, &batch](filament::MaterialInstance &material) {
        configure_overlay_material(material, batch.state);
        material.setParameter("innerRadius", batch.inner_radius);
        material.setParameter("componentPass", -1.0f);
        material.setParameter("pointSize", batch.size_pixels);
        material.setParameter("nearPlane", batch.near_plane);
        material.setParameter("depthBias", batch.depth_bias);
      });
  proxies.back().visibility_point_indices = batch.visibility_point_indices;
}

void OverlayProxyFactory::create_icon_proxy(
    const OverlayIconBatch &batch, std::uint8_t layer_mask,
    std::vector<OverlayRenderableProxy> &proxies) {
  filament::Texture *icon_texture = texture_for_icon(batch.icon);
  if (batch.mesh.empty() || icon_texture == nullptr) {
    return;
  }

  (void)batch.size_pixels;
  (void)batch.depth_bias;
  append_overlay_proxy(
      *engine_, *scene_, *overlay_icon_material_, batch.mesh,
      OverlayRenderableProxyKind::Icon, batch.state,
      layer_mask, proxies,
      [](filament::VertexBuffer::Builder &builder) {
        builder
            .attribute(filament::VertexAttribute::POSITION, 0,
                       AttributeType::FLOAT4,
                       offsetof(OverlayIconVertex, position),
                       sizeof(OverlayIconVertex))
            .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2,
                       offsetof(OverlayIconVertex, uv),
                       sizeof(OverlayIconVertex))
            .attribute(
                filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4,
                offsetof(OverlayIconVertex, color), sizeof(OverlayIconVertex))
            .normalized(filament::VertexAttribute::COLOR);
      },
      [this, &batch, icon_texture](filament::MaterialInstance &material) {
        configure_overlay_material(material, batch.state);
        const filament::TextureSampler sampler(
            filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
            filament::TextureSampler::MagFilter::LINEAR,
            filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);
        material.setParameter("iconTexture", icon_texture, sampler);
      });
}

void OverlayProxyFactory::update_triangle_proxy(
    OverlayRenderableProxy &proxy, const OverlayFaceFillBatch &batch,
    std::uint8_t layer_mask) {
  update_proxy_buffers(*engine_, proxy, batch.mesh, layer_mask);
  configure_overlay_material(*proxy.material_instance, batch.state);
}

void OverlayProxyFactory::update_line_proxy(OverlayRenderableProxy &proxy,
                                             const OverlayLineBatch &batch,
                                             std::uint8_t layer_mask) {
  update_canonical_quad_proxy_vertices(*engine_, proxy, batch.mesh,
                                       layer_mask);
  configure_overlay_material(*proxy.material_instance, batch.state);
  proxy.material_instance->setParameter("lineWidth", batch.width_pixels);
  proxy.material_instance->setParameter("featherPixels",
                                        kOverlayLineFeatherPixels);
  proxy.material_instance->setParameter("nearPlane", batch.near_plane);
  proxy.material_instance->setParameter("depthBias", batch.depth_bias);
  proxy.material_instance->setParameter("alphaMultiplier", 1.0f);
  if (proxy.occluded_material_instance != nullptr && batch.occluded_state) {
    filament::MaterialInstance &material = *proxy.occluded_material_instance;
    configure_overlay_material(material, *batch.occluded_state);
    material.setParameter("lineWidth", batch.width_pixels);
    material.setParameter("featherPixels", kOverlayLineFeatherPixels);
    material.setParameter("nearPlane", batch.near_plane);
    material.setParameter("depthBias", batch.depth_bias);
    material.setParameter("alphaMultiplier", batch.occluded_alpha_multiplier);
  }
  filament::RenderableManager &renderables = engine_->getRenderableManager();
  const filament::RenderableManager::Instance instance =
      renderables.getInstance(to_filament_entity(proxy.entity_id));
  if (instance) {
    const auto [visible_order, occluded_order] = line_blend_orders(
        batch, engine_->getBackend() == filament::Engine::Backend::VULKAN);
    renderables.setBlendOrderAt(instance, 0u, visible_order);
    if (batch.occluded_state) {
      renderables.setBlendOrderAt(instance, 1u, occluded_order);
    }
  }
}

void OverlayProxyFactory::update_edit_wire_proxy(
    OverlayRenderableProxy &proxy, const OverlayEditWireBatch &batch,
    std::uint8_t layer_mask) {
  update_canonical_quad_proxy_vertices(*engine_, proxy, batch.mesh,
                                       layer_mask);
  configure_edit_wire_material(*proxy.material_instance, batch);
}

void OverlayProxyFactory::configure_edit_wire_material(
    filament::MaterialInstance &material, const OverlayEditWireBatch &batch) const {
  configure_overlay_material(material, batch.state);
  material.setParameter("lineWidth", batch.width_pixels);
  material.setParameter("featherPixels", kOverlayLineFeatherPixels);
  material.setParameter("nearPlane", batch.near_plane);
  material.setParameter("farPlane", batch.far_plane);
  material.setParameter("orthographicProjection",
                        batch.orthographic_projection ? 1.0f : 0.0f);
}

void OverlayProxyFactory::update_point_proxy(OverlayRenderableProxy &proxy,
                                             const OverlayPointBatch &batch,
                                             std::uint8_t layer_mask) {
  update_canonical_quad_proxy_vertices(*engine_, proxy, batch.mesh, layer_mask);
  proxy.visibility_point_indices = batch.visibility_point_indices;
  if (proxy.point_visibility_stream.size() != batch.mesh.vertices.size() ||
      std::any_of(proxy.point_visibility_stream.begin(),
                  proxy.point_visibility_stream.end(),
                  [](float visibility) { return visibility != 1.0f; })) {
    proxy.point_visibility_stream.assign(batch.mesh.vertices.size(), 1.0f);
    proxy.vertex_buffer->setBufferAt(
        *engine_, 2,
        FilamentBufferDescriptorFactory::vector(proxy.point_visibility_stream));
    ++upload_stats_.visibility_upload_count;
  }
  configure_overlay_material(*proxy.material_instance, batch.state);
  proxy.material_instance->setParameter("innerRadius", batch.inner_radius);
  proxy.material_instance->setParameter("pointSize", batch.size_pixels);
  proxy.material_instance->setParameter("nearPlane", batch.near_plane);
  proxy.material_instance->setParameter("depthBias", batch.depth_bias);
}

void OverlayProxyFactory::update_icon_proxy(OverlayRenderableProxy &proxy,
                                            const OverlayIconBatch &batch,
                                            std::uint8_t layer_mask) {
  update_proxy_buffers(*engine_, proxy, batch.mesh, layer_mask);
  configure_overlay_material(*proxy.material_instance, batch.state);
  if (filament::Texture *icon_texture = texture_for_icon(batch.icon)) {
    const filament::TextureSampler sampler(
        filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
        filament::TextureSampler::MagFilter::LINEAR,
        filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);
    proxy.material_instance->setParameter("iconTexture", icon_texture,
                                          sampler);
  }
}

void OverlayProxyFactory::configure_overlay_material(
    filament::MaterialInstance &material,
    const OverlayRenderState &state) const {
  material.setColorWrite(state.color_write);
  material.setDepthWrite(state.depth_write);
  material.setDepthCulling(state.depth_culling);
  material.setDepthFunc(to_filament_depth_func(state.depth_func));
  material.setCullingMode(to_filament_culling_mode(state.culling_mode));
  material.setTransparencyMode(
      to_filament_transparency_mode(state.transparency_mode));
  material.setPolygonOffset(state.polygon_offset_scale,
                            state.polygon_offset_constant);
  apply_overlay_face_fill_stencil(
      material, state.face_fill_class, state.face_fill_pass,
      engine_->getBackend() == filament::Engine::Backend::VULKAN);
  if (state.silhouette_pass != OverlaySilhouettePass::None) {
    // The paired depth passes remain available on the fallback backend, but
    // its custom targets cannot provide stencil-based visible precedence.
    apply_overlay_silhouette_stencil(
        material, state.silhouette_pass,
        engine_->getBackend() == filament::Engine::Backend::VULKAN);
  }
}

void OverlayProxyFactory::load_icon_textures(
    const std::filesystem::path &resource_root) {
  if (engine_ == nullptr) {
    return;
  }
  const std::vector<std::byte> point_light_pixels =
      make_point_light_icon_rgba();
  point_light_icon_texture_ = load_rgba_texture(
      *engine_, point_light_pixels, kPointLightIconTextureExtent,
      kPointLightIconTextureExtent,
      PngTextureLoadOptions{
          .color_space = PngTextureColorSpace::Srgb,
          .flip_y = false,
          .generate_mipmaps = true,
          .label = "procedural point light viewport icon",
      });
  if (point_light_icon_texture_ == nullptr) {
    point_light_icon_texture_ = load_png_texture(
        *engine_, resource_root / "icons" / "point_light.png",
        PngTextureLoadOptions{
            .color_space = PngTextureColorSpace::Srgb,
            .flip_y = false,
            .generate_mipmaps = true,
            .label = "point light viewport icon fallback",
        });
  }
}

void OverlayProxyFactory::destroy_icon_textures() {
  if (engine_ == nullptr) {
    point_light_icon_texture_ = nullptr;
    return;
  }
  if (point_light_icon_texture_ != nullptr) {
    engine_->destroy(point_light_icon_texture_);
    point_light_icon_texture_ = nullptr;
  }
}

std::size_t OverlayProxyFactory::planned_loaded_proxy_count(
    const OverlayLayerBuildPlan &plan) const {
  std::size_t count = planned_proxy_count(plan);
  for (const OverlayIconBatch &batch : plan.icon_batches) {
    if (!batch.mesh.empty() && texture_for_icon(batch.icon) == nullptr) {
      --count;
    }
  }
  return count;
}

filament::Texture *
OverlayProxyFactory::texture_for_icon(ViewportOverlayIconKind icon) const {
  switch (icon) {
  case ViewportOverlayIconKind::PointLight:
    return point_light_icon_texture_;
  }
  return nullptr;
}

} // namespace quader
