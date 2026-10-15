#pragma once
#include "ShaderCompiler.h"
#include <filesystem>
#include <string>
#include <vector>

namespace crimson {
struct ProjectMaterialCacheReport {
    std::size_t materials{},topologies{},prepared{},failed{},cacheHits{},stages{};
    double elapsedMilliseconds{};
    ShaderCompilerMetrics metrics;
    std::vector<std::string> diagnostics;
};
// CPU-only build preparation. Writes disposable shader artifacts only; native
// material sources, scene documents and texture data remain read-only.
ProjectMaterialCacheReport prepareProjectMaterialCache(const std::filesystem::path& project,const ShaderCompileConfig&);
std::string serializeProjectMaterialCacheReport(const ProjectMaterialCacheReport&);
}
