////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/viewport_overlay_frame.hpp"

#include <cstdint>

namespace quader {

/// Renderer-facing selection category; native modeling modes are adapted above render.
enum class RenderOverlaySelectionMode : std::uint8_t {
  Object,
  Group,
  Component,
};

/**
 * Builds scene-derived viewport overlay geometry for renderer submission.
 */
class ViewportSceneOverlayBuilder final {
public:
  [[nodiscard]] static ViewportOverlayFrame
  build_selection_frame(ViewportOverlayFrame base_frame,
                        const RenderOverlaySelectionSnapshot &snapshot,
                        bool point_light_gizmos_visible = false,
                        RenderOverlaySelectionMode selection_mode =
                            RenderOverlaySelectionMode::Object,
                        bool include_tool_overlays = true,
                        bool selection_overlays_visible = true);
  [[nodiscard]] static ViewportOverlayFrame build_wireframe_frame(
      ViewportOverlayFrame base_frame,
      RenderOverlaySelectionMode selection_mode,
      const RenderOverlaySceneWireframeSnapshot &snapshot);
  [[nodiscard]] static ViewportOverlayFrame
  strip_persistent_object_selection_fills(
      ViewportOverlayFrame frame, RenderOverlaySelectionMode selection_mode);
  [[nodiscard]] static ViewportOverlayFrame
  strip_wireframe_selection_fills(ViewportOverlayFrame frame,
                                  RenderOverlaySelectionMode selection_mode);
};

} // namespace quader
