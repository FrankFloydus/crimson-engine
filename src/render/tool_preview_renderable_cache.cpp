////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/tool_preview_renderable_cache.hpp"

#include "diagnostics/profile.hpp"
#include "render/material_binding_service.hpp"
#include "render/render_layers.hpp"
#include "render/render_signature_builder.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <numeric>
#include <span>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

std::size_t
preview_transform_count(const RenderToolPreviewMeshRecord &preview_mesh) {
  return preview_mesh.instance_transforms.empty()
             ? 1u
             : preview_mesh.instance_transforms.size();
}

const QMat4 &preview_transform(const RenderToolPreviewMeshRecord &preview_mesh,
                               std::size_t index) {
  return preview_mesh.instance_transforms.empty()
             ? preview_mesh.transform
             : preview_mesh.instance_transforms[index];
}

const QMat4 &preview_texture_mapping(
    const RenderToolPreviewMeshRecord &preview_mesh, std::size_t index) {
  return index < preview_mesh.instance_sampling_from_local.size()
             ? preview_mesh.instance_sampling_from_local[index]
             : preview_mesh.sampling_from_local;
}

void copy_preview_texture_mappings(
    std::vector<QMat4> &destination,
    const RenderToolPreviewMeshRecord &preview_mesh) {
  destination.resize(preview_transform_count(preview_mesh));
  for (std::size_t index = 0u; index < destination.size(); ++index) {
    destination[index] = preview_texture_mapping(preview_mesh, index);
  }
}

void copy_preview_transforms(std::vector<QMat4> &destination,
                             const RenderToolPreviewMeshRecord &preview_mesh) {
  const std::size_t count = preview_transform_count(preview_mesh);
  destination.resize(count);
  for (std::size_t index = 0u; index < count; ++index) {
    destination[index] = preview_transform(preview_mesh, index);
  }
}

std::span<const QVec3>
preview_dynamic_positions(const RenderToolPreviewMeshRecord &preview_mesh) {
  return preview_mesh.dynamic_position_payload
             ? std::span<const QVec3>(*preview_mesh.dynamic_position_payload)
             : std::span<const QVec3>{};
}

bool same_float_bits(float left, float right) {
  return std::bit_cast<std::uint32_t>(left) ==
         std::bit_cast<std::uint32_t>(right);
}

bool same_vec3(const QVec3 left, const QVec3 right) {
  return same_float_bits(left.x, right.x) &&
         same_float_bits(left.y, right.y) &&
         same_float_bits(left.z, right.z);
}

bool same_mat4(const QMat4 &left, const QMat4 &right) {
  return std::equal(left.values.begin(), left.values.end(),
                    right.values.begin(), same_float_bits);
}

bool preview_transforms_equal(
    const std::vector<QMat4> &transforms,
    const RenderToolPreviewMeshRecord &preview_mesh) {
  if (transforms.size() != preview_transform_count(preview_mesh)) {
    return false;
  }
  for (std::size_t index = 0u; index < transforms.size(); ++index) {
    if (!same_mat4(transforms[index], preview_transform(preview_mesh, index))) {
      return false;
    }
  }
  return true;
}

bool preview_texture_mappings_equal(
    const std::vector<QMat4> &mappings,
    const RenderToolPreviewMeshRecord &preview_mesh) {
  if (mappings.size() != preview_transform_count(preview_mesh)) {
    return false;
  }
  for (std::size_t index = 0u; index < mappings.size(); ++index) {
    if (!same_mat4(mappings[index], preview_texture_mapping(preview_mesh, index))) {
      return false;
    }
  }
  return true;
}

bool same_geometry_payload(
    const std::shared_ptr<const MeshGeometryPayload> &left,
    const std::shared_ptr<const MeshGeometryPayload> &right,
    bool include_positions) {
  if (left.get() == right.get()) {
    return true;
  }
  if (!left || !right || left->vertices.size() != right->vertices.size() ||
      left->indices != right->indices ||
      left->primitives != right->primitives) {
    return false;
  }
  for (std::size_t index = 0u; index < left->vertices.size(); ++index) {
    const MeshVertex &a = left->vertices[index];
    const MeshVertex &b = right->vertices[index];
    if ((include_positions && !same_vec3(a.position, b.position)) ||
        a.source_vertex_id != b.source_vertex_id ||
        !same_vec3(a.normal, b.normal) ||
        !same_float_bits(a.uv.x, b.uv.x) ||
        !same_float_bits(a.uv.y, b.uv.y) || a.rgba != b.rgba ||
        a.uv_carrier != b.uv_carrier ||
        !same_vec3(a.uv_geometric_normal, b.uv_geometric_normal)) {
      return false;
    }
  }
  return true;
}

bool same_dynamic_geometry_layout(
    const std::shared_ptr<const MeshGeometryPayload> &left,
    const std::shared_ptr<const MeshGeometryPayload> &right) {
  if (left.get() == right.get()) {
    return true;
  }
  if (!left || !right || left->vertices.size() != right->vertices.size() ||
      left->indices.size() != right->indices.size() ||
      left->primitives != right->primitives) {
    return false;
  }
  for (std::size_t index = 0u; index < left->vertices.size(); ++index) {
    if (left->vertices[index].source_vertex_id !=
        right->vertices[index].source_vertex_id) {
      return false;
    }
  }
  return true;
}

std::uint64_t exact_reuse_signature(std::uint64_t base_signature,
                                    bool wildcard_dynamic_layout,
                                    std::uint64_t dynamic_layout_key) {
  return RenderSignatureBuilder{base_signature}
      .add_bool(wildcard_dynamic_layout)
      .add_value(dynamic_layout_key)
      .value();
}

/// Retains candidate group indices in stable source order during matching.
struct StableCandidateBucket final {
  std::vector<std::size_t> group_indices;
  std::size_t cursor = 0u;
};

using StableCandidateBuckets =
    std::unordered_map<std::uint64_t, StableCandidateBucket>;

void append_candidate(StableCandidateBuckets &buckets, std::uint64_t key,
                      std::size_t group_index) {
  buckets[key].group_indices.push_back(group_index);
}

/// Identifies one unclaimed retained group selected from a candidate bucket.
struct CandidateMatch final {
  StableCandidateBucket *bucket = nullptr;
  std::size_t bucket_position = 0u;
  std::size_t group_index = 0u;

  [[nodiscard]] explicit operator bool() const noexcept {
    return bucket != nullptr;
  }
};

