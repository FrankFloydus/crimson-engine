#include "crimson-renderer/Renderer.h"
#include "GpuTestBackend.h"
#include "crimson-renderer/RenderPassPlan.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/MaterialGraphShader.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Picking.h"
#include "resources/ResourceManager.h"

#include <SDL3/SDL.h>
#include <bgfx/platform.h>
#include <d3d11.h>
#include <dxgi.h>
#include <lodepng.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
constexpr std::uint16_t ReadView=254;
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Temporary {
    fs::path path=fs::temp_directory_path()/("crimson-ssgi-"+std::to_string(Clock::now().time_since_epoch().count()));
    Temporary(){fs::create_directories(path);}
    ~Temporary(){std::error_code error;const auto target=fs::weakly_canonical(path,error),parent=fs::weakly_canonical(fs::temp_directory_path(),error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("crimson-ssgi-"))fs::remove_all(target,error);}
};
struct Sdl {Sdl(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~Sdl(){SDL_Quit();}};
struct Texture {bgfx::TextureHandle value{BGFX_INVALID_HANDLE};~Texture(){if(bgfx::isValid(value))bgfx::destroy(value);}};
struct Options {bool benchmark{};unsigned warmup{48},samples{120};fs::path artifacts;};
struct ComRelease {void operator()(IUnknown* value)const{if(value)value->Release();}};
void printAdapter(ID3D11Device* device) {
    IDXGIDevice* rawDevice{};
    check(SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(&rawDevice))),"Cannot query the selected D3D11 DXGI device.");
    std::unique_ptr<IDXGIDevice,ComRelease> dxgiDevice(rawDevice);
    IDXGIAdapter* rawAdapter{};check(SUCCEEDED(dxgiDevice->GetAdapter(&rawAdapter)),"Cannot query the selected DXGI adapter.");
    std::unique_ptr<IDXGIAdapter,ComRelease> adapter(rawAdapter);
    DXGI_ADAPTER_DESC description{};check(SUCCEEDED(adapter->GetDesc(&description)),"Cannot describe the selected DXGI adapter.");
    std::array<char,512> name{};
    check(WideCharToMultiByte(CP_UTF8,0,description.Description,-1,name.data(),int(name.size()),nullptr,nullptr)>0,"Cannot decode the selected DXGI adapter name.");
    std::cout<<"D3D11 adapter: "<<name.data()<<" vendor=0x"<<std::hex<<description.VendorId<<" device=0x"<<description.DeviceId<<std::dec<<'\n';
}

std::shared_ptr<ModelAsset> quad() {
    auto asset=std::make_shared<ModelAsset>();asset->id="ssgi-open-wall-floor";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    constexpr std::array<Vec3,4> positions{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
    constexpr std::array<Vec2,4> uvs{{{0,1},{1,1},{1,0},{0,0}}};
    for(unsigned index=0;index<4;++index){ModelVertex vertex;vertex.position=positions[index];vertex.normal={0,0,1};
        vertex.uv0=vertex.uv1=uvs[index];vertex.tangent={1,0,0,-1};primitive.vertices.push_back(vertex);math::include(asset->bounds,vertex.position);}
    primitive.indices={0,1,2,0,2,3};asset->primitives.push_back(std::move(primitive));asset->draws.push_back({0,{}});
    ModelMaterial material;material.baseColor={.65f,.65f,.65f,1};material.metallic=0;material.roughness=1;material.specular=0;
    asset->materials.push_back(material);return asset;
}
std::array<double,3> floorColor(const DecodedImage& image,const Camera& camera) {
    const auto matrix=camera.projectionMatrix(float(image.width)/image.height,ClipDepth::ZeroToOne)*camera.viewMatrix();
    std::array<double,3> sum{};unsigned count{};
    // A broad interior floor patch avoids tracing-ray and edge-specific claims.
    for(unsigned z=0;z<6;++z)for(unsigned x=0;x<9;++x) {
        const Vec3 position{-1.2f+.3f*x,.002f,-1.05f+.22f*z};
        const auto pixel=math::project(position,matrix,{0,0,float(image.width),float(image.height)},ClipDepth::ZeroToOne);
        check(bool(pixel),"SSGI floor sample is not projectable.");
        const int px=int((*pixel)[0]),py=int((*pixel)[1]);
        check(px>=1&&py>=1&&px<int(image.width)-1&&py<int(image.height)-1,"SSGI floor patch escaped the viewport.");
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const auto offset=(std::size_t(py+dy)*image.width+std::size_t(px+dx))*4;
            for(unsigned channel=0;channel<3;++channel)sum[channel]+=image.rgba[offset+channel];++count;}
    }
    for(auto& value:sum)value/=count;return sum;
}
std::vector<double> floorLinearSamples(const DecodedImage& image,const Camera& camera) {
    const auto matrix=camera.projectionMatrix(float(image.width)/image.height,ClipDepth::ZeroToOne)*camera.viewMatrix();
    std::vector<double> values;values.reserve(54);
    for(unsigned z=0;z<6;++z)for(unsigned x=0;x<9;++x) {
        const auto projected=math::project(Vec3{-1.2f+.3f*x,.002f,-1.05f+.22f*z},matrix,{0,0,float(image.width),float(image.height)},ClipDepth::ZeroToOne);
        check(bool(projected),"SSGI variance sample is not projectable.");const int px=int((*projected)[0]),py=int((*projected)[1]);
        check(px>=0&&py>=0&&px<int(image.width)&&py<int(image.height),"SSGI variance sample escaped the viewport.");
        const double value=image.rgba[(std::size_t(py)*image.width+px)*4]/255.;
        values.push_back(value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4));
    }
    return values;
}
std::array<double,3> surfaceColor(const DecodedImage& image,const Camera& camera,Vec3 origin,Vec3 stepX,Vec3 stepY) {
    const auto matrix=camera.projectionMatrix(float(image.width)/image.height,ClipDepth::ZeroToOne)*camera.viewMatrix();
    std::array<double,3> sum{};unsigned count{};
    for(unsigned y=0;y<6;++y)for(unsigned x=0;x<9;++x) {
        const auto pixel=math::project(origin+stepX*float(x)+stepY*float(y),matrix,{0,0,float(image.width),float(image.height)},ClipDepth::ZeroToOne);
        check(bool(pixel),"Multi-bounce sample is not projectable.");const int px=int((*pixel)[0]),py=int((*pixel)[1]);
        check(px>=0&&py>=0&&px<int(image.width)&&py<int(image.height),"Multi-bounce sample escaped the viewport.");
        const auto offset=(std::size_t(py)*image.width+std::size_t(px))*4;
        for(unsigned channel=0;channel<3;++channel)sum[channel]+=image.rgba[offset+channel];++count;
    }
    for(auto& value:sum)value/=count;return sum;
}
void saveComparison(const fs::path& directory,const char* name,const DecodedImage& image) {
    fs::create_directories(directory);std::vector<unsigned char> encoded;
    check(lodepng::encode(encoded,image.rgba,image.width,image.height)==0,"Cannot encode SSGI comparison image.");
    const auto path=directory/name;std::ofstream output(path,std::ios::binary);
    check(bool(output),"Cannot create SSGI comparison image: "+path.string());
    output.write(reinterpret_cast<const char*>(encoded.data()),static_cast<std::streamsize>(encoded.size()));output.flush();
    check(bool(output),"Cannot write SSGI comparison image: "+path.string());
}
void equivalent(const DecodedImage& a,const DecodedImage& b,const char* reason,unsigned tolerance=1) {
    check(a.width==b.width&&a.height==b.height&&a.rgba.size()==b.rgba.size(),"SSGI readback dimensions changed.");
    unsigned maximum{};for(std::size_t i=0;i<a.rgba.size();++i)maximum=std::max(maximum,unsigned(std::abs(int(a.rgba[i])-int(b.rgba[i]))));
    check(maximum<=tolerance,std::string(reason)+"; maximum RGBA error="+std::to_string(maximum));
}

