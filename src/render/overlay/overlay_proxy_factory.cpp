////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_proxy_factory.hpp"

#include "diagnostics/profile.hpp"
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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

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

filament::MaterialInstance::DepthFunc
to_filament_depth_func(OverlayDepthFunc depth_func) {
  switch (depth_func) {
  case OverlayDepthFunc::Always:
    return filament::MaterialInstance::DepthFunc::A;
  case OverlayDepthFunc::Equal:
    return filament::MaterialInstance::DepthFunc::E;
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
}

template <typename Vertex, typename ConfigureAttributes,
          typename ConfigureMaterial>
void append_overlay_proxy(filament::Engine &engine, filament::Scene &scene,
                          filament::Material &material,
                          const OverlayMesh<Vertex> &mesh,
                          OverlayRenderableProxyKind kind,
                          const OverlayRenderState &state,
                          std::uint8_t layer_mask,
                          std::vector<OverlayRenderableProxy> &proxies,
                          ConfigureAttributes configure_attributes,
                          ConfigureMaterial configure_material) {
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
    for (const Vertex &vertex : mesh.vertices) {
      proxy.primary_position_stream4.push_back(vertex.center);
    }
  }
  const utils::Entity entity = utils::EntityManager::get().create();
  proxy.entity_id = to_overlay_entity_id(entity);
  OverlayProxyCreationGuard creation_guard(engine, scene, proxy);
  proxy.material_instance = material.createInstance();
  configure_material(*proxy.material_instance);

  filament::VertexBuffer::Builder vertex_builder;
  const std::uint8_t buffer_count =
      !proxy.position_stream3.empty() ||
              !proxy.primary_position_stream4.empty()
          ? static_cast<std::uint8_t>(
                proxy.secondary_position_stream4.empty() ? 2u : 3u)
          : 1u;
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
  if (!proxy.secondary_position_stream4.empty()) {
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

  filament::RenderableManager::Builder(1)
      .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES,
                proxy.vertex_buffer, proxy.index_buffer)
      .material(0, proxy.material_instance)
      .blendOrder(0, state.blend_order)
      .globalBlendOrderEnabled(0, true)
      .layerMask(kViewportAllLayerMask, layer_mask)
      .boundingBox(overlay_mesh_bounds())
      .culling(false)
      .castShadows(false)
      .receiveShadows(false)
      .build(engine, entity);
  scene.addEntity(entity);
  creation_guard.mark_scene_added();
  proxies.push_back(std::move(proxy));
  creation_guard.release();
}

template <typename Vertex>
bool proxy_can_update_mesh(const OverlayRenderableProxy &proxy,
                           OverlayRenderableProxyKind kind,
                           const OverlayMesh<Vertex> &mesh) {
  return !mesh.empty() && proxy.kind == kind &&
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
  load_icon_textures(resource_root);
}

void OverlayProxyFactory::shutdown() {
  destroy_icon_textures();
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
  return true;
}

bool OverlayProxyFactory::update_position_streams(
    OverlayRenderableProxy &proxy) {
  return engine_ != nullptr &&
         upload_proxy_position_streams(*engine_, proxy);
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
    case OverlayRenderableProxyKind::Point:
      material->setParameter("nearPlane", camera.near_plane);
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

  append_overlay_proxy(
      *engine_, *scene_, *overlay_line_material_, batch.mesh,
      OverlayRenderableProxyKind::Line, batch.state,
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
        configure_overlay_material(material, batch.state);
        material.setParameter("lineWidth", batch.width_pixels);
        material.setParameter("featherPixels", kOverlayLineFeatherPixels);
        material.setParameter("nearPlane", batch.near_plane);
        material.setParameter("farPlane", batch.far_plane);
        material.setParameter("orthographicProjection",
                              batch.orthographic_projection ? 1.0f : 0.0f);
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
        material.setParameter("pointSize", batch.size_pixels);
        material.setParameter("nearPlane", batch.near_plane);
        material.setParameter("depthBias", batch.depth_bias);
      });
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
}

void OverlayProxyFactory::update_edit_wire_proxy(
    OverlayRenderableProxy &proxy, const OverlayEditWireBatch &batch,
    std::uint8_t layer_mask) {
  update_canonical_quad_proxy_vertices(*engine_, proxy, batch.mesh,
                                       layer_mask);
  configure_overlay_material(*proxy.material_instance, batch.state);
  proxy.material_instance->setParameter("lineWidth", batch.width_pixels);
  proxy.material_instance->setParameter("featherPixels",
                                        kOverlayLineFeatherPixels);
  proxy.material_instance->setParameter("nearPlane", batch.near_plane);
  proxy.material_instance->setParameter("farPlane", batch.far_plane);
  proxy.material_instance->setParameter(
      "orthographicProjection",
      batch.orthographic_projection ? 1.0f : 0.0f);
}

void OverlayProxyFactory::update_point_proxy(OverlayRenderableProxy &proxy,
                                             const OverlayPointBatch &batch,
                                             std::uint8_t layer_mask) {
  update_canonical_quad_proxy_vertices(*engine_, proxy, batch.mesh, layer_mask);
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
