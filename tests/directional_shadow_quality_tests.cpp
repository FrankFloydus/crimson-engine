#include "crimson-renderer/Renderer.h"
#include "GpuTestBackend.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/MaterialGraphShader.h"
#include "crimson-renderer/Picking.h"
#include "assets/BuiltinGeometry.h"
#include "resources/ResourceManager.h"
#include <SDL3/SDL.h>
#include <bgfx/platform.h>
#include <d3d11.h>
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
namespace fs=std::filesystem;
constexpr std::uint16_t Size=256,ReadView=254;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Sdl {Sdl(){require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~Sdl(){SDL_Quit();}};
struct Texture {bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};~Texture(){if(bgfx::isValid(handle))bgfx::destroy(handle);}};
struct Temporary {
    fs::path path=fs::temp_directory_path()/("crimson-directional-quality-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary(){fs::create_directories(path);}
    ~Temporary(){std::error_code error;const auto resolved=fs::weakly_canonical(path,error),parent=fs::weakly_canonical(fs::temp_directory_path(),error);
        if(!error&&resolved.parent_path()==parent&&resolved.filename().string().starts_with("crimson-directional-quality-"))fs::remove_all(resolved,error);}
};
std::shared_ptr<ModelAsset> floorGeometry(){
    auto mesh=std::make_shared<ModelAsset>();mesh->id="directional-quality-floor";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    constexpr std::array<Vec3,4> positions{{{-18,0,18},{18,0,18},{18,0,-18},{-18,0,-18}}};
    constexpr std::array<Vec2,4> uv{{{0,1},{1,1},{1,0},{0,0}}};
    for(unsigned index=0;index<4;++index){ModelVertex v;v.position=positions[index];v.normal={0,1,0};v.tangent={1,0,0,-1};v.uv0=v.uv1=uv[index];primitive.vertices.push_back(v);math::include(mesh->bounds,v.position);}
    primitive.indices={0,1,2,0,2,3};mesh->primitives.push_back(std::move(primitive));mesh->draws.push_back({0,{}});mesh->materials.emplace_back();return mesh;
}
Camera gameCamera(){
    Camera camera;camera.position={32,24,24};camera.target=camera.position+Vec3{-.612372458f,-.5f,-.612372398f}*48.f;
    camera.fov=15;camera.fovAxis=CameraFovAxis::Vertical;camera.nearPlane=.1f;camera.farPlane=2048;return camera;
}
Camera sceneCamera(){
    Camera camera;camera.position={8.6003008f,10.6881323f,4.6002536f};camera.target={.6423655f,1.6077204f,-1.1630378f};
    camera.fov=45;camera.nearPlane=.01f;camera.farPlane=2048;return camera;
}
std::optional<unsigned> pixel(const Camera& camera,Vec3 world){
    const auto projected=math::project(world,camera.projectionMatrix(1,ClipDepth::ZeroToOne)*camera.viewMatrix(),{0,0,Size,Size},ClipDepth::ZeroToOne);
    if(!projected||(*projected)[0]<3||(*projected)[1]<3||(*projected)[0]>=Size-3||(*projected)[1]>=Size-3)return {};
    return (static_cast<unsigned>((*projected)[1])*Size+static_cast<unsigned>((*projected)[0]))*4;
}
double brightness(const DecodedImage& image,unsigned offset){return (double(image.rgba[offset])+image.rgba[offset+1]+image.rgba[offset+2])/3;}
void save(const DecodedImage& image,const fs::path& directory,const std::string& name){
    if(directory.empty())return;fs::create_directories(directory);std::vector<std::uint8_t> png;
    require(lodepng::encode(png,image.rgba,image.width,image.height)==0,"Cannot encode directional quality evidence.");
    std::ofstream out(directory/(name+".png"),std::ios::binary);out.write(reinterpret_cast<const char*>(png.data()),static_cast<std::streamsize>(png.size()));require(bool(out),"Cannot write directional quality evidence.");
}
void litSamples(const DecodedImage& reference,const DecodedImage& shadowed,const Camera& camera,
    std::span<const Vec3> points,const std::string& label,unsigned minimum){
    unsigned samples{},maximumLoss{},bad{};double lossSum{};
    for(const auto point:points)if(const auto offset=pixel(camera,point)){
        if(brightness(reference,*offset)<25)continue;
        ++samples;for(unsigned channel=0;channel<3;++channel){const auto loss=static_cast<unsigned>(std::max(0,int(reference.rgba[*offset+channel])-int(shadowed.rgba[*offset+channel])));
            maximumLoss=std::max(maximumLoss,loss);lossSum+=loss;bad+=loss>3;}
    }
    const auto meanLoss=samples?lossSum/(samples*3):0;
    std::cout<<label<<" samples="<<samples<<" maximum_loss="<<maximumLoss<<" mean_loss="<<meanLoss<<" bad_channels="<<bad<<std::endl;
    require(samples>=minimum,label+": insufficient visible lit interior samples.");
    require(maximumLoss<=3&&bad==0,label+": directional self-shadow acne: maximum_loss="+std::to_string(maximumLoss)+", mean_loss="+std::to_string(meanLoss));
}
void exercise(SDL_Window* window,ShaderCompileConfig config,const fs::path& capture){
    // Readback storage survives exceptional renderer shutdown.
    std::vector<std::shared_ptr<DecodedImage>> destinations;
    ResourceManager resources;Renderer renderer(window,nullptr,selectedTestGraphicsBackend());MaterialGraphPrograms programs(resources,config);
    renderer.resizeViewport(Size,Size);renderer.resizeGameViewport(Size,Size);renderer.setGridVisible(false);
    bgfx::reset(Size,Size,BGFX_RESET_NONE);TestGpuContext context;
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x4453485155;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.1f,.1f,.1f})},environment.settings);
    require(renderer.publishEnvironment(environment),renderer.environmentStatus());
    std::uint32_t frame{};const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=bgfx::frame();context.flush();};
    const auto read=[&](bgfx::TextureHandle source){
        Texture staging{bgfx::createTexture2D(Size,Size,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        require(bgfx::isValid(staging.handle),"Directional quality readback allocation failed.");
        auto image=std::make_shared<DecodedImage>(DecodedImage{Size,Size,std::vector<std::uint8_t>(Size*Size*4)});destinations.push_back(image);
        bgfx::blit(ReadView,staging.handle,0,0,source,0,0,Size,Size);const auto ready=bgfx::readTexture(staging.handle,image->rgba.data());
        require(ready!=UINT32_MAX,"Directional quality readback is unsupported.");
        for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return *image;}
        throw std::runtime_error("Directional quality readback timed out.");
    };
    const auto floor=floorGeometry();const auto pillar=builtinBoxGeometry();
    require(renderer.prepareModel(*floor)&&renderer.prepareModel(*pillar),renderer.modelStatus());
    std::vector<ModelMaterial> white(1);white[0].baseColor={1,1,1,1};white[0].roughness=1;white[0].metallic=0;white[0].specular=0;
    std::vector<ModelMaterialTextures> bindings(1);
    RenderModelInstance ground;ground.entityId=1;ground.model.asset=floor;ground.materials=&white;ground.textures=&bindings;ground.forceFullDetail=true;
    auto column=ground;column.entityId=2;column.model.asset=pillar;column.model.transform=math::translation({1,2,-3})*math::scaling({1,4,1});
    std::array<RenderModelInstance,2> instances;
    DirectionalLight sun;sun.entityId=100;sun.color={1,1,1};sun.intensity=2;sun.castShadows=true;
    sun.shadowDistance=100;sun.shadowDepthBias=.005f;
    SceneSettings settings;settings.environment.enabled=false;settings.environment.toneMapping=ToneMapping::Linear;
    settings.environment.background.overrideColor=true;settings.environment.background.color={};settings.exposure=1;
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.directionalLights=std::span(&sun,1);scene.shadowWorldId=42;
    std::vector<Vec3> floorPoints;
    for(float z=-12;z<=8;z+=.3f)for(float x=-10;x<=10;x+=.3f)floorPoints.push_back({x,0,z});
    std::vector<Vec3> pillarPoints;
    for(float y=.5f;y<=3.5f;y+=.3f)for(float u=-.2f;u<=.2f;u+=.1f){pillarPoints.push_back({1.5f,y,-3+u});pillarPoints.push_back({1+u,y,-2.5f});}
    const auto render=[&](bool game,bool includePillar,bool receiving){
        ground.receiveShadows=column.receiveShadows=receiving;instances={ground,column};scene.instances=std::span(instances).first(includePillar?2:1);scene.shadowInstances=scene.instances;
        if(game)renderer.renderGame(scene);else renderer.render(scene);
        require(renderer.shadowStatus().empty(),renderer.shadowStatus());return read(game?renderer.gameViewportTexture():renderer.viewportTexture());
    };
    const std::array<Vec3,3> directions{{{-.354550123f,-.611820817f,-.707084596f},{-.7f,-.2f,-.6f},{-.55f,-.75f,-.4f}}};
    for(bool generated:{false,true}){
        if(generated){
            MaterialAsset material;addMaterialNode(material.graph,MaterialNodeKind::Output);auto& output=materialOutputNode(material.graph).output;
            output.baseColor={1,1,1};output.roughness=1;output.metallic=0;output.specular=0;
            const auto shader=generateMaterialGraphShader(material);MaterialGraphProgramResult result;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{45};
            do{tick();result=programs.request(shader);if(!result.current)std::this_thread::sleep_for(std::chrono::milliseconds{1});}
            while(!result.current&&std::chrono::steady_clock::now()<deadline);
            require(result.current&&bool(result.programs),"Directional quality generated material failed: "+result.diagnostic);
            auto graph=std::make_shared<PreparedGraphMaterial>();graph->programs=result.programs;graph->uniforms=shader.uniforms;graph->sources=shader.textures;graph->signature=result.programs->signature;bindings[0].graph=std::move(graph);
        }
        for(const unsigned resolution:{256u,1024u})for(const float normalBias:{1.f,0.f})for(unsigned direction=0;direction<directions.size();++direction)for(bool game:{false,true}){
            settings.camera=game?gameCamera():sceneCamera();sun.direction=math::normalized(directions[direction]);sun.shadowNormalBias=normalBias;scene.shadowResolution=static_cast<std::uint16_t>(resolution);
            const auto label=std::string(generated?"graph-":"fallback-")+(game?"game-":"scene-")+std::to_string(resolution)+"-bias"+std::to_string(int(normalBias))+"-sun"+std::to_string(direction);
            const auto floorClear=render(game,false,false),floorShadow=render(game,false,true);
            save(floorClear,capture,label+"-floor-reference");save(floorShadow,capture,label+"-floor-shadowed");
            litSamples(floorClear,floorShadow,settings.camera,floorPoints,label+" floor",100);
            const auto pillarClear=render(game,true,false),pillarShadow=render(game,true,true);
            save(pillarClear,capture,label+"-pillar-reference");save(pillarShadow,capture,label+"-pillar-shadowed");
            litSamples(pillarClear,pillarShadow,settings.camera,pillarPoints,label+" pillar",20);
        }
    }
    // A real near-contact caster must stay occluding after acne correction.
    bindings[0].graph.reset();sun.direction=math::normalized(Vec3{1,-2,.3f});
    for(const unsigned resolution:{256u,1024u})for(const float normalBias:{1.f,0.f})for(bool game:{false,true}){
        scene.shadowResolution=static_cast<std::uint16_t>(resolution);sun.shadowNormalBias=normalBias;settings.camera=game?gameCamera():sceneCamera();
        const auto clear=render(game,true,false),shadow=render(game,true,true);unsigned visible{},occluded{};
        for(float z=-3.2f;z<=-2.8f;z+=.1f)if(const auto offset=pixel(settings.camera,{1.65f,0,z})){
            const auto reference=brightness(clear,*offset);if(reference<30)continue;++visible;
            if(brightness(shadow,*offset)<reference*.75)++occluded;
        }
        require(visible>=3&&occluded>=3,"Directional receiver correction detached the near-contact pillar shadow.");
    }
    bindings[0].graph.reset();resources.stop();renderer.drainPending(frame);
}
}
int main(int argc,char** argv){try{
    require(argc==4||(argc==6&&std::string(argv[4])=="--capture"),"Pass shaderc, source, include directories; optionally --capture output-directory.");
    Temporary temporary;ShaderCompileConfig config{argv[1],argv[2],argv[3],temporary.path};Sdl sdl;
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson directional shadow quality checks",Size,Size,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
    require(bool(window),SDL_GetError());exercise(window.get(),config,argc==6?fs::path(argv[5]):fs::path{});
    std::cout<<"Directional Scene/Game oblique/grazing floor/pillar acne, generated/fallback materials and near-contact checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
