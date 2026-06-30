////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <imgui.h>

namespace quader {

[[nodiscard]] bool imguiOpenPopupContainsScreenPoint(ImVec2 screenPoint);
bool imguiClosePopupForPassThroughClick(ImVec2 passThroughMin, ImVec2 passThroughMax);
bool imguiCloseComboPopupForClickInside(ImVec2 targetMin, ImVec2 targetMax, ImVec2 exclusionMin, ImVec2 exclusionMax);

} // namespace quader
