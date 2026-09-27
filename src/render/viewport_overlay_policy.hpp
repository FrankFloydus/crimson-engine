////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/viewport_overlay_frame.hpp"
#include "viewport/viewport_pixel_scale.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace quader {

/**
 * Enumerates ViewportOverlayGeometryKind values used by the render layer.
 */
enum class ViewportOverlayGeometryKind {
    Triangles,
    Lines,
    Points,
    Icons,
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
    int draw_order = 0;
    bool writes_transient_hover_suppression_mask = false;
    bool suppressed_by_transient_hover = false;
};

inline constexpr std::array<ViewportOverlayLayer, kViewportOverlayLayerCount> kViewportOverlayDrawOrder{{
        ViewportOverlayLayer::CollisionFill,
        ViewportOverlayLayer::SelectedVisibleFace,
        ViewportOverlayLayer::SelectedFaceMask,
        ViewportOverlayLayer::WireframeBase,
        ViewportOverlayLayer::WireframeXray,
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
        ViewportOverlayLayer::PointLightGizmo,
        ViewportOverlayLayer::ToolPlane,
        ViewportOverlayLayer::ToolPreviewFill,
        ViewportOverlayLayer::ToolPlaneLine,
        ViewportOverlayLayer::ToolPreviewLine,
        ViewportOverlayLayer::ToolPreviewSeam,
        ViewportOverlayLayer::ToolHandle,
}};

inline constexpr std::uint32_t kOverlaySelectionFaceColor = 0x081facffU;
inline constexpr std::uint32_t kOverlaySelectionWireColor = 0xff1fd3ffU;
inline constexpr std::uint32_t kOverlayCurveColor = rgba8(224, 231, 239, 255);
inline constexpr std::uint32_t kOverlayInstanceSelectionFillColor =
    rgba8(201, 92, 88, 89);
inline constexpr std::uint32_t kOverlayWireframeXrayColor = rgba8(238, 242, 247, 230);
inline constexpr std::uint32_t kOverlaySourceWireColor = 0xebffe05cU;
inline constexpr std::uint32_t kOverlayOpenEdgeColor = 0xff00ffffU;
inline constexpr std::uint32_t kOverlayDiagnosticEdgeColor = 0xff0000ffU;
inline constexpr std::uint32_t kOverlayGroupBoundsColor = rgba8(255, 230, 0, 255);
inline constexpr std::uint32_t kOverlayHoverFaceColor = 0x161fff66U;
inline constexpr std::uint32_t kOverlayHoverWireColor = 0xff00ff51U;
inline constexpr std::uint32_t kOverlayFacePlaneHoverFaceColor =
    rgba8(255, 36, 52, 70);
inline constexpr std::uint32_t kOverlayFacePlaneHoverWireColor =
    rgba8(255, 36, 52, 255);
inline constexpr std::uint32_t kOverlayRemoveFaceColor = 0x52ff6b1fU;
inline constexpr std::uint32_t kOverlayRemoveWireColor = 0xffff6b1fU;
inline constexpr std::uint32_t kOverlayVertexColor = 0xffffe66bU;
inline constexpr std::uint32_t kOverlaySelectedVertexColor = 0xff1fadffU;
inline constexpr std::uint32_t kOverlayHoverVertexColor = 0xff38ff00U;
inline constexpr std::uint32_t kOverlayVertexOutlineColor = 0xe6000000U;
inline constexpr std::uint32_t kOverlayToolPlaneColor = rgba8(209, 219, 224, 46);
inline constexpr std::uint32_t kOverlayToolPlaneLineColor = rgba8(235, 245, 255, 148);
inline constexpr std::uint32_t kOverlayToolHandleColor = rgba8(0, 245, 255, 255);
inline constexpr std::uint32_t kOverlayPivotHandleColor = rgba8(255, 214, 0, 255);
inline constexpr std::uint32_t kOverlayPrimitiveCubeFaceColor =
    rgba8(230, 62, 62, 255);
inline constexpr std::uint32_t kOverlayPrimitiveCubeFaceHighlightColor =
    rgba8(255, 128, 112, 255);
inline constexpr std::uint32_t kOverlayPrimitiveCubeFaceOutlineColor =
    rgba8(255, 255, 255, 255);
inline constexpr float kOverlayCircularHandleInnerRadius = 0.72f;
inline constexpr std::uint32_t kOverlayPrimitiveCubeSubdivisionColor =
    rgba8(255, 214, 0, 255);
inline constexpr std::uint32_t kOverlayPrimitiveCubeSubdivisionHighlightColor =
    rgba8(255, 239, 128, 255);
inline constexpr std::uint32_t kOverlayPrimitiveCubeSubdivisionDisabledColor =
    rgba8(170, 144, 32, 255);
