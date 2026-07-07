////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <imgui.h>

namespace quader {

bool quader_theme_slider_float(
    const char *label, float *value, float minimum, float maximum,
    const char *format = "%.3f",
    ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp);
bool quader_theme_slider_int(
    const char *label, int *value, int minimum, int maximum,
    const char *format = "%d",
    ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp);

} // namespace quader
