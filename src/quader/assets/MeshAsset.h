#pragma once

#include "quader/core/Types.h"
#include "quader/math/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace quader {

struct MeshVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    std::uint32_t rgba = 0xffffffffu;
};

struct MeshAsset {
    AssetId id = InvalidAssetId;
    std::string name;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    bool hasStableCpuTopology = true;
};

} // namespace quader