inline constexpr std::uint32_t kOverlayPrimitiveCubeSubdivisionGlyphColor =
    rgba8(0, 0, 0, 255);
inline constexpr std::uint32_t kOverlayToolAxisXColor = rgba8(245, 51, 82, 230);
inline constexpr std::uint32_t kOverlayToolAxisYColor = rgba8(135, 214, 3, 230);
inline constexpr std::uint32_t kOverlayToolAxisZColor = rgba8(41, 140, 245, 230);
inline constexpr std::uint32_t kOverlayToolAxisMixedColor = rgba8(238, 242, 247, 230);
inline constexpr std::uint32_t kOverlayToolAxisXHighlightColor = rgba8(255, 204, 211, 255);
inline constexpr std::uint32_t kOverlayToolAxisYHighlightColor = rgba8(230, 255, 192, 255);
inline constexpr std::uint32_t kOverlayToolAxisZHighlightColor = rgba8(202, 228, 255, 255);
inline constexpr std::uint32_t kOverlayToolAxisHighlightColor = rgba8(255, 250, 204, 255);
inline constexpr std::uint32_t kOverlayTransformGizmoMoveCenterColor =
    rgba8(253, 255, 136, 255);
inline constexpr std::uint32_t kOverlayToolLineShadowColor = rgba8(0, 0, 0, 112);
inline constexpr std::uint32_t kOverlayToolTrackballColor = rgba8(210, 214, 219, 138);
inline constexpr std::uint32_t kOverlayTransformGizmoTrackballHoverFillColor =
    rgba8(255, 255, 255, 8);
inline constexpr std::uint32_t kOverlayTransformGizmoTrackballActiveFillColor =
    rgba8(255, 255, 255, 12);
inline constexpr std::uint32_t kOverlayToolPlaneXYColor = kOverlayToolAxisZColor;
inline constexpr std::uint32_t kOverlayToolPlaneYZColor = kOverlayToolAxisXColor;
inline constexpr std::uint32_t kOverlayToolPlaneXZColor = kOverlayToolAxisYColor;
inline constexpr std::uint32_t kOverlayTransformGizmoRotatePieFillColor = rgba8(156, 162, 168, 92);
inline constexpr std::uint32_t kOverlayTransformGizmoRotatePieLineColor = rgba8(218, 224, 230, 188);
inline constexpr std::uint32_t kOverlayToolBoxPreviewLineColor = rgba8(255, 235, 41, 209);
inline constexpr std::uint32_t kOverlaySelectionMarqueeFillColor = rgba8(255, 214, 0, 31);
inline constexpr std::uint32_t kOverlaySelectionMarqueeLineColor = rgba8(255, 214, 0, 255);
inline constexpr std::uint32_t kOverlaySelectionLassoFillColor = rgba8(255, 255, 255, 16);
inline constexpr std::uint32_t kOverlaySelectionLassoDarkLineColor = rgba8(102, 102, 102, 255);
inline constexpr std::uint32_t kOverlaySelectionLassoLightLineColor = rgba8(255, 255, 255, 255);
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
inline constexpr float kOverlayCurveLineWidthPixels = 2.0f;
inline constexpr float kOverlayWireframeXrayLineWidthPixels = 1.35f;
inline constexpr float kOverlaySourceWireLineWidthPixels = 1.5f;
inline constexpr float kOverlaySelectionWireLineWidthPixels = 2.5f;
inline constexpr float kOverlayHoverWireLineWidthPixels = 2.5f;
inline constexpr float kOverlayOpenEdgeLineWidthPixels = 1.0f;
inline constexpr float kOverlayDiagnosticEdgeLineWidthPixels = 4.0f;
inline constexpr float kOverlayGroupBoundsLineWidthPixels = 1.5f;
inline constexpr float kOverlaySelectionDimensionLineWidthPixels = 2.0f;
inline constexpr float kOverlayToolPlaneLineWidthPixels = 1.0f;
inline constexpr float kOverlayToolPreviewLineWidthPixels = 1.15f;
inline constexpr float kOverlayToolPreviewSeamLineWidthPixels = 2.25f;
inline constexpr float kOverlayToolHandleLineWidthPixels = 2.0f;
inline constexpr float kOverlayToolBoxPreviewLineWidthPixels = 2.0f;
inline constexpr float kOverlaySelectionMarqueeLineWidthPixels = 1.5f;
inline constexpr float kOverlaySelectionLassoLineWidthPixels = 1.0f;
inline constexpr float kOverlaySelectionLassoDashPeriodPixels = 2.0f;
inline constexpr float kOverlaySelectionLassoDashFactor = 0.5f;
inline constexpr float kOverlayDrawnPlanePointSizePixels = 8.0f;
inline constexpr float kOverlayDrawnPlaneLineWidthPixels = 2.0f;
inline constexpr float kOverlayLightMarkerPointSizePixels = 13.0f;
inline constexpr float kOverlayLightMarkerOutlineGrowthPixels = 2.0f;
inline constexpr float kOverlayPointLightGizmoSizePixels = 51.2f;
inline constexpr std::uint32_t kOverlayPointLightRadiusWireColor =
    rgba8(255, 255, 255, 89);
