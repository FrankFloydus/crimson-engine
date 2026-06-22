#include "render/ViewportOverlayPolicy.h"

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

bool viewportOverlayTriangleIsTwoSided(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::ToolPlane ||
            layer == ViewportOverlayLayer::ToolPreviewFill;
}

bool viewportOverlayLineDrawsOnTop(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SelectedFaceEdge ||
            layer == ViewportOverlayLayer::SelectedEdge ||
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
        return 1.0f;
    case ViewportOverlayLayer::SourceWire:
        return 1.5f;
    case ViewportOverlayLayer::SelectedFaceEdge:
    case ViewportOverlayLayer::SelectedEdge:
    case ViewportOverlayLayer::HoverFaceEdge:
    case ViewportOverlayLayer::HoverEdge:
        return 2.5f;
    case ViewportOverlayLayer::OpenEdge:
        return 1.0f;
    case ViewportOverlayLayer::DiagnosticEdge:
        return 4.0f;
    case ViewportOverlayLayer::GroupBounds:
        return 1.5f;
    case ViewportOverlayLayer::ToolPlaneLine:
        return 1.0f;
    case ViewportOverlayLayer::ToolPreviewLine:
        return 1.15f;
    case ViewportOverlayLayer::ToolPreviewSeam:
        return 2.25f;
    case ViewportOverlayLayer::ToolHandle:
        return 2.0f;
    default:
        return 1.0f;
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
