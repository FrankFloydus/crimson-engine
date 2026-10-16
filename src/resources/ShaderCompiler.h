#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <utility>
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
// Cumulative CPU diagnostics; callers take differences around the workload.
// These counters do not change cache identity, compiler options or publication.
struct ShaderCompilerMetrics {
    std::uint64_t compilerInvocations{},compilerNanoseconds{},dependencyRequests{},dependencyNanoseconds{};
    std::uint64_t cacheOnlyRequests{},cacheOnlyHits{},cacheOnlyNanoseconds{},workerRequests{},workerCacheHits{};
};
ShaderCompilerMetrics shaderCompilerMetrics();
enum class ShaderStage { Vertex, Fragment, Compute };
// Interactive code uses identical shader semantics with FXC optimization
// deferred. Full is the persistent build/cook quality and remains the default.
enum class ShaderCompileOptimization { Full, Interactive };
// Explicit generated-source recipe. The caller owns immutable source text;
// workers stage it only under cacheDirectory and never edit engine sources.
struct ShaderCompileRecipe {
    std::string name, source;
    ShaderStage stage{ShaderStage::Fragment};
    std::filesystem::path varyingDefinition;
    std::vector<std::filesystem::path> includeDirectories;
    std::string contract;
    ShaderCompileOptimization optimization{ShaderCompileOptimization::Full};
};
// Cache-only hydration at the presentation boundary. A source-epoch session
// shares content hashing across program bundles. find never runs shaderc or
// modifies files; callers reject a changed snapshot before publication.
class ShaderCacheReadSession {
public:
    explicit ShaderCacheReadSession(ShaderCompileConfig);
    std::optional<ShaderBinary> find(const std::string& name);
    std::optional<ShaderBinary> find(const ShaderCompileRecipe&);
    bool unchanged() const;
    // Explicit recovery after full ABI/DXBC preflight rejects a cached bundle.
    // Removes only artifacts actually read by this session, never source data.
    void discard(std::span<const ShaderBinary>);
private:
    ShaderCompileConfig config_;
    std::unordered_map<std::string,std::uint64_t> prefixes_;
    std::unordered_map<std::string,std::pair<std::filesystem::path,std::uint64_t>> observed_;
    std::unordered_map<std::string,std::filesystem::path> artifacts_;
    bool coherent_{true};
    std::uint64_t fingerprint(const std::vector<std::filesystem::path>&);
};
std::vector<std::filesystem::path> shaderDependencies(const ShaderCompileConfig&,const ShaderCompileRecipe&);
ShaderBinary compileShader(const ShaderCompileConfig&,const ShaderCompileRecipe&,std::stop_token={});
std::vector<std::filesystem::path> shaderDependencies(const ShaderCompileConfig&,const std::string& name);
std::vector<std::filesystem::path> parseShaderDependencyFile(const std::filesystem::path&);
ShaderBinary compileShader(const ShaderCompileConfig&,const std::string& name,std::stop_token={});
std::vector<std::string> engineShaderNames();
}
