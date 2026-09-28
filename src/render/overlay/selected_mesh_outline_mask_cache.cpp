////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/selected_mesh_outline_mask_cache.hpp"
#include "diagnostics/profile.hpp"

#include "math/glm_bridge.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"

#include <backend/DriverEnums.h>
#include <filament/Box.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <math/mat4.h>
#include <math/vec3.h>
#include <utils/Entity.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr std::uint32_t kMaximumBatchIndices = 196'608u;
constexpr std::size_t kResourceChangesPerSubmission = 32u;
constexpr std::uint32_t kUnmappedVertex = std::numeric_limits<std::uint32_t>::max();
constexpr double kMaximumBatchOffset = 65'536.0;

bool finite_position(glm::dvec3 point) {
  return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

/// Pins immutable source payloads, retaining only cheap geometry identity facts.
struct MaskOccurrenceSignature final {
  std::shared_ptr<const MeshGeometryPayload> payload;
  MeshGeometryContentId content_id;
  AssetId mesh_id = kInvalidAssetId;
  std::uint64_t mesh_revision = 0u;
  const MeshVertex *vertices = nullptr;
  std::size_t vertex_count = 0u;
  const std::uint32_t *indices = nullptr;
  std::size_t index_count = 0u;
  std::array<float, 16u> transform{};
  bool active = false;
  friend bool operator==(const MaskOccurrenceSignature &,
                         const MaskOccurrenceSignature &) = default;
};

/// Borrows source spans only for the synchronous rebuild that captures them.
struct MaskOccurrence final {
  MaskOccurrenceSignature signature;
  QMat4 transform;
  std::span<const MeshVertex> vertices;
  std::span<const std::uint32_t> indices;
};

/// Describes one bounded POSITION-only world-space upload.
struct MaskGeometryBatch final {
  std::vector<filament::math::float3> positions;
  std::vector<std::uint32_t> indices;
  glm::dvec3 origin{};
  QVec3 minimum{std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::max()};
  QVec3 maximum{std::numeric_limits<float>::lowest(),
                 std::numeric_limits<float>::lowest(),
                 std::numeric_limits<float>::lowest()};

  void append(glm::dvec3 world_position) {
    if (positions.empty()) {
      // A float-representable origin also works when the engine does not keep
      // accurate double translations. Preserve small vertex offsets separately.
      origin = glm::dvec3(glm::vec3(world_position));
    }
    const glm::vec3 local(world_position - origin);
    const QVec3 position{local.x, local.y, local.z};
    positions.push_back({position.x, position.y, position.z});
    minimum.x = std::min(minimum.x, position.x);
    minimum.y = std::min(minimum.y, position.y);
    minimum.z = std::min(minimum.z, position.z);
    maximum.x = std::max(maximum.x, position.x);
    maximum.y = std::max(maximum.y, position.y);
    maximum.z = std::max(maximum.z, position.z);
  }

  filament::Box bounds() const {
    const auto center = [](float low, float high) {
      return static_cast<float>((static_cast<double>(low) + high) * 0.5);
    };
    const auto extent = [](float low, float high) {
      return static_cast<float>((static_cast<double>(high) - low) * 0.5);
    };
    return {{center(minimum.x, maximum.x), center(minimum.y, maximum.y),
             center(minimum.z, maximum.z)},
            {extent(minimum.x, maximum.x), extent(minimum.y, maximum.y),
             extent(minimum.z, maximum.z)}};
  }
};

/// Holds GPU resources; the cache detaches the complete set before retirement.
struct MaskGpuBatch final {
  utils::Entity entity;
  filament::VertexBuffer *vertices = nullptr;
  filament::IndexBuffer *indices = nullptr;
  bool active = false;
};

/// Captures O(1) owner publication facts before any occurrence enumeration.
struct MaskSourceSignature final {
  const RenderOverlaySelectedObjectSnapshot *selected = nullptr;
  const RenderOverlaySelectionSnapshot *hierarchy = nullptr;
  RenderSourceStamp selected_stamp;
  RenderSourceStamp hierarchy_stamp;
  const void *meshes = nullptr;
  std::size_t mesh_count = 0u;
  const void *instances = nullptr;
  std::size_t instance_count = 0u;
  const void *hierarchy_meshes = nullptr;
  std::size_t hierarchy_count = 0u;
  bool visible = false;
  friend bool operator==(const MaskSourceSignature &,
                         const MaskSourceSignature &) = default;
};

MaskSourceSignature source_signature(
    const RenderOverlaySelectedObjectSnapshot *selected,
    const RenderOverlaySelectionSnapshot *hierarchy) {
  MaskSourceSignature result{.selected = selected, .hierarchy = hierarchy};
  if (selected != nullptr) {
    result.selected_stamp = *selected;
    result.visible = selected->visible;
    result.meshes = selected->meshes.data();
    result.mesh_count = selected->meshes.size();
    result.instances = selected->instance_selection_fills.data();
    result.instance_count = selected->instance_selection_fills.size();
  }
  if (hierarchy != nullptr) {
    result.hierarchy_stamp = *hierarchy;
    result.hierarchy_meshes = hierarchy->hierarchy_selection_wires.data();
    result.hierarchy_count = hierarchy->hierarchy_selection_wires.size();
  }
  return result;
}

} // namespace

