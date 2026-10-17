// Explicit, hidden renderer comparison. It creates only synthetic CPU/GPU
// resources and output reports; it never opens or changes an authored project.
// Run each backend in a fresh process, serially, with identical arguments.
#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/GraphicsBackend.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/RenderFrame.h"
#include "crimson-renderer/Picking.h"
#include "resources/ResourceManager.h"
#include "assets/MaterialPreviewGeometry.h"

#include <SDL3/SDL.h>
#include <bgfx/platform.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <lodepng.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
constexpr std::uint16_t ReadView=254;
constexpr float Pi=3.14159265358979323846f;
void check(bool value,const std::string& text) { if(!value)throw std::runtime_error(text); }
double ms(Clock::time_point a,Clock::time_point b=Clock::now()) { return std::chrono::duration<double,std::milli>(b-a).count(); }
template<class T> struct Release { void operator()(T* p)const { if(p)p->Release(); } };
template<class T> using Com=std::unique_ptr<T,Release<T>>;
struct Sdl { Sdl(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());} ~Sdl(){SDL_Quit();} };
struct Texture { bgfx::TextureHandle value{BGFX_INVALID_HANDLE}; ~Texture(){if(bgfx::isValid(value))bgfx::destroy(value);} };
struct Options {
    GraphicsBackend backend{GraphicsBackend::Direct3D12};
    unsigned width{1920},height{1080},warmup{120},samples{360};
    bool capture{true};
    fs::path output;
};

// Both paths touch the hidden main swapchain and use bgfx's normal primary
// presentation cadence. There is no extra native Flush or GPU synchronization.
// Native device queries here describe the selected adapter only.
struct NativeAdapter {
    Com<IDXGIAdapter1> adapter;
    Com<IDXGIAdapter3> memory;
    Json describe() {
        const auto* internal=bgfx::getInternalData();
        if(bgfx::getRendererType()==bgfx::RendererType::Direct3D11) {
            auto* device=static_cast<ID3D11Device*>(internal->context);
            IDXGIDevice* rawDevice{};
            check(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&rawDevice))),"Cannot query DX11 DXGI device.");
            Com<IDXGIDevice> dxgiDevice(rawDevice);IDXGIAdapter* rawAdapter{};
            check(SUCCEEDED(dxgiDevice->GetAdapter(&rawAdapter)),"Cannot query DX11 adapter.");
            Com<IDXGIAdapter> selected(rawAdapter);IDXGIAdapter1* adapter1{};
            check(SUCCEEDED(selected->QueryInterface(IID_PPV_ARGS(&adapter1))),"Cannot query DX11 adapter description.");
            adapter.reset(adapter1);
        } else if(bgfx::getRendererType()==bgfx::RendererType::Direct3D12) {
            auto* device=static_cast<ID3D12Device*>(internal->context);
            IDXGIFactory4* rawFactory{};check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&rawFactory))),"Cannot create DXGI factory.");
            Com<IDXGIFactory4> factory(rawFactory);IDXGIAdapter1* adapter1{};
            check(SUCCEEDED(factory->EnumAdapterByLuid(device->GetAdapterLuid(),IID_PPV_ARGS(&adapter1))),"Cannot resolve DX12 adapter LUID.");
            adapter.reset(adapter1);
        } else throw std::runtime_error("Benchmark supports only actual DX11 and DX12 devices.");
        DXGI_ADAPTER_DESC1 description{};check(SUCCEEDED(adapter->GetDesc1(&description)),"Cannot describe selected adapter.");
        std::array<char,512> name{};
        check(WideCharToMultiByte(CP_UTF8,0,description.Description,-1,name.data(),int(name.size()),nullptr,nullptr)>0,"Cannot decode adapter name.");
        Json result{{"name",name.data()},{"vendor_id",description.VendorId},{"device_id",description.DeviceId},
            {"subsystem_id",description.SubSysId},{"revision",description.Revision},{"dedicated_video_bytes",description.DedicatedVideoMemory},
            {"shared_system_bytes",description.SharedSystemMemory},{"luid_high",description.AdapterLuid.HighPart},{"luid_low",description.AdapterLuid.LowPart}};
        LARGE_INTEGER version{};
        if(SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&version))) {
            const auto v=static_cast<std::uint64_t>(version.QuadPart);
            result["driver_version"]=std::to_string((v>>48)&65535)+"."+std::to_string((v>>32)&65535)+"."+
                std::to_string((v>>16)&65535)+"."+std::to_string(v&65535);
        } else result["driver_version"]=nullptr;
        IDXGIAdapter3* adapter3{};if(SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&adapter3))))memory.reset(adapter3);
        return result;
    }
    Json residency()const {
        DXGI_QUERY_VIDEO_MEMORY_INFO local{},nonlocal{};
        if(!memory||FAILED(memory->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local)))return nullptr;
        Json result{{"local_usage_bytes",local.CurrentUsage},{"local_budget_bytes",local.Budget}};
        if(SUCCEEDED(memory->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&nonlocal)))
            result["nonlocal_usage_bytes"]=nonlocal.CurrentUsage;
        return result;
    }
};

