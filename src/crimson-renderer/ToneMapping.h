#pragma once

#include "math/Math.h"

namespace crimson {

// Serialized values: keep Reinhard at zero to preserve existing scenes.
enum class ToneMapping { Reinhard = 0, Linear = 1, ACES = 2, AgX = 3 };

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

}
