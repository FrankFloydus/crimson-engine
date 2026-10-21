#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Environment.h"
#include <crimson/renderer/forward/ModelAnimation.h>
#include "crimson-renderer/Picking.h"
#include "assets/MaterialAsset.h"
#include "assets/BuiltinGeometry.h"
#include "resources/ResourceManager.h"
#include "GpuTestBackend.h"
#include <crimson/renderer/forward/SsrGpu.h>
#include "cs_ssr_color.h"
#include "cs_ssr_depth.h"
#include <SDL3/SDL.h>
#include <lodepng.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
// Native odd extents exercise the conservative min/max reduction footprint.
constexpr std::uint16_t Width=257,Height=193;
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Texture {bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};~Texture(){if(bgfx::isValid(handle))bgfx::destroy(handle);}};
std::shared_ptr<ModelAsset> plane(const char* id,Vec3 normal={0,0,1}) {
    auto result=std::make_shared<ModelAsset>();result->id=id;ModelPrimitive primitive;primitive.uvMask=3;
    const auto tangent=math::normalized(Vec3{normal[2],0,-normal[0]});
    constexpr std::array<Vec3,4> points{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
    constexpr std::array<Vec2,4> uv{{{0,1},{1,1},{1,0},{0,0}}};
    for(unsigned i=0;i<4;++i){ModelVertex vertex;vertex.position=points[i];vertex.normal=normal;
        vertex.tangent={tangent[0],tangent[1],tangent[2],1};vertex.uv0=vertex.uv1=uv[i];
        primitive.vertices.push_back(vertex);math::include(result->bounds,points[i]);}
    primitive.indices={0,1,2,0,2,3};result->primitives.push_back(std::move(primitive));
    result->draws.push_back({0,{}});result->materials.emplace_back();return result;
}
Vec3 center(const DecodedImage& image) {
    Vec3 result{};for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x){
        const auto offset=(std::size_t(int(image.height/2)+y)*image.width+int(image.width/2)+x)*4;
        for(unsigned c=0;c<3;++c)result[c]+=image.rgba[offset+c];}
    return result/49;
}
double difference(const DecodedImage& a,const DecodedImage& b) {
    check(a.width==b.width&&a.height==b.height,"SSR comparison extent mismatch.");
    double total{};unsigned count{};
    for(unsigned y=a.height*2/5;y<a.height*3/5;++y)for(unsigned x=a.width*2/5;x<a.width*3/5;++x)for(unsigned c=0;c<3;++c){
        const auto offset=(std::size_t(y)*a.width+x)*4+c;total+=std::abs(int(a.rgba[offset])-int(b.rgba[offset]));++count;}
    return total/count;
}
double variation(const DecodedImage& image) {
    double total{};unsigned count{};
    for(unsigned y=image.height/3;y<image.height*2/3-1;++y)for(unsigned x=image.width*2/5;x<image.width*3/5;++x){
        const auto offset=(std::size_t(y)*image.width+x)*4;
        total+=std::abs(int(image.rgba[offset])-int(image.rgba[offset+image.width*4]));++count;}
    return total/count;
}
void exercise(SDL_Window* window,const ShaderCompileConfig& config,const fs::path& artifacts,bool closedCubeOnly) {
    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));
    MaterialGraphPrograms programs(resources,config);TestGpuContext gpu;
    bgfx::reset(320,240,BGFX_RESET_NONE);renderer.resizeViewport(Width,Height);renderer.resizeGameViewport(Width,Height);renderer.setGridVisible(false);
    fs::create_directories(artifacts);
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x535352434f4e5452;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.2f,.2f,.2f})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    check(renderer.publishMaterialPreviewEnvironment(environment),"SSR preview environment publication failed.");
    check(renderer.publishThumbnailEnvironment(environment),renderer.thumbnailStatus());
    std::uint32_t frame{};
    std::vector<std::shared_ptr<DecodedImage>> destinations;
    const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=renderer.advance_frame();gpu.flush();};
    const auto read=[&](bgfx::TextureHandle texture,std::uint16_t width=Width,std::uint16_t height=Height){
        Texture staging{bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.handle),"SSR readback allocation failed.");
        auto pixels=std::make_shared<DecodedImage>(DecodedImage{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4)});destinations.push_back(pixels);
        bgfx::blit(DiagnosticReadbackView,staging.handle,0,0,texture,0,0,width,height);
        const auto ready=bgfx::readTexture(staging.handle,pixels->rgba.data());check(ready!=UINT32_MAX,"SSR readback unsupported.");
        for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return *pixels;}
        throw std::runtime_error("SSR readback timeout.");
    };
    {
        const auto compute=[](const auto& bytes){auto shader=bgfx::createShader(bgfx::copy(bytes,std::uint32_t(sizeof(bytes))));
            check(bgfx::isValid(shader),"SSR depth fixture shader allocation failed.");auto program=bgfx::createProgram(shader,true);
            check(bgfx::isValid(program),"SSR depth fixture program allocation failed.");return program;};
        const auto colorProgram=compute(cc_cs_ssr_color),depthProgram=compute(cc_cs_ssr_depth);
        {
            SsrGpu pyramid(colorProgram,depthProgram);check(pyramid.ensure(Width,Height),pyramid.status());
            std::vector<float> source(std::size_t(Width)*Height*4,0.f);
            source[(std::size_t(Height/2)*Width+Width/2)*4+3]=2.f;
            source.back()=5.f;
            Texture input{bgfx::createTexture2D(Width,Height,false,1,bgfx::TextureFormat::RGBA32F,0,bgfx::copy(source.data(),std::uint32_t(source.size()*sizeof(float))))};
            check(bgfx::isValid(input.handle),"SSR depth fixture input allocation failed.");
            bgfx::blit(40,pyramid.color_texture(),0,0,input.handle,0,0,Width,Height);
            check(pyramid.record(41),pyramid.status());
            const auto readDepth=[&](unsigned level){const auto width=std::uint16_t(std::max(1u,unsigned(Width)>>level));
                const auto height=std::uint16_t(std::max(1u,unsigned(Height)>>level));
                Texture staging{bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RG32F,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
                std::vector<float> values(std::size_t(width)*height*2);
                bgfx::blit(DiagnosticReadbackView,staging.handle,0,0,0,0,pyramid.depth_texture(),std::uint8_t(level),0,0,0,width,height,1);
                const auto ready=bgfx::readTexture(staging.handle,values.data());check(ready!=UINT32_MAX,"SSR min/max readback unsupported.");
                for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return values;}
                throw std::runtime_error("SSR min/max readback timed out.");};
            const auto baseDepth=readDepth(0);check(baseDepth[0]>=SsrEmptyMinimum*.99f&&baseDepth[1]==0,"Clear depth became a valid SSR donor.");
            const auto reduced=readDepth(1);constexpr unsigned reducedWidth=Width/2;
            for(unsigned y:{47u,48u})for(unsigned x:{63u,64u})
                check(std::abs(reduced[(std::size_t(y)*reducedWidth+x)*2]-2)<.001f,"Odd-size Hi-Z lost an overlapping one-pixel donor.");
            const auto last=readDepth(pyramid.mip_count()-1);
            check(std::abs(last[0]-2)<.001f&&std::abs(last[1]-5)<.003f,"Hi-Z omitted the trailing texel or corrupted its conservative range.");
            for(unsigned i=0;i<3;++i)tick();
        }
        bgfx::destroy(colorProgram);bgfx::destroy(depthProgram);
    }
    MaterialAsset asset;asset.id="ssr-gpu-contract";asset.name="SSR GPU contract";asset.graph.nodes.reserve(64);
    const auto outputId=addMaterialNode(asset.graph,MaterialNodeKind::Output).id;
    const auto output=[&]() -> MaterialOutputNode& {return findMaterialNode(asset.graph,outputId)->output;};
    output().shading=MaterialShading::StandardPbr;output().doubleSided=true;output().baseColor={.8f,.8f,.8f};output().metallic=1;output().roughness=.045f;
    const auto prepare=[&](bgfx::TextureHandle texture=BGFX_INVALID_HANDLE){
        const auto generated=generateMaterialGraphShader(asset);MaterialGraphProgramResult published;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{180};
        do{tick();published=programs.request(generated);
            if(!published.current&&!published.compiling)throw std::runtime_error(published.diagnostic);
            if(!published.current)std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }while(!published.current&&std::chrono::steady_clock::now()<deadline);
        check(published.current&&published.programs,"SSR program publication timed out: "+published.diagnostic);
        auto packet=std::make_shared<PreparedGraphMaterial>();packet->programs=published.programs;packet->uniforms=generated.uniforms;
        packet->sources=generated.textures;packet->runtimeGraph=generated.runtimeGraph;packet->runtimeParameters=generated.runtimeParameters;
        packet->shading=output().shading;packet->blend=output().blend;packet->receiveFog=output().receiveFog;
        packet->refraction=generated.refraction;packet->tessellation=generated.tessellation;packet->signature=published.programs->signature;
        for(const auto& source:generated.textures){packet->textures[source.slot]=texture;
            packet->flags[source.slot]=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;}
        return packet;
    };
    const auto flat=plane("ssr-flat-plane"),tilted=plane("ssr-tilted-normal-plane",{.6f,0,.8f});
    check(renderer.prepareModel(*flat)&&renderer.prepareModel(*tilted),renderer.modelStatus());
    std::vector<ModelMaterial> mirrorMaterials(1),donorMaterials(1),backMaterials(1);
    std::vector<ModelMaterialTextures> mirrorBindings(1);
    donorMaterials[0].doubleSided=backMaterials[0].doubleSided=true;
    donorMaterials[0].baseColor={.9f,.05f,.025f,1};backMaterials[0].baseColor={.025f,.025f,.025f,1};
    std::array<RenderModelInstance,3> instances;
    auto& mirror=instances[0];mirror.entityId=10;mirror.model={tilted,math::scaling({.7f,.7f,.7f})};
    mirror.materials=&mirrorMaterials;mirror.textures=&mirrorBindings;mirror.forceFullDetail=true;
    auto& donor=instances[1];donor.entityId=11;donor.materials=&donorMaterials;donor.shading=MaterialShading::Unlit;donor.forceFullDetail=true;
    const auto donorTransform=[&](float y=0.f){return math::translation({1.3f,y,0})*
        math::rotation(math::axisAngle({0,1,0},std::numbers::pi/2))*math::scaling({2.5f,1.5f,1});};
    donor.model={flat,donorTransform()};
    auto& backdrop=instances[2];backdrop.entityId=12;backdrop.model={flat,math::translation({0,0,-2})*math::scaling({4,3,1})};
    backdrop.materials=&backMaterials;backdrop.shading=MaterialShading::Unlit;backdrop.forceFullDetail=true;
    SceneSettings settings;settings.camera.position={0,0,3};settings.camera.target={0,0,0};settings.camera.fov=50;
    settings.camera.fovAxis=CameraFovAxis::Vertical;settings.camera.nearPlane=.05f;settings.camera.farPlane=30;
    settings.environment.background.show=false;settings.environment.toneMapping=ToneMapping::Linear;
    settings.antiAliasing=AntiAliasing::Off;settings.environment.ssr.quality=SsrQuality::High;
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances=instances;
    unsigned captureIndex{};
    const auto capture=[&](const char* name,bool game=true){
        if(game)renderer.renderGame(scene);else renderer.render(scene);
        check(renderer.ssrStatus().empty(),renderer.ssrStatus());
        auto image=read(game?renderer.gameViewportTexture():renderer.viewportTexture());
        const auto file=artifacts/(std::to_string(++captureIndex)+"-"+name+".png");
        check(lodepng::encode(file.string(),image.rgba,image.width,image.height)==0,"SSR fixture PNG write failed.");
        const auto rgb=center(image);std::cout<<name<<": center "<<rgb[0]<<", "<<rgb[1]<<", "<<rgb[2]<<'\n';
        return image;
    };
    const auto pair=[&](const char* off,const char* on){settings.environment.ssr.enabled=false;auto baseline=capture(off);
        settings.environment.ssr.enabled=true;return std::pair{std::move(baseline),capture(on)};};
    {
        // Reproduce the authored closed mirror rather than substituting a
        // shading-normal tilt on an open plane. Hard normal charts retain the
        // cube's six actual face normals, including with Smooth selected.
        const auto savedOutput=output();const auto savedSettings=settings;const auto savedInstances=scene.instances;
        const auto box=builtinBoxGeometry();check(renderer.prepareModel(*box),renderer.modelStatus());
        constexpr Vec3 authoredPosition{4.1101737f,.50492734f,1.0236609f};
        output().blend=MaterialBlend::Alpha;output().opacity=1;output().metallic=1;
        output().baseColor={1,1,1};output().doubleSided=false;output().refraction=false;
        std::vector<ModelMaterial> cubeMaterials(1),floorMaterials(1);
        std::vector<ModelMaterialTextures> cubeBindings(1),floorBindings(1);
        floorMaterials[0].metallic=0;floorMaterials[0].roughness=.42f;floorMaterials[0].baseColor={1,1,1,1};
        floorMaterials[0].uvScale={3,3,1};
        std::array<std::uint8_t,32*32*4> checker{};
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
            const auto p=(y*32+x)*4;const std::uint8_t value=((x/4+y/4)&1)?210:42;
            checker[p]=checker[p+1]=checker[p+2]=value;checker[p+3]=255;
        }
        Texture checkerTexture{bgfx::createTexture2D(32,32,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(checker.data(),std::uint32_t(checker.size())))};
        check(bgfx::isValid(checkerTexture.handle),"Closed mirror checker source allocation failed.");
        floorBindings[0].handles[0]=checkerTexture.handle;
        floorBindings[0].flags[0]=BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
        std::array<std::vector<ModelMaterial>,4> stepMaterials;
        std::array<RenderModelInstance,6> closedScene;
        auto& cube=closedScene[0];cube.entityId=0x53535243554245;cube.model={box,math::translation(authoredPosition)};
        cube.materials=&cubeMaterials;cube.textures=&cubeBindings;cube.meshShading=MeshShading::Smooth;cube.forceFullDetail=true;
        auto& ground=closedScene[1];ground.entityId=cube.entityId+1;
        ground.model={flat,math::translation({authoredPosition[0],0,authoredPosition[2]})*
            math::rotation(math::axisAngle({1,0,0},-std::numbers::pi/2))*math::scaling({6,6,1})};
        ground.materials=&floorMaterials;ground.textures=&floorBindings;ground.forceFullDetail=true;
        for(unsigned step=0;step<stepMaterials.size();++step) {
            const float height=.12f+.12f*step;
            stepMaterials[step].resize(1);stepMaterials[step][0].baseColor=step&1?Vec4{.95f,.08f,.025f,1}:Vec4{.6f,.04f,.015f,1};
            auto& tread=closedScene[step+2];tread.entityId=cube.entityId+2+step;tread.model={box,
                math::translation({authoredPosition[0]+1.05f+.5f*step,height*.5f,authoredPosition[2]-.65f})*
                math::scaling({.5f,height,2.0f})};
            tread.materials=&stepMaterials[step];tread.shading=MaterialShading::Unlit;tread.forceFullDetail=true;
        }
        scene.instances=closedScene;
        settings.environment.ssgi.enabled=settings.environment.ssao.enabled=settings.environment.glow.enabled=false;
        settings.environment.fog.enabled=false;settings.environment.ssr.quality=SsrQuality::Medium;
        settings.environment.ssr.strength=1;settings.environment.ssr.maxDistance=50;
        settings.environment.ssr.maxRoughness=.8f;settings.environment.ssr.bias=.0005f;settings.environment.ssr.depthTolerance=.059f;
        settings.camera.fov=45;settings.camera.nearPlane=.0006994245f;settings.camera.farPlane=2048;
        const std::array<Vec3,2> eyes{{{4.5f,2.6f,3.f},{3.2f,1.6f,2.4f}}};
        std::vector<std::string> closedFailures;
        const auto cross=[](const Vec3& a,const Vec3& b){return Vec3{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
        for(unsigned angle=0;angle<eyes.size();++angle) {
            settings.camera.position=authoredPosition+eyes[angle];settings.camera.target=authoredPosition;settings.camera.up={0,1,0};
            const auto forward=math::normalized(settings.camera.target-settings.camera.position);
            const auto right=math::normalized(cross(forward,settings.camera.up)),up=cross(right,forward);
            const float halfHeight=std::tan(settings.camera.fov*std::numbers::pi_v<float>/360);
            const auto cubePixel=[&](unsigned x,unsigned y) {
                const auto ray=math::normalized(forward+right*((2*(x+.5f)/Width-1)*halfHeight*Width/Height)+
                    up*((1-2*(y+.5f)/Height)*halfHeight));
                const auto eye=settings.camera.position-authoredPosition;float entry=0,exit=1000000;
                for(unsigned axis=0;axis<3;++axis) {
                    if(std::abs(ray[axis])<1e-8f){if(std::abs(eye[axis])>.5f)return false;continue;}
                    const float a=(-.5f-eye[axis])/ray[axis],b=(.5f-eye[axis])/ray[axis];
                    entry=std::max(entry,std::min(a,b));exit=std::min(exit,std::max(a,b));
                }
                if(exit<=entry||entry<=0)return false;
                const auto point=eye+ray*entry;unsigned nearEdges{};
                for(unsigned axis=0;axis<3;++axis)if(std::abs(point[axis])>.46f)++nearEdges;
                return nearEdges==1; // Exclude rasterized edges/corners, retain all three visible faces.
            };
            double sharpInteriorContrast{};
            const auto decode=[](std::uint8_t code){const double value=code/255.;
                return value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4);};
            for(float roughness:{.045f,.09f,.10f,.116f,.15f}) {
                output().roughness=roughness;cubeBindings[0].graph=prepare();
                const auto label="closed-mirror-angle-"+std::to_string(angle)+"-roughness-"+std::to_string(roughness);
                const auto [without,with]=pair((label+"-off").c_str(),(label+"-on").c_str());
                double maximumResponse{},meanResponse{},maximumNeighborJump{};unsigned samples{},changed{};
                for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x)if(cubePixel(x,y)) {
                    const auto p=(std::size_t(y)*Width+x)*4;double response{};
                    for(unsigned c=0;c<3;++c)response=std::max(response,double(std::abs(int(with.rgba[p+c])-int(without.rgba[p+c]))));
                    maximumResponse=std::max(maximumResponse,response);meanResponse+=response;++samples;if(response>2)++changed;
                    if(x+1<Width&&cubePixel(x+1,y))for(unsigned c=0;c<3;++c)maximumNeighborJump=std::max(maximumNeighborJump,
                        double(std::abs((int(with.rgba[p+c])-int(without.rgba[p+c]))-
                        (int(with.rgba[p+4+c])-int(without.rgba[p+4+c])))));
                }
                std::cout<<label<<": max response="<<maximumResponse<<", mean="<<meanResponse/samples
                    <<", changed="<<changed<<'/'<<samples<<", max correction neighbor jump="<<maximumNeighborJump<<'\n';
                check(samples>100,"Closed mirror camera did not cover a meaningful receiver area.");
                if(maximumResponse<=3)closedFailures.push_back(label+": closed Alpha mirror lost every floor/step donor response.");
                // Changing the known red treads to green must change their
                // reflected footprint, independently of the gray environment.
                for(auto& material:stepMaterials)material[0].baseColor={.025f,.95f,.04f,1};
                const auto green=capture((label+"-green-steps").c_str());double coloredResponse{};
                for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x)if(cubePixel(x,y)) {
                    const auto p=(std::size_t(y)*Width+x)*4;
                    coloredResponse=std::max(coloredResponse,double(int(green.rgba[p+1])-int(with.rgba[p+1])));
                }
                std::cout<<label<<": known donor green response="<<coloredResponse<<'\n';
                if(coloredResponse<=3)closedFailures.push_back(label+": closed mirror failed to reflect its visible opaque step donor.");
                double interiorContrast{};
                for(int y=int(Height/2)-3;y<=int(Height/2)+3;++y)for(int x=int(Width/2)-3;x<=int(Width/2)+3;++x) {
                    check(cubePixel(unsigned(x),unsigned(y)),"Closed mirror donor probe left its interior.");
                    const auto p=(std::size_t(y)*Width+std::size_t(x))*4+1;
                    interiorContrast+=(decode(green.rgba[p])-decode(with.rgba[p]))/49.;
                }
                if(roughness==.09f){sharpInteriorContrast=interiorContrast;
                    check(sharpInteriorContrast>.1,"Closed mirror sharp reference missed the broad stair interior.");}
                if(roughness>=.1f) {
                    const double retained=interiorContrast/sharpInteriorContrast,minimum=roughness<.15f?.6:.4;
                    std::cout<<label<<": linear broad-donor contrast="<<interiorContrast<<", retained versus .09="<<retained<<'\n';
                    if(retained<minimum)closedFailures.push_back(label+": rough branch retained only "+std::to_string(retained)+
                        " of the broad uniform donor's linear contrast; minimum="+std::to_string(minimum));
                }
                for(unsigned step=0;step<stepMaterials.size();++step)stepMaterials[step][0].baseColor=
                    step&1?Vec4{.95f,.08f,.025f,1}:Vec4{.6f,.04f,.015f,1};
            }
        }
        output()=savedOutput;settings=savedSettings;scene.instances=savedInstances;
        if(!closedCubeOnly)for(unsigned i=0;i<9;++i){renderer.renderGame(scene);tick();}
        check(closedFailures.empty(),"Closed mirror reflection regression: "+
            (closedFailures.empty()?std::string{}:closedFailures.front())+" ("+std::to_string(closedFailures.size())+" failures).");
    }
    if(closedCubeOnly) {
        resources.stop();frame=renderer.drainPending(frame);
        std::cout<<"Closed Alpha mirror/floor/visible step donor fixture passed. Artifacts: "<<artifacts.string()<<'\n';return;
    }
    mirrorBindings[0].graph=prepare();
    settings.environment.ssr.enabled=false;const auto disabled=capture("opaque-disabled");
    check(renderer.ssrResidentBytes()==0,"Disabled SSR allocated view resources.");
    settings.environment.ssr.enabled=true;const auto reflected=capture("opaque-reflection");
    const auto base=center(disabled),hit=center(reflected);
    check(hit[0]>base[0]+10&&hit[1]<base[1]-5,"Opaque mirror did not replace environment radiance with the visible red donor.");
    // This horizontal mirror ray lies on a native Hi-Z cell boundary. Tiny
    // negative projected Y deltas must not stall on that same boundary until
    // the iteration budget expires and leave an environment-colored seam.
    for(unsigned x=Width/2-8;x<=Width/2+8;++x){
        const auto red=[&](unsigned y){return int(reflected.rgba[(std::size_t(y)*Width+x)*4]);};
        const double neighbor=.5*(red(Height/2-1)+red(Height/2+1));
        check(std::abs(red(Height/2)-neighbor)<=2,"Horizontal Hi-Z cell-boundary seam at center-row x="+std::to_string(x)+
            ": center="+std::to_string(red(Height/2))+", neighbor="+std::to_string(neighbor));
    }
    {
        // Transpose the optical arrangement: N=(0,.6,.8), R=(0,.96,.28).
        // Y=1 puts the donor hit at Z≈.292, inside the vertical camera frustum.
        const auto mirrorTransform=mirror.model.transform,savedDonorTransform=donor.model.transform;
        mirror.model.transform=math::rotation(math::axisAngle({0,0,1},std::numbers::pi/2))*math::scaling({.7f,.7f,.7f});
        donor.model.transform=math::translation({0,1,0})*math::rotation(math::axisAngle({1,0,0},std::numbers::pi/2))*math::scaling({1.5f,2.5f,1});
        const auto [verticalOff,verticalOn]=pair("vertical-ray-disabled","vertical-ray-reflection");
        check(center(verticalOn)[0]>center(verticalOff)[0]+10,"Vertical mirror fixture did not hit its visible opaque donor.");
        for(unsigned y=Height/2-8;y<=Height/2+8;++y){
            const auto red=[&](unsigned x){return int(verticalOn.rgba[(std::size_t(y)*Width+x)*4]);};
            const double neighbor=.5*(red(Width/2-1)+red(Width/2+1));
            check(std::abs(red(Width/2)-neighbor)<=2,"Vertical Hi-Z cell-boundary seam at center-column y="+std::to_string(y)+
                ": center="+std::to_string(red(Width/2))+", neighbor="+std::to_string(neighbor));
        }
        mirror.model.transform=mirrorTransform;donor.model.transform=savedDonorTransform;
    }
    {
        // A rough dielectric floor has no visible reflected donor here. With
        // a very small editor near plane, depth quantization must not turn
        // samples of the receiver itself into apparent reflected geometry.
        const auto savedOutput=output();const auto savedPacket=mirrorBindings[0].graph;
        const auto savedModel=mirror.model;const auto savedCamera=settings.camera;
        const auto savedSsr=settings.environment.ssr;const auto savedInstances=scene.instances;
        output().metallic=0;output().roughness=.42f;output().baseColor={.04f,.04f,.04f};
        mirrorBindings[0].graph=prepare();
        scene.instances={instances.data(),1};settings.camera.farPlane=2048;
        settings.environment.ssr.bias=.0005f;settings.environment.ssr.depthTolerance=.059f;settings.environment.ssr.maxRoughness=.51f;
        for(float scale:{1.f,4.f}) {
            mirror.model={flat,math::rotation(math::axisAngle({1,0,0},-std::numbers::pi/2))*math::scaling({5*scale,8*scale,1})};
            settings.camera.position={0,2.2f*scale,5*scale};settings.camera.target={0,.4f*scale,0};
            for(float nearPlane:{.05f,.0006994245f}) {
                settings.camera.nearPlane=nearPlane;
                const auto [floorOff,floorOn]=pair(nearPlane>.01f?"rough-floor-normal-near-off":"rough-floor-tiny-near-off",
                    nearPlane>.01f?"rough-floor-normal-near-on":"rough-floor-tiny-near-on");
                const auto change=difference(floorOff,floorOn);
                std::cout<<"Donor-free rough floor scale="<<scale<<", near="<<nearPlane<<": SSR difference="<<change<<'\n';
                check(change<.5,"SSR accepted quantized samples of a donor-free rough floor as reflection hits.");
            }
        }
        // Upright donors reflect into actual horizontal geometry. Roughness
        // must spread angular visibility, and distant editor clipping must not
        // change the reflected footprints or introduce stair-like surface hits.
        std::array<std::vector<ModelMaterial>,3> floorDonorMaterials;
        std::array<RenderModelInstance,4> floorInstances;
        floorInstances[0]=mirror;
        const std::array<Vec4,3> floorColors{{{.95f,.03f,.015f,1},{.015f,.95f,.03f,1},{.03f,.015f,.95f,1}}};
        for(unsigned i=0;i<3;++i){floorDonorMaterials[i].resize(1);floorDonorMaterials[i][0].baseColor=floorColors[i];floorDonorMaterials[i][0].doubleSided=true;
            auto& panel=floorInstances[i+1];panel.entityId=100+i;panel.model={flat,math::translation({(float(i)-1)*6,4,-6})*math::scaling({2,4,1})};
            panel.materials=&floorDonorMaterials[i];panel.shading=MaterialShading::Unlit;panel.forceFullDetail=true;}
        scene.instances=floorInstances;settings.environment.ssr.maxRoughness=.6f;
        for(float roughness:{.045f,.35f,.42f}){
            output().roughness=roughness;mirrorBindings[0].graph=prepare();settings.camera.nearPlane=.05f;
            const auto [roughFloorOff,roughFloorOn]=pair("dielectric-floor-donors-off","dielectric-floor-donors-on");
            double maximumResponse=0,meanResponse=0;unsigned samples=0;
            for(unsigned y=Height/2;y<Height*9/10;++y)for(unsigned x=Width/5;x<Width*4/5;++x)for(unsigned channel=0;channel<3;++channel){
                const auto p=(std::size_t(y)*Width+x)*4+channel;const auto response=std::abs(int(roughFloorOn.rgba[p])-int(roughFloorOff.rgba[p]));
                maximumResponse=std::max(maximumResponse,double(response));meanResponse+=response;++samples;}
            meanResponse/=samples;
            std::cout<<"Dielectric floor roughness="<<roughness<<": max response="<<maximumResponse<<", mean="<<meanResponse<<'\n';
            check(maximumResponse>2,"Rough dielectric floor lost all valid reflected donor response.");
            settings.camera.nearPlane=.0006994245f;const auto tinyNear=capture("dielectric-floor-tiny-near");
            check(difference(roughFloorOn,tinyNear)<1,"Editor near-plane precision changed the dielectric reflection footprint.");
        }
        output()=savedOutput;mirrorBindings[0].graph=savedPacket;mirror.model=savedModel;
        settings.camera=savedCamera;settings.environment.ssr=savedSsr;scene.instances=savedInstances;
    }
    check(renderer.ssrResidentBytes()>0,"Active SSR did not allocate a source and depth hierarchy.");
    for(const auto quality:{SsrQuality::Low,SsrQuality::Medium,SsrQuality::High}){
        settings.environment.ssr.quality=quality;check(center(capture("hierarchy-quality"))[0]>base[0]+5,"A supported hierarchy budget missed the broad mirror donor.");}
    settings.environment.ssr.quality=SsrQuality::High;
    donorMaterials[0].baseColor={0,0,0,1};const auto black=capture("signed-dark-correction");
    check(center(black)[0]<base[0]-15,"SSR could not subtract the existing bright environment contribution.");
    donorMaterials[0].baseColor={.9f,.05f,.025f,1};
    settings.environment.ssgi.enabled=true;settings.environment.ssgi.quality=SsgiQuality::Low;settings.environment.ssao.enabled=true;
    const auto [combinedOff,combinedOn]=pair("gi-ao-ssr-disabled","gi-ao-ssr-enabled");
    check(renderer.ssgiStatus().empty()&&renderer.ssaoStatus().empty(),"SSR combined GI/AO preparation failed: "+renderer.ssgiStatus()+renderer.ssaoStatus());
    check(center(combinedOn)[0]>center(combinedOff)[0]+5,"Five-target GI/AO capture lost the independent SSR source.");
    settings.environment.ssgi.enabled=false;settings.environment.ssao.enabled=false;
    {
        auto hdrEnvironment=environment;hdrEnvironment.key^=0x484452;
        hdrEnvironment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{10000,10000,10000})},hdrEnvironment.settings);
        check(renderer.publishEnvironment(hdrEnvironment),renderer.environmentStatus());
        settings.environment.intensity=10;settings.exposure=.00001f;donorMaterials[0].baseColor={0,0,0,1};
        // A tiny positive strength retains the participating unsaturated HDR
        // path. Strength zero intentionally preserves the old disabled path.
        settings.environment.ssr.strength=.000001f;const auto highBaseline=capture("high-hdr-negligible-correction");
        settings.environment.ssr.strength=.5f;const auto highHalf=capture("high-hdr-half-dark-correction");
        const auto linear=[](float code){const float v=code/255;return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);};
        const float original=linear(center(highBaseline)[0]),corrected=linear(center(highHalf)[0]);
        check(original>.7f&&original<.95f,"High-HDR fixture did not exceed the former 60000 storage bound before exposure.");
        check(corrected/original>.45f&&corrected/original<.55f,"Premature HDR saturation broke half-strength signed environment replacement.");
        check(renderer.publishEnvironment(environment),renderer.environmentStatus());settings.environment.intensity=1;settings.exposure=1;
        settings.environment.ssr.strength=1;donorMaterials[0].baseColor={.9f,.05f,.025f,1};
    }
    settings.environment.ssr.strength=0;check(difference(capture("zero-strength"),disabled)<1.2,"Zero-strength SSR changed the baseline.");
    for(unsigned i=0;i<5;++i)tick();check(renderer.ssrResidentBytes()==0,"Zero-strength SSR retained retired allocations.");settings.environment.ssr.strength=1;
    settings.environment.ssr.maxDistance=.2f;check(difference(capture("short-distance-miss"),disabled)<1.2,"Ray distance limit accepted an out-of-range donor.");settings.environment.ssr.maxDistance=50;
    mirror.model.asset=flat;check(difference(capture("receiver-normal-miss"),disabled)<1.5,"SSR used a donor/opaque normal instead of the receiver's own normal.");mirror.model.asset=tilted;
    for(const auto blend:{MaterialBlend::Alpha,MaterialBlend::Additive}){
        donor.blend=blend;const auto [without,with]=pair("transparent-donor-disabled","transparent-donor-enabled");
        check(difference(with,without)<1.5,"Transparent/Additive geometry entered the opaque SSR donor set.");}
    donor.blend=MaterialBlend::Opaque;
    const auto stable=capture("feedback-frame-one");for(unsigned i=0;i<4;++i){(void)capture("feedback-warm-frame");}
    check(difference(capture("feedback-frame-last"),stable)<1.1,"SSR fed reflected output back into its immutable source.");
    donor.model.transform=donorTransform(4);check(difference(capture("moving-donor-gone"),disabled)<1.5,"A moved donor left a previous-frame reflection.");
    donor.model.transform=donorTransform();check(difference(capture("moving-donor-return"),reflected)<1.5,"Current donor motion failed to restore the reflection.");
    {
        auto deformed=plane("ssr-deforming-donor");deformed->draws[0].node=0;deformed->nodes.resize(1);deformed->nodes[0].mesh=0;
        ModelMorphTarget morph;morph.positions.assign(4,Vec3{0,4,0});deformed->primitives[0].morphTargets.push_back(std::move(morph));
        check(renderer.prepareModel(*deformed),renderer.modelStatus());donor.model.asset=deformed;
        for(const float weight:{0.f,1.f}){auto pose=std::make_shared<ModelPose>();pose->localTransforms.resize(1);pose->worldTransforms.resize(1);
            pose->nodeWeights={{weight}};pose->skinPalettes.resize(1);pose->drawTransforms.resize(1);pose->bounds={{-1,-1,0},{1,5,0},true};
            pose->signature=weight>0?0x53535202:0x53535201;donor.pose=pose;const auto image=capture(weight>0?"morph-donor-gone":"morph-donor-visible");
            if(weight>0)check(difference(image,disabled)<1.5,"Current GPU morph left a stale SSR source/depth hit.");
            else check(center(image)[0]>base[0]+5,"GPU-morphed opaque geometry failed to supply SSR.");}
        auto skinned=plane("ssr-skinned-donor");skinned->draws[0].node=0;skinned->nodes.resize(1);skinned->nodes[0].mesh=0;skinned->nodes[0].skin=0;
        skinned->skins.resize(1);skinned->skins[0].joints={0};skinned->skins[0].inverseBindMatrices.resize(1);
        auto& primitive=skinned->primitives[0];primitive.hasSkinning=true;primitive.skinWeights.resize(4);
        for(auto& weights:primitive.skinWeights)weights.weights[0]=1;
        check(renderer.prepareModel(*skinned),renderer.modelStatus());donor.model.asset=skinned;
        for(const float offset:{0.f,4.f}){auto pose=std::make_shared<ModelPose>();pose->localTransforms.resize(1);pose->worldTransforms.resize(1);
            pose->nodeWeights.resize(1);pose->skinPalettes={{math::translation({0,offset,0})}};pose->drawTransforms.resize(1);
            pose->bounds={{-1,-1,0},{1,5,0},true};pose->signature=offset>0?0x53535204:0x53535203;donor.pose=pose;
            const auto image=capture(offset>0?"skin-donor-gone":"skin-donor-visible");
            if(offset>0)check(difference(image,disabled)<1.5,"Current GPU skinning left a stale SSR source/depth hit.");
            else check(center(image)[0]>base[0]+5,"GPU-skinned opaque geometry failed to supply SSR.");}
        donor.pose.reset();donor.model.asset=flat;
    }
    settings.environment.fog.enabled=true;settings.environment.fog.density=.2f;settings.environment.fog.color={.1f,.2f,.9f};
    const auto [fogOff,fogOn]=pair("fog-ssr-disabled","fog-ssr-enabled");
    const auto decode=[](float code){const float v=code/255;return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);};
    const float clearDelta=decode(hit[0])-decode(base[0]);const float fogDelta=decode(center(fogOn)[0])-decode(center(fogOff)[0]);
    check(clearDelta>0&&fogDelta/clearDelta>.42f&&fogDelta/clearDelta<.68f,"SSR fog correction did not apply receiver transmittance exactly once.");
    output().receiveFog=false;mirrorBindings[0].graph=prepare();const auto [excludedOff,excludedOn]=pair("receive-fog-off-disabled","receive-fog-off-enabled");
    check(std::abs((decode(center(excludedOn)[0])-decode(center(excludedOff)[0]))-clearDelta)<.035f,"Receive Fog off attenuated the reflection correction.");
    output().receiveFog=true;settings.environment.fog.enabled=false;
    output().blend=MaterialBlend::Alpha;output().metallic=0;output().baseColor={0,0,0};output().roughness=.045f;
    output().refraction=true;output().ior=1.5f;output().transmission=1;output().thickness=.15f;mirrorBindings[0].graph=prepare();
    const auto [glassOff,glassOn]=pair("full-transmission-disabled","full-transmission-reflection");
    check(center(glassOn)[0]>center(glassOff)[0]+3,"Full optical transmission removed the glass's independent reflection.");
    mirror.model.asset=flat;const auto [flatGlassOff,flatGlassOn]=pair("flat-glass-disabled","flat-glass-miss");
    check(difference(flatGlassOn,flatGlassOff)<1.5,"Glass used opaque depth/normal underneath itself as the reflection receiver.");mirror.model.asset=tilted;
    output().ior=1;mirrorBindings[0].graph=prepare();const auto [indexOff,indexOn]=pair("index-matched-disabled","index-matched-enabled");
    check(difference(indexOn,indexOff)<1.1,"Index-matched glass acquired a dielectric SSR reflection.");
    scene.instances={};const auto emptyMatte=capture("empty-matte");scene.instances=std::span(&mirror,1);
    check(difference(capture("index-matched-matte"),emptyMatte)<1.1,"SSR/glass composition changed the empty display matte.");scene.instances=instances;
    output().ior=1.5f;output().opacity=0;mirrorBindings[0].graph=prepare();const auto [zeroOff,zeroOn]=pair("zero-coverage-disabled","zero-coverage-enabled");
    check(difference(zeroOn,zeroOff)<1.1,"Zero coverage contributed an SSR correction or changed the display matte.");
    output().opacity=.5f;output().refraction=false;output().metallic=1;output().baseColor={.8f,.8f,.8f};mirrorBindings[0].graph=prepare();
    const auto [alphaOff,alphaOn]=pair("ordinary-alpha-disabled","ordinary-alpha-reflection");
    check(center(alphaOn)[0]>center(alphaOff)[0]+5,"Ordinary Lit Alpha did not receive SSR.");
    output().blend=MaterialBlend::Mask;output().opacityMask=1;output().opacity=1;mirrorBindings[0].graph=prepare();
    const auto [maskOff,maskOn]=pair("cutout-disabled","cutout-reflection");check(center(maskOn)[0]>center(maskOff)[0]+5,"Cutout receiver coverage disagreed with SSR depth.");
    output().opacityMask=0;mirrorBindings[0].graph=prepare();const auto [holeOff,holeOn]=pair("cutout-hole-disabled","cutout-hole-enabled");
    check(difference(holeOn,holeOff)<1.1,"A clipped receiver still contributed an SSR correction.");output().opacityMask=1;
    output().blend=MaterialBlend::Additive;mirrorBindings[0].graph=prepare();const auto [additiveOff,additiveOn]=pair("additive-receiver-disabled","additive-receiver-enabled");
    check(difference(additiveOn,additiveOff)<1.2,"Additive receiver unexpectedly opted into SSR.");
    output().blend=MaterialBlend::Opaque;output().shading=MaterialShading::Unlit;mirrorBindings[0].graph=prepare();const auto [unlitOff,unlitOn]=pair("unlit-receiver-disabled","unlit-receiver-enabled");
    check(difference(unlitOn,unlitOff)<1.2,"Unlit receiver unexpectedly opted into SSR.");output().shading=MaterialShading::StandardPbr;
    // A texture-driven material normal must steer the reflection exactly as an
    // authored vertex normal does; opaque depth at this pixel supplies no such normal.
    output().blend=MaterialBlend::Opaque;const auto normalNode=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(asset.graph,normalNode)->texture.role=MaterialTextureRole::Normal;
    findMaterialNode(asset.graph,normalNode)->texture.image.id="assets/textures/ssr-normal.png";
    connectMaterialNodes(asset.graph,normalNode,MaterialPin::Normal,outputId,MaterialPin::Normal);
    const std::array<std::uint8_t,4> normalPixel{204,128,230,255};Texture normalTexture{bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(normalPixel.data(),4))};
    mirror.model.asset=flat;mirrorBindings[0].graph=prepare(normalTexture.handle);const auto [normalOff,normalOn]=pair("normal-map-disabled","normal-map-reflection");
    check(center(normalOn)[0]>center(normalOff)[0]+5,"Texture-driven receiver normals did not steer SSR.");
    asset.graph.links.clear();mirror.model.asset=tilted;
    // Twelve distinct live graph bindings leave exactly the four published SSR bindings.
    MaterialNodeId previous{};
    for(unsigned slot=0;slot<12;++slot){const auto texture=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D).id;
        auto* node=findMaterialNode(asset.graph,texture);node->texture.role=MaterialTextureRole::Data;node->texture.image.id="assets/textures/ssr-slot-"+std::to_string(slot)+".png";
        if(!previous)previous=texture;
        else {const auto product=addMaterialNode(asset.graph,MaterialNodeKind::Multiply).id;
            findMaterialNode(asset.graph,product)->left.type=findMaterialNode(asset.graph,product)->right.type=MaterialValueType::Color;
            connectMaterialNodes(asset.graph,previous,findMaterialNode(asset.graph,previous)->kind==MaterialNodeKind::Texture2D?MaterialPin::RGB:MaterialPin::Value,product,MaterialPin::Left);
            connectMaterialNodes(asset.graph,texture,MaterialPin::RGB,product,MaterialPin::Right);previous=product;}}
    connectMaterialNodes(asset.graph,previous,MaterialPin::Value,outputId,MaterialPin::BaseColor);
    const std::array<std::uint8_t,4> whitePixel{255,255,255,255};Texture whiteTexture{bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(whitePixel.data(),4))};
    mirrorBindings[0].graph=prepare(whiteTexture.handle);check(mirrorBindings[0].graph->sources.size()==12,"SSR fixture did not exercise all twelve graph slots.");
    const auto [slotsOff,slotsOn]=pair("twelve-slots-disabled","twelve-slots-reflection");
    check(center(slotsOn)[0]>center(slotsOff)[0]+5,"SSR reduced the twelve-texture material budget or bound the wrong source.");
    asset.graph.links.clear();output().baseColor={.8f,.8f,.8f};output().roughness=.045f;
    output().tessellation.enabled=selectedTestGraphicsBackend()==GraphicsBackend::Direct3D12;
    output().tessellation.maxFactor=4;output().tessellation.amount=.1f;output().height=.6f;
    mirrorBindings[0].graph=prepare();const auto [heightOff,heightOn]=pair("height-disabled","height-reflection");
    check(center(heightOn)[0]>center(heightOff)[0]+5,"Tessellated reflection geometry disagreed with the lit/depth surface.");output().tessellation.enabled=false;
    // Thin alternating donors force rough-source filtering and hierarchy depth
    // bounds to agree across silhouettes at the odd native view extent.
    std::array<std::vector<ModelMaterial>,18> stripeMaterials;std::vector<RenderModelInstance> stripes;stripes.reserve(20);
    stripes.push_back(mirror);stripes.push_back(backdrop);
    for(unsigned i=0;i<stripeMaterials.size();++i){stripeMaterials[i].resize(1);stripeMaterials[i][0].doubleSided=true;
        stripeMaterials[i][0].baseColor=i%2?Vec4{.9f,.05f,.025f,1}:Vec4{.025f,.025f,.025f,1};
        auto stripe=donor;stripe.entityId=100+i;stripe.materials=&stripeMaterials[i];
        stripe.model.transform=math::translation({1.3f,-1.2f+(i+.5f)*2.4f/18,0})*
            math::rotation(math::axisAngle({0,1,0},std::numbers::pi/2))*math::scaling({2.5f,1.2f/18,1});stripes.push_back(stripe);}
    mirrorBindings[0].graph=prepare();scene.instances=stripes;const auto sharp=capture("thin-donor-sharp");
    check(variation(sharp)>3,"Sharp SSR failed to resolve the alternating thin opaque donors.");
    output().roughness=.35f;mirrorBindings[0].graph=prepare();const auto [roughOff,rough]=pair("thin-donor-rough-disabled","thin-donor-rough");
    check(difference(rough,roughOff)>1.5,"Rough SSR lost all visible donor response instead of filtering the reflection.");
    check(variation(rough)<=variation(sharp)+1.5,"Rough SSR created stronger secondary silhouette edges than the sharp reflection.");
    output().roughness=.045f;mirrorBindings[0].graph=prepare();scene.instances=instances;
    // A translated camera changes the perspective ray without history reprojection.
    const auto camera=settings.camera;settings.camera.position={.15f,.08f,3};settings.camera.target={.15f,.08f,0};
    const auto [perspectiveOff,perspectiveOn]=pair("perspective-disabled","perspective-reflection");
    check(center(perspectiveOn)[0]>center(perspectiveOff)[0]+5,"Off-axis perspective ray interpolation lost a valid mirror hit.");settings.camera=camera;
    const auto game=capture("independent-game");settings.environment.ssr.enabled=false;(void)capture("independent-scene-off",false);
    settings.environment.ssr.enabled=true;check(difference(capture("independent-game-restored"),game)<1.2,"Scene rendering overwrote Game SSR inputs.");
    settings.environment.ssr.enabled=false;renderer.render(scene);settings.environment.ssr.enabled=true;(void)capture("independent-scene-on",false);
    renderer.resizeMaterialPreview(Width,Height);renderer.renderMaterialPreview(settings,asset,mirrorBindings[0],MaterialPreviewMesh::Sphere,Mat4{},true,mirrorBindings[0].graph);
    check(renderer.ssrStatus().empty(),renderer.ssrStatus());(void)read(renderer.materialPreviewTexture());
    RenderParticle particle;particle.size=1.4f;particle.right={.8f,0,-.6f};particle.up={0,1,0};
    RenderParticleMaterial particleMaterial;particleMaterial.shading=MaterialShading::StandardPbr;particleMaterial.blend=MaterialBlend::Alpha;
    particleMaterial.doubleSided=true;output().blend=MaterialBlend::Alpha;output().refraction=true;output().ior=1.5f;output().transmission=1;
    particleMaterial.graph=prepare();scene.instances=std::span(instances.data()+1,2);scene.particles=std::span(&particle,1);scene.particleMaterials=std::span(&particleMaterial,1);
    const auto [spriteOff,spriteOn]=pair("sprite-disabled","sprite-reflection");check(center(spriteOn)[0]>center(spriteOff)[0]+2,"Lit refractive sprite did not receive its own SSR reflection.");
    renderer.resizeParticlePreview(Width,Height);renderer.renderParticlePreview(settings,std::span(&particle,1),std::span(&particleMaterial,1));
    check(renderer.ssrStatus().empty(),renderer.ssrStatus());(void)read(renderer.particlePreviewTexture());
    scene.particles={};scene.particleMaterials={};scene.instances=instances;
    constexpr std::uint16_t ThumbnailSize=128;check(renderer.renderThumbnail("ssr-contract-thumbnail",17,scene,ThumbnailSize,mirrorBindings[0].graph),renderer.thumbnailStatus());
    std::optional<ThumbnailResult> thumbnail;for(unsigned i=0;i<120&&!thumbnail;++i){tick();thumbnail=renderer.pollThumbnail(frame);}
    check(thumbnail&&thumbnail->id=="ssr-contract-thumbnail"&&thumbnail->signature==17&&thumbnail->image.width==ThumbnailSize,"SSR thumbnail identity/readback failed.");
    check(renderer.ssrStatus().empty(),renderer.ssrStatus());
    renderer.releaseMaterialPreview();renderer.releaseParticlePreview();settings.environment.ssr.enabled=false;
    scene.instances={};for(unsigned i=0;i<9;++i){renderer.render(scene);renderer.renderGame(scene);tick();}
    check(renderer.ssrResidentBytes()==0,"Disabled SSR retained view sources or retired hierarchy allocations.");
    {
        constexpr std::uint16_t CostWidth=1920,CostHeight=1080;
        renderer.resizeGameViewport(CostWidth,CostHeight);scene.instances=instances;
        settings.environment.ssr.quality=SsrQuality::Medium;settings.environment.ssgi.enabled=false;
        settings.environment.ssao.enabled=false;settings.environment.glow.enabled=false;settings.antiAliasing=AntiAliasing::Off;
        const auto median=[](auto values){std::sort(values.begin(),values.end());return values[values.size()/2];};
        const auto measure=[&](bool enabled){settings.environment.ssr.enabled=enabled;
            for(unsigned i=0;i<20;++i){renderer.renderGame(scene);tick();}
            check(renderer.ssrStatus().empty(),renderer.ssrStatus());
            std::vector<double> elapsed;std::vector<std::uint32_t> draws,computes;
            std::uint32_t previousGpuFrame=UINT32_MAX;bool available=false;
            for(unsigned i=0;i<80;++i){renderer.renderGame(scene);tick();const auto* stats=bgfx::getStats();
                draws.push_back(stats->numDraw);computes.push_back(stats->numCompute);
                available=stats->gpuTimerFreq>0&&stats->gpuTimeEnd>stats->gpuTimeBegin;
                if(available&&stats->gpuFrameNum!=previousGpuFrame){previousGpuFrame=stats->gpuFrameNum;
                    elapsed.push_back(double(stats->gpuTimeEnd-stats->gpuTimeBegin)*1000/double(stats->gpuTimerFreq));}
                if(i>=19&&(elapsed.size()>=20||!available))break;
            }
            std::cout<<"SSR cost 1920x1080 Medium "<<(enabled?"on":"off")<<": draws="<<median(draws)<<", compute="<<median(computes)
                <<", resident="<<double(renderer.ssrResidentBytes())/(1024*1024)<<" MiB";
            if(!elapsed.empty())std::cout<<", GPU elapsed median="<<median(elapsed)<<" ms ("<<elapsed.size()<<" distinct frames, range="
                <<*std::min_element(elapsed.begin(),elapsed.end())<<".."<<*std::max_element(elapsed.begin(),elapsed.end())<<" ms)";
            else std::cout<<", GPU timer unavailable";
            std::cout<<". GPU elapsed is a timestamp interval, not active execution time.\n";
            return std::pair{median(draws),median(computes)};
        };
        const auto off=measure(false);check(renderer.ssrResidentBytes()==0,"Disabled 1080p SSR allocated effect storage.");
        const auto on=measure(true);const auto expected=ssrAllocationBytes(CostWidth,CostHeight);
        check(renderer.ssrResidentBytes()==expected,"1080p SSR memory disagreed with one independent source/depth/scratch owner.");
        check(on.first>off.first&&on.second>off.second,"Active 1080p SSR recorded no additional reflection/hierarchy work.");
        settings.environment.ssr.enabled=false;scene.instances={};renderer.renderGame(scene);
        std::cout<<"SSR retired allocation immediately after disable: "<<double(renderer.ssrResidentBytes())/(1024*1024)<<" MiB; expected active allocation "
            <<double(expected)/(1024*1024)<<" MiB.\n";
        tick();for(unsigned i=0;i<9;++i){renderer.renderGame(scene);tick();}
        check(renderer.ssrResidentBytes()==0,"Disabled 1080p SSR retained active or retired memory.");
    }
    resources.stop();frame=renderer.drainPending(frame);
    std::cout<<"SSR signed replacement, opaque donors, current motion, own normals, optical identities, coverage, fog, twelve slots, cutout/tessellation, perspective, rough silhouettes and independent views passed. Artifacts: "<<artifacts.string()<<'\n';
}
}
int main(int argc,char** argv){try{
    check(argc==5||(argc==6&&std::string_view(argv[5])=="--closed-cube-only"),
        "Pass shaderc, source/include directories, an artifact output directory and optional --closed-cube-only.");
    const auto cache=fs::temp_directory_path()/("crimson-ssr-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup{fs::path path;~Cleanup(){std::error_code error;const auto resolved=fs::weakly_canonical(path,error),root=fs::weakly_canonical(fs::temp_directory_path(),error);
        if(!error&&resolved.parent_path()==root&&resolved.filename().string().starts_with("crimson-ssr-"))fs::remove_all(resolved,error);}} cleanup{cache};
    ShaderCompileConfig config{argv[1],argv[2],argv[3],cache};
    check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());struct Quit{~Quit(){SDL_Quit();}} quit;
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson SSR checks",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
    check(bool(window),SDL_GetError());exercise(window.get(),config,fs::path(argv[4])/(selectedTestGraphicsBackend()==GraphicsBackend::Direct3D11?"dx11":"dx12"),argc==6);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
