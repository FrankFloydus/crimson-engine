////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/mesh/face_texture_actions.hpp>

#include <array>
#include <string>
#include <vector>

namespace quader::modeling {

/// Borrows one immutable face and its occurrence placement and texture sampling.
struct FaceTextureCanvasInput {
  std::uint64_t key = 0;
  const PolygonDocument *document = nullptr;
  FaceId face{};
  Transform3 world_from_local{};
  Transform3 sampling_from_local{};
};

/// Maps physical plane coordinates to normalized effective UVs, before material UVs.
struct FaceTextureCanvasMap {
  /// Row-major coefficients: u_x, u_y, v_x, v_y.
  std::array<double, 4> uv_from_plane{1.0, 0.0, 0.0, 1.0};
  std::array<double, 2> uv_offset{};
  friend bool operator==(const FaceTextureCanvasMap &,
                         const FaceTextureCanvasMap &) = default;
};

/// Owns a geometry-derived orthonormal chart whose outline never depends on UVs.
struct FaceTextureCanvasChart {
  /// Coordinates are world units; the first corner is the plane origin.
  std::vector<std::array<double, 2>> outline;
  FaceTextureCanvasMap map;
  bool mapping_usable = false;
  std::string disabled_reason;
};

/**
 * Captures the physical outline and affine effective texture mapping. Valid
 * geometry with a warped or singular texture mapping retains its outline and
 * reports mapping_usable=false without rewriting the authored UVs.
 */
[[nodiscard]] Result<FaceTextureCanvasChart>
query_face_texture_canvas(const FaceTextureCanvasInput &input);

/**
 * Prepares an immutable face replacement from a drag-start document and an
 * absolute desired map. Retains geometry, selection, face settings, material
 * slots and occurrence sampling. A semantically unchanged map shares the input
 * publication, allowing the host to preserve history on a return to origin.
 */
[[nodiscard]] Result<PreparedFaceTextureDocument> prepare_face_texture_canvas(
    const FaceTextureCanvasInput &input, const FaceTextureCanvasMap &map);

} // namespace quader::modeling
