#pragma once
#include <string_view>

namespace crimson {
inline constexpr const char* BuiltinBoxModelId = "ec337913-e4bd-5069-af3a-f18227b0209f";
inline constexpr const char* BuiltinSphereModelId = "95765ef1-f7c8-5ed8-b768-3c745ceab902";
inline constexpr const char* BuiltinCylinderModelId = "887714c2-afcd-5ad9-b2b4-6a641198fac9";
inline constexpr bool isBuiltinModel(std::string_view id) {
    return id == BuiltinBoxModelId || id == "builtin:mesh-box-v1" ||
        id == BuiltinSphereModelId || id == "builtin:mesh-sphere-v1" ||
        id == BuiltinCylinderModelId || id == "builtin:mesh-cylinder-v1";
}
}