template <typename Matches>
CandidateMatch find_candidate(StableCandidateBuckets &buckets,
                              std::uint64_t key,
                              const std::vector<bool> &claimed,
                              std::uint64_t &candidate_visits,
                              Matches &&matches) {
  const auto found = buckets.find(key);
  if (found == buckets.end()) {
    return {};
  }
  StableCandidateBucket &bucket = found->second;
  while (bucket.cursor < bucket.group_indices.size() &&
         claimed[bucket.group_indices[bucket.cursor]]) {
    ++candidate_visits;
    ++bucket.cursor;
  }
  for (std::size_t position = bucket.cursor;
       position < bucket.group_indices.size(); ++position) {
    ++candidate_visits;
    const std::size_t group_index = bucket.group_indices[position];
    if (!claimed[group_index] && matches(group_index)) {
      return {
          .bucket = &bucket,
          .bucket_position = position,
          .group_index = group_index,
      };
    }
  }
  return {};
}

void claim_candidate(const CandidateMatch match, std::vector<bool> &claimed) {
  assert(match);
  assert(match.group_index < claimed.size());
  assert(!claimed[match.group_index]);
  claimed[match.group_index] = true;
  if (match.bucket_position == match.bucket->cursor) {
    ++match.bucket->cursor;
  }
}

CandidateMatch earlier_candidate(CandidateMatch left,
                                 CandidateMatch right) {
  if (!left) {
    return right;
  }
  if (!right) {
    return left;
  }
  return left.group_index <= right.group_index ? left : right;
}

} // namespace

ToolPreviewRenderableCache::~ToolPreviewRenderableCache() {
  shutdown();
}

void ToolPreviewRenderableCache::initialize(
    MeshRenderableFactory &factory, MaterialBindingService &material_binding) {
  factory_ = &factory;
  material_binding_ = &material_binding;
}

void ToolPreviewRenderableCache::shutdown() {
  if (!factory_) {
    return;
  }
  clear();
  factory_ = nullptr;
  material_binding_ = nullptr;
}

ToolPreviewSubmissionResult ToolPreviewRenderableCache::set_meshes(
    const std::vector<RenderToolPreviewMeshRecord> &meshes) {
  QDR_PROFILE_FUNCTION();
  if (!factory_ || !factory_->initialized() || !material_binding_) {
    return ToolPreviewSubmissionResult::Failed;
  }

  const std::uint64_t next_signature = preview_meshes_signature(meshes);
  // The aggregate hash is only a cheap rejection gate. Exact state, with
  // pointer-first geometry checks, prevents a collision from suppressing a
  // real submission while keeping repeated dense ticks O(group count).
  if (next_signature == mesh_signature_ &&
      submitted_meshes_equal(submitted_meshes_, meshes)) {
    return ToolPreviewSubmissionResult::Unchanged;
  }

  if (meshes.empty()) {
    QDR_PROFILE_SCOPE("Tool preview retain current and warm generations");
    if (current_generation_) {
      hide_generation(*current_generation_);
    }
    mesh_signature_ = next_signature;
    submitted_meshes_ = capture_submission(meshes);
    return ToolPreviewSubmissionResult::Published;
  }

  std::vector<std::size_t> candidate_indices;
  if (current_generation_) {
    QDR_PROFILE_SCOPE("Tool preview match current generation");
    if (prepare_generation(*current_generation_, meshes, candidate_indices)) {
      publish_generation_subset(*current_generation_,
                                std::move(candidate_indices));
      mesh_signature_ = next_signature;
      submitted_meshes_ = capture_submission(meshes);
      material_uniforms_dirty_ =
          !active_material_uniforms_are_current(*current_generation_);
      return ToolPreviewSubmissionResult::Published;
    }
  }

  if (warm_generation_) {
    QDR_PROFILE_SCOPE("Tool preview match warm generation");
    if (prepare_generation(*warm_generation_, meshes, candidate_indices)) {
      // The candidate is made visible first. No frame can observe a source
      // hidden without either the old or new preview surface present.
      publish_generation_subset(*warm_generation_,
                                std::move(candidate_indices));
      if (current_generation_) {
        hide_generation(*current_generation_);
      }
      current_generation_.swap(warm_generation_);
      mesh_signature_ = next_signature;
      submitted_meshes_ = capture_submission(meshes);
      material_uniforms_dirty_ =
          !active_material_uniforms_are_current(*current_generation_);
      return ToolPreviewSubmissionResult::Published;
    }
  }

  std::unique_ptr<PreviewGeneration> candidate;
  bool candidate_complete = false;
  {
    QDR_PROFILE_SCOPE("Tool preview build hidden candidate generation");
    candidate = build_generation(meshes, candidate_complete);
  }
  if (!candidate_complete) {
    // Successful pieces of a failed candidate are detached and queued. The
    // submission path never destroys a Filament proxy synchronously.
    retire_generation(std::move(candidate));
    return ToolPreviewSubmissionResult::Failed;
  }

  candidate_indices.resize(candidate->groups.size());
  std::iota(candidate_indices.begin(), candidate_indices.end(), 0u);
  {
    QDR_PROFILE_SCOPE("Tool preview publish candidate generation");
    publish_generation_subset(*candidate, std::move(candidate_indices));
    if (current_generation_) {
      hide_generation(*current_generation_);
    }
  }

  // Keep the immediate predecessor intact. Only an older third generation is
  // moved, in O(1), to an explicitly budgeted idle-destruction queue.
  if (warm_generation_) {
    QDR_PROFILE_SCOPE("Tool preview retire older warm generation");
    retire_generation(std::move(warm_generation_));
  }
  warm_generation_ = std::move(current_generation_);
  current_generation_ = std::move(candidate);
  mesh_signature_ = next_signature;
  submitted_meshes_ = capture_submission(meshes);
  material_uniforms_dirty_ =
      !active_material_uniforms_are_current(*current_generation_);
  return ToolPreviewSubmissionResult::Published;
}

