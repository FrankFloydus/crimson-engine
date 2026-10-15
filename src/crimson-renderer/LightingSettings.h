#pragma once

#include "math/Math.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace crimson {
namespace lighting_scope {
inline constexpr std::uint32_t World=1,Character=2,All=World|Character;
}

// These roles are independent; direct-light exclusion must not remove an
// object's shadow or stop it receiving ambient visibility.
struct SurfaceLightingScopes {
    std::uint32_t directReceiverMask{lighting_scope::All};
    std::uint32_t shadowCasterMask{lighting_scope::All};
    std::uint32_t ambientReceiverMask{lighting_scope::All};
    bool operator==(const SurfaceLightingScopes&) const = default;
};
struct LightRoles {
    std::uint32_t receiverMask{lighting_scope::All};
    std::uint32_t shadowCasterMask{lighting_scope::All};
    std::uint32_t ambientRevealMask{lighting_scope::All};
    bool ambientReveal{false};
    float ambientRevealStrength{1};
    bool operator==(const LightRoles&) const = default;
};
struct AmbientVisibilitySettings {
    bool enabled{false};
    float strength{1},darknessFloor{0},threshold{.1f},transition{.25f};
    bool shadowAware{true};
    bool attenuateEmission{false};
    bool operator==(const AmbientVisibilitySettings&) const = default;
};

// Character-only directional irradiance. It has no position, source slot or
// world shadow map, so nearby architecture cannot block character readability.
struct CharacterLight {
    bool enabled{false};
    Vec3 direction{-.5f,-.8f,-.6f},color{1,.96f,.9f};
    float intensity{},diffuse{1},specular{1};
    bool operator==(const CharacterLight&) const = default;
};
struct LocalWorldLightSettings {
    bool enabled{true};
    float height{2};
    Vec3 color{1,.96f,.9f};
    float intensity{64.f/9.f};
    float radius{8},nearFalloffRadius{1.5f},specularStrength{0};
    float rangeFalloffExponent{1},edgeSoftness{1};
    float sourceRadius{0}; // Emitter size in meters: shadow penumbra and specular extent.
    float attenuation{2}; // Distance decay exponent, independent of the finite range window.
    bool castShadows{true};
    float shadowDepthBias{.005f},shadowNormalBias{1};
    bool operator==(const LocalWorldLightSettings&) const = default;
};
// The Key owns this silhouette. Its direction and effective enable state are
// derived from the Key, never independently authored.
struct CharacterShadowSettings {
    bool enabled{true};
    float strength{.8f},distance{20};
    float shadowDepthBias{.005f},shadowNormalBias{1};
    bool operator==(const CharacterShadowSettings&) const = default;
};
// Separate player-caster shadow contribution. No ordinary direct-light slot.
struct CharacterShadowSource {
    Vec3 position{},direction{-.5f,-.8f,-.6f};
    float strength{},distance{20},shadowDepthBias{.005f},shadowNormalBias{1};
    bool enabled{false};
    bool operator==(const CharacterShadowSource&) const = default;
};
struct PlayerLightRigSettings {
    bool enabled{false};
    CharacterLight key=[] {CharacterLight v;v.enabled=true;v.intensity=1.5f;return v;}();
    CharacterLight fill=[] {
        CharacterLight v;v.enabled=true;v.direction={.7f,-.4f,.6f};
        v.color={.55f,.65f,1};v.intensity=.35f;return v;
    }();
    LocalWorldLightSettings localWorld{};
    CharacterShadowSettings characterShadow{};
    bool showGizmos{false};
    bool operator==(const PlayerLightRigSettings&) const = default;
};
struct CharacterRevealSettings {
    bool enabled{true};
    float spread{8}; // Half visibility distance of the independent player darkness reveal.
    bool operator==(const CharacterRevealSettings&) const = default;
};
// A visibility range, not a punctual source: no direct radiance or shadow map.
struct PlayerRevealSource {
    Vec3 position{};
    float spread{};
    bool enabled{false};
    bool operator==(const PlayerRevealSource&) const = default;
};

// Bound the visibility center without restricting or rewriting an authored
// Character capsule height. Local World Light has its own independent height.
inline float characterLightingCenterHeight(float characterHeight) noexcept {
    return std::clamp(characterHeight*.5f,0.f,100000.f);
}
// The Gaussian tail tapers to zero between six and eight spreads. Callers
// validate/sanitize the authored spread before deriving its spatial bounds.
inline constexpr float characterLightingSupport(float spread) noexcept {
    return 8.f*spread;
}

