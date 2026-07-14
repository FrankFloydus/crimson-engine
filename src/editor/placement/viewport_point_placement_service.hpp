////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/pivot/pivot_types.hpp"
#include "math/types.hpp"

#include <cstdint>
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
  bool include_effectively_locked_meshes = true;
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
 * Owns native scene landmark extraction and viewport point placement policy.
 *
 * World-space landmarks and triangles are cached until either the scene or
 * asset registry revision changes. Projection and ray work remain per-query so
 * camera changes cannot reuse stale screen-space data.
 */
class ViewportPointPlacementService {
public:
  [[nodiscard]] ViewportPointPlacementResult
  resolve(const SceneDocument &document, const AssetRegistry &assets,
          const ViewportCameraController &camera,
          const ViewportPointPlacementRequest &request);

  void invalidate();

private:
  /// Stores one world-space feature candidate behind scene/asset gates.
  struct CachedLandmark {
    QVec3 world_position = {};
    QVec3 incident_world_normal = {};
    ViewportPointSnapKind kind = ViewportPointSnapKind::None;
    EntityId entity = kInvalidEntityId;
    bool effectively_locked = false;
  };

  /// Stores one world-space pick triangle behind scene/asset gates.
  struct CachedTriangle {
    QVec3 a = {};
    QVec3 b = {};
    QVec3 c = {};
    QVec3 world_normal = {};
    EntityId entity = kInvalidEntityId;
    bool effectively_locked = false;
  };

  /// Stores one camera-projected landmark behind the projection gate.
  struct CachedProjection {
    QVec2 screen_position = {};
    bool visible = false;
  };

  void refresh_cache(const SceneDocument &document,
                     const AssetRegistry &assets);
  void refresh_projection_cache(const ViewportCameraController &camera);

  const SceneDocument *cached_document_ = nullptr;
  const AssetRegistry *cached_assets_ = nullptr;
  std::uint64_t cached_scene_revision_ = 0;
  std::uint64_t cached_asset_revision_ = 0;
  bool cache_valid_ = false;
  std::vector<CachedLandmark> landmarks_;
  std::vector<CachedTriangle> triangles_;
  const ViewportCameraController *cached_projection_camera_ = nullptr;
  std::uint64_t cached_projection_signature_ = 0;
  std::vector<CachedProjection> projected_landmarks_;
};

} // namespace quader
