#pragma once
#include "DecalSurface.h"
#include <filesystem>
#include <optional>

namespace crimson::decals {
// Bump for any change to topology, chart construction, or cached representation.
inline constexpr uint32_t DecalChartCacheVersion=2;
std::filesystem::path decalCachePath(const std::filesystem::path& projectRoot,uint64_t recipe);
// Cache data is disposable. A missing, obsolete, malformed, or incompatible
// entry is a miss; callers then run the normal bounded chart builder.
std::optional<DecalPatch> readDecalCache(const std::filesystem::path&,uint64_t recipe,
    const DecalSurfaceSnapshot&,const DecalBuildRequest&,std::stop_token={});
// Best effort publication. Storage failure never prevents a valid CPU patch.
bool writeDecalCache(const std::filesystem::path&,uint64_t recipe,const DecalPatch&,
    const DecalBuildLimits&,std::stop_token={});
}