inline bool validLightingMask(std::uint32_t value) noexcept {
    return (value&~lighting_scope::All)==0;
}
inline bool validSurfaceLightingScopes(const SurfaceLightingScopes& value) noexcept {
    return validLightingMask(value.directReceiverMask)&&validLightingMask(value.shadowCasterMask)&&
        validLightingMask(value.ambientReceiverMask);
}
inline bool validLightRoles(const LightRoles& value) noexcept {
    return validLightingMask(value.receiverMask)&&validLightingMask(value.shadowCasterMask)&&
        validLightingMask(value.ambientRevealMask)&&std::isfinite(value.ambientRevealStrength)&&
        value.ambientRevealStrength>=0&&value.ambientRevealStrength<=1;
}
inline bool validAmbientVisibilitySettings(const AmbientVisibilitySettings& value) noexcept {
    return std::isfinite(value.strength)&&value.strength>=0&&value.strength<=1&&
        std::isfinite(value.darknessFloor)&&value.darknessFloor>=0&&value.darknessFloor<=1&&
        std::isfinite(value.threshold)&&value.threshold>=0&&value.threshold<=10000&&
        std::isfinite(value.transition)&&value.transition>.0001f&&value.transition<=1;
}
inline bool validLightColor(const Vec3& color) noexcept {
    return math::finite(color)&&std::all_of(color.begin(),color.end(),[](float v){return v>=0&&v<=1;});
}
inline bool validLightDirection(const Vec3& direction) noexcept {
    return math::finite(direction)&&math::length(direction)>=1e-6&&
        std::all_of(direction.begin(),direction.end(),[](float v){return std::abs(v)<=1000000;});
}
inline bool validLightIntensity(float v) noexcept {return std::isfinite(v)&&v>=0&&v<=10000;}
inline bool validLightRange(float v) noexcept {return std::isfinite(v)&&v>=.01f&&v<=100000;}
inline bool validShadowBiases(float depth,float normal) noexcept {
    return std::isfinite(depth)&&depth>=0&&std::isfinite(normal)&&normal>=0;
}
inline bool validCharacterLight(const CharacterLight& value) noexcept {
    return validLightDirection(value.direction)&&validLightColor(value.color)&&validLightIntensity(value.intensity)&&
        std::isfinite(value.diffuse)&&value.diffuse>=0&&value.diffuse<=1&&
        std::isfinite(value.specular)&&value.specular>=0&&value.specular<=1;
}
inline bool validPointLightResponse(float nearRadius,float specular,float exponent,float softness,float sourceRadius=0,float attenuation=2) noexcept {
    return std::isfinite(nearRadius)&&nearRadius>=0&&nearRadius<=100000&&
        std::isfinite(specular)&&specular>=0&&specular<=1&&
        std::isfinite(exponent)&&exponent>=.25f&&exponent<=8&&
        std::isfinite(softness)&&softness>=.05f&&softness<=1&&
        std::isfinite(sourceRadius)&&sourceRadius>=0&&sourceRadius<=100000&&
        std::isfinite(attenuation)&&attenuation>=-10&&attenuation<=10;
}
inline bool validLocalWorldLightSettings(const LocalWorldLightSettings& value) noexcept {
    return std::isfinite(value.height)&&value.height>=.01f&&value.height<=100000&&
        validLightColor(value.color)&&std::isfinite(value.intensity)&&value.intensity>=0&&value.intensity<=1000000&&validLightRange(value.radius)&&
        validPointLightResponse(value.nearFalloffRadius,value.specularStrength,value.rangeFalloffExponent,value.edgeSoftness,value.sourceRadius,value.attenuation)&&
        validShadowBiases(value.shadowDepthBias,value.shadowNormalBias);
}
inline bool validCharacterShadowSettings(const CharacterShadowSettings& value) noexcept {
    return std::isfinite(value.strength)&&value.strength>=0&&value.strength<=1&&
        validLightRange(value.distance)&&validShadowBiases(value.shadowDepthBias,value.shadowNormalBias);
}
inline bool validPlayerLightRigSettings(const PlayerLightRigSettings& value) noexcept {
    return validCharacterLight(value.key)&&validCharacterLight(value.fill)&&
        validLocalWorldLightSettings(value.localWorld)&&validCharacterShadowSettings(value.characterShadow);
}
inline bool validCharacterRevealSettings(const CharacterRevealSettings& value) noexcept {
    return std::isfinite(value.spread)&&value.spread>=.01f&&value.spread<=100000;
}
} // namespace crimson
