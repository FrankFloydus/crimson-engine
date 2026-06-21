#pragma once

#include "math/Types.h"
#include "viewport/ViewportInputRouter.h"

#include <imgui.h>

namespace quader {

struct ViewportInputFrameBuildRequest {
    bool hovered = false;
    bool focused = false;
    ImVec2 itemMin = {};
    ImVec2 itemSize = {};
    bool routeActiveNavigationInput = false;
    bool useCapturedMouseDelta = false;
    Vec2 capturedMouseDelta = {};
};

class ViewportInputFrameBuilder {
public:
    [[nodiscard]] ViewportInputFrame build(const ViewportInputFrameBuildRequest& request) const;
};

} // namespace quader
