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
#include <limits>
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
  std::uint32_t index_offset = 0;
  std::uint32_t index_count = 0;
  std::uint32_t material_slot = 0;
};

/**
 * Represents a Mesh Asset value used by the asset document and registry layer.
 */
struct MeshAsset final : AssetDocumentBase<MeshAssetRevision> {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshPrimitiveSpan> primitives;
    bool has_stable_cpu_topology = true;
    bool has_authored_polygon_document = false;
    std::shared_ptr<quader_poly::Document> authored_polygon_document;
};

inline std::vector<MeshPrimitiveSpan>
normalized_mesh_primitive_spans(const MeshAsset &mesh) {
  std::vector<MeshPrimitiveSpan> spans;
  spans.reserve(mesh.primitives.empty() ? 1u : mesh.primitives.size());
  for (const MeshPrimitiveSpan &primitive : mesh.primitives) {
    const std::uint64_t offset = primitive.index_offset;
    const std::uint64_t count = primitive.index_count;
    if (count == 0u || offset >= mesh.indices.size() ||
        offset + count > mesh.indices.size()) {
      continue;
    }
    spans.push_back(primitive);
  }
  if (spans.empty() && !mesh.indices.empty()) {
    spans.push_back(MeshPrimitiveSpan{
        .index_offset = 0,
        .index_count = static_cast<std::uint32_t>(mesh.indices.size()),
        .material_slot = 0,
    });
  }
  return spans;
}

inline std::vector<MeshPrimitiveSpan>
coalesced_mesh_primitive_spans(const MeshAsset &mesh) {
  const std::vector<MeshPrimitiveSpan> normalized =
      normalized_mesh_primitive_spans(mesh);
  std::vector<MeshPrimitiveSpan> spans;
  spans.reserve(normalized.size());
  for (const MeshPrimitiveSpan &primitive : normalized) {
    if (!spans.empty()) {
      MeshPrimitiveSpan &previous = spans.back();
      const std::uint64_t previous_end =
          static_cast<std::uint64_t>(previous.index_offset) +
          static_cast<std::uint64_t>(previous.index_count);
      const std::uint64_t merged_count =
          static_cast<std::uint64_t>(previous.index_count) +
          static_cast<std::uint64_t>(primitive.index_count);
      if (previous.material_slot == primitive.material_slot &&
          previous_end == primitive.index_offset &&
          merged_count <= static_cast<std::uint64_t>(
                              std::numeric_limits<std::uint32_t>::max())) {
        previous.index_count = static_cast<std::uint32_t>(merged_count);
        continue;
      }
    }
    spans.push_back(primitive);
  }
  return spans;
}

} // namespace quader
