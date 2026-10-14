#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "crimson-renderer/Picking.h"
#include "crimson-renderer/PointShadowMath.h"
#include "resources/ShaderReloadSession.h"
#include "assets/AtomicFile.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
constexpr std::uint16_t ImageSize=128,ReadbackView=254;
void check(bool condition,const std::string& message) {if (!condition) throw std::runtime_error(message);}
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("crimson-point-shadows-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp(){std::filesystem::create_directories(path);}
    ~Temp(){std::error_code error;const auto target=std::filesystem::weakly_canonical(path,error);
        const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if (!error && target.parent_path()==parent && target.filename().string().starts_with("crimson-point-shadows-")) std::filesystem::remove_all(target,error);}
};
struct SdlOwner {SdlOwner(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~SdlOwner(){SDL_Quit();}};
struct ImGuiOwner {ImGuiOwner(){ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;}~ImGuiOwner(){ImGui::DestroyContext();}};
struct TextureOwner {bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};~TextureOwner(){if(bgfx::isValid(handle))bgfx::destroy(handle);}};
std::shared_ptr<ModelAsset> quad() {
    auto asset=std::make_shared<ModelAsset>();asset->id="point-shadow-test-quad";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    constexpr std::array<Vec3,4> positions{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
    constexpr std::array<Vec2,4> coordinates{{{0,1},{1,1},{1,0},{0,0}}};
    for (unsigned i=0;i<4;++i) {ModelVertex vertex;vertex.position=positions[i];vertex.normal={0,0,1};
        vertex.uv0=coordinates[i];vertex.uv1={1-coordinates[i][0],coordinates[i][1]};primitive.vertices.push_back(vertex);math::include(asset->bounds,vertex.position);}
    primitive.indices={0,1,2,0,2,3};asset->primitives.push_back(std::move(primitive));asset->draws.push_back({0,{}});
    ModelMaterial material;material.metallic=0;material.roughness=1;asset->materials.push_back(material);return asset;
}
Mat4 plane(const Camera& camera,const Vec3& position,float extent) {
    const auto right=camera.right()*extent,up=math::cross(camera.right(),camera.forward())*extent,normal=-camera.forward();
    return {right[0],right[1],right[2],0,up[0],up[1],up[2],0,normal[0],normal[1],normal[2],0,position[0],position[1],position[2],1};
}
double brightness(const DecodedImage& image,const Camera& camera,const Vec3& point) {
    const auto pixel=math::project(point,camera.projectionMatrix(1,ClipDepth::ZeroToOne)*camera.viewMatrix(),{0,0,ImageSize,ImageSize},ClipDepth::ZeroToOne);
    check(bool(pixel),"Fixture sample is not projectable.");
    const int x=static_cast<int>((*pixel)[0]),y=static_cast<int>((*pixel)[1]);
    check(x>1 && x<ImageSize-2 && y>1 && y<ImageSize-2,"Fixture sample escaped its image.");
    double sum=0;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)for(unsigned channel=0;channel<3;++channel)
        sum+=image.rgba[(std::size_t(y+dy)*image.width+std::size_t(x+dx))*4+channel];
    return sum/27;
}
void exercise(SDL_Window* window,const ShaderCompileConfig& config) {
    // Destinations outlive Renderer even if a readback times out.
    std::vector<std::shared_ptr<DecodedImage>> destinations;
    ResourceManager resources;Renderer renderer(window);ImGuiOwner context;ImGuiBgfx tools;
    renderer.resize(320,240);renderer.resizeViewport(ImageSize,ImageSize);renderer.resizeGameViewport(ImageSize,ImageSize);
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=887;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.1f,.1f,.1f})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    check(renderer.publishThumbnailEnvironment(environment),renderer.thumbnailStatus());
    std::uint32_t frame{};
    const auto tick=[&] {resources.pump();renderer.advanceStreaming();frame=bgfx::frame();tools.postFrame(frame);std::this_thread::sleep_for(std::chrono::milliseconds{1});};
    const auto read=[&](bgfx::TextureHandle source) {
        TextureOwner staging{bgfx::createTexture2D(ImageSize,ImageSize,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.handle),"Shadow fixture readback allocation failed.");
        auto image=std::make_shared<DecodedImage>(DecodedImage{ImageSize,ImageSize,std::vector<std::uint8_t>(ImageSize*ImageSize*4)});
        destinations.push_back(image);bgfx::setViewMode(ReadbackView,bgfx::ViewMode::Sequential);
        bgfx::blit(ReadbackView,staging.handle,0,0,source,0,0,ImageSize,ImageSize);
        const auto ready=bgfx::readTexture(staging.handle,image->rgba.data());check(ready!=UINT32_MAX,"Shadow fixture readback unsupported.");
        for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return *image;}
        throw std::runtime_error("Shadow fixture readback timed out.");
    };
    const auto mesh=quad();check(renderer.prepareModel(*mesh),renderer.modelStatus());
    std::vector<ModelMaterial> receiverMaterials=mesh->materials,casterMaterials=mesh->materials;
    std::vector<ModelMaterialTextures> casterTextures(1);
    RenderModelInstance receiver;receiver.entityId=1;receiver.model.asset=mesh;receiver.materials=&receiverMaterials;receiver.forceFullDetail=true;
    RenderModelInstance caster;caster.entityId=2;caster.model.asset=mesh;caster.materials=&casterMaterials;caster.textures=&casterTextures;caster.forceFullDetail=true;
    std::array<RenderModelInstance,2> shadowInstances;
    std::array<PointLight,16> lights;
    for(unsigned i=0;i<lights.size();++i){lights[i].entityId=100+i;lights[i].position={};lights[i].color={1,1,1};
        lights[i].radius=20;lights[i].intensity=100;lights[i].castShadows=true;}
    SceneSettings settings;settings.environment.enabled=false;settings.exposure=1;
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=std::span(&receiver,1);scene.pointLights=std::span(lights.data(),1);
    scene.shadowWorldId=1;scene.shadowResolution=256;
    Vec3 center{},right{};PointShadowStats submittedStats;
    const auto orient=[&](unsigned face) {
        settings.camera=point_shadow::faceCamera({},20,face,256);const auto direction=settings.camera.forward();
        center=direction*4;settings.camera.position=direction*-2;settings.camera.target=center;settings.camera.fov=45;
        right=settings.camera.right();receiver.model.transform=plane(settings.camera,center,3);
        caster.model.transform=plane(settings.camera,direction*2,.5f);
    };
    const auto capture=[&] {
        shadowInstances={receiver,caster};scene.shadowInstances=shadowInstances;
        renderer.renderGame(scene);check(renderer.shadowStatus().empty(),renderer.shadowStatus());
        submittedStats=renderer.shadowStats(); // advanceStreaming resets per-frame counters during readback ticks.
        return read(renderer.gameViewportTexture());
    };
    for (unsigned face=0;face<6;++face) {
        orient(face);lights[0].castShadows=false;const auto clear=capture();lights[0].castShadows=true;const auto shadow=capture();
        const auto lit=brightness(clear,settings.camera,center),dark=brightness(shadow,settings.camera,center);
        check(lit>80 && dark<lit*.35,"A cube face failed to cast an opaque shadow: "+std::to_string(face));
        const auto outside=center+right*1.6;
        check(std::abs(brightness(clear,settings.camera,outside)-brightness(shadow,settings.camera,outside))<8,"Shadow leaked outside caster bounds or onto another tile.");
    }
    // Render receivers straddling exact cube-face edges/corners and compare
    // neighboring samples on both sides, including the guarded PCF footprint.
    for (const Vec3 diagonal:{Vec3{1,0,1},Vec3{1,1,1},Vec3{0,1,1}}) {
        const auto direction=math::normalized(diagonal);center=direction*4;
        settings.camera.position=direction*-2;settings.camera.target=center;settings.camera.up={0,1,0};settings.camera.fov=45;
        right=settings.camera.right();receiver.model.transform=plane(settings.camera,center,3);caster.model.transform=plane(settings.camera,direction*2,.5f);
        lights[0].castShadows=false;const auto clear=capture();lights[0].castShadows=true;const auto shadow=capture();
        for (float offset:{-.08f,0.f,.08f}) {
            const auto point=center+right*offset;
            check(brightness(shadow,settings.camera,point)<brightness(clear,settings.camera,point)*.35,"Cube seam or corner leaked light through an opaque caster.");
        }
    }
    orient(5);const auto opaque=capture();const auto dark=brightness(opaque,settings.camera,center);
    const auto smallBytes=point_shadow::atlasLayout(256,1).bytes(),defaultBytes=point_shadow::atlasLayout(1024,1).bytes();
    scene.shadowResolution=1024;const auto defaultResolution=capture();
    check(submittedStats.resolution==1024 && submittedStats.renderedFaces==6 && submittedStats.allocatedBytes>=defaultBytes+smallBytes,
        "Default resolution switch failed to recreate all faces or count the retired small atlas.");
    check(brightness(defaultResolution,settings.camera,center)<35,"Default 1024 resolution lost the shadow.");
    scene.shadowResolution=256;capture();
    check(submittedStats.resolution==256 && submittedStats.renderedFaces==6 && submittedStats.allocatedBytes>=defaultBytes+smallBytes,
        "Returning to 256 failed to invalidate faces or retain old-atlas accounting.");
    for(unsigned i=0;i<3;++i)tick();
    check(renderer.shadowResidentBytes()==smallBytes,"Retired shadow allocations did not drain after their frame fence.");
    capture();check(submittedStats.renderedFaces==0 && submittedStats.cacheHits==1,"Static shadow frame did not reuse its six cached faces.");
    const auto cached=[&](const char* reason){capture();check(submittedStats.renderedFaces==0 && submittedStats.cacheHits==1,reason);};
    receiverMaterials[0].baseColor[0]=.7f;cached("Receiver RGB edit invalidated shadow depth.");receiverMaterials[0].baseColor[0]=1;
    lights[0].color={.8f,.9f,1};cached("Light color edit invalidated shadow depth.");lights[0].color={1,1,1};
    lights[0].intensity=110;cached("Light intensity edit invalidated shadow depth.");lights[0].intensity=100;
    settings.exposure=2;cached("Exposure edit invalidated shadow depth.");settings.exposure=1;
    settings.camera.position=settings.camera.position+right*.1;cached("Camera movement invalidated shadow depth.");orient(5);
    const auto streamingSignature=renderer.streamingSignature();
    lights[0].position=right*.1;capture();check(submittedStats.renderedFaces==6,"Light movement did not invalidate all six faces.");
    check(renderer.streamingSignature()==streamingSignature,"Light shadow update changed the Scene picking resource signature.");lights[0].position={};capture();
    caster.model.transform=math::translation(right*.1)*caster.model.transform;capture();
    check(submittedStats.renderedFaces==6,"Caster movement did not invalidate all six faces.");
    check(renderer.streamingSignature()==streamingSignature,"Shadow-only caster pose changed the Scene picking resource signature.");orient(5);capture();
    check(renderer.requestPick({1,1,1,ImageSize/2,ImageSize/2,renderer.streamingSignature()}),renderer.pickingStatus());
    shadowInstances={receiver,caster};scene.shadowInstances=shadowInstances;renderer.render(scene);tick();
    const auto queuedSignature=renderer.streamingSignature();
    lights[0].position=right*.1;caster.model.transform=math::translation(right*.1)*caster.model.transform;capture();
    std::optional<PickResult> picked;
    for(unsigned i=0;i<120 && !picked;++i){picked=renderer.pollPick(frame);if(!picked)tick();}
    check(picked && picked->entityId==receiver.entityId && picked->resourceSignature==queuedSignature && renderer.streamingSignature()==queuedSignature,
        "Shadow-only light/caster updates invalidated or corrupted a queued Scene pick.");
    lights[0].position={};orient(5);capture();
    caster.castShadows=false;const auto noCast=capture();check(brightness(noCast,settings.camera,center)>dark+60,"Cast Shadows disabled retained a stale tile.");
    caster.castShadows=true;receiver.receiveShadows=false;const auto noReceive=capture();
    check(brightness(noReceive,settings.camera,center)>dark+60,"Receive Shadows disabled still sampled occlusion.");receiver.receiveShadows=true;
    caster.blend=MaterialBlend::Alpha;const auto blend=capture();check(brightness(blend,settings.camera,center)>dark+60,"Alpha-blended material unexpectedly cast an opaque shadow.");caster.blend=MaterialBlend::Opaque;
    caster.model.transform=math::translation(right*3)*caster.model.transform;const auto moved=capture();
    check(brightness(moved,settings.camera,center)>dark+60,"Moving a caster did not invalidate its shadow.");orient(5);
    caster.model.transform=caster.model.transform*math::scaling({-1,1,-1});const auto backface=capture();
    check(brightness(backface,settings.camera,center)>dark+60,"Single-sided backface unexpectedly cast a shadow.");
    casterMaterials[0].doubleSided=true;const auto doubleSided=capture();
    check(brightness(doubleSided,settings.camera,center)<dark+10,"Double-sided backface failed to cast a shadow.");casterMaterials[0].doubleSided=false;orient(5);
    receiverMaterials[0].emissive={.4f,.4f,.4f};settings.environment.enabled=true;lights[0].intensity=0;
    const auto indirectShadow=capture();lights[0].castShadows=false;const auto indirectClear=capture();
    check(brightness(indirectShadow,settings.camera,center)>40 &&
        std::abs(brightness(indirectShadow,settings.camera,center)-brightness(indirectClear,settings.camera,center))<=1,"Point shadows changed IBL or emissive lighting.");
    receiverMaterials[0].emissive={};settings.environment.enabled=false;lights[0].intensity=100;lights[0].castShadows=true;
    // A masked caster uses the same alpha UVs/threshold as the color pass.
    constexpr std::array<std::uint8_t,8> alphaPixels{{255,255,255,0,255,255,255,255}};
    TextureOwner alpha{bgfx::createTexture2D(2,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(alphaPixels.data(),static_cast<std::uint32_t>(alphaPixels.size())))};
    check(bgfx::isValid(alpha.handle),"Mask fixture texture allocation failed.");casterTextures[0].handles[0]=alpha.handle;
    casterTextures[0].flags[0]=BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP;
    casterMaterials[0].alphaMask=true;casterMaterials[0].baseColorTexture.texCoord=1;
    const auto masked=capture();check(brightness(masked,settings.camera,center+right*.5)>brightness(masked,settings.camera,center-right*.5)+60,"Masked caster did not preserve its UV1 transparent silhouette.");
    caster.model.transform=caster.model.transform*math::scaling({-1,1,1});const auto mirrored=capture();
    check(brightness(mirrored,settings.camera,center-right*.5)>brightness(mirrored,settings.camera,center+right*.5)+60,"Mirrored caster lost alpha/culling parity.");
    casterMaterials[0].baseColor[3]=.25f;const auto factor=capture();
    check(brightness(factor,settings.camera,center)>dark+60,"Caster alpha factor was ignored by the mask threshold.");casterMaterials[0].baseColor[3]=1;
    casterTextures[0].handles[0]=BGFX_INVALID_HANDLE;casterMaterials[0].alphaMask=false;orient(5);
    // All sixteen slots must occlude their contribution, not merely a selected subset.
    scene.pointLights=lights;for(auto& light:lights)light.intensity=100.f/16;
    const auto all=capture();check(submittedStats.activeLights==16 && submittedStats.renderedFaces==96,"The full sixteen-light shadow budget was not rendered.");
    check(brightness(all,settings.camera,center)<35,"One or more of the sixteen point lights remained unshadowed.");
    check(renderer.shadowResidentBytes()>=point_shadow::atlasLayout(256,16).bytes(),"Atlas accounting omitted live shadow storage.");
    // A separate saved-scene thumbnail is interleaved between Scene and Game.
    // It has no blocker and must neither overwrite nor invalidate active shadows.
    shadowInstances={receiver,caster};scene.shadowInstances=shadowInstances;RenderScene thumbnail=scene;
    thumbnail.shadowWorldId=77;thumbnail.shadowInstances=std::span(&receiver,1);
    renderer.render(scene);check(renderer.renderThumbnail("isolated-point-shadows",1,thumbnail,ImageSize),renderer.thumbnailStatus());renderer.renderGame(scene);
    check(renderer.shadowStats().renderedFaces==0 && renderer.shadowStats().cacheHits==16 && renderer.shadowStats().worldId==scene.shadowWorldId,
        "Scene, thumbnail and Game failed to retain the cached active-world shadow set.");
    const auto dual=read(renderer.gameViewportTexture());check(brightness(dual,settings.camera,center)<35,"Scene/Game sharing corrupted the Game shadows.");
    std::optional<ThumbnailResult> thumbnailImage;
    for(unsigned i=0;i<120 && !thumbnailImage;++i){thumbnailImage=renderer.pollThumbnail(frame);if(!thumbnailImage)tick();}
    check(thumbnailImage && brightness(thumbnailImage->image,settings.camera,center)>brightness(dual,settings.camera,center)+60,
        "Saved-scene thumbnail sampled the active-world blocker instead of its own shadow context.");
    // A broad floor crosses the downward cube face into the four side faces.
    // With no blocker, toggling either light's shadows must not draw a square
    // (or self-shadow stripes) along those face boundaries.
    scene.pointLights=std::span(lights.data(),2);caster.castShadows=false;
    settings.camera.position={10,14,10};settings.camera.target={};settings.camera.up={0,1,0};settings.camera.fov=65;
    const auto floorCamera=point_shadow::faceCamera({},20,3,256);
    lights[0].position={0,2,0};lights[1].position={2,3,1};
    lights[0].intensity=lights[1].intensity=100;
    for (const std::uint16_t resolution:std::array<std::uint16_t,2>{256,1024})
        for (const float slope:{0.f,.3f,-.3f}) for (const bool reflected:{false,true}) {
        const auto tilt=math::rotation(math::axisAngle({0,0,1},slope));
        receiver.model.transform=tilt*plane(floorCamera,{},12);
        if (reflected) receiver.model.transform=receiver.model.transform*math::scaling({-1,1,1});
        scene.shadowResolution=resolution;
        lights[0].castShadows=lights[1].castShadows=false;const auto unshadowedFloor=capture();
        for (unsigned mask:{1u,3u,2u,0u,3u}) {
            lights[0].castShadows=(mask&1)!=0;lights[1].castShadows=(mask&2)!=0;
            const auto shadowedFloor=capture();
            for (float x:{-6.f,-4.f,-2.1f,-1.9f,0.f,1.9f,2.1f,4.f,6.f}) for (float z:{-4.f,0.f,4.f}) {
                const auto point=math::transformPoint(tilt,{x,0,z});
                const auto lit=brightness(unshadowedFloor,settings.camera,point);
                const auto shadow=brightness(shadowedFloor,settings.camera,point);
                check(std::abs(lit-shadow)<4,"Unoccluded floor self-shadowed across a cube face: resolution="+
                    std::to_string(resolution)+", slope="+std::to_string(slope)+", reflected="+std::to_string(reflected)+
                    ", mask="+std::to_string(mask)+", x="+std::to_string(x)+", z="+
                    std::to_string(z)+", clear="+std::to_string(lit)+", shadow="+std::to_string(shadow));
            }
        }
    }
    // Preserve nearby real occlusion while correcting the receiver plane. Both
    // samples lie on a side face, where a larger global bias could hide it.
    scene.pointLights=std::span(lights.data(),1);lights[0].castShadows=true;
    receiver.model.transform=plane(floorCamera,{},12);caster.castShadows=true;
    caster.model.transform=plane(floorCamera,{4,.2f,0},.3f);
    const auto contactShadow=capture();caster.castShadows=false;const auto contactClear=capture();
    check(brightness(contactShadow,settings.camera,{4.444444f,0,0})<brightness(contactClear,settings.camera,{4.444444f,0,0})*.5,
        "Receiver-plane correction erased a nearby side-face caster shadow.");
    scene.shadowResolution=256;caster.castShadows=true;
    // An occluder behind the camera is absent from visible instances but remains
    // in the independently prepared caster set and must affect visible geometry.
    scene.pointLights=std::span(lights.data(),1);lights[0].intensity=1000;lights[0].position={0,0,10};
    settings.camera.position={0,0,6};settings.camera.target={};center={};right={1,0,0};
    receiver.model.transform=plane(settings.camera,{},3);caster.model.transform=plane(settings.camera,{0,0,8},.2f);
    const auto offscreen=capture();lights[0].castShadows=false;const auto offscreenClear=capture();lights[0].castShadows=true;
    check(brightness(offscreenClear,settings.camera,center)>brightness(offscreen,settings.camera,center)+60,"Off-camera caster did not shadow the visible receiver.");
    // Shader publication is atomic: a failing caster must preserve the entire
    // current renderer+ImGui group and its rendered shadow, not just its handle.
    ShaderReloadSession reload(resources,renderer,tools,config);
    const auto pump=[&] {const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{45};
        do{tick();reload.pump();}while(reload.pending() && std::chrono::steady_clock::now()<deadline);
        check(!reload.pending(),"Shadow shader reload timed out.");};
    pump();check(reload.revision()==1,"Valid shadow shader reload failed: "+reload.status());
    const auto healthySignature=renderer.shaderSignature();const auto healthy=capture();
    const auto source=config.sourceDirectory/"fs_point_shadow.sc";std::ifstream input(source);
    const std::string original{std::istreambuf_iterator<char>(input),{}};input.close();
    writeFileAtomically(source,original+"\nINVALID_POINT_SHADOW_SHADER\n");const std::array changed{source};reload.changed(changed);pump();
    check(reload.revision()==1 && renderer.shaderSignature()==healthySignature,"Failed caster reload replaced part of the live program group.");
    check(capture().rgba==healthy.rgba,"Failed caster reload changed the rendered shadow.");
    resources.stop();frame=renderer.drainPending(frame);
}
}
int main(int argc,char** argv) {
    try {
        check(argc==4,"Pass shaderc, engine shader directory and shader include directory.");Temp temp;
        ShaderCompileConfig config{std::filesystem::path(argv[1]),temp.path/"shaders",temp.path/"includes",temp.path/"cache"};
        std::filesystem::copy(std::filesystem::path(argv[2]),config.sourceDirectory,std::filesystem::copy_options::recursive);
        std::filesystem::copy(std::filesystem::path(argv[3]),config.includeDirectory,std::filesystem::copy_options::recursive);
        SdlOwner sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson point-shadow checks",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(bool(window),SDL_GetError());exercise(window.get(),config);
        std::cout<<"Hidden D3D11 point-shadow faces, masks, flags, live edits, sixteen lights, shared views and reload checks passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
