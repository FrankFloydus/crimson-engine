////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/editable_mesh_surface_cache.hpp"

#include "assets/mesh_geometry_projection.hpp"
#include "diagnostics/profile.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/render_signature_builder.hpp"
#include "render/texture_binding_service.hpp"
#include "viewport/viewport_settings.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <cmath>
#include <optional>
#include <limits>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

/// Pins one actually drawn material and its original canonical primitive ranges.
struct SurfacePart final {
  std::uint32_t slot = 0u;
  std::vector<MeshPrimitiveSpan> primitives;
  std::shared_ptr<const RenderMaterialAssetSnapshot> material;
  std::uint64_t material_signature = 0u;
  std::size_t index_count = 0u;
};

/// Holds immutable source facts; GPU resources never become document truth.
struct SurfaceObject final {
  EntityId entity = kInvalidEntityId;
  RenderMeshAssetSnapshot mesh;
  std::shared_ptr<const std::vector<SurfacePart>> parts;
  std::shared_ptr<const std::vector<QVec3>> position_payload;
  QMat4 transform;
  QMat4 sampling;
  std::uint64_t position_revision = 0u;
  bool cast_shadows = false;
  bool double_sided = false;
  bool dimmed = false;
  bool preview = false;
};

using SurfaceObjects = std::unordered_map<EntityId, SurfaceObject>;
using MaterialPool = std::unordered_map<std::uint64_t,
    std::vector<std::shared_ptr<const RenderMaterialAssetSnapshot>>>;

bool same_matrix(const QMat4 &left, const QMat4 &right) {
  return left.values == right.values;
}

bool same_primitives(std::span<const MeshPrimitiveSpan> left,
                     std::span<const MeshPrimitiveSpan> right) {
  return left.size() == right.size() &&
         (left.data() == right.data() || std::equal(left.begin(), left.end(), right.begin()));
}

std::shared_ptr<const std::vector<SurfacePart>> resolve_parts(
    const RenderMeshAssetSnapshot &mesh,
    const RenderMaterialBindingSnapshot &binding,
    const SurfaceObject *previous, MaterialPool &materials) {
  if (previous && previous->parts &&
      same_primitives(previous->mesh.primitives, mesh.primitives) &&
      previous->mesh.indices.size() == mesh.indices.size() &&
      std::ranges::all_of(*previous->parts, [&](const SurfacePart &part) {
        return render_material_payloads_equal(part.material.get(),
                                               binding.material_for_slot(part.slot));
      })) {
    return previous->parts;
  }
  auto parts = std::make_shared<std::vector<SurfacePart>>();
  for (const auto primitive : coalesced_render_mesh_primitive_spans(mesh)) {
    const auto *material = binding.material_for_slot(primitive.material_slot);
    const auto signature = render_material_payload_signature(material);
    auto &bucket = materials[signature];
    auto found = std::ranges::find_if(bucket, [&](const auto &candidate) {
      return render_material_payloads_equal(candidate.get(), material);
    });
    if (found == bucket.end()) {
      bucket.push_back(std::make_shared<RenderMaterialAssetSnapshot>(
          material ? *material : RenderMaterialAssetSnapshot{}));
      found = std::prev(bucket.end());
    }
    auto part = std::ranges::find_if(*parts, [&](const SurfacePart &candidate) {
      return candidate.slot == primitive.material_slot;
    });
    if (part == parts->end()) {
      parts->push_back({.slot = primitive.material_slot,
          .material = *found, .material_signature = signature});
      part = std::prev(parts->end());
    }
    part->primitives.push_back(primitive);
    part->index_count += primitive.index_count;
  }
  return parts;
}

/// Identifies a stable occurrence range inside one material-compatible batch.
struct SurfaceMember final {
  EntityId entity = kInvalidEntityId;
  std::uint32_t slot = 0u;
  friend bool operator==(const SurfaceMember &, const SurfaceMember &) = default;
};

std::uint64_t member_key(SurfaceMember member) {
  return RenderSignatureBuilder{}.add_value(member.entity).add_value(member.slot).value();
}

/// Owns a palette/geometry allocation and exactly three ordered pass borrowers.
struct SurfaceBatch final {
  MeshRenderableFactory *factory = nullptr;
  std::optional<MeshRenderableProxy> geometry;
  std::array<std::optional<MeshRenderableProxy>, 3u> passes;
  std::vector<SurfaceMember> members;
  std::shared_ptr<const RenderMaterialAssetSnapshot> material;
  std::uint64_t material_signature = 0u;
  std::uint64_t grid_signature = 0u;
  bool grid_initialized = false;
  bool double_sided = false;
  bool cast_shadows = false;
  bool detached = false;

  ~SurfaceBatch() {
    if (!factory) return;
    for (auto &pass : passes) if (pass) factory->destroy(*pass);
    if (geometry) factory->destroy(*geometry);
  }
};

/// Borrows candidate source records only until all CPU preparation has joined.
struct SurfaceEntry final {
  const SurfaceObject *object = nullptr;
  const SurfacePart *part = nullptr;
  bool claimed = false;
};

/// Stages validated ranges and optional hidden replacement for one batch.
struct PreparedSurfaceBatch final {
  std::shared_ptr<SurfaceBatch> batch;
  std::vector<std::size_t> entries;
  std::vector<MeshRenderableFactory::EditableSurfaceSource> sources;
  std::shared_ptr<MeshRenderableFactory::EditableSurfaceUpdate> update;
  RenderInstanceBounds bounds;
  bool created = false;
  bool cast_shadows = false;
};

