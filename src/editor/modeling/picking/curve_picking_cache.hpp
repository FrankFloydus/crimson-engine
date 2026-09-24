////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/interactions/selection_box.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "math/types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace quader_editor_core {
struct ViewportCameraState;
}

namespace quader {

namespace modeling {
class CurveDocument;
}

class AssetRegistry;
class SceneDocument;
class ViewportLocalVisibilitySnapshot;
class ViewportCurveTubeProjectionCache;
struct CurveComponent;
struct CurveSamplePayload;

/// Stores one local-space triangle from a derived solidified Curve tube.
struct ViewportCurveTubeTriangle final {
  QVec3 a{};
  QVec3 b{};
  QVec3 c{};
};

/// Reports the nearest exact tube surface hit in world-ray coordinates.
struct ViewportCurveTubeRayHit final {
  QVec3 world_position{};
  double ray_distance = 0.0;
  std::size_t primitive_index = 0u;
};

/// Supplies camera and optional mesh-occlusion policy for one tube projection.
struct ViewportCurveTubeProjectionRequest final {
  const quader_editor_core::ViewportCameraState &camera;
  double near_plane = 0.01;
  double far_plane = 1000000.0;
  /// Empty means every depth-clipped tube point is visible.
  std::function<bool(QVec3)> world_point_visible;
  /// Required to cache projections that use world_point_visible.
  std::optional<std::uint64_t> visibility_revision;
};

/// Carries one tube's projected selection silhouette and stable depth witness.
struct ViewportCurveTubeProjection final {
  quader_editor_core::RectangleProjectedObject projected;
  QVec3 nearest_world_position{};
  double nearest_ray_distance = std::numeric_limits<double>::infinity();
  bool valid = false;
};

/**
 * Pins one Curve tube's local triangles and shared neutral triangle BVH.
 *
 * Candidate-cache rebuilds create this payload once. Pointer queries only
 * transform the ray and traverse the retained BLAS; they never retessellate
 * the Curve or rescan its asset.
 */
class ViewportCurveTubePickPayload final {
public:
  ~ViewportCurveTubePickPayload();

  ViewportCurveTubePickPayload(const ViewportCurveTubePickPayload &) = delete;
  ViewportCurveTubePickPayload &
  operator=(const ViewportCurveTubePickPayload &) = delete;

  [[nodiscard]] static std::shared_ptr<const ViewportCurveTubePickPayload>
  build(std::vector<ViewportCurveTubeTriangle> triangles);

  [[nodiscard]] std::span<const ViewportCurveTubeTriangle>
  triangles() const noexcept {
    return triangles_;
  }
  [[nodiscard]] std::size_t triangle_count() const noexcept {
    return triangles_.size();
  }
  [[nodiscard]] std::size_t estimated_bytes() const noexcept;
  [[nodiscard]] std::uint64_t projection_build_count() const noexcept;
  [[nodiscard]] std::optional<ViewportCurveTubeRayHit> nearest_hit(
      const QMat4 &world_transform, QVec3 normalized_world_origin,
      QVec3 normalized_world_direction, bool include_backfaces,
      double maximum_ray_distance =
          std::numeric_limits<double>::infinity()) const;
  [[nodiscard]] ViewportCurveTubeProjection project(
      const QMat4 &world_transform,
      const ViewportCurveTubeProjectionRequest &request) const;

private:
  class Impl;

  explicit ViewportCurveTubePickPayload(
      std::vector<ViewportCurveTubeTriangle> triangles,
      std::shared_ptr<ViewportCurveTubeProjectionCache> projection_cache,
      std::uint64_t projection_cache_identity,
      std::size_t registered_payload_bytes);
  [[nodiscard]] static std::shared_ptr<const ViewportCurveTubePickPayload>
  build(std::vector<ViewportCurveTubeTriangle> triangles,
        std::shared_ptr<ViewportCurveTubeProjectionCache> projection_cache);
  [[nodiscard]] static std::optional<std::size_t>
  estimated_byte_cost(std::size_t triangle_count) noexcept;

  std::vector<ViewportCurveTubeTriangle> triangles_;
  std::unique_ptr<Impl> impl_;

  friend class ViewportCurvePickCache;
};

/// Pins one selectable authored curve for camera-dependent segment picking.
struct ViewportSceneCurvePickCandidate {
  EntityId entity = kInvalidEntityId;
  QMat4 world_transform;
  /// Pins authored anchors so drag-time snap queries never rescan assets.
  std::shared_ptr<const modeling::CurveDocument> document;
  std::shared_ptr<const CurveSamplePayload> sample_payload;
  /// True only when a usable derived tube payload exists. A Curve whose
  /// solidification cannot be derived retains canonical sampled-stroke picking.
  bool solidified = false;
  std::shared_ptr<const ViewportCurveTubePickPayload> tube_payload;
};

/**
 * Projects the retained tube triangles for canonical contain/crossing tests.
 *
 * This is shared by public/API box selection and live viewport marquee
 * arbitration. It never tessellates the Curve and can optionally apply the
 * viewport's existing render-surface visibility policy to projection samples.
 */
[[nodiscard]] ViewportCurveTubeProjection project_viewport_curve_tube(
    const ViewportSceneCurvePickCandidate &candidate,
    const ViewportCurveTubeProjectionRequest &request);

/// Owns filtered scene/asset candidates and cached curve click/marquee inputs.
class ViewportCurvePickCache final {
public:
  ViewportCurvePickCache();

