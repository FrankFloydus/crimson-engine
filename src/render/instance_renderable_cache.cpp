////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/instance_renderable_cache.hpp"

#include "diagnostics/profile.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/render_layers.hpp"
#include "render/render_signature_builder.hpp"
#include "viewport/viewport_settings.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

/// Describes pass-specific render state applied to one explicit instance batch.
struct InstanceVariant final {
  std::uint8_t layer_mask = 0u;
  std::uint8_t channel = kViewportDefaultRenderableChannel;
  bool cast_shadows = false;
  bool receive_shadows = false;
  bool editor_unlit = false;
  bool depth_only = false;
};

constexpr std::array<InstanceVariant, 3u> kInstanceVariants{{
    {
        .layer_mask = kViewportRenderedSceneLayerMask,
        .cast_shadows = true,
        .receive_shadows = true,
    },
    {
        .layer_mask = kViewportShadedSceneLayerMask,
        .editor_unlit = true,
    },
    {
        .layer_mask = kViewportOverlayLayerMask,
        .channel = kViewportOverlayDepthChannel,
        .editor_unlit = true,
        .depth_only = true,
    },
}};

RenderInstancePrimitiveKey variant_key(
    const RenderSceneInstancePrimitiveRecord &record,
    const RenderSceneInstanceOccurrenceRecord &occurrence,
    const InstanceVariant &variant) {
  RenderInstancePrimitiveKey key = record.key;
  key.layer_mask = variant.layer_mask;
  key.channel = variant.channel;
  key.cast_shadows = variant.cast_shadows && key.cast_shadows;
  key.receive_shadows = variant.receive_shadows;
  key.editor_unlit = variant.editor_unlit;
  key.presentation_flags = occurrence.dimmed
                               ? static_cast<std::uint8_t>(
                                     RenderInstancePresentationFlag::Dimmed)
                               : 0u;
  return key;
}

const InstanceVariant *variant_for_key(const RenderInstancePrimitiveKey &key) {
  const auto found = std::ranges::find_if(kInstanceVariants,
                                         [&](const InstanceVariant &variant) {
    return variant.layer_mask == key.layer_mask &&
           variant.channel == key.channel &&
           variant.editor_unlit == key.editor_unlit;
  });
  return found == kInstanceVariants.end() ? nullptr : &*found;
}

std::span<const QMat4> visible_world_transforms(
    const RenderInstanceBatchChunk &chunk, std::vector<QMat4> &scratch) {
  if (chunk.visible_occurrence_count == chunk.world_transforms.size()) {
    return std::span<const QMat4>(chunk.world_transforms);
  }
  // Compact real transforms into the retained InstanceBuffer capacity. The
  // renderable binds only this active prefix, so suppression needs neither
  // duplicate draws nor singular/far-away sentinel matrices.
  scratch.clear();
  scratch.reserve(chunk.visible_occurrence_count);
  for (std::size_t index = 0u; index < chunk.world_transforms.size(); ++index) {
    if (index >= chunk.visibility.size() || chunk.visibility[index] != 0u) {
      scratch.push_back(chunk.world_transforms[index]);
    }
  }
  return std::span<const QMat4>(scratch);
}

bool same_persistent_chunk_layout(const RenderInstanceBatchChunk &left,
                                  const RenderInstanceBatchChunk &right) {
  return left.key == right.key && left.occurrences == right.occurrences &&
         left.world_transforms.size() == right.world_transforms.size() &&
         left.visibility.size() == right.visibility.size();
}

/// Projects the exact primitive fields historically compared by the scan.
struct PrimitiveBaseKey final {
  std::uint64_t geometry_payload = 0u;
  std::uint64_t material_payload = 0u;
  std::uint32_t index_offset = 0u;
  std::uint32_t index_count = 0u;
  bool double_sided = false;

  friend bool operator==(const PrimitiveBaseKey &,
                         const PrimitiveBaseKey &) = default;
};

