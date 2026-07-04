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
#include "mesh/polygon/document_model.hpp"
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

constexpr float kSelectedObjectLineNearPlane = 0.05f;

/**
 * Stores one shader-expanded selected object line vertex.
 */
struct SelectedObjectLineVertex {
    OverlayFloat4 position;
    OverlayFloat4 start;
    OverlayFloat4 end;
    OverlayFloat4 control;
    std::uint32_t color = 0u;
};

/**
 * Stores one fallback triangle edge collected from a mesh asset.
 */
struct TriangleEdgeRecord {
    std::uint32_t a = 0u;
    std::uint32_t b = 0u;
    std::uint32_t count = 0u;
    QVec3 first_normal;
    bool coplanar_with_first = true;
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

void append_selected_line_quad(
    std::vector<SelectedObjectLineVertex> &vertices,
    std::vector<std::uint32_t> &indices, QVec3 start, QVec3 end,
    std::uint32_t color_rgba = kOverlaySelectionWireColor) {
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

filament::Box bounds_for_selected_line_vertices(
    const std::vector<SelectedObjectLineVertex> &vertices) {
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
  auto include = [&](const OverlayFloat4 &point) {
    min_value.x = std::min(min_value.x, point.x);
    min_value.y = std::min(min_value.y, point.y);
    min_value.z = std::min(min_value.z, point.z);
    max_value.x = std::max(max_value.x, point.x);
    max_value.y = std::max(max_value.y, point.y);
    max_value.z = std::max(max_value.z, point.z);
  };

  for (const SelectedObjectLineVertex &vertex : vertices) {
    include(vertex.start);
    include(vertex.end);
  }
  if (min_value.x == max_value.x) {
    min_value.x -= 0.5f;
    max_value.x += 0.5f;
  }
  if (min_value.y == max_value.y) {
    min_value.y -= 0.5f;
    max_value.y += 0.5f;
  }
  if (min_value.z == max_value.z) {
    min_value.z -= 0.5f;
    max_value.z += 0.5f;
  }

  filament::Box box;
  box.set(to_filament(min_value), to_filament(max_value));
  return box;
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
selected_face_vertices(std::span<const MeshVertex> mesh_vertices) {
  std::vector<OverlayFaceFillVertex> result;
  result.reserve(mesh_vertices.size());
  const std::uint32_t color = rgba8_srgb_to_linear(kOverlaySelectionFaceColor);
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

bool selected_object_face_fills_enabled_for_selection_count(
    std::size_t selected_mesh_count) {
  return selected_mesh_count > 0u &&
         selected_mesh_count <= kSelectedObjectFaceFillSelectionLimit;
}

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
    filament::Material &face_fill_material, filament::Material &line_material) {
  engine_ = &engine;
  scene_ = &scene;
  face_fill_material_ = &face_fill_material;
  line_material_ = &line_material;
}

void SelectedObjectOverlayCache::shutdown() {
    if (!engine_) {
        return;
    }

    destroy_proxy(line_proxy_, true);
    destroy_line_batch_root();
    destroy_face_entries();
    line_entries_.clear();
    synced_states_.clear();
    synced_scene_revision_ = 0;
    synced_asset_revision_ = 0;
    synced_selection_revision_ = 0;
    synced_visibility_revision_ = 0;
    synced_transform_revision_ = 0;
    synced_mesh_binding_revision_ = 0;
    synced_face_fills_enabled_ = false;
    has_synced_ = false;
    engine_ = nullptr;
    scene_ = nullptr;
    face_fill_material_ = nullptr;
    line_material_ = nullptr;
    visible_ = true;
}

void SelectedObjectOverlayCache::sync(
    const RenderOverlaySelectedObjectSnapshot &snapshot) {
    QDR_PROFILE_FUNCTION();
    if (!initialized()) {
        return;
    }

    if (!snapshot.visible) {
      set_visible(false);
      return;
    }
    set_visible(true);

    if (has_synced_ && synced_asset_revision_ == snapshot.asset_revision &&
        synced_selection_revision_ == snapshot.selection_revision &&
        synced_visibility_revision_ == snapshot.visibility_revision &&
        synced_mesh_binding_revision_ == snapshot.mesh_binding_revision) {
      if (synced_transform_revision_ != snapshot.transform_revision) {
        update_line_proxy(synced_states_, snapshot);
        if (synced_face_fills_enabled_) {
          update_entry_transforms(snapshot);
        }
        synced_transform_revision_ = snapshot.transform_revision;
      }
      synced_scene_revision_ = snapshot.scene_revision;
      return;
    }

    const std::vector<SelectedEntityState> states =
        selected_entity_states(snapshot);
    const bool face_fills_enabled =
        selected_object_face_fills_enabled_for_selection_count(states.size());
    if (has_synced_ && synced_face_fills_enabled_ == face_fills_enabled &&
        states == synced_states_) {
      if (synced_transform_revision_ != snapshot.transform_revision) {
        update_line_proxy(synced_states_, snapshot);
        if (synced_face_fills_enabled_) {
          update_entry_transforms(snapshot);
        }
      }
      synced_scene_revision_ = snapshot.scene_revision;
      synced_asset_revision_ = snapshot.asset_revision;
      synced_selection_revision_ = snapshot.selection_revision;
      synced_visibility_revision_ = snapshot.visibility_revision;
      synced_transform_revision_ = snapshot.transform_revision;
      synced_mesh_binding_revision_ = snapshot.mesh_binding_revision;
      return;
    }

    sync_line_entries(states, snapshot);
    update_line_proxy(states, snapshot);

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
          Entry entry = create_entry(*mesh);
          existing = entries_.emplace(state.entity, std::move(entry)).first;
        }
        const RenderOverlayMeshSnapshot *mesh =
            find_overlay_mesh_snapshot(snapshot.meshes, state.entity);
        if (mesh != nullptr) {
          update_entry_transform(existing->second, mesh->world_transform);
        }
      }
      remove_except(live_entities);
    } else {
      destroy_face_entries();
    }

