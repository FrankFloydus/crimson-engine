////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_document.hpp"
#include "core/types.hpp"
#include "math/types.hpp"

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
    QVec3 position;
    QVec3 normal;
    QVec2 uv;
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

inline std::vector<MeshPrimitiveSpan> normalizedMeshPrimitiveSpans(const MeshAsset& mesh) {
    std::vector<MeshPrimitiveSpan> spans;
    spans.reserve(mesh.primitives.empty() ? 1u : mesh.primitives.size());
    for (const MeshPrimitiveSpan& primitive : mesh.primitives) {
        const std::uint64_t offset = primitive.indexOffset;
        const std::uint64_t count = primitive.indexCount;
        if (count == 0u || offset >= mesh.indices.size() || offset + count > mesh.indices.size()) {
            continue;
        }
        spans.push_back(primitive);
    }
    if (spans.empty() && !mesh.indices.empty()) {
        spans.push_back(MeshPrimitiveSpan{
                .indexOffset = 0,
                .indexCount = static_cast<std::uint32_t>(mesh.indices.size()),
                .materialSlot = 0,
        });
    }
    return spans;
}

} // namespace quader
