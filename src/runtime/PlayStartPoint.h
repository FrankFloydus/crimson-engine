#pragma once
#include "crimson-renderer/RenderTypes.h"

namespace crimson {
struct PlayStartPoint {
    Vec3 position; // World-space capsule feet at the clicked surface.
    Camera view;
};
}