inline constexpr std::uint32_t kOverlayPointLightRadiusSilhouetteColor =
    rgba8(255, 255, 255, 255);
inline constexpr float kOverlayPointLightRadiusLineWidthPixels = 1.0f;
inline constexpr float kOverlayPointLightRadiusHandleSizePixels = 12.0f;
inline constexpr float kOverlayTransformGizmoSizePixels = 80.0f;
inline constexpr float kOverlayTransformGizmoPickRadiusPixels = 12.0f;
inline constexpr float kOverlayTransformGizmoMoveCenterRadiusPixels = 10.0f;
inline constexpr float kOverlayTransformGizmoMoveCenterBorderWidthPixels =
    1.0f;
inline constexpr float kOverlayTransformGizmoMoveCenterDotSizePixels = 4.0f;
inline constexpr float kOverlayTransformGizmoScaleHandleSizePixels = 11.2f;
inline constexpr float kOverlayTransformGizmoScaleCenterSizePixels = 10.0f;
inline constexpr float kOverlayTransformGizmoAxisLineWidthPixels = 1.6f;
inline constexpr float kOverlayVertexSizePixels = 7.0f;
inline constexpr float kOverlaySelectedVertexGrowthPixels = 0.5f;
inline constexpr float kOverlayHoverVertexGrowthPixels = 0.5f;
inline constexpr float kOverlayVertexOutlineSizePixels = 1.0f;
inline constexpr float kViewportOverlayRasterScaleMinimum =
    kViewportPixelScaleMinimum;
inline constexpr float kViewportOverlayRasterScaleMaximum =
    kViewportPixelScaleMaximum;
float viewport_overlay_sanitized_raster_scale(float raster_scale);
int viewport_overlay_draw_order(ViewportOverlayLayer layer);
bool viewport_overlay_triangle_is_two_sided(ViewportOverlayLayer layer);
bool viewport_overlay_line_draws_on_top(ViewportOverlayLayer layer);
bool viewport_overlay_point_draws_on_top(ViewportOverlayLayer layer);
ViewportOverlayDepthMode
viewport_overlay_depth_mode(ViewportOverlayLayer layer,
                            ViewportOverlayGeometryKind kind);
ViewportOverlayDepthMode
viewport_overlay_depth_mode(const ViewportOverlayLine &line);
ViewportOverlayDepthMode
viewport_overlay_depth_mode(const ViewportOverlayPoint &point);
bool viewport_overlay_depth_tested(ViewportOverlayLayer layer,
                                   ViewportOverlayGeometryKind kind);
float viewport_overlay_depth_bias(ViewportOverlayDepthMode mode,
                                  ViewportOverlayGeometryKind kind);
float viewport_overlay_depth_bias(ViewportOverlayLayer layer,
                                  ViewportOverlayGeometryKind kind);
bool viewport_overlay_layer_writes_transient_hover_suppression_mask(
    ViewportOverlayLayer layer);
bool viewport_overlay_layer_is_suppressed_by_transient_hover(
    ViewportOverlayLayer layer);
ViewportOverlayLayerPolicy
viewport_overlay_layer_policy(ViewportOverlayLayer layer);
float viewport_overlay_line_width_pixels(ViewportOverlayLayer layer,
                                         float native_width_pixels = 1.0f);
float viewport_overlay_point_size_pixels(ViewportOverlayLayer layer,
                                         float native_size_pixels = 8.0f);
float viewport_overlay_point_outline_growth_pixels(ViewportOverlayLayer layer);
bool viewport_overlay_point_has_outline(ViewportOverlayLayer layer);
bool viewport_overlay_point_is_outline_only(ViewportOverlayLayer layer);
float viewport_overlay_point_inner_radius(ViewportOverlayLayer layer);
bool viewport_overlay_color_approximately(std::uint32_t a, std::uint32_t b);
bool viewport_overlay_is_native_remove_color(std::uint32_t color);
bool viewport_overlay_is_native_selection_wire_color(std::uint32_t color);
bool selected_object_face_fills_enabled_for_selection_count(
    std::size_t selected_mesh_count);
std::uint32_t viewport_overlay_color_for(ViewportOverlayLayer layer,
                                         std::uint32_t original_rgba);

} // namespace quader