/// Captures scalar preview state before expensive normalization and allocation.
struct SurfacePreviewKey final {
  EntityId source = kInvalidEntityId;
  EntityId visibility = kInvalidEntityId;
  std::shared_ptr<const MeshGeometryPayload> geometry;
  std::shared_ptr<const MeshGeometryProjection> projection;
  std::shared_ptr<const std::vector<QVec3>> position_payload;
  std::shared_ptr<const std::vector<SurfacePart>> material_parts;
  std::uint64_t material_signature = 0u;
  std::uint64_t position_revision = 0u;
  std::uint64_t layout_key = 0u;
  QMat4 transform;
  QMat4 sampling;
  std::vector<QMat4> transforms;
  std::vector<QMat4> mappings;
  bool dynamic = false;
  bool suppress = false;
  bool double_sided = false;
  bool cast_shadows = false;
};

std::shared_ptr<const MeshGeometryProjection> preview_projection(
    const RenderToolPreviewMeshRecord &preview) {
  return preview.geometry_projection ? preview.geometry_projection : preview.mesh.geometry_projection();
}

bool preview_layout_matches(const SurfacePreviewKey &key,
                            const RenderToolPreviewMeshRecord &preview) {
  return key.source == preview.source_entity && key.visibility == preview.visibility_entity &&
      key.material_signature == preview.material.revision && key.material_parts &&
      std::ranges::all_of(*key.material_parts, [&](const SurfacePart &part) {
        return render_material_payloads_equal(part.material.get(), preview.material.material_for_slot(part.slot));
      }) && key.layout_key == preview.layout_key &&
      same_matrix(key.transform, preview.transform) && same_matrix(key.sampling, preview.sampling_from_local) &&
      key.transforms.size() == preview.instance_transforms.size() &&
      std::equal(key.transforms.begin(), key.transforms.end(), preview.instance_transforms.begin(), same_matrix) &&
      key.mappings.size() == preview.instance_sampling_from_local.size() &&
      std::equal(key.mappings.begin(), key.mappings.end(), preview.instance_sampling_from_local.begin(), same_matrix) &&
      key.dynamic == preview.dynamic_positions && key.suppress == preview.suppress_source &&
      key.double_sided == preview.double_sided && key.cast_shadows == preview.cast_shadows;
}

bool preview_matches(const SurfacePreviewKey &key,
                     const RenderToolPreviewMeshRecord &preview) {
  const auto projection = preview_projection(preview);
  return key.projection == projection &&
      key.geometry == (projection ? projection->source_geometry : preview.mesh.geometry_payload()) &&
      key.position_payload == preview.dynamic_position_payload &&
      key.position_revision == preview.position_revision && preview_layout_matches(key, preview);
}

SurfacePreviewKey preview_key(const RenderToolPreviewMeshRecord &preview) {
  const auto projection = preview_projection(preview);
  return {.source = preview.source_entity, .visibility = preview.visibility_entity,
      .geometry = projection ? projection->source_geometry : preview.mesh.geometry_payload(), .projection = projection,
      .position_payload = preview.dynamic_position_payload,
      .material_signature = preview.material.revision,
      .position_revision = preview.position_revision, .layout_key = preview.layout_key,
      .transform = preview.transform, .sampling = preview.sampling_from_local,
      .transforms = preview.instance_transforms, .mappings = preview.instance_sampling_from_local,
      .dynamic = preview.dynamic_positions, .suppress = preview.suppress_source,
      .double_sided = preview.double_sided, .cast_shadows = preview.cast_shadows};
}

bool eligible_surface(const SurfaceObject &object,
                      const MaterialBindingService &binding,
                      EditableMeshSurfaceWorkCounters &counters) {
  if (!object.mesh.renderable() || !object.parts || object.parts->empty()) {
    ++counters.fallback_nonrenderable_count;
    return false;
  }
  if (object.dimmed) { ++counters.fallback_presentation_count; return false; }
  const auto &values = object.transform.values;
  const float determinant = dot(cross(QVec3{values[0], values[1], values[2]},
      QVec3{values[4], values[5], values[6]}), QVec3{values[8], values[9], values[10]});
  if (determinant < 0.0f) { ++counters.fallback_reflected_count; return false; }
  if (!(determinant > 0.0f)) { ++counters.fallback_singular_count; return false; }
  if (!MeshRenderableFactory::supports_editable_surface_transform(object.transform)) {
    ++counters.fallback_nonconformal_count;
    return false;
  }
  if (object.mesh.vertices.size() > MeshRenderableFactory::kEditableSurfaceMaximumVertices ||
      std::ranges::any_of(*object.parts, [](const SurfacePart &part) {
        return part.index_count > MeshRenderableFactory::kEditableSurfaceMaximumIndices;
      })) {
    ++counters.fallback_oversized_geometry_count;
    return false;
  }
  for (const auto &part : *object.parts) {
    switch (binding.material_opacity(part.material.get())) {
    case MaterialOpacity::Unknown: ++counters.fallback_unknown_opacity_count; return false;
    case MaterialOpacity::Transparent: ++counters.fallback_transparent_count; return false;
    case MaterialOpacity::Opaque: break;
    }
  }
  return true;
}

bool compatible_entry(const SurfaceEntry &left, const SurfaceEntry &right) {
  return left.object->cast_shadows == right.object->cast_shadows &&
      left.object->double_sided == right.object->double_sided &&
      left.part->material_signature == right.part->material_signature &&
      render_material_payloads_equal(left.part->material.get(), right.part->material.get());
}

/// Bounds bone translations independently of absolute scene coordinates.
struct SurfacePlacementRange final {
  QVec3 minimum;
  QVec3 maximum;
  bool present = false;

