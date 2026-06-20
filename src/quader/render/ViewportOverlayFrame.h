#pragma once

#include "quader/math/Types.h"

#include <cstdint>
#include <vector>

namespace quader {

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
};

constexpr std::uint32_t rgba8(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255u) {
    return static_cast<std::uint32_t>(r) |
            (static_cast<std::uint32_t>(g) << 8u) |
            (static_cast<std::uint32_t>(b) << 16u) |
            (static_cast<std::uint32_t>(a) << 24u);
}

struct ViewportOverlayLine {
    ViewportOverlayLayer layer = ViewportOverlayLayer::SourceWire;
    Vec3 start;
    Vec3 end;
    std::uint32_t color = rgba8(255, 255, 255);
    float widthPixels = 1.0f;
};

struct ViewportOverlayTriangle {
    ViewportOverlayLayer layer = ViewportOverlayLayer::SelectedVisibleFace;
    Vec3 a;
    Vec3 b;
    Vec3 c;
    std::uint32_t color = rgba8(255, 255, 255, 64);
};

struct ViewportOverlayPoint {
    ViewportOverlayLayer layer = ViewportOverlayLayer::UnselectedVertex;
    Vec3 position;
    std::uint32_t color = rgba8(255, 255, 255);
    float sizePixels = 8.0f;
};

enum class ViewportOverlayProjection {
    Perspective,
    Orthographic,
};

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

struct ViewportOverlayFrame {
    std::vector<ViewportOverlayLine> lines;
    std::vector<ViewportOverlayTriangle> triangles;
    std::vector<ViewportOverlayPoint> points;
    bool hoverTargetSelected = false;
};

} // namespace quader
