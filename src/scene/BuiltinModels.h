#pragma once
#include <string_view>

namespace crimson {
inline constexpr const char* BuiltinBoxModelId = "ec337913-e4bd-5069-af3a-f18227b0209f";
inline constexpr bool isBuiltinModel(std::string_view id) { return id == BuiltinBoxModelId || id == "builtin:mesh-box-v1"; }
}
