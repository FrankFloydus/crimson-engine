////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/mesh_renderable_factory.hpp"

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

#include <cmath>
#include <cstddef>
#include <cstdint>
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

void destroy_mesh_renderable_proxy_resources(filament::Engine &engine,
                                             filament::Scene *scene,
                                             MeshRenderableProxy &proxy,
                                             bool remove_from_scene);

/**
 * Cleans up partially constructed Filament mesh resources until ownership is handed to the proxy.
 */
class MeshRenderableCreationGuard {
public:
    MeshRenderableCreationGuard(filament::Engine& engine, filament::Scene& scene, MeshRenderableProxy& proxy)
        : engine_(engine)
        , scene_(scene)
        , proxy_(proxy) {}

    MeshRenderableCreationGuard(const MeshRenderableCreationGuard&) = delete;
    MeshRenderableCreationGuard& operator=(const MeshRenderableCreationGuard&) = delete;
    MeshRenderableCreationGuard(MeshRenderableCreationGuard&&) = delete;
    MeshRenderableCreationGuard& operator=(MeshRenderableCreationGuard&&) = delete;

    ~MeshRenderableCreationGuard() {
        if (active_) {
          destroy_mesh_renderable_proxy_resources(engine_, &scene_, proxy_,
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

filament::math::float4 tangent_frame_from_normal(QVec3 normal) {
  if (length(normal) <= 0.000001f) {
    normal = {0.0f, 1.0f, 0.0f};
  } else {
    normal = normalize(normal);
  }

  const QVec3 reference = std::fabs(normal.y) < 0.999f
                              ? QVec3{0.0f, 1.0f, 0.0f}
                              : QVec3{1.0f, 0.0f, 0.0f};
  QVec3 tangent = cross(reference, normal);
  if (length(tangent) <= 0.000001f) {
    tangent = {1.0f, 0.0f, 0.0f};
  } else {
    tangent = normalize(tangent);
  }
  const QVec3 bitangent = cross(normal, tangent);
  const filament::math::mat3f tangent_frame{
      to_filament(tangent),
      to_filament(bitangent),
      to_filament(normal),
  };
  const filament::math::quatf packed =
      filament::math::mat3f::packTangentFrame(tangent_frame, sizeof(float));
  return {packed.x, packed.y, packed.z, packed.w};
}

void destroy_mesh_renderable_proxy_resources(filament::Engine &engine,
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
  for (filament::MaterialInstance *&material_instance :
       proxy.material_instances) {
    if (material_instance) {
      engine.destroy(material_instance);
      material_instance = nullptr;
    }
  }
  proxy.material_instances.clear();
  proxy.material_slots.clear();
  if (proxy.vertex_buffer) {
    engine.destroy(proxy.vertex_buffer);
    proxy.vertex_buffer = nullptr;
  }
  if (proxy.index_buffer) {
    engine.destroy(proxy.index_buffer);
    proxy.index_buffer = nullptr;
  }
  if (proxy.entity) {
    utils::EntityManager::get().destroy(proxy.entity);
    proxy.entity.clear();
  }
}

} // namespace

void MeshRenderableFactory::initialize(
    filament::Engine &engine, filament::Scene &scene,
    MaterialBindingService &material_binding) {
  engine_ = &engine;
  scene_ = &scene;
  material_binding_ = &material_binding;
}

void MeshRenderableFactory::shutdown() {
    engine_ = nullptr;
    scene_ = nullptr;
    material_binding_ = nullptr;
}

MeshRenderableProxy MeshRenderableFactory::create(
    EntityId entity_id, AssetId mesh_id, const MeshAsset &mesh,
    const MaterialComponent *material, const AssetRegistry &assets,
    const QMat4 &world_transform, bool cast_shadows, bool double_sided,
    std::uint64_t material_revision, std::uint8_t layer_mask, bool editor_unlit,
    bool depth_only, std::uint8_t channel) {
  QDR_PROFILE_FUNCTION();
  MeshRenderableProxy proxy;
  MeshRenderableCreationGuard creation_guard(*engine_, *scene_, proxy);
  proxy.entity = utils::EntityManager::get().create();
  proxy.source_entity = entity_id;
  proxy.mesh = mesh_id;
  proxy.mesh_revision = mesh.revision.value;
  proxy.material = material ? material->material : kInvalidAssetId;
  proxy.material_revision = material_revision;
  proxy.cast_shadows = cast_shadows;
  proxy.double_sided = double_sided;

  std::vector<RenderVertex> vertices;
  {
    QDR_PROFILE_SCOPE("Mesh renderable build vertices");
    vertices.reserve(mesh.vertices.size());
    for (const MeshVertex &vertex : mesh.vertices) {
      vertices.push_back({
          to_filament(vertex.position),
          tangent_frame_from_normal(vertex.normal),
          {vertex.uv.x, vertex.uv.y},
          vertex.rgba,
      });
    }
  }

  {
    QDR_PROFILE_SCOPE("Mesh renderable upload vertex/index buffers");
    proxy.vertex_buffer =
        filament::VertexBuffer::Builder()
            .vertexCount(static_cast<std::uint32_t>(vertices.size()))
            .bufferCount(1)
            .attribute(filament::VertexAttribute::POSITION, 0,
                       AttributeType::FLOAT3, offsetof(RenderVertex, position),
                       sizeof(RenderVertex))
            .attribute(
                filament::VertexAttribute::TANGENTS, 0, AttributeType::FLOAT4,
                offsetof(RenderVertex, tangent_frame), sizeof(RenderVertex))
            .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2,
                       offsetof(RenderVertex, uv), sizeof(RenderVertex))
            .attribute(filament::VertexAttribute::COLOR, 0,
                       AttributeType::UBYTE4, offsetof(RenderVertex, color),
                       sizeof(RenderVertex))
            .normalized(filament::VertexAttribute::COLOR)
            .build(*engine_);
    proxy.vertex_buffer->setBufferAt(
        *engine_, 0, FilamentBufferDescriptorFactory::vector(vertices));

    proxy.index_buffer =
        filament::IndexBuffer::Builder()
            .indexCount(static_cast<std::uint32_t>(mesh.indices.size()))
            .bufferType(filament::IndexBuffer::IndexType::UINT)
            .build(*engine_);
    proxy.index_buffer->setBuffer(
        *engine_, FilamentBufferDescriptorFactory::vector(mesh.indices));
  }

  const std::vector<MeshPrimitiveSpan> primitive_spans =
      coalesced_mesh_primitive_spans(mesh);
  proxy.material_slots.reserve(primitive_spans.size());
  proxy.material_instances.reserve(primitive_spans.size());
  filament::RenderableManager::Builder builder(primitive_spans.size());
  {
    QDR_PROFILE_SCOPE("Mesh renderable build primitives");
    for (std::size_t primitive_index = 0;
         primitive_index < primitive_spans.size(); ++primitive_index) {
      const MeshPrimitiveSpan &primitive = primitive_spans[primitive_index];
      const AssetId primitive_material_id =
          material ? material->material_for_slot(primitive.material_slot)
                   : kInvalidAssetId;
      const MaterialDocument *primitive_material =
          assets.material(primitive_material_id);
      filament::MaterialInstance *material_instance =
          material_binding_->create_mesh_material_instance(
              assets, primitive_material, double_sided, editor_unlit);
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

    destroy_mesh_renderable_proxy_resources(*engine_, scene_, proxy, true);
}

void MeshRenderableFactory::destroy_detached(MeshRenderableProxy &proxy) {
  if (!engine_) {
    return;
  }

  destroy_mesh_renderable_proxy_resources(*engine_, nullptr, proxy, false);
}

std::uint64_t
MeshRenderableFactory::mesh_asset_signature(std::uint64_t hash,
                                            const MeshAsset &mesh) {
  return RenderSignatureBuilder::mesh_asset_signature(hash, mesh);
}

} // namespace quader
