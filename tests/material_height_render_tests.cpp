#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/RenderPassPlan.h"
#include "crimson-renderer/Picking.h"
#include "resources/ResourceManager.h"
#include "resources/ImageDecoder.h"
#include "assets/MaterialAsset.h"
#include "GpuTestBackend.h"
#include <SDL3/SDL.h>
#include <lodepng.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
constexpr std::uint16_t Size=256;
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Texture {
    bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};
    ~Texture(){if(bgfx::isValid(handle))bgfx::destroy(handle);}
};
struct Options {fs::path artifacts;bool benchmark{};unsigned warmup{240},samples{600};};
std::shared_ptr<ModelAsset> plane(unsigned divisions=1) {
    auto mesh=std::make_shared<ModelAsset>();mesh->id="height-plane-"+std::to_string(divisions);
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    for(unsigned y=0;y<=divisions;++y)for(unsigned x=0;x<=divisions;++x) {
        const float u=float(x)/divisions,v=float(y)/divisions;
        ModelVertex vertex;vertex.position={1.2f*u-.6f,1.2f*v-.6f,0};vertex.normal={0,0,1};
        vertex.uv0={u,1-v};vertex.uv1=vertex.uv0;vertex.tangent={1,0,0,-1};
        primitive.vertices.push_back(vertex);math::include(mesh->bounds,vertex.position);
    }
    for(unsigned y=0;y<divisions;++y)for(unsigned x=0;x<divisions;++x) {
        const auto a=y*(divisions+1)+x,b=a+1,c=b+divisions+1,d=a+divisions+1;
        primitive.indices.insert(primitive.indices.end(),{a,b,c,a,c,d});
    }
    mesh->primitives.push_back(std::move(primitive));mesh->draws.push_back({0,{}});mesh->materials.emplace_back();return mesh;
}
std::shared_ptr<ModelAsset> smoothSphere() {
    auto mesh=std::make_shared<ModelAsset>();mesh->id="height-smooth-normal-sphere";
    ModelPrimitive primitive;primitive.uvMask=3;
    constexpr unsigned Rings=12,Segments=24;constexpr double Pi=3.141592653589793;
    for(unsigned y=0;y<=Rings;++y)for(unsigned x=0;x<=Segments;++x) {
        const double theta=Pi*y/Rings,phi=2*Pi*x/Segments;
        ModelVertex vertex;vertex.normal={float(std::sin(theta)*std::cos(phi)),float(std::cos(theta)),float(std::sin(theta)*std::sin(phi))};
        vertex.position=vertex.normal*.6f;vertex.uv0={float(x)/Segments,float(y)/Rings};vertex.uv1=vertex.uv0;
        vertex.tangent={float(-std::sin(phi)),0,float(std::cos(phi)),1};primitive.vertices.push_back(vertex);math::include(mesh->bounds,vertex.position);
    }
    for(unsigned y=0;y<Rings;++y)for(unsigned x=0;x<Segments;++x) {
        const auto a=y*(Segments+1)+x,b=a+1,c=b+Segments+1,d=a+Segments+1;
        primitive.indices.insert(primitive.indices.end(),{a,b,c,a,c,d});
    }
    mesh->primitives.push_back(std::move(primitive));mesh->draws.push_back({0,{}});mesh->materials.emplace_back();return mesh;
}
MaterialAsset material(bool enabled=true) {
    MaterialAsset asset;asset.id="height-gpu-material";asset.name="Height GPU contract";
    auto& output=addMaterialNode(asset.graph,MaterialNodeKind::Output).output;
    output.shading=MaterialShading::Unlit;output.baseColor={1,1,1};output.doubleSided=true;
    output.tessellation.enabled=enabled;output.tessellation.amount=.8f;output.tessellation.maxFactor=8;
    return asset;
}
unsigned litPixels(const DecodedImage& image) {
    unsigned count{};for(std::size_t i=0;i<image.rgba.size();i+=4)if(image.rgba[i]>128)++count;return count;
}
void save(const fs::path& directory,const char* name,const DecodedImage& image) {
    if(directory.empty())return;fs::create_directories(directory);std::vector<unsigned char> bytes;
    check(lodepng::encode(bytes,image.rgba,image.width,image.height)==0,"Cannot encode height capture.");
    std::ofstream file(directory/name,std::ios::binary);check(bool(file),"Cannot create height capture.");
    file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));check(bool(file),"Cannot write height capture.");
}
double imageRms(const DecodedImage& a,const DecodedImage& b) {
    check(a.width==b.width&&a.height==b.height,"Height comparison dimensions differ.");double squared{};
    for(std::size_t i=0;i<a.rgba.size();i+=4)for(unsigned c=0;c<3;++c){const double d=int(a.rgba[i+c])-int(b.rgba[i+c]);squared+=d*d;}
    return std::sqrt(squared/(std::size_t(a.width)*a.height*3));
}
std::optional<Vec3> intersect(const Camera& camera,unsigned x,unsigned y,float z) {
    const auto ray=camera.screenRay(float(x)+.5f,float(y)+.5f,{0,0,Size,Size});
    if(!ray||std::abs(ray->direction[2])<1e-6f)return {};
    const float distance=(z-ray->origin[2])/ray->direction[2];
    if(distance<=0)return {};return ray->origin+ray->direction*distance;
}
bool inside(const std::optional<Vec3>& point,float inset) {
    return point&&std::abs((*point)[0])<.6f-inset&&std::abs((*point)[1])<.6f-inset;
}

