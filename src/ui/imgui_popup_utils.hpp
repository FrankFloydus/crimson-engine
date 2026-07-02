////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <imgui.h>

namespace quader {

[[nodiscard]] bool imgui_open_popup_contains_screen_point(ImVec2 screen_point);
bool imgui_close_popup_for_pass_through_click(ImVec2 pass_through_min,
                                              ImVec2 pass_through_max);
bool imgui_close_combo_popup_for_click_inside(ImVec2 target_min,
                                              ImVec2 target_max,
                                              ImVec2 exclusion_min,
                                              ImVec2 exclusion_max);

} // namespace quader
