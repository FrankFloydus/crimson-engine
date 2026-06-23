////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document.h>

#include <limits>

namespace quader_poly::document_internal {

/**
 * Represents a Face UV Bounds value used by the polygon document and mesh editing core.
 */
struct FaceUvBounds {
    quader::QVec2 min { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() };
    quader::QVec2 max { -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
    bool valid = false;
};

} // namespace quader_poly::document_internal