  [[nodiscard]] bool includes(const QMat4 &transform) const {
    if (!present) return true;
    const auto &values = transform.values;
    const auto bounded = [](float low, float high, float value) {
      return static_cast<double>(std::max(high, value)) -
                 static_cast<double>(std::min(low, value)) <=
             MeshRenderableFactory::kEditableSurfaceMaximumPlacementOffset;
    };
    return bounded(minimum.x, maximum.x, values[12]) &&
        bounded(minimum.y, maximum.y, values[13]) &&
        bounded(minimum.z, maximum.z, values[14]);
  }

  void append(const QMat4 &transform) {
    const QVec3 origin{transform.values[12], transform.values[13], transform.values[14]};
    if (!present) { minimum = origin; maximum = origin; present = true; return; }
    minimum = {std::min(minimum.x, origin.x), std::min(minimum.y, origin.y), std::min(minimum.z, origin.z)};
    maximum = {std::max(maximum.x, origin.x), std::max(maximum.y, origin.y), std::max(maximum.z, origin.z)};
  }
};

/// Uses floating cell coordinates to avoid integer overflow in large worlds.
struct SurfacePlacementCell final {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

SurfacePlacementCell placement_cell(const QMat4 &transform) {
  const auto coordinate = [](float value) {
    return std::floor(static_cast<double>(value) /
                      MeshRenderableFactory::kEditableSurfaceMaximumPlacementOffset);
  };
  return {coordinate(transform.values[12]), coordinate(transform.values[13]), coordinate(transform.values[14])};
}

std::uint64_t placement_bucket(std::uint64_t material_key, SurfacePlacementCell cell) {
  const auto bits = [](double value) { return std::bit_cast<std::uint64_t>(value == 0.0 ? 0.0 : value); };
  return RenderSignatureBuilder{material_key}.add_value(bits(cell.x))
      .add_value(bits(cell.y)).add_value(bits(cell.z)).value();
}

} // namespace

/// Binds an accepted batch slot directly to its immutable facts and preview order.
struct RetainedSurfaceBinding final {
  SurfaceObject *object = nullptr;
  const SurfacePart *part = nullptr;
  std::size_t preview_index = std::numeric_limits<std::size_t>::max();
};

/// Owns accepted/pending derived state and bounded borrower-before-owner retirement.
struct EditableMeshSurfaceCache::Impl final {
  /// Owns the complete candidate until the compound preview accepts every lane.
  struct Pending final {
    RenderSourceStamp stamp;
    std::shared_ptr<const SurfaceObjects> base;
    SurfaceObjects effective;
    std::unordered_set<EntityId> enrolled;
    std::vector<SurfacePreviewKey> keys;
    RenderToolPreviewSuppressionSnapshot suppression;
    RenderToolPreviewSuppressionSnapshot exclusion;
    std::vector<RenderToolPreviewMeshRecord> fallback;
    std::vector<PreparedSurfaceBatch> batches;
    std::vector<std::shared_ptr<SurfaceBatch>> next_batches;
    std::vector<std::shared_ptr<SurfaceBatch>> retiring;
    EditableMeshSurfaceWorkCounters counters;
    std::vector<SurfaceObject> preview_objects;
    std::vector<SurfaceObject *> preview_targets;
    std::vector<std::vector<RetainedSurfaceBinding>> layout;
    bool retained_layout = false;
  };

  MeshRenderableFactory *factory = nullptr;
  MaterialBindingService *binding = nullptr;
  RenderSourceStamp stamp;
  bool has_stamp = false;
  std::shared_ptr<const SurfaceObjects> base;
  SurfaceObjects effective;
  std::unordered_set<EntityId> enrolled;
  std::vector<SurfacePreviewKey> keys;
  RenderToolPreviewSuppressionSnapshot suppression;
  RenderToolPreviewSuppressionSnapshot exclusion;
  std::vector<RenderToolPreviewMeshRecord> fallback;
  std::vector<std::shared_ptr<SurfaceBatch>> batches;
  std::vector<std::shared_ptr<SurfaceBatch>> retired;
  std::unique_ptr<Pending> pending;
  EditableMeshSurfaceWorkCounters counters;
  std::vector<SurfaceObject *> preview_targets;
  std::vector<std::vector<RetainedSurfaceBinding>> layout;

  void detach(SurfaceBatch &batch) {
    if (batch.detached) return;
    std::array<utils::Entity, 3u> entities{};
    for (std::size_t index = 0u; index < entities.size(); ++index) {
      if (batch.passes[index]) entities[index] = batch.passes[index]->entity;
    }
    factory->remove_from_scene(entities);
    factory->destroy_renderable_components(entities);
    batch.detached = true;
  }

  std::shared_ptr<const SurfaceObjects> capture_base(const RenderSceneSnapshot &scene,
                                                   MaterialPool &materials) {
    if (has_stamp && stamp == static_cast<const RenderSourceStamp &>(scene)) return base;
    auto result = std::make_shared<SurfaceObjects>();
    result->reserve(scene.live_mesh_entities.size());
    const auto add = [&](EntityId entity, const RenderMeshAssetSnapshot &mesh,
        const RenderMaterialBindingSnapshot &material, const QMat4 &transform,
        const QMat4 &sampling, bool cast_shadows, bool double_sided, bool dimmed) {
      if (result->contains(entity)) return;
      const auto previous = base ? base->find(entity) : SurfaceObjects::const_iterator{};
      const SurfaceObject *old = base && previous != base->end() ? &previous->second : nullptr;
      SurfaceObject object{.entity = entity, .mesh = mesh,
          .parts = resolve_parts(mesh, material, old, materials),
          .transform = transform, .sampling = sampling,
          .cast_shadows = cast_shadows, .double_sided = double_sided, .dimmed = dimmed};
      result->emplace(entity, std::move(object));
    };
    for (const auto &record : scene.meshes) {
      add(record.entity_id, record.mesh, record.material, record.world_transform,
          record.sampling_from_local, record.cast_shadows(), record.double_sided(), false);
    }
    for (const auto &record : scene.instance_primitives) {
      if (record.definition_entity != kInvalidEntityId || record.member_entity != kInvalidEntityId) continue;
      for (const auto &occurrence : record.occurrences) {
        add(occurrence.entity_id, record.mesh, record.material, occurrence.world_transform,
            occurrence.sampling_from_local, record.key.cast_shadows, record.key.double_sided,
            occurrence.dimmed);
      }
    }
    return result;
  }

