#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/Picking.h"
#include "crimson-renderer/RenderPassPlan.h"
#include "assets/MaterialAsset.h"
#include "editor/DecalPreview.h"
#include "resources/ResourceManager.h"
#include "GpuTestBackend.h"
#include <crimson/renderer/forward/DecalRenderTypes.h>
#include <SDL3/SDL.h>
#include <lodepng.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
constexpr std::uint16_t Width=161,Height=129;
constexpr std::uint32_t Clamp=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP;
void check(bool result,const std::string& diagnostic){if(!result)throw std::runtime_error(diagnostic);}
struct Texture {
    bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};
    ~Texture(){if(bgfx::isValid(handle))bgfx::destroy(handle);}
};
std::shared_ptr<ModelAsset> quad(const std::string& id,const std::array<Vec3,4>& points,Vec3 normal) {
    auto asset=std::make_shared<ModelAsset>();asset->id=id;asset->name=id;
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    constexpr std::array<Vec2,4> uv{{{0,1},{1,1},{1,0},{0,0}}};
    for(std::size_t i=0;i<points.size();++i) {
        ModelVertex vertex;vertex.position=points[i];vertex.normal=normal;vertex.uv0=vertex.uv1=uv[i];
        vertex.tangent={1,0,0,-1};primitive.vertices.push_back(vertex);
        math::include(asset->bounds,points[i]);math::include(primitive.bounds,points[i]);
    }
    primitive.indices={0,1,2,0,2,3};asset->primitives.push_back(std::move(primitive));
    asset->draws.push_back({0,{}});asset->materials.emplace_back();
    asset->materials[0].baseColor={.24f,.24f,.24f,1};asset->materials[0].metallic=0;
    asset->materials[0].roughness=.8f;return asset;
}
std::shared_ptr<DecalPatchData> flatPatch(std::uint64_t receiver=1,float z=0) {
    auto patch=std::make_shared<DecalPatchData>();patch->fingerprint=100+receiver;
    patch->vertices={{{-.65f,-.65f,z},{0,0,1},{0,0}},{{.65f,-.65f,z},{0,0,1},{1,0}},
        {{.65f,.65f,z},{0,0,1},{1,1}},{{-.65f,.65f,z},{0,0,1},{0,1}}};
    patch->indices={0,1,2,0,2,3};patch->receivers={{receiver,0,0,0,6,0}};
    for(const auto& vertex:patch->vertices)math::include(patch->bounds,vertex.position);return patch;
}
RenderDecal decal(std::shared_ptr<const DecalPatchData> patch,std::shared_ptr<const PreparedDecalMaterial> material,
                  std::uint64_t id=101,std::int32_t priority=0) {
    RenderDecal result;result.entityId=result.stableOrder=id;result.layer=priority;
    result.patch=std::move(patch);result.material=std::move(material);result.edgeFeather=0;return result;
}
std::size_t changed(const DecodedImage& a,const DecodedImage& b,unsigned tolerance=1) {
    check(a.width==b.width&&a.height==b.height,"Generated decal image extents differ.");std::size_t count{};
    for(std::size_t i=0;i<a.rgba.size();i+=4) {
        bool differs=false;for(unsigned c=0;c<3;++c)differs|=unsigned(std::abs(int(a.rgba[i+c])-int(b.rgba[i+c])))>tolerance;
        count+=differs;
    }return count;
}
double error(const DecodedImage& a,const DecodedImage& b,bool center=false) {
    double sum{};std::size_t count{};
    for(unsigned y=center?Height/3:0;y<(center?Height*2/3:Height);++y)
        for(unsigned x=center?Width/3:0;x<(center?Width*2/3:Width);++x)for(unsigned c=0;c<3;++c) {
            const auto offset=(std::size_t(y)*Width+x)*4+c;
            sum+=std::abs(int(a.rgba[offset])-int(b.rgba[offset]));++count;
        }
    return sum/count;
}
void write(const fs::path& root,const std::string& name,const DecodedImage& image) {
    if(root.empty())return;fs::create_directories(root);
    const auto result=lodepng::encode((root/(name+".png")).string(),image.rgba,image.width,image.height);
    check(result==0,"Could not write generated decal image: "+std::string(lodepng_error_text(result)));
}

