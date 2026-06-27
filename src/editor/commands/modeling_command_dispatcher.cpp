////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling_command_dispatcher.hpp"

#include "editor/commands/modeling_command_tool_catalog.hpp"

namespace quader {

const CommandTool* ModelingCommandDispatcher::find(std::string_view toolId) const {
    return findModelingCommandTool(toolId);
}

ModelingCommandHistoryPolicy ModelingCommandDispatcher::historyPolicy(std::string_view toolId) const {
    const CommandTool* tool = find(toolId);
    return tool != nullptr ? tool->historyPolicy() : ModelingCommandHistoryPolicy::none();
}

ModelingCommandAvailability ModelingCommandDispatcher::availability(
        std::string_view toolId,
        ModelingSessionService& modeling,
        EditorSelectionService* selection) const {
    const CommandTool* tool = find(toolId);
    if (tool == nullptr) {
        return ModelingCommandAvailability::unavailable("Command tool is not registered.");
    }
    const ModelingCommandExecutionContext context{.modeling = modeling, .selection = selection};
    return tool->availability(context);
}

ModelingCommandResult ModelingCommandDispatcher::execute(
        std::string_view toolId,
        ModelingSessionService& modeling,
        EditorSelectionService* selection) const {
    const CommandTool* tool = find(toolId);
    if (tool == nullptr) {
        return {
                .ok = false,
                .changed = false,
                .message = "Command tool is not registered.",
        };
    }
    return execute(*tool, modeling, selection);
}

ModelingCommandResult ModelingCommandDispatcher::execute(
        const CommandTool& tool,
        ModelingSessionService& modeling,
        EditorSelectionService* selection) const {
    ModelingCommandExecutionContext context{.modeling = modeling, .selection = selection};
    const ModelingCommandAvailability available = tool.availability(context);
    if (!available.enabled) {
        return {
                .ok = false,
                .changed = false,
                .message = available.disabledReason.empty() ? "Command is disabled." : available.disabledReason,
        };
    }
    return tool.execute(context);
}

} // namespace quader
