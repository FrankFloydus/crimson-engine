////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/Types.h"

#include <algorithm>
#include <cmath>

namespace quader {

inline constexpr float kViewportOverlayDashPixels = 8.0f;
inline constexpr float kViewportOverlayDashGapPixels = 5.0f;

template <typename AppendSegment>
void appendViewportOverlayDashedEdge(
        QVec2 start,
        QVec2 end,
        bool dashed,
        AppendSegment appendSegment) {
    const QVec2 delta = end - start;
    const float edgeLength = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (edgeLength <= 0.001f) {
        return;
    }
    if (!dashed) {
        appendSegment(start, end, 0.0f, 1.0f);
        return;
    }

    for (float offset = 0.0f; offset < edgeLength; offset += kViewportOverlayDashPixels + kViewportOverlayDashGapPixels) {
        const float segmentEnd = std::min(offset + kViewportOverlayDashPixels, edgeLength);
        const float startT = offset / edgeLength;
        const float endT = segmentEnd / edgeLength;
        appendSegment(
                start + delta * startT,
                start + delta * endT,
                startT,
                endT);
    }
}

} // namespace quader
