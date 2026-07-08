////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/types.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace quader::modeling {

/**
 * Defines the viewport rectangle used for host-to-SDK input translation.
 */
struct ViewportRect {
  float x = 0.0F;
  float y = 0.0F;
  float width = 1.0F;
  float height = 1.0F;
};

/**
 * Stores keyboard modifier state for one host input frame.
 */
struct ViewportModifiers {
  bool shift = false;
  bool control = false;
  bool alt = false;
  bool command = false;
};

/**
 * Stores pointer button state for one host input frame.
 */
struct ViewportPointerButtons {
  bool left = false;
  bool middle = false;
  bool right = false;
};

/**
 * Captures toolkit-neutral viewport input for SDK tool and navigation owners.
 */
struct ViewportHostInputSnapshot {
  Vec2 pointer_position{};
  ViewportRect viewport_rect{};
  float wheel_delta = 0.0F;
  ViewportPointerButtons buttons{};
  ViewportModifiers modifiers{};
  bool focused = false;
  bool text_input_active = false;
  bool navigation_capture = false;
  std::vector<std::uint32_t> pressed_chords;
  double frame_time_seconds = 0.0;
};

/**
 * Identifies the projection model used by the host viewport camera.
 */
enum class ViewportProjection {
  Perspective,
  Orthographic,
};

/**
 * Identifies how the host packed view-projection matrix should be interpreted.
 */
enum class MatrixConvention {
  ColumnMajor,
  RowMajor,
};

/**
 * Captures renderer-neutral camera state for SDK tool interpretation.
 */
struct ViewportCameraSnapshot {
  ViewportProjection projection = ViewportProjection::Perspective;
  Transform3 camera_transform;
  Vec2 viewport_size;
  double orthographic_size = 24.0;
  double fov_degrees = 60.0;
  MatrixConvention matrix_convention = MatrixConvention::ColumnMajor;
  std::array<float, 16> view_projection{};
};

} // namespace quader::modeling