/// Owns one mask scene and shared material, never document or selection state.
struct SelectedMeshOutlineMaskCache::Impl final {
  ~Impl() { shutdown(); }

  void record_resource_change() {
    if (++pending_resource_changes >= kResourceChangesPerSubmission) {
      flush_commands();
    }
  }

  void flush_commands() {
    if (engine != nullptr && pending_resource_changes != 0u) {
      engine->flush();
      pending_resource_changes = 0u;
      ++stats.command_submissions;
    }
  }

  void destroy_batch(MaskGpuBatch &batch) {
    if (batch.entity) {
      engine->destroy(batch.entity);
      utils::EntityManager::get().destroy(batch.entity);
      batch.entity.clear();
    }
    if (batch.vertices != nullptr) {
      engine->destroy(batch.vertices);
      batch.vertices = nullptr;
    }
    if (batch.indices != nullptr) {
      engine->destroy(batch.indices);
      batch.indices = nullptr;
    }
    record_resource_change();
  }

  void retire_batches() {
    for (const auto &batch : batches) {
      if (batch.entity) {
        mask_scene->remove(batch.entity);
        engine->getRenderableManager().destroy(batch.entity);
        record_resource_change();
      }
    }
    for (auto &batch : batches) destroy_batch(batch);
    batches.clear();
    stats.ordinary_batches = 0u;
    stats.active_batches = 0u;
  }

  void shutdown() {
    if (engine == nullptr) return;
    retire_batches();
    if (instance != nullptr) {
      engine->destroy(instance);
      instance = nullptr;
      record_resource_change();
    }
    if (mask_scene != nullptr) {
      engine->destroy(mask_scene);
      mask_scene = nullptr;
      record_resource_change();
    }
    flush_commands();
    engine = nullptr;
    geometry_ready = false;
    valid_source = false;
    retained_occurrences.clear();
    source = {};
  }

  bool upload(MaskGeometryBatch &geometry, bool active) {
    if (geometry.indices.empty()) return true;
    const auto vertex_count = static_cast<std::uint32_t>(geometry.positions.size());
    const auto index_count = static_cast<std::uint32_t>(geometry.indices.size());
    const auto bounds = geometry.bounds();
    const auto origin = geometry.origin;
    MaskGpuBatch batch{.active = active};
    batch.vertices = filament::VertexBuffer::Builder()
        .vertexCount(vertex_count)
        .bufferCount(1u)
        .attribute(filament::VertexAttribute::POSITION, 0u,
                   filament::backend::ElementType::FLOAT3, 0u,
                   sizeof(filament::math::float3))
        .build(*engine);
    batch.indices = filament::IndexBuffer::Builder()
        .indexCount(index_count)
        .bufferType(filament::IndexBuffer::IndexType::UINT)
        .build(*engine);
    if (batch.vertices == nullptr || batch.indices == nullptr) {
      destroy_batch(batch);
      return false;
    }
    batch.vertices->setBufferAt(*engine, 0u,
        FilamentBufferDescriptorFactory::vector(std::move(geometry.positions)));
    batch.indices->setBuffer(*engine,
        FilamentBufferDescriptorFactory::vector(std::move(geometry.indices)));
    batch.entity = utils::EntityManager::get().create();
    const auto result = filament::RenderableManager::Builder(1u)
        .geometry(0u, filament::RenderableManager::PrimitiveType::TRIANGLES,
                   batch.vertices, batch.indices)
        .material(0u, instance)
        .layerMask(0xffu, active ? kActiveLayer : kOrdinaryLayer)
        .boundingBox(bounds)
        .culling(false)
        .castShadows(false)
        .receiveShadows(false)
        .build(*engine, batch.entity);
    if (result != filament::RenderableManager::Builder::Success) {
      destroy_batch(batch);
      return false;
    }
    engine->getTransformManager().create(batch.entity, {},
        filament::math::mat4::translation(
            filament::math::double3{origin.x, origin.y, origin.z}));
    mask_scene->addEntity(batch.entity);
    batches.push_back(batch);
    stats.uploaded_vertices += vertex_count;
    stats.uploaded_indices += index_count;
    if (active) ++stats.active_batches;
    else ++stats.ordinary_batches;
    record_resource_change();
    geometry = {};
    return true;
  }

