////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/scene_wireframe_overlay_cache.hpp"

#include "core/color_space.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/overlay/overlay_mesh_types.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/render_layers.hpp"
#include "render/render_signature_builder.hpp"
#include "render/viewport_overlay_policy.hpp"

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
#include <bit>
#include <limits>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

/// Stores one shader-expanded semantic edge-ribbon vertex for scene wires.
struct SceneWireframeLineVertex final {
  OverlayFloat4 position;
  OverlayFloat4 start;
  OverlayFloat4 end;
  OverlayFloat4 control;
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

std::uint32_t wire_color(
    const RenderOverlayMeshSnapshot &mesh,
    const ViewportPersistentOverlayStyleSnapshot &style) {
  if (mesh.selected) {
    return style.selection_wire_color;
  }
  constexpr std::uint8_t kWireframeAlpha = static_cast<std::uint8_t>(
      (kOverlayWireframeXrayColor >> 24u) & 0xffu);
  return rgba8(normalized_to_byte(mesh.wireframe_tint.x),
               normalized_to_byte(mesh.wireframe_tint.y),
               normalized_to_byte(mesh.wireframe_tint.z), kWireframeAlpha);
}

ViewportOverlayLayer wire_layer(const RenderOverlayMeshSnapshot &mesh) {
  return mesh.selected ? ViewportOverlayLayer::SelectedEdge
                       : ViewportOverlayLayer::WireframeXray;
}

float wire_width(const RenderOverlayMeshSnapshot &mesh,
                 const ViewportPersistentOverlayStyleSnapshot &style) {
  return mesh.selected
             ? style.selection_edge_line_width_pixels
             : viewport_overlay_line_width_pixels(wire_layer(mesh), 0.0f);
}

std::uint64_t wire_geometry_identity(const RenderOverlayMeshSnapshot &mesh) {
  if (mesh.wire_geometry_payload != nullptr &&
      mesh.wire_geometry_payload->content_id != 0u) {
    return mesh.wire_geometry_payload->content_id;
  }
  return RenderSignatureBuilder{}
      .add_value(mesh.mesh_id)
      .add_value(mesh.mesh_revision)
      .value();
}

std::uint64_t wire_geometry_signature(
    const RenderOverlayMeshSnapshot &mesh,
    const ViewportPersistentOverlayStyleSnapshot &style) {
  return RenderSignatureBuilder{}
      .add_value(wire_geometry_identity(mesh))
      .add_value(wire_color(mesh, style))
      .value();
}

RenderInstancePrimitiveKey wire_key(
    const RenderOverlayMeshSnapshot &mesh,
    const ViewportPersistentOverlayStyleSnapshot &style) {
  return {
      // These exact runtime tokens deliberately avoid grouping on a content
      // hash. The remaining exact style fields distinguish the derived VB and
      // its per-batch material state without scene or AssetId identity.
      .geometry_payload = wire_geometry_identity(mesh),
      .material_payload = wire_geometry_identity(mesh),
      .index_offset = wire_color(mesh, style),
      .index_count = std::bit_cast<std::uint32_t>(wire_width(mesh, style)),
      .layer_mask = static_cast<std::uint8_t>(wire_layer(mesh)),
      .channel = kViewportDefaultRenderableChannel,
      .double_sided = true,
      .editor_unlit = true,
  };
}

/// Hashes every exact field used to group one derived wire primitive.
struct PrimitiveKeyHash final {
  std::size_t operator()(const RenderInstancePrimitiveKey &key) const noexcept {
    return static_cast<std::size_t>(
        RenderSignatureBuilder{}
            .add_value(key.geometry_payload)
            .add_value(key.material_payload)
            .add_value(key.index_offset)
            .add_value(key.index_count)
            .add_value(key.layer_mask)
            .add_value(key.channel)
            .add_value(key.presentation_flags)
            .add_bool(key.cast_shadows)
            .add_bool(key.receive_shadows)
            .add_bool(key.double_sided)
            .add_bool(key.editor_unlit)
            .value());
  }
};

RenderInstanceBounds compute_local_bounds(
    std::span<const MeshVertex> vertices) {
  RenderInstanceBounds bounds;
  for (const MeshVertex &vertex : vertices) {
    if (!bounds.present) {
      bounds.min = vertex.position;
      bounds.max = vertex.position;
      bounds.present = true;
      continue;
    }
    bounds.min.x = std::min(bounds.min.x, vertex.position.x);
    bounds.min.y = std::min(bounds.min.y, vertex.position.y);
    bounds.min.z = std::min(bounds.min.z, vertex.position.z);
    bounds.max.x = std::max(bounds.max.x, vertex.position.x);
    bounds.max.y = std::max(bounds.max.y, vertex.position.y);
    bounds.max.z = std::max(bounds.max.z, vertex.position.z);
  }
  return bounds;
}

std::uint64_t participant_token(const RenderOverlayMeshSnapshot &mesh) {
  return mesh.participant_token != 0u ? mesh.participant_token : mesh.entity;
}

/// Holds indexed scene-wire occurrences before instance batch planning.
struct SceneWireframeOccurrencePreparation final {
  std::vector<RenderInstancePrimitiveOccurrence> occurrences;
  std::unordered_map<RenderInstancePrimitiveKey,
                     const RenderOverlayMeshSnapshot *, PrimitiveKeyHash>
      record_by_key;
  SceneWireframeOverlayPlanWorkStats work;
  std::size_t unique_geometry_style_count = 0u;
};

SceneWireframeOccurrencePreparation prepare_wire_occurrences(
    const RenderOverlaySceneWireframeSnapshot &snapshot) {
  SceneWireframeOccurrencePreparation prepared;
  prepared.occurrences.reserve(snapshot.meshes.size());
  prepared.record_by_key.reserve(snapshot.meshes.size());
  std::unordered_set<RenderInstancePrimitiveKey, PrimitiveKeyHash> unique_keys;
  unique_keys.reserve(snapshot.meshes.size());
  std::unordered_map<const MeshGeometryPayload *, RenderInstanceBounds>
      local_bounds_by_geometry;
  local_bounds_by_geometry.reserve(snapshot.meshes.size());
  for (const RenderOverlayMeshSnapshot &mesh : snapshot.meshes) {
    if (!mesh.has_wire_geometry()) {
      continue;
    }
    const RenderInstancePrimitiveKey key = wire_key(mesh, snapshot.style);
    prepared.record_by_key.try_emplace(key, &mesh);
    const std::uint64_t participant = participant_token(mesh);
    if (participant == 0u) {
      continue;
    }

    RenderInstanceBounds bounds;
    const MeshGeometryPayload *geometry = mesh.geometry_payload.get();
    const auto cached = geometry != nullptr
                            ? local_bounds_by_geometry.find(geometry)
                            : local_bounds_by_geometry.end();
    if (cached != local_bounds_by_geometry.end()) {
      bounds = cached->second;
      ++prepared.work.local_bounds_cache_hit_count;
    } else {
      bounds = compute_local_bounds(mesh.vertices);
      prepared.work.local_bounds_vertex_visit_count +=
          static_cast<std::uint64_t>(mesh.vertices.size());
      if (geometry != nullptr) {
        local_bounds_by_geometry.try_emplace(geometry, bounds);
      }
    }

    unique_keys.insert(key);
    prepared.occurrences.push_back({
        .key = key,
        .occurrence = participant,
        .world_transform = mesh.world_transform,
        .local_bounds = bounds,
    });
  }
  prepared.work.primitive_record_index_entry_count =
      prepared.record_by_key.size();
  prepared.unique_geometry_style_count = unique_keys.size();
  return prepared;
}

/// Couples planned wire batches with their source mesh records.
struct SceneWireframeBatchPreparation final {
  SceneWireframeOverlayBatchPlan planned;
  std::vector<const RenderOverlayMeshSnapshot *> records;
};

SceneWireframeBatchPreparation prepare_wire_batches(
    const RenderOverlaySceneWireframeSnapshot &snapshot,
    std::size_t max_instances_per_chunk) {
  SceneWireframeOccurrencePreparation occurrences =
      prepare_wire_occurrences(snapshot);
  SceneWireframeBatchPreparation prepared;
  prepared.planned.batches = plan_render_instance_batches(
      occurrences.occurrences, max_instances_per_chunk);
  prepared.planned.participant_count = occurrences.occurrences.size();
  prepared.planned.unique_geometry_style_count =
      occurrences.unique_geometry_style_count;
  prepared.planned.work = occurrences.work;
  prepared.records.reserve(prepared.planned.batches.chunks.size());
  for (const RenderInstanceBatchChunk &chunk :
       prepared.planned.batches.chunks) {
    ++prepared.planned.work.chunk_record_lookup_count;
    const auto found = occurrences.record_by_key.find(chunk.key);
    prepared.records.push_back(
        found == occurrences.record_by_key.end() ? nullptr : found->second);
  }
  return prepared;
}

void append_line_quad(std::vector<SceneWireframeLineVertex> &vertices,
                      std::vector<std::uint32_t> &indices, QVec3 start,
                      QVec3 end, std::uint32_t color_rgba) {
  const std::uint32_t color = rgba8_srgb_to_linear(color_rgba);
  const std::uint32_t offset = static_cast<std::uint32_t>(vertices.size());
  const OverlayFloat4 origin{0.0f, 0.0f, 0.0f, 1.0f};
  const OverlayFloat4 a{start.x, start.y, start.z, 1.0f};
  const OverlayFloat4 b{end.x, end.y, end.z, 1.0f};
  vertices.push_back({origin, a, b, {-1.0f, -1.0f, 0.0f, 0.0f}, color});
  vertices.push_back({origin, a, b, {-1.0f, 1.0f, 1.0f, 0.0f}, color});
  vertices.push_back({origin, a, b, {1.0f, 1.0f, 1.0f, 0.0f}, color});
  vertices.push_back({origin, a, b, {1.0f, -1.0f, 0.0f, 0.0f}, color});
  indices.insert(indices.end(), {offset, offset + 1u, offset + 2u, offset,
                                 offset + 2u, offset + 3u});
}

void append_wire_segments(const RenderOverlayMeshSnapshot &mesh,
                          std::uint32_t color,
                          std::vector<SceneWireframeLineVertex> &vertices,
                          std::vector<std::uint32_t> &indices) {
  for (const MeshWireSegment &segment : mesh.wire_segments) {
    append_line_quad(vertices, indices, segment.start, segment.end, color);
  }
}

bool same_geometry_resource(
    const RenderOverlayMeshSnapshot &mesh,
    const ViewportPersistentOverlayStyleSnapshot &style,
    const std::shared_ptr<const MeshWireGeometryPayload> &wire_geometry,
    std::uint32_t color) {
  return wire_geometry != nullptr && mesh.wire_geometry_payload != nullptr &&
         wire_geometry->content_id == mesh.wire_geometry_payload->content_id &&
         color == wire_color(mesh, style);
}

} // namespace

