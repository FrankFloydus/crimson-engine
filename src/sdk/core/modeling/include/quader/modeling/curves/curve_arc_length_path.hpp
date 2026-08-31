////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/curves/curve_document.hpp>

#include <memory>
#include <span>

namespace quader::modeling {

/**
 * Describes one world-space position along a Curve path.
 *
 * tangent is a normalized path direction when one can be resolved. A wholly
 * zero-length path returns the zero vector. segment and parameter retain the
 * exact authored provenance used to evaluate position.
 */
struct CurvePathEvaluation final {
  Vec3 position{};
  Vec3 tangent{};
  CurveSegmentId segment{};
  double parameter = 0.0;
};

/** Associates one adaptive evaluation with its world-space path distance. */
struct CurveArcLengthStation final {
  double distance = 0.0;
  CurvePathEvaluation evaluation{};
};

struct CurveArcLengthPathData;

/**
 * Pins a bounded adaptive arc-length approximation of one transformed Curve.
 *
 * Sampling happens after world_from_curve is applied, so distances use world
 * units even under non-uniform scale or shear. The approximation bounds both
 * geometric chord error and nonlinear progress along each parameter interval;
 * this keeps distance lookup valid for collinear Beziers with non-uniform
 * parameter speed. Authored Curve state is never mutated.
 */
class CurveArcLengthPath final {
public:
  [[nodiscard]] static Result<CurveArcLengthPath>
  build(const CurveDocument &document, Transform3 world_from_curve = {},
        CurveSamplingOptions options = {});

  [[nodiscard]] double total_length() const noexcept;
  [[nodiscard]] bool closed() const noexcept;
  /**
   * Borrows immutable stations in path order for this value's lifetime.
   * Equal-distance entries are intentional at authored segment boundaries.
   */
  [[nodiscard]] std::span<const CurveArcLengthStation> stations() const noexcept;

  /**
   * Evaluates one finite distance in the inclusive [0, total_length()] range.
   * Repeated zero-length stations are retained to preserve segment provenance.
   * An empty Curve has no evaluable position. A single-point or coincident path
   * can be evaluated only at distance zero.
   */
  [[nodiscard]] Result<CurvePathEvaluation>
  evaluate_at_distance(double distance) const;

private:
  explicit CurveArcLengthPath(
      std::shared_ptr<const CurveArcLengthPathData> data) noexcept;

  std::shared_ptr<const CurveArcLengthPathData> data_;
};

} // namespace quader::modeling
