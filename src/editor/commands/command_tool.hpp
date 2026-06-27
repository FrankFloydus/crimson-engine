////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "editor/commands/command_tool_descriptor.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace quader {

class ModelingSessionService;
class EditorSelectionService;

/**
 * Stores the Modeling Command Execution Context data contract used by the editor command and action routing layer.
 */
struct ModelingCommandExecutionContext {
    ModelingSessionService& modeling;
    EditorSelectionService* selection = nullptr;
};

/**
 * Represents a Modeling Command Availability value used by the editor command and action routing layer.
 */
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

/**
 * Enumerates ModelingCommandHistoryMode values used by the editor layer.
 */
enum class ModelingCommandHistoryMode {
    None,
    ModelingUndoRedo,
};

/**
 * Stores the Modeling Command History Policy data contract used by the editor command and action routing layer.
 */
struct ModelingCommandHistoryPolicy {
    ModelingCommandHistoryMode mode = ModelingCommandHistoryMode::None;

    [[nodiscard]] static ModelingCommandHistoryPolicy none() {
        return {};
    }

    [[nodiscard]] static ModelingCommandHistoryPolicy modelingUndoRedo() {
        return {.mode = ModelingCommandHistoryMode::ModelingUndoRedo};
    }
};

/**
 * Implements Command Tool behavior for the editor command and action routing layer.
 */
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
    [[nodiscard]] std::string_view publicCommandId() const { return descriptor().publicCommandId; }
    [[nodiscard]] bool registersPublicCommand() const { return descriptor().registersPublicCommand; }
    [[nodiscard]] std::string_view hostShortcutTargetId() const { return descriptor().hostShortcutTargetId; }
    [[nodiscard]] std::string publicCommandTargetId() const {
        const std::string_view fixedId = publicCommandId();
        if (!fixedId.empty()) {
            return std::string(fixedId);
        }
        std::string commandId("modeling.");
        commandId.append(id());
        return commandId;
    }

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
