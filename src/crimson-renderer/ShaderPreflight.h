#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace crimson {
// Validates pinned bgfx/DXBC containers, matching varyings and unchanged bindings.
// Runs without a renderer/device; failed candidates must never reach bgfx.
bool preflightShaderPair(std::span<const std::uint8_t> vertex,std::span<const std::uint8_t> fragment,
    std::span<const std::uint8_t> contractVertex,std::span<const std::uint8_t> contractFragment,std::string& diagnostic);
}
