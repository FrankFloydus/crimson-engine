#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/Picking.h"
#include "resources/ResourceManager.h"
#include "GpuTestBackend.h"
#include <crimson/renderer/forward/GlowGpu.h>
#include <crimson/renderer/forward/ToneMapping.h>
#include "cs_glow_downsample.h"
#include "cs_glow_upsample.h"
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <lodepng.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Sdl {Sdl(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~Sdl(){SDL_Quit();}};
struct Options {bool benchmark{},levelResponseOnly{},hdrSourceOnly{},referenceUnbounded{};unsigned warmup{240},samples{600};fs::path artifacts;};
struct Texture {bgfx::TextureHandle value{BGFX_INVALID_HANDLE};~Texture(){if(bgfx::isValid(value))bgfx::destroy(value);}};
struct Program {bgfx::ProgramHandle value{BGFX_INVALID_HANDLE};~Program(){if(bgfx::isValid(value))bgfx::destroy(value);}};
double linear(unsigned value) {const double v=value/255.;return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);}
double luminance(const DecodedImage& image,std::size_t p) {return .2126*linear(image.rgba[p*4])+.7152*linear(image.rgba[p*4+1])+.0722*linear(image.rgba[p*4+2]);}
unsigned maximumDifference(const DecodedImage& a,const DecodedImage& b) {
    check(a.width==b.width&&a.height==b.height,"Readback dimensions changed.");unsigned result{};
    for(std::size_t i=0;i<a.rgba.size();++i)result=std::max(result,unsigned(std::abs(int(a.rgba[i])-int(b.rgba[i]))));return result;
}
Json haloMetrics(const DecodedImage& image,const DecodedImage& base) {
    double energy{},radius{},horizontal{},vertical{};const double cx=(image.width-1)*.5,cy=(image.height-1)*.5;
    for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x){const auto p=std::size_t(y)*image.width+x;
        const double v=std::max(0.,luminance(image,p)-luminance(base,p)),dx=x-cx,dy=y-cy;
        energy+=v;radius+=v*(dx*dx+dy*dy);horizontal+=v*dx*dx;vertical+=v*dy*dy;}
    return {{"energy",energy},{"rms_radius_pixels",energy>0?std::sqrt(radius/energy):0},
        {"horizontal_rms_pixels",energy>0?std::sqrt(horizontal/energy):0},{"vertical_rms_pixels",energy>0?std::sqrt(vertical/energy):0}};
}
void continuousHalo(const DecodedImage& image,const char* label) {
    const int cx=int(image.width/2),cy=int(image.height/2);
    for(const auto direction:std::array<std::array<int,2>,4>{{{1,0},{-1,0},{0,1},{0,-1}}}) {
        unsigned minimum=255;
        for(int radius=14;radius<=100;++radius) {
            const auto pixel=(std::size_t(cy+direction[1]*radius)*image.width+std::size_t(cx+direction[0]*radius))*4;
            const unsigned red=image.rgba[pixel];
            check(red<=minimum+3,std::string(label)+" developed a separated bright lobe/dark gap at radius "+std::to_string(radius)+
                "; red="+std::to_string(red)+", preceding minimum="+std::to_string(minimum));
            minimum=std::min(minimum,red);
        }
    }
}
void save(const fs::path& directory,const char* name,const DecodedImage& image) {
    if(directory.empty())return;fs::create_directories(directory);std::vector<unsigned char> encoded;
    check(lodepng::encode(encoded,image.rgba,image.width,image.height)==0,"Cannot encode Glow comparison.");
    std::ofstream output(directory/name,std::ios::binary);check(bool(output),"Cannot create comparison image.");
    output.write(reinterpret_cast<const char*>(encoded.data()),std::streamsize(encoded.size()));
}
Json summary(std::vector<double> values) {
    if(values.empty())return {{"samples",0}};
    std::sort(values.begin(),values.end());const auto n=values.size();
    return {{"samples",n},{"median_ms",n%2?values[n/2]:(values[n/2-1]+values[n/2])*.5},
        {"p95_ms",values[std::min(n-1,std::size_t(std::ceil(n*.95))-1)]},{"minimum_ms",values.front()},{"maximum_ms",values.back()}};
}
std::shared_ptr<ModelAsset> quad() {
    auto asset=std::make_shared<ModelAsset>();asset->id="glow-bloom-controlled-quad";ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    constexpr std::array<Vec3,4> positions{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
    constexpr std::array<Vec2,4> coordinates{{{0,1},{1,1},{1,0},{0,0}}};
    for(unsigned i=0;i<4;++i){ModelVertex vertex;vertex.position=positions[i];vertex.normal={0,0,1};vertex.uv0=vertex.uv1=coordinates[i];
        vertex.tangent={1,0,0,-1};primitive.vertices.push_back(vertex);math::include(asset->bounds,vertex.position);}
    primitive.indices={0,1,2,0,2,3};asset->primitives.push_back(std::move(primitive));asset->draws.push_back({0,{}});
    asset->materials.emplace_back();return asset;
}
std::optional<double> viewTime(const bgfx::Stats& stats,unsigned requested) {
    for(unsigned i=0;i<stats.numViews;++i){const auto& view=stats.viewStats[i];
        if(view.view==requested&&view.gpuFrameNum==stats.gpuFrameNum&&view.gpuTimeEnd>=view.gpuTimeBegin)
            return double(view.gpuTimeEnd-view.gpuTimeBegin)*1000./stats.gpuTimerFreq;}
    return {};
}
void exercise(SDL_Window* window,const Options& options) {
    // Pending destinations outlive Renderer if a readback times out.
    std::vector<std::shared_ptr<DecodedImage>> retained;
    std::vector<std::shared_ptr<std::vector<float>>> retainedFloat;
    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));TestGpuContext context;
    std::uint32_t frame{};bgfx::reset(320,240,BGFX_RESET_NONE);renderer.setGridVisible(false);
    const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=renderer.advance_frame();context.flush();};
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x424c4f4f4d;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());ViewportAppearance appearance;appearance.backgroundSrgb={};renderer.setAppearance(appearance);
    const auto mesh=quad();check(renderer.prepareModel(*mesh),renderer.modelStatus());
    auto surfaceMaterials=mesh->materials,emitterMaterials=mesh->materials;
    surfaceMaterials[0].baseColor={.25f,.25f,.25f,1};surfaceMaterials[0].roughness=.25f;
    emitterMaterials[0].baseColor={0,0,0,1};emitterMaterials[0].emissive={32,4,.5f};
    std::vector<ModelMaterialTextures> textures(1);std::array<RenderModelInstance,3> instances;
    for(std::size_t i=0;i<instances.size();++i){auto& instance=instances[i];instance.entityId=0x424c00+i;instance.model.asset=mesh;
        instance.forceFullDetail=true;instance.materials=i==2?&emitterMaterials:&surfaceMaterials;instance.textures=&textures;}
    constexpr float pi=3.14159265359f;
    instances[0].model.transform=math::translation({0,0,-1})*math::scaling({3,2,1});
    instances[1].model.transform=math::translation({0,-1,0})*math::rotation(math::axisAngle({1,0,0},-pi*.5f))*math::scaling({3,3,1});
    instances[2].model.transform=math::translation({-.6f,.2f,.1f})*math::scaling({.12f,.12f,1});
    SceneSettings gameSettings;gameSettings.camera.position={0,1,4};gameSettings.camera.target={0,0,0};gameSettings.camera.fovAxis=CameraFovAxis::Vertical;
    gameSettings.camera.nearPlane=.1f;gameSettings.camera.farPlane=32;gameSettings.environment.enabled=false;
    gameSettings.environment.ambientLight.source=AmbientLightSource::Disabled;gameSettings.environment.toneMapping=ToneMapping::Linear;
    gameSettings.environment.background.overrideColor=true;gameSettings.environment.background.color={};gameSettings.environment.debanding=false;
    gameSettings.environment.ssgi.enabled=gameSettings.environment.ssao.enabled=false;gameSettings.antiAliasing=AntiAliasing::Off;
    SceneSettings sceneSettings=gameSettings;sceneSettings.camera.position={-.2f,1,4};
    RenderScene game{gameSettings,{}},scene{sceneSettings,{}};game.sceneMode=scene.sceneMode=true;game.instances=scene.instances=instances;
    if(options.benchmark) {
        bgfx::setDebug(BGFX_DEBUG_PROFILER);const auto* caps=bgfx::getCaps();
        Json report{{"schema",3},{"backend",renderer.backendName()},{"vendor_id",caps->vendorId},{"device_id",caps->deviceId},
            {"warmup_frames",options.warmup},{"requested_unique_gpu_samples",options.samples},
            {"fresh_view_targets_each_phase",true},
            {"fixture","Three shared-mesh wall/floor/emissive instances; HDR Scene/Game with GI, AO, SMAA and shadows disabled"},
            {"limitations","Synthetic offscreen work; hidden 320x240 primary presentation. GPU timestamp intervals are elapsed GPU time, not utilization or editor FPS."},
            {"phases",Json::array()}};
        for(const auto extent:std::array<std::array<std::uint16_t,2>,2>{{{1920,1080},{2560,1440}}})for(const bool dual:{false,true}) {
            const auto width=extent[0],height=extent[1];renderer.resizeGameViewport(width,height);renderer.resizeViewport(dual?width:1,dual?height:1);
            struct EffectCase {const char* name;bool enabled;float radius;GlowQuality quality{GlowQuality::High};};
            for(const auto effectCase:std::array<EffectCase,5>{{{"off",false,1},{"glow-high",true,1},{"glow-high",true,8},
                {"glow-low",true,1,GlowQuality::Low},{"glow-low",true,8,GlowQuality::Low}}}) {
                const float radius=effectCase.radius;const bool active=effectCase.enabled;
                // Isolate active-effect residency from allocations retained by
                // a previously enabled effect. Allocation is outside warmup and
                // every measured interval; immutable model/program data stays.
                renderer.releaseGameViewport();renderer.resizeGameViewport(width,height);
                renderer.resizeViewport(1,1);if(dual)renderer.resizeViewport(width,height);
                gameSettings.environment.glow=sceneSettings.environment.glow={effectCase.enabled,.8f,radius,1};
                gameSettings.environment.glow.quality=sceneSettings.environment.glow.quality=effectCase.quality;
                const auto submit=[&]{if(dual)renderer.render(scene);renderer.renderGame(game);check(renderer.glowStatus().empty(),renderer.glowStatus());bgfx::touch(ToolsView);tick();};
                std::cout<<"Glow benchmark "<<width<<'x'<<height<<" views="<<(dual?2:1)<<" effect="<<effectCase.name<<" spread="<<radius<<" warmup="<<options.warmup<<" samples="<<options.samples<<std::endl;
                for(unsigned i=0;i<options.warmup;++i)submit();
                const auto firstFrame=frame+1;std::set<std::uint32_t> completed;std::vector<double> total,effect,source,backgroundTimes,transparentTimes,presentationTimes,cpu;
                Json samples=Json::array();const auto deadline=Clock::now()+std::chrono::minutes{4};unsigned submitted{};
                while(total.size()<options.samples&&submitted<options.samples*4+128&&Clock::now()<deadline) {
                    const auto begin=Clock::now();submit();++submitted;cpu.push_back(std::chrono::duration<double,std::milli>(Clock::now()-begin).count());
                    const auto* stats=bgfx::getStats();
                    if(stats->gpuFrameNum<firstFrame||stats->gpuTimerFreq<=0||stats->gpuTimeEnd<=stats->gpuTimeBegin||!completed.insert(stats->gpuFrameNum).second)continue;
                    const double elapsed=double(stats->gpuTimeEnd-stats->gpuTimeBegin)*1000./stats->gpuTimerFreq;total.push_back(elapsed);
                    Json sample{{"gpu_frame",stats->gpuFrameNum},{"frame_elapsed_ms",elapsed}};
                    double effectElapsed{},sourceElapsed{},backgroundElapsed{},transparentElapsed{},presentationElapsed{};bool valid=active,sourceValid=true;
                    for(const auto views:dual?std::vector<std::array<unsigned,5>>{{GameGlowExtractView,GameSceneView,GameBackgroundView,GameTransparentView,GamePresentView},
                        {SceneGlowExtractView,SceneView,SceneBackgroundView,SceneTransparentView,PresentView}}:
                        std::vector<std::array<unsigned,5>>{{GameGlowExtractView,GameSceneView,GameBackgroundView,GameTransparentView,GamePresentView}}) {
                        const auto e=viewTime(*stats,views[0]),s=viewTime(*stats,views[1]),background=viewTime(*stats,views[2]),
                            transparent=viewTime(*stats,views[3]),presentation=viewTime(*stats,views[4]);
                        if(e)effectElapsed+=*e;else valid=false;
                        if(s)sourceElapsed+=*s;else sourceValid=false;
                        if(background)backgroundElapsed+=*background;else sourceValid=false;
                        if(transparent)transparentElapsed+=*transparent;else sourceValid=false;
                        if(presentation)presentationElapsed+=*presentation;else sourceValid=false;
                    }
                    // Frame and view timestamp rings can resolve on different
                    // polls. Accept only a coherent completed set, never stale
                    // view intervals attributed to a newer frame.
                    if((active&&!valid)||!sourceValid){total.pop_back();completed.erase(stats->gpuFrameNum);continue;}
                    if(valid){effect.push_back(effectElapsed);sample["effect_elapsed_sum_ms"]=effectElapsed;}
                    if(sourceValid){source.push_back(sourceElapsed);backgroundTimes.push_back(backgroundElapsed);transparentTimes.push_back(transparentElapsed);presentationTimes.push_back(presentationElapsed);
                        sample["opaque_source_elapsed_sum_ms"]=sourceElapsed;sample["background_elapsed_sum_ms"]=backgroundElapsed;
                        sample["transparent_elapsed_sum_ms"]=transparentElapsed;sample["presentation_elapsed_sum_ms"]=presentationElapsed;}
                    samples.push_back(std::move(sample));
                }
                check(total.size()==options.samples,"Insufficient distinct completed GPU timestamps.");
                if(active&&effect.size()!=options.samples) {
                    const auto* diagnostic=bgfx::getStats();std::cerr<<"Effect samples="<<effect.size()<<" total="<<total.size()<<" gpu_frame="<<diagnostic->gpuFrameNum<<" views="<<diagnostic->numViews<<'\n';
                    for(unsigned i=0;i<diagnostic->numViews;++i){const auto& v=diagnostic->viewStats[i];std::cerr<<"view="<<v.view<<" frame="<<v.gpuFrameNum<<" begin="<<v.gpuTimeBegin<<" end="<<v.gpuTimeEnd<<'\n';}
                    check(false,"Missing per-effect view timestamps.");
                }
                const auto* stats=bgfx::getStats();Json phase{{"width",width},{"height",height},{"views",dual?2:1},{"spread",radius},
                    {"effect",effectCase.name},{"submitted_frames",submitted},{"gpu_frame_elapsed",summary(total)},
                    {"effect_elapsed_sum",summary(effect)},
                    {"opaque_source_elapsed_sum",summary(source)},{"cpu_submit_wall",summary(cpu)},{"post_owner_bytes",renderer.glowResidentBytes()},
                    {"background_elapsed_sum",summary(backgroundTimes)},{"transparent_elapsed_sum",summary(transparentTimes)},{"presentation_elapsed_sum",summary(presentationTimes)},
                    {"draws",stats->numDraw},{"computes",stats->numCompute},{"gpu_samples",std::move(samples)}};
                std::cout<<"Result frame="<<phase["gpu_frame_elapsed"]<<" effect="<<phase["effect_elapsed_sum"]<<std::endl;report["phases"].push_back(std::move(phase));
            }
        }
        fs::create_directories(options.artifacts);std::ofstream output(options.artifacts/"glow-benchmark.json");check(bool(output),"Cannot create benchmark report.");output<<report.dump(2)<<'\n';
        auto compact=report;for(auto& phase:compact["phases"])phase.erase("gpu_samples");std::cout<<"GLOW_BENCHMARK_JSON "<<compact.dump()<<std::endl;
        bgfx::setDebug(BGFX_DEBUG_NONE);
    }
    if(!options.benchmark) {
        {
            const auto compute=[](const auto& bytes) {const auto shader=bgfx::createShader(bgfx::copy(bytes,std::uint32_t(sizeof(bytes))));
                check(bgfx::isValid(shader),"Direct glare test compute shader creation failed.");const auto program=bgfx::createProgram(shader,true);
                check(bgfx::isValid(program),"Direct glare test compute program creation failed.");return program;};
            Program down{compute(cc_cs_glow_downsample)},up{compute(cc_cs_glow_upsample)};
            GlowGpu reconstruction(down.value,up.value);reconstruction.advance(frame);
            check(!reconstruction.prepare(129,97,{})&&reconstruction.resident_bytes()==0,"Disabled Glow allocated direct filter resources.");
            const auto readFloat=[&](bgfx::TextureHandle input,std::uint16_t w,std::uint16_t h) {
                Texture staging{bgfx::createTexture2D(w,h,false,1,bgfx::TextureFormat::RGBA32F,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
                check(bgfx::isValid(staging.value),"FP32 glare readback allocation failed.");
                auto values=std::make_shared<std::vector<float>>(std::size_t(w)*h*4);retainedFloat.push_back(values);
                bgfx::blit(DiagnosticReadbackView,staging.value,0,0,input,0,0,w,h);const auto ready=bgfx::readTexture(staging.value,values->data());
                check(ready!=UINT32_MAX,"FP32 glare readback unsupported.");
                for(unsigned i=0;i<120;++i){tick();reconstruction.advance(frame);if(picking::frameReady(frame,ready))return *values;}
                renderer.drainPending(frame);throw std::runtime_error("FP32 glare readback timed out.");
            };
            if(!options.levelResponseOnly) {
                constexpr std::uint16_t w=257,h=193;
                constexpr double sourceCap=12.;
                GlowSettings authored;authored.enabled=true;authored.strength=.74f;authored.radius=1.3f;
                authored.anamorphic=.12f;authored.threshold=1;authored.quality=GlowQuality::High;
                authored.levels={.39f,.27f,.31f,.24f,.30f,.29f,.36f};
                double gain{};for(const float level:authored.levels)gain+=level;
                const double outputBound=sourceCap*gain*authored.strength;
                Json report{{"space","linear FP32 Glow; previews use the CPU ACES white=1 reference and one sRGB encoding"},
                    {"width",w},{"height",h},{"exposure",1},{"reference_unbounded",options.referenceUnbounded},
                    {"strength",authored.strength},{"spread",authored.radius},{"anamorphic",authored.anamorphic},
                    {"threshold",authored.threshold},{"levels",authored.levels},{"conditioned_source_cap",sourceCap},
                    {"maximum_linear_glow",outputBound},{"cases",Json::array()},{"linearity",Json::array()}};
                std::vector<std::string> failures;
                const auto require=[&](bool passed,const std::string& message){if(!passed)failures.push_back(message);};
                const auto capture=[&](const std::vector<float>& pixels,const GlowSettings& settings) {
                    Texture input{bgfx::createTexture2D(w,h,false,1,bgfx::TextureFormat::RGBA32F,
                        BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP,
                        bgfx::copy(pixels.data(),std::uint32_t(pixels.size()*sizeof(float))))};
                    check(bgfx::isValid(input.value),"HDR Glow source allocation failed.");
                    check(reconstruction.prepare(w,h,settings),reconstruction.status());
                    check(reconstruction.execute(input.value,1,SceneGlowExtractView),reconstruction.status());
                    return readFloat(reconstruction.texture(),reconstruction.width(),reconstruction.height());
                };
                // Match glowBilinear in presentation, including half-texel
                // centers and edge clamping; the source image stays native size.
                const auto nativeGlow=[&](const std::vector<float>& filtered) {
                    const int fw=reconstruction.width(),fh=reconstruction.height();
                    std::vector<float> result(std::size_t(w)*h*4);
                    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
                        const double sx=(x+.5)*fw/w-.5,sy=(y+.5)*fh/h-.5;
                        const int ix=int(std::floor(sx)),iy=int(std::floor(sy));
                        const double fx=sx-ix,fy=sy-iy;
                        const auto at=[&](int px,int py,unsigned channel){return filtered[
                            (std::size_t(std::clamp(py,0,fh-1))*fw+std::clamp(px,0,fw-1))*4+channel];};
                        for(unsigned c=0;c<3;++c)result[(std::size_t(y)*w+x)*4+c]=float(
                            (at(ix,iy,c)*(1-fx)+at(ix+1,iy,c)*fx)*(1-fy)+
                            (at(ix,iy+1,c)*(1-fx)+at(ix+1,iy+1,c)*fx)*fy);
                    }
                    return result;
                };
                const auto encode=[](float value) {
                    value=std::clamp(value,0.f,1.f);
                    const float srgb=value<=.0031308f?value*12.92f:1.055f*std::pow(value,1.f/2.4f)-.055f;
                    return std::uint8_t(std::lround(std::clamp(srgb,0.f,1.f)*255));
                };
                struct SourceCase {const char* name;std::array<float,3> color;bool disk;};
                const std::array<SourceCase,3> sources{{
                    {"white-48000-disk",{48000,48000,48000},true},
                    {"orange-emissive-rectangle",{5.58f,.757f,0},false},
                    {"finite-extreme-1e11-disk",{1e11f,1e11f,1e11f},true}}};
                constexpr std::array<unsigned,4> coverage{0,50,90,100};
                for(const auto& source:sources) {
                    std::vector<std::size_t> support;
                    // Scan columns so every later mask removes a nested set of
                    // the exact same fixed source pixels, with no camera motion.
                    for(unsigned x=0;x<w;++x)for(unsigned y=0;y<h;++y) {
                        const int dx=int(x)-int(w/2),dy=int(y)-int(h/2);
                        if(source.disk?dx*dx+dy*dy<=16:(std::abs(dx)<=20&&std::abs(dy)<=15))
                            support.push_back(std::size_t(y)*w+x);
                    }
                    std::vector<bool> originalSupport(std::size_t(w)*h);
                    for(const auto p:support)originalSupport[p]=true;
                    std::vector<float> previous,full,fullSource;double previousEnergy{};
                    for(const unsigned covered:coverage) {
                        const auto removed=(support.size()*covered+99)/100;
                        std::vector<float> pixels(std::size_t(w)*h*4);
                        for(std::size_t i=removed;i<support.size();++i)for(unsigned c=0;c<3;++c)
                            pixels[support[i]*4+c]=source.color[c];
                        const auto filtered=capture(pixels,authored),glare=nativeGlow(filtered);
                        DecodedImage preview{w,h,std::vector<std::uint8_t>(std::size_t(w)*h*4)},
                            sourcePreview=preview,glowPreview=preview;
                        double peak{},energy{},outsideEnergy{},coveredEnergy{},increase{},minimum{};
                        std::array<double,3> channelEnergy{};
                        std::size_t clipped{},whiteClipped{},outsideClipped{},occludedClipped{};bool finite=true;
                        for(std::size_t p=0;p<std::size_t(w)*h;++p) {
                            const auto index=p*4;
                            const Vec3 mapped=toneMap({pixels[index]+glare[index],pixels[index+1]+glare[index+1],
                                pixels[index+2]+glare[index+2]},ToneMapping::ACES);
                            const Vec3 mappedSource=toneMap({pixels[index],pixels[index+1],pixels[index+2]},ToneMapping::ACES);
                            const Vec3 mappedGlow=toneMap({glare[index],glare[index+1],glare[index+2]},ToneMapping::ACES);
                            const std::array<float,3> display{mapped[0],mapped[1],mapped[2]},
                                displaySource{mappedSource[0],mappedSource[1],mappedSource[2]},
                                displayGlow{mappedGlow[0],mappedGlow[1],mappedGlow[2]};
                            bool anyClipped=false,allClipped=true;
                            for(unsigned c=0;c<3;++c) {
                                const double value=glare[index+c];finite&=std::isfinite(value);
                                peak=std::max(peak,value);minimum=std::min(minimum,value);energy+=value;channelEnergy[c]+=value;
                                if(!originalSupport[p])outsideEnergy+=value;
                                if(originalSupport[p]&&pixels[index]==0)coveredEnergy+=value;
                                if(!previous.empty())increase=std::max(increase,value-previous[index+c]);
                                anyClipped|=display[c]>=.98f;allClipped&=display[c]>=.98f;
                                preview.rgba[index+c]=encode(display[c]);sourcePreview.rgba[index+c]=encode(displaySource[c]);
                                glowPreview.rgba[index+c]=encode(displayGlow[c]);
                            }
                            preview.rgba[index+3]=sourcePreview.rgba[index+3]=glowPreview.rgba[index+3]=255;
                            if(anyClipped){++clipped;if(!originalSupport[p])++outsideClipped;
                                if(originalSupport[p]&&pixels[index]==0)++occludedClipped;}
                            if(allClipped)++whiteClipped;
                        }
                        const std::string label=std::string(source.name)+"-covered-"+std::to_string(covered);
                        const double tolerance=std::max(1e-7,peak*.00005);
                        require(finite&&minimum>=-1e-7,label+": Glow contains negative/nonfinite radiance.");
                        require(previous.empty()||increase<=tolerance,label+": hiding nested source pixels increased a Glow pixel.");
                        require(previous.empty()||energy<=previousEnergy+std::max(1e-6,previousEnergy*.00005),
                            label+": hiding source pixels increased total linear Glow energy.");
                        require(covered==100?peak<=1e-8:energy>0,label+": covered/visible source produced the wrong zero/nonzero response.");
                        if(!options.referenceUnbounded)require(peak<=outputBound*1.0005+1e-6,
                            label+": Glow exceeded the conditioned source cap times authored linear gains.");
                        report["cases"].push_back({{"source",source.name},{"covered_percent",covered},{"source_pixels",support.size()},
                            {"removed_pixels",removed},{"remaining_pixels",support.size()-removed},
                            {"filter_width",reconstruction.width()},{"filter_height",reconstruction.height()},
                            {"linear_peak",peak},{"linear_energy",energy},{"channel_energy",channelEnergy},
                            {"outside_original_emitter_energy",outsideEnergy},{"over_occluded_emitter_energy",coveredEnergy},
                            {"maximum_increase_from_less_covered",increase},{"componentwise_tolerance",tolerance},
                            {"aces_clipped_pixels",clipped},{"aces_white_clipped_pixels",whiteClipped},
                            {"aces_clipped_outside_original_emitter_pixels",outsideClipped},
                            {"aces_clipped_over_occluded_emitter_pixels",occludedClipped},{"finite",finite}});
                        save(options.artifacts,(label+"-composite.png").c_str(),preview);
                        save(options.artifacts,(label+"-source.png").c_str(),sourcePreview);
                        save(options.artifacts,(label+"-glow.png").c_str(),glowPreview);
                        if(covered==0){full=glare;fullSource=pixels;}
                        previous=glare;previousEnergy=energy;
                    }
                    auto halfSettings=authored;for(auto& level:halfSettings.levels)level*=.5f;
                    const auto half=nativeGlow(capture(fullSource,halfSettings));double error{},referencePeak{};
                    for(std::size_t i=0;i<half.size();++i)if(i%4<3) {
                        error=std::max(error,std::abs(double(half[i])-full[i]*.5));referencePeak=std::max(referencePeak,double(full[i])*.5);
                    }
                    const double tolerance=std::max(1e-8,referencePeak*.0005);
                    require(error<=tolerance,std::string(source.name)+": halving all level gains did not halve linear Glow.");
                    report["linearity"].push_back({{"source",source.name},{"maximum_absolute_error",error},{"tolerance",tolerance}});
                }
                report["passed"]=failures.empty();report["failures"]=failures;
                if(!options.artifacts.empty()) {
                    fs::create_directories(options.artifacts);std::ofstream output(options.artifacts/"glow-hdr-source.json");
                    check(bool(output),"Cannot write HDR Glow source report.");output<<report.dump(2)<<'\n';
                }
                std::cout<<"GLOW_HDR_SOURCE_JSON "<<report.dump()<<std::endl;
                check(failures.empty(),"HDR Glow source failed "+std::to_string(failures.size())+" checks; first: "+
                    (failures.empty()?std::string{}:failures.front()));
                reconstruction.disable();
                for(unsigned i=0;i<4;++i){tick();reconstruction.advance(frame);}
                check(reconstruction.resident_bytes()==0,"HDR Glow source fixture retained retired filter allocations.");
            }
            // Read the filter's linear FP32 output. Display encoding and tone
            // mapping can conceal coefficient changes around bright sources.
            if(!options.hdrSourceOnly) {
                constexpr std::uint16_t w=129,h=97;
                GlowSettings levels;levels.enabled=true;levels.strength=1;levels.threshold=0;levels.levels.fill(0);
                check(!reconstruction.prepare(w,h,levels)&&!reconstruction.active()&&reconstruction.resident_bytes()==0,
                    "Enabled Glow with zero levels allocated or activated direct filter resources.");
                for(unsigned i=0;i<4;++i){tick();reconstruction.advance(frame);}
                check(bgfx::getStats()->numCompute==0,"Zero direct Glow levels scheduled compute work.");
                Json levelReport{{"space","linear FP32 filter output before presentation"},{"width",w},{"height",h},
                    {"checks",Json::array()}};
                std::vector<std::string> failures;
                const auto compare=[&](const std::string& label,const std::vector<float>& actual,const std::vector<float>& expected) {
                    check(actual.size()==expected.size(),"Direct Glow response dimensions changed.");
                    double maximumError{},maximumExpected{},actualEnergy{},expectedEnergy{};bool finite=true;
                    for(std::size_t i=0;i<actual.size();++i)if(i%4<3) {
                        finite&=std::isfinite(actual[i]);
                        maximumError=std::max(maximumError,std::abs(double(actual[i])-expected[i]));
                        maximumExpected=std::max(maximumExpected,std::abs(double(expected[i])));
                        actualEnergy+=actual[i];expectedEnergy+=expected[i];
                    }
                    const double tolerance=std::max(1e-10,maximumExpected*.0005);
                    const bool passed=finite&&maximumError<=tolerance;
                    levelReport["checks"].push_back({{"name",label},{"passed",passed},{"maximum_absolute_error",maximumError},
                        {"tolerance",tolerance},{"actual_energy",actualEnergy},{"expected_energy",expectedEnergy}});
                    if(!passed)failures.push_back(label+": maximum error "+std::to_string(maximumError)+
                        ", expected peak "+std::to_string(maximumExpected));
                };
                const auto scaled=[](const std::vector<float>& input,float scale) {
                    auto result=input;for(std::size_t i=0;i<result.size();++i)if(i%4<3)result[i]*=scale;return result;
                };
                for(const bool constant:{true,false}) {
                    const std::string sourceName=constant?"constant":"impulse";
                    std::vector<float> pixels(std::size_t(w)*h*4);
                    const auto sourcePixel=[&](unsigned x,unsigned y,float strength) {
                        const auto pixel=(std::size_t(y)*w+x)*4;
                        pixels[pixel]=.5f*strength;pixels[pixel+1]=.25f*strength;pixels[pixel+2]=.125f*strength;
                    };
                    if(constant)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)sourcePixel(x,y,1);
                    else {
                        for(unsigned y=h/2-1;y<=h/2+1;++y)for(unsigned x=w/2-1;x<=w/2+1;++x)sourcePixel(x,y,1);
                        sourcePixel(w/3,h/3,.3f);
                    }
                    Texture input{bgfx::createTexture2D(w,h,false,1,bgfx::TextureFormat::RGBA32F,
                        BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP,bgfx::copy(pixels.data(),std::uint32_t(pixels.size()*sizeof(float))))};
                    check(bgfx::isValid(input.value),"Linear Glow level-response source allocation failed.");
                    const auto captureLevels=[&](const std::array<float,GlowLevelCount>& coefficients) {
                        levels.levels=coefficients;
                        check(reconstruction.prepare(w,h,levels),reconstruction.status());
                        check(reconstruction.execute(input.value,1,SceneGlowExtractView),reconstruction.status());
                        return readFloat(reconstruction.texture(),reconstruction.width(),reconstruction.height());
                    };
                    std::array<std::vector<float>,GlowLevelCount> isolated;
                    for(unsigned level=0;level<GlowLevelCount;++level) {
                        std::array<float,GlowLevelCount> coefficients{};coefficients[level]=1;
                        isolated[level]=captureLevels(coefficients);
                        double peak{};for(std::size_t i=0;i<isolated[level].size();++i)if(i%4<3)
                            peak=std::max(peak,double(isolated[level][i]));
                        check(peak>0,"Isolated linear Glow level produced no radiance.");
                        for(const float coefficient:{0.f,.01f,.1f,.25f,.5f,1.f}) {
                            coefficients[level]=coefficient;
                            const std::string label=sourceName+" level "+std::to_string(level+1)+" coefficient "+std::to_string(coefficient);
                            if(coefficient==0) {
                                levels.levels=coefficients;
                                check(!reconstruction.prepare(w,h,levels)&&!reconstruction.active(),
                                    "Zero isolated Glow coefficient activated a filter pyramid.");
                                for(unsigned i=0;i<4;++i){tick();reconstruction.advance(frame);}
                                check(bgfx::getStats()->numCompute==0&&reconstruction.resident_bytes()==0,
                                    "Zero isolated Glow coefficient retained active/retired resources or compute work.");
                                levelReport["checks"].push_back({{"name",label},{"passed",true},{"compute",0},{"resident_bytes",0}});
                            } else compare(label,captureLevels(coefficients),scaled(isolated[level],coefficient));
                        }
                    }
                    std::array<float,GlowLevelCount> mixedWeights{.07f,.11f,.13f,.17f,.19f,.23f,.29f};
                    auto expected=scaled(isolated[0],0);
                    for(unsigned level=0;level<GlowLevelCount;++level)for(std::size_t i=0;i<expected.size();++i)if(i%4<3)
                        expected[i]+=mixedWeights[level]*isolated[level][i];
                    const auto mixed=captureLevels(mixedWeights);compare(sourceName+" mixed scale additivity",mixed,expected);
                    auto doubled=mixedWeights;for(auto& coefficient:doubled)coefficient*=2;
                    compare(sourceName+" doubling contributions doubles radiance",captureLevels(doubled),scaled(mixed,2));
                    auto changed=mixedWeights;changed[3]+=.3f;auto independent=captureLevels(changed);
                    for(std::size_t i=0;i<independent.size();++i)if(i%4<3)independent[i]-=mixed[i];
                    compare(sourceName+" one slider leaves other contributions unchanged",independent,scaled(isolated[3],.3f));
                    for(unsigned boundary=1;boundary<GlowLevelCount;++boundary) {
                        std::array<float,GlowLevelCount> coefficients{};coefficients[boundary-1]=.6f;
                        const auto preceding=captureLevels(coefficients);
                        for(const float epsilon:{.01f,.0001f}) {
                            coefficients[boundary]=epsilon;auto expectedLimit=preceding;
                            for(std::size_t i=0;i<expectedLimit.size();++i)if(i%4<3)expectedLimit[i]+=epsilon*isolated[boundary][i];
                            compare(sourceName+" pruning continuity at level "+std::to_string(boundary+1)+" epsilon "+std::to_string(epsilon),
                                captureLevels(coefficients),expectedLimit);
                        }
                    }
                }
                levelReport["passed"]=failures.empty();levelReport["failures"]=failures;
                if(!options.artifacts.empty()) {
                    fs::create_directories(options.artifacts);std::ofstream output(options.artifacts/"glow-level-response.json");
                    check(bool(output),"Cannot write linear Glow level-response report.");output<<levelReport.dump(2)<<'\n';
                }
                std::cout<<"GLOW_LEVEL_RESPONSE_JSON "<<levelReport.dump()<<std::endl;
                check(failures.empty(),"Linear Glow level response failed "+std::to_string(failures.size())+" checks; first: "+
                    (failures.empty()?std::string{}:failures.front()));
            }
            if(!options.levelResponseOnly&&!options.hdrSourceOnly)for(const auto extent:std::array<std::array<std::uint16_t,2>,2>{{{129,97},{1,1}}}) {
                const auto w=extent[0],h=extent[1];constexpr std::array<float,4> radiance{1e11f,5e10f,1e10f,0};
                std::vector<float> pixels(std::size_t(w)*h*4);for(std::size_t i=0;i<pixels.size();++i)pixels[i]=radiance[i%4];
                Texture input{bgfx::createTexture2D(w,h,false,1,bgfx::TextureFormat::RGBA32F,BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP,
                    bgfx::copy(pixels.data(),std::uint32_t(pixels.size()*sizeof(float))))};check(bgfx::isValid(input.value),"FP32 glare source allocation failed.");
                GlowSettings flatGlow;flatGlow.enabled=true;flatGlow.strength=.8f;flatGlow.threshold=0;
                check(reconstruction.prepare(w,h,flatGlow),reconstruction.status());
                // High exposure must reach the extracted-source ceiling, while
                // low exposure must recover the original extreme constant field
                // accurately before the ceiling. This rejects unstable inverse
                // compression and premature clamping of unexposed HDR radiance.
                for(const float exposure:{256.f,1e-12f}) {
                    check(reconstruction.execute(input.value,exposure,SceneGlowExtractView),reconstruction.status());
                    const auto values=readFloat(reconstruction.texture(),reconstruction.width(),reconstruction.height());
                    for(std::size_t i=0;i<values.size();++i) {
                        check(std::isfinite(values[i])&&values[i]>=0,"Glow produced negative/nonfinite FP32 HDR radiance.");
                        if(i%4<3) {const double expected=std::min(double(radiance[i%4])*exposure,12.)*flatGlow.strength;
                            check(std::abs(values[i]-expected)<=std::max(1e-8,expected*.002),
                                "Glow failed bounded extraction or accurate low-exposure HDR recovery.");}
                    }
                }
            }
        }
        if(options.levelResponseOnly||options.hdrSourceOnly){resources.stop();renderer.drainPending(frame);return;}
        constexpr std::uint16_t W=257,H=257;renderer.resizeViewport(W,H);renderer.resizeGameViewport(W,H);
        const auto read=[&](bgfx::TextureHandle texture) {
            Texture staging{bgfx::createTexture2D(W,H,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
            check(bgfx::isValid(staging.value),"Glow readback allocation failed.");
            auto result=std::make_shared<DecodedImage>(DecodedImage{W,H,std::vector<std::uint8_t>(std::size_t(W)*H*4)});retained.push_back(result);
            bgfx::blit(DiagnosticReadbackView,staging.value,0,0,texture,0,0,W,H);const auto ready=bgfx::readTexture(staging.value,result->rgba.data());
            check(ready!=UINT32_MAX,"Glow readback unsupported.");
            for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return *result;}
            renderer.drainPending(frame);throw std::runtime_error("Glow readback timed out.");
        };
        gameSettings.camera.position={0,0,4};gameSettings.camera.target={};gameSettings.exposure=1;
        gameSettings.environment.glow={};gameSettings.environment.toneMapping=ToneMapping::Linear;
        emitterMaterials[0].emissive={32,4,.5f};instances[2].model.transform=math::scaling({.08f,.08f,1});
        game.instances=std::span(&instances[2],1);
        const auto capture=[&]{renderer.renderGame(game);check(renderer.glowStatus().empty(),renderer.glowStatus());return read(renderer.gameViewportTexture());};
        const auto base=capture();save(options.artifacts,"effects-off.png",base);
        auto& glow=gameSettings.environment.glow;
        glow.enabled=true;glow.strength=.6f;glow.radius=1;glow.threshold=1;
        const auto glowImage=capture();check(maximumDifference(base,glowImage)>5,"Glow created no halo around visible HDR emission.");
        save(options.artifacts,"glow-default.png",glowImage);
        glow.radius=8;const auto glowWide=capture();save(options.artifacts,"glow-spread-8.png",glowWide);
        continuousHalo(glowWide,"Maximum Glow Spread");glow.radius=1;
        gameSettings.environment.toneMapping=ToneMapping::Reinhard;
        glow.enabled=false;const auto blendBase=capture();glow.enabled=true;glow.blendMode=GlowBlendMode::Additive;const auto additive=capture();
        glow.blendMode=GlowBlendMode::Screen;const auto screen=capture();
        for(std::size_t p=0;p<screen.rgba.size();++p)if(p%4<3)check(int(screen.rgba[p])+1>=int(blendBase.rgba[p]),"Screen Glow darkened the original scene.");
        glow.blendMode=GlowBlendMode::SoftLight;const auto softLight=capture();
        glow.tint={};check(maximumDifference(blendBase,capture())<=1,"Black Soft Light Glow changed the original scene.");glow.tint={1,1,1};
        glow.blendMode=GlowBlendMode::Replace;const auto replaced=capture();
        glow.blendMode=GlowBlendMode::Mix;glow.mix=0;check(maximumDifference(blendBase,capture())==0,"Glow Mix 0 did not preserve the original scene.");
        for(unsigned i=0;i<5;++i){renderer.renderGame(game);tick();}
        check(bgfx::getStats()->numCompute==0,"Glow Mix 0 still scheduled a Glow pyramid.");
        glow.mix=1;check(maximumDifference(replaced,capture())<=1,"Glow Mix 1 did not match Replace.");
        check(maximumDifference(additive,screen)>1&&maximumDifference(additive,softLight)>1&&maximumDifference(additive,replaced)>1,
            "Glow blend modes did not produce distinct artistic compositions.");
        for(const auto* image:{&additive,&screen,&softLight,&replaced})for(std::size_t p=3;p<image->rgba.size();p+=4)
            check(image->rgba[p]==blendBase.rgba[p],"Glow blend mode changed presentation alpha.");
        glow.blendMode=GlowBlendMode::Additive;glow.mix=.5f;gameSettings.environment.toneMapping=ToneMapping::Linear;
        glow.enabled=false;check(maximumDifference(capture(),base)==0,"Disabled Glow did not restore exact original output.");
        glow.enabled=true;glow.levels.fill(0);const auto zeroLevels=capture();check(maximumDifference(zeroLevels,base)==0,"Zero Glow levels changed the frame.");
        for(unsigned i=0;i<5;++i){renderer.renderGame(game);tick();}check(bgfx::getStats()->numCompute==0,"Zero Glow levels still scheduled compute work.");
        glow.levels[0]=1;const auto tight=capture();const auto tightMetric=haloMetrics(tight,base);
        glow.levels.fill(0);glow.levels[6]=1;const auto wide=capture();const auto wideMetric=haloMetrics(wide,base);
        check(tightMetric["energy"].get<double>()>.01&&wideMetric["energy"].get<double>()>.01,"Isolated Glow level produced no usable halo.");
        check(wideMetric["rms_radius_pixels"].get<double>()>tightMetric["rms_radius_pixels"].get<double>()*1.5,"Glow Level 7 did not produce a wider contribution than Level 1.");
        save(options.artifacts,"glow-level-1.png",tight);save(options.artifacts,"glow-level-7.png",wide);
        glow=GlowSettings{};glow.enabled=true;glow.strength=.6f;glow.threshold=100000;
        check(maximumDifference(base,capture())<=1,"Glow leaked a below-threshold HDR source.");
        glow.threshold=1;glow.strength=0;check(maximumDifference(base,capture())==0,"Zero Glow intensity changed output.");
        glow.strength=.6f;glow.radius=0;check(maximumDifference(base,capture())==0,"Zero Glow spread changed output.");
        glow.radius=1;gameSettings.exposure=0;const auto zeroExposure=capture();glow.enabled=false;
        check(maximumDifference(zeroExposure,capture())==0,"Zero exposure left an exposed Glow halo or matte.");gameSettings.exposure=1;
        glow.enabled=true;glow.strength=1;glow.threshold=1;glow.softKnee=0;emitterMaterials[0].emissive={1,0,0};
        const auto hardThreshold=capture();glow.softKnee=1;const auto softThreshold=capture();
        check(maximumDifference(hardThreshold,softThreshold)>1,"Glow threshold softness did not change highlight onset.");
        // Glow collects the complete HDR scene, including nonemissive direct
        // and reflected light, rather than a separate emission-only MRT.
        glow=GlowSettings{};glow.strength=.6f;
        emitterMaterials[0].emissive={};emitterMaterials[0].baseColor={1,1,1,1};emitterMaterials[0].metallic=0;emitterMaterials[0].roughness=.2f;
        DirectionalLight sun;sun.direction={0,0,-1};sun.intensity=100;sun.castShadows=false;game.directionalLights=std::span(&sun,1);
        const auto brightBase=capture();glow.enabled=true;const auto reflectedGlow=capture();
        check(maximumDifference(brightBase,reflectedGlow)>5,"Glow ignored bright nonemissive Lit/direct radiance.");
        save(options.artifacts,"glow-lit-highlight.png",reflectedGlow);
        game.directionalLights={};emitterMaterials[0].baseColor={0,0,0,1};emitterMaterials[0].emissive={32,32,32};
        glow.tint={1,0,0};const auto tinted=capture();
        double red{},green{},blue{};for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)if(std::abs(int(x)-128)>16||std::abs(int(y)-128)>16) {
            const auto p=(std::size_t(y)*W+x)*4;red+=tinted.rgba[p];green+=tinted.rgba[p+1];blue+=tinted.rgba[p+2];}
        check(red>100&&green<red*.02&&blue<red*.02,"Glow tint failed to color the halo independently from the source.");
        glow.tint={1,1,1};emitterMaterials[0].emissive={32,4,.5f};
        glow.saturation=0;const auto desaturated=capture();unsigned neutralHaloPixels{};
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)if(std::abs(int(x)-128)>16||std::abs(int(y)-128)>16) {
            const auto p=(std::size_t(y)*W+x)*4;if(desaturated.rgba[p]>2){++neutralHaloPixels;
                check(std::abs(int(desaturated.rgba[p])-int(desaturated.rgba[p+1]))<=1&&std::abs(int(desaturated.rgba[p])-int(desaturated.rgba[p+2]))<=1,
                    "Glow saturation zero left colored halo radiance.");}}
        check(neutralHaloPixels>10,"Glow saturation test contained no visible halo.");glow.saturation=1;
        glow.enabled=false;const auto opaqueBase=capture();glow.enabled=true;const auto opaqueGlow=capture();
        const auto opaqueMetric=haloMetrics(opaqueGlow,opaqueBase);
        instances[2].blend=MaterialBlend::Alpha;instances[2].opacity=.25f;
        glow.enabled=false;const auto alphaBase=capture();glow.enabled=true;const auto alphaGlow=capture();const auto alphaMetric=haloMetrics(alphaGlow,alphaBase);
        const double alphaRatio=alphaMetric["energy"].get<double>()/opaqueMetric["energy"].get<double>();
        check(alphaRatio>.12&&alphaRatio<.4,"Transparent material Glow did not follow visible coverage; halo energy ratio="+std::to_string(alphaRatio));
        instances[2].blend=MaterialBlend::Opaque;instances[2].opacity=1;
        emitterMaterials[0].emissive={};emitterMaterials[0].baseColor={32,4,.5f,1};instances[2].shading=MaterialShading::Unlit;
        glow.enabled=false;const auto unlitBase=capture();glow.enabled=true;
        check(maximumDifference(unlitBase,capture())>5,"Visible Unlit self-lit radiance was missing from Glow.");
        emitterMaterials[0].baseColor={0,0,0,1};emitterMaterials[0].emissive={32,4,.5f};instances[2].shading=MaterialShading::StandardPbr;
        // Additive particles keep zero scene alpha but their visible HDR
        // radiance is still part of the scene supplied to Glow.
        RenderParticle spark;spark.position={};spark.size=.12f;spark.color={64,0,0,1};spark.billboardMode=ParticleBillboardMode::Enabled;
        RenderParticleMaterial sparkMaterial;sparkMaterial.blend=MaterialBlend::Additive;sparkMaterial.shading=MaterialShading::Unlit;sparkMaterial.baseColor={64,0,0,1};
        game.instances={};game.particles=std::span(&spark,1);game.particleMaterials=std::span(&sparkMaterial,1);
        glow.enabled=false;const auto sparkBase=capture();glow.enabled=true;const auto sparkGlow=capture();
        check(maximumDifference(sparkBase,sparkGlow)>5,"Additive particle with zero HDR coverage produced no Glow.");
        appearance.backgroundSrgb={.3f,.3f,.3f};gameSettings.environment.background.color={.3f,.3f,.3f};renderer.setAppearance(appearance);glow.enabled=false;const auto grayBackdrop=capture();
        glow.enabled=true;glow.blendMode=GlowBlendMode::SoftLight;const auto graySoftLight=capture();unsigned backdropResponse{};
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)if(std::abs(int(x)-128)>16||std::abs(int(y)-128)>16) {
            const auto p=(std::size_t(y)*W+x)*4;backdropResponse=std::max(backdropResponse,unsigned(std::max(0,int(graySoftLight.rgba[p])-int(grayBackdrop.rgba[p]))));
            check(graySoftLight.rgba[p+3]==grayBackdrop.rgba[p+3],"Soft Light Glow changed uncovered-matte alpha.");}
        check(backdropResponse>1,"Soft Light Glow did not blend against the visible gray matte around a zero-coverage additive particle.");
        glow.tint={};check(maximumDifference(grayBackdrop,capture())<=1,"Black Soft Light Glow changed uncovered gray matte.");
        glow.tint={1,1,1};glow.blendMode=GlowBlendMode::Additive;appearance.backgroundSrgb={};gameSettings.environment.background.color={};renderer.setAppearance(appearance);
        // Extreme source radiance must retain its bright particle while the
        // conditioned Glow remains visible without whitening nearby background.
        // The direct FP32 cases above verify bounded high-exposure extraction
        // and accurate recovery of radiance 1e11 at low exposure independently.
        sparkMaterial.baseColor[0]=1e6f;glow.enabled=false;const auto hdrSparkBase=capture();
        glow.enabled=true;const auto hdrSpark=capture();
        check(hdrSparkBase.rgba[(std::size_t(128)*W+128)*4]>=250&&hdrSpark.rgba[(std::size_t(128)*W+128)*4]>=250,
            "Bounded Glow changed the extreme particle's visible HDR source.");
        check(maximumDifference(hdrSparkBase,hdrSpark)>5,"Extreme HDR particle lost its conditioned Glow.");
        check(hdrSpark.rgba[(std::size_t(128)*W+155)*4]<250,
            "Extreme HDR particle saturated nearby background through Glow.");
        save(options.artifacts,"glow-hdr-particle.png",hdrSpark);game.particles={};game.particleMaterials={};game.instances=std::span(&instances[2],1);
        surfaceMaterials[0].baseColor={0,0,0,1};surfaceMaterials[0].emissive={};
        instances[0].model.transform=math::translation({0,0,.3f})*math::scaling({.3f,.3f,1});
        std::array occluded{instances[2],instances[0]};game.instances=occluded;
        const auto hiddenGlow=capture();glow.enabled=false;const auto hiddenBase=capture();
        check(maximumDifference(hiddenBase,hiddenGlow)<=1,"Occluded emission leaked into Glow.");
        game.instances=std::span(&instances[2],1);emitterMaterials[0].emissive={32,4,.5f};
        glow.enabled=true;glow.levels.fill(0);glow.levels[3]=1;glow.anamorphic=1;const auto horizontal=capture();
        glow.anamorphic=-1;const auto vertical=capture();const auto horizontalMetric=haloMetrics(horizontal,base),verticalMetric=haloMetrics(vertical,base);
        check(horizontalMetric["horizontal_rms_pixels"].get<double>()>horizontalMetric["vertical_rms_pixels"].get<double>()*1.2&&
            verticalMetric["vertical_rms_pixels"].get<double>()>verticalMetric["horizontal_rms_pixels"].get<double>()*1.2,"Glow anamorphic stretch did not rotate its dominant axis.");
        const auto emitterSettings=gameSettings;
        game.instances={};gameSettings.environment.enabled=true;gameSettings.environment.source=EnvironmentSource::ProceduralSky;
        gameSettings.environment.background.show=true;gameSettings.environment.background.overrideColor=false;
        auto& sky=gameSettings.environment.procedural;sky.skyTop=sky.skyHorizon=sky.groundBottom=sky.groundHorizon={};sky.sunDiameter=2;sky.sunHaloExtent=2;
        gameSettings.proceduralSun.enabled=true;gameSettings.proceduralSun.direction=math::normalized(Vec3{0,.05f,-1});
        gameSettings.proceduralSun.color={1,.8f,.5f};gameSettings.proceduralSun.intensity=32;
        glow=GlowSettings{};const auto sunBase=capture();glow.enabled=true;glow.strength=.6f;const auto sunGlow=capture();
        check(maximumDifference(sunBase,sunGlow)>5,"Glow ignored the visible procedural sun.");
        game.instances=std::span(&instances[0],1);glow.enabled=false;const auto coveredSunBase=capture();glow.enabled=true;const auto coveredSunGlow=capture();
        check(maximumDifference(coveredSunBase,coveredSunGlow)<=1,"Fully occluded sun continued supplying Glow.");
        save(options.artifacts,"glow-visible-sun.png",sunGlow);save(options.artifacts,"glow-covered-sun.png",coveredSunGlow);
        // Isolate solar visibility from legitimate lighting/reflections on the
        // occluder. The visible sky remains enabled while surface IBL is off.
        // GI/AO and SSR still exercise the shared HDR/five-target routing.
        auto solarOccluderMaterials=surfaceMaterials;
        solarOccluderMaterials[0].baseColor={0,0,0,1};solarOccluderMaterials[0].emissive={};
        solarOccluderMaterials[0].metallic=0;solarOccluderMaterials[0].roughness=.3f;
        solarOccluderMaterials[0].doubleSided=true;solarOccluderMaterials[0].receiveFog=false;
        RenderModelInstance solarOccluder=instances[0];solarOccluder.entityId=0x534f4c4152;
        solarOccluder.materials=&solarOccluderMaterials;solarOccluder.shading=MaterialShading::StandardPbr;
        game.instances=std::span(&solarOccluder,1);
        gameSettings.environment.enabled=false;gameSettings.environment.intensity=1;
        gameSettings.environment.ssgi.enabled=gameSettings.environment.ssao.enabled=true;
        gameSettings.environment.ssr.quality=SsrQuality::Medium;
        // Keep these coverage comparisons below presentation clipping; the
        // preceding 32-unit sun case separately covers bright HDR saturation.
        gameSettings.proceduralSun.intensity=.25f;
        glow=GlowSettings{};glow.strength=.6f;glow.threshold=0;
        struct SunOcclusionAngle {Vec3 eye,forward,sun;float yaw,pitch;};
        const std::array<SunOcclusionAngle,2> sunAngles{{
            {{0,0,4},{0,0,-1},{0,.05f,-1},0,0},
            {{.4f,.2f,4.2f},{-.1f,.12f,-1},{.18f,.18f,-1},35,20}}};
        constexpr float occluderHalfSize=.24f;
        constexpr std::array<float,5> solarOffsets{0,.85f,1,1.15f,2};
        Json solarOcclusion=Json::array();
        const auto sceneEnergy=[](const DecodedImage& image){double energy{};
            for(std::size_t pixel=0;pixel<image.rgba.size()/4;++pixel)energy+=luminance(image,pixel);return energy;};
        const auto sunCapture=[&]{auto image=capture();
            check(renderer.ssgiStatus().empty()&&renderer.ssaoStatus().empty()&&renderer.ssrStatus().empty(),
                "Sun occlusion effect preparation failed: "+renderer.ssgiStatus()+renderer.ssaoStatus()+renderer.ssrStatus());return image;};
        const auto setSunAngle=[&](const SunOcclusionAngle& angle,float offset){
            gameSettings.camera.position=angle.eye;gameSettings.camera.target=angle.eye+math::normalized(angle.forward)*4.f;
            gameSettings.proceduralSun.direction=math::normalized(angle.sun);
            const auto orientation=math::rotation(math::axisAngle({0,1,0},angle.yaw*pi/180))*
                math::rotation(math::axisAngle({1,0,0},angle.pitch*pi/180));
            const auto center=angle.eye+gameSettings.proceduralSun.direction*3.f;
            solarOccluder.model.transform=math::translation(center)*orientation*
                math::translation({offset*occluderHalfSize,0,0})*math::scaling({occluderHalfSize,occluderHalfSize,1});
        };
        for(std::size_t angleIndex=0;angleIndex<sunAngles.size();++angleIndex)for(const bool ssr:{false,true}) {
            gameSettings.environment.ssr.enabled=ssr;
            std::array<double,solarOffsets.size()> haloEnergy{},visibleEnergy{};
            for(std::size_t step=0;step<solarOffsets.size();++step) {
                setSunAngle(sunAngles[angleIndex],solarOffsets[step]);
                glow.enabled=false;const auto withoutGlow=sunCapture();glow.enabled=true;const auto withGlow=sunCapture();
                const auto difference=maximumDifference(withoutGlow,withGlow);
                haloEnergy[step]=haloMetrics(withGlow,withoutGlow)["energy"].get<double>();
                visibleEnergy[step]=sceneEnergy(withoutGlow);
                if(step==0)check(difference<=1&&visibleEnergy[step]<.001,
                    "Fully covered solar disk/halo leaked into HDR or Glow at camera/occluder angle "+std::to_string(angleIndex)+", SSR="+std::to_string(ssr));
                if(step+1==solarOffsets.size())check(difference>5&&visibleEnergy[step]>1,
                    "Uncovered solar disk failed to supply the angular Glow fixture.");
                const auto label="glow-sun-angle-"+std::to_string(angleIndex)+"-ssr-"+std::to_string(ssr)+"-step-"+std::to_string(step);
                save(options.artifacts,(label+"-off.png").c_str(),withoutGlow);save(options.artifacts,(label+"-on.png").c_str(),withGlow);
                solarOcclusion.push_back({{"angle",angleIndex},{"ssr",ssr},{"offset",solarOffsets[step]},
                    {"visible_solar_energy",visibleEnergy[step]},{"glow_energy",haloEnergy[step]},{"maximum_glow_difference",difference}});
            }
            for(std::size_t step=1;step<solarOffsets.size();++step) {
                check(visibleEnergy[step]>=visibleEnergy[step-1]-.001,"Solar coverage changed discontinuously as its opaque edge crossed the disk.");
                check(haloEnergy[step]>=haloEnergy[step-1]-haloEnergy.back()*.025,
                    "Partial solar Glow developed a discontinuous increase behind its opaque occluder.");
            }
            check(haloEnergy[2]>.01&&haloEnergy[2]<haloEnergy.back()*.95,
                "Half-covered solar disk did not retain a partial halo between covered and visible limits.");
        }
        // Covering just the disk leaves an authored wider sky halo visible.
        // Glow may spread that visible radiance across the dark occluder.
        setSunAngle(sunAngles[0],0);sky.sunHaloExtent=12;sky.sunHaloCurve=.5f;
        gameSettings.environment.ssr.enabled=true;glow.enabled=false;const auto haloOutsideDisk=sunCapture();
        glow.enabled=true;const auto spreadOutsideDisk=sunCapture();
        check(sceneEnergy(haloOutsideDisk)>1&&maximumDifference(haloOutsideDisk,spreadOutsideDisk)>5,
            "Visible authored solar halo was incorrectly suppressed when only its disk was covered.");
        save(options.artifacts,"glow-covered-disk-visible-sky-halo-off.png",haloOutsideDisk);
        save(options.artifacts,"glow-covered-disk-visible-sky-halo-on.png",spreadOutsideDisk);
        gameSettings=emitterSettings;game.instances=std::span(&instances[2],1);
        // GI retains its opaque donor and prior-world metadata independently
        // from the full-scene HDR source consumed by Glow after lighting.
        gameSettings.environment.ssgi.enabled=true;glow.enabled=true;
        const auto giGlow=capture();check(renderer.ssgiStatus().empty(),renderer.ssgiStatus());
        check(renderer.ssgiResidentBytes()>0&&maximumDifference(base,giGlow)>5,"SSGI + Glow bypassed its donor resources or halo.");
        glow.enabled=false;capture();check(renderer.ssgiStatus().empty(),renderer.ssgiStatus());
        check(renderer.ssgiResidentBytes()>0,"Disabling Glow invalidated the retained SSGI resources.");
        gameSettings.environment.ssgi.enabled=false;glow.enabled=true;
        sceneSettings=gameSettings;sceneSettings.environment.glow={};scene.instances=game.instances;
        const auto single=capture();renderer.render(scene);renderer.renderGame(game);const auto isolatedScene=read(renderer.viewportTexture()),isolatedGame=read(renderer.gameViewportTexture());
        check(maximumDifference(base,isolatedScene)<=1&&maximumDifference(single,isolatedGame)<=1,"Scene/Game Glow settings or resources leaked between views.");
        Json quality{{"glow_level_1",tightMetric},{"glow_level_7",wideMetric},{"transparent_emitter_halo_ratio",alphaRatio},{"solar_occlusion",solarOcclusion},
            {"glow_horizontal",horizontalMetric},{"glow_vertical",verticalMetric},{"checks","disabled/zero-work, linear FP32 level response/additivity/continuity, threshold/exposure, full-scene HDR eligibility, opaque occlusion, tint, saturation, anamorphic and Scene/Game isolation"}};
        if(!options.artifacts.empty()){fs::create_directories(options.artifacts);std::ofstream output(options.artifacts/"glow-quality.json");output<<quality.dump(2)<<'\n';}
        std::cout<<"GLOW_QUALITY_JSON "<<quality.dump()<<'\n';
    }
    resources.stop();renderer.drainPending(frame);
}
}
int main(int argc,char** argv) {
    try {Options options;for(int i=1;i<argc;++i){const std::string_view option=argv[i];
        const auto value=[&] {check(i+1<argc,"Missing option value.");return std::string(argv[++i]);};
        if(option=="--benchmark")options.benchmark=true;else if(option=="--level-response-only")options.levelResponseOnly=true;
        else if(option=="--hdr-source-only")options.hdrSourceOnly=true;
        else if(option=="--reference-unbounded")options.referenceUnbounded=true;
        else if(option=="--artifacts")options.artifacts=value();
        else if(option=="--warmup")options.warmup=std::stoul(value());else if(option=="--samples")options.samples=std::stoul(value());
        else throw std::runtime_error("Unknown argument: "+std::string(option));}
        check(!options.benchmark||!options.artifacts.empty(),"Benchmark requires --artifacts.");
        check(unsigned(options.benchmark)+unsigned(options.levelResponseOnly)+unsigned(options.hdrSourceOnly)<=1,
            "Benchmark, level-response-only and hdr-source-only are mutually exclusive.");
        check(!options.referenceUnbounded||options.hdrSourceOnly,"Reference-unbounded requires --hdr-source-only.");
        Sdl sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson Glow regression",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(bool(window),SDL_GetError());exercise(window.get(),options);return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