PrimitiveBaseKey primitive_base_key(
    const RenderInstancePrimitiveKey &key) {
  return {
      .geometry_payload = key.geometry_payload,
      .material_payload = key.material_payload,
      .index_offset = key.index_offset,
      .index_count = key.index_count,
      .double_sided = key.double_sided,
  };
}

/// Hashes the immutable primitive facts shared by instanced chunks.
struct PrimitiveBaseKeyHash final {
  std::size_t operator()(const PrimitiveBaseKey &key) const noexcept {
    RenderSignatureBuilder signature;
    signature.add_value(key.geometry_payload)
        .add_value(key.material_payload)
        .add_value(key.index_offset)
        .add_value(key.index_count)
        .add_bool(key.double_sided);
    return static_cast<std::size_t>(signature.value());
  }
};

/// Borrows one stable chunk so the reuse index does not copy occurrence IDs.
struct PersistentChunkLayoutRef final {
  const RenderInstanceBatchChunk *chunk = nullptr;
};

/// Hashes a borrowed persistent chunk by its complete reusable layout.
struct PersistentChunkLayoutRefHash final {
  std::size_t operator()(
      const PersistentChunkLayoutRef &layout) const noexcept {
    const RenderInstanceBatchChunk &chunk = *layout.chunk;
    const RenderInstancePrimitiveKey &key = chunk.key;
    RenderSignatureBuilder signature;
    signature.add_value(key.geometry_payload)
        .add_value(key.material_payload)
        .add_value(key.index_offset)
        .add_value(key.index_count)
        .add_value(key.layer_mask)
        .add_value(key.channel)
        .add_value(key.presentation_flags)
        .add_bool(key.cast_shadows)
        .add_bool(key.receive_shadows)
        .add_bool(key.double_sided)
        .add_bool(key.editor_unlit)
        .add_size(chunk.occurrences.size());
    for (const std::uint64_t occurrence : chunk.occurrences) {
      signature.add_value(occurrence);
    }
    signature.add_size(chunk.world_transforms.size())
        .add_size(chunk.visibility.size());
    return static_cast<std::size_t>(signature.value());
  }
};

/// Compares borrowed persistent chunks for exact layout reuse.
struct PersistentChunkLayoutRefEqual final {
  bool operator()(const PersistentChunkLayoutRef &left,
                  const PersistentChunkLayoutRef &right) const {
    return same_persistent_chunk_layout(*left.chunk, *right.chunk);
  }
};

/// Retains old indices in original order for first-match scan parity.
struct PersistentChunkCandidateBucket final {
  std::vector<std::size_t> old_indices;
  std::size_t first_unprobed = 0u;
};

/// Holds a replacement proxy until every affected chunk has been created.
/// Unaffected live proxies remain untouched if any replacement creation fails.
struct StagedInstanceProxy final {
  MeshRenderableProxy proxy;
};

} // namespace

