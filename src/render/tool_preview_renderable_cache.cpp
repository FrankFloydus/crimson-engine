////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/tool_preview_renderable_cache.hpp"

#include "diagnostics/profile.hpp"
#include "render/material_binding_service.hpp"
#include "render/texture_binding_service.hpp"
#include "render/light_render_policy.hpp"
#include "render/render_layers.hpp"
#include "render/render_signature_builder.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <numeric>
#include <span>
#include <thread>
#include <unordered_map>
#include <utility>

#include <filament/LightManager.h>
#include <utils/EntityManager.h>

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
             : preview_mesh.geometry_projection && preview_mesh.geometry_projection->positions
             ? std::span<const QVec3>(*preview_mesh.geometry_projection->positions)
             : std::span<const QVec3>{};
}

std::shared_ptr<const MeshGeometryPayload> preview_geometry_owner(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  return preview_mesh.geometry_projection ? preview_mesh.geometry_projection->source_geometry
                                           : preview_mesh.mesh.geometry_payload();
}

std::shared_ptr<const std::vector<QVec3>> preview_position_owner(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  return preview_mesh.dynamic_position_payload ? preview_mesh.dynamic_position_payload
      : preview_mesh.geometry_projection ? preview_mesh.geometry_projection->positions : nullptr;
}

RenderMeshAssetSnapshot preview_mesh_snapshot(const RenderToolPreviewMeshRecord &preview_mesh) {
  return render_mesh_asset_snapshot(kInvalidAssetId, preview_mesh.mesh, preview_mesh.geometry_projection);
}

/// Pins each CPU job's immutable geometry and producer-owned position stream
/// until every worker has joined and the render thread consumes its result.
struct PreparedPreviewGeometry final {
  std::size_t submission_index = 0u;
  std::size_t canonical_index = 0u;
  RenderMeshAssetSnapshot mesh;
  std::shared_ptr<const std::vector<QVec3>> position_owner;
  RenderInstanceBounds bounds;
  std::shared_ptr<MeshRenderableFactory::DynamicGeometryUpdate> upload;
};

bool same_vec3(QVec3 left, QVec3 right);
bool same_geometry_payload(
    const std::shared_ptr<const MeshGeometryPayload> &left,
    const std::shared_ptr<const MeshGeometryPayload> &right,
    bool include_positions);
bool same_geometry_streams(
    const std::shared_ptr<const MeshGeometryPayload> &left,
    const std::shared_ptr<const MeshGeometryProjection> &left_projection,
    const std::shared_ptr<const MeshGeometryPayload> &right,
    const std::shared_ptr<const MeshGeometryProjection> &right_projection,
    bool layout_only, bool include_positions);

