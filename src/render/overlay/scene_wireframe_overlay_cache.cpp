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
#include "mesh/polygon/document_model.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/filament_utils.hpp"
#include "render/render_layers.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/overlay/overlay_mesh_types.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <backend/DriverEnums.h>
#include <filament/Engine.h>
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
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

/**
 * Stores one shader-expanded persistent wireframe line vertex.
 */
struct SceneWireframeLineVertex {
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

std::uint64_t edge_key(std::uint32_t a, std::uint32_t b) {
  const std::uint32_t lo = std::min(a, b);
  const std::uint32_t hi = std::max(a, b);
  return (static_cast<std::uint64_t>(lo) << 32u) |
         static_cast<std::uint64_t>(hi);
}

void append_wireframe_line_quad(std::vector<SceneWireframeLineVertex> &vertices,
                                std::vector<std::uint32_t> &indices,
                                QVec3 start, QVec3 end,
                                std::uint32_t color_rgba) {
  const std::uint32_t color = rgba8_srgb_to_linear(color_rgba);
  const std::uint32_t offset = static_cast<std::uint32_t>(vertices.size());
  const OverlayFloat4 start_value{start.x, start.y, start.z, 1.0f};
  const OverlayFloat4 end_value{end.x, end.y, end.z, 1.0f};
  const OverlayFloat4 device_origin{0.0f, 0.0f, 0.0f, 1.0f};
  vertices.push_back({device_origin,
                      start_value,
                      end_value,
                      {-1.0f, -1.0f, 0.0f, 0.0f},
                      color});
  vertices.push_back({device_origin,
                      start_value,
                      end_value,
                      {-1.0f, 1.0f, 1.0f, 0.0f},
                      color});
  vertices.push_back(
      {device_origin, start_value, end_value, {1.0f, 1.0f, 1.0f, 0.0f}, color});
  vertices.push_back({device_origin,
                      start_value,
                      end_value,
                      {1.0f, -1.0f, 0.0f, 0.0f},
                      color});
  indices.push_back(offset);
  indices.push_back(offset + 1u);
  indices.push_back(offset + 2u);
  indices.push_back(offset);
  indices.push_back(offset + 2u);
  indices.push_back(offset + 3u);
}

const RenderOverlayMeshSnapshot *find_wireframe_mesh_snapshot(
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

bool append_authored_polygon_wireframe_lines(
    const RenderOverlayMeshSnapshot &mesh, std::uint32_t color,
    std::vector<SceneWireframeLineVertex> &vertices,
    std::vector<std::uint32_t> &indices) {
  if (!mesh.has_authored_polygon_geometry()) {
    return false;
  }

  std::unordered_map<quader_poly::ElementId, QVec3> positions;
  positions.reserve(mesh.authored_vertices.size());
  for (const quader_poly::Vertex &vertex : mesh.authored_vertices) {
    positions.emplace(vertex.id, vertex.position);
  }

  std::unordered_set<std::uint64_t> emitted_edges;
  for (const quader_poly::Face &face : mesh.authored_faces) {
    if (face.vertices.size() < 2u) {
      continue;
    }
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
      const quader_poly::ElementId a_id = face.vertices[index];
      const quader_poly::ElementId b_id =
          face.vertices[(index + 1u) % face.vertices.size()];
      const std::uint64_t key = edge_key(static_cast<std::uint32_t>(a_id),
                                         static_cast<std::uint32_t>(b_id));
      if (!emitted_edges.insert(key).second) {
        continue;
      }
      const auto a = positions.find(a_id);
      const auto b = positions.find(b_id);
      if (a == positions.end() || b == positions.end()) {
        continue;
      }
      append_wireframe_line_quad(vertices, indices, a->second, b->second,
                                 color);
    }
  }
  return !emitted_edges.empty();
}

void append_triangle_mesh_wireframe_lines(
    const RenderOverlayMeshSnapshot &mesh, std::uint32_t color,
    std::vector<SceneWireframeLineVertex> &vertices,
    std::vector<std::uint32_t> &indices) {
  std::unordered_set<std::uint64_t> emitted_edges;
  emitted_edges.reserve(mesh.indices.size());
  auto append_edge = [&](std::uint32_t a, std::uint32_t b) {
    if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || a == b) {
      return;
    }
    if (!emitted_edges.insert(edge_key(a, b)).second) {
      return;
    }
    append_wireframe_line_quad(vertices, indices, mesh.vertices[a].position,
                               mesh.vertices[b].position, color);
  };

