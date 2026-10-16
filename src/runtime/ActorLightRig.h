#pragma once

#include "crimson-renderer/LightingSettings.h"
#include "crimson-renderer/RenderTypes.h"
#include <array>
#include <cstddef>
#include <span>
#include <optional>
#include <string>

namespace crimson {
// Validates stored fields even while disabled, retaining coherent edits and
// serialization. Empty means valid; no values are silently clamped.
std::string validateActorLightRigSettings(const ActorLightRigSettings&);

struct ActorLightRigEvaluation {
    std::array<CharacterLight,2> characterLights{};
    std::optional<PointLight> localWorld;
    CharacterShadowSource characterShadow{};
    std::string error;
    bool valid() const noexcept {return error.empty();}
};

// Character directions and the local point's height are world aligned.
// Local World illumination is independent of the separate darkness reveal.
// Existing spans include disabled IDs.
ActorLightRigEvaluation evaluateActorLightRig(const ActorLightRigSettings&,
    Vec3 presentedPosition,std::span<const PointLight> existingPoints,
    std::span<const DirectionalLight> existingDirectionals,std::uint64_t owner=0);
} // namespace crimson