  bool rebuild(std::span<const MaskOccurrence> occurrences) {
    QDR_PROFILE_FUNCTION();
    retire_batches();
    std::array<MaskGeometryBatch, 2u> geometry;
    std::vector<glm::dvec3> world_positions;
    std::vector<std::uint32_t> vertex_remap;
    for (const auto &occurrence : occurrences) {
      auto &batch = geometry[occurrence.signature.active ? 1u : 0u];
      world_positions.resize(occurrence.vertices.size());
      vertex_remap.assign(occurrence.vertices.size(), kUnmappedVertex);
      const glm::dmat4 transform(to_glm(occurrence.transform));
      for (std::size_t index = 0u; index < occurrence.vertices.size(); ++index) {
        const glm::dvec4 point = transform * glm::dvec4(
            glm::dvec3(to_glm(occurrence.vertices[index].position)), 1.0);
        world_positions[index] = glm::dvec3(point) / point.w;
      }
      for (std::size_t triangle = 0u; triangle + 2u < occurrence.indices.size(); triangle += 3u) {
        const std::array indices{occurrence.indices[triangle],
            occurrence.indices[triangle + 1u], occurrence.indices[triangle + 2u]};
        if (std::ranges::any_of(indices, [&](std::uint32_t index) {
              return index >= world_positions.size() || !finite_position(world_positions[index]);
            })) continue;
        const auto normal = glm::cross(world_positions[indices[1u]] - world_positions[indices[0u]],
                                       world_positions[indices[2u]] - world_positions[indices[0u]]);
        if (!finite_position(normal) || glm::dot(normal, normal) <= 0.0) continue;
        // Bound float offsets within a draw while retaining whole triangles.
        // Widely separated scene clusters receive independent local origins.
        const bool distant = !batch.positions.empty() &&
            std::ranges::any_of(indices, [&](std::uint32_t index) {
              const auto offset = glm::abs(world_positions[index] - batch.origin);
              return std::max({offset.x, offset.y, offset.z}) > kMaximumBatchOffset;
            });
        const glm::dvec3 next_origin = batch.positions.empty() || distant
            ? glm::dvec3(glm::vec3(world_positions[indices[0u]])) : batch.origin;
        if (!finite_position(next_origin) ||
            std::ranges::any_of(indices, [&](std::uint32_t index) {
              const auto offset = glm::abs(world_positions[index] - next_origin);
              return std::max({offset.x, offset.y, offset.z}) >
                     static_cast<double>(std::numeric_limits<float>::max());
            })) continue;
        std::size_t unmapped = 0u;
        for (const auto index : indices) unmapped += vertex_remap[index] == kUnmappedVertex;
        if (distant || batch.positions.size() + unmapped > kMaximumBatchVertices ||
            batch.indices.size() + 3u > kMaximumBatchIndices) {
          if (!upload(batch, occurrence.signature.active)) return false;
          std::fill(vertex_remap.begin(), vertex_remap.end(), kUnmappedVertex);
        }
        for (const auto index : indices) {
          auto &mapped = vertex_remap[index];
          if (mapped == kUnmappedVertex) {
            mapped = static_cast<std::uint32_t>(batch.positions.size());
            batch.append(world_positions[index]);
          }
          batch.indices.push_back(mapped);
        }
      }
    }
    if (!upload(geometry[0u], false) || !upload(geometry[1u], true)) return false;
    return true;
  }

  filament::Engine *engine = nullptr;
  filament::Scene *mask_scene = nullptr;
  filament::MaterialInstance *instance = nullptr;
  std::vector<MaskGpuBatch> batches;
  std::vector<MaskOccurrenceSignature> retained_occurrences;
  MaskSourceSignature source;
  bool valid_source = false;
  bool geometry_ready = false;
  std::size_t pending_resource_changes = 0u;
  SelectedMeshOutlineMaskWorkStats stats;
};