  for (std::size_t index = 0; index + 2u < mesh.indices.size(); index += 3u) {
    append_edge(mesh.indices[index], mesh.indices[index + 1u]);
    append_edge(mesh.indices[index + 1u], mesh.indices[index + 2u]);
    append_edge(mesh.indices[index + 2u], mesh.indices[index]);
  }
}

SceneWireframeOverlayCache::Proxy
create_proxy(filament::Engine &engine, filament::Scene &scene,
             filament::Material &material,
             filament::TransformManager::Instance parent_transform,
             ViewportOverlayLayer layer,
             const std::vector<SceneWireframeLineVertex> &vertices,
             const std::vector<std::uint32_t> &indices,
             const filament::Box &bounds, std::uint8_t layer_mask,
             ViewportCameraClipPlanes clip_planes,
             float line_width_pixels) {
  SceneWireframeOverlayCache::Proxy proxy;
  if (vertices.empty() || indices.empty()) {
    return proxy;
  }

  const std::uint16_t blend_order =
      static_cast<std::uint16_t>(viewport_overlay_draw_order(layer) * 64);
  const OverlayRenderState state = OverlayRenderableBuilder::make_state(
      layer, ViewportOverlayGeometryKind::Lines, blend_order);

  proxy.entity = utils::EntityManager::get().create();
  proxy.vertex_count = static_cast<std::uint32_t>(vertices.size());
  proxy.index_count = static_cast<std::uint32_t>(indices.size());
  proxy.material_instance = material.createInstance();
  configure_overlay_material(*proxy.material_instance, state);
  proxy.material_instance->setParameter("lineWidth", line_width_pixels);
  proxy.material_instance->setParameter("featherPixels",
                                        kOverlayLineFeatherPixels);
  proxy.material_instance->setParameter("nearPlane", clip_planes.near_plane);

  proxy.vertex_buffer =
      filament::VertexBuffer::Builder()
          .vertexCount(proxy.vertex_count)
          .bufferCount(1)
          .attribute(filament::VertexAttribute::POSITION, 0,
                     AttributeType::FLOAT4,
                     offsetof(SceneWireframeLineVertex, position),
                     sizeof(SceneWireframeLineVertex))
          .attribute(filament::VertexAttribute::CUSTOM0, 0,
                     AttributeType::FLOAT4,
                     offsetof(SceneWireframeLineVertex, start),
                     sizeof(SceneWireframeLineVertex))
          .attribute(filament::VertexAttribute::CUSTOM1, 0,
                     AttributeType::FLOAT4,
                     offsetof(SceneWireframeLineVertex, end),
                     sizeof(SceneWireframeLineVertex))
          .attribute(filament::VertexAttribute::CUSTOM2, 0,
                     AttributeType::FLOAT4,
                     offsetof(SceneWireframeLineVertex, control),
                     sizeof(SceneWireframeLineVertex))
          .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4,
                     offsetof(SceneWireframeLineVertex, color),
                     sizeof(SceneWireframeLineVertex))
          .normalized(filament::VertexAttribute::COLOR)
          .build(engine);
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

SceneWireframeOverlayCache::Proxy::Proxy(Proxy &&other) noexcept
    : entity(other.entity),
      vertex_buffer(std::exchange(other.vertex_buffer, nullptr)),
      index_buffer(std::exchange(other.index_buffer, nullptr)),
      material_instance(std::exchange(other.material_instance, nullptr)),
      vertex_count(std::exchange(other.vertex_count, 0u)),
      index_count(std::exchange(other.index_count, 0u)) {
  other.entity.clear();
}

