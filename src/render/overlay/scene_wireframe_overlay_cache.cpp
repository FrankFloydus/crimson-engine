////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/scene_wireframe_overlay_cache.hpp"

#include "assets/asset_registry.hpp"
#include "core/color_space.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "mesh/polygon/document.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/filament_utils.hpp"
#include "render/render_layers.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/overlay/overlay_mesh_types.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_document.hpp"

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
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

constexpr float kSceneWireframeLineNearPlane = 0.05f;

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

bool append_authored_polygon_wireframe_lines(
    const MeshAsset &mesh, std::uint32_t color,
    std::vector<SceneWireframeLineVertex> &vertices,
    std::vector<std::uint32_t> &indices) {
  if (!mesh.has_authored_polygon_document || !mesh.authored_polygon_document) {
    return false;
  }

  const quader_poly::Document &document = *mesh.authored_polygon_document;
  std::unordered_map<quader_poly::ElementId, QVec3> positions;
  positions.reserve(document.vertices.size());
  for (const quader_poly::Vertex &vertex : document.vertices) {
    positions.emplace(vertex.id, vertex.position);
  }

  std::unordered_set<std::uint64_t> emitted_edges;
  for (const quader_poly::Edge edge : quader_poly::document_edges(document)) {
    const auto a = positions.find(edge.a);
    const auto b = positions.find(edge.b);
    if (a == positions.end() || b == positions.end()) {
      continue;
    }
    const std::uint64_t key = edge_key(static_cast<std::uint32_t>(edge.a),
                                       static_cast<std::uint32_t>(edge.b));
    if (!emitted_edges.insert(key).second) {
      continue;
    }
    append_wireframe_line_quad(vertices, indices, a->second, b->second, color);
  }
  return !emitted_edges.empty();
}

void append_triangle_mesh_wireframe_lines(
    const MeshAsset &mesh, std::uint32_t color,
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
             const MeshAsset &bounds_mesh, std::uint8_t layer_mask) {
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
  proxy.material_instance->setParameter(
      "lineWidth", viewport_overlay_line_width_pixels(layer, 0.0f));
  proxy.material_instance->setParameter("featherPixels",
                                        kOverlayLineFeatherPixels);
  proxy.material_instance->setParameter("nearPlane",
                                        kSceneWireframeLineNearPlane);

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
      .boundingBox(compute_bounds(bounds_mesh))
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
    synced_scene_revision_ = 0;
    synced_asset_revision_ = 0;
    layer_mask_ = 0u;
    has_synced_ = false;
    engine_ = nullptr;
    scene_ = nullptr;
    line_material_ = nullptr;
}

void SceneWireframeOverlayCache::sync(const SceneDocument &document,
                                      const AssetRegistry &assets,
                                      std::uint8_t layer_mask) {
  QDR_PROFILE_FUNCTION();
  if (!initialized()) {
    return;
  }

  set_layer_mask(layer_mask);
  if (layer_mask == 0u) {
    return;
  }

  const std::uint64_t scene_revision = document.revision();
  const std::uint64_t asset_revision = assets.revision();
  if (has_synced_ && synced_scene_revision_ == scene_revision &&
      synced_asset_revision_ == asset_revision) {
    return;
  }

  const std::vector<WireframeEntityState> states =
      wireframe_entity_states(document, assets);
  if (has_synced_ && synced_asset_revision_ == asset_revision &&
      states == synced_states_) {
    update_entry_transforms(document);
    synced_scene_revision_ = scene_revision;
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
        existing->second.selected != state.selected;
    if (rebuild) {
      if (existing != entries_.end()) {
        destroy_entry(existing->second);
        entries_.erase(existing);
      }
      Entry entry = create_entry(state.entity, state.mesh_id, state.selected,
                                 document, assets);
      existing = entries_.emplace(state.entity, std::move(entry)).first;
    }
    update_entry_transform(existing->second, document, state.entity);
  }

  remove_except(live_entities);
  synced_states_ = states;
  synced_scene_revision_ = scene_revision;
  synced_asset_revision_ = asset_revision;
  has_synced_ = true;
}

std::vector<SceneWireframeOverlayCache::WireframeEntityState>
SceneWireframeOverlayCache::wireframe_entity_states(
    const SceneDocument &document, const AssetRegistry &assets) const {
  QDR_PROFILE_FUNCTION();
  std::vector<WireframeEntityState> states;
  const std::vector<EntityId> entities = document.entities();
  states.reserve(entities.size());
  for (const EntityId entity_id : entities) {
    if (!document.is_effectively_visible(entity_id)) {
      continue;
    }
    const MeshComponent *mesh_component = document.mesh(entity_id);
    if (mesh_component == nullptr || mesh_component->mesh == kInvalidAssetId) {
      continue;
    }
    const MeshAsset *mesh = assets.mesh(mesh_component->mesh);
    if (mesh == nullptr || mesh->vertices.empty() ||
        mesh->indices.size() < 3u) {
      continue;
    }
    states.push_back({
        .entity = entity_id,
        .mesh_id = mesh_component->mesh,
        .mesh_revision = mesh->revision.value,
        .selected =
            [&]() {
              const SelectionComponent *selection =
                  document.selection(entity_id);
              return selection != nullptr && selection->selected;
            }(),
    });
  }
  return states;
}

SceneWireframeOverlayCache::Entry SceneWireframeOverlayCache::create_entry(
    EntityId entity_id, AssetId mesh_id, bool selected,
    const SceneDocument &document, const AssetRegistry &assets) {
  QDR_PROFILE_FUNCTION();
  Entry entry;
  entry.mesh_id = mesh_id;
  const MeshAsset *mesh = assets.mesh(mesh_id);
  if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.size() < 3u) {
    return entry;
  }
  entry.mesh_revision = mesh->revision.value;
  entry.selected = selected;
  entry.transform_root = utils::EntityManager::get().create();
  filament::TransformManager &transforms = engine_->getTransformManager();
  transforms.create(entry.transform_root);
  const filament::TransformManager::Instance parent_transform =
      transforms.getInstance(entry.transform_root);

  const ViewportOverlayLayer layer = selected
                                         ? ViewportOverlayLayer::SelectedEdge
                                         : ViewportOverlayLayer::WireframeXray;
  const std::uint32_t color =
      selected ? kOverlaySelectionWireColor : kOverlayWireframeXrayColor;
  std::vector<SceneWireframeLineVertex> line_vertices;
  std::vector<std::uint32_t> line_indices;
  if (!append_authored_polygon_wireframe_lines(*mesh, color, line_vertices,
                                               line_indices)) {
    append_triangle_mesh_wireframe_lines(*mesh, color, line_vertices,
                                         line_indices);
  }
  entry.line_proxy =
      create_proxy(*engine_, *scene_, *line_material_, parent_transform, layer,
                   line_vertices, line_indices, *mesh, layer_mask_);
  update_entry_transform(entry, document, entity_id);
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

void SceneWireframeOverlayCache::update_entry_transform(
    const Entry &entry, const SceneDocument &document,
    EntityId entity_id) const {
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
  transforms.setTransform(
      instance, make_transform(document.world_transform_matrix(entity_id)));
}

void SceneWireframeOverlayCache::update_entry_transforms(
    const SceneDocument &document) {
  QDR_PROFILE_FUNCTION();
  if (!engine_) {
    return;
  }
  filament::TransformManager &transforms = engine_->getTransformManager();
  transforms.openLocalTransformTransaction();
  for (const WireframeEntityState &state : synced_states_) {
    const auto existing = entries_.find(state.entity);
    if (existing != entries_.end()) {
      update_entry_transform(existing->second, document, state.entity);
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
