////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/command_tool.hpp"

#include <optional>

namespace quader {

/**
 * Implements Instant Command Tool behavior for the editor command and action routing layer.
 */
class InstantCommandTool : public CommandTool {
public:
  [[nodiscard]] ModelingCommandHistoryPolicy history_policy() const override {
    return rebuilds_mesh_on_success()
               ? ModelingCommandHistoryPolicy::modeling_undo_redo()
               : ModelingCommandHistoryPolicy::none();
  }

  [[nodiscard]] virtual std::optional<ModelingInstantOperationId>
  instant_operation() const {
    return std::nullopt;
  }

  [[nodiscard]] bool has_instant_operation() const {
    return instant_operation().has_value();
  }
    [[nodiscard]] ModelingInstantOperationId operation() const {
      return instant_operation().value();
    }

    [[nodiscard]] ModelingCommandAvailability availability(const ModelingCommandExecutionContext& context) const override {
        (void)context;
        return ModelingCommandAvailability::available();
    }
};

} // namespace quader
