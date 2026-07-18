////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/public_command_targets.hpp"
#include "assets/material_reference.hpp"
#include "editor/commands/command_tool_descriptor.hpp"
#include "editor/modeling/modeling_result_types.hpp"

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
    MaterialReference active_material;
};

/**
 * Represents a Modeling Command Availability value used by the editor command and action routing layer.
 */
struct ModelingCommandAvailability {
    bool enabled = true;
    std::string disabled_reason;

    [[nodiscard]] static ModelingCommandAvailability available() {
        return {};
    }

    [[nodiscard]] static ModelingCommandAvailability unavailable(std::string reason) {
      return {
          .enabled = false,
          .disabled_reason = std::move(reason),
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

    [[nodiscard]] static ModelingCommandHistoryPolicy modeling_undo_redo() {
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
    [[nodiscard]] virtual ModelingCommandHistoryPolicy
    history_policy() const = 0;
    virtual ModelingCommandResult execute(ModelingCommandExecutionContext& context) const = 0;

    [[nodiscard]] std::string_view id() const { return descriptor().id; }
    [[nodiscard]] std::string_view operation_id() const {
      return descriptor().operation_id;
    }
    [[nodiscard]] std::string_view label() const { return descriptor().label; }
    [[nodiscard]] CommandToolKind kind() const { return descriptor().kind; }
    [[nodiscard]] ModelingActionPreviewKind preview_kind() const {
      return descriptor().preview_kind;
    }
    [[nodiscard]] bool rebuilds_mesh_on_success() const {
      return descriptor().rebuilds_mesh_on_success;
    }
    [[nodiscard]] std::string_view public_command_id() const {
      return descriptor().public_command_id;
    }
    [[nodiscard]] bool registers_public_command() const {
      return descriptor().registers_public_command;
    }
    [[nodiscard]] std::string_view host_shortcut_target_id() const {
      return descriptor().host_shortcut_target_id;
    }
    [[nodiscard]] std::string public_command_target_id() const {
      const std::string_view fixed_id = public_command_id();
      if (!fixed_id.empty()) {
        return std::string(fixed_id);
      }
      return quader_modeling_command_target_id(id());
    }

    [[nodiscard]] bool enabled(ModelingSessionService& modeling) const {
        const ModelingCommandExecutionContext context{.modeling = modeling};
        return availability(context).enabled;
    }

    [[nodiscard]] std::string
    disabled_reason(ModelingSessionService &modeling) const {
      const ModelingCommandExecutionContext context{.modeling = modeling};
      return availability(context).disabled_reason;
    }

    ModelingCommandResult execute(ModelingSessionService& modeling) const {
        ModelingCommandExecutionContext context{.modeling = modeling};
        return execute(context);
    }
};

} // namespace quader