Json distribution(std::vector<double> values) {
    if(values.empty())return nullptr;
    std::sort(values.begin(),values.end());
    const auto percentile=[&](double fraction) {return values[std::min(values.size()-1,static_cast<std::size_t>(std::ceil(fraction*values.size()))-1)];};
    return {{"count",values.size()},{"mean",std::accumulate(values.begin(),values.end(),0.)/values.size()},
        {"p50",percentile(.5)},{"p95",percentile(.95)},{"min",values.front()},{"max",values.back()}};
}

struct Fixture : std::enable_shared_from_this<Fixture> {
    std::string name;
    bool effects{};
    unsigned columns{},rows{};
    std::shared_ptr<const ModelAsset> cube=materialPreviewGeometry(MaterialPreviewMesh::Cube);
    std::shared_ptr<const ModelAsset> sphere=materialPreviewGeometry(MaterialPreviewMesh::Sphere);
    std::array<std::vector<ModelMaterial>,8> materials;
    std::vector<ModelMaterialTextures> textures{1};
    std::vector<RenderModelInstance> instances;
    std::vector<PointLight> points;
    std::vector<DirectionalLight> directional;
    std::vector<RenderParticle> particles;
    std::vector<RenderParticleMaterial> particleMaterials{1};
    SceneSettings settings;
    Json metadata;

