#pragma once

#include <filesystem>
#include <vector>
#include "assets/ContentStorage.h"

namespace crimson {
// Source and bgfx headers form one interface family. Release uses the payload
// staged with its executable even while a development checkout is present.
struct ShaderRuntimePaths {
    std::filesystem::path sources,includes;
    bool operator==(const ShaderRuntimePaths&) const = default;
};
inline ShaderRuntimePaths selectShaderRuntimePaths(bool development,const ShaderRuntimePaths& live,
    const ShaderRuntimePaths& staged,bool liveComplete) {
    return development&&liveComplete?live:staged;
}
inline std::vector<std::filesystem::path> shaderRuntimeWatchedRoots(const std::filesystem::path& project,
    const ShaderRuntimePaths& selected) {
    return {project,selected.sources,selected.includes};
}
}
