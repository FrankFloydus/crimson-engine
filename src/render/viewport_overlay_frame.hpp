////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace quader {

/**
 * Enumerates ViewportOverlayLayer values used by the render layer.
 */
enum class ViewportOverlayLayer {
    SelectedVisibleFace = 0,
    SelectedFaceEdge = 1,
    UnselectedVertex = 2,
    SelectedVertex = 3,
    HoverFace = 4,
    HoverFaceEdge = 5,
    HoverEdge = 6,
    HoverVertex = 7,
    SelectedEdge = 8,
    SelectedXrayFace = 9,
    SelectedFaceMask = 10,
    SourceWire = 11,
    OpenEdge = 12,
    SnapTargetVertex = 13,
    DiagnosticEdge = 14,
    ToolPlane = 15,
    ToolPreviewFill = 16,
    ToolPlaneLine = 17,
    ToolPreviewLine = 18,
    ToolPreviewSeam = 19,
    ToolHandle = 20,
    GroupBounds = 21,
    WireframeBase = 22,
    SourceWireDepthStamp = 23,
    WireframeXray = 24,
    PointLightGizmo = 25,
    CollisionFill = 26,
    DiagnosticFace = 27,
};

inline constexpr std::size_t kViewportOverlayLayerCount = 28u;

/**
 * Enumerates ViewportOverlayIconKind values used by render-owned billboard overlays.
 */
enum class ViewportOverlayIconKind {
    PointLight,
};

/**
 * Enumerates ViewportOverlayStyleRole values used by the render layer.
 */
enum class ViewportOverlayStyleRole {
    LayerDefault,
    ExplicitColor,
    SelectionFace,
    SelectionWire,
    SelectedSourceWire,
    SourceWire,
    OpenEdge,
    DiagnosticEdge,
    HoverFace,
    HoverWire,
    RemoveSelectionFace,
    RemoveSelectionWire,
    Vertex,
    SelectedVertex,
    HoverVertex,
    RemoveSelectionVertex,
    SnapTargetVertex,
    GroupBounds,
    WireframeBase,
    ToolPlaneFill,
    ToolPlaneLine,
    ToolHandle,
    ToolPreviewFill,
    ToolPreviewLine,
    ToolPreviewSeam,
    CutPreviewKeepFill,
    CutPreviewKeepBothDiscardFill,
    CutPreviewDiscardFill,
    CutPreviewKeepBothOutline,
    CutPreviewDiscardOutline,
    CutPreviewSeam,
    FacePlaneHoverFace,
    FacePlaneHoverWire,
    ActiveSelectionFace,
    ActiveSelectionWire,
    ActiveSelectedVertex,
    ActiveGroupBounds,
    KnifeReferenceLine,
    LaminaFace,
    LaminaWire,
};

/**
 * Enumerates ViewportOverlaySource values used by the render layer.
 */
enum class ViewportOverlaySource {
    Unknown,
    Selection,
    Hover,
    Gizmo,
    SceneWireframe,
    HierarchySelection,
    GroupBounds,
    Tool,
    Diagnostic,
    ComponentSelection,
    ComponentHover,
    External,
    SceneCurve,
    MeshOutline,
    SelectionDimensions,
};

/**
 * Represents a Viewport Overlay Element Ref value used by the render extraction and Filament resource layer.
 */
struct ViewportOverlayElementRef {
  int object_id = -1;
  int vertex_id = -1;
  int edge_a = -1;
  int edge_b = -1;
  int face_id = -1;
};

/**
 * Carries the world-space interior-side reference for the one hovered edge
 * dimension represented by a viewport overlay frame.
 */
struct ViewportHoverEdgeDimensionContext {
    QVec3 inside_reference;
    ViewportOverlayElementRef element;
};

/** World-axis extents of the geometry participating in selection dimensions. */
struct ViewportSelectionDimensionBounds {
    QVec3 min;
    QVec3 max;
};

/** World-space Knife directions and their unsigned line angle. */
struct ViewportKnifeAngleContext {
    QVec3 start;
    QVec3 reference_end;
    QVec3 cut_point;
    double angle_degrees = 0.0;
};

/** The live authored Knife segment, independent of reference availability. */
struct ViewportKnifeSegmentContext {
    QVec3 start;
    QVec3 end;
};

constexpr std::uint32_t rgba8(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255u) {
    return static_cast<std::uint32_t>(r) |
            (static_cast<std::uint32_t>(g) << 8u) |
            (static_cast<std::uint32_t>(b) << 16u) |
            (static_cast<std::uint32_t>(a) << 24u);
}

inline constexpr std::uint32_t kDefaultViewportActiveSelectionColor =
    rgba8(255, 105, 180);

/// Resolves the primary element tint while retaining the stream's authored opacity.
[[nodiscard]] constexpr std::uint32_t viewport_active_selection_color(
    std::uint32_t color,
    std::uint32_t active_color = kDefaultViewportActiveSelectionColor) {
    const auto alpha = ((color >> 24u) * (active_color >> 24u) + 127u) / 255u;
    return (active_color & 0x00ffffffu) | (alpha << 24u);
}

