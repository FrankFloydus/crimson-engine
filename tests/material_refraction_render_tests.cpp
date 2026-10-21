#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/RenderPassPlan.h"
#include "crimson-renderer/Picking.h"
#include "assets/MaterialAsset.h"
#include "resources/ResourceManager.h"
#include "GpuTestBackend.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
constexpr std::uint16_t Width=257,Height=193;
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Texture {bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};~Texture(){if(bgfx::isValid(handle))bgfx::destroy(handle);}};
std::shared_ptr<ModelAsset> plane(){
    auto result=std::make_shared<ModelAsset>();result->id="refraction-contract-plane";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    constexpr std::array<Vec3,4> points{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
    constexpr std::array<Vec2,4> uv{{{0,1},{1,1},{1,0},{0,0}}};
    for(unsigned i=0;i<4;++i){ModelVertex vertex;vertex.position=points[i];vertex.normal={.5f,0,.8660254f};
        vertex.tangent={.8660254f,0,-.5f,1};vertex.uv0=vertex.uv1=uv[i];primitive.vertices.push_back(vertex);math::include(result->bounds,points[i]);}
    primitive.indices={0,1,2,0,2,3};result->primitives.push_back(std::move(primitive));result->draws.push_back({0,{}});result->materials.emplace_back();return result;
}
double difference(const DecodedImage& a,const DecodedImage& b){
    double total{};unsigned count{};
    for(unsigned y=Height/3;y<Height*2/3;++y)for(unsigned x=Width/3;x<Width*2/3;++x)for(unsigned c=0;c<3;++c){
        const auto i=(std::size_t(y)*Width+x)*4+c;total+=std::abs(int(a.rgba[i])-int(b.rgba[i]));++count;}
    return total/count;
}
double variation(const DecodedImage& image){
    double total{};unsigned count{};
    for(unsigned y=Height/3;y<Height*2/3;++y)for(unsigned x=Width/3;x<Width*2/3-1;++x)for(unsigned c=0;c<3;++c){
        const auto i=(std::size_t(y)*Width+x)*4+c;total+=std::abs(int(image.rgba[i])-int(image.rgba[i+4]));++count;}
    return total/count;
}
Vec3 average(const DecodedImage& image){
    Vec3 result{};float count{};
    for(unsigned y=Height/3;y<Height*2/3;++y)for(unsigned x=Width/3;x<Width*2/3;++x){
        const auto i=(std::size_t(y)*Width+x)*4;for(unsigned c=0;c<3;++c)result[c]+=image.rgba[i+c];++count;}
    return result/count;
}
Vec3 centerRgb(const DecodedImage& image){
    Vec3 result{};
    for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x){
        const auto i=(std::size_t(int(Height/2)+y)*Width+int(Width/2)+x)*4;
        for(unsigned c=0;c<3;++c)result[c]+=image.rgba[i+c];
    }
    return result/49;
}
double patchDifference(const DecodedImage& a,const DecodedImage& b,const Camera& camera,Vec3 position){
    const auto point=math::project(position,camera.projectionMatrix(float(Width)/Height,ClipDepth::ZeroToOne)*camera.viewMatrix(),
        {0,0,float(Width),float(Height)},ClipDepth::ZeroToOne);
    check(bool(point),"Refraction fixture patch cannot be projected.");const int px=int((*point)[0]),py=int((*point)[1]);
    check(px>4&&py>4&&px<int(Width)-5&&py<int(Height)-5,"Refraction fixture patch escaped the viewport.");
    double total{};
    for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x)for(unsigned c=0;c<3;++c){
        const auto i=(std::size_t(py+y)*Width+px+x)*4+c;total+=std::abs(int(a.rgba[i])-int(b.rgba[i]));
    }
    return total/(49*3);
}
double patchMean(const DecodedImage& image,const Camera& camera,Vec3 position){
    const auto point=math::project(position,camera.projectionMatrix(float(Width)/Height,ClipDepth::ZeroToOne)*camera.viewMatrix(),
        {0,0,float(Width),float(Height)},ClipDepth::ZeroToOne);
    check(bool(point),"Refraction brightness patch cannot be projected.");const int px=int((*point)[0]),py=int((*point)[1]);
    check(px>4&&py>4&&px<int(Width)-5&&py<int(Height)-5,"Refraction brightness patch escaped the viewport.");
    double total{};for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x)for(unsigned c=0;c<3;++c)total+=image.rgba[(std::size_t(py+y)*Width+px+x)*4+c];
    return total/(49*3);
}
double coverageMidpointError(const DecodedImage& half,const DecodedImage& absent,const DecodedImage& full){
    const auto decode=[](double value){value/=255;return value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4);};
    const auto encode=[](double value){return 255*(value<=.0031308?12.92*value:1.055*std::pow(value,1/2.4)-.055);};
    double total{};unsigned count{};
    for(unsigned y=Height/3;y<Height*2/3;++y)for(unsigned x=Width/3;x<Width*2/3;++x)for(unsigned c=0;c<3;++c){
        const auto i=(std::size_t(y)*Width+x)*4+c;total+=std::abs(half.rgba[i]-encode((decode(absent.rgba[i])+decode(full.rgba[i]))*.5));++count;
    }
    return total/count;
}
void exercise(SDL_Window* window,const ShaderCompileConfig& config){
    std::vector<std::shared_ptr<DecodedImage>> destinations;
    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));
    MaterialGraphPrograms programs(resources,config);TestGpuContext context;
    bgfx::reset(320,240,BGFX_RESET_NONE);renderer.resizeViewport(Width,Height);renderer.resizeGameViewport(Width,Height);renderer.setGridVisible(false);
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x52454652414354;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.5f,.5f,.5f})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());check(renderer.publishMaterialPreviewEnvironment(environment),"Refraction preview environment failed.");
    check(renderer.publishThumbnailEnvironment(environment),renderer.thumbnailStatus());
    std::uint32_t frame{};
    const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=renderer.advance_frame();context.flush();};
    const auto read=[&](bgfx::TextureHandle source){
        Texture staging{bgfx::createTexture2D(Width,Height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.handle),"Refraction readback allocation failed.");
        auto result=std::make_shared<DecodedImage>(DecodedImage{Width,Height,std::vector<std::uint8_t>(std::size_t(Width)*Height*4)});destinations.push_back(result);
        bgfx::blit(DiagnosticReadbackView,staging.handle,0,0,source,0,0,Width,Height);
        const auto ready=bgfx::readTexture(staging.handle,result->rgba.data());check(ready!=UINT32_MAX,"Refraction readback unsupported.");
        for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return *result;}
        throw std::runtime_error("Refraction readback timeout.");
    };
    MaterialAsset asset;asset.id="refraction-gpu-contract";asset.name="Refraction GPU contract";asset.graph.nodes.reserve(16);
    const auto outputId=addMaterialNode(asset.graph,MaterialNodeKind::Output).id;
    const auto output=[&]() -> MaterialOutputNode& {return findMaterialNode(asset.graph,outputId)->output;};
    output().shading=MaterialShading::StandardPbr;output().blend=MaterialBlend::Alpha;output().doubleSided=true;output().refraction=true;
    output().ior=1;output().roughness=0;output().baseColor={.3f,.3f,.3f};
    const auto prepare=[&](bgfx::TextureHandle texture=BGFX_INVALID_HANDLE){
        const auto generated=generateMaterialGraphShader(asset);MaterialGraphProgramResult published;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{180};
        do{tick();published=programs.request(generated);if(!published.current&&!published.compiling)throw std::runtime_error(published.diagnostic);
            if(!published.current)std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }while(!published.current&&std::chrono::steady_clock::now()<deadline);
        check(published.current&&published.programs,"Refraction program publication timed out: "+published.diagnostic);
        auto packet=std::make_shared<PreparedGraphMaterial>();packet->programs=published.programs;packet->uniforms=generated.uniforms;packet->sources=generated.textures;
        packet->runtimeGraph=generated.runtimeGraph;packet->runtimeParameters=generated.runtimeParameters;packet->shading=output().shading;packet->blend=output().blend;
        packet->receiveFog=output().receiveFog;packet->refraction=generated.refraction;packet->tessellation=generated.tessellation;packet->signature=published.programs->signature;
        for(const auto& source:generated.textures){packet->textures[source.slot]=texture;packet->flags[source.slot]=
            BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;}
        return packet;
    };
    const auto mesh=plane();check(renderer.prepareModel(*mesh),renderer.modelStatus());
    constexpr unsigned Stripes=40;
    std::array<std::vector<ModelMaterial>,Stripes> backgrounds;
    std::vector<ModelMaterial> surface(1);std::vector<ModelMaterialTextures> bindings(1);
    std::array<RenderModelInstance,Stripes+1> instances;
    for(unsigned i=0;i<Stripes;++i){
        backgrounds[i].resize(1);backgrounds[i][0].baseColor=i%2?Vec4{.1f,.35f,.6f,1}:Vec4{.65f,.35f,.1f,1};backgrounds[i][0].doubleSided=true;
        auto& instance=instances[i];instance.entityId=i+1;instance.model={mesh,math::translation({-2.f+(i+.5f)*4.f/Stripes,0,-1})*math::scaling({2.f/Stripes,2,1})};
        instance.materials=&backgrounds[i];instance.shading=MaterialShading::Unlit;instance.forceFullDetail=true;
    }
    auto& glass=instances.back();glass.entityId=100;glass.model={mesh,math::scaling({.8f,.8f,1})};glass.materials=&surface;glass.textures=&bindings;glass.forceFullDetail=true;
    SceneSettings settings;settings.camera.position={0,0,3};settings.camera.target={0,0,0};settings.camera.fov=50;settings.camera.fovAxis=CameraFovAxis::Vertical;
    settings.camera.nearPlane=.05f;settings.camera.farPlane=20;settings.environment.enabled=false;settings.environment.background.show=false;
    settings.environment.toneMapping=ToneMapping::Linear;settings.antiAliasing=AntiAliasing::Off;
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=std::span(instances.data(),Stripes);
    const auto capture=[&](bool game=true){if(game)renderer.renderGame(scene);else renderer.render(scene);
        check(renderer.refractionStatus().empty(),renderer.refractionStatus());return read(game?renderer.gameViewportTexture():renderer.viewportTexture());};
    const auto baseline=capture();check(renderer.refractionResidentBytes()==0,"Non-refractive views allocated refraction resources.");
    bindings[0].graph=prepare();scene.instances=instances;
    const auto identity=capture();check(difference(identity,baseline)<1.1,"IOR one changed the undistorted background.");
    check(renderer.refractionResidentBytes()>0,"Active glass did not allocate a snapshot.");
    // The editor matte is composed after HDR presentation. Glass over an
    // empty view must transmit that matte, including its colored absorption,
    // without moving the matte into exposure/tone mapping.
    ViewportAppearance defaultAppearance,matteAppearance;matteAppearance.backgroundSrgb={.42f,.55f,.68f};renderer.setAppearance(matteAppearance);
    scene.instances={};const auto matteBaseline=capture();scene.instances=std::span(&glass,1);
    const auto matteIdentity=capture();check(difference(matteIdentity,matteBaseline)<1.1,"Index-matched glass blackened the empty editor matte.");
    output().thickness=.4f;output().attenuationDistance=.4f;output().attenuationColor={.25f,1,.5f};bindings[0].graph=prepare();const auto matteTint=capture();
    const auto matteRgb=centerRgb(matteBaseline),matteTintRgb=centerRgb(matteTint);
    check(matteRgb[0]>60&&matteRgb[1]>80&&matteRgb[2]>100,"The empty-view matte fixture did not publish its nonblack editor background.");
    const auto decode=[](float value){value/=255;return value<=.04045f?value/12.92f:std::pow((value+.055f)/1.055f,2.4f);};
    const auto encode=[](float value){return 255*(value<=.0031308f?12.92f*value:1.055f*std::pow(value,1.f/2.4f)-.055f);};
    const auto glassNormalMatrix=math::normalMatrix(glass.model.transform);check(glassNormalMatrix.has_value(),"The matte fixture has a singular glass transform.");
    const auto glassNormal=math::normalized(math::transformDirection(*glassNormalMatrix,mesh->primitives[0].vertices[0].normal));
    const auto planeNormal=math::normalized(math::transformDirection(*glassNormalMatrix,{0,0,1}));
    const auto planePosition=math::transformPoint(glass.model.transform,{0,0,0});
    const auto viewProjection=settings.camera.projectionMatrix(float(Width)/Height,ClipDepth::ZeroToOne)*settings.camera.viewMatrix();
    Vec3 expectedMatteRgb{};
    for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x){
        const int px=int(Width/2)+x,py=int(Height/2)+y;
        const auto ray=math::rayFromScreen(float(px)+.5f,float(py)+.5f,viewProjection,{0,0,float(Width),float(Height)},ClipDepth::ZeroToOne);
        check(ray.has_value(),"The matte fixture cannot reconstruct its camera ray.");const auto point=math::intersectPlane(*ray,planePosition,planeNormal);
        check(point.has_value(),"The matte fixture camera ray missed its glass plane.");
        const auto viewDirection=math::normalized(settings.camera.eye()-*point);
        const auto pathRatio=output().thickness/(std::abs(math::dot(glassNormal,viewDirection))*output().attenuationDistance);
        for(unsigned c=0;c<3;++c){const auto background=matteBaseline.rgba[(std::size_t(py)*Width+px)*4+c];
            expectedMatteRgb[c]+=encode(decode(float(background))*std::pow(output().attenuationColor[c],float(pathRatio)));}
    }
    expectedMatteRgb=expectedMatteRgb/49;
    for(unsigned c=0;c<3;++c){
        const auto expected=expectedMatteRgb[c];
        check(std::abs(matteTintRgb[c]-expected)<2,"Editor matte absorption did not preserve linear RGB throughput; channel="+
            std::to_string(c)+", expected="+std::to_string(expected)+", actual="+std::to_string(matteTintRgb[c])+".");
    }
    output().opacity=.5f;bindings[0].graph=prepare();check(coverageMidpointError(capture(),matteBaseline,matteTint)<1.5,"Partial optical coverage failed to mix tinted and uncovered editor matte in linear space.");
    output().opacity=1;bindings[0].graph=prepare();settings.exposure=4;
    check(difference(capture(),matteTint)<1.1,"Exposure changed the transmitted editor matte.");
    settings.exposure=1;output().thickness=0;output().attenuationColor={1,1,1};output().attenuationDistance=1;
    renderer.setAppearance(defaultAppearance);scene.instances=instances;bindings[0].graph=prepare();
    output().ior=1.5f;output().thickness=.4f;bindings[0].graph=prepare();const auto bent=capture();
    check(difference(bent,identity)>3,"IOR/thickness failed to bend the background.");
    glass.opacity=.5f;const auto faded=capture();glass.opacity=1;
    check(coverageMidpointError(faded,baseline,bent)<1.5,"Instance opacity did not scale the complete optical surface in linear space.");
    output().thickness=0;bindings[0].graph=prepare();const auto thin=capture();
    check(difference(thin,bent)>3,"Zero thickness retained macroscopic displacement.");
    output().ior=1;output().thickness=.4f;output().attenuationColor={.2f,1,.2f};output().attenuationDistance=.4f;bindings[0].graph=prepare();
    const auto tinted=capture();const auto whiteMean=average(identity),tintMean=average(tinted);
    check(tintMean[0]<whiteMean[0]*.8f&&tintMean[2]<whiteMean[2]*.8f&&std::abs(tintMean[1]-whiteMean[1])<2,"RGB Beer absorption failed or attenuated the white channel.");
    output().thickness=0;bindings[0].graph=prepare();check(difference(capture(),identity)<1.1,"Zero thickness retained volume absorption.");
    output().opacity=0;output().ior=2.42f;output().thickness=.8f;bindings[0].graph=prepare();check(difference(capture(),baseline)<1.1,"Opacity zero retained glass presence.");
    output().opacity=1;output().attenuationColor={1,1,1};output().ior=1.5f;output().thickness=.2f;output().roughness=0;bindings[0].graph=prepare();const auto sharp=capture();
    output().roughness=.65f;bindings[0].graph=prepare();const auto frosted=capture();
    check(variation(frosted)<variation(sharp)*.85,"Roughness failed to blur transmitted background.");
    output().ior=1;bindings[0].graph=prepare();check(difference(capture(),identity)<1.1,"IOR one retained rough transmission blur or grazing reflectance.");
    // Index-matched transmission must preserve the already fogged background.
    settings.environment.fog.enabled=true;settings.environment.fog.density=.2f;settings.environment.fog.color={.3f,.4f,.5f};
    scene.instances=std::span(instances.data(),Stripes);const auto fogBaseline=capture();scene.instances=instances;
    check(difference(capture(),fogBaseline)<1.5,"Transmission applied foreground fog twice.");
    output().receiveFog=false;bindings[0].graph=prepare();check(difference(capture(),fogBaseline)<1.5,"Receive Fog off contaminated the already fogged transmitted background.");
    output().receiveFog=true;bindings[0].graph=prepare();
    settings.environment.fog.enabled=false;settings.environment.enabled=true;output().ior=1.5f;output().roughness=.15f;bindings[0].graph=prepare();const auto reflection=capture();
    settings.environment.enabled=false;const auto noReflection=capture();check(difference(reflection,noReflection)>1,"Full transmission lost environmental reflection.");
    // Independent Scene and Game sources must agree without sharing captures.
    check(difference(capture(false),noReflection)<1.1,"Scene and Game refraction sources differ.");
    output().transmission=0;output().refraction=false;settings.environment.enabled=true;bindings[0].graph=prepare();const auto ordinarySurface=capture();
    output().refraction=true;bindings[0].graph=prepare();check(difference(capture(),ordinarySurface)<1.5,"Transmission zero changed the ordinary dielectric surface.");
    output().transmission=1;settings.environment.enabled=false;
    // A background hit can be closer than the authored virtual exit: optical
    // thickness estimates displacement and must not reject this real surface.
    std::vector<ModelMaterial> brightMaterials(1),darkMaterials(1);
    brightMaterials[0].baseColor={1,1,1,1};brightMaterials[0].doubleSided=true;
    darkMaterials[0].baseColor={.005f,.005f,.005f,1};darkMaterials[0].doubleSided=true;
    auto distant=instances[0];distant.entityId=310;distant.materials=&brightMaterials;
    distant.model.transform=math::translation({0,0,-3})*math::scaling({4,4,1});
    auto silhouette=instances[0];silhouette.entityId=311;silhouette.materials=&darkMaterials;
    silhouette.model.transform=math::translation({0,0,-.2f})*math::scaling({.08f,.3f,1});
    std::array silhouetteInstances{distant,silhouette,glass};scene.instances=silhouetteInstances;
    output().ior=1.595f;output().roughness=0;output().thickness=0;bindings[0].graph=prepare();const auto silhouetteStraight=capture();
    output().thickness=.674f;bindings[0].graph=prepare();const auto silhouetteShifted=capture();
    const auto brightPatch=patchMean(silhouetteStraight,settings.camera,{.13f,0,0}),darkPatch=patchMean(silhouetteShifted,settings.camera,{.13f,0,0});
    check(brightPatch>170&&darkPatch<60&&darkPatch<brightPatch*.4,"Refraction lost a nearby silhouette before its virtual exit; straight="+
        std::to_string(brightPatch)+", shifted="+std::to_string(darkPatch)+".");
    // At long sky depth, a roughness cone has a finite angular footprint.
    // A distant black half-wall must not blur across the entire bright sky.
    settings.environment.enabled=true;settings.environment.background.show=true;settings.camera.farPlane=2048;
    distant.materials=&darkMaterials;distant.model.transform=math::translation({-3,0,-3})*math::scaling({3,4,1});
    std::array halfWallInstances{distant,glass};
    for(const float roughness:{.084f,.228f}){
        output().roughness=roughness;bindings[0].graph=prepare();scene.instances=std::span(&glass,1);const auto clearSky=capture();
        scene.instances=halfWallInstances;const auto boundedSky=capture();
        const auto skyDifference=patchDifference(boundedSky,clearSky,settings.camera,{.6f,0,0});
        check(skyDifference<2,"Rough refraction spread a distant silhouette across the sky; roughness="+
            std::to_string(roughness)+", sky patch difference="+std::to_string(skyDifference)+".");
    }
    settings.camera.farPlane=20;settings.environment.enabled=false;settings.environment.background.show=false;scene.instances=instances;
    output().ior=32;output().thickness=1000000;output().roughness=.9f;bindings[0].graph=prepare();
    const auto extremeMean=average(capture());check(extremeMean[1]>10&&extremeMean[1]<220,"Extreme optical values produced an invalid or empty transmission fallback.");
    output().attenuationColor={0,0,0};bindings[0].graph=prepare();check(average(capture())[1]<2,"Black attenuation failed to extinguish finite optical transmission.");
    output().ior=1.5f;output().thickness=.4f;output().attenuationColor={1,1,1};
    // Depth rejection must not pull a nearer opaque occluder into a glass
    // pixel immediately beside it. The zero-thickness result is the fallback.
    output().roughness=0;output().thickness=0;bindings[0].graph=prepare();
    std::vector<ModelMaterial> foregroundMaterials(1);foregroundMaterials[0].baseColor={0,.6f,0,1};foregroundMaterials[0].doubleSided=true;
    auto foreground=instances[0];foreground.entityId=200;foreground.materials=&foregroundMaterials;
    foreground.model.transform=math::translation({0,0,.35f})*math::scaling({.25f,.7f,1});
    std::array<RenderModelInstance,Stripes+2> foregroundInstances;
    std::copy_n(instances.begin(),Stripes,foregroundInstances.begin());foregroundInstances[Stripes]=foreground;foregroundInstances.back()=glass;
    scene.instances=foregroundInstances;const auto foregroundThin=capture();
    output().thickness=.4f;bindings[0].graph=prepare();const auto foregroundBent=capture();
    check(patchDifference(foregroundBent,foregroundThin,settings.camera,{.32f,0,0})<1.5,"Foreground depth rejection dragged an occluder across its silhouette.");
    // A front object in the transparent queue must be composed after the
    // complete transmission/surface pair rather than being replaced by it.
    foregroundInstances[Stripes].blend=MaterialBlend::Alpha;
    scene.instances=std::span(foregroundInstances.data(),Stripes+1);const auto alphaForegroundBaseline=capture();scene.instances=foregroundInstances;
    check(patchDifference(capture(),alphaForegroundBaseline,settings.camera,{0,0,.35f})<1.1,"Refraction replaced a nearer transparent-queue surface.");
    scene.instances=instances;
    // A sharply tilted shading normal forces the estimated exit outside the
    // screen. Both invalid projections and index-matched grazing views must
    // retain a finite, undistorted source.
    const auto edgeMesh=std::make_shared<ModelAsset>(*mesh);edgeMesh->id="refraction-contract-edge-plane";
    for(auto& vertex:edgeMesh->primitives[0].vertices){vertex.normal={.98480775f,0,.17364818f};vertex.tangent={.17364818f,0,-.98480775f,1};}
    check(renderer.prepareModel(*edgeMesh),renderer.modelStatus());glass.model.asset=edgeMesh;
    output().ior=2.42f;output().thickness=0;bindings[0].graph=prepare();const auto edgeThin=capture();
    output().thickness=10;bindings[0].graph=prepare();check(difference(capture(),edgeThin)<1.1,"Offscreen refraction clamped a streak instead of using the unshifted background.");
    output().ior=1;output().roughness=.65f;bindings[0].graph=prepare();check(difference(capture(),identity)<1.1,"Index-matched grazing glass retained reflection or rough blur.");
    glass.model.asset=mesh;
    // Two-channel thickness authoring reaches the same already published
    // refraction family through a real linear-data texture binding.
    output().ior=1.5f;output().roughness=0;output().thickness=0;bindings[0].graph=prepare();const auto textureThin=capture();
    output().thickness=.4f;bindings[0].graph=prepare();const auto textureThick=capture();
    const auto thicknessNode=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D).id;
    auto* textureNode=findMaterialNode(asset.graph,thicknessNode);textureNode->texture.role=MaterialTextureRole::Data;textureNode->texture.image.id="assets/textures/refraction-thickness.png";
    const auto thicknessScale=addMaterialNode(asset.graph,MaterialNodeKind::Multiply).id;findMaterialNode(asset.graph,thicknessScale)->right.value={.4f,.4f,.4f,.4f};
    connectMaterialNodes(asset.graph,thicknessNode,MaterialPin::R,thicknessScale,MaterialPin::Left);
    connectMaterialNodes(asset.graph,thicknessScale,MaterialPin::Value,outputId,MaterialPin::Thickness);
    constexpr std::array<std::uint8_t,8> thicknessPixels{0,0,0,255,255,255,255,255};
    Texture thicknessTexture{bgfx::createTexture2D(2,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(thicknessPixels.data(),std::uint32_t(thicknessPixels.size())))};
    check(bgfx::isValid(thicknessTexture.handle),"Refraction thickness texture allocation failed.");bindings[0].graph=prepare(thicknessTexture.handle);const auto texturedThickness=capture();
    const auto thinPatchDifference=patchDifference(texturedThickness,textureThin,settings.camera,{-.35f,0,0});
    const auto thickPatchDifference=patchDifference(texturedThickness,textureThick,settings.camera,{.35f,0,0});
    check(thinPatchDifference<1.5&&thickPatchDifference<1.5,"Texture-driven thickness disagrees with constant optical thickness; thin patch="+
        std::to_string(thinPatchDifference)+", thick patch="+std::to_string(thickPatchDifference)+", reversed thin="+
        std::to_string(patchDifference(texturedThickness,textureThin,settings.camera,{.35f,0,0}))+", reversed thick="+
        std::to_string(patchDifference(texturedThickness,textureThick,settings.camera,{-.35f,0,0}))+".");
    asset.graph.links.clear();output().ior=1;output().thickness=0;bindings[0].graph=prepare();
    std::uint64_t pickSerial{};
    for(const auto mode:{PickMode::Selection,PickMode::MaterialSlot}){
        check(renderer.requestPick({1,1,++pickSerial,Width/2,Height/2,renderer.pickResourceSignature(mode),mode}),renderer.pickingStatus());renderer.render(scene);
        std::optional<PickResult> picked;for(unsigned i=0;i<120&&!picked;++i){tick();picked=renderer.pollPick(frame);}
        check(picked&&picked->entityId==glass.entityId,"Full optical transmission made the authored surface unpickable.");
        if(mode==PickMode::MaterialSlot)check(picked->materialSlot==0,"Refraction changed material-slot picking identity.");
    }
    RenderParticle opticalParticle;opticalParticle.size=1.6f;opticalParticle.right={.8660254f,0,-.5f};opticalParticle.up={0,1,0};
    RenderParticleMaterial opticalMaterial;opticalMaterial.blend=MaterialBlend::Alpha;opticalMaterial.shading=MaterialShading::StandardPbr;opticalMaterial.doubleSided=true;opticalMaterial.graph=bindings[0].graph;
    scene.instances=std::span(instances.data(),Stripes);scene.particles=std::span(&opticalParticle,1);scene.particleMaterials=std::span(&opticalMaterial,1);
    check(difference(capture(),identity)<1.1,"Index-matched sprite transmission changed the background.");
    output().ior=1.5f;output().thickness=.4f;bindings[0].graph=prepare();opticalMaterial.graph=bindings[0].graph;
    check(difference(capture(),identity)>3,"Sprite transmission failed to use its normal, IOR or thickness.");
    scene.particles={};scene.particleMaterials={};scene.instances=instances;
    settings.environment.enabled=true;settings.environment.background.show=true;output().ior=1;bindings[0].graph=prepare();
    scene.instances={};const auto skyBaseline=capture();scene.instances=std::span(&glass,1);
    check(difference(capture(),skyBaseline)<1.1,"Index-matched transmission lost the sky background.");
    output().ior=1.5f;bindings[0].graph=prepare();check(average(capture())[1]>30,"Refracted sky became an empty black source.");
    settings.environment.enabled=false;settings.environment.background.show=false;scene.instances=instances;
    settings.antiAliasing=AntiAliasing::Smaa;(void)capture();
    settings.environment.glow.enabled=true;(void)capture();settings.environment.glow.enabled=false;
    renderer.resizeMaterialPreview(Width,Height);
    for(const auto preview:{MaterialPreviewMesh::Sphere,MaterialPreviewMesh::Sprite}){
        renderer.renderMaterialPreview(settings,asset,bindings[0],preview,Mat4{},true,bindings[0].graph);
        check(renderer.refractionStatus().empty(),renderer.refractionStatus());(void)read(renderer.materialPreviewTexture());
    }
    renderer.resizeParticlePreview(Width,Height);renderer.renderParticlePreview(settings,std::span(&opticalParticle,1),std::span(&opticalMaterial,1));
    check(renderer.refractionStatus().empty(),renderer.refractionStatus());(void)read(renderer.particlePreviewTexture());
    output().tessellation.enabled=selectedTestGraphicsBackend()==GraphicsBackend::Direct3D12;output().tessellation.amount=.05f;
    bindings[0].graph=prepare();(void)capture();
    const auto viewBytes=renderer.refractionResidentBytes();
    constexpr std::uint16_t ThumbnailSize=128;
    check(renderer.renderThumbnail("refraction-gpu-thumbnail",1,scene,ThumbnailSize,bindings[0].graph),renderer.thumbnailStatus());
    check(renderer.refractionStatus().empty(),renderer.refractionStatus());
    check(renderer.refractionResidentBytes()>viewBytes,"Thumbnail refraction reused another view's snapshot or omitted its resource accounting.");
    std::optional<ThumbnailResult> thumbnail;
    for(unsigned i=0;i<120&&!thumbnail;++i){tick();thumbnail=renderer.pollThumbnail(frame);}
    check(thumbnail.has_value(),"Refraction thumbnail readback timed out.");
    check(thumbnail->id=="refraction-gpu-thumbnail"&&thumbnail->signature==1&&thumbnail->image.width==ThumbnailSize&&thumbnail->image.height==ThumbnailSize&&
        thumbnail->image.rgba.size()==std::size_t(ThumbnailSize)*ThumbnailSize*4,"Refraction thumbnail returned stale identity or incorrect image dimensions.");
    std::uint64_t thumbnailLight{};
    for(std::size_t pixel=0;pixel<thumbnail->image.rgba.size();pixel+=4)
        thumbnailLight+=thumbnail->image.rgba[pixel]+thumbnail->image.rgba[pixel+1]+thumbnail->image.rgba[pixel+2];
    check(thumbnailLight>std::uint64_t(ThumbnailSize)*ThumbnailSize*3*20,"Refraction thumbnail was an empty black image.");
    check(renderer.refractionStatus().empty(),renderer.refractionStatus());
    const auto activeBytes=renderer.refractionResidentBytes();
    renderer.releaseMaterialPreview();renderer.releaseParticlePreview();scene.instances=std::span(instances.data(),Stripes);
    renderer.render(scene);renderer.renderGame(scene);for(unsigned i=0;i<5;++i)tick();
    check(renderer.refractionResidentBytes()==0,"Disabled refraction retained snapshot or retired scratch allocations.");
    std::cout<<"Active independent view refraction allocations: "<<double(activeBytes)/(1024*1024)<<" MiB; disabled allocations: 0 MiB.\n";
    resources.stop();frame=renderer.drainPending(frame);
    std::cout<<"Refraction identity, bending, absorption, roughness, coverage, fog, reflection, depth rejection, foreground ordering, offscreen fallback, texture thickness, picking, sprites, sky, isolated views, post effects, previews, tessellation and thumbnail readback passed.\n";
}
}
int main(int argc,char** argv){try{
    check(argc==4,"Pass shaderc, source and include directories.");
    const auto cache=fs::temp_directory_path()/("crimson-refraction-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup{fs::path path;~Cleanup(){std::error_code error;const auto resolved=fs::weakly_canonical(path,error),root=fs::weakly_canonical(fs::temp_directory_path(),error);
        if(!error&&resolved.parent_path()==root&&resolved.filename().string().starts_with("crimson-refraction-"))fs::remove_all(resolved,error);}} cleanup{cache};
    ShaderCompileConfig config{argv[1],argv[2],argv[3],cache};
    check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());struct Quit{~Quit(){SDL_Quit();}} quit;
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson refraction checks",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
    check(bool(window),SDL_GetError());exercise(window.get(),config);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
