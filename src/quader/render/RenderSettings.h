#pragma once

#include "quader/math/Types.h"

namespace quader {

enum class ViewportRenderMode {
    TexturedUnlit,
    LitPreview,
};

struct EnvironmentDocument {
    Vec3 clearColor = {0.051f, 0.051f, 0.051f};
    float clearAlpha = 1.0f;
};

struct RenderSettings {
    ViewportRenderMode viewportMode = ViewportRenderMode::TexturedUnlit;
    EnvironmentDocument environment;
};

} // namespace quader
