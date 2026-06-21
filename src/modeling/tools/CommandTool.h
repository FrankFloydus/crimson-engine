#pragma once

#include "modeling/ModelingSessionService.h"
#include "modeling/tools/CommandToolDescriptor.h"

#include <string>
#include <string_view>
#include <utility>

namespace quader {

struct ModelingCommandExecutionContext {
    ModelingSessionService& modeling;
};

struct ModelingCommandAvailability {
    bool enabled = true;
    std::string disabledReason;

    [[nodiscard]] static ModelingCommandAvailability available() {
        return {};
    }

    [[nodiscard]] static ModelingCommandAvailability unavailable(std::string reason) {
        return {
                .enabled = false,
                .disabledReason = std::move(reason),
        };
    }
};

enum class ModelingCommandHistoryMode {
    None,
    ModelingUndoRedo,
};

struct ModelingCommandHistoryPolicy {
    ModelingCommandHistoryMode mode = ModelingCommandHistoryMode::None;

    [[nodiscard]] static ModelingCommandHistoryPolicy none() {
        return {};
    }

    [[nodiscard]] static ModelingCommandHistoryPolicy modelingUndoRedo() {
        return {.mode = ModelingCommandHistoryMode::ModelingUndoRedo};
    }
};

class CommandTool {
public:
    CommandTool() = default;
    virtual ~CommandTool() = default;

    CommandTool(const CommandTool&) = delete;
    CommandTool& operator=(const CommandTool&) = delete;
    CommandTool(CommandTool&&) = delete;
    CommandTool& operator=(CommandTool&&) = delete;

    [[nodiscard]] virtual CommandToolDescriptor descriptor() const = 0;
    [[nodiscard]] virtual ModelingCommandAvailability availability(const ModelingCommandExecutionContext& context) const = 0;
    [[nodiscard]] virtual ModelingCommandHistoryPolicy historyPolicy() const = 0;
    virtual ModelingCommandResult execute(ModelingCommandExecutionContext& context) const = 0;

    [[nodiscard]] std::string_view id() const { return descriptor().id; }
    [[nodiscard]] std::string_view operationId() const { return descriptor().operationId; }
    [[nodiscard]] std::string_view label() const { return descriptor().label; }
    [[nodiscard]] CommandToolKind kind() const { return descriptor().kind; }
    [[nodiscard]] ModelingActionPreviewKind previewKind() const { return descriptor().previewKind; }
    [[nodiscard]] bool rebuildsMeshOnSuccess() const { return descriptor().rebuildsMeshOnSuccess; }

    [[nodiscard]] bool enabled(ModelingSessionService& modeling) const {
        const ModelingCommandExecutionContext context{.modeling = modeling};
        return availability(context).enabled;
    }

    [[nodiscard]] std::string disabledReason(ModelingSessionService& modeling) const {
        const ModelingCommandExecutionContext context{.modeling = modeling};
        return availability(context).disabledReason;
    }

    ModelingCommandResult execute(ModelingSessionService& modeling) const {
        ModelingCommandExecutionContext context{.modeling = modeling};
        return execute(context);
    }
};

} // namespace quader
