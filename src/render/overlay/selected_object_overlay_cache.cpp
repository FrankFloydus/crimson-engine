////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/selected_object_overlay_cache.hpp"

#include "core/color_space.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/filament_utils.hpp"
#include "render/render_layers.hpp"
#include "render/overlay/overlay_mesh_types.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <backend/DriverEnums.h>
#include <filament/Engine.h>
#include <filament/Box.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

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

const RenderOverlayMeshSnapshot *find_overlay_mesh_snapshot(
    const std::vector<RenderOverlayMeshSnapshot> &meshes, EntityId entity) {
  const auto found =
      std::find_if(meshes.begin(), meshes.end(),
                   [entity](const RenderOverlayMeshSnapshot &mesh) {
                     return mesh.entity == entity;
                   });
  return found == meshes.end() ? nullptr : &*found;
}

filament::Box bounds_for_mesh_vertices(std::span<const MeshVertex> vertices) {
  if (vertices.empty()) {
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
  for (const MeshVertex &vertex : vertices) {
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

std::vector<OverlayFaceFillVertex>
selected_face_vertices(std::span<const MeshVertex> mesh_vertices,
                       std::uint32_t selection_face_color) {
  std::vector<OverlayFaceFillVertex> result;
  result.reserve(mesh_vertices.size());
  const std::uint32_t color =
      rgba8_srgb_to_linear(selection_face_color);
  for (const MeshVertex &vertex : mesh_vertices) {
    result.push_back(
        {{vertex.position.x, vertex.position.y, vertex.position.z}, {}, color});
  }
  return result;
}

std::vector<std::uint32_t> valid_triangle_indices(
    std::span<const MeshVertex> vertices,
    std::span<const std::uint32_t> indices_source) {
  std::vector<std::uint32_t> indices;
  indices.reserve(indices_source.size());
  for (std::size_t index = 0u; index + 2u < indices_source.size();
       index += 3u) {
    const std::uint32_t a = indices_source[index];
    const std::uint32_t b = indices_source[index + 1u];
    const std::uint32_t c = indices_source[index + 2u];
    if (a < vertices.size() && b < vertices.size() && c < vertices.size()) {
      indices.push_back(a);
      indices.push_back(b);
      indices.push_back(c);
    }
  }
  return indices;
}

template <typename Vertex, typename ConfigureAttributes,
          typename ConfigureMaterial>
SelectedObjectOverlayCache::Proxy
create_proxy(filament::Engine &engine, filament::Scene &scene,
             filament::Material &material,
             filament::TransformManager::Instance parent_transform,
             const std::vector<Vertex> &vertices,
             const std::vector<std::uint32_t> &indices,
             const filament::Box &bounds, const OverlayRenderState &state,
             std::uint8_t layer_mask, ConfigureAttributes configure_attributes,
             ConfigureMaterial configure_material) {
  SelectedObjectOverlayCache::Proxy proxy;
  if (vertices.empty() || indices.empty()) {
    return proxy;
  }

  proxy.entity = utils::EntityManager::get().create();
  proxy.vertex_count = static_cast<std::uint32_t>(vertices.size());
  proxy.index_count = static_cast<std::uint32_t>(indices.size());
  proxy.material_instance = material.createInstance();
  configure_material(*proxy.material_instance);

  filament::VertexBuffer::Builder vertex_builder;
  vertex_builder.vertexCount(proxy.vertex_count).bufferCount(1);
  configure_attributes(vertex_builder);
  proxy.vertex_buffer = vertex_builder.build(engine);
  proxy.vertex_buffer->setBufferAt(
      engine, 0, FilamentBufferDescriptorFactory::vector(vertices));

  proxy.index_buffer = filament::IndexBuffer::Builder()
                           .indexCount(proxy.index_count)
                           .bufferType(filament::IndexBuffer::IndexType::UINT)
                           .build(engine);
  proxy.index_buffer->setBuffer(
      engine, FilamentBufferDescriptorFactory::vector(indices));

  filament::RenderableManager::Builder(1)
      .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES,
                proxy.vertex_buffer, proxy.index_buffer)
      .material(0, proxy.material_instance)
      .blendOrder(0, state.blend_order)
      .globalBlendOrderEnabled(0, true)
      .layerMask(kViewportAllLayerMask, layer_mask)
      .boundingBox(bounds)
      .culling(false)
      .castShadows(false)
      .receiveShadows(false)
      .build(engine, proxy.entity);
  engine.getTransformManager().create(proxy.entity, parent_transform);
  scene.addEntity(proxy.entity);
  return proxy;
}

} // namespace

SelectedObjectOverlayCache::Proxy::Proxy(Proxy &&other) noexcept
    : entity(other.entity),
      vertex_buffer(std::exchange(other.vertex_buffer, nullptr)),
      index_buffer(std::exchange(other.index_buffer, nullptr)),
      material_instance(std::exchange(other.material_instance, nullptr)),
      vertex_count(std::exchange(other.vertex_count, 0u)),
      index_count(std::exchange(other.index_count, 0u)) {
  other.entity.clear();
}

SelectedObjectOverlayCache::Proxy& SelectedObjectOverlayCache::Proxy::operator=(Proxy&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    entity = other.entity;
    other.entity.clear();
    vertex_buffer = std::exchange(other.vertex_buffer, nullptr);
    index_buffer = std::exchange(other.index_buffer, nullptr);
    material_instance = std::exchange(other.material_instance, nullptr);
    vertex_count = std::exchange(other.vertex_count, 0u);
    index_count = std::exchange(other.index_count, 0u);
    return *this;
}

SelectedObjectOverlayCache::~SelectedObjectOverlayCache() {
    shutdown();
}

void SelectedObjectOverlayCache::initialize(
    filament::Engine &engine, filament::Scene &scene,
    filament::Material &face_fill_material) {
  engine_ = &engine;
  scene_ = &scene;
  face_fill_material_ = &face_fill_material;
}

void SelectedObjectOverlayCache::shutdown() {
    if (!engine_) {
        return;
    }

    destroy_face_entries();
    synced_states_.clear();
    synced_source_stamp_ = {};
    synced_face_style_signature_ = 0;
    synced_face_fills_enabled_ = false;
    has_synced_ = false;
    engine_ = nullptr;
    scene_ = nullptr;
    face_fill_material_ = nullptr;
    visible_ = true;
}

void SelectedObjectOverlayCache::sync(
    const RenderOverlaySelectedObjectSnapshot &snapshot) {
    QDR_PROFILE_FUNCTION();
    if (!initialized()) {
        return;
    }

    if (has_synced_ && !render_source_dependencies_match(
                           synced_source_stamp_, snapshot,
                           RenderSourceDependency::SceneBinding)) {
      destroy_face_entries();
      synced_states_.clear();
      synced_source_stamp_ = {};
      synced_face_style_signature_ = 0;
      synced_face_fills_enabled_ = false;
      has_synced_ = false;
    }

    if (!snapshot.visible) {
      set_visible(false);
      return;
    }
    set_visible(true);
    const bool face_style_changed = selected_object_face_style_changed(
        has_synced_, synced_face_style_signature_, snapshot.style);
    if (has_synced_ && render_source_dependencies_match(
                           synced_source_stamp_, snapshot,
                           kRenderOverlaySelectedObjectSourceDependencies) &&
        !face_style_changed) {
      if (!render_source_dependencies_match(
              synced_source_stamp_, snapshot,
              RenderSourceDependency::TransformRevision)) {
        if (synced_face_fills_enabled_) {
          update_entry_transforms(snapshot);
        }
      }
      synced_source_stamp_ = snapshot;
      return;
    }

    const std::vector<SelectedEntityState> states =
        selected_entity_states(snapshot);
    const bool face_fills_enabled =
        selected_object_face_fills_enabled_for_selection_count(states.size());
    if (has_synced_ && render_source_dependencies_match(
                           synced_source_stamp_, snapshot,
                           RenderSourceDependency::SceneBinding) &&
        !face_style_changed &&
        synced_face_fills_enabled_ == face_fills_enabled &&
        states == synced_states_) {
      if (!render_source_dependencies_match(
              synced_source_stamp_, snapshot,
              RenderSourceDependency::TransformRevision)) {
        if (synced_face_fills_enabled_) {
          update_entry_transforms(snapshot);
        }
      }
      synced_source_stamp_ = snapshot;
      synced_face_style_signature_ = snapshot.style.face_signature;
      return;
    }

    if (face_fills_enabled) {
      std::unordered_set<EntityId> live_entities;
      live_entities.reserve(states.size());
      for (const SelectedEntityState &state : states) {
        QDR_PROFILE_SCOPE("Selected object overlay sync entity");
        live_entities.insert(state.entity);
        auto existing = entries_.find(state.entity);
        const bool rebuild =
            existing == entries_.end() ||
            existing->second.mesh_id != state.mesh_id ||
            existing->second.mesh_revision != state.mesh_revision;
        if (rebuild) {
          if (existing != entries_.end()) {
            destroy_entry(existing->second);
            entries_.erase(existing);
          }
          const RenderOverlayMeshSnapshot *mesh =
              find_overlay_mesh_snapshot(snapshot.meshes, state.entity);
          if (mesh == nullptr) {
            continue;
          }
          Entry entry = create_entry(*mesh, snapshot.style);
          existing = entries_.emplace(state.entity, std::move(entry)).first;
        }
        const RenderOverlayMeshSnapshot *mesh =
            find_overlay_mesh_snapshot(snapshot.meshes, state.entity);
        if (mesh != nullptr) {
          update_entry_face_color(existing->second, *mesh, snapshot.style);
          update_entry_transform(existing->second, mesh->world_transform);
        }
      }
      remove_except(live_entities);
    } else {
      destroy_face_entries();
    }

    synced_states_ = states;
    synced_source_stamp_ = snapshot;
    synced_face_style_signature_ = snapshot.style.face_signature;
    synced_face_fills_enabled_ = face_fills_enabled;
    has_synced_ = true;
}

std::vector<SelectedObjectOverlayCache::SelectedEntityState>
SelectedObjectOverlayCache::selected_entity_states(
    const RenderOverlaySelectedObjectSnapshot &snapshot) const {
  QDR_PROFILE_FUNCTION();
  std::vector<SelectedEntityState> states;
  states.reserve(snapshot.meshes.size());
  for (const RenderOverlayMeshSnapshot &mesh : snapshot.meshes) {
    if (!mesh.has_triangle_geometry() || mesh.mesh_id == kInvalidAssetId) {
      continue;
    }
    states.push_back({
        .entity = mesh.entity,
        .mesh_id = mesh.mesh_id,
        .mesh_revision = mesh.mesh_revision,
        .active_selected = mesh.active_selected,
    });
  }
  return states;
}

SelectedObjectOverlayCache::Entry
SelectedObjectOverlayCache::create_entry(
    const RenderOverlayMeshSnapshot &mesh,
    const ViewportPersistentOverlayStyleSnapshot &style) {
  QDR_PROFILE_FUNCTION();
  Entry entry;
  entry.mesh_id = mesh.mesh_id;
  if (!mesh.has_triangle_geometry()) {
    return entry;
  }
  entry.mesh_revision = mesh.mesh_revision;
  entry.face_color = mesh.active_selected ? style.active_selection_face_color
                                          : style.selection_face_color;
  entry.transform_root = utils::EntityManager::get().create();
  filament::TransformManager &transforms = engine_->getTransformManager();
  transforms.create(entry.transform_root);
  const filament::TransformManager::Instance parent_transform =
      transforms.getInstance(entry.transform_root);

  const std::uint8_t layer_mask = visible_ ? kViewportOverlayLayerMask : 0u;
  std::vector<OverlayFaceFillVertex> face_vertices =
      selected_face_vertices(mesh.vertices, entry.face_color);
  std::vector<std::uint32_t> face_indices =
      valid_triangle_indices(mesh.vertices, mesh.indices);
  if (!face_vertices.empty() && !face_indices.empty()) {
    const filament::Box bounds = bounds_for_mesh_vertices(mesh.vertices);
    for (const ViewportOverlayLayer layer : {
             ViewportOverlayLayer::SelectedXrayFace,
             ViewportOverlayLayer::SelectedFaceMask,
         }) {
      std::uint16_t blend_order =
          static_cast<std::uint16_t>(viewport_overlay_draw_order(layer) * 64);
      const OverlayRenderState depth_state =
          OverlayRenderableBuilder::make_state(
              layer, ViewportOverlayGeometryKind::Triangles, blend_order++,
              true, false);
      entry.face_proxies.push_back(create_proxy(
          *engine_, *scene_, *face_fill_material_, parent_transform,
          face_vertices, face_indices, bounds, depth_state, layer_mask,
          [](filament::VertexBuffer::Builder &builder) {
            builder
                .attribute(filament::VertexAttribute::POSITION, 0,
                           AttributeType::FLOAT3,
                           offsetof(OverlayFaceFillVertex, position),
                           sizeof(OverlayFaceFillVertex))
                .attribute(filament::VertexAttribute::UV0, 0,
                           AttributeType::FLOAT2,
                           offsetof(OverlayFaceFillVertex, uv),
                           sizeof(OverlayFaceFillVertex))
                .attribute(filament::VertexAttribute::COLOR, 0,
                           AttributeType::UBYTE4,
                           offsetof(OverlayFaceFillVertex, color),
                           sizeof(OverlayFaceFillVertex))
                .normalized(filament::VertexAttribute::COLOR);
          },
          [&depth_state](filament::MaterialInstance &material) {
            configure_overlay_material(material, depth_state);
          }));

      const OverlayRenderState color_state =
          OverlayRenderableBuilder::make_state(
              layer, ViewportOverlayGeometryKind::Triangles, blend_order++,
              false, true);
      entry.face_proxies.push_back(create_proxy(
          *engine_, *scene_, *face_fill_material_, parent_transform,
          face_vertices, face_indices, bounds, color_state, layer_mask,
          [](filament::VertexBuffer::Builder &builder) {
            builder
                .attribute(filament::VertexAttribute::POSITION, 0,
                           AttributeType::FLOAT3,
                           offsetof(OverlayFaceFillVertex, position),
                           sizeof(OverlayFaceFillVertex))
                .attribute(filament::VertexAttribute::UV0, 0,
                           AttributeType::FLOAT2,
                           offsetof(OverlayFaceFillVertex, uv),
                           sizeof(OverlayFaceFillVertex))
                .attribute(filament::VertexAttribute::COLOR, 0,
                           AttributeType::UBYTE4,
                           offsetof(OverlayFaceFillVertex, color),
                           sizeof(OverlayFaceFillVertex))
                .normalized(filament::VertexAttribute::COLOR);
          },
          [&color_state](filament::MaterialInstance &material) {
            configure_overlay_material(material, color_state);
          }));
    }
  }

  update_entry_transform(entry, mesh.world_transform);
  return entry;
}

void SelectedObjectOverlayCache::update_entry_face_color(
    Entry &entry, const RenderOverlayMeshSnapshot &mesh,
    const ViewportPersistentOverlayStyleSnapshot &style) {
  const auto color = mesh.active_selected ? style.active_selection_face_color
                                          : style.selection_face_color;
  if (entry.face_color == color) return;
  const auto vertices = selected_face_vertices(mesh.vertices, color);
  for (const auto &proxy : entry.face_proxies) {
    if (proxy.vertex_buffer != nullptr) {
      proxy.vertex_buffer->setBufferAt(
          *engine_, 0, FilamentBufferDescriptorFactory::vector(vertices));
    }
  }
  entry.face_color = color;
}

void SelectedObjectOverlayCache::destroy_entry(Entry &entry) {
  for (Proxy &proxy : entry.face_proxies) {
    destroy_proxy(proxy, true);
  }
  entry.face_proxies.clear();
  destroy_transform_root(entry);
}

void SelectedObjectOverlayCache::destroy_proxy(Proxy &proxy,
                                               bool remove_from_scene) {
  if (!engine_) {
    return;
  }

  if (proxy.entity) {
    if (remove_from_scene && scene_ != nullptr) {
      scene_->remove(proxy.entity);
    }
    engine_->getRenderableManager().destroy(proxy.entity);
    engine_->getTransformManager().destroy(proxy.entity);
  }
  if (proxy.material_instance) {
    engine_->destroy(proxy.material_instance);
    proxy.material_instance = nullptr;
  }
  if (proxy.vertex_buffer) {
    engine_->destroy(proxy.vertex_buffer);
    proxy.vertex_buffer = nullptr;
  }
  if (proxy.index_buffer) {
    engine_->destroy(proxy.index_buffer);
    proxy.index_buffer = nullptr;
  }
  if (proxy.entity) {
    utils::EntityManager::get().destroy(proxy.entity);
    proxy.entity.clear();
  }
  proxy.vertex_count = 0u;
  proxy.index_count = 0u;
}

void SelectedObjectOverlayCache::destroy_face_entries() {
  for (auto &[entityId, entry] : entries_) {
    (void)entityId;
    destroy_entry(entry);
  }
  entries_.clear();
}

void SelectedObjectOverlayCache::destroy_transform_root(Entry &entry) {
  if (!engine_ || !entry.transform_root) {
    return;
  }
  engine_->getTransformManager().destroy(entry.transform_root);
  utils::EntityManager::get().destroy(entry.transform_root);
  entry.transform_root.clear();
}

void SelectedObjectOverlayCache::set_proxy_layer_mask(
    const Proxy &proxy, std::uint8_t layer_mask) const {
  if (!engine_ || !proxy.entity) {
    return;
  }
  filament::RenderableManager &renderables = engine_->getRenderableManager();
  const filament::RenderableManager::Instance instance =
      renderables.getInstance(proxy.entity);
  if (!instance) {
    return;
  }
  renderables.setLayerMask(instance, kViewportAllLayerMask, layer_mask);
}

void SelectedObjectOverlayCache::update_entry_transform(
    const Entry &entry, const QMat4 &world_transform) const {
  if (!engine_ || !entry.transform_root) {
    return;
  }
  filament::TransformManager &transforms = engine_->getTransformManager();
  filament::TransformManager::Instance instance =
      transforms.getInstance(entry.transform_root);
  if (!instance) {
    transforms.create(entry.transform_root);
    instance = transforms.getInstance(entry.transform_root);
  }
  transforms.setTransform(instance, make_transform(world_transform));
}

void SelectedObjectOverlayCache::update_entry_transforms(
    const RenderOverlaySelectedObjectSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  if (!engine_) {
    return;
  }
  filament::TransformManager &transforms = engine_->getTransformManager();
  transforms.openLocalTransformTransaction();
  for (const SelectedEntityState &state : synced_states_) {
    const auto existing = entries_.find(state.entity);
    if (existing != entries_.end()) {
      const RenderOverlayMeshSnapshot *mesh =
          find_overlay_mesh_snapshot(snapshot.meshes, state.entity);
      if (mesh != nullptr) {
        update_entry_transform(existing->second, mesh->world_transform);
      }
    }
  }
  transforms.commitLocalTransformTransaction();
}

void SelectedObjectOverlayCache::set_visible(bool visible) {
  if (visible_ == visible) {
    return;
  }
  visible_ = visible;
  const std::uint8_t layer_mask = visible_ ? kViewportOverlayLayerMask : 0u;
  for (auto &[entityId, entry] : entries_) {
    (void)entityId;
    for (const Proxy &proxy : entry.face_proxies) {
      set_proxy_layer_mask(proxy, layer_mask);
    }
  }
}

void SelectedObjectOverlayCache::remove_except(
    const std::unordered_set<EntityId> &live_entities) {
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (!live_entities.contains(it->first)) {
      destroy_entry(it->second);
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace quader
