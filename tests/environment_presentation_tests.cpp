#include "crimson-renderer/EnvironmentPresentation.h"
#include "crimson-renderer/Fog.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <source_location>
#include <stdexcept>
#include <string>

namespace {
using namespace crimson;
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
void near(float actual,float expected,const std::source_location location=std::source_location::current()) {
    if(!std::isfinite(actual) || std::abs(actual-expected)>=2e-6f)
        throw std::runtime_error(std::string("Fog equation mismatch in ")+location.function_name()+":"+
            std::to_string(location.line())+", actual="+std::to_string(actual)+", expected="+std::to_string(expected));
}
void relativeNear(float actual,double expected,const std::source_location location=std::source_location::current()) {
    if(!std::isfinite(actual) || std::abs(double(actual)-expected)>std::max(std::abs(expected)*2e-6,2e-6))
        throw std::runtime_error(std::string("FP32 fog radiance mismatch in ")+location.function_name()+":"+
            std::to_string(location.line())+", actual="+std::to_string(actual)+", expected="+std::to_string(expected));
}
void reflectionThumbnailIdentity() {
    EnvironmentSettings environment;const ProceduralSun sun;
    const auto inactive=thumbnailEnvironmentKey(environment,sun);
    environment.ssr.quality=SsrQuality::High;environment.ssr.maxDistance=75;environment.ssr.depthTolerance=.1f;
    environment.ssr.maxRoughness=.8f;environment.ssr.bias=.01f;environment.ssr.strength=.5f;
    check(thumbnailEnvironmentKey(environment,sun)==inactive,"Inactive SSR tuning invalidated a thumbnail.");
    environment.ssr.enabled=true;const auto active=thumbnailEnvironmentKey(environment,sun);
    check(active!=inactive,"Enabling SSR did not invalidate a thumbnail.");
    for(unsigned field=0;field<6;++field){auto changed=environment;
        if(field==0)changed.ssr.strength=.75f;if(field==1)changed.ssr.quality=SsrQuality::Low;
        if(field==2)changed.ssr.maxDistance=25;if(field==3)changed.ssr.depthTolerance=.05f;
        if(field==4)changed.ssr.maxRoughness=.6f;if(field==5)changed.ssr.bias=.002f;
        check(thumbnailEnvironmentKey(changed,sun)!=active,"An active SSR control did not invalidate a thumbnail.");}
    environment.ssr.strength=0;check(thumbnailEnvironmentKey(environment,sun)==inactive,"Zero-strength SSR altered the inactive thumbnail identity.");
}
void fogComposition() {
    FogSettings settings;settings.enabled=true;settings.color={.5f,.5f,.5f};settings.density=0;
    auto params=fog::parameters(settings,1000);const Vec3 source{4,2,1};
    const auto identity=fog::premultiplied(source,.4f,params,100,0,false);
    near(identity[0],1.6f);near(identity[1],.8f);near(identity[2],.4f);near(identity[3],.4f);
    near(fog::equations::fogTransmittance(0,std::numeric_limits<float>::infinity()),1);
    settings.density=.01f;params=fog::parameters(settings,1000);
    near(params.colorDensity[0],.21404114f);near(fog::transmittance(params,100,0),std::exp(-1.f));
    near(fog::transmittance(params,0,0),1);
    const auto opaque=fog::premultiplied(source,1,params,100,0,false);
    near(opaque[0],4*std::exp(-1.f)+params.colorDensity[0]*(1-std::exp(-1.f)));
    const auto far=fog::premultiplied(source,1,params,10000,0,false);
    near(far[0],params.colorDensity[0]);near(far[1],params.colorDensity[1]);near(far[3],1);
    const auto alpha=fog::premultiplied(source,.25f,params,100,0,false);
    for(std::size_t c=0;c<3;++c)near(alpha[c],opaque[c]*.25f);
    near(alpha[3],.25f);
    for(bool additive:{false,true}) {
        const auto invisible=fog::premultiplied(source,0,params,100,0,additive);
        for(float value:invisible)near(value,0);
    }
    const auto additive=fog::premultiplied(source,.25f,params,100,0,true);
    for(std::size_t c=0;c<3;++c)near(additive[c],source[c]*.25f*std::exp(-1.f));
    near(additive[3],.25f);
    const auto blackAdditive=fog::premultiplied({0,0,0},1,params,1000,0,true);
    for(std::size_t c=0;c<3;++c)near(blackAdditive[c],0);
    // Off-axis billboard fragments have a longer optical path than their center.
    check(fog::transmittance(params,std::sqrt(125.f),0)<
        fog::transmittance(params,10,0),"Fog must use each fragment's distance, not center depth.");
    settings.enabled=false;near(fog::parameters(settings,1000).colorDensity[3],0);
}
void depthAndHeightFog() {
    FogSettings settings;settings.enabled=true;settings.mode=FogMode::Depth;settings.depthOpacity=.75f;
    auto params=fog::parameters(settings,1000);
    near(fog::transmittance(params,0,0),1);
    near(fog::transmittance(params,10,0),1);
    near(fog::transmittance(params,55,0),.625f);
    near(fog::transmittance(params,100,0),.25f);
    near(fog::transmittance(params,1000,0),.25f);
    settings.depthCurve=2;params=fog::parameters(settings,1000);
    near(fog::transmittance(params,55,0),.8125f);
    // Exponential density and Depth opacity are separate, persistent controls.
    settings.density=100;near(fog::transmittance(fog::parameters(settings,1000),55,0),.8125f);
    settings.depthOpacity=0;params=fog::parameters(settings,1000);
    near(fog::transmittance(params,1000,0),1);
    settings.mode=FogMode::Exponential;settings.density=.02f;
    near(fog::transmittance(fog::parameters(settings,1000),50,0),std::exp(-1.f));

    settings.mode=FogMode::Depth;settings.depthCurve=1;settings.depthOpacity=1;settings.depthEnd=0;
    params=fog::parameters(settings,1000);
    near(params.depth[0],10);near(params.depth[1],1000);near(fog::transmittance(params,1000,0),0);
    settings.depthBegin=100;
    params=fog::parameters(settings,5);
    near(params.depth[0],4.999f);near(params.depth[1],5);
    near(fog::transmittance(params,4.999f,0),1);near(fog::transmittance(params,5,0),0);
    near(settings.depthBegin,100);near(settings.depthEnd,0);
    near(fog::equations::fogDepthTransmittance(10,10,1,.7f,9),1);
    near(fog::equations::fogDepthTransmittance(10,10,1,.7f,10),.3f);

    settings.depthBegin=10;settings.depthEnd=100;settings.depthOpacity=0;
    settings.height=10;settings.heightDensity=.1f;
    params=fog::parameters(settings,1000);
    near(fog::transmittance(params,0,20),1);near(fog::transmittance(params,0,10),1);
    near(fog::transmittance(params,0,0),std::exp(-1.f));
    settings.heightDensity=-.1f;params=fog::parameters(settings,1000);
    near(fog::transmittance(params,0,0),1);near(fog::transmittance(params,0,20),std::exp(-1.f));
    settings.heightDensity=.1f;settings.depthOpacity=.8f;params=fog::parameters(settings,1000);
    near(fog::transmittance(params,55,10),.6f);
    near(fog::transmittance(params,55,8),.6f);
    near(fog::transmittance(params,55,0),std::exp(-1.f));
    settings.mode=FogMode::Exponential;settings.density=.01f;params=fog::parameters(settings,1000);
    near(fog::transmittance(params,100,10),std::exp(-1.f));
    near(fog::transmittance(params,100,-10),std::exp(-2.f));
    settings.density=0;near(fog::transmittance(fog::parameters(settings,1000),100,-10),std::exp(-2.f));

    for(auto mode:{FogMode::Exponential,FogMode::Depth}) {
        settings.mode=mode;settings.enabled=false;
        near(fog::transmittance(fog::parameters(settings,1000),1000,-1000),1);
        settings.enabled=true;params=fog::parameters(settings,1000);
        const Vec3 source{4,2,1};const float transmission=fog::transmittance(params,55,0);
        const auto opaque=fog::premultiplied(source,1,params,55,0,false);
        const auto alpha=fog::premultiplied(source,.25f,params,55,0,false);
        const auto additive=fog::premultiplied(source,.25f,params,55,0,true);
        for(std::size_t c=0;c<3;++c) {near(alpha[c],opaque[c]*.25f);near(additive[c],source[c]*transmission*.25f);}
        near(alpha[3],.25f);near(additive[3],.25f);
        for(bool add:{false,true})for(float channel:fog::premultiplied(source,0,params,55,0,add))near(channel,0);
    }
}
void fogLightEnergy() {
    FogSettings settings;settings.enabled=true;settings.color={.5f,.5f,.5f};settings.lightEnergy=4;
    const auto params=fog::parameters(settings,1000);
    near(params.colorDensity[0],.21404114f*4);
    near(fog::transmittance(params,100,0),std::exp(-1.f));
    const auto lit=fog::premultiplied({0,0,0},.3f,params,100,0,false);
    near(lit[0],.21404114f*4*(1-std::exp(-1.f))*.3f);near(lit[3],.3f);
    settings.lightEnergy=0;
    const auto dark=fog::premultiplied({4,2,1},.3f,fog::parameters(settings,1000),100,0,false);
    near(dark[0],4*std::exp(-1.f)*.3f);near(dark[3],.3f);
    settings.color={1,1,1};settings.lightEnergy=std::numeric_limits<float>::max();
    const auto extremeParams=fog::parameters(settings,1000);
    for(float opacity:{0.f,.25f,1.f}) {
        const auto extreme=fog::premultiplied({0,0,0},opacity,extremeParams,1000,0,false);
        // The shared fog light color is bounded before applying the finite
        // optical path; maximum Energy cannot bypass its residual transmission.
        for(std::size_t c=0;c<3;++c)relativeNear(extreme[c],1e30f*(1-std::exp(-10.f))*opacity);
        near(extreme[3],opacity);
    }
}
void fullPrecisionFogRadiance() {
    // Surface, background, and refraction sources use FP32 scene HDR. Values
    // above the GI donors' separate RGBA16F ceiling must survive until exposure.
    const std::array<Vec3,3> sources{{{129536,129024,122368},{1e10f,1e9f,1e8f},{1e30f,1e29f,1e28f}}};
    FogSettings settings;
    auto params=fog::parameters(settings,1000);
    for(const auto& source:sources) {
        for(const float opacity:{0.f,.25f,1.f})for(const bool additive:{false,true}) {
            const auto result=fog::premultiplied(source,opacity,params,32,0,additive);
            for(std::size_t channel=0;channel<3;++channel)relativeNear(result[channel],double(source[channel])*opacity);
            near(result[3],opacity);
        }
        const auto background=fog::background(params,source,true,32);
        for(std::size_t channel=0;channel<3;++channel)relativeNear(background[channel],source[channel]);
        near(background[3],1);
    }
    settings.enabled=true;settings.density=std::log(2.f)/32;
    settings.color={1,1,1};settings.lightEnergy=1e10f;
    params=fog::parameters(settings,1000);
    for(const auto& source:sources) {
        const auto result=fog::premultiplied(source,.25f,params,32,0,false);
        const auto background=fog::background(params,source,true,32);
        for(std::size_t channel=0;channel<3;++channel) {
            const double expected=(double(source[channel])*.5+1e10*.5);
            relativeNear(result[channel],expected*.25);
            relativeNear(background[channel],expected);
        }
    }
    // Aerial Perspective and Sun Scatter also mix FP32 sky/light radiance.
    params.sky[1]=1;
    for(const auto& source:sources) {
        const auto color=fog::lightColor(params,source,{0,0,-1});
        for(std::size_t channel=0;channel<3;++channel)relativeNear(color[channel],source[channel]);
    }
    params.sky[1]=0;params.sky[2]=1;params.colorDensity={0,0,0,0};
    params.sunDirections[0]={0,0,-1,1};params.sunColors[0]={129536,1e10f,1e29f,0};
    const auto sunlight=fog::lightColor(params,{}, {0,0,-1});
    for(std::size_t channel=0;channel<3;++channel)
        relativeNear(sunlight[channel],double(params.sunColors[0][channel])*std::numbers::pi_v<float>);

    for(const float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity()}) {
        near(fog::equations::fogFiniteRadiance(invalid),0);
        for(const float opacity:{0.f,.25f,1.f})near(fog::equations::fogPremultipliedChannel(invalid,opacity),0);
    }
    near(fog::equations::fogPremultipliedChannel(std::numeric_limits<float>::max(),0),0);
    relativeNear(fog::equations::fogPremultipliedChannel(std::numeric_limits<float>::max(),.25f),double(1e30f)*.25);
    relativeNear(fog::equations::fogFiniteRadiance(std::numeric_limits<float>::max()),1e30f);
    near(fog::equations::fogPremultipliedChannel(-1e30f,1),0);
}
void materialFogReception() {
    for(auto mode:{FogMode::Exponential,FogMode::Depth})for(float heightDensity:{0.f,.1f,-.1f}) {
        FogSettings settings;settings.enabled=true;settings.mode=mode;settings.color={0,0,1};
        settings.density=.02f;settings.depthBegin=10;settings.depthEnd=100;settings.depthOpacity=.75f;
        settings.height=heightDensity<0?-10.f:10.f;settings.heightDensity=heightDensity;
        const auto view=fog::parameters(settings,1000);
        const auto excluded=fog::forMaterial(view,false);
        near(fog::transmittance(excluded,100,0),1);
        check(view.height[3]==1 && excluded.colorDensity==view.colorDensity && excluded.depth==view.depth,
            "Per-material fog exclusion mutated the view or discarded authored fog parameters.");
        for(bool additive:{false,true})for(float opacity:{0.f,.25f,1.f}) {
            const auto black=fog::premultiplied({},opacity,excluded,100,0,additive);
            const Vec3 source{4,2,1};const auto unchanged=fog::premultiplied(source,opacity,excluded,100,0,additive);
            for(std::size_t channel=0;channel<3;++channel){near(black[channel],0);near(unchanged[channel],source[channel]*opacity);}
            near(black[3],opacity);near(unchanged[3],opacity);
        }
        const auto receiving=fog::forMaterial(view,true);
        near(fog::transmittance(receiving,100,0),fog::transmittance(view,100,0));
        check(fog::premultiplied({},1,receiving,100,0,false)[2]>.5f,
            "Excluding one material suppressed fog for a following receiving material.");
        settings.enabled=false;
        near(fog::transmittance(fog::forMaterial(fog::parameters(settings,1000),true),100,-1000),1);
    }
}
void backgroundFog() {
    FogSettings settings;settings.color={.5f,.25f,1};settings.lightEnergy=2;
    const Vec3 sky{4,2,.25f};
    const auto disabled=fog::parameters(settings,2048);
    check(fog::background(disabled,sky,true,32)==Vec4{4,2,.25f,1},
        "Disabled fog changed the visible sky's radiance or coverage.");
    check(fog::background(disabled,sky,false,32)==Vec4{},
        "Hidden background wrote radiance or coverage without fog.");

    settings.enabled=true;settings.density=0;settings.heightDensity=0;
    auto params=fog::parameters(settings,2048);
    check(fog::background(params,sky,true,32)==fog::background(disabled,sky,true,32) &&
        fog::background(params,sky,false,32)==fog::background(disabled,sky,false,32),
        "Zero Density with height fog disabled failed to restore the unfogged background.");
    settings.density=std::log(2.f)/32;params=fog::parameters(settings,2048);
    const auto partial=fog::background(params,sky,false,32);
    near(partial[0],.21404114f);near(partial[1],.05087609f);near(partial[2],1);near(partial[3],.5f);
    const auto visible=fog::background(params,sky,true,32);
    for(unsigned channel=0;channel<3;++channel)near(visible[channel],sky[channel]*.5f+partial[channel]);
    near(visible[3],1);
    near(fog::background(params,sky,false,64)[3],.75f);
    // Values remain premultiplied linear HDR for the ordinary exposure/tone
    // path; hidden matte coverage must not multiply fog's RGB a second time.
    const Vec3 fogRadiance{partial[0],partial[1],partial[2]};
    near(toneMap(fogRadiance*.5f,ToneMapping::Reinhard)[2],1.f/3.f);
    for(const auto mode:{ToneMapping::Linear,ToneMapping::Reinhard,ToneMapping::ACES,ToneMapping::AgX})
        for(const float channel:toneMap(fogRadiance*0.f,mode))near(channel,0);
    settings.skyAffect=.5f;params=fog::parameters(settings,2048);
    const auto halfSky=fog::background(params,sky,false,32);
    for(unsigned channel=0;channel<3;++channel)near(halfSky[channel],partial[channel]*.5f);
    near(halfSky[3],.25f);
    settings.skyAffect=0;params=fog::parameters(settings,2048);
    check(fog::background(params,sky,true,32)==Vec4{4,2,.25f,1} && fog::background(params,sky,false,32)==Vec4{},
        "Sky Affect zero failed to exclude background fog.");
    settings.skyAffect=1;settings.mode=FogMode::Depth;settings.depthBegin=0;settings.depthEnd=64;
    settings.depthOpacity=.8f;params=fog::parameters(settings,2048);
    near(fog::background(params,sky,false,32)[3],.4f);
    settings.depthOpacity=0;params=fog::parameters(settings,2048);
    check(fog::background(params,sky,false,32)==Vec4{},"Depth opacity zero left background fog.");
    settings.mode=FogMode::Exponential;settings.density=0;settings.height=1;settings.heightDensity=.1f;
    params=fog::parameters(settings,2048);
    near(fog::background(params,sky,false,32,-9)[3],1-std::exp(-1.f));
    near(fog::background(params,sky,false,32,11)[3],0);
    settings.heightDensity=0;settings.density=std::log(2.f)/32;
    settings.aerialPerspective=1;params=fog::parameters(settings,2048);
    const auto skyHaze=fog::background(params,sky,false,32);
    for(unsigned channel=0;channel<3;++channel)near(skyHaze[channel],sky[channel]*.5f);
    near(skyHaze[3],.5f);
    check(fog::background(params,sky,true,32)==Vec4{4,2,.25f,1},
        "Full Aerial Perspective tinted a constant visible sky instead of using its radiance.");
    settings.aerialPerspective=.25f;params=fog::parameters(settings,2048);
    const auto mixedHaze=fog::background(params,sky,false,32);
    for(unsigned channel=0;channel<3;++channel)near(mixedHaze[channel],partial[channel]*.75f+sky[channel]*.125f);
    settings.aerialPerspective=0;settings.sunScatter=1;params=fog::parameters(settings,2048);
    params.sunDirections[0]={0,0,-1,1};params.sunColors[0]={.1f,.2f,.3f,0};
    const auto towardSun=fog::background(params,sky,false,32,0,{0,0,-1});
    for(unsigned channel=0;channel<3;++channel)
        near(towardSun[channel],partial[channel]+params.sunColors[0][channel]*std::numbers::pi_v<float>*.5f);
    check(fog::background(params,sky,false,32,0,{0,0,1})==partial,
        "Sun Scatter added radiance when viewing away from the sun.");
    params.sunDirections[0][3]=0;
    check(fog::background(params,sky,false,32)==partial,"A disabled directional light still contributed Sun Scatter.");
    params.sunDirections[0][3]=1;params.sky[2]=0;
    check(fog::background(params,sky,false,32)==partial,"Sun Scatter zero still contributed directional radiance.");
    settings.sunScatter=0;
    settings.lightEnergy=0;
    const auto black=fog::background(fog::parameters(settings,2048),sky,false,32);
    for(unsigned channel=0;channel<3;++channel)near(black[channel],0);
    near(black[3],.5f);
    settings.color={1,1,1};settings.lightEnergy=std::numeric_limits<float>::max();
    settings.density=2;
    const auto bounded=fog::background(fog::parameters(settings,2048),sky,false,32);
    for(unsigned channel=0;channel<3;++channel)relativeNear(bounded[channel],1e30f);
    near(bounded[3],1);
}
void independentPresentation() {
    EnvironmentSettings environment;ViewportAppearance appearance;
    environment.source=EnvironmentSource::ProceduralSky;environment.fog.enabled=true;
    environment.debanding=true;
    environment.fog.mode=FogMode::Depth;environment.fog.heightDensity=-.3f;environment.fog.lightEnergy=2;
    environment.background.show=true;environment.background.overrideColor=true;environment.background.color={.4f,.2f,.8f};
    environment.enabled=false;
    auto presentation=environmentPresentation(environment,appearance,true);
    check(presentation.showBackground && presentation.procedural && presentation.fog.enabled,"Disabling IBL also disabled background or fog.");
    check(presentation.debanding,"Disabling environment lighting also disabled Debanding.");
    check(presentation.fog==environment.fog,"Authored fog parameters were lost during presentation extraction.");
    const auto fogBackground=fog::background(fog::parameters(presentation.fog,2048),{},presentation.showBackground,2048);
    check(fogBackground[3]==1 && fogBackground[0]>0,
        "Disabling environment illumination also disabled background fog.");
    environment.enabled=true;environment.background.show=false;
    presentation=environmentPresentation(environment,appearance,true);
    check(!presentation.showBackground && presentation.procedural && presentation.matte==environment.background.color,"Hidden sky lost its independent matte or lighting source.");
    environment.background.overrideColor=false;
    check(environmentPresentation(environment,appearance,true).matte==appearance.backgroundSrgb,"Absent background override must use editor preference.");
    environment.background.overrideColor=true;
    for(bool previewSky:{false,true}) {
        const auto preview=environmentPresentation(environment,appearance,false,previewSky);
        check(preview.showBackground==previewSky && !preview.procedural && !preview.fog.enabled && !preview.debanding && preview.matte==appearance.backgroundSrgb,
            "Asset previews inherited scene background, fog or linked sky.");
        near(fog::transmittance(fog::parameters(preview.fog,1000),1000,-1000),1);
        const Vec3 previewSource{2,1,.25f};
        check(fog::background(fog::parameters(preview.fog,1000),previewSource,previewSky,1000)==
            (previewSky?Vec4{2,1,.25f,1}:Vec4{}),"Asset preview background inherited scene fog.");
    }
    EnvironmentSettings fallback;
    check(!environmentPresentation(fallback,appearance,true).showBackground && fallback.enabled && fallback.source==EnvironmentSource::Hdri,
        "Absent Environment must retain studio lighting and editor matte.");
}
void thumbnailInvalidation() {
    EnvironmentSettings environment;ProceduralSun sun{{0,1,0},{1,1,1},2,true};
    check(!environment.specularAA,"Specular AA must remain opt-in.");
    auto signature=thumbnailEnvironmentKey(environment,sun);
    environment.specularAA=true;
    check(thumbnailEnvironmentKey(environment,sun)!=signature,"Specular AA did not invalidate the thumbnail's lighting fingerprint.");
    environment.specularAA=false;
    check(thumbnailEnvironmentKey(environment,sun)==signature,"Restoring Specular AA reused a different thumbnail fingerprint.");
    environment.procedural.skyTop={1,0,0};sun.direction={1,1,0};
    check(thumbnailEnvironmentKey(environment,sun)==signature,"Inactive procedural settings changed HDRI thumbnail.");
    environment.source=EnvironmentSource::ProceduralSky;
    signature=thumbnailEnvironmentKey(environment,sun);
    environment.map="assets/textures/inactive.hdr";environment.rotation=45;
    check(thumbnailEnvironmentKey(environment,sun)==signature,"Inactive HDRI changed procedural thumbnail.");
    const auto lighting=procedural::cacheKey(environment.procedural,sun);
    environment.debanding=true;
    check(thumbnailEnvironmentKey(environment,sun)!=signature,"Debanding did not invalidate thumbnail.");
    check(procedural::cacheKey(environment.procedural,sun)==lighting,"Debanding changed procedural lighting inputs.");
    signature=thumbnailEnvironmentKey(environment,sun);
    environment.fog.enabled=true;
    check(thumbnailEnvironmentKey(environment,sun)!=signature,"Fog did not invalidate thumbnail.");
    signature=thumbnailEnvironmentKey(environment,sun);environment.background.overrideColor=true;environment.background.color={1,0,0};
    check(thumbnailEnvironmentKey(environment,sun)!=signature,"Custom background did not invalidate thumbnail.");
    check(procedural::cacheKey(environment.procedural,sun)==lighting,"Presentation edits changed procedural lighting inputs.");
    signature=thumbnailEnvironmentKey(environment,sun);sun.direction={0,1,0};
    check(thumbnailEnvironmentKey(environment,sun)!=signature,"Linked-sun rotation did not invalidate thumbnail.");
    signature=thumbnailEnvironmentKey(environment,sun);sun.color={1,0,0};
    check(thumbnailEnvironmentKey(environment,sun)!=signature,"Linked-sun color did not invalidate thumbnail.");
    signature=thumbnailEnvironmentKey(environment,sun);sun.intensity=4;
    check(thumbnailEnvironmentKey(environment,sun)!=signature,"Linked-sun intensity did not invalidate thumbnail.");
    signature=thumbnailEnvironmentKey(environment,sun);sun.enabled=false;
    check(thumbnailEnvironmentKey(environment,sun)!=signature,"Disabling linked sun did not invalidate thumbnail.");
}
void toneMappingThumbnailInvalidation() {
    EnvironmentSettings environment;ProceduralSun sun;
    for(const auto mode:{ToneMapping::Reinhard,ToneMapping::Linear,ToneMapping::ACES,ToneMapping::AgX}) {
        environment.toneMapping=mode;
        const auto key=thumbnailEnvironmentKey(environment,sun);
        for(const auto member:{&ToneMappingParameters::reinhardWhite,&ToneMappingParameters::acesWhite,
                &ToneMappingParameters::agxWhite,&ToneMappingParameters::agxContrast}) {
            auto changed=environment;changed.toneMappingParameters.*member+=.5f;
            const bool active=(mode==ToneMapping::Reinhard && member==&ToneMappingParameters::reinhardWhite) ||
                (mode==ToneMapping::ACES && member==&ToneMappingParameters::acesWhite) ||
                (mode==ToneMapping::AgX && (member==&ToneMappingParameters::agxWhite || member==&ToneMappingParameters::agxContrast));
            check((thumbnailEnvironmentKey(changed,sun)!=key)==active,
                "Tone parameter thumbnail invalidation did not match the selected operator.");
        }
    }
}
void fogThumbnailInvalidation() {
    EnvironmentSettings environment;ProceduralSun sun;
    auto key=thumbnailEnvironmentKey(environment,sun);
    environment.fog.mode=FogMode::Depth;environment.fog.depthBegin=20;environment.fog.heightDensity=-.1f;environment.fog.lightEnergy=2;
    check(thumbnailEnvironmentKey(environment,sun)==key,"Disabled fog settings changed thumbnail key.");
    environment.fog.enabled=true;key=thumbnailEnvironmentKey(environment,sun);
    environment.fog.density=.4f;
    check(thumbnailEnvironmentKey(environment,sun)==key,"Inactive exponential density changed Depth fog thumbnail.");
    for(const auto member:{&FogSettings::depthBegin,&FogSettings::depthEnd,&FogSettings::depthCurve,&FogSettings::depthOpacity,
            &FogSettings::height,&FogSettings::heightDensity,&FogSettings::lightEnergy}) {
        auto changed=environment;changed.fog.*member+=.1f;
        check(thumbnailEnvironmentKey(changed,sun)!=key,"Active Depth/height/light-energy control did not invalidate thumbnail.");
    }
    environment.fog.mode=FogMode::Exponential;
    check(thumbnailEnvironmentKey(environment,sun)!=key,"Fog mode did not invalidate thumbnail.");
    key=thumbnailEnvironmentKey(environment,sun);
    environment.fog.depthBegin+=1;environment.fog.depthEnd+=1;environment.fog.depthCurve+=1;environment.fog.depthOpacity=.5f;
    check(thumbnailEnvironmentKey(environment,sun)==key,"Inactive Depth controls changed exponential fog thumbnail.");
    environment.fog.density+=.1f;
    check(thumbnailEnvironmentKey(environment,sun)!=key,"Exponential density did not invalidate thumbnail.");
    environment.fog.heightDensity=0;key=thumbnailEnvironmentKey(environment,sun);environment.fog.height+=5;
    check(thumbnailEnvironmentKey(environment,sun)==key,"Disabled height fog changed thumbnail key.");
    environment.fog.heightDensity=-.1f;key=thumbnailEnvironmentKey(environment,sun);environment.fog.heightDensity=.1f;
    check(thumbnailEnvironmentKey(environment,sun)!=key,"Height density sign did not invalidate thumbnail.");
}
void ambientLightControls() {
    EnvironmentSettings environment;
    check(environmentAmbientLight(environment,true)==Vec4{0,0,0,1},"Default ambient changed existing sky diffuse.");
    environment.ambientLight.source=AmbientLightSource::Color;
    environment.ambientLight.color={.5f,.25f,1};environment.ambientLight.energy=2;
    environment.enabled=false;
    auto values=environmentAmbientLight(environment,true);
    near(values[0],.42808228f);near(values[1],.10175218f);near(values[2],2);near(values[3],0);
    check(environmentAmbientLight(environment,false)==Vec4{0,0,0,1},"Scene ambient leaked into independent previews.");
    const auto colorValues=values;environment.ambientLight.skyContribution=.3f;
    check(environmentAmbientLight(environment,true)==colorValues,"Color ambient unexpectedly included sky contribution.");
    environment.ambientLight.source=AmbientLightSource::Sky;
    values=environmentAmbientLight(environment,true);
    for(std::size_t channel=0;channel<3;++channel)near(values[channel],colorValues[channel]*.7f);
    near(values[3],.3f);
    const auto signature=thumbnailEnvironmentKey(environment,{});
    environment.ambientLight.skyContribution=.6f;
    check(thumbnailEnvironmentKey(environment,{})!=signature,"Ambient sky contribution did not invalidate saved-scene thumbnail.");
    environment.ambientLight.source=AmbientLightSource::Disabled;
    check(environmentAmbientLight(environment,true)==Vec4{},"Disabled ambient retained color or sky diffuse.");
    const auto disabled=thumbnailEnvironmentKey(environment,{});
    environment.ambientLight.color={1,0,1};environment.ambientLight.skyContribution=0;environment.ambientLight.energy=5;
    check(thumbnailEnvironmentKey(environment,{})==disabled,"Inactive ambient controls invalidated saved-scene thumbnail.");
    environment.ambientLight.source=AmbientLightSource::Color;environment.ambientLight.energy=0;
    check(environmentAmbientLight(environment,true)==Vec4{},"Zero ambient energy retained diffuse lighting.");
    environment.ambientLight.energy=std::numeric_limits<float>::max();
    for(const float value:environmentAmbientLight(environment,true))check(std::isfinite(value),"Extreme ambient energy produced nonfinite GPU data.");
}
}
int main() {
    try {reflectionThumbnailIdentity();fogComposition();depthAndHeightFog();fogLightEnergy();fullPrecisionFogRadiance();materialFogReception();backgroundFog();independentPresentation();thumbnailInvalidation();toneMappingThumbnailInvalidation();fogThumbnailInvalidation();ambientLightControls();
        std::cout<<"Environment presentation and fog checks passed.\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
