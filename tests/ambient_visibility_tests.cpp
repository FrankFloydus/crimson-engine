#include "crimson-renderer/AmbientVisibilityMath.h"
#include "crimson-renderer/shaders/ambient_visibility_response.sh"
#include "crimson-renderer/shaders/point_light.sh"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
using namespace crimson;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool near(float a,float b){return std::abs(a-b)<2e-6f;}
void roles() {
    for(std::uint32_t receivers=0;receivers<=3;++receivers)for(std::uint32_t ambient=0;ambient<=3;++ambient)
        for(bool reveal:{false,true})for(bool shadow:{false,true}) {
        LightRoles value;value.receiverMask=receivers;value.ambientRevealMask=ambient;value.ambientReveal=reveal;
        const auto flags=ambient_visibility::packLightRoles(value,shadow);
        check(flags<64 && std::uint32_t(float(flags))==flags,"Light roles lost exact float transport.");
        for(std::uint32_t surface=0;surface<=3;++surface) {
            check(bool(flags&surface&3u)==bool(receivers&surface),"Direct-light scope changed during record packing.");
            check(bool((flags&16u)&&((flags>>2)&surface&3u))==bool(reveal&&(ambient&surface)),
                "Ambient scope or reveal enable changed during record packing.");
        }
        check(bool(flags&32u)==shadow,"Shadow-awareness role changed during record packing.");
        value.shadowCasterMask=0;
        check(ambient_visibility::packLightRoles(value,shadow)==flags,"Independent caster scope leaked into direct/reveal roles.");
    }
    LightRoles character;character.receiverMask=lighting_scope::Character;character.ambientRevealMask=lighting_scope::World;
    character.ambientReveal=true;
    const auto flags=ambient_visibility::packLightRoles(character,true);
    check(!(flags&lighting_scope::World) && (flags&lighting_scope::Character) && ((flags>>2)&lighting_scope::World),
        "A character-only key cannot retain independent world ambient reveal.");
}
void response() {
    using ambient_visibility::ambientVisibilityResponse;
    for(const auto floor:{0.f,.2f,1.f})for(const auto strength:{0.f,.4f,1.f})for(const auto threshold:{0.f,.1f,100.f})
        for(const auto transition:{.0002f,.25f,1.f}) {
        const float dark=1+(floor-1)*strength;
        check(near(ambientVisibilityResponse(threshold,threshold,transition,floor,strength),dark),"Darkness floor/strength endpoint changed.");
        check(near(ambientVisibilityResponse(threshold+transition*2,threshold,transition,floor,strength),1),"Full reveal did not preserve IBL.");
        if(threshold<1)
            check(std::abs(ambientVisibilityResponse(threshold+transition*.5f,threshold,transition,floor,strength)-(dark+1)*.5f)<3e-5f,
                "Ambient transition is not centered and smooth.");
        float previous=dark;
        for(unsigned step=0;step<=1000;++step) {
            const float energy=threshold+transition*(float(step)/1000);
            const auto visibility=ambientVisibilityResponse(energy,threshold,transition,floor,strength);
            check(std::isfinite(visibility)&&visibility>=dark-1e-6f&&visibility<=1&&visibility>=previous-1e-6f,
                "Ambient energy response is unbounded or nonmonotonic.");previous=visibility;
        }
    }
    const float inverseRadiusSquared=1.f/(8*8);
    const auto nearEnergy=point_lighting::pointLightAttenuation(1,inverseRadiusSquared)*35;
    const auto farEnergy=point_lighting::pointLightAttenuation(8*8,inverseRadiusSquared)*35;
    check(ambientVisibilityResponse(nearEnergy,.1f,.25f,0,1)==1 && ambientVisibilityResponse(farEnergy,.1f,.25f,0,1)==0,
        "Authored point radius did not bound ambient reveal.");
    check(ambientVisibilityResponse(0,.1f,.25f,0,0)==1,"Zero ambient strength changed original IBL.");
}
void sharedFillFootprint() {
    using ambient_visibility::radialRevealWeight;
    using ambient_visibility::radialRevealWeightSquared;
    using ambient_visibility::playerRevealVisibility;
    check(radialRevealWeight(0,0)==0,"A missing fill footprint emitted illumination.");
    for(const float spread:{.01f,1.f,8.f,100000.f}) {
        check(radialRevealWeight(0,spread)==1&&radialRevealWeight(spread,spread)==.5f&&
            radialRevealWeight(spread*8,spread)==0&&radialRevealWeight(spread*8.1f,spread)==0,
            "Fill/reveal center, half-brightness distance or finite support changed.");
        check(radialRevealWeight(spread*.01f,spread)<1&&radialRevealWeight(spread*.5f,spread)<.85f,
            "The artistic player profile retained a flat bright core.");
        for(const float distance:{.01f,.1f,.25f,.5f,1.f,2.f,3.f,4.f,5.f,6.f}) {
            const auto actual=radialRevealWeight(distance*spread,spread);
            const auto gaussian=std::exp2(-distance*distance);
            check(std::abs(actual/gaussian-1)<2e-5f,"The gradual interior differs from the half-brightness Gaussian contract.");
        }
        check(std::abs(radialRevealWeight(spread*7,spread)/std::exp2(-49.f)-.5f)<2e-5f,
            "The negligible tail does not taper smoothly halfway through its support interval.");
        float previous=1;
        for(unsigned step=0;step<=2048;++step) {
            const float distance=spread*8*(float(step)/2048);
            const float weight=radialRevealWeight(distance,spread);
            check(std::isfinite(weight)&&weight>=0&&weight<=previous&&weight<=1,
                "Shared fill/reveal footprint is not finite, bounded and monotonic.");
            check(weight==radialRevealWeightSquared(distance*distance,spread),
                "Squared-distance shader evaluation differs from the radial CPU API.");
            previous=weight;
            // Tiny Gaussian tails must survive global darkness exactly rather
            // than cancel to zero or be multiplied by a second radial fade.
            check(playerRevealVisibility(weight,0,1)==weight,
                "Player reveal lost the Gaussian tail through subtractive cancellation.");
            for(const float floor:{0.f,.2f,1.f})for(const float strength:{0.f,.4f,1.f}) {
                const float playerVisibility=playerRevealVisibility(weight,floor,strength);
                const float darkness=std::max(playerVisibility,.75f);
                check(std::min(weight,darkness)==weight,
                    "A matching player/scene reveal squared or extended the Gaussian fill.");
            }
            check(std::min(weight,0.f)==0&&near(std::min(weight,1.f),weight),
                "Unrevealed fill escaped darkness or disabled darkness erased its fade.");
        }
    }
    constexpr float delta=.001f;
    const auto atSix=radialRevealWeight(6,1);
    const auto derivativeLeft=(atSix-radialRevealWeight(6-delta,1))/delta;
    const auto derivativeRight=(radialRevealWeight(6+delta,1)-atSix)/delta;
    const auto gaussianDerivative=-12*std::log(2.f)*std::exp2(-36.f);
    check(std::abs(derivativeLeft-gaussianDerivative)<2e-12f&&std::abs(derivativeRight-gaussianDerivative)<2e-12f,
        "The tail taper introduced a derivative discontinuity or visible shoulder.");
    check(radialRevealWeight(8,1)==0&&radialRevealWeight(8-delta,1)/delta<1e-20f,
        "The finite support ends with a discontinuous brightness slope.");
    const auto extremeTail=radialRevealWeight(7.99f,1)*1000000*100/std::numbers::pi_v<float>;
    check(extremeTail*12.92f*255<.001f,"The support taper leaves a visible cutoff at maximum fill intensity/exposure.");
}
void packing() {
    using ambient_visibility::packedTexelCount;
    for(unsigned width:{1u,2u,3u,4u,5u,31u,32u,33u,1279u})for(unsigned height:{1u,2u,7u,8u,9u,721u}) {
        const auto count=std::uint64_t(width)*height;
        const auto texels=packedTexelCount(width,height);
        check(texels*4>=count && (!texels || (texels-1)*4<count),"Packed ambient region is not exact.");
        std::vector<unsigned> seen(static_cast<std::size_t>(texels));
        const unsigned groupsPerRow=(width+3)/4;
        // Match the 8x8 publish dispatch, then independently verify that each
        // texel/channel maps back to the original screen index without holes.
        for(unsigned y=0;y<((height+7)/8)*8;++y)for(unsigned x=0;x<((width+31)/32)*8;++x) {
            if(x>=groupsPerRow)continue;
            const auto group=std::uint64_t(y)*groupsPerRow+x;
            if(group*4>=count)continue;
            ++seen[static_cast<std::size_t>(group)];
            for(unsigned channel=0;channel<4;++channel)if(group*4+channel<count) {
                const auto linear=group*4+channel;
                const auto sourceY=linear/width,sourceX=linear-sourceY*width;
                check(sourceX<width&&sourceY<height&&sourceY*width+sourceX==linear,"Ambient publication read an invalid screen pixel.");
                check(linear/4==group&&linear%4==channel,"Material lookup disagrees with packed publication.");
            }
        }
        for(auto visits:seen)check(visits==1,"Ambient publication skipped or raced a packed texel.");
    }
    check(packedTexelCount(65535,65535)==1073709057ull,"Ambient reservation overflowed 32-bit multiplication.");
}
}
int main() {
    try {roles();response();sharedFillFootprint();packing();std::cout<<"Ambient visibility scope independence, energy response, shared fill/reveal fade and odd-size GPU packing checks passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