    explicit Fixture(bool gpu):name(gpu?"effects-heavy":"draw-heavy"),effects(gpu),columns(gpu?24:64),rows(gpu?18:48) {
        for(unsigned i=0;i<materials.size();++i) {
            materials[i]=cube->materials;auto& material=materials[i][0];
            material.baseColor={.25f+.07f*float(i%3),.24f+.06f*float((i+1)%3),.22f+.05f*float((i+2)%3),1};
            material.roughness=.25f+.08f*float(i%7);material.metallic=i==6?.8f:0;
            if(gpu&&i==7)material.emissive={5,.3f,.03f};
        }
        const float extentX=float(columns)*.62f,extentZ=float(rows)*.62f;
        RenderModelInstance floor;floor.entityId=1;floor.model.asset=cube;floor.materials=&materials[0];floor.textures=&textures;
        floor.model.transform=math::translation({0,-.15f,0})*math::scaling({extentX/.75f,.2f,extentZ/.75f});floor.forceFullDetail=true;
        floor.lightingScopes.directReceiverMask=lighting_scope::World;instances.push_back(floor);
        std::uint64_t triangles{};
        for(unsigned z=0;z<rows;++z)for(unsigned x=0;x<columns;++x) {
            auto instance=floor;instance.entityId=2+z*columns+x;instance.materials=&materials[(x+z)%materials.size()];
            instance.model.asset=gpu?sphere:cube;
            const float height=.35f+.15f*float((x*13+z*7)%5);
            instance.model.transform=math::translation({(float(x)-float(columns-1)*.5f)*1.2f,gpu?height:height*.75f,(float(z)-float(rows-1)*.5f)*1.2f})*
                math::rotation(math::axisAngle({0,1,0},float((x+z)%8)*Pi/8))*math::scaling({.42f,height,.42f});
            instances.push_back(instance);
            for(const auto& primitive:instance.model.asset->primitives)triangles+=primitive.indices.size()/3;
        }
        for(unsigned i=0;i<(gpu?256u:64u);++i) {
            PointLight point;point.entityId=100000+i;
            point.position={(float(i%16)-7.5f)*(extentX/8.f),1.3f+float(i%3)*.35f,(float(i/16)-float(gpu?15:3)*.5f)*(extentZ/(gpu?8.f:2.f))};
            point.color={.45f+.25f*float(i%3),.3f+.25f*float((i+1)%3),.2f+.25f*float((i+2)%3)};
            point.intensity=12;point.radius=gpu?7.f:5.f;point.castShadows=gpu&&i<2;point.shadowDepthBias=.002f;points.push_back(point);
        }
        DirectionalLight sun;sun.entityId=200000;sun.direction={-.35f,-1.f,-.25f};sun.intensity=.7f;
        sun.castShadows=true;sun.shadowDistance=160;directional.push_back(sun);
        settings.camera.position={0,extentZ*1.25f,extentZ*1.15f};settings.camera.target={0,0,-extentZ*.08f};
        settings.camera.fov=58;settings.camera.fovAxis=CameraFovAxis::Vertical;settings.camera.nearPlane=.1f;settings.camera.farPlane=240;
        settings.environment.enabled=true;settings.environment.intensity=.1f;
        settings.environment.toneMapping=ToneMapping::AgX;settings.environment.background.overrideColor=true;settings.environment.background.color={};
        settings.environment.ssgi.enabled=gpu;settings.environment.ssgi.quality=SsgiQuality::Medium;
        settings.environment.ssao.enabled=gpu;settings.environment.ssao.quality=SsaoQuality::Medium;
        settings.environment.glow.enabled=gpu;settings.environment.glow.strength=.5f;
        if(gpu)for(unsigned i=0;i<2048;++i) {
            RenderParticle particle;particle.position={(float(i%64)-31.5f)*.4f,1.f+float(i%11)*.08f,(float(i/64)-15.5f)*.4f};
            particle.size=.12f;particle.color={2,.4f,.03f,.4f};particle.billboardMode=ParticleBillboardMode::Enabled;particles.push_back(particle);
        }
        metadata={{"name",name},{"models_per_view",instances.size()},{"grid_columns",columns},{"grid_rows",rows},
            {"triangles_per_color_view_before_culling",triangles+12},{"point_lights",points.size()},{"point_shadow_sources",gpu?2:0},
            {"directional_shadow_sources",1},{"shadow_resolution",1024},{"ssgi",gpu},{"ssgi_quality","Medium"},{"ssgi_bounces",1},
            {"ssao",gpu},{"ssao_quality","Medium"},{"glow",gpu},{"particles_per_view",particles.size()},
            {"motion","Small deterministic camera orbit; sixteen moving models and two moving point lights"},
            {"geometry","Shared built-in cube/sphere buffers, native PBR material factors, no disk textures or authored assets"}};
    }

