////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "uv_peel_structured.hpp"
#include <quader/modeling/neutral/polygon/document_model.hpp>

#include <span>
#include <vector>

namespace quader::modeling::detail {

/** One compatible face's shared analytic corner field and base-space descriptor. */
struct CircularPeelFace {
  StructuredPeelFaceCoordinates coordinates;
  quader_poly::CircularUvInterpolation interpolation;
};

/** Unsupported geometry returns no annotation and keeps ordinary Peel intact. */
[[nodiscard]] std::vector<CircularPeelFace> fit_circular_peel(
    const quader_poly::Document &document,
    std::span<const quader_poly::ElementId> guide,
    std::span<const StructuredPeelFaceCoordinates> faces);

} // namespace quader::modeling::detail
