#pragma once

#include "math/Math.h"
#include <array>
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
// Ordinary linear colors display without tone mapping. HDR colors normalize
// their peak for a hue-preserving swatch only; authored RGB is never changed.
Vec4 hdrColorDisplay(const Vec4& linearRgba);

struct HdrColorEdit {
    bool changed{},activated{},active{},deactivated{};
    void include(bool changed=false);
};
struct HdrColorEditorState {
    HdrColorDecomposition decomposition;
    Vec3 synchronized{};
    bool initialized{};
    bool expanded{};
    std::uint32_t activeItem{};
    // Transient widget geometry also allows private-context input tests without
    // a native window or implementation-dependent ImGui numeric-component IDs.
    Vec2 disclosureMinimum{},disclosureMaximum{},swatchMinimum{},swatchMaximum{};
    std::array<Vec2,3> channelMinimum{},channelMaximum{};
    Vec2 opacityMinimum{},opacityMaximum{};
    void cancel();
};

// The caller owns document begin/update/commit/cancel. A full picker is useful
// inside a popup; compact controls show a swatch with optional linear channels.
// The normalized picker changes hue without discarding existing HDR strength.
HdrColorEdit drawHdrColorControl(const char* label,Vec3& linearRgb,float* opacity,
    HdrColorEditorState&,bool fullPicker=false,float maximum=1000000.f,const char* alphaLabel="Opacity");

} // namespace crimson