std::unique_ptr<ToolPreviewRenderableCache::PreviewGeneration>
ToolPreviewRenderableCache::build_generation(
    const std::vector<RenderToolPreviewMeshRecord> &meshes, bool &complete) {
  complete = false;
  auto generation = std::make_unique<PreviewGeneration>();
  generation->groups.reserve(meshes.size());
  generation->scene_entities.reserve(meshes.size() * 3u);

  const auto retain_entity = [&](const MeshRenderableProxy &proxy) {
    if (proxy.entity) {
      generation->scene_entities.push_back(proxy.entity);
    }
  };

  for (const RenderToolPreviewMeshRecord &preview_mesh : meshes) {
    if (!preview_mesh.renderable()) {
      if (preview_mesh.suppress_source) {
        return generation;
      }
      continue;
    }

    std::vector<QMat4> transforms;
    copy_preview_transforms(transforms, preview_mesh);
    std::vector<QMat4> mappings;
    copy_preview_texture_mappings(mappings, preview_mesh);
    if (transforms.empty()) {
      if (preview_mesh.suppress_source) {
        return generation;
      }
      continue;
    }

    const RenderMeshAssetSnapshot mesh_snapshot =
        render_mesh_asset_snapshot(kInvalidAssetId, preview_mesh.mesh);
    const std::span<const QVec3> positions =
        preview_dynamic_positions(preview_mesh);
    const RenderInstanceBounds local_bounds =
        preview_mesh.dynamic_positions
            ? (positions.empty()
                   ? MeshRenderableFactory::compute_local_bounds(mesh_snapshot)
                   : MeshRenderableFactory::compute_local_bounds(positions))
            : RenderInstanceBounds{};
    const RenderInstanceBounds *bounds_override =
        preview_mesh.dynamic_positions ? &local_bounds : nullptr;
    MeshRenderableProxy rendered_prototype = factory_->create(
        preview_mesh.source_entity, mesh_snapshot, &preview_mesh.material,
        transforms.front(), false, preview_mesh.double_sided, 0u, false, false,
        kViewportDefaultRenderableChannel, preview_mesh.dynamic_positions,
        positions, bounds_override);
    if (!rendered_prototype.entity) {
      return generation;
    }
    retain_entity(rendered_prototype);
    factory_->update_texture_mapping(rendered_prototype, mappings.front());

    MeshRenderableProxy shaded_prototype = factory_->create_shared_geometry(
        preview_mesh.source_entity, mesh_snapshot, &preview_mesh.material,
        rendered_prototype, transforms.front(), preview_mesh.double_sided, 0u,
        true, false,
        kViewportDefaultRenderableChannel, bounds_override);
    const auto make_group = [&](MeshRenderableProxy rendered,
                                MeshRenderableProxy shaded = {},
                                MeshRenderableProxy overlay_depth = {}) {
      return PreviewGroup{
          .source_object_id = preview_mesh.source_object_id,
          .source_entity = preview_mesh.source_entity,
          .visibility_entity = preview_mesh.visibility_entity,
          .geometry_signature = preview_geometry_signature(preview_mesh),
          .topology_signature = preview_topology_signature(preview_mesh),
          .material_signature = preview_material_signature(preview_mesh),
          .material_uniform_signature = 0u,
          .layout_key = preview_mesh.layout_key,
          .position_revision = preview_mesh.position_revision,
          .topology_payload = preview_mesh.mesh.geometry_payload(),
          .transforms = std::move(transforms),
          .sampling_from_local = std::move(mappings),
          .dynamic_positions = preview_mesh.dynamic_positions,
          .visible = false,
          .material_uniforms_initialized = false,
          .live_proxy_count =
              static_cast<std::size_t>(rendered.entity ? 1u : 0u) +
              static_cast<std::size_t>(shaded.entity ? 1u : 0u) +
              static_cast<std::size_t>(overlay_depth.entity ? 1u : 0u),
          .rendered_prototype = std::move(rendered),
          .shaded_prototype = std::move(shaded),
          .overlay_depth_prototype = std::move(overlay_depth),
      };
    };
    if (!shaded_prototype.entity) {
      generation->groups.push_back(
          make_group(std::move(rendered_prototype)));
      return generation;
    }
    retain_entity(shaded_prototype);
    factory_->update_texture_mapping(shaded_prototype, mappings.front());
    MeshRenderableProxy overlay_depth_prototype =
        factory_->create_shared_geometry(
            preview_mesh.source_entity, mesh_snapshot, &preview_mesh.material,
            rendered_prototype, transforms.front(), preview_mesh.double_sided,
            0u, true, true, kViewportOverlayDepthChannel, bounds_override);
    if (!overlay_depth_prototype.entity) {
      generation->groups.push_back(make_group(
          std::move(rendered_prototype), std::move(shaded_prototype)));
      return generation;
    }
    retain_entity(overlay_depth_prototype);
    factory_->update_texture_mapping(overlay_depth_prototype, mappings.front());
    PreviewGroup group = make_group(
        std::move(rendered_prototype), std::move(shaded_prototype),
        std::move(overlay_depth_prototype));
    group.rendered_instances.reserve(group.transforms.size() - 1u);
    group.shaded_instances.reserve(group.transforms.size() - 1u);
    group.overlay_depth_instances.reserve(group.transforms.size() - 1u);
    for (std::size_t index = 1u; index < group.transforms.size(); ++index) {
      MeshRenderableProxy rendered_instance = factory_->create_shared_instance(
          preview_mesh.source_entity, mesh_snapshot,
          group.rendered_prototype, group.transforms[index], 0u,
          kViewportDefaultRenderableChannel, true, bounds_override);
      if (!rendered_instance.entity) {
        generation->groups.push_back(std::move(group));
        return generation;
      }
      retain_entity(rendered_instance);
      factory_->update_texture_mapping(rendered_instance, group.sampling_from_local[index]);
      group.rendered_instances.push_back(std::move(rendered_instance));
      ++group.live_proxy_count;

      MeshRenderableProxy shaded_instance = factory_->create_shared_instance(
          preview_mesh.source_entity, mesh_snapshot, group.shaded_prototype,
          group.transforms[index], 0u, kViewportDefaultRenderableChannel,
          false, bounds_override);
      if (!shaded_instance.entity) {
        generation->groups.push_back(std::move(group));
        return generation;
      }
      retain_entity(shaded_instance);
      factory_->update_texture_mapping(shaded_instance, group.sampling_from_local[index]);
      group.shaded_instances.push_back(std::move(shaded_instance));
      ++group.live_proxy_count;

      MeshRenderableProxy overlay_depth_instance =
          factory_->create_shared_instance(
              preview_mesh.source_entity, mesh_snapshot,
              group.overlay_depth_prototype, group.transforms[index], 0u,
              kViewportOverlayDepthChannel, false, bounds_override);
      if (!overlay_depth_instance.entity) {
        generation->groups.push_back(std::move(group));
        return generation;
      }
      retain_entity(overlay_depth_instance);
      factory_->update_texture_mapping(overlay_depth_instance,
                                       group.sampling_from_local[index]);
      group.overlay_depth_instances.push_back(
          std::move(overlay_depth_instance));
      ++group.live_proxy_count;
    }
    generation->groups.push_back(std::move(group));
  }
  complete = true;
  return generation;
}

