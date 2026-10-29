#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/RenderPassPlan.h"
#include "crimson-renderer/Picking.h"
#include "resources/ResourceManager.h"
#include "resources/ImageDecoder.h"
#include "resources/TextureStreamer.h"
#include <crimson/renderer/forward/MaterialGraphImageBake.h>
#include "assets/MaterialAsset.h"
#include "assets/ModelAsset.h"
#include "assets/ContentStorage.h"
#include <crimson/renderer/forward/MeshNormals.h>
#include <crimson/renderer/forward/ModelAnimation.h>
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
#include <functional>
#include <iostream>
#include <memory>
#include <map>
#include <limits>
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
struct Options {fs::path artifacts,heightSceneRoot;bool benchmark{};unsigned warmup{240},samples{600};};
std::uint32_t samplerFlags(const ModelSampler& sampler) {
    std::uint32_t flags{};
    if(sampler.wrapU==ModelWrap::Clamp)flags|=BGFX_SAMPLER_U_CLAMP;else if(sampler.wrapU==ModelWrap::Mirror)flags|=BGFX_SAMPLER_U_MIRROR;
    if(sampler.wrapV==ModelWrap::Clamp)flags|=BGFX_SAMPLER_V_CLAMP;else if(sampler.wrapV==ModelWrap::Mirror)flags|=BGFX_SAMPLER_V_MIRROR;
    if(sampler.minFilter==ModelFilter::Nearest||sampler.minFilter==ModelFilter::NearestMipmapNearest||sampler.minFilter==ModelFilter::NearestMipmapLinear)flags|=BGFX_SAMPLER_MIN_POINT;
    if(sampler.minFilter==ModelFilter::NearestMipmapNearest||sampler.minFilter==ModelFilter::LinearMipmapNearest)flags|=BGFX_SAMPLER_MIP_POINT;
    if(sampler.magFilter==ModelFilter::Nearest)flags|=BGFX_SAMPLER_MAG_POINT;return flags;
}
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
std::shared_ptr<ModelAsset> hardCube() {
    auto mesh=std::make_shared<ModelAsset>();mesh->id="mesh-shading-hard-cube";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    const std::array<Vec3,6> normals{{{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}}};
    const std::array<Vec3,6> axes{{{1,0,0},{-1,0,0},{0,0,-1},{0,0,1},{1,0,0},{1,0,0}}};
    for(unsigned face=0;face<normals.size();++face) {
        const auto normal=normals[face],uAxis=axes[face],vAxis=math::cross(normal,uAxis);
        const auto first=static_cast<std::uint32_t>(primitive.vertices.size());
        for(const Vec2 uv:std::array<Vec2,4>{{{0,0},{1,0},{1,1},{0,1}}}) {
            ModelVertex vertex;vertex.position=normal*.6f+uAxis*(1.2f*uv[0]-.6f)+vAxis*(1.2f*uv[1]-.6f);
            vertex.normal=normal;vertex.uv0={uv[0],1-uv[1]};vertex.uv1=vertex.uv0;vertex.tangent={uAxis[0],uAxis[1],uAxis[2],-1};
            primitive.vertices.push_back(vertex);math::include(mesh->bounds,vertex.position);
        }
        primitive.indices.insert(primitive.indices.end(),{first,first+1,first+2,first,first+2,first+3});
    }
    mesh->primitives.push_back(std::move(primitive));mesh->draws.push_back({0,{}});mesh->materials.emplace_back();return mesh;
}
std::shared_ptr<ModelAsset> thinQuadTriangle(unsigned orientation) {
    // Exact chart/triangle 311 from the staircase import, rotated onto XY and
    // recentered. It has a 16.93 edge-length ratio and near-parallel source
    // parameter directions. All three cyclic index orders describe one surface.
    constexpr std::array<Vec3,3> source{{{-1.129502773284912f,1.2027982473373413f,0},
        {-.32360678911209106f,.23511409759521484f,0},{-.27381885051727295f,.2915874421596527f,0}}};
    constexpr std::array<Vec2,3> uv{{{2.000000238418579f,1.742229700088501f},
        {3.250000238418579f,2.0527873039245605f},{3.250000238418579f,1.742229700088501f}}};
    constexpr std::array<Vec4,3> chart{{{0,1,0,1},{1,0,0,1},{1,1,1,1}}};
    const auto center=(source[0]+source[1]+source[2])*(1.f/3);
    auto mesh=std::make_shared<ModelAsset>();mesh->id="height-thin-quad-"+std::to_string(orientation);ModelPrimitive primitive;
    primitive.uvMask=3;primitive.tangentUv=0;
    for(unsigned i=0;i<source.size();++i) {
        ModelVertex vertex;vertex.position=source[i]-center;vertex.normal={0,0,1};vertex.tangent={1,0,0,-1};
        vertex.uv0=uv[i];vertex.uv1={uv[i][1]*.9f+.1f,uv[i][0]*1.1f-.2f};vertex.uvInterpolationCoordinates=chart[i];
        vertex.uvInterpolationCorrection={-3.559525563190391e-9f,-.7575758099555969f,4.449406620921081e-9f,-.2352709323167801f};
        vertex.uvInterpolationMode=2;primitive.vertices.push_back(vertex);math::include(mesh->bounds,vertex.position);
    }
    primitive.indices={orientation%3,(orientation+1)%3,(orientation+2)%3};mesh->primitives.push_back(std::move(primitive));
    mesh->draws.push_back({0,{}});mesh->materials.emplace_back();return mesh;
}
Vec3 debugNormal(const DecodedImage& image,unsigned x,unsigned y) {
    Vec3 result;const auto offset=(std::size_t(y)*image.width+x)*4;
    for(unsigned c=0;c<3;++c) {
        const double encoded=double(image.rgba[offset+c])/255;
        const double linear=encoded<=.04045?encoded/12.92:std::pow((encoded+.055)/1.055,2.4);
        result[c]=float(2*linear-1);
    }
    return math::normalized(result);
}
std::string normalText(const Vec3& value) {
    return "("+std::to_string(value[0])+","+std::to_string(value[1])+","+std::to_string(value[2])+")";
}
std::array<unsigned,2> projectProbe(const Camera& camera,const Vec3& point) {
    const auto projected=math::project(point,camera.projectionMatrix(1,ClipDepth::ZeroToOne)*camera.viewMatrix(),
        {0,0,Size,Size},ClipDepth::ZeroToOne);
    check(projected&&(*projected)[0]>=0&&(*projected)[0]<Size&&(*projected)[1]>=0&&(*projected)[1]<Size,"Mesh shading probe is not projectable.");
    return {unsigned((*projected)[0]),unsigned((*projected)[1])};
}
std::optional<Vec3> quadraticSurfacePoint(const Camera& camera,unsigned x,unsigned y,const Mat4& inverse,float amount) {
    const auto ray=camera.screenRay(float(x)+.5f,float(y)+.5f,{0,0,Size,Size});if(!ray)return {};
    const auto origin=math::transformPoint(inverse,ray->origin),direction=math::transformDirection(inverse,ray->direction);
    const double u=(origin[0]+.6)/1.2,du=direction[0]/1.2;
    const double a=amount*du*du,b=2*amount*u*du-direction[2],c=amount*(u*u-.5)-origin[2];
    double t=std::numeric_limits<double>::infinity();
    if(std::abs(a)<1e-12) {if(std::abs(b)>1e-12)t=-c/b;}
    else {
        const double discriminant=b*b-4*a*c;if(discriminant<0)return {};
        for(const double root:{(-b-std::sqrt(discriminant))/(2*a),(-b+std::sqrt(discriminant))/(2*a)})if(root>0)t=std::min(t,root);
    }
    if(!std::isfinite(t)||t<=0)return {};return origin+direction*float(t);
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
    const auto prepare=[&](const MaterialAsset& authored,bgfx::TextureHandle image=BGFX_INVALID_HANDLE,
        const std::function<bgfx::TextureHandle(const MaterialGraphTextureBinding&)>& textureLookup={}) {
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
            if(textureLookup) {
                packet->textures[source.slot]=textureLookup(source);packet->flags[source.slot]=samplerFlags(source.sampler);
                check(bgfx::isValid(packet->textures[source.slot]),"Diagnostic graph texture upload failed.");continue;
            }
            check(bgfx::isValid(image)&&source.role==MaterialGraphTextureRole::Data,"Height graph must bind its image as linear data.");
            packet->textures[source.slot]=image;packet->flags[source.slot]=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP;
        }
        return packet;
    };
    if(!options.heightSceneRoot.empty()) {
        check(bgfx::getRendererType()==bgfx::RendererType::Direct3D12,"Actual height diagnostic requires Direct3D 12.");
        check(!options.artifacts.empty(),"Actual height diagnostic requires --artifacts.");
        const auto root=fs::weakly_canonical(options.heightSceneRoot);
        (void)refreshContentStorage(root);
        check(contentStorageReady(root),"Cannot read the existing project location index without migration.");
        auto actual=loadMaterialAsset(root,"8bbc4fed-ae99-5080-a32b-e96dbe088c08");
        const auto manifest=root/".crimson/cache/quader-imports/a8695214-0c58-5bed-bd3b-77a26ce5f828/c27dc7a1-0e0b-55a0-ac6a-c0b2bccf8937/manifest.json";
        auto target=loadModelPackage(manifest,root,ModelResolvers{{},[&](const std::string&)->std::optional<ModelMaterial>{return actual.surface;}});
        check(renderer.prepareModel(*target),renderer.modelStatus());
        std::map<std::string,std::unique_ptr<Texture>> textures;nlohmann::json textureReport=nlohmann::json::array();
        const auto lookup=[&](const MaterialGraphTextureBinding& source) {
            const auto key=source.image.id+":"+std::to_string(static_cast<unsigned>(source.role));
            if(const auto found=textures.find(key);found!=textures.end())return found->second->handle;
            const auto file=resolveAssetPath(root,source.image.id);const auto decoded=decodeManagedImage(file);
            const auto role=source.role==MaterialGraphTextureRole::Color?streaming::TextureRole::Color:
                source.role==MaterialGraphTextureRole::Normal?streaming::TextureRole::Normal:streaming::TextureRole::Data;
            const auto mips=streaming::generateTextureMips(decoded.width,decoded.height,decoded.rgba,role);
            auto texture=std::make_unique<Texture>();texture->handle=bgfx::createTexture2D(static_cast<std::uint16_t>(decoded.width),
                static_cast<std::uint16_t>(decoded.height),true,1,bgfx::TextureFormat::RGBA8,
                source.role==MaterialGraphTextureRole::Color?BGFX_TEXTURE_SRGB:BGFX_TEXTURE_NONE);
            check(bgfx::isValid(texture->handle),"Actual diagnostic texture allocation failed.");
            for(unsigned level=0;level<mips.size();++level)bgfx::updateTexture2D(texture->handle,0,static_cast<std::uint8_t>(level),0,0,
                static_cast<std::uint16_t>(mips[level].width),static_cast<std::uint16_t>(mips[level].height),
                bgfx::copy(mips[level].rgba.data(),static_cast<std::uint32_t>(mips[level].rgba.size())));
            textureReport.push_back({{"id",source.image.id},{"file",file.string()},{"role",static_cast<unsigned>(source.role)},
                {"width",decoded.width},{"height",decoded.height},{"mip_levels",mips.size()}});
            const auto handle=texture->handle;textures.emplace(key,std::move(texture));return handle;
        };
        constexpr std::uint16_t Width=1024,Height=768;renderer.resizeGameViewport(Width,Height);renderer.resizeViewport(1,1);
        SceneSettings actualSettings;actualSettings.environment.enabled=true;actualSettings.environment.toneMapping=ToneMapping::Linear;
        actualSettings.environment.background.show=false;actualSettings.environment.background.overrideColor=true;actualSettings.environment.background.color={};
        actualSettings.camera.position={4.521776676f,2.161623955f,3.139229536f};actualSettings.camera.target={8.848543167f,-2.295485973f,10.090497971f};
        actualSettings.camera.up={.252654999f,.878300905f,.405897796f};actualSettings.camera.fov=45;
        actualSettings.camera.nearPlane=.000699424f;actualSettings.camera.farPlane=2048;actualSettings.camera.fovAxis=CameraFovAxis::Vertical;
        const auto closeup=actualSettings.camera;Camera overview=closeup;
        const auto world=math::translation({7.5f,.5f,6});const auto bounds=math::transformBounds(target->bounds,world);
        check(bounds.valid,"Actual height target has invalid bounds.");const auto center=(bounds.minimum+bounds.maximum)*.5f;
        overview.position=center+math::normalized(Vec3{1,1,-1})*float(std::max(2.,math::length(bounds.maximum-bounds.minimum)));
        overview.target=center;overview.up={0,1,0};overview.frameBounds(bounds.minimum,bounds.maximum,float(Width)/Height);
        std::vector<ModelMaterial> actualSurfaces(target->materials.size(),actual.surface);
        std::vector<ModelMaterialTextures> actualBindings(target->materials.size());
        RenderModelInstance instance;instance.entityId=501;instance.model={target,world};instance.materials=&actualSurfaces;
        instance.textures=&actualBindings;instance.forceFullDetail=true;instance.meshShading=MeshShading::Smooth;
        RenderScene actualScene{actualSettings,{}};actualScene.sceneMode=true;actualScene.instances=std::span(&instance,1);
        DirectionalLight sun;sun.direction=math::normalized(Vec3{-.45f,-1,-.3f});sun.intensity=2;sun.color={1,1,1};actualScene.directionalLights=std::span(&sun,1);
        for(const auto lod:{0.f,5.f}) {
            auto diagnostic=actual;materialOutputNode(diagnostic.graph).output.tessellation.heightLod=lod;
            const auto packet=prepare(diagnostic,BGFX_INVALID_HANDLE,lookup);for(auto& binding:actualBindings)binding.graph=packet;
            for(unsigned view=0;view<2;++view)for(const auto mode:{MeshShading::Flat,MeshShading::Smooth})for(const auto debug:{RenderDebugView::Normal,RenderDebugView::Lit}) {
                actualSettings.camera=view?closeup:overview;instance.meshShading=mode;renderer.setDebugView(RenderViewPurpose::Game,debug);
                renderer.renderGame(actualScene);check(renderer.modelStatus().empty(),renderer.modelStatus());const auto image=read(renderer.gameViewportTexture(),Width,Height);
                const auto name="actual-height-"+std::to_string(unsigned(lod))+"-"+(view?"close":"overview")+"-"+
                    (mode==MeshShading::Smooth?"smooth":"flat")+"-"+(debug==RenderDebugView::Normal?"normal":"lit")+".png";
                save(options.artifacts,name.c_str(),image);
            }
        }
        nlohmann::json report{{"manifest",manifest.string()},{"material",actual.id},{"name",actual.name},{"textures",textureReport},
            {"width",Width},{"height",Height},{"factor",materialOutputNode(actual.graph).output.tessellation.maxFactor},
            {"amount",materialOutputNode(actual.graph).output.tessellation.amount},{"scope","read-only target mesh/material diagnostic; authored source unchanged; real full production mip chains; diagnostic Height LOD 0 and 5 copies"}};
        fs::create_directories(options.artifacts);std::ofstream file(options.artifacts/"actual-height-diagnostic.json");file<<report.dump(2)<<'\n';check(bool(file),"Cannot write actual height diagnostic report.");
        actualScene.instances={};actualBindings.clear();resources.stop();frame=renderer.drainPending(frame);return;
    }
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
        if(!options.benchmark) {
            // Exercise the real rectangular RGBA16F upload path, then compare
            // native domain sampling against a separately uploaded RGBA32F image.
            // This catches byte-pitch/format bugs that a Noop stream cannot see.
            TextureStreamer resizeStreamer(resources,{});
            auto resizedMaterial=material(true);
            const auto sourceId=addMaterialNode(resizedMaterial.graph,MaterialNodeKind::Texture2D).id;
            auto* sourceNode=findMaterialNode(resizedMaterial.graph,sourceId);
            sourceNode->texture.role=MaterialTextureRole::Data;
            sourceNode->texture.image.id="assets/textures/resize-reference.png";
            const auto resizeId=addMaterialNode(resizedMaterial.graph,MaterialNodeKind::Resize).id;
            findMaterialNode(resizedMaterial.graph,resizeId)->resize={256,128,MaterialResizeFilter::Linear};
            connectMaterialNodes(resizedMaterial.graph,sourceId,MaterialPin::R,resizeId,MaterialPin::Left);
            connectMaterialNodes(resizedMaterial.graph,resizeId,MaterialPin::Value,
                materialOutputNode(resizedMaterial.graph).id,MaterialPin::Height);
            auto sourceImage=std::make_shared<DecodedImage>(DecodedImage{HeightEdge,HeightEdge,heightPixels});
            auto baked=std::make_shared<FloatImage>(bakeMaterialResizeImage(resizedMaterial.graph,resizeId,
                [sourceImage](const std::string&){return sourceImage;}));
            PreparedTexture streamed;
            const auto deadline=Clock::now()+std::chrono::seconds{30};
            do {
                resources.pump();resizeStreamer.beginFrame();
                streamed=resizeStreamer.requestGeneratedFloat("height-resize-upload-v1",
                    [baked]{return *baked;},256,128,true,256,ResourcePriority::Selected);
                resizeStreamer.advance(8192,10);tick();
                check(streamed.diagnostic.empty(),streamed.diagnostic);
                if(!streamed.complete)std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }while(!streamed.complete&&Clock::now()<deadline);
            check(streamed.complete&&bgfx::isValid(streamed.handle),"DX12 Resize float upload did not complete.");
            bindings[0].graph=prepare(resizedMaterial,streamed.handle);const auto halfResult=capture();
            Texture floatReference{bgfx::createTexture2D(256,128,false,1,bgfx::TextureFormat::RGBA32F,0,
                bgfx::copy(baked->rgba.data(),static_cast<std::uint32_t>(baked->rgba.size()*sizeof(float))))};
            check(bgfx::isValid(floatReference.handle),"Resize independent RGBA32F reference allocation failed.");
            bindings[0].graph=prepare(resizedMaterial,floatReference.handle);const auto fullResult=capture();
            check(imageRms(halfResult,fullResult)<2,"Streamed RGBA16F Resize Height differs from the full-float reference.");
            check(imageRms(halfResult,flat)>5,"Resize Height remained flat instead of sampling the baked image.");
            save(options.artifacts,"height-resize-rgba16f.png",halfResult);
            std::cout<<"DX12 Resize RGBA16F upload/domain reference RMS="<<imageRms(halfResult,fullResult)<<".\n";
            bindings[0].graph=prepare(authored,heightTexture.handle);
            streamed={};for(unsigned i=0;i<3;++i)tick();
        }
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

    nlohmann::json shadingChecks=nlohmann::json::object();
    {
        const auto savedSettings=settings;const auto savedFront=front;const auto savedGraph=bindings[0].graph;
        scene.instances=std::span(&front,1);scene.pointLights={};scene.directionalLights={};
        settings.camera.position={0,0,3};settings.camera.target={};
        renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Normal);
        renderer.setDebugView(RenderViewPurpose::Scene,RenderDebugView::Normal);
        front.shading=MaterialShading::StandardPbr;front.model.transform={};bindings[0].graph.reset();
        surfaces[0]=ModelMaterial{};surfaces[0].baseColor={1,1,1,1};surfaces[0].doubleSided=false;
        auto custom=plane();custom->id="mesh-shading-custom-normal-plane";
        const auto authoredNormal=math::normalized(Vec3{.4f,.25f,1});
        for(auto& vertex:custom->primitives[0].vertices)vertex.normal=authoredNormal;
        check(renderer.prepareModel(*custom),renderer.modelStatus());front.model.asset=custom;
        std::array<DecodedImage,3> ordinary;
        constexpr std::array ordinaryNames{"mesh-shading-ordinary-authored.png","mesh-shading-ordinary-flat.png","mesh-shading-ordinary-smooth.png"};
        for(unsigned mode=0;mode<ordinary.size();++mode) {
            front.meshShading=static_cast<MeshShading>(mode);ordinary[mode]=capture();
            const auto expected=mode?Vec3{0,0,1}:authoredNormal;
            const auto actual=debugNormal(ordinary[mode],Size/2,Size/2);
            save(options.artifacts,ordinaryNames[mode],ordinary[mode]);
            check(math::length(actual-expected)<.015f,
                "Ordinary mesh mode="+std::to_string(mode)+" did not select its normals: actual="+normalText(actual)+", expected="+normalText(expected));
        }
        check(imageRms(ordinary[0],ordinary[2])>5&&imageRms(ordinary[1],ordinary[2])<1,
            "Ordinary Smooth neither regenerated geometry normals nor agreed with a planar face.");
        std::array independent{front,front};independent[0].entityId=711;independent[1].entityId=712;
        independent[0].meshShading=MeshShading::UseMeshNormals;independent[1].meshShading=MeshShading::Smooth;
        independent[0].model.transform=math::translation({-.7f,0,0})*math::scaling({.8f,.8f,.8f});
        independent[1].model.transform=math::translation({.7f,0,0})*math::scaling({.8f,.8f,.8f});
        scene.instances=independent;const auto occurrences=capture();
        for(unsigned i=0;i<independent.size();++i) {
            const auto probe=projectProbe(settings.camera,{i?.7f:-.7f,0,0});
            check(math::length(debugNormal(occurrences,probe[0],probe[1])-(i?Vec3{0,0,1}:authoredNormal))<.015f,
                "Occurrences sharing one GPU mesh leaked their shading choice.");
            check(pick(probe[0],probe[1])==independent[i].entityId,"Per-mesh shading changed shared-asset selection identity.");
        }
        scene.instances=std::span(&front,1);
        {
            auto morphed=plane();morphed->id="mesh-shading-morphed-plane";morphed->draws[0].node=0;
            morphed->nodes.resize(1);morphed->nodes[0].mesh=0;constexpr float Slope=.65f;
            ModelMorphTarget morph;for(const auto& vertex:morphed->primitives[0].vertices)morph.positions.push_back({0,0,Slope*vertex.position[0]});
            morphed->primitives[0].morphTargets.push_back(std::move(morph));
            check(renderer.prepareModel(*morphed),renderer.modelStatus());front.model.asset=morphed;
            for(const float weight:{1.f,-.75f}) {
                auto pose=std::make_shared<ModelPose>();pose->localTransforms.resize(1);pose->worldTransforms.resize(1);
                pose->nodeWeights={{weight}};pose->skinPalettes.resize(1);pose->drawTransforms.resize(1);
                pose->bounds={{-.6f,-.6f,-.6f},{.6f,.6f,.6f},true};pose->signature=weight>0?1711:1712;front.pose=pose;
                const auto expected=math::normalized(Vec3{-Slope*weight,0,1});
                std::array<DecodedImage,3> images;
                for(unsigned mode=0;mode<images.size();++mode) {
                    front.meshShading=static_cast<MeshShading>(mode);images[mode]=capture();
                    const auto actual=debugNormal(images[mode],Size/2,Size/2),reference=mode?expected:Vec3{0,0,1};
                    const auto name="mesh-shading-morph-"+std::to_string(weight)+"-"+std::to_string(mode)+".png";
                    save(options.artifacts,name.c_str(),images[mode]);
                    check(math::length(actual-reference)<.015f,
                        "GPU morph normals mode="+std::to_string(mode)+" actual="+normalText(actual)+", expected="+normalText(reference));
                }
                check(imageRms(images[1],images[2])<1,"A planar morph disagreed between regenerated Smooth and face normals.");
                if(supported) {
                    auto neutral=material();auto& output=materialOutputNode(neutral.graph).output;
                    output.shading=MaterialShading::StandardPbr;output.doubleSided=false;
                    bindings[0].graph=prepare(neutral);front.meshShading=MeshShading::Smooth;
                    const auto neutralImage=capture();const auto name="mesh-shading-morph-neutral-"+std::to_string(weight)+".png";
                    save(options.artifacts,name.c_str(),neutralImage);
                    const auto before=debugNormal(images[2],Size/2,Size/2),after=debugNormal(neutralImage,Size/2,Size/2);
                    const auto rms=imageRms(images[2],neutralImage);
                    double interiorSquared{};unsigned interiorPixels{},silhouettePixels{};
                    for(std::size_t i=0;i<images[2].rgba.size();i+=4) {
                        const bool beforeCovered=images[2].rgba[i]>15||images[2].rgba[i+1]>15||images[2].rgba[i+2]>15;
                        const bool afterCovered=neutralImage.rgba[i]>15||neutralImage.rgba[i+1]>15||neutralImage.rgba[i+2]>15;
                        silhouettePixels+=beforeCovered!=afterCovered;
                    }
                    for(unsigned y=4;y+4<Size;++y)for(unsigned x=4;x+4<Size;++x) {
                        bool interior=true;
                        for(const int dy:{-3,0,3})for(const int dx:{-3,0,3}) {
                            const auto i=(std::size_t(int(y)+dy)*Size+unsigned(int(x)+dx))*4;
                            if(images[2].rgba[i+2]<128||neutralImage.rgba[i+2]<128)interior=false;
                        }
                        if(!interior)continue;++interiorPixels;const auto i=(std::size_t(y)*Size+x)*4;
                        for(unsigned c=0;c<3;++c){const double d=int(images[2].rgba[i+c])-int(neutralImage.rgba[i+c]);interiorSquared+=d*d;}
                    }
                    const double interiorRms=interiorPixels?std::sqrt(interiorSquared/(interiorPixels*3)):1000;
                    // Subdivision can move an outer edge by a pixel through
                    // floating-point raster setup. Test interior normals and
                    // bound coverage independently so that two edge pixels
                    // cannot masquerade as a changed shading normal.
                    check(interiorPixels>1500&&interiorRms<=1&&silhouettePixels<=8&&
                        math::length(before-expected)<.015f&&math::length(after-expected)<.015f,
                        "Neutral tessellation changed GPU-morphed Smooth normals: whole-image RGB RMS="+std::to_string(rms)+
                        ", interior RGB RMS="+std::to_string(interiorRms)+", interior pixels="+std::to_string(interiorPixels)+
                        ", silhouette pixels="+std::to_string(silhouettePixels)+
                        ", center before="+normalText(before)+", center after="+normalText(after)+", expected="+normalText(expected));
                    shadingChecks["neutral_morph"].push_back({{"weight",weight},{"whole_image_rgb_rms",rms},
                        {"interior_rgb_rms",interiorRms},{"interior_pixels",interiorPixels},{"silhouette_pixels",silhouettePixels}});
                    bindings[0].graph.reset();
                }
            }
            front.pose.reset();shadingChecks["current_gpu_morph_normals"]=true;
        }
        auto cube=hardCube();check(renderer.prepareModel(*cube),renderer.modelStatus());front.model.asset=cube;
        settings.camera.position={2,1.6f,3};settings.camera.target={};front.meshShading=MeshShading::UseMeshNormals;
        const auto cubeReference=capture();
        for(const auto mode:{MeshShading::Flat,MeshShading::Smooth}) {
            front.meshShading=mode;const auto image=capture();
            check(imageRms(cubeReference,image)<1,"Mesh shading rounded a cube's authored hard edges.");
            for(const Vec3 normal:std::array<Vec3,3>{{{0,0,1},{1,0,0},{0,1,0}}}) {
                const auto probe=projectProbe(settings.camera,normal*.6f);
                check(math::length(debugNormal(image,probe[0],probe[1])-normal)<.015f,
                    "A cube side acquired averaged cross-edge normals.");
                check(pick(probe[0],probe[1])==front.entityId,"A shading mode changed cube surface picking.");
            }
            if(mode==MeshShading::Smooth)save(options.artifacts,"mesh-shading-hard-cube.png",image);
        }
        if(supported) {
            auto neutral=material();auto& output=materialOutputNode(neutral.graph).output;
            output.shading=MaterialShading::StandardPbr;output.doubleSided=false;
            bindings[0].graph=prepare(neutral);front.meshShading=MeshShading::Smooth;
            check(imageRms(cubeReference,capture())<1,"Tessellated Smooth rounded the cube's independent hard-edge charts.");
            bindings[0].graph.reset();
        }
        shadingChecks["ordinary_custom_normals"]=true;shadingChecks["independent_same_asset_occurrences"]=true;
        shadingChecks["cube_hard_edges"]=true;

        if(supported) {
            // Exact independent analytic reference: z = amount * (u*u-.5),
            // u=(x+.6)/1.2. The 256x1 R32F map avoids byte quantization noise;
            // its bilinear reconstruction differs from the polynomial by less
            // than four millionths. No CPU helper from the shader is reused.
            constexpr unsigned PolynomialEdge=256;constexpr float Amount=.8f;
            std::array<float,PolynomialEdge> polynomial{};
            for(unsigned x=0;x<PolynomialEdge;++x){const float u=(float(x)+.5f)/PolynomialEdge;polynomial[x]=u*u;}
            Texture polynomialTexture{bgfx::createTexture2D(PolynomialEdge,1,false,1,bgfx::TextureFormat::R32F,0,
                bgfx::copy(polynomial.data(),sizeof(polynomial)))};
            check(bgfx::isValid(polynomialTexture.handle),"Analytic height texture allocation failed.");
            auto curved=material();curved.id="mesh-shading-analytic-height";
            auto& controls=materialOutputNode(curved.graph).output;controls.shading=MaterialShading::StandardPbr;
            controls.doubleSided=false;controls.tessellation.amount=Amount;controls.tessellation.maxFactor=8;
            const auto output=materialOutputNode(curved.graph).id;
            const auto texture=addMaterialNode(curved.graph,MaterialNodeKind::Texture2D).id;
            findMaterialNode(curved.graph,texture)->texture.role=MaterialTextureRole::Data;
            findMaterialNode(curved.graph,texture)->texture.image.id="assets/textures/analytic-height.png";
            connectMaterialNodes(curved.graph,texture,MaterialPin::R,output,MaterialPin::Height);
            bindings[0].graph=prepare(curved,polynomialTexture.handle);front.model.asset=mesh;
            settings.camera.position={0,0,3};settings.camera.target={};
            Mat4 shear;shear.at(0,1)=.25f;shear.at(2,1)=.15f;
            const std::array transforms{Mat4{},shear*math::scaling({1.15f,.85f,1.25f})};
            for(unsigned transformIndex=0;transformIndex<transforms.size();++transformIndex) {
                front.model.transform=transforms[transformIndex];
                const auto inverse=math::inverse(front.model.transform),normalMatrix=math::normalMatrix(front.model.transform);
                check(inverse&&normalMatrix,"Analytic shading fixture has a singular transform.");
                std::array<DecodedImage,3> images;
                for(unsigned mode=0;mode<images.size();++mode){front.meshShading=static_cast<MeshShading>(mode);images[mode]=capture();}
                for(unsigned mode=0;mode<images.size();++mode) {
                    const auto name="mesh-shading-curve-"+std::to_string(transformIndex)+"-"+std::to_string(mode)+".png";
                    save(options.artifacts,name.c_str(),images[mode]);
                }
                std::array<double,3> squared{};unsigned probes{};
                for(unsigned y=8;y+8<Size;y+=2)for(unsigned x=8;x+8<Size;x+=2) {
                    const auto point=quadraticSurfacePoint(settings.camera,x,y,*inverse,Amount);
                    if(!point||std::abs((*point)[0])>.48f||std::abs((*point)[1])>.48f)continue;
                    const float u=((*point)[0]+.6f)/1.2f;
                    const auto expected=math::normalized(math::transformDirection(*normalMatrix,Vec3{-2*Amount*u/1.2f,0,1}));
                    for(unsigned mode=0;mode<images.size();++mode) {
                        const auto delta=debugNormal(images[mode],x,y)-expected;squared[mode]+=math::dot(delta,delta);
                    }
                    ++probes;
                }
                check(probes>700,"Analytic curved-height oracle has insufficient interior coverage.");
                std::array<double,3> rms{};for(unsigned mode=0;mode<rms.size();++mode)rms[mode]=std::sqrt(squared[mode]/probes);
                check(rms[2]<.025&&rms[1]>.025&&rms[2]<rms[1]*.65,
                    "Smooth failed to remove tessellation faceting against analytic surface normals: Flat="+std::to_string(rms[1])+", Smooth="+std::to_string(rms[2]));
                shadingChecks[transformIndex?"curved_height_affine":"curved_height_identity"]={{"interior_probes",probes},
                    {"authored_normal_rms",rms[0]},{"flat_normal_rms",rms[1]},{"smooth_normal_rms",rms[2]}};
            }
            front.model.transform={};settings.camera.position={0,0,3};settings.camera.target={};
            renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Lit);
            renderer.setDebugView(RenderViewPurpose::Scene,RenderDebugView::Lit);
            materialOutputNode(curved.graph).output.shading=MaterialShading::Unlit;bindings[0].graph=prepare(curved,polynomialTexture.handle);
            front.shading=MaterialShading::Unlit;front.meshShading=MeshShading::UseMeshNormals;const auto silhouette=capture();
            const auto probe=projectProbe(settings.camera,{.25f,0,Amount*((.85f/1.2f)*(.85f/1.2f)-.5f)});
            for(const auto mode:{MeshShading::UseMeshNormals,MeshShading::Flat,MeshShading::Smooth}) {
                front.meshShading=mode;check(imageRms(silhouette,capture())<1,"Smooth/Flat changed displaced geometry or silhouette.");
                check(pick(probe[0],probe[1])==front.entityId&&pick(probe[0],probe[1],PickMode::MaterialSlot)==front.entityId,
                    "Smooth/Flat changed tessellated surface picking.");
                std::vector<ModelMaterial> occluderMaterial(1);occluderMaterial[0].baseColor={0,1,0,1};
                RenderModelInstance occluder=front;occluder.entityId=713;occluder.textures=nullptr;occluder.materials=&occluderMaterial;
                occluder.model.transform=math::translation({0,0,-.05f});occluder.meshShading=MeshShading::UseMeshNormals;
                std::array depthPair{front,occluder};scene.instances=depthPair;
                const auto depthImage=capture();const auto offset=(std::size_t(probe[1])*Size+probe[0])*4;
                check(depthImage.rgba[offset]>240&&depthImage.rgba[offset+1]>240&&depthImage.rgba[offset+2]>240&&
                    pick(probe[0],probe[1])==front.entityId,"Shading mode changed displaced depth against an occluder.");
                scene.instances=std::span(&front,1);
            }
        }
        if(supported&&!options.benchmark) {
            // Cyclic control-point order is a parameterization choice, not a
            // surface edit. A stable differential must yield the same normals
            // for this ill-conditioned source triangle under all three orders.
            // Two independent high-frequency analytic waves expose large-probe
            // filtering that a low-frequency quadratic cannot detect.
            constexpr unsigned Edge=1024;
            std::vector<float> pixels(Edge*Edge);
            for(unsigned y=0;y<Edge;++y)for(unsigned x=0;x<Edge;++x) {
                const double u=(double(x)+.5)/Edge,v=(double(y)+.5)/Edge;
                pixels[std::size_t(y)*Edge+x]=float(.5+.25*std::sin(Tau*(97*u+61*v))+.20*std::cos(Tau*(53*u-89*v)));
            }
            Texture waves{bgfx::createTexture2D(Edge,Edge,true,1,bgfx::TextureFormat::R32F,BGFX_TEXTURE_NONE)};
            check(bgfx::isValid(waves.handle),"Thin Quad analytic height allocation failed.");
            unsigned edge=Edge,mip{};
            while(true) {
                bgfx::updateTexture2D(waves.handle,0,static_cast<std::uint8_t>(mip),0,0,static_cast<std::uint16_t>(edge),static_cast<std::uint16_t>(edge),
                    bgfx::copy(pixels.data(),static_cast<std::uint32_t>(pixels.size()*sizeof(float))));
                if(edge==1)break;const unsigned next=edge/2;std::vector<float> reduced(next*next);
                for(unsigned y=0;y<next;++y)for(unsigned x=0;x<next;++x)
                    reduced[std::size_t(y)*next+x]=(pixels[std::size_t(2*y)*edge+2*x]+pixels[std::size_t(2*y)*edge+2*x+1]+
                        pixels[std::size_t(2*y+1)*edge+2*x]+pixels[std::size_t(2*y+1)*edge+2*x+1])*.25f;
                pixels=std::move(reduced);edge=next;++mip;
            }
            std::array<std::shared_ptr<ModelAsset>,3> triangles;
            for(unsigned i=0;i<triangles.size();++i){triangles[i]=thinQuadTriangle(i);check(renderer.prepareModel(*triangles[i]),renderer.modelStatus());}
            const auto e=math::normalized(triangles[0]->primitives[0].vertices[1].position-triangles[0]->primitives[0].vertices[0].position);
            const Vec3 perpendicular{-e[1],e[0],0};Mat4 stretch;
            for(unsigned row=0;row<3;++row)for(unsigned column=0;column<3;++column)
                stretch.at(row,column)=(row==column?1.f:0.f)+11*perpendicular[row]*perpendicular[column];
            stretch.at(2,2)=12;front.model.transform=stretch;front.meshShading=MeshShading::Smooth;front.shading=MaterialShading::StandardPbr;
            settings.camera.position={0,0,3};settings.camera.target={};scene.instances=std::span(&front,1);
            renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Normal);
            auto thin=material();auto& output=materialOutputNode(thin.graph).output;
            output.shading=MaterialShading::StandardPbr;output.doubleSided=false;output.tessellation.amount=.001f;output.tessellation.maxFactor=20;
            const auto outputId=materialOutputNode(thin.graph).id;
            const auto mapping=addMaterialNode(thin.graph,MaterialNodeKind::UV).id;
            const auto texture=addMaterialNode(thin.graph,MaterialNodeKind::Texture2D).id;
            findMaterialNode(thin.graph,texture)->texture.role=MaterialTextureRole::Data;
            findMaterialNode(thin.graph,texture)->texture.image.id="assets/textures/thin-quad-analytic-waves.png";
            connectMaterialNodes(thin.graph,mapping,MaterialPin::UV,texture,MaterialPin::UV);
            connectMaterialNodes(thin.graph,texture,MaterialPin::R,outputId,MaterialPin::Height);
            for(unsigned variant=0;variant<4;++variant) {
                auto& uv=findMaterialNode(thin.graph,mapping)->uv;uv={};
                uv.mode=variant==2?MaterialUvMode::UV1:MaterialUvMode::UV0;
                if(variant==1){uv.scale={.75f,1.3f,1};uv.rotation={0,0,23};uv.offset={.13f,-.27f,0};}
                materialOutputNode(thin.graph).output.tessellation.heightLod=variant==3?2.f:0.f;
                bindings[0].graph=prepare(thin,BGFX_INVALID_HANDLE,[&](const MaterialGraphTextureBinding&){return waves.handle;});
                std::array<DecodedImage,3> images;
                for(unsigned i=0;i<images.size();++i) {
                    front.model.asset=triangles[i];images[i]=capture();const auto name="height-thin-quad-"+std::to_string(variant)+"-order-"+std::to_string(i)+".png";
                    save(options.artifacts,name.c_str(),images[i]);
                }
                std::vector<double> angles;
                for(unsigned y=4;y+4<Size;++y)for(unsigned x=4;x+4<Size;++x) {
                    bool interior=true;
                    for(const auto& image:images)for(const int dy:{-3,0,3})for(const int dx:{-3,0,3}) {
                        const auto offset=(std::size_t(int(y)+dy)*Size+unsigned(int(x)+dx))*4;
                        if(image.rgba[offset]<15&&image.rgba[offset+1]<15&&image.rgba[offset+2]<15)interior=false;
                    }
                    if(!interior)continue;const auto reference=debugNormal(images[0],x,y);double error{};
                    for(unsigned i=1;i<images.size();++i)error=std::max(error,std::acos(std::clamp(math::dot(reference,debugNormal(images[i],x,y)),-1.,1.))*180/3.141592653589793);
                    angles.push_back(error);
                }
                check(angles.size()>1000,"Thin Quad source-order oracle has insufficient interior coverage.");
                std::sort(angles.begin(),angles.end());const double median=angles[angles.size()/2],p95=angles[std::size_t(.95*(angles.size()-1))];
                std::cout<<"Thin Quad Height source-order variant="<<variant<<" interior="<<angles.size()<<" median_degrees="<<median<<" p95_degrees="<<p95<<'\n';
                shadingChecks["thin_quad_gradient"].push_back({{"variant",variant},{"interior_pixels",angles.size()},{"median_angle_degrees",median},{"p95_angle_degrees",p95}});
                check(median<3&&p95<12,"Smooth Height gradients depend on thin Quad triangle control-point order: variant="+
                    std::to_string(variant)+", median="+std::to_string(median)+" degrees, p95="+std::to_string(p95)+" degrees.");
            }
        }
        settings=savedSettings;front=savedFront;bindings[0].graph=savedGraph;surfaces[0]=ModelMaterial{};
        scene.instances=std::span(&front,1);renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Lit);
        renderer.setDebugView(RenderViewPurpose::Scene,RenderDebugView::Lit);
    }

    if(options.benchmark) {
        check(supported,"Hardware tessellation benchmark requires Direct3D 12.");
        check(!options.artifacts.empty(),"Height benchmark requires an artifact directory.");
        renderer.resizeViewport(1,1);renderer.resizeGameViewport(1920,1080);auto benchmarkMesh=plane(16);
        benchmarkMesh->id="mesh-shading-benchmark-stream";
        auto level=std::make_shared<ModelGeometryLevel>();level->geometry=benchmarkMesh->primitives.front();
        auto descriptor=std::make_shared<streaming::ModelStream>();descriptor->id=benchmarkMesh->id;
        descriptor->key=streaming::model_primitive_content_key(level->geometry);
        streaming::PrimitiveLods primitive;primitive.bounds=benchmarkMesh->bounds;
        primitive.levels.push_back({{},descriptor->key,static_cast<std::uint32_t>(level->geometry.vertices.size()),
            static_cast<std::uint32_t>(level->geometry.indices.size()),0});descriptor->primitives.push_back(std::move(primitive));
        benchmarkMesh->stream=descriptor;const auto bytesBefore=renderer.modelResidentBytes();
        renderer.configureModelStream(benchmarkMesh->id,1,*descriptor);
        check(renderer.queueModelLod(benchmarkMesh->id,1,level),"Cannot queue shading benchmark geometry.");
        for(unsigned i=0;i<120&&!renderer.modelLodReady(benchmarkMesh->id,1,0,0);++i)tick();
        check(renderer.modelLodReady(benchmarkMesh->id,1,0,0),"Shading benchmark geometry upload timed out.");
        check(!renderer.modelLodReady(benchmarkMesh->id,1,0,0,true),"Default mesh upload eagerly generated Smooth normals.");
        const auto baseResidentBytes=renderer.modelResidentBytes()-bytesBefore;
        const auto queueStarted=Clock::now();check(renderer.queueModelLod(benchmarkMesh->id,1,level,true),"Cannot request lazy Smooth benchmark normals.");
        const double lazyQueueCpuMs=std::chrono::duration<double,std::milli>(Clock::now()-queueStarted).count();
        for(unsigned i=0;i<120&&!renderer.modelLodReady(benchmarkMesh->id,1,0,0,true);++i)tick();
        check(renderer.modelLodReady(benchmarkMesh->id,1,0,0,true),"Lazy Smooth normal upload timed out.");
        const auto smoothResidentBytes=renderer.modelResidentBytes()-bytesBefore;
        check(smoothResidentBytes-baseResidentBytes==level->geometry.vertices.size()*3*sizeof(float),
            "Lazy Smooth geometry did not account for exactly one 12-byte normal per vertex.");
        const auto retainedBytes=renderer.modelResidentBytes();
        check(renderer.queueModelLod(benchmarkMesh->id,1,level,true)&&renderer.modelResidentBytes()==retainedBytes,
            "Repeated Smooth requests duplicated the derived GPU stream.");
        std::vector<double> normalCpuMs;std::uint64_t normalChecksum{};
        for(unsigned repeat=0;repeat<116;++repeat) {
            const auto started=Clock::now();const auto normals=generate_smooth_mesh_normals(level->geometry);
            const auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
            for(const auto& normal:normals)normalChecksum+=static_cast<std::uint64_t>(std::lround((normal[2]+1)*1000000));
            check(normals.size()==level->geometry.vertices.size(),"CPU Smooth normals changed vertex count.");
            if(repeat>=16)normalCpuMs.push_back(elapsed);
        }
        std::sort(normalCpuMs.begin(),normalCpuMs.end());
        front.model.asset=benchmarkMesh;front.resourceRevision=1;front.shading=MaterialShading::StandardPbr;
        front.model.transform=math::scaling({2.4f,1.35f,1});scene.instances=std::span(&front,1);
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
        std::array<std::shared_ptr<PreparedGraphMaterial>,2> packets;
        for(unsigned i=0;i<packets.size();++i){
            auto& controls=materialOutputNode(benchmarkMaterial.graph).output.tessellation;
            controls.enabled=i!=0;controls.maxFactor=8;
            packets[i]=prepare(benchmarkMaterial,varyingHeight.handle);
        }
        const auto* capabilities=bgfx::getCaps();
        nlohmann::json report{{"schema",2},{"backend",bgfx::getRendererName(bgfx::getRendererType())},
            {"gpu_vendor_id",capabilities->vendorId},{"gpu_device_id",capabilities->deviceId},{"width",1920},{"height",1080},
            {"base_triangles",512},{"factor",8},{"height","64x64 linear RGBA8 data: 0.5+0.5*sin(2*pi*u)*sin(2*pi*v); amount 0.02; midpoint 0.5; bilinear LOD-0 sampling; Smooth adds height evaluations for differential normals"},
            {"height_texture_bytes",HeightEdge*HeightEdge*4},{"common_warmup_seconds",3},
            {"phase_order","ordinary-use,ordinary-flat,ordinary-smooth,tess-use,tess-flat,tess-smooth,tess-smooth,tess-flat,tess-use,ordinary-smooth,ordinary-flat,ordinary-use"},
            {"warmup",options.warmup},{"requested_samples",options.samples},
            {"geometry_residency",{{"base_bytes",baseResidentBytes},{"with_smooth_bytes",smoothResidentBytes},
                {"derived_bytes",smoothResidentBytes-baseResidentBytes},{"derived_stride",12},{"vertex_count",level->geometry.vertices.size()},
                {"lazy_first_request_cpu_ms",lazyQueueCpuMs},{"pure_normals_cpu_median_ms",normalCpuMs[normalCpuMs.size()/2]},
                {"pure_normals_cpu_p95_ms",normalCpuMs[94]},{"cpu_warmup",16},{"cpu_samples",normalCpuMs.size()},{"cpu_checksum",normalChecksum}}},
            {"correctness",shadingChecks},
            {"scope","Game opaque PBR pass with one directional and one point light; excludes presentation, uploads and shadow work; timestamp elapsed, not active GPU execution"},
            {"phases",nlohmann::json::array()}};
        // Exercise both directions after a sustained common warmup. Keep every
        // phase's raw samples: a cold first baseline must not imply a speedup.
        bindings[0].graph=packets.back();front.meshShading=MeshShading::Smooth;
        const auto settleUntil=Clock::now()+std::chrono::seconds{3};
        while(Clock::now()<settleUntil){renderer.renderGame(scene);tick();}
        constexpr std::array phaseOrder{0u,1u,2u,3u,4u,5u,5u,4u,3u,2u,1u,0u};
        constexpr std::array modeNames{"Use Mesh Normals","Flat","Smooth"};
        for(const auto i:phaseOrder) {
            bindings[0].graph=packets[i/3];front.meshShading=static_cast<MeshShading>(i%3);
            const auto submit=[&]{renderer.renderGame(scene);tick();};
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
            nlohmann::json phase{{"phase_index",report["phases"].size()},{"tessellation",i>=3},{"mode",modeNames[i%3]},
                {"factor",8},{"first_submitted_frame",firstSubmitted},{"submissions",submissions},{"unique_completed_frames",completed.size()},
                {"median_ms",percentile(.5)},{"p95_ms",percentile(.95)},{"samples",std::move(raw)}};
            std::cout<<"Mesh shading opaque PBR tessellation="<<(i>=3)<<" mode="<<modeNames[i%3]<<" median_ms="<<phase["median_ms"]<<" p95_ms="<<phase["p95_ms"]<<'\n';report["phases"].push_back(std::move(phase));
        }
        {
            // A fresh immutable pose each frame prevents deformation-output
            // reuse. All modes consume the same source, pose sequence, vertex
            // count and material; only Smooth adds the normal reconstruction.
            auto animated=plane(128);animated->id="mesh-shading-animated-benchmark";animated->draws[0].node=0;
            animated->nodes.resize(1);animated->nodes[0].mesh=0;ModelMorphTarget morph;
            for(const auto& vertex:animated->primitives[0].vertices) {
                const float u=vertex.uv0[0],v=vertex.uv0[1];
                morph.positions.push_back({0,0,.12f*std::sin(float(Tau)*2*u)*std::cos(float(Tau)*2*v)});
            }
            animated->primitives[0].morphTargets.push_back(std::move(morph));
            check(renderer.prepareModel(*animated),renderer.modelStatus());
            const auto topology=make_mesh_normal_topology(animated->primitives[0]);
            const auto groupCount=topology.groupOffsets.size()-1;
            const auto topologyBytes=(groupCount+1+topology.vertexGroups.size()+topology.groupOffsets.size()+
                topology.groupFaces.size()+topology.triangleVertices.size())*sizeof(std::uint32_t);
            bindings[0].graph=packets[0];front.model.asset=animated;front.resourceRevision=0;front.meshShading=MeshShading::Smooth;
            scene.pointLights={};scene.shadowInstances={};scene.shadowWorldId=0;scene.localLightMode=LocalLightMode::AllLightsReference;
            constexpr unsigned AnimatedInstances=16;
            std::array<RenderModelInstance,AnimatedInstances> animatedInstances;
            for(unsigned i=0;i<animatedInstances.size();++i) {
                animatedInstances[i]=front;animatedInstances[i].entityId=10001+i;
                animatedInstances[i].model.transform=math::translation({float(i%4)-1.5f,(float(i/4)-1.5f)*.56f,0})*
                    math::scaling({.72f,.42f,1});
            }
            scene.instances=animatedInstances;
            std::uint64_t poseSerial{};
            const auto submitAnimated=[&] {
                ++poseSerial;
                for(unsigned i=0;i<animatedInstances.size();++i) {
                    auto pose=std::make_shared<ModelPose>();pose->localTransforms.resize(1);pose->worldTransforms.resize(1);
                    pose->nodeWeights={{((poseSerial+i)&1)?.8f:.4f}};pose->skinPalettes.resize(1);pose->drawTransforms.resize(1);
                    pose->bounds={{-.6f,-.6f,-.12f},{.6f,.6f,.12f},true};pose->signature=(poseSerial<<8)|(i+1);
                    animatedInstances[i].pose=std::move(pose);
                }
                renderer.renderGame(scene);tick();
            };
            const auto settle=Clock::now()+std::chrono::seconds{3};while(Clock::now()<settle)submitAnimated();
            nlohmann::json animatedReport{{"vertices",animated->primitives[0].vertices.size()},
                {"triangles",animated->primitives[0].indices.size()/3},{"tessellation",false},{"morph_targets",1},
                {"instances",AnimatedInstances},{"total_vertices",AnimatedInstances*animated->primitives[0].vertices.size()},
                {"total_triangles",AnimatedInstances*animated->primitives[0].indices.size()/3},
                {"layout","4x4 visible grid; all occurrences share source geometry, each owns an independent fresh pose"},
                {"pose_sequence","new immutable pose per occurrence per frame; morph weight alternates 0.8/0.4"},
                {"smooth_topology_groups",groupCount},{"packed_gpu_topology_bytes",topologyBytes},
                {"smooth_output_bytes_per_pose",animated->primitives[0].vertices.size()*3*sizeof(float)},
                {"common_warmup_seconds",3},{"warmup",options.warmup},{"requested_samples",options.samples},
                {"phase_order","Use Mesh Normals,Flat,Smooth,Smooth,Flat,Use Mesh Normals"},
                {"compute_scope","EnvironmentComputeView elapsed: 16 deformation dispatches, plus 16 Smooth reconstruction dispatches in Smooth; includes command-list submission/queue gaps around the view boundary; not isolated kernel execution time; no point lights, procedural environment, shadows, GI/AO, external jobs or tessellation"},
                {"opaque_scope","GameSceneView elapsed for 16 opaque PBR draws; one directional light; excludes presentation; timestamp elapsed, not active GPU execution"},
                {"combined_scope","sum of coherent compute-view and opaque-view elapsed durations; excludes time outside those view intervals; not total GPU frame time"},
                {"global_scope","coherent bgfx gpuTimeEnd-gpuTimeBegin for the complete benchmark GPU frame, including presentation and queue/submission gaps; elapsed rather than isolated active execution; not an FPS prediction"},
                {"topology_scope","exact bytes of production packed member-offset/member/group-offset/incident-corner/triangle CSR arrays; shared across poses"},
                {"phases",nlohmann::json::array()}};
            for(const unsigned mode:std::array{0u,1u,2u,2u,1u,0u}) {
                for(auto& instance:animatedInstances)instance.meshShading=static_cast<MeshShading>(mode);
                for(unsigned w=0;w<options.warmup;++w)submitAnimated();const auto firstSubmitted=frame+1;
                std::set<std::uint32_t> completed;std::vector<double> opaque,compute,combined,global;nlohmann::json raw=nlohmann::json::array();
                unsigned minCalls=UINT32_MAX,maxCalls{},submissions{};const auto deadline=Clock::now()+std::chrono::minutes{2};
                while(opaque.size()<options.samples&&submissions<options.samples*5+128&&Clock::now()<deadline) {
                    submitAnimated();++submissions;const auto* stats=bgfx::getStats();
                    if(stats->gpuFrameNum<firstSubmitted||stats->gpuTimerFreq<=0||completed.contains(stats->gpuFrameNum))continue;
                    std::optional<double> opaqueMs,computeMs;
                    for(unsigned viewIndex=0;viewIndex<stats->numViews;++viewIndex) {
                        const auto& view=stats->viewStats[viewIndex];if(view.gpuFrameNum!=stats->gpuFrameNum||view.gpuTimeEnd<=view.gpuTimeBegin)continue;
                        const double ms=double(view.gpuTimeEnd-view.gpuTimeBegin)*1000./stats->gpuTimerFreq;
                        if(view.view==GameSceneView)opaqueMs=ms;else if(view.view==EnvironmentComputeView)computeMs=ms;
                    }
                    if(!opaqueMs||!computeMs)continue;
                    const auto expectedCalls=AnimatedInstances*(mode==2?2u:1u);
                    check(stats->numCompute==expectedCalls,"Animated benchmark did not dispatch every independent deformation/Smooth reconstruction.");
                    completed.insert(stats->gpuFrameNum);opaque.push_back(*opaqueMs);
                    compute.push_back(*computeMs);combined.push_back(*opaqueMs+*computeMs);
                    minCalls=std::min(minCalls,stats->numCompute);maxCalls=std::max(maxCalls,stats->numCompute);
                    nlohmann::json sample{{"gpu_frame",stats->gpuFrameNum},{"opaque_ms",*opaqueMs},{"compute_calls",stats->numCompute},
                        {"compute_ms",*computeMs},{"combined_views_ms",*opaqueMs+*computeMs}};
                    if(stats->gpuTimeEnd>stats->gpuTimeBegin) {
                        const double ms=double(stats->gpuTimeEnd-stats->gpuTimeBegin)*1000./stats->gpuTimerFreq;
                        global.push_back(ms);sample["gpu_frame_ms"]=ms;
                    }
                    raw.push_back(std::move(sample));
                }
                check(opaque.size()==options.samples&&compute.size()==options.samples,
                    "Could not collect positive coherent animated compute and opaque GPU samples.");
                const auto distribution=[](std::vector<double>& values) {
                    if(values.empty())return nlohmann::json(nullptr);std::sort(values.begin(),values.end());
                    const auto at=[&](double p){return values[std::min(values.size()-1,std::size_t(std::ceil(p*values.size())-1))];};
                    return nlohmann::json{{"median_ms",at(.5)},{"p95_ms",at(.95)},{"samples",values.size()}};
                };
                nlohmann::json phase{{"phase_index",animatedReport["phases"].size()},{"mode",modeNames[mode]},
                    {"first_submitted_frame",firstSubmitted},{"submissions",submissions},{"unique_completed_frames",completed.size()},
                    {"compute_calls_min",minCalls},{"compute_calls_max",maxCalls},{"compute_timing_available",compute.size()==options.samples},
                    {"gpu_frame_timing_available",global.size()==options.samples},
                    {"opaque",distribution(opaque)},{"compute",distribution(compute)},{"combined_views",distribution(combined)},
                    {"gpu_frame",distribution(global)},{"samples",std::move(raw)}};
                if(global.size()!=options.samples)phase["gpu_frame_timing_note"]="The backend did not expose a positive coherent full GPU frame interval for every sample.";
                std::cout<<"Animated mesh shading instances="<<AnimatedInstances<<" mode="<<modeNames[mode]<<" opaque="<<phase["opaque"]<<" compute="<<phase["compute"]<<" frame="<<phase["gpu_frame"]<<'\n';
                animatedReport["phases"].push_back(std::move(phase));
            }
            report["animated"]=std::move(animatedReport);front.pose.reset();scene.instances=std::span(&front,1);
        }
        fs::create_directories(options.artifacts);std::ofstream file(options.artifacts/"mesh-shading-benchmark.json");check(bool(file),"Cannot create shading GPU report.");file<<report.dump(2)<<'\n';file.flush();check(bool(file),"Cannot write shading GPU report.");
    }
    if(!options.artifacts.empty()){fs::create_directories(options.artifacts);std::ofstream file(options.artifacts/"mesh-shading-correctness.json");file<<shadingChecks.dump(2)<<'\n';check(bool(file),"Cannot write mesh shading correctness report.");}
    bindings[0].graph.reset();scene.instances={};resources.stop();frame=renderer.drainPending(frame);
}
}
int main(int argc,char** argv) {
    try {
        check(argc>=4,"Pass shaderc, shader source and shader include directories, optionally --artifacts path or --benchmark path.");
        Options options;for(int i=4;i<argc;++i) {
            const std::string arg=argv[i];check(i+1<argc,"Height option requires a value.");
            if(arg=="--artifacts")options.artifacts=argv[++i];else if(arg=="--benchmark"){options.benchmark=true;options.artifacts=argv[++i];}
            else if(arg=="--height-scene-root")options.heightSceneRoot=argv[++i];
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
        std::cout<<(options.heightSceneRoot.empty()?"Material height perspective silhouette, depth/picking, preview, shadow geometry and source edits passed.\n":
            "Actual target mesh/material height diagnostic captures completed without authored changes.\n");return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
