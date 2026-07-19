////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/instance_renderable_cache.hpp"

#include "diagnostics/profile.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/render_layers.hpp"
#include "viewport/viewport_settings.hpp"

#include <algorithm>
#include <array>
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

bool same_base_key(const RenderInstancePrimitiveKey &left,
                   const RenderInstancePrimitiveKey &right) {
  return left.geometry_payload == right.geometry_payload &&
         left.material_payload == right.material_payload &&
         left.index_offset == right.index_offset &&
         left.index_count == right.index_count &&
         left.double_sided == right.double_sided;
}

const RenderSceneInstancePrimitiveRecord *record_for_chunk(
    std::span<const RenderSceneInstancePrimitiveRecord> records,
    const RenderInstanceBatchChunk &chunk) {
  const auto found = std::ranges::find_if(records, [&](const auto &record) {
    return same_base_key(record.key, chunk.key);
  });
  return found == records.end() ? nullptr : &*found;
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

} // namespace

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
        return std::ranges::any_of(
            preview_occurrence_suppressions_,
            [&](const RenderInstancePreviewSuppression &suppression) {
              return suppression.occurrence == occurrence.entity_id &&
                     (suppression.source_entity == record.member_entity ||
                      suppression.source_entity == record.definition_entity);
            });
      };
  std::size_t occurrence_count = 0u;
  std::size_t participant_count = 0u;
  for (const RenderSceneInstancePrimitiveRecord &record : primitives) {
    if (!record.renderable()) {
      continue;
    }
    for (const RenderSceneInstanceOccurrenceRecord &occurrence :
         record.occurrences) {
      if (!occurrence_suppressed(record, occurrence)) {
        ++participant_count;
        occurrence_count += kInstanceVariants.size();
      }
    }
  }
  std::vector<RenderInstancePrimitiveOccurrence> occurrences;
  occurrences.reserve(occurrence_count);
  for (const RenderSceneInstancePrimitiveRecord &record : primitives) {
    if (!record.renderable()) {
      continue;
    }
    for (const InstanceVariant &variant : kInstanceVariants) {
      for (const RenderSceneInstanceOccurrenceRecord &occurrence :
           record.occurrences) {
        if (occurrence_suppressed(record, occurrence)) {
          continue;
        }
        occurrences.push_back({
            .key = variant_key(record, occurrence, variant),
            .occurrence = occurrence.occurrence,
            .world_transform = occurrence.world_transform,
            .local_bounds = record.local_bounds,
        });
      }
    }
  }

  PreparedPlan prepared;
  prepared.participant_count = participant_count;
  prepared.plan = plan_render_instance_batches(
      occurrences, max_instances_per_chunk_);
  prepared.records_by_chunk.reserve(prepared.plan.chunks.size());
  for (const RenderInstanceBatchChunk &chunk : prepared.plan.chunks) {
    prepared.records_by_chunk.push_back(record_for_chunk(primitives, chunk));
  }
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
    return false;
  }
  const PreparedPlan prepared = prepare(primitives);
  switch (classify_instance_batch_update(plan_, prepared.plan)) {
  case RenderInstanceBatchUpdateKind::Unchanged:
    return false;
  case RenderInstanceBatchUpdateKind::TransformsOnly:
    if (update_transforms(prepared)) {
      plan_ = prepared.plan;
      ++stats_.transform_update_count;
      stats_.participant_count = prepared.participant_count;
      stats_.occurrence_count = plan_.occurrence_count;
      stats_.chunk_count = plan_.chunks.size();
      return true;
    }
    break;
  case RenderInstanceBatchUpdateKind::Rebuild:
    break;
  }
  return rebuild(prepared);
}

bool InstanceRenderableCache::rebuild(const PreparedPlan &prepared) {
  QDR_PROFILE_FUNCTION();
  clear();
  proxies_.reserve(prepared.plan.chunks.size());
  for (std::size_t index = 0u; index < prepared.plan.chunks.size(); ++index) {
    const RenderInstanceBatchChunk &chunk = prepared.plan.chunks[index];
    const RenderSceneInstancePrimitiveRecord *record =
        prepared.records_by_chunk[index];
    const InstanceVariant *variant = variant_for_key(chunk.key);
    if (record == nullptr || variant == nullptr) {
      clear();
      return false;
    }
    MeshPrimitiveSpan primitive = record->primitive;
    primitive.index_offset = chunk.key.index_offset;
    primitive.index_count = chunk.key.index_count;
    MeshRenderableProxy proxy = factory_->create_explicit_instances(
        record->mesh, &record->material, primitive, chunk.world_transforms,
        chunk.world_bounds, chunk.key.cast_shadows,
        chunk.key.receive_shadows, chunk.key.double_sided,
        chunk.key.layer_mask, chunk.key.editor_unlit, variant->depth_only,
        chunk.key.channel, chunk.key.presentation_flags);
    if (!proxy.entity || proxy.instance_buffer == nullptr) {
      factory_->destroy(proxy);
      clear();
      return false;
    }
    proxies_.push_back(std::move(proxy));
  }
  plan_ = prepared.plan;
  ++stats_.rebuild_count;
  stats_.participant_count = prepared.participant_count;
  stats_.occurrence_count = plan_.occurrence_count;
  stats_.chunk_count = plan_.chunks.size();
  return true;
}

bool InstanceRenderableCache::update_transforms(
    const PreparedPlan &prepared) {
  if (prepared.plan.chunks.size() != proxies_.size()) {
    return false;
  }
  for (std::size_t index = 0u; index < proxies_.size(); ++index) {
    const RenderInstanceBatchChunk &chunk = prepared.plan.chunks[index];
    if (!factory_->update_explicit_instances(
            proxies_[index], chunk.world_transforms, chunk.world_bounds)) {
      return false;
    }
  }
  return true;
}

void InstanceRenderableCache::update_surface_grid(
    float grid_world_spacing, const ViewportSettings &settings) {
  if (factory_ == nullptr) {
    return;
  }
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

} // namespace quader