  std::shared_ptr<SurfaceBatch> create_batch(
      const PreparedSurfaceBatch &prepared, std::span<const SurfaceEntry> entries) {
    const SurfaceEntry &first = entries[prepared.entries.front()];
    auto batch = std::make_shared<SurfaceBatch>();
    batch->factory = factory;
    batch->material = first.part->material;
    batch->material_signature = first.part->material_signature;
    batch->double_sided = first.object->double_sided;
    batch->cast_shadows = first.object->cast_shadows;
    batch->members.reserve(prepared.entries.size());
    for (const auto index : prepared.entries) {
      batch->members.push_back({entries[index].object->entity, entries[index].part->slot});
    }
    batch->geometry.emplace(factory->create_editable_surface_geometry(prepared.update));
    if (!batch->geometry->vertex_buffer || !batch->geometry->index_buffer) return {};
    for (std::size_t index = 0u; index < batch->passes.size(); ++index) {
      batch->passes[index].emplace(factory->create_editable_surface_proxy(*batch->geometry,
          batch->material.get(), prepared.bounds, index == 0u && batch->cast_shadows,
          index == 0u, batch->double_sided, 0u, index != 0u, index == 2u,
          index == 2u ? kViewportOverlayDepthChannel : kViewportDefaultRenderableChannel));
      if (!batch->passes[index]->entity) return {};
      if (!factory->update_editable_surface_presentation(*batch->passes[index],
          *prepared.update, prepared.bounds, index == 0u && batch->cast_shadows)) return {};
    }
    return batch;
  }

  bool prepare_stream_updates(Pending &candidate, std::size_t vertex_count) {
    const auto context = MeshRenderableFactory::create_editable_surface_preparation_context();
    for (const auto &prepared : candidate.batches) {
      MeshRenderableFactory::register_editable_surface_sources(*context, prepared.sources);
    }
    const auto prepare_chunk = [&](std::size_t index) {
      auto &prepared = candidate.batches[index];
      prepared.update = MeshRenderableFactory::prepare_editable_surface_update(prepared.sources,
          prepared.batch ? &*prepared.batch->geometry : nullptr, context);
    };
    if (candidate.batches.size() >= 16u && vertex_count >= 4096u) {
      QDR_PROFILE_SCOPE("Editable surface parallel chunk preparation");
      constexpr unsigned int kMaximumWorkers = 8u;
      const auto worker_count = std::clamp(std::thread::hardware_concurrency(), 1u, kMaximumWorkers);
      std::atomic<std::size_t> next_chunk = 0u;
      const auto work = [&] {
        for (;;) {
          const auto index = next_chunk.fetch_add(1u, std::memory_order_relaxed);
          if (index >= candidate.batches.size()) return;
          prepare_chunk(index);
        }
      };
      std::vector<std::jthread> workers;
      workers.reserve(worker_count - 1u);
      for (unsigned int index = 1u; index < worker_count; ++index) workers.emplace_back(work);
      work();
      // Join before any Filament mutation or candidate source release.
    } else {
      for (std::size_t index = 0u; index < candidate.batches.size(); ++index) prepare_chunk(index);
    }
    return std::ranges::all_of(candidate.batches, [](const auto &prepared) { return prepared.update != nullptr; });
  }

  bool validate_retained_batch(const PreparedSurfaceBatch &prepared) const {
    if (!MeshRenderableFactory::editable_surface_update_reuses_geometry(*prepared.update) ||
        !factory->validate_editable_surface_update(*prepared.batch->geometry, *prepared.update)) return false;
    for (const auto &pass : prepared.batch->passes) {
      if (!pass || !factory->validate_editable_surface_presentation(*pass,
          *prepared.batch->geometry, *prepared.update)) return false;
    }
    return true;
  }

  void retain_layout(Pending &candidate) {
    candidate.preview_targets.reserve(candidate.keys.size());
    std::unordered_map<EntityId, std::size_t> preview_indices;
    preview_indices.reserve(candidate.keys.size());
    for (std::size_t index = 0u; index < candidate.keys.size(); ++index) {
      const auto found = candidate.effective.find(candidate.keys[index].source);
      candidate.preview_targets.push_back(found == candidate.effective.end() || !found->second.preview
          ? nullptr : &found->second);
      if (candidate.preview_targets.back()) preview_indices.emplace(found->first, index);
    }
    candidate.layout.reserve(candidate.next_batches.size());
    for (const auto &batch : candidate.next_batches) {
      auto &bindings = candidate.layout.emplace_back();
      bindings.reserve(batch->members.size());
      for (const auto member : batch->members) {
        auto &object = candidate.effective.at(member.entity);
        const auto part = std::ranges::find(*object.parts, member.slot, &SurfacePart::slot);
        const auto preview = preview_indices.find(member.entity);
        bindings.push_back({&object, &*part, preview == preview_indices.end()
            ? std::numeric_limits<std::size_t>::max() : preview->second});
      }
    }
  }