namespace instance_renderable_cache_detail {

PrimitiveRecordLookupResult resolve_primitive_records(
    std::span<const RenderSceneInstancePrimitiveRecord> primitives,
    std::span<const RenderInstanceBatchChunk> chunks) {
  PrimitiveRecordLookupResult result;
  std::unordered_map<PrimitiveBaseKey,
                     const RenderSceneInstancePrimitiveRecord *,
                     PrimitiveBaseKeyHash>
      first_record_by_key;
  first_record_by_key.reserve(primitives.size());
  for (const RenderSceneInstancePrimitiveRecord &record : primitives) {
    first_record_by_key.try_emplace(primitive_base_key(record.key), &record);
  }

  result.indexed_key_count = first_record_by_key.size();
  result.records_by_chunk.reserve(chunks.size());
  for (const RenderInstanceBatchChunk &chunk : chunks) {
    ++result.key_lookup_count;
    const auto found = first_record_by_key.find(primitive_base_key(chunk.key));
    result.records_by_chunk.push_back(
        found == first_record_by_key.end() ? nullptr : found->second);
  }
  return result;
}

ChunkProxyReuseResult match_reusable_chunk_proxies(
    std::span<const RenderInstanceBatchChunk> old_chunks,
    std::span<const ChunkProxyReuseCandidate> old_candidates,
    std::span<const RenderInstanceBatchChunk> next_chunks) {
  ChunkProxyReuseResult result;
  result.no_old_chunk = old_candidates.size();
  result.old_chunk_by_next_chunk.assign(next_chunks.size(),
                                        result.no_old_chunk);

  using CandidateIndex = std::unordered_map<
      PersistentChunkLayoutRef, PersistentChunkCandidateBucket,
      PersistentChunkLayoutRefHash, PersistentChunkLayoutRefEqual>;
  CandidateIndex candidates_by_layout;
  candidates_by_layout.reserve(
      std::min(old_chunks.size(), old_candidates.size()));
  for (std::size_t old_index = 0u;
       old_index < old_chunks.size() && old_index < old_candidates.size();
       ++old_index) {
    const ChunkProxyReuseCandidate &candidate = old_candidates[old_index];
    const RenderInstanceBatchChunk &chunk = old_chunks[old_index];
    if (!candidate.valid ||
        candidate.instance_capacity < chunk.world_transforms.size()) {
      continue;
    }
    auto [found, inserted] = candidates_by_layout.try_emplace(
        PersistentChunkLayoutRef{.chunk = &chunk});
    (void)inserted;
    found->second.old_indices.push_back(old_index);
    ++result.indexed_candidate_count;
  }

  std::vector<bool> old_candidate_reserved(old_candidates.size(), false);
  for (std::size_t next_index = 0u; next_index < next_chunks.size();
       ++next_index) {
    const RenderInstanceBatchChunk &next = next_chunks[next_index];
    if (next_index < old_chunks.size() &&
        next_index < old_candidates.size()) {
      ++result.same_index_probe_count;
      const ChunkProxyReuseCandidate &same_index =
          old_candidates[next_index];
      if (same_index.valid &&
          same_index.instance_capacity >= next.world_transforms.size() &&
          !old_candidate_reserved[next_index] &&
          same_persistent_chunk_layout(old_chunks[next_index], next)) {
        result.old_chunk_by_next_chunk[next_index] = next_index;
        old_candidate_reserved[next_index] = true;
        continue;
      }
    }

    ++result.key_lookup_count;
    const auto found = candidates_by_layout.find(
        PersistentChunkLayoutRef{.chunk = &next});
    if (found == candidates_by_layout.end()) {
      continue;
    }
    PersistentChunkCandidateBucket &bucket = found->second;
    while (bucket.first_unprobed < bucket.old_indices.size()) {
      const std::size_t old_index =
          bucket.old_indices[bucket.first_unprobed++];
      ++result.candidate_probe_count;
      if (old_candidate_reserved[old_index]) {
        continue;
      }
      result.old_chunk_by_next_chunk[next_index] = old_index;
      old_candidate_reserved[old_index] = true;
      break;
    }
  }
  return result;
}

} // namespace instance_renderable_cache_detail

InstanceRenderableCache::~InstanceRenderableCache() { shutdown(); }

void InstanceRenderableCache::initialize(
    MeshRenderableFactory &factory, std::size_t max_instances_per_chunk) {
  shutdown();
  factory_ = &factory;
  max_instances_per_chunk_ =
      std::max<std::size_t>(1u, max_instances_per_chunk);
}

void InstanceRenderableCache::shutdown() {
  clear();
  preview_suppressed_entities_.clear();
  preview_occurrence_suppressions_.clear();
  factory_ = nullptr;
  max_instances_per_chunk_ = 1u;
  material_uniform_signature_ = 0u;
  material_uniforms_dirty_ = true;
  last_sync_succeeded_ = true;
  stats_ = {};
}

