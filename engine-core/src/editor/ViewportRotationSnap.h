#pragma once

#include <cmath>

namespace crimson {
inline constexpr float DefaultRotationSnapDegrees=15.f;

// Query every frame so Shift can enable or release snapping during a drag.
inline const float* rotationGizmoSnap(bool rotating,bool shift,const float& degrees) {
    return rotating&&shift&&std::isfinite(degrees)&&degrees>0.f?&degrees:nullptr;
}
}