  // Returns no candidate for a layout change; that case uses normal planning.
  std::unique_ptr<Pending> prepare_retained(const std::vector<RenderToolPreviewMeshRecord> &previews) {
    if (!fallback.empty() || previews.empty() || preview_targets.size() != previews.size() ||
        layout.size() != batches.size()) return {};
    auto candidate = std::make_unique<Pending>();
    candidate->retained_layout = true;
    candidate->counters = counters;
    candidate->preview_objects.reserve(previews.size());
    candidate->keys.reserve(previews.size());
    for (std::size_t index = 0u; index < previews.size(); ++index) {
      if (!preview_targets[index] || !preview_layout_matches(keys[index], previews[index])) return {};
      const auto &old = *preview_targets[index];
      auto mesh = render_mesh_asset_snapshot(kInvalidAssetId, previews[index].mesh, preview_projection(previews[index]));
      if (!mesh.renderable() || mesh.vertices.size() != old.mesh.vertices.size() ||
          mesh.indices.size() != old.mesh.indices.size() || !same_primitives(mesh.primitives, old.mesh.primitives)) return {};
      SurfaceObject object = old;
      object.mesh = std::move(mesh);
      object.position_payload = previews[index].dynamic_position_payload;
      object.position_revision = previews[index].position_revision;
      candidate->preview_objects.push_back(std::move(object));
      candidate->keys.push_back(preview_key(previews[index]));
      candidate->keys.back().material_parts = old.parts;
    }
    QDR_PROFILE_SCOPE("Editable surface retained slot preparation");
    candidate->batches.reserve(batches.size());
    std::size_t vertex_count = 0u;
    for (std::size_t index = 0u; index < batches.size(); ++index) {
      auto &prepared = candidate->batches.emplace_back();
      prepared.batch = batches[index];
      prepared.cast_shadows = batches[index]->cast_shadows;
      prepared.sources.reserve(layout[index].size());
      for (const auto &binding : layout[index]) {
        const auto &object = binding.preview_index < previews.size()
            ? candidate->preview_objects[binding.preview_index] : *binding.object;
        prepared.sources.push_back({.mesh = object.mesh,
            .positions = object.position_payload ? std::span<const QVec3>(*object.position_payload) : object.mesh.positions,
            .position_payload = object.position_payload, .primitives = binding.part->primitives,
            .world_from_local = object.transform, .sampling_from_local = object.sampling,
            .position_revision = object.position_revision});
        vertex_count += object.mesh.vertices.size();
      }
    }
    if (!prepare_stream_updates(*candidate, vertex_count)) {
      // A malformed same-layout candidate must fail atomically, not disappear
      // into another presentation lane.
      throw std::runtime_error("Invalid retained editable surface streams");
    }
    for (auto &prepared : candidate->batches) {
      if (!validate_retained_batch(prepared)) throw std::runtime_error("Invalid retained editable surface layout");
      prepared.bounds = MeshRenderableFactory::editable_surface_world_bounds(*prepared.update);
    }
    return candidate;
  }