void InstanceRenderableCache::clear() {
  if (factory_ != nullptr) {
    for (MeshRenderableProxy &proxy : proxies_) {
      factory_->destroy(proxy);
    }
  }
  proxies_.clear();
  plan_ = {};
  material_uniforms_dirty_ = true;
  stats_.participant_count = 0u;
  stats_.occurrence_count = 0u;
  stats_.chunk_count = 0u;
}

InstanceRenderableCache::PreparedPlan InstanceRenderableCache::prepare(
    std::span<const RenderSceneInstancePrimitiveRecord> primitives) const {
  QDR_PROFILE_FUNCTION();
  const auto occurrence_suppressed =
      [this](const RenderSceneInstancePrimitiveRecord &record,
             const RenderSceneInstanceOccurrenceRecord &occurrence) {
        if (preview_suppressed_entities_.contains(occurrence.entity_id) ||
            (record.definition_entity != kInvalidEntityId &&
             preview_suppressed_entities_.contains(record.definition_entity)) ||
            (record.member_entity != kInvalidEntityId &&
             preview_suppressed_entities_.contains(record.member_entity))) {
          return true;
        }
        const auto pair_suppressed = [&](EntityId source_entity) {
          const std::pair key{occurrence.entity_id, source_entity};
          const auto found = std::ranges::lower_bound(
              preview_occurrence_suppressions_, key, {},
              [](const RenderInstancePreviewSuppression &suppression) {
                return std::pair{suppression.occurrence,
                                 suppression.source_entity};
              });
          return found != preview_occurrence_suppressions_.end() &&
                 std::pair{found->occurrence, found->source_entity} == key;
        };
        return pair_suppressed(record.member_entity) ||
               (record.definition_entity != record.member_entity &&
                pair_suppressed(record.definition_entity));
      };
  std::size_t storage_occurrence_count = 0u;
  std::size_t visible_occurrence_count = 0u;
  std::size_t participant_count = 0u;
  for (const RenderSceneInstancePrimitiveRecord &record : primitives) {
    if (!record.renderable()) {
      continue;
    }
    storage_occurrence_count +=
        record.occurrences.size() * kInstanceVariants.size();
  }
  std::vector<RenderInstancePrimitiveOccurrence> occurrences;
  occurrences.reserve(storage_occurrence_count);
  for (const RenderSceneInstancePrimitiveRecord &record : primitives) {
    if (!record.renderable()) {
      continue;
    }
    for (const RenderSceneInstanceOccurrenceRecord &occurrence :
         record.occurrences) {
      const bool visible = !occurrence_suppressed(record, occurrence);
      if (visible) {
        ++participant_count;
        visible_occurrence_count += kInstanceVariants.size();
      }
      for (const InstanceVariant &variant : kInstanceVariants) {
        occurrences.push_back({
            .key = variant_key(record, occurrence, variant),
            .occurrence = occurrence.occurrence,
            .world_transform = occurrence.world_transform,
            .local_bounds = record.local_bounds,
            .visible = visible,
        });
      }
    }
  }

  PreparedPlan prepared;
  prepared.participant_count = participant_count;
  prepared.visible_occurrence_count = visible_occurrence_count;
  prepared.plan = plan_render_instance_batches(
      occurrences, max_instances_per_chunk_, &plan_);
  prepared.records_by_chunk =
      instance_renderable_cache_detail::resolve_primitive_records(
          primitives, prepared.plan.chunks)
          .records_by_chunk;
  return prepared;
}

