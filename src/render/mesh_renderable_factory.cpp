////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/mesh_renderable_factory.hpp"

#include "assets/mesh_collision_geometry_payload.hpp"
#include "core/color_space.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/filament_utils.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_quad_uv_interpolation.hpp"
#include "render/mesh_surface_grid_projection.hpp"
#include "render/overlay/overlay_face_fill_composition.hpp"
#include "render/render_signature_builder.hpp"
#include "viewport/viewport_settings.hpp"

#include <backend/DriverEnums.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <math/mat3.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

/**
 * Represents a Render Vertex value used by the render extraction and Filament resource layer.
 */
struct RenderVertex {
    filament::math::float3 position;
    filament::math::float4 tangent_frame;
    filament::math::float2 uv;
    filament::math::float4 surface_grid_projection;
    filament::math::float4 uv_carrier;
    filament::math::float3 uv_geometric_normal;
    filament::math::float4 quad_uv_coordinates;
    filament::math::float4 quad_uv_correction;
    std::uint32_t color;
};

/// Stores static vertex attributes retained while a modal tool streams only
/// changing positions.
struct DynamicRenderVertexStatic {
    filament::math::float4 tangent_frame;
    filament::math::float2 uv;
    filament::math::float4 surface_grid_projection;
    filament::math::float4 uv_carrier;
    filament::math::float3 uv_geometric_normal;
    filament::math::float4 quad_uv_coordinates;
    filament::math::float4 quad_uv_correction;
    std::uint32_t color;
};

/**
 * Cleans up partially constructed Filament mesh resources until ownership is handed to the proxy.
 */
class MeshRenderableCreationGuard {
public:
    MeshRenderableCreationGuard(MeshRenderableFactory &factory,
                                MeshRenderableProxy &proxy)
        : factory_(factory)
        , proxy_(proxy) {}

    MeshRenderableCreationGuard(const MeshRenderableCreationGuard&) = delete;
    MeshRenderableCreationGuard& operator=(const MeshRenderableCreationGuard&) = delete;
    MeshRenderableCreationGuard(MeshRenderableCreationGuard&&) = delete;
    MeshRenderableCreationGuard& operator=(MeshRenderableCreationGuard&&) = delete;

    ~MeshRenderableCreationGuard() {
        if (active_) {
          if (scene_added_) {
            factory_.destroy(proxy_);
          } else {
            factory_.destroy_detached(proxy_);
          }
        }
    }

    void mark_scene_added() { scene_added_ = true; }

    void release() {
        active_ = false;
    }

private:
    MeshRenderableFactory &factory_;
    MeshRenderableProxy& proxy_;
    bool scene_added_ = false;
    bool active_ = true;
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

filament::math::float4 tangent_frame_from_basis(
    QVec3 normal, QVec3 authored_tangent = {}, QVec3 authored_bitangent = {}) {
  if (length(normal) <= 0.000001f) {
    normal = {0.0f, 1.0f, 0.0f};
  } else {
    normal = normalize(normal);
  }

  QVec3 tangent = authored_tangent - normal * dot(normal, authored_tangent);
  if (length(tangent) <= 0.000001f) {
    const QVec3 reference = std::fabs(normal.y) < 0.999f
                                ? QVec3{0.0f, 1.0f, 0.0f}
                                : QVec3{1.0f, 0.0f, 0.0f};
    tangent = cross(reference, normal);
    if (length(tangent) <= 0.000001f) {
      tangent = {1.0f, 0.0f, 0.0f};
    } else {
      tangent = normalize(tangent);
    }
  } else {
    tangent = normalize(tangent);
  }
  const float handedness =
      length(authored_bitangent) > 0.000001f &&
              dot(cross(normal, tangent), authored_bitangent) < 0.0f
          ? -1.0f
          : 1.0f;
  const QVec3 bitangent = cross(normal, tangent) * handedness;
  const filament::math::mat3f tangent_frame{
      to_filament(tangent),
      to_filament(bitangent),
      to_filament(normal),
  };
  return pack_filament_tangent_frame(tangent_frame);
}

std::vector<filament::math::float4>
mesh_tangent_frames(const RenderMeshAssetSnapshot &mesh) {
  std::vector<QVec3> tangents(mesh.vertices.size());
  std::vector<QVec3> bitangents(mesh.vertices.size());
  for (std::size_t index = 0u; index + 2u < mesh.indices.size(); index += 3u) {
    const std::uint32_t i0 = mesh.indices[index];
    const std::uint32_t i1 = mesh.indices[index + 1u];
    const std::uint32_t i2 = mesh.indices[index + 2u];
    if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() ||
        i2 >= mesh.vertices.size()) {
      continue;
    }
    const MeshVertex &v0 = mesh.vertices[i0];
    const MeshVertex &v1 = mesh.vertices[i1];
    const MeshVertex &v2 = mesh.vertices[i2];
    const QVec3 edge1 = v1.position - v0.position;
    const QVec3 edge2 = v2.position - v0.position;
    const QVec2 delta1 = v1.uv - v0.uv;
    const QVec2 delta2 = v2.uv - v0.uv;
    const float determinant = delta1.x * delta2.y - delta1.y * delta2.x;
    if (!std::isfinite(determinant) || std::fabs(determinant) <= 0.000001f) {
      continue;
    }
    const float reciprocal = 1.0f / determinant;
    const QVec3 tangent =
        (edge1 * delta2.y - edge2 * delta1.y) * reciprocal;
    const QVec3 bitangent =
        (edge2 * delta1.x - edge1 * delta2.x) * reciprocal;
    if (!std::isfinite(tangent.x) || !std::isfinite(tangent.y) ||
        !std::isfinite(tangent.z) || !std::isfinite(bitangent.x) ||
        !std::isfinite(bitangent.y) || !std::isfinite(bitangent.z)) {
      continue;
    }
    tangents[i0] += tangent;
    tangents[i1] += tangent;
    tangents[i2] += tangent;
    bitangents[i0] += bitangent;
    bitangents[i1] += bitangent;
    bitangents[i2] += bitangent;
  }

  std::vector<filament::math::float4> frames;
  frames.reserve(mesh.vertices.size());
  for (std::size_t index = 0u; index < mesh.vertices.size(); ++index) {
    frames.push_back(tangent_frame_from_basis(
        mesh.vertices[index].normal, tangents[index], bitangents[index]));
  }
  return frames;
}

std::vector<RenderVertex>
build_render_vertices(const RenderMeshAssetSnapshot &mesh,
                      std::span<const QVec3> positions = {}) {
  const std::vector<filament::math::float4> tangent_frames =
      mesh_tangent_frames(mesh);
  const std::vector<MeshSurfaceGridVertexProjection> grid_projections =
      build_mesh_surface_grid_vertex_projections(
          mesh.vertices, mesh.indices, mesh.primitives);
  const std::vector<MeshQuadUvInterpolation> quad_interpolations =
      build_mesh_quad_uv_interpolations(mesh.vertices, mesh.indices,
                                       mesh.primitives, positions);
  std::vector<RenderVertex> vertices;
  vertices.reserve(mesh.vertices.size());
  for (std::size_t index = 0u; index < mesh.vertices.size(); ++index) {
    const MeshVertex &vertex = mesh.vertices[index];
    const MeshSurfaceGridVertexProjection &grid_projection =
        grid_projections[index];
    const MeshQuadUvInterpolation &quad = quad_interpolations[index];
    vertices.push_back({
        to_filament(vertex.position),
        tangent_frames[index],
        {vertex.uv.x, vertex.uv.y},
        {grid_projection.local_normal.x, grid_projection.local_normal.y,
         grid_projection.local_normal.z,
         grid_projection.face_projected ? 1.0f : 0.0f},
        {vertex.uv_carrier[0], vertex.uv_carrier[1], vertex.uv_carrier[2],
         vertex.uv_carrier[3]},
        to_filament(vertex.uv_geometric_normal),
        {quad.coordinates.x, quad.coordinates.y, quad.coordinates.z,
         quad.enabled ? 1.0f : 0.0f},
        {quad.geometry_warp.x, quad.geometry_warp.y, quad.uv_correction.x,
         quad.uv_correction.y},
        vertex.rgba,
    });
  }
  return vertices;
}

std::vector<DynamicRenderVertexStatic>
pack_dynamic_static_vertices(std::span<const RenderVertex> vertices) {
  std::vector<DynamicRenderVertexStatic> packed;
  packed.reserve(vertices.size());
  for (const RenderVertex &vertex : vertices) {
    packed.push_back({
        .tangent_frame = vertex.tangent_frame,
        .uv = vertex.uv,
        .surface_grid_projection = vertex.surface_grid_projection,
        .uv_carrier = vertex.uv_carrier,
        .uv_geometric_normal = vertex.uv_geometric_normal,
        .quad_uv_coordinates = vertex.quad_uv_coordinates,
        .quad_uv_correction = vertex.quad_uv_correction,
        .color = vertex.color,
    });
  }
  return packed;
}

RenderInstanceBounds
compute_local_bounds_impl(const RenderMeshAssetSnapshot &mesh) {
  if (mesh.vertices.empty()) {
    return {};
  }

  QVec3 min_value{
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),
  };
  QVec3 max_value{
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::lowest(),
  };

  for (const MeshVertex &vertex : mesh.vertices) {
    min_value.x = std::min(min_value.x, vertex.position.x);
    min_value.y = std::min(min_value.y, vertex.position.y);
    min_value.z = std::min(min_value.z, vertex.position.z);
    max_value.x = std::max(max_value.x, vertex.position.x);
    max_value.y = std::max(max_value.y, vertex.position.y);
    max_value.z = std::max(max_value.z, vertex.position.z);
  }

  return {.min = min_value, .max = max_value, .present = true};
}

RenderInstanceBounds
compute_local_bounds_impl(std::span<const QVec3> positions) {
  if (positions.empty()) {
    return {};
  }
  QVec3 min_value = positions.front();
  QVec3 max_value = positions.front();
  for (const QVec3 position : positions.subspan(1u)) {
    min_value.x = std::min(min_value.x, position.x);
    min_value.y = std::min(min_value.y, position.y);
    min_value.z = std::min(min_value.z, position.z);
    max_value.x = std::max(max_value.x, position.x);
    max_value.y = std::max(max_value.y, position.y);
    max_value.z = std::max(max_value.z, position.z);
  }
  return {.min = min_value, .max = max_value, .present = true};
}