/// Couples a full-scene wire batch plan with its source mesh records.
struct SceneWireframeOverlayCache::PreparedPlan final {
  SceneWireframeOverlayBatchPlan planned;
  std::vector<const RenderOverlayMeshSnapshot *> records;
};

/// Owns one derived local edge-ribbon resource for a topology/style pair.
struct SceneWireframeOverlayCache::WireGeometryResource final {
  std::uint64_t signature = 0u;
  std::shared_ptr<const MeshWireGeometryPayload> wire_geometry;
  std::uint32_t color = 0u;
  filament::VertexBuffer *vertex_buffer = nullptr;
  filament::IndexBuffer *index_buffer = nullptr;
  std::uint32_t index_count = 0u;
  std::uint64_t vertex_bytes = 0u;
  std::uint64_t index_bytes = 0u;
};

/// Owns one Filament renderable and instance buffer for a scene-wire chunk.
struct SceneWireframeOverlayCache::WireBatchProxy final {
  WireBatchProxy() = default;
  WireBatchProxy(const WireBatchProxy &) = delete;
  WireBatchProxy &operator=(const WireBatchProxy &) = delete;
  WireBatchProxy(WireBatchProxy &&other) noexcept
      : entity(other.entity),
        instance_buffer(std::exchange(other.instance_buffer, nullptr)),
        material_instance(std::exchange(other.material_instance, nullptr)),
        geometry(std::exchange(other.geometry, nullptr)) {
    other.entity.clear();
  }
  WireBatchProxy &operator=(WireBatchProxy &&) = delete;

