#pragma once

#include "math/Math.h"

namespace crimson {

// Serialized values: keep Reinhard at zero to preserve existing scenes.
enum class ToneMapping { Reinhard = 0, Linear = 1, ACES = 2, AgX = 3 };

struct ToneMappingParameters {
    // Zero retains the original unbounded Reinhard shoulder.
    float reinhardWhite{0.f};
    float acesWhite{1.f};
    float agxWhite{16.29f};
    float agxContrast{1.25f};
    bool operator==(const ToneMappingParameters&) const = default;
};
bool validToneMappingParameters(const ToneMappingParameters& value) noexcept;

// Precompute the Godot SDR curve coefficients outside frame submission's
// fragment work. These map directly to u_toneCurve and u_toneShoulder.
struct ToneMappingUniforms {
    Vec4 curve; // Reinhard inverse white squared, ACES denominator, AgX contrast/toe A.
    Vec4 shoulder; // AgX slope/W, reserved ZW.
};
ToneMappingUniforms toneMappingUniforms(const ToneMappingParameters& value) noexcept;

inline constexpr bool validToneMapping(ToneMapping value) {
    return value == ToneMapping::Reinhard || value == ToneMapping::Linear ||
        value == ToneMapping::ACES || value == ToneMapping::AgX;
}
inline constexpr const char* toneMappingName(ToneMapping value) {
    switch (value) {
    case ToneMapping::Linear: return "Linear";
    case ToneMapping::ACES: return "ACES";
    case ToneMapping::AgX: return "AgX";
    default: return "Reinhard";
    }
}

// CPU reference for tone_mapping.sh. Input is nonnegative scene-linear RGB with
// exposure already applied. Output is display-linear RGB, not sRGB encoded.
// Gamut/display clipping belongs to final encoding, after the editor matte/grid.
Vec3 toneMap(Vec3 sceneLinear, ToneMapping mode);
Vec3 toneMap(Vec3 sceneLinear, ToneMapping mode, const ToneMappingParameters& parameters);

}
