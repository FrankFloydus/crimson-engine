#pragma once

#include <imgui.h>

namespace quader {

struct UIIconSet {
    ImTextureID mesh = ImTextureID_Invalid;
    ImTextureID empty = ImTextureID_Invalid;
    ImTextureID lock = ImTextureID_Invalid;
    ImTextureID hide = ImTextureID_Invalid;
};

} // namespace quader
