////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <imgui.h>

namespace quader {

class LocalizationService;

/**
 * Owns Modal Tool Overlay Renderer behavior within the ImGui editor presentation layer.
 */
class ModalToolOverlayRenderer {
public:
  void draw_box_preview(ImDrawList *draw_list,
                        const ModelingBoxPreview &preview,
                        const ViewportCameraController &camera, ImVec2 item_min,
                        ImVec2 item_size) const;

  void draw_fly_navigation_overlay(
      ImDrawList *draw_list, ImVec2 item_min, ImVec2 item_size,
      bool draw_crosshair, bool draw_speed, bool draw_reset_hint, float speed,
      const LocalizationService &localization) const;
};

} // namespace quader