bool prepare_preview_geometry(std::vector<PreparedPreviewGeometry> &jobs,
                              bool &parallel, std::size_t &canonical_count) {
  QDR_PROFILE_SCOPE("Tool preview prepare dynamic geometry uploads");
  std::vector<std::size_t> canonical_indices;
  {
    QDR_PROFILE_SCOPE("Tool preview group identical dynamic inputs");
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> candidates;
    std::unordered_map<const std::vector<QVec3> *, std::uint64_t> position_signatures;
    canonical_indices.reserve(jobs.size());
    candidates.reserve(jobs.size());
    position_signatures.reserve(jobs.size());
    for (std::size_t index = 0u; index < jobs.size(); ++index) {
      auto &job = jobs[index];
      RenderSignatureBuilder signature;
      signature.add_value(job.mesh.geometry_content_id.value)
          .add_value(reinterpret_cast<std::uintptr_t>(job.mesh.geometry_projection.get()))
          .add_bool(job.position_owner != nullptr);
      if (job.position_owner) {
        auto [found, inserted] = position_signatures.try_emplace(job.position_owner.get(), 0u);
        if (inserted) {
          RenderSignatureBuilder positions;
          positions.add_size(job.position_owner->size());
          for (const QVec3 position : *job.position_owner) positions.add_vec3(position);
          found->second = positions.value();
        }
        signature.add_value(found->second);
      }
      auto &bucket = candidates[signature.value()];
      job.canonical_index = index;
      for (const std::size_t candidate_index : bucket) {
        const auto &candidate = jobs[candidate_index];
        if (!job.mesh.geometry_payload ||
            !same_geometry_streams(job.mesh.geometry_payload, job.mesh.geometry_projection,
                                   candidate.mesh.geometry_payload, candidate.mesh.geometry_projection,
                                   false, true)) continue;
        const bool same_positions = job.position_owner == candidate.position_owner ||
            (job.position_owner && candidate.position_owner &&
             job.position_owner->size() == candidate.position_owner->size() &&
             std::equal(job.position_owner->begin(), job.position_owner->end(),
                        candidate.position_owner->begin(), same_vec3));
        if (same_positions) {
          job.canonical_index = candidate_index;
          break;
        }
      }
      if (job.canonical_index == index) {
        canonical_indices.push_back(index);
        bucket.push_back(index);
      }
    }
  }
  canonical_count = canonical_indices.size();
  constexpr std::size_t kParallelThreshold = 32u;
  constexpr std::size_t kMaximumWorkers = 8u;
  const std::size_t worker_count = canonical_count < kParallelThreshold ? 1u :
      std::min(canonical_count, std::clamp<std::size_t>(
          std::thread::hardware_concurrency(), 1u, kMaximumWorkers));
  parallel = worker_count > 1u;
  const auto prepare = [&](std::size_t index) {
    PreparedPreviewGeometry &job = jobs[index];
    const std::span<const QVec3> positions = job.position_owner
        ? std::span<const QVec3>(*job.position_owner) : std::span<const QVec3>{};
    job.upload = MeshRenderableFactory::prepare_dynamic_geometry_update(job.mesh, positions);
    job.bounds = positions.empty()
        ? MeshRenderableFactory::compute_local_bounds(job.mesh)
        : MeshRenderableFactory::compute_local_bounds(positions);
  };
  try {
    if (!parallel) {
      for (const std::size_t index : canonical_indices) prepare(index);
    } else {
      std::atomic<std::size_t> next_job{0u};
      std::vector<std::jthread> workers;
      workers.reserve(worker_count);
      for (std::size_t worker = 0u; worker < worker_count; ++worker) {
        workers.emplace_back([&] {
          for (;;) {
            const std::size_t next = next_job.fetch_add(1u, std::memory_order_relaxed);
            if (next >= canonical_indices.size()) return;
            const std::size_t index = canonical_indices[next];
            try {
              prepare(index);
            } catch (...) {
              jobs[index].upload.reset();
            }
          }
        });
      }
      // jthread destruction joins before the immutable results are inspected.
    }
    {
      QDR_PROFILE_SCOPE("Tool preview reuse prepared dynamic uploads");
      for (std::size_t index = 0u; index < jobs.size(); ++index) {
        auto &job = jobs[index];
        if (job.canonical_index == index) continue;
        const auto &canonical = jobs[job.canonical_index];
        if (!canonical.upload) return false;
        job.upload = MeshRenderableFactory::prepare_dynamic_geometry_update(
            job.mesh, job.position_owner ? std::span<const QVec3>(*job.position_owner)
                                         : std::span<const QVec3>{}, canonical.upload);
        job.bounds = canonical.bounds;
      }
    }
  } catch (...) {
    return false;
  }
  return std::ranges::all_of(jobs, [](const auto &job) { return job.upload != nullptr; });
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

bool preview_uses_explicit_instances(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  return !preview_mesh.dynamic_positions && preview_transform_count(preview_mesh) > 1u;
}

bool same_preview_material_payload(const RenderMaterialBindingSnapshot &left,
                                   const RenderMaterialBindingSnapshot &right) {
  if (left.present != right.present || left.component != right.component ||
      left.material_slots.size() != right.material_slots.size() ||
      !render_material_payloads_equal(&left.primary_material, &right.primary_material)) {
    return false;
  }
  for (std::size_t index = 0u; index < left.material_slots.size(); ++index) {
    if (left.material_slots[index].first != right.material_slots[index].first ||
        !render_material_payloads_equal(&left.material_slots[index].second,
                                       &right.material_slots[index].second)) return false;
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
        !std::equal(a.uv_carrier.begin(), a.uv_carrier.end(),
                    b.uv_carrier.begin(), same_float_bits) ||
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

bool same_geometry_streams(
    const std::shared_ptr<const MeshGeometryPayload> &left,
    const std::shared_ptr<const MeshGeometryProjection> &left_projection,
    const std::shared_ptr<const MeshGeometryPayload> &right,
    const std::shared_ptr<const MeshGeometryProjection> &right_projection,
    bool layout_only, bool include_positions) {
  if (!left_projection && !right_projection) {
    return layout_only ? same_dynamic_geometry_layout(left, right)
                       : same_geometry_payload(left, right, include_positions);
  }
  if (left == right && left_projection == right_projection) return true;
  if (!left || !right) return false;
  const auto left_vertices = left_projection ? left_projection->vertices() : std::span<const MeshVertex>(left->vertices);
  const auto right_vertices = right_projection ? right_projection->vertices() : std::span<const MeshVertex>(right->vertices);
  const auto left_indices = left_projection ? left_projection->indices() : std::span<const std::uint32_t>(left->indices);
  const auto right_indices = right_projection ? right_projection->indices() : std::span<const std::uint32_t>(right->indices);
  if (left_vertices.size() != right_vertices.size() || left_indices.size() != right_indices.size() ||
      left->primitives != right->primitives ||
      (!layout_only && !std::equal(left_indices.begin(), left_indices.end(), right_indices.begin()))) return false;
  for (std::size_t index = 0u; index < left_vertices.size(); ++index) {
    const auto &a = left_vertices[index];
    const auto &b = right_vertices[index];
    if (a.source_vertex_id != b.source_vertex_id) return false;
    if (layout_only) continue;
    if (include_positions) {
      const QVec3 left_position = left_projection && left_projection->positions && index < left_projection->positions->size()
          ? (*left_projection->positions)[index] : a.position;
      const QVec3 right_position = right_projection && right_projection->positions && index < right_projection->positions->size()
          ? (*right_projection->positions)[index] : b.position;
      if (!same_vec3(left_position, right_position)) return false;
    }
    if (!same_vec3(a.normal, b.normal) || !same_float_bits(a.uv.x, b.uv.x) ||
        !same_float_bits(a.uv.y, b.uv.y) || a.rgba != b.rgba ||
        !std::equal(a.uv_carrier.begin(), a.uv_carrier.end(), b.uv_carrier.begin(), same_float_bits) ||
        !same_vec3(a.uv_geometric_normal, b.uv_geometric_normal)) return false;
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

ToolPreviewInstancePlan plan_tool_preview_instances(
    std::size_t active_count, std::size_t maximum_chunk_capacity,
    std::span<const std::size_t> retained_capacities) {
  ToolPreviewInstancePlan plan;
  plan.active_count = active_count;
  maximum_chunk_capacity = std::max<std::size_t>(1u,
                                                 maximum_chunk_capacity);
  std::size_t transform_offset = 0u;
  std::size_t remaining = active_count;
  plan.chunks.reserve(retained_capacities.size() +
                      active_count / maximum_chunk_capacity + 1u);
  for (const std::size_t retained_capacity : retained_capacities) {
    if (retained_capacity == 0u) {
      continue;
    }
    const std::size_t chunk_active = std::min(remaining,
                                              retained_capacity);
    plan.chunks.push_back({
        .transform_offset = transform_offset,
        .active_count = chunk_active,
        .capacity = retained_capacity,
        .retained = true,
    });
    transform_offset += chunk_active;
    plan.allocated_capacity += retained_capacity;
    remaining -= chunk_active;
  }
  while (remaining > 0u) {
    const std::size_t chunk_active = std::min(remaining,
                                              maximum_chunk_capacity);
    plan.chunks.push_back({
        .transform_offset = transform_offset,
        .active_count = chunk_active,
        .capacity = maximum_chunk_capacity,
        .retained = false,
    });
    transform_offset += chunk_active;
    plan.allocated_capacity += maximum_chunk_capacity;
    remaining -= chunk_active;
    ++plan.new_chunk_count;
  }
  return plan;
}

ToolPreviewLightPoolPlan plan_tool_preview_light_pools(
    std::span<const std::size_t> requested_counts,
    std::span<const std::size_t> retained_capacities) {
  ToolPreviewLightPoolPlan plan;
  plan.requested_counts.assign(requested_counts.begin(),
                               requested_counts.end());
  plan.retained_capacities.resize(requested_counts.size(), 0u);
  for (std::size_t index = 0u; index < requested_counts.size(); ++index) {
    const std::size_t retained = index < retained_capacities.size()
                                     ? retained_capacities[index]
                                     : 0u;
    plan.retained_capacities[index] =
        std::max(retained, requested_counts[index]);
    plan.new_proxy_count += requested_counts[index] > retained
                                ? requested_counts[index] - retained
                                : 0u;
  }
  return plan;
}

ToolPreviewRenderableCache::~ToolPreviewRenderableCache() {
  shutdown();
}

std::vector<RenderToolPreviewMeshRecord>
ToolPreviewRenderableCache::build_dynamic_families(
    const std::vector<RenderToolPreviewMeshRecord> &meshes) const {
  QDR_PROFILE_SCOPE("Tool preview build dynamic GPU families");
  std::vector<RenderToolPreviewMeshRecord> families;
  std::unordered_map<std::uint64_t, std::vector<std::size_t>> buckets;
  families.reserve(meshes.size());
  buckets.reserve(meshes.size());
  for (const auto &mesh : meshes) {
    const auto positions = preview_dynamic_positions(mesh);
    if (!positions.empty() && positions.size() != preview_mesh_snapshot(mesh).vertices.size()) return {};
    RenderSignatureBuilder signature;
    signature.add_value(mesh.geometry_projection
        ? reinterpret_cast<std::uintptr_t>(mesh.geometry_projection.get()) : mesh.mesh.geometry_content_id().value)
        .add_value(preview_material_signature(mesh)).add_bool(!positions.empty());
    for (const QVec3 position : positions) signature.add_vec3(position);
    auto &bucket = buckets[signature.value()];
    std::size_t family_index = families.size();
    for (const std::size_t candidate_index : bucket) {
      const auto &candidate = families[candidate_index];
      const auto candidate_positions = preview_dynamic_positions(candidate);
      if (mesh.double_sided != candidate.double_sided ||
          mesh.cast_shadows != candidate.cast_shadows ||
          mesh.material_reference != candidate.material_reference ||
          !same_geometry_streams(preview_geometry_owner(mesh), mesh.geometry_projection,
                                preview_geometry_owner(candidate), candidate.geometry_projection, false, true) ||
          positions.size() != candidate_positions.size() ||
          !std::equal(positions.begin(), positions.end(), candidate_positions.begin(), same_vec3) ||
          !same_preview_material_payload(mesh.material, candidate.material)) continue;
      family_index = candidate_index;
      break;
    }
    if (family_index == families.size()) {
      families.push_back(mesh);
      families.back().instance_transforms.clear();
      families.back().instance_sampling_from_local.clear();
      // Family slots use exact layout comparison; producer tokens remain local
      // to logical records and cannot identify shared mutable GPU storage.
      families.back().layout_key = 0u;
      bucket.push_back(family_index);
    }
    auto &family = families[family_index];
    for (std::size_t index = 0u; index < preview_transform_count(mesh); ++index) {
      family.instance_transforms.push_back(preview_transform(mesh, index));
      family.instance_sampling_from_local.push_back(preview_texture_mapping(mesh, index));
    }
  }
  return families;
}

void ToolPreviewRenderableCache::initialize(
    MeshRenderableFactory &factory, MaterialBindingService &material_binding,
    std::size_t maximum_instances_per_chunk) {
  shutdown();
  factory_ = &factory;
  material_binding_ = &material_binding;
  maximum_instances_per_chunk_ = std::clamp(
      maximum_instances_per_chunk, std::size_t{1u},
      kMeshTextureMappingCapacity);
}

void ToolPreviewRenderableCache::shutdown() {
  if (!factory_) {
    return;
  }
  clear();
  factory_ = nullptr;
  material_binding_ = nullptr;
  maximum_instances_per_chunk_ = 1u;
}

ToolPreviewSubmissionResult ToolPreviewRenderableCache::set_meshes(
    const std::vector<RenderToolPreviewMeshRecord> &source_meshes) {
  QDR_PROFILE_FUNCTION();
  if (!factory_ || !factory_->initialized() || !material_binding_) {
    return ToolPreviewSubmissionResult::Failed;
  }

  const std::uint64_t next_signature = preview_meshes_signature(source_meshes);
  // The aggregate hash is only a cheap rejection gate. Exact state, with
  // pointer-first geometry checks, prevents a collision from suppressing a
  // real submission while keeping repeated dense ticks O(group count).
  if (next_signature == mesh_signature_ &&
      submitted_meshes_equal(submitted_meshes_, source_meshes)) {
    return ToolPreviewSubmissionResult::Unchanged;
  }

  try {
    retired_generations_.reserve(retired_generations_.size() + 3u);
  } catch (...) {
    return ToolPreviewSubmissionResult::Failed;
  }

  if (source_meshes.empty()) {
    QDR_PROFILE_SCOPE("Tool preview retain current and warm generations");
    if (current_generation_) {
      hide_generation(*current_generation_);
      if (current_generation_->dynamic_families) retire_generation(std::move(current_generation_));
    }
    if (warm_generation_ && warm_generation_->dynamic_families) retire_generation(std::move(warm_generation_));
    mesh_signature_ = next_signature;
    submitted_meshes_ = capture_submission(source_meshes);
    work_counters_.active_dynamic_geometry_families = 0u;
    work_counters_.active_dynamic_geometry_participants = 0u;
    work_counters_.ordinary_static_records = 0u;
    work_counters_.ordinary_unknown_opacity_records = 0u;
    work_counters_.ordinary_transparent_records = 0u;
    work_counters_.ordinary_reflected_records = 0u;
    work_counters_.inactive_dynamic_geometry_families = 0u;
    QDR_PROFILE_PLOT("Quader preview dynamic GPU families", std::int64_t{0});
    QDR_PROFILE_PLOT("Quader preview dynamic logical participants", std::int64_t{0});
    QDR_PROFILE_PLOT("Quader retired preview proxies", static_cast<std::int64_t>(work_counters_.retired_proxy_count));
    return ToolPreviewSubmissionResult::Published;
  }

  std::vector<RenderToolPreviewMeshRecord> eligible;
  std::vector<RenderToolPreviewMeshRecord> ordinary;
  std::vector<RenderToolPreviewMeshRecord> families;
  std::vector<PreviewSubmissionState> submission;
  ToolPreviewCacheWorkCounters fallback_counts;
  std::size_t dynamic_family_count = 0u;
  try {
    submission = capture_submission(source_meshes);
    eligible.reserve(source_meshes.size());
    ordinary.reserve(source_meshes.size());
    for (const auto &mesh : source_meshes) {
      if (!mesh.renderable()) {
        if (mesh.suppress_source) return ToolPreviewSubmissionResult::Failed;
        continue;
      }
      const auto snapshot = preview_mesh_snapshot(mesh);
      const auto positions = preview_dynamic_positions(mesh);
      if ((preview_position_owner(mesh) && positions.size() != snapshot.vertices.size()) ||
          (mesh.geometry_projection && (!mesh.geometry_projection->positions ||
           mesh.geometry_projection->positions->size() != snapshot.vertices.size()))) {
        return ToolPreviewSubmissionResult::Failed;
      }
      bool family_eligible = mesh.dynamic_positions;
      if (!family_eligible) {
        ++fallback_counts.ordinary_static_records;
      } else {
        // Only drawn slots matter. An unused alpha assignment cannot change
        // the depth sorting requirements of this record's actual primitives.
        MaterialOpacity opacity = MaterialOpacity::Opaque;
        for (const auto primitive : coalesced_render_mesh_primitive_spans(snapshot)) {
          const auto used_opacity = material_binding_->material_opacity(
              mesh.material.material_for_slot(primitive.material_slot));
          if (used_opacity == MaterialOpacity::Transparent) {
            opacity = used_opacity;
            break;
          }
          if (used_opacity == MaterialOpacity::Unknown) opacity = used_opacity;
        }
        if (opacity != MaterialOpacity::Opaque) {
          family_eligible = false;
          if (opacity == MaterialOpacity::Transparent) ++fallback_counts.ordinary_transparent_records;
          else ++fallback_counts.ordinary_unknown_opacity_records;
        } else {
          // Instance matrices do not control Filament's entity winding.
          for (std::size_t index = 0u; index < preview_transform_count(mesh); ++index) {
            const auto &transform = preview_transform(mesh, index);
            const QVec3 first{transform.values[0], transform.values[1], transform.values[2]};
            const QVec3 second{transform.values[4], transform.values[5], transform.values[6]};
            const QVec3 third{transform.values[8], transform.values[9], transform.values[10]};
            if (!(dot(cross(first, second), third) > 0.0f)) {
              family_eligible = false;
              ++fallback_counts.ordinary_reflected_records;
              break;
            }
          }
        }
      }
      (family_eligible ? eligible : ordinary).push_back(mesh);
    }
    families = build_dynamic_families(eligible);
    if (!eligible.empty() && families.empty()) return ToolPreviewSubmissionResult::Failed;
    dynamic_family_count = families.size();
    families.insert(families.end(), ordinary.begin(), ordinary.end());
  } catch (...) {
    return ToolPreviewSubmissionResult::Failed;
  }
  const bool dynamic_families = dynamic_family_count != 0u;
  const auto &meshes = dynamic_families ? families : source_meshes;
  const auto publish_submission = [&] {
    submitted_meshes_ = std::move(submission);
    work_counters_.active_dynamic_geometry_families = dynamic_family_count;
    work_counters_.active_dynamic_geometry_participants = eligible.size();
    work_counters_.ordinary_static_records = fallback_counts.ordinary_static_records;
    work_counters_.ordinary_unknown_opacity_records = fallback_counts.ordinary_unknown_opacity_records;
    work_counters_.ordinary_transparent_records = fallback_counts.ordinary_transparent_records;
    work_counters_.ordinary_reflected_records = fallback_counts.ordinary_reflected_records;
    QDR_PROFILE_PLOT("Quader preview ordinary unknown opacity", static_cast<std::int64_t>(fallback_counts.ordinary_unknown_opacity_records));
    QDR_PROFILE_PLOT("Quader preview ordinary alpha", static_cast<std::int64_t>(fallback_counts.ordinary_transparent_records));
    QDR_PROFILE_PLOT("Quader preview ordinary reflected", static_cast<std::int64_t>(fallback_counts.ordinary_reflected_records));
    QDR_PROFILE_PLOT("Quader preview ordinary static", static_cast<std::int64_t>(fallback_counts.ordinary_static_records));
    QDR_PROFILE_PLOT("Quader preview dynamic GPU families", static_cast<std::int64_t>(work_counters_.active_dynamic_geometry_families));
    QDR_PROFILE_PLOT("Quader preview dynamic logical participants", static_cast<std::int64_t>(work_counters_.active_dynamic_geometry_participants));
    QDR_PROFILE_PLOT("Quader retired preview proxies", static_cast<std::int64_t>(work_counters_.retired_proxy_count));
  };

  std::vector<std::size_t> candidate_indices;
  try {
    candidate_indices.reserve(meshes.size());
  } catch (...) {
    return ToolPreviewSubmissionResult::Failed;
  }
  bool preparation_failed = false;
  if (current_generation_ && current_generation_->dynamic_families == dynamic_families) {
    QDR_PROFILE_SCOPE("Tool preview match current generation");
    const bool prepared = dynamic_families
        ? prepare_dynamic_family_generation(*current_generation_, meshes, candidate_indices, dynamic_family_count)
        : prepare_generation(*current_generation_, meshes, candidate_indices, false, preparation_failed);
    if (prepared) {
      publish_generation_subset(*current_generation_,
                                std::move(candidate_indices));
      mesh_signature_ = next_signature;
      publish_submission();
      material_uniforms_dirty_ =
          !active_material_uniforms_are_current(*current_generation_);
      return ToolPreviewSubmissionResult::Published;
    }
    if (preparation_failed || dynamic_families) return ToolPreviewSubmissionResult::Failed;
  }

  if (warm_generation_ && warm_generation_->dynamic_families == dynamic_families) {
    QDR_PROFILE_SCOPE("Tool preview match warm generation");
    const bool prepared = dynamic_families
        ? prepare_dynamic_family_generation(*warm_generation_, meshes, candidate_indices, dynamic_family_count)
        : prepare_generation(*warm_generation_, meshes, candidate_indices, true, preparation_failed);
    if (prepared) {
      // The candidate is made visible first. No frame can observe a source
      // hidden without either the old or new preview surface present.
      publish_generation_subset(*warm_generation_,
                                std::move(candidate_indices));
      if (current_generation_) {
        hide_generation(*current_generation_);
      }
      current_generation_.swap(warm_generation_);
      mesh_signature_ = next_signature;
      publish_submission();
      material_uniforms_dirty_ =
          !active_material_uniforms_are_current(*current_generation_);
      return ToolPreviewSubmissionResult::Published;
    }
    if (preparation_failed || dynamic_families) return ToolPreviewSubmissionResult::Failed;
  }

  std::unique_ptr<PreviewGeneration> candidate;
  bool candidate_complete = false;
  {
    QDR_PROFILE_SCOPE("Tool preview build hidden candidate generation");
    try {
      candidate = build_generation(meshes, candidate_complete, dynamic_family_count);
    } catch (...) {
      return ToolPreviewSubmissionResult::Failed;
    }
    if (candidate) candidate->dynamic_families = dynamic_families;
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
  if (current_generation_ && current_generation_->dynamic_families) {
    retire_generation(std::move(current_generation_));
  }
  warm_generation_ = std::move(current_generation_);
  current_generation_ = std::move(candidate);
  mesh_signature_ = next_signature;
  publish_submission();
  if (dynamic_families) work_counters_.dynamic_family_group_creations += dynamic_family_count;
  material_uniforms_dirty_ =
      !active_material_uniforms_are_current(*current_generation_);
  return ToolPreviewSubmissionResult::Published;
}

std::unique_ptr<ToolPreviewRenderableCache::PreviewGeneration>
ToolPreviewRenderableCache::build_generation(
    const std::vector<RenderToolPreviewMeshRecord> &meshes, bool &complete,
    std::size_t dynamic_family_count) {
  complete = false;
  auto generation = std::make_unique<PreviewGeneration>();
  generation->groups.reserve(meshes.size());
  generation->scene_entities.reserve(meshes.size() * 3u);

  const auto release_proxy = [&](MeshRenderableProxy *proxy) { factory_->destroy(*proxy); };
  const auto release_group = [&](PreviewGroup *group) { destroy_group(*group, false); };
  try {
  const auto retain_entity = [&](const MeshRenderableProxy &proxy) {
    if (proxy.entity) {
      generation->scene_entities.push_back(proxy.entity);
    }
  };

  for (std::size_t mesh_index = 0u; mesh_index < meshes.size(); ++mesh_index) {
    const auto &preview_mesh = meshes[mesh_index];
    const bool gpu_family = mesh_index < dynamic_family_count;
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
        preview_mesh_snapshot(preview_mesh);
    const std::span<const QVec3> positions =
        preview_dynamic_positions(preview_mesh);
    const RenderInstanceBounds local_bounds =
        preview_mesh.dynamic_positions
            ? (positions.empty()
                   ? MeshRenderableFactory::compute_local_bounds(mesh_snapshot)
                   : MeshRenderableFactory::compute_local_bounds(positions))
            : MeshRenderableFactory::compute_local_bounds(mesh_snapshot);
    const RenderInstanceBounds *bounds_override =
        preview_mesh.dynamic_positions ? &local_bounds : nullptr;

    const auto make_group = [&](bool explicit_instances,
                                MeshRenderableProxy geometry_owner) {
      PreviewGroup group{.rendered_prototype = std::move(geometry_owner)};
      std::unique_ptr<PreviewGroup, decltype(release_group)> guard(&group, release_group);
      group.source_object_id = preview_mesh.source_object_id;
      group.source_entity = preview_mesh.source_entity;
      group.visibility_entity = preview_mesh.visibility_entity;
      group.geometry_signature = preview_geometry_signature(preview_mesh);
      group.topology_signature = preview_topology_signature(preview_mesh);
      group.material_signature = preview_material_signature(preview_mesh);
      group.layout_key = preview_mesh.layout_key;
      group.position_revision = preview_mesh.position_revision;
      group.position_payload = preview_position_owner(preview_mesh);
      group.topology_payload = preview_geometry_owner(preview_mesh);
      group.geometry_projection = preview_mesh.geometry_projection;
      group.material_payload = preview_mesh.material;
      group.dynamic_positions = preview_mesh.dynamic_positions;
      group.gpu_family = gpu_family;
      group.explicit_instances = explicit_instances;
      guard.release();
      return group;
    };

    if (gpu_family || preview_uses_explicit_instances(preview_mesh)) {
      QDR_PROFILE_SCOPE("Tool preview build explicit instance chunks");
      auto owner = preview_mesh.dynamic_positions
          ? factory_->create_dynamic_geometry(mesh_snapshot, positions) : MeshRenderableProxy{};
      std::unique_ptr<MeshRenderableProxy, decltype(release_proxy)> owner_guard(&owner, release_proxy);
      if (preview_mesh.dynamic_positions &&
          (owner.vertex_buffer == nullptr || owner.index_buffer == nullptr)) {
        return generation;
      }
      PreviewGroup group = make_group(true, std::move(owner));
      std::unique_ptr<PreviewGroup, decltype(release_group)> group_guard(&group, release_group);
      const auto *geometry_source = preview_mesh.dynamic_positions ? &group.rendered_prototype : nullptr;
      ToolPreviewInstancePlan plan = plan_tool_preview_instances(
          transforms.size(), maximum_instances_per_chunk_);
      if (gpu_family) {
        // Dynamic slots match exact occurrence counts, so unused capacity
        // cannot serve a later count change and need not own local matrices.
        for (auto &chunk : plan.chunks) chunk.capacity = chunk.active_count;
        plan.allocated_capacity = transforms.size();
      }
      group.explicit_instance_capacities.reserve(plan.chunks.size());
      for (const ToolPreviewInstanceChunkPlan &chunk : plan.chunks) {
        group.explicit_instance_capacities.push_back(chunk.capacity);
      }
      const std::vector<MeshPrimitiveSpan> primitives =
          coalesced_render_mesh_primitive_spans(mesh_snapshot);
      if (primitives.empty() || plan.chunks.empty()) {
        generation->groups.push_back(std::move(group));
        return generation;
      }
      group.explicit_instance_chunks.reserve(primitives.size() *
                                             plan.chunks.size());
      for (const MeshPrimitiveSpan primitive : primitives) {
        for (const ToolPreviewInstanceChunkPlan &planned : plan.chunks) {
          if (planned.active_count == 0u ||
              planned.transform_offset + planned.active_count >
                  transforms.size()) {
            generation->groups.push_back(std::move(group));
            return generation;
          }
          const std::span<const QMat4> active_transforms{transforms.data() +
                                                            planned.transform_offset,
                                                        planned.active_count};
          const std::span<const QMat4> active_mappings{mappings.data() +
                                                          planned.transform_offset,
                                                      planned.active_count};
          const RenderInstanceBounds world_bounds =
              render_instance_world_bounds(local_bounds, active_transforms);
          MeshRenderableProxy rendered = factory_->create_explicit_instances(
              mesh_snapshot, &preview_mesh.material, primitive,
              active_transforms, world_bounds, preview_mesh.cast_shadows, true,
              preview_mesh.double_sided, 0u, false, false,
              kViewportDefaultRenderableChannel, 0u, planned.capacity, geometry_source);
          std::unique_ptr<MeshRenderableProxy, decltype(release_proxy)> rendered_guard(&rendered, release_proxy);
          if (!rendered.entity) {
            generation->groups.push_back(std::move(group));
            return generation;
          }
          rendered.source_entity = preview_mesh.source_entity;
          retain_entity(rendered);
          factory_->update_texture_mappings(rendered, active_mappings);
          ++group.live_proxy_count;

          MeshRenderableProxy shaded = factory_->create_explicit_instances(
              mesh_snapshot, &preview_mesh.material, primitive,
              active_transforms, world_bounds, false, false,
              preview_mesh.double_sided, 0u, true, false,
              kViewportDefaultRenderableChannel, 0u, planned.capacity, geometry_source);
          std::unique_ptr<MeshRenderableProxy, decltype(release_proxy)> shaded_guard(&shaded, release_proxy);
          if (!shaded.entity) {
            group.explicit_instance_chunks.push_back(
                PreviewGroup::ExplicitInstanceChunk{
                    .primitive = primitive,
                    .transform_offset = planned.transform_offset,
                    .active_count = planned.active_count,
                    .capacity = planned.capacity,
                    .rendered = std::move(rendered),
                });
            generation->groups.push_back(std::move(group));
            return generation;
          }
          shaded.source_entity = preview_mesh.source_entity;
          retain_entity(shaded);
          factory_->update_texture_mappings(shaded, active_mappings);
          ++group.live_proxy_count;

          MeshRenderableProxy overlay_depth = factory_->create_explicit_instances(
              mesh_snapshot, &preview_mesh.material, primitive,
              active_transforms, world_bounds, false, false,
              preview_mesh.double_sided, 0u, true, true,
              kViewportOverlayDepthChannel, 0u, planned.capacity, geometry_source);
          std::unique_ptr<MeshRenderableProxy, decltype(release_proxy)> overlay_depth_guard(&overlay_depth, release_proxy);
          if (!overlay_depth.entity) {
            group.explicit_instance_chunks.push_back(
                PreviewGroup::ExplicitInstanceChunk{
                    .primitive = primitive,
                    .transform_offset = planned.transform_offset,
                    .active_count = planned.active_count,
                    .capacity = planned.capacity,
                    .rendered = std::move(rendered),
                    .shaded = std::move(shaded),
                });
            generation->groups.push_back(std::move(group));
            return generation;
          }
          overlay_depth.source_entity = preview_mesh.source_entity;
          retain_entity(overlay_depth);
          factory_->update_texture_mappings(overlay_depth, active_mappings);
          ++group.live_proxy_count;
          group.explicit_instance_chunks.push_back(
              PreviewGroup::ExplicitInstanceChunk{
                  .primitive = primitive,
                  .transform_offset = planned.transform_offset,
                  .active_count = planned.active_count,
                  .capacity = planned.capacity,
                  .rendered = std::move(rendered),
                  .shaded = std::move(shaded),
                  .overlay_depth = std::move(overlay_depth),
              });
          ++work_counters_.explicit_instance_chunk_creations;
        }
      }
      group.transforms = std::move(transforms);
      group.sampling_from_local = std::move(mappings);
      generation->groups.push_back(std::move(group));
      continue;
    }

    MeshRenderableProxy rendered_prototype = factory_->create(
        preview_mesh.source_entity, mesh_snapshot, &preview_mesh.material,
        transforms.front(), preview_mesh.cast_shadows,
        preview_mesh.double_sided, 0u, false, false,
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
        true, false, kViewportDefaultRenderableChannel, bounds_override);
    const auto make_standard_group =
        [&](MeshRenderableProxy rendered, MeshRenderableProxy shaded,
            MeshRenderableProxy overlay_depth) {
          const std::size_t live_proxy_count =
              static_cast<std::size_t>(rendered.entity ? 1u : 0u) +
              static_cast<std::size_t>(shaded.entity ? 1u : 0u) +
              static_cast<std::size_t>(overlay_depth.entity ? 1u : 0u);
          return PreviewGroup{
              .source_object_id = preview_mesh.source_object_id,
              .source_entity = preview_mesh.source_entity,
              .visibility_entity = preview_mesh.visibility_entity,
              .geometry_signature = preview_geometry_signature(preview_mesh),
              .topology_signature = preview_topology_signature(preview_mesh),
              .material_signature = preview_material_signature(preview_mesh),
              .layout_key = preview_mesh.layout_key,
              .position_revision = preview_mesh.position_revision,
              .position_payload = preview_position_owner(preview_mesh),
              .topology_payload = preview_geometry_owner(preview_mesh),
              .geometry_projection = preview_mesh.geometry_projection,
              .material_payload = preview_mesh.material,
              .transforms = std::move(transforms),
              .sampling_from_local = std::move(mappings),
              .dynamic_positions = preview_mesh.dynamic_positions,
              .live_proxy_count = live_proxy_count,
              .rendered_prototype = std::move(rendered),
              .shaded_prototype = std::move(shaded),
              .overlay_depth_prototype = std::move(overlay_depth),
          };
        };
    if (!shaded_prototype.entity) {
      PreviewGroup group = make_standard_group(
          std::move(rendered_prototype), std::move(shaded_prototype),
          MeshRenderableProxy{});
      generation->groups.push_back(std::move(group));
      return generation;
    }
    retain_entity(shaded_prototype);
    factory_->update_texture_mapping(shaded_prototype, mappings.front());
    MeshRenderableProxy overlay_depth_prototype =
        factory_->create_shared_geometry(
        preview_mesh.source_entity, mesh_snapshot, &preview_mesh.material,
        rendered_prototype, transforms.front(), preview_mesh.double_sided, 0u,
        true, true, kViewportOverlayDepthChannel, bounds_override);
    if (!overlay_depth_prototype.entity) {
      PreviewGroup group = make_standard_group(
          std::move(rendered_prototype), std::move(shaded_prototype),
          std::move(overlay_depth_prototype));
      generation->groups.push_back(std::move(group));
      return generation;
    }
    retain_entity(overlay_depth_prototype);
    factory_->update_texture_mapping(overlay_depth_prototype, mappings.front());
    PreviewGroup group = make_standard_group(
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
      factory_->update_texture_mapping(
          rendered_instance, group.sampling_from_local[index]);
      group.rendered_instances.push_back(std::move(rendered_instance));
      ++group.live_proxy_count;

      MeshRenderableProxy shaded_instance = factory_->create_shared_instance(
          preview_mesh.source_entity, mesh_snapshot,
          group.shaded_prototype, group.transforms[index], 0u,
          kViewportDefaultRenderableChannel, false, bounds_override);
      if (!shaded_instance.entity) {
        generation->groups.push_back(std::move(group));
        return generation;
      }
      retain_entity(shaded_instance);
      factory_->update_texture_mapping(
          shaded_instance, group.sampling_from_local[index]);
      group.shaded_instances.push_back(std::move(shaded_instance));
      ++group.live_proxy_count;

      MeshRenderableProxy depth_instance = factory_->create_shared_instance(
          preview_mesh.source_entity, mesh_snapshot,
          group.overlay_depth_prototype, group.transforms[index], 0u,
          kViewportOverlayDepthChannel, false, bounds_override);
      if (!depth_instance.entity) {
        generation->groups.push_back(std::move(group));
        return generation;
      }
      retain_entity(depth_instance);
      factory_->update_texture_mapping(
          depth_instance, group.sampling_from_local[index]);
      group.overlay_depth_instances.push_back(std::move(depth_instance));
      ++group.live_proxy_count;
    }
    generation->groups.push_back(std::move(group));
  }
  } catch (...) {
    return generation;
  }
  complete = true;
  return generation;
}

bool ToolPreviewRenderableCache::prepare_dynamic_family_generation(
    PreviewGeneration &generation,
    const std::vector<RenderToolPreviewMeshRecord> &families,
    std::vector<std::size_t> &candidate_indices,
    std::size_t dynamic_family_count) {
  QDR_PROFILE_SCOPE("Tool preview reconcile dynamic GPU families");
  /// Prevalidated occurrence matrices and bounds for one retained draw chunk.
  struct PreparedChunk final {
    RenderInstanceBounds bounds;
    std::shared_ptr<MeshRenderableFactory::ExplicitInstanceUpdate> transforms;
  };
  /// Owns every fallible host allocation before publishing a family update.
  struct PreparedFamily final {
    std::size_t submission_index = 0u;
    std::size_t geometry_index = 0u;
    bool positions_changed = false;
    bool transforms_changed = false;
    bool mappings_changed = false;
    RenderInstanceBounds local_bounds;
    std::vector<QMat4> transforms;
    std::vector<QMat4> mappings;
    std::vector<PreparedChunk> chunks;
  };
  std::unique_ptr<PreviewGeneration> growth;
  try {
    StableCandidateBuckets buckets;
    buckets.reserve(generation.groups.size());
    for (std::size_t index = 0u; index < generation.groups.size(); ++index) {
      const auto &group = generation.groups[index];
      const auto key = RenderSignatureBuilder{}.add_value(group.topology_signature)
          .add_value(group.material_signature).add_size(group.transforms.size())
          .add_bool(group.gpu_family).value();
      append_candidate(buckets, key, index);
    }
    std::vector<bool> claimed(generation.groups.size(), false);
    std::vector<RenderToolPreviewMeshRecord> missing;
    std::size_t missing_family_count = 0u;
    std::vector<PreparedPreviewGeometry> geometry;
    std::vector<PreparedFamily> updates;
    candidate_indices.clear();
    candidate_indices.reserve(families.size());
    geometry.reserve(families.size());
    updates.reserve(families.size());
    for (std::size_t index = 0u; index < families.size(); ++index) {
      const auto &family = families[index];
      const bool gpu_family = index < dynamic_family_count;
      const auto key = RenderSignatureBuilder{}.add_value(preview_topology_signature(family))
          .add_value(preview_material_signature(family))
          .add_size(preview_transform_count(family)).add_bool(gpu_family).value();
      const auto match = find_candidate(buckets, key, claimed,
          work_counters_.structural_match_candidate_visits, [&](std::size_t group_index) {
        const auto &group = generation.groups[group_index];
        return group.gpu_family == gpu_family &&
               (gpu_family || group_matches_exact(group, family)) &&
               group.transforms.size() == preview_transform_count(family) &&
               group_topology_equals(group, family) &&
               same_preview_material_payload(group.material_payload, family.material) &&
               (!family.dynamic_positions || group_dynamic_update_is_valid(group, family)) &&
               (!group.explicit_instances || group_explicit_update_is_valid(group, family));
      });
      if (!match) {
        candidate_indices.push_back(generation.groups.size() + missing.size());
        missing.push_back(family);
        missing_family_count += gpu_family ? 1u : 0u;
        continue;
      }
      claim_candidate(match, claimed);
      candidate_indices.push_back(match.group_index);
      const auto &group = generation.groups[match.group_index];
      if (group_submission_unchanged(group, family)) continue;
      PreparedFamily update{.submission_index = index,
          .geometry_index = geometry.size(),
          .positions_changed = group_positions_changed(group, family),
          .transforms_changed = !preview_transforms_equal(group.transforms, family),
          .mappings_changed = !preview_texture_mappings_equal(group.sampling_from_local, family)};
      copy_preview_transforms(update.transforms, family);
      copy_preview_texture_mappings(update.mappings, family);
      if (update.positions_changed) {
        geometry.push_back({.submission_index = index,
            .mesh = preview_mesh_snapshot(family),
            .position_owner = preview_position_owner(family)});
      }
      update.chunks.reserve(group.explicit_instance_chunks.size());
      updates.push_back(std::move(update));
    }
    bool parallel = false;
    std::size_t canonical_count = 0u;
    if (!prepare_preview_geometry(geometry, parallel, canonical_count)) return false;
    for (auto &update : updates) {
      const auto &group = generation.groups[candidate_indices[update.submission_index]];
      const auto &family = families[update.submission_index];
      const auto positions = preview_dynamic_positions(family);
      const auto local_bounds = update.positions_changed ? geometry[update.geometry_index].bounds :
          positions.empty() ? MeshRenderableFactory::compute_local_bounds(
              preview_mesh_snapshot(family)) :
              MeshRenderableFactory::compute_local_bounds(positions);
      update.local_bounds = local_bounds;
      for (const auto &chunk : group.explicit_instance_chunks) {
        const std::span<const QMat4> transforms{update.transforms.data() + chunk.transform_offset,
                                               chunk.active_count};
        PreparedChunk prepared_chunk{.bounds = render_instance_world_bounds(local_bounds, transforms)};
        if (update.transforms_changed) {
          prepared_chunk.transforms = MeshRenderableFactory::prepare_explicit_instance_update(transforms);
          if (!prepared_chunk.transforms ||
              !factory_->validate_explicit_instances(chunk.rendered, *prepared_chunk.transforms) ||
              !factory_->validate_explicit_instances(chunk.shaded, *prepared_chunk.transforms) ||
              !factory_->validate_explicit_instances(chunk.overlay_depth, *prepared_chunk.transforms)) return false;
        }
        update.chunks.push_back(std::move(prepared_chunk));
      }
    }
    if (!missing.empty()) {
      bool complete = false;
      growth = build_generation(missing, complete, missing_family_count);
      if (!complete) {
        retire_generation(std::move(growth));
        return false;
      }
      generation.groups.reserve(generation.groups.size() + growth->groups.size());
      generation.scene_entities.reserve(generation.scene_entities.size() + growth->scene_entities.size());
    }
    // Every host allocation, exact-count instance validation and geometry pack
    // completes before accepted buffers change. Counts never rebind a live
    // renderable: split/merge candidates use hidden exact-count family slots.
    for (auto &update : updates) {
      auto &group = generation.groups[candidate_indices[update.submission_index]];
      const auto &family = families[update.submission_index];
      if (update.positions_changed) {
        auto &prepared = geometry[update.geometry_index];
        const bool uploaded = factory_->update_dynamic_geometry(group.rendered_prototype,
            prepared.mesh, preview_dynamic_positions(family), &prepared.bounds, std::move(prepared.upload));
        assert(uploaded);
        (void)uploaded;
      }
      if (!group.explicit_instances) {
        const auto apply = [&](const MeshRenderableProxy &proxy, std::size_t index) {
          if (update.positions_changed) factory_->update_bounds(proxy, update.local_bounds);
          if (update.transforms_changed) factory_->update_transform(proxy, update.transforms[index]);
          if (update.mappings_changed) factory_->update_texture_mapping(proxy, update.mappings[index]);
        };
        apply(group.rendered_prototype, 0u);
        apply(group.shaded_prototype, 0u);
        apply(group.overlay_depth_prototype, 0u);
        for (std::size_t index = 0u; index < group.rendered_instances.size(); ++index) {
          apply(group.rendered_instances[index], index + 1u);
          apply(group.shaded_instances[index], index + 1u);
          apply(group.overlay_depth_instances[index], index + 1u);
        }
      }
      for (std::size_t index = 0u; index < group.explicit_instance_chunks.size(); ++index) {
        auto &chunk = group.explicit_instance_chunks[index];
        const auto &prepared = update.chunks[index];
        const auto apply = [&](const MeshRenderableProxy &proxy) {
          if (prepared.transforms) {
            const bool uploaded = factory_->update_explicit_instances_prepared(proxy, *prepared.transforms,
                                                                                prepared.bounds);
            assert(uploaded);
            (void)uploaded;
          } else if (update.positions_changed) {
            factory_->update_bounds(proxy, prepared.bounds);
          }
          if (update.mappings_changed) {
            factory_->update_texture_mappings(proxy,
                std::span<const QMat4>{update.mappings.data() + chunk.transform_offset, chunk.active_count});
          }
        };
        apply(chunk.rendered);
        apply(chunk.shaded);
        apply(chunk.overlay_depth);
        work_counters_.explicit_instance_prefix_updates += update.transforms_changed ? 1u : 0u;
        chunk.rendered.source_entity = family.source_entity;
        chunk.shaded.source_entity = family.source_entity;
        chunk.overlay_depth.source_entity = family.source_entity;
      }
      group.source_object_id = family.source_object_id;
      group.source_entity = family.source_entity;
      group.visibility_entity = family.visibility_entity;
      group.geometry_signature = preview_geometry_signature(family);
      group.topology_signature = preview_topology_signature(family);
      group.layout_key = family.layout_key;
      group.position_revision = family.position_revision;
      group.position_payload = preview_position_owner(family);
      group.topology_payload = preview_geometry_owner(family);
      group.geometry_projection = family.geometry_projection;
      group.transforms = std::move(update.transforms);
      group.sampling_from_local = std::move(update.mappings);
    }
    if (growth) {
      work_counters_.dynamic_family_group_creations += missing_family_count;
      for (auto &group : growth->groups) generation.groups.push_back(std::move(group));
      generation.scene_entities.insert(generation.scene_entities.end(),
          growth->scene_entities.begin(), growth->scene_entities.end());
    }
    work_counters_.dynamic_geometry_preparations += geometry.size();
    work_counters_.canonical_dynamic_geometry_preparations += canonical_count;
    work_counters_.reused_dynamic_geometry_preparations += geometry.size() - canonical_count;
    work_counters_.parallel_dynamic_geometry_batches += parallel ? 1u : 0u;
    return true;
  } catch (...) {
    retire_generation(std::move(growth));
    return false;
  }
}

bool ToolPreviewRenderableCache::prepare_generation(
    PreviewGeneration &generation,
    const std::vector<RenderToolPreviewMeshRecord> &meshes,
    std::vector<std::size_t> &candidate_indices,
    bool allow_explicit_count_change, bool &preparation_failed) {
  QDR_PROFILE_FUNCTION();
  candidate_indices.clear();
  preparation_failed = false;

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
                                          meshes[index])) ||
          (preview_uses_explicit_instances(meshes[index]) &&
           !group_explicit_update_is_valid(generation.groups[group_index],
                                           meshes[index])) ||
          (!allow_explicit_count_change &&
           generation.groups[group_index].explicit_instances &&
           generation.groups[group_index].transforms.size() !=
               preview_transform_count(meshes[index]))) {
        ordered_active_match = false;
        break;
      }
    }
    if (ordered_active_match) {
      QDR_PROFILE_SCOPE("Tool preview ordered active fast path");
      candidate_indices = generation.active_group_indices;
      std::vector<PreparedPreviewGeometry> prepared;
      bool parallel = false;
      std::size_t canonical_count = 0u;
      try {
        prepared.reserve(meshes.size());
        for (std::size_t index = 0u; index < meshes.size(); ++index) {
          const PreviewGroup &group = generation.groups[candidate_indices[index]];
          if (!group.explicit_instances && group_positions_changed(group, meshes[index])) {
            prepared.push_back({.submission_index = index,
                .mesh = preview_mesh_snapshot(meshes[index]),
                .position_owner = preview_position_owner(meshes[index])});
          }
        }
        if (!prepare_preview_geometry(prepared, parallel, canonical_count)) {
          preparation_failed = true;
          return false;
        }
      } catch (...) {
        preparation_failed = true;
        return false;
      }
      work_counters_.dynamic_geometry_preparations += prepared.size();
      work_counters_.canonical_dynamic_geometry_preparations += canonical_count;
      work_counters_.reused_dynamic_geometry_preparations += prepared.size() - canonical_count;
      work_counters_.parallel_dynamic_geometry_batches += parallel ? 1u : 0u;
      std::size_t prepared_index = 0u;
      for (std::size_t index = 0u; index < meshes.size(); ++index) {
        PreviewGroup &group = generation.groups[candidate_indices[index]];
        if (!group_submission_unchanged(group, meshes[index])) {
          const PreparedPreviewGeometry *job = prepared_index < prepared.size() &&
              prepared[prepared_index].submission_index == index
                  ? &prepared[prepared_index++] : nullptr;
          if (!update_group(group, meshes[index], job ? job->upload : nullptr,
                            job ? &job->bounds : nullptr)) {
            return false;
          }
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
    if (preview_uses_explicit_instances(*renderable_meshes[index]) &&
        !group_explicit_update_is_valid(group, *renderable_meshes[index])) {
      return false;
    }
    if (!allow_explicit_count_change && group.explicit_instances &&
        group.transforms.size() !=
            preview_transform_count(*renderable_meshes[index])) {
      return false;
    }
  }

  {
    QDR_PROFILE_SCOPE("Tool preview update reused candidate groups");
    for (std::size_t index = 0u; index < renderable_meshes.size(); ++index) {
      PreviewGroup &group = generation.groups[candidate_indices[index]];
      if (!group_submission_unchanged(group, *renderable_meshes[index])) {
        if (!update_group(group, *renderable_meshes[index])) {
          return false;
        }
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

bool ToolPreviewRenderableCache::update_group(
    PreviewGroup &group,
    const RenderToolPreviewMeshRecord &preview_mesh,
    std::shared_ptr<MeshRenderableFactory::DynamicGeometryUpdate> prepared,
    const RenderInstanceBounds *prepared_bounds) {
  QDR_PROFILE_FUNCTION();
  const std::uint64_t geometry_signature =
      preview_geometry_signature(preview_mesh);
  const std::shared_ptr<const MeshGeometryPayload> topology_payload =
      preview_geometry_owner(preview_mesh);
  const bool positions_changed = group_positions_changed(group, preview_mesh);

  if (group.explicit_instances) {
    if (!group_explicit_update_is_valid(group, preview_mesh)) {
      return false;
    }
    std::vector<QMat4> next_transforms;
    copy_preview_transforms(next_transforms, preview_mesh);
    std::vector<QMat4> next_mappings;
    copy_preview_texture_mappings(next_mappings, preview_mesh);
    const bool transforms_changed =
        !preview_transforms_equal(group.transforms, preview_mesh);
    const bool mappings_changed =
        !preview_texture_mappings_equal(group.sampling_from_local,
                                        preview_mesh);
    const ToolPreviewInstancePlan plan = plan_tool_preview_instances(
        next_transforms.size(), maximum_instances_per_chunk_,
        group.explicit_instance_capacities);
    if (plan.new_chunk_count != 0u ||
        plan.chunks.size() != group.explicit_instance_capacities.size()) {
      return false;
    }
    const RenderMeshAssetSnapshot mesh_snapshot =
        preview_mesh_snapshot(preview_mesh);
    const auto positions = preview_dynamic_positions(preview_mesh);
    const RenderInstanceBounds local_bounds = prepared_bounds != nullptr ? *prepared_bounds :
        positions.empty() ? MeshRenderableFactory::compute_local_bounds(mesh_snapshot)
                          : MeshRenderableFactory::compute_local_bounds(positions);
    if (positions_changed && !factory_->update_dynamic_geometry(
            group.rendered_prototype, mesh_snapshot, positions, &local_bounds,
            std::move(prepared))) return false;
    const std::size_t chunks_per_primitive = plan.chunks.size();
    for (std::size_t chunk_index = 0u;
         chunk_index < group.explicit_instance_chunks.size();
         ++chunk_index) {
      PreviewGroup::ExplicitInstanceChunk &chunk =
          group.explicit_instance_chunks[chunk_index];
      const ToolPreviewInstanceChunkPlan &planned =
          plan.chunks[chunk_index % chunks_per_primitive];
      if (planned.active_count != 0u &&
          (planned.transform_offset > next_transforms.size() ||
           planned.active_count >
               next_transforms.size() - planned.transform_offset)) {
        return false;
      }
      const std::span<const QMat4> active_transforms =
          planned.active_count == 0u
              ? std::span<const QMat4>{}
              : std::span<const QMat4>{
                    next_transforms.data() + planned.transform_offset,
                    planned.active_count};
      const std::span<const QMat4> active_mappings =
          planned.active_count == 0u
              ? std::span<const QMat4>{}
              : std::span<const QMat4>{
                    next_mappings.data() + planned.transform_offset,
                    planned.active_count};
      const RenderInstanceBounds world_bounds =
          active_transforms.empty()
              ? RenderInstanceBounds{}
              : render_instance_world_bounds(local_bounds, active_transforms);
      if (transforms_changed) {
        const bool same_active_count =
            chunk.active_count == planned.active_count &&
            planned.active_count != 0u;
        const auto update_variant =
            [&](const MeshRenderableProxy &proxy, bool cast_shadows,
                bool receive_shadows, bool depth_only,
                std::uint8_t visible_layer,
                std::uint8_t channel) {
              if (same_active_count) {
                return factory_->update_explicit_instances(
                    proxy, active_transforms, world_bounds);
              }
              const std::uint8_t layer =
                  group.visible && !active_transforms.empty()
                      ? visible_layer
                      : 0u;
              return factory_->rebind_explicit_instances(
                  proxy, chunk.primitive, active_transforms, world_bounds,
                  cast_shadows, receive_shadows, preview_mesh.double_sided,
                  layer, depth_only, channel);
            };
        if (!update_variant(chunk.rendered, preview_mesh.cast_shadows, true,
                            false, kViewportRenderedSceneLayerMask,
                            kViewportDefaultRenderableChannel) ||
            !update_variant(chunk.shaded, false, false, false,
                            kViewportShadedSceneLayerMask,
                            kViewportDefaultRenderableChannel) ||
            !update_variant(chunk.overlay_depth, false, false, true,
                            kViewportOverlayLayerMask,
                            kViewportOverlayDepthChannel)) {
          return false;
        }
        chunk.transform_offset = planned.transform_offset;
        chunk.active_count = planned.active_count;
        ++work_counters_.explicit_instance_prefix_updates;
      } else if (positions_changed) {
        factory_->update_bounds(chunk.rendered, world_bounds);
        factory_->update_bounds(chunk.shaded, world_bounds);
        factory_->update_bounds(chunk.overlay_depth, world_bounds);
      }
      if (mappings_changed && !active_mappings.empty()) {
        factory_->update_texture_mappings(chunk.rendered, active_mappings);
        factory_->update_texture_mappings(chunk.shaded, active_mappings);
        factory_->update_texture_mappings(chunk.overlay_depth,
                                          active_mappings);
      }
    }
    group.transforms = std::move(next_transforms);
    group.sampling_from_local = std::move(next_mappings);
  } else {
    if (positions_changed) {
      QDR_PROFILE_SCOPE("Tool preview changed position payload");
      const RenderMeshAssetSnapshot mesh_snapshot =
          preview_mesh_snapshot(preview_mesh);
      const std::span<const QVec3> positions =
          preview_dynamic_positions(preview_mesh);
      const RenderInstanceBounds local_bounds =
          prepared_bounds != nullptr ? *prepared_bounds : positions.empty()
              ? MeshRenderableFactory::compute_local_bounds(mesh_snapshot)
              : MeshRenderableFactory::compute_local_bounds(positions);
      const bool updated = factory_->update_dynamic_geometry(
          group.rendered_prototype, mesh_snapshot, positions, &local_bounds,
          std::move(prepared));
      if (!updated) {
        return false;
      }
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

    if (!preview_texture_mappings_equal(group.sampling_from_local,
                                        preview_mesh)) {
      QDR_PROFILE_SCOPE("Tool preview changed texture mappings");
      copy_preview_texture_mappings(group.sampling_from_local, preview_mesh);
      factory_->update_texture_mapping(group.rendered_prototype,
                                       group.sampling_from_local.front());
      factory_->update_texture_mapping(group.shaded_prototype,
                                       group.sampling_from_local.front());
      factory_->update_texture_mapping(group.overlay_depth_prototype,
                                       group.sampling_from_local.front());
      for (std::size_t index = 0u;
           index < group.rendered_instances.size(); ++index) {
        const QMat4 &mapping = group.sampling_from_local[index + 1u];
        factory_->update_texture_mapping(group.rendered_instances[index],
                                         mapping);
        factory_->update_texture_mapping(group.shaded_instances[index],
                                         mapping);
        factory_->update_texture_mapping(
            group.overlay_depth_instances[index], mapping);
      }
    }
  }

  if (group.source_entity != preview_mesh.source_entity) {
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
    for (PreviewGroup::ExplicitInstanceChunk &chunk :
         group.explicit_instance_chunks) {
      update_source(chunk.rendered);
      update_source(chunk.shaded);
      update_source(chunk.overlay_depth);
    }
  }

  group.source_object_id = preview_mesh.source_object_id;
  group.source_entity = preview_mesh.source_entity;
  group.visibility_entity = preview_mesh.visibility_entity;
  group.geometry_signature = geometry_signature;
  group.material_signature = preview_material_signature(preview_mesh);
  group.layout_key = preview_mesh.layout_key;
  group.position_revision = preview_mesh.position_revision;
  group.position_payload = preview_position_owner(preview_mesh);
  if (!group.topology_payload ||
      group.topology_payload.get() != topology_payload.get() ||
      group.geometry_projection != preview_mesh.geometry_projection) {
    group.topology_signature = preview_topology_signature(preview_mesh);
  }
  group.topology_payload = topology_payload;
  group.geometry_projection = preview_mesh.geometry_projection;
  group.dynamic_positions = preview_mesh.dynamic_positions;
  return true;
}
void ToolPreviewRenderableCache::publish_generation_subset(
    PreviewGeneration &generation,
    std::vector<std::size_t> candidate_indices) {
  for (const std::size_t index : generation.active_group_indices) {
    generation.groups[index].candidate_visible = false;
  }
  for (const std::size_t index : candidate_indices) {
    if (index < generation.groups.size()) {
      generation.groups[index].candidate_visible = true;
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
    if (index < generation.groups.size() && !generation.groups[index].candidate_visible) {
      set_group_visible(generation.groups[index], false);
    }
  }
  generation.active_group_indices = std::move(candidate_indices);
  if (generation.dynamic_families) trim_inactive_dynamic_families(generation);
}

void ToolPreviewRenderableCache::trim_inactive_dynamic_families(
    PreviewGeneration &generation) {
  constexpr std::size_t kRetainedInactiveFamilies = 64u;
  constexpr std::size_t kRetainedInactiveProxies = 384u;
  work_counters_.inactive_dynamic_geometry_families =
      std::ranges::count_if(generation.groups, [](const auto &group) { return !group.visible && group.gpu_family; });
  std::size_t inactive_proxy_count = 0u;
  for (const auto &group : generation.groups) {
    if (!group.visible) inactive_proxy_count += group.live_proxy_count;
  }
  if (generation.groups.size() - generation.active_group_indices.size() <= kRetainedInactiveFamilies &&
      inactive_proxy_count <= kRetainedInactiveProxies) return;
  QDR_PROFILE_SCOPE("Tool preview trim inactive GPU families");
  // Allocate before moving ownership. A failed optional trim leaves the valid
  // generation intact and can be retried on the next accepted submission.
  std::unique_ptr<PreviewGeneration> retired;
  std::vector<PreviewGroup> retained;
  std::vector<utils::Entity> retained_entities;
  std::vector<std::size_t> remap;
  std::vector<bool> active;
  try {
    retired = std::make_unique<PreviewGeneration>();
    remap.resize(generation.groups.size());
    active.resize(generation.groups.size(), false);
    for (const auto index : generation.active_group_indices) active[index] = true;
    retired->groups.reserve(generation.groups.size());
    retired->scene_entities.reserve(generation.scene_entities.size());
    retained.reserve(generation.active_group_indices.size() + kRetainedInactiveFamilies);
    retained_entities.reserve(generation.scene_entities.size());
    retired_generations_.reserve(retired_generations_.size() + 1u);
  } catch (...) {
    return;
  }
  const auto append_entities = [](const PreviewGroup &group, auto &entities) {
    for (const auto entity : {group.rendered_prototype.entity, group.shaded_prototype.entity,
                              group.overlay_depth_prototype.entity}) {
      if (entity) entities.push_back(entity);
    }
    for (const auto *instances : {&group.rendered_instances, &group.shaded_instances,
                                  &group.overlay_depth_instances}) {
      for (const auto &proxy : *instances) if (proxy.entity) entities.push_back(proxy.entity);
    }
    for (const auto &chunk : group.explicit_instance_chunks) {
      for (const auto entity : {chunk.rendered.entity, chunk.shaded.entity, chunk.overlay_depth.entity}) {
        if (entity) entities.push_back(entity);
      }
    }
  };
  std::size_t inactive = 0u;
  std::size_t retained_proxy_count = 0u;
  for (std::size_t index = 0u; index < generation.groups.size(); ++index) {
    auto &group = generation.groups[index];
    const bool retain_inactive = inactive < kRetainedInactiveFamilies &&
        group.live_proxy_count <= kRetainedInactiveProxies - retained_proxy_count;
    if (active[index] || retain_inactive) {
      inactive += active[index] ? 0u : 1u;
      retained_proxy_count += active[index] ? 0u : group.live_proxy_count;
      remap[index] = retained.size();
      append_entities(group, retained_entities);
      retained.push_back(std::move(group));
    } else {
      append_entities(group, retired->scene_entities);
      retired->groups.push_back(std::move(group));
    }
  }
  generation.groups = std::move(retained);
  generation.scene_entities = std::move(retained_entities);
  for (auto &index : generation.active_group_indices) index = remap[index];
  work_counters_.inactive_dynamic_geometry_families =
      std::ranges::count_if(generation.groups, [](const auto &group) { return !group.visible && group.gpu_family; });
  retire_generation(std::move(retired));
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
  for (const PreviewGroup::ExplicitInstanceChunk &chunk :
       group.explicit_instance_chunks) {
    const bool chunk_visible = visible && chunk.active_count != 0u;
    factory_->update_layer_mask(
        chunk.rendered,
        chunk_visible ? kViewportRenderedSceneLayerMask : 0u);
    factory_->update_layer_mask(
        chunk.shaded,
        chunk_visible ? kViewportShadedSceneLayerMask : 0u);
    factory_->update_layer_mask(
        chunk.overlay_depth,
        chunk_visible ? kViewportOverlayLayerMask : 0u);
  }
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
        for (const PreviewGroup::ExplicitInstanceChunk &chunk :
             group.explicit_instance_chunks) {
          if (chunk.active_count == 0u) {
            continue;
          }
          factory_->update_surface_grid(chunk.rendered, grid_world_spacing,
                                        settings);
          factory_->update_surface_grid(chunk.shaded, grid_world_spacing,
                                        settings);
          factory_->update_shaded_lighting(chunk.shaded, settings);
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
  factory_->destroy_renderable_components(generation->scene_entities);
  for (const auto &group : generation->groups) {
    work_counters_.retired_proxy_count += group.live_proxy_count;
  }
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
    --work_counters_.retired_proxy_count;
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
  while (!group.explicit_instance_chunks.empty()) {
    PreviewGroup::ExplicitInstanceChunk &chunk =
        group.explicit_instance_chunks.back();
    if (chunk.overlay_depth.entity) {
      destroy_proxy(chunk.overlay_depth);
      record_destroyed_proxy();
      return true;
    }
    if (chunk.shaded.entity) {
      destroy_proxy(chunk.shaded);
      record_destroyed_proxy();
      return true;
    }
    if (chunk.rendered.entity) {
      destroy_proxy(chunk.rendered);
      record_destroyed_proxy();
      return true;
    }
    group.explicit_instance_chunks.pop_back();
  }
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
  const auto started = std::chrono::steady_clock::now();
  constexpr auto kMaximumDuration = std::chrono::milliseconds(2);
  while (destroyed < max_proxy_count && !retired_generations_.empty()) {
    PreviewGeneration &generation = *retired_generations_.back();
    while (!generation.groups.empty() &&
           group_proxy_count(generation.groups.back()) == 0u) {
      // Buffer-only dynamic owners have no entity and therefore do not count
      // as proxies. Their borrowers have all retired before releasing them.
      destroy_group(generation.groups.back(), generation.detached);
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
    if (std::chrono::steady_clock::now() - started >= kMaximumDuration) break;
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
  for (PreviewGroup::ExplicitInstanceChunk &chunk :
       group.explicit_instance_chunks) {
    destroy_proxy(chunk.overlay_depth);
    destroy_proxy(chunk.shaded);
    destroy_proxy(chunk.rendered);
  }
  group.explicit_instance_chunks.clear();
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
  work_counters_.retired_proxy_count = 0u;
  work_counters_.active_dynamic_geometry_families = 0u;
  work_counters_.active_dynamic_geometry_participants = 0u;
  work_counters_.ordinary_static_records = 0u;
  work_counters_.ordinary_unknown_opacity_records = 0u;
  work_counters_.ordinary_transparent_records = 0u;
  work_counters_.ordinary_reflected_records = 0u;
  work_counters_.inactive_dynamic_geometry_families = 0u;
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
  RenderSignatureBuilder signature;
  // The revision normally equals the binding signature. Hash both as values:
  // seeding with one then XORing the other would cancel all material changes.
  signature.add_value(render_material_binding_signature(preview_mesh.material))
      .add_value(preview_mesh.material_revision)
      .add_bool(preview_mesh.double_sided)
      .add_bool(preview_mesh.cast_shadows)
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
  signature.add_value(preview_mesh.geometry_projection
      ? reinterpret_cast<std::uintptr_t>(preview_mesh.geometry_projection.get())
      : preview_mesh.mesh.geometry_content_id().value)
      .add_value(preview_mesh.mesh.revision.value);
  if (preview_mesh.dynamic_positions) {
    signature.add_value(preview_mesh.layout_key)
        .add_value(preview_mesh.position_revision);
  }
  return signature.value();
}

std::uint64_t ToolPreviewRenderableCache::preview_topology_signature(
    const RenderToolPreviewMeshRecord &preview_mesh) {
  const auto geometry = preview_mesh_snapshot(preview_mesh);
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
  const bool explicit_instances =
      preview_uses_explicit_instances(preview_mesh);
  RenderSignatureBuilder signature;
  signature.add_value(preview_mesh.source_object_id)
      .add_value(preview_mesh.source_entity)
      .add_value(preview_mesh.visibility_entity)
      .add_value(preview_material_signature(preview_mesh))
      .add_bool(preview_mesh.dynamic_positions)
      .add_bool(explicit_instances);
  if (!explicit_instances) {
    signature.add_size(preview_transform_count(preview_mesh));
  }
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
      .add_bool(group.explicit_instances);
  if (!group.explicit_instances) {
    signature.add_size(group.transforms.size());
  }
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
      group.explicit_instances !=
          preview_uses_explicit_instances(preview_mesh) ||
      (!group.explicit_instances &&
       group.transforms.size() != preview_transform_count(preview_mesh))) {
    return false;
  }
  if (!preview_mesh.dynamic_positions) {
    if (group.geometry_signature != preview_geometry_signature(preview_mesh)) {
      return false;
    }
    const std::shared_ptr<const MeshGeometryPayload> geometry_payload =
        preview_geometry_owner(preview_mesh);
    const std::size_t retained_capacity = std::accumulate(
        group.explicit_instance_capacities.begin(),
        group.explicit_instance_capacities.end(), std::size_t{0u});
    return same_geometry_streams(group.topology_payload, group.geometry_projection,
                                 geometry_payload, preview_mesh.geometry_projection, false, true) &&
           (!group.explicit_instances ||
            retained_capacity >= preview_transform_count(preview_mesh));
  }
  const bool layout_compatible =
      group.layout_key == preview_mesh.layout_key || group.layout_key == 0u ||
      preview_mesh.layout_key == 0u;
  const std::shared_ptr<const MeshGeometryPayload> topology_payload =
      preview_geometry_owner(preview_mesh);
  if (group.layout_key != 0u &&
      group.layout_key == preview_mesh.layout_key &&
      group.topology_payload.get() == topology_payload.get() &&
      group.geometry_projection == preview_mesh.geometry_projection) {
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
    return same_geometry_streams(group.topology_payload, group.geometry_projection,
        preview_geometry_owner(preview_mesh), preview_mesh.geometry_projection, true, false);
  }
  return same_geometry_streams(group.topology_payload, group.geometry_projection,
      preview_geometry_owner(preview_mesh), preview_mesh.geometry_projection, false, false);
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
         group.position_payload == preview_position_owner(preview_mesh) &&
         group.geometry_projection == preview_mesh.geometry_projection &&
         group.topology_payload.get() ==
             preview_geometry_owner(preview_mesh).get() &&
         group.dynamic_positions == preview_mesh.dynamic_positions &&
         preview_transforms_equal(group.transforms, preview_mesh) &&
         preview_texture_mappings_equal(group.sampling_from_local, preview_mesh);
}

bool ToolPreviewRenderableCache::group_positions_changed(
    const PreviewGroup &group,
    const RenderToolPreviewMeshRecord &preview_mesh) {
  return preview_mesh.dynamic_positions &&
      (group.source_object_id != preview_mesh.source_object_id ||
       group.source_entity != preview_mesh.source_entity ||
       group.visibility_entity != preview_mesh.visibility_entity ||
       group.layout_key != preview_mesh.layout_key ||
       group.position_revision != preview_mesh.position_revision ||
       group.position_payload != preview_position_owner(preview_mesh) ||
       group.geometry_projection != preview_mesh.geometry_projection ||
       group.geometry_signature != preview_geometry_signature(preview_mesh) ||
       (!preview_mesh.dynamic_position_payload &&
        group.topology_payload.get() != preview_geometry_owner(preview_mesh).get()));
}

bool ToolPreviewRenderableCache::group_dynamic_update_is_valid(
    const PreviewGroup &group,
    const RenderToolPreviewMeshRecord &preview_mesh) const {
  const auto geometry = preview_mesh_snapshot(preview_mesh);
  const std::span<const QVec3> positions =
      preview_dynamic_positions(preview_mesh);
  // This mirrors every guard in MeshRenderableFactory::update_dynamic_geometry.
  // Complete generations separately guarantee sibling/vector shape. With the
  // factory confined to the render thread, these guards remain true throughout
  // prepare_generation's following non-failing mutation pass.
  return factory_ != nullptr && factory_->initialized() &&
         group.rendered_prototype.dynamic_geometry &&
         group.rendered_prototype.owns_mesh_buffers &&
         group.rendered_prototype.vertex_buffer != nullptr &&
         group.rendered_prototype.index_buffer != nullptr &&
         group.rendered_prototype.vertex_count == geometry.vertices.size() &&
         group.rendered_prototype.index_count == geometry.indices.size() &&
         (positions.empty() || positions.size() == geometry.vertices.size());
}

bool ToolPreviewRenderableCache::group_explicit_update_is_valid(
    const PreviewGroup &group,
    const RenderToolPreviewMeshRecord &preview_mesh) const {
  if (factory_ == nullptr || !factory_->initialized() ||
      !group.explicit_instances ||
      (!preview_mesh.dynamic_positions && !preview_uses_explicit_instances(preview_mesh)) ||
      group.explicit_instance_capacities.empty()) {
    return false;
  }
  const ToolPreviewInstancePlan plan = plan_tool_preview_instances(
      preview_transform_count(preview_mesh), maximum_instances_per_chunk_,
      group.explicit_instance_capacities);
  if (plan.new_chunk_count != 0u ||
      plan.chunks.size() != group.explicit_instance_capacities.size()) {
    return false;
  }
  const RenderMeshAssetSnapshot mesh_snapshot =
      preview_mesh_snapshot(preview_mesh);
  const std::vector<MeshPrimitiveSpan> primitives =
      coalesced_render_mesh_primitive_spans(mesh_snapshot);
  if (primitives.empty() ||
      group.explicit_instance_chunks.size() !=
          primitives.size() * plan.chunks.size()) {
    return false;
  }
  for (std::size_t primitive_index = 0u;
       primitive_index < primitives.size(); ++primitive_index) {
    for (std::size_t plan_index = 0u; plan_index < plan.chunks.size();
         ++plan_index) {
      const PreviewGroup::ExplicitInstanceChunk &chunk =
          group.explicit_instance_chunks[primitive_index *
                                              plan.chunks.size() +
                                          plan_index];
      const ToolPreviewInstanceChunkPlan &planned = plan.chunks[plan_index];
      if (chunk.primitive != primitives[primitive_index] ||
          chunk.capacity != planned.capacity ||
          !chunk.rendered.entity || !chunk.shaded.entity ||
          !chunk.overlay_depth.entity ||
          chunk.rendered.instance_buffer == nullptr ||
          chunk.shaded.instance_buffer == nullptr ||
          chunk.overlay_depth.instance_buffer == nullptr ||
          chunk.rendered.instance_buffer->getInstanceCount() <
              planned.capacity ||
          chunk.shaded.instance_buffer->getInstanceCount() <
              planned.capacity ||
          chunk.overlay_depth.instance_buffer->getInstanceCount() <
              planned.capacity ||
          chunk.rendered.cast_shadows != preview_mesh.cast_shadows ||
          chunk.shaded.cast_shadows || chunk.overlay_depth.cast_shadows ||
          chunk.rendered.double_sided != preview_mesh.double_sided ||
          chunk.shaded.double_sided != preview_mesh.double_sided ||
          chunk.overlay_depth.double_sided != preview_mesh.double_sided) {
        return false;
      }
    }
  }
  return true;
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
        left.cast_shadows != right.cast_shadows ||
        left.suppress_source != right.suppress_source ||
        left.position_payload != preview_position_owner(right) ||
        !same_geometry_streams(left.geometry_payload, left.geometry_projection,
                              preview_geometry_owner(right), right.geometry_projection, false, true) ||
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
        .position_payload = preview_position_owner(mesh),
        .geometry_payload = preview_geometry_owner(mesh),
        .geometry_projection = mesh.geometry_projection,
        .transform = mesh.transform,
        .instance_transforms = mesh.instance_transforms,
        .sampling_from_local = mesh.sampling_from_local,
        .instance_sampling_from_local = mesh.instance_sampling_from_local,
        .dynamic_positions = mesh.dynamic_positions,
        .cast_shadows = mesh.cast_shadows,
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
        .add_bool(preview_mesh.cast_shadows)
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

ToolPreviewLightCache::~ToolPreviewLightCache() { shutdown(); }

void ToolPreviewLightCache::initialize(filament::Engine &engine,
                                       filament::Scene &scene) {
  shutdown();
  engine_ = &engine;
  scene_ = &scene;
  rebuild_count_ = 0u;
  transform_update_count_ = 0u;
  created_proxy_count_ = 0u;
}

void ToolPreviewLightCache::shutdown() {
  if (engine_ == nullptr) {
    return;
  }
  discard_prepared();
  destroy_generation(std::move(current_));
  submitted_.clear();
  pending_submission_.clear();
  pending_kind_ = PendingKind::None;
  engine_ = nullptr;
  scene_ = nullptr;
}

ToolPreviewSubmissionResult ToolPreviewLightCache::prepare(
    const std::vector<RenderToolPreviewLightRecord> &lights) {
  if (!initialized()) {
    return ToolPreviewSubmissionResult::Failed;
  }
  discard_prepared();
  std::size_t punctual_light_count = 0u;
  for (const RenderToolPreviewLightRecord &light : lights) {
    if (!filament_light_is_punctual(light.light.type)) {
      continue;
    }
    if (light.instance_transforms.size() >
        kMaximumFilamentPunctualLightCount - punctual_light_count) {
      return ToolPreviewSubmissionResult::Failed;
    }
    punctual_light_count += light.instance_transforms.size();
  }
  if (same_submission(lights)) {
    pending_kind_ = PendingKind::Unchanged;
    return ToolPreviewSubmissionResult::Unchanged;
  }
  pending_submission_ = lights;
  if (same_structure(lights)) {
    std::vector<std::size_t> requested_counts;
    std::vector<std::size_t> retained_capacities;
    requested_counts.reserve(lights.size());
    retained_capacities.reserve(lights.size());
    for (std::size_t index = 0u; index < lights.size(); ++index) {
      requested_counts.push_back(lights[index].instance_transforms.size());
      retained_capacities.push_back(current_->pools[index].proxies.size());
    }
    const ToolPreviewLightPoolPlan pool_plan =
        plan_tool_preview_light_pools(requested_counts,
                                      retained_capacities);
    if (pool_plan.new_proxy_count == 0u) {
      pending_kind_ = PendingKind::RetainedUpdate;
      return ToolPreviewSubmissionResult::Published;
    }
    bool complete = false;
    prepared_ = build_inert_growth(lights, complete);
    if (!complete || prepared_ == nullptr) {
      discard_prepared();
      return ToolPreviewSubmissionResult::Failed;
    }
    pending_kind_ = PendingKind::Grow;
    return ToolPreviewSubmissionResult::Published;
  }

  bool complete = false;
  prepared_ = build_inert_generation(lights, complete);
  if (!complete || prepared_ == nullptr) {
    discard_prepared();
    return ToolPreviewSubmissionResult::Failed;
  }
  pending_kind_ = PendingKind::Rebuild;
  return ToolPreviewSubmissionResult::Published;
}

void ToolPreviewLightCache::publish_prepared() {
  switch (pending_kind_) {
  case PendingKind::None:
  case PendingKind::Unchanged:
    break;
  case PendingKind::RetainedUpdate:
    apply_updates(pending_submission_);
    submitted_ = std::move(pending_submission_);
    ++transform_update_count_;
    break;
  case PendingKind::Grow:
    if (current_ != nullptr && prepared_ != nullptr &&
        current_->pools.size() == prepared_->pools.size()) {
      for (std::size_t index = 0u; index < current_->pools.size(); ++index) {
        LightPool &target = current_->pools[index];
        LightPool &growth = prepared_->pools[index];
        target.proxies.reserve(target.proxies.size() +
                               growth.proxies.size());
        for (LightProxy &proxy : growth.proxies) {
          target.proxies.push_back(std::move(proxy));
        }
        growth.proxies.clear();
      }
      apply_updates(pending_submission_);
      submitted_ = std::move(pending_submission_);
      ++transform_update_count_;
    }
    break;
  case PendingKind::Rebuild: {
    set_generation_intensity(*prepared_, true);
    std::unique_ptr<Generation> retired = std::move(current_);
    current_ = std::move(prepared_);
    submitted_ = std::move(pending_submission_);
    destroy_generation(std::move(retired));
    ++rebuild_count_;
    break;
  }
  }
  destroy_generation(std::move(prepared_));
  pending_submission_.clear();
  pending_kind_ = PendingKind::None;
}

void ToolPreviewLightCache::discard_prepared() {
  destroy_generation(std::move(prepared_));
  pending_submission_.clear();
  pending_kind_ = PendingKind::None;
}

void ToolPreviewLightCache::clear() {
  if (!initialized()) {
    return;
  }
  if (prepare({}) != ToolPreviewSubmissionResult::Failed) {
    publish_prepared();
  }
}

std::size_t ToolPreviewLightCache::proxy_count() const noexcept {
  if (current_ == nullptr) {
    return 0u;
  }
  std::size_t count = 0u;
  for (const LightPool &pool : current_->pools) {
    count += pool.proxies.size();
  }
  return count;
}

bool ToolPreviewLightCache::same_structure(
    const std::vector<RenderToolPreviewLightRecord> &lights) const {
  if (current_ == nullptr || submitted_.size() != lights.size() ||
      current_->pools.size() != lights.size()) {
    return false;
  }
  for (std::size_t index = 0u; index < lights.size(); ++index) {
    const RenderToolPreviewLightRecord &before = submitted_[index];
    const RenderToolPreviewLightRecord &after = lights[index];
    const LightPool &pool = current_->pools[index];
    if (before.source_entity != after.source_entity ||
        before.visibility_entity != after.visibility_entity ||
        !filament_light_structure_matches(before.light, after.light) ||
        before.layout_key != after.layout_key ||
        pool.source_entity != after.source_entity ||
        !filament_light_structure_matches(pool.light, after.light) ||
        pool.layout_key != after.layout_key) {
      return false;
    }
  }
  return true;
}

bool ToolPreviewLightCache::same_submission(
    const std::vector<RenderToolPreviewLightRecord> &lights) const {
  if (!same_structure(lights)) {
    return false;
  }
  for (std::size_t record_index = 0u; record_index < lights.size();
       ++record_index) {
    if (!filament_light_properties_match(submitted_[record_index].light,
                                          lights[record_index].light)) {
      return false;
    }
    const auto &before = submitted_[record_index].instance_transforms;
    const auto &after = lights[record_index].instance_transforms;
    if (before.size() != after.size()) {
      return false;
    }
    for (std::size_t transform_index = 0u; transform_index < after.size();
         ++transform_index) {
      if (!same_mat4(before[transform_index], after[transform_index])) {
        return false;
      }
    }
  }
  return true;
}

bool ToolPreviewLightCache::create_inert_proxy(
    const RenderToolPreviewLightRecord &record,
    const QMat4 &world_transform, LightProxy &proxy) {
  proxy = LightProxy{
      .entity = utils::EntityManager::get().create(),
      .source_entity = record.source_entity,
      .light = record.light,
      .world_transform = world_transform,
  };
  filament::LightManager::Builder builder(
      filament_light_type(record.light.type));
  builder.color({record.light.color.x, record.light.color.y,
                 record.light.color.z})
      .intensity(0.0f)
      .position(filament_light_position(world_transform))
      .direction(filament_light_direction(world_transform))
      .falloff(filament_light_falloff(record.light))
      .distanceAttenuation(record.light.distance_attenuation)
      .castShadows(record.light.casts_shadows);
  builder.build(*engine_, proxy.entity);
  if (!engine_->getLightManager().getInstance(proxy.entity)) {
    destroy_proxy(proxy);
    return false;
  }
  scene_->addEntity(proxy.entity);
  ++created_proxy_count_;
  return true;
}

std::unique_ptr<ToolPreviewLightCache::Generation>
ToolPreviewLightCache::build_inert_generation(
    const std::vector<RenderToolPreviewLightRecord> &lights, bool &complete) {
  complete = false;
  auto generation = std::make_unique<Generation>();
  generation->pools.reserve(lights.size());
  for (const RenderToolPreviewLightRecord &record : lights) {
    LightPool pool{
        .source_entity = record.source_entity,
        .light = record.light,
        .layout_key = record.layout_key,
        .active_count = record.instance_transforms.size(),
    };
    pool.proxies.reserve(record.instance_transforms.size());
    for (const QMat4 &world_transform : record.instance_transforms) {
      LightProxy proxy;
      if (!create_inert_proxy(record, world_transform, proxy)) {
        generation->pools.push_back(std::move(pool));
        destroy_generation(std::move(generation));
        return nullptr;
      }
      pool.proxies.push_back(std::move(proxy));
    }
    generation->pools.push_back(std::move(pool));
  }
  complete = true;
  return generation;
}

std::unique_ptr<ToolPreviewLightCache::Generation>
ToolPreviewLightCache::build_inert_growth(
    const std::vector<RenderToolPreviewLightRecord> &lights, bool &complete) {
  complete = false;
  if (current_ == nullptr || current_->pools.size() != lights.size()) {
    return nullptr;
  }
  auto growth = std::make_unique<Generation>();
  growth->pools.reserve(lights.size());
  for (std::size_t record_index = 0u; record_index < lights.size();
       ++record_index) {
    const RenderToolPreviewLightRecord &record = lights[record_index];
    const std::size_t retained =
        current_->pools[record_index].proxies.size();
    LightPool pool{
        .source_entity = record.source_entity,
        .light = record.light,
        .layout_key = record.layout_key,
        .active_count = record.instance_transforms.size(),
    };
    if (record.instance_transforms.size() > retained) {
      pool.proxies.reserve(record.instance_transforms.size() - retained);
      for (std::size_t transform_index = retained;
           transform_index < record.instance_transforms.size();
           ++transform_index) {
        LightProxy proxy;
        if (!create_inert_proxy(record,
                                record.instance_transforms[transform_index],
                                proxy)) {
          growth->pools.push_back(std::move(pool));
          destroy_generation(std::move(growth));
          return nullptr;
        }
        pool.proxies.push_back(std::move(proxy));
      }
    }
    growth->pools.push_back(std::move(pool));
  }
  complete = true;
  return growth;
}

void ToolPreviewLightCache::apply_updates(
    const std::vector<RenderToolPreviewLightRecord> &lights) {
  if (current_ == nullptr || current_->pools.size() != lights.size()) {
    return;
  }
  filament::LightManager &manager = engine_->getLightManager();
  for (std::size_t record_index = 0u; record_index < lights.size();
       ++record_index) {
    const RenderToolPreviewLightRecord &record = lights[record_index];
    LightPool &pool = current_->pools[record_index];
    pool.active_count = record.instance_transforms.size();
    pool.light = record.light;
    for (std::size_t proxy_index = 0u; proxy_index < pool.proxies.size();
         ++proxy_index) {
      LightProxy &proxy = pool.proxies[proxy_index];
      const filament::LightManager::Instance instance =
          manager.getInstance(proxy.entity);
      if (!instance) {
        continue;
      }
      const bool enabled = proxy_index < record.instance_transforms.size();
      update_filament_light_properties(manager, instance, proxy.light,
                                       record.light, proxy.enabled, enabled);
      proxy.light = record.light;
      proxy.enabled = enabled;
      if (!enabled) {
        continue;
      }
      const QMat4 &world_transform =
          record.instance_transforms[proxy_index];
      if (!same_mat4(proxy.world_transform, world_transform)) {
        manager.setPosition(instance,
                            filament_light_position(world_transform));
        manager.setDirection(instance,
                             filament_light_direction(world_transform));
        proxy.world_transform = world_transform;
      }
    }
  }
}

void ToolPreviewLightCache::set_generation_intensity(Generation &generation,
                                                     bool enabled) {
  filament::LightManager &manager = engine_->getLightManager();
  for (LightPool &pool : generation.pools) {
    for (std::size_t proxy_index = 0u; proxy_index < pool.proxies.size();
         ++proxy_index) {
      LightProxy &proxy = pool.proxies[proxy_index];
      const filament::LightManager::Instance instance =
          manager.getInstance(proxy.entity);
      if (instance) {
        proxy.enabled = enabled && proxy_index < pool.active_count;
        manager.setIntensity(
            instance,
            proxy.enabled ? filament_light_intensity(proxy.light) : 0.0f);
      }
    }
  }
}

void ToolPreviewLightCache::destroy_generation(
    std::unique_ptr<Generation> generation) {
  if (generation == nullptr) {
    return;
  }
  for (LightPool &pool : generation->pools) {
    for (LightProxy &proxy : pool.proxies) {
      destroy_proxy(proxy);
    }
  }
}

void ToolPreviewLightCache::destroy_proxy(LightProxy &proxy) {
  if (engine_ == nullptr || !proxy.entity) {
    return;
  }
  scene_->remove(proxy.entity);
  engine_->getLightManager().destroy(proxy.entity);
  utils::EntityManager::get().destroy(proxy.entity);
  proxy.entity.clear();
}
} // namespace quader