bool ToolPreviewRenderableCache::prepare_generation(
    PreviewGeneration &generation,
    const std::vector<RenderToolPreviewMeshRecord> &meshes,
    std::vector<std::size_t> &candidate_indices) {
  QDR_PROFILE_FUNCTION();
  candidate_indices.clear();

  // Steady modal ticks preserve submission order and the active group set.
  // Validate that common case without allocating lookup tables or hashing
  // every retained inactive group.
  if (meshes.size() == generation.active_group_indices.size()) {
    bool ordered_active_match = true;
    for (std::size_t index = 0u; index < meshes.size(); ++index) {
      const std::size_t group_index =
          generation.active_group_indices[index];
      if (!meshes[index].renderable() ||
          group_index >= generation.groups.size() ||
          !group_matches_exact(generation.groups[group_index],
                               meshes[index]) ||
          (meshes[index].dynamic_positions &&
           !group_dynamic_update_is_valid(generation.groups[group_index],
                                          meshes[index]))) {
        ordered_active_match = false;
        break;
      }
    }
    if (ordered_active_match) {
      QDR_PROFILE_SCOPE("Tool preview ordered active fast path");
      candidate_indices = generation.active_group_indices;
      for (std::size_t index = 0u; index < meshes.size(); ++index) {
        PreviewGroup &group = generation.groups[candidate_indices[index]];
        if (!group_submission_unchanged(group, meshes[index])) {
          update_group(group, meshes[index]);
        }
      }
      return true;
    }
  }

  std::vector<const RenderToolPreviewMeshRecord *> renderable_meshes;
  renderable_meshes.reserve(meshes.size());
  for (const RenderToolPreviewMeshRecord &mesh : meshes) {
    if (mesh.renderable()) {
      renderable_meshes.push_back(&mesh);
    } else if (mesh.suppress_source) {
      return false;
    }
  }
  if (renderable_meshes.size() > generation.groups.size()) {
    return false;
  }

  if (match_generation_groups(
          generation,
          std::span<const RenderToolPreviewMeshRecord *const>{
              renderable_meshes.data(), renderable_meshes.size()},
          candidate_indices) ==
      GenerationMatchResult::None) {
    return false;
  }

  for (std::size_t index = 0u; index < renderable_meshes.size(); ++index) {
    const PreviewGroup &group = generation.groups[candidate_indices[index]];
    if (renderable_meshes[index]->dynamic_positions &&
        !group_dynamic_update_is_valid(group, *renderable_meshes[index])) {
      return false;
    }
  }

  {
    QDR_PROFILE_SCOPE("Tool preview update reused candidate groups");
    for (std::size_t index = 0u; index < renderable_meshes.size(); ++index) {
      PreviewGroup &group = generation.groups[candidate_indices[index]];
      if (!group_submission_unchanged(group, *renderable_meshes[index])) {
        update_group(group, *renderable_meshes[index]);
      }
    }
  }
  return true;
}

ToolPreviewRenderableCache::GenerationMatchResult
ToolPreviewRenderableCache::match_generation_groups(
    const PreviewGeneration &generation,
    std::span<const RenderToolPreviewMeshRecord *const> renderable_meshes,
    std::vector<std::size_t> &candidate_indices) {
  candidate_indices.clear();
  if (renderable_meshes.size() > generation.groups.size()) {
    return GenerationMatchResult::None;
  }

  // Exact candidates use the complete stable association and geometry facts,
  // not only source_entity. Each bucket retains generation order and advances
  // a cursor after a claim, so repeated equivalent groups are visited once
  // instead of rescanning an ever-growing claimed prefix.
  std::vector<bool> claimed(generation.groups.size(), false);
  StableCandidateBuckets exact_buckets;
  StableCandidateBuckets exact_dynamic_wildcard_buckets;
  exact_buckets.reserve(generation.groups.size() * 2u);
  exact_dynamic_wildcard_buckets.reserve(generation.groups.size());
  for (std::size_t index = 0u; index < generation.groups.size(); ++index) {
    const PreviewGroup &group = generation.groups[index];
    const std::uint64_t base = group_exact_reuse_base_signature(group);
    append_candidate(
        exact_buckets,
        exact_reuse_signature(base, false,
                              group.dynamic_positions ? group.layout_key : 0u),
        index);
    if (group.dynamic_positions) {
      append_candidate(exact_dynamic_wildcard_buckets,
                       exact_reuse_signature(base, true, 0u), index);
    }
  }

  bool exact = true;
  for (const RenderToolPreviewMeshRecord *mesh : renderable_meshes) {
    const std::uint64_t base = preview_exact_reuse_base_signature(*mesh);
    const auto matches = [&](std::size_t group_index) {
      return group_matches_exact(generation.groups[group_index], *mesh);
    };
    CandidateMatch match;
    if (!mesh->dynamic_positions) {
      match = find_candidate(
          exact_buckets, exact_reuse_signature(base, false, 0u), claimed,
          work_counters_.exact_match_candidate_visits, matches);
    } else if (mesh->layout_key == 0u) {
      match = find_candidate(
          exact_dynamic_wildcard_buckets,
          exact_reuse_signature(base, true, 0u), claimed,
          work_counters_.exact_match_candidate_visits, matches);
    } else {
      CandidateMatch same_layout = find_candidate(
          exact_buckets,
          exact_reuse_signature(base, false, mesh->layout_key), claimed,
          work_counters_.exact_match_candidate_visits, matches);
      CandidateMatch wildcard_layout = find_candidate(
          exact_buckets, exact_reuse_signature(base, false, 0u), claimed,
          work_counters_.exact_match_candidate_visits, matches);
      match = earlier_candidate(same_layout, wildcard_layout);
    }
    if (!match) {
      exact = false;
      break;
    }
    claim_candidate(match, claimed);
    candidate_indices.push_back(match.group_index);
  }
  if (exact) {
    return GenerationMatchResult::Exact;
  }

  // Structural reassignment changes the stable source/layout identity, so it
  // is allowed only when the complete generation is being reused. Subset
  // reuse remains exact and therefore preserves a dense warm generation.
  if (renderable_meshes.size() != generation.groups.size()) {
    candidate_indices.clear();
    return GenerationMatchResult::None;
  }

  candidate_indices.clear();
  std::fill(claimed.begin(), claimed.end(), false);
  StableCandidateBuckets structural_buckets;
  structural_buckets.reserve(generation.groups.size());
  for (std::size_t index = 0u; index < generation.groups.size(); ++index) {
    append_candidate(
        structural_buckets,
        group_structural_reuse_signature(generation.groups[index]), index);
  }
  for (const RenderToolPreviewMeshRecord *mesh : renderable_meshes) {
    const CandidateMatch match = find_candidate(
        structural_buckets, preview_structural_reuse_signature(*mesh),
        claimed, work_counters_.structural_match_candidate_visits,
        [&](std::size_t group_index) {
          return group_matches_structure(generation.groups[group_index],
                                         *mesh);
        });
    if (!match) {
      candidate_indices.clear();
      return GenerationMatchResult::None;
    }
    claim_candidate(match, claimed);
    candidate_indices.push_back(match.group_index);
  }
  return GenerationMatchResult::Structural;
}

