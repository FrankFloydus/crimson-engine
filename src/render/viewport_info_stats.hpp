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
