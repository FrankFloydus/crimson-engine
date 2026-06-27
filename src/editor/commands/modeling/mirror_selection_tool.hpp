////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/runtime_command_tool.hpp"

#include <string>
#include <string_view>

namespace quader {

/**
 * Implements Mirror Selection Tool behavior for the editor command and action routing layer.
 */
class MirrorSelectionTool final : public RuntimeCommandTool {
public:
    MirrorSelectionTool(ModelingMirrorAxis axis, std::string_view id, std::string_view label);

    [[nodiscard]] CommandToolDescriptor descriptor() const override;
    [[nodiscard]] ModelingMirrorAxis axis() const;

private:
    ModelingCommandResult executeRuntimeCommand(ModelingCommandExecutionContext& context) const override;

    ModelingMirrorAxis axis_ = ModelingMirrorAxis::X;
    std::string id_;
    std::string label_;
};

} // namespace quader
