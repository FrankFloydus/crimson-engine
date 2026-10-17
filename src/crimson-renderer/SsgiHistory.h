#pragma once

#include "RenderTypes.h"
#include "Ssgi.h"
#include <algorithm>
#include <cstdint>

namespace crimson::ssgi {
inline constexpr std::uint16_t halfExtent(std::uint16_t extent) {
    return static_cast<std::uint16_t>((std::uint32_t(extent)+1)/2);
}
struct HistoryIdentity {
    std::uint64_t world{},camera{},environment{};
    bool operator==(const HistoryIdentity&) const = default;
};
// Reprojection handles ordinary movement. Large jumps and projection changes
// deliberately start again rather than presenting stale indirect lighting.
inline bool continuousCamera(const Camera& previous,const Camera& current,float maxDistance) {
    return math::finite(current.position)&&math::finite(current.target)&&math::finite(current.up)&&
        previous.fovAxis==current.fovAxis&&previous.fov==current.fov&&
        previous.nearPlane==current.nearPlane&&previous.farPlane==current.farPlane&&
        math::length(current.eye()-previous.eye())<=std::max(1.f,maxDistance)&&
        math::dot(previous.forward(),current.forward())>.5&&math::dot(previous.up,current.up)>.5;
}
inline bool consecutiveFrame(std::uint64_t previous,std::uint64_t current) {
    return previous!=UINT64_MAX&&current==previous+1;
}
inline bool compatibleSettings(const SsgiSettings& previous,const SsgiSettings& current) {
    // Strength is applied during material shading; captured radiance and
    // irradiance histories are unscaled. Enabled transitions call disable().
    return previous.maxDistance==current.maxDistance&&previous.thickness==current.thickness&&
        previous.quality==current.quality&&previous.temporalStability==current.temporalStability&&previous.bounces==current.bounces&&
        previous.rays==current.rays&&previous.steps==current.steps&&previous.rayBias==current.rayBias&&
        previous.denoiseRadius==current.denoiseRadius&&previous.denoiseStrength==current.denoiseStrength&&previous.radianceClamp==current.radianceClamp;
}
inline bool reusableHistory(std::uint64_t previousFrame,std::uint64_t currentFrame,
    const HistoryIdentity& previousIdentity,const HistoryIdentity& currentIdentity,
    const Camera& previousCamera,const Camera& currentCamera,
    const SsgiSettings& previousSettings,const SsgiSettings& currentSettings) {
    return consecutiveFrame(previousFrame,currentFrame)&&previousIdentity==currentIdentity&&
        compatibleSettings(previousSettings,currentSettings)&&continuousCamera(previousCamera,currentCamera,currentSettings.maxDistance);
}
}