bool same_vertex(const MeshVertex &left, const MeshVertex &right) {
  return left.position.x == right.position.x &&
         left.position.y == right.position.y &&
         left.position.z == right.position.z &&
         left.normal.x == right.normal.x && left.normal.y == right.normal.y &&
         left.normal.z == right.normal.z && left.uv.x == right.uv.x &&
         left.uv.y == right.uv.y && left.rgba == right.rgba &&
         left.source_vertex_id == right.source_vertex_id &&
         left.uv_carrier == right.uv_carrier &&
         left.uv_geometric_normal.x == right.uv_geometric_normal.x &&
         left.uv_geometric_normal.y == right.uv_geometric_normal.y &&
         left.uv_geometric_normal.z == right.uv_geometric_normal.z;
}

std::uint64_t geometry_payload_signature(
    const RenderMeshAssetSnapshot &mesh) {
  if (mesh.geometry_content_id.value != 0u) {
    return mesh.geometry_content_id.value;
  }
  RenderSignatureBuilder signature;
  signature.add_size(mesh.vertices.size());
  for (const MeshVertex &vertex : mesh.vertices) {
    signature.add_vec3(vertex.position)
        .add_vec3(vertex.normal)
        .add_vec2(vertex.uv)
        .add_vec3(vertex.uv_geometric_normal)
        .add_value(vertex.source_vertex_id)
        .add_value(vertex.rgba);
    for (const float coefficient : vertex.uv_carrier) {
      signature.add_float(coefficient);
    }
  }
  signature.add_size(mesh.indices.size());
  for (const std::uint32_t index : mesh.indices) {
    signature.add_value(index);
  }
  signature.add_size(mesh.primitives.size());
  for (const MeshPrimitiveSpan primitive : mesh.primitives) {
    signature.add_value(primitive.index_offset)
        .add_value(primitive.index_count)
        .add_value(primitive.material_slot);
  }
  return signature.value();
}

filament::Box filament_bounds(const RenderInstanceBounds &bounds) {
  if (!bounds.present) {
    return {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
  }
  filament::Box box;
  box.set(to_filament(bounds.min), to_filament(bounds.max));
  return box;
}

void submit_resource_retirement(filament::Engine &engine,
                                std::size_t &pending_retirements) {
  // Destruction does not automatically flush Filament's command arena. Count
  // individual resources, including each material of a multi-primitive mesh.
  constexpr std::size_t kRetirementBatchSize = 64u;
  if (++pending_retirements >= kRetirementBatchSize) {
    engine.flush();
    pending_retirements = 0u;
  }
}

void destroy_renderable_component_resources(filament::Engine &engine,
                                            filament::Scene *scene,
                                            MaterialBindingService &materials,
                                            MeshRenderableProxy &proxy,
                                            bool remove_from_scene,
                                            std::size_t &pending_retirements) {
  if (proxy.entity) {
    if (remove_from_scene && scene != nullptr) {
      scene->remove(proxy.entity);
    }
    auto &renderables = engine.getRenderableManager();
    if (renderables.hasComponent(proxy.entity)) {
      renderables.destroy(proxy.entity);
      submit_resource_retirement(engine, pending_retirements);
    }
    engine.getTransformManager().destroy(proxy.entity);
  }
  if (proxy.instance_buffer) {
    engine.destroy(proxy.instance_buffer);
    submit_resource_retirement(engine, pending_retirements);
    proxy.instance_buffer = nullptr;
  }
  if (proxy.owns_material_instances) {
    for (filament::MaterialInstance *&material_instance :
         proxy.material_instances) {
      if (material_instance) {
        materials.destroy_mesh_material_instance(engine, material_instance);
        submit_resource_retirement(engine, pending_retirements);
      }
    }
  }
  proxy.material_instances.clear();
  proxy.material_slots.clear();
  if (proxy.entity) {
    utils::EntityManager::get().destroy(proxy.entity);
    proxy.entity.clear();
  }
}

} // namespace

/// CPU-only upload storage. Source identity prevents accidental cross-mesh or
/// repeated consumption; the snapshot pins immutable attribute inputs.
struct MeshRenderableFactory::DynamicGeometryUpdate final {
  RenderMeshAssetSnapshot source;
  const QVec3 *source_positions = nullptr;
  std::size_t source_position_count = 0u;
  filament::backend::BufferDescriptor positions;
  filament::backend::BufferDescriptor static_vertices;
  filament::backend::BufferDescriptor indices;
  bool consumed = false;
};

/// Owns shared Filament vertex/index buffers keyed by immutable geometry.
struct MeshRenderableFactory::GeometryCacheState final {
  /// Pins one geometry payload and all GPU buffers derived from it.
  struct Resource final {
    std::uint64_t id = 0u;
    std::uint64_t signature = 0u;
    std::shared_ptr<const MeshGeometryPayload> payload;
    std::shared_ptr<const MeshCollisionGeometryPayload> collision_payload;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshPrimitiveSpan> primitives;
    filament::VertexBuffer *vertex_buffer = nullptr;
    filament::IndexBuffer *index_buffer = nullptr;
    std::size_t reference_count = 0u;
  };

  std::uint64_t next_id = 1u;
  std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> candidates;
  std::unordered_map<std::uint64_t, std::unique_ptr<Resource>> resources;
  GeometryCacheStats stats;
};

MeshRenderableFactory::MeshRenderableFactory() = default;

MeshRenderableFactory::~MeshRenderableFactory() { shutdown(); }

RenderInstanceBounds MeshRenderableFactory::compute_local_bounds(
    const RenderMeshAssetSnapshot &mesh) {
  return compute_local_bounds_impl(mesh);
}

RenderInstanceBounds MeshRenderableFactory::compute_local_bounds(
    std::span<const QVec3> positions) {
  return compute_local_bounds_impl(positions);
}

void MeshRenderableFactory::initialize(
    filament::Engine &engine, filament::Scene &scene,
    MaterialBindingService &material_binding) {
  shutdown();
  engine_ = &engine;
  scene_ = &scene;
  material_binding_ = &material_binding;
  geometry_cache_ = std::make_unique<GeometryCacheState>();
}

void MeshRenderableFactory::shutdown() {
    if (engine_ != nullptr && geometry_cache_ != nullptr) {
      for (auto &[id, resource] : geometry_cache_->resources) {
        (void)id;
        if (resource->vertex_buffer != nullptr) {
          engine_->destroy(resource->vertex_buffer);
          submit_resource_retirement(*engine_,
                                     pending_resource_retirements_);
        }
        if (resource->index_buffer != nullptr) {
          engine_->destroy(resource->index_buffer);
          submit_resource_retirement(*engine_,
                                     pending_resource_retirements_);
        }
      }
      if (pending_resource_retirements_ != 0u) {
        engine_->flush();
      }
    }
    geometry_cache_.reset();
    pending_resource_retirements_ = 0u;
    engine_ = nullptr;
    scene_ = nullptr;
    material_binding_ = nullptr;
}

bool MeshRenderableFactory::acquire_geometry(
    const RenderMeshAssetSnapshot &mesh, MeshRenderableProxy &proxy) {
  if (engine_ == nullptr || geometry_cache_ == nullptr ||
      !mesh.renderable()) {
    return false;
  }
  const std::uint64_t signature = geometry_payload_signature(mesh);
  if (const auto found = geometry_cache_->candidates.find(signature);
      found != geometry_cache_->candidates.end()) {
    for (const std::uint64_t resource_id : found->second) {
      const auto resource = geometry_cache_->resources.find(resource_id);
      if (resource == geometry_cache_->resources.end()) {
        continue;
      }
      const GeometryCacheState::Resource &candidate = *resource->second;
      if (candidate.collision_payload != nullptr) {
        continue;
      }
      const bool same_pinned_payload =
          candidate.payload != nullptr && mesh.geometry_payload != nullptr &&
           (candidate.payload == mesh.geometry_payload ||
           (candidate.payload->vertices.size() == mesh.vertices.size() &&
            candidate.payload->indices.size() == mesh.indices.size() &&
            candidate.payload->primitives.size() == mesh.primitives.size() &&
            std::equal(candidate.payload->vertices.begin(),
                       candidate.payload->vertices.end(),
                       mesh.vertices.begin(), same_vertex) &&
            std::equal(candidate.payload->indices.begin(),
                       candidate.payload->indices.end(),
                       mesh.indices.begin()) &&
            std::equal(candidate.payload->primitives.begin(),
                       candidate.payload->primitives.end(),
                       mesh.primitives.begin())));
      const bool same_fallback_payload =
          candidate.payload == nullptr && mesh.geometry_payload == nullptr &&
          candidate.vertices.size() == mesh.vertices.size() &&
          candidate.indices.size() == mesh.indices.size() &&
          candidate.primitives.size() == mesh.primitives.size() &&
          std::equal(candidate.vertices.begin(), candidate.vertices.end(),
                     mesh.vertices.begin(), same_vertex) &&
          std::equal(candidate.indices.begin(), candidate.indices.end(),
                     mesh.indices.begin()) &&
          std::equal(candidate.primitives.begin(), candidate.primitives.end(),
                     mesh.primitives.begin());
      if (!same_pinned_payload && !same_fallback_payload) {
        continue;
      }
      ++resource->second->reference_count;
      ++geometry_cache_->stats.cache_hit_count;
      proxy.geometry_resource = resource_id;
      proxy.vertex_buffer = candidate.vertex_buffer;
      proxy.index_buffer = candidate.index_buffer;
      proxy.owns_mesh_buffers = true;
      proxy.vertex_count = static_cast<std::uint32_t>(mesh.vertices.size());
      proxy.index_count = static_cast<std::uint32_t>(mesh.indices.size());
      return true;
    }
  }

  std::vector<RenderVertex> vertices = build_render_vertices(mesh);
  auto resource = std::make_unique<GeometryCacheState::Resource>();
  resource->id = geometry_cache_->next_id++;
  resource->signature = signature;
  resource->payload = mesh.geometry_payload;
  if (resource->payload == nullptr) {
    resource->vertices.assign(mesh.vertices.begin(), mesh.vertices.end());
    resource->indices.assign(mesh.indices.begin(), mesh.indices.end());
    resource->primitives.assign(mesh.primitives.begin(),
                                mesh.primitives.end());
  }
  resource->reference_count = 1u;
  resource->vertex_buffer =
      filament::VertexBuffer::Builder()
          .vertexCount(static_cast<std::uint32_t>(vertices.size()))
          .bufferCount(1)
          .attribute(filament::VertexAttribute::POSITION, 0,
                     AttributeType::FLOAT3, offsetof(RenderVertex, position),
                     sizeof(RenderVertex))
          .attribute(filament::VertexAttribute::TANGENTS, 0,
                     AttributeType::FLOAT4,
                     offsetof(RenderVertex, tangent_frame),
                     sizeof(RenderVertex))
          .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2,
                     offsetof(RenderVertex, uv), sizeof(RenderVertex))
          .attribute(filament::VertexAttribute::CUSTOM0, 0,
                     AttributeType::FLOAT4,
                     offsetof(RenderVertex, surface_grid_projection),
                     sizeof(RenderVertex))
          .attribute(filament::VertexAttribute::COLOR, 0,
                     AttributeType::UBYTE4, offsetof(RenderVertex, color),
                     sizeof(RenderVertex))
          .attribute(filament::VertexAttribute::CUSTOM1, 0,
                     AttributeType::FLOAT4, offsetof(RenderVertex, uv_carrier),
                     sizeof(RenderVertex))
          .attribute(filament::VertexAttribute::CUSTOM2, 0,
                     AttributeType::FLOAT3,
                     offsetof(RenderVertex, uv_geometric_normal),
                     sizeof(RenderVertex))
          .attribute(filament::VertexAttribute::CUSTOM3, 0,
                     AttributeType::FLOAT4,
                     offsetof(RenderVertex, quad_uv_coordinates),
                     sizeof(RenderVertex))
          .attribute(filament::VertexAttribute::CUSTOM4, 0,
                     AttributeType::FLOAT4,
                     offsetof(RenderVertex, quad_uv_correction),
                     sizeof(RenderVertex))
          .normalized(filament::VertexAttribute::COLOR)
          .build(*engine_);
  resource->vertex_buffer->setBufferAt(
      *engine_, 0, FilamentBufferDescriptorFactory::vector(vertices));
  resource->index_buffer =
      filament::IndexBuffer::Builder()
          .indexCount(static_cast<std::uint32_t>(mesh.indices.size()))
          .bufferType(filament::IndexBuffer::IndexType::UINT)
          .build(*engine_);
  resource->index_buffer->setBuffer(
      *engine_, FilamentBufferDescriptorFactory::span(mesh.indices));

  const std::uint64_t resource_id = resource->id;
  proxy.geometry_resource = resource_id;
  proxy.vertex_buffer = resource->vertex_buffer;
  proxy.index_buffer = resource->index_buffer;
  proxy.owns_mesh_buffers = true;
  proxy.vertex_count = static_cast<std::uint32_t>(mesh.vertices.size());
  proxy.index_count = static_cast<std::uint32_t>(mesh.indices.size());
  geometry_cache_->candidates[signature].push_back(resource_id);
  geometry_cache_->resources.emplace(resource_id, std::move(resource));
  ++geometry_cache_->stats.upload_count;
  geometry_cache_->stats.uploaded_vertex_bytes +=
      static_cast<std::uint64_t>(vertices.size() * sizeof(RenderVertex));
  geometry_cache_->stats.uploaded_index_bytes +=
      static_cast<std::uint64_t>(mesh.indices.size_bytes());
  geometry_cache_->stats.resource_count = geometry_cache_->resources.size();
  return true;
}

