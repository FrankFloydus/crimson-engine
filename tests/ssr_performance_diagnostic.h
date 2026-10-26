// Explicit saved-scene performance diagnostic. Source data is copied before
// opening stores; timed frames never touch the authored project or drive UI.
#include <cmath>
#include <optional>
#include <set>
#include <string_view>
#include <crimson/renderer/core/Profiler.hpp>
#if CRIMSON_ENABLE_PROFILING
#include <bgfx/profiling.h>
#endif

namespace {
Json ssrDistribution(std::vector<double> values) {
    if(values.empty())return nullptr;
    std::sort(values.begin(),values.end());
    const auto percentile=[&](double fraction){return values[std::min(values.size()-1,static_cast<std::size_t>(std::ceil(values.size()*fraction))-1)];};
    return {{"count",values.size()},{"mean",std::accumulate(values.begin(),values.end(),0.)/values.size()},
        {"p50",percentile(.5)},{"p95",percentile(.95)},{"min",values.front()},{"max",values.back()}};
}
int ssrPerformanceDiagnostic(int argc,char** argv) {
    require(argc>=7,"Usage: forward_plus_room_benchmark --ssr-performance-project PROJECT SHADERC SHADER_SOURCES SHADER_INCLUDES OUTPUT [--width N --height N --scene-width N --scene-height N --warmup N --samples N --views game|both --ssr authored|off --contact-geometry prepared|dense --profile-views --tracy --backend dx11|dx12 --shader-cache DIR --contact-shader-override DIR --ssr-trace-shader-override DIR]");
    unsigned width=1280,height=720,warmupMinimum=60,sampleMinimum=120;
    std::optional<unsigned> sceneWidthOption,sceneHeightOption;
    bool both=false,ssrOff=false,profile=false,tracy=false,denseContactGeometry=false,contactGeometryRequested=false;auto backend=selectedTestGraphicsBackend();
    fs::path persistentShaderCache,contactShaderSources,traceShaderSources;
    for(int i=7;i<argc;++i) {
        const std::string option=argv[i];
        if(option=="--profile"||option=="--profile-views")profile=true;
        else if(option=="--tracy")tracy=true;
        else if(option=="--both")both=true;
        else if(option=="--ssr-off")ssrOff=true;
        else {
            require(i+1<argc,"Missing SSR performance option value: "+option);const std::string value=argv[++i];
            if(option=="--shader-cache")persistentShaderCache=fs::absolute(value);
            else if(option=="--contact-shader-override")contactShaderSources=fs::weakly_canonical(value);
            else if(option=="--ssr-trace-shader-override")traceShaderSources=fs::weakly_canonical(value);
            else if(option=="--views"){require(value=="game"||value=="both","--views must be game or both.");both=value=="both";}
            else if(option=="--ssr"){require(value=="authored"||value=="off","--ssr must be authored or off.");ssrOff=value=="off";}
            else if(option=="--contact-geometry"){require(value=="prepared"||value=="dense","--contact-geometry must be prepared or dense.");denseContactGeometry=value=="dense";contactGeometryRequested=true;}
            else if(option=="--backend"){const auto parsed=parseGraphicsBackend(value);require(parsed.has_value(),"Unknown SSR diagnostic backend.");backend=*parsed;}
            else if(option=="--width"||option=="--height"||option=="--scene-width"||option=="--scene-height"||option=="--warmup"||option=="--samples") {
                const auto count=std::stoul(value);require(count>0&&count<=100000,"SSR diagnostic numeric option is out of range.");
                if(option=="--width"||option=="--height"||option=="--scene-width"||option=="--scene-height")require(count<=8192,"SSR diagnostic viewport is too large.");
                if(option=="--width")width=static_cast<unsigned>(count);else if(option=="--height")height=static_cast<unsigned>(count);
                else if(option=="--scene-width")sceneWidthOption=static_cast<unsigned>(count);else if(option=="--scene-height")sceneHeightOption=static_cast<unsigned>(count);
                else if(option=="--warmup")warmupMinimum=static_cast<unsigned>(count);else sampleMinimum=static_cast<unsigned>(count);
            } else throw std::runtime_error("Unknown SSR performance option: "+option);
        }
    }
    const unsigned sceneWidth=sceneWidthOption.value_or(width),sceneHeight=sceneHeightOption.value_or(height);
    const fs::path original=fs::weakly_canonical(argv[2]),output=fs::absolute(argv[6]);Temporary temporary;
    std::cout<<"Staging saved-scene snapshot at "<<temporary.root<<std::endl;
    copyGiProject(original,temporary.root);const auto project=temporary.root;initializeContentStorage(project);fs::create_directories(output);
    Json session;{std::ifstream file(project/".crimson/editor-session.json");if(!file)file.open(project/"assets/editor-session.json");require(bool(file),"No saved editor session in snapshot.");file>>session;}
    const auto sceneId=session.at("scene").get<std::string>();SceneDocument document(project);require(document.load(sceneId),document.status());
    const auto extracted=extractScene(document.scene());const auto gameCamera=resolveGameCamera(document.scene());
    require(gameCamera.has_value(),"Saved scene has no resolvable active Game camera.");
    const auto activeContactLights=std::count_if(extracted.directional.begin(),
        extracted.directional.begin()+std::min<std::size_t>(extracted.directional.size(),MaxDirectionalLights),[](const auto& light) {
            return light.enabled&&light.castShadows&&std::isfinite(light.intensity)&&light.intensity>0&&
                light.roles.receiverMask&&light.roles.shadowCasterMask;
        });
    require(!contactGeometryRequested||activeContactLights>0,
        "The requested contact-geometry comparison has no active shadow-casting directional light in the saved fixture.");
    Camera sceneCamera=*gameCamera;
    if(session.contains("cameras")&&session.at("cameras").contains(sceneId)) {
        const auto& saved=session.at("cameras").at(sceneId);sceneCamera.position=saved.at("position").get<Vec3>();
        sceneCamera.target=saved.at("target").get<Vec3>();sceneCamera.up=saved.at("up").get<Vec3>();
        sceneCamera.fov=saved.value("fov",45.f);sceneCamera.nearPlane=saved.value("near",.01f);sceneCamera.farPlane=saved.value("far",2048.f);
    }
    Sdl sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Crimson saved-scene SSR performance diagnostic",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
    require(bool(window),SDL_GetError());
    AntiAliasing antiAliasing=AntiAliasing::Smaa;unsigned shadowResolution=1024;Json preferenceSnapshot;
    // A frozen benchmark fixture can carry the source application's preferences
    // without reading mutable global settings again for each paired trial.
    fs::path preferencePath=project/".crimson/diagnostic-preferences.json";
    if(!fs::is_regular_file(preferencePath))if(char* preferenceDirectory=SDL_GetPrefPath("CrimsonEngine","Editor")) {
        preferencePath=fs::path(preferenceDirectory)/"preferences.json";SDL_free(preferenceDirectory);
    }
    if(std::ifstream file(preferencePath);file) {
        file>>preferenceSnapshot;
        antiAliasing=preferenceSnapshot.value("antiAliasing",std::string{"smaa"})=="off"?AntiAliasing::Off:AntiAliasing::Smaa;
        shadowResolution=preferenceSnapshot.value("pointShadowResolution",1024u);
    }
    // Native Tracy GPU contexts are created during bgfx initialization.
    shared::profiling::set_enabled(tracy);
    ResourceManager resources;Renderer renderer(native_renderer_context(window.get(),nullptr,backend));
    ShaderCompileConfig config{argv[3],argv[4],argv[5],resolveAssetPath(project,"assets/cache/shaders")};
    if(!persistentShaderCache.empty()) {fs::create_directories(persistentShaderCache);config.cacheDirectory=persistentShaderCache;}
    if(!contactShaderSources.empty()) {
        require(fs::is_directory(contactShaderSources),"Private contact shader directory does not exist.");
        require(contactShaderSources!=fs::weakly_canonical(config.sourceDirectory),"Contact attribution requires a private shader source directory.");
        ShaderCompileConfig overrideConfig{config.compiler,contactShaderSources,config.includeDirectory,output/"contact-shader-cache"};
        const std::array pairs{RendererShaderPair{RendererProgram::ShadowContactTrace,{},compileShader(overrideConfig,"cs_shadow_contact_trace").bytes},
            RendererShaderPair{RendererProgram::ShadowContactPublish,{},compileShader(overrideConfig,"cs_shadow_contact_publish").bytes}};
        require(renderer.reloadPrograms(pairs),renderer.shaderStatus());
    }
    if(!traceShaderSources.empty()) {
        require(fs::is_directory(traceShaderSources),"Private SSR trace shader directory does not exist.");
        require(traceShaderSources!=fs::weakly_canonical(config.sourceDirectory),"SSR attribution requires a private shader source directory.");
        ShaderCompileConfig overrideConfig{config.compiler,traceShaderSources,config.includeDirectory,output/"ssr-trace-cache"};
        const std::array pairs{RendererShaderPair{RendererProgram::SsrTraceCompute,{},compileShader(overrideConfig,"cs_ssr_trace").bytes}};
        require(renderer.reloadPrograms(pairs),renderer.shaderStatus());
    }
    MaterialGraphPrograms programs(resources,config);AssetLibrary assets(project,&resources);assets.setMaterialGraphPrograms(&programs);
    EditorResources prepared(assets,renderer,resources,project);
    const auto map=extracted.environment.environment.source==EnvironmentSource::Hdri?
        assets.references().resolveEnvironment(extracted.environment.environment.map):std::string{};
    require(renderer.publishEnvironment(*prepareEnvironmentCpu(project,map)),renderer.environmentStatus());
    renderer.resizeViewport(both?sceneWidth:1,both?sceneHeight:1);renderer.resizeGameViewport(width,height);renderer.setGridVisible(false);
    bgfx::reset(320,240,BGFX_RESET_NONE);bgfx::setDebug(profile?BGFX_DEBUG_PROFILER:BGFX_DEBUG_NONE);
    struct Cpu {double wall{},update{},prepare{},shadowPrepare{},receiverPrepare{},submit{},advance{},backend{};
        std::uint64_t draws{},computes{},triangles{},shadowDraws{},sceneModels{},gameModels{},rayInstances{},rayTriangles{},rayNodes{};std::uint32_t frame{},query{};};
    struct Gpu {std::uint32_t frame{};double elapsed{};};
    std::uint32_t frame{};bool ready{};std::shared_ptr<const PreparedRenderFrame> lastPrepared;
    struct DenseModel {std::shared_ptr<const ModelAsset> source,replacement;};
    std::map<const ModelAsset*,DenseModel> denseModels;
    std::shared_ptr<const PreparedRenderFrame> denseSourceFrame,densePreparedFrame;
    bool freezeDenseFrame{};
    std::size_t denseClearedPrimitives{};
    const auto sameInstances=[](const auto& first,const auto& second) {
        if(first.size()!=second.size())return false;
        for(std::size_t index=0;index<first.size();++index) {
            const auto& a=first[index];const auto& b=second[index];
            if(a.entityId!=b.entityId||a.model.asset!=b.model.asset||a.resourceRevision!=b.resourceRevision||
                a.materials!=b.materials||a.textures!=b.textures||a.pose!=b.pose)return false;
        }
        return true;
    };
    const auto contactGeometryFrame=[&](std::shared_ptr<const PreparedRenderFrame> source) {
        if(!denseContactGeometry)return source;
        if(freezeDenseFrame) {
            // The diagnostic's cameras, scene, material time and lights are
            // frozen. Reuse the complete pinned warm frame, and reject a late
            // resource publication rather than cloning/preparing during timing.
            require(denseSourceFrame&&densePreparedFrame&&source->world&&denseSourceFrame->world&&
                source->resourceSignature==denseSourceFrame->resourceSignature&&source->views.size()==denseSourceFrame->views.size()&&
                sameInstances(source->world->shadowInstances,denseSourceFrame->world->shadowInstances),
                "Dense contact source frame changed during the measured workload.");
            for(std::size_t index=0;index<source->views.size();++index)
                require(sameInstances(source->views[index].instances,denseSourceFrame->views[index].instances),
                    "Dense contact receiver resources changed during the measured workload.");
            return densePreparedFrame;
        }
        const auto replace=[&](auto& instances) {
            for(auto& instance:instances) {
                const auto& asset=instance.model.asset;
                if(!asset||std::none_of(asset->primitives.begin(),asset->primitives.end(),
                    [](const auto& primitive){return bool(primitive.preparedShadowRays);}))continue;
                auto found=denseModels.find(asset.get());
                if(found==denseModels.end()) {
                    auto replacement=std::make_shared<ModelAsset>(*asset);
                    replacement->id+=":dense-contact-reference";
                    for(auto& primitive:replacement->primitives)if(primitive.preparedShadowRays) {
                        primitive.preparedShadowRays.reset();++denseClearedPrimitives;
                    }
                    require(renderer.prepareModel(*replacement),"Dense contact model preparation failed: "+renderer.modelStatus());
                    found=denseModels.emplace(asset.get(),DenseModel{asset,std::move(replacement)}).first;
                }
                instance.model.asset=found->second.replacement;
            }
        };
        auto adapted=std::make_shared<PreparedRenderFrame>(*source);
        require(bool(source->world),"Dense contact reference requires the prepared world.");
        auto world=std::make_shared<RenderWorldSnapshot>(*source->world);
        replace(world->shadowInstances);adapted->world=std::move(world);
        for(auto& view:adapted->views)replace(view.instances);
        // Copies retain the original world/view pins, paint streams, materials,
        // poses, IDs and transforms. Only the contact BVH source is substituted.
        denseSourceFrame=std::move(source);densePreparedFrame=std::move(adapted);
        return densePreparedFrame;
    };
    const auto tick=[&]() {
        const auto begin=Clock::now();Cpu sample;resources.pump();assets.updateResources(frame);renderer.advanceStreaming();prepared.beginFrame();sample.update=milliseconds(begin);
        EditorRenderFrameInput input;input.extracted=extracted;input.settings.environment=extracted.environment.environment;
        if(ssrOff)input.settings.environment.ssr.enabled=false;
        input.settings.environment.map=assets.references().resolveEnvironment(input.settings.environment.map);
        input.settings.exposure=extracted.environment.exposure;input.settings.proceduralSun=extracted.proceduralSun;input.settings.antiAliasing=antiAliasing;
        input.sceneCamera=sceneCamera;input.gameCamera=gameCamera;input.sceneWidth=both?sceneWidth:1;input.sceneHeight=both?sceneHeight:1;
        input.gameWidth=width;input.gameHeight=height;input.sceneAspect=float(sceneWidth)/sceneHeight;input.sceneVisible=both;input.gameVisible=true;
        input.worldId=1;input.giWorldId=1;input.gameCameraId=document.scene().properties().activeCamera;
        input.shadowResolution=static_cast<std::uint16_t>(shadowResolution);input.materialContext={};
        EditorRenderFramePreparationStats preparation;const auto preparationStart=Clock::now();
        lastPrepared=contactGeometryFrame(prepareEditorRenderFrame(std::move(input),assets,prepared,&preparation));sample.prepare=milliseconds(preparationStart);
        sample.shadowPrepare=preparation.shadowPreparationMs;sample.receiverPrepare=preparation.receiverPreparationMs;
        const auto resourceState=resources.stats();ready=preparation.ready&&preparation.refined&&!resourceState.queued&&!resourceState.running;
        for(const auto& view:lastPrepared->views)if(view.identity.purpose==RenderViewPurpose::Game)sample.gameModels=view.instances.size();else sample.sceneModels=view.instances.size();
        const auto submissionStart=Clock::now();renderer.renderFrame(lastPrepared);sample.submit=milliseconds(submissionStart);
        // The public owner exposes the last rendered world view, which is Game
        // here. Read before advance_frame can publish/reset another generation.
        const auto rayStats=renderer.shadowStats();sample.rayInstances=rayStats.rayInstances;
        sample.rayTriangles=rayStats.rayTriangles;sample.rayNodes=rayStats.rayNodes;
        prepared.endFrame();const auto retained=renderer.modelResidentBytes()+renderer.thumbnailResidentBytes();
        assets.trimTextureCache(retained>=resources.config().gpuRetentionBytes?0:resources.config().gpuRetentionBytes-retained);
        // Match the ordinary primary swapchain cadence on both APIs. There is
        // no manual native Flush, fence wait, readback or capture in this path.
        bgfx::touch(ToolsView);const auto advanceStart=Clock::now();frame=renderer.advance_frame();sample.advance=milliseconds(advanceStart);
        sample.wall=milliseconds(begin);sample.frame=frame;const auto* stats=bgfx::getStats();sample.query=stats->gpuFrameNum;
        sample.draws=stats->numDraw;sample.computes=stats->numCompute;sample.triangles=stats->numPrims[bgfx::Topology::TriList];sample.shadowDraws=renderer.shadowStats().drawCalls;
        if(stats->cpuTimerFreq>0)sample.backend=double(stats->cpuTimeEnd-stats->cpuTimeBegin)*1000./stats->cpuTimerFreq;
        shared::profiling::counter("Diagnostic submitted frame",frame);shared::profiling::frame_mark();
        return sample;
    };
    const auto deadline=Clock::now()+std::chrono::seconds{900};unsigned warm{},settled{};
    Clock::time_point readySince{};
    do {
        tick();++warm;settled=ready?settled+1:0;
        // Background O3 admission waits for 750 ms of quiet. A short run of
        // ready frames can precede that work, so require sustained readiness
        // across that admission interval before opening the timed window.
        if(!ready)readySince={};
        else if(readySince==Clock::time_point{})readySince=Clock::now();
        if(warm%120==0)std::cout<<"Warmup "<<warm<<" ready="<<settled<<" model="<<prepared.status()<<" SSR="<<renderer.ssrStatus()<<std::endl;
        if(!ready)std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }while((warm<warmupMinimum||settled<8||readySince==Clock::time_point{}||Clock::now()-readySince<std::chrono::seconds{2})&&Clock::now()<deadline);
    require(ready&&settled>=8&&readySince!=Clock::time_point{}&&Clock::now()-readySince>=std::chrono::seconds{2},"Saved-scene resources did not become ready: "+prepared.status()+"; "+assets.status());
    freezeDenseFrame=denseContactGeometry;
    const auto denseModelsBefore=denseModels.size(),densePrimitivesBefore=denseClearedPrimitives;
    require(!denseContactGeometry||denseModelsBefore>0,
        "Dense contact reference found no worker-prepared contact geometry to replace.");
    const auto compilerBefore=shaderCompilerMetrics();const auto resourcesBefore=resources.stats();
#if CRIMSON_ENABLE_PROFILING
    const auto profilerBefore=bgfx::getProfilerStats();
#endif
    const unsigned measurementFrames=sampleMinimum*2;const std::uint32_t first=frame+1,last=first+measurementFrames-1;
    shared::profiling::message("SSR diagnostic measurement begin");
    std::vector<Cpu> cpu;std::vector<Gpu> gpu;std::set<std::uint32_t> gpuFrames;
    std::map<std::uint16_t,std::map<std::uint32_t,double>> viewDurations;
    std::map<std::uint16_t,std::string> viewNames;Json rawViews=Json::array();
    const auto collect=[&]() {
        const auto* stats=bgfx::getStats();
        if(stats->gpuTimerFreq<=0)return;
        if(stats->gpuFrameNum>=first&&stats->gpuFrameNum<=last&&stats->gpuTimeEnd>stats->gpuTimeBegin&&gpuFrames.insert(stats->gpuFrameNum).second)
            gpu.push_back({stats->gpuFrameNum,double(stats->gpuTimeEnd-stats->gpuTimeBegin)*1000./stats->gpuTimerFreq});
        if(profile)for(unsigned i=0;i<stats->numViews;++i) {
            const auto& view=stats->viewStats[i];
            if(view.gpuFrameNum<first||view.gpuFrameNum>last||view.gpuTimeEnd<=view.gpuTimeBegin)continue;
            const double elapsed=double(view.gpuTimeEnd-view.gpuTimeBegin)*1000./stats->gpuTimerFreq;
            if(!viewDurations[view.view].try_emplace(view.gpuFrameNum,elapsed).second)continue;
            viewNames[view.view]=view.name;
            rawViews.push_back({{"publication_gpu_frame",stats->gpuFrameNum},{"gpu_frame",view.gpuFrameNum},{"view",view.view},{"name",view.name},{"elapsed_ms",elapsed}});
        }
    };
    const auto measureStart=Clock::now();
    for(unsigned i=0;i<measurementFrames;++i){cpu.push_back(tick());require(ready,"Resource preparation changed during timed workload.");collect();}
    const double measurementWall=milliseconds(measureStart);
    shared::profiling::message("SSR diagnostic measurement end");
    for(unsigned i=0;i<16;++i){tick();collect();}
#if CRIMSON_ENABLE_PROFILING
    const auto profilerAfter=bgfx::getProfilerStats();
    if(tracy) {
        require(shared::profiling::connected()&&profilerAfter.gpuZones>profilerBefore.gpuZones,
            "Tracy diagnostic did not record native GPU zones; enable profiling before renderer initialization and connect its collector.");
        require(profilerAfter.gpuZonesDropped==profilerBefore.gpuZonesDropped,"Tracy diagnostic dropped native GPU zones.");
    }
#endif
    const auto cpuValues=[&](auto member){std::vector<double> values;for(const auto& sample:cpu)values.push_back(static_cast<double>(sample.*member));return ssrDistribution(std::move(values));};
    std::vector<double> gpuValues;for(const auto& sample:gpu)gpuValues.push_back(sample.elapsed);
    const auto compilerAfter=shaderCompilerMetrics();const auto resourceState=resources.stats();const auto& authored=extracted.environment.environment.ssr;
    Json failureDiagnostics=Json::array();
    if(resourceState.failures) {
        // ResourceManager's counter includes historical failures and exposes no
        // generic record inspection. Preserve every currently available owned
        // asset/binding diagnostic without guessing which past job failed.
        for(const auto& model:extracted.models) {
            const auto diagnostic=assets.modelDiagnostic(model.assetId);
            if(!diagnostic.empty())failureDiagnostics.push_back({{"owner","model"},{"entity",model.id},{"asset",model.assetId},{"diagnostic",diagnostic}});
            for(const auto& material:model.materialOverrides)if(!material.empty()) {
                const auto text=assets.materialDiagnostic(material.id);
                if(!text.empty())failureDiagnostics.push_back({{"owner","material"},{"entity",model.id},{"asset",material.id},{"diagnostic",text}});
            }
        }
        if(lastPrepared)for(const auto& view:lastPrepared->views)for(const auto& instance:view.instances)if(instance.textures)
            for(std::size_t slot=0;slot<instance.textures->size();++slot) {
                const auto& diagnostic=(*instance.textures)[slot].diagnostic;
                if(!diagnostic.empty())failureDiagnostics.push_back({{"owner","prepared-binding"},{"entity",instance.entityId},
                    {"asset",instance.model.asset?instance.model.asset->id:std::string{}},{"slot",slot},{"diagnostic",diagnostic}});
            }
    }
    Json report{{"schema",1},{"scope","Static saved-scene render workload at authored pose/material time zero; no physics, Play simulation, editor UI, particle simulation, or asset IO in timed frames. Scene/Game dimensions are explicit diagnostic dimensions."},
        {"timing_notes","Whole-frame GPU timestamps measure elapsed queue interval, not active GPU execution; primary Present/queue backpressure remains in wall timing. Whole-frame and per-view GPU queries are independently filtered to the measured submitted frame range and deduplicated by their actual GPU frame IDs. Profiled attribution must not be mixed with unprofiled baseline cadence."},
        {"source_project",original.string()},{"scene_id",sceneId},{"backend",renderer.backendName()},{"vendor_id",bgfx::getCaps()->vendorId},{"device_id",bgfx::getCaps()->deviceId},
        {"compiler",config.compiler.string()},{"shader_sources",config.sourceDirectory.string()},{"shader_includes",config.includeDirectory.string()},
        {"compiler_fingerprint",fingerprintFiles({config.compiler})},{"source_family_fingerprint",fingerprintFiles({config.sourceDirectory/"varying.def.sc",config.sourceDirectory/"fs_model.sc"})},
        {"views",both?"scene_game":"game"},{"viewport",{width,height}},
        {"viewports",{{"scene",both?Json::array({sceneWidth,sceneHeight}):Json(nullptr)},{"game",Json::array({width,height})}}},
        {"contact_geometry",denseContactGeometry?"dense":"prepared"},{"contact_geometry_requested",contactGeometryRequested},
        {"active_contact_directional_lights",activeContactLights},
        {"contact_geometry_reference",{{"cloned_models",denseModelsBefore},{"cleared_primitive_proxies",densePrimitivesBefore},
            {"timed_model_clones",denseModels.size()-denseModelsBefore},{"timed_proxy_changes",denseClearedPrimitives-densePrimitivesBefore},
            {"scope",denseContactGeometry?"Test-only dense reference clears preparedShadowRays on cached immutable model copies; render triangles, paint bindings, materials, poses, world IDs and transforms are retained. Model/frame cloning and renderer preparation finish in warmup; timed frames reuse the complete pinned static frame.":"Production prepared contact geometry without diagnostic substitution."}}},
        {"profile_views",profile},{"warmup_frames",warm},{"measured_cpu_frames",measurementFrames},
        {"shader_cache",config.cacheDirectory.string()},{"contact_shader_override",contactShaderSources.empty()?Json(nullptr):Json(contactShaderSources.string())},
        {"ssr_trace_shader_override",traceShaderSources.empty()?Json(nullptr):Json(traceShaderSources.string())},{"tracy",tracy},
#if CRIMSON_ENABLE_PROFILING
        {"native_gpu_zones",profilerAfter.gpuZones-profilerBefore.gpuZones},
        {"native_gpu_zones_dropped",profilerAfter.gpuZonesDropped-profilerBefore.gpuZonesDropped},
#endif
        {"preferences",preferenceSnapshot},{"preferences_source",preferencePath.string()},
        {"anti_aliasing",antiAliasing==AntiAliasing::Smaa?"smaa":"off"},{"point_shadow_resolution",shadowResolution},
        {"authored_ssr",{{"enabled",authored.enabled},{"strength",authored.strength},{"quality",int(authored.quality)},{"max_distance",authored.maxDistance},{"max_roughness",authored.maxRoughness},{"depth_tolerance",authored.depthTolerance},{"bias",authored.bias}}},
        {"effective_ssr",!ssrOff&&effectiveSsr(authored)},
        {"scene_camera",{{"position",sceneCamera.position},{"target",sceneCamera.target},{"up",sceneCamera.up},{"fov",sceneCamera.fov},{"near",sceneCamera.nearPlane},{"far",sceneCamera.farPlane}}},
        {"game_camera",{{"position",gameCamera->position},{"target",gameCamera->target},{"up",gameCamera->up},{"fov",gameCamera->fov},{"near",gameCamera->nearPlane},{"far",gameCamera->farPlane}}},
        {"world",{{"models",extracted.models.size()},{"points",extracted.points.size()},{"directionals",extracted.directional.size()},{"particle_entities",extracted.particles.size()}}},
        {"cpu_wall_ms",cpuValues(&Cpu::wall)},{"cpu_update_ms",cpuValues(&Cpu::update)},{"cpu_prepare_ms",cpuValues(&Cpu::prepare)},
        {"cpu_shadow_prepare_ms",cpuValues(&Cpu::shadowPrepare)},{"cpu_receiver_prepare_ms",cpuValues(&Cpu::receiverPrepare)},
        {"cpu_render_submission_ms",cpuValues(&Cpu::submit)},{"cpu_bgfx_frame_ms",cpuValues(&Cpu::advance)},{"bgfx_backend_ms",cpuValues(&Cpu::backend)},
        {"draws",cpuValues(&Cpu::draws)},{"computes",cpuValues(&Cpu::computes)},{"triangles",cpuValues(&Cpu::triangles)},{"shadow_draws",cpuValues(&Cpu::shadowDraws)},
        {"scene_models",cpuValues(&Cpu::sceneModels)},{"game_models",cpuValues(&Cpu::gameModels)},{"gpu_elapsed_ms",ssrDistribution(std::move(gpuValues))},
        {"contact_payload",{{"scope","Public shadow stats sampled immediately after renderFrame, before advance_frame; last submitted world view is Game. Scene and Game share the same immutable world caster asset list; these are not independent per-view counters."},
            {"last_view","game"},{"shared_world_casters",both},{"ray_instances",cpuValues(&Cpu::rayInstances)},
            {"ray_triangles",cpuValues(&Cpu::rayTriangles)},{"ray_nodes",cpuValues(&Cpu::rayNodes)}}},
        {"throughput_fps",1000.*measurementFrames/measurementWall},{"wall_measurement_ms",measurementWall},
        {"gpu_query_availability",{{"minimum_unique_queries",sampleMinimum},{"observed_unique_queries",gpuFrames.size()},{"measured_cpu_frames",measurementFrames},{"first_measured_frame",first},{"last_measured_frame",last},{"frame_ids",gpuFrames},{"complete",gpuFrames.size()>=sampleMinimum}}},
        {"status",{{"model",prepared.status()},{"assets",assets.status()},{"renderer_model",renderer.modelStatus()},{"ssr",renderer.ssrStatus()},{"ssgi",renderer.ssgiStatus()},{"ssao",renderer.ssaoStatus()},{"shadow",renderer.shadowStatus()}}},
        {"resources",{{"queued",resourceState.queued},{"running",resourceState.running},{"failures",resourceState.failures},
            {"current_failure_diagnostics",failureDiagnostics},
            {"failure_diagnostic_scope","Available current model/material/prepared-binding diagnostics only; the lifetime failure counter cannot identify an already repaired historical job."},
            {"timed_loads",resourceState.loads-resourcesBefore.loads},{"timed_compiler_processes",compilerAfter.compilerInvocations-compilerBefore.compilerInvocations}}}};
    for(const auto& [view,values]:viewDurations) {
        std::vector<double> elapsed;std::vector<std::uint32_t> frameIds;
        for(const auto& [gpuFrame,value]:values){frameIds.push_back(gpuFrame);elapsed.push_back(value);}
        report["view_attribution"][std::to_string(view)]={{"name",viewNames[view]},{"elapsed_ms",ssrDistribution(std::move(elapsed))},{"frame_ids",frameIds}};
    }
    std::ofstream cpuFile(output/"cpu-frames.csv"),gpuFile(output/"gpu-frames.csv");require(bool(cpuFile)&&bool(gpuFile),"Cannot write diagnostic raw samples.");
    cpuFile<<"index,submitted_frame,published_gpu_frame,wall_ms,update_ms,prepare_ms,shadow_prepare_ms,receiver_prepare_ms,submission_ms,bgfx_frame_ms,backend_ms,draws,computes,triangles,shadow_draws,scene_models,game_models,last_view_ray_instances,last_view_ray_triangles,last_view_ray_nodes\n";
    for(std::size_t i=0;i<cpu.size();++i){const auto& s=cpu[i];cpuFile<<i<<','<<s.frame<<','<<s.query<<','<<s.wall<<','<<s.update<<','<<s.prepare<<','<<s.shadowPrepare<<','<<s.receiverPrepare<<','<<s.submit<<','<<s.advance<<','<<s.backend<<','<<s.draws<<','<<s.computes<<','<<s.triangles<<','<<s.shadowDraws<<','<<s.sceneModels<<','<<s.gameModels<<','<<s.rayInstances<<','<<s.rayTriangles<<','<<s.rayNodes<<'\n';}
    gpuFile<<"gpu_frame,elapsed_ms\n";for(const auto& s:gpu)gpuFile<<s.frame<<','<<s.elapsed<<'\n';
    {std::ofstream file(output/"report.json");file<<report.dump(2)<<'\n';require(bool(file),"Cannot write diagnostic summary.");}
    if(profile){std::ofstream file(output/"view-samples.json");file<<rawViews.dump(2)<<'\n';require(bool(file),"Cannot write per-view diagnostic samples.");}
    resources.stop();lastPrepared.reset();renderer.drainPending(frame);
    std::cout<<report.dump(2)<<std::endl;
    require(gpuFrames.size()>=sampleMinimum,"Too few unique measured GPU queries; actual frame IDs and samples retained in report.");
    require(!contactGeometryRequested||std::all_of(cpu.begin(),cpu.end(),[](const auto& sample) {
        return sample.rayInstances>0&&sample.rayTriangles>0&&sample.rayNodes>0;
    }),"The requested contact-geometry comparison did not render a nonempty contact payload in every measured frame.");
    require(compilerAfter.compilerInvocations==compilerBefore.compilerInvocations,"Shader compilation occurred during measured frames.");
    require(resourceState.loads==resourcesBefore.loads,"Asset loads occurred during measured frames.");
    require(resourceState.failures==0,"Resource failures invalidate the saved-scene measurement.");
    require(denseModels.size()==denseModelsBefore&&denseClearedPrimitives==densePrimitivesBefore,
        "Dense contact geometry changed during the measured workload.");
    return 0;
}
}
