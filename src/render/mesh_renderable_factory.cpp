////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/mesh_renderable_factory.hpp"

#include "assets/mesh_collision_geometry_payload.hpp"
#include "assets/mesh_geometry_projection.hpp"
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
#include <filament/SkinningBuffer.h>
#include <filament/TransformManager.h>
#include <math/mat3.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <type_traits>
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
    float occurrence_index = 0.0f;
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
    float occurrence_index = 0.0f;
};

std::span<const QVec3> effective_mesh_positions(
    const RenderMeshAssetSnapshot &mesh, std::span<const QVec3> positions = {}) {
  return positions.empty() ? mesh.positions : positions;
}

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
mesh_tangent_frames(const RenderMeshAssetSnapshot &mesh,
                    std::span<const QVec3> positions) {
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
    const QVec3 edge1 = positions.empty() ? v1.position - v0.position
                                        : positions[i1] - positions[i0];
    const QVec3 edge2 = positions.empty() ? v2.position - v0.position
                                        : positions[i2] - positions[i0];
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

template <typename Vertex = RenderVertex>
std::vector<Vertex>
build_render_vertices(const RenderMeshAssetSnapshot &mesh,
                      std::span<const QVec3> positions = {},
                      const MeshSurfaceGridProjectionPlan *grid_plan = nullptr,
                      const MeshQuadUvInterpolationPlan *quad_plan = nullptr) {
  positions = effective_mesh_positions(mesh, positions);
  const std::vector<filament::math::float4> tangent_frames =
      mesh_tangent_frames(mesh, positions);
  const std::vector<MeshSurfaceGridVertexProjection> grid_projections =
      grid_plan ? build_mesh_surface_grid_vertex_projections(mesh.vertices, *grid_plan, positions)
                : build_mesh_surface_grid_vertex_projections(mesh.vertices, mesh.indices, mesh.primitives, positions);
  const std::vector<MeshQuadUvInterpolation> quad_interpolations =
      quad_plan ? build_mesh_quad_uv_interpolations(mesh.vertices, *quad_plan, positions)
                : build_mesh_quad_uv_interpolations(mesh.vertices, mesh.indices, mesh.primitives, positions);
  std::vector<Vertex> vertices;
  vertices.reserve(mesh.vertices.size());
  for (std::size_t index = 0u; index < mesh.vertices.size(); ++index) {
    const MeshVertex &vertex = mesh.vertices[index];
    const MeshSurfaceGridVertexProjection &grid_projection =
        grid_projections[index];
    const MeshQuadUvInterpolation &quad = quad_interpolations[index];
    Vertex packed{
        .tangent_frame = tangent_frames[index],
        .uv = {vertex.uv.x, vertex.uv.y},
        .surface_grid_projection = {grid_projection.local_normal.x, grid_projection.local_normal.y,
         grid_projection.local_normal.z,
         grid_projection.face_projected ? 1.0f : 0.0f},
        .uv_carrier = {vertex.uv_carrier[0], vertex.uv_carrier[1], vertex.uv_carrier[2],
         vertex.uv_carrier[3]},
        .uv_geometric_normal = to_filament(vertex.uv_geometric_normal),
        .quad_uv_coordinates = {quad.coordinates.x, quad.coordinates.y, quad.coordinates.z,
         quad.enabled ? 1.0f : 0.0f},
        .quad_uv_correction = {quad.geometry_warp.x, quad.geometry_warp.y, quad.uv_correction.x,
         quad.uv_correction.y},
        .color = vertex.rgba,
    };
    if constexpr (std::is_same_v<Vertex, RenderVertex>) {
      packed.position = to_filament(positions.empty() ? vertex.position : positions[index]);
    }
    vertices.push_back(packed);
  }
  return vertices;
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

  if (!mesh.positions.empty() && mesh.positions.size() != mesh.vertices.size()) return {};
  for (std::size_t index = 0u; index < mesh.vertices.size(); ++index) {
    const QVec3 position = mesh.positions.empty() ? mesh.vertices[index].position : mesh.positions[index];
    min_value.x = std::min(min_value.x, position.x);
    min_value.y = std::min(min_value.y, position.y);
    min_value.z = std::min(min_value.z, position.z);
    max_value.x = std::max(max_value.x, position.x);
    max_value.y = std::max(max_value.y, position.y);
    max_value.z = std::max(max_value.z, position.z);
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

bool same_dynamic_float(float left, float right) {
  return std::bit_cast<std::uint32_t>(left) ==
         std::bit_cast<std::uint32_t>(right);
}

bool same_dynamic_vec3(QVec3 left, QVec3 right) {
  return same_dynamic_float(left.x, right.x) &&
         same_dynamic_float(left.y, right.y) &&
         same_dynamic_float(left.z, right.z);
}

bool same_dynamic_vertex(const MeshVertex &left, const MeshVertex &right) {
  return same_dynamic_vec3(left.position, right.position) &&
         same_dynamic_vec3(left.normal, right.normal) &&
         same_dynamic_float(left.uv.x, right.uv.x) &&
         same_dynamic_float(left.uv.y, right.uv.y) &&
         left.rgba == right.rgba &&
         left.source_vertex_id == right.source_vertex_id &&
         std::equal(left.uv_carrier.begin(), left.uv_carrier.end(),
                    right.uv_carrier.begin(), same_dynamic_float) &&
         same_dynamic_vec3(left.uv_geometric_normal,
                           right.uv_geometric_normal);
}

bool same_dynamic_geometry(const RenderMeshAssetSnapshot &left,
                           const RenderMeshAssetSnapshot &right) {
  if (left.vertices.size() != right.vertices.size() ||
      left.indices.size() != right.indices.size() ||
      left.primitives.size() != right.primitives.size()) return false;
  if (left.geometry_payload && left.geometry_payload == right.geometry_payload &&
      left.vertices.data() == right.vertices.data() &&
      left.indices.data() == right.indices.data() &&
      left.primitives.data() == right.primitives.data()) return true;
  return std::equal(left.vertices.begin(), left.vertices.end(),
                    right.vertices.begin(), same_dynamic_vertex) &&
         std::equal(left.indices.begin(), left.indices.end(), right.indices.begin()) &&
         std::equal(left.primitives.begin(), left.primitives.end(),
                    right.primitives.begin());
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

/// CPU-only transforms shared by sibling renderable instance updates.
struct MeshRenderableFactory::ExplicitInstanceUpdate final {
  std::vector<filament::math::mat4f> transforms;
};

/// CPU-only upload storage. Source identity prevents accidental cross-mesh or
/// repeated consumption; the snapshot pins immutable attribute inputs.
struct MeshRenderableFactory::DynamicGeometryUpdate final {
  /// Shared immutable CPU bytes; each destination owns independent descriptors.
  struct PackedStreams final {
    std::vector<filament::math::float3> positions;
    std::vector<DynamicRenderVertexStatic> static_vertices;
    std::vector<std::uint32_t> indices;
  };
  RenderMeshAssetSnapshot source;
  const QVec3 *source_positions = nullptr;
  std::size_t source_position_count = 0u;
  filament::backend::BufferDescriptor positions;
  filament::backend::BufferDescriptor static_vertices;
  filament::backend::BufferDescriptor indices;
  std::shared_ptr<const PackedStreams> packed;
  bool consumed = false;
};

/// Immutable canonical streams retained by one coarse geometry owner. Records
/// share unchanged CPU packs; only the current accepted population stays pinned.
struct EditableMeshGeometryState final {
  /// Full canonical streams shared independently of material index partitions.
  struct CanonicalStreams final {
    std::vector<filament::math::float3> positions;
    std::vector<DynamicRenderVertexStatic> attributes;
  };
  /// Validated immutable triangle, material-range and authored-face bindings.
  struct Topology final {
    RenderMeshAssetSnapshot source;
    std::vector<MeshPrimitiveSpan> selected_primitives;
    std::vector<std::uint32_t> indices;
    MeshSurfaceGridProjectionPlan grid_plan;
    MeshQuadUvInterpolationPlan quad_plan;
  };
  /// Pins source attributes and owns renderer-derived streams for one occurrence.
  struct Record final {
    RenderMeshAssetSnapshot source;
    std::shared_ptr<const MeshGeometryPayload> copied_source;
    std::shared_ptr<const std::vector<QVec3>> position_payload;
    std::shared_ptr<const Topology> topology;
    std::shared_ptr<const CanonicalStreams> streams;
    RenderInstanceBounds local_bounds;
    bool uses_source_positions = false;
  };
  std::vector<std::shared_ptr<const Record>> records;
  std::vector<std::uint32_t> vertex_offsets;
  std::shared_ptr<const std::vector<std::uint32_t>> indices;
  std::vector<QMat4> source_transforms;
  std::vector<QMat4> source_mappings;
  std::vector<filament::math::mat4f> transforms;
  std::vector<filament::math::mat4f> mappings;
  std::vector<filament::math::mat3f> mapping_cofactors;
  std::vector<filament::math::mat3f> world_normals;
  QVec3 world_anchor;
  RenderInstanceBounds local_bounds;
  RenderInstanceBounds world_bounds;
  std::uint32_t vertex_count = 0u;
};

/// One rigid bone per vertex preserves canonical positions and tangent frames.
struct EditableSurfaceVertexBinding final {
  std::array<std::uint16_t, 4u> bone_indices{};
  filament::math::float4 bone_weights{1.0f, 0.0f, 0.0f, 0.0f};
  float occurrence_index = 0.0f;
};

/// A complete CPU candidate with a bounded number of owned upload descriptors.
struct MeshRenderableFactory::EditableSurfaceUpdate final {
  /// A contiguous range of one GPU vertex attribute stream.
  struct Range final {
    std::uint32_t first_vertex = 0u;
    std::size_t byte_count = 0u;
    filament::backend::BufferDescriptor upload;
  };
  std::shared_ptr<const EditableMeshGeometryState> previous;
  std::shared_ptr<const EditableMeshGeometryState> state;
  filament::VertexBuffer *expected_vertices = nullptr;
  filament::IndexBuffer *expected_indices = nullptr;
  filament::SkinningBuffer *expected_palette = nullptr;
  std::vector<Range> position_ranges;
  std::vector<Range> attribute_ranges;
  filament::backend::BufferDescriptor binding_upload;
  filament::backend::BufferDescriptor index_upload;
  std::size_t attribute_pack_count = 0u;
  std::size_t canonical_reuse_count = 0u;
  bool reuses_geometry = false;
  bool indices_changed = false;
  bool palette_changed = false;
  bool mappings_changed = false;
  bool bounds_changed = false;
  bool anchor_changed = false;
  bool consumed = false;
};

/// Owns exact canonical keys and per-key completion, never retained globally.
struct MeshRenderableFactory::EditableSurfacePreparationContext final {
  /// Pins comparison inputs while one worker derives the shared attribute pack.
  struct Entry final {
    std::shared_ptr<const EditableMeshGeometryState::Record> key;
    std::shared_future<std::shared_ptr<const EditableMeshGeometryState::CanonicalStreams>> result;
  };
  std::mutex mutex;
  std::unordered_map<std::uint64_t, std::vector<Entry>> entries;
  std::unordered_map<std::uint64_t, std::size_t> source_counts;
};

namespace {

bool finite_surface_position(QVec3 position) {
  return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

bool same_surface_attributes(const MeshVertex &left, const MeshVertex &right) {
  return same_dynamic_vec3(left.normal, right.normal) &&
      same_dynamic_float(left.uv.x, right.uv.x) && same_dynamic_float(left.uv.y, right.uv.y) &&
      left.rgba == right.rgba && left.source_vertex_id == right.source_vertex_id &&
      left.uv_carrier == right.uv_carrier &&
      same_dynamic_vec3(left.uv_geometric_normal, right.uv_geometric_normal);
}

template <typename Value>
bool same_surface_bytes(std::span<const Value> left, std::span<const Value> right) {
  static_assert(std::is_trivially_copyable_v<Value>);
  return left.size() == right.size() && (left.empty() ||
      std::memcmp(left.data(), right.data(), left.size_bytes()) == 0);
}

bool same_surface_bounds(const RenderInstanceBounds &left, const RenderInstanceBounds &right) {
  return left.present == right.present && same_dynamic_vec3(left.min, right.min) &&
      same_dynamic_vec3(left.max, right.max);
}

filament::math::mat3f surface_cofactor(const QMat4 &matrix, bool rescale) {
  QVec3 first{matrix.values[0], matrix.values[1], matrix.values[2]};
  QVec3 second{matrix.values[4], matrix.values[5], matrix.values[6]};
  QVec3 third{matrix.values[8], matrix.values[9], matrix.values[10]};
  if (rescale) {
    const float magnitude = std::max({std::fabs(first.x), std::fabs(first.y), std::fabs(first.z),
        std::fabs(second.x), std::fabs(second.y), std::fabs(second.z),
        std::fabs(third.x), std::fabs(third.y), std::fabs(third.z)});
    first = first / magnitude;
    second = second / magnitude;
    third = third / magnitude;
  }
  return {to_filament(cross(second, third)), to_filament(cross(third, first)),
          to_filament(cross(first, second))};
}

bool pinned_surface_streams(const RenderMeshAssetSnapshot &mesh) {
  if (mesh.geometry_projection) {
    return mesh.vertices.data() == mesh.geometry_projection->vertices().data() &&
        mesh.vertices.size() == mesh.geometry_projection->vertices().size() &&
        mesh.indices.data() == mesh.geometry_projection->indices().data() &&
        mesh.indices.size() == mesh.geometry_projection->indices().size() &&
        mesh.primitives.data() == mesh.geometry_projection->primitives().data() &&
        mesh.primitives.size() == mesh.geometry_projection->primitives().size();
  }
  return mesh.geometry_payload && mesh.vertices.data() == mesh.geometry_payload->vertices.data() &&
      mesh.vertices.size() == mesh.geometry_payload->vertices.size() &&
      mesh.indices.data() == mesh.geometry_payload->indices.data() &&
      mesh.indices.size() == mesh.geometry_payload->indices.size() &&
      mesh.primitives.data() == mesh.geometry_payload->primitives.data() &&
      mesh.primitives.size() == mesh.geometry_payload->primitives.size();
}

bool same_surface_topology(const EditableMeshGeometryState::Topology &previous,
                           const MeshRenderableFactory::EditableSurfaceSource &source) {
  const auto &left = previous.source;
  const auto &right = source.mesh;
  return left.vertices.size() == right.vertices.size() &&
      left.indices.size() == right.indices.size() && left.primitives.size() == right.primitives.size() &&
      previous.selected_primitives.size() == source.primitives.size() &&
      std::equal(previous.selected_primitives.begin(), previous.selected_primitives.end(), source.primitives.begin()) &&
      ((pinned_surface_streams(right) && left.indices.data() == right.indices.data() &&
        left.primitives.data() == right.primitives.data()) ||
       (std::equal(left.indices.begin(), left.indices.end(), right.indices.begin()) &&
        std::equal(left.primitives.begin(), left.primitives.end(), right.primitives.begin())));
}

bool same_surface_source(const EditableMeshGeometryState::Record &previous,
                         const MeshRenderableFactory::EditableSurfaceSource &source,
                         std::span<const QVec3> positions) {
  const auto &left = previous.source;
  const auto &right = source.mesh;
  if (left.vertices.size() != right.vertices.size() || left.indices.size() != right.indices.size() ||
      left.primitives.size() != right.primitives.size()) return false;
  const bool same_owners = pinned_surface_streams(right) && pinned_surface_streams(left);
  if (!(same_owners && left.vertices.data() == right.vertices.data()) &&
      !std::equal(left.vertices.begin(), left.vertices.end(), right.vertices.begin(),
                  same_surface_attributes)) return false;
  if (!(same_owners && left.indices.data() == right.indices.data()) &&
      !std::equal(left.indices.begin(), left.indices.end(), right.indices.begin())) return false;
  if (!(same_owners && left.primitives.data() == right.primitives.data()) &&
      !std::equal(left.primitives.begin(), left.primitives.end(), right.primitives.begin())) return false;
  if (source.position_payload && source.position_payload == previous.position_payload &&
      positions.data() == source.position_payload->data() &&
      positions.size() == source.position_payload->size()) return true;
  if (positions.empty() && same_owners && left.vertices.data() == right.vertices.data() &&
      previous.uses_source_positions) return true;
  if (right.geometry_projection &&
      right.geometry_projection->positions == previous.position_payload &&
      right.geometry_projection->positions &&
      positions.data() == right.geometry_projection->positions->data() &&
      positions.size() == right.geometry_projection->positions->size()) return true;
  for (std::size_t index = 0u; index < previous.streams->positions.size(); ++index) {
    const QVec3 position = positions.empty() ? right.vertices[index].position : positions[index];
    const auto packed = previous.streams->positions[index];
    if (!same_dynamic_vec3(position, {packed.x, packed.y, packed.z})) return false;
  }
  return true;
}

std::uint64_t surface_canonical_signature(const RenderMeshAssetSnapshot &mesh,
                                        const void *positions) {
  // Immutable stream identity is the common-case key. Distinct unique geometry
  // never pays a whole-mesh hash merely to discover it has no sharing partner.
  return RenderSignatureBuilder{}
      .add_value(reinterpret_cast<std::uintptr_t>(mesh.vertices.data()))
      .add_value(reinterpret_cast<std::uintptr_t>(mesh.indices.data()))
      .add_value(reinterpret_cast<std::uintptr_t>(mesh.primitives.data()))
      .add_value(reinterpret_cast<std::uintptr_t>(positions))
      .add_size(mesh.vertices.size()).add_size(mesh.indices.size()).value();
}

bool append_surface_world_bounds(RenderInstanceBounds &bounds,
                                 const RenderInstanceBounds &local, const QMat4 &transform) {
  for (std::uint32_t corner = 0u; corner < 8u; ++corner) {
    const QVec3 point = transform_point(transform,
        {(corner & 1u) != 0u ? local.max.x : local.min.x,
         (corner & 2u) != 0u ? local.max.y : local.min.y,
         (corner & 4u) != 0u ? local.max.z : local.min.z});
    if (!finite_surface_position(point)) return false;
    if (!bounds.present) {
      bounds = {.min = point, .max = point, .present = true};
    } else {
      bounds.min = {std::min(bounds.min.x, point.x), std::min(bounds.min.y, point.y),
                    std::min(bounds.min.z, point.z)};
      bounds.max = {std::max(bounds.max.x, point.x), std::max(bounds.max.y, point.y),
                    std::max(bounds.max.z, point.z)};
    }
  }
  return true;
}

} // namespace

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

bool MeshRenderableFactory::supports_editable_surface_transform(const QMat4 &transform) {
  if (!std::ranges::all_of(transform.values, [](float value) { return std::isfinite(value); }) ||
      transform.values[3] != 0.0f || transform.values[7] != 0.0f ||
      transform.values[11] != 0.0f || transform.values[15] != 1.0f) return false;
  QVec3 first{transform.values[0], transform.values[1], transform.values[2]};
  QVec3 second{transform.values[4], transform.values[5], transform.values[6]};
  QVec3 third{transform.values[8], transform.values[9], transform.values[10]};
  const float magnitude = std::max({std::fabs(first.x), std::fabs(first.y), std::fabs(first.z),
      std::fabs(second.x), std::fabs(second.y), std::fabs(second.z),
      std::fabs(third.x), std::fabs(third.y), std::fabs(third.z)});
  // Skinning normalizes raw cofactors. Keep their squared magnitudes within
  // normal float range; ordinary renderables retain the extreme-scale path.
  if (magnitude < 1.0e-8f || magnitude > 1.0e8f) return false;
  first = first / magnitude;
  second = second / magnitude;
  third = third / magnitude;
  const float maximum = std::max({dot(first, first), dot(second, second), dot(third, third)});
  const float minimum = std::min({dot(first, first), dot(second, second), dot(third, third)});
  const float tolerance = maximum * 0.00001f;
  return dot(cross(first, second), third) > 0.0f && maximum - minimum <= tolerance &&
      std::fabs(dot(first, second)) <= tolerance && std::fabs(dot(first, third)) <= tolerance &&
      std::fabs(dot(second, third)) <= tolerance;
}

std::shared_ptr<MeshRenderableFactory::EditableSurfacePreparationContext>
MeshRenderableFactory::create_editable_surface_preparation_context() {
  return std::make_shared<EditableSurfacePreparationContext>();
}

void MeshRenderableFactory::register_editable_surface_sources(
    EditableSurfacePreparationContext &context, std::span<const EditableSurfaceSource> sources) {
  for (const auto &source : sources) {
    const auto positions = effective_mesh_positions(source.mesh, source.positions);
    const void *position_data = positions.empty() && source.position_payload
        ? source.position_payload->data() : positions.data();
    ++context.source_counts[surface_canonical_signature(source.mesh, position_data)];
  }
}

std::shared_ptr<MeshRenderableFactory::EditableSurfaceUpdate>
MeshRenderableFactory::prepare_editable_surface_update(
    std::span<const EditableSurfaceSource> sources, const MeshRenderableProxy *existing,
    std::shared_ptr<EditableSurfacePreparationContext> context) {
  QDR_PROFILE_SCOPE("Editable surface prepare chunk streams");
  if (sources.empty() || sources.size() > kEditableSurfaceMaximumOccurrences) return {};
  try {
    auto prepared = std::make_shared<EditableSurfaceUpdate>();
    if (existing != nullptr && existing->owns_mesh_buffers && existing->skinning_buffer != nullptr) {
      prepared->previous = existing->editable_geometry_state;
      prepared->expected_vertices = existing->vertex_buffer;
      prepared->expected_indices = existing->index_buffer;
      prepared->expected_palette = existing->skinning_buffer;
    }
    auto state = std::make_shared<EditableMeshGeometryState>();
    const auto &first_transform = sources.front().world_from_local;
    state->world_anchor = {first_transform.values[12], first_transform.values[13], first_transform.values[14]};
    const auto within_anchor = [&](QVec3 anchor) {
      return std::ranges::all_of(sources, [&](const EditableSurfaceSource &source) {
        return std::fabs(static_cast<double>(source.world_from_local.values[12]) - anchor.x) <= kEditableSurfaceMaximumPlacementOffset &&
            std::fabs(static_cast<double>(source.world_from_local.values[13]) - anchor.y) <= kEditableSurfaceMaximumPlacementOffset &&
            std::fabs(static_cast<double>(source.world_from_local.values[14]) - anchor.z) <= kEditableSurfaceMaximumPlacementOffset;
      });
    };
    if (prepared->previous && within_anchor(prepared->previous->world_anchor)) {
      state->world_anchor = prepared->previous->world_anchor;
    }
    if (!within_anchor(state->world_anchor)) return {};
    state->records.reserve(sources.size());
    state->vertex_offsets.reserve(sources.size());
    state->source_transforms.reserve(sources.size());
    state->source_mappings.reserve(sources.size());
    state->transforms.reserve(sources.size());
    state->mappings.reserve(sources.size());
    state->mapping_cofactors.reserve(sources.size());
    state->world_normals.reserve(sources.size());
    bool layout_matches = prepared->previous && prepared->previous->records.size() == sources.size();
    bool indices_match = layout_matches;
    std::size_t index_count = 0u;
    for (std::size_t occurrence = 0u; occurrence < sources.size(); ++occurrence) {
      const auto &input = sources[occurrence];
      const auto &mesh = input.mesh;
      auto positions = effective_mesh_positions(mesh, input.positions);
      if (positions.empty() && input.position_payload) positions = *input.position_payload;
      if (!mesh.renderable() || mesh.indices.size() % 3u != 0u ||
          (!positions.empty() && positions.size() != mesh.vertices.size()) ||
          mesh.vertices.size() > kEditableSurfaceMaximumVertices - state->vertex_count ||
          !supports_editable_surface_transform(input.world_from_local) ||
          !std::ranges::all_of(input.sampling_from_local.values,
                              [](float value) { return std::isfinite(value); })) return {};
      const auto previous = prepared->previous && occurrence < prepared->previous->records.size()
          ? prepared->previous->records[occurrence] : nullptr;
      std::shared_ptr<const EditableMeshGeometryState::Record> record;
      const bool retained_topology = previous && same_surface_topology(*previous->topology, input);
      const bool retained_attributes = previous && pinned_surface_streams(mesh) &&
          previous->source.vertices.data() == mesh.vertices.data() &&
          previous->source.vertices.size() == mesh.vertices.size();
      if (retained_topology && same_surface_source(*previous, input, positions)) {
        record = previous;
      } else {
        auto next = std::make_shared<EditableMeshGeometryState::Record>();
        auto streams = std::make_shared<EditableMeshGeometryState::CanonicalStreams>();
        next->streams = streams;
        next->source = mesh;
        const bool pinned = pinned_surface_streams(mesh);
        if (!pinned) {
          auto copy = std::make_shared<MeshGeometryPayload>();
          copy->vertices.assign(mesh.vertices.begin(), mesh.vertices.end());
          copy->indices.assign(mesh.indices.begin(), mesh.indices.end());
          copy->primitives.assign(mesh.primitives.begin(), mesh.primitives.end());
          next->copied_source = copy;
          next->source.geometry_payload = copy;
          next->source.geometry_projection.reset();
          next->source.vertices = copy->vertices;
          next->source.indices = copy->indices;
          next->source.primitives = copy->primitives;
        }
        next->source.positions = {};
        next->uses_source_positions = positions.empty();
        if (input.position_payload && positions.data() == input.position_payload->data() &&
            positions.size() == input.position_payload->size()) next->position_payload = input.position_payload;
        if (!next->position_payload && mesh.geometry_projection && mesh.geometry_projection->positions &&
            positions.data() == mesh.geometry_projection->positions->data() &&
            positions.size() == mesh.geometry_projection->positions->size()) {
          next->position_payload = mesh.geometry_projection->positions;
        }
        streams->positions.reserve(mesh.vertices.size());
        for (std::size_t vertex = 0u; vertex < mesh.vertices.size(); ++vertex) {
          const auto &attribute = mesh.vertices[vertex];
          const auto point = positions.empty() ? attribute.position : positions[vertex];
          if (!finite_surface_position(point) || (!retained_attributes &&
              (!finite_surface_position(attribute.normal) || !finite_surface_position(attribute.uv_geometric_normal) ||
               !std::isfinite(attribute.uv.x) || !std::isfinite(attribute.uv.y) ||
               !std::ranges::all_of(attribute.uv_carrier,
                                   [](float value) { return std::isfinite(value); })))) return {};
          streams->positions.push_back(to_filament(point));
        }
        if (retained_topology) {
          next->topology = previous->topology;
        } else {
          auto topology = std::make_shared<EditableMeshGeometryState::Topology>();
          topology->source = next->source;
          topology->selected_primitives.assign(input.primitives.begin(), input.primitives.end());
          for (const auto index : mesh.indices) if (index >= mesh.vertices.size()) return {};
          for (const auto &primitive : mesh.primitives) {
            if (primitive.index_count % 3u != 0u || static_cast<std::uint64_t>(primitive.index_offset) +
                primitive.index_count > mesh.indices.size()) return {};
          }
          if (input.primitives.empty()) {
            topology->indices.assign(mesh.indices.begin(), mesh.indices.end());
          } else {
            for (const auto &primitive : input.primitives) {
              if (primitive.index_count % 3u != 0u || static_cast<std::uint64_t>(primitive.index_offset) +
                  primitive.index_count > mesh.indices.size()) return {};
              const auto selected = mesh.indices.subspan(primitive.index_offset, primitive.index_count);
              topology->indices.insert(topology->indices.end(), selected.begin(), selected.end());
            }
          }
          if (topology->indices.empty()) return {};
          topology->grid_plan = prepare_mesh_surface_grid_projection(mesh.vertices.size(), mesh.indices, mesh.primitives);
          topology->quad_plan = prepare_mesh_quad_uv_interpolation(mesh.vertices.size(), mesh.indices, mesh.primitives);
          next->topology = std::move(topology);
        }
        // Derive from every canonical face before selecting this material's
        // indices, preserving smoothing, authored quad charts and grid hints.
        next->local_bounds = positions.empty() ? compute_local_bounds_impl(mesh)
                                               : compute_local_bounds_impl(positions);
        using CanonicalStreams = EditableMeshGeometryState::CanonicalStreams;
        std::shared_future<std::shared_ptr<const CanonicalStreams>> pending;
        std::shared_ptr<std::promise<std::shared_ptr<const CanonicalStreams>>> producer;
        const auto signature = context ? surface_canonical_signature(mesh, positions.data()) : 0u;
        const auto count = context ? context->source_counts.find(signature)
            : std::unordered_map<std::uint64_t, std::size_t>::const_iterator{};
        if (context && (context->source_counts.empty() ||
                        (count != context->source_counts.end() && count->second > 1u))) {
          std::lock_guard lock(context->mutex);
          auto &entries = context->entries[signature];
          for (const auto &entry : entries) {
            if (same_surface_source(*entry.key, input, positions)) {
              pending = entry.result;
              break;
            }
          }
          if (!pending.valid()) {
            producer = std::make_shared<std::promise<std::shared_ptr<const CanonicalStreams>>>();
            pending = producer->get_future().share();
            entries.push_back({next, pending});
          }
        }
        if (pending.valid() && !producer) {
          next->streams = pending.get();
          ++prepared->canonical_reuse_count;
        } else {
          try {
            streams->attributes = build_render_vertices<DynamicRenderVertexStatic>(mesh, positions,
                &next->topology->grid_plan, &next->topology->quad_plan);
            ++prepared->attribute_pack_count;
            if (producer) producer->set_value(streams);
          } catch (...) {
            if (producer) producer->set_exception(std::current_exception());
            throw;
          }
        }
        record = std::move(next);
      }
      if (record->topology->indices.size() > kEditableSurfaceMaximumIndices - index_count) return {};
      index_count += record->topology->indices.size();
      state->vertex_offsets.push_back(state->vertex_count);
      state->vertex_count += static_cast<std::uint32_t>(record->streams->positions.size());
      layout_matches = layout_matches && previous &&
          previous->streams->positions.size() == record->streams->positions.size() &&
          previous->topology->indices.size() == record->topology->indices.size();
      indices_match = indices_match && layout_matches &&
          (previous->topology == record->topology || previous->topology->indices == record->topology->indices);
      // Keep small canonical details away from absolute float translations.
      // Filament combines the entity anchor with its camera-relative transform
      // in double precision before the shader applies these local bone matrices.
      QMat4 local_from_source = input.world_from_local;
      local_from_source.values[12] = static_cast<float>(static_cast<double>(input.world_from_local.values[12]) - state->world_anchor.x);
      local_from_source.values[13] = static_cast<float>(static_cast<double>(input.world_from_local.values[13]) - state->world_anchor.y);
      local_from_source.values[14] = static_cast<float>(static_cast<double>(input.world_from_local.values[14]) - state->world_anchor.z);
      if (!append_surface_world_bounds(state->local_bounds, record->local_bounds, local_from_source)) return {};
      if (!append_surface_world_bounds(state->world_bounds, record->local_bounds, input.world_from_local)) return {};
      state->records.push_back(std::move(record));
      const bool same_transform = previous && prepared->previous->source_transforms[occurrence].values == input.world_from_local.values;
      const bool same_mapping = previous && prepared->previous->source_mappings[occurrence].values == input.sampling_from_local.values;
      const bool same_anchor = prepared->previous && same_dynamic_vec3(state->world_anchor, prepared->previous->world_anchor);
      state->source_transforms.push_back(input.world_from_local);
      state->source_mappings.push_back(input.sampling_from_local);
      state->transforms.push_back(same_transform && same_anchor ? prepared->previous->transforms[occurrence] : make_transform(local_from_source));
      state->mappings.push_back(same_mapping ? prepared->previous->mappings[occurrence] : make_transform(input.sampling_from_local));
      state->mapping_cofactors.push_back(same_mapping ? prepared->previous->mapping_cofactors[occurrence] : surface_cofactor(input.sampling_from_local, false));
      state->world_normals.push_back(same_transform ? prepared->previous->world_normals[occurrence] : surface_cofactor(input.world_from_local, true));
    }
    if (indices_match) {
      state->indices = prepared->previous->indices;
    } else {
      auto indices = std::make_shared<std::vector<std::uint32_t>>();
      indices->reserve(index_count);
      for (std::size_t occurrence = 0u; occurrence < state->records.size(); ++occurrence) {
        for (const auto index : state->records[occurrence]->topology->indices) {
          indices->push_back(index + state->vertex_offsets[occurrence]);
        }
      }
      state->indices = std::move(indices);
    }
    prepared->reuses_geometry = layout_matches && prepared->expected_vertices != nullptr &&
        prepared->expected_indices != nullptr && existing->vertex_count == state->vertex_count &&
        existing->index_count == state->indices->size();
    const auto old = prepared->previous;
    prepared->indices_changed = !prepared->reuses_geometry || state->indices != old->indices;
    prepared->palette_changed = !prepared->reuses_geometry ||
        !same_surface_bytes<filament::math::mat4f>(state->transforms, old->transforms);
    prepared->mappings_changed = !prepared->reuses_geometry ||
        !same_surface_bytes<filament::math::mat4f>(state->mappings, old->mappings);
    prepared->bounds_changed = !prepared->reuses_geometry || !same_surface_bounds(state->local_bounds, old->local_bounds);
    prepared->anchor_changed = !prepared->reuses_geometry || !same_dynamic_vec3(state->world_anchor, old->world_anchor);
    const auto prepare_ranges = [&]<typename Vertex>(bool attributes,
        std::vector<EditableSurfaceUpdate::Range> &ranges) {
      std::vector<std::pair<std::size_t, std::size_t>> spans;
      for (std::size_t index = 0u; index < state->records.size(); ++index) {
        const auto &record = *state->records[index];
        const auto &values = [&]() -> const std::vector<Vertex> & {
          if constexpr (std::is_same_v<Vertex, DynamicRenderVertexStatic>) return record.streams->attributes;
          else return record.streams->positions;
        }();
        bool changed = !prepared->reuses_geometry;
        if (!changed && state->records[index] != old->records[index]) {
          const auto &previous_values = [&]() -> const std::vector<Vertex> & {
            if constexpr (std::is_same_v<Vertex, DynamicRenderVertexStatic>) return old->records[index]->streams->attributes;
            else return old->records[index]->streams->positions;
          }();
          changed = !same_surface_bytes<Vertex>(values, previous_values);
        }
        if (!changed) continue;
        if (!spans.empty() && spans.back().second == index) spans.back().second = index + 1u;
        else spans.emplace_back(index, index + 1u);
      }
      // Command count is bounded independently from fragmented object edits.
      constexpr std::size_t kMaximumUploadRanges = 8u;
      if (spans.size() > kMaximumUploadRanges) spans = {{spans.front().first, spans.back().second}};
      ranges.reserve(spans.size());
      for (const auto [begin, end] : spans) {
        std::vector<Vertex> packed;
        const auto first_vertex = state->vertex_offsets[begin];
        const auto last_vertex = end == state->records.size() ? state->vertex_count : state->vertex_offsets[end];
        packed.reserve(last_vertex - first_vertex);
        for (std::size_t index = begin; index < end; ++index) {
          const auto &values = [&]() -> const std::vector<Vertex> & {
            if constexpr (std::is_same_v<Vertex, DynamicRenderVertexStatic>) return state->records[index]->streams->attributes;
            else return state->records[index]->streams->positions;
          }();
          packed.insert(packed.end(), values.begin(), values.end());
        }
        const auto bytes = packed.size() * sizeof(Vertex);
        ranges.push_back({first_vertex, bytes, FilamentBufferDescriptorFactory::vector(std::move(packed))});
      }
      (void)attributes;
    };
    prepare_ranges.template operator()<filament::math::float3>(false, prepared->position_ranges);
    prepare_ranges.template operator()<DynamicRenderVertexStatic>(true, prepared->attribute_ranges);
    if (prepared->indices_changed) prepared->index_upload = FilamentBufferDescriptorFactory::retained_bytes(
        state->indices->data(), state->indices->size() * sizeof(std::uint32_t), state->indices);
    if (!prepared->reuses_geometry) {
      std::vector<EditableSurfaceVertexBinding> bindings;
      bindings.reserve(state->vertex_count);
      for (std::size_t index = 0u; index < state->records.size(); ++index) {
        EditableSurfaceVertexBinding binding;
        binding.bone_indices[0] = static_cast<std::uint16_t>(index);
        binding.occurrence_index = static_cast<float>(index);
        bindings.insert(bindings.end(), state->records[index]->streams->positions.size(), binding);
      }
      prepared->binding_upload = FilamentBufferDescriptorFactory::vector(std::move(bindings));
    }
    prepared->state = std::move(state);
    return prepared;
  } catch (...) {
    return {};
  }
}

bool MeshRenderableFactory::editable_surface_update_reuses_geometry(const EditableSurfaceUpdate &prepared) {
  return prepared.reuses_geometry;
}

RenderInstanceBounds MeshRenderableFactory::editable_surface_world_bounds(const EditableSurfaceUpdate &prepared) {
  return prepared.state ? prepared.state->world_bounds : RenderInstanceBounds{};
}

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
  if (!mesh.positions.empty()) return acquire_dynamic_geometry(mesh, proxy, mesh.positions);
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
          .attribute(filament::VertexAttribute::CUSTOM5, 0,
                     AttributeType::FLOAT3, offsetof(RenderVertex, position), sizeof(RenderVertex))
          .attribute(filament::VertexAttribute::CUSTOM6, 0,
                     AttributeType::FLOAT, offsetof(RenderVertex, occurrence_index), sizeof(RenderVertex))
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
  dynamic_positions = effective_mesh_positions(mesh, dynamic_positions);
  if (engine_ == nullptr || !mesh.renderable() ||
      (!dynamic_positions.empty() &&
       dynamic_positions.size() != mesh.vertices.size())) {
    return false;
  }
  std::vector<DynamicRenderVertexStatic> static_vertices =
      build_render_vertices<DynamicRenderVertexStatic>(mesh, dynamic_positions);
  std::vector<filament::math::float3> positions;
  positions.reserve(mesh.vertices.size());
  for (std::size_t index = 0u; index < mesh.vertices.size(); ++index) {
    positions.push_back(dynamic_positions.empty()
                            ? to_filament(mesh.vertices[index].position)
                            : to_filament(dynamic_positions[index]));
  }
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
          .attribute(filament::VertexAttribute::CUSTOM5, 0,
                     AttributeType::FLOAT3, 0, sizeof(filament::math::float3))
          .attribute(filament::VertexAttribute::CUSTOM6, 1,
                     AttributeType::FLOAT, offsetof(DynamicRenderVertexStatic, occurrence_index),
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
  if (geometry_cache_ != nullptr) {
    ++geometry_cache_->stats.dynamic_geometry_create_count;
    ++geometry_cache_->stats.dynamic_geometry_upload_count;
    geometry_cache_->stats.dynamic_uploaded_vertex_bytes +=
        mesh.vertices.size() *
        (sizeof(filament::math::float3) + sizeof(DynamicRenderVertexStatic));
    geometry_cache_->stats.dynamic_uploaded_index_bytes +=
        mesh.indices.size() * sizeof(std::uint32_t);
  }
  return true;
}

void MeshRenderableFactory::release_geometry(MeshRenderableProxy &proxy) {
  if (proxy.skinning_buffer != nullptr) {
    if (engine_ != nullptr && proxy.owns_mesh_buffers) {
      engine_->destroy(proxy.skinning_buffer);
      submit_resource_retirement(*engine_, pending_resource_retirements_);
      if (proxy.editable_geometry_state && geometry_cache_ != nullptr) {
        --geometry_cache_->stats.editable_surface_live_geometry_count;
      }
    }
    proxy.skinning_buffer = nullptr;
  }
  proxy.editable_geometry_state.reset();
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
    std::span<const QVec3> dynamic_positions,
    const std::shared_ptr<DynamicGeometryUpdate> &equivalent_update) {
  dynamic_positions = effective_mesh_positions(mesh, dynamic_positions);
  if (!dynamic_positions.empty() &&
      dynamic_positions.size() != mesh.vertices.size()) {
    return {};
  }
  try {
    auto prepared = std::make_shared<DynamicGeometryUpdate>();
    prepared->source = mesh;
    prepared->source_positions = dynamic_positions.data();
    prepared->source_position_count = dynamic_positions.size();
    if (equivalent_update) {
      if (equivalent_update->consumed || !equivalent_update->packed ||
          !same_dynamic_geometry(equivalent_update->source, mesh)) return {};
      const auto &positions = equivalent_update->packed->positions;
      if (positions.size() != mesh.vertices.size()) return {};
      for (std::size_t index = 0u; index < positions.size(); ++index) {
        const QVec3 expected = dynamic_positions.empty()
            ? mesh.vertices[index].position : dynamic_positions[index];
        if (!same_dynamic_vec3(expected,
                               {positions[index].x, positions[index].y,
                                positions[index].z})) return {};
      }
      prepared->packed = equivalent_update->packed;
    } else {
      auto packed = std::make_shared<DynamicGeometryUpdate::PackedStreams>();
      auto &positions = packed->positions;
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
        packed->static_vertices =
            build_render_vertices<DynamicRenderVertexStatic>(mesh, dynamic_positions);
        packed->indices.assign(mesh.indices.begin(), mesh.indices.end());
      }
      prepared->packed = std::move(packed);
    }
    // Each destination retains the shared bytes independently until Filament
    // releases its upload. Allocate every descriptor before the mutation pass.
    const auto &packed = *prepared->packed;
    prepared->positions = FilamentBufferDescriptorFactory::retained_bytes(
        packed.positions.data(), packed.positions.size() * sizeof(filament::math::float3),
        prepared->packed);
    prepared->static_vertices = FilamentBufferDescriptorFactory::retained_bytes(
        packed.static_vertices.data(),
        packed.static_vertices.size() * sizeof(DynamicRenderVertexStatic), prepared->packed);
    prepared->indices = FilamentBufferDescriptorFactory::retained_bytes(
        packed.indices.data(), packed.indices.size() * sizeof(std::uint32_t), prepared->packed);
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
  dynamic_positions = effective_mesh_positions(mesh, dynamic_positions);
  if (engine_ == nullptr || !proxy.dynamic_geometry ||
      proxy.skinning_buffer != nullptr ||
      !proxy.owns_mesh_buffers ||
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
      prepared->source.geometry_projection != mesh.geometry_projection ||
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
  if (geometry_cache_ != nullptr) {
    ++geometry_cache_->stats.dynamic_geometry_upload_count;
    geometry_cache_->stats.dynamic_uploaded_vertex_bytes +=
        mesh.vertices.size() *
        (sizeof(filament::math::float3) + sizeof(DynamicRenderVertexStatic));
    geometry_cache_->stats.dynamic_uploaded_index_bytes +=
        mesh.indices.size() * sizeof(std::uint32_t);
  }
  if (proxy.entity) {
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
      geometry_cache_ == nullptr || !mesh.positions.empty()) {
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
    if (proxy == nullptr || mesh == nullptr || !mesh->renderable() || !mesh->positions.empty() ||
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
  if (prototype.skinning_buffer != nullptr) return proxy;
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
      geometry_source.index_buffer == nullptr || geometry_source.skinning_buffer != nullptr) {
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

MeshRenderableProxy MeshRenderableFactory::create_dynamic_geometry(
    const RenderMeshAssetSnapshot &mesh,
    std::span<const QVec3> dynamic_positions) {
  QDR_PROFILE_FUNCTION();
  MeshRenderableProxy proxy;
  if (!initialized()) {
    return proxy;
  }
  MeshRenderableCreationGuard creation_guard(*this, proxy);
  proxy.mesh = mesh.mesh_id;
  proxy.mesh_revision = mesh.mesh_revision;
  if (!acquire_dynamic_geometry(mesh, proxy, dynamic_positions)) {
    return {};
  }
  creation_guard.release();
  return proxy;
}

MeshRenderableProxy MeshRenderableFactory::create_editable_surface_geometry(
    std::shared_ptr<EditableSurfaceUpdate> prepared) {
  QDR_PROFILE_FUNCTION();
  MeshRenderableProxy proxy;
  if (!initialized() || !prepared || !prepared->state || prepared->consumed ||
      prepared->reuses_geometry || prepared->position_ranges.size() != 1u ||
      prepared->attribute_ranges.size() != 1u) return proxy;
  MeshRenderableCreationGuard creation_guard(*this, proxy);
  prepared->consumed = true;
  const auto &state = *prepared->state;
  proxy.vertex_count = state.vertex_count;
  proxy.index_count = static_cast<std::uint32_t>(state.indices->size());
  proxy.dynamic_geometry = true;
  proxy.vertex_buffer = filament::VertexBuffer::Builder()
      .vertexCount(proxy.vertex_count).bufferCount(3u)
      .attribute(filament::VertexAttribute::POSITION, 0u, AttributeType::FLOAT3,
                 0u, sizeof(filament::math::float3))
      .attribute(filament::VertexAttribute::CUSTOM5, 0u, AttributeType::FLOAT3,
                 0u, sizeof(filament::math::float3))
      .attribute(filament::VertexAttribute::TANGENTS, 1u, AttributeType::FLOAT4,
                 offsetof(DynamicRenderVertexStatic, tangent_frame), sizeof(DynamicRenderVertexStatic))
      .attribute(filament::VertexAttribute::UV0, 1u, AttributeType::FLOAT2,
                 offsetof(DynamicRenderVertexStatic, uv), sizeof(DynamicRenderVertexStatic))
      .attribute(filament::VertexAttribute::COLOR, 1u, AttributeType::UBYTE4,
                 offsetof(DynamicRenderVertexStatic, color), sizeof(DynamicRenderVertexStatic))
      .attribute(filament::VertexAttribute::CUSTOM0, 1u, AttributeType::FLOAT4,
                 offsetof(DynamicRenderVertexStatic, surface_grid_projection), sizeof(DynamicRenderVertexStatic))
      .attribute(filament::VertexAttribute::CUSTOM1, 1u, AttributeType::FLOAT4,
                 offsetof(DynamicRenderVertexStatic, uv_carrier), sizeof(DynamicRenderVertexStatic))
      .attribute(filament::VertexAttribute::CUSTOM2, 1u, AttributeType::FLOAT3,
                 offsetof(DynamicRenderVertexStatic, uv_geometric_normal), sizeof(DynamicRenderVertexStatic))
      .attribute(filament::VertexAttribute::CUSTOM3, 1u, AttributeType::FLOAT4,
                 offsetof(DynamicRenderVertexStatic, quad_uv_coordinates), sizeof(DynamicRenderVertexStatic))
      .attribute(filament::VertexAttribute::CUSTOM4, 1u, AttributeType::FLOAT4,
                 offsetof(DynamicRenderVertexStatic, quad_uv_correction), sizeof(DynamicRenderVertexStatic))
      .attribute(filament::VertexAttribute::BONE_INDICES, 2u, AttributeType::USHORT4,
                 offsetof(EditableSurfaceVertexBinding, bone_indices), sizeof(EditableSurfaceVertexBinding))
      .attribute(filament::VertexAttribute::BONE_WEIGHTS, 2u, AttributeType::FLOAT4,
                 offsetof(EditableSurfaceVertexBinding, bone_weights), sizeof(EditableSurfaceVertexBinding))
      .attribute(filament::VertexAttribute::CUSTOM6, 2u, AttributeType::FLOAT,
                 offsetof(EditableSurfaceVertexBinding, occurrence_index), sizeof(EditableSurfaceVertexBinding))
      .normalized(filament::VertexAttribute::COLOR).build(*engine_);
  proxy.index_buffer = filament::IndexBuffer::Builder().indexCount(proxy.index_count)
      .bufferType(filament::IndexBuffer::IndexType::UINT).build(*engine_);
  proxy.skinning_buffer = filament::SkinningBuffer::Builder()
      .boneCount(static_cast<std::uint32_t>(state.records.size())).initialize(false).build(*engine_);
  if (proxy.vertex_buffer == nullptr || proxy.index_buffer == nullptr || proxy.skinning_buffer == nullptr) return {};
  proxy.vertex_buffer->setBufferAt(*engine_, 0u, std::move(prepared->position_ranges.front().upload));
  proxy.vertex_buffer->setBufferAt(*engine_, 1u, std::move(prepared->attribute_ranges.front().upload));
  proxy.vertex_buffer->setBufferAt(*engine_, 2u, std::move(prepared->binding_upload));
  proxy.index_buffer->setBuffer(*engine_, std::move(prepared->index_upload));
  proxy.skinning_buffer->setBones(*engine_, state.transforms.data(), state.transforms.size());
  proxy.editable_geometry_state = prepared->state;
  prepared->expected_vertices = proxy.vertex_buffer;
  prepared->expected_indices = proxy.index_buffer;
  prepared->expected_palette = proxy.skinning_buffer;
  auto &stats = geometry_cache_->stats;
  ++stats.editable_surface_live_geometry_count;
  ++stats.editable_surface_geometry_create_count;
  stats.editable_surface_attribute_pack_count += prepared->attribute_pack_count;
  stats.editable_surface_canonical_reuse_count += prepared->canonical_reuse_count;
  ++stats.editable_surface_position_upload_count;
  ++stats.editable_surface_attribute_upload_count;
  ++stats.editable_surface_index_upload_count;
  ++stats.editable_surface_palette_upload_count;
  stats.editable_surface_uploaded_vertex_bytes += static_cast<std::uint64_t>(state.vertex_count) *
      (sizeof(filament::math::float3) + sizeof(DynamicRenderVertexStatic) + sizeof(EditableSurfaceVertexBinding));
  stats.editable_surface_uploaded_index_bytes += state.indices->size() * sizeof(std::uint32_t);
  creation_guard.release();
  return proxy;
}

MeshRenderableProxy MeshRenderableFactory::create_editable_surface_proxy(
    const MeshRenderableProxy &geometry_owner, const RenderMaterialAssetSnapshot *material,
    const RenderInstanceBounds &world_bounds, bool cast_shadows, bool receive_shadows,
    bool double_sided, std::uint8_t layer_mask, bool editor_unlit, bool depth_only,
    std::uint8_t channel, std::uint8_t presentation_flags) {
  QDR_PROFILE_FUNCTION();
  MeshRenderableProxy proxy;
  proxy.owns_mesh_buffers = false;
  if (!initialized() || !geometry_owner.owns_mesh_buffers || !geometry_owner.editable_geometry_state ||
      geometry_owner.vertex_buffer == nullptr || geometry_owner.index_buffer == nullptr ||
      geometry_owner.skinning_buffer == nullptr || !world_bounds.present) return proxy;
  MeshRenderableCreationGuard creation_guard(*this, proxy);
  const auto &state = *geometry_owner.editable_geometry_state;
  proxy.vertex_buffer = geometry_owner.vertex_buffer;
  proxy.index_buffer = geometry_owner.index_buffer;
  proxy.skinning_buffer = geometry_owner.skinning_buffer;
  proxy.vertex_count = geometry_owner.vertex_count;
  proxy.index_count = geometry_owner.index_count;
  proxy.dynamic_geometry = true;
  proxy.cast_shadows = cast_shadows;
  proxy.double_sided = double_sided;
  proxy.material_instances.reserve(1u);
  proxy.material_slots.reserve(1u);
  auto *material_instance = material_binding_->create_mesh_material_instance(material, double_sided, editor_unlit);
  if (material_instance == nullptr) return {};
  proxy.material_instances.push_back(material_instance);
  proxy.material_slots.push_back(0u);
  material_binding_->apply_instance_edit_dim(*material_instance,
      (presentation_flags & static_cast<std::uint8_t>(RenderInstancePresentationFlag::Dimmed)) != 0u);
  bind_coarse_mesh_parameters(*material_instance, state.mappings, state.mapping_cofactors, state.world_normals);
  if (depth_only) {
    material_instance->setColorWrite(false);
    material_instance->setDepthWrite(true);
    material_instance->setDepthCulling(true);
  }
  proxy.entity = utils::EntityManager::get().create();
  const auto result = filament::RenderableManager::Builder(1u)
      .geometry(0u, filament::RenderableManager::PrimitiveType::TRIANGLES,
                proxy.vertex_buffer, proxy.index_buffer, 0u, proxy.index_count)
      .material(0u, material_instance).enableSkinningBuffers(true)
      .skinning(proxy.skinning_buffer, state.records.size(), 0u)
      .boundingBox(filament_bounds(state.local_bounds)).layerMask(kViewportAllLayerMask, layer_mask)
      .channel(channel).castShadows(cast_shadows).receiveShadows(receive_shadows)
      .build(*engine_, proxy.entity);
  if (result != filament::RenderableManager::Builder::Success) return {};
  engine_->getTransformManager().create(proxy.entity);
  QMat4 world_from_anchor;
  world_from_anchor.values[12] = state.world_anchor.x;
  world_from_anchor.values[13] = state.world_anchor.y;
  world_from_anchor.values[14] = state.world_anchor.z;
  update_transform(proxy, world_from_anchor);
  scene_->addEntity(proxy.entity);
  creation_guard.mark_scene_added();
  creation_guard.release();
  return proxy;
}

bool MeshRenderableFactory::validate_editable_surface_update(
    const MeshRenderableProxy &owner, const EditableSurfaceUpdate &prepared) const {
  return initialized() && prepared.state && !prepared.consumed && prepared.reuses_geometry &&
      owner.owns_mesh_buffers && owner.editable_geometry_state == prepared.previous &&
      owner.vertex_buffer == prepared.expected_vertices && owner.index_buffer == prepared.expected_indices &&
      owner.skinning_buffer == prepared.expected_palette && owner.skinning_buffer != nullptr &&
      owner.vertex_count == prepared.state->vertex_count && owner.index_count == prepared.state->indices->size();
}

bool MeshRenderableFactory::validate_editable_surface_presentation(
    const MeshRenderableProxy &proxy, const MeshRenderableProxy &owner,
    const EditableSurfaceUpdate &prepared) const {
  if (!initialized() || !prepared.state || !proxy.entity || proxy.owns_mesh_buffers ||
      proxy.vertex_buffer != owner.vertex_buffer || proxy.index_buffer != owner.index_buffer ||
      proxy.skinning_buffer != owner.skinning_buffer || proxy.skinning_buffer == nullptr ||
      proxy.material_instances.size() != 1u || proxy.material_instances.front() == nullptr ||
      (owner.editable_geometry_state != prepared.previous && owner.editable_geometry_state != prepared.state)) return false;
  const auto &renderables = engine_->getRenderableManager();
  const auto instance = renderables.getInstance(proxy.entity);
  return instance && renderables.getPrimitiveCount(instance) == 1u;
}

bool MeshRenderableFactory::update_editable_surface_geometry(
    MeshRenderableProxy &owner, std::shared_ptr<EditableSurfaceUpdate> prepared) {
  QDR_PROFILE_FUNCTION();
  if (!prepared || !validate_editable_surface_update(owner, *prepared)) return false;
  prepared->consumed = true;
  auto &stats = geometry_cache_->stats;
  for (auto &range : prepared->position_ranges) {
    owner.vertex_buffer->setBufferAt(*engine_, 0u, std::move(range.upload),
        range.first_vertex * sizeof(filament::math::float3));
    ++stats.editable_surface_position_upload_count;
    stats.editable_surface_uploaded_vertex_bytes += range.byte_count;
  }
  for (auto &range : prepared->attribute_ranges) {
    owner.vertex_buffer->setBufferAt(*engine_, 1u, std::move(range.upload),
        range.first_vertex * sizeof(DynamicRenderVertexStatic));
    ++stats.editable_surface_attribute_upload_count;
    stats.editable_surface_uploaded_vertex_bytes += range.byte_count;
  }
  if (prepared->indices_changed) {
    owner.index_buffer->setBuffer(*engine_, std::move(prepared->index_upload));
    ++stats.editable_surface_index_upload_count;
    stats.editable_surface_uploaded_index_bytes += prepared->state->indices->size() * sizeof(std::uint32_t);
  }
  if (prepared->palette_changed) {
    owner.skinning_buffer->setBones(*engine_, prepared->state->transforms.data(), prepared->state->transforms.size());
    ++stats.editable_surface_palette_upload_count;
  }
  stats.editable_surface_attribute_pack_count += prepared->attribute_pack_count;
  stats.editable_surface_canonical_reuse_count += prepared->canonical_reuse_count;
  owner.editable_geometry_state = prepared->state;
  return true;
}

bool MeshRenderableFactory::update_editable_surface_presentation(
    MeshRenderableProxy &proxy, const EditableSurfaceUpdate &prepared,
    const RenderInstanceBounds &world_bounds, bool cast_shadows) {
  if (!initialized() || !prepared.state || !proxy.entity || proxy.owns_mesh_buffers ||
      proxy.vertex_buffer != prepared.expected_vertices || proxy.index_buffer != prepared.expected_indices ||
      proxy.skinning_buffer != prepared.expected_palette || proxy.material_instances.size() != 1u ||
      proxy.material_instances.front() == nullptr || !world_bounds.present) return false;
  auto &renderables = engine_->getRenderableManager();
  const auto instance = renderables.getInstance(proxy.entity);
  if (!instance) return false;
  if (prepared.palette_changed || prepared.mappings_changed) {
    bind_coarse_mesh_parameters(*proxy.material_instances.front(), prepared.state->mappings,
        prepared.state->mapping_cofactors, prepared.state->world_normals);
  }
  if (prepared.bounds_changed) renderables.setAxisAlignedBoundingBox(instance, filament_bounds(prepared.state->local_bounds));
  if (prepared.anchor_changed) {
    QMat4 world_from_anchor;
    world_from_anchor.values[12] = prepared.state->world_anchor.x;
    world_from_anchor.values[13] = prepared.state->world_anchor.y;
    world_from_anchor.values[14] = prepared.state->world_anchor.z;
    update_transform(proxy, world_from_anchor);
  }
  if (proxy.cast_shadows != cast_shadows) {
    renderables.setCastShadows(instance, cast_shadows);
    proxy.cast_shadows = cast_shadows;
  }
  return true;
}

MeshRenderableProxy MeshRenderableFactory::create_explicit_instances(
    const RenderMeshAssetSnapshot &mesh,
    const RenderMaterialBindingSnapshot *material,
    MeshPrimitiveSpan primitive, std::span<const QMat4> world_transforms,
    const RenderInstanceBounds &world_bounds, bool cast_shadows,
    bool receive_shadows, bool double_sided, std::uint8_t layer_mask,
    bool editor_unlit, bool depth_only, std::uint8_t channel,
    std::uint8_t presentation_flags, std::size_t instance_capacity,
    const MeshRenderableProxy *geometry_source) {
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
  if (geometry_source != nullptr &&
      (!geometry_source->dynamic_geometry ||
       geometry_source->skinning_buffer != nullptr ||
       !geometry_source->owns_mesh_buffers ||
       geometry_source->vertex_buffer == nullptr ||
       geometry_source->index_buffer == nullptr ||
       geometry_source->vertex_count != mesh.vertices.size() ||
       geometry_source->index_count != mesh.indices.size())) {
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
  if (geometry_source != nullptr) {
    proxy.owns_mesh_buffers = false;
    proxy.dynamic_geometry = true;
    proxy.vertex_buffer = geometry_source->vertex_buffer;
    proxy.index_buffer = geometry_source->index_buffer;
    proxy.vertex_count = geometry_source->vertex_count;
    proxy.index_count = geometry_source->index_count;
  } else if (!acquire_geometry(mesh, proxy)) {
    return proxy;
  }

  const RenderMaterialAssetSnapshot *primitive_material =
      material != nullptr ? material->material_for_slot(primitive.material_slot)
                          : nullptr;
  // Reserve ownership storage before creating a material so allocation failure
  // cannot leave an untracked instance outside the construction guard.
  proxy.material_slots.reserve(1u);
  proxy.material_instances.reserve(1u);
  filament::MaterialInstance *material_instance =
      material_binding_->create_mesh_material_instance(
          primitive_material, double_sided, editor_unlit);
  if (material_instance == nullptr) {
    return proxy;
  }
  proxy.material_slots.push_back(primitive.material_slot);
  proxy.material_instances.push_back(material_instance);
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

void MeshRenderableFactory::update_instance_selection_fill_color(
    const MeshRenderableProxy &proxy, std::uint32_t color,
    bool active_selected) {
  constexpr float kInverseByte = 1.0f / 255.0f;
  const filament::math::float4 fill_color{
      srgb_to_linear(static_cast<float>(color & 0xffu) * kInverseByte),
      srgb_to_linear(static_cast<float>((color >> 8u) & 0xffu) * kInverseByte),
      srgb_to_linear(static_cast<float>((color >> 16u) & 0xffu) * kInverseByte),
      static_cast<float>(color >> 24u) * kInverseByte,
  };
  const auto fill_class = visible_overlay_face_fill_class(
      active_selected ? OverlayFaceFillClass::ActiveSelection
                      : OverlayFaceFillClass::Selection, color);
  const bool stencil_enabled =
      engine_->getBackend() == filament::Engine::Backend::VULKAN;
  constexpr std::array kPasses{OverlayFaceFillPass::Mask, OverlayFaceFillPass::Color};
  for (std::size_t index = 0u; index < proxy.material_instances.size(); ++index) {
    auto *material = proxy.material_instances[index];
    if (material != nullptr && index < kPasses.size()) {
      material->setParameter("fillColor", fill_color);
      apply_overlay_face_fill_stencil(*material, fill_class, kPasses[index],
                                    stencil_enabled);
    }
  }
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
  const auto prepared = prepare_explicit_instance_update(world_transforms);
  return prepared != nullptr &&
         update_explicit_instances_prepared(proxy, *prepared, world_bounds);
}

std::shared_ptr<MeshRenderableFactory::ExplicitInstanceUpdate>
MeshRenderableFactory::prepare_explicit_instance_update(
    std::span<const QMat4> world_transforms) {
  if (world_transforms.empty()) {
    return {};
  }
  auto prepared = std::make_shared<ExplicitInstanceUpdate>();
  prepared->transforms.reserve(world_transforms.size());
  for (const QMat4 &transform : world_transforms) {
    prepared->transforms.push_back(make_transform(transform));
  }
  return prepared;
}

bool MeshRenderableFactory::validate_explicit_instances(
    const MeshRenderableProxy &proxy,
    const ExplicitInstanceUpdate &prepared) const {
  if (!initialized() || proxy.instance_buffer == nullptr || !proxy.entity ||
      prepared.transforms.empty() ||
      prepared.transforms.size() > proxy.instance_buffer->getInstanceCount()) {
    return false;
  }
  const auto &renderables = engine_->getRenderableManager();
  const auto renderable = renderables.getInstance(proxy.entity);
  return renderable && renderables.getInstanceCount(renderable) ==
                           prepared.transforms.size();
}

bool MeshRenderableFactory::update_explicit_instances_prepared(
    const MeshRenderableProxy &proxy,
    const ExplicitInstanceUpdate &prepared,
    const RenderInstanceBounds &world_bounds) {
  QDR_PROFILE_FUNCTION();
  if (!validate_explicit_instances(proxy, prepared)) {
    return false;
  }
  auto &renderables = engine_->getRenderableManager();
  const auto renderable = renderables.getInstance(proxy.entity);
  proxy.instance_buffer->setLocalTransforms(prepared.transforms.data(),
                                            prepared.transforms.size());
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
