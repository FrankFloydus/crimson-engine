////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/curves/curve_document.hpp>
#include <quader/modeling/result.hpp>

namespace quader::modeling {

/**
 * Creates an origin-centered, counterclockwise cubic Bezier circle in XY.
 * The first anchor lies on +X. Radius must be finite, positive, and
 * representable by CurveDocument storage.
 */
[[nodiscard]] Result<CurveDocument> make_xy_circle_curve(double radius);

/**
 * Creates an origin-centered, counterclockwise horizontal stadium in XY.
 * Total length is its complete -X to +X tip distance and must be at least
 * twice the finite positive radius. Equality returns the canonical circle.
 */
[[nodiscard]] Result<CurveDocument>
make_xy_stadium_curve(double radius, double total_length);

} // namespace quader::modeling
