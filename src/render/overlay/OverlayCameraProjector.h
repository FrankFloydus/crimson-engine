#pragma once

#include "math/Types.h"
#include "render/ViewportOverlayFrame.h"
#include "render/overlay/OverlayMeshTypes.h"

#include <optional>

namespace quader {

struct OverlayViewPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct OverlayProjectedPoint {
    OverlayFloat4 device;
    Vec2 pixel;
    bool valid = false;
};

struct OverlayClippedLine {
    OverlayViewPoint start;
    OverlayViewPoint end;
};

class OverlayCameraProjector {
public:
    explicit OverlayCameraProjector(ViewportOverlayCamera camera);

    const ViewportOverlayCamera& camera() const;

    static ViewportOverlayCamera sanitizedCamera(ViewportOverlayCamera camera);

    OverlayViewPoint viewPointFor(Vec3 world) const;

    void applyDepthBias(OverlayViewPoint& point, float depthBias) const;

    float nearPlaneDistance(const OverlayViewPoint& point) const;

    std::optional<OverlayClippedLine> clipLineToNearPlane(Vec3 start, Vec3 end, float depthBias) const;

    OverlayProjectedPoint project(OverlayViewPoint point) const;

    OverlayFloat4 withPixelOffset(OverlayFloat4 device, Vec2 pixelOffset) const;

private:
    static constexpr float kPi = 3.14159265358979323846f;

    ViewportOverlayCamera camera_;
};

} // namespace quader