void InstanceRenderableCache::set_preview_suppressed_entities(
    std::unordered_set<EntityId> entities,
    std::vector<RenderInstancePreviewSuppression> occurrence_suppressions,
    std::span<const RenderSceneInstancePrimitiveRecord> primitives) {
  if (preview_suppressed_entities_ == entities &&
      preview_occurrence_suppressions_ == occurrence_suppressions) {
    return;
  }
  QDR_PROFILE_SCOPE("Instance preview suppression delta");
  preview_suppressed_entities_ = std::move(entities);
  preview_occurrence_suppressions_ = std::move(occurrence_suppressions);
  // Suppression changes only at preview begin/end. Re-plan once at that
  // boundary; subsequent scene synchronization keeps consulting the retained
  // set and therefore cannot accidentally restore a suppressed occurrence.
  (void)sync(primitives);
}

RenderInstanceBatchUpdateKind InstanceRenderableCache::classify_update(
    std::span<const RenderSceneInstancePrimitiveRecord> primitives) const {
  if (!initialized()) {
    return RenderInstanceBatchUpdateKind::Rebuild;
  }
  return classify_instance_batch_update(plan_, prepare(primitives).plan);
}

bool InstanceRenderableCache::sync(
    std::span<const RenderSceneInstancePrimitiveRecord> primitives) {
  QDR_PROFILE_FUNCTION();
  if (!initialized()) {
    last_sync_succeeded_ = false;
    return false;
  }
  const PreparedPlan prepared = prepare(primitives);
  switch (classify_instance_batch_update(plan_, prepared.plan)) {
  case RenderInstanceBatchUpdateKind::Unchanged:
    last_sync_succeeded_ = true;
    return false;
  case RenderInstanceBatchUpdateKind::TransformsOnly:
    if (update_transforms(prepared)) {
      plan_ = prepared.plan;
      ++stats_.transform_update_count;
      stats_.participant_count = prepared.participant_count;
      stats_.occurrence_count = prepared.visible_occurrence_count;
      stats_.chunk_count = plan_.chunks.size();
      last_sync_succeeded_ = true;
      return true;
    }
    break;
  case RenderInstanceBatchUpdateKind::Rebuild:
    break;
  }
  const bool rebuilt = rebuild(prepared);
  last_sync_succeeded_ = rebuilt;
  return rebuilt;
}

MeshRenderableProxy InstanceRenderableCache::create_chunk_proxy(
    const PreparedPlan &prepared, std::size_t index) {
  QDR_PROFILE_FUNCTION();
  if (factory_ == nullptr || index >= prepared.plan.chunks.size() ||
      index >= prepared.records_by_chunk.size()) {
    return {};
  }

  const RenderInstanceBatchChunk &chunk = prepared.plan.chunks[index];
  const RenderSceneInstancePrimitiveRecord *record =
      prepared.records_by_chunk[index];
  const InstanceVariant *variant = variant_for_key(chunk.key);
  if (record == nullptr || variant == nullptr) {
    return {};
  }

  MeshPrimitiveSpan primitive = record->primitive;
  primitive.index_offset = chunk.key.index_offset;
  primitive.index_count = chunk.key.index_count;
  const std::uint8_t initial_layer_mask =
      chunk.visible_occurrence_count == 0u ? 0u : chunk.key.layer_mask;
  MeshRenderableProxy proxy = factory_->create_explicit_instances(
      record->mesh, &record->material, primitive, chunk.world_transforms,
      chunk.world_bounds, chunk.key.cast_shadows, chunk.key.receive_shadows,
      chunk.key.double_sided, initial_layer_mask, chunk.key.editor_unlit,
      variant->depth_only, chunk.key.channel, chunk.key.presentation_flags);
  if (!proxy.entity || proxy.instance_buffer == nullptr) {
    factory_->destroy(proxy);
    return {};
  }

  if (chunk.visible_occurrence_count != chunk.world_transforms.size()) {
    std::vector<QMat4> visible_transforms;
    const std::span<const QMat4> active_transforms =
        visible_world_transforms(chunk, visible_transforms);
    QDR_PROFILE_SCOPE("Instance create apply retained suppression");
    if (!factory_->rebind_explicit_instances(
            proxy, primitive, active_transforms, chunk.world_bounds,
            chunk.key.cast_shadows, chunk.key.receive_shadows,
            chunk.key.double_sided, initial_layer_mask, variant->depth_only,
            chunk.key.channel)) {
      factory_->destroy(proxy);
      return {};
    }
  }
  return proxy;
}

