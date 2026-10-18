////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/payloads.hpp>
#include <quader/modeling/result.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace quader::modeling {

/**
 * Summarizes canonical bevel support clipping in mesh-local units. Applied
 * width is the maximum consumed across all sides and terminal patches. If it
 * is below requested width, replaying that applied width reproduces the same
 * candidate. Partial clipping alone does not limit still-changing larger sides.
 */
struct EdgeBevelWidthFeedback {
  float requested_width = 0.0F;
  float applied_width = 0.0F;
  bool support_limited = false;

  friend bool operator==(const EdgeBevelWidthFeedback &,
                         const EdgeBevelWidthFeedback &) = default;
};

/** Carries the accepted mean cubic, or a flagged representative rail cubic. */
struct BridgeShapeFeedback {
  std::array<Vec3, 4> controls{};
  Vec3 start_handle_axis{};
  Vec3 end_handle_axis{};
  float start_tension = 1.0F;
  float end_tension = 1.0F;
  bool representative_rail = false;

  friend bool operator==(const BridgeShapeFeedback &left,
                         const BridgeShapeFeedback &right) {
    const auto equal_vector = [](Vec3 first, Vec3 second) {
      return first.x == second.x && first.y == second.y && first.z == second.z;
    };
    for (std::size_t index = 0; index < left.controls.size(); ++index) {
      if (!equal_vector(left.controls[index], right.controls[index]))
        return false;
    }
    return equal_vector(left.start_handle_axis, right.start_handle_axis) &&
           equal_vector(left.end_handle_axis, right.end_handle_axis) &&
           left.start_tension == right.start_tension &&
           left.end_tension == right.end_tension &&
           left.representative_rail == right.representative_rail;
  }
};

/**
 * Reports structured operation status, topology deltas, dirty flags, and
 * remaps.
 */
struct OperationResult {
  bool success = true;
  bool changed = false;
  ErrorCode error_code = ErrorCode::Ok;
  std::string message;
  std::vector<Diagnostic> diagnostics;
  ElementDelta created;
  ElementDelta deleted;
  ElementDelta affected;
  ElementDelta modified;
  DirtyFlags dirty;
  SelectionRemap selection_remap;
  std::optional<EdgeBevelWidthFeedback> edge_bevel_width;
  std::optional<BridgeShapeFeedback> bridge_shape;
};

} // namespace quader::modeling
