////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_render_api.hpp"
#include "render/viewport_overlay_frame.hpp"

#include <cstddef>
#include <cstdint>

namespace quader {

class SceneRenderBridge;

/**
 * Provides runtime-only access to render synchronization without exposing render resources on the public API.
 */
class QuaderRenderApiRuntimeAccess final {
public:
  static QuaderApiVoidResult sync_scene(QuaderRenderApi &api,
                                        SceneRenderBridge &bridge);
  static QuaderApiVoidResult sync_tool_previews(QuaderRenderApi &api,
                                                SceneRenderBridge &bridge);
  static QuaderApiResult<QuaderRenderOverlaySubmission> sync_selection_overlay(
      QuaderRenderApi &api, SceneRenderBridge &bridge,
      std::size_t viewport_index, std::uint8_t layer_mask,
      ViewportOverlayCamera camera,
      const ViewportOverlayFrame *tool_overlay_frame = nullptr);
  static QuaderApiVoidResult
  set_selection_overlay(QuaderRenderApi &api, SceneRenderBridge &bridge,
                        std::size_t viewport_index, std::uint8_t layer_mask,
                        ViewportOverlayCamera camera);
};

} // namespace quader
