////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/viewport_overlay_frame.hpp"

#include <array>
#include <cstdint>

namespace quader {

/**
 * Enumerates ViewportOverlayGeometryKind values used by the render layer.
 */
enum class ViewportOverlayGeometryKind {
    Triangles,
    Lines,
    Points,
};

/**
 * Enumerates ViewportOverlayDepthMode values used by the render layer.
 */
enum class ViewportOverlayDepthMode {
    DepthTested,
    DrawOnTop,
};

/**
 * Stores the Viewport Overlay Layer Policy data contract used by the render extraction and Filament resource layer.
 */
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
        ViewportOverlayLayer::WireframeBase,
        ViewportOverlayLayer::SelectedXrayFace,
        ViewportOverlayLayer::HoverFace,
        ViewportOverlayLayer::SourceWireDepthStamp,
        ViewportOverlayLayer::SourceWire,
        ViewportOverlayLayer::OpenEdge,
        ViewportOverlayLayer::DiagnosticEdge,
        ViewportOverlayLayer::SelectedFaceEdge,
        ViewportOverlayLayer::SelectedEdge,
        ViewportOverlayLayer::GroupBounds,
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
inline constexpr std::uint32_t kOverlayGroupBoundsColor = rgba8(255, 230, 0, 255);
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
inline constexpr std::uint32_t kOverlayToolAxisXColor = rgba8(242, 46, 41, 255);
inline constexpr std::uint32_t kOverlayToolAxisYColor = rgba8(51, 209, 66, 255);
inline constexpr std::uint32_t kOverlayToolAxisZColor = rgba8(51, 107, 255, 255);
inline constexpr std::uint32_t kOverlayToolAxisMixedColor = rgba8(235, 240, 245, 255);
inline constexpr std::uint32_t kOverlayToolAxisHighlightColor = rgba8(255, 235, 20, 255);
inline constexpr std::uint32_t kOverlayToolCenterColor = rgba8(255, 31, 219, 255);
inline constexpr std::uint32_t kOverlayToolLineShadowColor = rgba8(0, 0, 0, 112);
inline constexpr std::uint32_t kOverlayToolTrackballColor = rgba8(191, 191, 191, 118);
inline constexpr std::uint32_t kOverlayToolPlaneXYColor = rgba8(255, 31, 41, 255);
inline constexpr std::uint32_t kOverlayToolPlaneYZColor = rgba8(5, 219, 46, 255);
inline constexpr std::uint32_t kOverlayToolPlaneXZColor = rgba8(0, 184, 255, 255);
inline constexpr std::uint32_t kOverlayToolBoxPreviewLineColor = rgba8(255, 235, 41, 209);
inline constexpr std::uint32_t kOverlaySelectionMarqueeFillColor = rgba8(255, 214, 0, 31);
inline constexpr std::uint32_t kOverlaySelectionMarqueeLineColor = rgba8(255, 214, 0, 255);
inline constexpr std::uint32_t kOverlayCutPreviewKeepFillColor = rgba8(13, 184, 242, 41);
inline constexpr std::uint32_t kOverlayCutPreviewKeepBothDiscardFillColor = rgba8(255, 184, 46, 41);
inline constexpr std::uint32_t kOverlayCutPreviewDiscardFillColor = rgba8(255, 31, 15, 66);
inline constexpr std::uint32_t kOverlayCutPreviewKeepBothOutlineColor = rgba8(255, 184, 46, 230);
inline constexpr std::uint32_t kOverlayCutPreviewDiscardOutlineColor = rgba8(255, 15, 5, 240);
inline constexpr std::uint32_t kOverlayCutPreviewSeamColor = rgba8(255, 122, 5, 255);

inline constexpr float kOverlayLineDepthBias = 1.0f;
inline constexpr float kOverlayPointDepthBias = 1.5f;
inline constexpr float kOverlayFillDepthBias = 0.035f;
inline constexpr float kOverlayDeviceDepthBiasPerNearPlane = 0.0025f;
inline constexpr float kOverlayLineFeatherPixels = 1.0f;
inline constexpr float kOverlayWireframeBaseLineWidthPixels = 1.0f;
inline constexpr float kOverlaySourceWireLineWidthPixels = 1.5f;
inline constexpr float kOverlaySelectionWireLineWidthPixels = 2.5f;
inline constexpr float kOverlayHoverWireLineWidthPixels = 2.5f;
inline constexpr float kOverlayOpenEdgeLineWidthPixels = 1.0f;
inline constexpr float kOverlayDiagnosticEdgeLineWidthPixels = 4.0f;
inline constexpr float kOverlayGroupBoundsLineWidthPixels = 1.5f;
inline constexpr float kOverlayToolPlaneLineWidthPixels = 1.0f;
inline constexpr float kOverlayToolPreviewLineWidthPixels = 1.15f;
inline constexpr float kOverlayToolPreviewSeamLineWidthPixels = 2.25f;
inline constexpr float kOverlayToolHandleLineWidthPixels = 2.0f;
inline constexpr float kOverlayVertexSizePixels = 7.0f;
inline constexpr float kOverlaySelectedVertexGrowthPixels = 0.5f;
inline constexpr float kOverlayHoverVertexGrowthPixels = 0.5f;
inline constexpr float kOverlayVertexOutlineSizePixels = 1.0f;

int viewportOverlayDrawOrder(ViewportOverlayLayer layer);
bool viewportOverlayLayerUsesDepthStampedFaceFill(ViewportOverlayLayer layer);
bool viewportOverlayLayerUsesDepthOnlyFaceFill(ViewportOverlayLayer layer);
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
