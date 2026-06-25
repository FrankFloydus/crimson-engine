////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/ViewportBoxOverlayBuilder.h"

#include "render/ViewportOverlayPolicy.h"

#include <array>

namespace quader {
namespace {

ViewportOverlayLine boxPreviewLine(QVec3 start, QVec3 end) {
    return {
            .layer = ViewportOverlayLayer::ToolPreviewLine,
            .start = start,
            .end = end,
            .color = kOverlayToolBoxPreviewLineColor,
            .widthPixels = 2.0f,
            .styleRole = ViewportOverlayStyleRole::ExplicitColor,
            .source = ViewportOverlaySource::Tool,
    };
}

void appendLine(ViewportOverlayFrame& overlayFrame, QVec3 start, QVec3 end) {
    overlayFrame.lines.push_back(boxPreviewLine(start, end));
}

} // namespace

void ViewportBoxOverlayBuilder::append(ViewportOverlayFrame& overlayFrame, const ModelingBoxPreview& preview) {
    if (!preview.valid) {
        return;
    }

    if (preview.cornerCount >= 8U) {
        constexpr std::array<std::array<int, 2>, 12> edges{{
                {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
                {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
                {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
        }};

        for (const std::array<int, 2>& edge : edges) {
            appendLine(
                    overlayFrame,
                    preview.corners[static_cast<std::size_t>(edge[0])],
                    preview.corners[static_cast<std::size_t>(edge[1])]);
        }
        return;
    }

    if (preview.footprintCount < 4U) {
        return;
    }
    for (std::size_t index = 0; index < preview.footprintCount; ++index) {
        appendLine(
                overlayFrame,
                preview.footprint[index],
                preview.footprint[(index + 1U) % preview.footprintCount]);
    }
}

} // namespace quader
