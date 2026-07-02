////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/instant_command_tool.hpp"

namespace quader {

/**
 * Implements Runtime Command Tool behavior for the editor command and action routing layer.
 */
class RuntimeCommandTool : public InstantCommandTool {
public:
    [[nodiscard]] ModelingCommandAvailability availability(const ModelingCommandExecutionContext& context) const override {
      return runtime_availability(context);
    }

    ModelingCommandResult execute(ModelingCommandExecutionContext& context) const final {
      return execute_runtime_command(context);
    }

protected:
  [[nodiscard]] virtual ModelingCommandAvailability
  runtime_availability(const ModelingCommandExecutionContext &context) const {
    (void)context;
    return ModelingCommandAvailability::available();
  }

  virtual ModelingCommandResult
  execute_runtime_command(ModelingCommandExecutionContext &context) const = 0;
};

} // namespace quader