void exercise(SDL_Window* window,const ShaderCompileConfig& config,const Options& options) {
    // Resource workers precede every GPU owner and are stopped before draining.
    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));MaterialGraphPrograms programs(resources,config);TestGpuContext context;
    if(auto* device=context.d3d11Device())printAdapter(device);
    else {
        const auto* caps=bgfx::getCaps();
        std::cout<<bgfx::getRendererName(bgfx::getRendererType())<<" adapter: vendor=0x"<<std::hex<<caps->vendorId<<
            " device=0x"<<caps->deviceId<<std::dec<<'\n';
    }
    bgfx::reset(320,240,BGFX_RESET_NONE);renderer.setGridVisible(false);
    ViewportAppearance appearance;appearance.backgroundSrgb={};renderer.setAppearance(appearance);
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x53534749454e56;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    std::uint16_t width=217,height=163;renderer.resizeViewport(width,height);renderer.resizeGameViewport(width,height);
    std::uint32_t frame{};
    const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=renderer.advance_frame();context.flush();};
    const auto prepare=[&](const MaterialAsset& material) {
        const auto shader=generateMaterialGraphShader(material);MaterialGraphProgramResult result;
        const auto deadline=Clock::now()+std::chrono::seconds{90};
        do{tick();result=programs.request(shader);
            if(!result.current&&!result.diagnostic.empty()&&result.diagnostic!="Compiling material graph...")throw std::runtime_error(result.diagnostic);
            if(!result.current)std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }while(!result.current&&Clock::now()<deadline);
        check(result.current&&bool(result.programs),"SSGI generated material compilation timed out.");
        auto packet=std::make_shared<PreparedGraphMaterial>();packet->programs=result.programs;packet->uniforms=shader.uniforms;packet->sources=shader.textures;
        packet->runtimeGraph=shader.runtimeGraph;packet->runtimeParameters=shader.runtimeParameters;packet->dynamicCoverage=shader.dynamicCoverage;
        packet->shading=materialOutputNode(material.graph).output.shading;packet->blend=materialOutputNode(material.graph).output.blend;
        packet->signature=result.programs->signature;return packet;
    };
    const auto read=[&](bgfx::TextureHandle source,const std::function<void()>& continuous=std::function<void()>{}) {
        Texture staging{bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.value),"SSGI readback allocation failed.");
        DecodedImage result{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4)};
        bgfx::blit(ReadView,staging.value,0,0,source,0,0,width,height);const auto ready=bgfx::readTexture(staging.value,result.rgba.data());
        check(ready!=UINT32_MAX,"SSGI readback is unsupported.");
        // The caller already submitted the frame whose image is requested.
        // Submitting again before its first tick aliases the per-view GI
        // history and intentionally invalidates that duplicate frame.
        for(unsigned attempt=0;attempt<120;++attempt){if(continuous&&attempt>0)continuous();tick();if(picking::frameReady(frame,ready))return result;}
        renderer.drainPending(frame);throw std::runtime_error("SSGI readback timed out.");
    };
    const auto mesh=quad();check(renderer.prepareModel(*mesh),renderer.modelStatus());
    std::vector<ModelMaterial> floorMaterials=mesh->materials,wallMaterials=mesh->materials,blueMaterials=mesh->materials;
    std::vector<ModelMaterialTextures> textures(1);wallMaterials[0].emissive={3.f,.015f,.005f};blueMaterials[0].emissive={.005f,.015f,3.f};
    RenderModelInstance floor;floor.entityId=1;floor.model.asset=mesh;floor.materials=&floorMaterials;floor.textures=&textures;floor.forceFullDetail=true;
    // Match SceneExtraction's category-intersected receiver masks.
    floor.lightingScopes.directReceiverMask=lighting_scope::World;
    floor.model.transform=math::rotation(math::axisAngle({1,0,0},-1.57079632679f))*math::scaling({3,3,1});
    RenderModelInstance wall=floor;wall.entityId=2;wall.materials=&wallMaterials;
    wall.model.transform=math::translation({0,1.25f,-1.5f})*math::scaling({2.5f,1.25f,1});
    std::array<RenderModelInstance,2> instances{floor,wall};
    SceneSettings settings;settings.camera.position={3.4f,4.7f,6.3f};settings.camera.target={0,.45f,-.4f};
    settings.camera.fov=48;settings.camera.fovAxis=CameraFovAxis::Vertical;settings.camera.nearPlane=.1f;settings.camera.farPlane=32;
    settings.environment.enabled=false;settings.environment.ambientLight.source=AmbientLightSource::Disabled;
    settings.environment.toneMapping=ToneMapping::Linear;settings.environment.background.overrideColor=true;settings.environment.background.color={};
    settings.environment.ssgi={false,2.f,3.f,.2f,.85f,SsgiQuality::High};
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=instances;scene.giWorldId=1;scene.giCameraId=1;scene.giEnvironmentId=1;
    const auto draw=[&]{renderer.renderGame(scene);check(renderer.ssgiStatus().empty(),renderer.ssgiStatus());};
    const auto warm=[&](unsigned count){for(unsigned i=0;i<count;++i){draw();tick();}};
    const auto capture=[&](unsigned count=32){warm(count);draw();return read(renderer.gameViewportTexture());};
    const auto baseline=capture(6);const auto baselineFloor=floorColor(baseline,settings.camera);
    check(baselineFloor[0]<1&&baselineFloor[1]<1&&baselineFloor[2]<1,"GI-off fixture has unwanted direct or ambient floor illumination.");
    settings.environment.ssgi.enabled=true;const auto bounced=capture(48);const auto red=floorColor(bounced,settings.camera);
    check(renderer.ssgiResidentBytes()>0,"Enabled GI allocated no camera resources.");
    check(red[0]>baselineFloor[0]+4&&red[0]>red[1]+3&&red[0]>red[2]+3,
        "Lit emissive wall failed to supply a measurable red bounce onto the ceiling-free floor: "+std::to_string(red[0])+","+std::to_string(red[1])+","+std::to_string(red[2]));
    if(!options.artifacts.empty()) {
        saveComparison(options.artifacts,"gi-off.png",baseline);saveComparison(options.artifacts,"gi-on.png",bounced);
        std::cout<<"SSGI comparison images: "<<options.artifacts.string()<<'\n';
    }
    {
        const auto savedSettings=settings;
        check(renderer.debugView(RenderViewPurpose::Scene)==RenderDebugView::Lit&&renderer.debugView(RenderViewPurpose::Game)==RenderDebugView::Lit,
            "A renderer did not start both viewport inspection modes in Lit.");
        renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Albedo);
        renderer.setDebugView(RenderViewPurpose::Scene,RenderDebugView::Roughness);
        for(const auto purpose:{RenderViewPurpose::MaterialPreview,RenderViewPurpose::ParticlePreview,RenderViewPurpose::Thumbnail,static_cast<RenderViewPurpose>(255)}) {
            renderer.setDebugView(purpose,RenderDebugView::Normal);
            check(renderer.debugView(purpose)==RenderDebugView::Lit,"A preview or invalid render purpose accepted a viewport diagnostic mode.");
        }
        check(renderer.debugView(RenderViewPurpose::Scene)==RenderDebugView::Roughness&&renderer.debugView(RenderViewPurpose::Game)==RenderDebugView::Albedo,
            "Scene and Game inspection selections overwrote each other.");
        renderer.setDebugView(RenderViewPurpose::Scene,RenderDebugView::Lit);
        const auto albedoImage=capture(4);const auto albedo=floorColor(albedoImage,settings.camera);
        for(auto value:albedo)check(std::abs(value-211)<4,"Albedo did not show the floor's linear .65 color after final sRGB encoding.");
        settings.exposure=.03125f;settings.environment.toneMapping=ToneMapping::AgX;
        settings.environment.fog={true,{.02f,.8f,.1f},.9f};settings.environment.debanding=true;
        equivalent(capture(4),albedoImage,"Albedo changed with exposure, tone mapping or fog",2);
        renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Roughness);const auto roughness=floorColor(capture(4),settings.camera);
        for(auto value:roughness)check(value>252,"Roughness=1 was not white in its diagnostic view.");
        renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Metal);const auto metallic=floorColor(capture(4),settings.camera);
        for(auto value:metallic)check(value<3,"Metal=0 was not black in its diagnostic view.");
        renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Normal);const auto normal=floorColor(capture(4),settings.camera);
        check(normal[1]>250&&normal[0]>180&&normal[0]<195&&normal[2]>180&&normal[2]<195,
            "World-up normal did not encode (.5,1,.5) in the diagnostic view.");
        settings=savedSettings;
        renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::DirectLighting);const auto directOnly=floorColor(capture(8),settings.camera);
        for(auto value:directOnly)check(value<3,"Direct Lighting included reflected emissive bounce.");
        renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::IndirectLighting);const auto indirectOnly=floorColor(capture(64),settings.camera);
        check(indirectOnly[0]>4&&indirectOnly[0]>indirectOnly[1]+3,"Indirect Lighting lost reflected emissive bounce.");
        settings.environment.ssgi.enabled=false;const auto indirectOff=floorColor(capture(4),settings.camera);
        for(auto value:indirectOff)check(value<3,"Indirect Lighting retained energy with GI, ambient and environment lighting off.");
        renderer.setDebugView(RenderViewPurpose::Game,static_cast<RenderDebugView>(255));
        check(renderer.debugView(RenderViewPurpose::Game)==RenderDebugView::Lit,"Invalid inspection mode did not restore Lit.");
        settings=savedSettings;warm(40);
        std::cout<<"Viewport channels: albedo="<<albedo[0]<<", roughness="<<roughness[0]<<", metal="<<metallic[0]
            <<", normal="<<normal[0]<<'/'<<normal[1]<<'/'<<normal[2]<<", direct/indirect="<<directOnly[0]<<'/'<<indirectOnly[0]<<'\n';
    }
    {
        const auto savedSettings=settings;const auto savedWall=instances[1];const auto savedEmission=wallMaterials[0].emissive;
        // A narrow bright donor produces infrequent hits. Compare the same
        // stationary floor in linear light, keeping rendering active during
        // readback so this measures history rather than repeated first frames.
        instances[1].model.transform=math::translation({0,1.25f,-1.5f})*math::scaling({.22f,1.25f,1});
        wallMaterials[0].emissive={12,.01f,.005f};settings.environment.ssgi.quality=SsgiQuality::Low;
        settings.environment.ssgi.strength=.1f;settings.environment.ssgi.radianceClamp=0;
        const auto measureNoise=[&](float stability) {
            settings.environment.ssgi.temporalStability=stability;warm(120);
            std::vector<double> sum(54),square(54);DecodedImage last;
            constexpr unsigned samples=16;
            for(unsigned sample=0;sample<samples;++sample) {
                draw();last=read(renderer.gameViewportTexture(),draw);
                const auto values=floorLinearSamples(last,settings.camera);
                for(unsigned i=0;i<values.size();++i){sum[i]+=values[i];square[i]+=values[i]*values[i];}
            }
            double mean{},variance{};for(unsigned i=0;i<sum.size();++i){const double average=sum[i]/samples;mean+=average;variance+=std::max(0.,square[i]/samples-average*average);}
            return std::tuple{mean/sum.size(),std::sqrt(variance/sum.size()),std::move(last)};
        };
        const auto [rawMean,rawNoise,rawImage]=measureNoise(0);
        const auto [stableMean,stableNoise,stableImage]=measureNoise(.9f);
        std::cout<<"SSGI sparse estimator: linear mean raw/stable="<<rawMean<<'/'<<stableMean<<", temporal deviation="<<rawNoise<<'/'<<stableNoise<<'\n';
        check(rawMean>.0001,"Sparse donor fixture supplied no useful reference irradiance.");
        check(stableMean>rawMean*.65,"Temporal denoising erased most of the sparse donor's reflected energy.");
        check(stableNoise<rawNoise*.8+.0001,"Temporal denoising did not suppress visible stationary sparse-ray fluctuations.");
        if(!options.artifacts.empty()){saveComparison(options.artifacts,"ssgi-no-history.png",rawImage);saveComparison(options.artifacts,"ssgi-denoised.png",stableImage);}
        settings=savedSettings;instances[1]=savedWall;wallMaterials[0].emissive=savedEmission;warm(40);
    }
    floorMaterials[0].metallic=1;
    check(floorColor(capture(32),settings.camera)[0]<2,"A fully metallic receiver acquired diffuse GI.");
    floorMaterials[0].metallic=0;instances[0].shading=MaterialShading::Unlit;
    const auto unlitWithGi=capture(16);settings.environment.ssgi.enabled=false;
    equivalent(capture(3),unlitWithGi,"Unlit surfaces changed when GI was disabled");
    settings.environment.ssgi.enabled=true;instances[0].shading=MaterialShading::StandardPbr;
    instances[0].opacity=.5f;
    check(floorColor(capture(32),settings.camera)[0]<2,"A partially faded model received GI.");
    instances[0].opacity=1;instances[1].opacity=.5f;
    check(floorColor(capture(32),settings.camera)[0]<2,"A partially faded model supplied GI.");
    instances[1].opacity=1;

    wallMaterials[0].emissive={.005f,.015f,3};const auto changedEmission=floorColor(capture(40),settings.camera);
    check(changedEmission[2]>changedEmission[0]+3,"Changing donor radiance retained stale red GI.");
    wallMaterials[0].emissive={3,.015f,.005f};warm(32);
    for(unsigned i=0;i<40;++i){instances[1].model.transform=math::translation({float(i+1)*.02f,1.25f,-1.5f})*math::scaling({2.5f,1.25f,1});draw();tick();}
    draw();check(floorColor(read(renderer.gameViewportTexture()),settings.camera)[0]>4,"A slowly moving donor stopped supplying current diffuse bounce.");
    instances[1]=wall;warm(32);
    {
        MaterialAsset material;material.id="ssgi-generated-masked";addMaterialNode(material.graph,MaterialNodeKind::Output);
        auto& output=materialOutputNode(material.graph).output;output.baseColor={.65f,.65f,.65f};output.metallic=0;output.roughness=1;output.specular=0;
        output.blend=MaterialBlend::Mask;
        std::vector<ModelMaterialTextures> generatedTextures(1);generatedTextures[0].graph=prepare(material);instances[0].textures=&generatedTextures;
        check(floorColor(capture(40),settings.camera)[0]>4,"Generated Lit/Masked material failed to receive GI beside a fallback donor.");
        output.opacityMask=0;generatedTextures[0].graph=prepare(material);
        check(floorColor(capture(40),settings.camera)[0]<2,"A discarded masked receiver retained GI coverage.");
        output.opacityMask=1;
        output.emission={3,.015f,.005f};generatedTextures[0].graph=prepare(material);instances[0].textures=&textures;instances[1].textures=&generatedTextures;
        check(floorColor(capture(40),settings.camera)[0]>4,"Generated Lit/Masked emissive material failed to supply GI to a fallback receiver.");
        output.opacityMask=0;generatedTextures[0].graph=prepare(material);
        check(floorColor(capture(40),settings.camera)[0]<2,"A discarded masked donor continued supplying GI.");
        instances[1].textures=&textures;
    }
    {
        std::vector<ModelMaterial> overlayMaterials=mesh->materials;overlayMaterials[0].baseColor={0,0,0,.6f};overlayMaterials[0].alphaBlend=true;
        auto overlay=wall;overlay.entityId=3;overlay.materials=&overlayMaterials;overlay.shading=MaterialShading::Unlit;overlay.blend=MaterialBlend::Alpha;
        overlay.model.transform=math::translation({0,1.25f,-1.49f})*math::scaling({2.5f,1.25f,1});
        std::array<RenderModelInstance,3> withOverlay{instances[0],instances[1],overlay};scene.instances=withOverlay;
        check(floorColor(capture(40),settings.camera)[0]>red[0]*.7,"Alpha surface overwrote the underlying opaque GI source attachments.");
        overlayMaterials[0].alphaBlend=false;withOverlay[2].blend=MaterialBlend::Additive;
        check(floorColor(capture(40),settings.camera)[0]>red[0]*.7,"Additive surface corrupted the underlying opaque GI source attachments.");
        scene.instances=instances;
    }
    settings.environment.ssgi.strength=0;equivalent(capture(3),baseline,"Zero GI Strength changed the disabled image");
    settings.environment.ssgi.strength=2;settings.environment.ssgi.enabled=false;
    equivalent(capture(3),baseline,"Disabling GI retained history light or changed existing coverage");
    settings.environment.ssgi.enabled=true;wallMaterials[0].emissive={};
    check(floorColor(capture(48),settings.camera)[0]<2,"Removing emissive light retained stale bounced illumination.");

    {
        const auto savedSettings=settings;const auto savedInstances=instances;
        // The finite light sphere lies entirely above the floor. It directly
        // lights the vertical wall, but the sampled floor can only receive its
        // radiance through the wall's diffuse screen-space bounce.
        PointLight point;point.entityId=81;point.position={0,1.6f,-.35f};point.radius=1.45f;
        point.color={1,.005f,.005f};point.intensity=120;point.specularStrength=0;
        // Exercise artist energy without the separate donor-radiance clamp
        // hiding the brighter setting's response.
        settings.environment.ssgi.radianceClamp=0;
        point.roles={lighting_scope::All,lighting_scope::All};
        scene.pointLights=std::span(&point,1);++scene.giWorldId;
        const auto wallColor=[&](const DecodedImage& image){return surfaceColor(image,settings.camera,
            {-.4f,1.2f,-1.498f},{.1f,0,0},{0,.1f,0});};
        const auto pointBounce=[&](const char* label) {
            const auto image=capture(64);const auto result=floorColor(image,settings.camera);
            check(result[0]>4&&result[0]>result[1]+3&&result[0]>result[2]+3,
                std::string(label)+" failed to supply colored point-light bounce: "+std::to_string(result[0])+","+
                std::to_string(result[1])+","+std::to_string(result[2]));
            return std::pair{image,result};
        };
        renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::DirectLighting);
        const auto pointDirect=capture(8);const auto wallDirect=wallColor(pointDirect),floorDirect=floorColor(pointDirect,settings.camera);
        check(wallDirect[0]>40&&wallDirect[0]>wallDirect[1]+20,"Point-only GI fixture did not directly light its donor wall.");
        for(auto value:floorDirect)check(value<2,"Point-only GI fixture directly lit the floor instead of isolating a reflected path.");
        renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::IndirectLighting);
        const auto [fallbackPointImage,fallbackPointBounce]=pointBounce("Fallback Lit donor");
        const auto checkPointEnergy=[&](const char* label,const std::array<double,3>& normalBounce) {
            point.indirectEnergy=0;
            for(const auto value:floorColor(capture(64),settings.camera))
                check(value<2,std::string(label)+" GI Energy zero retained point-light bounce.");
            point.indirectEnergy=2;
            const auto boosted=pointBounce(label).second;
            check(boosted[0]>normalBounce[0]+8,std::string(label)+" GI Energy did not boost indirect light: "+
                std::to_string(normalBounce[0])+" -> "+std::to_string(boosted[0]));
            renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::DirectLighting);
            const auto directAtTwo=capture(3);point.indirectEnergy=0;
            equivalent(capture(3),directAtTwo,"Point GI Energy changed Direct Lighting",0);
            point.indirectEnergy=1;renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::IndirectLighting);
            std::cout<<label<<" GI Energy 1/2 indirect="<<normalBounce[0]<<'/'<<boosted[0]<<'\n';
        };
        checkPointEnergy("Fallback point light",fallbackPointBounce);
        point.indirectEnergy=0;wallMaterials[0].emissive={3,.015f,.005f};
        check(floorColor(capture(48),settings.camera)[0]>4,"A point light's GI Energy suppressed independent Lit emission bounce.");
        wallMaterials[0].emissive={};point.indirectEnergy=1;
        // With GI disabled the complete Lit output must also stay unchanged.
        renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Lit);settings.environment.ssgi.enabled=false;
        point.indirectEnergy=0;const auto pointLitNoGi=capture(3);point.indirectEnergy=16;
        equivalent(capture(3),pointLitNoGi,"GI Energy changed Lit output while GI was disabled",0);
        point.indirectEnergy=1;settings.environment.ssgi.enabled=true;
        renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::IndirectLighting);
        point.enabled=false;const auto disabledPoint=floorColor(capture(48),settings.camera);
        for(auto value:disabledPoint)check(value<2,"Disabling a point light retained its indirect contribution.");
        point.enabled=true;
        if(!options.artifacts.empty()) {
            saveComparison(options.artifacts,"point-direct.png",pointDirect);
            saveComparison(options.artifacts,"point-indirect-fallback.png",fallbackPointImage);
        }

        MaterialAsset donor;donor.id="ssgi-generated-point-donor";addMaterialNode(donor.graph,MaterialNodeKind::Output);
        auto& output=materialOutputNode(donor.graph).output;output.baseColor={.65f,.65f,.65f};
        output.metallic=0;output.roughness=1;output.specular=0;output.emission={};
        std::vector<ModelMaterialTextures> donorTextures(1);donorTextures[0].graph=prepare(donor);instances[1].textures=&donorTextures;
        ++scene.giWorldId;const auto [generatedPointImage,generatedPointBounce]=pointBounce("Generated Lit donor");
        checkPointEnergy("Generated point light",generatedPointBounce);
        if(!options.artifacts.empty())saveComparison(options.artifacts,"point-indirect-generated.png",generatedPointImage);
        instances[1].textures=&textures;

        // Receiver bits share the point record with reveal/shadow role flags.
        // Keep those higher bits populated while checking both transport lanes.
        point.roles.receiverMask=lighting_scope::Character;
        instances[1].lightingCategory=lighting_scope::Character;
        instances[1].lightingScopes.directReceiverMask=lighting_scope::Character;++scene.giWorldId;
        const auto excludedWorld=floorColor(capture(48),settings.camera);
        for(auto value:excludedWorld)check(value<2,"Character-only point-light bounce leaked into the World receiver lane.");
        instances[0].lightingCategory=lighting_scope::Character;
        instances[0].lightingScopes.directReceiverMask=lighting_scope::Character;++scene.giWorldId;
        const auto characterPointBounce=pointBounce("Character-only point light").second;
        point.roles.receiverMask=lighting_scope::World;
        instances[1]=wall;++scene.giWorldId;
        const auto excludedCharacter=floorColor(capture(48),settings.camera);
        for(auto value:excludedCharacter)check(value<2,"World-only point-light bounce leaked into the Character receiver lane.");
        instances[0]=floor;++scene.giWorldId;
        const auto worldPointBounce=pointBounce("World-only point light with packed reveal flags").second;

        // Exercise the source capture after ordinary point-shadow evaluation,
        // not just the unshadowed material branch. The wall remains visible to
        // this light while both real fixture surfaces are eligible casters.
        point.roles.receiverMask=lighting_scope::All;point.roles.shadowCasterMask=lighting_scope::All;
