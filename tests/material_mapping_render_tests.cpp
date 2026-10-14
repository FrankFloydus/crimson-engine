#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/Picking.h"
#include "resources/ResourceManager.h"
#include "resources/ImageDecoder.h"
#include <chrono>
#include <filesystem>
#include <thread>
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
void disconnectMaterialInput(MaterialGraph& graph,MaterialNodeId node,MaterialPin pin){if(const auto* link=materialInputLink(graph,node,pin))removeMaterialLink(graph,link->id);}
int difference(std::array<int,3> a,std::array<int,3> b){return std::abs(a[0]-b[0])+std::abs(a[1]-b[1])+std::abs(a[2]-b[2]);}
void exercise(SDL_Window* window,const ShaderCompileConfig& config){
    // Outstanding readback destinations must outlive Renderer on failure.
    std::vector<std::shared_ptr<DecodedImage>> destinations;ResourceManager resources;Renderer renderer(window);MaterialGraphPrograms programs(resources,config);renderer.resizeViewport(Size,Size);renderer.resizeGameViewport(Size,Size);
    ViewportAppearance appearance;appearance.backgroundSrgb={};renderer.setAppearance(appearance);
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=902;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.5f,.5f,.5f})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    std::uint32_t frame{};const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=bgfx::frame();};
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
    // Sprite UV mapping shares the same scale/rotation/offset behavior.
    scene.instances={};scene.directionalLights={};settings.environment.enabled=false;
    RenderParticleMaterial sprite; sprite.colorTexture=color.handle;sprite.samplerFlags=point;
    std::array<RenderParticle,1> particles{{{{0,0,0},2,{1,1,1,1},0}}};particles[0].billboardMode=ParticleBillboardMode::Enabled;
    scene.particles=particles;scene.particleMaterials=std::span(&sprite,1);
    expect(position,0,"Sprite identity UV mapping changed.");sprite.uvOffset={.5f,0,0};
    expect(position,1,"Sprite material did not retain its shared 2D offset.");
    // A shared transparency queue must interleave meshes with particles rather
    // than submitting all sprites last. Linear presentation gives known OVER
    // channel values: half-opacity front=.5, half-opacity rear=.25.
    reset();bindings[0].handles.fill(BGFX_INVALID_HANDLE);material.alphaMask=false;
    material.baseColor={1,0,0,.5f};instance.shading=MaterialShading::Unlit;instance.blend=MaterialBlend::Alpha;
    instance.model.transform={};scene.instances=std::span(&instance,1);
    sprite=RenderParticleMaterial{};sprite.baseColor={0,0,1,.5f};sprite.blend=MaterialBlend::Alpha;
    settings.environment.toneMapping=ToneMapping::Linear;settings.environment.background.overrideColor=true;
    settings.environment.background.color={};settings.environment.background.show=false;
    particles[0].position={0,0,.5f};
    const auto particleFront=sample(capture(),settings.camera,{});
    check(std::abs(particleFront[0]-137)<=4 && std::abs(particleFront[2]-188)<=4,
        "Front particle did not composite once over the rear translucent mesh.");
    instance.model.transform=math::translation({0,0,.5f});particles[0].position={};
    const auto meshFront=sample(capture(),settings.camera,{});
    check(std::abs(meshFront[0]-188)<=4 && std::abs(meshFront[2]-137)<=4,
        "Transparent queue submitted particles after a nearer translucent mesh.");
    // Equal-depth stable ties place the mesh first. The particle must still
    // pass LESS: writing mesh depth would suppress the following blue sprite.
    instance.model.transform={};
    const auto equalDepth=sample(capture(),settings.camera,{});
    check(equalDepth[2]>equalDepth[0]+35,"A translucent mesh wrote visible depth and rejected an equal-depth particle.");
    material.baseColor[3]=.005f;
    check(picked({})==0,"Transparent mesh picking retained coverage below 0.01.");
    material.baseColor[3]=.5f;
    check(picked({})==instance.entityId,"Transparent mesh picking discarded visible coverage.");
    // Opaque geometry is submitted first even when extraction lists a near
    // transparent object before its opaque background.
    auto opaqueSurfaces=surfaces;opaqueSurfaces[0].baseColor={0,1,0,1};
    auto back=instance;back.materials=&opaqueSurfaces;back.blend=MaterialBlend::Opaque;back.model.transform=math::translation({0,0,-.5f});
    const std::array mixedInstances{instance,back};scene.instances=mixedInstances;scene.particles={};
    const auto overOpaque=sample(capture(),settings.camera,{});
    check(std::abs(overOpaque[0]-188)<=4 && std::abs(overOpaque[1]-188)<=4,
        "Opaque extraction order overwrote the queued transparent surface.");
    // Additive RGB must leave coverage alone. A green display matte reveals
    // accidental alpha accumulation without needing access to the HDR target.
    settings.environment.background.color={0,.5f,0};scene.instances={};
    const auto emptyMatte=sample(capture(),settings.camera,{});
    instance.blend=MaterialBlend::Additive;sprite.blend=MaterialBlend::Additive;
    scene.instances=std::span(&instance,1);scene.particles=particles;
    const auto additive=sample(capture(),settings.camera,{});
    check(std::abs(additive[1]-emptyMatte[1])<=2 && additive[0]>180 && additive[2]>180,
        "Additive mesh/particle blending changed destination coverage or premultiplied twice.");
    // Reproduce New Material 2: smoke_07 RGB * a linear Constant, with alpha
    // connected and disconnected explicitly. Source assets are only read.
    const auto project=config.sourceDirectory.parent_path().parent_path().parent_path();
    const auto smokeImage=decodeManagedImage(project/"assets/textures/smoke_07.png");
    check(smokeImage.width && smokeImage.height,"Smoke alpha regression image is unavailable.");
    const auto maximumAlpha=[&]{unsigned result{};for(std::size_t i=3;i<smokeImage.rgba.size();i+=4)result=std::max(result,unsigned(smokeImage.rgba[i]));return result;}();
    check(maximumAlpha>0 && maximumAlpha<255,"Smoke regression image no longer has partial real alpha.");
    Texture smoke{bgfx::createTexture2D(static_cast<std::uint16_t>(smokeImage.width),static_cast<std::uint16_t>(smokeImage.height),false,1,
        bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_SRGB,bgfx::copy(smokeImage.rgba.data(),static_cast<std::uint32_t>(smokeImage.rgba.size())))};
    check(bgfx::isValid(smoke.handle),"Smoke regression texture allocation failed.");
    MaterialAsset authored;authored.id="native-explicit-sprite";
    const auto root=addMaterialNode(authored.graph,MaterialNodeKind::Output).id;
    auto& output=materialOutputNode(authored.graph).output;output.shading=MaterialShading::Unlit;output.blend=MaterialBlend::Alpha;
    const auto texture=addMaterialNode(authored.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(authored.graph,texture)->texture.image.id="assets/textures/smoke_07.png";
    const auto factor=addMaterialNode(authored.graph,MaterialNodeKind::Color).id;
    findMaterialNode(authored.graph,factor)->color={.8355646729469299f,.3041378855705261f,.3041378855705261f,1};
    const auto product=addMaterialNode(authored.graph,MaterialNodeKind::Multiply).id;
    connectMaterialNodes(authored.graph,texture,MaterialPin::RGB,product,MaterialPin::Left);
    connectMaterialNodes(authored.graph,factor,MaterialPin::RGB,product,MaterialPin::Right);
    connectMaterialNodes(authored.graph,product,MaterialPin::Value,root,MaterialPin::BaseColor);
    const auto prepare=[&](const MaterialAsset& source,bgfx::TextureHandle image=BGFX_INVALID_HANDLE) {
        const auto generated=generateMaterialGraphShader(source);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(90);
        MaterialGraphProgramResult published;
        do {
            tick();published=programs.request(generated);
            if(!published.current && published.diagnostic!="Compiling material graph...")throw std::runtime_error(published.diagnostic);
            if(!published.current)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }while(!published.current && std::chrono::steady_clock::now()<deadline);
        check(published.current && published.programs,"Generated sprite shader timed out.");
        auto packet=std::make_shared<PreparedGraphMaterial>();packet->programs=published.programs;packet->uniforms=generated.uniforms;
        packet->sources=generated.textures;packet->shading=materialOutputNode(source.graph).output.shading;packet->blend=materialOutputNode(source.graph).output.blend;
        packet->signature=published.programs->signature;
        for(const auto& binding:generated.textures){check(bgfx::isValid(image),"Regression graph needs a supplied image.");packet->textures[binding.slot]=image;packet->flags[binding.slot]=point|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP;}
        return packet;
    };
    settings.environment.background.color={};settings.environment.enabled=false;scene.directionalLights={};scene.pointLights={};scene.instances={};
    particles[0].position={};particles[0].color={0,1,0,0};sprite=RenderParticleMaterial{};
    scene.particles=particles;scene.particleMaterials=std::span(&sprite,1);
    sprite.graph=prepare(authored,smoke.handle);const auto disconnected=capture();
    connectMaterialNodes(authored.graph,texture,MaterialPin::A,root,MaterialPin::Opacity);
    sprite.graph=prepare(authored,smoke.handle);const auto connected=capture();
    std::uint64_t disconnectedRed{},connectedRed{};
    for(std::size_t i=0;i<connected.rgba.size();i+=4){disconnectedRed+=disconnected.rgba[i];connectedRed+=connected.rgba[i];}
    check(disconnectedRed>10000 && connectedRed<disconnectedRed*.85,"Smoke texture alpha connection did not reduce generated sprite coverage.");
    disconnectMaterialInput(authored.graph,root,MaterialPin::Opacity);
    sprite.graph=prepare(authored,smoke.handle);check(capture().rgba==disconnected.rgba,"Disconnecting smoke alpha did not restore the same authored RGB-only material.");
    // Plain constants are complete quads and ignore instance color/opacity.
    disconnectMaterialInput(authored.graph,root,MaterialPin::BaseColor);
    materialOutputNode(authored.graph).output.baseColor={1,1,1};
    sprite.graph=prepare(authored);const auto plain=capture();
    const auto corner=sample(plain,settings.camera,{.7f,.7f,0});
    check(corner[0]>250 && corner[1]>250 && corner[2]>250,"Constant sprite retained implicit color, opacity or procedural-disc behavior.");
    sprite.graph.reset();sprite.baseColor={1,1,1,1};sprite.blend=MaterialBlend::Alpha;
    const auto fallbackCorner=sample(capture(),settings.camera,{.7f,.7f,0});
    check(fallbackCorner[0]>250 && fallbackCorner[1]>250 && fallbackCorner[2]>250,"Plain fallback implicitly culled or tinted zero-alpha particle input.");
    // The same Particle Color graph evaluates to white on ordinary geometry.
    const auto pc=addMaterialNode(authored.graph,MaterialNodeKind::ParticleColor).id;
    connectMaterialNodes(authored.graph,pc,MaterialPin::RGB,root,MaterialPin::BaseColor);
    connectMaterialNodes(authored.graph,pc,MaterialPin::A,root,MaterialPin::Opacity);
    const auto saturate=addMaterialNode(authored.graph,MaterialNodeKind::Saturate).id;
    findMaterialNode(authored.graph,saturate)->left.value[0]=2;
    const auto clampedAlpha=addMaterialNode(authored.graph,MaterialNodeKind::Multiply).id;
    connectMaterialNodes(authored.graph,saturate,MaterialPin::Value,clampedAlpha,MaterialPin::Left);
    connectMaterialNodes(authored.graph,pc,MaterialPin::A,clampedAlpha,MaterialPin::Right);
    connectMaterialNodes(authored.graph,clampedAlpha,MaterialPin::Value,root,MaterialPin::Opacity);
    sprite.graph=prepare(authored);particles[0].color={1,0,0,.25f};
    const auto explicitColor=sample(capture(),settings.camera,{});
    check(std::abs(explicitColor[0]-137)<=3 && explicitColor[1]<3 && explicitColor[2]<3,"Explicit Particle Color RGB/alpha did not produce a single premultiplication.");
    bindings[0].graph=sprite.graph;material.baseColor={1,1,1,1};instance.model.transform={};scene.particles={};scene.instances=std::span(&instance,1);
    const auto meshColor=sample(capture(),settings.camera,{});
    check(meshColor[0]>250 && meshColor[1]>250 && meshColor[2]>250,"Particle Color was not neutral white on a mesh.");
    bindings[0].graph.reset();scene.instances={};scene.particles=particles;
    disconnectMaterialInput(authored.graph,root,MaterialPin::BaseColor);disconnectMaterialInput(authored.graph,root,MaterialPin::Opacity);
    const auto radial=addMaterialNode(authored.graph,MaterialNodeKind::RadialGradient).id;
    materialOutputNode(authored.graph).output.blend=MaterialBlend::Mask;
    connectMaterialNodes(authored.graph,radial,MaterialPin::Value,root,MaterialPin::OpacityMask);
    sprite.graph=prepare(authored);const auto masked=capture();
    check(sample(masked,settings.camera,{})[0]>250 && sample(masked,settings.camera,{.7f,.7f,0})[0]<3,"Explicit radial Opacity Mask did not cut the sprite quad.");
    // Sparks keeps Lit/Masked coverage: its particle alpha modulates the radial
    // mask, and the cutoff discards the complete surface, including emission.
    // Reuse one prepared packet throughout so only instance alpha changes.
    {
        auto sparks=authored;
        auto& sparksOutput=materialOutputNode(sparks.graph).output;
        sparksOutput.shading=MaterialShading::StandardPbr;sparksOutput.alphaCutoff=.5f;sparksOutput.emission={2,.2f,0};
        connectMaterialNodes(sparks.graph,pc,MaterialPin::RGB,root,MaterialPin::BaseColor);
        const auto particleMask=addMaterialNode(sparks.graph,MaterialNodeKind::Multiply).id;
        connectMaterialNodes(sparks.graph,radial,MaterialPin::Value,particleMask,MaterialPin::Left);
        connectMaterialNodes(sparks.graph,pc,MaterialPin::A,particleMask,MaterialPin::Right);
        connectMaterialNodes(sparks.graph,particleMask,MaterialPin::Value,root,MaterialPin::OpacityMask);
        const auto previousGraph=sprite.graph;const auto previousColor=particles[0].color;
        sprite.graph=prepare(sparks);const auto sparksProgram=sprite.graph->programs;
        scene.particles={};const auto empty=capture();scene.particles=particles;
        particles[0].color={1,.25f,.05f,0};
        check(capture().rgba==empty.rgba,"Zero particle alpha left visible Lit/Masked Sparks emission.");
        particles[0].color[3]=1;const auto visible=capture();
        check(sample(visible,settings.camera,{})[0]>250 && sample(visible,settings.camera,{.7f,.7f,0})[0]<3,
            "Full particle alpha did not preserve the Lit/Masked Sparks center and clipped corners.");
        particles[0].color[3]=.49f;
        check(capture().rgba==empty.rgba,"Particle alpha below the Sparks mask cutoff did not discard the entire sprite.");
        particles[0].color[3]=1;
        check(capture().rgba==visible.rgba && sprite.graph->programs==sparksProgram,
            "Changing Sparks particle alpha did not restore coverage using the same compiled program.");
        // Without the radial connection, particle alpha controls the entire
        // billboard. Its shape must remain the user's authored full quad.
        connectMaterialNodes(sparks.graph,pc,MaterialPin::A,root,MaterialPin::OpacityMask);
        sprite.graph=prepare(sparks);const auto directProgram=sprite.graph->programs;
        particles[0].color[3]=0;
        check(capture().rgba==empty.rgba,"Zero direct particle alpha left visible Lit/Masked Sparks emission.");
        particles[0].color[3]=1;const auto fullQuad=capture();
        check(sample(fullQuad,settings.camera,{})[0]>250 && sample(fullQuad,settings.camera,{.7f,.7f,0})[0]>250,
            "Direct particle alpha retained an unauthored radial sprite shape.");
        particles[0].color[3]=.49f;
        check(capture().rgba==empty.rgba,"Direct particle alpha below the mask cutoff did not discard the full quad.");
        particles[0].color[3]=1;
        check(capture().rgba==fullQuad.rgba && sprite.graph->programs==directProgram,
            "Direct particle alpha edits did not restore full coverage using the same compiled program.");
        sprite.graph=previousGraph;particles[0].color=previousColor;
    }
    materialOutputNode(authored.graph).output.blend=MaterialBlend::Opaque;
    sprite.graph=prepare(authored);check(sample(capture(),settings.camera,{.7f,.7f,0})[0]>250,"Opaque sprite incorrectly evaluated its inactive mask branch.");
    // Lit sprites use the same direct lighting and normal mapping as meshes.
    materialOutputNode(authored.graph).output.shading=MaterialShading::StandardPbr;
    sprite.graph=prepare(authored);const auto darkSprite=sample(capture(),settings.camera,{});
    scene.directionalLights=lights;
    const auto litSprite=sample(capture(),settings.camera,{});
    check(litSprite[0]>darkSprite[0]+30,"Lit sprite did not receive directional lighting.");
    const auto normalNode=addMaterialNode(authored.graph,MaterialNodeKind::Texture2D).id;
    auto* normalDefinition=findMaterialNode(authored.graph,normalNode);normalDefinition->texture.role=MaterialTextureRole::Normal;normalDefinition->texture.image.id="assets/textures/native-normal.png";
    connectMaterialNodes(authored.graph,normalNode,MaterialPin::Normal,root,MaterialPin::Normal);
    sprite.graph=prepare(authored,flat.handle);check(difference(litSprite,sample(capture(),settings.camera,{}))<=3,"Flat sprite normal map altered the facing normal.");
    sprite.graph=prepare(authored,tilted.handle);check(difference(litSprite,sample(capture(),settings.camera,{}))>20,"Sprite normal mapping did not affect lighting.");
    // Opaque sprite depth rejects later transparent meshes behind the billboard.
    scene.directionalLights={};materialOutputNode(authored.graph).output.shading=MaterialShading::Unlit;materialOutputNode(authored.graph).output.baseColor={0,0,1};
    sprite.graph=prepare(authored);instance.model.transform=math::translation({0,0,-.5f});instance.blend=MaterialBlend::Alpha;material.baseColor={1,0,0,.5f};
    scene.instances=std::span(&instance,1);const auto depth=sample(capture(),settings.camera,{});
    check(depth[2]>250 && depth[0]<3,"Opaque sprite did not write visible depth before transparent mesh submission.");
    // Facing is an instance geometry choice, independent of material program.
    // Exercise both embedded fallback and generated programs using the same
    // camera changes; Disabled must retain the world plane, not follow the eye.
    scene.instances={};scene.directionalLights={};scene.pointLights={};
    MaterialAsset oriented;oriented.id="native-oriented-sprite";
    const auto orientedRoot=addMaterialNode(oriented.graph,MaterialNodeKind::Output).id;
    auto& orientedOutput=materialOutputNode(oriented.graph).output;
    orientedOutput.shading=MaterialShading::Unlit;orientedOutput.blend=MaterialBlend::Opaque;orientedOutput.baseColor={1,1,1};
    sprite=RenderParticleMaterial{};sprite.blend=MaterialBlend::Opaque;sprite.baseColor={1,1,1,1};
    const auto covered=[](const DecodedImage& image){std::size_t result{};for(std::size_t i=0;i<image.rgba.size();i+=4)if(image.rgba[i]>8)++result;return result;};
    for(const bool generated:{false,true}) {
        sprite.graph=generated?prepare(oriented):nullptr;
        particles[0].position={};particles[0].right={1,0,0};particles[0].up={0,1,0};particles[0].rotation=0;
        particles[0].billboardMode=ParticleBillboardMode::Disabled;
        settings.camera.position={0,0,3};settings.camera.target={};
        const auto front=covered(capture());check(front>3000,"World-plane particle did not render from its front.");
        settings.camera.position={3,0,0};
        check(covered(capture())<front/100,"Disabled billboarding still turned the quad toward the camera.");
        particles[0].billboardMode=ParticleBillboardMode::Enabled;
        const auto facing=capture();check(covered(facing)>front*.95,"Enabled billboard failed to follow a side camera.");
        particles[0].right={0,1,0};particles[0].up={0,0,1};
        check(capture().rgba==facing.rgba,"Enabled billboard inherited an authored emitter orientation.");
        particles[0].billboardMode=ParticleBillboardMode::Disabled;particles[0].right={0,0,-1};particles[0].up={0,1,0};
        check(covered(capture())>front*.95,"Disabled quad ignored its explicit world-space orientation.");
        particles[0].billboardMode=ParticleBillboardMode::FixedY;
        check(covered(capture())>front*.95,"Fixed-Y billboard did not turn around the vertical axis.");
        settings.camera.position={3,3,0};
        const auto fixedSlope=covered(capture());particles[0].billboardMode=ParticleBillboardMode::Enabled;
        const auto fullSlope=covered(capture());
        check(fixedSlope>500 && fixedSlope<fullSlope*.85,"Fixed-Y billboard tilted with the camera instead of keeping world Y upright.");
        particles[0].billboardMode=ParticleBillboardMode::FixedY;settings.camera.position={0,3,0};
        check(covered(capture())<10,"Fixed-Y pole fallback produced nonfinite or expanded geometry.");
        settings.camera.position={3,0,0};
        check(covered(capture())>front*.95,"Fixed-Y billboard failed to recover after a vertical camera pole.");
    }
    // Rotate a textured Disabled quad in its own plane; UVs follow the same
    // rotation and local triplanar normals retain that rotated tangent frame.
    particles[0].billboardMode=ParticleBillboardMode::Disabled;particles[0].right={0,0,-1};particles[0].up={0,1,0};
    const auto orientedTexture=addMaterialNode(oriented.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(oriented.graph,orientedTexture)->texture.image.id="assets/textures/orientation-fixture.png";
    connectMaterialNodes(oriented.graph,orientedTexture,MaterialPin::RGB,orientedRoot,MaterialPin::BaseColor);
    for(const bool generated:{false,true}) {
        sprite.graph=generated?prepare(oriented,color.handle):nullptr;sprite.colorTexture=color.handle;sprite.samplerFlags=point;
        particles[0].rotation=0;expect({0,.54f,.66f},0,"Oriented sprite UVs lost the authored right/up basis.");
        particles[0].rotation=std::numbers::pi_v<float>*.5f;
        expect({0,.54f,.66f},1,"Rotation did not spin a Disabled sprite within its world plane.");
    }
    disconnectMaterialInput(oriented.graph,orientedRoot,MaterialPin::BaseColor);
    materialOutputNode(oriented.graph).output.shading=MaterialShading::StandardPbr;
    materialOutputNode(oriented.graph).output.baseColor={.5f,.5f,.5f};
    particles[0].rotation=0;lights[0].direction={-1,0,-1};lights[0].intensity=1;scene.directionalLights=lights;
    sprite.graph=prepare(oriented);const auto orientedLit=sample(capture(),settings.camera,{});
    check(orientedLit[0]>40,"Lit Disabled sprite did not use its world-facing surface normal.");
    const auto orientedUv=addMaterialNode(oriented.graph,MaterialNodeKind::UV).id;
    findMaterialNode(oriented.graph,orientedUv)->uv.mode=MaterialUvMode::LocalTriplanar;
    findMaterialNode(oriented.graph,orientedTexture)->texture.role=MaterialTextureRole::Normal;
    connectMaterialNodes(oriented.graph,orientedUv,MaterialPin::UV,orientedTexture,MaterialPin::UV);
    connectMaterialNodes(oriented.graph,orientedTexture,MaterialPin::Normal,orientedRoot,MaterialPin::Normal);
    sprite.graph=prepare(oriented,flat.handle);
    check(difference(orientedLit,sample(capture(),settings.camera,{}))<=3,"Neutral local-triplanar normal changed an oriented sprite's world normal.");
    sprite.graph=prepare(oriented,tilted.handle);const auto orientedTilt=sample(capture(),settings.camera,{});
    particles[0].rotation=std::numbers::pi_v<float>;
    check(difference(orientedTilt,sample(capture(),settings.camera,{}))>10,"Local-triplanar normal did not rotate with the Disabled sprite basis.");
    scene.instances={};scene.particles={};sprite.graph.reset();resources.stop();
    frame=renderer.drainPending(frame);
}
}
int main(int argc,char** argv){try{
    check(argc==4,"Pass shaderc, source and include directories.");
    const auto temporary=std::filesystem::temp_directory_path()/("crimson-mapping-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code error;const auto resolved=std::filesystem::weakly_canonical(path,error);const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(!error && resolved.parent_path()==parent && resolved.filename().string().starts_with("crimson-mapping-"))std::filesystem::remove_all(resolved,error);}} cleanup{temporary};
    ShaderCompileConfig config{argv[1],argv[2],argv[3],temporary};
    Sdl sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson material mapping checks",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);check(bool(window),SDL_GetError());exercise(window.get(),config);std::cout<<"Material mapping, coverage/picking, mixed mesh/particle ordering, depth and additive checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
