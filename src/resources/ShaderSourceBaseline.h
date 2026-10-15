#pragma once
#include "ShaderCompiler.h"
#include <span>
#include <string_view>

namespace crimson {
enum class ShaderSourceRoot { Sources, Includes, Compiler, Contract };
struct ShaderSourceBaselineEntry {
    ShaderSourceRoot root;
    std::string_view relativePath,sha256;
};
// Build-generated content digests describe the sources used by the embedded
// programs. Path relocation and timestamp-only copies do not invalidate them.
bool shaderSourcesMatchEmbedded(const ShaderCompileConfig&,std::span<const ShaderSourceBaselineEntry>) noexcept;
}
