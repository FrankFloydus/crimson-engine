////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/viewport/viewport_interaction_frame.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <optional>
#include <vector>

namespace quader {

class SceneDocument;
class AssetRegistry;
class ModelingSessionService;
class ViewportLocalVisibilitySnapshot;

/**
 * Projects scene-owned lights and bounds into neutral viewport inputs.
 */
class ViewportSceneProjection final {
public:
  [[nodiscard]] static std::vector<ViewportSceneBillboardPickCandidate>
  point_light_pick_candidates(
      const SceneDocument &document,
      const ViewportLocalVisibilitySnapshot &local_visibility,
      float viewport_pixel_scale = 1.0f);
  [[nodiscard]] static std::optional<ViewportFocusBounds> world_bounds(
      const SceneDocument &document, const AssetRegistry &assets,
      const ViewportLocalVisibilitySnapshot &local_visibility);
  [[nodiscard]] static std::optional<ViewportFocusBounds>
  selection_bounds(const ModelingSessionService &modeling);
};

} // namespace quader
