#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/RenderPassPlan.h"
#include "crimson-renderer/Picking.h"
#include "resources/ResourceManager.h"
#include "resources/ImageDecoder.h"
#include <crimson/renderer/forward/MeshPaint.h>
#include "GpuTestBackend.h"
#include <SDL3/SDL.h>
#include <lodepng.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
constexpr std::uint16_t Size=192;
constexpr std::array<Vec3,4> Colors{{{.04f,.06f,.1f},{.8f,.03f,.02f},{.02f,.7f,.03f},{.03f,.04f,.75f}}};
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Texture {bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};~Texture(){if(bgfx::isValid(handle))bgfx::destroy(handle);}};
struct Fixture {std::shared_ptr<ModelAsset> model;std::shared_ptr<const MeshPaintGeometry> geometry;};
Fixture quad(bool skew,bool opposite,Vec4 tint={1,1,1,1}) {
    ModelPrimitive source;source.uvMask=1;source.tangentUv=0;
    constexpr std::array<Vec2,4> uv{{{0,0},{1,0},{1,1},{0,1}}};
    const std::array<Vec3,4> positions{{{-.9f,-.8f,0},{.9f,-.8f,0},{skew?.5f:.9f,.8f,0},{-.9f,.8f,0}}};
    for(unsigned i=0;i<4;++i){ModelVertex vertex;vertex.position=positions[i];vertex.normal={0,0,1};vertex.uv0=uv[i];vertex.tangent={1,0,0,1};vertex.color=tint;source.vertices.push_back(vertex);math::include(source.bounds,vertex.position);}
    source.indices=opposite?std::vector<std::uint32_t>{0,1,3,1,2,3}:std::vector<std::uint32_t>{0,1,2,0,2,3};
    source.sourceFaces.push_back({{0,1,2,3},{0,1},4});
    auto geometry=buildMeshPaintGeometry(source,0);
    check(geometry->cells.size()==1&&geometry->cells[0].cornerCount==4,"Native fixture lost its logical quad.");
    auto model=std::make_shared<ModelAsset>();model->id="blend-native-"+std::to_string(skew)+"-"+std::to_string(opposite)+"-"+std::to_string(tint[0]);
    model->primitives.push_back(meshPaintRenderPrimitive(source,*geometry));model->bounds=geometry->bounds;model->materials.emplace_back();model->draws.push_back({0,ModelIdentity});
    return {std::move(model),std::move(geometry)};
}
std::shared_ptr<const MeshPaintPayload> weights(const MeshPaintGeometry& geometry,unsigned layer,bool oneCorner=false) {
    auto value=makeMeshPaintPayload(static_cast<std::uint32_t>(geometry.vertices.size()));std::vector<MeshPaintChange> changes;
    if(layer){MeshPaintRgb rgb{};rgb[layer-1]=255;if(oneCorner)changes.push_back({geometry.cells[0].vertices[2],rgb});else for(std::uint32_t i=0;i<geometry.vertices.size();++i)changes.push_back({i,rgb});}
    return updateMeshPaintPayload(value,changes);
}
MaterialGraphSourceSet layers(bool normal) {
    MaterialGraphSourceSet result;
    for(unsigned i=0;i<4;++i){auto source=std::make_shared<MaterialAsset>();source->id="native-blend-source-"+std::to_string(i);source->name=source->id;
        const auto output=addMaterialNode(source->graph,MaterialNodeKind::Output).id;materialOutputNode(source->graph).output.baseColor=Colors[i];
        if(normal&&i==1){auto& node=addMaterialNode(source->graph,MaterialNodeKind::Texture2D);node.texture.role=MaterialTextureRole::Normal;node.texture.image.id="assets/textures/native-blend-normal.png";const auto id=node.id;connectMaterialNodesData(source->graph,id,MaterialPin::Normal,output,MaterialPin::Normal);}
        result.push_back(std::move(source));}
    return result;
}
MaterialAsset blend(const MaterialGraphSourceSet& sources) {
    MaterialAsset result;result.id="native-blend";result.name="Native Blend";result.shading=MaterialShading::BlendPbr;
    const auto output=addMaterialNode(result.graph,MaterialNodeKind::Output).id;auto& settings=materialOutputNode(result.graph).output;settings.shading=MaterialShading::BlendPbr;
    const std::array pins{MaterialPin::Base,MaterialPin::Red,MaterialPin::Green,MaterialPin::Blue};
    for(unsigned i=0;i<4;++i){auto& node=addMaterialNode(result.graph,MaterialNodeKind::MaterialReference);node.material.id=sources[i]->id;const auto id=node.id;connectMaterialNodesData(result.graph,id,MaterialPin::Surface,output,pins[i]);}
    return result;
}
int encoded(float value){value=std::clamp(value,0.f,1.f);return int(std::lround(255*(value<=.0031308f?value*12.92f:1.055f*std::pow(value,1.f/2.4f)-.055f)));}
float decoded(std::uint8_t value){const auto x=value/255.f;return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);}
std::array<unsigned,2> pixel(const Camera& camera,Vec3 point) {
    const auto projected=math::project(point,camera.projectionMatrix(1,ClipDepth::ZeroToOne)*camera.viewMatrix(),{0,0,Size,Size},ClipDepth::ZeroToOne);
    check(projected&&(*projected)[0]>0&&(*projected)[1]>0&&(*projected)[0]<Size&&(*projected)[1]<Size,"Blend probe escaped viewport.");return {unsigned((*projected)[0]),unsigned((*projected)[1])};
}
void expect(const DecodedImage& image,const Camera& camera,Vec3 point,Vec3 color,const std::string& label,int tolerance=4) {
    const auto p=pixel(camera,point);const auto offset=(std::size_t(p[1])*Size+p[0])*4;
    for(unsigned c=0;c<3;++c)check(std::abs(int(image.rgba[offset+c])-encoded(color[c]))<=tolerance,label+": channel "+std::to_string(c)+" actual="+std::to_string(image.rgba[offset+c])+" expected="+std::to_string(encoded(color[c])));
}
void save(const std::filesystem::path& folder,const std::string& name,const DecodedImage& image) {
    if(folder.empty())return;std::filesystem::create_directories(folder);std::vector<unsigned char> png;check(lodepng::encode(png,image.rgba,image.width,image.height)==0,"Cannot encode Blend capture.");std::ofstream file(folder/(name+".png"),std::ios::binary);file.write(reinterpret_cast<const char*>(png.data()),std::streamsize(png.size()));check(bool(file),"Cannot save Blend capture.");
}
void exercise(SDL_Window* window,const ShaderCompileConfig& config,const std::filesystem::path& output) {
    // Readback destinations outlive Renderer if a bounded wait fails.
    std::vector<std::shared_ptr<DecodedImage>> destinations;
    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));MaterialGraphPrograms programs(resources,config);TestGpuContext context;
    renderer.resizeViewport(Size,Size);renderer.resizeGameViewport(Size,Size);renderer.setGridVisible(false);
    ViewportAppearance appearance;appearance.backgroundSrgb={};renderer.setAppearance(appearance);
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x424c454e445047ull;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.2f,.2f,.2f})},environment.settings);check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    std::uint32_t frame{};
    const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=renderer.advance_frame();context.flush();};
    const auto read=[&](bgfx::TextureHandle source){
        Texture staging{bgfx::createTexture2D(Size,Size,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};check(bgfx::isValid(staging.handle),"Blend readback allocation failed.");
        auto pixels=std::make_shared<DecodedImage>(DecodedImage{Size,Size,std::vector<std::uint8_t>(std::size_t(Size)*Size*4)});destinations.push_back(pixels);
        bgfx::blit(DiagnosticReadbackView,staging.handle,0,0,source,0,0,Size,Size);const auto ready=bgfx::readTexture(staging.handle,pixels->rgba.data());check(ready!=UINT32_MAX,"Blend readback unsupported.");
        for(unsigned n=0;n<120;++n){tick();if(picking::frameReady(frame,ready))return *pixels;}throw std::runtime_error("Blend readback timed out.");
    };
    Texture normal{bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(std::array<std::uint8_t,4>{218,128,218,255}.data(),4))};check(bgfx::isValid(normal.handle),"Blend normal texture allocation failed.");
    const auto prepare=[&](const MaterialGraphSourceSet& sources) {
        const auto material=blend(sources);const auto generated=generateMaterialGraphShaderData(material,{},sources);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);MaterialGraphProgramResult result;
        do{tick();result=programs.request(generated);if(!result.current&&!result.compiling&&!result.diagnostic.empty())throw std::runtime_error(result.diagnostic);if(!result.current)std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(!result.current&&std::chrono::steady_clock::now()<deadline);
        check(result.current&&result.programs,"Blend shader publication timed out.");
        auto packet=std::make_shared<PreparedGraphMaterial>();packet->programs=result.programs;packet->uniforms=generated.uniforms;packet->sources=generated.textures;packet->shading=MaterialShading::BlendPbr;packet->blend=MaterialBlend::Opaque;packet->signature=result.programs->signature;packet->receiveFog=false;
        for(const auto& binding:generated.textures){packet->textures[binding.slot]=normal.handle;packet->flags[binding.slot]=BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT;}
        return packet;
    };
    auto plain=quad(false,false),alternate=quad(false,true),skew=quad(true,false),skewAlternate=quad(true,true),tinted=quad(false,false,{.5f,.75f,.25f,1});
    for(const auto* value:{&plain,&alternate,&skew,&skewAlternate,&tinted})check(renderer.prepareModel(*value->model),renderer.modelStatus());
    std::vector<ModelMaterial> surfaces(1);surfaces[0].metallic=0;surfaces[0].roughness=1;
    std::vector<ModelMaterialTextures> bindings(1);bindings[0].graph=prepare(layers(false));
    SceneSettings settings;settings.camera.position={0,0,3};settings.camera.target={};settings.camera.fov=45;settings.camera.fovAxis=CameraFovAxis::Vertical;settings.camera.nearPlane=.1f;settings.camera.farPlane=32;settings.antiAliasing=AntiAliasing::Off;
    settings.environment.enabled=false;settings.environment.toneMapping=ToneMapping::Linear;settings.exposure=1;settings.environment.background.overrideColor=true;settings.environment.background.color={};settings.environment.background.show=false;
    std::vector<RenderModelInstance> instances(1);instances[0].entityId=101;instances[0].model.asset=plain.model;instances[0].materials=&surfaces;instances[0].textures=&bindings;instances[0].shading=MaterialShading::BlendPbr;instances[0].forceFullDetail=true;
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=instances;scene.meshPopulationRevision=1;scene.shadowWorldId=0x424c454e44;
    renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Albedo);renderer.setDebugView(RenderViewPurpose::Scene,RenderDebugView::Albedo);
    const auto assign=[&](const Fixture& fixture,std::shared_ptr<const MeshPaintPayload> payload){instances[0].model.asset=fixture.model;instances[0].meshPaint={{0,0,fixture.geometry,std::move(payload)}};++scene.meshPopulationRevision;};
    const auto capture=[&]{for(unsigned n=0;n<2;++n){renderer.renderGame(scene);tick();}renderer.renderGame(scene);return read(renderer.gameViewportTexture());};
    for(unsigned layer=0;layer<4;++layer){assign(plain,weights(*plain.geometry,layer));const auto image=capture();expect(image,settings.camera,{},Colors[layer],"Material layer endpoint "+std::to_string(layer));save(output,"endpoint-"+std::to_string(layer),image);}
    instances[0].meshPaint.clear();expect(capture(),settings.camera,{},Colors[0],"Unpainted Blend defaults to Base");
    assign(tinted,weights(*tinted.geometry,1));const Vec3 tintedColor{Colors[1][0]*.5f,Colors[1][1]*.75f,Colors[1][2]*.25f};expect(capture(),settings.camera,{},tintedColor,"Imported vertex tint was lost or applied twice");
    const auto bilinear=[&](const Fixture& a,const Fixture& b,bool trapezoid) {
        assign(a,weights(*a.geometry,1,true));const auto first=capture();assign(b,weights(*b.geometry,1,true));const auto second=capture();
        unsigned witnesses{};
        for(unsigned y=40;y<Size-40;y+=7)for(unsigned x=40;x<Size-40;x+=7) {
            const auto ray=settings.camera.screenRay(float(x)+.5f,float(y)+.5f,{0,0,Size,Size});check(bool(ray),"Bilinear probe has no ray.");const auto position=ray->origin+ray->direction*(-ray->origin[2]/ray->direction[2]);
            const float v=(position[1]+.8f)/1.6f,u=(position[0]+.9f)/(1.8f-(trapezoid?.4f*v:0.f));if(u<.1f||u>.9f||v<.1f||v>.9f)continue;
            const float weight=u*v;const auto offset=(std::size_t(y)*Size+x)*4;
            for(unsigned c=0;c<3;++c){const auto expected=encoded(Colors[0][c]*(1-weight)+Colors[1][c]*weight);check(std::abs(int(first.rgba[offset+c])-expected)<=4,"Quad paint did not evaluate bilinearly.");check(std::abs(int(first.rgba[offset+c])-int(second.rgba[offset+c]))<=2,"GPU diagonal changed logical quad paint.");}++witnesses;
        }
        check(witnesses>40,"Bilinear fixture did not cover enough interior witnesses.");save(output,trapezoid?"trapezoid-diagonal-a":"quad-diagonal-a",first);save(output,trapezoid?"trapezoid-diagonal-b":"quad-diagonal-b",second);
    };
    bilinear(plain,alternate,false);bilinear(skew,skewAlternate,true);
    bindings[0].graph=prepare(layers(true));renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Normal);
    assign(plain,weights(*plain.geometry,0));const auto flat=capture();assign(plain,weights(*plain.geometry,1));const auto tilt=capture();
    const auto center=pixel(settings.camera,{});const auto offset=(std::size_t(center[1])*Size+center[0])*4;
    Vec3 flatN{},tiltedN{};for(unsigned c=0;c<3;++c){flatN[c]=2*decoded(flat.rgba[offset+c])-1;tiltedN[c]=2*decoded(tilt.rgba[offset+c])-1;}
    flatN=math::normalized(flatN);tiltedN=math::normalized(tiltedN);check(flatN[2]>.99f&&tiltedN[0]>.6f&&tiltedN[2]>.6f,"Layer normal map was lost during composition.");
    assign(skew,weights(*skew.geometry,1,true));const auto normalA=capture();assign(skewAlternate,weights(*skewAlternate.geometry,1,true));const auto normalB=capture();
    for(unsigned y=55;y<Size-55;y+=9)for(unsigned x=55;x<Size-55;x+=9){const auto at=(std::size_t(y)*Size+x)*4;for(unsigned c=0;c<3;++c)check(std::abs(int(normalA.rgba[at+c])-int(normalB.rgba[at+c]))<=3,"Quad diagonal changed blended normal maps.");}
    save(output,"trapezoid-normal",normalA);
    // Read the actual expanded paint mesh through the same rectangular surface
    // picker used by the viewport brush, including a 1x1 clipped footprint.
    instances[0].resourceRevision=0x5041494e54524full;
    std::uint64_t pickSerial{};
    const auto pickSurface=[&](std::uint32_t x,std::uint32_t y,std::uint32_t width,std::uint32_t height) {
        renderer.render(scene);tick();
        PickRequest request{17,31,++pickSerial,x,y,renderer.pickResourceSignature(PickMode::Surface),PickMode::Surface,width,height};
        check(renderer.requestPick(request),"Native paint surface request was rejected: "+renderer.pickingStatus());renderer.render(scene);
        for(unsigned n=0;n<120;++n) {
            tick();if(auto result=renderer.pollPick(frame)) {
                check(result->generation==17&&result->revision==31&&result->serial==pickSerial,"Surface readback lost request identity.");
                check(result->width==width&&result->height==height&&result->surfaceSamples.size()==std::size_t(width)*height,"Surface readback dimensions or sample count differ from the brush ROI.");
                check(!result->surfacePending,"Prepared derived paint geometry remained pending.");
                return *result;
            }
        }
        throw std::runtime_error("Native paint surface readback timed out: "+renderer.pickingStatus());
    };
    const auto verifySurface=[&](const PickSurfaceSample& sample,unsigned x,unsigned y) {
        check(sample.valid&&sample.entityId==101&&sample.resourceRevision==instances[0].resourceRevision&&sample.drawIndex==0&&sample.primitive==0,"Surface ROI lost exact derived draw identity or revision.");
        const auto ray=settings.camera.screenRay(float(x)+.5f,float(y)+.5f,{0,0,Size,Size});check(bool(ray),"Surface ROI probe has no ray.");
        const auto position=ray->origin+ray->direction*(-ray->origin[2]/ray->direction[2]);
        for(unsigned c=0;c<3;++c)check(std::abs(sample.worldPosition[c]-position[c])<.0002f,"Surface ROI row origin or evaluated world position is incorrect.");
    };
    auto region=pickSurface(80,72,23,17);
    for(unsigned y=0;y<region.height;++y)for(unsigned x=0;x<region.width;++x)verifySurface(region.surfaceSamples[std::size_t(y)*region.width+x],80+x,72+y);
    const auto single=pickSurface(96,96,1,1);verifySurface(single.surfaceSamples[0],96,96);
    const auto mixed=pickSurface(0,0,Size,Size);unsigned visible{},background{};
    for(unsigned y=0;y<Size;++y)for(unsigned x=0;x<Size;++x) {
        const auto ray=settings.camera.screenRay(float(x)+.5f,float(y)+.5f,{0,0,Size,Size});check(bool(ray),"Background ROI probe has no ray.");
        const auto position=ray->origin+ray->direction*(-ray->origin[2]/ray->direction[2]);const auto v=(position[1]+.8f)/1.6f,u=(position[0]+.9f)/(1.8f-.4f*v);
        const auto& sample=mixed.surfaceSamples[std::size_t(y)*Size+x];
        if(u>.03f&&u<.97f&&v>.03f&&v<.97f){verifySurface(sample,x,y);++visible;}
        else if(u<-.03f||u>1.03f||v<-.03f||v>1.03f){check(!sample.valid&&!sample.entityId,"Surface ROI background leaked a receiver identity.");++background;}
    }
    check(visible>1000&&background>1000,"Surface ROI did not exercise both visible geometry and background.");
    // A changed derived asset invalidates a queued result even if the host has
    // not yet consumed it, and cancellation keeps CPU destinations alive.
    renderer.render(scene);tick();
    PickRequest stale{17,31,++pickSerial,96,96,renderer.pickResourceSignature(PickMode::Surface),PickMode::Surface,3,5};
    check(renderer.requestPick(stale),"Stale surface test request failed.");renderer.render(scene);tick();
    assign(plain,weights(*plain.geometry,1,true));renderer.render(scene);
    for(unsigned n=0;n<12;++n){tick();check(!renderer.pollPick(frame),"Changed derived geometry published a stale surface result.");}
    stale.serial=++pickSerial;stale.resourceSignature=renderer.pickResourceSignature(PickMode::Surface);
    check(renderer.requestPick(stale),"Cancelled surface test request failed.");renderer.render(scene);renderer.cancelPicking();
    for(unsigned n=0;n<12;++n){tick();check(!renderer.pollPick(frame),"Cancelled surface result was published.");}
    // Both views share immutable geometry but retain independent uploaded paint.
    bindings[0].graph=prepare(layers(false));renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Albedo);
    instances.resize(2);instances[0].model.asset=plain.model;instances[0].model.transform=math::translation({-.55f,0,0})*math::scaling({.45f,.45f,.45f});instances[0].meshPaint={{0,0,plain.geometry,weights(*plain.geometry,1)}};
    instances[1]=instances[0];instances[1].entityId=202;instances[1].model.transform=math::translation({.55f,0,0})*math::scaling({.45f,.45f,.45f});instances[1].meshPaint[0].weights=weights(*plain.geometry,2);scene.instances=instances;++scene.meshPopulationRevision;
    auto gameInstances=instances;gameInstances[0].meshPaint[0].weights=weights(*plain.geometry,3);RenderScene game=scene;game.instances=gameInstances;++game.meshPopulationRevision;
    renderer.render(scene);renderer.renderGame(game);const auto sceneImage=read(renderer.viewportTexture()),gameImage=read(renderer.gameViewportTexture());
    expect(sceneImage,settings.camera,{-.55f,0,0},Colors[1],"Scene occurrence inherited another paint stream");expect(sceneImage,settings.camera,{.55f,0,0},Colors[2],"Second occurrence inherited first occurrence paint");
    expect(gameImage,settings.camera,{-.55f,0,0},Colors[3],"Game overwrote or reused Scene paint");expect(gameImage,settings.camera,{.55f,0,0},Colors[2],"Game second occurrence lost independent paint");
    save(output,"independent-scene",sceneImage);save(output,"independent-game",gameImage);
    renderer.render(scene);check(renderer.meshInstancingStats(RenderViewPurpose::Scene).instancedInstances==0,"Blend surfaces entered fixed-material instancing.");check(renderer.meshInstancingStats(RenderViewPurpose::Scene).uploadBytes==0,"Unchanged paint uploaded its stream again.");tick();
    scene.instances={};game.instances={};bindings[0].graph.reset();resources.stop();frame=renderer.drainPending(frame);
    std::cout<<"Native "<<renderer.backendName()<<" Blend endpoints/default/tint, quad and trapezoid bilinear interpolation, normal maps, ROI picking, independent occurrences/views and warm stream reuse passed.\n";
}
}
int main(int argc,char** argv) {
    try {
        check(argc==4||argc==5,"Pass shaderc, source/include directories and optional capture directory.");
        const auto temporary=std::filesystem::temp_directory_path()/("crimson-blend-gpu-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ShaderCompileConfig config{argv[1],argv[2],argv[3],temporary};
        check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());
        struct Sdl {~Sdl(){SDL_Quit();}} sdl;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson Blend material regressions",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);check(bool(window),SDL_GetError());
        exercise(window.get(),config,argc==5?std::filesystem::path(argv[4]):std::filesystem::path{});return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
