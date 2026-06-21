#pragma once

#include "modeling/ModelingSessionService.h"
#include "modeling/tools/CommandTool.h"

#include <string_view>

namespace quader {

class ModelingCommandDispatcher {
public:
    [[nodiscard]] const CommandTool* find(std::string_view toolId) const;
    [[nodiscard]] ModelingCommandHistoryPolicy historyPolicy(std::string_view toolId) const;
    [[nodiscard]] ModelingCommandAvailability availability(
            std::string_view toolId,
            ModelingSessionService& modeling) const;

    ModelingCommandResult execute(std::string_view toolId, ModelingSessionService& modeling) const;
    ModelingCommandResult execute(const CommandTool& tool, ModelingSessionService& modeling) const;
};

} // namespace quader
