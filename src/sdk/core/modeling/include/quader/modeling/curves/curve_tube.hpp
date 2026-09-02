////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/curves/curve_document.hpp>
#include <quader/modeling/mesh/polygon_document.hpp>
#include <quader/modeling/payloads.hpp>
#include <quader/modeling/result.hpp>

#include <cstdint>

namespace quader::modeling {

/** Configures a renderer-neutral tube derived from one authored Curve. */
struct CurveTubeOptions final {
  double radius = 0.1;
  std::uint32_t radial_resolution = 8U;
  CurveSamplingOptions sampling{};
};

/**
 * Owns both representations produced from one Curve tube evaluation.
 *
 * The renderer-neutral payload retains the seam vertices required for
 * continuous UV interpolation. The authored polygon document instead welds
 * coincident radial and closed-path seam vertices while storing the seams in
 * per-face corner UVs, so it remains suitable for ordinary mesh editing.
 */
struct CurveTubeMesh final {
  MeshPayload payload;
  PolygonDocument document;
};

/**
 * Builds an uncapped editable polygon tube and its matching render payload.
 *
 * The PolygonDocument contains one unique vertex per sampled station and
 * radial side, outward-wound quads, smooth shading, and material slot zero.
 * Closed paths weld the final strip back to the authored start ring.
 */
[[nodiscard]] Result<CurveTubeMesh>
build_curve_tube_mesh(const CurveDocument &document,
                      CurveTubeOptions options = {});

/**
 * Tessellates an uncapped tube around the Curve's adaptive display samples.
 *
 * Rings use rotation-minimizing frames in authored path order. Closed Curves
 * distribute residual frame twist across the loop and duplicate the authored
 * start ring at longitudinal U=1 so the closing strip has a continuous UV
 * seam. The returned payload owns one outward-wound primitive with smooth
 * radial normals and normalized longitudinal/radial UVs.
 */
[[nodiscard]] Result<MeshPayload>
tessellate_curve_tube(const CurveDocument &document,
                      CurveTubeOptions options = {});

} // namespace quader::modeling
