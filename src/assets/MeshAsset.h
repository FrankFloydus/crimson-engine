////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/AssetDocument.h"
#include "core/Types.h"
#include "math/Types.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace quader_poly {
struct Document;
}

namespace quader {

/**
 * Represents a Mesh Vertex value used by the asset document and registry layer.
 */
struct MeshVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    std::uint32_t rgba = 0xffffffffu;
};

/**
 * Represents a Mesh Primitive Span value used by the asset document and registry layer.
 */
struct MeshPrimitiveSpan {
    std::uint32_t indexOffset = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t materialSlot = 0;
};

/**
 * Represents a Mesh Asset value used by the asset document and registry layer.
 */
struct MeshAsset final : AssetDocumentBase<MeshAssetRevision> {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshPrimitiveSpan> primitives;
    bool hasStableCpuTopology = true;
    bool hasAuthoredPolygonDocument = false;
    std::shared_ptr<quader_poly::Document> authoredPolygonDocument;
};

} // namespace quader
