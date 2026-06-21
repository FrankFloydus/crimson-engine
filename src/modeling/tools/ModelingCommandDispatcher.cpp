#include "modeling/tools/ModelingCommandDispatcher.h"

#include "modeling/tools/ModelingCommandToolCatalog.h"

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
        ModelingSessionService& modeling) const {
    const CommandTool* tool = find(toolId);
    if (tool == nullptr) {
        return ModelingCommandAvailability::unavailable("Command tool is not registered.");
    }
    const ModelingCommandExecutionContext context{.modeling = modeling};
    return tool->availability(context);
}

ModelingCommandResult ModelingCommandDispatcher::execute(std::string_view toolId, ModelingSessionService& modeling) const {
    const CommandTool* tool = find(toolId);
    if (tool == nullptr) {
        return {
                .ok = false,
                .changed = false,
                .message = "Command tool is not registered.",
        };
    }
    return execute(*tool, modeling);
}

ModelingCommandResult ModelingCommandDispatcher::execute(const CommandTool& tool, ModelingSessionService& modeling) const {
    ModelingCommandExecutionContext context{.modeling = modeling};
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
