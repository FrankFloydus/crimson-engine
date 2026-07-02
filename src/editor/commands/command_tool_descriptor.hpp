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
    std::string_view operation_id;
    std::string_view label;
    CommandToolKind kind = CommandToolKind::Instant;
    ModelingActionPreviewKind preview_kind = ModelingActionPreviewKind::None;
    bool rebuilds_mesh_on_success = true;
    std::string_view public_command_id;
    bool registers_public_command = true;
    std::string_view host_shortcut_target_id;
};

} // namespace quader
