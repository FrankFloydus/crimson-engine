////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/viewport_overlay_frame.hpp"

namespace quader {

/**
 * Builds scene-derived viewport overlay geometry for renderer submission.
 */
class ViewportSceneOverlayBuilder final {
public:
  [[nodiscard]] static ViewportOverlayFrame
  build_selection_frame(ViewportOverlayFrame base_frame,
                        const RenderOverlaySelectionSnapshot &snapshot,
                        bool point_light_gizmos_visible = false);
  [[nodiscard]] static ViewportOverlayFrame build_wireframe_frame(
      ViewportOverlayFrame base_frame, ModelingSelectionMode selection_mode,
      const RenderOverlaySceneWireframeSnapshot &snapshot);
  [[nodiscard]] static ViewportOverlayFrame
  strip_persistent_object_selection(ViewportOverlayFrame frame,
                                    ModelingSelectionMode selection_mode);
  [[nodiscard]] static ViewportOverlayFrame
  strip_wireframe_selection_fills(ViewportOverlayFrame frame,
                                  ModelingSelectionMode selection_mode);
};

} // namespace quader
