////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"

#include <quader/modeling/curves/curve_document.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace quader::modeling {
class CurveDocument;
}

namespace quader {

/** Reports renderer-owned Curve tube derivation and reuse. */
struct CurveSolidGeometryCacheStats final {
  std::uint64_t build_count = 0u;
  std::uint64_t hit_count = 0u;
  std::uint64_t failure_count = 0u;
  std::uint64_t eviction_count = 0u;
  std::uint64_t oversize_bypass_count = 0u;
  std::size_t entry_count = 0u;
  std::size_t cached_bytes = 0u;
};

/**
 * Retains immutable mesh payloads derived from authored Curves.
 *
 * The exact authored content publication and every tessellation option form
 * the key. Failed derivations are retained too, preventing an invalid Curve
 * from being resampled every render frame.
 */
class CurveSolidGeometryCache final {
public:
  static constexpr std::size_t kDefaultCapacity = 128u;
  static constexpr std::size_t kDefaultByteCapacity =
      128u * 1024u * 1024u;

  explicit CurveSolidGeometryCache(
      std::size_t capacity = kDefaultCapacity,
      std::size_t byte_capacity = kDefaultByteCapacity) noexcept;

  [[nodiscard]] std::shared_ptr<const MeshGeometryPayload> geometry(
      const modeling::CurveDocument &document, double radius,
      std::uint32_t radial_resolution,
      modeling::CurveSamplingOptions sampling = {});

  void clear() noexcept;

  [[nodiscard]] CurveSolidGeometryCacheStats stats() const noexcept;

private:
  /// Identifies one exact authored Curve and tessellation request.
  struct Key final {
    modeling::CurveContentIdentity content_identity{};
    std::uint64_t content_revision = 0u;
    std::uint64_t radius_bits = 0u;
    std::uint32_t radial_resolution = 0u;
    std::uint64_t tolerance_bits = 0u;
    std::uint32_t max_depth = 0u;
    std::uint32_t max_samples = 0u;

    friend bool operator==(const Key &, const Key &) = default;
  };

  /// Retains a successful or failed derivation for one exact cache key.
  struct Entry final {
    Key key;
    std::shared_ptr<const MeshGeometryPayload> geometry;
    std::size_t bytes = 0u;
    std::uint64_t last_use = 0u;
  };

  std::size_t capacity_ = kDefaultCapacity;
  std::size_t byte_capacity_ = kDefaultByteCapacity;
  std::size_t cached_bytes_ = 0u;
  std::uint64_t use_sequence_ = 0u;
  std::uint64_t build_count_ = 0u;
  std::uint64_t hit_count_ = 0u;
  std::uint64_t failure_count_ = 0u;
  std::uint64_t eviction_count_ = 0u;
  std::uint64_t oversize_bypass_count_ = 0u;
  std::vector<Entry> entries_;
};

} // namespace quader
