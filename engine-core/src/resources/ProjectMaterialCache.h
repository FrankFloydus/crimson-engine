#pragma once
#include "ShaderCompiler.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace crimson {
struct ProjectMaterialCacheReport {
    std::size_t materials{},topologies{},prepared{},failed{},cacheHits{},stages{};
    std::size_t presentationPrepared{},presentationFailed{};
    double presentationMilliseconds{};
    double elapsedMilliseconds{};
    ShaderCompilerMetrics metrics;
    std::vector<std::string> diagnostics;
};
// CPU-only build preparation. Writes disposable shader artifacts only; native
// material sources, scene documents and texture data remain read-only.
// First publish usable coherent families for the entire catalog, then optimize
// them. The optional observer runs at that boundary, before any cold O3 surface.
ProjectMaterialCacheReport prepareProjectMaterialCache(const std::filesystem::path& project,const ShaderCompileConfig&,
    const std::function<void()>& presentationReady={});
std::string serializeProjectMaterialCacheReport(const ProjectMaterialCacheReport&);
}
