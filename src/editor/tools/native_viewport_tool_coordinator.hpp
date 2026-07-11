////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"

#include <string_view>

namespace quader {

class EditorSelectionService;
class LinearArrayToolService;
class PivotToolService;

/**
 * Routes native editor-owned viewport tools through one lifecycle boundary.
 */
class NativeViewportToolCoordinator final {
public:
  NativeViewportToolCoordinator(PivotToolService &pivot,
                                LinearArrayToolService &linear_array,
                                EditorSelectionService &selection);

  [[nodiscard]] bool handles(std::string_view tool_id) const;
  [[nodiscard]] bool can_activate(std::string_view tool_id) const;
  [[nodiscard]] bool has_pending_tool() const;
  [[nodiscard]] std::string_view active_tool_id() const;

  CommandResult activate(std::string_view tool_id, double grid_world_size);
  CommandResult commit_active();
  CommandResult cancel_active();

private:
  PivotToolService &pivot_;
  LinearArrayToolService &linear_array_;
  EditorSelectionService &selection_;
};

} // namespace quader
