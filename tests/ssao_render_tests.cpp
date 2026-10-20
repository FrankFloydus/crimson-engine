#include "crimson-renderer/Renderer.h"
#include "GpuTestBackend.h"
#include "crimson-renderer/RenderPassPlan.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/Picking.h"
#include "resources/ResourceManager.h"
#include <SDL3/SDL.h>
#include <bgfx/platform.h>
#include <d3d11.h>
#include <lodepng.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
void check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
struct Sdl{Sdl(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~Sdl(){SDL_Quit();}};
struct Texture{bgfx::TextureHandle value{BGFX_INVALID_HANDLE};~Texture(){if(bgfx::isValid(value))bgfx::destroy(value);}};
std::shared_ptr<ModelAsset> quad(){
    auto model=std::make_shared<ModelAsset>();model->id="ssao-ceiling-free-corner";ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    constexpr std::array<Vec3,4> positions{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
    for(const auto position:positions){ModelVertex vertex;vertex.position=position;vertex.normal={0,0,1};vertex.tangent={1,0,0,-1};primitive.vertices.push_back(vertex);math::include(model->bounds,position);}
    primitive.indices={0,1,2,0,2,3};model->primitives.push_back(std::move(primitive));model->draws.push_back({0,{}});
    ModelMaterial material;material.baseColor={.6f,.6f,.6f,1};material.roughness=1;material.metallic=0;material.specular=0;model->materials.push_back(material);return model;
}
double patch(const DecodedImage& image,const Camera& camera,Vec3 center){
    const auto vp=camera.projectionMatrix(float(image.width)/image.height,ClipDepth::ZeroToOne)*camera.viewMatrix();double sum{};unsigned count{};
    for(int z=-1;z<=1;++z)for(int x=-3;x<=3;++x){
        const auto point=math::project(center+Vec3{x*.04f,0,z*.012f},vp,{0,0,float(image.width),float(image.height)},ClipDepth::ZeroToOne);
        check(bool(point),"SSAO test patch cannot be projected.");const int px=int((*point)[0]),py=int((*point)[1]);
        check(px>0&&py>0&&px<int(image.width)-1&&py<int(image.height)-1,"SSAO test patch escaped viewport.");
        const auto index=(std::size_t(py)*image.width+px)*4;sum+=image.rgba[index]+image.rgba[index+1]+image.rgba[index+2];++count;
    }
    return sum/(3*count);
}
Vec3 patchRgb(const DecodedImage& image,const Camera& camera,Vec3 center){
    const auto vp=camera.projectionMatrix(float(image.width)/image.height,ClipDepth::ZeroToOne)*camera.viewMatrix();
    const auto projected=math::project(center,vp,{0,0,float(image.width),float(image.height)},ClipDepth::ZeroToOne);
    check(bool(projected),"Tone-mapping fixture patch cannot be projected.");const int px=int((*projected)[0]),py=int((*projected)[1]);
    check(px>0&&py>0&&px<int(image.width)-1&&py<int(image.height)-1,"Tone-mapping fixture patch escaped viewport.");
    Vec3 sum{};for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){const auto index=(std::size_t(py+y)*image.width+px+x)*4;
        for(unsigned channel=0;channel<3;++channel)sum[channel]+=image.rgba[index+channel];}return sum/9;
}
Vec3 encodedTone(Vec3 hdr,ToneMapping mode,const ToneMappingParameters& parameters,float exposure){
    auto result=toneMap(hdr*exposure,mode,parameters);
    for(auto& value:result){value=std::clamp(value,0.f,1.f);value=255*(value<=.0031308f?12.92f*value:1.055f*std::pow(value,1.f/2.4f)-.055f);}return result;
}
void matchesTone(Vec3 actual,Vec3 expected,const std::string& message){
    for(unsigned channel=0;channel<3;++channel)check(std::abs(actual[channel]-expected[channel])<=2,
        message+"; channel="+std::to_string(channel)+" GPU="+std::to_string(actual[channel])+" CPU="+std::to_string(expected[channel]));
}
double rgbDifference(Vec3 a,Vec3 b){double result{};for(unsigned channel=0;channel<3;++channel)result+=std::abs(a[channel]-b[channel]);return result;}
void equivalent(const DecodedImage& a,const DecodedImage& b,const char* message,unsigned tolerance=1){
    check(a.rgba.size()==b.rgba.size(),"SSAO image dimensions differ.");unsigned maximum{};for(std::size_t i=0;i<a.rgba.size();++i)maximum=std::max(maximum,unsigned(std::abs(int(a.rgba[i])-int(b.rgba[i]))));
    check(maximum<=tolerance,std::string(message)+"; maximum channel error="+std::to_string(maximum));
}
void save(const fs::path& directory,const char* name,const DecodedImage& image){if(directory.empty())return;fs::create_directories(directory);std::vector<std::uint8_t> encoded;
    check(lodepng::encode(encoded,image.rgba,image.width,image.height)==0,"SSAO PNG encoding failed.");std::ofstream file(directory/name,std::ios::binary);file.write(reinterpret_cast<const char*>(encoded.data()),std::streamsize(encoded.size()));check(bool(file),"SSAO image write failed.");}
void exercise(SDL_Window* window,const fs::path& artifacts){
    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));TestGpuContext context;
    bgfx::reset(320,240,BGFX_RESET_NONE);renderer.setGridVisible(false);ViewportAppearance appearance;appearance.backgroundSrgb={};renderer.setAppearance(appearance);
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x5353414f;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{})},environment.settings);check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    std::uint16_t width=321,height=241;renderer.resizeViewport(width,height);renderer.resizeGameViewport(width,height);std::uint32_t frame{};
    const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=renderer.advance_frame();context.flush();};
    const auto read=[&](bgfx::TextureHandle target){Texture staging{bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.value),"SSAO readback allocation failed.");DecodedImage image{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4)};
        bgfx::blit(DiagnosticReadbackView,staging.value,0,0,target,0,0,width,height);const auto ready=bgfx::readTexture(staging.value,image.rgba.data());check(ready!=UINT32_MAX,"SSAO readback unsupported.");
        for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return image;}renderer.drainPending(frame);throw std::runtime_error("SSAO readback timed out.");};
    const auto model=quad();check(renderer.prepareModel(*model),renderer.modelStatus());auto materials=model->materials;std::vector<ModelMaterialTextures> textures(1);
    RenderModelInstance floor;floor.entityId=1;floor.model={model,math::rotation(math::axisAngle({1,0,0},-1.57079632679f))*math::scaling({3,3,1})};floor.materials=&materials;floor.textures=&textures;floor.forceFullDetail=true;
    auto wall=floor;wall.entityId=2;wall.model.transform=math::translation({0,1.5f,-1})*math::scaling({3,1.5f,1});std::array instances{floor,wall};
    SceneSettings settings;settings.camera.position={2.8f,3.2f,4.2f};settings.camera.target={0,.35f,-.4f};settings.camera.fov=55;settings.camera.fovAxis=CameraFovAxis::Vertical;settings.camera.nearPlane=.1f;settings.camera.farPlane=32;
    settings.environment.enabled=false;settings.environment.ambientLight.source=AmbientLightSource::Color;settings.environment.ambientLight.color={1,1,1};settings.environment.ambientLight.energy=1;
    settings.environment.toneMapping=ToneMapping::Linear;settings.environment.background.overrideColor=true;settings.environment.background.color={};
    settings.environment.ssao={false,1,1,SsaoQuality::High};RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=instances;scene.giWorldId=1;
    const auto draw=[&]{renderer.renderGame(scene);check(renderer.ssaoStatus().empty(),renderer.ssaoStatus());};
    const auto capture=[&](unsigned warm=8){for(unsigned i=0;i<warm;++i){draw();tick();}draw();return read(renderer.gameViewportTexture());};
    const Vec3 nearPoint{0,.002f,-.84f},farPoint{0,.002f,1.15f};
    const auto baseline=capture();const double baseNear=patch(baseline,settings.camera,nearPoint),baseFar=patch(baseline,settings.camera,farPoint);
    check(baseNear>40&&std::abs(baseNear-baseFar)<3,"SSAO fixture lacks uniform diffuse ambient lighting.");
    settings.environment.ssao.enabled=true;const auto ao=capture();const double nearValue=patch(ao,settings.camera,nearPoint),farValue=patch(ao,settings.camera,farPoint);
    check(renderer.ssaoResidentBytes()>0,"Enabled SSAO created no scratch allocation.");
    check(nearValue<baseNear-3,"SSAO did not darken the ceiling-free wall/floor contact: "+std::to_string(baseNear)+" -> "+std::to_string(nearValue));
    check(std::abs(farValue-baseFar)<=2,"SSAO darkened the open flat plane: "+std::to_string(baseFar)+" -> "+std::to_string(farValue));
    save(artifacts,"ssao-off.png",baseline);save(artifacts,"ssao-on.png",ao);
    settings.environment.ssao.strength=2;const double strong=patch(capture(),settings.camera,nearPoint);check(strong<nearValue-2,"SSAO Strength did not deepen contact occlusion.");
    settings.environment.ssao.strength=1;settings.environment.ssao.radius=.05f;const double shortRadius=patch(capture(),settings.camera,nearPoint);check(shortRadius>nearValue+2,"SSAO Radius did not change world-space reach.");settings.environment.ssao.radius=1;
    settings.environment.ssao.strength=0;equivalent(capture(),baseline,"SSAO zero Strength changed existing output");settings.environment.ssao.strength=1;settings.environment.ssao.enabled=false;
    equivalent(capture(),baseline,"Disabled SSAO changed existing output");
    for(const auto quality:{SsaoQuality::Low,SsaoQuality::Medium,SsaoQuality::High}){settings.environment.ssao.enabled=true;settings.environment.ssao.quality=quality;check(patch(capture(),settings.camera,nearPoint)<baseNear-2,"SSAO quality lost corner occlusion.");}
    // Ordinary direct illumination and Lit emission are outside AO composition.
    settings.environment.ambientLight.source=AmbientLightSource::Disabled;DirectionalLight direct;direct.direction={0,-1,0};direct.intensity=1;direct.castShadows=false;scene.directionalLights=std::span(&direct,1);
    renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::DirectLighting);settings.environment.ssao.enabled=false;const auto directOff=capture();settings.environment.ssao.enabled=true;equivalent(capture(),directOff,"SSAO attenuated ordinary direct lighting");
    renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Lit);scene.directionalLights={};materials[0].emissive={.1f,.2f,.3f};settings.environment.ssao.enabled=false;const auto emissionOff=capture();settings.environment.ssao.enabled=true;equivalent(capture(),emissionOff,"SSAO attenuated Lit emission");materials[0].emissive={};
    settings.environment.ambientLight.source=AmbientLightSource::Color;
    SceneSettings sceneSettings=settings;sceneSettings.environment.ssao.enabled=true;settings.environment.ssao.enabled=false;RenderScene sceneView{sceneSettings,{}};sceneView.sceneMode=true;sceneView.instances=instances;
    for(unsigned i=0;i<8;++i){renderer.render(sceneView);draw();tick();}renderer.render(sceneView);draw();const auto game=read(renderer.gameViewportTexture());
    renderer.render(sceneView);draw();const auto editor=read(renderer.viewportTexture());check(patch(editor,settings.camera,nearPoint)<patch(game,settings.camera,nearPoint)-3,"Scene/Game SSAO state was shared across views.");
    width=323;height=243;renderer.resizeViewport(width,height);renderer.resizeGameViewport(width,height);settings.environment.ssao.enabled=true;
    check(patch(capture(),settings.camera,nearPoint)<baseNear-2,"Odd viewport resize lost SSAO.");
    settings.environment.ssgi.enabled=true;settings.environment.glow.enabled=true;settings.environment.fog.enabled=true;instances[1].opacity=.5f;
    capture(12);check(renderer.ssgiStatus().empty(),renderer.ssgiStatus());
    settings.environment.ssgi.enabled=false;settings.environment.ssao.enabled=false;sceneSettings.environment.ssao.enabled=false;
    for(unsigned i=0;i<8;++i){renderer.render(sceneView);draw();tick();}check(renderer.ssaoResidentBytes()==0,"SSAO scratch survived disabled-view retirement.");
    // The presentation controls use the same opaque, nonfaded model fixture;
    // known HDR emission isolates each curve from lighting and AO equations.
    instances[1].opacity=1;settings.environment.glow.enabled=false;
    settings.environment.fog.enabled=false;settings.environment.debanding=false;settings.environment.ambientLight.source=AmbientLightSource::Disabled;
    settings.exposure=1;scene.directionalLights={};scene.pointLights={};renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Lit);
    const auto toneSample=[&]{return patchRgb(capture(3),settings.camera,farPoint);};
    const auto& parameters=settings.environment.toneMappingParameters;
    for(const auto hdr:{Vec3{.18f,.18f,.18f},Vec3{.04f,.35f,3.2f}}) {
        materials[0].emissive=hdr;
        for(const auto mode:{ToneMapping::Reinhard,ToneMapping::ACES,ToneMapping::AgX}) {
            settings.environment.toneMapping=mode;settings.environment.toneMappingParameters={};
            matchesTone(toneSample(),encodedTone(hdr,mode,parameters,settings.exposure),std::string(toneMappingName(mode))+" default GPU curve differs from CPU reference");
        }
    }
    materials[0].emissive={.04f,.35f,3.2f};
    for(const auto mode:{ToneMapping::Reinhard,ToneMapping::ACES,ToneMapping::AgX}) {
        settings.environment.toneMapping=mode;settings.environment.toneMappingParameters={};const auto original=toneSample();
        if(mode==ToneMapping::Reinhard)settings.environment.toneMappingParameters.reinhardWhite=4;
        if(mode==ToneMapping::ACES)settings.environment.toneMappingParameters.acesWhite=4;
        if(mode==ToneMapping::AgX)settings.environment.toneMappingParameters.agxWhite=4;
        const auto tuned=toneSample();matchesTone(tuned,encodedTone(materials[0].emissive,mode,parameters,settings.exposure),std::string(toneMappingName(mode))+" White Point GPU curve differs from CPU reference");
        check(rgbDifference(tuned,original)>2,std::string(toneMappingName(mode))+" White Point did not change the GPU result.");
        settings.environment.toneMappingParameters={};matchesTone(toneSample(),original,std::string(toneMappingName(mode))+" did not restore its default appearance");
    }
    settings.environment.toneMapping=ToneMapping::AgX;settings.environment.toneMappingParameters={};const auto contrastDefault=toneSample();
    settings.environment.toneMappingParameters.agxContrast=2;const auto contrastTuned=toneSample();
    matchesTone(contrastTuned,encodedTone(materials[0].emissive,ToneMapping::AgX,parameters,settings.exposure),"AgX Contrast GPU curve differs from CPU reference");
    check(rgbDifference(contrastDefault,contrastTuned)>2,"AgX Contrast did not change the GPU result.");
    // Sequential Scene/Game submission must bind each view's complete curve.
    settings.environment.toneMapping=ToneMapping::ACES;settings.environment.toneMappingParameters={};
    sceneSettings=settings;settings.environment.toneMappingParameters.acesWhite=4;
    for(unsigned i=0;i<3;++i){renderer.render(sceneView);draw();tick();}
    renderer.render(sceneView);draw();const auto gameTone=patchRgb(read(renderer.gameViewportTexture()),settings.camera,farPoint);
    renderer.render(sceneView);draw();const auto editorTone=patchRgb(read(renderer.viewportTexture()),settings.camera,farPoint);
    matchesTone(gameTone,encodedTone(materials[0].emissive,ToneMapping::ACES,parameters,1),"Game tone parameters leaked from Scene");
    matchesTone(editorTone,encodedTone(materials[0].emissive,ToneMapping::ACES,sceneSettings.environment.toneMappingParameters,1),"Scene tone parameters leaked from Game");
    check(rgbDifference(editorTone,gameTone)>2,"Scene/Game White Point settings produced the same result.");
    // Data views keep their display contract even when all artistic curve
    // parameters and exposure change; lighting diagnostics retain those controls.
    renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Albedo);settings.environment.toneMappingParameters={};settings.exposure=1;
    const auto albedoDefault=capture(3);settings.environment.toneMapping=ToneMapping::AgX;
    settings.environment.toneMappingParameters={8,8,4,3};settings.exposure=8;equivalent(capture(3),albedoDefault,"Tone parameters or exposure changed Albedo inspection");
    materials[0].emissive={};settings.exposure=1;settings.environment.toneMapping=ToneMapping::ACES;settings.environment.toneMappingParameters={};
    scene.directionalLights=std::span(&direct,1);direct.intensity=2;
    renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::DirectLighting);const auto directToneDefault=toneSample();
    settings.environment.toneMappingParameters.acesWhite=4;const auto directToneTuned=toneSample();
    check(rgbDifference(directToneDefault,directToneTuned)>2,"Direct Lighting inspection ignored authored tone parameters.");
    settings.exposure=.5f;const auto directDim=toneSample();settings.exposure=2;check(rgbDifference(directDim,toneSample())>4,"Direct Lighting inspection ignored exposure.");
    scene.directionalLights={};settings.exposure=1;settings.environment.ambientLight.source=AmbientLightSource::Color;
    settings.environment.ambientLight.energy=2;settings.environment.toneMappingParameters={};
    renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::IndirectLighting);const auto indirectToneDefault=toneSample();
    settings.environment.toneMappingParameters.acesWhite=4;check(rgbDifference(indirectToneDefault,toneSample())>2,"Indirect Lighting inspection ignored authored tone parameters.");
    settings.exposure=.5f;const auto indirectDim=toneSample();settings.exposure=2;check(rgbDifference(indirectDim,toneSample())>4,"Indirect Lighting inspection ignored exposure.");
    renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Lit);
    std::cout<<"Tone mapping GPU defaults, White Point, AgX Contrast, Scene/Game isolation and debug-view contracts passed.\n";
    std::cout<<"SSAO wall/floor contact off="<<baseNear<<" on="<<nearValue<<" strength2="<<strong<<" radius.05="<<shortRadius<<"; open flat off="<<baseFar<<" on="<<farValue<<"\n";
    resources.stop();renderer.drainPending(frame);
}
}
int main(int argc,char** argv){try{fs::path artifacts;if(argc==3&&std::string_view(argv[1])=="--artifacts")artifacts=fs::absolute(argv[2]);else check(argc==1,"Usage: ssao_render_tests [--artifacts DIRECTORY]");
    Sdl sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson SSAO checks",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);check(bool(window),SDL_GetError());exercise(window.get(),artifacts);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
