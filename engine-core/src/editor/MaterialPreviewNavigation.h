#pragma once
#include "math/Math.h"

namespace crimson {
// Quader material mini-viewport gestures use physical pixels, rotate the
// object, and retain a fixed perspective camera instead of scene navigation.
class MaterialPreviewNavigation {
public:
    float yaw{},pitch{},distance{3.2f};
    Vec2 pan{};
    void tumble(Vec2 pixels);
    void translate(Vec2 pixels);
    void zoom(float steps);
    void reset() { yaw=pitch=0;distance=3.2f;pan={}; }
    Mat4 objectTransform() const;
    static float rotateEnvironment(float degrees,float pixels);
};
}
