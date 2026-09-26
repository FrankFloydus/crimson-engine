////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_operation_input_hint_types.hpp"

#include <string_view>
#include <vector>

namespace quader {

/// Phase facts projected from the existing native tool owners, after revision gating.
struct TransformOperationHintState {
  bool can_apply = false;
  bool owner_dragging = false;
  bool curve_drawing = false;
  bool curve_preset = false;
  bool curve_construction_dragging = false;
  bool radial_center_locked = false;
  bool path_has_source = false;
  bool instances_available = false;
};

/// Selects interaction-specific hints without reading or mutating editor owners.
[[nodiscard]] std::vector<QuaderOperationInputHint>
transform_operation_input_hints(std::string_view tool_id,
                                const TransformOperationHintState &state,
                                const QuaderOperationInputContext &context);

} // namespace quader
