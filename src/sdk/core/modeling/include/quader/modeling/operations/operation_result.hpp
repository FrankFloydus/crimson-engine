////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/payloads.hpp>
#include <quader/modeling/result.hpp>

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

/**
 * Reports structured operation status, topology deltas, dirty flags, and remaps.
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
};

} // namespace quader::modeling