SelectedMeshOutlineMaskCache::SelectedMeshOutlineMaskCache()
    : impl_(std::make_unique<Impl>()) {}

SelectedMeshOutlineMaskCache::~SelectedMeshOutlineMaskCache() = default;

bool SelectedMeshOutlineMaskCache::initialize(filament::Engine &engine,
                                              filament::Material &mask_material) {
  impl_->shutdown();
  impl_->stats = {};
  impl_->engine = &engine;
  impl_->mask_scene = engine.createScene();
  impl_->instance = mask_material.createInstance();
  if (impl_->mask_scene == nullptr || impl_->instance == nullptr) {
    impl_->shutdown();
    return false;
  }
  impl_->instance->setCullingMode(filament::MaterialInstance::CullingMode::NONE);
  impl_->instance->setDepthWrite(true);
  impl_->instance->setDepthCulling(true);
  impl_->instance->setColorWrite(true);
  impl_->record_resource_change();
  impl_->flush_commands();
  impl_->geometry_ready = true;
  return true;
}

void SelectedMeshOutlineMaskCache::shutdown() { impl_->shutdown(); }

bool SelectedMeshOutlineMaskCache::sync(
    const RenderOverlaySelectedObjectSnapshot *snapshot,
    const RenderOverlaySelectionSnapshot *hierarchy) {
  QDR_PROFILE_FUNCTION();
  if (impl_->engine == nullptr) return false;
  const auto signature = source_signature(snapshot, hierarchy);
  if (impl_->valid_source && impl_->source == signature) return false;
  ++impl_->stats.source_refreshes;
  std::vector<MaskOccurrence> occurrences;
  occurrences.reserve((signature.visible ? signature.mesh_count + signature.instance_count : 0u) +
                      signature.hierarchy_count);
  const auto append = [&](const auto &mesh) {
    if (mesh.vertices.empty() || mesh.indices.size() < 3u) return;
    occurrences.push_back({
        .signature = {.payload = mesh.geometry_payload,
            .content_id = mesh.geometry_content_id, .mesh_id = mesh.mesh_id,
            .mesh_revision = mesh.mesh_revision,
            .vertices = mesh.vertices.data(), .vertex_count = mesh.vertices.size(),
            .indices = mesh.indices.data(), .index_count = mesh.indices.size(),
            .transform = mesh.world_transform.values, .active = mesh.active_selected},
        .transform = mesh.world_transform,
        .vertices = mesh.vertices,
        .indices = mesh.indices,
    });
  };
  if (signature.visible) {
    for (const auto &mesh : snapshot->meshes) append(mesh);
    for (const auto &mesh : snapshot->instance_selection_fills) append(mesh);
  }
  if (hierarchy != nullptr) {
    for (const auto &mesh : hierarchy->hierarchy_selection_wires) append(mesh);
  }
  const bool same_geometry = impl_->retained_occurrences.size() == occurrences.size() &&
      std::equal(occurrences.begin(), occurrences.end(), impl_->retained_occurrences.begin(),
          [](const MaskOccurrence &next, const MaskOccurrenceSignature &retained) {
            return next.signature == retained;
          });
  impl_->source = signature;
  impl_->valid_source = true;
  if (same_geometry) {
    impl_->geometry_ready = true;
    return false;
  }
  ++impl_->stats.geometry_builds;
  if (!impl_->rebuild(occurrences)) {
    impl_->retire_batches();
    impl_->valid_source = false;
    impl_->geometry_ready = false;
    impl_->retained_occurrences.clear();
    impl_->flush_commands();
    return true;
  }
  impl_->retained_occurrences.clear();
  impl_->retained_occurrences.reserve(occurrences.size());
  for (auto &occurrence : occurrences) {
    impl_->retained_occurrences.push_back(std::move(occurrence.signature));
  }
  impl_->flush_commands();
  impl_->geometry_ready = true;
  return true;
}

filament::Scene *SelectedMeshOutlineMaskCache::scene() const noexcept {
  return impl_->mask_scene;
}

bool SelectedMeshOutlineMaskCache::ready() const noexcept {
  return impl_->geometry_ready;
}

bool SelectedMeshOutlineMaskCache::has_geometry(bool active) const noexcept {
  return active ? impl_->stats.active_batches != 0u : impl_->stats.ordinary_batches != 0u;
}

SelectedMeshOutlineMaskWorkStats SelectedMeshOutlineMaskCache::work_stats() const noexcept {
  return impl_->stats;
}

} // namespace quader
