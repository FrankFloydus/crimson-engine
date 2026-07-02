////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_settings.hpp"

namespace quader {

class ViewportCameraController;

/**
 * Stores the editor-owned viewport interaction input for one viewport frame.
 */
struct ViewportInteractionFrame {
  ViewportInputRouter &input_router;
  ViewportCameraController &camera;
  const ViewportSettings &settings;
  const ViewportInputFrame &input;
  const ViewportInputResult &input_result;
  QVec2 left_click_local = {};
  int left_click_count = 1;
  bool left_pressed = false;
  bool left_down = false;
  bool left_released = false;
};

/**
 * Stores pre-tool viewport interaction consumption state.
 */
struct ViewportBeforeToolInteractionResult {
  bool gizmo_consumed = false;
  bool selection_click_requested = false;
  QVec2 selection_click_local = {};
  int selection_click_count = 1;
};

} // namespace quader