bool InstanceRenderableCache::update_chunk_proxy(
    const MeshRenderableProxy &proxy,
    const RenderInstanceBatchChunk &before,
    const RenderInstanceBatchChunk &after,
    const RenderSceneInstancePrimitiveRecord *record) {
  QDR_PROFILE_FUNCTION();
  if (before.transform_signature == after.transform_signature &&
      before.world_bounds == after.world_bounds &&
      before.visibility == after.visibility) {
    return true;
  }

  std::vector<QMat4> visible_transforms;
  const std::span<const QMat4> active_transforms =
      visible_world_transforms(after, visible_transforms);
  const bool visibility_changed = before.visibility != after.visibility;
  if (visibility_changed) {
    const InstanceVariant *variant = variant_for_key(after.key);
    if (record == nullptr || variant == nullptr) {
      return false;
    }
    MeshPrimitiveSpan primitive = record->primitive;
    primitive.index_offset = after.key.index_offset;
    primitive.index_count = after.key.index_count;
    const std::uint8_t layer_mask =
        active_transforms.empty() ? 0u : after.key.layer_mask;
    QDR_PROFILE_SCOPE("Instance suppression update affected chunk");
    return factory_->rebind_explicit_instances(
        proxy, primitive, active_transforms, after.world_bounds,
        after.key.cast_shadows, after.key.receive_shadows,
        after.key.double_sided, layer_mask, variant->depth_only,
        after.key.channel);
  }

  {
    QDR_PROFILE_SCOPE("Instance transform update affected chunk");
    return factory_->update_explicit_instances(proxy, active_transforms,
                                               after.world_bounds);
  }
}

