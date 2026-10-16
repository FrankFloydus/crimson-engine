#include "ActorLightRig.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace crimson {
std::string validateActorLightRigSettings(const ActorLightRigSettings& settings) {
    if(!validCharacterLight(settings.key))return "Character Key light settings are invalid.";
    if(!validCharacterLight(settings.fill))return "Character Fill light settings are invalid.";
    if(!validLocalWorldLightSettings(settings.localWorld))return "Local World Light settings are invalid.";
    if(!validCharacterShadowSettings(settings.characterShadow))return "Character Key shadow settings are invalid.";
    return {};
}

ActorLightRigEvaluation evaluateActorLightRig(const ActorLightRigSettings& settings,
    Vec3 presentedPosition,std::span<const PointLight> existingPoints,
    std::span<const DirectionalLight> existingDirectionals,std::uint64_t owner) {
    ActorLightRigEvaluation result;
    result.error=validateActorLightRigSettings(settings);
    if(!result.valid()||!settings.enabled)return result;
    if(!math::finite(presentedPosition)) {
        result.error="Player light rig requires a finite presented world position.";return result;
    }
    if(settings.localWorld.enabled&&existingPoints.size()>=MaximumClusteredPointLights) {
        result.error="Player Local World Light exceeds the 4096 point-light source limit.";return result;
    }
    result.characterLights={settings.key,settings.fill};
    for(auto& light:result.characterLights)light.direction=math::normalized(light.direction);
    const auto& shadow=settings.characterShadow;
    result.characterShadow={presentedPosition,math::normalized(settings.key.direction),shadow.strength,shadow.distance,
        shadow.shadowDepthBias,shadow.shadowNormalBias,
        shadow.enabled&&settings.key.enabled&&settings.key.intensity>0};
    result.characterShadow.owner=owner;
    if(settings.localWorld.enabled) {
        const auto& settingsLight=settings.localWorld;
        PointLight value;
        value.position=presentedPosition+Vec3{0,settingsLight.height,0};value.color=settingsLight.color;
        value.intensity=settingsLight.intensity;value.enabled=true;
        value.radius=settingsLight.radius;value.nearFalloffRadius=settingsLight.nearFalloffRadius;
        value.specularStrength=settingsLight.specularStrength;value.rangeFalloffExponent=settingsLight.rangeFalloffExponent;
        value.edgeSoftness=settingsLight.edgeSoftness;
        value.sourceRadius=settingsLight.sourceRadius;
        value.attenuation=settingsLight.attenuation;
        value.castShadows=settingsLight.castShadows;value.shadowDepthBias=settingsLight.shadowDepthBias;
        value.shadowNormalBias=settingsLight.shadowNormalBias;
        value.roles.receiverMask=lighting_scope::World;value.roles.shadowCasterMask=lighting_scope::World;
        value.roles.ambientReveal=false;
        // Stable actor-derived identity keeps shadow residency across actor
        // insertion/removal. Collision probing is deterministic in extraction.
        auto id=owner?((owner^UINT64_C(0xa0761d6478bd642f))*UINT64_C(0xe7037ed1a0b428db)):
            std::numeric_limits<std::uint64_t>::max();
        if(!id)id=std::numeric_limits<std::uint64_t>::max();
        const auto occupied=[&](std::uint64_t candidate) {
            return std::any_of(existingPoints.begin(),existingPoints.end(),[&](const auto& p){return p.entityId==candidate;})||
                std::any_of(existingDirectionals.begin(),existingDirectionals.end(),[&](const auto& p){return p.entityId==candidate;});
        };
        while(occupied(id))id=id>1?id-1:std::numeric_limits<std::uint64_t>::max();
        value.entityId=id;result.localWorld=value;
    }
    return result;
}
} // namespace crimson
