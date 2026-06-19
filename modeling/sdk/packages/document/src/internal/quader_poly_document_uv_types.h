#pragma once

#include <quader_poly/document.h>

#include <limits>

namespace quader_poly::document_internal {

struct FaceUvBounds {
    Vec2 min { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() };
    Vec2 max { -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
    bool valid = false;
};

} // namespace quader_poly::document_internal
