#pragma once

#include "assets/AssetDocument.h"
#include "core/Types.h"
#include "math/Types.h"

#include <cstdint>
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

struct MeshAsset final : AssetDocumentBase<MeshAssetRevision> {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshPrimitiveSpan> primitives;
    bool hasStableCpuTopology = true;
};

} // namespace quader