    std::shared_ptr<const PreparedRenderFrame> frame(bool dual,unsigned number,std::uint64_t identity,const Options& options)const {
        auto world=std::make_shared<RenderWorldSnapshot>();world->pinned=shared_from_this();world->points=points;world->directional=directional;
        world->particles=particles;world->particleMaterials=particleMaterials;world->shadowWorldId=identity;world->shadowResolution=1024;
        world->shadowInstances=instances;
        const float time=float(number%240)*2*Pi/240.f;
        for(std::size_t i=1;i<std::min<std::size_t>(17,world->shadowInstances.size());++i)
            world->shadowInstances[i].model.transform=math::translation({0,std::sin(time+float(i))*.12f,0})*instances[i].model.transform;
        for(unsigned i=0;i<std::min<unsigned>(2,static_cast<unsigned>(world->points.size()));++i)
            world->points[i].position[0]+=std::sin(time+float(i))*.3f;
        auto result=std::make_shared<PreparedRenderFrame>();result->world=world;result->gridVisible=false;result->appearance.backgroundSrgb={};
        const auto append=[&](RenderViewPurpose purpose) {
            PreparedRenderView view;view.identity.purpose=purpose;view.settings=settings;
            view.settings.camera.position[0]+=std::sin(time)*.35f;view.settings.camera.target[0]+=std::sin(time)*.35f;
            if(purpose==RenderViewPurpose::Scene) {view.settings.camera.position[0]+=2.f;view.settings.camera.target[0]+=2.f;}
            view.width=options.width;view.height=options.height;view.instances=world->shadowInstances;
            view.giWorldId=identity;view.giCameraId=purpose==RenderViewPurpose::Scene?1:2;view.giEnvironmentId=1;
            result->views.push_back(std::move(view));
        };
        if(dual)append(RenderViewPurpose::Scene);append(RenderViewPurpose::Game);return result;
    }
};

struct CpuSample {
    std::uint32_t submitted{},queryFrame{};
    double prepare{},submit{},frame{},wall{},backend{};
    std::uint32_t draws{},computes{},triangles{},shadowDraws{};
};
struct GpuSample { std::uint32_t frame{};double whole{}; };
GpuSample gpuSample(const bgfx::Stats& stats) {
    GpuSample result;result.frame=stats.gpuFrameNum;
    const double scale=1000./double(stats.gpuTimerFreq);result.whole=double(stats.gpuTimeEnd-stats.gpuTimeBegin)*scale;
    return result;
}

Json memoryStats(Renderer& renderer,const NativeAdapter& adapter) {
    const auto* stats=bgfx::getStats();
    return {{"dxgi_process_residency",adapter.residency()},
        {"bgfx_driver_memory_bytes",stats->gpuMemoryUsed>=0?Json(stats->gpuMemoryUsed):Json(nullptr)},
        {"bgfx_texture_estimate_bytes",stats->textureMemoryUsed>=0?Json(stats->textureMemoryUsed):Json(nullptr)},
        {"bgfx_render_target_estimate_bytes",stats->rtMemoryUsed>=0?Json(stats->rtMemoryUsed):Json(nullptr)},
        {"model_owner_bytes",renderer.modelResidentBytes()},{"shadow_owner_bytes",renderer.shadowResidentBytes()},
        {"environment_and_screen_lighting_owner_bytes",renderer.environmentResidentBytes()},
        {"ssgi_owner_bytes_subset",renderer.ssgiResidentBytes()},{"ssao_owner_bytes_subset",renderer.ssaoResidentBytes()},
        {"glow_owner_bytes",renderer.glowResidentBytes()},{"textures",stats->numTextures},{"framebuffers",stats->numFrameBuffers}};
}

