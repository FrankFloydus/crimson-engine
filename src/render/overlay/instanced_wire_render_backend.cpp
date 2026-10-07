////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/instanced_wire_render_backend.hpp"

#include "core/color_space.hpp"
#include "math/glm_bridge.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/material_binding_service.hpp"
#include "render/overlay/overlay_mesh_types.hpp"
#include "render/render_layers.hpp"

#include <backend/DriverEnums.h>
#include <filament/Box.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/InstanceBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

void submit_wire_retirement(filament::Engine &engine,
                            std::size_t &pending_retirements) {
  constexpr std::size_t kRetirementBatchSize = 64u;
  if (++pending_retirements >= kRetirementBatchSize) {
    engine.flush();
    pending_retirements = 0u;
  }
}

struct InstancedWireVertex final {
  OverlayFloat4 position;
  OverlayFloat4 start;
  OverlayFloat4 end;
  OverlayFloat4 control;
  OverlayFloat4 surface_support0;
  OverlayFloat4 surface_support1;
  std::uint32_t color = 0u;
};

filament::math::mat4f make_transform(const QMat4 &transform) {
  const glm::mat4 glm_transform = to_glm(transform);
  filament::math::mat4f result;
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      result[column][row] = glm_transform[column][row];
    }
  }
  return result;
}