  bool prepare_batches(Pending &candidate) {
    QDR_PROFILE_SCOPE("Editable surface prepare coarse batches");
    std::vector<SurfaceEntry> entries;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> entry_indices;
    std::vector<EntityId> ordered_entities;
    ordered_entities.reserve(candidate.effective.size());
    for (const auto &[entity, object] : candidate.effective) { (void)object; ordered_entities.push_back(entity); }
    std::ranges::sort(ordered_entities);
    for (const EntityId entity : ordered_entities) {
      const auto &object = candidate.effective.at(entity);
      for (const auto &part : *object.parts) {
        entry_indices[member_key({entity, part.slot})].push_back(entries.size());
        entries.push_back({.object = &object, .part = &part});
      }
    }
    const auto find_entry = [&](SurfaceMember member) -> std::optional<std::size_t> {
      const auto found = entry_indices.find(member_key(member));
      if (found == entry_indices.end()) return {};
      for (const auto index : found->second) {
        if (!entries[index].claimed && entries[index].object->entity == member.entity &&
            entries[index].part->slot == member.slot) return index;
      }
      return {};
    };
    candidate.batches.reserve(batches.size() + entries.size() / 64u + 1u);
    for (const auto &batch : batches) {
      PreparedSurfaceBatch prepared{.batch = batch};
      bool compatible = true;
      std::size_t vertex_count = 0u;
      std::size_t index_count = 0u;
      SurfacePlacementRange placement;
      for (const auto member : batch->members) {
        const auto entry = find_entry(member);
        if (!entry || (!prepared.entries.empty() &&
                       !compatible_entry(entries[prepared.entries.front()], entries[*entry]))) {
          compatible = false;
          break;
        }
        const auto &source = entries[*entry];
        compatible = compatible && source.object->double_sided == batch->double_sided &&
            source.part->material_signature == batch->material_signature &&
            render_material_payloads_equal(source.part->material.get(), batch->material.get()) &&
            placement.includes(source.object->transform);
        if (!compatible) break;
        placement.append(source.object->transform);
        vertex_count += source.object->mesh.vertices.size();
        index_count += source.part->index_count;
        prepared.entries.push_back(*entry);
      }
      if (!compatible || prepared.entries.empty() ||
          vertex_count > MeshRenderableFactory::kEditableSurfaceMaximumVertices ||
          index_count > MeshRenderableFactory::kEditableSurfaceMaximumIndices) continue;
      for (const auto index : prepared.entries) entries[index].claimed = true;
      candidate.batches.push_back(std::move(prepared));
    }
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> open_batches;
    std::vector<std::pair<std::size_t, std::size_t>> sizes(candidate.batches.size());
    std::vector<SurfacePlacementRange> placements(candidate.batches.size());
    std::vector<std::uint64_t> batch_buckets(candidate.batches.size());
    for (std::size_t index = 0u; index < entries.size(); ++index) {
      auto &entry = entries[index];
      if (entry.claimed) continue;
      const auto material_key = RenderSignatureBuilder{}.add_value(entry.part->material_signature)
          .add_bool(entry.object->double_sided).add_bool(entry.object->cast_shadows).value();
      const auto cell = placement_cell(entry.object->transform);
      std::optional<std::size_t> destination;
      // Neighbor cells allow nearby objects on opposite sides of a cell edge
      // to share a batch, while a fixed search bound avoids distant-world scans.
      for (int z = -1; z <= 1 && !destination; ++z) {
        for (int y = -1; y <= 1 && !destination; ++y) {
          for (int x = -1; x <= 1 && !destination; ++x) {
            const auto found = open_batches.find(placement_bucket(material_key,
                {cell.x + x, cell.y + y, cell.z + z}));
            if (found == open_batches.end()) continue;
            for (const auto candidate_index : found->second) {
              const auto &prepared = candidate.batches[candidate_index];
              if (prepared.entries.size() < MeshRenderableFactory::kEditableSurfaceMaximumOccurrences &&
                  sizes[candidate_index].first + entry.object->mesh.vertices.size() <= MeshRenderableFactory::kEditableSurfaceMaximumVertices &&
                  sizes[candidate_index].second + entry.part->index_count <= MeshRenderableFactory::kEditableSurfaceMaximumIndices &&
                  placements[candidate_index].includes(entry.object->transform) &&
                  compatible_entry(entries[prepared.entries.front()], entry)) {
                destination = candidate_index;
                break;
              }
            }
          }
        }
      }
      if (!destination) {
        destination = candidate.batches.size();
        candidate.batches.push_back({});
        sizes.emplace_back();
        placements.emplace_back();
        batch_buckets.push_back(placement_bucket(material_key, cell));
        open_batches[batch_buckets.back()].push_back(*destination);
      }
      candidate.batches[*destination].entries.push_back(index);
      sizes[*destination].first += entry.object->mesh.vertices.size();
      sizes[*destination].second += entry.part->index_count;
      placements[*destination].append(entry.object->transform);
      entry.claimed = true;
      if (candidate.batches[*destination].entries.size() ==
          MeshRenderableFactory::kEditableSurfaceMaximumOccurrences) {
        std::erase(open_batches.at(batch_buckets[*destination]), *destination);
      }
    }
    retired.reserve(retired.size() + batches.size() + candidate.batches.size());
    candidate.next_batches.reserve(candidate.batches.size());
    candidate.retiring.reserve(batches.size());
    std::size_t vertex_count = 0u;
    for (auto &prepared : candidate.batches) {
      prepared.sources.reserve(prepared.entries.size());
      for (const auto index : prepared.entries) {
        const auto &entry = entries[index];
        const auto &object = *entry.object;
        prepared.sources.push_back({.mesh = object.mesh,
            .positions = object.position_payload ? std::span<const QVec3>(*object.position_payload) : object.mesh.positions,
            .position_payload = object.position_payload, .primitives = entry.part->primitives,
            .world_from_local = object.transform, .sampling_from_local = object.sampling,
            .position_revision = object.position_revision});
        vertex_count += object.mesh.vertices.size();
      }
      prepared.cast_shadows = entries[prepared.entries.front()].object->cast_shadows;
    }
    if (!prepare_stream_updates(candidate, vertex_count)) return false;
    for (auto &prepared : candidate.batches) {
      prepared.bounds = MeshRenderableFactory::editable_surface_world_bounds(*prepared.update);
      if (!prepared.batch || !MeshRenderableFactory::editable_surface_update_reuses_geometry(*prepared.update)) {
        prepared.batch = create_batch(prepared, entries);
        if (!prepared.batch) return false;
        prepared.created = true;
        ++candidate.counters.batch_create_count;
      } else {
        if (!factory->validate_editable_surface_update(*prepared.batch->geometry, *prepared.update)) return false;
        for (const auto &pass : prepared.batch->passes) {
          if (!pass || !factory->validate_editable_surface_presentation(*pass,
              *prepared.batch->geometry, *prepared.update)) return false;
        }
      }
      candidate.next_batches.push_back(prepared.batch);
    }
    std::unordered_set<const SurfaceBatch *> kept;
    kept.reserve(candidate.next_batches.size());
    for (const auto &batch : candidate.next_batches) kept.insert(batch.get());
    for (const auto &batch : batches) if (!kept.contains(batch.get())) candidate.retiring.push_back(batch);
    retain_layout(candidate);
    return true;
  }
};

EditableMeshSurfaceCache::EditableMeshSurfaceCache() : impl_(std::make_unique<Impl>()) {}
EditableMeshSurfaceCache::~EditableMeshSurfaceCache() { shutdown(); }

void EditableMeshSurfaceCache::initialize(MeshRenderableFactory &factory,
                                         MaterialBindingService &material_binding) {
  shutdown();
  impl_->factory = &factory;
  impl_->binding = &material_binding;
}

bool EditableMeshSurfaceCache::initialized() const noexcept {
  return impl_->factory != nullptr && impl_->binding != nullptr;
}

