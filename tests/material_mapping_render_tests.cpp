#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/Picking.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace {
using namespace crimson;
constexpr std::uint16_t Size=128,ReadView=254;
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Sdl {Sdl(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~Sdl(){SDL_Quit();}};
struct Texture {bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};~Texture(){if(bgfx::isValid(handle))bgfx::destroy(handle);}};
std::shared_ptr<ModelAsset> quad(){
    auto asset=std::make_shared<ModelAsset>();asset->id="material-mapping-quad";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    constexpr std::array<Vec3,4> positions{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
    constexpr std::array<Vec2,4> uv{{{0,1},{1,1},{1,0},{0,0}}};
    for(unsigned i=0;i<4;++i){ModelVertex v;v.position=positions[i];v.normal={0,0,1};v.uv0=uv[i];v.uv1={1-uv[i][0],uv[i][1]};v.tangent={1,0,0,-1};primitive.vertices.push_back(v);math::include(asset->bounds,v.position);}
    primitive.indices={0,1,2,0,2,3};asset->primitives.push_back(std::move(primitive));asset->draws.push_back({0,{}});
    ModelMaterial material;material.metallic=0;material.roughness=1;asset->materials.push_back(material);return asset;
}
std::array<int,3> sample(const DecodedImage& image,const Camera& camera,Vec3 world){
    const auto p=math::project(world,camera.projectionMatrix(1,ClipDepth::ZeroToOne)*camera.viewMatrix(),{0,0,Size,Size},ClipDepth::ZeroToOne);
    check(bool(p),"Mapping fixture point is not projectable.");const int x=static_cast<int>((*p)[0]),y=static_cast<int>((*p)[1]);
    check(x>1 && y>1 && x<Size-2 && y<Size-2,"Mapping fixture point escaped the image.");
    std::array<int,3> result{};for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)for(unsigned c=0;c<3;++c)result[c]+=image.rgba[(std::size_t(y+dy)*Size+x+dx)*4+c];
    for(auto& value:result)value/=9;return result;
}
int difference(std::array<int,3> a,std::array<int,3> b){return std::abs(a[0]-b[0])+std::abs(a[1]-b[1])+std::abs(a[2]-b[2]);}
void exercise(SDL_Window* window){
    // Outstanding readback destinations must outlive Renderer on failure.
    std::vector<std::shared_ptr<DecodedImage>> destinations;Renderer renderer(window);renderer.resizeViewport(Size,Size);renderer.resizeGameViewport(Size,Size);
    ViewportAppearance appearance;appearance.backgroundSrgb={};renderer.setAppearance(appearance);
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=902;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.5f,.5f,.5f})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    std::uint32_t frame{};const auto tick=[&]{renderer.advanceStreaming();frame=bgfx::frame();};
    const auto read=[&](bgfx::TextureHandle target){
        Texture staging{bgfx::createTexture2D(Size,Size,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.handle),"Mapping readback allocation failed.");
        auto pixels=std::make_shared<DecodedImage>(DecodedImage{Size,Size,std::vector<std::uint8_t>(Size*Size*4)});destinations.push_back(pixels);
        bgfx::blit(ReadView,staging.handle,0,0,target,0,0,Size,Size);const auto ready=bgfx::readTexture(staging.handle,pixels->rgba.data());
        check(ready!=UINT32_MAX,"Mapping readback is unsupported.");
        for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return *pixels;}
        throw std::runtime_error("Mapping readback timed out.");
    };
    auto mesh=quad();check(renderer.prepareModel(*mesh),renderer.modelStatus());
    std::vector<ModelMaterial> surfaces=mesh->materials;auto& material=surfaces.front();std::vector<ModelMaterialTextures> bindings(1);
    RenderModelInstance instance;instance.entityId=11;instance.model.asset=mesh;instance.materials=&surfaces;instance.textures=&bindings;instance.shading=MaterialShading::Unlit;
    SceneSettings settings;settings.camera.position={0,0,3};settings.camera.target={};settings.camera.fov=45;settings.camera.fovAxis=CameraFovAxis::Vertical;settings.environment.enabled=false;
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=std::span(&instance,1);
    const auto capture=[&]{renderer.renderGame(scene);return read(renderer.gameViewportTexture());};
    const auto reset=[&]{material.uvScale={1,1,1};material.uvOffset={};material.uvRotation={};material.triplanar=false;material.triplanarWorld=false;};
    constexpr std::array<std::uint8_t,16> colors{{255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255}};
    Texture color{bgfx::createTexture2D(2,2,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_SRGB,bgfx::copy(colors.data(),16))};check(bgfx::isValid(color.handle),"Mapping color texture allocation failed.");
    const std::uint32_t point=BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT;
    bindings[0].handles[0]=color.handle;bindings[0].flags.fill(point);
    const Vec3 position{-.66f,.54f,0};
    const auto expect=[&](Vec3 p,unsigned channel,const char* reason){const auto rgb=sample(capture(),settings.camera,p);check(rgb[channel]>170 && rgb[(channel+1)%3]<5 && rgb[(channel+2)%3]<5,reason);};
    expect(position,0,"Identity mapping changed imported UV0.");
    material.uvScale={2,3,1};expect(position,2,"Material-wide UV scale was not applied.");
    reset();material.uvOffset={.5f,0,0};expect(position,1,"Material-wide UV offset was not applied.");
    reset();material.uvRotation={0,0,90};material.uvOffset={1,0,0};expect(position,1,"UV Z rotation did not precede offset.");
    reset();material.uvScale={-1,1,1};material.uvOffset={1,0,0};expect(position,1,"Negative UV scale did not mirror the texture.");
    reset();material.baseColorTexture.texCoord=1;expect(position,1,"Mapping lost the imported UV1 selection.");material.baseColorTexture.texCoord=0;
    material.triplanar=true;expect({.3f,.3f,0},2,"Triplanar projection did not use local mesh position.");
    instance.model.transform=math::translation({3,2,0});settings.camera.position={3,2,3};settings.camera.target={3,2,0};
    expect({3.3f,2.3f,0},2,"Triplanar texture moved when its entity translated.");
    instance.model.transform={};settings.camera.position={0,0,3};settings.camera.target={};
    material.uvRotation={0,0,180};expect({.3f,.3f,0},1,"Triplanar rotation was not shared by its projections.");
    reset();material.triplanar=true;material.triplanarWorld=true;
    const auto anchored=capture();
    const auto parent=math::translation({.2f,-.1f,0})*math::rotation(math::axisAngle({0,0,1},.7f))*math::scaling({1.7f,2.1f,1});
    const auto child=math::rotation(math::axisAngle({0,0,1},-.4f))*math::scaling({-1.2f,.9f,1});
    const std::array worldTransforms{math::scaling({2,3,.8f}),math::translation({.5f,.25f,0})*math::scaling({2,3,1}),parent*child};
    for(const auto& transform:worldTransforms) {
        instance.model.transform=transform;const auto moved=capture();
        for(const auto p:{Vec3{.3f,.3f,0},Vec3{.7f,.3f,0},Vec3{-.3f,-.3f,0}})
            check(difference(sample(anchored,settings.camera,p),sample(moved,settings.camera,p))<=3,
                "World triplanar tiling changed under scale, translation or accumulated parent rotation/reflection.");
    }
    instance.model.transform=math::translation({.5f,0,0});material.triplanarWorld=false;
    check(difference(sample(anchored,settings.camera,{.3f,.3f,0}),sample(capture(),settings.camera,{.3f,.3f,0}))>100,
        "Local triplanar unexpectedly became world-anchored.");
    instance.model.transform={};
    reset();bindings[0].handles[0]=BGFX_INVALID_HANDLE;instance.shading=MaterialShading::StandardPbr;
    std::array<DirectionalLight,1> lights{{{{-1,0,-1},{1,1,1},4,true}}};scene.directionalLights=lights;
    constexpr std::array<std::uint8_t,4> flatPixel{{128,128,255,255}},tiltedPixel{{255,128,255,255}};
    Texture flat{bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(flatPixel.data(),4))};
    Texture tilted{bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(tiltedPixel.data(),4))};
    const auto neutral=sample(capture(),settings.camera,{});bindings[0].handles[2]=flat.handle;
    material.uvScale={2,-3,4};material.uvRotation={27,42,73};material.triplanar=true;
    check(difference(neutral,sample(capture(),settings.camera,{}))<=3,"Flat triplanar normal changed the original smooth normal.");
    auto smooth=quad();smooth->id="material-mapping-smooth-normals";
    constexpr std::array<Vec3,4> smoothNormals{{{-.7f,-.2f,1},{.5f,-.3f,1},{.5f,.7f,1},{-.4f,.5f,1}}};
    for(unsigned i=0;i<4;++i)smooth->primitives[0].vertices[i].normal=math::normalized(smoothNormals[i]);
    check(renderer.prepareModel(*smooth),renderer.modelStatus());instance.model.asset=smooth;
    for(bool world:{false,true}) for(float reflection:{-1.f,1.f}) {
        material.triplanarWorld=world;
        instance.model.transform=math::scaling({.9f*reflection,1.2f,.7f});bindings[0].handles[2]=BGFX_INVALID_HANDLE;
        const auto smoothBase=capture();bindings[0].handles[2]=flat.handle;const auto smoothFlat=capture();
        for(const auto p:{Vec3{},Vec3{-.2f,.2f,0},Vec3{.2f,-.2f,0}})
            check(difference(sample(smoothBase,settings.camera,p),sample(smoothFlat,settings.camera,p))<=3,
                "Flat triplanar normal changed a curved smooth normal under nonuniform/reflected world scale.");
    }
    instance.model.asset=mesh;instance.model.transform={};
    reset();bindings[0].handles[2]=tilted.handle;const auto forward=sample(capture(),settings.camera,{});
    material.uvRotation[2]=180;const auto backward=sample(capture(),settings.camera,{});
    check(forward[0]>backward[0]+50,"Rotating ordinary UVs did not reorient the normal map.");
    reset();material.uvScale[0]=-1;check(difference(backward,sample(capture(),settings.camera,{}))<=3,"Mirrored UVs lost normal-map handedness.");
    reset();material.triplanar=true;check(difference(forward,sample(capture(),settings.camera,{}))<=3,"Triplanar Z normal was not reoriented into the model basis.");
    material.uvRotation[2]=180;check(difference(backward,sample(capture(),settings.camera,{}))<=3,"Triplanar normal rotation was not inverted back to local space.");
    reset();material.triplanar=true;material.triplanarWorld=true;
    for(const auto& transform:worldTransforms) {
        instance.model.transform=transform;
        check(difference(forward,sample(capture(),settings.camera,{}))<=3,
            "World triplanar normal rotated or reflected with the entity instead of retaining world axes.");
    }
    instance.model.transform=math::rotation(math::axisAngle({0,1,0},std::numbers::pi_v<float>*.5f))*math::scaling({2,3,.7f});
    settings.camera.position={3,0,0};lights[0].direction={-1,0,1};
    check(difference(forward,sample(capture(),settings.camera,{}))<=3,"World triplanar X projection normal has incorrect axis reorientation.");
    instance.model.transform={};settings.camera.position={0,0,3};lights[0].direction={-1,0,-1};
    // Each non-color slot must use the same transformed domain and retain its
    // own data role. Uniform two-column maps make an offset swap measurable.
    reset();bindings[0].handles.fill(BGFX_INVALID_HANDLE);scene.directionalLights={};settings.environment.enabled=true;
    constexpr std::array<std::uint8_t,8> values{{0,0,0,255,255,255,255,255}};
    Texture data{bgfx::createTexture2D(2,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(values.data(),8))};
    material.metallic=0;material.roughness=1;material.occlusionStrength=1;bindings[0].handles[3]=data.handle;
    const auto occluded=sample(capture(),settings.camera,position);material.uvOffset[0]=.5f;const auto open=sample(capture(),settings.camera,position);
    check(open[0]>occluded[0]+50,"AO map ignored the shared offset.");
    bindings[0].handles[3]=BGFX_INVALID_HANDLE;bindings[0].handles[4]=color.handle;material.emissive={1,1,1};settings.environment.enabled=false;
    reset();expect(position,0,"Emissive map did not use the standard transformed domain.");material.uvOffset[0]=.5f;expect(position,1,"Emissive map ignored the shared offset.");
    bindings[0].handles[4]=BGFX_INVALID_HANDLE;material.emissive={};scene.directionalLights=lights;material.metallic=1;bindings[0].handles[1]=data.handle;
    reset();const auto packedA=sample(capture(),settings.camera,position);material.uvOffset[0]=.5f;const auto packedB=sample(capture(),settings.camera,position);
    check(difference(packedA,packedB)>30,"Packed metallic/roughness map ignored the shared offset.");
    // Color and ID picking must agree on a transformed alpha silhouette.
    reset();bindings[0].handles.fill(BGFX_INVALID_HANDLE);material.metallic=0;instance.shading=MaterialShading::Unlit;
    constexpr std::array<std::uint8_t,8> alphaPixels{{255,255,255,0,255,255,255,255}};
    Texture alpha{bgfx::createTexture2D(2,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(alphaPixels.data(),8))};
    bindings[0].handles[0]=alpha.handle;material.alphaMask=true;material.alphaCutoff=.5f;
    std::uint64_t serial{};
    const auto picked=[&](Vec3 p){
        const auto screen=math::project(p,settings.camera.projectionMatrix(1,ClipDepth::ZeroToOne)*settings.camera.viewMatrix(),{0,0,Size,Size},ClipDepth::ZeroToOne);
        check(bool(screen),"Mask pick point failed projection.");check(renderer.requestPick({1,1,++serial,static_cast<std::uint32_t>((*screen)[0]),static_cast<std::uint32_t>((*screen)[1]),renderer.streamingSignature()}),renderer.pickingStatus());
        renderer.render(scene);std::optional<PickResult> result;for(unsigned i=0;i<120 && !result;++i){tick();result=renderer.pollPick(frame);}check(bool(result),"Mask pick timed out.");return result->entityId;
    };
    check(picked(position)==0,"Identity alpha mask returned a transparent entity.");material.uvOffset[0]=.5f;
    check(picked(position)==instance.entityId && sample(capture(),settings.camera,position)[0]>170,"Transformed color/pick alpha masks disagree.");
    reset();material.triplanar=true;check(picked({.3f,.3f,0})==0,"Triplanar alpha pick did not use local projection.");
    material.uvOffset[0]=.5f;check(picked({.3f,.3f,0})==instance.entityId,"Triplanar alpha pick ignored mapping edits.");
    reset();material.triplanar=true;material.triplanarWorld=true;
    for(const auto& transform:worldTransforms) {
        instance.model.transform=transform;
        check(picked({.3f,.3f,0})==0 && picked({.7f,.3f,0})==instance.entityId,
            "World triplanar picking mask changed under accumulated world transforms.");
        const auto maskImage=capture();
        check(sample(maskImage,settings.camera,{.3f,.3f,0})[0]<5 && sample(maskImage,settings.camera,{.7f,.3f,0})[0]>170,
            "World triplanar color/picking mask parity was lost.");
    }
    instance.model.transform=math::scaling({2,3,1});material.triplanarWorld=false;
    check(picked({.7f,.3f,0})==0,"Local triplanar picking mask lost its authored local domain.");
    instance.model.transform={};
    // Sprite materials deliberately keep a 2D domain while sharing XY mapping.
    scene.instances={};scene.directionalLights={};settings.environment.enabled=false;
    RenderParticleMaterial sprite; sprite.colorTexture=color.handle;sprite.samplerFlags=point;
    std::array<RenderParticle,1> particles{{{{0,0,0},2,{1,1,1,1},0}}};scene.particles=particles;scene.particleMaterials=std::span(&sprite,1);
    expect(position,0,"Sprite identity UV mapping changed.");sprite.uvOffset={.5f,0,0};sprite.triplanar=true;
    expect(position,1,"Sprite material did not retain its shared 2D offset.");
    frame=renderer.drainPending(frame);
}
}
int main(){try{Sdl sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson material mapping checks",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);check(bool(window),SDL_GetError());exercise(window.get());std::cout<<"Material UV, triplanar, normal, slot, mask/pick and sprite checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
