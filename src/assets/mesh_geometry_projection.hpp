////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"

#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace quader {

/**
 * Pins an immutable coordinate projection over retained compiled topology.
 * Positions are authoritative; attribute vertices may retain older positions.
 * Missing replacement streams borrow the source, and unchanged streams retain
 * their owners across publications. Producers finish all streams before sharing.
 */
struct MeshGeometryProjection final {
  std::shared_ptr<const MeshGeometryPayload> source_geometry;
  std::shared_ptr<const std::vector<MeshVertex>> vertex_attributes;
  std::shared_ptr<const std::vector<std::uint32_t>> triangle_indices;
  std::shared_ptr<const std::vector<QVec3>> positions;

  [[nodiscard]] std::span<const MeshVertex> vertices() const noexcept {
    return vertex_attributes ? std::span<const MeshVertex>(*vertex_attributes)
        : source_geometry ? std::span<const MeshVertex>(source_geometry->vertices)
                          : std::span<const MeshVertex>{};
  }

  [[nodiscard]] std::span<const std::uint32_t> indices() const noexcept {
    return triangle_indices ? std::span<const std::uint32_t>(*triangle_indices)
        : source_geometry ? std::span<const std::uint32_t>(source_geometry->indices)
                          : std::span<const std::uint32_t>{};
  }

  [[nodiscard]] std::span<const MeshPrimitiveSpan> primitives() const noexcept {
    return source_geometry ? std::span<const MeshPrimitiveSpan>(source_geometry->primitives)
                           : std::span<const MeshPrimitiveSpan>{};
  }

  [[nodiscard]] MeshGeometryContentId content_id() const noexcept {
    std::uint64_t hash = content_id_.load(std::memory_order_acquire);
    if (hash != 0u) return {hash};
    hash = MeshGeometryPayload::content_id_for(vertices(), indices(), primitives(),
        positions ? std::span<const QVec3>(*positions) : std::span<const QVec3>{}).value;
    std::uint64_t expected = 0u;
    if (!content_id_.compare_exchange_strong(expected, hash,
            std::memory_order_release, std::memory_order_acquire)) hash = expected;
    return {hash};
  }

  /** Combines streams once for persistence/API export, never viewport packing. */
  [[nodiscard]] std::shared_ptr<const MeshGeometryPayload>
  materialized_geometry() const {
    if (!source_geometry || !positions || positions->size() != vertices().size()) {
      return {};
    }
    std::call_once(materialization_once_, [this] {
      auto geometry = std::make_shared<MeshGeometryPayload>();
      const auto attributes = vertices();
      geometry->vertices.assign(attributes.begin(), attributes.end());
      for (std::size_t index = 0U; index < geometry->vertices.size(); ++index) {
        geometry->vertices[index].position = (*positions)[index];
      }
      const auto triangles = indices();
      geometry->indices.assign(triangles.begin(), triangles.end());
      geometry->primitives = source_geometry->primitives;
      materialized_geometry_ = std::move(geometry);
    });
    return materialized_geometry_;
  }

private:
  friend class AssetRegistry;
  mutable std::atomic<bool> asset_geometry_validated_{false};
  mutable std::once_flag materialization_once_;
  mutable std::shared_ptr<const MeshGeometryPayload> materialized_geometry_;
  mutable std::atomic<std::uint64_t> content_id_{0u};
};

} // namespace quader
