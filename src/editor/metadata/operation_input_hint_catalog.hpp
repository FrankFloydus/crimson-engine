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

/// Cheap semantic phase projected from the authoritative modeling tool owner.
struct ModelingOperationHintState {
  bool can_apply = false;
  bool dragging = false;
  bool active_stroke = false;
  bool closed = false;
  bool extrusion_pending = false;
  bool extrusion_region = true;
  bool cut_keep_mode_enabled = true;
};

[[nodiscard]] std::vector<QuaderOperationInputHint>
lasso_selection_input_hints(const QuaderOperationInputContext &context = {});
[[nodiscard]] std::vector<QuaderOperationInputHint>
selection_gesture_input_hints(const QuaderOperationInputContext &context);
[[nodiscard]] std::vector<QuaderOperationInputHint>
radial_array_input_hints(bool can_finalize, bool center_locked);
[[nodiscard]] std::vector<QuaderOperationInputHint>
primitive_creation_input_hints(QuaderPrimitiveCreationShape kind, bool can_finalize,
                               const QuaderOperationInputContext &context = {});
[[nodiscard]] std::vector<QuaderOperationInputHint>
viewport_tool_input_hints(std::string_view tool_id,
                          const ModelingOperationHintState &state = {},
                          const QuaderOperationInputContext &context = {});
[[nodiscard]] std::vector<QuaderOperationInputHint>
viewport_navigation_input_hints(const QuaderOperationInputContext &context = {});
[[nodiscard]] std::vector<QuaderOperationInputHint>
viewport_auxiliary_input_hints(const QuaderOperationInputContext &context);

} // namespace quader
