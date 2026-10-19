#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/Picking.h"
#include "crimson-renderer/RenderPassPlan.h"
#include "resources/ResourceManager.h"
#include "GpuTestBackend.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace crimson;
void check(bool condition,const std::string& message) {
    if(!condition)throw std::runtime_error(message);
}
struct Sdl {
    Sdl(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}
    ~Sdl(){SDL_Quit();}
};
struct Texture {
    bgfx::TextureHandle value{BGFX_INVALID_HANDLE};
    ~Texture(){if(bgfx::isValid(value))bgfx::destroy(value);}
};
Vec3 linear(Vec3 color) {
    for(auto& value:color)value=value<=.04045f?value/12.92f:std::pow((value+.055f)/1.055f,2.4f);
    return color;
}
Vec3 encoded(Vec3 color) {
    for(auto& value:color) {
        check(std::isfinite(value),"Nonfinite CPU fog background reference.");
        value=std::clamp(value,0.f,1.f);
        value=255*(value<=.0031308f?12.92f*value:1.055f*std::pow(value,1.f/2.4f)-.055f);
    }
    return color;
}
Vec3 presented(Vec3 hdr,const SceneSettings& settings) {
    return encoded(toneMap(hdr*settings.exposure,settings.environment.toneMapping,settings.environment.toneMappingParameters));
}
Vec3 fogColor(const SceneSettings& settings) {
    return presented(linear(settings.environment.fog.color)*settings.environment.fog.lightEnergy,settings);
}
Vec3 backgroundColor(const SceneSettings& settings,const Vec3& matte,const Vec3& source,float worldHeight=0) {
    const auto& fog=settings.environment.fog;
    float transmission=1;
    if(fog.enabled) {
        if(fog.mode==FogMode::Exponential)transmission=std::exp(-fog.density*settings.camera.farPlane);
        else {
            const float end=fog.depthEnd>0?fog.depthEnd:settings.camera.farPlane;
            const float begin=std::min(fog.depthBegin,end-.001f);
            const float unit=std::clamp((settings.camera.farPlane-begin)/(end-begin),0.f,1.f);
            const float smooth=unit*unit*(3-2*unit);
            transmission=1-std::pow(smooth,fog.depthCurve)*fog.depthOpacity;
        }
        if(fog.heightDensity!=0)
            transmission=std::min(transmission,std::exp(std::min(0.f,(worldHeight-fog.height)*fog.heightDensity)));
    }
    const float opacity=(1-transmission)*fog.skyAffect;
    const Vec3 referenceSky=settings.environment.background.show?source:linear(matte);
    const Vec3 haze=linear(fog.color)*(fog.lightEnergy*(1-fog.aerialPerspective))+referenceSky*fog.aerialPerspective;
    const Vec3 radiance=haze*opacity+
        (settings.environment.background.show?source*(1-opacity):Vec3{});
    auto display=toneMap(radiance*settings.exposure,settings.environment.toneMapping,settings.environment.toneMappingParameters);
    if(!settings.environment.background.show)display=display+linear(matte)*(1-opacity);
    return encoded(display);
}
void solid(const DecodedImage& image,const Vec3& expected,const std::string& label) {
    check(image.width&&image.height&&image.rgba.size()==std::size_t(image.width)*image.height*4,label+": invalid image.");
    // All pixels are background in this regression, so an unfogged edge or a
    // stale viewport's uniform is as much a failure as an incorrect center.
    for(std::size_t pixel=0;pixel<image.rgba.size()/4;++pixel) {
        for(std::size_t channel=0;channel<3;++channel) {
            const auto actual=image.rgba[pixel*4+channel];
            if(std::abs(float(actual)-expected[channel])>2)
                throw std::runtime_error(label+": pixel="+std::to_string(pixel)+" channel="+std::to_string(channel)+
                    " GPU="+std::to_string(actual)+" CPU="+std::to_string(expected[channel]));
        }
        check(image.rgba[pixel*4+3]==255,label+": final background is not opaque.");
    }
}
void equivalent(const DecodedImage& actual,const DecodedImage& expected,const std::string& label) {
    check(actual.width==expected.width&&actual.height==expected.height,label+": dimensions changed.");
    check(actual.rgba==expected.rgba,label+": disabled fog did not restore the exact baseline.");
}
std::shared_ptr<ModelAsset> opaqueQuad() {
    auto asset=std::make_shared<ModelAsset>();asset->id="fog-opaque-quad";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    constexpr std::array<Vec3,4> positions{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
    constexpr std::array<Vec2,4> uv{{{0,1},{1,1},{1,0},{0,0}}};
    for(unsigned index=0;index<4;++index) {
        ModelVertex vertex;vertex.position=positions[index];vertex.normal={0,0,1};
        vertex.uv0=vertex.uv1=uv[index];vertex.tangent={1,0,0,-1};
        primitive.vertices.push_back(vertex);math::include(asset->bounds,vertex.position);
    }
    primitive.indices={0,1,2,0,2,3};asset->primitives.push_back(std::move(primitive));asset->draws.push_back({0,{}});
    asset->materials.emplace_back();return asset;
}
void centerColor(const DecodedImage& image,const Vec3& expected,const std::string& label) {
    const auto pixel=(std::size_t(image.height/2)*image.width+image.width/2)*4;
    for(unsigned channel=0;channel<3;++channel)
        check(std::abs(float(image.rgba[pixel+channel])-expected[channel])<=3,label+": unexpected center color.");
}
void exercise(SDL_Window* window) {
    ResourceManager resources;
    Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));
    TestGpuContext context;
    bgfx::reset(320,240,BGFX_RESET_NONE);
    renderer.setGridVisible(false);
    ViewportAppearance appearance;appearance.backgroundSrgb={.08f,.13f,.21f};renderer.setAppearance(appearance);
    constexpr std::uint16_t Width=161,Height=121;
    renderer.resizeViewport(Width,Height);renderer.resizeGameViewport(Width,Height);
    // Purely in-memory fixtures: no scene, session, asset or cache file is read
    // or changed. A constant HDR source makes sky-off restoration measurable.
    constexpr Vec3 HdriColor{.035f,.11f,.24f};
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x464f474247;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,HdriColor)},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    SceneSettings sceneSettings;
    sceneSettings.camera.position={3,4,6};sceneSettings.camera.target={0,0,0};
    sceneSettings.camera.nearPlane=.1f;sceneSettings.camera.farPlane=32;
    sceneSettings.environment.enabled=false;sceneSettings.environment.background.show=false;
    sceneSettings.environment.ambientLight.source=AmbientLightSource::Disabled;
    sceneSettings.environment.toneMapping=ToneMapping::Linear;sceneSettings.exposure=.7f;
    sceneSettings.environment.fog.color={.24f,.43f,.7f};sceneSettings.environment.fog.lightEnergy=2.4f;
    SceneSettings gameSettings=sceneSettings;
    RenderScene scene{sceneSettings,{}};scene.sceneMode=true;
    RenderScene game{gameSettings,{}};game.sceneMode=true;
    std::uint32_t frame{};
    const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=renderer.advance_frame();context.flush();};
    const auto draw=[&]{renderer.render(scene);renderer.renderGame(game);};
    const auto read=[&](bgfx::TextureHandle target) {
        Texture staging{bgfx::createTexture2D(Width,Height,false,1,bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.value),"Fog background readback allocation failed.");
        DecodedImage image{Width,Height,std::vector<std::uint8_t>(std::size_t(Width)*Height*4)};
        bgfx::blit(254,staging.value,0,0,target,0,0,Width,Height);
        const auto ready=bgfx::readTexture(staging.value,image.rgba.data());
        check(ready!=UINT32_MAX,"Fog background readback is unsupported.");
        for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return image;}
        renderer.drainPending(frame);throw std::runtime_error("Fog background readback timed out.");
    };
    const auto capture=[&] {
        for(unsigned i=0;i<3;++i){draw();tick();}
        draw();return std::array{read(renderer.viewportTexture()),read(renderer.gameViewportTexture())};
    };
    const auto expectBoth=[&](const std::array<DecodedImage,2>& images,const Vec3& expected,const std::string& label) {
        solid(images[0],expected,"Scene "+label);solid(images[1],expected,"Game "+label);
    };
    const auto zeroAndGrade=[&](const std::array<DecodedImage,2>& clear,const std::string& label,const Vec3& source=Vec3{}) {
        const auto matte=sceneSettings.environment.background.overrideColor?
            sceneSettings.environment.background.color:appearance.backgroundSrgb;
        sceneSettings.environment.fog.enabled=gameSettings.environment.fog.enabled=true;
        sceneSettings.environment.fog.mode=gameSettings.environment.fog.mode=FogMode::Exponential;
        sceneSettings.environment.fog.heightDensity=gameSettings.environment.fog.heightDensity=0;
        sceneSettings.environment.fog.density=gameSettings.environment.fog.density=0;
        const auto zero=capture();
        equivalent(zero[0],clear[0],"Scene "+label+" zero Density");
        equivalent(zero[1],clear[1],"Game "+label+" zero Density");
        const auto center=[](const DecodedImage& image) {
            const auto index=(std::size_t(image.height/2)*image.width+image.width/2)*4;
            return unsigned(image.rgba[index])+image.rgba[index+1]+image.rgba[index+2];
        };
        std::array<unsigned,2> previous{center(clear[0]),center(clear[1])};
        for(const float density:{.005f,.02f,.08f}) {
            sceneSettings.environment.fog.density=gameSettings.environment.fog.density=density;
            const auto fogged=capture();
            expectBoth(fogged,backgroundColor(sceneSettings,matte,source),label+" graded exponential reference");
            for(unsigned view=0;view<2;++view) {
                const auto actual=center(fogged[view]);
                check(actual>previous[view]+6,label+": increasing Density did not progressively blend the background toward fog.");
                previous[view]=actual;
            }
        }
        // A dense background still converges to the authored linear HDR fog.
        sceneSettings.environment.fog.density=gameSettings.environment.fog.density=2;
        expectBoth(capture(),fogColor(sceneSettings),label+" dense fog limit");
        sceneSettings.environment.fog.density=gameSettings.environment.fog.density=.02f;
        for(const float aerial:{.5f,1.f}) {
            sceneSettings.environment.fog.aerialPerspective=gameSettings.environment.fog.aerialPerspective=aerial;
            expectBoth(capture(),backgroundColor(sceneSettings,matte,source),label+" Aerial Perspective reference");
        }
        sceneSettings.environment.fog.aerialPerspective=gameSettings.environment.fog.aerialPerspective=0;
        sceneSettings.environment.fog.skyAffect=gameSettings.environment.fog.skyAffect=0;
        const auto noSkyFog=capture();
        equivalent(noSkyFog[0],clear[0],"Scene "+label+" Sky Affect zero");
        equivalent(noSkyFog[1],clear[1],"Game "+label+" Sky Affect zero");
        sceneSettings.environment.fog.skyAffect=gameSettings.environment.fog.skyAffect=.5f;
        expectBoth(capture(),backgroundColor(sceneSettings,matte,source),label+" Sky Affect half");
        sceneSettings.environment.fog.skyAffect=gameSettings.environment.fog.skyAffect=1;
        const auto sceneFar=sceneSettings.camera.farPlane,gameFar=gameSettings.camera.farPlane;
        sceneSettings.camera.farPlane=gameSettings.camera.farPlane=8;
        expectBoth(capture(),backgroundColor(sceneSettings,matte,source),label+" shorter fog integration range");
        sceneSettings.camera.farPlane=sceneFar;gameSettings.camera.farPlane=gameFar;
        sceneSettings.environment.fog.mode=gameSettings.environment.fog.mode=FogMode::Depth;
        sceneSettings.environment.fog.depthBegin=gameSettings.environment.fog.depthBegin=0;
        sceneSettings.environment.fog.depthEnd=gameSettings.environment.fog.depthEnd=0;
        sceneSettings.environment.fog.depthOpacity=gameSettings.environment.fog.depthOpacity=0;
        const auto noDepthFog=capture();
        equivalent(noDepthFog[0],clear[0],"Scene "+label+" Depth opacity zero");
        equivalent(noDepthFog[1],clear[1],"Game "+label+" Depth opacity zero");
        for(const float opacity:{.25f,.75f}) {
            sceneSettings.environment.fog.depthOpacity=gameSettings.environment.fog.depthOpacity=opacity;
            expectBoth(capture(),backgroundColor(sceneSettings,matte,source),label+" graded Depth reference");
        }
        sceneSettings.environment.fog.mode=gameSettings.environment.fog.mode=FogMode::Exponential;
        sceneSettings.environment.fog.density=gameSettings.environment.fog.density=2;
        sceneSettings.environment.fog.enabled=gameSettings.environment.fog.enabled=false;
        const auto restored=capture();
        equivalent(restored[0],clear[0],"Scene "+label+" toggle restoration");
        equivalent(restored[1],clear[1],"Game "+label+" toggle restoration");
    };
    const auto baseline=capture();
    expectBoth(baseline,encoded(linear(appearance.backgroundSrgb)),"empty Preferences matte");
    sceneSettings.exposure=gameSettings.exposure=3;
    const auto brightMatte=capture();
    equivalent(brightMatte[0],baseline[0],"Scene exposure-independent matte");
    equivalent(brightMatte[1],baseline[1],"Game exposure-independent matte");
    sceneSettings.exposure=gameSettings.exposure=.7f;

    // This is the original reported defect: no rendered geometry and no sky.
    zeroAndGrade(baseline,"empty Preferences matte");
    sceneSettings.environment.fog.enabled=gameSettings.environment.fog.enabled=true;
    expectBoth(capture(),fogColor(sceneSettings),"fog-only empty environment");
    for(const auto mode:{ToneMapping::Reinhard,ToneMapping::ACES,ToneMapping::AgX}) {
        sceneSettings.environment.toneMapping=gameSettings.environment.toneMapping=mode;
        sceneSettings.exposure=gameSettings.exposure=1.3f;
        expectBoth(capture(),fogColor(sceneSettings),std::string(toneMappingName(mode))+" HDR fog color/energy/exposure");
    }
    // Scene and Game must bind their own complete fog and presentation values
    // even when submitted in the same frame and one has a visible source sky.
    gameSettings.environment.background.show=true;
    gameSettings.environment.fog.color={.72f,.18f,.09f};gameSettings.environment.fog.lightEnergy=1.2f;
    gameSettings.environment.toneMapping=ToneMapping::ACES;gameSettings.exposure=.45f;
    const auto independent=capture();
    solid(independent[0],fogColor(sceneSettings),"independent Scene fog");
    solid(independent[1],fogColor(gameSettings),"independent Game fog");

    sceneSettings.environment.fog.enabled=false;
    sceneSettings.environment.background.overrideColor=true;sceneSettings.environment.background.color={.17f,.06f,.12f};
    sceneSettings.environment.toneMapping=ToneMapping::Linear;sceneSettings.exposure=.7f;
    gameSettings=sceneSettings;
    const auto customMatte=capture();
    expectBoth(customMatte,encoded(linear(sceneSettings.environment.background.color)),"custom matte");
    zeroAndGrade(customMatte,"custom matte");

    sceneSettings.environment.background.show=true;sceneSettings.environment.intensity=1.7f;
    gameSettings=sceneSettings;
    const auto hdri=capture();expectBoth(hdri,presented(HdriColor*sceneSettings.environment.intensity,sceneSettings),"visible constant HDRI");
    zeroAndGrade(hdri,"visible HDRI",HdriColor*sceneSettings.environment.intensity);

    sceneSettings.environment.source=EnvironmentSource::ProceduralSky;
    constexpr Vec3 ProceduralColor{.13f,.26f,.38f};
    auto& sky=sceneSettings.environment.procedural;
    sky.skyTop=sky.skyHorizon=sky.groundBottom=sky.groundHorizon=ProceduralColor;
    gameSettings=sceneSettings;
    const auto procedural=capture();expectBoth(procedural,presented(linear(ProceduralColor)*sceneSettings.environment.intensity,sceneSettings),"visible constant procedural sky");
    zeroAndGrade(procedural,"procedural sky",linear(ProceduralColor)*sceneSettings.environment.intensity);

    // Height fog uses world-space ray endpoints. HDRI sampling rotation cannot
    // rotate the height layer or substitute a single center-ray height.
    sceneSettings.environment.source=EnvironmentSource::Hdri;
    sceneSettings.environment.rotation=67;
    sceneSettings.environment.fog.enabled=true;sceneSettings.environment.fog.density=0;
    sceneSettings.environment.fog.height=-6;sceneSettings.environment.fog.heightDensity=.15f;
    gameSettings=sceneSettings;
    const auto heightFog=capture();
    for(unsigned view=0;view<2;++view)for(const unsigned x:{Width/4,Width/2,3*Width/4})
        for(const unsigned y:{Height/4,Height/2,3*Height/4}) {
            const auto ray=sceneSettings.camera.screenRay(float(x)+.5f,float(y)+.5f,{0,0,float(Width),float(Height)});
            check(bool(ray),"Height fog regression ray is invalid.");
            const float worldHeight=sceneSettings.camera.eye()[1]+ray->direction[1]*sceneSettings.camera.farPlane;
            const auto expected=backgroundColor(sceneSettings,{},HdriColor*sceneSettings.environment.intensity,worldHeight);
            const auto pixel=(std::size_t(y)*Width+x)*4;
            for(unsigned channel=0;channel<3;++channel)
                check(std::abs(float(heightFog[view].rgba[pixel+channel])-expected[channel])<=3,
                    "Background height fog did not follow the unrotated world-space ray.");
        }
    sceneSettings.environment.fog.heightDensity=0;
    sceneSettings.environment.fog.enabled=false;
    gameSettings=sceneSettings;

    // A directional source adds a forward lobe rather than a uniform lift.
    // The same frame contains both views so stale sun uniforms are observable.
    sceneSettings.camera.position={0,0,3};sceneSettings.camera.target={};
    sceneSettings.environment.background.show=false;
    sceneSettings.environment.background.overrideColor=true;sceneSettings.environment.background.color={};
    sceneSettings.environment.fog={};sceneSettings.environment.fog.enabled=true;
    sceneSettings.environment.fog.color={};sceneSettings.environment.fog.density=std::log(2.f)/32;
    sceneSettings.environment.toneMapping=ToneMapping::Linear;sceneSettings.exposure=1;
    gameSettings=sceneSettings;
    DirectionalLight sun;sun.direction={0,0,1};sun.color={.3f,.5f,.7f};sun.intensity=.2f;
    scene.directionalLights=game.directionalLights=std::span(&sun,1);
    const auto noScatter=capture();expectBoth(noScatter,{},"Sun Scatter zero");
    sceneSettings.environment.fog.sunScatter=gameSettings.environment.fog.sunScatter=1;
    const auto scattered=capture();
    for(unsigned view=0;view<2;++view)for(const unsigned x:{Width/4,Width/2,3*Width/4}) {
        constexpr unsigned Y=Height/2;
        const auto ray=sceneSettings.camera.screenRay(float(x)+.5f,float(Y)+.5f,{0,0,float(Width),float(Height)});
        check(bool(ray),"Sun Scatter regression ray is invalid.");
        const float cosine=std::max(-ray->direction[2],0.f);
        const float lobe=std::pow(cosine,8.f)*std::numbers::pi_v<float>;
        const auto expected=encoded(linear(sun.color)*(sun.intensity*lobe*.5f));
        const auto pixel=(std::size_t(Y)*Width+x)*4;
        for(unsigned channel=0;channel<3;++channel)
            check(std::abs(float(scattered[view].rgba[pixel+channel])-expected[channel])<=3,
                "Directional Sun Scatter disagreed with its forward lobe.");
    }
    sun.direction={0,0,-1};
    const auto away=capture();
    equivalent(away[0],noScatter[0],"Scene Sun Scatter away from source");
    equivalent(away[1],noScatter[1],"Game Sun Scatter away from source");
    sun.direction={0,0,1};sun.enabled=false;
    const auto disabledSun=capture();
    equivalent(disabledSun[0],noScatter[0],"Scene disabled Sun Scatter source");
    equivalent(disabledSun[1],noScatter[1],"Game disabled Sun Scatter source");

    // Finite opaque surfaces use their own distance and the same aerial/sun
    // color controls. Sky Affect remains a background-only exclusion.
    const auto mesh=opaqueQuad();check(renderer.prepareModel(*mesh),renderer.modelStatus());
    std::vector<ModelMaterial> surfaces(1);surfaces[0].baseColor={0,0,0,1};
    RenderModelInstance instance;instance.entityId=177;instance.model.asset=mesh;
    instance.materials=&surfaces;instance.shading=MaterialShading::Unlit;
    scene.instances=game.instances=std::span(&instance,1);
    sceneSettings.environment.fog.density=std::log(2.f)/3;
    sceneSettings.environment.fog.sunScatter=0;sceneSettings.environment.fog.skyAffect=0;
    sceneSettings.environment.fog.color={.2f,.4f,.6f};gameSettings=sceneSettings;
    const auto finiteColor=capture();
    for(unsigned view=0;view<2;++view)
        centerColor(finiteColor[view],presented(linear(sceneSettings.environment.fog.color)*.5f,sceneSettings),"Finite surface fog color");
    sceneSettings.environment.fog.aerialPerspective=gameSettings.environment.fog.aerialPerspective=1;
    const auto finiteAerial=capture();
    for(unsigned view=0;view<2;++view)
        centerColor(finiteAerial[view],presented(HdriColor*(sceneSettings.environment.intensity*.5f),sceneSettings),"Finite surface Aerial Perspective");
    sceneSettings.environment.fog.aerialPerspective=0;sceneSettings.environment.fog.color={};
    sceneSettings.environment.fog.sunScatter=1;gameSettings=sceneSettings;sun.enabled=true;
    const auto finiteSun=capture();
    for(unsigned view=0;view<2;++view)
        centerColor(finiteSun[view],presented(linear(sun.color)*(sun.intensity*std::numbers::pi_v<float>*.5f),sceneSettings),"Finite surface Sun Scatter");
    surfaces[0].receiveFog=false;
    const auto excluded=capture();
    for(unsigned view=0;view<2;++view)centerColor(excluded[view],{},"Receive Fog excluded aerial/sun controls");
    surfaces[0].receiveFog=true;instance.blend=MaterialBlend::Additive;
    const auto additive=capture();
    for(unsigned view=0;view<2;++view)centerColor(additive[view],{},"Additive fog added aerial/sun radiance");
    scene.instances=game.instances={};scene.directionalLights=game.directionalLights={};
    renderer.evictModel(mesh->id);
    sceneSettings.environment.fog.enabled=false;gameSettings=sceneSettings;

    for(const auto mode:{RenderDebugView::Albedo,RenderDebugView::Roughness,RenderDebugView::Metal,
        RenderDebugView::Normal,RenderDebugView::DirectLighting,RenderDebugView::IndirectLighting}) {
        renderer.setDebugView(RenderViewPurpose::Scene,mode);renderer.setDebugView(RenderViewPurpose::Game,mode);
        const auto debugBaseline=capture();
        sceneSettings.environment.fog.enabled=gameSettings.environment.fog.enabled=true;
        const auto debugFog=capture();
        equivalent(debugFog[0],debugBaseline[0],"Scene non-Lit background inspection");
        equivalent(debugFog[1],debugBaseline[1],"Game non-Lit background inspection");
        sceneSettings.environment.fog.enabled=gameSettings.environment.fog.enabled=false;
    }
    resources.stop();renderer.drainPending(frame);
    std::cout<<"Fog Scene/Game zero Density restoration, graded matte/HDRI/procedural response, Sky Affect/aerial/sun controls, finite-surface exclusions, HDR presentation and view isolation passed on "<<renderer.backendName()<<".\n";
}
}
int main() {
    try {
        Sdl sdl;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Crimson fog background regression",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(bool(window),SDL_GetError());exercise(window.get());return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
