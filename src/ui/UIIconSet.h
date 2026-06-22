////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <imgui.h>

namespace quader {

/**
 * Represents an UI Icon Set value used by the ImGui editor presentation layer.
 */
struct UIIconSet {
    ImTextureID mesh = ImTextureID_Invalid;
    ImTextureID empty = ImTextureID_Invalid;
    ImTextureID lock = ImTextureID_Invalid;
    ImTextureID hide = ImTextureID_Invalid;
};

} // namespace quader
