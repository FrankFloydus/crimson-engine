#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/RenderPassPlan.h"
#include "crimson-renderer/Picking.h"
#include "crimson-renderer/Environment.h"
#include "assets/BuiltinGeometry.h"
#include "GpuTestBackend.h"
#include <SDL3/SDL.h>
#include <lodepng.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace crimson;
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Sdl {Sdl(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~Sdl(){SDL_Quit();}};
struct Texture {bgfx::TextureHandle value{BGFX_INVALID_HANDLE};~Texture(){if(bgfx::isValid(value))bgfx::destroy(value);}};
void equivalent(const DecodedImage& a,const DecodedImage& b,const std::string& label) {
    check(a.rgba.size()==b.rgba.size(),label+": image sizes differ.");
    std::size_t differences{};
    for(std::size_t pixel=0;pixel<a.rgba.size()/4;++pixel) {
        bool different{};for(unsigned c=0;c<3;++c)different|=std::abs(int(a.rgba[pixel*4+c])-int(b.rgba[pixel*4+c]))>3;
        differences+=different;
    }
    check(differences<a.rgba.size()/4/100+1,label+": more than 1% of pixels differ ("+std::to_string(differences)+").");
}
void save(const std::filesystem::path& path,const DecodedImage& image) {
    if(path.empty())return;std::filesystem::create_directories(path.parent_path());std::vector<std::uint8_t> png;
    check(!lodepng::encode(png,image.rgba,image.width,image.height),"Cannot encode instance evidence.");
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(png.data()),std::streamsize(png.size()));
    check(bool(file),"Cannot save instance evidence.");
}
void exercise(SDL_Window* window,const std::filesystem::path& output) {
    Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));TestGpuContext context;
    bgfx::reset(320,240,BGFX_RESET_NONE);renderer.setGridVisible(false);
    ViewportAppearance appearance;appearance.backgroundSrgb={};renderer.setAppearance(appearance);
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x494e5354414e4345ull;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.1f,.12f,.15f})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    const auto model=builtinBoxGeometry();check(renderer.prepareModel(*model),renderer.modelStatus());
    auto materials=model->materials;materials[0].baseColor={.45f,.58f,.72f,1};materials[0].metallic=.15f;materials[0].roughness=.4f;
    std::vector<ModelMaterialTextures> textures(1);
    std::vector<RenderModelInstance> instances;instances.reserve(100000);
    for(unsigned y=0;y<10;++y)for(unsigned z=0;z<100;++z)for(unsigned x=0;x<100;++x) {
        RenderModelInstance instance;instance.entityId=instances.size()+1;
        instance.model={model,math::translation({(float(x)-49.5f)*.8f,float(y)*.8f,(float(z)-49.5f)*.8f})*math::scaling({.35f,.35f,.35f})};
        instance.forceFullDetail=true;instance.materials=&materials;instance.textures=&textures;instances.push_back(std::move(instance));
    }
    constexpr std::uint16_t Width=641,Height=481;
    renderer.resizeViewport(Width,Height);renderer.resizeGameViewport(Width,Height);
    SceneSettings settings;settings.camera.position={65,60,85};settings.camera.target={0,3,0};settings.camera.fov=60;
    settings.camera.fovAxis=CameraFovAxis::Vertical;settings.camera.nearPlane=.1f;settings.camera.farPlane=512;
    settings.environment.ambientLight.source=AmbientLightSource::Color;settings.environment.ambientLight.color={.3f,.35f,.4f};
    settings.environment.toneMapping=ToneMapping::Linear;
    DirectionalLight sun;sun.direction={-.4f,-1,-.6f};sun.intensity=2;
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=instances;scene.directionalLights=std::span(&sun,1);
    scene.shadowWorldId=0x494e5354414e4345ull;scene.meshPopulationRevision=1;
    std::uint32_t frame{};
    const auto tick=[&]{renderer.advanceStreaming();frame=renderer.advance_frame();context.flush();};
    const auto draw=[&]{renderer.render(scene);};
    const auto read=[&]{
        Texture staging{bgfx::createTexture2D(Width,Height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.value),"Instance readback allocation failed.");
        DecodedImage image{Width,Height,std::vector<std::uint8_t>(std::size_t(Width)*Height*4)};
        bgfx::blit(254,staging.value,0,0,renderer.viewportTexture(),0,0,Width,Height);
        const auto ready=bgfx::readTexture(staging.value,image.rgba.data());
        check(ready!=UINT32_MAX,"Instance readback is unavailable.");
        for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return image;}
        throw std::runtime_error("Instance readback timed out.");
    };
    const auto capture=[&]{for(unsigned i=0;i<3;++i){draw();tick();}draw();return read();};
    double coldSubmissionMs{};std::vector<double> warmSubmissions;
    for(unsigned i=0;i<33;++i) {
        const auto start=std::chrono::steady_clock::now();draw();
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(i)warmSubmissions.push_back(elapsed);else coldSubmissionMs=elapsed;
        const auto stats=renderer.meshInstancingStats(RenderViewPurpose::Scene);
        check(stats.instancedInstances==100000&&stats.instancedDraws==2&&!stats.fallbackDraws,"100000 identical cubes did not use two actual GPU instanced color draws.");
        if(i)check(!stats.uploadBytes&&!stats.dirtyRecords&&!stats.cpuRecordRebuilds,"Unchanged 100000-cube population was reuploaded.");
        tick();
    }
    double warmMean{};for(const auto value:warmSubmissions)warmMean+=value;warmMean/=warmSubmissions.size();
    std::sort(warmSubmissions.begin(),warmSubmissions.end());
    const double warmMedian=warmSubmissions[warmSubmissions.size()/2],warmP95=warmSubmissions[(warmSubmissions.size()*95+99)/100-1];
    const auto* gpu=bgfx::getStats();
    std::cout<<"100000 cubes on "<<renderer.backendName()<<": color_draws=2, submitted_instances=100000, bgfx_total_draws="<<gpu->numDraw
        <<", primitive_instances="<<gpu->numPrims[bgfx::Topology::TriList]<<", unchanged_upload_bytes=0, unchanged_cpu_record_rebuilds=0"
        <<", cold_submit_cpu_ms="<<coldSubmissionMs<<", warm_samples="<<warmSubmissions.size()<<", warm_mean_submit_cpu_ms="<<warmMean
        <<", warm_median_submit_cpu_ms="<<warmMedian<<", warm_p95_submit_cpu_ms="<<warmP95<<"\n";
    const auto populationImage=capture();
    std::size_t lit{};for(std::size_t p=0;p<populationImage.rgba.size()/4;++p)lit+=populationImage.rgba[p*4]+populationImage.rgba[p*4+1]+populationImage.rgba[p*4+2]>20;
    check(lit>1000,"100000-cube pass produced no substantial visible geometry.");
    if(!output.empty())save(output/"100000-cubes.png",populationImage);
    instances[50505].model.transform[12]+=.125f;++scene.meshPopulationRevision;draw();
    const auto dirty=renderer.meshInstancingStats(RenderViewPurpose::Scene);
    check(dirty.dirtyRecords==1&&dirty.uploadBytes==256,"Single transform edit did not upload exactly one 256-byte record.");tick();
    std::cout<<"Single edit: dirty_records="<<dirty.dirtyRecords<<", upload_bytes="<<dirty.uploadBytes<<"\n";
    draw();check(!renderer.meshInstancingStats(RenderViewPurpose::Scene).uploadBytes,"Dirty edit was uploaded again on the unchanged frame.");tick();
    // Independent Game receives the same population through the actual shared
    // Forward+ entry point and owns its own retained palette.
    renderer.renderGame(scene);check(renderer.meshInstancingStats(RenderViewPurpose::Game).instancedInstances==100000,"Game did not use general mesh instancing.");tick();
    renderer.renderGame(scene);check(!renderer.meshInstancingStats(RenderViewPurpose::Game).uploadBytes,"Game unchanged palette was reuploaded.");tick();
    // Full affine/reflected occurrences share exactly the same fixed PBR and
    // coverage programs as the per-object reference.
    instances.resize(6);scene.instances=instances;scene.meshPopulationRevision=3;
    settings.camera.position={0,3.5f,9};settings.camera.target={0,.5f,0};settings.camera.fov=50;settings.camera.farPlane=32;
    Mat4 shear;shear.at(0,1)=.35f;
    const std::array transforms{
        math::translation({-1.8f,0,0})*math::rotation(math::axisAngle({0,1,0},.3))*math::scaling({.8f,1.2f,.5f}),
        math::scaling({.9f,.9f,.9f}),
        math::translation({1.8f,0,0})*shear*math::scaling({.8f,1.1f,.7f}),
        math::translation({-1.8f,1.6f,-2})*math::scaling({-1,1,1}),
        math::translation({0,1.6f,-2})*math::rotation(math::axisAngle({0,1,0},.2))*math::scaling({-1,.7f,1.2f}),
        math::translation({1.8f,1.6f,-2})*shear};
    for(unsigned i=0;i<instances.size();++i){instances[i].entityId=1001+i;instances[i].model.transform=transforms[i];}
    const auto instanced=capture();check(renderer.meshInstancingStats(RenderViewPurpose::Scene).instancedInstances==6,"Affine/reflected groups fell back to per-object draws.");
    scene.meshInstancingEnabled=false;const auto reference=capture();equivalent(instanced,reference,"Affine PBR");scene.meshInstancingEnabled=true;
    if(!output.empty()){save(output/"affine-instanced.png",instanced);save(output/"affine-reference.png",reference);}
    // Selection maps the GPU instance payload back to exact persistent object
    // identity, including a non-first member of the compatibility group.
    const auto vp=settings.camera.projectionMatrix(float(Width)/Height,ClipDepth::ZeroToOne)*settings.camera.viewMatrix();
    const auto point=math::project({0,0,0},vp,{0,0,float(Width),float(Height)},ClipDepth::ZeroToOne);
    check(bool(point),"Cannot project pick witness.");
    PickRequest request;request.generation=1;request.revision=1;request.serial=1;request.x=std::uint32_t((*point)[0]);request.y=std::uint32_t((*point)[1]);
    draw();tick();draw();
    check(!renderer.meshInstancingStats(RenderViewPurpose::Scene).cpuRecordRebuilds,"Six-cube witness failed to reuse its accepted CPU population.");tick();
    check(renderer.requestPick(request),renderer.pickingStatus());draw();
    check(!renderer.meshInstancingStats(RenderViewPurpose::Scene).cpuRecordRebuilds,"Enabling picking rebuilt the unchanged CPU population.");
    check(renderer.meshInstancingStats(RenderViewPurpose::Scene).pickingDraws==2,"Selection did not use instanced occurrence identities.");
    std::optional<PickResult> picked;
    for(unsigned i=0;i<120&&!picked;++i){tick();picked=renderer.pollPick(frame);}
    check(picked&&picked->entityId==1002&&picked->materialSlot==0,"Instanced selection resolved an incorrect stable entity/material identity.");
    // Readable scene depth (SSAO prepass) and both shadow families retain the
    // same affine transforms and fixed cutout coverage.
    settings.environment.ssao.enabled=true;const auto depthImage=capture();
    check(!renderer.meshInstancingStats(RenderViewPurpose::Scene).cpuRecordRebuilds,"Enabling readable depth rebuilt the unchanged CPU population.");
    check(renderer.meshInstancingStats(RenderViewPurpose::Scene).depthDraws==2,"Readable depth did not instance the same occurrence groups.");
    scene.meshInstancingEnabled=false;equivalent(depthImage,capture(),"Readable depth/AO");scene.meshInstancingEnabled=true;settings.environment.ssao.enabled=false;
    sun.castShadows=true;sun.shadowDistance=24;sun.shadowAngularDiameter=0;
    PointLight pointLight;pointLight.position={0,3,4};pointLight.radius=15;pointLight.castShadows=true;pointLight.entityId=55;
    scene.pointLights=std::span(&pointLight,1);scene.shadowInstances=instances;scene.shadowResolution=256;
    draw();const auto shadowDraws=renderer.meshInstancingStats(RenderViewPurpose::Scene).shadowDraws;check(shadowDraws>0,"Shadow casters did not issue any instanced draws.");tick();
    const auto shadowImage=capture();
    // Invalidate cached shadow signatures before comparing the reference.
    scene.meshInstancingEnabled=false;++scene.shadowWorldId;const auto shadowReference=capture();equivalent(shadowImage,shadowReference,"Point/directional shadows");
    scene.meshInstancingEnabled=true;++scene.shadowWorldId;
    // Authored transparency remains deliberately outside opaque grouping.
    instances[1].opacity=.5f;++scene.meshPopulationRevision;draw();check(renderer.meshInstancingStats(RenderViewPurpose::Scene).fallbackDraws>0,"Transparent object lost the sorted per-object fallback.");tick();
    renderer.drainPending(frame);
    std::cout<<"Full affine/reflected PBR parity, Game, stable object/material picking, readable depth/AO, point/directional shadows, transparency fallback and retained uploads passed.\n";
}
}
int main(int argc,char** argv) {
    try {
        Sdl sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Crimson general mesh instancing regression",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(bool(window),SDL_GetError());exercise(window.get(),argc>1?std::filesystem::path(argv[1]):std::filesystem::path{});return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