void exercise(SDL_Window* window,const Options& options) {
    // Destinations precede Renderer so an exceptional shutdown cannot invalidate
    // readback storage while bgfx still owns a queued write to it.
    std::vector<std::shared_ptr<DecodedImage>> readbacks;
    ResourceManager resources;Renderer renderer(window,nullptr,options.backend);NativeAdapter adapter;
    const unsigned measurementFrames=options.samples*2;
    Json report;report["schema"]=2;report["fixture_version"]=1;report["backend"]=renderer.backendName();report["adapter"]=adapter.describe();
    report["viewport"]={options.width,options.height};report["warmup_frames"]=options.warmup;
    report["minimum_gpu_samples"]=options.samples;report["measured_cpu_frames_per_phase"]=measurementFrames;
    report["vsync"]=false;report["presentation"]="Offscreen views plus an empty touch of the hidden main swapchain on both backends; CPU wall includes hidden Present/queue backpressure, not visible desktop composition latency";
    report["scope"]="Renderer-only production prepared-frame submission; fixture snapshot preparation included, editor UI/game simulation/asset IO excluded";
    report["per_view_gpu_profiling"]=false;
    report["explicit_native_flush"]=false;
    report["timing_notes"]="Both backends render a fixed CPU window of twice --samples and must expose at least --samples unique whole-frame GPU queries from that window. Sixteen untimed tail frames expose delayed results. Both use bgfx's normal primary Present cadence without extra native Flush or GPU waits. Whole-frame timers remain enabled by bgfx; optional per-view GPU profiling is disabled to match ordinary application rendering. Pinned bgfx returns only the latest completed query per kind; DX12 TimerQueryD3D12::update drains completed queries and overwrites that result, so intermediate GPU frame IDs can be unavailable when several complete between getStats calls. Actual coverage/gaps are reported; duplicate results are never counted. CPU frame includes normal queue backpressure.";
    report["memory_notes"]="DXGI process-local residency is comparable across backends but includes driver allocations. Owner counters count retained allocations; SSGI/SSAO are subsets of environment. bgfx estimates may overlap and are not summed. Driver memory semantics can differ by backend.";
    report["fixture_notes"]="Deterministic synthetic stress, not a prediction of gameplay frame rate. Warmup excludes construction, shader creation, first shadow population and temporal-history activation.";
    report["timestamp_utc_unix_seconds"]=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    fs::create_directories(options.output);
    std::ofstream cpuCsv(options.output/"cpu-frames.csv"),gpuCsv(options.output/"gpu-frames.csv");
    check(bool(cpuCsv)&&bool(gpuCsv),"Cannot create benchmark CSV reports.");
    cpuCsv<<"phase,index,submitted_frame,query_gpu_frame,fixture_prepare_ms,renderer_submit_ms,bgfx_frame_ms,cpu_wall_ms,bgfx_backend_ms,draws,computes,triangles,shadow_draws\n";
    gpuCsv<<"phase,gpu_frame,gpu_ms\n";
    bgfx::reset(options.width,options.height,BGFX_RESET_NONE);bgfx::setDebug(BGFX_DEBUG_NONE);renderer.setGridVisible(false);
    bgfx::setViewClear(ToolsView,BGFX_CLEAR_NONE);bgfx::setViewName(ToolsView,"Benchmark: empty hidden swapchain touch");
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x42454e4348445832;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.16f,.19f,.25f})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    std::uint32_t frame{};
    const auto tick=[&] {resources.pump();renderer.advanceStreaming();bgfx::touch(ToolsView);frame=bgfx::frame();};
    const auto checkStatus=[&] {
        check(renderer.modelStatus().empty(),renderer.modelStatus());check(renderer.shadowStatus().empty(),renderer.shadowStatus());
        check(renderer.clusteredLightingStatus().empty(),renderer.clusteredLightingStatus());
        check(renderer.ssgiStatus().empty(),renderer.ssgiStatus());check(renderer.ssaoStatus().empty(),renderer.ssaoStatus());check(renderer.glowStatus().empty(),renderer.glowStatus());
    };
    for(bool effects:{false,true}) {
        auto fixture=std::make_shared<Fixture>(effects);
        check(renderer.prepareModel(*fixture->cube),renderer.modelStatus());check(renderer.prepareModel(*fixture->sphere),renderer.modelStatus());
        for(bool dual:{false,true}) {
            const std::string phase=fixture->name+(dual?"-scene-game":"-game");
            const std::uint64_t identity=(effects?30:10)+(dual?1:0);
            const auto deadline=Clock::now()+std::chrono::seconds{180};
            renderer.resizeViewport(dual?options.width:1,dual?options.height:1);renderer.resizeGameViewport(options.width,options.height);
            const auto submit=[&](unsigned number) {
                check(Clock::now()<deadline,"Benchmark phase exceeded its three-minute bound: "+phase);
                const auto begin=Clock::now();CpuSample sample;resources.pump();renderer.advanceStreaming();
                auto prepared=fixture->frame(dual,number,identity,options);sample.prepare=ms(begin);
                const auto submitBegin=Clock::now();renderer.renderFrame(std::move(prepared));sample.submit=ms(submitBegin);
                // DX12 presents its primary swapchain unconditionally. Touch
                // the DX11 primary too so its m_needPresent gate follows the
                // same path; an offscreen-only benchmark would be asymmetric.
                bgfx::touch(ToolsView);
                const auto frameBegin=Clock::now();frame=bgfx::frame();sample.frame=ms(frameBegin);sample.wall=ms(begin);sample.submitted=frame;
                const auto* stats=bgfx::getStats();sample.queryFrame=stats->gpuFrameNum;sample.draws=stats->numDraw;sample.computes=stats->numCompute;
                sample.triangles=stats->numPrims[bgfx::Topology::TriList];sample.shadowDraws=renderer.shadowStats().drawCalls;
                if(stats->cpuTimerFreq>0)sample.backend=double(stats->cpuTimeEnd-stats->cpuTimeBegin)*1000./double(stats->cpuTimerFreq);
                return sample;
            };
            std::cout<<"Warming "<<phase<<" ("<<options.warmup<<" frames)"<<std::endl;
            for(unsigned i=0;i<options.warmup;++i)submit(i);
            checkStatus();
            const auto checkWorkload=[&] {
                const auto* stats=bgfx::getStats();const auto views=dual?2u:1u;
                check(stats->numDraw>=fixture->instances.size()*views,"Fixture model draw workload was not submitted: "+phase);
                check(renderer.modelResidentBytes()>0&&renderer.shadowResidentBytes()>0,"Fixture geometry/shadow resources are missing: "+phase);
                check(renderer.shadowStats().directionalLights>0,"Fixture directional shadow was bypassed: "+phase);
                if(effects) {
                    check(renderer.ssgiResidentBytes()>0&&renderer.ssaoResidentBytes()>0&&renderer.glowResidentBytes()>0,
                        "A requested GI/AO/Glow workload allocated no resources: "+phase);
                    check(stats->numCompute>0&&renderer.shadowStats().activeLights>=2,"A requested compute/point-shadow workload was bypassed: "+phase);
                    const auto particlesPresent=[&](const ParticleRenderStats& s) {
                        return s.requested==fixture->particles.size()&&s.submitted==fixture->particles.size()&&!s.invalid&&!s.dropped&&s.drawCalls>0;
                    };
                    check(particlesPresent(renderer.gameParticleStats()),"Game particle workload was dropped: "+phase);
                    if(dual)check(particlesPresent(renderer.particleStats()),"Scene particle workload was dropped: "+phase);
                }
            };
            checkWorkload();
            std::vector<CpuSample> cpu;std::vector<GpuSample> gpu;std::set<std::uint32_t> gpuFrames;
            const auto first=frame+1,last=first+measurementFrames-1;
            const auto collectGpu=[&] {
                const auto* stats=bgfx::getStats();
                if(stats->gpuFrameNum>=first&&stats->gpuFrameNum<=last&&stats->gpuTimerFreq>0&&stats->gpuTimeEnd>stats->gpuTimeBegin&&gpuFrames.insert(stats->gpuFrameNum).second)
                    gpu.push_back(gpuSample(*stats));
            };
            const auto measureBegin=Clock::now();
            for(unsigned i=0;i<measurementFrames;++i) {cpu.push_back(submit(options.warmup+i));collectGpu();}
            const double measureWall=ms(measureBegin);
            for(unsigned i=0;i<16;++i) {submit(options.warmup+measurementFrames+i);collectGpu();}
            checkStatus();checkWorkload();
            for(std::size_t i=0;i<cpu.size();++i) {const auto& s=cpu[i];cpuCsv<<phase<<','<<i<<','<<s.submitted<<','<<s.queryFrame<<','<<s.prepare<<','<<s.submit<<','<<s.frame<<','<<s.wall<<','<<s.backend<<','<<s.draws<<','<<s.computes<<','<<s.triangles<<','<<s.shadowDraws<<'\n';}
            for(const auto& s:gpu)gpuCsv<<phase<<','<<s.frame<<','<<s.whole<<'\n';
            Json result;result["fixture"]=fixture->metadata;result["views"]=dual?2:1;result["wall_measurement_ms"]=measureWall;
            result["throughput_fps"]=1000.*cpu.size()/measureWall;result["memory"]=memoryStats(renderer,adapter);
            unsigned interiorGaps{},largestJump{},previousGpuFrame{};
            for(const auto id:gpuFrames) {
                if(previousGpuFrame) {const auto jump=id-previousGpuFrame;interiorGaps+=jump-1;largestJump=std::max(largestJump,jump);}
                previousGpuFrame=id;
            }
            result["gpu_query_availability"]={{"measured_cpu_frames",measurementFrames},{"minimum_unique_queries",options.samples},
                {"observed_unique_queries",gpu.size()},{"coverage",double(gpu.size())/measurementFrames},
                {"unavailable_measured_frame_ids",measurementFrames-gpu.size()},{"interior_sequence_gaps",interiorGaps},
                {"largest_observed_frame_jump",largestJump},{"first_measured_frame",first},{"last_measured_frame",last},
                {"motion_cycles",double(measurementFrames)/240.},{"complete",gpu.size()>=options.samples}};
            const auto cpuDistribution=[&](auto member) {std::vector<double> values;for(const auto& sample:cpu)values.push_back(static_cast<double>(sample.*member));return distribution(std::move(values));};
            const auto gpuDistribution=[&](auto member) {std::vector<double> values;for(const auto& sample:gpu)values.push_back(sample.*member);return distribution(std::move(values));};
            result["cpu_fixture_prepare_ms"]=cpuDistribution(&CpuSample::prepare);result["cpu_renderer_submit_ms"]=cpuDistribution(&CpuSample::submit);
            result["cpu_bgfx_frame_ms"]=cpuDistribution(&CpuSample::frame);result["cpu_wall_ms"]=cpuDistribution(&CpuSample::wall);result["bgfx_backend_ms"]=cpuDistribution(&CpuSample::backend);
            result["draws"]=cpuDistribution(&CpuSample::draws);result["computes"]=cpuDistribution(&CpuSample::computes);result["triangles"]=cpuDistribution(&CpuSample::triangles);result["shadow_draws"]=cpuDistribution(&CpuSample::shadowDraws);
            result["gpu_ms"]=gpuDistribution(&GpuSample::whole);
            // Preserve diagnostics even if a driver does not expose enough
            // unique timestamps; a failure must leave its actual frame IDs.
            cpuCsv.flush();gpuCsv.flush();report["phases"][phase]=result;
            {std::ofstream output(options.output/"report.json");check(bool(output),"Cannot create benchmark report.");output<<report.dump(2)<<'\n';check(bool(output),"Cannot write benchmark report.");}
            check(gpu.size()>=options.samples,"Too few unique GPU timing samples for "+phase+": "+std::to_string(gpu.size())+
                " observed from "+std::to_string(measurementFrames)+" submitted frames; minimum="+std::to_string(options.samples));
            std::cout<<phase<<" CPU p50/p95 "<<result["cpu_wall_ms"]["p50"]<<'/'<<result["cpu_wall_ms"]["p95"]
                <<" ms, GPU p50/p95 "<<result["gpu_ms"]["p50"]<<'/'<<result["gpu_ms"]["p95"]<<" ms ("<<gpu.size()<<'/'<<measurementFrames
                <<" unique queries), draws "<<result["draws"]["p50"]<<std::endl;
            // Capture a fixed pose after fixed-pose convergence, independently
            // of the timed camera/model motion and GPU query completion lag.
            if(options.capture) {
                for(unsigned i=0;i<96;++i)submit(0);
                Texture staging{bgfx::createTexture2D(static_cast<std::uint16_t>(options.width),static_cast<std::uint16_t>(options.height),false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
                check(bgfx::isValid(staging.value),"Cannot allocate benchmark readback.");
                auto image=std::make_shared<DecodedImage>(DecodedImage{options.width,options.height,std::vector<std::uint8_t>(std::size_t(options.width)*options.height*4)});readbacks.push_back(image);
                bgfx::blit(ReadView,staging.value,0,0,renderer.gameViewportTexture(),0,0,static_cast<std::uint16_t>(options.width),static_cast<std::uint16_t>(options.height));
                const auto ready=bgfx::readTexture(staging.value,image->rgba.data());check(ready!=UINT32_MAX,"Benchmark readback unsupported.");
                // Always advance the same number of owner frames on both APIs,
                // even if one readback completes sooner. SSGI's stochastic seed
                // derives from that owner ordinal, including subsequent phases.
                bool complete{};for(unsigned i=0;i<16;++i){tick();complete=complete||picking::frameReady(frame,ready);}
                check(complete,"Benchmark readback timed out.");
                std::uint64_t energy{};unsigned minimum=255,maximum{};
                for(std::size_t i=0;i<image->rgba.size();++i)if(i%4!=3) {
                    energy+=image->rgba[i];minimum=std::min(minimum,unsigned(image->rgba[i]));maximum=std::max(maximum,unsigned(image->rgba[i]));
                }
                check(energy>std::uint64_t(options.width)*options.height*3,"Benchmark fixture rendered black.");
                check(maximum-minimum>16,"Benchmark capture contains no meaningful image contrast.");
                std::vector<unsigned char> png;check(lodepng::encode(png,image->rgba,image->width,image->height)==0,"Cannot encode benchmark capture.");
                const auto name=phase+".png";std::ofstream output(options.output/name,std::ios::binary);check(bool(output),"Cannot create benchmark capture.");
                output.write(reinterpret_cast<const char*>(png.data()),static_cast<std::streamsize>(png.size()));check(bool(output),"Cannot write benchmark capture.");
                result["capture"]={{"file",name},{"mean_rgb_code_value",double(energy)/(options.width*options.height*3.)}};
            }
            report["phases"][phase]=std::move(result);
            std::ofstream output(options.output/"report.json");check(bool(output),"Cannot create benchmark report.");output<<report.dump(2)<<'\n';check(bool(output),"Cannot write benchmark report.");
        }
        renderer.drainPending(frame);
    }
    resources.stop();renderer.drainPending(frame);std::cout<<"Report: "<<(options.output/"report.json").string()<<'\n';
}
}

