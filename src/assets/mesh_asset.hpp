////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_document.hpp"
#include "assets/mesh_authored_polygon_document.hpp"
#include "core/types.hpp"
#include "math/types.hpp"

#include <quader/modeling/neutral/polygon/document_model.hpp>

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace quader_poly {
struct Document;
}

namespace quader {

struct MeshWireGeometryPayload;
struct MeshCollisionGeometryPayload;
struct MeshGeometryProjection;

/**
 * Represents a Mesh Vertex value used by the asset document and registry layer.
 */
struct MeshVertex {
    QVec3 position;
    QVec3 normal;
    QVec2 uv;
    std::uint32_t rgba = 0xffffffffu;
    // Stable authored identity for position-only modeling previews. A compiled
    // corner may duplicate an authored vertex, but every duplicate carries the
    // same ID so topology and indices can remain retained during a drag.
    std::uint32_t source_vertex_id = 0u;
    // Affine UV carrier plus the canonical geometric normal. Occurrence
    // placement corrections reuse these without duplicating mesh geometry.
    std::array<float, 4> uv_carrier{};
    QVec3 uv_geometric_normal{0.0f, 1.0f, 0.0f};
    std::uint32_t circular_uv_index = 0u;
};

/**
 * Represents a Mesh Primitive Span value used by the asset document and registry layer.
 */
struct MeshPrimitiveSpan {
  std::uint32_t index_offset = 0;
  std::uint32_t index_count = 0;
  std::uint32_t material_slot = 0;

  [[nodiscard]] bool operator==(const MeshPrimitiveSpan &) const = default;
};

/// Identifies one immutable compiled geometry payload by authored content.
struct MeshGeometryContentId final {
  std::uint64_t value = 0;

  [[nodiscard]] bool operator==(const MeshGeometryContentId &) const = default;
};

/**
 * Owns immutable-by-contract compiled mesh geometry shared by logical assets,
 * render snapshots, history, and persistence snapshots.
 */
struct MeshGeometryPayload final {
  MeshGeometryPayload() = default;

  MeshGeometryPayload(const MeshGeometryPayload &other)
      : vertices(other.vertices), indices(other.indices),
        primitives(other.primitives),
        circular_uv_interpolations(other.circular_uv_interpolations),
        content_id_(other.content_id_.load(std::memory_order_relaxed)) {}

  MeshGeometryPayload(MeshGeometryPayload &&other) noexcept
      : vertices(std::move(other.vertices)), indices(std::move(other.indices)),
        primitives(std::move(other.primitives)),
        circular_uv_interpolations(std::move(other.circular_uv_interpolations)),
        content_id_(other.content_id_.load(std::memory_order_relaxed)) {}

  MeshGeometryPayload &operator=(const MeshGeometryPayload &other) {
    if (this == &other) {
      return *this;
    }
    vertices = other.vertices;
    indices = other.indices;
    primitives = other.primitives;
    circular_uv_interpolations = other.circular_uv_interpolations;
    wire_geometry_payload_.reset();
    std::atomic_store_explicit(
        &collision_geometry_payload_,
        std::shared_ptr<const MeshCollisionGeometryPayload>{},
        std::memory_order_release);
    content_id_.store(other.content_id_.load(std::memory_order_relaxed),
                      std::memory_order_relaxed);
    return *this;
  }

  MeshGeometryPayload &operator=(MeshGeometryPayload &&other) noexcept {
    if (this == &other) {
      return *this;
    }
    vertices = std::move(other.vertices);
    indices = std::move(other.indices);
    primitives = std::move(other.primitives);
    circular_uv_interpolations = std::move(other.circular_uv_interpolations);
    wire_geometry_payload_.reset();
    std::atomic_store_explicit(
        &collision_geometry_payload_,
        std::shared_ptr<const MeshCollisionGeometryPayload>{},
        std::memory_order_release);
    content_id_.store(other.content_id_.load(std::memory_order_relaxed),
                      std::memory_order_relaxed);
    return *this;
  }

