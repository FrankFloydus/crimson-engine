////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/mesh_renderable_factory.hpp"

#include "core/color_space.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/filament_utils.hpp"
#include "render/material_binding_service.hpp"
#include "render/render_signature_builder.hpp"

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
  const filament::math::quatf packed =
      filament::math::mat3f::packTangentFrame(tangent_frame, sizeof(float));
  return {packed.x, packed.y, packed.z, packed.w};
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
build_render_vertices(const RenderMeshAssetSnapshot &mesh) {
  const std::vector<filament::math::float4> tangent_frames =
      mesh_tangent_frames(mesh);
  std::vector<RenderVertex> vertices;
  vertices.reserve(mesh.vertices.size());
  for (std::size_t index = 0u; index < mesh.vertices.size(); ++index) {
    const MeshVertex &vertex = mesh.vertices[index];
    vertices.push_back({
        to_filament(vertex.position),
        tangent_frames[index],
        {vertex.uv.x, vertex.uv.y},
        vertex.rgba,
    });
  }
  return vertices;
}

filament::Box compute_bounds(const RenderMeshAssetSnapshot &mesh) {
  if (mesh.vertices.empty()) {
    return {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
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

  filament::Box box;
  box.set(to_filament(min_value), to_filament(max_value));
  return box;
}

bool same_vertex(const MeshVertex &left, const MeshVertex &right) {
  return left.position.x == right.position.x &&
         left.position.y == right.position.y &&
         left.position.z == right.position.z &&
         left.normal.x == right.normal.x && left.normal.y == right.normal.y &&
         left.normal.z == right.normal.z && left.uv.x == right.uv.x &&
         left.uv.y == right.uv.y && left.rgba == right.rgba;
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
        .add_value(vertex.rgba);
  }
  signature.add_size(mesh.indices.size());
  for (const std::uint32_t index : mesh.indices) {
    signature.add_value(index);
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

void destroy_renderable_component_resources(filament::Engine &engine,
                                            filament::Scene *scene,
                                            MeshRenderableProxy &proxy,
                                            bool remove_from_scene) {
  if (proxy.entity) {
    if (remove_from_scene && scene != nullptr) {
      scene->remove(proxy.entity);
    }
    engine.getRenderableManager().destroy(proxy.entity);
    engine.getTransformManager().destroy(proxy.entity);
  }
  if (proxy.instance_buffer) {
    engine.destroy(proxy.instance_buffer);
    proxy.instance_buffer = nullptr;
  }
  if (proxy.owns_material_instances) {
    for (filament::MaterialInstance *&material_instance :
         proxy.material_instances) {
      if (material_instance) {
        engine.destroy(material_instance);
        material_instance = nullptr;
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

/// Owns shared Filament vertex/index buffers keyed by immutable geometry.
struct MeshRenderableFactory::GeometryCacheState final {
  /// Pins one geometry payload and all GPU buffers derived from it.
  struct Resource final {
    std::uint64_t id = 0u;
    std::uint64_t signature = 0u;
    std::shared_ptr<const MeshGeometryPayload> payload;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
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
        }
        if (resource->index_buffer != nullptr) {
          engine_->destroy(resource->index_buffer);
        }
      }
    }
    geometry_cache_.reset();
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
      const bool same_pinned_payload =
          candidate.payload != nullptr && mesh.geometry_payload != nullptr &&
          (candidate.payload == mesh.geometry_payload ||
           (candidate.payload->vertices.size() == mesh.vertices.size() &&
            candidate.payload->indices.size() == mesh.indices.size() &&
            std::equal(candidate.payload->vertices.begin(),
                       candidate.payload->vertices.end(),
                       mesh.vertices.begin(), same_vertex) &&
            std::equal(candidate.payload->indices.begin(),
                       candidate.payload->indices.end(),
                       mesh.indices.begin())));
      const bool same_fallback_payload =
          candidate.payload == nullptr && mesh.geometry_payload == nullptr &&
          candidate.vertices.size() == mesh.vertices.size() &&
          candidate.indices.size() == mesh.indices.size() &&
          std::equal(candidate.vertices.begin(), candidate.vertices.end(),
                     mesh.vertices.begin(), same_vertex) &&
          std::equal(candidate.indices.begin(), candidate.indices.end(),
                     mesh.indices.begin());
      if (!same_pinned_payload && !same_fallback_payload) {
        continue;
      }
      ++resource->second->reference_count;
      ++geometry_cache_->stats.cache_hit_count;
      proxy.geometry_resource = resource_id;
      proxy.vertex_buffer = candidate.vertex_buffer;
      proxy.index_buffer = candidate.index_buffer;
      proxy.owns_mesh_buffers = true;
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
          .attribute(filament::VertexAttribute::COLOR, 0,
                     AttributeType::UBYTE4, offsetof(RenderVertex, color),
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

void MeshRenderableFactory::release_geometry(MeshRenderableProxy &proxy) {
  if (proxy.geometry_resource == 0u || geometry_cache_ == nullptr) {
    if (engine_ != nullptr && proxy.owns_mesh_buffers) {
      if (proxy.vertex_buffer != nullptr) {
        engine_->destroy(proxy.vertex_buffer);
      }
      if (proxy.index_buffer != nullptr) {
        engine_->destroy(proxy.index_buffer);
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
    std::uint8_t channel) {
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
    if (!acquire_geometry(mesh, proxy)) {
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
    builder.boundingBox(compute_bounds(mesh))
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

MeshRenderableProxy MeshRenderableFactory::create_shared_instance(
    EntityId entity_id, const RenderMeshAssetSnapshot &mesh,
    const MeshRenderableProxy &prototype, const QMat4 &world_transform,
    std::uint8_t layer_mask, std::uint8_t channel) {
  QDR_PROFILE_FUNCTION();
  MeshRenderableProxy proxy;
  proxy.owns_mesh_buffers = false;
  proxy.owns_material_instances = false;
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
  proxy.material_instances = prototype.material_instances;

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
  builder.boundingBox(compute_bounds(mesh))
      .layerMask(kViewportAllLayerMask, layer_mask)
      .channel(channel)
      .castShadows(proxy.cast_shadows)
      .receiveShadows(false)
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
    std::uint8_t presentation_flags) {
  QDR_PROFILE_FUNCTION();
  MeshRenderableProxy proxy;
  if (!initialized() || world_transforms.empty() ||
      world_transforms.size() > engine_->getMaxAutomaticInstances() ||
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
  transforms.reserve(world_transforms.size());
  for (const QMat4 &transform : world_transforms) {
    transforms.push_back(make_transform(transform));
  }
  proxy.instance_buffer =
      filament::InstanceBuffer::Builder(transforms.size())
          .localTransforms(transforms.data())
          .build(*engine_);

  filament::RenderableManager::Builder(1u)
      .geometry(0u, filament::RenderableManager::PrimitiveType::TRIANGLES,
                proxy.vertex_buffer, proxy.index_buffer,
                primitive.index_offset, primitive.index_count)
      .material(0u, material_instance)
      .instances(transforms.size(), proxy.instance_buffer)
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
    std::uint16_t blend_order) {
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

  filament::MaterialInstance *material_instance = material.createInstance();
  if (material_instance == nullptr) {
    return proxy;
  }
  const float inverse_byte = 1.0f / 255.0f;
  const float red = srgb_to_linear(
      static_cast<float>(color & 0xffu) * inverse_byte);
  const float green = srgb_to_linear(
      static_cast<float>((color >> 8u) & 0xffu) * inverse_byte);
  const float blue = srgb_to_linear(
      static_cast<float>((color >> 16u) & 0xffu) * inverse_byte);
  const float alpha =
      static_cast<float>((color >> 24u) & 0xffu) * inverse_byte;
  material_instance->setParameter(
      "fillColor", filament::math::float4{red, green, blue, alpha});
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
  proxy.material_instances.push_back(material_instance);

  std::vector<filament::math::mat4f> transforms;
  transforms.reserve(world_transforms.size());
  for (const QMat4 &transform : world_transforms) {
    transforms.push_back(make_transform(transform));
  }
  proxy.instance_buffer =
      filament::InstanceBuffer::Builder(transforms.size())
          .localTransforms(transforms.data())
          .build(*engine_);

  filament::RenderableManager::Builder(1u)
      .geometry(0u, filament::RenderableManager::PrimitiveType::TRIANGLES,
                proxy.vertex_buffer, proxy.index_buffer, 0u,
                static_cast<std::uint32_t>(mesh.indices.size()))
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
  engine_->getTransformManager().create(proxy.entity);
  scene_->addEntity(proxy.entity);
  creation_guard.mark_scene_added();
  creation_guard.release();
  return proxy;
}

bool MeshRenderableFactory::update_explicit_instances(
    const MeshRenderableProxy &proxy,
    std::span<const QMat4> world_transforms,
    const RenderInstanceBounds &world_bounds) {
  QDR_PROFILE_FUNCTION();
  if (!initialized() || proxy.instance_buffer == nullptr || !proxy.entity ||
      proxy.instance_buffer->getInstanceCount() != world_transforms.size()) {
    return false;
  }
  std::vector<filament::math::mat4f> transforms;
  transforms.reserve(world_transforms.size());
  for (const QMat4 &transform : world_transforms) {
    transforms.push_back(make_transform(transform));
  }
  proxy.instance_buffer->setLocalTransforms(transforms.data(),
                                             transforms.size());
  filament::RenderableManager &renderables = engine_->getRenderableManager();
  const filament::RenderableManager::Instance renderable =
      renderables.getInstance(proxy.entity);
  if (!renderable) {
    return false;
  }
  renderables.setAxisAlignedBoundingBox(renderable,
                                        filament_bounds(world_bounds));
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

void MeshRenderableFactory::remove_from_scene(
    const MeshRenderableProxy &proxy) {
  if (!scene_ || !proxy.entity) {
    return;
  }

  scene_->remove(proxy.entity);
}

void MeshRenderableFactory::destroy(MeshRenderableProxy& proxy) {
    if (!engine_) {
        return;
    }

    destroy_renderable_component_resources(*engine_, scene_, proxy, true);
    release_geometry(proxy);
}

void MeshRenderableFactory::destroy_detached(MeshRenderableProxy &proxy) {
  if (!engine_) {
    return;
  }

  destroy_renderable_component_resources(*engine_, nullptr, proxy, false);
  release_geometry(proxy);
}

std::uint64_t
MeshRenderableFactory::mesh_asset_signature(std::uint64_t hash,
                                            const MeshAsset &mesh) {
  return RenderSignatureBuilder::mesh_asset_signature(hash, mesh);
}

} // namespace quader
