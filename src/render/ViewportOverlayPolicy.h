#pragma once

#include "render/ViewportOverlayFrame.h"

#include <array>
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

inline constexpr std::array<ViewportOverlayLayer, kViewportOverlayLayerCount> kViewportOverlayDrawOrder{{
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
        ViewportOverlayLayer::ToolPlane,
        ViewportOverlayLayer::ToolPreviewFill,
        ViewportOverlayLayer::ToolPlaneLine,
        ViewportOverlayLayer::ToolPreviewLine,
        ViewportOverlayLayer::ToolPreviewSeam,
        ViewportOverlayLayer::ToolHandle,
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
inline constexpr std::uint32_t kOverlayToolPlaneColor = rgba8(209, 219, 224, 46);
inline constexpr std::uint32_t kOverlayToolPlaneLineColor = rgba8(235, 245, 255, 148);
inline constexpr std::uint32_t kOverlayToolHandleColor = rgba8(0, 245, 255, 255);
inline constexpr std::uint32_t kOverlayCutPreviewKeepFillColor = rgba8(13, 184, 242, 41);
inline constexpr std::uint32_t kOverlayCutPreviewKeepBothDiscardFillColor = rgba8(255, 184, 46, 41);
inline constexpr std::uint32_t kOverlayCutPreviewDiscardFillColor = rgba8(255, 31, 15, 66);
inline constexpr std::uint32_t kOverlayCutPreviewKeepBothOutlineColor = rgba8(255, 184, 46, 230);
inline constexpr std::uint32_t kOverlayCutPreviewDiscardOutlineColor = rgba8(255, 15, 5, 240);
inline constexpr std::uint32_t kOverlayCutPreviewSeamColor = rgba8(255, 122, 5, 255);

inline constexpr float kOverlayLineDepthBias = 0.02f;
inline constexpr float kOverlayPointDepthBias = 0.01f;
inline constexpr float kOverlayFillDepthBias = 0.035f;
inline constexpr float kOverlayLineFeatherPixels = 1.0f;
inline constexpr float kOverlayVertexSizePixels = 7.0f;
inline constexpr float kOverlaySelectedVertexGrowthPixels = 0.5f;
inline constexpr float kOverlayHoverVertexGrowthPixels = 0.5f;
inline constexpr float kOverlayVertexOutlineSizePixels = 1.0f;

int viewportOverlayDrawOrder(ViewportOverlayLayer layer);
bool viewportOverlayLayerUsesDepthStampedFaceFill(ViewportOverlayLayer layer);
bool viewportOverlayTriangleIsTwoSided(ViewportOverlayLayer layer);
bool viewportOverlayLineDrawsOnTop(ViewportOverlayLayer layer);
bool viewportOverlayPointDrawsOnTop(ViewportOverlayLayer layer);
ViewportOverlayDepthMode viewportOverlayDepthMode(ViewportOverlayLayer layer, ViewportOverlayGeometryKind kind);
bool viewportOverlayDepthTested(ViewportOverlayLayer layer, ViewportOverlayGeometryKind kind);
float viewportOverlayDepthBias(ViewportOverlayLayer layer, ViewportOverlayGeometryKind kind);
bool viewportOverlayLayerWritesTransientHoverSuppressionMask(ViewportOverlayLayer layer);
bool viewportOverlayLayerIsSuppressedByTransientHover(ViewportOverlayLayer layer);
ViewportOverlayLayerPolicy viewportOverlayLayerPolicy(ViewportOverlayLayer layer);
float viewportOverlayLineWidthPixels(ViewportOverlayLayer layer, float nativeWidthPixels = 1.0f);
float viewportOverlayPointSizePixels(ViewportOverlayLayer layer, float nativeSizePixels = 8.0f);
float viewportOverlayPointOutlineGrowthPixels(ViewportOverlayLayer layer);
bool viewportOverlayPointHasOutline(ViewportOverlayLayer layer);
bool viewportOverlayPointIsOutlineOnly(ViewportOverlayLayer layer);
float viewportOverlayPointInnerRadius(ViewportOverlayLayer layer);
bool viewportOverlayColorApproximately(std::uint32_t a, std::uint32_t b);
bool viewportOverlayIsNativeRemoveColor(std::uint32_t color);
bool viewportOverlayIsNativeSelectionWireColor(std::uint32_t color);
std::uint32_t viewportOverlayColorFor(ViewportOverlayLayer layer, std::uint32_t originalRgba);

} // namespace quader
