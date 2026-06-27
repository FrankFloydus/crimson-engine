////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/services/modeling_action_catalog.hpp"
#include "editor/commands/command_tool_kind.hpp"

#include <string_view>

namespace quader {

/**
 * Stores the Command Tool Descriptor data contract used by the editor command and action routing layer.
 */
struct CommandToolDescriptor {
    std::string_view id;
    std::string_view operationId;
    std::string_view label;
    CommandToolKind kind = CommandToolKind::Instant;
    ModelingActionPreviewKind previewKind = ModelingActionPreviewKind::None;
    bool rebuildsMeshOnSuccess = true;
    std::string_view publicCommandId;
    bool registersPublicCommand = true;
    std::string_view hostShortcutTargetId;
};

} // namespace quader
