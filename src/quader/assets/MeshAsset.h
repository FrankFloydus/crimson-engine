#pragma once

#include "quader/assets/AssetRevision.h"
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

struct MeshPrimitiveSpan {
    std::uint32_t indexOffset = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t materialSlot = 0;
};

struct MeshAsset {
    AssetId id = InvalidAssetId;
    MeshAssetRevision revision;
    std::string name;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshPrimitiveSpan> primitives;
    bool hasStableCpuTopology = true;
};

} // namespace quader