int main(int argc,char** argv) {
    try {
        Options options;
        for(int i=1;i<argc;++i) {
            const std::string_view option=argv[i];
            if(option=="--no-capture")options.capture=false;
            else {
                check(i+1<argc,"Missing value for "+std::string(option));const std::string_view value=argv[++i];
                if(option=="--backend") {
                    check(value=="dx11"||value=="dx12","Backend must be dx11 or dx12.");
                    options.backend=value=="dx11"?GraphicsBackend::Direct3D11:GraphicsBackend::Direct3D12;
                } else if(option=="--output")options.output=fs::absolute(value).lexically_normal();
                else {
                    check(option=="--warmup"||option=="--samples"||option=="--width"||option=="--height","Unknown option: "+std::string(option));
                    std::size_t parsed{};const auto number=std::stoul(std::string(value),&parsed);check(parsed==value.size()&&number>0&&number<=10000,"Invalid frame count or dimension.");
                    if(option=="--warmup")options.warmup=static_cast<unsigned>(number);
                    else if(option=="--samples")options.samples=static_cast<unsigned>(number);
                    else if(option=="--width")options.width=static_cast<unsigned>(number);else options.height=static_cast<unsigned>(number);
                }
            }
        }
        check(!options.output.empty(),"Usage: renderer_backend_benchmark --backend dx11|dx12 --output DIRECTORY [--warmup 120 --samples 360 --width 1920 --height 1080 --no-capture]");
        check(options.warmup>=32&&options.samples>=30,"Use at least 32 warmup and 30 measured frames.");
        Sdl sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson backend benchmark",static_cast<int>(options.width),static_cast<int>(options.height),SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(bool(window),SDL_GetError());std::cout<<std::fixed<<std::setprecision(3);exercise(window.get(),options);return 0;
    } catch(const std::exception& error) {std::cerr<<"Backend benchmark failed: "<<error.what()<<'\n';return 1;}
}