void ToolPreviewRenderableCache::update_group(
    PreviewGroup &group,
    const RenderToolPreviewMeshRecord &preview_mesh) {
  QDR_PROFILE_FUNCTION();
  const bool identity_changed =
      group.source_object_id != preview_mesh.source_object_id ||
      group.source_entity != preview_mesh.source_entity ||
      group.visibility_entity != preview_mesh.visibility_entity ||
      group.layout_key != preview_mesh.layout_key;
  const std::uint64_t geometry_signature =
      preview_geometry_signature(preview_mesh);
  const std::shared_ptr<const MeshGeometryPayload> topology_payload =
      preview_mesh.mesh.geometry_payload();
  const bool positions_changed =
      preview_mesh.dynamic_positions &&
      (identity_changed ||
       group.position_revision != preview_mesh.position_revision ||
       group.geometry_signature != geometry_signature ||
       (!preview_mesh.dynamic_position_payload &&
         group.topology_payload.get() != topology_payload.get()));

  if (positions_changed) {
    QDR_PROFILE_SCOPE("Tool preview changed position payload");
    const RenderMeshAssetSnapshot mesh_snapshot =
        render_mesh_asset_snapshot(kInvalidAssetId, preview_mesh.mesh);
    const std::span<const QVec3> positions =
        preview_dynamic_positions(preview_mesh);
    const RenderInstanceBounds local_bounds =
        positions.empty()
            ? MeshRenderableFactory::compute_local_bounds(mesh_snapshot)
            : MeshRenderableFactory::compute_local_bounds(positions);
    // prepare_generation exhaustively validates the only guards that can make
    // update_dynamic_geometry return false. Factory lifetime is single-threaded
    // here, so no guard can change between validation and this mutation pass.
    const bool updated = factory_->update_dynamic_geometry(
        group.rendered_prototype, mesh_snapshot, positions, &local_bounds);
    assert(updated);
    (void)updated;
    factory_->update_bounds(group.shaded_prototype, local_bounds);
    factory_->update_bounds(group.overlay_depth_prototype, local_bounds);
    for (std::size_t index = 0u;
         index < group.rendered_instances.size(); ++index) {
      factory_->update_bounds(group.rendered_instances[index], local_bounds);
      factory_->update_bounds(group.shaded_instances[index], local_bounds);
      factory_->update_bounds(group.overlay_depth_instances[index],
                              local_bounds);
    }
  }

  if (!preview_transforms_equal(group.transforms, preview_mesh)) {
    QDR_PROFILE_SCOPE("Tool preview changed transforms");
    copy_preview_transforms(group.transforms, preview_mesh);
    factory_->update_transform(group.rendered_prototype,
                               group.transforms.front());
    factory_->update_transform(group.shaded_prototype,
                               group.transforms.front());
    factory_->update_transform(group.overlay_depth_prototype,
                               group.transforms.front());
    for (std::size_t index = 0u; index < group.rendered_instances.size();
         ++index) {
      factory_->update_transform(group.rendered_instances[index],
                                 group.transforms[index + 1u]);
      factory_->update_transform(group.shaded_instances[index],
                                 group.transforms[index + 1u]);
      factory_->update_transform(group.overlay_depth_instances[index],
                                 group.transforms[index + 1u]);
    }
  }

  if (!preview_texture_mappings_equal(group.sampling_from_local, preview_mesh)) {
    QDR_PROFILE_SCOPE("Tool preview changed texture mappings");
    copy_preview_texture_mappings(group.sampling_from_local, preview_mesh);
    factory_->update_texture_mapping(group.rendered_prototype,
                                     group.sampling_from_local.front());
    factory_->update_texture_mapping(group.shaded_prototype,
                                     group.sampling_from_local.front());
    factory_->update_texture_mapping(group.overlay_depth_prototype,
                                     group.sampling_from_local.front());
    for (std::size_t index = 0u; index < group.rendered_instances.size(); ++index) {
      const QMat4 &mapping = group.sampling_from_local[index + 1u];
      factory_->update_texture_mapping(group.rendered_instances[index], mapping);
      factory_->update_texture_mapping(group.shaded_instances[index], mapping);
      factory_->update_texture_mapping(group.overlay_depth_instances[index], mapping);
    }
  }

  if (group.source_entity != preview_mesh.source_entity) {
    // Source changes below are independent of placement; correction-only
    // previews reuse the same geometry and Filament entity generation.
    const auto update_source = [&](MeshRenderableProxy &proxy) {
      proxy.source_entity = preview_mesh.source_entity;
    };
    update_source(group.rendered_prototype);
    update_source(group.shaded_prototype);
    update_source(group.overlay_depth_prototype);
    for (MeshRenderableProxy &proxy : group.rendered_instances) {
      update_source(proxy);
    }
    for (MeshRenderableProxy &proxy : group.shaded_instances) {
      update_source(proxy);
    }
    for (MeshRenderableProxy &proxy : group.overlay_depth_instances) {
      update_source(proxy);
    }
  }

  group.source_object_id = preview_mesh.source_object_id;
  group.source_entity = preview_mesh.source_entity;
  group.visibility_entity = preview_mesh.visibility_entity;
  group.geometry_signature = geometry_signature;
  group.material_signature = preview_material_signature(preview_mesh);
  group.layout_key = preview_mesh.layout_key;
  group.position_revision = preview_mesh.position_revision;
  if (!group.topology_payload ||
      group.topology_payload.get() != topology_payload.get()) {
    group.topology_signature = preview_topology_signature(preview_mesh);
  }
  group.topology_payload = topology_payload;
  group.dynamic_positions = preview_mesh.dynamic_positions;
}

void ToolPreviewRenderableCache::publish_generation_subset(
    PreviewGeneration &generation,
    std::vector<std::size_t> candidate_indices) {
  std::vector<bool> desired(generation.groups.size(), false);
  for (const std::size_t index : candidate_indices) {
    if (index < desired.size()) {
      desired[index] = true;
    }
  }

  // Candidate-first visibility swap: show replacements before hiding any
  // previously active group.
  for (const std::size_t index : candidate_indices) {
    if (index < generation.groups.size()) {
      set_group_visible(generation.groups[index], true);
    }
  }
  for (const std::size_t index : generation.active_group_indices) {
    if (index < generation.groups.size() && !desired[index]) {
      set_group_visible(generation.groups[index], false);
    }
  }
  generation.active_group_indices = std::move(candidate_indices);
}

void ToolPreviewRenderableCache::set_group_visible(PreviewGroup &group,
                                                   bool visible) {
  if (!factory_ || group.visible == visible) {
    return;
  }
  const std::uint8_t rendered_mask =
      visible ? kViewportRenderedSceneLayerMask : 0u;
  const std::uint8_t shaded_mask =
      visible ? kViewportShadedSceneLayerMask : 0u;
  const std::uint8_t depth_mask = visible ? kViewportOverlayLayerMask : 0u;
  factory_->update_layer_mask(group.rendered_prototype, rendered_mask);
  for (const MeshRenderableProxy &proxy : group.rendered_instances) {
    factory_->update_layer_mask(proxy, rendered_mask);
  }
  factory_->update_layer_mask(group.shaded_prototype, shaded_mask);
  for (const MeshRenderableProxy &proxy : group.shaded_instances) {
    factory_->update_layer_mask(proxy, shaded_mask);
  }
  factory_->update_layer_mask(group.overlay_depth_prototype, depth_mask);
  for (const MeshRenderableProxy &proxy : group.overlay_depth_instances) {
    factory_->update_layer_mask(proxy, depth_mask);
  }
  group.visible = visible;
}