void exercise(SDL_Window* window,const ShaderCompileConfig& config,const fs::path& artifacts) {
    // Readback storage survives Renderer on normal and exceptional teardown.
    std::vector<std::shared_ptr<DecodedImage>> destinations;
    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));
    MaterialGraphPrograms programs(resources,config);TestGpuContext gpu;std::uint32_t frame{};
    renderer.resizeViewport(Width,Height);renderer.setGridVisible(false);
    const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=renderer.advance_frame();gpu.flush();};
    const auto read=[&](bgfx::TextureHandle source) {
        Texture staging{bgfx::createTexture2D(Width,Height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.handle),"Generated decal staging allocation failed.");
        auto result=std::make_shared<DecodedImage>(DecodedImage{Width,Height,std::vector<std::uint8_t>(std::size_t(Width)*Height*4)});
        destinations.push_back(result);bgfx::blit(DiagnosticReadbackView,staging.handle,0,0,source,0,0,Width,Height);
        const auto ready=bgfx::readTexture(staging.handle,result->rgba.data());check(ready!=UINT32_MAX,"Generated decal readback unsupported.");
        for(unsigned attempt=0;attempt<120;++attempt){tick();if(picking::frameReady(frame,ready))return *result;}
        renderer.drainPending(frame);throw std::runtime_error("Generated decal readback timed out.");
    };
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x44454347454e;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.03f,.03f,.03f})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    constexpr std::array<std::uint8_t,4> whitePixel{255,255,255,255},normalPixel{235,128,198,255};
    Texture white{bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,Clamp|BGFX_TEXTURE_SRGB,bgfx::copy(whitePixel.data(),4))};
    Texture normal{bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,Clamp,bgfx::copy(normalPixel.data(),4))};
    check(bgfx::isValid(white.handle)&&bgfx::isValid(normal.handle),"Generated decal fixture texture allocation failed.");

    MaterialAsset asset;asset.id="generated-decal-pbr";asset.name="Generated decal PBR";asset.graph.nodes.reserve(32);
    const auto outputId=addMaterialNode(asset.graph,MaterialNodeKind::Output).id;
    const auto output=[&]() -> MaterialOutputNode& {return findMaterialNode(asset.graph,outputId)->output;};
    output().shading=MaterialShading::StandardPbr;output().roughness=.8f;output().metallic=0;output().specular=.5f;
    MaterialNodeId previous{},factorId{};
    // All twelve graph samplers remain live while the atlas supplies decals.
    for(unsigned slot=0;slot<12;++slot) {
        const auto texture=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D).id;
        findMaterialNode(asset.graph,texture)->texture.image.id="assets/textures/generated-decal-"+std::to_string(slot)+".png";
        const auto product=addMaterialNode(asset.graph,MaterialNodeKind::Multiply).id;
        if(previous)connectMaterialNodes(asset.graph,previous,MaterialPin::Value,product,MaterialPin::Left);
        else {
            factorId=product;findMaterialNode(asset.graph,product)->left={MaterialValueType::Color,{.24f,.24f,.24f,1}};
        }
        connectMaterialNodes(asset.graph,texture,MaterialPin::RGB,product,MaterialPin::Right);previous=product;
    }
    connectMaterialNodes(asset.graph,previous,MaterialPin::Value,outputId,MaterialPin::BaseColor);
    const auto prepare=[&]() {
        const auto generated=generateMaterialGraphShader(asset);MaterialGraphProgramResult result;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{180};
        do {
            tick();result=programs.request(generated);
            if(!result.current&&!result.compiling)throw std::runtime_error("Generated decal compile failed: "+result.diagnostic);
            if(!result.current)std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }while(!result.current&&std::chrono::steady_clock::now()<deadline);
        check(result.current&&result.programs,"Generated decal program publication timed out: "+result.diagnostic);
        auto packet=std::make_shared<PreparedGraphMaterial>();packet->programs=result.programs;
        packet->uniforms=generated.uniforms;packet->sources=generated.textures;packet->runtimeGraph=generated.runtimeGraph;
        packet->runtimeParameters=generated.runtimeParameters;packet->shading=output().shading;packet->blend=output().blend;
        packet->receiveFog=output().receiveFog;packet->refraction=generated.refraction;packet->tessellation=generated.tessellation;
        packet->dynamicCoverage=generated.dynamicCoverage;packet->constantRoughness=generated.constantRoughness;
        packet->signature=result.programs->signature;packet->decalChannels=generated.decalChannels;
        for(const auto& source:generated.textures){packet->textures[source.slot]=white.handle;packet->flags[source.slot]=Clamp;}
        check(packet->sources.size()==12,"Generated decal fixture lost part of the twelve-texture budget.");
        for(unsigned slot=0;slot<12;++slot)check(bgfx::isValid(packet->textures[slot])&&packet->flags[slot]!=UINT32_MAX,
            "A live graph texture slot was not bound.");
        return packet;
    };
    auto graph=prepare();auto mesh=quad("generated-decal-plane",{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}},{0,0,1});
    check(renderer.prepareModel(*mesh),renderer.modelStatus());auto surfaces=mesh->materials;
    std::vector<ModelMaterialTextures> graphBindings(1);graphBindings[0].graph=graph;
    RenderModelInstance receiver;receiver.entityId=1;receiver.model.asset=mesh;receiver.materials=&surfaces;
    receiver.receiveDecals=true;receiver.forceFullDetail=true;std::vector<RenderModelInstance> instances{receiver};
    SceneSettings settings;settings.camera.position={0,0,3};settings.camera.target={};settings.camera.nearPlane=.1f;settings.camera.farPlane=32;
    settings.camera.fovAxis=CameraFovAxis::Vertical;settings.environment.enabled=false;settings.environment.background.show=false;
    settings.environment.toneMapping=ToneMapping::Linear;settings.environment.ambientLight.source=AmbientLightSource::Disabled;
    DirectionalLight light;light.direction={-.3f,-.2f,-1};light.color={1,1,1};light.intensity=2;
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.directionalLights=std::span(&light,1);
    const auto capture=[&](std::span<const RenderDecal> decals) {
        scene.instances=instances;scene.decals=decals;renderer.render(scene);return read(renderer.viewportTexture());
    };
    const auto decalGraph=[&](std::uint64_t fingerprint,bool color,bool normalEnabled,bool roughness,Vec3 rgb=Vec3{.75f,.015f,.01f},float normalOpacity=1.f,bool metallic=false) {
        MaterialAsset definition;definition.id="decal-domain-fixture-"+std::to_string(fingerprint);
        auto& out=addMaterialNode(definition.graph,MaterialNodeKind::Output);
        const auto decalOutput=out.id;out.output.shading=MaterialShading::Decal;out.output.baseColor=rgb;
        out.output.decalColorEnabled=color;out.output.decalNormalEnabled=normalEnabled;out.output.decalRoughnessEnabled=roughness;
        out.output.decalNormalStrength=normalOpacity;out.output.roughness=.06f;out.output.decalMetallicEnabled=metallic;out.output.metallic=.9f;
        if(normalEnabled) {
            const auto texture=addMaterialNode(definition.graph,MaterialNodeKind::Texture2D).id;
            findMaterialNode(definition.graph,texture)->texture.role=MaterialTextureRole::Normal;
            findMaterialNode(definition.graph,texture)->texture.image.id="assets/textures/decal-fixture-normal.png";
            connectMaterialNodes(definition.graph,texture,MaterialPin::Normal,decalOutput,MaterialPin::Normal);
        }
        const auto generated=generateMaterialGraphShader(definition);MaterialGraphProgramResult result;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{180};
        do {tick();result=programs.request(generated);if(!result.current&&!result.compiling)throw std::runtime_error(result.diagnostic);
            if(!result.current)std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }while(!result.current&&std::chrono::steady_clock::now()<deadline);
        check(result.current&&result.programs,"Decal domain graph compilation timed out: "+result.diagnostic);
        auto packet=std::make_shared<PreparedGraphMaterial>();packet->programs=result.programs;packet->uniforms=generated.uniforms;
        packet->sources=generated.textures;packet->runtimeGraph=generated.runtimeGraph;packet->runtimeParameters=generated.runtimeParameters;
        packet->shading=MaterialShading::Decal;packet->signature=result.programs->signature;packet->decalChannels=generated.decalChannels;
        for(const auto& source:generated.textures){packet->textures[source.slot]=normal.handle;packet->flags[source.slot]=Clamp;}
        auto prepared=std::make_shared<PreparedDecalMaterial>();prepared->fingerprint=fingerprint;prepared->graph=std::move(packet);return prepared;
    };
    auto patch=flatPatch();auto red=decalGraph(1,true,false,false);
    auto normalOnly=decalGraph(2,false,true,false);
    auto wet=decalGraph(3,false,false,true);
    auto all=decalGraph(4,true,true,true,{.75f,.015f,.01f},1,true);
    auto metal=decalGraph(7,false,false,false,{1,1,1},1,true);
    const auto fixedBase=capture({});instances[0].textures=&graphBindings;const auto graphBase=capture({});
    check(error(fixedBase,graphBase)<.2,"Fixed/generated base PBR surfaces diverged before decal testing.");
    const std::array<std::shared_ptr<const PreparedDecalMaterial>,5> materials{red,normalOnly,wet,all,metal};
    const std::array<std::string,5> names{"color","normal","roughness","combined","metallic"};
    std::vector<RenderDecal> decals;
    for(unsigned channel=0;channel<materials.size();++channel) {
        decals={decal(patch,materials[channel])};instances[0].textures=nullptr;const auto fixed=capture(decals);
        instances[0].textures=&graphBindings;const auto generated=capture(decals);
        const auto parity=error(fixed,generated);
        std::cout<<names[channel]<<" fixed/generated mean byte error: "<<parity<<'\n';
        check(parity<.25,"Fixed/generated decal "+names[channel]+" results diverged.");
        check(changed(graphBase,generated)>100,"Generated decal "+names[channel]+" channel did not affect PBR.");
        write(artifacts,"generated-"+names[channel],generated);write(artifacts,"fixed-"+names[channel],fixed);
    }
    decals={decal(patch,normalOnly)};const auto lowerNormal=capture(decals);
    auto zeroNormal=decalGraph(5,false,true,false,{1,1,1},0);
    decals.push_back(decal(patch,zeroNormal,102,2));
    check(capture(decals).rgba==lowerNormal.rgba,"Zero normal strength erased a lower normal on a generated receiver.");

    // Index-matched transmission must see the completed painted opaque source.
    decals={decal(patch,red)};const auto opaquePaint=capture(decals);
    output().blend=MaterialBlend::Alpha;output().doubleSided=true;output().refraction=true;
    output().ior=1;output().roughness=0;output().transmission=1;output().opacity=1;output().thickness=0;
    output().attenuationColor={1,1,1};auto glassGraph=prepare();
    std::vector<ModelMaterialTextures> glassBindings(1);glassBindings[0].graph=glassGraph;
    auto glass=receiver;glass.entityId=2;glass.model.transform=math::translation({0,0,.3f})*math::scaling({.8f,.8f,1});
    glass.textures=&glassBindings;glass.blend=MaterialBlend::Alpha;glass.receiveDecals=true;instances.push_back(glass);
    const auto throughGlass=capture(decals);
    check(renderer.refractionStatus().empty()&&renderer.refractionResidentBytes()>0,"Generated glass did not publish an optical snapshot: "+renderer.refractionStatus());
    check(error(opaquePaint,throughGlass,true)<1.1,"Refraction snapshot omitted completed decal shading on the opaque receiver.");
    auto blue=decalGraph(6,true,false,false,{.01f,.01f,.9f});
    decals.push_back(decal(flatPatch(2,.3f),blue,103,3));
    check(capture(decals).rgba==throughGlass.rgba,"Unsupported translucent graph received a decal.");
    check(renderer.decalStats(RenderViewPurpose::Scene).submitted==1,"Translucent surface was admitted as a decal receiver.");
    write(artifacts,"paint-through-refraction",throughGlass);instances.resize(1);

    // The generated roughness-one floor must be admitted by its wet decal,
    // retaining all twelve graph slots during exact-material SSR composition.
    auto floor=quad("generated-decal-floor",{{{-1,-.2f,0},{-1,-.2f,2},{1,-.2f,2},{1,-.2f,0}}},{0,1,0});
    auto wall=quad("generated-decal-wall",{{{-1,-.2f,0},{1,-.2f,0},{1,1.8f,0},{-1,1.8f,0}}},{0,0,1});
    check(renderer.prepareModel(*floor)&&renderer.prepareModel(*wall),renderer.modelStatus());
    auto floorSurfaces=surfaces,wallSurfaces=surfaces;floorSurfaces[0].baseColor={.02f,.02f,.02f,1};floorSurfaces[0].roughness=1;
    wallSurfaces[0].baseColor={4,.01f,.01f,1};
    output().blend=MaterialBlend::Opaque;output().doubleSided=false;output().refraction=false;output().roughness=1;
    findMaterialNode(asset.graph,factorId)->left.value={.02f,.02f,.02f,1};graphBindings[0].graph=prepare();
    check(graphBindings[0].graph->constantRoughness==std::optional<float>{1},"Wet SSR graph did not retain roughness-one eligibility metadata.");
    instances={receiver,receiver};instances[0].model.asset=floor;instances[0].materials=&floorSurfaces;
    instances[1].entityId=2;instances[1].model.asset=wall;instances[1].materials=&wallSurfaces;
    instances[1].shading=MaterialShading::Unlit;instances[1].receiveDecals=false;
    auto floorPatch=std::make_shared<DecalPatchData>();floorPatch->fingerprint=300;
    floorPatch->vertices={{{-.65f,-.2f,0},{0,1,0},{0,1}},{{-.65f,-.2f,.65f},{0,1,0},{0,0}},
        {{.65f,-.2f,.65f},{0,1,0},{1,0}},{{.65f,-.2f,0},{0,1,0},{1,1}}};
    floorPatch->indices={0,1,2,0,2,3};floorPatch->receivers={{1,0,0,0,6,0}};
    for(const auto& vertex:floorPatch->vertices)math::include(floorPatch->bounds,vertex.position);
    decals={decal(floorPatch,wet)};settings.camera.position={0,1.8f,3.4f};settings.camera.target={0,.2f,0};light.direction={-.3f,-1,-1};
    settings.environment.ssr.quality=SsrQuality::Low;settings.environment.ssr.maxRoughness=.6f;
    settings.environment.ssr.bias=.0005f;settings.environment.ssr.depthTolerance=.06f;
    settings.environment.ssr.enabled=false;const auto fixedWetOff=capture(decals);
    settings.environment.ssr.enabled=true;const auto fixedWetOn=capture(decals);
    instances[0].textures=&graphBindings;settings.environment.ssr.enabled=false;const auto graphWetOff=capture(decals);
    settings.environment.ssr.enabled=true;const auto graphWetOn=capture(decals);
    std::cout<<"Generated roughness-one wet SSR changed pixels: "<<changed(graphWetOff,graphWetOn,2)<<'\n';
    check(changed(graphWetOff,graphWetOn,2)>30,"Generated roughness decal did not activate visible SSR.");
    check(error(fixedWetOff,graphWetOff)<.25&&error(fixedWetOn,graphWetOn)<.35,
        "Fixed/generated wet SSR receiver composition diverged.");
    const auto dryOn=capture({});settings.environment.ssr.enabled=false;const auto dryOff=capture({});
    check(dryOn.rgba==dryOff.rgba,"Dry generated roughness-one reference unexpectedly entered SSR.");
    write(artifacts,"generated-wet-ssr-off",graphWetOff);write(artifacts,"generated-wet-ssr-on",graphWetOn);

    // Exercise the actual Material Editor receiver producers and preview pass.
    // Hand-authored fixture quads above cannot detect invalid preview bounds.
    renderer.resizeMaterialPreview(Width,Height);
    check(renderer.publishMaterialPreviewEnvironment(environment),renderer.environmentStatus());
    constexpr std::array previewKinds{MaterialPreviewMesh::Sphere,MaterialPreviewMesh::Plane,MaterialPreviewMesh::Cylinder,
        MaterialPreviewMesh::Cube,MaterialPreviewMesh::RoundedCube};
    constexpr std::array previewNames{"sphere","plane","cylinder","cube","rounded-cube"};
    for(std::size_t i=0;i<previewKinds.size();++i){
        DecalPreviewGeometry geometry;const DecalPreviewSurface* surface{};
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{60};
        do{tick();surface=&geometry.prepare(previewKinds[i],resources);
            if(!surface->patch)std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }while(!surface->patch&&std::chrono::steady_clock::now()<deadline);
        check(surface->model&&surface->patch,"Material preview chart failed: "+geometry.diagnostic());
        check(renderer.prepareModel(*surface->model),renderer.modelStatus());
        RenderModelInstance previewReceiver;previewReceiver.entityId=1;previewReceiver.resourceRevision=1;
        previewReceiver.model.asset=surface->model;previewReceiver.materials=&surface->model->materials;
        previewReceiver.receiveDecals=true;previewReceiver.forceFullDetail=true;previewReceiver.castShadows=false;
        const std::array previewInstances{previewReceiver};
        auto previewSettings=settings;previewSettings.camera=surface->camera;previewSettings.environment.enabled=true;
        previewSettings.environment.background.show=false;
        RenderScene previewScene{previewSettings,{}};previewScene.sceneMode=true;previewScene.instances=previewInstances;
        previewScene.directionalLights=std::span(&light,1);
        auto previewDecal=decal(surface->patch,red);previewDecal.frame=surface->frame;
        const std::array previewDecals{previewDecal};
        renderer.renderMaterialPreview(previewScene);const auto baseline=read(renderer.materialPreviewTexture());
        previewScene.decals=previewDecals;renderer.renderMaterialPreview(previewScene);
        const auto painted=read(renderer.materialPreviewTexture());
        const auto affected=changed(baseline,painted);
        write(artifacts,"material-preview-"+std::string(previewNames[i])+"-baseline",baseline);
        write(artifacts,"material-preview-"+std::string(previewNames[i])+"-decal",painted);
        std::cout<<"Material preview "<<previewNames[i]<<": changed pixels="<<affected<<'\n';
        check(affected>100,"Material preview "+std::string(previewNames[i])+" did not visibly render its decal.");
        renderer.renderMaterialPreview(previewScene);const auto repeated=read(renderer.materialPreviewTexture());
        check(repeated.rgba==painted.rgba,"Material preview decal changed between static frames.");
        previewSettings.environment.background.show=true;previewScene.decals={};
        renderer.renderMaterialPreview(previewScene);const auto backgroundBaseline=read(renderer.materialPreviewTexture());
        previewScene.decals=previewDecals;renderer.renderMaterialPreview(previewScene);
        const auto backgroundPainted=read(renderer.materialPreviewTexture());
        check(changed(backgroundBaseline,backgroundPainted)>100,"Showing the material preview background hid its decal.");
        check(changed(painted,backgroundPainted)>100,"Material preview background visibility did not affect its image.");
        check(error(painted,backgroundPainted,true)==0,"Material preview background visibility changed its opaque receiver.");
        write(artifacts,"material-preview-"+std::string(previewNames[i])+"-background",backgroundPainted);
    }
    resources.stop();renderer.drainPending(frame);
    std::cout<<"Generated twelve-texture PBR decal parity, normal-zero identity, completed refraction snapshot, transparent receiver rejection and wet SSR passed on "
        <<renderer.backendName()<<".\n";
}
}
int main(int argc,char** argv) {
    try {
        check(argc==5,"Pass shaderc, source/include directories and artifact root.");
        const auto cache=fs::temp_directory_path()/("crimson-decal-graph-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        struct Cleanup {
            fs::path path;
            ~Cleanup(){std::error_code ec;const auto target=fs::weakly_canonical(path,ec),owner=fs::weakly_canonical(fs::temp_directory_path(),ec);
                if(!ec&&target.parent_path()==owner&&target.filename().string().starts_with("crimson-decal-graph-"))fs::remove_all(target,ec);}
        } cleanup{cache};
        ShaderCompileConfig config{argv[1],argv[2],argv[3],cache};
        check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());struct Quit{~Quit(){SDL_Quit();}} quit;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Crimson generated decal checks",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(bool(window),SDL_GetError());exercise(window.get(),config,fs::path(argv[4])/graphicsBackendKey(selectedTestGraphicsBackend()));return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