filament::Box filament_bounds(const RenderInstanceBounds &bounds) {
  if (!bounds.present) {
    return {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
  }
  filament::Box result;
  result.set({bounds.min.x, bounds.min.y, bounds.min.z},
             {bounds.max.x, bounds.max.y, bounds.max.z});
  return result;
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

void configure_overlay_material(filament::MaterialInstance &material,
                                const OverlayRenderState &state) {
  material.setColorWrite(state.color_write);
  material.setDepthWrite(state.depth_write);
  material.setDepthCulling(state.depth_culling);
  material.setDepthFunc(to_filament_depth_func(state.depth_func));
  material.setCullingMode(to_filament_culling_mode(state.culling_mode));
  material.setTransparencyMode(
      to_filament_transparency_mode(state.transparency_mode));
  material.setPolygonOffset(state.polygon_offset_scale,
                            state.polygon_offset_constant);
}

void append_line_quad(std::vector<InstancedWireVertex> &vertices,
                      std::vector<std::uint32_t> &indices,
                      const MeshWireSegment &segment, std::uint32_t color_rgba) {
  const std::uint32_t color = rgba8_srgb_to_linear(color_rgba);
  const std::uint32_t offset = static_cast<std::uint32_t>(vertices.size());
  const OverlayFloat4 origin{0.0f, 0.0f, 0.0f, 1.0f};
  const OverlayFloat4 a{segment.start.x, segment.start.y, segment.start.z, 1.0f};
  const OverlayFloat4 b{segment.end.x, segment.end.y, segment.end.z, 1.0f};
  const QVec3 support0 = segment.surface_support_count > 0u
      ? segment.surface_support[0] : segment.start;
  const QVec3 support1 = segment.surface_support_count > 1u
      ? segment.surface_support[1] : segment.start;
  const OverlayFloat4 first_support{support0.x, support0.y, support0.z, 1.0f};
  const OverlayFloat4 second_support{support1.x, support1.y, support1.z, 1.0f};
  const float support_count = static_cast<float>(segment.surface_support_count);
  vertices.push_back({origin, a, b, {-1.0f, -1.0f, 0.0f, support_count}, first_support, second_support, color});
  vertices.push_back({origin, a, b, {-1.0f, 1.0f, 1.0f, support_count}, first_support, second_support, color});
  vertices.push_back({origin, a, b, {1.0f, 1.0f, 1.0f, support_count}, first_support, second_support, color});
  vertices.push_back({origin, a, b, {1.0f, -1.0f, 0.0f, support_count}, first_support, second_support, color});
  indices.insert(indices.end(), {offset, offset + 1u, offset + 2u, offset,
                                 offset + 2u, offset + 3u});
}

} // namespace

struct InstancedWireRenderBackend::GeometryResource final {
  std::uint64_t signature = 0u;
  std::shared_ptr<const MeshWireGeometryPayload> wire_geometry;
  std::uint32_t color = 0u;
  filament::VertexBuffer *vertex_buffer = nullptr;
  filament::IndexBuffer *index_buffer = nullptr;
  std::uint32_t index_count = 0u;
  std::uint64_t vertex_bytes = 0u;
  std::uint64_t index_bytes = 0u;
};

InstancedWireRenderBackend::InstancedWireRenderBackend() = default;

InstancedWireRenderBackend::BatchProxy::BatchProxy(
    BatchProxy &&other) noexcept
    : entity_(other.entity_),
      instance_buffer_(std::exchange(other.instance_buffer_, nullptr)),
      material_instance_(std::exchange(other.material_instance_, nullptr)),
      geometry_(std::exchange(other.geometry_, nullptr)),
      line_width_pixels_(std::exchange(other.line_width_pixels_, 1.0f)),
      dimmed_(other.dimmed_) {
  other.entity_.clear();
}

InstancedWireRenderBackend::~InstancedWireRenderBackend() { shutdown(); }

void InstancedWireRenderBackend::initialize(
    filament::Engine &engine, filament::Scene &scene,
    filament::Material &line_material,
    std::size_t max_instances_per_chunk) {
  shutdown();
  engine_ = &engine;
  scene_ = &scene;
  line_material_ = &line_material;
  max_instances_per_chunk_ =
      std::max<std::size_t>(1u, max_instances_per_chunk);
}

void InstancedWireRenderBackend::shutdown() {
  destroy_geometry_resources();
  engine_ = nullptr;
  scene_ = nullptr;
  line_material_ = nullptr;
  max_instances_per_chunk_ = 1u;
  stats_ = {};
}

bool InstancedWireRenderBackend::initialized() const {
  return engine_ != nullptr && scene_ != nullptr && line_material_ != nullptr;
}

InstancedWireRenderBackend::GeometryResource *
InstancedWireRenderBackend::acquire_geometry(
    const InstancedWireGeometryInput &input) {
  auto &bucket = geometry_resources_[input.signature];
  for (const std::unique_ptr<GeometryResource> &resource : bucket) {
    if (resource->wire_geometry != nullptr && input.wire_geometry != nullptr &&
        resource->wire_geometry->content_id == input.wire_geometry->content_id &&
        resource->color == input.color) {
      ++stats_.geometry_reuse_count;
      return resource.get();
    }
  }

  std::vector<InstancedWireVertex> vertices;
  std::vector<std::uint32_t> indices;
  vertices.reserve(input.segments.size() * 4u);
  indices.reserve(input.segments.size() * 6u);
  for (const MeshWireSegment &segment : input.segments) {
    append_line_quad(vertices, indices, segment, input.color);
  }
  if (vertices.empty() || indices.empty()) {
    return nullptr;
  }

  auto resource = std::make_unique<GeometryResource>();
  resource->signature = input.signature;
  resource->wire_geometry = input.wire_geometry;
  resource->color = input.color;
  resource->index_count = static_cast<std::uint32_t>(indices.size());
  resource->vertex_bytes = vertices.size() * sizeof(InstancedWireVertex);
  resource->index_bytes = indices.size() * sizeof(std::uint32_t);
  resource->vertex_buffer =
      filament::VertexBuffer::Builder()
          .vertexCount(static_cast<std::uint32_t>(vertices.size()))
          .bufferCount(1u)
          .attribute(filament::VertexAttribute::POSITION, 0u,
                     AttributeType::FLOAT4,
                     offsetof(InstancedWireVertex, position),
                     sizeof(InstancedWireVertex))
          .attribute(filament::VertexAttribute::CUSTOM0, 0u,
                     AttributeType::FLOAT4,
                     offsetof(InstancedWireVertex, start),
                     sizeof(InstancedWireVertex))
          .attribute(filament::VertexAttribute::CUSTOM1, 0u,
                     AttributeType::FLOAT4,
                     offsetof(InstancedWireVertex, end),
                     sizeof(InstancedWireVertex))
          .attribute(filament::VertexAttribute::CUSTOM2, 0u,
                     AttributeType::FLOAT4,
                     offsetof(InstancedWireVertex, control),
                     sizeof(InstancedWireVertex))
          .attribute(filament::VertexAttribute::CUSTOM3, 0u,
                     AttributeType::FLOAT4,
                     offsetof(InstancedWireVertex, surface_support0),
                     sizeof(InstancedWireVertex))
          .attribute(filament::VertexAttribute::CUSTOM4, 0u,
                     AttributeType::FLOAT4,
                     offsetof(InstancedWireVertex, surface_support1),
                     sizeof(InstancedWireVertex))
          .attribute(filament::VertexAttribute::COLOR, 0u,
                     AttributeType::UBYTE4,
                     offsetof(InstancedWireVertex, color),
                     sizeof(InstancedWireVertex))
          .normalized(filament::VertexAttribute::COLOR)
          .build(*engine_);
  resource->vertex_buffer->setBufferAt(
      *engine_, 0u, FilamentBufferDescriptorFactory::vector(vertices));
  resource->index_buffer =
      filament::IndexBuffer::Builder()
          .indexCount(resource->index_count)
          .bufferType(filament::IndexBuffer::IndexType::UINT)
          .build(*engine_);
  resource->index_buffer->setBuffer(
      *engine_, FilamentBufferDescriptorFactory::vector(indices));

  GeometryResource *result = resource.get();
  const std::uint64_t vertex_bytes = resource->vertex_bytes;
  const std::uint64_t index_bytes = resource->index_bytes;
  bucket.push_back(std::move(resource));
  stats_.derived_vertex_bytes += vertex_bytes;
  stats_.derived_index_bytes += index_bytes;
  ++stats_.geometry_upload_count;
  ++stats_.unique_geometry_resource_count;
  return result;
}

InstancedWireRenderBackend::BatchProxy
InstancedWireRenderBackend::create_batch(
    const InstancedWireGeometryInput &geometry,
    const RenderInstanceBatchChunk &chunk,
    const InstancedWireBatchStyle &style) {
  BatchProxy proxy;
  proxy.geometry_ = acquire_geometry(geometry);
  if (proxy.geometry_ == nullptr || chunk.world_transforms.empty() ||
      chunk.world_transforms.size() > max_instances_per_chunk_) {
    return proxy;
  }

  proxy.entity_ = utils::EntityManager::get().create();
  proxy.material_instance_ = line_material_->createInstance();
  if (proxy.material_instance_ == nullptr) {
    return proxy;
  }
  configure_overlay_material(*proxy.material_instance_, style.render_state);
  proxy.material_instance_->setParameter("surfaceDepthEnabled", style.surface_depth_enabled);
  proxy.dimmed_ = style.instance_edit_dim != 0.0f;
  proxy.material_instance_->setParameter("instanceEditDim",
                                         style.instance_edit_dim);
  bind_instance_edit_context_style(*proxy.material_instance_,
                                   style.instance_edit_style);
  proxy.line_width_pixels_ = style.line_width_pixels;
  proxy.material_instance_->setParameter("lineWidth",
                                         proxy.line_width_pixels_);
  proxy.material_instance_->setParameter("featherPixels",
                                         kOverlayLineFeatherPixels);
  proxy.material_instance_->setParameter("nearPlane", style.near_plane);
  update_line_tint(std::span<BatchProxy>(&proxy, 1u), style.line_tint);

  std::vector<filament::math::mat4f> transforms;
  transforms.reserve(chunk.world_transforms.size());
  for (const QMat4 &transform : chunk.world_transforms) {
    transforms.push_back(make_transform(transform));
  }
  proxy.instance_buffer_ =
      filament::InstanceBuffer::Builder(transforms.size())
          .localTransforms(transforms.data())
          .build(*engine_);
  filament::RenderableManager::Builder(1u)
      .geometry(0u, filament::RenderableManager::PrimitiveType::TRIANGLES,
                proxy.geometry_->vertex_buffer,
                proxy.geometry_->index_buffer, 0u,
                proxy.geometry_->index_count)
      .material(0u, proxy.material_instance_)
      .instances(transforms.size(), proxy.instance_buffer_)
      .blendOrder(0u, style.render_state.blend_order)
      .globalBlendOrderEnabled(0u, true)
      .layerMask(kViewportAllLayerMask, style.layer_mask)
      .channel(style.channel)
      .boundingBox(filament_bounds(chunk.world_bounds))
      .culling(false)
      .castShadows(false)
      .receiveShadows(false)
      .build(*engine_, proxy.entity_);
  engine_->getTransformManager().create(proxy.entity_);
  scene_->addEntity(proxy.entity_);
  return proxy;
}

bool InstancedWireRenderBackend::valid(const BatchProxy &proxy) const {
  return proxy.entity_ && proxy.instance_buffer_ != nullptr &&
         proxy.material_instance_ != nullptr;
}

bool InstancedWireRenderBackend::update_batch(
    BatchProxy &proxy, const RenderInstanceBatchChunk &chunk) {
  if (proxy.instance_buffer_ == nullptr || !proxy.entity_ ||
      proxy.instance_buffer_->getInstanceCount() !=
          chunk.world_transforms.size()) {
    return false;
  }
  std::vector<filament::math::mat4f> transforms;
  transforms.reserve(chunk.world_transforms.size());
  for (const QMat4 &transform : chunk.world_transforms) {
    transforms.push_back(make_transform(transform));
  }
  proxy.instance_buffer_->setLocalTransforms(transforms.data(),
                                             transforms.size());
  filament::RenderableManager &renderables = engine_->getRenderableManager();
  const filament::RenderableManager::Instance instance =
      renderables.getInstance(proxy.entity_);
  if (!instance) {
    return false;
  }
  renderables.setAxisAlignedBoundingBox(instance,
                                        filament_bounds(chunk.world_bounds));
  return true;
}

void InstancedWireRenderBackend::update_line_tint(
    std::span<BatchProxy> proxies, const ViewportSettingColor &tint) {
  const filament::math::float4 linear_tint{
      srgb_to_linear(tint.r), srgb_to_linear(tint.g),
      srgb_to_linear(tint.b), tint.a};
  for (BatchProxy &proxy : proxies) {
    if (proxy.material_instance_ != nullptr) {
      proxy.material_instance_->setParameter("lineTint", linear_tint);
    }
  }
}

bool InstancedWireRenderBackend::update_line_width(
    BatchProxy &proxy, float line_width_pixels) {
  if (proxy.material_instance_ == nullptr ||
      !std::isfinite(line_width_pixels) || line_width_pixels <= 0.0f) {
    return false;
  }
  if (proxy.line_width_pixels_ == line_width_pixels) {
    return true;
  }
  proxy.material_instance_->setParameter("lineWidth", line_width_pixels);
  proxy.line_width_pixels_ = line_width_pixels;
  ++stats_.line_width_update_count;
  return true;
}

void InstancedWireRenderBackend::destroy_batch(BatchProxy &proxy) {
  if (engine_ == nullptr) {
    return;
  }
  if (proxy.entity_) {
    if (scene_ != nullptr) {
      scene_->remove(proxy.entity_);
    }
    engine_->getRenderableManager().destroy(proxy.entity_);
    engine_->getTransformManager().destroy(proxy.entity_);
  }
  if (proxy.instance_buffer_ != nullptr) {
    engine_->destroy(proxy.instance_buffer_);
    proxy.instance_buffer_ = nullptr;
  }
  if (proxy.material_instance_ != nullptr) {
    engine_->destroy(proxy.material_instance_);
    proxy.material_instance_ = nullptr;
  }
  if (proxy.entity_) {
    utils::EntityManager::get().destroy(proxy.entity_);
    proxy.entity_.clear();
  }
  proxy.geometry_ = nullptr;
  proxy.line_width_pixels_ = 1.0f;
}

void InstancedWireRenderBackend::destroy_batches(
    std::span<BatchProxy> proxies) {
  if (engine_ == nullptr || proxies.empty()) {
    return;
  }
  std::size_t pending_retirements = 0u;
  auto &renderables = engine_->getRenderableManager();
  // Release all borrowing components before their materials. Merely removing
  // scene membership leaves Filament's material-lifetime validation scans live.
  for (const BatchProxy &proxy : proxies) {
    if (proxy.entity_ && renderables.hasComponent(proxy.entity_)) {
      renderables.destroy(proxy.entity_);
      submit_wire_retirement(*engine_, pending_retirements);
    }
  }
  for (BatchProxy &proxy : proxies) {
    const bool had_resources = proxy.entity_ ||
                               proxy.material_instance_ != nullptr ||
                               proxy.instance_buffer_ != nullptr;
    destroy_batch(proxy);
    if (had_resources) {
      submit_wire_retirement(*engine_, pending_retirements);
    }
  }
  if (pending_retirements != 0u) {
    engine_->flush();
  }
}

void InstancedWireRenderBackend::destroy_geometry_resources() {
  if (engine_ != nullptr) {
    std::size_t pending_retirements = 0u;
    for (auto &[signature, resources] : geometry_resources_) {
      (void)signature;
      for (const std::unique_ptr<GeometryResource> &resource : resources) {
        if (resource->vertex_buffer != nullptr) {
          engine_->destroy(resource->vertex_buffer);
          submit_wire_retirement(*engine_, pending_retirements);
        }
        if (resource->index_buffer != nullptr) {
          engine_->destroy(resource->index_buffer);
          submit_wire_retirement(*engine_, pending_retirements);
        }
      }
    }
    if (pending_retirements != 0u) {
      engine_->flush();
    }
  }
  geometry_resources_.clear();
  stats_.unique_geometry_resource_count = 0u;
  stats_.derived_vertex_bytes = 0u;
  stats_.derived_index_bytes = 0u;
}

void InstancedWireRenderBackend::prune_unused_geometry_resources(
    std::span<const BatchProxy> live_proxies) {
  const std::array live_proxy_groups{live_proxies};
  prune_unused_geometry_resources(live_proxy_groups);
}

void InstancedWireRenderBackend::prune_unused_geometry_resources(
    std::span<const std::span<const BatchProxy>> live_proxy_groups) {
  std::size_t pending_retirements = 0u;
  std::unordered_set<const GeometryResource *> live;
  std::size_t live_proxy_count = 0u;
  for (const std::span<const BatchProxy> group : live_proxy_groups) {
    live_proxy_count += group.size();
  }
  live.reserve(live_proxy_count);
  for (const std::span<const BatchProxy> group : live_proxy_groups) {
    for (const BatchProxy &proxy : group) {
      if (proxy.geometry_ != nullptr) {
        live.insert(proxy.geometry_);
      }
    }
  }
  for (auto bucket_it = geometry_resources_.begin();
       bucket_it != geometry_resources_.end();) {
    auto &bucket = bucket_it->second;
    for (auto it = bucket.begin(); it != bucket.end();) {
      GeometryResource &resource = **it;
      if (live.contains(&resource)) {
        ++it;
        continue;
      }
      if (resource.vertex_buffer != nullptr) {
        engine_->destroy(resource.vertex_buffer);
        submit_wire_retirement(*engine_, pending_retirements);
      }
      if (resource.index_buffer != nullptr) {
        engine_->destroy(resource.index_buffer);
        submit_wire_retirement(*engine_, pending_retirements);
      }
      stats_.derived_vertex_bytes -= resource.vertex_bytes;
      stats_.derived_index_bytes -= resource.index_bytes;
      --stats_.unique_geometry_resource_count;
      it = bucket.erase(it);
    }
    if (bucket.empty()) {
      bucket_it = geometry_resources_.erase(bucket_it);
    } else {
      ++bucket_it;
    }
  }
  if (pending_retirements != 0u) {
    engine_->flush();
  }
}

void InstancedWireRenderBackend::update_clip_plane(
    std::span<BatchProxy> proxies, float near_plane) {
  for (BatchProxy &proxy : proxies) {
    if (proxy.material_instance_ != nullptr) {
      proxy.material_instance_->setParameter("nearPlane", near_plane);
    }
  }
}

void InstancedWireRenderBackend::update_instance_edit_style(
    std::span<BatchProxy> proxies, const InstanceEditStyle &style,
    bool dimmed_only) {
  for (BatchProxy &proxy : proxies) {
    if (proxy.material_instance_ != nullptr &&
        (!dimmed_only || proxy.dimmed_)) {
      bind_instance_edit_context_style(*proxy.material_instance_, style);
    }
  }
}

void InstancedWireRenderBackend::set_layer_mask(
    std::span<const BatchProxy> proxies, std::uint8_t layer_mask) {
  if (engine_ == nullptr) {
    return;
  }
  filament::RenderableManager &renderables = engine_->getRenderableManager();
  for (const BatchProxy &proxy : proxies) {
    if (!proxy.entity_) {
      continue;
    }
    const filament::RenderableManager::Instance instance =
        renderables.getInstance(proxy.entity_);
    if (instance) {
      renderables.setLayerMask(instance, kViewportAllLayerMask, layer_mask);
    }
  }
}

} // namespace quader
