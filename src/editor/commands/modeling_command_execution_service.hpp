////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/history/editor_history_service.hpp"
#include "editor/commands/modeling_command_dispatcher.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace quader {

class EditorSelectionService;
class SceneStructuralLifecycleService;
enum class ModelingViewportTool;

/**
 * Coordinates Modeling Command Execution Service responsibilities for the editor coordination layer.
 */
class ModelingCommandExecutionService final {
public:
  ModelingCommandResult execute(EditorHistoryService &history,
                                ModelingSessionService &modeling,
                                EditorSelectionService &selection,
                                std::string_view tool_id,
                                MaterialReference active_material = {},
                                SceneStructuralLifecycleService *
                                    structural_lifecycle = nullptr);

  ModelingCommandResult
  execute_untracked(ModelingSessionService &modeling, std::string_view tool_id,
                    EditorSelectionService *selection = nullptr);

  ModelingCommandResult begin_preview(EditorHistoryService &history,
                                      ModelingSessionService &modeling,
                                      EditorSelectionService &selection,
                                      std::string_view action_id,
                                      SceneStructuralLifecycleService *
                                          structural_lifecycle = nullptr);

  ModelingCommandResult
  begin_preview_untracked(ModelingSessionService &modeling,
                          std::string_view action_id);

  ModelingCommandResult commit_preview(EditorHistoryService &history,
                                       ModelingSessionService &modeling,
                                       std::string label);

  /// Accepts the active viewport modeling owner through editor history.
  ModelingCommandResult commit_viewport_tool(
      EditorHistoryService &history, ModelingSessionService &modeling,
      ModelingViewportTool tool, std::string label,
      MaterialReference active_material = {});

  ModelingCommandResult cancel_preview(ModelingSessionService &modeling);

  [[nodiscard]] ModelingCommandAvailability
  availability(ModelingSessionService &modeling, std::string_view tool_id,
               EditorSelectionService *selection = nullptr,
               MaterialReference active_material = {},
               SceneStructuralLifecycleService *structural_lifecycle =
                   nullptr) const;

  [[nodiscard]] ModelingCommandHistoryPolicy
  history_policy(std::string_view tool_id) const;

private:
    ModelingCommandDispatcher dispatcher_;
};

} // namespace quader
