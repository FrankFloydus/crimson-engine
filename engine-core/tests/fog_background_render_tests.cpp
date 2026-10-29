#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/Picking.h"
#include "crimson-renderer/RenderPassPlan.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "assets/MaterialGraph.h"
#include "resources/ResourceManager.h"
#include "GpuTestBackend.h"
#include "fog_aerial_perspective_fixture.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <lodepng.h>

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
struct ParticleShaderCache {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("crimson-particle-pixelization-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ParticleShaderCache(){std::filesystem::create_directories(path);}
    ~ParticleShaderCache(){std::error_code error;const auto target=std::filesystem::weakly_canonical(path,error);
        const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("crimson-particle-pixelization-"))
            std::filesystem::remove_all(target,error);}
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
void exercise(SDL_Window* window,bool postProcessingOnly,const std::filesystem::path& shaderTools) {
    ParticleShaderCache particleShaderCache;
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
        bgfx::blit(DiagnosticReadbackView,staging.value,0,0,target,0,0,Width,Height);
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
    if(!postProcessingOnly) {
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
    fog_aerial_fixture::exercise(renderer,sceneSettings,gameSettings,scene,game,environment,capture);
    }

    // Presentation effects operate on the antialiased display image. A smooth
    // sky plus a rotated opaque edge exercises block boundaries and tone levels
    // without a texture, authored project or external asset dependency.
    renderer.setDebugView(RenderViewPurpose::Scene,RenderDebugView::Lit);
    renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Lit);
    sceneSettings=SceneSettings{};sceneSettings.camera.position={0,0,3};sceneSettings.camera.target={};
    sceneSettings.environment.enabled=false;sceneSettings.environment.source=EnvironmentSource::ProceduralSky;
    sceneSettings.environment.background.show=true;sceneSettings.environment.intensity=1;
    sceneSettings.environment.procedural.skyTop={.95f,.6f,.3f};
    sceneSettings.environment.procedural.skyHorizon={.2f,.35f,.7f};
    sceneSettings.environment.procedural.groundHorizon={.2f,.35f,.7f};
    sceneSettings.environment.procedural.groundBottom={.5f,.8f,.4f};
    sceneSettings.environment.toneMapping=ToneMapping::Linear;sceneSettings.exposure=1;
    sceneSettings.antiAliasing=AntiAliasing::Smaa;gameSettings=sceneSettings;
    const auto postMesh=opaqueQuad();check(renderer.prepareModel(*postMesh),renderer.modelStatus());
    std::vector<ModelMaterial> postSurfaces(1);postSurfaces[0].baseColor={.65f,.25f,.1f,1};
    RenderModelInstance postInstance;postInstance.entityId=188;postInstance.model.asset=postMesh;
    postInstance.model.transform=math::rotation(math::axisAngle({0,0,1},.4f))*math::scaling({.65f,.65f,1});
    postInstance.materials=&postSurfaces;postInstance.shading=MaterialShading::Unlit;
    scene.instances=game.instances=std::span(&postInstance,1);scene.directionalLights=game.directionalLights={};
    const auto postBaseline=capture();
    const auto maxDifference=[](const DecodedImage& a,const DecodedImage& b) {
        unsigned maximum{};for(std::size_t byte=0;byte<a.rgba.size();++byte)
            maximum=std::max(maximum,unsigned(std::abs(int(a.rgba[byte])-int(b.rgba[byte]))));return maximum;
    };
    auto& pixel=sceneSettings.environment.pixelization;auto& poster=sceneSettings.environment.posterization;
    auto& dither=sceneSettings.environment.dither;
    auto& vignette=sceneSettings.environment.vignette;
    pixel.enabled=true;pixel.pixelSize=8;const auto pixelized=capture();
    check(maxDifference(pixelized[0],postBaseline[0])>10,"Pixelization did not alter the antialiased scene.");
    equivalent(pixelized[1],postBaseline[1],"Pixelization Game isolation");
    for(unsigned startY=0;startY<Height;startY+=8)for(unsigned startX=0;startX<Width;startX+=8) {
        const auto reference=(std::size_t(startY)*Width+startX)*4;
        for(unsigned y=startY;y<std::min(startY+8,unsigned(Height));++y)for(unsigned x=startX;x<std::min(startX+8,unsigned(Width));++x)
            for(unsigned channel=0;channel<3;++channel)
                check(pixelized[0].rgba[(std::size_t(y)*Width+x)*4+channel]==pixelized[0].rgba[reference+channel],
                    "SMAA or sampling softened a Pixelization block, including an odd-sized final block.");
    }
    pixel.enabled=false;poster.enabled=true;poster.levels=7;const auto posterized=capture();
    check(maxDifference(posterized[0],postBaseline[0])>10,"Posterization did not alter the scene.");
    equivalent(posterized[1],postBaseline[1],"Posterization Game isolation");
    for(std::size_t byte=0;byte<posterized[0].rgba.size();++byte)if(byte%4!=3) {
        const float code=posterized[0].rgba[byte];const float level=std::round(code*6/255);
        check(std::abs(code-level*255/6)<=1,"Posterization produced a color outside its seven display-referred levels.");
    }
    poster.enabled=false;dither.enabled=true;const auto dithered=capture();
    check(maxDifference(dithered[0],postBaseline[0])>=5,"Standalone ordered Dither did not produce a visible pattern.");
    equivalent(dithered[1],postBaseline[1],"Dither Game isolation");
    const auto stableDither=capture();equivalent(stableDither[0],dithered[0],"Dither frame stability");
    poster.enabled=true;const auto ditheredPoster=capture();
    check(maxDifference(ditheredPoster[0],posterized[0])>10,"Ordered Dither did not distribute Posterization bands.");
    for(std::size_t byte=0;byte<ditheredPoster[0].rgba.size();++byte)if(byte%4!=3) {
        const float code=ditheredPoster[0].rgba[byte];const float level=std::round(code*6/255);
        check(std::abs(code-level*255/6)<=1,"Dither introduced values outside the Posterization palette.");
    }
    // A four-pixel block size used as a sampled texel coordinate would alias
    // the 4x4 Bayer pattern to one threshold. Block indices must retain it.
    pixel={true,4};poster.enabled=false;const auto blockDither=capture();
    for(unsigned startY=0;startY<Height;startY+=4)for(unsigned startX=0;startX<Width;startX+=4) {
        const auto reference=(std::size_t(startY)*Width+startX)*4;
        for(unsigned y=startY;y<std::min(startY+4,unsigned(Height));++y)for(unsigned x=startX;x<std::min(startX+4,unsigned(Width));++x)
            for(unsigned channel=0;channel<3;++channel)
                check(blockDither[0].rgba[(std::size_t(y)*Width+x)*4+channel]==blockDither[0].rgba[reference+channel],
                    "Dither broke a Pixelization block.");
    }
    dither.enabled=false;const auto blocksOnly=capture();
    bool lighter=false,darker=false;
    for(std::size_t byte=0;byte<blockDither[0].rgba.size();++byte)if(byte%4!=3) {
        const int difference=int(blockDither[0].rgba[byte])-int(blocksOnly[0].rgba[byte]);
        lighter|=difference>=3;darker|=difference<=-3;
    }
    check(lighter&&darker,"Pixelization aliased the ordered Dither pattern to a constant threshold.");
    pixel.enabled=false;dither={true,0};const auto zeroDither=capture();
    equivalent(zeroDither[0],postBaseline[0],"Zero-strength Dither exact restoration");
    // A constant unlit receiver covers the viewport so cell checks inspect
    // the actual shader pattern, independently of scene gradients or edges.
    const auto previousPostTransform=postInstance.model.transform;
    postInstance.model.transform=math::scaling({100,100,1});dither.enabled=false;
    const auto constantBaseline=capture();
    solid(constantBaseline[0],{float(constantBaseline[0].rgba[0]),float(constantBaseline[0].rgba[1]),float(constantBaseline[0].rgba[2])},
        "Constant unlit Dither receiver");
    const auto uniformCells=[&](const DecodedImage& image,unsigned size,const char* label) {
        for(unsigned firstY=0;firstY<Height;firstY+=size)for(unsigned firstX=0;firstX<Width;firstX+=size) {
            const auto reference=(std::size_t(firstY)*Width+firstX)*4;
            for(unsigned y=firstY;y<std::min(firstY+size,unsigned(Height));++y)
                for(unsigned x=firstX;x<std::min(firstX+size,unsigned(Width));++x)for(unsigned channel=0;channel<3;++channel)
                    check(image.rgba[(std::size_t(y)*Width+x)*4+channel]==image.rgba[reference+channel],label);
        }
    };
    constexpr std::array patterns{DitherPattern::Bayer2x2,DitherPattern::Bayer4x4,DitherPattern::Bayer8x8,DitherPattern::InterleavedGradientNoise};
    std::array<DecodedImage,patterns.size()> patternImages;
    for(std::size_t index=0;index<patterns.size();++index) {
        pixel.enabled=poster.enabled=false;dither={true,1,patterns[index],3};
        const auto sized=capture();patternImages[index]=sized[0];
        check(maxDifference(sized[0],constantBaseline[0])>=3,"A Dither pattern did not alter its constant receiver.");
        equivalent(sized[1],constantBaseline[1],"Selectable Dither Game isolation");
        uniformCells(sized[0],3,"Size 3 split a Dither threshold cell.");
        dither.size=16;const auto largest=capture();
        uniformCells(largest[0],16,"Size 16 split a Dither threshold cell.");
        check(maxDifference(largest[0],sized[0])>=3,"Dither Size did not change the visible cell scale.");
        pixel={true,4};dither.size=3;poster={true,7};const auto pixelPalette=capture();
        uniformCells(pixelPalette[0],12,"Pattern or Size split a Pixelization block or Dither cell.");
        for(std::size_t byte=0;byte<pixelPalette[0].rgba.size();++byte)if(byte%4!=3) {
            const float code=pixelPalette[0].rgba[byte];const float level=std::round(code*6/255);
            check(std::abs(code-level*255/6)<=1,"Selectable Dither escaped the Posterization palette.");
        }
    }
    for(std::size_t a=0;a<patterns.size();++a)for(std::size_t b=a+1;b<patterns.size();++b)
        check(maxDifference(patternImages[a],patternImages[b])>=1,"Two selectable Dither patterns rendered the same image.");
    pixel.enabled=poster.enabled=false;dither={true,1,DitherPattern::InterleavedGradientNoise,3};
    const auto stableNoise=capture();equivalent(stableNoise[0],patternImages.back(),"Interleaved gradient noise frame stability");
    postInstance.model.transform=previousPostTransform;
    dither={};
    dither.enabled=false;vignette={true,.8f,.6f,.3f};const auto vignetted=capture();
    equivalent(vignetted[1],postBaseline[1],"Vignette Game isolation");
    const auto center=(std::size_t(Height/2)*Width+Width/2)*4;
    for(unsigned channel=0;channel<3;++channel) {
        check(std::abs(int(vignetted[0].rgba[center+channel])-int(postBaseline[0].rgba[center+channel]))<=1,"Vignette changed its clear center.");
        const auto expected=encoded(linear(Vec3{postBaseline[0].rgba[channel]/255.f,0,0})*.2f)[0];
        check(std::abs(float(vignetted[0].rgba[channel])-expected)<=3,"Vignette corner did not apply the authored linear attenuation.");
    }
    check(maxDifference(vignetted[0],postBaseline[0])>20,"Vignette did not darken the viewport edge.");
    pixel.enabled=poster.enabled=true;dither={true,1};const auto combined=capture();
    check(maxDifference(combined[0],pixelized[0])>10&&maxDifference(combined[0],posterized[0])>10&&
        maxDifference(combined[0],vignetted[0])>10,"Combined post-processing lost one or more enabled effects.");
    gameSettings=sceneSettings;const auto bothEffects=capture();
    check(maxDifference(bothEffects[0],bothEffects[1])<=1,"Scene/Game post-processing did not bind independent matching settings.");
    pixel.enabled=dither.enabled=poster.enabled=vignette.enabled=false;gameSettings=sceneSettings;const auto off=capture();
    equivalent(off[0],postBaseline[0],"Scene all post-processing disabled restoration");
    equivalent(off[1],postBaseline[1],"Game all post-processing disabled restoration");
    pixel.enabled=true;pixel.pixelSize=1;vignette.enabled=true;vignette.intensity=0;gameSettings=sceneSettings;
    const auto neutral=capture();equivalent(neutral[0],postBaseline[0],"Scene neutral Pixelization/Vignette");
    equivalent(neutral[1],postBaseline[1],"Game neutral Pixelization/Vignette");
    for(const auto mode:{RenderDebugView::Albedo,RenderDebugView::Roughness,RenderDebugView::Metal,RenderDebugView::Normal}) {
        renderer.setDebugView(RenderViewPurpose::Scene,mode);renderer.setDebugView(RenderViewPurpose::Game,mode);
        pixel.enabled=dither.enabled=poster.enabled=vignette.enabled=false;gameSettings=sceneSettings;const auto raw=capture();
        pixel={true,8};dither={true,1};poster={true,7};vignette={true,.8f,.6f,.3f};gameSettings=sceneSettings;const auto rawEffects=capture();
        equivalent(rawEffects[0],raw[0],"Scene RawData post-processing bypass");
        equivalent(rawEffects[1],raw[1],"Game RawData post-processing bypass");
    }
    scene.instances=game.instances={};renderer.evictModel(postMesh->id);
    renderer.setDebugView(RenderViewPurpose::Scene,RenderDebugView::Lit);
    renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Lit);
    sceneSettings=SceneSettings{};sceneSettings.camera.position={0,0,3};sceneSettings.camera.target={};
    sceneSettings.environment.enabled=false;sceneSettings.environment.ambientLight.source=AmbientLightSource::Disabled;
    sceneSettings.environment.background.overrideColor=true;sceneSettings.environment.background.color={};
    sceneSettings.environment.toneMapping=ToneMapping::Reinhard;sceneSettings.environment.debanding=false;
    sceneSettings.environment.vignette.enabled=false;gameSettings=sceneSettings;
    constexpr unsigned SoftExtent=32;
    std::vector<std::uint8_t> softPixels(SoftExtent*SoftExtent*4);
    for(unsigned y=0;y<SoftExtent;++y)for(unsigned x=0;x<SoftExtent;++x) {
        const double u=(2*(double(x)+.5)/SoftExtent-1)/.9,v=(2*(double(y)+.5)/SoftExtent-1)/.55;
        const double edge=std::clamp((1-std::hypot(u,v))*3,0.,1.);
        const auto value=static_cast<std::uint8_t>(std::lround(255*edge*edge*(3-2*edge)));
        const auto offset=(std::size_t(y)*SoftExtent+x)*4;
        softPixels[offset]=softPixels[offset+1]=softPixels[offset+2]=softPixels[offset+3]=value;
    }
    Texture softTexture{bgfx::createTexture2D(SoftExtent,SoftExtent,false,1,bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP,bgfx::copy(softPixels.data(),static_cast<std::uint32_t>(softPixels.size())))};
    check(bgfx::isValid(softTexture.value),"Particle Pixelization soft-edge texture allocation failed.");
    ShaderCompileConfig shaderConfig{shaderTools/"shaderc.exe",shaderTools/"shaders",shaderTools/"include",particleShaderCache.path};
    check(std::filesystem::is_regular_file(shaderConfig.compiler)&&std::filesystem::is_directory(shaderConfig.sourceDirectory)&&
        std::filesystem::is_directory(shaderConfig.includeDirectory),"Particle Pixelization requires the executable's paired shader-tools payload.");
    MaterialGraphPrograms programs(resources,shaderConfig);
    MaterialAsset authored;authored.id="post-process-soft-alpha-particle";
    const auto outputId=addMaterialNode(authored.graph,MaterialNodeKind::Output).id;
    auto& particleOutput=materialOutputNode(authored.graph).output;
    particleOutput.shading=MaterialShading::Unlit;particleOutput.blend=MaterialBlend::Alpha;
    particleOutput.baseColor={3,.75f,.2f};
    const auto alphaId=addMaterialNode(authored.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(authored.graph,alphaId)->texture.image.id="assets/textures/post-process-soft-alpha.png";
    findMaterialNode(authored.graph,alphaId)->texture.role=MaterialTextureRole::Data;
    connectMaterialNodes(authored.graph,alphaId,MaterialPin::A,outputId,MaterialPin::Opacity);
    const auto shader=generateMaterialGraphShader(authored);MaterialGraphProgramResult compiled;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{120};
    do {
        tick();compiled=programs.request(shader,ResourcePriority::Preview,MaterialGraphUsage::Surface);
        if(!compiled.current&&!compiled.diagnostic.empty()&&compiled.diagnostic!="Compiling material graph...")throw std::runtime_error(compiled.diagnostic);
        if(!compiled.current)std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }while(!compiled.current&&std::chrono::steady_clock::now()<deadline);
    check(compiled.current&&bool(compiled.programs),"Generated soft-alpha particle material publication timed out.");
    auto alphaPacket=std::make_shared<PreparedGraphMaterial>();alphaPacket->programs=compiled.programs;
    alphaPacket->uniforms=shader.uniforms;alphaPacket->sources=shader.textures;alphaPacket->shading=MaterialShading::Unlit;
    alphaPacket->blend=MaterialBlend::Alpha;alphaPacket->signature=compiled.programs->signature;
    check(shader.textures.size()==1,"Soft-alpha particle fixture lost its authored coverage texture.");
    alphaPacket->textures[shader.textures[0].slot]=softTexture.value;
    alphaPacket->flags[shader.textures[0].slot]=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP;
    RenderParticle particle;particle.position={.03f,-.02f,0};particle.size=1.8f;particle.rotation=.43f;
    RenderParticleMaterial sprite;sprite.shading=MaterialShading::Unlit;sprite.baseColor={3,.75f,.2f,.75f};
    sprite.colorTexture=softTexture.value;sprite.samplerFlags=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP;
    scene.particles=game.particles=std::span(&particle,1);scene.particleMaterials=game.particleMaterials=std::span(&sprite,1);
    char* captureValue{};std::size_t captureLength{};
    const auto captureError=_dupenv_s(&captureValue,&captureLength,"CRIMSON_TEST_CAPTURE_DIR");
    const std::filesystem::path captureDirectory=captureError==0&&captureValue?captureValue:"";std::free(captureValue);
    const auto saveEvidence=[&](const char* name,const DecodedImage& image) {
        if(captureDirectory.empty())return;
        std::filesystem::create_directories(captureDirectory);
        std::vector<unsigned char> bytes;check(lodepng::encode(bytes,image.rgba,image.width,image.height)==0,"Cannot encode particle Pixelization evidence.");
        std::ofstream file(captureDirectory/name,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        check(bool(file),"Cannot write particle Pixelization evidence.");
    };
    constexpr std::array blends{MaterialBlend::Alpha,MaterialBlend::Additive,MaterialBlend::Mask};
    for(unsigned materialCase=0;materialCase<4;++materialCase)for(const auto aa:{AntiAliasing::Off,AntiAliasing::Smaa})for(bool glow:{false,true}) {
        sprite.graph=materialCase==3?alphaPacket:std::shared_ptr<const PreparedGraphMaterial>{};
        sprite.blend=materialCase<3?blends[materialCase]:MaterialBlend::Alpha;
        sceneSettings.antiAliasing=aa;sceneSettings.environment.pixelization.enabled=false;
        sceneSettings.environment.glow.enabled=glow;sceneSettings.environment.glow.threshold=.05f;
        sceneSettings.environment.glow.hdrScale=.1f;sceneSettings.environment.glow.strength=.6f;
        gameSettings=sceneSettings;
        const auto baseline=capture();
        check(renderer.particleStats().submitted==1&&renderer.gameParticleStats().submitted==1,
            "Particle-only Pixelization fixture did not submit its Scene/Game sprites.");
        unsigned visible{};for(std::size_t byte=0;byte<baseline[0].rgba.size();byte+=4)
            visible+=baseline[0].rgba[byte]>32||baseline[0].rgba[byte+1]>32||baseline[0].rgba[byte+2]>32;
        check(visible>32,"Particle-only baseline has no visible textured sprite.");
        sceneSettings.environment.pixelization={true,8};const auto sceneOnly=capture();
        check(maxDifference(sceneOnly[0],baseline[0])>10,"Pixelization did not change particle-only Scene pixels.");
        uniformCells(sceneOnly[0],8,"A Scene particle bypassed an 8-pixel Pixelization block.");
        equivalent(sceneOnly[1],baseline[1],"Particle Pixelization Game isolation");
        gameSettings.environment.pixelization={true,8};const auto both=capture();
        uniformCells(both[0],8,"A Scene particle or its Glow escaped Pixelization.");
        uniformCells(both[1],8,"A Game particle or its Glow escaped Pixelization.");
        check(maxDifference(both[1],baseline[1])>10,"Pixelization did not change particle-only Game pixels.");
        if(materialCase==3&&aa==AntiAliasing::Smaa&&glow) {
            saveEvidence("particle-alpha-baseline-scene.png",baseline[0]);saveEvidence("particle-alpha-pixelized-scene.png",both[0]);
            saveEvidence("particle-alpha-baseline-game.png",baseline[1]);saveEvidence("particle-alpha-pixelized-game.png",both[1]);
        }
        sceneSettings.environment.pixelization.enabled=false;const auto gameOnly=capture();
        equivalent(gameOnly[0],baseline[0],"Particle Pixelization Scene disabled restoration");
        equivalent(gameOnly[1],both[1],"Particle Pixelization Scene isolation");
        gameSettings.environment.pixelization.enabled=false;const auto restored=capture();
        equivalent(restored[0],baseline[0],"Particle Pixelization Scene exact restoration");
        equivalent(restored[1],baseline[1],"Particle Pixelization Game exact restoration");
    }
    scene.particles=game.particles={};scene.particleMaterials=game.particleMaterials={};
    std::cout<<"Particle-only Scene/Game Pixelization passed for Alpha/Additive/Masked sprites, generated soft-alpha coverage, Off/SMAA, Glow off/on, odd 161x121 blocks, view isolation and exact restoration on "<<renderer.backendName()<<".\n";
    resources.stop();renderer.drainPending(frame);
    std::cout<<"Post-processing Pixelization after SMAA, ordered Dither, perceptual Posterization, Vignette, combined effects, exact disabled restoration, RawData bypass and Scene/Game isolation passed on "<<renderer.backendName()<<".\n";
    if(!postProcessingOnly)std::cout<<"Fog background Scene/Game controls and reference checks passed.\n";
}
}
int main(int argc,char** argv) {
    try {
        check(argc==1||(argc==2&&std::string_view(argv[1])=="--post-processing-only"),"Unknown fog/presentation test option.");
        Sdl sdl;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Crimson fog background regression",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(bool(window),SDL_GetError());exercise(window.get(),argc==2,std::filesystem::absolute(argv[0]).parent_path()/"shader-tools");return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
