#pragma once
#include "ShaderCompiler.h"
#include <crimson/renderer/forward/ShaderSourceBaseline.hpp>
#include <span>
#include <string_view>

namespace crimson {
// Build-generated content digests describe the sources used by the embedded
// programs. Path relocation and timestamp-only copies do not invalidate them.
bool shaderSourcesMatchEmbedded(const ShaderCompileConfig&,std::span<const ShaderSourceBaselineEntry>) noexcept;
}
