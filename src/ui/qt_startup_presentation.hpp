////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>

namespace quader {

/**
 * Describes whether startup geometry should remain hidden or may be revealed.
 */
enum class QtStartupPresentationAction {
  WaitForStableGeometry,
  GeometryStabilized,
  PresentationReady,
};

inline bool qt_startup_native_child_should_be_visible(
    QtStartupPresentationAction action) {
  return action != QtStartupPresentationAction::WaitForStableGeometry;
}

/**
 * Retains an exposed host's physical-size sample while its initial native
 * geometry and child layout settle.
 */
struct QtStartupPresentationState {
  std::uint64_t observed_geometry_signature = 0;
  unsigned int consecutive_equal_samples = 0;
  bool presentation_ready = false;
};

/**
 * Prevents a self-consistent retained layout at the saved normal size from
 * qualifying while the native maximized client already has its final extent.
 */
inline bool qt_startup_client_extents_match(
    int retained_width_pixels, int retained_height_pixels,
    int native_width_pixels, int native_height_pixels,
    int tolerance_pixels = 2) {
  if (retained_width_pixels <= 0 || retained_height_pixels <= 0 ||
      native_width_pixels <= 0 || native_height_pixels <= 0) {
    return false;
  }
  const auto within_tolerance = [tolerance_pixels](int left, int right) {
    return left >= right - tolerance_pixels &&
           left <= right + tolerance_pixels;
  };
  return within_tolerance(retained_width_pixels, native_width_pixels) &&
         within_tolerance(retained_height_pixels, native_height_pixels);
}

/**
 * Requires an exposed, valid physical size to remain unchanged across two app
 * frames. The following frame becomes presentation-ready, leaving one event
 * loop turn for child exposure and final retained-widget painting.
 */
inline QtStartupPresentationAction qt_startup_presentation_action(
    QtStartupPresentationState &state, bool host_ready,
    std::uint64_t geometry_signature) {
  if (state.presentation_ready) {
    return QtStartupPresentationAction::PresentationReady;
  }
  if (!host_ready || geometry_signature == 0) {
    state.observed_geometry_signature = 0;
    state.consecutive_equal_samples = 0;
    return QtStartupPresentationAction::WaitForStableGeometry;
  }
  if (state.observed_geometry_signature != geometry_signature) {
    state.observed_geometry_signature = geometry_signature;
    state.consecutive_equal_samples = 1;
    return QtStartupPresentationAction::WaitForStableGeometry;
  }
  ++state.consecutive_equal_samples;
  if (state.consecutive_equal_samples == 2) {
    return QtStartupPresentationAction::GeometryStabilized;
  }
  state.presentation_ready = true;
  return QtStartupPresentationAction::PresentationReady;
}

inline void qt_invalidate_startup_presentation_geometry(
    QtStartupPresentationState &state) {
  if (!state.presentation_ready) {
    state.observed_geometry_signature = 0;
    state.consecutive_equal_samples = 0;
  }
}

} // namespace quader
