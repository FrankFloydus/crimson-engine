////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "render/viewport_overlay_frame.hpp"

#include <cstdint>

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

ViewportInfoSceneStats
collect_viewport_info_scene_stats(const SceneDocument &document,
                                  const AssetRegistry &assets,
                                  const ViewportOverlayCamera &camera);

} // namespace quader