  ViewportCurvePickCache(const ViewportCurvePickCache &) = delete;
  ViewportCurvePickCache &operator=(const ViewportCurvePickCache &) = delete;
  ViewportCurvePickCache(ViewportCurvePickCache &&) = delete;
  ViewportCurvePickCache &operator=(ViewportCurvePickCache &&) = delete;

  /// Borrows candidates until the next source change or reset.
  [[nodiscard]] std::span<const ViewportSceneCurvePickCandidate> candidates(
      const SceneDocument &document, const AssetRegistry &assets,
      const ViewportLocalVisibilitySnapshot &local_visibility);
  [[nodiscard]] std::span<const ViewportSceneCurvePickCandidate> candidates(
      const SceneDocument &document, const AssetRegistry &assets,
      std::uint64_t visibility_revision,
      const std::function<bool(EntityId)> &entity_visible,
      bool instance_edit_active = false);
  /// Resolves the same contain/crossing rules as meshes without a polygon proxy.
  [[nodiscard]] std::vector<EntityId> box_pick_entities(
      const SceneDocument &document, const AssetRegistry &assets,
      const ModelingBoxSelectRequest &request, std::uint64_t visibility_revision,
      const std::function<bool(EntityId)> &entity_visible,
      bool instance_edit_active = false);
  void reset();
  [[nodiscard]] std::uint64_t build_count() const noexcept { return build_count_; }
  [[nodiscard]] std::uint64_t rectangle_build_count() const noexcept {
    return rectangle_build_count_;
  }
  [[nodiscard]] std::uint64_t tube_payload_build_count() const noexcept {
    return tube_payload_build_count_;
  }
  /// Estimated retained BLAS/tube bytes reserved from the aggregate budget.
  [[nodiscard]] std::size_t tube_payload_cached_bytes() const noexcept;
  /// Retained camera-dependent projection bytes across every cached payload.
  [[nodiscard]] std::size_t tube_projection_cached_bytes() const noexcept;
  [[nodiscard]] std::size_t tube_cached_bytes() const noexcept;
  [[nodiscard]] std::size_t tube_cache_byte_capacity() const noexcept;
  [[nodiscard]] std::size_t tube_projection_cache_entry_count() const noexcept;
  [[nodiscard]] std::uint64_t tube_projection_build_count() const noexcept;
  [[nodiscard]] std::uint64_t tube_projection_eviction_count() const noexcept;

private:
  /// Retains one exact solidified Curve payload under the shared pick budget.
  struct TubePayloadCacheEntry final {
    std::uint64_t content_identity = 0u;
    std::uint64_t content_revision = 0u;
    std::uint32_t radius_bits = 0u;
    int resolution = 0;
    std::shared_ptr<const ViewportCurveTubePickPayload> payload;
    std::size_t estimated_bytes = 0u;
    std::uint64_t last_use_sequence = 0u;
  };

  [[nodiscard]] std::shared_ptr<const ViewportCurveTubePickPayload>
  tube_payload_for(const modeling::CurveDocument &document,
                   const CurveComponent &component);

  const SceneDocument *document_ = nullptr;
  const AssetRegistry *assets_ = nullptr;
  std::uint64_t scene_revision_ = 0;
  std::uint64_t asset_revision_ = 0;
  std::uint64_t visibility_revision_ = 0;
  std::uint64_t build_count_ = 0;
  std::vector<ViewportSceneCurvePickCandidate> candidates_;
  bool instance_edit_active_ = false;
  bool valid_ = false;
  std::uint64_t rectangle_source_revision_ = 0;
  std::uint64_t rectangle_settings_revision_ = 0;
  std::uint64_t rectangle_build_count_ = 0;
  ModelingViewportCamera rectangle_camera_;
  quader_editor_core::RectangleProjectedHitsRequest rectangle_request_;
  std::vector<EntityId> rectangle_entities_;
  bool rectangle_valid_ = false;
  std::vector<TubePayloadCacheEntry> tube_payload_cache_;
  std::shared_ptr<ViewportCurveTubeProjectionCache> tube_projection_cache_;
  std::size_t tube_payload_cached_bytes_ = 0u;
  std::uint64_t tube_payload_use_sequence_ = 0u;
  std::uint64_t tube_payload_build_count_ = 0u;
};

} // namespace quader
