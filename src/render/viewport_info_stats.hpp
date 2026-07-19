////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "scene/scene_bounds.hpp"
#include "viewport/viewport_local_visibility.hpp"

#include <cstdint>
#include <vector>

namespace quader {

class AssetRegistry;
class SceneDocument;

/**
 * Stores the scene-visible counters presented by the View > Show Info viewport overlay.
 */
struct ViewportInfoSceneStats {
  std::uint64_t objects = 0;
  std::uint64_t vertices = 0;
  std::uint64_t edges = 0;
  std::uint64_t faces = 0;
  std::uint64_t triangles = 0;
  std::uint64_t primitives = 0;
  std::uint64_t draw_calls = 0;
};

/**
 * Stores camera-independent submission facts for one renderable scene mesh.
 *
 * Building these records may traverse the scene hierarchy, resolve assets,
 * coalesce material spans, and transform local bounds. Retaining them lets a
 * camera-only viewport update perform just frustum tests and counter sums.
 */
struct ViewportInfoRenderableStats {
  SceneBounds world_bounds = {};
  SceneBounds local_bounds = {};
  EntityId source_entity = kInvalidEntityId;
  EntityId instance_definition = kInvalidEntityId;
  EntityId instance_occurrence = kInvalidEntityId;
  std::uint64_t vertices = 0;
  std::uint64_t edges = 0;
  std::uint64_t faces = 0;
  std::uint64_t triangles = 0;
  std::uint64_t primitives = 0;
  std::uint64_t draw_calls = 0;
};

/// Camera-independent input for the viewport info visibility counters.
struct ViewportInfoSceneSnapshot {
  std::vector<ViewportInfoRenderableStats> renderables;
};

ViewportInfoSceneSnapshot build_viewport_info_scene_snapshot(
    const SceneDocument &document, const AssetRegistry &assets,
    const ViewportLocalVisibilitySnapshot *local_visibility = nullptr);

[[nodiscard]] bool try_refresh_viewport_info_scene_snapshot_transforms(
    ViewportInfoSceneSnapshot &snapshot, const SceneDocument &document);

ViewportInfoSceneStats collect_viewport_info_scene_stats(
    const ViewportInfoSceneSnapshot &snapshot,
    const ViewportOverlayCamera &camera);

ViewportInfoSceneStats
collect_viewport_info_scene_stats(const SceneDocument &document,
                                  const AssetRegistry &assets,
                                  const ViewportOverlayCamera &camera,
                                  const ViewportLocalVisibilitySnapshot *
                                      local_visibility = nullptr);

} // namespace quader
