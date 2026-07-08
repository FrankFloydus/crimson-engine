////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/payloads.hpp>
#include <quader/modeling/result.hpp>

#include <string>
#include <vector>

namespace quader::modeling {

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
};

} // namespace quader::modeling