  std::vector<MeshVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<MeshPrimitiveSpan> primitives;
  std::vector<quader_poly::CircularUvInterpolation> circular_uv_interpolations;

  [[nodiscard]] std::size_t byte_size() const noexcept {
    return vertices.size() * sizeof(MeshVertex) +
           indices.size() * sizeof(std::uint32_t) +
           primitives.size() * sizeof(MeshPrimitiveSpan) +
           circular_uv_interpolations.size() * sizeof(quader_poly::CircularUvInterpolation);
  }

  /** Hashes effective immutable streams without assembling another payload. */
  [[nodiscard]] static MeshGeometryContentId content_id_for(
      std::span<const MeshVertex> vertices,
      std::span<const std::uint32_t> indices,
      std::span<const MeshPrimitiveSpan> primitives,
      std::span<const QVec3> positions = {},
      std::span<const quader_poly::CircularUvInterpolation> circular_uv_interpolations = {}) noexcept {
    if (!positions.empty() && positions.size() != vertices.size()) return {};
    constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
    std::uint64_t hash = kFnvOffset;
    const auto hash_u32 = [&hash](std::uint32_t value) {
      for (unsigned int byte = 0; byte < 4U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xffU;
        hash *= kFnvPrime;
      }
    };
    const auto hash_size = [&hash_u32](std::size_t value) {
      const std::uint64_t widened = static_cast<std::uint64_t>(value);
      hash_u32(static_cast<std::uint32_t>(widened & 0xffffffffULL));
      hash_u32(static_cast<std::uint32_t>(widened >> 32U));
    };
    const auto hash_float = [&hash_u32](float value) {
      std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
      if (bits == 0x80000000U) {
        bits = 0U;
      }
      hash_u32(bits);
    };

    hash_size(vertices.size());
    for (std::size_t index = 0u; index < vertices.size(); ++index) {
      const MeshVertex &vertex = vertices[index];
      const QVec3 position = positions.empty() ? vertex.position : positions[index];
      hash_float(position.x);
      hash_float(position.y);
      hash_float(position.z);
      hash_float(vertex.normal.x);
      hash_float(vertex.normal.y);
      hash_float(vertex.normal.z);
      hash_float(vertex.uv.x);
      hash_float(vertex.uv.y);
      hash_u32(vertex.rgba);
      hash_u32(vertex.source_vertex_id);
      for (const float value : vertex.uv_carrier) {
        hash_float(value);
      }
      hash_float(vertex.uv_geometric_normal.x);
      hash_float(vertex.uv_geometric_normal.y);
      hash_float(vertex.uv_geometric_normal.z);
      hash_u32(vertex.circular_uv_index);
    }
    hash_size(circular_uv_interpolations.size());
    for (const auto &field : circular_uv_interpolations) {
      for (const QVec3 value : {field.center, field.axis_u, field.axis_v}) {
        hash_float(value.x);
        hash_float(value.y);
        hash_float(value.z);
      }
      for (const float value : field.uv_from_polar) hash_float(value);
    }
    hash_size(indices.size());
    for (const std::uint32_t index : indices) {
      hash_u32(index);
    }
    hash_size(primitives.size());
    for (const MeshPrimitiveSpan &primitive : primitives) {
      hash_u32(primitive.index_offset);
      hash_u32(primitive.index_count);
      hash_u32(primitive.material_slot);
    }
    if (hash == 0) {
      hash = 1;
    }

    return MeshGeometryContentId{hash};
  }

  /** Computes the deterministic ID once, then serves it in O(1). */
  [[nodiscard]] MeshGeometryContentId content_id() const noexcept {
    std::uint64_t hash = content_id_.load(std::memory_order_acquire);
    if (hash != 0) return MeshGeometryContentId{hash};
    hash = content_id_for(vertices, indices, primitives, {}, circular_uv_interpolations).value;
    std::uint64_t expected = 0;
    if (!content_id_.compare_exchange_strong(
            expected, hash, std::memory_order_release,
            std::memory_order_acquire)) {
      hash = expected;
    }
    return MeshGeometryContentId{hash};
  }