    synced_states_ = states;
    synced_scene_revision_ = snapshot.scene_revision;
    synced_asset_revision_ = snapshot.asset_revision;
    synced_selection_revision_ = snapshot.selection_revision;
    synced_visibility_revision_ = snapshot.visibility_revision;
    synced_transform_revision_ = snapshot.transform_revision;
    synced_mesh_binding_revision_ = snapshot.mesh_binding_revision;
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
    });
  }
  return states;
}

SelectedObjectOverlayCache::LineEntry
SelectedObjectOverlayCache::create_line_entry(
    const RenderOverlayMeshSnapshot &mesh) const {
  QDR_PROFILE_FUNCTION();
  LineEntry entry;
  entry.mesh_id = mesh.mesh_id;
  if (!mesh.has_triangle_geometry()) {
    return entry;
  }
  entry.mesh_revision = mesh.mesh_revision;

  if (mesh.has_authored_polygon_geometry()) {
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
        if (!emitted_edges
                 .insert(edge_key(static_cast<std::uint32_t>(a_id),
                                  static_cast<std::uint32_t>(b_id)))
                 .second) {
          continue;
        }
        const auto a = positions.find(a_id);
        const auto b = positions.find(b_id);
        if (a == positions.end() || b == positions.end()) {
          continue;
        }
        entry.segments.push_back({.start = a->second, .end = b->second});
      }
    }
  }

  if (!entry.segments.empty()) {
    return entry;
  }

  std::unordered_map<std::uint64_t, TriangleEdgeRecord> edges;
  edges.reserve(mesh.indices.size());

  auto append_edge = [&](std::uint32_t a, std::uint32_t b, QVec3 normal) {
    if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || a == b) {
      return;
    }
    TriangleEdgeRecord &record = edges[edge_key(a, b)];
    if (record.count == 0u) {
      record.a = a;
      record.b = b;
      record.first_normal = normal;
    } else if (length(record.first_normal) > 0.000001f &&
               length(normal) > 0.000001f) {
      record.coplanar_with_first = record.coplanar_with_first &&
                                   std::abs(dot(normalize(record.first_normal),
                                                normalize(normal))) > 0.999f;
    }
    ++record.count;
  };

  for (std::size_t index = 0u; index + 2u < mesh.indices.size(); index += 3u) {
    const std::uint32_t a = mesh.indices[index];
    const std::uint32_t b = mesh.indices[index + 1u];
    const std::uint32_t c = mesh.indices[index + 2u];
    if (a >= mesh.vertices.size() || b >= mesh.vertices.size() ||
        c >= mesh.vertices.size()) {
      continue;
    }
    QVec3 normal =
        cross(mesh.vertices[b].position - mesh.vertices[a].position,
              mesh.vertices[c].position - mesh.vertices[a].position);
    if (length(normal) > 0.000001f) {
      normal = normalize(normal);
    }
    append_edge(a, b, normal);
    append_edge(b, c, normal);
    append_edge(c, a, normal);
  }

  entry.segments.reserve(edges.size());
  for (const auto &[key, edge] : edges) {
    (void)key;
    if (edge.count == 2u && edge.coplanar_with_first) {
      continue;
    }
    entry.segments.push_back({
        .start = mesh.vertices[edge.a].position,
        .end = mesh.vertices[edge.b].position,
    });
  }
  return entry;
}

