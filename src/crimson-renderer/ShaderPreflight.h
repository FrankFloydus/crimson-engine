#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace crimson {
enum class ShaderUniformKind : std::uint8_t { Sampler=0, Vec4=2, Mat3=3, Mat4=4 };
enum class ShaderTextureDimension : std::uint8_t { Texture2D, Cube };
struct ShaderUniformContract {
    std::string name;
    ShaderUniformKind kind{ShaderUniformKind::Vec4};
    std::uint16_t count{1};
    std::uint16_t samplerSlot{};
    bool required{}; // Optimized-away inputs are otherwise legal.
    ShaderTextureDimension textureDimension{ShaderTextureDimension::Texture2D};
    bool comparisonSampler{};
};
// Generated fragments use a declared binding manifest, while the existing
// vertex shader retains the strict embedded contract. No bgfx/device calls.
bool preflightGeneratedShaderPair(std::span<const std::uint8_t> vertex,std::span<const std::uint8_t> fragment,
    std::span<const std::uint8_t> contractVertex,std::span<const ShaderUniformContract> fragmentManifest,std::string& diagnostic);
// Validates pinned bgfx/DXBC containers, matching varyings and unchanged bindings.
// Runs without a renderer/device; failed candidates must never reach bgfx.
bool preflightShaderPair(std::span<const std::uint8_t> vertex,std::span<const std::uint8_t> fragment,
    std::span<const std::uint8_t> contractVertex,std::span<const std::uint8_t> contractFragment,std::string& diagnostic);
bool preflightComputeShader(std::span<const std::uint8_t> candidate,
    std::span<const std::uint8_t> contract,std::string& diagnostic);
}