point.castShadows=true;
        scene.shadowWorldId=8100;scene.shadowResolution=256;scene.shadowInstances=instances;++scene.giWorldId;
        warm(64);draw();const auto pointShadows=renderer.shadowStats();
        check(renderer.shadowStatus().empty(),renderer.shadowStatus());
        check(pointShadows.activeLights==1&&pointShadows.cacheHits+pointShadows.renderedLights>0&&renderer.shadowResidentBytes()>0,
            "Point-light GI regression did not execute a resident point-shadow path.");
        const auto shadowedPointImage=read(renderer.gameViewportTexture());const auto shadowedPointBounce=floorColor(shadowedPointImage,settings.camera);
        check(shadowedPointBounce[0]>4&&shadowedPointBounce[0]>shadowedPointBounce[1]+3,
            "An eligible shadowed point light stopped supplying diffuse indirect light: "+std::to_string(shadowedPointBounce[0]));
        if(!options.artifacts.empty())saveComparison(options.artifacts,"point-indirect-shadowed.png",shadowedPointImage);
        std::cout<<"SSGI point-only fixture: wall/floor direct="<<wallDirect[0]<<'/'<<floorDirect[0]
            <<", fallback/generated indirect="<<fallbackPointBounce[0]<<'/'<<generatedPointBounce[0]
            <<", World/Character indirect="<<worldPointBounce[0]<<'/'<<characterPointBounce[0]
            <<", shadowed indirect="<<shadowedPointBounce[0]<<'\n';
        renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Lit);
        settings=savedSettings;instances=savedInstances;scene.pointLights={};scene.shadowInstances={};scene.shadowWorldId=0;
        scene.shadowResolution=1024;++scene.giWorldId;
    }

    // A horizontal directional source lights the wall but not the floor. Only
    // its accepted receiver class may receive the corresponding bounced light.
    DirectionalLight directional;directional.direction={0,0,-1};directional.color={1,.005f,.005f};directional.intensity=8;
    directional.roles.receiverMask=lighting_scope::Character;scene.directionalLights=std::span(&directional,1);
    instances[1].lightingCategory=lighting_scope::Character;instances[1].lightingScopes.directReceiverMask=lighting_scope::Character;
    ++scene.giWorldId;const auto worldExcluded=floorColor(capture(40),settings.camera);
    instances[0].lightingCategory=lighting_scope::Character;instances[0].lightingScopes.directReceiverMask=lighting_scope::Character;
    ++scene.giWorldId;const auto characterBounce=floorColor(capture(40),settings.camera);
    check(worldExcluded[0]<2&&characterBounce[0]>worldExcluded[0]+4,
        "Character-only scene-light bounce escaped World/Character receiver channels: "+std::to_string(worldExcluded[0])+"/"+std::to_string(characterBounce[0]));
    const auto directionalEnergy=[&](const char* label) {
        directional.indirectEnergy=1;const auto normal=floorColor(capture(48),settings.camera);
        directional.indirectEnergy=0;const auto zero=floorColor(capture(64),settings.camera);
        for(const auto value:zero)check(value<2,std::string(label)+" GI Energy zero retained directional bounce.");
        directional.indirectEnergy=2;const auto boosted=floorColor(capture(48),settings.camera);
        check(boosted[0]>normal[0]+8,std::string(label)+" GI Energy did not boost directional bounce: "+
            std::to_string(normal[0])+" -> "+std::to_string(boosted[0]));
        renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::DirectLighting);
        const auto directAtTwo=capture(3);directional.indirectEnergy=0;
        equivalent(capture(3),directAtTwo,"Directional GI Energy changed Direct Lighting",0);
        directional.indirectEnergy=1;renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Lit);
        std::cout<<label<<" GI Energy 1/2 indirect="<<normal[0]<<'/'<<boosted[0]<<'\n';
    };
    directionalEnergy("Fallback directional light");
    MaterialAsset energyDonor;energyDonor.id="ssgi-generated-directional-energy";addMaterialNode(energyDonor.graph,MaterialNodeKind::Output);
    auto& energyOutput=materialOutputNode(energyDonor.graph).output;energyOutput.baseColor={.65f,.65f,.65f};
    energyOutput.metallic=0;energyOutput.roughness=1;energyOutput.specular=0;energyOutput.emission={};
    std::vector<ModelMaterialTextures> energyTextures(1);energyTextures[0].graph=prepare(energyDonor);instances[1].textures=&energyTextures;
    directionalEnergy("Generated directional light");instances[1].textures=&textures;
    scene.directionalLights={};instances[0]=floor;instances[1]=wall;wallMaterials[0].emissive={};
    instances[1]=wall;wallMaterials[0].emissive={3,.015f,.005f};++scene.giWorldId;
    warm(32);settings.camera.position={-3.4f,4.7f,6.3f};++scene.giCameraId;
    const auto moved=floorColor(capture(40),settings.camera);check(moved[0]>4,"Camera discontinuity failed to recover visible local bounce.");
    width=219;height=165;renderer.resizeGameViewport(width,height);renderer.resizeViewport(width,height);
    check(floorColor(capture(40),settings.camera)[0]>4,"Odd-size viewport recreation failed to restore GI.");

    SceneSettings editorSettings=settings;RenderScene editorScene{editorSettings,{}};editorScene.sceneMode=true;
    auto editorInstances=instances;editorInstances[1].materials=&blueMaterials;editorScene.instances=editorInstances;
    editorScene.giWorldId=scene.giWorldId;editorScene.giCameraId=99;editorScene.giEnvironmentId=scene.giEnvironmentId;
    for(unsigned i=0;i<48;++i){renderer.render(editorScene);draw();tick();}
    renderer.render(editorScene);draw();const auto gameImage=read(renderer.gameViewportTexture());
    // read() advances idle frames; only a new warm sequence may be compared.
    for(unsigned i=0;i<40;++i){renderer.render(editorScene);draw();tick();}
    renderer.render(editorScene);draw();const auto editorImage=read(renderer.viewportTexture());
    const auto gameColor=floorColor(gameImage,settings.camera),editorColor=floorColor(editorImage,editorSettings.camera);
    check(gameColor[0]>gameColor[2]+3&&editorColor[2]>editorColor[0]+3,
        "Independent Scene/Game GI histories or atlas publication contaminated the other camera.");
    std::cout<<"SSGI open fixture: off="<<baselineFloor[0]<<", red bounce="<<red[0]<<'/'<<red[1]<<'/'<<red[2]
        <<", class World/Character="<<worldExcluded[0]<<'/'<<characterBounce[0]<<", Scene blue/Game red="<<editorColor[2]<<'/'<<gameColor[0]<<'\n';

    {
        // The red emitter is coplanar with the neutral floor. It cannot light
        // that floor in one diffuse bounce; a visible wall supplies the
        // intermediate reflection for emitter -> wall -> floor.
        const auto savedSettings=settings;const auto savedWall=wallMaterials[0];
        settings.camera.position={3.4f,4.7f,6.3f};settings.camera.target={0,.45f,-.4f};
        settings.environment.ssgi.maxDistance=5;settings.environment.ssgi.thickness=.1f;
        settings.environment.ssgi.quality=SsgiQuality::High;settings.environment.ssgi.temporalStability=.9f;
        wallMaterials[0].emissive={};
        auto emitterMaterials=mesh->materials;emitterMaterials[0].emissive={16,.005f,.005f};
        auto emitter=floor;emitter.entityId=4;emitter.materials=&emitterMaterials;
        emitter.model.transform=math::translation({-1.5f,.01f,.2f})*
            math::rotation(math::axisAngle({1,0,0},-1.57079632679f))*math::scaling({1.1f,1.25f,1});
        std::array<RenderModelInstance,3> multi{floor,wall,emitter};scene.instances=multi;++scene.giWorldId;
        const auto sample=[&](const DecodedImage& image){return surfaceColor(image,settings.camera,{.3f,.002f,-1.f},{.1f,0,0},{0,0,.1f});};
        settings.environment.ssgi.bounces=1;const auto oneBounce=capture(80);const auto one=sample(oneBounce);
        const auto oneBounceBytes=renderer.ssgiResidentBytes();
        settings.environment.ssgi.bounces=2;const auto twoBounces=capture(80);const auto two=sample(twoBounces);
        if(!options.artifacts.empty()) {
            saveComparison(options.artifacts,"bounces-1.png",oneBounce);
            saveComparison(options.artifacts,"bounces-2.png",twoBounces);
        }
        check(one[0]<2,"Coplanar emission reached the floor without its required intermediate bounce: "+std::to_string(one[0]));
        check(two[0]>one[0]+3&&two[0]>two[1]+2,
            "Bounces=2 failed to transport light through the intermediate wall: "+std::to_string(one[0])+"/"+std::to_string(two[0]));
        settings.environment.ssgi.bounces=3;
        check(sample(capture(80))[0]>one[0]+3,"Bounces=3 lost the valid second-bounce path.");
        settings.environment.ssgi.bounces=4;const auto fourBounces=capture(80);const auto four=sample(fourBounces);
        check(four[0]>one[0]+3,"Bounces=4 lost the valid second-bounce path.");
        check(renderer.ssgiResidentBytes()==oneBounceBytes,"Bounce count unexpectedly allocated additional histories or targets.");
        // The intermediate material must actually reflect the added energy.
        multi[1].shading=MaterialShading::Unlit;const auto stopped=sample(capture(64));
        check(stopped[0]<2,"An Unlit intermediate surface reflected extra-bounce light.");
        multi[1].shading=MaterialShading::StandardPbr;wallMaterials[0].metallic=1;
        check(sample(capture(64))[0]<2,"A metallic intermediate surface reflected extra diffuse bounces.");
        wallMaterials[0].metallic=0;multi[1].lightingCategory=lighting_scope::Character;
        multi[1].lightingScopes.directReceiverMask=lighting_scope::Character;
        check(sample(capture(64))[0]<2,"Extra-bounce transport crossed the intermediate receiver's World/Character scope.");
        multi[1]=wall;
        MaterialAsset intermediate;intermediate.id="ssgi-generated-intermediate";addMaterialNode(intermediate.graph,MaterialNodeKind::Output);
        auto& output=materialOutputNode(intermediate.graph).output;output.baseColor={.65f,.65f,.65f};output.metallic=0;output.roughness=1;output.specular=0;
        std::vector<ModelMaterialTextures> intermediateTextures(1);intermediateTextures[0].graph=prepare(intermediate);multi[1].textures=&intermediateTextures;
        check(sample(capture(80))[0]>one[0]+3,"Generated Lit intermediate material failed to transport the additional diffuse bounce.");
        emitterMaterials[0].emissive={};
        check(sample(capture(64))[0]<2,"Removing the emitter retained recursive multi-bounce energy.");
        if(!options.artifacts.empty()) {
            saveComparison(options.artifacts,"bounces-1.png",oneBounce);
            saveComparison(options.artifacts,"bounces-2.png",twoBounces);
            saveComparison(options.artifacts,"bounces-4.png",fourBounces);
        }
        std::cout<<"SSGI multi-bounce fixture: floor R 1/2/4 bounces="<<one[0]<<'/'<<two[0]<<'/'<<four[0]
            <<", Unlit intermediate="<<stopped[0]<<'\n';
        settings=savedSettings;wallMaterials[0]=savedWall;scene.instances=instances;++scene.giWorldId;
    }

    if(options.benchmark) {
        width=1920;height=1080;renderer.resizeViewport(width,height);renderer.resizeGameViewport(width,height);
        settings.environment.ssgi.quality=editorSettings.environment.ssgi.quality=SsgiQuality::Medium;
        const auto measure=[&](bool both,bool enabled,int bounces) {
            settings.environment.ssgi.enabled=editorSettings.environment.ssgi.enabled=enabled;
            settings.environment.ssgi.bounces=editorSettings.environment.ssgi.bounces=bounces;
            const auto submit=[&]{if(both)renderer.render(editorScene);draw();tick();};
            for(unsigned i=0;i<options.warmup;++i)submit();
            std::vector<double> samples;std::uint32_t lastGpuFrame{};
            for(unsigned i=0;i<options.samples;++i){submit();const auto* stats=bgfx::getStats();
                if(stats->gpuFrameNum&&stats->gpuFrameNum!=lastGpuFrame&&stats->gpuTimerFreq>0&&stats->gpuTimeEnd>=stats->gpuTimeBegin) {
                    samples.push_back(double(stats->gpuTimeEnd-stats->gpuTimeBegin)*1000/stats->gpuTimerFreq);lastGpuFrame=stats->gpuFrameNum;}}
            std::sort(samples.begin(),samples.end());
            std::cout<<"SSGI benchmark 1920x1080 quality=Medium views="<<(both?"Scene+Game":"Game")<<" GI="<<(enabled?"on":"off")
                <<" bounces="<<bounces<<" samples="<<samples.size()<<" ssgi_owner_bytes="<<renderer.ssgiResidentBytes()
                <<" environment_resources_bytes="<<renderer.environmentResidentBytes();
            const auto* stats=bgfx::getStats();
            if(stats->gpuMemoryUsed>=0)std::cout<<" driver_gpu_memory_used="<<stats->gpuMemoryUsed;
            if(samples.empty())std::cout<<" GPU_median_ms=N/A GPU_p95_ms=N/A\n";
            else std::cout<<" GPU_median_ms="<<samples[samples.size()/2]<<" GPU_p95_ms="<<samples[std::min(samples.size()-1,samples.size()*95/100)]<<'\n';
        };
        // Release the previously exercised Scene owner before the single-view
        // measurement so resident bytes identify the requested live views.
        renderer.resizeViewport(1,1);
        for(unsigned i=0;i<6;++i){draw();tick();}
        measure(false,false,1);measure(false,true,1);measure(false,true,2);measure(false,true,4);
        renderer.resizeViewport(width,height);measure(true,false,1);measure(true,true,1);measure(true,true,2);measure(true,true,4);
    }
    resources.stop();renderer.drainPending(frame);
    std::cout<<"SSGI render checks passed.\n";
}
}
int main(int argc,char** argv) {
    try {
        check(argc>=4,"Pass shaderc, shader source and shader include directories, optionally --benchmark --warmup N --samples N --artifacts DIRECTORY.");
        Options options;
        for(int i=4;i<argc;++i){const std::string_view option=argv[i];
            if(option=="--benchmark")options.benchmark=true;
            else if(option=="--artifacts"){check(i+1<argc,"Missing SSGI artifact directory.");options.artifacts=fs::absolute(argv[++i]).lexically_normal();}
            else if(option=="--warmup"||option=="--samples") {check(i+1<argc,"Missing SSGI frame count.");const auto count=std::stoul(argv[++i]);
                check(count>0&&count<=10000,"SSGI frame count is out of range.");if(option=="--warmup")options.warmup=unsigned(count);else options.samples=unsigned(count);}
            else throw std::runtime_error("Unknown SSGI option: "+std::string(option));}
        Temporary temporary;ShaderCompileConfig config{argv[1],argv[2],argv[3],temporary.path};Sdl sdl;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson SSGI checks",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(bool(window),SDL_GetError());std::cout<<std::fixed<<std::setprecision(3);exercise(window.get(),config,options);return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