void SelectedObjectOverlayCache::sync_line_entries(
    const std::vector<SelectedEntityState> &states,
    const RenderOverlaySelectedObjectSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  std::unordered_set<EntityId> live_entities;
  live_entities.reserve(states.size());
  for (const SelectedEntityState &state : states) {
    QDR_PROFILE_SCOPE("Selected object overlay sync line entity");
    live_entities.insert(state.entity);
    const auto existing = line_entries_.find(state.entity);
    if (existing == line_entries_.end() ||
        existing->second.mesh_id != state.mesh_id ||
        existing->second.mesh_revision != state.mesh_revision) {
      const RenderOverlayMeshSnapshot *mesh =
          find_overlay_mesh_snapshot(snapshot.meshes, state.entity);
      if (mesh != nullptr) {
        line_entries_[state.entity] = create_line_entry(*mesh);
      }
    }
  }

  for (auto it = line_entries_.begin(); it != line_entries_.end();) {
    if (!live_entities.contains(it->first)) {
      it = line_entries_.erase(it);
    } else {
      ++it;
    }
  }
}

void SelectedObjectOverlayCache::update_line_proxy(
    const std::vector<SelectedEntityState> &states,
    const RenderOverlaySelectedObjectSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  if (states.empty()) {
    destroy_proxy(line_proxy_, true);
    destroy_line_batch_root();
    return;
  }

  std::size_t segment_count = 0u;
  for (const SelectedEntityState &state : states) {
    const auto existing = line_entries_.find(state.entity);
    if (existing != line_entries_.end()) {
      segment_count += existing->second.segments.size();
    }
  }

  std::vector<SelectedObjectLineVertex> line_vertices;
  std::vector<std::uint32_t> line_indices;
  line_vertices.reserve(segment_count * 4u);
  line_indices.reserve(segment_count * 6u);
  for (const SelectedEntityState &state : states) {
    QDR_PROFILE_SCOPE("Selected object overlay transform batched wire entity");
    const auto existing = line_entries_.find(state.entity);
    if (existing == line_entries_.end()) {
      continue;
    }
    const RenderOverlayMeshSnapshot *mesh =
        find_overlay_mesh_snapshot(snapshot.meshes, state.entity);
    if (mesh == nullptr) {
      continue;
    }
    for (const SelectedObjectLineSegment &segment : existing->second.segments) {
      append_selected_line_quad(
          line_vertices, line_indices,
          transform_point(mesh->world_transform, segment.start),
          transform_point(mesh->world_transform, segment.end));
    }
  }
  if (line_vertices.empty() || line_indices.empty()) {
    destroy_proxy(line_proxy_, true);
    destroy_line_batch_root();
    return;
  }

  if (!line_batch_transform_root_) {
    line_batch_transform_root_ = utils::EntityManager::get().create();
    engine_->getTransformManager().create(line_batch_transform_root_);
  }
  const filament::TransformManager::Instance parent_transform =
      engine_->getTransformManager().getInstance(line_batch_transform_root_);
  const std::uint16_t line_blend_order = static_cast<std::uint16_t>(
      viewport_overlay_draw_order(ViewportOverlayLayer::SelectedEdge) * 64);
  const OverlayRenderState line_state = OverlayRenderableBuilder::make_state(
      ViewportOverlayLayer::SelectedEdge, ViewportOverlayGeometryKind::Lines,
      line_blend_order);
  const filament::Box bounds = bounds_for_selected_line_vertices(line_vertices);
  const std::uint32_t vertex_count =
      static_cast<std::uint32_t>(line_vertices.size());
  const std::uint32_t index_count =
      static_cast<std::uint32_t>(line_indices.size());
  if (line_proxy_.entity && line_proxy_.vertex_buffer != nullptr &&
      line_proxy_.index_buffer != nullptr &&
      line_proxy_.vertex_count == vertex_count &&
      line_proxy_.index_count == index_count) {
    line_proxy_.vertex_buffer->setBufferAt(
        *engine_, 0, FilamentBufferDescriptorFactory::vector(line_vertices));
    filament::RenderableManager &renderables = engine_->getRenderableManager();
    const filament::RenderableManager::Instance instance =
        renderables.getInstance(line_proxy_.entity);
    if (instance) {
      renderables.setAxisAlignedBoundingBox(instance, bounds);
    }
    set_proxy_layer_mask(line_proxy_,
                         visible_ ? kViewportOverlayLayerMask : 0u);
    return;
  }

  destroy_proxy(line_proxy_, true);
  line_proxy_ = create_proxy(
      *engine_, *scene_, *line_material_, parent_transform, line_vertices,
      line_indices, bounds, line_state,
      visible_ ? kViewportOverlayLayerMask : 0u,
      [](filament::VertexBuffer::Builder &builder) {
        builder
            .attribute(filament::VertexAttribute::POSITION, 0,
                       AttributeType::FLOAT4,
                       offsetof(SelectedObjectLineVertex, position),
                       sizeof(SelectedObjectLineVertex))
            .attribute(filament::VertexAttribute::CUSTOM0, 0,
                       AttributeType::FLOAT4,
                       offsetof(SelectedObjectLineVertex, start),
                       sizeof(SelectedObjectLineVertex))
            .attribute(filament::VertexAttribute::CUSTOM1, 0,
                       AttributeType::FLOAT4,
                       offsetof(SelectedObjectLineVertex, end),
                       sizeof(SelectedObjectLineVertex))
            .attribute(filament::VertexAttribute::CUSTOM2, 0,
                       AttributeType::FLOAT4,
                       offsetof(SelectedObjectLineVertex, control),
                       sizeof(SelectedObjectLineVertex))
            .attribute(filament::VertexAttribute::COLOR, 0,
                       AttributeType::UBYTE4,
                       offsetof(SelectedObjectLineVertex, color),
                       sizeof(SelectedObjectLineVertex))
            .normalized(filament::VertexAttribute::COLOR);
      },
      [&line_state](filament::MaterialInstance &material) {
        configure_overlay_material(material, line_state);
        material.setParameter("lineWidth",
                              kOverlaySelectionWireLineWidthPixels);
        material.setParameter("featherPixels", kOverlayLineFeatherPixels);
        material.setParameter("nearPlane", kSelectedObjectLineNearPlane);
      });
}

