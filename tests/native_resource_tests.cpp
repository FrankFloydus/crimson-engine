#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "resources/TextureStreamer.h"
#include "resources/ShaderReloadSession.h"
#include "assets/AtomicFile.h"
#include "assets/MaterialAsset.h"
#include "assets/MaterialPreviewGeometry.h"
#include <SDL3/SDL.h>
#include <lodepng.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
void check(bool success,const std::string& text) {if(!success)throw std::runtime_error(text);}
void cleanup(const std::filesystem::path& root) {
    const auto resolved=std::filesystem::weakly_canonical(root),temporary=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    if(resolved.parent_path()!=temporary || !resolved.filename().string().starts_with("crimson-native-resources-"))throw std::runtime_error("Native test cleanup target escaped its disposable directory.");
    std::filesystem::remove_all(resolved);
}
struct SdlOwner {SdlOwner(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~SdlOwner(){SDL_Quit();}};
struct ImGuiOwner {ImGuiOwner(){ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;}~ImGuiOwner(){ImGui::DestroyContext();}};
void createImage(const std::filesystem::path& file) {
    constexpr unsigned width=256,height=256;std::vector<std::uint8_t> pixels(width*height*4);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const auto offset=(y*width+x)*4;pixels[offset]=180;pixels[offset+1]=70;pixels[offset+2]=40;pixels[offset+3]=x<width/2 ? 0:255;}
    std::vector<std::uint8_t> encoded;check(lodepng::encode(encoded,pixels,width,height)==0,"Could not encode native fixture image.");
    writeFileAtomically(file,{reinterpret_cast<const char*>(encoded.data()),encoded.size()});
}
PreparedEnvironment environment() {
    PreparedEnvironment result;result.settings={2,8,8,16,16,32};result.key=12345;result.status="Native test constant environment.";
    ibl::Image source{4,2,std::vector<Vec3>(8,Vec3{.25f,.25f,.25f})};result.data=ibl::bake(source,result.settings);return result;
}
std::shared_ptr<ModelAsset> quad() {
    auto asset=std::make_shared<ModelAsset>();asset->id="native-model";asset->name="Native resource fixture";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    const std::array<Vec3,4> positions{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
    const std::array<Vec2,4> coordinates{{{0,1},{1,1},{1,0},{0,0}}};
    for(std::size_t i=0;i<4;++i) {
        ModelVertex vertex;vertex.position=positions[i];vertex.normal={0,0,1};vertex.uv0=vertex.uv1=coordinates[i];primitive.vertices.push_back(vertex);math::include(asset->bounds,vertex.position);
    }
    primitive.indices={0,1,2,0,2,3};asset->primitives.push_back(std::move(primitive));asset->draws.push_back({0,{}});
    ModelMaterial material;material.metallic=0;material.roughness=1;asset->materials.push_back(material);return asset;
}
double component(const DecodedImage& image,unsigned x,unsigned y,unsigned channel) {return image.rgba[(static_cast<std::size_t>(y)*image.width+x)*4+channel];}
void exercise(const std::filesystem::path& root,const ShaderCompileConfig& shaderConfig,SDL_Window* window) {
    ResourceManager resources;Renderer renderer(window);ImGuiOwner context;ImGuiBgfx tools;
    renderer.resize(320,240);renderer.resizeViewport(128,128);
    const auto lighting=environment();check(renderer.publishEnvironment(lighting),renderer.environmentStatus());check(renderer.publishThumbnailEnvironment(lighting),renderer.thumbnailStatus());
    TextureStreamer textures(resources,root);const auto source=root/"source.png";createImage(source);
    std::uint32_t frame{};
    const auto tick=[&] {resources.pump();textures.advance(8192,2);renderer.advanceStreaming(8192,2);frame=bgfx::frame();std::this_thread::sleep_for(std::chrono::milliseconds{1});};
    PreparedTexture color,normal;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{20};
    do {
        textures.beginFrame();color=textures.request("native-image",source,streaming::TextureRole::Color,false,256,ResourcePriority::Selected);
        normal=textures.request(NeutralNormalTextureId,{},streaming::TextureRole::Normal,false,1,ResourcePriority::Selected);
        tick();
        if(!color.diagnostic.empty())throw std::runtime_error(color.diagnostic);if(!normal.diagnostic.empty())throw std::runtime_error(normal.diagnostic);
    }while((!color.complete || !normal.complete) && std::chrono::steady_clock::now()<deadline);
    check(color.complete && normal.complete,"Native texture streaming did not publish complete resources.");
    auto asset=quad();auto descriptor=streaming::buildModelCache(*asset,root);renderer.configureModelStream(asset->id,1,descriptor);
    check(renderer.queueModelLod(asset->id,1,streaming::loadModelLod(descriptor.primitives[0].levels[0])),"Native mesh streaming queue failed.");
    for(unsigned i=0;i<100 && !renderer.modelStreamReady(asset->id,1);++i)tick();
    check(renderer.modelStreamReady(asset->id,1),"Native mesh buffers were not published.");
    SceneSettings settings;settings.camera.position={0,0,3};settings.camera.target={0,0,0};settings.environment.enabled=false;
    DirectionalLight light;light.direction={0,0,-1};light.color={1,1,1};light.intensity=3;
    std::array<DirectionalLight,1> lights{light};std::vector<ModelMaterialTextures> bindings(1);
    bindings[0].handles[0]=color.handle;bindings[0].leases[0]=color.lease;bindings[0].flags[0]=BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT;
    std::vector<ModelMaterial> materials=asset->materials;
    RenderModelInstance instance{77,{asset,{}},&bindings,1,true,true,&materials};
    RenderScene scene{settings,{},nullptr,nullptr,std::span(&instance,1),lights,{},true};
    scene.pinned=std::make_shared<std::vector<ModelMaterialTextures>>(bindings);
    const auto capture=[&](const char* name) {
        check(renderer.renderThumbnail(name,1,scene,128,scene.pinned),"Native thumbnail request failed: "+renderer.thumbnailStatus());
        const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds{10};
        do {tick();if(auto result=renderer.pollThumbnail(frame))return result->image;}while(std::chrono::steady_clock::now()<limit);
        throw std::runtime_error("Native offscreen readback did not complete.");
    };
    const auto baseline=capture("opaque");check(component(baseline,64,64,0)>30,"Native model thumbnail did not contain visible lit geometry.");
    bindings[0].handles[2]=normal.handle;bindings[0].leases[2]=normal.lease;bindings[0].flags[2]=0;materials[0].normalTexture.image=AssetRef<DecodedImage>{NeutralNormalTextureId};
    scene.pinned=std::make_shared<std::vector<ModelMaterialTextures>>(bindings);
    const auto flat=capture("flat-normal");
    for(unsigned channel=0;channel<3;++channel)check(std::abs(component(flat,64,64,channel)-component(baseline,64,64,channel))<=1,"A neutral normal map changed native model lighting.");
    instance.model.transform=math::scaling({-1.1f,.8f,1.2f});
    const auto mirrored=capture("mirrored");
    for(unsigned channel=0;channel<3;++channel)check(std::abs(component(mirrored,64,64,channel)-component(flat,64,64,channel))<=2,"Mirrored/nonuniform model transforms changed neutral front-face lighting.");
    instance.model.transform={};materials[0].alphaMask=true;
    const auto mask=capture("cutout");check(component(mask,32,64,0)<30 && component(mask,96,64,0)>30,"Native cutout material did not discard its transparent region.");
    const auto pick=[&](unsigned x,std::uint64_t serial) {
        check(renderer.requestPick({1,1,serial,x,64,renderer.streamingSignature()}),renderer.pickingStatus());renderer.render(scene);
        for(unsigned i=0;i<50;++i){tick();if(auto result=renderer.pollPick(frame))return result->entityId;}
        throw std::runtime_error("Native entity picking readback did not complete.");
    };
    check(pick(32,1)==0 && pick(96,2)==77,"Native ID picking must match material cutout/depth coverage.");
    const auto sphere=materialPreviewGeometry(); const auto priorBytes=renderer.modelResidentBytes();
    const auto priorSignature=renderer.streamingSignature();
    renderer.configureModelStream(sphere->id,1,*sphere->stream);
    check(renderer.queueModelLod(sphere->id,1,materialPreviewGeometryLevel()),"Material preview sphere GPU queue failed.");
    for(unsigned i=0;i<100 && !renderer.modelStreamReady(sphere->id,1);++i)tick();
    check(renderer.modelStreamReady(sphere->id,1) && renderer.modelResidentBytes()>priorBytes,"Shared material sphere must publish through tracked GPU resources.");
    check(renderer.streamingSignature()==priorSignature,"Offscreen material geometry must not invalidate main-view picking.");
    const auto sphereBytes=renderer.modelResidentBytes();
    check(renderer.queueModelLod(sphere->id,1,materialPreviewGeometryLevel()) && renderer.modelResidentBytes()==sphereBytes,"Material thumbnails must share a single GPU sphere allocation.");
    const std::array<DirectionalLight,2> previewLights{{DirectionalLight{{-1,-1,-1},{1,1,1},1,true},DirectionalLight{{0,1,0},{.7f,.7f,.7f},1,true}}};
    scene.directionalLights=previewLights; settings.environment.enabled=true;
    settings.camera.position={0,0,3};settings.camera.target={0,0,0};settings.camera.up={0,1,0};
    settings.camera.fov=45;settings.camera.nearPlane=.1f;settings.camera.farPlane=10;
    instance.model={sphere,Mat4{}};instance.pickable=false;materials=sphere->materials;bindings={ModelMaterialTextures{}};
    materials[0].baseColor={.6f,.05f,.02f,1};
    scene.pinned=std::make_shared<std::vector<ModelMaterialTextures>>(bindings);
    const auto materialRed=capture("material-red");
    check(component(materialRed,64,64,0)>component(materialRed,64,64,2)+30,"Material sphere preview must display the assigned linear surface factors.");
    check(component(materialRed,0,0,0)<30 && component(materialRed,64,64,0)>30,"Material preview must frame a visible sphere on its fixed background.");
    bindings[0].handles[2]=normal.handle;bindings[0].leases[2]=normal.lease;bindings[0].flags[2]=0;
    materials[0].normalTexture.image=AssetRef<DecodedImage>{NeutralNormalTextureId};materials[0].normalTexture.texCoord=1;
    scene.pinned=std::make_shared<std::vector<ModelMaterialTextures>>(bindings);
    const auto materialNormal=capture("material-uv1-normal");
    for(unsigned channel=0;channel<3;++channel)check(std::abs(component(materialNormal,64,64,channel)-component(materialRed,64,64,channel))<=1,"Material sphere UV1 normal maps must use a correct tangent basis.");
    materials[0].baseColor={.02f,.6f,.05f,1};
    const auto materialGreen=capture("material-green");
    check(component(materialGreen,64,64,1)>component(materialGreen,64,64,0)+30 && materialGreen.rgba!=materialRed.rgba,"Material previews must use individual surfaces without reallocating sphere geometry.");
    check(renderer.modelResidentBytes()==sphereBytes,"Multiple material captures must retain shared geometry accounting.");
    ShaderReloadSession shaders(resources,renderer,tools,shaderConfig);
    const auto shaderDeadline=std::chrono::steady_clock::now()+std::chrono::seconds{30};
    do {tick();shaders.pump();}while(shaders.pending() && std::chrono::steady_clock::now()<shaderDeadline);
    check(shaders.revision()==1,"Valid native shader candidates did not publish: "+shaders.status());
    const auto healthy=renderer.shaderSignature();const auto healthyPixels=capture("valid-reload");
    const auto bad=shaderConfig.sourceDirectory/"fs_model.sc";std::ifstream input(bad);std::string text(std::istreambuf_iterator<char>{input},{});input.close();
    text+="\nTHIS_IS_NOT_VALID_SHADER_CODE\n";writeFileAtomically(bad,text);const std::array<std::filesystem::path,1> changed{bad};shaders.changed(changed);
    const auto failureDeadline=std::chrono::steady_clock::now()+std::chrono::seconds{30};
    do {tick();shaders.pump();}while(shaders.pending() && std::chrono::steady_clock::now()<failureDeadline);
    check(!shaders.pending() && shaders.revision()==1 && renderer.shaderSignature()==healthy,"Failed shader reload changed published native programs.");
    const auto retained=capture("failed-reload-retained");check(retained.rgba==healthyPixels.rgba,"Failed shader reload changed the rendered native image.");
    check(renderer.setThumbnail("cached",1,retained) && bgfx::isValid(renderer.thumbnailTexture("cached",1)),"Native thumbnail GPU cache publication failed.");
    renderer.evictThumbnail("cached");renderer.evictModel(asset->id);renderer.evictModel(sphere->id);resources.stop();frame=renderer.drainPending(frame);(void)frame;
}
}
int main(int argc,char** argv) {
    if(argc!=4){std::cerr<<"Pass shaderc, source and include directories.\n";return 1;}
    const auto root=std::filesystem::temp_directory_path()/("crimson-native-resources-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(root);
        ShaderCompileConfig config{std::filesystem::path(argv[1]),root/"shaders",root/"includes",root/"cache"};
        std::filesystem::copy(std::filesystem::path(argv[2]),config.sourceDirectory,std::filesystem::copy_options::recursive);
        std::filesystem::copy(std::filesystem::path(argv[3]),config.includeDirectory,std::filesystem::copy_options::recursive);
        SdlOwner sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson native resource checks",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(!!window,SDL_GetError());exercise(root,config,window.get());window.reset();cleanup(root);
        std::cout<<"Hidden native DX11 streaming/shader/normal/cutout/material-thumbnail checks passed.\n";return 0;
    }catch(const std::exception& exception){std::error_code ignored;if(std::filesystem::exists(root,ignored))try{cleanup(root);}catch(...){}std::cerr<<exception.what()<<'\n';return 1;}
}
