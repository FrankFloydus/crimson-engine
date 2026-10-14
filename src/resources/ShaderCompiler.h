#pragma once
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>
namespace crimson {
struct ShaderBinary {
    std::string name;
    bool vertex{};
    std::vector<std::uint8_t> bytes;
    std::vector<std::filesystem::path> dependencies;
    std::uint64_t fingerprint{};
    std::uint64_t reloadEpoch{}; // Coordinator batch identity; not part of compiled code.
};
struct ShaderCompileConfig {
    std::filesystem::path compiler,sourceDirectory,includeDirectory,cacheDirectory;
    std::string buildContract{"crimson-shader-v2;bgfx-bin11;dx11;windows;s_5_0;O3"};
};
enum class ShaderStage { Vertex, Fragment, Compute };
// Explicit generated-source recipe. The caller owns immutable source text;
// workers stage it only under cacheDirectory and never edit engine sources.
struct ShaderCompileRecipe {
    std::string name, source;
    ShaderStage stage{ShaderStage::Fragment};
    std::filesystem::path varyingDefinition;
    std::vector<std::filesystem::path> includeDirectories;
    std::string contract;
};
std::vector<std::filesystem::path> shaderDependencies(const ShaderCompileConfig&,const ShaderCompileRecipe&);
ShaderBinary compileShader(const ShaderCompileConfig&,const ShaderCompileRecipe&,std::stop_token={});
std::vector<std::filesystem::path> shaderDependencies(const ShaderCompileConfig&,const std::string& name);
std::vector<std::filesystem::path> parseShaderDependencyFile(const std::filesystem::path&);
ShaderBinary compileShader(const ShaderCompileConfig&,const std::string& name,std::stop_token={});
std::vector<std::string> engineShaderNames();
}
