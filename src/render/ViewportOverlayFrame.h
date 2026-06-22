////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/Types.h"

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
};

inline constexpr std::size_t kViewportOverlayLayerCount = 23u;

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
};

/**
 * Enumerates ViewportOverlaySource values used by the render layer.
 */
enum class ViewportOverlaySource {
    Unknown,
    Selection,
    Hover,
    SceneWireframe,
    HierarchySelection,
    GroupBounds,
    Tool,
    Diagnostic,
    External,
};

/**
 * Represents a Viewport Overlay Element Ref value used by the render extraction and Filament resource layer.
 */
struct ViewportOverlayElementRef {
    int objectId = -1;
    int vertexId = -1;
    int edgeA = -1;
    int edgeB = -1;
    int faceId = -1;
};

constexpr std::uint32_t rgba8(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255u) {
    return static_cast<std::uint32_t>(r) |
            (static_cast<std::uint32_t>(g) << 8u) |
            (static_cast<std::uint32_t>(b) << 16u) |
            (static_cast<std::uint32_t>(a) << 24u);
}

/**
 * Represents a Viewport Overlay Line value used by the render extraction and Filament resource layer.
 */
struct ViewportOverlayLine {
    ViewportOverlayLayer layer = ViewportOverlayLayer::SourceWire;
    Vec3 start;
    Vec3 end;
    std::uint32_t color = rgba8(255, 255, 255);
    float widthPixels = 1.0f;
    ViewportOverlayStyleRole styleRole = ViewportOverlayStyleRole::LayerDefault;
    ViewportOverlaySource source = ViewportOverlaySource::Unknown;
    ViewportOverlayElementRef element;
};

/**
 * Represents a Viewport Overlay Triangle value used by the render extraction and Filament resource layer.
 */
struct ViewportOverlayTriangle {
    ViewportOverlayLayer layer = ViewportOverlayLayer::SelectedVisibleFace;
    Vec3 a;
    Vec3 b;
    Vec3 c;
    std::uint32_t color = rgba8(255, 255, 255, 64);
    ViewportOverlayStyleRole styleRole = ViewportOverlayStyleRole::LayerDefault;
    ViewportOverlaySource source = ViewportOverlaySource::Unknown;
    ViewportOverlayElementRef element;
};

/**
 * Represents a Viewport Overlay Point value used by the render extraction and Filament resource layer.
 */
struct ViewportOverlayPoint {
    ViewportOverlayLayer layer = ViewportOverlayLayer::UnselectedVertex;
    Vec3 position;
    std::uint32_t color = rgba8(255, 255, 255);
    float sizePixels = 8.0f;
    ViewportOverlayStyleRole styleRole = ViewportOverlayStyleRole::LayerDefault;
    ViewportOverlaySource source = ViewportOverlaySource::Unknown;
    ViewportOverlayElementRef element;
    std::optional<float> outlineGrowthPixels;
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
    Vec3 eye;
    Vec3 forward = {0.0f, 0.0f, -1.0f};
    Vec3 right = {1.0f, 0.0f, 0.0f};
    Vec3 up = {0.0f, 1.0f, 0.0f};
    Vec2 viewportSize = {1.0f, 1.0f};
    ViewportOverlayProjection projection = ViewportOverlayProjection::Perspective;
    float orthographicSize = 24.0f;
    float fovDegrees = 60.0f;
    float nearPlane = 0.05f;
    float farPlane = 1000.0f;
};

/**
 * Stores the Viewport Overlay Frame data contract used by the render extraction and Filament resource layer.
 */
struct ViewportOverlayFrame {
    std::vector<ViewportOverlayLine> lines;
    std::vector<ViewportOverlayTriangle> triangles;
    std::vector<ViewportOverlayPoint> points;
    bool hoverTargetSelected = false;
};

} // namespace quader