bool InstanceRenderableCache::rebuild(const PreparedPlan &prepared) {
  QDR_PROFILE_FUNCTION();
  const std::size_t old_proxy_count = proxies_.size();
  const std::size_t no_proxy = old_proxy_count;
  std::vector<instance_renderable_cache_detail::ChunkProxyReuseCandidate>
      old_candidates;
  old_candidates.reserve(old_proxy_count);
  for (const MeshRenderableProxy &proxy : proxies_) {
    old_candidates.push_back({
        .instance_capacity =
            proxy.instance_buffer != nullptr
                ? proxy.instance_buffer->getInstanceCount()
                : 0u,
        .valid = proxy.entity && proxy.instance_buffer != nullptr,
    });
  }

  std::vector<std::size_t> old_proxy_by_chunk;
  {
    QDR_PROFILE_SCOPE("Instance rebuild match persistent chunks");
    old_proxy_by_chunk =
        instance_renderable_cache_detail::match_reusable_chunk_proxies(
            plan_.chunks, old_candidates, prepared.plan.chunks)
            .old_chunk_by_next_chunk;
  }

  std::vector<StagedInstanceProxy> staged;
  staged.reserve(prepared.plan.chunks.size());
  {
    QDR_PROFILE_SCOPE("Instance rebuild create affected chunks");
    for (std::size_t next_index = 0u;
         next_index < prepared.plan.chunks.size(); ++next_index) {
      if (old_proxy_by_chunk[next_index] != no_proxy) {
        continue;
      }
      MeshRenderableProxy proxy = create_chunk_proxy(prepared, next_index);
      if (!proxy.entity || proxy.instance_buffer == nullptr) {
        for (StagedInstanceProxy &replacement : staged) {
          factory_->destroy(replacement.proxy);
        }
        return false;
      }
      staged.push_back({.proxy = std::move(proxy)});
    }
  }

  {
    QDR_PROFILE_SCOPE("Instance rebuild update retained chunks");
    for (std::size_t next_index = 0u;
         next_index < prepared.plan.chunks.size(); ++next_index) {
      const std::size_t old_index = old_proxy_by_chunk[next_index];
      if (old_index == no_proxy) {
        continue;
      }
      if (!update_chunk_proxy(
              proxies_[old_index], plan_.chunks[old_index],
              prepared.plan.chunks[next_index],
              prepared.records_by_chunk[next_index])) {
        for (StagedInstanceProxy &replacement : staged) {
          factory_->destroy(replacement.proxy);
        }
        // A count-changing Filament rebind can leave that component detached
        // on failure. Preserve the old all-or-nothing recovery path for this
        // exceptional backend case; normal layout changes never reach it.
        QDR_PROFILE_SCOPE("Instance rebuild fallback full replacement");
        clear();
        return rebuild(prepared);
      }
    }
  }

  std::vector<MeshRenderableProxy> next_proxies;
  next_proxies.reserve(prepared.plan.chunks.size());
  std::size_t staged_index = 0u;
  for (std::size_t next_index = 0u;
       next_index < prepared.plan.chunks.size(); ++next_index) {
    const std::size_t old_index = old_proxy_by_chunk[next_index];
    if (old_index != no_proxy) {
      next_proxies.emplace_back(std::move(proxies_[old_index]));
      continue;
    }
    next_proxies.emplace_back(std::move(staged[staged_index].proxy));
    ++staged_index;
  }

  {
    QDR_PROFILE_SCOPE("Instance rebuild destroy stale chunks");
    for (MeshRenderableProxy &proxy : proxies_) {
      if (proxy.entity) {
        factory_->destroy(proxy);
      }
    }
  }
  const bool created_chunks = !staged.empty();
  proxies_ = std::move(next_proxies);
  plan_ = prepared.plan;
  ++stats_.rebuild_count;
  stats_.participant_count = prepared.participant_count;
  stats_.occurrence_count = prepared.visible_occurrence_count;
  stats_.chunk_count = plan_.chunks.size();
  material_uniforms_dirty_ = material_uniforms_dirty_ || created_chunks;
  return true;
}

bool InstanceRenderableCache::update_transforms(
    const PreparedPlan &prepared) {
  QDR_PROFILE_FUNCTION();
  if (prepared.plan.chunks.size() != proxies_.size()) {
    return false;
  }
  for (std::size_t index = 0u; index < proxies_.size(); ++index) {
    const RenderInstanceBatchChunk &before = plan_.chunks[index];
    const RenderInstanceBatchChunk &after = prepared.plan.chunks[index];
    if (!update_chunk_proxy(proxies_[index], before, after,
                            prepared.records_by_chunk[index])) {
      return false;
    }
  }
  return true;
}

void InstanceRenderableCache::update_surface_grid(
    float grid_world_spacing, const ViewportSettings &settings) {
  QDR_PROFILE_FUNCTION();
  if (factory_ == nullptr) {
    return;
  }

  const std::uint64_t signature =
      mesh_surface_grid_signature(grid_world_spacing, settings);
  if (!material_uniforms_dirty_ &&
      material_uniform_signature_ == signature) {
    return;
  }

  {
    QDR_PROFILE_SCOPE("Instance material uniforms apply");
    for (std::size_t index = 0u; index < proxies_.size(); ++index) {
      const MeshRenderableProxy &proxy = proxies_[index];
      factory_->update_surface_grid(proxy, grid_world_spacing, settings);
      if (index < plan_.chunks.size() &&
          plan_.chunks[index].key.layer_mask ==
              kViewportShadedSceneLayerMask &&
          plan_.chunks[index].key.editor_unlit) {
        factory_->update_shaded_lighting(proxy, settings);
      }
    }
  }
  material_uniform_signature_ = signature;
  material_uniforms_dirty_ = false;
}

} // namespace quader