  void invalidate_content_id() noexcept {
    wire_geometry_payload_.reset();
    std::atomic_store_explicit(
        &collision_geometry_payload_,
        std::shared_ptr<const MeshCollisionGeometryPayload>{},
        std::memory_order_release);
    content_id_.store(0, std::memory_order_release);
  }

private:
  friend struct MeshAsset;
  friend std::shared_ptr<const MeshCollisionGeometryPayload>
  mesh_collision_geometry_payload(const MeshGeometryPayload &geometry);

  mutable std::shared_ptr<const MeshWireGeometryPayload>
      wire_geometry_payload_;
  // Immutable geometry snapshots share the lazily published collision data.
  mutable std::shared_ptr<const MeshCollisionGeometryPayload>
      collision_geometry_payload_;
  mutable std::atomic<std::uint64_t> content_id_{0};
};

/**
 * Represents a Mesh Asset value used by the asset document and registry layer.
 */
struct MeshAsset final : AssetDocumentBase<MeshAssetRevision> {
  MeshAsset() {
    auto geometry = std::make_shared<MeshGeometryPayload>();
    geometry_ = geometry;
    editable_geometry_ = geometry;
  }

  [[nodiscard]] const MeshGeometryPayload &geometry() const;

  /** Legacy contiguous payload access; retained streams materialize once. */
  [[nodiscard]] std::shared_ptr<const MeshGeometryPayload> geometry_payload() const;

  /** Effective streams remain pinned independently of legacy materialization. */
  [[nodiscard]] const std::shared_ptr<const MeshGeometryProjection> &
  geometry_projection() const noexcept { return geometry_projection_; }
  void share_geometry_projection(std::shared_ptr<const MeshGeometryProjection> projection);
  [[nodiscard]] std::span<const MeshVertex> geometry_vertices() const noexcept;
  [[nodiscard]] std::span<const QVec3> geometry_positions() const noexcept;
  [[nodiscard]] std::span<const std::uint32_t> geometry_indices() const noexcept;
  [[nodiscard]] std::span<const MeshPrimitiveSpan> geometry_primitives() const noexcept;
  [[nodiscard]] std::span<const quader_poly::CircularUvInterpolation>
  geometry_circular_uv_interpolations() const noexcept;
  [[nodiscard]] std::uintptr_t geometry_token() const noexcept;
  [[nodiscard]] std::size_t geometry_byte_size() const noexcept;
  [[nodiscard]] MeshGeometryContentId geometry_content_id() const noexcept;

  /** Returns cached renderer-neutral authored/compiled boundary segments. */
  [[nodiscard]] std::shared_ptr<const MeshWireGeometryPayload>
  wire_geometry_payload() const;

  /** Detaches shared geometry before a caller mutates the returned payload. */
  MeshGeometryPayload &edit_geometry() {
    if (geometry_projection_) {
      geometry_ = geometry_payload();
      geometry_projection_.reset();
    }
    if (geometry_.use_count() == 1) {
      if (std::shared_ptr<MeshGeometryPayload> editable =
              editable_geometry_.lock()) {
        editable->invalidate_content_id();
        invalidate_wire_geometry_payload();
        return *editable;
      }
    }
    auto editable = std::make_shared<MeshGeometryPayload>(*geometry_);
    editable->invalidate_content_id();
    geometry_ = editable;
    editable_geometry_ = editable;
    invalidate_wire_geometry_payload();
    return *editable;
  }

  void replace_geometry(MeshGeometryPayload geometry) {
    geometry_projection_.reset();
    auto replacement =
        std::make_shared<MeshGeometryPayload>(std::move(geometry));
    replacement->invalidate_content_id();
    geometry_ = replacement;
    editable_geometry_ = replacement;
    invalidate_wire_geometry_payload();
  }

