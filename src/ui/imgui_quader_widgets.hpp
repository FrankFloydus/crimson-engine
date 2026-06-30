////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <imgui.h>

namespace quader {

bool quaderBeginCombo(
        const char* label,
        const char* previewValue,
        ImGuiComboFlags flags = ImGuiComboFlags_None,
        bool closeOnRepeatedClick = true);
bool quaderCollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None);

} // namespace quader
