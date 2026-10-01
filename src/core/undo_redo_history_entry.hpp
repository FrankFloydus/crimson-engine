////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstddef>
#include <string>

namespace quader {

/// Identifies whether a reset starts original history or preserves an evicted prefix.
enum class UndoRedoHistoryBaseline { Original, Retained };

/// Describes a retained operation state, or the reachable Original state at index zero.
struct UndoRedoHistoryEntry {
  std::size_t applied_count = 0;
  std::string label;
  bool current = false;
};

} // namespace quader