  utils::Entity entity;
  filament::InstanceBuffer *instance_buffer = nullptr;
  filament::MaterialInstance *material_instance = nullptr;
  WireGeometryResource *geometry = nullptr;
};

SceneWireframeOverlayCache::SceneWireframeOverlayCache() = default;

SceneWireframeOverlayBatchPlan plan_scene_wireframe_overlay_batches(
    const RenderOverlaySceneWireframeSnapshot &snapshot,
    std::size_t max_instances_per_chunk) {
  SceneWireframeBatchPreparation prepared =
      prepare_wire_batches(snapshot, max_instances_per_chunk);
  return std::move(prepared.planned);
}

SceneWireframeOverlayCache::~SceneWireframeOverlayCache() { shutdown(); }

void SceneWireframeOverlayCache::initialize(
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

void SceneWireframeOverlayCache::shutdown() {
  clear();
  destroy_geometry_resources();
  engine_ = nullptr;
  scene_ = nullptr;
  line_material_ = nullptr;
  max_instances_per_chunk_ = 1u;
  synced_scene_binding_ = {};
  synced_scene_revision_ = 0u;
  synced_asset_revision_ = 0u;
  synced_local_visibility_revision_ = 0u;
  synced_wire_style_signature_ = 0u;
  synced_clip_planes_ = {};
  layer_mask_ = 0u;
  has_synced_ = false;
  stats_ = {};
}

void SceneWireframeOverlayCache::clear() {
  for (WireBatchProxy &proxy : proxies_) {
    destroy_batch(proxy);
  }
  proxies_.clear();
  plan_ = {};
  stats_.participant_count = 0u;
  stats_.chunk_count = 0u;
}

SceneWireframeOverlayCache::PreparedPlan
SceneWireframeOverlayCache::prepare(
    const RenderOverlaySceneWireframeSnapshot &snapshot) const {
  SceneWireframeBatchPreparation batch_preparation =
      prepare_wire_batches(snapshot, max_instances_per_chunk_);
  PreparedPlan prepared;
  prepared.planned = std::move(batch_preparation.planned);
  prepared.records = std::move(batch_preparation.records);
  return prepared;
}

void SceneWireframeOverlayCache::sync(
    const RenderOverlaySceneWireframeSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  if (!initialized()) {
    return;
  }
  if (has_synced_ && synced_scene_binding_ != snapshot.scene_binding) {
    clear();
    destroy_geometry_resources();
    has_synced_ = false;
  }

  set_layer_mask(snapshot.layer_mask);
  if (snapshot.layer_mask == 0u) {
    return;
  }
  const bool clip_changed =
      !has_synced_ || synced_clip_planes_ != snapshot.clip_planes;
  const bool style_changed = scene_wireframe_style_changed(
      has_synced_, synced_wire_style_signature_, snapshot.style);
  if (has_synced_ && synced_scene_binding_ == snapshot.scene_binding &&
      synced_scene_revision_ == snapshot.scene_revision &&
      synced_asset_revision_ == snapshot.asset_revision &&
      synced_local_visibility_revision_ ==
          snapshot.local_visibility_revision &&
      !clip_changed &&
      !style_changed) {
    return;
  }

  const PreparedPlan prepared = prepare(snapshot);
  const RenderInstanceBatchUpdateKind update =
      classify_instance_batch_update(plan_, prepared.planned.batches);
  bool synchronized = false;
  if (!style_changed &&
      update == RenderInstanceBatchUpdateKind::TransformsOnly) {
    synchronized = update_transforms(prepared);
    if (synchronized) {
      plan_ = prepared.planned.batches;
      ++stats_.transform_update_count;
    }
  } else if (!style_changed &&
             update == RenderInstanceBatchUpdateKind::Unchanged) {
    synchronized = true;
  }
  if (!synchronized) {
    synchronized = rebuild(prepared, snapshot);
  }
  if (!synchronized) {
    return;
  }
  if (clip_changed) {
    update_clip_planes(snapshot.clip_planes);
  }
  synced_scene_binding_ = snapshot.scene_binding;
  synced_scene_revision_ = snapshot.scene_revision;
  synced_asset_revision_ = snapshot.asset_revision;
  synced_local_visibility_revision_ = snapshot.local_visibility_revision;
  synced_wire_style_signature_ = snapshot.style.wire_signature;
  synced_clip_planes_ = snapshot.clip_planes;
  has_synced_ = true;
  stats_.participant_count = prepared.planned.participant_count;
  stats_.chunk_count = prepared.planned.batches.chunks.size();
}

bool SceneWireframeOverlayCache::rebuild(
    const PreparedPlan &prepared,
    const RenderOverlaySceneWireframeSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  std::vector<WireBatchProxy> created;
  created.reserve(prepared.planned.batches.chunks.size());
  for (std::size_t index = 0u;
       index < prepared.planned.batches.chunks.size(); ++index) {
    const RenderOverlayMeshSnapshot *record = prepared.records[index];
    if (record == nullptr) {
      for (WireBatchProxy &proxy : created) {
        destroy_batch(proxy);
      }
      return false;
    }
    WireBatchProxy proxy = create_batch(
        *record, prepared.planned.batches.chunks[index], snapshot.style,
        snapshot.clip_planes);
    if (!proxy.entity || proxy.instance_buffer == nullptr) {
      destroy_batch(proxy);
      for (WireBatchProxy &existing : created) {
        destroy_batch(existing);
      }
      return false;
    }
    created.push_back(std::move(proxy));
  }

  for (WireBatchProxy &proxy : proxies_) {
    destroy_batch(proxy);
  }
  proxies_.swap(created);
  plan_ = prepared.planned.batches;
  ++stats_.rebuild_count;
  stats_.participant_count = prepared.planned.participant_count;
  stats_.chunk_count = proxies_.size();
  prune_unused_geometry_resources();
  return true;
}

bool SceneWireframeOverlayCache::update_transforms(
    const PreparedPlan &prepared) {
  if (prepared.planned.batches.chunks.size() != proxies_.size()) {
    return false;
  }
  for (std::size_t index = 0u; index < proxies_.size(); ++index) {
    if (!update_batch(proxies_[index],
                      prepared.planned.batches.chunks[index])) {
      return false;
    }
  }
  return true;
}

SceneWireframeOverlayCache::WireGeometryResource *
SceneWireframeOverlayCache::acquire_geometry(
    const RenderOverlayMeshSnapshot &mesh,
    const ViewportPersistentOverlayStyleSnapshot &style) {
  const std::uint64_t signature = wire_geometry_signature(mesh, style);
  auto &bucket = geometry_resources_[signature];
  const std::uint32_t color = wire_color(mesh, style);
  for (const std::unique_ptr<WireGeometryResource> &resource : bucket) {
    if (same_geometry_resource(mesh, style, resource->wire_geometry,
                               resource->color)) {
      ++stats_.geometry_reuse_count;
      return resource.get();
    }
  }

  std::vector<SceneWireframeLineVertex> vertices;
  std::vector<std::uint32_t> indices;
  append_wire_segments(mesh, color, vertices, indices);
  if (vertices.empty() || indices.empty()) {
    return nullptr;
  }

  auto resource = std::make_unique<WireGeometryResource>();
  resource->signature = signature;
  resource->wire_geometry = mesh.wire_geometry_payload;
  resource->color = color;
  resource->index_count = static_cast<std::uint32_t>(indices.size());
  resource->vertex_bytes =
      vertices.size() * sizeof(SceneWireframeLineVertex);
  resource->index_bytes = indices.size() * sizeof(std::uint32_t);
  resource->vertex_buffer =
      filament::VertexBuffer::Builder()
          .vertexCount(static_cast<std::uint32_t>(vertices.size()))
          .bufferCount(1u)
          .attribute(filament::VertexAttribute::POSITION, 0u,
                     AttributeType::FLOAT4,
                     offsetof(SceneWireframeLineVertex, position),
                     sizeof(SceneWireframeLineVertex))
          .attribute(filament::VertexAttribute::CUSTOM0, 0u,
                     AttributeType::FLOAT4,
                     offsetof(SceneWireframeLineVertex, start),
                     sizeof(SceneWireframeLineVertex))
          .attribute(filament::VertexAttribute::CUSTOM1, 0u,
                     AttributeType::FLOAT4,
                     offsetof(SceneWireframeLineVertex, end),
                     sizeof(SceneWireframeLineVertex))
          .attribute(filament::VertexAttribute::CUSTOM2, 0u,
                     AttributeType::FLOAT4,
                     offsetof(SceneWireframeLineVertex, control),
                     sizeof(SceneWireframeLineVertex))
          .attribute(filament::VertexAttribute::COLOR, 0u,
                     AttributeType::UBYTE4,
                     offsetof(SceneWireframeLineVertex, color),
                     sizeof(SceneWireframeLineVertex))
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

  WireGeometryResource *result = resource.get();
  const std::uint64_t vertex_bytes = resource->vertex_bytes;
  const std::uint64_t index_bytes = resource->index_bytes;
  bucket.push_back(std::move(resource));
  stats_.derived_vertex_bytes += vertex_bytes;
  stats_.derived_index_bytes += index_bytes;
  ++stats_.geometry_upload_count;
  ++stats_.unique_geometry_resource_count;
  return result;
}

SceneWireframeOverlayCache::WireBatchProxy
SceneWireframeOverlayCache::create_batch(
    const RenderOverlayMeshSnapshot &mesh,
    const RenderInstanceBatchChunk &chunk,
    const ViewportPersistentOverlayStyleSnapshot &style,
    ViewportCameraClipPlanes clip_planes) {
  WireBatchProxy proxy;
  proxy.geometry = acquire_geometry(mesh, style);
  if (proxy.geometry == nullptr || chunk.world_transforms.empty() ||
      chunk.world_transforms.size() > max_instances_per_chunk_) {
    return proxy;
  }

  const ViewportOverlayLayer layer = wire_layer(mesh);
  const std::uint16_t blend_order = static_cast<std::uint16_t>(
      viewport_overlay_draw_order(layer) * 64u);
  const OverlayRenderState state = OverlayRenderableBuilder::make_state(
      layer, ViewportOverlayGeometryKind::Lines, blend_order);

  proxy.entity = utils::EntityManager::get().create();
  proxy.material_instance = line_material_->createInstance();
  if (proxy.material_instance == nullptr) {
    return proxy;
  }
  configure_overlay_material(*proxy.material_instance, state);
  proxy.material_instance->setParameter("lineWidth", wire_width(mesh, style));
  proxy.material_instance->setParameter("featherPixels",
                                        kOverlayLineFeatherPixels);
  proxy.material_instance->setParameter("nearPlane",
                                        clip_planes.near_plane);

  std::vector<filament::math::mat4f> transforms;
  transforms.reserve(chunk.world_transforms.size());
  for (const QMat4 &transform : chunk.world_transforms) {
    transforms.push_back(make_transform(transform));
  }
  proxy.instance_buffer =
      filament::InstanceBuffer::Builder(transforms.size())
          .localTransforms(transforms.data())
          .build(*engine_);
  filament::RenderableManager::Builder(1u)
      .geometry(0u, filament::RenderableManager::PrimitiveType::TRIANGLES,
                proxy.geometry->vertex_buffer, proxy.geometry->index_buffer,
                0u, proxy.geometry->index_count)
      .material(0u, proxy.material_instance)
      .instances(transforms.size(), proxy.instance_buffer)
      .blendOrder(0u, state.blend_order)
      .globalBlendOrderEnabled(0u, true)
      .layerMask(kViewportAllLayerMask, layer_mask_)
      .channel(kViewportDefaultRenderableChannel)
      .boundingBox(filament_bounds(chunk.world_bounds))
      .culling(false)
      .castShadows(false)
      .receiveShadows(false)
      .build(*engine_, proxy.entity);
  engine_->getTransformManager().create(proxy.entity);
  scene_->addEntity(proxy.entity);
  return proxy;
}

bool SceneWireframeOverlayCache::update_batch(
    WireBatchProxy &proxy, const RenderInstanceBatchChunk &chunk) {
  if (proxy.instance_buffer == nullptr || !proxy.entity ||
      proxy.instance_buffer->getInstanceCount() !=
          chunk.world_transforms.size()) {
    return false;
  }
  std::vector<filament::math::mat4f> transforms;
  transforms.reserve(chunk.world_transforms.size());
  for (const QMat4 &transform : chunk.world_transforms) {
    transforms.push_back(make_transform(transform));
  }
  proxy.instance_buffer->setLocalTransforms(transforms.data(),
                                             transforms.size());
  filament::RenderableManager &renderables = engine_->getRenderableManager();
  const filament::RenderableManager::Instance instance =
      renderables.getInstance(proxy.entity);
  if (!instance) {
    return false;
  }
  renderables.setAxisAlignedBoundingBox(instance,
                                        filament_bounds(chunk.world_bounds));
  return true;
}

void SceneWireframeOverlayCache::destroy_batch(WireBatchProxy &proxy) {
  if (engine_ == nullptr) {
    return;
  }
  if (proxy.entity) {
    if (scene_ != nullptr) {
      scene_->remove(proxy.entity);
    }
    engine_->getRenderableManager().destroy(proxy.entity);
    engine_->getTransformManager().destroy(proxy.entity);
  }
  if (proxy.instance_buffer != nullptr) {
    engine_->destroy(proxy.instance_buffer);
    proxy.instance_buffer = nullptr;
  }
  if (proxy.material_instance != nullptr) {
    engine_->destroy(proxy.material_instance);
    proxy.material_instance = nullptr;
  }
  if (proxy.entity) {
    utils::EntityManager::get().destroy(proxy.entity);
    proxy.entity.clear();
  }
  proxy.geometry = nullptr;
}

void SceneWireframeOverlayCache::destroy_geometry_resources() {
  if (engine_ != nullptr) {
    for (auto &[signature, bucket] : geometry_resources_) {
      (void)signature;
      for (const std::unique_ptr<WireGeometryResource> &resource : bucket) {
        if (resource->vertex_buffer != nullptr) {
          engine_->destroy(resource->vertex_buffer);
        }
        if (resource->index_buffer != nullptr) {
          engine_->destroy(resource->index_buffer);
        }
      }
    }
  }
  geometry_resources_.clear();
  stats_.unique_geometry_resource_count = 0u;
  stats_.derived_vertex_bytes = 0u;
  stats_.derived_index_bytes = 0u;
}

void SceneWireframeOverlayCache::prune_unused_geometry_resources() {
  std::unordered_set<const WireGeometryResource *> live;
  live.reserve(proxies_.size());
  for (const WireBatchProxy &proxy : proxies_) {
    if (proxy.geometry != nullptr) {
      live.insert(proxy.geometry);
    }
  }
  for (auto bucket_it = geometry_resources_.begin();
       bucket_it != geometry_resources_.end();) {
    auto &bucket = bucket_it->second;
    for (auto it = bucket.begin(); it != bucket.end();) {
      WireGeometryResource &resource = **it;
      if (live.contains(&resource)) {
        ++it;
        continue;
      }
      if (resource.vertex_buffer != nullptr) {
        engine_->destroy(resource.vertex_buffer);
      }
      if (resource.index_buffer != nullptr) {
        engine_->destroy(resource.index_buffer);
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
}

void SceneWireframeOverlayCache::update_clip_planes(
    ViewportCameraClipPlanes clip_planes) {
  for (WireBatchProxy &proxy : proxies_) {
    if (proxy.material_instance != nullptr) {
      proxy.material_instance->setParameter("nearPlane",
                                            clip_planes.near_plane);
    }
  }
}

void SceneWireframeOverlayCache::set_layer_mask(std::uint8_t layer_mask) {
  if (layer_mask_ == layer_mask) {
    return;
  }
  layer_mask_ = layer_mask;
  if (engine_ == nullptr) {
    return;
  }
  filament::RenderableManager &renderables = engine_->getRenderableManager();
  for (const WireBatchProxy &proxy : proxies_) {
    if (!proxy.entity) {
      continue;
    }
    const filament::RenderableManager::Instance instance =
        renderables.getInstance(proxy.entity);
    if (instance) {
      renderables.setLayerMask(instance, kViewportAllLayerMask, layer_mask_);
    }
  }
}

} // namespace quader
