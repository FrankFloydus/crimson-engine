////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/viewport_camera_controller.hpp"

#include <algorithm>
#include <optional>

#include <imgui.h>

namespace quader {

inline float viewportDistanceSquared(ImVec2 a, ImVec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

inline float viewportDistanceToSegmentSquared(ImVec2 point, ImVec2 a, ImVec2 b) {
    const ImVec2 ab{b.x - a.x, b.y - a.y};
    const ImVec2 ap{point.x - a.x, point.y - a.y};
    const float lengthSquared = ab.x * ab.x + ab.y * ab.y;
    if (lengthSquared <= 0.0001f) {
        return viewportDistanceSquared(point, a);
    }

    const float t = std::clamp((ap.x * ab.x + ap.y * ab.y) / lengthSquared, 0.0f, 1.0f);
    const ImVec2 closest{a.x + ab.x * t, a.y + ab.y * t};
    return viewportDistanceSquared(point, closest);
}

inline std::optional<ImVec2> projectWorldToViewport(
        QVec3 world,
        const ViewportCameraController& camera,
        ImVec2 itemMin,
        ImVec2 itemSize) {
    (void)itemSize;

    const ViewportScreenPoint projected = camera.projectWorldPoint(world);
    if (projected.depth <= camera.state().nearPlane || projected.depth >= camera.state().farPlane) {
        return std::nullopt;
    }

    return ImVec2{
            itemMin.x + projected.position.x,
            itemMin.y + projected.position.y,
    };
}

} // namespace quader
