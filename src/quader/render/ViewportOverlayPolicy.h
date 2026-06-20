#pragma once

#include "quader/render/ViewportOverlayFrame.h"

#include <array>
#include <algorithm>
#include <cstdint>

namespace quader {

enum class ViewportOverlayGeometryKind {
    Triangles,
    Lines,
    Points,
};

enum class ViewportOverlayDepthMode {
    DepthTested,
    DrawOnTop,
};

struct ViewportOverlayLayerPolicy {
    ViewportOverlayLayer layer = ViewportOverlayLayer::SourceWire;
    int drawOrder = 0;
    bool depthStampedFaceFill = false;
    bool writesTransientHoverSuppressionMask = false;
    bool suppressedByTransientHover = false;
};

inline constexpr std::array<ViewportOverlayLayer, 15> kViewportOverlayDrawOrder{{
        ViewportOverlayLayer::SelectedVisibleFace,
        ViewportOverlayLayer::SelectedFaceMask,
        ViewportOverlayLayer::SourceWire,
        ViewportOverlayLayer::SelectedXrayFace,
        ViewportOverlayLayer::HoverFace,
        ViewportOverlayLayer::OpenEdge,
        ViewportOverlayLayer::DiagnosticEdge,
        ViewportOverlayLayer::SelectedFaceEdge,
        ViewportOverlayLayer::SelectedEdge,
        ViewportOverlayLayer::HoverFaceEdge,
        ViewportOverlayLayer::HoverEdge,
        ViewportOverlayLayer::UnselectedVertex,
        ViewportOverlayLayer::SelectedVertex,
        ViewportOverlayLayer::HoverVertex,
        ViewportOverlayLayer::SnapTargetVertex,
}};

inline constexpr std::uint32_t kOverlaySelectionFaceColor = 0x081facffU;
inline constexpr std::uint32_t kOverlaySelectionWireColor = 0xff1fd3ffU;
inline constexpr std::uint32_t kOverlaySourceWireColor = 0xebffe05cU;
inline constexpr std::uint32_t kOverlayOpenEdgeColor = 0xff00ffffU;
inline constexpr std::uint32_t kOverlayDiagnosticEdgeColor = 0xff0000ffU;
inline constexpr std::uint32_t kOverlayHoverFaceColor = 0x161fff66U;
inline constexpr std::uint32_t kOverlayHoverWireColor = 0xff00ff51U;
inline constexpr std::uint32_t kOverlayRemoveFaceColor = 0x52ff6b1fU;
inline constexpr std::uint32_t kOverlayRemoveWireColor = 0xffff6b1fU;
inline constexpr std::uint32_t kOverlayVertexColor = 0xffffe66bU;
inline constexpr std::uint32_t kOverlaySelectedVertexColor = 0xff1fadffU;
inline constexpr std::uint32_t kOverlayHoverVertexColor = 0xff38ff00U;
inline constexpr std::uint32_t kOverlayVertexOutlineColor = 0xe6000000U;

inline constexpr float kOverlayLineDepthBias = 0.02f;
inline constexpr float kOverlayPointDepthBias = 0.01f;
inline constexpr float kOverlayFillDepthBias = 0.035f;
inline constexpr float kOverlayLineFeatherPixels = 1.0f;
inline constexpr float kOverlayVertexSizePixels = 7.0f;
inline constexpr float kOverlaySelectedVertexGrowthPixels = 0.5f;
inline constexpr float kOverlayHoverVertexGrowthPixels = 0.5f;
inline constexpr float kOverlayVertexOutlineSizePixels = 1.0f;

inline constexpr int viewportOverlayDrawOrder(ViewportOverlayLayer layer) {
    for (int index = 0; index < static_cast<int>(kViewportOverlayDrawOrder.size()); ++index) {
        if (kViewportOverlayDrawOrder[static_cast<std::size_t>(index)] == layer) {
            return index;
        }
    }
    return static_cast<int>(kViewportOverlayDrawOrder.size());
}

inline constexpr bool viewportOverlayLayerUsesDepthStampedFaceFill(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SelectedVisibleFace ||
            layer == ViewportOverlayLayer::SelectedXrayFace ||
            layer == ViewportOverlayLayer::SelectedFaceMask ||
            layer == ViewportOverlayLayer::HoverFace;
}

inline constexpr bool viewportOverlayLineDrawsOnTop(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SelectedFaceEdge ||
            layer == ViewportOverlayLayer::SelectedEdge ||
            layer == ViewportOverlayLayer::DiagnosticEdge;
}

inline constexpr bool viewportOverlayPointDrawsOnTop(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SelectedVertex ||
            layer == ViewportOverlayLayer::HoverVertex ||
            layer == ViewportOverlayLayer::SnapTargetVertex;
}

inline constexpr ViewportOverlayDepthMode viewportOverlayDepthMode(
        ViewportOverlayLayer layer,
        ViewportOverlayGeometryKind kind) {
    if (kind == ViewportOverlayGeometryKind::Lines && viewportOverlayLineDrawsOnTop(layer)) {
        return ViewportOverlayDepthMode::DrawOnTop;
    }
    if (kind == ViewportOverlayGeometryKind::Points && viewportOverlayPointDrawsOnTop(layer)) {
        return ViewportOverlayDepthMode::DrawOnTop;
    }
    return ViewportOverlayDepthMode::DepthTested;
}

inline constexpr bool viewportOverlayDepthTested(ViewportOverlayLayer layer, ViewportOverlayGeometryKind kind) {
    return viewportOverlayDepthMode(layer, kind) == ViewportOverlayDepthMode::DepthTested;
}

inline constexpr float viewportOverlayDepthBias(ViewportOverlayLayer layer, ViewportOverlayGeometryKind kind) {
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

inline constexpr bool viewportOverlayLayerWritesTransientHoverSuppressionMask(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::HoverFace ||
            layer == ViewportOverlayLayer::HoverFaceEdge ||
            layer == ViewportOverlayLayer::HoverEdge ||
            layer == ViewportOverlayLayer::HoverVertex;
}

inline constexpr bool viewportOverlayLayerIsSuppressedByTransientHover(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SelectedVisibleFace ||
            layer == ViewportOverlayLayer::SelectedXrayFace ||
            layer == ViewportOverlayLayer::SelectedFaceMask ||
            layer == ViewportOverlayLayer::SelectedFaceEdge ||
            layer == ViewportOverlayLayer::SelectedEdge ||
            layer == ViewportOverlayLayer::SelectedVertex;
}

inline constexpr ViewportOverlayLayerPolicy viewportOverlayLayerPolicy(ViewportOverlayLayer layer) {
    return {
            .layer = layer,
            .drawOrder = viewportOverlayDrawOrder(layer),
            .depthStampedFaceFill = viewportOverlayLayerUsesDepthStampedFaceFill(layer),
            .writesTransientHoverSuppressionMask = viewportOverlayLayerWritesTransientHoverSuppressionMask(layer),
            .suppressedByTransientHover = viewportOverlayLayerIsSuppressedByTransientHover(layer),
    };
}

inline constexpr float viewportOverlayLineWidthPixels(ViewportOverlayLayer layer, float nativeWidthPixels = 1.0f) {
    (void)nativeWidthPixels;
    switch (layer) {
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
    default:
        return 1.0f;
    }
}

inline constexpr float viewportOverlayPointSizePixels(ViewportOverlayLayer layer, float nativeSizePixels = 8.0f) {
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

inline constexpr float viewportOverlayPointOutlineGrowthPixels(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SelectedVertex ||
                    layer == ViewportOverlayLayer::HoverVertex ||
                    layer == ViewportOverlayLayer::SnapTargetVertex
            ? kOverlayVertexOutlineSizePixels
            : 0.0f;
}

inline constexpr bool viewportOverlayPointHasOutline(ViewportOverlayLayer layer) {
    return viewportOverlayPointOutlineGrowthPixels(layer) > 0.0f;
}

inline constexpr bool viewportOverlayPointIsOutlineOnly(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SnapTargetVertex;
}

inline constexpr float viewportOverlayPointInnerRadius(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SnapTargetVertex ? 0.62f : 0.0f;
}

inline constexpr bool viewportOverlayColorApproximately(std::uint32_t a, std::uint32_t b) {
    return a == b;
}

inline constexpr bool viewportOverlayIsNativeRemoveColor(std::uint32_t color) {
    return viewportOverlayColorApproximately(color, kOverlayRemoveFaceColor) ||
            viewportOverlayColorApproximately(color, kOverlayRemoveWireColor);
}

inline constexpr bool viewportOverlayIsNativeSelectionWireColor(std::uint32_t color) {
    return viewportOverlayColorApproximately(color, kOverlaySelectedVertexColor) ||
            viewportOverlayColorApproximately(color, kOverlaySelectionWireColor);
}

inline constexpr std::uint32_t viewportOverlayColorFor(ViewportOverlayLayer layer, std::uint32_t originalRgba) {
    switch (layer) {
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
    }
    return originalRgba;
}

} // namespace quader
