////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/command_tool.hpp"

#include <string_view>

namespace quader {

class EditorSelectionService;

/**
 * Owns Modeling Command Dispatcher behavior within the editor command and action routing layer.
 */
class ModelingCommandDispatcher {
public:
  [[nodiscard]] const CommandTool *find(std::string_view tool_id) const;
  [[nodiscard]] ModelingCommandHistoryPolicy
  history_policy(std::string_view tool_id) const;
  [[nodiscard]] ModelingCommandAvailability
  availability(std::string_view tool_id, ModelingSessionService &modeling,
               EditorSelectionService *selection = nullptr) const;

  ModelingCommandResult
  execute(std::string_view tool_id, ModelingSessionService &modeling,
          EditorSelectionService *selection = nullptr) const;
  ModelingCommandResult
  execute(const CommandTool &tool, ModelingSessionService &modeling,
          EditorSelectionService *selection = nullptr) const;
};

} // namespace quader
