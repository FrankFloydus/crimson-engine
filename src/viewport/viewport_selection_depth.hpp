////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace quader {

inline constexpr std::size_t kViewportSelectionDepthLayers = 4u;

/// Freezes the continuous projection used by selection, independently of a view.
struct ViewportSelectionDepthCamera final {
  QVec3 origin{};
  QVec3 right{1.0f, 0.0f, 0.0f};
  QVec3 up{0.0f, 1.0f, 0.0f};
  QVec3 forward{0.0f, 0.0f, -1.0f};
  QVec2 viewport_size{};
  float vertical_fov_degrees = 60.0f;
  float orthographic_size = 10.0f;
  bool orthographic = false;
  bool supported = true;

  [[nodiscard]] bool operator==(const ViewportSelectionDepthCamera &other) const noexcept;
};

/// Identifies one exact frozen occluder; IDs are one-based positions in this list.
struct ViewportSelectionDepthTriangle final {
  QVec3 a{};
  QVec3 b{};
  QVec3 c{};
};

/// Pins a derived GPU projection of the surface-query owner's immutable triangles.
struct ViewportSelectionDepthGeometry final {
  std::vector<ViewportSelectionDepthTriangle> triangles;
  double transform_arithmetic_magnitude = 0.0;
  bool conservative_transforms = true;
};

/// Pins a completed conservative raster; it proposes candidates, never selection.
struct ViewportSelectionDepthSnapshot final {
  std::shared_ptr<const ViewportSelectionDepthGeometry> geometry;
  ViewportSelectionDepthCamera camera;
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;
  std::array<std::vector<std::array<float, 4u>>, kViewportSelectionDepthLayers> layers;
  double camera_depth_error = 0.0;
  bool conservative_coverage = false;
  bool readback_top_left = false;

  [[nodiscard]] bool matches(const ViewportSelectionDepthGeometry *source,
                             const ViewportSelectionDepthCamera &query_camera) const noexcept;
};

/// Shares completed immutable readbacks without retaining a renderer or scene owner.
class ViewportSelectionDepthSource {
public:
  virtual ~ViewportSelectionDepthSource() = default;
  [[nodiscard]] virtual std::shared_ptr<const ViewportSelectionDepthSnapshot>
  latest() const = 0;
};

/// Distinguishes proven GPU certificates from queries requiring the exact CPU path.
enum class ViewportSelectionDepthDecision : std::uint8_t {
  Accepted,
  Rejected,
  Overflow,
  Unavailable,
};

/// Supplies the authoritative owner's scalar intersection for one proposed triangle.
struct ViewportSelectionDepthIntersection final {
  const void *owner = nullptr;
  std::optional<float> (*intersect)(const void *, std::uint32_t, QVec3, QVec3) = nullptr;
};

/// Counts acceleration decisions separately from exact CPU fallbacks.
struct ViewportSelectionDepthQueryStats final {
  std::uint64_t gpu_accepted_witnesses = 0u;
  std::uint64_t gpu_rejected_witnesses = 0u;
  std::uint64_t gpu_overflow_witnesses = 0u;
  std::uint64_t cpu_fallback_witnesses = 0u;
  std::uint64_t stale_snapshot_queries = 0u;
};

/// Verifies up to four GPU candidates against the exact ray and conservative tail bound.
[[nodiscard]] ViewportSelectionDepthDecision classify_viewport_selection_depth(
    const ViewportSelectionDepthSnapshot &snapshot, QVec2 screen_position,
    QVec3 ray_origin, QVec3 ray_direction, double witness_distance,
    double occlusion_tolerance, ViewportSelectionDepthIntersection intersection);

} // namespace quader