void exercise(SDL_Window* window,const ShaderCompileConfig& config,const Options& options) {
    // Pending destination storage survives Renderer destruction on exceptions.
    std::vector<std::shared_ptr<DecodedImage>> destinations;
    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));
    MaterialGraphPrograms programs(resources,config);TestGpuContext context;
    renderer.resizeViewport(Size,Size);renderer.resizeGameViewport(Size,Size);renderer.setGridVisible(false);
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x484549474854;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.1f,.1f,.1f})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    check(renderer.publishMaterialPreviewEnvironment(environment),"Cannot publish height material-preview environment.");
    ViewportAppearance appearance;appearance.backgroundSrgb={};renderer.setAppearance(appearance);
    bgfx::reset(320,240,BGFX_RESET_NONE);bgfx::setDebug(BGFX_DEBUG_PROFILER);
    std::uint32_t frame{};std::uint64_t pickSerial{};
    const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=renderer.advance_frame();context.flush();};
    const auto read=[&](bgfx::TextureHandle source,std::uint16_t width=Size,std::uint16_t height=Size) {
        Texture staging{bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.handle),"Height readback allocation failed.");
        auto image=std::make_shared<DecodedImage>(DecodedImage{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4)});destinations.push_back(image);
        bgfx::blit(DiagnosticReadbackView,staging.handle,0,0,source,0,0,width,height);
        const auto ready=bgfx::readTexture(staging.handle,image->rgba.data());check(ready!=UINT32_MAX,"Height readback unsupported.");
        for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return *image;}
        throw std::runtime_error("Height readback timed out.");
    };
    const auto prepare=[&](const MaterialAsset& authored,bgfx::TextureHandle image=BGFX_INVALID_HANDLE) {
        const auto generated=generateMaterialGraphShader(authored);MaterialGraphProgramResult published;
        const auto deadline=Clock::now()+std::chrono::seconds{120};
        do {
            tick();published=programs.request(generated);
            if(!published.current&&!published.compiling)throw std::runtime_error(published.diagnostic);
            if(!published.current||!published.programs||!published.programs->optimized)std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }while((!published.current||!published.programs||!published.programs->optimized)&&Clock::now()<deadline);
        check(published.current&&published.programs&&published.programs->optimized,"Height material shader publication timed out.");
        auto packet=std::make_shared<PreparedGraphMaterial>();packet->programs=published.programs;packet->uniforms=generated.uniforms;packet->sources=generated.textures;
        packet->runtimeGraph=generated.runtimeGraph;packet->runtimeParameters=generated.runtimeParameters;
        packet->shading=materialOutputNode(authored.graph).output.shading;packet->blend=materialOutputNode(authored.graph).output.blend;
        packet->receiveFog=materialOutputNode(authored.graph).output.receiveFog;packet->tessellation=generated.tessellation;
        packet->dynamicCoverage=generated.dynamicCoverage;packet->dynamicHeight=generated.dynamicHeight;packet->signature=published.programs->signature;
        for(const auto& source:generated.textures) {
            check(bgfx::isValid(image)&&source.role==MaterialGraphTextureRole::Data,"Height graph must bind its image as linear data.");
            packet->textures[source.slot]=image;packet->flags[source.slot]=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP;
        }
        return packet;
    };
    auto mesh=plane();check(renderer.prepareModel(*mesh),renderer.modelStatus());
    std::vector<ModelMaterial> surfaces(1);std::vector<ModelMaterialTextures> bindings(1);
    RenderModelInstance front;front.entityId=501;front.model.asset=mesh;front.materials=&surfaces;front.textures=&bindings;
    front.shading=MaterialShading::Unlit;front.forceFullDetail=true;
    SceneSettings settings;settings.camera.position={1.8f,1.1f,3};settings.camera.target={0,0,.1f};settings.camera.fov=45;
    settings.camera.fovAxis=CameraFovAxis::Vertical;settings.camera.nearPlane=.05f;settings.camera.farPlane=20;
    settings.environment.enabled=false;settings.environment.toneMapping=ToneMapping::Linear;settings.environment.background.overrideColor=true;
    settings.environment.background.color={};settings.environment.background.show=false;
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=std::span(&front,1);
    const auto capture=[&](bool game=true) {
        if(game)renderer.renderGame(scene);else renderer.render(scene);
        check(renderer.modelStatus().empty(),renderer.modelStatus());return read(game?renderer.gameViewportTexture():renderer.viewportTexture());
    };
    const auto pick=[&](unsigned x,unsigned y,PickMode mode=PickMode::Selection) {
        check(renderer.requestPick({1,1,++pickSerial,x,y,renderer.pickResourceSignature(mode),mode}),renderer.pickingStatus());
        renderer.render(scene);std::optional<PickResult> result;
        for(unsigned i=0;i<120&&!result;++i){tick();result=renderer.pollPick(frame);}
        check(bool(result),"Height selection readback timed out.");return result->entityId;
    };
    auto authored=material(false);bindings[0].graph=prepare(authored);const auto flat=capture();
    const auto outputId=materialOutputNode(authored.graph).id;materialOutputNode(authored.graph).output.tessellation.enabled=true;
    const auto height=addMaterialNode(authored.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(authored.graph,height)->texture.role=MaterialTextureRole::Data;findMaterialNode(authored.graph,height)->texture.image.id="assets/textures/gpu-height.png";
    const auto multiply=addMaterialNode(authored.graph,MaterialNodeKind::Multiply).id;findMaterialNode(authored.graph,multiply)->right.value={2,2,2,2};
    connectMaterialNodes(authored.graph,height,MaterialPin::RGB,multiply,MaterialPin::Left);
    connectMaterialNodes(authored.graph,multiply,MaterialPin::Value,outputId,MaterialPin::Height);
    constexpr std::array<std::uint8_t,4> white{255,0,0,255};
    Texture heightTexture{bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(white.data(),4))};
    check(bgfx::isValid(heightTexture.handle),"Linear height texture allocation failed.");
    // Interior peaks cannot be reconstructed by displacing the four original
    // corners: all corner samples remain close to the neutral height 0.5.
    constexpr unsigned HeightEdge=64;
    std::vector<std::uint8_t> heightPixels(HeightEdge*HeightEdge*4);
    constexpr double Tau=6.283185307179586;
    for(unsigned y=0;y<HeightEdge;++y)for(unsigned x=0;x<HeightEdge;++x) {
        const double u=(double(x)+.5)/HeightEdge,v=(double(y)+.5)/HeightEdge;
        const auto value=static_cast<std::uint8_t>(std::lround(255*(.5+.5*std::sin(Tau*u)*std::sin(Tau*v))));
        const auto offset=(y*HeightEdge+x)*4;
        heightPixels[offset]=heightPixels[offset+1]=heightPixels[offset+2]=value; heightPixels[offset+3]=255;
    }
    Texture varyingHeight{bgfx::createTexture2D(HeightEdge,HeightEdge,false,1,bgfx::TextureFormat::RGBA8,0,
        bgfx::copy(heightPixels.data(),static_cast<std::uint32_t>(heightPixels.size())))};
    check(bgfx::isValid(varyingHeight.handle),"Varying linear height texture allocation failed.");
    bindings[0].graph=prepare(authored,heightTexture.handle);const auto displaced=capture();
    const bool supported=bgfx::getRendererType()==bgfx::RendererType::Direct3D12;
    if(!supported) {
        check(imageRms(flat,displaced)<=1,"Unsupported backend did not retain an ordinary material surface.");
        check(materialOutputNode(authored.graph).output.tessellation.enabled,"Unsupported backend rewrote authored tessellation.");
        std::cout<<"DX11 material height fallback preserves the ordinary surface and authored settings.\n";
    } else {
        check(imageRms(capture(false),displaced)<=1,"Scene and Game disagree on material displacement.");
        auto singleSided=material(false);materialOutputNode(singleSided.graph).output.doubleSided=false;
        canonicalizeMaterialAsset(singleSided);surfaces[0]=singleSided.surface;bindings[0].graph=prepare(singleSided);
        const auto singleFlat=capture();check(litPixels(singleFlat)>1000,"Single-sided reference plane was culled.");
        materialOutputNode(singleSided.graph).output.tessellation.enabled=true;bindings[0].graph=prepare(singleSided);
        check(imageRms(singleFlat,capture())<=1,"Neutral tessellation changed single-sided winding/culling.");
        bindings[0].graph=prepare(authored,heightTexture.handle);surfaces[0]=ModelMaterial{};
        unsigned checked{},exclusive{};std::optional<std::array<unsigned,2>> displacedOnly;
        for(unsigned y=8;y+8<Size;y+=3)for(unsigned x=8;x+8<Size;x+=3) {
            const auto movedPoint=intersect(settings.camera,x,y,.4f),flatPoint=intersect(settings.camera,x,y,0);
            const bool whiteInterior=inside(movedPoint,.025f),outside=!inside(movedPoint,-.025f);
            if(!whiteInterior&&!outside)continue;
            const bool pixel=displaced.rgba[(std::size_t(y)*Size+x)*4]>128;
            check(pixel==whiteInterior,"Material displacement silhouette differs from the analytic perspective surface.");++checked;
            if(whiteInterior&&!inside(flatPoint,-.025f)){++exclusive;displacedOnly=std::array{x,y};}
        }
        check(checked>1000&&exclusive>8&&displacedOnly.has_value(),"Height silhouette oracle has insufficient independent coverage.");
        check(pick((*displacedOnly)[0],(*displacedOnly)[1])==front.entityId&&pick((*displacedOnly)[0],(*displacedOnly)[1],PickMode::MaterialSlot)==front.entityId,
            "Selection or material-slot picking used the undisplaced silhouette.");
        save(options.artifacts,"height-flat.png",flat);save(options.artifacts,"height-displaced.png",displaced);

        std::vector<ModelMaterial> rearSurfaces(1);rearSurfaces[0].baseColor={0,1,0,1};rearSurfaces[0].doubleSided=true;
        RenderModelInstance rear=front;rear.entityId=502;rear.materials=&rearSurfaces;rear.textures=nullptr;rear.model.transform=math::translation({0,0,.2f});
        std::array pair{front,rear};scene.instances=pair;
        const auto center=math::project(Vec3{0,0,.4f},settings.camera.projectionMatrix(1,ClipDepth::ZeroToOne)*settings.camera.viewMatrix(),{0,0,Size,Size},ClipDepth::ZeroToOne);
        check(bool(center),"Displacement depth probe is not projectable.");const unsigned x=unsigned((*center)[0]),y=unsigned((*center)[1]);
        const auto colorAt=[&](const DecodedImage& image){const auto i=(std::size_t(y)*Size+x)*4;return std::array{image.rgba[i],image.rgba[i+1],image.rgba[i+2]};};
        const auto nearColor=colorAt(capture());check(nearColor[0]>240&&nearColor[1]>240&&pick(x,y)==front.entityId,"Displaced material did not win color/depth/picking against the nearer undisplaced reference.");
        const auto programsBefore=bindings[0].graph->programs;materialOutputNode(authored.graph).output.tessellation.amount=-.8f;
        bindings[0].graph=prepare(authored,heightTexture.handle);check(bindings[0].graph->programs==programsBefore,"Amount edit recompiled material topology.");
        const auto signedColor=colorAt(capture());check(signedColor[0]<10&&signedColor[1]>240&&pick(x,y)==rear.entityId,"Signed displacement did not move behind the depth reference.");
        materialOutputNode(authored.graph).output.tessellation.amount=.8f;findMaterialNode(authored.graph,multiply)->right.value={.25f,.25f,.25f,.25f};
        bindings[0].graph=prepare(authored,heightTexture.handle);check(bindings[0].graph->programs==programsBefore,"Height arithmetic value edit recompiled material topology.");
        check(colorAt(capture())[0]<10&&pick(x,y)==rear.entityId,"Updated Height arithmetic retained the previous displaced geometry.");
        materialOutputNode(authored.graph).output.tessellation.enabled=false;bindings[0].graph=prepare(authored,heightTexture.handle);
        scene.instances=std::span(&front,1);check(imageRms(capture(),flat)<=1,"Disabling tessellation did not restore the ordinary scene material.");

        auto varying=material();
        auto& varyingOutput=materialOutputNode(varying.graph).output;
        varyingOutput.tessellation.amount=.4f;varyingOutput.tessellation.maxFactor=16;
        const auto varyingOutputId=materialOutputNode(varying.graph).id;
        const auto varyingTexture=addMaterialNode(varying.graph,MaterialNodeKind::Texture2D).id;
        findMaterialNode(varying.graph,varyingTexture)->texture.role=MaterialTextureRole::Data;
        findMaterialNode(varying.graph,varyingTexture)->texture.image.id="assets/textures/varying-height.png";
        connectMaterialNodes(varying.graph,varyingTexture,MaterialPin::R,varyingOutputId,MaterialPin::Height);
        const auto originalCamera=settings.camera;
        settings.camera.position={0,0,3};settings.camera.target={};
        pair[1].model.transform=math::translation({0,0,.025f});
        const auto verifyInterior=[&](bool mirrorUv1) {
            bindings[0].graph=prepare(varying,varyingHeight.handle);
            pair[0]=front;scene.instances=pair;
            const auto image=capture();
            unsigned peaks{},troughs{};
            for(float py:{-.3f,.3f})for(float px:{-.3f,.3f}) {
                // UV0 is ((x+.6)/1.2, 1-(y+.6)/1.2). Quarter-UV
                // peaks/troughs are far from the height-zero crossing.
                const bool raised=((px<0)==(py>0))!=mirrorUv1;
                const auto probe=math::project(Vec3{px,py,raised?.2f:.025f},
                    settings.camera.projectionMatrix(1,ClipDepth::ZeroToOne)*settings.camera.viewMatrix(),
                    {0,0,Size,Size},ClipDepth::ZeroToOne);
                check(bool(probe),"Varying height interior probe failed perspective projection.");
                const unsigned probeX=unsigned((*probe)[0]),probeY=unsigned((*probe)[1]);
                const auto offset=(std::size_t(probeY)*Size+probeX)*4;
                const bool whitePixel=image.rgba[offset]>230&&image.rgba[offset+1]>230&&image.rgba[offset+2]>230;
                const bool greenPixel=image.rgba[offset]<10&&image.rgba[offset+1]>230&&image.rgba[offset+2]<10;
                check(raised?whitePixel:greenPixel,"Subdivision did not follow varying linear height at an interior UV peak/trough.");
                const auto expected=raised?front.entityId:pair[1].entityId;
                check(pick(probeX,probeY)==expected&&pick(probeX,probeY,PickMode::MaterialSlot)==expected,
                    "Picking did not follow the subdivided interior height surface.");
                if(raised)++peaks;else ++troughs;
            }
            check(peaks==2&&troughs==2,"Varying height oracle lost an independent positive or negative quadrant.");
            save(options.artifacts,mirrorUv1?"height-varying-uv1.png":"height-varying-uv0.png",image);
        };
        verifyInterior(false);
        auto uv1Mesh=plane();uv1Mesh->id="height-mirrored-uv1";
        for(auto& vertex:uv1Mesh->primitives[0].vertices)vertex.uv1={1-vertex.uv0[0],vertex.uv0[1]};
        check(renderer.prepareModel(*uv1Mesh),renderer.modelStatus());front.model.asset=uv1Mesh;
        const auto uv1=addMaterialNode(varying.graph,MaterialNodeKind::UV).id;
        findMaterialNode(varying.graph,uv1)->uv.mode=MaterialUvMode::UV1;
        connectMaterialNodes(varying.graph,uv1,MaterialPin::UV,varyingTexture,MaterialPin::UV);
        verifyInterior(true);
        front.model.asset=mesh;settings.camera=originalCamera;scene.instances=std::span(&front,1);

        // Dedicated material-preview geometry consumes the same authored packet.
        auto preview=material();materialOutputNode(preview.graph).output.tessellation.amount=.6f;canonicalizeMaterialAsset(preview);
        ModelMaterialTextures previewBindings;previewBindings.graph=prepare(preview);renderer.resizeMaterialPreview(Size,Size);
        SceneSettings previewSettings=settings;previewSettings.camera.position={0,0,3.2f};previewSettings.camera.target={};
        const auto previewCapture=[&]{renderer.renderMaterialPreview(previewSettings,preview,previewBindings,MaterialPreviewMesh::Sphere,{},false);return read(renderer.materialPreviewTexture());};
        for(unsigned i=0;i<8;++i){renderer.renderMaterialPreview(previewSettings,preview,previewBindings,MaterialPreviewMesh::Sphere,{},false);tick();}
        const auto neutralPreview=previewCapture();materialOutputNode(preview.graph).output.height=1;canonicalizeMaterialAsset(preview);previewBindings.graph=prepare(preview);const auto raisedPreview=previewCapture();
        check(litPixels(neutralPreview)>1000&&litPixels(raisedPreview)>litPixels(neutralPreview)*1.15,"Material preview did not displace its sphere silhouette.");
        save(options.artifacts,"height-preview.png",raisedPreview);renderer.releaseMaterialPreview();

        {
            const auto previousSettings=settings;
            auto smooth=smoothSphere();check(renderer.prepareModel(*smooth),renderer.modelStatus());
            auto lit=material(false);auto& litOutput=materialOutputNode(lit.graph).output;
            litOutput.shading=MaterialShading::StandardPbr;litOutput.baseColor={.45f,.35f,.25f};litOutput.roughness=.6f;litOutput.doubleSided=false;
            canonicalizeMaterialAsset(lit);std::vector<ModelMaterial> litSurfaces{lit.surface};
            front.model.asset=smooth;front.model.transform=math::scaling({1.15f,.85f,1});front.materials=&litSurfaces;front.shading=MaterialShading::StandardPbr;
            settings.camera.position={1.1f,.8f,3};settings.camera.target={};
            DirectionalLight light;light.direction=math::normalized(Vec3{-.4f,-.6f,-1});light.color={1,1,1};light.intensity=2;scene.directionalLights=std::span(&light,1);
            bindings[0].graph=prepare(lit);const auto smoothReference=capture();save(options.artifacts,"height-smooth-reference.png",smoothReference);
            for(const bool zeroAmount:{false,true}) {
                auto& controls=materialOutputNode(lit.graph).output;controls.tessellation.enabled=true;
                controls.height=zeroAmount?1.f:.5f;controls.tessellation.amount=zeroAmount?0.f:.8f;
                bindings[0].graph=prepare(lit);const auto neutral=capture();
                save(options.artifacts,zeroAmount?"height-smooth-zero-amount.png":"height-smooth-midpoint.png",neutral);
                double squared{};unsigned pixels{},maximum{};
                for(unsigned y=3;y+3<Size;++y)for(unsigned x=3;x+3<Size;++x) {
                    bool interior=true;
                    for(int dy=-3;dy<=3;dy+=3)for(int dx=-3;dx<=3;dx+=3) {
                        const auto offset=(std::size_t(int(y)+dy)*Size+unsigned(int(x)+dx))*4;
                        if(smoothReference.rgba[offset]<15||neutral.rgba[offset]<15)interior=false;
                    }
                    if(!interior)continue;++pixels;const auto offset=(std::size_t(y)*Size+x)*4;
                    for(unsigned channel=0;channel<3;++channel) {
                        const unsigned error=unsigned(std::abs(int(smoothReference.rgba[offset+channel])-int(neutral.rgba[offset+channel])));
                        squared+=double(error)*error;maximum=std::max(maximum,error);
                    }
                }
                const double rms=pixels?std::sqrt(squared/(pixels*3)):1000;
                check(pixels>1500&&rms<=1&&maximum<=8,"Neutral height changed smooth lit interior normals: RMS="+std::to_string(rms)+", max="+std::to_string(maximum));
            }
            settings=previousSettings;scene.directionalLights={};front.model.asset=mesh;front.model.transform={};front.materials=&surfaces;front.shading=MaterialShading::Unlit;
        }

        // A constant height has an exact geometric reference: translating the
        // original caster. This tests both actual shadow shader families.
        SceneSettings shadowSettings=settings;shadowSettings.camera.position={0,0,4};shadowSettings.camera.target={};
        std::vector<ModelMaterial> receiverSurface(1);receiverSurface[0].baseColor={.6f,.6f,.6f,1};receiverSurface[0].roughness=1;receiverSurface[0].doubleSided=true;
        RenderModelInstance receiver=front;receiver.entityId=601;receiver.shading=MaterialShading::StandardPbr;receiver.materials=&receiverSurface;receiver.textures=nullptr;receiver.castShadows=false;
        receiver.model.transform=math::scaling({2.5f,2.5f,1});
        auto casterMaterial=material();materialOutputNode(casterMaterial.graph).output.height=1;bindings[0].graph=prepare(casterMaterial);
        RenderModelInstance caster=front;caster.entityId=602;caster.model.transform=math::translation({0,0,1})*math::scaling({.4f,.4f,1});
        RenderScene shadowScene{shadowSettings,{}};shadowScene.sceneMode=true;shadowScene.instances=std::span(&receiver,1);shadowScene.shadowInstances=std::span(&caster,1);shadowScene.shadowResolution=512;
        PointLight point;point.entityId=603;point.position={.9f,.5f,3};point.color={1,1,1};point.intensity=30;point.radius=10;point.castShadows=true;point.shadowNormalBias=0;point.shadowDepthBias=.001f;
        DirectionalLight sun;sun.entityId=604;sun.direction=math::normalized(Vec3{-.5f,-.1f,-1});sun.color={1,1,1};sun.intensity=2;sun.castShadows=true;sun.shadowDistance=10;sun.shadowNormalBias=0;sun.shadowDepthBias=.001f;sun.shadowAngularDiameter=0;
        for(const bool directional:{false,true}) {
            shadowScene.pointLights=directional?std::span<const PointLight>{}:std::span(&point,1);
            shadowScene.directionalLights=directional?std::span(&sun,1):std::span<const DirectionalLight>{};
            PointShadowStats recordedStats;
            const std::string shadowKind=directional?"Directional":"Point";
            const auto shadowCapture=[&]{shadowScene.shadowWorldId++;renderer.renderGame(shadowScene);check(renderer.shadowStatus().empty(),renderer.shadowStatus());recordedStats=renderer.shadowStats();return read(renderer.gameViewportTexture());};
            caster.castShadows=false;const auto clear=shadowCapture();caster.castShadows=true;const auto gpuShadow=shadowCapture();
            check(directional?recordedStats.directionalCascades>0:recordedStats.renderedFaces>0,shadowKind+" displaced shadow stage recorded no shadow work.");
            caster.textures=nullptr;caster.model.transform=math::translation({0,0,1.4f})*math::scaling({.4f,.4f,1});const auto reference=shadowCapture();
            check(imageRms(clear,gpuShadow)>2,"Displaced caster produced no visible shadow.");
            check(imageRms(gpuShadow,reference)<2,"Displaced shadow geometry differs from its exact translated reference.");
            save(options.artifacts,directional?"height-directional-shadow.png":"height-point-shadow.png",gpuShadow);
            caster.textures=&bindings;caster.model.transform=math::translation({0,0,1})*math::scaling({.4f,.4f,1});
        }
    }

    if(options.benchmark) {
        check(supported,"Hardware tessellation benchmark requires Direct3D 12.");
        check(!options.artifacts.empty(),"Height benchmark requires an artifact directory.");
        renderer.resizeViewport(1,1);renderer.resizeGameViewport(1920,1080);auto benchmarkMesh=plane(16);check(renderer.prepareModel(*benchmarkMesh),renderer.modelStatus());
        front.model.asset=benchmarkMesh;front.shading=MaterialShading::StandardPbr;front.model.transform=math::scaling({2.4f,1.35f,1});scene.instances=std::span(&front,1);
        settings.camera.position={0,0,3};settings.camera.target={};
        DirectionalLight sun;sun.direction={0,0,-1};sun.color={1,1,1};sun.intensity=2;scene.directionalLights=std::span(&sun,1);
        PointLight point;point.position={1,1,2};point.color={.6f,.8f,1};point.intensity=10;point.radius=8;scene.pointLights=std::span(&point,1);
        auto benchmarkMaterial=material();auto& output=materialOutputNode(benchmarkMaterial.graph).output;
        output.shading=MaterialShading::StandardPbr;output.baseColor={.4f,.3f,.2f};output.roughness=.65f;output.tessellation.amount=.02f;
        const auto benchmarkOutput=materialOutputNode(benchmarkMaterial.graph).id;
        const auto benchmarkTexture=addMaterialNode(benchmarkMaterial.graph,MaterialNodeKind::Texture2D).id;
        findMaterialNode(benchmarkMaterial.graph,benchmarkTexture)->texture.role=MaterialTextureRole::Data;
        findMaterialNode(benchmarkMaterial.graph,benchmarkTexture)->texture.image.id="assets/textures/benchmark-height.png";
        const auto benchmarkMapping=addMaterialNode(benchmarkMaterial.graph,MaterialNodeKind::UV).id;
        connectMaterialNodes(benchmarkMaterial.graph,benchmarkMapping,MaterialPin::UV,benchmarkTexture,MaterialPin::UV);
        connectMaterialNodes(benchmarkMaterial.graph,benchmarkTexture,MaterialPin::R,benchmarkOutput,MaterialPin::Height);
        std::array<std::shared_ptr<PreparedGraphMaterial>,4> packets;
        constexpr std::array factors{0u,4u,8u,16u};
        for(unsigned i=0;i<factors.size();++i){
            auto& controls=materialOutputNode(benchmarkMaterial.graph).output.tessellation;
            controls.enabled=factors[i]!=0;controls.maxFactor=std::max(1u,factors[i]);
            packets[i]=prepare(benchmarkMaterial,varyingHeight.handle);
        }
        const auto* capabilities=bgfx::getCaps();
        nlohmann::json report{{"schema",1},{"backend",bgfx::getRendererName(bgfx::getRendererType())},
            {"gpu_vendor_id",capabilities->vendorId},{"gpu_device_id",capabilities->deviceId},{"width",1920},{"height",1080},
            {"base_triangles",512},{"height","64x64 linear RGBA8 data: 0.5+0.5*sin(2*pi*u)*sin(2*pi*v); amount 0.02; midpoint 0.5; one bilinear SampleLevel LOD-0 fetch per domain invocation"},
            {"height_texture_bytes",HeightEdge*HeightEdge*4},{"common_warmup_seconds",3},
            {"phase_order","off,4,8,16,16,8,4,off"},{"warmup",options.warmup},{"requested_samples",options.samples},
            {"scope","Game opaque PBR pass with one directional and one point light; excludes presentation, uploads and shadow work; timestamp elapsed, not active GPU execution"},
            {"phases",nlohmann::json::array()}};
        // Exercise both directions after a sustained common warmup. Keep every
        // phase's raw samples: a cold first baseline must not imply a speedup.
        bindings[0].graph=packets.back();
        const auto settleUntil=Clock::now()+std::chrono::seconds{3};
        while(Clock::now()<settleUntil){renderer.renderGame(scene);tick();}
        constexpr std::array phaseOrder{0u,1u,2u,3u,3u,2u,1u,0u};
        for(const auto i:phaseOrder) {
            bindings[0].graph=packets[i];const auto submit=[&]{renderer.renderGame(scene);tick();};
            for(unsigned w=0;w<options.warmup;++w)submit();const auto firstSubmitted=frame+1;
            std::set<std::uint32_t> completed;std::vector<double> timings;nlohmann::json raw=nlohmann::json::array();
            const auto deadline=Clock::now()+std::chrono::minutes{2};unsigned submissions{};
            while(timings.size()<options.samples&&submissions<options.samples*5+128&&Clock::now()<deadline) {
                submit();++submissions;const auto* stats=bgfx::getStats();
                if(stats->gpuFrameNum<firstSubmitted||stats->gpuTimerFreq<=0||completed.contains(stats->gpuFrameNum))continue;
                std::optional<double> milliseconds;
                for(unsigned viewIndex=0;viewIndex<stats->numViews;++viewIndex) {
                    const auto& view=stats->viewStats[viewIndex];
                    if(view.view==GameSceneView&&view.gpuFrameNum==stats->gpuFrameNum&&view.gpuTimeEnd>view.gpuTimeBegin)
                        milliseconds=double(view.gpuTimeEnd-view.gpuTimeBegin)*1000./stats->gpuTimerFreq;
                }
                if(!milliseconds)continue;check(stats->numDraw>0,"Height benchmark did not submit its rich material draw.");
                completed.insert(stats->gpuFrameNum);timings.push_back(*milliseconds);raw.push_back({{"gpu_frame",stats->gpuFrameNum},{"opaque_ms",*milliseconds}});
            }
            check(timings.size()==options.samples,"Could not collect unique coherent post-warmup height GPU samples.");
            std::sort(timings.begin(),timings.end());const auto percentile=[&](double p){return timings[std::min(timings.size()-1,std::size_t(std::ceil(p*timings.size())-1))];};
            nlohmann::json phase{{"phase_index",report["phases"].size()},{"factor",factors[i]},{"first_submitted_frame",firstSubmitted},{"submissions",submissions},{"unique_completed_frames",completed.size()},
                {"median_ms",percentile(.5)},{"p95_ms",percentile(.95)},{"samples",std::move(raw)}};
            std::cout<<"Height opaque PBR factor="<<factors[i]<<" median_ms="<<phase["median_ms"]<<" p95_ms="<<phase["p95_ms"]<<'\n';report["phases"].push_back(std::move(phase));
        }
        fs::create_directories(options.artifacts);std::ofstream file(options.artifacts/"material-height-benchmark.json");check(bool(file),"Cannot create height GPU report.");file<<report.dump(2)<<'\n';file.flush();check(bool(file),"Cannot write height GPU report.");
    }
    bindings[0].graph.reset();scene.instances={};resources.stop();frame=renderer.drainPending(frame);
}
}
int main(int argc,char** argv) {
    try {
        check(argc>=4,"Pass shaderc, shader source and shader include directories, optionally --artifacts path or --benchmark path.");
        Options options;for(int i=4;i<argc;++i) {
            const std::string arg=argv[i];check(i+1<argc,"Height option requires a value.");
            if(arg=="--artifacts")options.artifacts=argv[++i];else if(arg=="--benchmark"){options.benchmark=true;options.artifacts=argv[++i];}
            else if(arg=="--warmup")options.warmup=std::stoul(argv[++i]);else if(arg=="--samples")options.samples=std::stoul(argv[++i]);else throw std::runtime_error("Unknown height option: "+arg);
        }
        check(options.warmup>=32&&options.samples>0,"Height timing requires at least 32 warmup frames and a positive sample count.");
        const auto temporary=fs::temp_directory_path()/("crimson-height-"+std::to_string(Clock::now().time_since_epoch().count()));
        struct Cleanup {fs::path path;~Cleanup(){std::error_code error;const auto resolved=fs::weakly_canonical(path,error),parent=fs::weakly_canonical(fs::temp_directory_path(),error);
            if(!error&&resolved.parent_path()==parent&&resolved.filename().string().starts_with("crimson-height-"))fs::remove_all(resolved,error);}} cleanup{temporary};
        ShaderCompileConfig config{argv[1],argv[2],argv[3],temporary};
        check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());struct Quit{~Quit(){SDL_Quit();}} quit;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson material height GPU checks",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(bool(window),SDL_GetError());exercise(window.get(),config,options);
        std::cout<<"Material height perspective silhouette, depth/picking, preview, shadow geometry and source edits passed.\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