ToolPreviewSubmissionResult EditableMeshSurfaceCache::prepare(
    const RenderSceneSnapshot &scene, const std::vector<RenderToolPreviewMeshRecord> &previews,
    const RenderToolPreviewSuppressionSnapshot &suppression) {
  QDR_PROFILE_FUNCTION();
  if (!initialized() || !impl_->factory->initialized()) return ToolPreviewSubmissionResult::Failed;
  discard_prepared();
  if (impl_->counters.fallback_unknown_opacity_count == 0u &&
      impl_->has_stamp && impl_->stamp == static_cast<const RenderSourceStamp &>(scene) &&
      impl_->suppression.entities == suppression.entities && impl_->suppression.occurrences == suppression.occurrences &&
      impl_->keys.size() == previews.size() &&
      std::equal(impl_->keys.begin(), impl_->keys.end(), previews.begin(), preview_matches)) {
    return ToolPreviewSubmissionResult::Unchanged;
  }
  try {
    if (impl_->counters.fallback_unknown_opacity_count == 0u && impl_->has_stamp &&
        impl_->stamp == static_cast<const RenderSourceStamp &>(scene) &&
        impl_->suppression.entities == suppression.entities && impl_->suppression.occurrences == suppression.occurrences &&
        impl_->keys.size() == previews.size()) {
      if (auto retained = impl_->prepare_retained(previews)) {
        impl_->pending = std::move(retained);
        return ToolPreviewSubmissionResult::Published;
      }
    }
    auto candidate = std::make_unique<Impl::Pending>();
    candidate->stamp = scene;
    candidate->suppression = suppression;
    candidate->counters.batch_create_count = impl_->counters.batch_create_count;
    MaterialPool materials;
    candidate->base = impl_->capture_base(scene, materials);
    if (!impl_->has_stamp || impl_->stamp.scene_binding == scene.scene_binding) candidate->enrolled = impl_->enrolled;
    std::unordered_set<EntityId> overridden(suppression.entities.begin(), suppression.entities.end());
    std::unordered_map<EntityId, std::size_t> replacing_counts;
    for (const auto &preview : previews) {
      if (preview.suppress_source && preview.source_entity != kInvalidEntityId) {
        overridden.insert(preview.source_entity);
        ++replacing_counts[preview.source_entity];
      }
    }
    candidate->effective.reserve(candidate->enrolled.size() + previews.size());
    candidate->keys.reserve(previews.size());
    candidate->fallback.reserve(previews.size());
    for (const auto &preview : previews) {
      ++candidate->counters.source_record_count;
      candidate->keys.push_back(preview_key(preview));
      const auto base = candidate->base->find(preview.source_entity);
      bool usable = true;
      if (!preview.dynamic_positions || !preview.suppress_source) {
        ++candidate->counters.fallback_static_count;
        usable = false;
      } else if (base == candidate->base->end() || replacing_counts[preview.source_entity] != 1u ||
          (preview.visibility_entity != kInvalidEntityId && preview.visibility_entity != preview.source_entity) ||
          preview.instance_transforms.size() > 1u) {
        ++candidate->counters.fallback_unmapped_source_count;
        usable = false;
      }
      if (usable) {
        const auto previous = impl_->effective.find(preview.source_entity);
        const auto *old = previous != impl_->effective.end() ? &previous->second : &base->second;
        auto mesh = render_mesh_asset_snapshot(kInvalidAssetId, preview.mesh, preview.geometry_projection);
        SurfaceObject object{.entity = preview.source_entity, .mesh = mesh,
            .parts = resolve_parts(mesh, preview.material, old, materials),
            .position_payload = preview.dynamic_position_payload,
            .transform = preview.instance_transforms.empty() ? preview.transform : preview.instance_transforms.front(),
            .sampling = preview.instance_sampling_from_local.empty() ? preview.sampling_from_local : preview.instance_sampling_from_local.front(),
            .position_revision = preview.position_revision, .cast_shadows = preview.cast_shadows,
            .double_sided = preview.double_sided, .preview = true};
        candidate->keys.back().material_parts = object.parts;
        if (object.position_payload && object.position_payload->size() != mesh.vertices.size()) return ToolPreviewSubmissionResult::Failed;
        usable = eligible_surface(object, *impl_->binding, candidate->counters);
        if (usable) {
          candidate->enrolled.insert(object.entity);
          candidate->effective.emplace(object.entity, std::move(object));
          ++candidate->counters.active_preview_object_count;
        }
      }
      if (!usable) {
        if (!candidate->keys.back().material_parts) {
          const auto mesh = render_mesh_asset_snapshot(kInvalidAssetId, preview.mesh, preview.geometry_projection);
          candidate->keys.back().material_parts = resolve_parts(mesh, preview.material, nullptr, materials);
        }
        candidate->fallback.push_back(preview);
      }
    }
    for (auto iterator = candidate->enrolled.begin(); iterator != candidate->enrolled.end();) {
      const auto base = candidate->base->find(*iterator);
      if (base == candidate->base->end()) { iterator = candidate->enrolled.erase(iterator); continue; }
      if (!overridden.contains(*iterator)) {
        ++candidate->counters.source_record_count;
        if (eligible_surface(base->second, *impl_->binding, candidate->counters)) {
          candidate->effective.emplace(*iterator, base->second);
        }
      }
      ++iterator;
    }
    candidate->counters.eligible_record_count = candidate->effective.size();
    candidate->counters.active_retained_object_count = candidate->effective.size();
    candidate->exclusion.entities.reserve(candidate->effective.size());
    for (const auto &[entity, object] : candidate->effective) { (void)object; candidate->exclusion.entities.push_back(entity); }
    std::ranges::sort(candidate->exclusion.entities);
    RenderSignatureBuilder exclusion_signature;
    for (const auto entity : candidate->exclusion.entities) exclusion_signature.add_value(entity);
    candidate->exclusion.signature = exclusion_signature.value();
    impl_->pending = std::move(candidate);
    if (!impl_->prepare_batches(*impl_->pending)) {
      discard_prepared();
      return ToolPreviewSubmissionResult::Failed;
    }
    impl_->pending->counters.active_batch_count = impl_->pending->next_batches.size();
    return ToolPreviewSubmissionResult::Published;
  } catch (...) {
    discard_prepared();
    return ToolPreviewSubmissionResult::Failed;
  }
}

