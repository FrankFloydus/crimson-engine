////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/Types.h"
#include "viewport/ViewportInputRouter.h"

#include <imgui.h>

namespace quader {

/**
 * Stores the Viewport Input Frame Build Request data contract used by the ImGui editor presentation layer.
 */
struct ViewportInputFrameBuildRequest {
    bool hovered = false;
    bool focused = false;
    ImVec2 itemMin = {};
    ImVec2 itemSize = {};
    bool routeActiveNavigationInput = false;
    bool useCapturedMouseDelta = false;
    Vec2 capturedMouseDelta = {};
};

/**
 * Builds Viewport Input Frame Builder data for the ImGui editor presentation layer.
 */
class ViewportInputFrameBuilder {
public:
    [[nodiscard]] ViewportInputFrame build(const ViewportInputFrameBuildRequest& request) const;
};

} // namespace quader
