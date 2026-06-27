////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/viewport_overlay_frame.hpp"

namespace quader_editor_core {
struct TransformGizmoFrame;
}

namespace quader {

struct ViewportOverlayCamera;

/**
 * Converts transform gizmo runtime frames into viewport overlay primitives.
 */
class ViewportTransformGizmoOverlayBuilder {
public:
    static void append(
            ViewportOverlayFrame& overlayFrame,
            const quader_editor_core::TransformGizmoFrame& gizmoFrame,
            const ViewportOverlayCamera& camera);
};

} // namespace quader