void ToolPreviewRenderableCache::hide_generation(
    PreviewGeneration &generation) {
  for (const std::size_t index : generation.active_group_indices) {
    if (index < generation.groups.size()) {
      set_group_visible(generation.groups[index], false);
    }
  }
}

void ToolPreviewRenderableCache::update_surface_grid(
    float grid_world_spacing, const ViewportSettings &settings) {
  QDR_PROFILE_FUNCTION();
  if (!factory_) {
    return;
  }
  const std::uint64_t signature =
      mesh_surface_grid_signature(grid_world_spacing, settings);
  if (!material_uniforms_dirty_ &&
      material_uniform_signature_ == signature) {
    return;
  }

  {
    QDR_PROFILE_SCOPE("Tool preview material uniforms apply");
    if (current_generation_) {
      for (const std::size_t index :
           current_generation_->active_group_indices) {
        if (index >= current_generation_->groups.size()) {
          continue;
        }
        PreviewGroup &group = current_generation_->groups[index];
        if (!group.visible ||
            (group.material_uniforms_initialized &&
             group.material_uniform_signature == signature)) {
          continue;
        }
        factory_->update_surface_grid(group.rendered_prototype,
                                      grid_world_spacing, settings);
        factory_->update_surface_grid(group.shaded_prototype,
                                      grid_world_spacing, settings);
        factory_->update_shaded_lighting(group.shaded_prototype, settings);
        for (const MeshRenderableProxy &proxy : group.rendered_instances) {
          factory_->update_surface_grid(proxy, grid_world_spacing, settings);
        }
        for (const MeshRenderableProxy &proxy : group.shaded_instances) {
          factory_->update_surface_grid(proxy, grid_world_spacing, settings);
          factory_->update_shaded_lighting(proxy, settings);
        }
        group.material_uniform_signature = signature;
        group.material_uniforms_initialized = true;
      }
    }
  }
  material_uniform_signature_ = signature;
  material_uniforms_dirty_ = false;
}

void ToolPreviewRenderableCache::detach_generation(
    PreviewGeneration &generation) {
  if (generation.detached) {
    return;
  }
  if (!generation.scene_entities.empty()) {
    factory_->remove_from_scene(
        std::span<const utils::Entity>(generation.scene_entities));
  }
  generation.detached = true;
}

void ToolPreviewRenderableCache::retire_generation(
    std::unique_ptr<PreviewGeneration> generation) {
  if (!generation) {
    return;
  }
  // Retired generations have already been hidden (or were built at layer 0).
  // Keep their prebuilt entity list so retirement needs one Filament call,
  // independent of proxy count; resource destruction remains idle-budgeted.
  detach_generation(*generation);
  retired_generations_.push_back(std::move(generation));
}

bool ToolPreviewRenderableCache::destroy_one_proxy(
    PreviewGeneration &generation) {
  if (!factory_) {
    return false;
  }
  PreviewGroup &group = generation.groups.back();
  const auto record_destroyed_proxy = [&] {
    assert(group.live_proxy_count > 0u);
    --group.live_proxy_count;
  };
  const auto destroy_proxy = [&](MeshRenderableProxy &proxy) {
    if (generation.detached) {
      factory_->destroy_detached(proxy);
    } else {
      factory_->destroy(proxy);
    }
  };
  const auto destroy_back = [&](std::vector<MeshRenderableProxy> &proxies) {
    if (proxies.empty()) {
      return false;
    }
    destroy_proxy(proxies.back());
    proxies.pop_back();
    record_destroyed_proxy();
    return true;
  };
  if (destroy_back(group.overlay_depth_instances) ||
      destroy_back(group.shaded_instances) ||
      destroy_back(group.rendered_instances)) {
    return true;
  }
  if (group.overlay_depth_prototype.entity) {
    destroy_proxy(group.overlay_depth_prototype);
    record_destroyed_proxy();
    return true;
  }
  if (group.shaded_prototype.entity) {
    destroy_proxy(group.shaded_prototype);
    record_destroyed_proxy();
    return true;
  }
  if (group.rendered_prototype.entity) {
    destroy_proxy(group.rendered_prototype);
    record_destroyed_proxy();
    return true;
  }
  return false;
}

void ToolPreviewRenderableCache::drain_retired_destroy_queue(
    std::size_t max_proxy_count) {
  QDR_PROFILE_FUNCTION();
  if (!factory_ || max_proxy_count == 0u) {
    return;
  }
  std::size_t destroyed = 0u;
  while (destroyed < max_proxy_count && !retired_generations_.empty()) {
    PreviewGeneration &generation = *retired_generations_.back();
    while (!generation.groups.empty() &&
           group_proxy_count(generation.groups.back()) == 0u) {
      generation.groups.pop_back();
    }
    if (generation.groups.empty()) {
      retired_generations_.pop_back();
      continue;
    }
    QDR_PROFILE_SCOPE("Tool preview destroy retired proxy");
    ++work_counters_.retired_proxy_destroy_attempts;
    if (destroy_one_proxy(generation)) {
      ++destroyed;
    }
  }
}

void ToolPreviewRenderableCache::destroy_group(PreviewGroup &group,
                                               bool detached) {
  if (!factory_) {
    return;
  }
  const auto destroy_proxy = [&](MeshRenderableProxy &proxy) {
    if (detached) {
      factory_->destroy_detached(proxy);
    } else {
      factory_->destroy(proxy);
    }
  };
  for (MeshRenderableProxy &proxy : group.rendered_instances) {
    destroy_proxy(proxy);
  }
  for (MeshRenderableProxy &proxy : group.shaded_instances) {
    destroy_proxy(proxy);
  }
  for (MeshRenderableProxy &proxy : group.overlay_depth_instances) {
    destroy_proxy(proxy);
  }
  destroy_proxy(group.overlay_depth_prototype);
  destroy_proxy(group.shaded_prototype);
  destroy_proxy(group.rendered_prototype);
  group.live_proxy_count = 0u;
}

void ToolPreviewRenderableCache::clear() {
  const auto destroy_generation = [&](std::unique_ptr<PreviewGeneration> &gen) {
    if (!gen || !factory_) {
      gen.reset();
      return;
    }
    for (PreviewGroup &group : gen->groups) {
      destroy_group(group, gen->detached);
    }
    gen.reset();
  };
  destroy_generation(current_generation_);
  destroy_generation(warm_generation_);
  for (std::unique_ptr<PreviewGeneration> &generation :
       retired_generations_) {
    destroy_generation(generation);
  }
  retired_generations_.clear();
  submitted_meshes_.clear();
  mesh_signature_ = 0u;
  material_uniform_signature_ = 0u;
  material_uniforms_dirty_ = true;
}