/**
 * Pins one incident surface plane to an authored mesh vertex. An invalid
 * vertex ID leaves the line's depth plane degenerate at its start endpoint.
 */
struct ViewportOverlaySurfaceSupport {
    QVec3 position;
    int vertex_id = -1;
};

/// Selects an explicit hidden-span presentation without changing layer defaults.
enum class ViewportOverlayLineOcclusionStyle {
    Default,
    Dashed,
};

/**
 * Represents a Viewport Overlay Line value used by the render extraction and Filament resource layer.
 */
struct ViewportOverlayLine {
    ViewportOverlayLayer layer = ViewportOverlayLayer::SourceWire;
    QVec3 start;
    QVec3 end;
    std::uint32_t color = rgba8(255, 255, 255);
    float width_pixels = 1.0f;
    ViewportOverlayStyleRole style_role =
        ViewportOverlayStyleRole::LayerDefault;
    ViewportOverlaySource source = ViewportOverlaySource::Unknown;
    ViewportOverlayElementRef element;
    std::array<ViewportOverlaySurfaceSupport, 2u> surface_support{};
    ViewportOverlayLineOcclusionStyle occlusion_style =
        ViewportOverlayLineOcclusionStyle::Default;
    /// Resolved physical period; zero uses the render policy's logical default.
    float occluded_dash_period_pixels = 0.0f;
};

/**
 * Represents a Viewport Overlay Triangle value used by the render extraction and Filament resource layer.
 */
struct ViewportOverlayTriangle {
    ViewportOverlayLayer layer = ViewportOverlayLayer::SelectedVisibleFace;
    QVec3 a;
    QVec3 b;
    QVec3 c;
    std::uint32_t color = rgba8(255, 255, 255, 64);
    ViewportOverlayStyleRole style_role =
        ViewportOverlayStyleRole::LayerDefault;
    ViewportOverlaySource source = ViewportOverlaySource::Unknown;
    ViewportOverlayElementRef element;
};

/**
 * Represents a Viewport Overlay Point value used by the render extraction and Filament resource layer.
 */
struct ViewportOverlayPoint {
    ViewportOverlayLayer layer = ViewportOverlayLayer::UnselectedVertex;
    QVec3 position;
    std::uint32_t color = rgba8(255, 255, 255);
    float size_pixels = 8.0f;
    ViewportOverlayStyleRole style_role =
        ViewportOverlayStyleRole::LayerDefault;
    ViewportOverlaySource source = ViewportOverlaySource::Unknown;
    ViewportOverlayElementRef element;
    std::optional<float> outline_growth_pixels;
};

/**
 * Represents a Viewport Overlay Icon value used by render-owned billboard gizmos.
 */
struct ViewportOverlayIcon {
    ViewportOverlayLayer layer = ViewportOverlayLayer::PointLightGizmo;
    ViewportOverlayIconKind kind = ViewportOverlayIconKind::PointLight;
    QVec3 position;
    std::uint32_t color = rgba8(255, 255, 255);
    float size_pixels = 28.0f;
    ViewportOverlayStyleRole style_role = ViewportOverlayStyleRole::ExplicitColor;
    ViewportOverlaySource source = ViewportOverlaySource::Gizmo;
    ViewportOverlayElementRef element;
};

/**
 * Enumerates ViewportOverlayProjection values used by the render layer.
 */
enum class ViewportOverlayProjection {
    Perspective,
    Orthographic,
};

/**
 * Represents a Viewport Overlay Camera value used by the render extraction and Filament resource layer.
 */
struct ViewportOverlayCamera {
    QVec3 eye;
    QVec3 forward = {0.0f, 0.0f, -1.0f};
    QVec3 right = {1.0f, 0.0f, 0.0f};
    QVec3 up = {0.0f, 1.0f, 0.0f};
    QVec2 viewport_size = {1.0f, 1.0f};
    ViewportOverlayProjection projection = ViewportOverlayProjection::Perspective;
    float orthographic_size = 24.0f;
    float fov_degrees = 60.0f;
    float near_plane = 0.05f;
    float far_plane = 1000.0f;
};

/**
 * Stores the Viewport Overlay Frame data contract used by the render extraction and Filament resource layer.
 */
struct ViewportOverlayFrame {
    std::vector<ViewportOverlayLine> lines;
    std::vector<ViewportOverlayTriangle> triangles;
    std::vector<ViewportOverlayPoint> points;
    std::vector<ViewportOverlayIcon> icons;
    std::optional<ViewportHoverEdgeDimensionContext>
        hover_edge_dimension_context;
    bool hover_target_selected = false;
    std::optional<ViewportKnifeAngleContext> knife_angle_context;
    std::optional<ViewportKnifeSegmentContext> knife_segment_context;
};

} // namespace quader