void EditableMeshSurfaceCache::publish_prepared() noexcept {
  if (!impl_->pending) return;
  auto &candidate = *impl_->pending;
  for (auto &prepared : candidate.batches) {
    auto &batch = *prepared.batch;
    if (!prepared.created) {
      const bool updated = impl_->factory->update_editable_surface_geometry(*batch.geometry, prepared.update);
      assert(updated);
      (void)updated;
      for (std::size_t index = 0u; index < batch.passes.size(); ++index) {
        const bool presented = impl_->factory->update_editable_surface_presentation(*batch.passes[index],
            *prepared.update, prepared.bounds, index == 0u && prepared.cast_shadows);
        assert(presented);
        (void)presented;
      }
      batch.cast_shadows = prepared.cast_shadows;
    }
    if (prepared.created) {
      impl_->factory->update_layer_mask(*batch.passes[0], kViewportRenderedSceneLayerMask);
      impl_->factory->update_layer_mask(*batch.passes[1], kViewportShadedSceneLayerMask);
      impl_->factory->update_layer_mask(*batch.passes[2], kViewportOverlayLayerMask);
    }
  }
  if (candidate.retained_layout) {
    for (std::size_t index = 0u; index < candidate.preview_objects.size(); ++index) {
      *impl_->preview_targets[index] = std::move(candidate.preview_objects[index]);
    }
    impl_->keys = std::move(candidate.keys);
    impl_->pending.reset();
    return;
  }
  for (auto &batch : candidate.retiring) { impl_->detach(*batch); impl_->retired.push_back(std::move(batch)); }
  impl_->preview_targets = std::move(candidate.preview_targets);
  impl_->layout = std::move(candidate.layout);
  impl_->stamp = candidate.stamp;
  impl_->has_stamp = true;
  impl_->base = std::move(candidate.base);
  impl_->effective = std::move(candidate.effective);
  impl_->enrolled = std::move(candidate.enrolled);
  impl_->keys = std::move(candidate.keys);
  impl_->suppression = std::move(candidate.suppression);
  impl_->exclusion = std::move(candidate.exclusion);
  impl_->fallback = std::move(candidate.fallback);
  impl_->batches = std::move(candidate.next_batches);
  impl_->counters = candidate.counters;
  impl_->counters.retired_proxy_count = impl_->retired.size() * 3u;
  impl_->pending.reset();
  QDR_PROFILE_PLOT("Quader editable surface batches", static_cast<std::int64_t>(impl_->counters.active_batch_count));
  QDR_PROFILE_PLOT("Quader editable surface objects", static_cast<std::int64_t>(impl_->counters.active_retained_object_count));
  QDR_PROFILE_PLOT("Quader editable surface fallback records", static_cast<std::int64_t>(impl_->fallback.size()));
}

void EditableMeshSurfaceCache::discard_prepared() {
  if (!impl_->pending) return;
  for (auto &prepared : impl_->pending->batches) {
    if (!prepared.created || !prepared.batch) continue;
    impl_->detach(*prepared.batch);
    impl_->retired.push_back(std::move(prepared.batch));
  }
  impl_->pending.reset();
  impl_->counters.retired_proxy_count = impl_->retired.size() * 3u;
}

const std::vector<RenderToolPreviewMeshRecord> &EditableMeshSurfaceCache::prepared_fallback_meshes() const noexcept {
  return impl_->pending && !impl_->pending->retained_layout ? impl_->pending->fallback : impl_->fallback;
}
const RenderToolPreviewSuppressionSnapshot &EditableMeshSurfaceCache::prepared_scene_exclusion() const noexcept {
  return impl_->pending && !impl_->pending->retained_layout ? impl_->pending->exclusion : impl_->exclusion;
}
const RenderToolPreviewSuppressionSnapshot &EditableMeshSurfaceCache::scene_exclusion() const noexcept { return impl_->exclusion; }
EditableMeshSurfaceWorkCounters EditableMeshSurfaceCache::work_counters() const noexcept { return impl_->counters; }
std::size_t EditableMeshSurfaceCache::proxy_count() const noexcept { return impl_->batches.size() * 3u; }

void EditableMeshSurfaceCache::update_surface_grid(float grid_world_spacing, const ViewportSettings &settings) {
  if (!initialized()) return;
  const auto signature = mesh_surface_grid_signature(grid_world_spacing, settings);
  for (const auto &batch : impl_->batches) {
    if (batch->grid_initialized && batch->grid_signature == signature) continue;
    impl_->factory->update_surface_grid(*batch->passes[0], grid_world_spacing, settings);
    impl_->factory->update_surface_grid(*batch->passes[1], grid_world_spacing, settings);
    impl_->factory->update_shaded_lighting(*batch->passes[1], settings);
    batch->grid_signature = signature;
    batch->grid_initialized = true;
  }
}

void EditableMeshSurfaceCache::drain_retired_destroy_queue(std::size_t max_proxy_count) {
  const auto started = std::chrono::steady_clock::now();
  std::size_t destroyed = 0u;
  while (!impl_->retired.empty() && destroyed + 3u <= max_proxy_count) {
    impl_->retired.pop_back();
    destroyed += 3u;
    if (std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(2)) break;
  }
  impl_->counters.retired_proxy_count = impl_->retired.size() * 3u;
}

void EditableMeshSurfaceCache::clear() {
  discard_prepared();
  for (const auto &batch : impl_->batches) impl_->detach(*batch);
  impl_->batches.clear();
  impl_->retired.clear();
  impl_->base.reset();
  impl_->preview_targets.clear();
  impl_->layout.clear();
  impl_->effective.clear();
  impl_->enrolled.clear();
  impl_->keys.clear();
  impl_->fallback.clear();
  impl_->exclusion = {};
  impl_->suppression = {};
  impl_->has_stamp = false;
  impl_->counters = {};
}

void EditableMeshSurfaceCache::shutdown() {
  clear();
  impl_->factory = nullptr;
  impl_->binding = nullptr;
}

} // namespace quader