std::size_t ToolPreviewRenderableCache::proxy_count() const {
  const auto generation_count = [](const PreviewGeneration *generation) {
    std::size_t count = 0u;
    if (generation) {
      for (const PreviewGroup &group : generation->groups) {
        count += group_proxy_count(group);
      }
    }
    return count;
  };
  std::size_t count = generation_count(current_generation_.get()) +
                      generation_count(warm_generation_.get());
  for (const std::unique_ptr<PreviewGeneration> &generation :
       retired_generations_) {
    count += generation_count(generation.get());
  }
  return count;
}

std::size_t ToolPreviewRenderableCache::group_proxy_count(
    const PreviewGroup &group) {
  return group.live_proxy_count;
}

std::uint64_t ToolPreviewRenderableCache::preview_material_signature(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  RenderSignatureBuilder signature(
      render_material_binding_signature(preview_mesh.material));
  signature.add_value(preview_mesh.material_revision)
      .add_bool(preview_mesh.double_sided)
      .add_value(static_cast<std::uint64_t>(
          preview_mesh.material_reference.kind()));
  if (preview_mesh.material_reference.project_owned()) {
    signature.add_string(preview_mesh.material_reference.project_asset().str());
  } else {
    signature.add_value(preview_mesh.material_reference.embedded_asset());
  }
  return signature.value();
}

std::uint64_t ToolPreviewRenderableCache::preview_geometry_signature(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  RenderSignatureBuilder signature;
  signature.add_value(preview_mesh.mesh.geometry_content_id().value)
      .add_value(preview_mesh.mesh.revision.value);
  if (preview_mesh.dynamic_positions) {
    signature.add_value(preview_mesh.layout_key)
        .add_value(preview_mesh.position_revision);
  }
  return signature.value();
}

std::uint64_t ToolPreviewRenderableCache::preview_topology_signature(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  const MeshGeometryPayload &geometry = preview_mesh.mesh.geometry();
  RenderSignatureBuilder signature;
  signature.add_size(geometry.vertices.size())
      .add_size(geometry.indices.size());
  for (const MeshVertex &vertex : geometry.vertices) {
    signature.add_value(vertex.source_vertex_id);
    if (!preview_mesh.dynamic_positions) {
      signature.add_vec3(vertex.normal)
          .add_vec2(vertex.uv)
          .add_vec3(vertex.uv_geometric_normal)
          .add_value(vertex.rgba);
      for (const float coefficient : vertex.uv_carrier) {
        signature.add_float(coefficient);
      }
    }
  }
  if (!preview_mesh.dynamic_positions) {
    for (const std::uint32_t index : geometry.indices) {
      signature.add_value(index);
    }
  }
  signature.add_size(geometry.primitives.size());
  for (const MeshPrimitiveSpan primitive : geometry.primitives) {
    signature.add_value(primitive.index_offset)
        .add_value(primitive.index_count)
        .add_value(primitive.material_slot);
  }
  return signature.value();
}

std::uint64_t ToolPreviewRenderableCache::preview_structural_reuse_signature(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  return RenderSignatureBuilder{}
      .add_value(preview_topology_signature(preview_mesh))
      .add_value(preview_material_signature(preview_mesh))
      .add_bool(preview_mesh.dynamic_positions)
      .add_size(preview_transform_count(preview_mesh))
      .value();
}

std::uint64_t ToolPreviewRenderableCache::group_structural_reuse_signature(
    const PreviewGroup &group) {
  return RenderSignatureBuilder{}
      .add_value(group.topology_signature)
      .add_value(group.material_signature)
      .add_bool(group.dynamic_positions)
      .add_size(group.transforms.size())
      .value();
}

std::uint64_t
ToolPreviewRenderableCache::preview_exact_reuse_base_signature(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  RenderSignatureBuilder signature;
  signature.add_value(preview_mesh.source_object_id)
      .add_value(preview_mesh.source_entity)
      .add_value(preview_mesh.visibility_entity)
      .add_value(preview_material_signature(preview_mesh))
      .add_bool(preview_mesh.dynamic_positions)
      .add_size(preview_transform_count(preview_mesh));
  if (preview_mesh.dynamic_positions) {
    signature.add_value(preview_topology_signature(preview_mesh));
  } else {
    signature.add_value(preview_geometry_signature(preview_mesh));
  }
  return signature.value();
}

std::uint64_t ToolPreviewRenderableCache::group_exact_reuse_base_signature(
    const PreviewGroup &group) {
  RenderSignatureBuilder signature;
  signature.add_value(group.source_object_id)
      .add_value(group.source_entity)
      .add_value(group.visibility_entity)
      .add_value(group.material_signature)
      .add_bool(group.dynamic_positions)
      .add_size(group.transforms.size());
  if (group.dynamic_positions) {
    signature.add_value(group.topology_signature);
  } else {
    signature.add_value(group.geometry_signature);
  }
  return signature.value();
}

bool ToolPreviewRenderableCache::group_matches_exact(
    const PreviewGroup &group,
    const RenderToolPreviewMeshRecord &preview_mesh) {
  if (group.source_object_id != preview_mesh.source_object_id ||
      group.source_entity != preview_mesh.source_entity ||
      group.visibility_entity != preview_mesh.visibility_entity ||
      group.material_signature != preview_material_signature(preview_mesh) ||
      group.dynamic_positions != preview_mesh.dynamic_positions ||
      group.transforms.size() != preview_transform_count(preview_mesh)) {
    return false;
  }
  if (!preview_mesh.dynamic_positions) {
    if (group.geometry_signature != preview_geometry_signature(preview_mesh)) {
      return false;
    }
    const std::shared_ptr<const MeshGeometryPayload> geometry_payload =
        preview_mesh.mesh.geometry_payload();
    return same_geometry_payload(group.topology_payload, geometry_payload,
                                 true);
  }
  const bool layout_compatible =
      group.layout_key == preview_mesh.layout_key || group.layout_key == 0u ||
      preview_mesh.layout_key == 0u;
  const std::shared_ptr<const MeshGeometryPayload> topology_payload =
      preview_mesh.mesh.geometry_payload();
  if (group.layout_key != 0u &&
      group.layout_key == preview_mesh.layout_key &&
      group.topology_payload.get() == topology_payload.get()) {
    return true;
  }
  return layout_compatible &&
         group.topology_signature == preview_topology_signature(preview_mesh) &&
         group_topology_equals(group, preview_mesh) &&
         group.dynamic_positions;
}

bool ToolPreviewRenderableCache::group_matches_structure(
    const PreviewGroup &group,
    const RenderToolPreviewMeshRecord &preview_mesh) {
  return group.dynamic_positions && preview_mesh.dynamic_positions &&
         group.topology_signature == preview_topology_signature(preview_mesh) &&
         group_topology_equals(group, preview_mesh) &&
         group.material_signature == preview_material_signature(preview_mesh) &&
         group.transforms.size() == preview_transform_count(preview_mesh);
}

