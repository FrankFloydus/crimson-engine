#pragma once

#include "math/Math.h"
#include <cstdint>

namespace crimson {

// Hue is an ordinary display-sRGB color. Intensity is an exposure multiplier
// on its decoded linear RGB; neither value is serialized separately.
struct HdrColorDecomposition {
    Vec3 hueSrgb{1,1,1};
    float intensityEv{};
};
HdrColorDecomposition decomposeHdrColor(const Vec3& linearRgb);
Vec3 composeHdrColor(const HdrColorDecomposition&,float maximum=1000000.f);
// A hue-preserving display mapping for editor swatches only. Alpha is unchanged.
Vec4 hdrColorDisplay(const Vec4& linearRgba);

struct HdrColorEdit {
    bool changed{},activated{},active{},deactivated{};
    void include(bool changed=false);
};
struct HdrColorEditorState {
    HdrColorDecomposition decomposition;
    Vec3 synchronized{};
    bool initialized{};
    std::uint32_t activeItem{};
    // Transient widget geometry also allows private-context input tests without
    // a native window or implementation-dependent ImGui numeric-component IDs.
    Vec2 intensityMinimum{},intensityMaximum{},opacityMinimum{},opacityMaximum{};
    void cancel();
};

// The caller owns document begin/update/commit/cancel. A full picker is useful
// inside a popup; the compact form uses the usual ImGui color field and popup.
HdrColorEdit drawHdrColorControl(const char* label,Vec3& linearRgb,float* opacity,
    HdrColorEditorState&,bool fullPicker=false,float maximum=1000000.f);

} // namespace crimson
