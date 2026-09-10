////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_operation_input_hint_types.hpp"
#include "api/quader_primitive_creation_types.hpp"

#include <string_view>
#include <vector>

namespace quader {

/// Declares the selection gesture semantics owned by the lasso tool.
[[nodiscard]] std::vector<QuaderOperationInputHint> lasso_selection_input_hints();

/// Describes radial placement and settings using canonical input targets.
[[nodiscard]] std::vector<QuaderOperationInputHint>
radial_array_input_hints(bool can_finalize, bool center_locked);

/// Selects hints from owner state without resolving bindings or localizing text.
[[nodiscard]] std::vector<QuaderOperationInputHint>
primitive_creation_input_hints(QuaderPrimitiveCreationShape kind, bool can_finalize);

/// Describes the pointer and canonical key actions for one active viewport tool.
[[nodiscard]] std::vector<QuaderOperationInputHint>
viewport_tool_input_hints(std::string_view tool_id);

/// Describes navigation gestures that remain available across viewport tools.
[[nodiscard]] std::vector<QuaderOperationInputHint>
viewport_navigation_input_hints();

} // namespace quader
