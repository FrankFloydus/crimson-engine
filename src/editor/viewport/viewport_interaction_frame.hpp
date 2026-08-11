////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"
#include "core/types.hpp"
#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_settings.hpp"

#include <span>

namespace quader {

class ViewportCameraController;

/**
 * Stores a selectable scene billboard projected by the viewport interaction layer.
 */
struct ViewportSceneBillboardPickCandidate {
  EntityId entity = kInvalidEntityId;
  QVec3 position = {};
  float size_pixels = 1.0f;
};

/**
 * Captures camera-independent selection inputs used by transform gizmos.
 * Revisions let the interaction layer retain an expensive world-space pivot
 * while camera-only frames continue to rebuild projected gizmo geometry.
 */
struct ViewportInteractionSelectionState {
  QuaderSelectionSummary summary;
  std::uint64_t scene_revision = 0;
  std::uint64_t asset_revision = 0;
  std::uint64_t pivot_revision = 0;
};

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
  QVec2 right_click_local = {};
  int right_click_count = 1;
  bool right_pressed = false;
  bool right_down = false;
  bool right_released = false;
  double double_click_interval_seconds = 0.5;
  double monotonic_time_seconds = 0.0;
  bool active_tool_allows_selection_fallback = false;
  std::span<const ViewportSceneBillboardPickCandidate>
      scene_billboard_pick_candidates;
  ViewportInteractionSelectionState selection_state;
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