  void share_geometry(std::shared_ptr<const MeshGeometryPayload> geometry) {
    geometry_projection_.reset();
    if (geometry) {
      geometry_ = std::move(geometry);
      editable_geometry_.reset();
      invalidate_wire_geometry_payload();
      return;
    }
    auto replacement = std::make_shared<MeshGeometryPayload>();
    geometry_ = replacement;
    editable_geometry_ = replacement;
    invalidate_wire_geometry_payload();
  }

  bool has_stable_cpu_topology = true;
  bool has_authored_polygon_document = false;
  MeshAuthoredPolygonDocument authored_polygon_document;

  quader_poly::Document &edit_authored_polygon_document();
  void replace_authored_polygon_document(quader_poly::Document document);
  void share_authored_polygon_document(
      std::shared_ptr<const quader_poly::Document> document) noexcept {
    authored_polygon_document = std::move(document);
    has_authored_polygon_document =
        static_cast<bool>(authored_polygon_document);
    invalidate_wire_geometry_payload();
  }
  void share_authored_polygon_snapshot(modeling::PolygonDocumentReadSnapshot snapshot) {
    authored_polygon_document.share_snapshot(std::move(snapshot));
    has_authored_polygon_document = true;
    invalidate_wire_geometry_payload();
  }

private:
  void invalidate_wire_geometry_payload() const noexcept {
    wire_geometry_payload_.reset();
    wire_geometry_source_ = 0u;
    wire_geometry_authored_source_ = 0u;
    wire_geometry_authored_enabled_ = false;
  }

  mutable std::shared_ptr<const MeshGeometryPayload> geometry_;
  std::shared_ptr<const MeshGeometryProjection> geometry_projection_;
  mutable std::weak_ptr<MeshGeometryPayload> editable_geometry_;
  mutable std::shared_ptr<const MeshWireGeometryPayload>
      wire_geometry_payload_;
  mutable std::uintptr_t wire_geometry_source_ = 0u;
  mutable std::uintptr_t wire_geometry_authored_source_ = 0u;
  mutable bool wire_geometry_authored_enabled_ = false;
};

inline std::vector<MeshPrimitiveSpan>
normalized_mesh_primitive_spans(const MeshGeometryPayload &geometry) {
  std::vector<MeshPrimitiveSpan> spans;
  spans.reserve(geometry.primitives.empty() ? 1u
                                            : geometry.primitives.size());
  for (const MeshPrimitiveSpan &primitive : geometry.primitives) {
    const std::uint64_t offset = primitive.index_offset;
    const std::uint64_t count = primitive.index_count;
    if (count == 0u || offset >= geometry.indices.size() ||
        offset + count > geometry.indices.size()) {
      continue;
    }
    spans.push_back(primitive);
  }
  if (spans.empty() && !geometry.indices.empty()) {
    spans.push_back(MeshPrimitiveSpan{
        .index_offset = 0,
        .index_count = static_cast<std::uint32_t>(geometry.indices.size()),
        .material_slot = 0,
    });
  }
  return spans;
}

inline std::vector<MeshPrimitiveSpan>
normalized_mesh_primitive_spans(const MeshAsset &mesh) {
  const auto primitives = mesh.geometry_primitives();
  const auto index_count = mesh.geometry_indices().size();
  std::vector<MeshPrimitiveSpan> spans;
  spans.reserve(primitives.empty() ? 1u : primitives.size());
  for (const auto &primitive : primitives) {
    const std::uint64_t end = static_cast<std::uint64_t>(primitive.index_offset) + primitive.index_count;
    if (primitive.index_count != 0u && end <= index_count) spans.push_back(primitive);
  }
  if (spans.empty() && index_count != 0u) {
    spans.push_back({.index_count = static_cast<std::uint32_t>(index_count)});
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
