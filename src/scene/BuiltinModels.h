#pragma once
#include <string_view>

namespace crimson {
inline constexpr const char* BuiltinBoxModelId = "builtin:mesh-box-v1";
inline constexpr bool isBuiltinModel(std::string_view id) { return id == BuiltinBoxModelId; }
}
