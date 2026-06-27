////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/viewport_overlay_policy.hpp"

#include <cstddef>

namespace quader {

int viewportOverlayDrawOrder(ViewportOverlayLayer layer) {
    for (int index = 0; index < static_cast<int>(kViewportOverlayDrawOrder.size()); ++index) {
        if (kViewportOverlayDrawOrder[static_cast<std::size_t>(index)] == layer) {
            return index;
        }
    }
    return static_cast<int>(kViewportOverlayDrawOrder.size());
}

bool viewportOverlayLayerUsesDepthStampedFaceFill(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SelectedVisibleFace ||
            layer == ViewportOverlayLayer::SelectedXrayFace ||
            layer == ViewportOverlayLayer::SelectedFaceMask ||
            layer == ViewportOverlayLayer::HoverFace;
}

bool viewportOverlayLayerUsesDepthOnlyFaceFill(ViewportOverlayLayer layer) {
    (void)layer;
    return false;
}

bool viewportOverlayTriangleIsTwoSided(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SelectedVisibleFace ||
            layer == ViewportOverlayLayer::SelectedXrayFace ||
            layer == ViewportOverlayLayer::SelectedFaceMask ||
            layer == ViewportOverlayLayer::HoverFace ||
            layer == ViewportOverlayLayer::ToolPlane ||
            layer == ViewportOverlayLayer::ToolHandle ||
            layer == ViewportOverlayLayer::ToolPreviewFill;
}

bool viewportOverlayLineDrawsOnTop(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SourceWire ||
            layer == ViewportOverlayLayer::SelectedFaceEdge ||
            layer == ViewportOverlayLayer::SelectedEdge ||
            layer == ViewportOverlayLayer::HoverFaceEdge ||
            layer == ViewportOverlayLayer::HoverEdge ||
            layer == ViewportOverlayLayer::GroupBounds ||
            layer == ViewportOverlayLayer::DiagnosticEdge ||
            layer == ViewportOverlayLayer::ToolPlaneLine ||
            layer == ViewportOverlayLayer::ToolPreviewLine ||
            layer == ViewportOverlayLayer::ToolPreviewSeam ||
            layer == ViewportOverlayLayer::ToolHandle;
}

bool viewportOverlayPointDrawsOnTop(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SelectedVertex ||
            layer == ViewportOverlayLayer::HoverVertex ||
            layer == ViewportOverlayLayer::SnapTargetVertex ||
            layer == ViewportOverlayLayer::ToolHandle;
}

ViewportOverlayDepthMode viewportOverlayDepthMode(ViewportOverlayLayer layer, ViewportOverlayGeometryKind kind) {
    if (layer == ViewportOverlayLayer::ToolPlane ||
            layer == ViewportOverlayLayer::ToolHandle ||
            layer == ViewportOverlayLayer::ToolPreviewFill) {
        return ViewportOverlayDepthMode::DrawOnTop;
    }
    if (kind == ViewportOverlayGeometryKind::Lines && viewportOverlayLineDrawsOnTop(layer)) {
        return ViewportOverlayDepthMode::DrawOnTop;
    }
    if (kind == ViewportOverlayGeometryKind::Points && viewportOverlayPointDrawsOnTop(layer)) {
        return ViewportOverlayDepthMode::DrawOnTop;
    }
    return ViewportOverlayDepthMode::DepthTested;
}

bool viewportOverlayDepthTested(ViewportOverlayLayer layer, ViewportOverlayGeometryKind kind) {
    return viewportOverlayDepthMode(layer, kind) == ViewportOverlayDepthMode::DepthTested;
}

float viewportOverlayDepthBias(ViewportOverlayLayer layer, ViewportOverlayGeometryKind kind) {
    if (!viewportOverlayDepthTested(layer, kind)) {
        return 0.0f;
    }
    switch (kind) {
    case ViewportOverlayGeometryKind::Triangles:
        return kOverlayFillDepthBias;
    case ViewportOverlayGeometryKind::Lines:
        return kOverlayLineDepthBias;
    case ViewportOverlayGeometryKind::Points:
        return kOverlayPointDepthBias;
    }
    return 0.0f;
}

bool viewportOverlayLayerWritesTransientHoverSuppressionMask(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::HoverFace ||
            layer == ViewportOverlayLayer::HoverFaceEdge ||
            layer == ViewportOverlayLayer::HoverEdge ||
            layer == ViewportOverlayLayer::HoverVertex;
}

bool viewportOverlayLayerIsSuppressedByTransientHover(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SelectedVisibleFace ||
            layer == ViewportOverlayLayer::SelectedXrayFace ||
            layer == ViewportOverlayLayer::SelectedFaceMask ||
            layer == ViewportOverlayLayer::SelectedFaceEdge ||
            layer == ViewportOverlayLayer::SelectedEdge ||
            layer == ViewportOverlayLayer::SelectedVertex;
}

ViewportOverlayLayerPolicy viewportOverlayLayerPolicy(ViewportOverlayLayer layer) {
    return {
            .layer = layer,
            .drawOrder = viewportOverlayDrawOrder(layer),
            .depthStampedFaceFill = viewportOverlayLayerUsesDepthStampedFaceFill(layer),
            .writesTransientHoverSuppressionMask = viewportOverlayLayerWritesTransientHoverSuppressionMask(layer),
            .suppressedByTransientHover = viewportOverlayLayerIsSuppressedByTransientHover(layer),
    };
}

float viewportOverlayLineWidthPixels(ViewportOverlayLayer layer, float nativeWidthPixels) {
    if (nativeWidthPixels > 0.0f) {
        return nativeWidthPixels;
    }
    switch (layer) {
    case ViewportOverlayLayer::WireframeBase:
        return kOverlayWireframeBaseLineWidthPixels;
    case ViewportOverlayLayer::SourceWire:
        return kOverlaySourceWireLineWidthPixels;
    case ViewportOverlayLayer::SelectedFaceEdge:
    case ViewportOverlayLayer::SelectedEdge:
        return kOverlaySelectionWireLineWidthPixels;
    case ViewportOverlayLayer::HoverFaceEdge:
    case ViewportOverlayLayer::HoverEdge:
        return kOverlayHoverWireLineWidthPixels;
    case ViewportOverlayLayer::OpenEdge:
        return kOverlayOpenEdgeLineWidthPixels;
    case ViewportOverlayLayer::DiagnosticEdge:
        return kOverlayDiagnosticEdgeLineWidthPixels;
    case ViewportOverlayLayer::GroupBounds:
        return kOverlayGroupBoundsLineWidthPixels;
    case ViewportOverlayLayer::ToolPlaneLine:
        return kOverlayToolPlaneLineWidthPixels;
    case ViewportOverlayLayer::ToolPreviewLine:
        return kOverlayToolPreviewLineWidthPixels;
    case ViewportOverlayLayer::ToolPreviewSeam:
        return kOverlayToolPreviewSeamLineWidthPixels;
    case ViewportOverlayLayer::ToolHandle:
        return kOverlayToolHandleLineWidthPixels;
    default:
        return kOverlayWireframeBaseLineWidthPixels;
    }
}

float viewportOverlayPointSizePixels(ViewportOverlayLayer layer, float nativeSizePixels) {
    if (nativeSizePixels >= 2.0f) {
        return nativeSizePixels;
    }
    switch (layer) {
    case ViewportOverlayLayer::UnselectedVertex:
        return kOverlayVertexSizePixels;
    case ViewportOverlayLayer::SelectedVertex:
        return kOverlayVertexSizePixels + kOverlaySelectedVertexGrowthPixels;
    case ViewportOverlayLayer::HoverVertex:
        return kOverlayVertexSizePixels + kOverlayHoverVertexGrowthPixels;
    default:
        break;
    }
    return nativeSizePixels < 2.0f ? 2.0f : nativeSizePixels;
}

float viewportOverlayPointOutlineGrowthPixels(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SelectedVertex ||
                    layer == ViewportOverlayLayer::HoverVertex ||
                    layer == ViewportOverlayLayer::SnapTargetVertex
            ? kOverlayVertexOutlineSizePixels
            : 0.0f;
}

bool viewportOverlayPointHasOutline(ViewportOverlayLayer layer) {
    return viewportOverlayPointOutlineGrowthPixels(layer) > 0.0f;
}

bool viewportOverlayPointIsOutlineOnly(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SnapTargetVertex;
}

float viewportOverlayPointInnerRadius(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SnapTargetVertex ? 0.62f : 0.0f;
}

bool viewportOverlayColorApproximately(std::uint32_t a, std::uint32_t b) {
    return a == b;
}

bool viewportOverlayIsNativeRemoveColor(std::uint32_t color) {
    return viewportOverlayColorApproximately(color, kOverlayRemoveFaceColor) ||
            viewportOverlayColorApproximately(color, kOverlayRemoveWireColor);
}

bool viewportOverlayIsNativeSelectionWireColor(std::uint32_t color) {
    return viewportOverlayColorApproximately(color, kOverlaySelectedVertexColor) ||
            viewportOverlayColorApproximately(color, kOverlaySelectionWireColor);
}

std::uint32_t viewportOverlayColorFor(ViewportOverlayLayer layer, std::uint32_t originalRgba) {
    switch (layer) {
    case ViewportOverlayLayer::WireframeBase:
        return originalRgba;
    case ViewportOverlayLayer::SelectedVisibleFace:
    case ViewportOverlayLayer::SelectedXrayFace:
    case ViewportOverlayLayer::SelectedFaceMask:
        return kOverlaySelectionFaceColor;
    case ViewportOverlayLayer::SelectedFaceEdge:
    case ViewportOverlayLayer::SelectedEdge:
        return kOverlaySelectionWireColor;
    case ViewportOverlayLayer::SourceWire:
    case ViewportOverlayLayer::SourceWireDepthStamp:
        return viewportOverlayIsNativeSelectionWireColor(originalRgba) ? kOverlaySelectionWireColor : kOverlaySourceWireColor;
    case ViewportOverlayLayer::OpenEdge:
        return kOverlayOpenEdgeColor;
    case ViewportOverlayLayer::DiagnosticEdge:
        return kOverlayDiagnosticEdgeColor;
    case ViewportOverlayLayer::GroupBounds:
        return kOverlayGroupBoundsColor;
    case ViewportOverlayLayer::HoverFace:
        return viewportOverlayIsNativeRemoveColor(originalRgba) ? kOverlayRemoveFaceColor : kOverlayHoverFaceColor;
    case ViewportOverlayLayer::HoverFaceEdge:
    case ViewportOverlayLayer::HoverEdge:
        return viewportOverlayIsNativeRemoveColor(originalRgba) ? kOverlayRemoveWireColor : kOverlayHoverWireColor;
    case ViewportOverlayLayer::UnselectedVertex:
        return kOverlayVertexColor;
    case ViewportOverlayLayer::SelectedVertex:
        return kOverlaySelectedVertexColor;
    case ViewportOverlayLayer::HoverVertex:
        return viewportOverlayIsNativeRemoveColor(originalRgba) ? kOverlayRemoveWireColor : kOverlayHoverVertexColor;
    case ViewportOverlayLayer::SnapTargetVertex:
        return originalRgba;
    case ViewportOverlayLayer::ToolPlane:
        return kOverlayToolPlaneColor;
    case ViewportOverlayLayer::ToolPlaneLine:
        return kOverlayToolPlaneLineColor;
    case ViewportOverlayLayer::ToolPreviewFill:
    case ViewportOverlayLayer::ToolPreviewLine:
    case ViewportOverlayLayer::ToolPreviewSeam:
        return originalRgba;
    case ViewportOverlayLayer::ToolHandle:
        return kOverlayToolHandleColor;
    }
    return originalRgba;
}

} // namespace quader
