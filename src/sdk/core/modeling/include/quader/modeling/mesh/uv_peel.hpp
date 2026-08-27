////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/mesh/polygon_document.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace quader::modeling {

/// Selects which texture axis follows the selected edge-flow guides.
enum class UvPeelAxis {
  U,
  V,
};

/// Captures peel orientation, physical density, repetitions, and UV offsets.
struct UvPeelOptions {
  UvPeelAxis axis = UvPeelAxis::U;
  bool world_space = true;
  double u_repeat = 1.0;
  double v_repeat = 1.0;
  double u_offset = 0.0;
  double v_offset = 0.0;

  friend bool operator==(const UvPeelOptions &, const UvPeelOptions &) = default;
};

/**
 * Borrows an immutable document during preparation. Selected authored edges
 * guide texture flow; they are not seams. The opaque key identifies the host
 * target, while world and sampling transforms remain unmodified host state.
 */
struct UvPeelTarget {
  std::uint64_t key = 0;
  const PolygonDocument *document = nullptr;
  std::vector<EdgeKey> edges;
  std::optional<EdgeKey> active_edge;
  Transform3 world_from_local{};
  Transform3 sampling_from_local{};
};

/// Owns one validated UV-only replacement or an unchanged shared document.
struct PreparedUvPeelDocument {
  std::uint64_t key = 0;
  PolygonDocument document;
  OperationResult receipt;
};

/// Publishes all prepared targets together or none when preparation fails.
struct PreparedUvPeel {
  std::vector<PreparedUvPeelDocument> documents;
  bool changed = false;
};

/// Validates finite positive repetition factors, finite offsets, and the axis.
[[nodiscard]] Result<UvPeelOptions>
validate_uv_peel_options(const UvPeelOptions &options);

/**
 * Prepares guided surface UVs without mutating input documents, topology,
 * selection, materials, or occurrence transforms. Only edge-connected face
 * patches of faces incident to guide edges participate. Unsupported or ambiguous
 * chart topology and unsafe numerical results fail the entire batch.
 */
[[nodiscard]] Result<PreparedUvPeel> prepare_uv_peel(
    std::span<const UvPeelTarget> targets, const UvPeelOptions &options);

} // namespace quader::modeling