SceneWireframeOverlayCache::Proxy& SceneWireframeOverlayCache::Proxy::operator=(Proxy&& other) noexcept {
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

SceneWireframeOverlayCache::~SceneWireframeOverlayCache() {
    shutdown();
}

void SceneWireframeOverlayCache::initialize(filament::Engine &engine,
                                            filament::Scene &scene,
                                            filament::Material &line_material) {
  engine_ = &engine;
  scene_ = &scene;
  line_material_ = &line_material;
}

void SceneWireframeOverlayCache::shutdown() {
    if (!engine_) {
        return;
    }

    for (auto& [entityId, entry] : entries_) {
        (void)entityId;
        destroy_entry(entry);
    }
    entries_.clear();
    synced_states_.clear();
    synced_scene_binding_ = {};
    synced_scene_revision_ = 0;
    synced_asset_revision_ = 0;
    synced_wire_style_signature_ = 0;
    synced_clip_planes_ = {};
    layer_mask_ = 0u;
    has_synced_ = false;
    engine_ = nullptr;
    scene_ = nullptr;
    line_material_ = nullptr;
}

void SceneWireframeOverlayCache::sync(
    const RenderOverlaySceneWireframeSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  if (!initialized()) {
    return;
  }

  if (has_synced_ && synced_scene_binding_ != snapshot.scene_binding) {
    for (auto &[entity_id, entry] : entries_) {
      (void)entity_id;
      destroy_entry(entry);
    }
    entries_.clear();
    synced_states_.clear();
    synced_scene_binding_ = {};
    synced_scene_revision_ = 0;
    synced_asset_revision_ = 0;
    synced_wire_style_signature_ = 0;
    has_synced_ = false;
  }

  set_layer_mask(snapshot.layer_mask);
  if (snapshot.layer_mask == 0u) {
    return;
  }
  const bool clip_planes_changed =
      !has_synced_ || synced_clip_planes_ != snapshot.clip_planes;
  if (clip_planes_changed) {
    set_proxy_clip_planes(snapshot.clip_planes);
  }
  synced_clip_planes_ = snapshot.clip_planes;
  const bool wire_style_changed = scene_wireframe_style_changed(
      has_synced_, synced_wire_style_signature_, snapshot.style);

  if (has_synced_ && synced_scene_binding_ == snapshot.scene_binding &&
      synced_scene_revision_ == snapshot.scene_revision &&
      synced_asset_revision_ == snapshot.asset_revision &&
      !clip_planes_changed && !wire_style_changed) {
    return;
  }

  const std::vector<WireframeEntityState> states =
      wireframe_entity_states(snapshot);
  if (has_synced_ && synced_scene_binding_ == snapshot.scene_binding &&
      !wire_style_changed &&
      synced_asset_revision_ == snapshot.asset_revision &&
      states == synced_states_) {
    update_entry_transforms(snapshot);
    synced_scene_binding_ = snapshot.scene_binding;
    synced_scene_revision_ = snapshot.scene_revision;
    return;
  }

  std::unordered_set<EntityId> live_entities;
  live_entities.reserve(states.size());
  for (const WireframeEntityState &state : states) {
    QDR_PROFILE_SCOPE("Scene wireframe overlay sync entity");
    live_entities.insert(state.entity);
    auto existing = entries_.find(state.entity);
    const bool rebuild =
        existing == entries_.end() ||
        existing->second.mesh_id != state.mesh_id ||
        existing->second.mesh_revision != state.mesh_revision ||
        existing->second.selected != state.selected ||
        (state.selected && wire_style_changed);
    if (rebuild) {
      if (existing != entries_.end()) {
        destroy_entry(existing->second);
        entries_.erase(existing);
      }
      const RenderOverlayMeshSnapshot *mesh =
          find_wireframe_mesh_snapshot(snapshot.meshes, state.entity);
      if (mesh == nullptr) {
        continue;
      }
      Entry entry = create_entry(*mesh, snapshot.style);
      existing = entries_.emplace(state.entity, std::move(entry)).first;
    }
    const RenderOverlayMeshSnapshot *mesh =
        find_wireframe_mesh_snapshot(snapshot.meshes, state.entity);
    if (mesh != nullptr) {
      update_entry_transform(existing->second, mesh->world_transform);
    }
  }

  remove_except(live_entities);
  synced_states_ = states;
  synced_scene_binding_ = snapshot.scene_binding;
  synced_scene_revision_ = snapshot.scene_revision;
  synced_asset_revision_ = snapshot.asset_revision;
  synced_wire_style_signature_ = snapshot.style.wire_signature;
  has_synced_ = true;
}

std::vector<SceneWireframeOverlayCache::WireframeEntityState>
SceneWireframeOverlayCache::wireframe_entity_states(
    const RenderOverlaySceneWireframeSnapshot &snapshot) const {
  QDR_PROFILE_FUNCTION();
  std::vector<WireframeEntityState> states;
  states.reserve(snapshot.meshes.size());
  for (const RenderOverlayMeshSnapshot &mesh : snapshot.meshes) {
    if (!mesh.has_triangle_geometry() || mesh.mesh_id == kInvalidAssetId) {
      continue;
    }
    states.push_back({
        .entity = mesh.entity,
        .mesh_id = mesh.mesh_id,
        .mesh_revision = mesh.mesh_revision,
        .selected = mesh.selected,
    });
  }
  return states;
}

SceneWireframeOverlayCache::Entry SceneWireframeOverlayCache::create_entry(
    const RenderOverlayMeshSnapshot &mesh,
    const ViewportPersistentOverlayStyleSnapshot &style) {
  QDR_PROFILE_FUNCTION();
  Entry entry;
  entry.mesh_id = mesh.mesh_id;
  if (!mesh.has_triangle_geometry()) {
    return entry;
  }
  entry.mesh_revision = mesh.mesh_revision;
  entry.selected = mesh.selected;
  entry.transform_root = utils::EntityManager::get().create();
  filament::TransformManager &transforms = engine_->getTransformManager();
  transforms.create(entry.transform_root);
  const filament::TransformManager::Instance parent_transform =
      transforms.getInstance(entry.transform_root);

  const ViewportOverlayLayer layer = mesh.selected
                                         ? ViewportOverlayLayer::SelectedEdge
                                         : ViewportOverlayLayer::WireframeXray;
  const std::uint32_t color =
      mesh.selected ? style.selection_wire_color : kOverlayWireframeXrayColor;
  const float line_width_pixels =
      mesh.selected
          ? style.selection_edge_line_width_pixels
          : viewport_overlay_line_width_pixels(layer, 0.0f);
  std::vector<SceneWireframeLineVertex> line_vertices;
  std::vector<std::uint32_t> line_indices;
  if (!append_authored_polygon_wireframe_lines(mesh, color, line_vertices,
                                               line_indices)) {
    append_triangle_mesh_wireframe_lines(mesh, color, line_vertices,
                                         line_indices);
  }
  const filament::Box bounds = bounds_for_mesh_vertices(mesh.vertices);
  entry.line_proxy =
      create_proxy(*engine_, *scene_, *line_material_, parent_transform, layer,
                   line_vertices, line_indices, bounds, layer_mask_,
                   synced_clip_planes_, line_width_pixels);
  update_entry_transform(entry, mesh.world_transform);
  return entry;
}

void SceneWireframeOverlayCache::destroy_entry(Entry &entry) {
  destroy_proxy(entry.line_proxy, true);
  destroy_transform_root(entry);
}

void SceneWireframeOverlayCache::destroy_proxy(Proxy &proxy,
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

void SceneWireframeOverlayCache::destroy_transform_root(Entry &entry) {
  if (!engine_ || !entry.transform_root) {
    return;
  }
  engine_->getTransformManager().destroy(entry.transform_root);
  utils::EntityManager::get().destroy(entry.transform_root);
  entry.transform_root.clear();
}

void SceneWireframeOverlayCache::set_proxy_layer_mask(
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

void SceneWireframeOverlayCache::set_layer_mask(std::uint8_t layer_mask) {
  if (layer_mask_ == layer_mask) {
    return;
  }
  layer_mask_ = layer_mask;
  for (auto &[entityId, entry] : entries_) {
    (void)entityId;
    set_proxy_layer_mask(entry.line_proxy, layer_mask_);
  }
}

void SceneWireframeOverlayCache::set_proxy_clip_planes(
    ViewportCameraClipPlanes clip_planes) {
  for (auto &[entityId, entry] : entries_) {
    (void)entityId;
    if (entry.line_proxy.material_instance == nullptr) {
      continue;
    }
    entry.line_proxy.material_instance->setParameter(
        "nearPlane", clip_planes.near_plane);
  }
}

void SceneWireframeOverlayCache::update_entry_transform(
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

void SceneWireframeOverlayCache::update_entry_transforms(
    const RenderOverlaySceneWireframeSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  if (!engine_) {
    return;
  }
  filament::TransformManager &transforms = engine_->getTransformManager();
  transforms.openLocalTransformTransaction();
  for (const WireframeEntityState &state : synced_states_) {
    const auto existing = entries_.find(state.entity);
    if (existing != entries_.end()) {
      const RenderOverlayMeshSnapshot *mesh =
          find_wireframe_mesh_snapshot(snapshot.meshes, state.entity);
      if (mesh != nullptr) {
        update_entry_transform(existing->second, mesh->world_transform);
      }
    }
  }
  transforms.commitLocalTransformTransaction();
}

void SceneWireframeOverlayCache::remove_except(
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
