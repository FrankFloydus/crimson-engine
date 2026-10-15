#include "runtime/PlayerLightRig.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(Vec3 actual,Vec3 expected) {
    require(math::finite(actual)&&math::length(actual-expected)<1e-5,"Unexpected player lighting world value.");
}
PlayerLightRigEvaluation evaluateRig(const PlayerLightRigSettings& settings,Vec3 position,
    std::span<const PointLight> points,std::span<const DirectionalLight> suns) {
    return evaluatePlayerLightRig(settings,position,points,suns);
}
PlayerLightRigSettings fullRig() {PlayerLightRigSettings value;value.enabled=true;return value;}
void separatedDefaultsAndPresentedFollow() {
    PlayerLightRigSettings settings;
    require(validPlayerLightRigSettings(settings)&&validatePlayerLightRigSettings(settings).empty(),"Default rig is invalid.");
    require(!settings.enabled&&settings.key.enabled&&settings.fill.enabled&&!settings.showGizmos,
        "Old scenes acquired enabled player lighting.");
    require(settings.localWorld.intensity==64.f/9.f&&settings.localWorld.height==2&&settings.localWorld.radius==8&&
        settings.localWorld.nearFalloffRadius==1.5f&&settings.localWorld.rangeFalloffExponent==1&&
        settings.localWorld.edgeSoftness==1&&settings.localWorld.specularStrength==0&&settings.localWorld.sourceRadius==0&&settings.localWorld.attenuation==2,
        "Default Local World Light attenuation controls changed.");
    require(PointLight{}.nearFalloffRadius==0&&PointLight{}.specularStrength==1&&PointLight{}.rangeFalloffExponent==4&&
        PointLight{}.edgeSoftness==1&&PointLight{}.sourceRadius==0&&PointLight{}.attenuation==2,
        "Player light defaults changed ordinary point lighting.");
    auto disabled=evaluateRig(settings,{}, {},{});
    require(disabled.valid()&&!disabled.localWorld&&!disabled.characterLights[0].enabled&&!disabled.characterShadow.enabled,
        "A disabled rig published contributions.");
    settings=fullRig();const Vec3 presented{12.25f,3,-19.5f},translation{.125f,-1.5f,4};
    const auto first=evaluateRig(settings,presented,{},{});
    const auto moved=evaluateRig(settings,presented+translation,{},{});
    require(first.valid()&&moved.valid()&&first.localWorld&&moved.localWorld,"Local world source was not published.");
    near(first.localWorld->position,presented+Vec3{0,settings.localWorld.height,0});
    near(moved.localWorld->position-first.localWorld->position,translation);
    require(first.localWorld->entityId==moved.localWorld->entityId,"Translation changed the local source identity.");
    require(first.characterLights==moved.characterLights,"Moving the player changed character light directions.");
    near(first.characterLights[0].direction,math::normalized(settings.key.direction));
    near(first.characterShadow.position,presented);near(moved.characterShadow.position,presented+translation);
    near(first.characterShadow.direction,math::normalized(settings.key.direction));
    require(first.localWorld->roles.receiverMask==lighting_scope::World&&
        first.localWorld->roles.shadowCasterMask==lighting_scope::World&&!first.localWorld->roles.ambientReveal,
        "Local world light still affects or casts Character, or secretly reveals darkness.");
    require(first.localWorld->radius==8&&first.localWorld->nearFalloffRadius==1.5f&&
        first.localWorld->specularStrength==0&&first.localWorld->rangeFalloffExponent==1&&first.localWorld->edgeSoftness==1,
        "Local World Light lost its independent finite range or attenuation settings.");
    settings.localWorld.radius=13;settings.localWorld.nearFalloffRadius=2.25f;
    settings.localWorld.rangeFalloffExponent=.7f;settings.localWorld.edgeSoftness=.4f;
    settings.localWorld.specularStrength=.3f;settings.localWorld.sourceRadius=.6f;settings.localWorld.attenuation=1.25f;
    const auto tuned=evaluateRig(settings,presented,{},{});
    require(tuned.localWorld&&tuned.localWorld->radius==13&&tuned.localWorld->nearFalloffRadius==2.25f&&
        tuned.localWorld->rangeFalloffExponent==.7f&&tuned.localWorld->edgeSoftness==.4f&&
        tuned.localWorld->specularStrength==.3f&&tuned.localWorld->sourceRadius==.6f&&tuned.localWorld->attenuation==1.25f&&
        tuned.localWorld->position==first.localWorld->position&&tuned.localWorld->roles==first.localWorld->roles&&
        tuned.characterLights==first.characterLights&&tuned.characterShadow==first.characterShadow,
        "Editing the world point's response changed independent character lighting or source placement.");
    // The evaluator only receives presented translation. Yaw/parent scale cannot
    // turn character illumination, move the source sideways or rotate its shadow.
    require(first.characterShadow.direction==moved.characterShadow.direction,"Character shadow turned with translation.");
    settings.localWorld.enabled=false;
    const auto characterOnly=evaluateRig(settings,presented,{},{});
    require(characterOnly.valid()&&!characterOnly.localWorld&&characterOnly.characterLights[0].enabled&&
        characterOnly.characterShadow.enabled,"Disabling world illumination disabled the other lighting jobs.");
    settings.key.direction={.1f,-1,.5f};
    near(evaluateRig(settings,presented,{},{}).characterShadow.direction,math::normalized(settings.key.direction));
    settings.key.enabled=false;
    require(!evaluateRig(settings,presented,{},{}).characterShadow.enabled,
        "A disabled Key retained its character silhouette.");
    settings.key.enabled=true;settings.key.intensity=0;
    require(!evaluateRig(settings,presented,{},{}).characterShadow.enabled,
        "A zero-energy Key retained its character silhouette.");
    settings=fullRig();settings.characterShadow.enabled=false;
    require(evaluateRig(settings,presented,{},{}).characterLights[0].enabled&&
        !evaluateRig(settings,presented,{},{}).characterShadow.enabled,
        "Disabling Key shadow disabled Key illumination.");
    settings.key.enabled=false;settings.fill.enabled=false;settings.localWorld.enabled=true;
    const auto worldOnly=evaluateRig(settings,presented,{},{});
    require(worldOnly.localWorld&&!worldOnly.characterLights[0].enabled&&!worldOnly.characterLights[1].enabled&&
        !worldOnly.characterShadow.enabled,"Independent lighting switches were conflated.");
}
void collisionSafeIdentity() {
    constexpr auto high=std::numeric_limits<std::uint64_t>::max();
    std::array<PointLight,3> points;points[0].entityId=high;points[1].entityId=high-2;points[2].entityId=high-7;
    points[0].enabled=false;
    std::array<DirectionalLight,2> suns;suns[0].entityId=high-1;suns[1].entityId=high-4;
    const auto first=evaluateRig(fullRig(),{},points,suns);
    require(first.valid()&&first.localWorld&&first.localWorld->entityId==high-3,"Local source collided with authored disabled or directional IDs.");
    std::reverse(points.begin(),points.end());std::reverse(suns.begin(),suns.end());
    require(evaluateRig(fullRig(),{},points,suns).localWorld->entityId==first.localWorld->entityId,
        "Local source identity depends on extraction order.");
}
void validation() {
    constexpr auto nan=std::numeric_limits<float>::quiet_NaN(),infinity=std::numeric_limits<float>::infinity();
    const auto invalid=[](const PlayerLightRigSettings& value) {
        require(!validPlayerLightRigSettings(value)&&!validatePlayerLightRigSettings(value).empty(),"Invalid rig setting accepted.");
        const auto result=evaluateRig(value,{}, {},{});
        require(!result.valid()&&!result.localWorld&&!result.characterLights[0].enabled&&!result.characterShadow.enabled,
            "Invalid rig published partial lighting.");
    };
    PlayerLightRigSettings value;
    for(const auto bad:{nan,infinity,-.01f,10001.f}){value={};value.key.intensity=bad;invalid(value);}
    for(const auto bad:{nan,infinity,.009f,100001.f}){value={};value.localWorld.height=bad;invalid(value);}
    for(const auto bad:{nan,infinity,-.001f,1000001.f}){value={};value.localWorld.intensity=bad;invalid(value);}
    for(const auto intensity:{0.f,1000000.f}) {
        value={};value.localWorld.intensity=intensity;
        require(validPlayerLightRigSettings(value),"Valid migrated Local World Light intensity boundary was rejected.");
    }
    for(const auto bad:{nan,infinity,.009f,100001.f}){value={};value.localWorld.radius=bad;invalid(value);}
    for(const auto bad:{nan,infinity,-.001f,100001.f}) {
        value={};value.localWorld.nearFalloffRadius=bad;invalid(value);
        value={};value.localWorld.sourceRadius=bad;invalid(value);
    }
    for(const auto bad:{nan,infinity,-.001f,1.001f}){value={};value.localWorld.specularStrength=bad;invalid(value);}
    for(const auto bad:{nan,infinity,.249f,8.001f}){value={};value.localWorld.rangeFalloffExponent=bad;invalid(value);}
    for(const auto bad:{nan,infinity,.049f,1.001f}){value={};value.localWorld.edgeSoftness=bad;invalid(value);}
    for(const auto bad:{nan,infinity,-10.001f,10.001f}){value={};value.localWorld.attenuation=bad;invalid(value);}
    for(const auto valid:{-10.f,0.f,1.f,2.f,10.f}) {
        value=fullRig();value.localWorld.attenuation=valid;
        require(validPlayerLightRigSettings(value)&&evaluateRig(value,{}, {},{}).localWorld->attenuation==valid,
            "A supported distance attenuation exponent was rejected or changed by rig evaluation.");
    }
    for(const auto bad:{nan,infinity,-.001f,1.001f}){value={};value.key.color[1]=bad;invalid(value);}
    for(const auto bad:{nan,infinity,-.001f,1.001f}){value={};value.fill.diffuse=bad;invalid(value);value={};value.fill.specular=bad;invalid(value);}
    value={};value.fill.enabled=false;value.fill.direction={};invalid(value);
    value={};value.key.direction={nan,0,0};invalid(value);
    for(const auto bad:{nan,infinity,-.001f}) {
        value={};value.localWorld.shadowDepthBias=bad;invalid(value);
        value={};value.characterShadow.shadowNormalBias=bad;invalid(value);
    }
    for(const auto bad:{nan,infinity,-.001f,1.001f}){value={};value.characterShadow.strength=bad;invalid(value);}
    for(const auto bad:{nan,infinity,.009f,100001.f}){value={};value.characterShadow.distance=bad;invalid(value);}
    value=fullRig();const auto badPosition=evaluateRig(value,{nan,0,0},{},{});
    require(!badPosition.valid()&&!badPosition.localWorld,"Nonfinite player position published lighting.");
    CharacterRevealSettings reveal;
    require(reveal.enabled&&reveal.spread==8&&validCharacterRevealSettings(reveal),"Default character lighting spread invalid.");
    require(characterLightingSupport(.01f)==.08f&&characterLightingSupport(100000)==800000,
        "Character lighting support lost its complete eight-spread Gaussian tail.");
    for(const auto bad:{nan,infinity,.009f,100001.f}) {
        reveal.enabled=false;reveal.spread=bad;
        require(!validCharacterRevealSettings(reveal),"Invalid stored lighting spread accepted.");
    }
    auto far=fullRig();far.localWorld.height=100000;
    const auto farPoint=evaluateRig(far,{},{},{});
    require(farPoint.valid()&&farPoint.localWorld&&farPoint.localWorld->position[1]==100000&&farPoint.localWorld->radius==8,
        "Point height changed its independent finite lighting/shadow range.");
    require(characterLightingCenterHeight(1000000)==100000,"Extreme Character height lost its bounded reveal center.");
    require(!validLightingMask(4)&&!validLightRoles({0,0,4}),"Unknown lighting mask bits accepted.");
    AmbientVisibilitySettings ambient;
    require(validAmbientVisibilitySettings(ambient)&&!ambient.attenuateEmission,"Default ambient settings invalid.");
    for(const auto bad:{nan,infinity,-.001f,10001.f}){ambient={};ambient.threshold=bad;require(!validAmbientVisibilitySettings(ambient),"Invalid ambient threshold accepted.");}
}
void sourceCapacity() {
    auto settings=fullRig();std::vector<PointLight> points(MaximumClusteredPointLights-1);
    const auto exact=evaluateRig(settings,{},points,{});
    require(exact.valid()&&exact.localWorld,"One local world source rejected exact capacity.");
    points.emplace_back();const auto overflow=evaluateRig(settings,{},points,{});
    require(!overflow.valid()&&!overflow.localWorld&&!overflow.characterLights[0].enabled,
        "Capacity overflow published a partial rig.");
    settings.localWorld.enabled=false;const auto characterOnly=evaluateRig(settings,{},points,{});
    require(characterOnly.valid()&&characterOnly.characterLights[0].enabled&&characterOnly.characterShadow.enabled,
        "Character-only illumination or shadow consumed an ordinary point slot.");
    settings.enabled=false;require(evaluateRig(settings,{},points,{}).valid(),"Disabled rig changed authored point capacity.");
}
}
int main(){try {
    separatedDefaultsAndPresentedFollow();collisionSafeIdentity();validation();sourceCapacity();
    std::cout<<"Separated character/world lighting, stable shadow direction, independent toggles and source capacity passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
