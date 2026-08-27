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

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace quader_poly {
struct Document;
}

namespace quader {

struct MeshWireGeometryPayload;

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
        content_id_(other.content_id_.load(std::memory_order_relaxed)) {}

  MeshGeometryPayload(MeshGeometryPayload &&other) noexcept
      : vertices(std::move(other.vertices)), indices(std::move(other.indices)),
        primitives(std::move(other.primitives)),
        content_id_(other.content_id_.load(std::memory_order_relaxed)) {}

  MeshGeometryPayload &operator=(const MeshGeometryPayload &other) {
    if (this == &other) {
      return *this;
    }
    vertices = other.vertices;
    indices = other.indices;
    primitives = other.primitives;
    wire_geometry_payload_.reset();
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
    wire_geometry_payload_.reset();
    content_id_.store(other.content_id_.load(std::memory_order_relaxed),
                      std::memory_order_relaxed);
    return *this;
  }

  std::vector<MeshVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<MeshPrimitiveSpan> primitives;

  [[nodiscard]] std::size_t byte_size() const noexcept {
    return vertices.size() * sizeof(MeshVertex) +
           indices.size() * sizeof(std::uint32_t) +
           primitives.size() * sizeof(MeshPrimitiveSpan);
  }

  /** Computes the deterministic ID once, then serves it in O(1). */
  [[nodiscard]] MeshGeometryContentId content_id() const noexcept {
    std::uint64_t cached = content_id_.load(std::memory_order_acquire);
    if (cached != 0) {
      return MeshGeometryContentId{cached};
    }

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
    for (const MeshVertex &vertex : vertices) {
      hash_float(vertex.position.x);
      hash_float(vertex.position.y);
      hash_float(vertex.position.z);
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
    content_id_.store(0, std::memory_order_release);
  }

private:
  friend struct MeshAsset;

  mutable std::shared_ptr<const MeshWireGeometryPayload>
      wire_geometry_payload_;
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

  [[nodiscard]] const MeshGeometryPayload &geometry() const noexcept {
    return *geometry_;
  }

  /** Returns a lifetime-pinned immutable payload for snapshots and caches. */
  [[nodiscard]] std::shared_ptr<const MeshGeometryPayload>
  geometry_payload() const noexcept {
    (void)geometry_->content_id();
    editable_geometry_.reset();
    return geometry_;
  }

  /** Runtime-stable identity shared by every logical asset using this payload. */
  [[nodiscard]] std::uintptr_t geometry_token() const noexcept {
    return reinterpret_cast<std::uintptr_t>(geometry_.get());
  }

  [[nodiscard]] std::size_t geometry_byte_size() const noexcept {
    return geometry_->byte_size();
  }

  [[nodiscard]] MeshGeometryContentId geometry_content_id() const noexcept {
    (void)geometry_payload();
    return geometry_->content_id();
  }

  /** Returns cached renderer-neutral authored/compiled boundary segments. */
  [[nodiscard]] std::shared_ptr<const MeshWireGeometryPayload>
  wire_geometry_payload() const;

  /** Detaches shared geometry before a caller mutates the returned payload. */
  MeshGeometryPayload &edit_geometry() {
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
    auto replacement =
        std::make_shared<MeshGeometryPayload>(std::move(geometry));
    replacement->invalidate_content_id();
    geometry_ = replacement;
    editable_geometry_ = replacement;
    invalidate_wire_geometry_payload();
  }

  void share_geometry(std::shared_ptr<const MeshGeometryPayload> geometry) {
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
  std::shared_ptr<const quader_poly::Document> authored_polygon_document;

  quader_poly::Document &edit_authored_polygon_document();
  void replace_authored_polygon_document(quader_poly::Document document);
  void share_authored_polygon_document(
      std::shared_ptr<const quader_poly::Document> document) noexcept {
    authored_polygon_document = std::move(document);
    has_authored_polygon_document =
        static_cast<bool>(authored_polygon_document);
    invalidate_wire_geometry_payload();
  }

private:
  void invalidate_wire_geometry_payload() const noexcept {
    wire_geometry_payload_.reset();
    wire_geometry_source_ = nullptr;
    wire_geometry_authored_source_ = nullptr;
    wire_geometry_authored_enabled_ = false;
  }

  std::shared_ptr<const MeshGeometryPayload> geometry_;
  mutable std::weak_ptr<MeshGeometryPayload> editable_geometry_;
  mutable std::shared_ptr<const MeshWireGeometryPayload>
      wire_geometry_payload_;
  mutable const MeshGeometryPayload *wire_geometry_source_ = nullptr;
  mutable const quader_poly::Document *wire_geometry_authored_source_ = nullptr;
  mutable bool wire_geometry_authored_enabled_ = false;
};

inline std::vector<MeshPrimitiveSpan>
normalized_mesh_primitive_spans(const MeshAsset &mesh) {
  const MeshGeometryPayload &geometry = mesh.geometry();
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
