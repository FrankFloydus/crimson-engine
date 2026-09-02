////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/curve_solid_geometry_cache.hpp"

#include "assets/modeling_mesh_payload_adapter.hpp"

#include <quader/modeling/curves/curve_tube.hpp>

#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>
#include <utility>

namespace quader {

CurveSolidGeometryCache::CurveSolidGeometryCache(
    std::size_t capacity, std::size_t byte_capacity) noexcept
    : capacity_(std::max<std::size_t>(capacity, 1u)),
      byte_capacity_(std::max<std::size_t>(byte_capacity, 1u)) {}

std::shared_ptr<const MeshGeometryPayload> CurveSolidGeometryCache::geometry(
    const modeling::CurveDocument &document, double radius,
    std::uint32_t radial_resolution,
    modeling::CurveSamplingOptions sampling) {
  const Key key{
      .content_identity = document.content_identity(),
      .content_revision = document.content_revision(),
      .radius_bits = std::bit_cast<std::uint64_t>(radius),
      .radial_resolution = radial_resolution,
      .tolerance_bits = std::bit_cast<std::uint64_t>(sampling.tolerance),
      .max_depth = sampling.max_depth,
      .max_samples = sampling.max_samples,
  };
  const auto found =
      std::ranges::find(entries_, key, &Entry::key);
  if (found != entries_.end()) {
    ++hit_count_;
    found->last_use = ++use_sequence_;
    return found->geometry;
  }

  ++build_count_;
  std::shared_ptr<const MeshGeometryPayload> geometry;
  modeling::Result<modeling::MeshPayload> result =
      modeling::tessellate_curve_tube(
          document,
          {.radius = radius,
           .radial_resolution = radial_resolution,
           .sampling = sampling});
  if (result.ok()) {
    try {
      if (std::optional<MeshGeometryPayload> payload =
              mesh_geometry_payload_from_modeling_payload(result.value())) {
        geometry =
            std::make_shared<MeshGeometryPayload>(std::move(*payload));
      }
    } catch (const std::bad_alloc &) {
      geometry.reset();
    } catch (const std::length_error &) {
      geometry.reset();
    }
  }
  if (!geometry) {
    ++failure_count_;
  }

  const std::size_t geometry_bytes = geometry != nullptr
                                         ? geometry->byte_size()
                                         : 0u;
  if (geometry_bytes > byte_capacity_) {
    ++oversize_bypass_count_;
    return geometry;
  }
  while (!entries_.empty() &&
         (entries_.size() >= capacity_ ||
          geometry_bytes > byte_capacity_ - cached_bytes_)) {
    const auto least_recent = std::ranges::min_element(
        entries_, {}, &Entry::last_use);
    cached_bytes_ -= least_recent->bytes;
    entries_.erase(least_recent);
    ++eviction_count_;
  }
  entries_.push_back({
      .key = key,
      .geometry = geometry,
      .bytes = geometry_bytes,
      .last_use = ++use_sequence_,
  });
  cached_bytes_ += geometry_bytes;
  return geometry;
}

void CurveSolidGeometryCache::clear() noexcept {
  entries_.clear();
  cached_bytes_ = 0u;
  use_sequence_ = 0u;
}

CurveSolidGeometryCacheStats CurveSolidGeometryCache::stats() const noexcept {
  return {
      .build_count = build_count_,
      .hit_count = hit_count_,
      .failure_count = failure_count_,
      .eviction_count = eviction_count_,
      .oversize_bypass_count = oversize_bypass_count_,
      .entry_count = entries_.size(),
      .cached_bytes = cached_bytes_,
  };
}

} // namespace quader