bool MeshRenderableFactory::acquire_collision_geometry(
    std::shared_ptr<const MeshCollisionGeometryPayload> geometry,
    MeshRenderableProxy &proxy) {
  if (engine_ == nullptr || geometry_cache_ == nullptr || geometry == nullptr ||
      !geometry->error_message.empty() || geometry->faces.empty() ||
      geometry->faces.size() % 3u != 0u ||
      geometry->faces.size() > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  const std::uint64_t signature = geometry->content_id;
  const auto bind_resource = [&](const GeometryCacheState::Resource &resource) {
    proxy.geometry_resource = resource.id;
    proxy.vertex_buffer = resource.vertex_buffer;
    proxy.index_buffer = resource.index_buffer;
    proxy.owns_mesh_buffers = true;
    proxy.vertex_count = static_cast<std::uint32_t>(geometry->faces.size());
    proxy.index_count = proxy.vertex_count;
  };
  if (const auto found = geometry_cache_->candidates.find(signature);
      found != geometry_cache_->candidates.end()) {
    for (const std::uint64_t resource_id : found->second) {
      const auto resource = geometry_cache_->resources.find(resource_id);
      if (resource == geometry_cache_->resources.end()) {
        continue;
      }
      const auto &candidate = resource->second->collision_payload;
      if (candidate == nullptr ||
          (candidate != geometry &&
           (candidate->faces.size() != geometry->faces.size() ||
            !std::equal(candidate->faces.begin(), candidate->faces.end(),
                        geometry->faces.begin(),
                        [](QVec3 left, QVec3 right) {
                          return left.x == right.x && left.y == right.y &&
                                 left.z == right.z;
                        })))) {
        continue;
      }
      ++resource->second->reference_count;
      ++geometry_cache_->stats.cache_hit_count;
      bind_resource(*resource->second);
      return true;
    }
  }

  auto resource = std::make_unique<GeometryCacheState::Resource>();
  resource->id = geometry_cache_->next_id++;
  resource->signature = signature;
  resource->collision_payload = geometry;
  resource->reference_count = 1u;
  const auto face_count = static_cast<std::uint32_t>(geometry->faces.size());
  resource->vertex_buffer =
      filament::VertexBuffer::Builder()
          .vertexCount(face_count)
          .bufferCount(1u)
          .attribute(filament::VertexAttribute::POSITION, 0,
                     AttributeType::FLOAT3, 0, sizeof(QVec3))
          .build(*engine_);
  resource->index_buffer =
      filament::IndexBuffer::Builder()
          .indexCount(face_count)
          .bufferType(filament::IndexBuffer::IndexType::UINT)
          .build(*engine_);
  if (resource->vertex_buffer == nullptr || resource->index_buffer == nullptr) {
    if (resource->vertex_buffer != nullptr) {
      engine_->destroy(resource->vertex_buffer);
    }
    if (resource->index_buffer != nullptr) {
      engine_->destroy(resource->index_buffer);
    }
    return false;
  }
  std::vector<std::uint32_t> indices(face_count);
  for (std::uint32_t index = 0u; index < face_count; ++index) {
    indices[index] = index;
  }
  resource->vertex_buffer->setBufferAt(
      *engine_, 0,
      FilamentBufferDescriptorFactory::span<QVec3>(geometry->faces));
  resource->index_buffer->setBuffer(
      *engine_, FilamentBufferDescriptorFactory::vector(std::move(indices)));
  bind_resource(*resource);
  const std::uint64_t resource_id = resource->id;
  geometry_cache_->candidates[signature].push_back(resource_id);
  geometry_cache_->resources.emplace(resource_id, std::move(resource));
  ++geometry_cache_->stats.upload_count;
  geometry_cache_->stats.uploaded_vertex_bytes +=
      geometry->faces.size() * sizeof(QVec3);
  geometry_cache_->stats.uploaded_index_bytes +=
      geometry->faces.size() * sizeof(std::uint32_t);
  geometry_cache_->stats.resource_count = geometry_cache_->resources.size();
  return true;
}

bool MeshRenderableFactory::acquire_dynamic_geometry(
    const RenderMeshAssetSnapshot &mesh, MeshRenderableProxy &proxy,
    std::span<const QVec3> dynamic_positions) {
  if (engine_ == nullptr || !mesh.renderable() ||
      (!dynamic_positions.empty() &&
       dynamic_positions.size() != mesh.vertices.size())) {
    return false;
  }
  const std::vector<RenderVertex> vertices =
      build_render_vertices(mesh, dynamic_positions);
  std::vector<filament::math::float3> positions;
  positions.reserve(vertices.size());
  for (std::size_t index = 0u; index < vertices.size(); ++index) {
    positions.push_back(dynamic_positions.empty()
                            ? vertices[index].position
                            : to_filament(dynamic_positions[index]));
  }
  std::vector<DynamicRenderVertexStatic> static_vertices =
      pack_dynamic_static_vertices(vertices);
  proxy.vertex_buffer =
      filament::VertexBuffer::Builder()
          .vertexCount(static_cast<std::uint32_t>(positions.size()))
          .bufferCount(2)
          .attribute(filament::VertexAttribute::POSITION, 0,
                     AttributeType::FLOAT3, 0,
                     sizeof(filament::math::float3))
          .attribute(filament::VertexAttribute::TANGENTS, 1,
                     AttributeType::FLOAT4,
                     offsetof(DynamicRenderVertexStatic, tangent_frame),
                     sizeof(DynamicRenderVertexStatic))
          .attribute(filament::VertexAttribute::UV0, 1, AttributeType::FLOAT2,
                     offsetof(DynamicRenderVertexStatic, uv),
                     sizeof(DynamicRenderVertexStatic))
          .attribute(filament::VertexAttribute::CUSTOM0, 1,
                     AttributeType::FLOAT4,
                     offsetof(DynamicRenderVertexStatic,
                              surface_grid_projection),
                     sizeof(DynamicRenderVertexStatic))
          .attribute(filament::VertexAttribute::COLOR, 1,
                     AttributeType::UBYTE4,
                     offsetof(DynamicRenderVertexStatic, color),
                     sizeof(DynamicRenderVertexStatic))
          .attribute(filament::VertexAttribute::CUSTOM1, 1,
                     AttributeType::FLOAT4,
                     offsetof(DynamicRenderVertexStatic, uv_carrier),
                     sizeof(DynamicRenderVertexStatic))
          .attribute(filament::VertexAttribute::CUSTOM2, 1,
                     AttributeType::FLOAT3,
                     offsetof(DynamicRenderVertexStatic, uv_geometric_normal),
                     sizeof(DynamicRenderVertexStatic))
          .attribute(filament::VertexAttribute::CUSTOM3, 1,
                     AttributeType::FLOAT4,
                     offsetof(DynamicRenderVertexStatic, quad_uv_coordinates),
                     sizeof(DynamicRenderVertexStatic))
          .attribute(filament::VertexAttribute::CUSTOM4, 1,
                     AttributeType::FLOAT4,
                     offsetof(DynamicRenderVertexStatic, quad_uv_correction),
                     sizeof(DynamicRenderVertexStatic))
          .normalized(filament::VertexAttribute::COLOR)
          .build(*engine_);
  proxy.vertex_buffer->setBufferAt(
      *engine_, 0,
      FilamentBufferDescriptorFactory::vector(std::move(positions)));
  proxy.vertex_buffer->setBufferAt(
      *engine_, 1,
      FilamentBufferDescriptorFactory::vector(std::move(static_vertices)));
  proxy.index_buffer =
      filament::IndexBuffer::Builder()
          .indexCount(static_cast<std::uint32_t>(mesh.indices.size()))
          .bufferType(filament::IndexBuffer::IndexType::UINT)
          .build(*engine_);
  proxy.index_buffer->setBuffer(
      *engine_, FilamentBufferDescriptorFactory::span(mesh.indices));
  proxy.geometry_resource = 0u;
  proxy.owns_mesh_buffers = true;
  proxy.dynamic_geometry = true;
  proxy.vertex_count = static_cast<std::uint32_t>(mesh.vertices.size());
  proxy.index_count = static_cast<std::uint32_t>(mesh.indices.size());
  return true;
}

void MeshRenderableFactory::release_geometry(MeshRenderableProxy &proxy) {
  if (proxy.geometry_resource == 0u || geometry_cache_ == nullptr) {
    if (engine_ != nullptr && proxy.owns_mesh_buffers) {
      if (proxy.vertex_buffer != nullptr) {
        engine_->destroy(proxy.vertex_buffer);
        submit_resource_retirement(*engine_,
                                   pending_resource_retirements_);
      }
      if (proxy.index_buffer != nullptr) {
        engine_->destroy(proxy.index_buffer);
        submit_resource_retirement(*engine_,
                                   pending_resource_retirements_);
      }
    }
    proxy.vertex_buffer = nullptr;
    proxy.index_buffer = nullptr;
    return;
  }

  const std::uint64_t resource_id = proxy.geometry_resource;
  const auto found = geometry_cache_->resources.find(resource_id);
  if (found != geometry_cache_->resources.end() &&
      found->second->reference_count > 0u) {
    --found->second->reference_count;
  }
  // Geometry payloads are immutable and remain resident for this Engine's
  // lifetime. Keeping zero-reference resources in the cache ensures a batch
  // layout or material-only rebuild cannot trigger a redundant GPU upload.
  // MeshRenderableFactory::shutdown() destroys every resident resource.
  proxy.geometry_resource = 0u;
  proxy.vertex_buffer = nullptr;
  proxy.index_buffer = nullptr;
  geometry_cache_->stats.resource_count = geometry_cache_->resources.size();
}

MeshRenderableFactory::GeometryCacheStats
MeshRenderableFactory::geometry_cache_stats() const {
  return geometry_cache_ != nullptr ? geometry_cache_->stats
                                    : GeometryCacheStats{};
}

MeshRenderableProxy MeshRenderableFactory::create(
    EntityId entity_id, const RenderMeshAssetSnapshot &mesh,
    const RenderMaterialBindingSnapshot *material,
    const QMat4 &world_transform, bool cast_shadows, bool double_sided,
    std::uint8_t layer_mask, bool editor_unlit, bool depth_only,
    std::uint8_t channel, bool dynamic_geometry,
    std::span<const QVec3> dynamic_positions,
    const RenderInstanceBounds *local_bounds_override) {
  QDR_PROFILE_FUNCTION();
  MeshRenderableProxy proxy;
  MeshRenderableCreationGuard creation_guard(*this, proxy);
  proxy.entity = utils::EntityManager::get().create();
  proxy.source_entity = entity_id;
  proxy.mesh = mesh.mesh_id;
  proxy.mesh_revision = mesh.mesh_revision;
  proxy.material =
      material ? material->component.material : MaterialReference{};
  proxy.material_revision = material ? material->revision : 0;
  proxy.cast_shadows = cast_shadows;
  proxy.double_sided = double_sided;

  {
    QDR_PROFILE_SCOPE("Mesh renderable acquire shared geometry");
    if (!(dynamic_geometry
              ? acquire_dynamic_geometry(mesh, proxy, dynamic_positions)
                           : acquire_geometry(mesh, proxy))) {
      return proxy;
    }
  }

  const std::vector<MeshPrimitiveSpan> primitive_spans =
      coalesced_render_mesh_primitive_spans(mesh);
  proxy.material_slots.reserve(primitive_spans.size());
  proxy.material_instances.reserve(primitive_spans.size());
  filament::RenderableManager::Builder builder(primitive_spans.size());
  {
    QDR_PROFILE_SCOPE("Mesh renderable build primitives");
    for (std::size_t primitive_index = 0;
         primitive_index < primitive_spans.size(); ++primitive_index) {
      const MeshPrimitiveSpan &primitive = primitive_spans[primitive_index];
      const RenderMaterialAssetSnapshot *primitive_material =
          material ? material->material_for_slot(primitive.material_slot)
                   : nullptr;
      filament::MaterialInstance *material_instance =
          material_binding_->create_mesh_material_instance(
              primitive_material, double_sided, editor_unlit);
      if (depth_only && material_instance) {
        material_instance->setColorWrite(false);
        material_instance->setDepthWrite(true);
        material_instance->setDepthCulling(true);
      }
      proxy.material_slots.push_back(primitive.material_slot);
      proxy.material_instances.push_back(material_instance);
      builder.geometry(primitive_index,
                       filament::RenderableManager::PrimitiveType::TRIANGLES,
                       proxy.vertex_buffer, proxy.index_buffer,
                       primitive.index_offset, primitive.index_count);
      builder.material(primitive_index, material_instance);
    }
  }
  {
    QDR_PROFILE_SCOPE("Mesh renderable build Filament entity");
    const RenderInstanceBounds local_bounds =
        local_bounds_override != nullptr
            ? *local_bounds_override
            : (dynamic_positions.empty()
                   ? compute_local_bounds_impl(mesh)
                   : compute_local_bounds_impl(dynamic_positions));
    builder.boundingBox(filament_bounds(local_bounds))
        .layerMask(kViewportAllLayerMask, layer_mask)
        .channel(channel)
        .castShadows(cast_shadows)
        .receiveShadows(!editor_unlit && !depth_only)
        .build(*engine_, proxy.entity);
  }

  engine_->getTransformManager().create(proxy.entity);
  update_transform(proxy, world_transform);
  scene_->addEntity(proxy.entity);
  creation_guard.mark_scene_added();
  creation_guard.release();
  return proxy;
}

std::shared_ptr<MeshRenderableFactory::DynamicGeometryUpdate>
MeshRenderableFactory::prepare_dynamic_geometry_update(
    const RenderMeshAssetSnapshot &mesh,
    std::span<const QVec3> dynamic_positions) {
  if (!dynamic_positions.empty() &&
      dynamic_positions.size() != mesh.vertices.size()) {
    return {};
  }
  try {
    auto prepared = std::make_shared<DynamicGeometryUpdate>();
    prepared->source = mesh;
    prepared->source_positions = dynamic_positions.data();
    prepared->source_position_count = dynamic_positions.size();
    std::vector<filament::math::float3> positions;
    {
      QDR_PROFILE_SCOPE("Mesh dynamic position upload pack");
      positions.reserve(mesh.vertices.size());
      if (dynamic_positions.empty()) {
        for (const MeshVertex &vertex : mesh.vertices) {
          positions.push_back(to_filament(vertex.position));
        }
      } else {
        for (const QVec3 position : dynamic_positions) {
          positions.push_back(to_filament(position));
        }
      }
    }
    {
      QDR_PROFILE_SCOPE("Mesh dynamic derived attributes pack");
      const std::vector<RenderVertex> render_vertices =
          build_render_vertices(mesh, dynamic_positions);
      auto static_vertices = pack_dynamic_static_vertices(render_vertices);
      std::vector<std::uint32_t> indices(mesh.indices.begin(), mesh.indices.end());
      // Descriptor wrappers own CPU bytes and may allocate. Prepare them here
      // as well so submission cannot fail after uploading only some streams.
      prepared->positions = FilamentBufferDescriptorFactory::vector(std::move(positions));
      prepared->static_vertices = FilamentBufferDescriptorFactory::vector(std::move(static_vertices));
      prepared->indices = FilamentBufferDescriptorFactory::vector(std::move(indices));
    }
    return prepared;
  } catch (...) {
    // Preparation is isolated from renderer state; keep the accepted preview
    // intact if any worker cannot allocate its candidate upload storage.
    return {};
  }
}

bool MeshRenderableFactory::update_dynamic_geometry(
    const MeshRenderableProxy &proxy,
    const RenderMeshAssetSnapshot &mesh,
    std::span<const QVec3> dynamic_positions,
    const RenderInstanceBounds *local_bounds_override,
    std::shared_ptr<DynamicGeometryUpdate> prepared) {
  if (engine_ == nullptr || !proxy.dynamic_geometry ||
      proxy.vertex_buffer == nullptr || proxy.index_buffer == nullptr ||
      proxy.vertex_count != mesh.vertices.size() ||
      proxy.index_count != mesh.indices.size() ||
      (!dynamic_positions.empty() &&
       dynamic_positions.size() != mesh.vertices.size())) {
    return false;
  }
  if (!prepared) {
    prepared = prepare_dynamic_geometry_update(mesh, dynamic_positions);
  }
  if (!prepared || prepared->consumed ||
      prepared->source.mesh_id != mesh.mesh_id ||
      prepared->source.mesh_revision != mesh.mesh_revision ||
      prepared->source.geometry_payload != mesh.geometry_payload ||
      prepared->source.geometry_token != mesh.geometry_token ||
      prepared->source.geometry_content_id != mesh.geometry_content_id ||
      prepared->source.vertices.data() != mesh.vertices.data() ||
      prepared->source.indices.data() != mesh.indices.data() ||
      prepared->source.primitives.data() != mesh.primitives.data() ||
      prepared->source.primitives.size() != mesh.primitives.size() ||
      prepared->source_positions != dynamic_positions.data() ||
      prepared->source_position_count != dynamic_positions.size() ||
      prepared->source.vertices.size() != mesh.vertices.size() ||
      prepared->source.indices.size() != mesh.indices.size()) {
    return false;
  }
  prepared->consumed = true;
  {
    QDR_PROFILE_SCOPE("Mesh dynamic position upload submit");
    proxy.vertex_buffer->setBufferAt(
        *engine_, 0, std::move(prepared->positions));
  }
  {
    QDR_PROFILE_SCOPE("Mesh dynamic derived attributes upload submit");
    proxy.vertex_buffer->setBufferAt(
        *engine_, 1, std::move(prepared->static_vertices));
  }
  {
    QDR_PROFILE_SCOPE("Mesh dynamic index upload submit");
    proxy.index_buffer->setBuffer(
        *engine_, std::move(prepared->indices));
  }
  {
    QDR_PROFILE_SCOPE("Mesh dynamic position update bounds");
    filament::RenderableManager &renderable_manager =
        engine_->getRenderableManager();
    const filament::RenderableManager::Instance instance =
        renderable_manager.getInstance(proxy.entity);
    if (instance) {
      const RenderInstanceBounds local_bounds =
          local_bounds_override != nullptr
              ? *local_bounds_override
              : (dynamic_positions.empty()
                     ? compute_local_bounds_impl(mesh)
                     : compute_local_bounds_impl(dynamic_positions));
      renderable_manager.setAxisAlignedBoundingBox(
          instance, filament_bounds(local_bounds));
    }
  }
  return true;
}

template <typename Resource>
bool same_geometry_topology(const Resource &resource,
                            const RenderMeshAssetSnapshot &mesh) {
  const std::span<const MeshVertex> previous_vertices =
      resource.payload != nullptr
          ? std::span<const MeshVertex>(resource.payload->vertices)
          : std::span<const MeshVertex>(resource.vertices);
  const std::span<const std::uint32_t> previous_indices =
      resource.payload != nullptr
          ? std::span<const std::uint32_t>(resource.payload->indices)
          : std::span<const std::uint32_t>(resource.indices);
  const std::span<const MeshPrimitiveSpan> previous_primitives =
      resource.payload != nullptr
          ? std::span<const MeshPrimitiveSpan>(resource.payload->primitives)
          : std::span<const MeshPrimitiveSpan>(resource.primitives);
  return previous_vertices.size() == mesh.vertices.size() &&
         previous_indices.size() == mesh.indices.size() &&
         previous_primitives.size() == mesh.primitives.size() &&
         std::equal(previous_indices.begin(), previous_indices.end(),
                    mesh.indices.begin()) &&
         std::equal(previous_primitives.begin(), previous_primitives.end(),
                    mesh.primitives.begin());
}

bool MeshRenderableFactory::try_rebind_static_geometry(
    std::span<MeshRenderableProxy *> proxies,
    const RenderMeshAssetSnapshot &mesh) {
  QDR_PROFILE_FUNCTION();
  if (!initialized() || proxies.empty() || !mesh.renderable() ||
      geometry_cache_ == nullptr) {
    return false;
  }

  const std::vector<MeshPrimitiveSpan> primitive_spans =
      coalesced_render_mesh_primitive_spans(mesh);
  filament::RenderableManager &renderables = engine_->getRenderableManager();
  {
    QDR_PROFILE_SCOPE("Mesh renderable validate topology-stable rebind");
    for (const MeshRenderableProxy *proxy : proxies) {
      if (proxy == nullptr || !proxy->entity || proxy->dynamic_geometry ||
          proxy->instance_buffer != nullptr ||
          proxy->mesh != mesh.mesh_id ||
          proxy->geometry_resource == 0u || proxy->vertex_buffer == nullptr ||
          proxy->index_buffer == nullptr || !proxy->owns_mesh_buffers ||
          !proxy->owns_material_instances ||
          proxy->vertex_count != mesh.vertices.size() ||
          proxy->index_count != mesh.indices.size() ||
          proxy->material_slots.size() != primitive_spans.size() ||
          proxy->material_instances.size() != primitive_spans.size()) {
        return false;
      }

      const auto resource =
          geometry_cache_->resources.find(proxy->geometry_resource);
      if (resource == geometry_cache_->resources.end()) {
        return false;
      }
      const GeometryCacheState::Resource &geometry = *resource->second;
      if (geometry.vertex_buffer != proxy->vertex_buffer ||
          geometry.index_buffer != proxy->index_buffer ||
          !same_geometry_topology(geometry, mesh)) {
        return false;
      }
      for (std::size_t primitive_index = 0u;
           primitive_index < primitive_spans.size(); ++primitive_index) {
        if (proxy->material_slots[primitive_index] !=
            primitive_spans[primitive_index].material_slot) {
          return false;
        }
      }

      const filament::RenderableManager::Instance instance =
          renderables.getInstance(proxy->entity);
      if (!instance ||
          renderables.getPrimitiveCount(instance) != primitive_spans.size()) {
        return false;
      }
      for (std::size_t primitive_index = 0u;
           primitive_index < primitive_spans.size(); ++primitive_index) {
        if (renderables.getMaterialInstanceAt(instance, primitive_index) !=
            proxy->material_instances[primitive_index]) {
          return false;
        }
      }
    }
    for (std::size_t left = 0u; left < proxies.size(); ++left) {
      for (std::size_t right = left + 1u; right < proxies.size(); ++right) {
        if (proxies[left] == proxies[right]) {
          return false;
        }
      }
    }
  }

  // Acquire every replacement reference before changing a renderable. Once
  // validation and acquisition complete, Filament's setGeometryAt operation
  // is noexcept, so all passes advance together or the caller can take the
  // unchanged full-rebuild fallback.
  std::vector<MeshRenderableProxy> replacement_geometry;
  replacement_geometry.reserve(proxies.size());
  {
    QDR_PROFILE_SCOPE("Mesh renderable acquire topology-stable geometry");
    for (std::size_t index = 0u; index < proxies.size(); ++index) {
      replacement_geometry.emplace_back();
      if (!acquire_geometry(mesh, replacement_geometry.back())) {
        for (MeshRenderableProxy &replacement : replacement_geometry) {
          release_geometry(replacement);
        }
        return false;
      }
    }
  }

  const filament::Box replacement_bounds =
      filament_bounds(compute_local_bounds_impl(mesh));
  {
    QDR_PROFILE_SCOPE("Mesh renderable bind topology-stable geometry");
    for (std::size_t proxy_index = 0u; proxy_index < proxies.size();
         ++proxy_index) {
      MeshRenderableProxy &proxy = *proxies[proxy_index];
      const MeshRenderableProxy &replacement =
          replacement_geometry[proxy_index];
      const filament::RenderableManager::Instance instance =
          renderables.getInstance(proxy.entity);
      for (std::size_t primitive_index = 0u;
           primitive_index < primitive_spans.size(); ++primitive_index) {
        const MeshPrimitiveSpan &primitive = primitive_spans[primitive_index];
        renderables.setGeometryAt(
            instance, primitive_index,
            filament::RenderableManager::PrimitiveType::TRIANGLES,
            replacement.vertex_buffer, replacement.index_buffer,
            primitive.index_offset, primitive.index_count);
      }
      renderables.setAxisAlignedBoundingBox(instance, replacement_bounds);
    }
  }

  for (std::size_t proxy_index = 0u; proxy_index < proxies.size();
       ++proxy_index) {
    MeshRenderableProxy &proxy = *proxies[proxy_index];
    MeshRenderableProxy &replacement = replacement_geometry[proxy_index];
    release_geometry(proxy);
    proxy.mesh = mesh.mesh_id;
    proxy.mesh_revision = mesh.mesh_revision;
    proxy.geometry_resource =
        std::exchange(replacement.geometry_resource, 0u);
    proxy.owns_mesh_buffers = replacement.owns_mesh_buffers;
    proxy.vertex_buffer = std::exchange(replacement.vertex_buffer, nullptr);
    proxy.index_buffer = std::exchange(replacement.index_buffer, nullptr);
    proxy.vertex_count = std::exchange(replacement.vertex_count, 0u);
    proxy.index_count = std::exchange(replacement.index_count, 0u);
  }
  return true;
}

bool MeshRenderableFactory::try_rebind_instance_geometry(
    std::span<const InstanceGeometryRebind> requests) {
  QDR_PROFILE_FUNCTION();
  if (!initialized() || geometry_cache_ == nullptr || requests.empty()) {
    return false;
  }
  auto &renderables = engine_->getRenderableManager();
  std::unordered_set<const MeshRenderableProxy *> unique_proxies;
  unique_proxies.reserve(requests.size());
  for (const InstanceGeometryRebind &request : requests) {
    const MeshRenderableProxy *proxy = request.proxy;
    const RenderMeshAssetSnapshot *mesh = request.mesh;
    if (proxy == nullptr || mesh == nullptr || !mesh->renderable() ||
        !proxy->entity || proxy->dynamic_geometry ||
        proxy->instance_buffer == nullptr || proxy->mesh != mesh->mesh_id ||
        proxy->geometry_resource == 0u || !proxy->owns_mesh_buffers ||
        proxy->vertex_count != mesh->vertices.size() ||
        proxy->index_count != mesh->indices.size() ||
        !proxy->owns_material_instances || proxy->material_slots.size() != 1u ||
        proxy->material_instances.size() != 1u ||
        proxy->material_slots.front() != request.primitive.material_slot ||
        request.primitive.index_count == 0u ||
        static_cast<std::uint64_t>(request.primitive.index_offset) +
                request.primitive.index_count > mesh->indices.size() ||
        !unique_proxies.insert(proxy).second) {
      return false;
    }
    const auto resource = geometry_cache_->resources.find(proxy->geometry_resource);
    if (resource == geometry_cache_->resources.end() ||
        resource->second->vertex_buffer != proxy->vertex_buffer ||
        resource->second->index_buffer != proxy->index_buffer ||
        !same_geometry_topology(*resource->second, *mesh)) {
      return false;
    }
    const auto instance = renderables.getInstance(proxy->entity);
    if (!instance || renderables.getPrimitiveCount(instance) != 1u ||
        renderables.getMaterialInstanceAt(instance, 0u) !=
            proxy->material_instances.front()) {
      return false;
    }
  }

  std::vector<MeshRenderableProxy> replacement_geometry;
  replacement_geometry.reserve(requests.size());
  try {
    for (const InstanceGeometryRebind &request : requests) {
      replacement_geometry.emplace_back();
      if (!acquire_geometry(*request.mesh, replacement_geometry.back())) {
        for (MeshRenderableProxy &replacement : replacement_geometry) {
          release_geometry(replacement);
        }
        return false;
      }
      const auto &replacement = replacement_geometry.back();
      if (!replacement.vertex_buffer->isCreationComplete() ||
          !replacement.index_buffer->isCreationComplete()) {
        for (MeshRenderableProxy &pending : replacement_geometry) {
          release_geometry(pending);
        }
        return false;
      }
      // New immutable payloads can also arrive as one large commit. Submit
      // their buffer commands before the command arena can wrap.
      constexpr std::size_t kGeometryBatchSize = 64u;
      if (replacement_geometry.size() % kGeometryBatchSize == 0u) {
        engine_->flush();
      }
    }
  } catch (...) {
    for (MeshRenderableProxy &replacement : replacement_geometry) {
      release_geometry(replacement);
    }
    throw;
  }

  // All requests and all replacement references are ready before the first
  // renderable changes. Entities, material instances and InstanceBuffers stay.
  for (std::size_t index = 0u; index < requests.size(); ++index) {
    const InstanceGeometryRebind &request = requests[index];
    MeshRenderableProxy &proxy = *request.proxy;
    MeshRenderableProxy &replacement = replacement_geometry[index];
    const auto instance = renderables.getInstance(proxy.entity);
    renderables.setGeometryAt(instance, 0u,
        filament::RenderableManager::PrimitiveType::TRIANGLES,
        replacement.vertex_buffer, replacement.index_buffer,
        request.primitive.index_offset, request.primitive.index_count);
    renderables.setAxisAlignedBoundingBox(instance, filament_bounds(request.world_bounds));
    release_geometry(proxy);
    proxy.mesh_revision = request.mesh->mesh_revision;
    proxy.geometry_resource = std::exchange(replacement.geometry_resource, 0u);
    proxy.vertex_buffer = std::exchange(replacement.vertex_buffer, nullptr);
    proxy.index_buffer = std::exchange(replacement.index_buffer, nullptr);
    proxy.vertex_count = std::exchange(replacement.vertex_count, 0u);
    proxy.index_count = std::exchange(replacement.index_count, 0u);
    constexpr std::size_t kRebindBatchSize = 64u;
    if ((index + 1u) % kRebindBatchSize == 0u) {
      engine_->flush();
    }
  }
  return true;
}

MeshRenderableProxy MeshRenderableFactory::create_shared_instance(
    EntityId entity_id, const RenderMeshAssetSnapshot &mesh,
    const MeshRenderableProxy &prototype, const QMat4 &world_transform,
    std::uint8_t layer_mask, std::uint8_t channel, bool receive_shadows,
    const RenderInstanceBounds *local_bounds_override) {
  QDR_PROFILE_FUNCTION();
  MeshRenderableProxy proxy;
  proxy.owns_mesh_buffers = false;
  MeshRenderableCreationGuard creation_guard(*this, proxy);
  proxy.entity = utils::EntityManager::get().create();
  proxy.source_entity = entity_id;
  proxy.mesh = prototype.mesh;
  proxy.mesh_revision = prototype.mesh_revision;
  proxy.material = prototype.material;
  proxy.material_revision = prototype.material_revision;
  proxy.cast_shadows = prototype.cast_shadows;
  proxy.double_sided = prototype.double_sided;
  proxy.vertex_buffer = prototype.vertex_buffer;
  proxy.index_buffer = prototype.index_buffer;
  proxy.material_slots = prototype.material_slots;
  // Preview siblings share geometry, never occurrence-dependent uniforms.
  proxy.material_instances.reserve(prototype.material_instances.size());
  for (const filament::MaterialInstance *instance : prototype.material_instances) {
    proxy.material_instances.push_back(
        material_binding_->duplicate_mesh_material_instance(instance));
  }

  const std::vector<MeshPrimitiveSpan> primitive_spans =
      coalesced_render_mesh_primitive_spans(mesh);
  filament::RenderableManager::Builder builder(primitive_spans.size());
  for (std::size_t primitive_index = 0;
       primitive_index < primitive_spans.size(); ++primitive_index) {
    const MeshPrimitiveSpan &primitive = primitive_spans[primitive_index];
    builder.geometry(primitive_index,
                     filament::RenderableManager::PrimitiveType::TRIANGLES,
                     proxy.vertex_buffer, proxy.index_buffer,
                     primitive.index_offset, primitive.index_count);
    builder.material(primitive_index,
                     primitive_index < proxy.material_instances.size()
                         ? proxy.material_instances[primitive_index]
                         : nullptr);
  }
  builder
      .boundingBox(filament_bounds(
          local_bounds_override != nullptr
              ? *local_bounds_override
              : compute_local_bounds_impl(mesh)))
      .layerMask(kViewportAllLayerMask, layer_mask)
      .channel(channel)
      .castShadows(proxy.cast_shadows)
      .receiveShadows(receive_shadows)
      .build(*engine_, proxy.entity);

  engine_->getTransformManager().create(proxy.entity);
  update_transform(proxy, world_transform);
  scene_->addEntity(proxy.entity);
  creation_guard.mark_scene_added();
  creation_guard.release();
  return proxy;
}

MeshRenderableProxy MeshRenderableFactory::create_shared_geometry(
    EntityId entity_id, const RenderMeshAssetSnapshot &mesh,
    const RenderMaterialBindingSnapshot *material,
    const MeshRenderableProxy &geometry_source,
    const QMat4 &world_transform, bool double_sided,
    std::uint8_t layer_mask, bool editor_unlit, bool depth_only,
    std::uint8_t channel,
    const RenderInstanceBounds *local_bounds_override) {
  QDR_PROFILE_FUNCTION();
  MeshRenderableProxy proxy;
  proxy.owns_mesh_buffers = false;
  MeshRenderableCreationGuard creation_guard(*this, proxy);
  if (!initialized() || geometry_source.vertex_buffer == nullptr ||
      geometry_source.index_buffer == nullptr) {
    return proxy;
  }

  proxy.entity = utils::EntityManager::get().create();
  proxy.source_entity = entity_id;
  proxy.mesh = geometry_source.mesh;
  proxy.mesh_revision = geometry_source.mesh_revision;
  proxy.material = material ? material->component.material
                            : MaterialReference{};
  proxy.material_revision = material ? material->revision : 0u;
  proxy.double_sided = double_sided;
  proxy.vertex_buffer = geometry_source.vertex_buffer;
  proxy.index_buffer = geometry_source.index_buffer;
  proxy.vertex_count = geometry_source.vertex_count;
  proxy.index_count = geometry_source.index_count;

  const std::vector<MeshPrimitiveSpan> primitive_spans =
      coalesced_render_mesh_primitive_spans(mesh);
  proxy.material_slots.reserve(primitive_spans.size());
  proxy.material_instances.reserve(primitive_spans.size());
  filament::RenderableManager::Builder builder(primitive_spans.size());
  for (std::size_t primitive_index = 0u;
       primitive_index < primitive_spans.size(); ++primitive_index) {
    const MeshPrimitiveSpan &primitive = primitive_spans[primitive_index];
    const RenderMaterialAssetSnapshot *primitive_material =
        material ? material->material_for_slot(primitive.material_slot)
                 : nullptr;
    filament::MaterialInstance *material_instance =
        material_binding_->create_mesh_material_instance(
            primitive_material, double_sided, editor_unlit);
    if (depth_only && material_instance != nullptr) {
      material_instance->setColorWrite(false);
      material_instance->setDepthWrite(true);
      material_instance->setDepthCulling(true);
    }
    proxy.material_slots.push_back(primitive.material_slot);
    proxy.material_instances.push_back(material_instance);
    builder.geometry(primitive_index,
                     filament::RenderableManager::PrimitiveType::TRIANGLES,
                     proxy.vertex_buffer, proxy.index_buffer,
                     primitive.index_offset, primitive.index_count);
    builder.material(primitive_index, material_instance);
  }
  builder
      .boundingBox(filament_bounds(
          local_bounds_override != nullptr
              ? *local_bounds_override
              : compute_local_bounds_impl(mesh)))
      .layerMask(kViewportAllLayerMask, layer_mask)
      .channel(channel)
      .castShadows(false)
      .receiveShadows(!editor_unlit && !depth_only)
      .build(*engine_, proxy.entity);

  engine_->getTransformManager().create(proxy.entity);
  update_transform(proxy, world_transform);
  scene_->addEntity(proxy.entity);
  creation_guard.mark_scene_added();
  creation_guard.release();
  return proxy;
}

MeshRenderableProxy MeshRenderableFactory::create_explicit_instances(
    const RenderMeshAssetSnapshot &mesh,
    const RenderMaterialBindingSnapshot *material,
    MeshPrimitiveSpan primitive, std::span<const QMat4> world_transforms,
    const RenderInstanceBounds &world_bounds, bool cast_shadows,
    bool receive_shadows, bool double_sided, std::uint8_t layer_mask,
    bool editor_unlit, bool depth_only, std::uint8_t channel,
    std::uint8_t presentation_flags, std::size_t instance_capacity) {
  QDR_PROFILE_FUNCTION();
  MeshRenderableProxy proxy;
  instance_capacity = instance_capacity == 0u
                          ? world_transforms.size()
                          : instance_capacity;
  if (!initialized() || world_transforms.empty() ||
      instance_capacity < world_transforms.size() ||
      instance_capacity > engine_->getMaxAutomaticInstances() ||
      instance_capacity > kMeshTextureMappingCapacity ||
      primitive.index_count == 0u ||
      static_cast<std::uint64_t>(primitive.index_offset) +
              primitive.index_count >
          mesh.indices.size()) {
    return proxy;
  }

  MeshRenderableCreationGuard creation_guard(*this, proxy);
  proxy.entity = utils::EntityManager::get().create();
  proxy.mesh = mesh.mesh_id;
  proxy.mesh_revision = mesh.mesh_revision;
  proxy.material =
      material != nullptr ? material->component.material : MaterialReference{};
  proxy.material_revision = material != nullptr ? material->revision : 0u;
  proxy.cast_shadows = cast_shadows;
  proxy.double_sided = double_sided;
  if (!acquire_geometry(mesh, proxy)) {
    return proxy;
  }

  const RenderMaterialAssetSnapshot *primitive_material =
      material != nullptr ? material->material_for_slot(primitive.material_slot)
                          : nullptr;
  filament::MaterialInstance *material_instance =
      material_binding_->create_mesh_material_instance(
          primitive_material, double_sided, editor_unlit);
  if (material_instance == nullptr) {
    return proxy;
  }
  material_binding_->apply_instance_edit_dim(
      *material_instance,
      (presentation_flags & static_cast<std::uint8_t>(
                                RenderInstancePresentationFlag::Dimmed)) !=
          0u);
  if (depth_only) {
    material_instance->setColorWrite(false);
    material_instance->setDepthWrite(true);
    material_instance->setDepthCulling(true);
  }
  proxy.material_slots.push_back(primitive.material_slot);
  proxy.material_instances.push_back(material_instance);

  std::vector<filament::math::mat4f> transforms;
  transforms.reserve(instance_capacity);
  for (const QMat4 &transform : world_transforms) {
    transforms.push_back(make_transform(transform));
  }
  transforms.resize(instance_capacity, make_transform(QMat4{}));
  proxy.instance_buffer =
      filament::InstanceBuffer::Builder(instance_capacity)
          .localTransforms(transforms.data())
          .build(*engine_);

  filament::RenderableManager::Builder(1u)
      .geometry(0u, filament::RenderableManager::PrimitiveType::TRIANGLES,
                proxy.vertex_buffer, proxy.index_buffer,
                primitive.index_offset, primitive.index_count)
      .material(0u, material_instance)
      .instances(world_transforms.size(), proxy.instance_buffer)
      .boundingBox(filament_bounds(world_bounds))
      .layerMask(kViewportAllLayerMask, layer_mask)
      .channel(channel)
      .castShadows(cast_shadows)
      .receiveShadows(receive_shadows)
      .build(*engine_, proxy.entity);
  engine_->getTransformManager().create(proxy.entity);
  scene_->addEntity(proxy.entity);
  creation_guard.mark_scene_added();
  creation_guard.release();
  return proxy;
}

MeshRenderableProxy MeshRenderableFactory::create_instance_selection_fill(
    const RenderMeshAssetSnapshot &mesh, filament::Material &material,
    std::span<const QMat4> world_transforms,
    const RenderInstanceBounds &world_bounds, std::uint32_t color,
    std::uint16_t blend_order, bool active_selected) {
  QDR_PROFILE_FUNCTION();
  MeshRenderableProxy proxy;
  if (!initialized() || world_transforms.empty() || !mesh.renderable() ||
      world_transforms.size() > engine_->getMaxAutomaticInstances()) {
    return proxy;
  }

  MeshRenderableCreationGuard creation_guard(*this, proxy);
  proxy.entity = utils::EntityManager::get().create();
  proxy.mesh = mesh.mesh_id;
  proxy.mesh_revision = mesh.mesh_revision;
  if (!acquire_geometry(mesh, proxy)) {
    return proxy;
  }

  const float inverse_byte = 1.0f / 255.0f;
  const filament::math::float4 fill_color{
      srgb_to_linear(static_cast<float>(color & 0xffu) * inverse_byte),
      srgb_to_linear(static_cast<float>((color >> 8u) & 0xffu) * inverse_byte),
      srgb_to_linear(static_cast<float>((color >> 16u) & 0xffu) * inverse_byte),
      static_cast<float>((color >> 24u) & 0xffu) * inverse_byte,
  };
  const OverlayFaceFillClass fill_class = visible_overlay_face_fill_class(
      active_selected ? OverlayFaceFillClass::ActiveSelection
                      : OverlayFaceFillClass::Selection,
      color);
  const bool stencil_enabled =
      engine_->getBackend() == filament::Engine::Backend::VULKAN;
  for (const OverlayFaceFillPass pass :
       {OverlayFaceFillPass::Mask, OverlayFaceFillPass::Color}) {
    filament::MaterialInstance *fill = material.createInstance();
    if (fill == nullptr) {
      return proxy;
    }
    proxy.material_instances.push_back(fill);
    fill->setParameter("fillColor", fill_color);
    fill->setColorWrite(pass == OverlayFaceFillPass::Color);
    fill->setDepthWrite(false);
    fill->setDepthCulling(true);
    fill->setDepthFunc(filament::MaterialInstance::DepthFunc::GE);
    fill->setCullingMode(filament::MaterialInstance::CullingMode::NONE);
    fill->setTransparencyMode(
        filament::MaterialInstance::TransparencyMode::TWO_PASSES_TWO_SIDES);
    fill->setPolygonOffset(-1.0f, -1.0f);
    apply_overlay_face_fill_stencil(*fill, fill_class, pass, stencil_enabled);
  }

  std::vector<filament::math::mat4f> transforms;
  transforms.reserve(world_transforms.size());
  for (const QMat4 &transform : world_transforms) {
    transforms.push_back(make_transform(transform));
  }
  proxy.instance_buffer =
      filament::InstanceBuffer::Builder(transforms.size())
          .localTransforms(transforms.data())
          .build(*engine_);
  if (proxy.instance_buffer == nullptr) {
    return proxy;
  }
  // Both passes share the retained geometry and the same occurrence transforms.
  filament::RenderableManager::Builder builder(2u);
  for (std::size_t primitive = 0u; primitive < 2u; ++primitive) {
    builder.geometry(primitive,
                     filament::RenderableManager::PrimitiveType::TRIANGLES,
                     proxy.vertex_buffer, proxy.index_buffer, 0u,
                     proxy.index_count)
        .material(primitive, proxy.material_instances[primitive])
        .blendOrder(primitive, primitive == 0u
            ? overlay_face_fill_mask_blend_order() : blend_order)
        .globalBlendOrderEnabled(primitive, true);
  }
  const filament::RenderableManager::Builder::Result result =
      builder.instances(transforms.size(), proxy.instance_buffer)
          .boundingBox(filament_bounds(world_bounds))
          .layerMask(kViewportAllLayerMask, kViewportOverlayLayerMask)
          .channel(kViewportDefaultRenderableChannel)
          .castShadows(false)
          .receiveShadows(false)
          .build(*engine_, proxy.entity);
  if (result != filament::RenderableManager::Builder::Success) {
    return proxy;
  }
  engine_->getTransformManager().create(proxy.entity);
  scene_->addEntity(proxy.entity);
  creation_guard.mark_scene_added();
  creation_guard.release();
  return proxy;
}

MeshRenderableProxy MeshRenderableFactory::create_collision_shape_fill(
    std::shared_ptr<const MeshCollisionGeometryPayload> geometry,
    filament::Material &material, std::span<const QMat4> world_transforms,
    const RenderInstanceBounds &world_bounds,
    const ViewportSettingColor &color, std::uint16_t blend_order) {
  QDR_PROFILE_FUNCTION();
  MeshRenderableProxy proxy;
  if (!initialized() || world_transforms.empty() ||
      world_transforms.size() > engine_->getMaxAutomaticInstances()) {
    return proxy;
  }
  MeshRenderableCreationGuard creation_guard(*this, proxy);
  proxy.entity = utils::EntityManager::get().create();
  if (!acquire_collision_geometry(std::move(geometry), proxy) ||
      !initialize_instance_overlay_fill(proxy, material, world_transforms,
                                        world_bounds, color, blend_order)) {
    return proxy;
  }
  for (filament::MaterialInstance *fill : proxy.material_instances) {
    fill->setCullingMode(filament::MaterialInstance::CullingMode::BACK);
    fill->setTransparencyMode(
        filament::MaterialInstance::TransparencyMode::DEFAULT);
    fill->setPolygonOffset(0.0f, 0.0f);
  }
  scene_->addEntity(proxy.entity);
  creation_guard.mark_scene_added();
  creation_guard.release();
  return proxy;
}

void MeshRenderableFactory::update_collision_shape_fill_color(
    const MeshRenderableProxy &proxy, const ViewportSettingColor &color) {
  const filament::math::float4 fill_color{
      srgb_to_linear(color.r), srgb_to_linear(color.g),
      srgb_to_linear(color.b), color.a};
  for (filament::MaterialInstance *material : proxy.material_instances) {
    if (material != nullptr) {
      material->setParameter("fillColor", fill_color);
    }
  }
}

bool MeshRenderableFactory::initialize_instance_overlay_fill(
    MeshRenderableProxy &proxy, filament::Material &material,
    std::span<const QMat4> world_transforms,
    const RenderInstanceBounds &world_bounds,
    const ViewportSettingColor &color, std::uint16_t blend_order) {
  filament::MaterialInstance *material_instance = material.createInstance();
  if (material_instance == nullptr) {
    return false;
  }
  proxy.material_instances.push_back(material_instance);
  update_collision_shape_fill_color(proxy, color);
  material_instance->setColorWrite(true);
  material_instance->setDepthWrite(false);
  material_instance->setDepthCulling(true);
  material_instance->setDepthFunc(
      filament::MaterialInstance::DepthFunc::GE);
  material_instance->setCullingMode(
      filament::MaterialInstance::CullingMode::NONE);
  material_instance->setTransparencyMode(
      filament::MaterialInstance::TransparencyMode::TWO_PASSES_TWO_SIDES);
  material_instance->setPolygonOffset(-1.0f, -1.0f);

  std::vector<filament::math::mat4f> transforms;
  transforms.reserve(world_transforms.size());
  for (const QMat4 &transform : world_transforms) {
    transforms.push_back(make_transform(transform));
  }
  proxy.instance_buffer =
      filament::InstanceBuffer::Builder(transforms.size())
          .localTransforms(transforms.data())
          .build(*engine_);
  if (proxy.instance_buffer == nullptr) {
    return false;
  }
  const filament::RenderableManager::Builder::Result result =
      filament::RenderableManager::Builder(1u)
      .geometry(0u, filament::RenderableManager::PrimitiveType::TRIANGLES,
                proxy.vertex_buffer, proxy.index_buffer, 0u,
                proxy.index_count)
      .material(0u, material_instance)
      .instances(transforms.size(), proxy.instance_buffer)
      .blendOrder(0u, blend_order)
      .globalBlendOrderEnabled(0u, true)
      .boundingBox(filament_bounds(world_bounds))
      .layerMask(kViewportAllLayerMask, kViewportOverlayLayerMask)
      .channel(kViewportDefaultRenderableChannel)
      .castShadows(false)
      .receiveShadows(false)
      .build(*engine_, proxy.entity);
  if (result != filament::RenderableManager::Builder::Success) {
    return false;
  }
  engine_->getTransformManager().create(proxy.entity);
  return true;
}

bool MeshRenderableFactory::update_explicit_instances(
    const MeshRenderableProxy &proxy,
    std::span<const QMat4> world_transforms,
    const RenderInstanceBounds &world_bounds) {
  QDR_PROFILE_FUNCTION();
  if (!initialized() || proxy.instance_buffer == nullptr || !proxy.entity ||
      world_transforms.empty() ||
      world_transforms.size() > proxy.instance_buffer->getInstanceCount()) {
    return false;
  }
  filament::RenderableManager &renderables = engine_->getRenderableManager();
  const filament::RenderableManager::Instance renderable =
      renderables.getInstance(proxy.entity);
  if (!renderable ||
      renderables.getInstanceCount(renderable) != world_transforms.size()) {
    return false;
  }
  std::vector<filament::math::mat4f> transforms;
  transforms.reserve(world_transforms.size());
  for (const QMat4 &transform : world_transforms) {
    transforms.push_back(make_transform(transform));
  }
  proxy.instance_buffer->setLocalTransforms(transforms.data(),
                                             transforms.size());
  renderables.setAxisAlignedBoundingBox(renderable,
                                        filament_bounds(world_bounds));
  return true;
}

bool MeshRenderableFactory::rebind_explicit_instances(
    const MeshRenderableProxy &proxy, MeshPrimitiveSpan primitive,
    std::span<const QMat4> active_world_transforms,
    const RenderInstanceBounds &world_bounds, bool cast_shadows,
    bool receive_shadows, bool double_sided, std::uint8_t layer_mask,
    bool depth_only, std::uint8_t channel) {
  QDR_PROFILE_FUNCTION();
  if (!initialized() || !proxy.entity || proxy.instance_buffer == nullptr ||
      proxy.vertex_buffer == nullptr || proxy.index_buffer == nullptr ||
      proxy.material_instances.size() != 1u ||
      proxy.material_instances.front() == nullptr ||
      proxy.material_slots.size() != 1u ||
      proxy.material_slots.front() != primitive.material_slot ||
      proxy.cast_shadows != cast_shadows ||
      proxy.double_sided != double_sided ||
      active_world_transforms.size() >
          proxy.instance_buffer->getInstanceCount() ||
      primitive.index_count == 0u ||
      static_cast<std::uint64_t>(primitive.index_offset) +
              primitive.index_count >
          proxy.index_count) {
    return false;
  }

  filament::RenderableManager &renderables = engine_->getRenderableManager();
  const filament::RenderableManager::Instance current =
      renderables.getInstance(proxy.entity);
  if (!current || renderables.getPrimitiveCount(current) != 1u ||
      renderables.getMaterialInstanceAt(current, 0u) !=
          proxy.material_instances.front()) {
    return false;
  }

  if (active_world_transforms.empty()) {
    QDR_PROFILE_SCOPE("Explicit instance rebind hide empty batch");
    renderables.setLayerMask(current, kViewportAllLayerMask, 0u);
    return true;
  }

  std::vector<filament::math::mat4f> transforms;
  transforms.reserve(active_world_transforms.size());
  for (const QMat4 &transform : active_world_transforms) {
    transforms.push_back(make_transform(transform));
  }
  proxy.instance_buffer->setLocalTransforms(transforms.data(),
                                             transforms.size());

  if (renderables.getInstanceCount(current) ==
      active_world_transforms.size()) {
    QDR_PROFILE_SCOPE("Explicit instance update retained active count");
    renderables.setAxisAlignedBoundingBox(current,
                                          filament_bounds(world_bounds));
    renderables.setLayerMask(current, kViewportAllLayerMask, layer_mask);
    renderables.setCastShadows(current, cast_shadows);
    renderables.setReceiveShadows(current,
                                  receive_shadows && !depth_only);
    return true;
  }

  {
    QDR_PROFILE_SCOPE("Explicit instance rebind renderable component");
    scene_->remove(proxy.entity);
    renderables.destroy(proxy.entity);
    const filament::RenderableManager::Builder::Result result =
        filament::RenderableManager::Builder(1u)
            .geometry(0u,
                      filament::RenderableManager::PrimitiveType::TRIANGLES,
                      proxy.vertex_buffer, proxy.index_buffer,
                      primitive.index_offset, primitive.index_count)
            .material(0u, proxy.material_instances.front())
            .instances(active_world_transforms.size(), proxy.instance_buffer)
            .boundingBox(filament_bounds(world_bounds))
            .layerMask(kViewportAllLayerMask, layer_mask)
            .channel(channel)
            .castShadows(cast_shadows)
            .receiveShadows(receive_shadows && !depth_only)
            .build(*engine_, proxy.entity);
    if (result != filament::RenderableManager::Builder::Success) {
      // The proxy still owns every retained resource and its transform/entity
      // components. Leaving only the renderable component detached gives the
      // instance cache one deterministic state to destroy before its
      // immediate full-rebuild fallback.
      return false;
    }
    scene_->addEntity(proxy.entity);
  }
  return true;
}

void MeshRenderableFactory::update_transform(const MeshRenderableProxy &proxy,
                                             const QMat4 &world_transform) {
  QDR_PROFILE_FUNCTION();
  if (!engine_ || !proxy.entity) {
    return;
  }

  auto &transform_manager = engine_->getTransformManager();
  filament::TransformManager::Instance instance =
      transform_manager.getInstance(proxy.entity);
  if (!instance) {
    transform_manager.create(proxy.entity);
    instance = transform_manager.getInstance(proxy.entity);
  }
  transform_manager.setTransform(instance, make_transform(world_transform));
}

void MeshRenderableFactory::update_texture_mapping(
    const MeshRenderableProxy &proxy, const QMat4 &sampling_from_local) {
  update_texture_mappings(proxy, std::span<const QMat4>(&sampling_from_local, 1u));
}

void MeshRenderableFactory::update_texture_mappings(
    const MeshRenderableProxy &proxy,
    std::span<const QMat4> sampling_from_local) {
  for (filament::MaterialInstance *material : proxy.material_instances) {
    if (material != nullptr) {
      bind_mesh_texture_mappings(*material, sampling_from_local);
    }
  }
}

void MeshRenderableFactory::update_layer_mask(
    const MeshRenderableProxy &proxy, std::uint8_t layer_mask) {
  if (engine_ == nullptr || !proxy.entity) {
    return;
  }
  filament::RenderableManager &renderables = engine_->getRenderableManager();
  const filament::RenderableManager::Instance instance =
      renderables.getInstance(proxy.entity);
  if (instance) {
    renderables.setLayerMask(instance, kViewportAllLayerMask, layer_mask);
  }
}

void MeshRenderableFactory::update_bounds(
    const MeshRenderableProxy &proxy, const RenderMeshAssetSnapshot &mesh) {
  if (engine_ == nullptr || !proxy.entity) {
    return;
  }
  filament::RenderableManager &renderables = engine_->getRenderableManager();
  const filament::RenderableManager::Instance instance =
      renderables.getInstance(proxy.entity);
  if (instance) {
    renderables.setAxisAlignedBoundingBox(
        instance, filament_bounds(compute_local_bounds_impl(mesh)));
  }
}

void MeshRenderableFactory::update_bounds(
    const MeshRenderableProxy &proxy, std::span<const QVec3> positions) {
  if (engine_ == nullptr || !proxy.entity || positions.empty()) {
    return;
  }
  filament::RenderableManager &renderables = engine_->getRenderableManager();
  const filament::RenderableManager::Instance instance =
      renderables.getInstance(proxy.entity);
  if (instance) {
    renderables.setAxisAlignedBoundingBox(
        instance, filament_bounds(compute_local_bounds_impl(positions)));
  }
}

void MeshRenderableFactory::update_bounds(
    const MeshRenderableProxy &proxy,
    const RenderInstanceBounds &local_bounds) {
  if (engine_ == nullptr || !proxy.entity) {
    return;
  }
  filament::RenderableManager &renderables = engine_->getRenderableManager();
  const filament::RenderableManager::Instance instance =
      renderables.getInstance(proxy.entity);
  if (instance) {
    renderables.setAxisAlignedBoundingBox(instance,
                                          filament_bounds(local_bounds));
  }
}

void MeshRenderableFactory::update_surface_grid(
    const MeshRenderableProxy &proxy, float grid_world_spacing,
    const ViewportSettings &settings) {
  QDR_PROFILE_FUNCTION();
  if (!material_binding_) {
    return;
  }

  for (filament::MaterialInstance *material_instance :
       proxy.material_instances) {
    if (material_instance) {
      material_binding_->apply_mesh_surface_grid(
          *material_instance, grid_world_spacing, settings);
    }
  }
}

void MeshRenderableFactory::update_shaded_lighting(
    const MeshRenderableProxy &proxy, const ViewportSettings &settings) {
  QDR_PROFILE_FUNCTION();
  if (!material_binding_) {
    return;
  }

  for (filament::MaterialInstance *material_instance :
       proxy.material_instances) {
    if (material_instance) {
      material_binding_->apply_shaded_lighting(*material_instance, settings);
    }
  }
}

void MeshRenderableFactory::remove_from_scene(
    const MeshRenderableProxy &proxy) {
  if (!scene_ || !proxy.entity) {
    return;
  }

  scene_->remove(proxy.entity);
}

void MeshRenderableFactory::remove_from_scene(
    std::span<const utils::Entity> entities) {
  if (!scene_ || entities.empty()) {
    return;
  }
  scene_->removeEntities(entities.data(), entities.size());
}

void MeshRenderableFactory::destroy(MeshRenderableProxy& proxy) {
    if (!engine_) {
        return;
    }

    destroy_renderable_component_resources(*engine_, scene_, *material_binding_,
                                           proxy, true,
                                           pending_resource_retirements_);
    release_geometry(proxy);
}

void MeshRenderableFactory::destroy_detached(MeshRenderableProxy &proxy) {
  if (!engine_) {
    return;
  }

  destroy_renderable_component_resources(*engine_, nullptr, *material_binding_,
                                         proxy, false,
                                         pending_resource_retirements_);
  release_geometry(proxy);
}

void MeshRenderableFactory::destroy_renderable_components(
    std::span<const utils::Entity> entities) {
  if (engine_ == nullptr || entities.empty()) {
    return;
  }
  remove_from_scene(entities);
  auto &renderables = engine_->getRenderableManager();
  for (const utils::Entity entity : entities) {
    if (renderables.hasComponent(entity)) {
      renderables.destroy(entity);
      submit_resource_retirement(*engine_,
                                 pending_resource_retirements_);
    }
  }
}

std::uint64_t
MeshRenderableFactory::mesh_asset_signature(std::uint64_t hash,
                                            const MeshAsset &mesh) {
  return RenderSignatureBuilder::mesh_asset_signature(hash, mesh);
}

} // namespace quader
