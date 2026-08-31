////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/pivot/pivot_types.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "math/types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace quader {

class AssetRegistry;
class SceneDocument;
class ViewportCameraController;

/**
 * Identifies the geometric source used to resolve a viewport point placement.
 */
enum class ViewportPointSnapKind {
  None,
  Vertex,
  EdgeMidpoint,
  FaceCenter,
  Surface,
  Grid,
};

/**
 * Defines an infinite world-space placement plane and its grid axes.
 *
 * The axes need not be unit length, but must be finite, nonzero, and linearly
 * independent. Their cross product defines the positive plane normal.
 */
struct ViewportPointPlacementPlane {
  QVec3 origin = {};
  QVec3 u_axis = {1.0f, 0.0f, 0.0f};
  QVec3 v_axis = {0.0f, 0.0f, -1.0f};
};

/**
 * Selects the candidate classes and resolution rules for one placement use.
 */
struct ViewportPointPlacementPolicy {
  bool vertex = true;
  bool edge_midpoint = true;
  bool face_center = true;
  bool surface = false;
  bool grid = true;
  bool prefer_surface_normal_for_features = false;
  bool strict_kind_priority = false;
  bool grid_requires_snap_radius = true;
  float snap_radius_pixels = 14.0f;

  [[nodiscard]] static ViewportPointPlacementPolicy
  pivot(PivotSnapSettings settings);
  [[nodiscard]] static ViewportPointPlacementPolicy radial_array();
};

/**
 * Describes one pointer-to-world placement query.
 */
struct ViewportPointPlacementRequest {
  QVec2 mouse_local = {};
  ViewportPointPlacementPlane active_grid = {};
  float grid_size = 1.0f;
  ViewportPointPlacementPolicy policy = {};

  // Pivot supplies a horizontal plane through the current pivot. When no snap
  // candidate is found, the ray intersection (or plane origin when parallel)
  // preserves the existing free-drag behavior. Radial Array leaves this empty.
  std::optional<ViewportPointPlacementPlane> fallback_plane;
};

/**
 * Stores a resolved world-space placement and its geometric provenance.
 */
struct ViewportPointPlacementResult {
  bool valid = false;
  QVec3 world_position = {};
  QVec3 world_normal = {0.0f, 1.0f, 0.0f};
  ViewportPointSnapKind snap_kind = ViewportPointSnapKind::None;
  EntityId entity = kInvalidEntityId;
};

/**
 * Returns the canonical modeling plane represented by the active viewport
 * grid. Perspective resolves to XZ; locked orthographic cameras resolve to the
 * corresponding XY, YZ, or XZ plane.
 */
[[nodiscard]] ViewportPointPlacementPlane
viewport_active_grid_placement_plane(const ViewportCameraController &camera);

/**
 * Owns viewport point placement policy over the canonical scene surface index.
 *
 * A SceneSession injects its shared frozen participant/TLAS snapshot. Standalone
 * tests and tools lazily construct the same index rather than maintaining a
 * second triangle pipeline. Only camera projection remains service-local.
 */
class ViewportPointPlacementService {
public:
  ViewportPointPlacementService() = default;
  explicit ViewportPointPlacementService(SceneSurfaceQuery &surface_query);

  void set_viewport_visibility_filter(
      std::function<bool(EntityId)> filter,
      std::function<std::uint64_t()> revision_provider);

  [[nodiscard]] ViewportPointPlacementResult
  resolve(const SceneDocument &document, const AssetRegistry &assets,
          const ViewportCameraController &camera,
          const ViewportPointPlacementRequest &request);

  /**
   * Freezes the canonical render-visible surface participants for one input
   * operation. Viewport interactions can share this snapshot for occlusion
   * without receiving SceneSession or constructing a second query.
   */
  [[nodiscard]] SceneSurfaceQuerySnapshot
  surface_snapshot(const SceneDocument &document,
                   const AssetRegistry &assets);

  void invalidate();

  [[nodiscard]] SceneSurfaceQueryStats surface_query_stats() const noexcept;

private:
  /// Stores one camera-projected landmark behind the projection gate.
  struct CachedProjection {
    QVec2 screen_position = {};
    bool visible = false;
  };

  void refresh_projection_cache(
      const ViewportCameraController &camera,
      const SceneSurfaceQuerySnapshot &surface_snapshot);

  SceneSurfaceQuery *shared_surface_query_ = nullptr;
  std::unique_ptr<SceneSurfaceQuery> standalone_surface_query_;
  const SceneDocument *standalone_document_ = nullptr;
  const AssetRegistry *standalone_assets_ = nullptr;
  SceneSurfaceQuerySnapshot cached_surface_snapshot_;
  std::uint64_t cached_surface_revision_ = 0u;
  const ViewportCameraController *cached_projection_camera_ = nullptr;
  std::uint64_t cached_projection_signature_ = 0;
  std::vector<CachedProjection> projected_landmarks_;
  std::function<bool(EntityId)> viewport_visibility_filter_;
  std::function<std::uint64_t()> viewport_visibility_revision_provider_;
  SceneSurfaceQueryScratch raycast_scratch_;
};

} // namespace quader