bool ToolPreviewRenderableCache::group_topology_equals(
    const PreviewGroup &group,
    const RenderToolPreviewMeshRecord &preview_mesh) {
  if (preview_mesh.dynamic_positions) {
    return same_dynamic_geometry_layout(
        group.topology_payload, preview_mesh.mesh.geometry_payload());
  }
  return same_geometry_payload(group.topology_payload,
                               preview_mesh.mesh.geometry_payload(), false);
}

bool ToolPreviewRenderableCache::group_submission_unchanged(
    const PreviewGroup &group,
    const RenderToolPreviewMeshRecord &preview_mesh) {
  return group.source_object_id == preview_mesh.source_object_id &&
         group.source_entity == preview_mesh.source_entity &&
         group.visibility_entity == preview_mesh.visibility_entity &&
         group.material_signature ==
             preview_material_signature(preview_mesh) &&
         group.geometry_signature ==
             preview_geometry_signature(preview_mesh) &&
         group.layout_key == preview_mesh.layout_key &&
         group.position_revision == preview_mesh.position_revision &&
         group.topology_payload.get() ==
             preview_mesh.mesh.geometry_payload().get() &&
         group.dynamic_positions == preview_mesh.dynamic_positions &&
         preview_transforms_equal(group.transforms, preview_mesh) &&
         preview_texture_mappings_equal(group.sampling_from_local, preview_mesh);
}

bool ToolPreviewRenderableCache::group_dynamic_update_is_valid(
    const PreviewGroup &group,
    const RenderToolPreviewMeshRecord &preview_mesh) const {
  const MeshGeometryPayload &geometry = preview_mesh.mesh.geometry();
  const std::span<const QVec3> positions =
      preview_dynamic_positions(preview_mesh);
  // This mirrors every guard in MeshRenderableFactory::update_dynamic_geometry.
  // Complete generations separately guarantee sibling/vector shape. With the
  // factory confined to the render thread, these guards remain true throughout
  // prepare_generation's following non-failing mutation pass.
  return factory_ != nullptr && factory_->initialized() &&
         group.rendered_prototype.dynamic_geometry &&
         group.rendered_prototype.vertex_buffer != nullptr &&
         group.rendered_prototype.index_buffer != nullptr &&
         group.rendered_prototype.vertex_count == geometry.vertices.size() &&
         group.rendered_prototype.index_count == geometry.indices.size() &&
         (positions.empty() || positions.size() == geometry.vertices.size());
}

bool ToolPreviewRenderableCache::submitted_meshes_equal(
    const std::vector<PreviewSubmissionState> &submitted,
    const std::vector<RenderToolPreviewMeshRecord> &meshes) {
  if (submitted.size() != meshes.size()) {
    return false;
  }
  for (std::size_t index = 0u; index < meshes.size(); ++index) {
    const PreviewSubmissionState &left = submitted[index];
    const RenderToolPreviewMeshRecord &right = meshes[index];
    if (left.source_object_id != right.source_object_id ||
        left.source_entity != right.source_entity ||
        left.visibility_entity != right.visibility_entity ||
        left.material_reference != right.material_reference ||
        left.material_signature != preview_material_signature(right) ||
        left.material_revision != right.material_revision ||
        left.mesh_revision != right.mesh.revision.value ||
        left.dynamic_positions != right.dynamic_positions ||
        left.suppress_source != right.suppress_source ||
        !same_geometry_payload(left.geometry_payload,
                               right.mesh.geometry_payload(), true) ||
        !same_mat4(left.transform, right.transform) ||
        !same_mat4(left.sampling_from_local, right.sampling_from_local) ||
        left.instance_sampling_from_local.size() != right.instance_sampling_from_local.size() ||
        !std::equal(left.instance_sampling_from_local.begin(),
                    left.instance_sampling_from_local.end(),
                    right.instance_sampling_from_local.begin(), same_mat4) ||
        left.instance_transforms.size() !=
            right.instance_transforms.size() ||
        !std::equal(left.instance_transforms.begin(),
                    left.instance_transforms.end(),
                    right.instance_transforms.begin(), same_mat4)) {
      return false;
    }
    if (left.layout_key != right.layout_key ||
        (right.dynamic_positions &&
         left.position_revision != right.position_revision)) {
      return false;
    }
  }
  return true;
}

std::vector<ToolPreviewRenderableCache::PreviewSubmissionState>
ToolPreviewRenderableCache::capture_submission(
    const std::vector<RenderToolPreviewMeshRecord> &meshes) {
  std::vector<PreviewSubmissionState> captured;
  captured.reserve(meshes.size());
  for (const RenderToolPreviewMeshRecord &mesh : meshes) {
    captured.push_back({
        .source_object_id = mesh.source_object_id,
        .source_entity = mesh.source_entity,
        .visibility_entity = mesh.visibility_entity,
        .material_reference = mesh.material_reference,
        .material_signature = preview_material_signature(mesh),
        .material_revision = mesh.material_revision,
        .mesh_revision = mesh.mesh.revision.value,
        .layout_key = mesh.layout_key,
        .position_revision = mesh.position_revision,
        .geometry_payload = mesh.mesh.geometry_payload(),
        .transform = mesh.transform,
        .instance_transforms = mesh.instance_transforms,
        .sampling_from_local = mesh.sampling_from_local,
        .instance_sampling_from_local = mesh.instance_sampling_from_local,
        .dynamic_positions = mesh.dynamic_positions,
        .suppress_source = mesh.suppress_source,
    });
  }
  return captured;
}

bool ToolPreviewRenderableCache::active_material_uniforms_are_current(
    const PreviewGeneration &generation) const {
  for (const std::size_t index : generation.active_group_indices) {
    if (index >= generation.groups.size()) {
      continue;
    }
    const PreviewGroup &group = generation.groups[index];
    if (group.visible &&
        (!group.material_uniforms_initialized ||
         group.material_uniform_signature != material_uniform_signature_)) {
      return false;
    }
  }
  return true;
}

std::uint64_t ToolPreviewRenderableCache::preview_meshes_signature(
    const std::vector<RenderToolPreviewMeshRecord> &preview_meshes) {
  RenderSignatureBuilder signature;
  signature.add_size(preview_meshes.size());
  for (const RenderToolPreviewMeshRecord &preview_mesh : preview_meshes) {
    signature.add_value(preview_mesh.source_object_id)
        .add_value(preview_mesh.source_entity)
        .add_value(preview_mesh.visibility_entity)
        .add_value(preview_material_signature(preview_mesh))
        .add_value(preview_geometry_signature(preview_mesh))
        .add_bool(preview_mesh.dynamic_positions)
        .add_bool(preview_mesh.suppress_source);
    const std::size_t transform_count = preview_transform_count(preview_mesh);
    signature.add_size(transform_count);
    for (std::size_t index = 0u; index < transform_count; ++index) {
      signature.add_mat4(preview_transform(preview_mesh, index));
      signature.add_mat4(preview_texture_mapping(preview_mesh, index));
    }
  }
  return signature.value();
}

} // namespace quader