SelectedObjectOverlayCache::Entry
SelectedObjectOverlayCache::create_entry(
    const RenderOverlayMeshSnapshot &mesh) {
  QDR_PROFILE_FUNCTION();
  Entry entry;
  entry.mesh_id = mesh.mesh_id;
  if (!mesh.has_triangle_geometry()) {
    return entry;
  }
  entry.mesh_revision = mesh.mesh_revision;
  entry.transform_root = utils::EntityManager::get().create();
  filament::TransformManager &transforms = engine_->getTransformManager();
  transforms.create(entry.transform_root);
  const filament::TransformManager::Instance parent_transform =
      transforms.getInstance(entry.transform_root);

  const std::uint8_t layer_mask = visible_ ? kViewportOverlayLayerMask : 0u;
  std::vector<OverlayFaceFillVertex> face_vertices =
      selected_face_vertices(mesh.vertices);
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

void SelectedObjectOverlayCache::destroy_line_batch_root() {
  if (!engine_ || !line_batch_transform_root_) {
    return;
  }
  engine_->getTransformManager().destroy(line_batch_transform_root_);
  utils::EntityManager::get().destroy(line_batch_transform_root_);
  line_batch_transform_root_.clear();
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
  set_proxy_layer_mask(line_proxy_, layer_mask);
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
