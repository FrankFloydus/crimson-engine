// Explicit saved-level direct-light diagnostic. Only the disposable snapshot is opened.
#include "crimson-renderer/DirectionalShadowMath.h"
#include <optional>

namespace {
int lightLeakEditorDiagnostic(int argc,char** argv) {
    require(argc>=7,
        "Usage: forward_plus_room_benchmark --light-leak-project PROJECT SHADERC SOURCES INCLUDES OUTPUT [--shader-source-override PRIVATE_SOURCE_DIR | --contact-shader-override PRIVATE_SOURCE_DIR] [--shader-cache CACHE] [--shadow-resolution N] [--profile] [--authored-presentation] [--receiver-normal-only]");
    fs::path privateShaderSources,contactShaderSources,shaderCache;std::optional<unsigned> resolutionOverride;bool profile{},authoredPresentation{},normalCaptureOnly{};
    for(int index=7;index<argc;++index) {
        const std::string option=argv[index];
        if(option=="--profile"){require(!profile,"Duplicate --profile option.");profile=true;continue;}
        if(option=="--authored-presentation"){require(!authoredPresentation,"Duplicate --authored-presentation option.");authoredPresentation=true;continue;}
        if(option=="--receiver-normal-only"){require(!normalCaptureOnly,"Duplicate --receiver-normal-only option.");normalCaptureOnly=true;continue;}
        require(index+1<argc,"Missing light-leak diagnostic option value: "+option);
        const std::string value=argv[++index];
        if(option=="--contact-shader-override") {
            require(contactShaderSources.empty(),"Duplicate contact shader override.");contactShaderSources=fs::weakly_canonical(value);
            require(fs::is_directory(contactShaderSources),"Private contact shader directory does not exist.");
        } else if(option=="--raw-directional"||option=="--shader-source-override") {
            require(privateShaderSources.empty(),"Duplicate --raw-directional option.");
            privateShaderSources=fs::weakly_canonical(value);
            require(fs::is_directory(privateShaderSources),"Private raw shader directory does not exist.");
        } else if(option=="--shader-cache") {
            require(shaderCache.empty(),"Duplicate --shader-cache option.");shaderCache=fs::absolute(value);
        } else if(option=="--shadow-resolution") {
            require(!resolutionOverride.has_value(),"Duplicate --shadow-resolution option.");
            std::size_t consumed{};const auto count=std::stoul(value,&consumed);
            require(consumed==value.size()&&(count==256||count==512||count==1024),
                "--shadow-resolution must be 256, 512 or 1024, matching the native shadow atlas.");
            resolutionOverride=static_cast<unsigned>(count);
        } else throw std::runtime_error("Unknown light-leak diagnostic option: "+option);
    }
    const bool rawDirectional=!privateShaderSources.empty();
    require(!rawDirectional||contactShaderSources.empty(),"Material-family and contact-only attribution overrides cannot be combined.");
    require(!normalCaptureOnly||(!profile&&!authoredPresentation),"Receiver-normal capture cannot be combined with presentation or timing modes.");
    const fs::path original=fs::weakly_canonical(argv[2]),output=fs::absolute(argv[6]);Temporary temporary;
    std::cout<<"Staging light-leak snapshot at "<<temporary.root<<std::endl;
    copyGiProject(original,temporary.root);const auto project=temporary.root;initializeContentStorage(project);fs::create_directories(output);
    Json session;{std::ifstream file(project/".crimson/editor-session.json");if(!file)file.open(project/"assets/editor-session.json");
        require(bool(file),"No saved editor session in snapshot.");file>>session;}
    const auto sceneId=session.at("scene").get<std::string>();SceneDocument document(project);require(document.load(sceneId),document.status());
    const auto authored=extractScene(document.scene());const auto gameCamera=resolveGameCamera(document.scene());
    require(gameCamera.has_value(),"Saved scene has no active Game camera.");
    Camera sceneCamera=*gameCamera;
    if(session.contains("cameras")&&session.at("cameras").contains(sceneId)) {
        const auto& saved=session.at("cameras").at(sceneId);sceneCamera.position=saved.at("position").get<Vec3>();
        sceneCamera.target=saved.at("target").get<Vec3>();sceneCamera.up=saved.at("up").get<Vec3>();
        sceneCamera.fov=saved.value("fov",45.f);sceneCamera.nearPlane=saved.value("near",.01f);sceneCamera.farPlane=saved.value("far",2048.f);
    }
    Sdl sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Crimson saved-level direct light isolation",Width,Height,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
    require(bool(window),SDL_GetError());
    unsigned savedShadowResolution=1024;AntiAliasing savedAntiAliasing=AntiAliasing::Smaa;
    if(char* directory=SDL_GetPrefPath("CrimsonEngine","Editor")) {
        const fs::path path=fs::path(directory)/"preferences.json";SDL_free(directory);
        if(std::ifstream file(path);file){Json preferences;file>>preferences;savedShadowResolution=preferences.value("pointShadowResolution",1024u);
            savedAntiAliasing=preferences.value("antiAliasing",std::string{"smaa"})=="off"?AntiAliasing::Off:AntiAliasing::Smaa;}
    }
    const unsigned shadowResolution=resolutionOverride.value_or(savedShadowResolution);
    require(shadowResolution==256||shadowResolution==512||shadowResolution==1024,
        "Saved shadow resolution is unsupported; use --shadow-resolution 256, 512 or 1024.");
    // Readback destinations outlive renderer shutdown, including exceptions.
    std::vector<std::shared_ptr<std::vector<std::uint8_t>>> destinations;
    ResourceManager resources;Renderer renderer(native_renderer_context(window.get(),nullptr,selectedTestGraphicsBackend()));
    ShaderCompileConfig config{argv[3],argv[4],argv[5],resolveAssetPath(project,"assets/cache/shaders")};
    ShaderCompileConfig graphConfig=config;
    if(!contactShaderSources.empty()) {
        require(contactShaderSources!=fs::weakly_canonical(config.sourceDirectory)&&contactShaderSources!=fs::weakly_canonical("third_party/crimson-renderer/src/shaders"),
            "Contact attribution requires a private shader source directory.");
        ShaderCompileConfig contactConfig{config.compiler,contactShaderSources,config.includeDirectory,
            shaderCache.empty()?project/"contact-attribution":shaderCache/"contact"};
        const std::array pairs{RendererShaderPair{RendererProgram::ShadowContactTrace,{},compileShader(contactConfig,"cs_shadow_contact_trace").bytes},
            RendererShaderPair{RendererProgram::ShadowContactPublish,{},compileShader(contactConfig,"cs_shadow_contact_publish").bytes}};
        require(renderer.reloadPrograms(pairs),renderer.shaderStatus());
    }
    if(rawDirectional) {
        const auto& privateSources=privateShaderSources;
        require(privateSources!=fs::weakly_canonical(config.sourceDirectory)&&privateSources!=fs::weakly_canonical("third_party/crimson-renderer/src/shaders"),
            "--raw-directional requires a private supplied shader source directory.");
        ShaderCompileConfig modelConfig{config.compiler,privateSources,config.includeDirectory,shaderCache.empty()?project/"raw-model":shaderCache/"fixed"};
        const auto fragment=compileShader(modelConfig,"fs_model").bytes;
        std::vector pairs{RendererShaderPair{RendererProgram::Model,compileShader(modelConfig,"vs_model").bytes,fragment},
            RendererShaderPair{RendererProgram::InstancedModel,compileShader(modelConfig,"vs_model_instanced").bytes,fragment}};
        // A private family may also attribute the once-per-view shadow work.
        // Reload both stages atomically with the surfaces; the ordinary path
        // continues to use the executable's validated embedded programs.
        const bool contactTrace=fs::is_regular_file(privateSources/"cs_shadow_contact_trace.sc");
        const bool contactPublish=fs::is_regular_file(privateSources/"cs_shadow_contact_publish.sc");
        require(contactTrace==contactPublish,"Private contact tracing and publication must be supplied together.");
        if(contactTrace) {
            pairs.push_back({RendererProgram::ShadowContactTrace,{},compileShader(modelConfig,"cs_shadow_contact_trace").bytes});
            pairs.push_back({RendererProgram::ShadowContactPublish,{},compileShader(modelConfig,"cs_shadow_contact_publish").bytes});
        }
        require(renderer.reloadPrograms(pairs),renderer.shaderStatus());
        // The saved room uses generated packets too. Give their private source
        // family an honest identity and isolated cache; never reuse PCF programs.
        graphConfig.sourceDirectory=privateSources;graphConfig.cacheDirectory=shaderCache.empty()?project/"raw-graphs":shaderCache/"graphs";
    }
    MaterialGraphPrograms programs(resources,graphConfig);AssetLibrary assets(project,&resources);assets.setMaterialGraphPrograms(&programs);
    EditorResources prepared(assets,renderer,resources,project);
    const auto map=authored.environment.environment.source==EnvironmentSource::Hdri?
        assets.references().resolveEnvironment(authored.environment.environment.map):std::string{};
    require(renderer.publishEnvironment(*prepareEnvironmentCpu(project,map)),renderer.environmentStatus());
    renderer.resizeViewport(Width,Height);renderer.resizeGameViewport(Width,Height);renderer.setGridVisible(false);
    renderer.setDebugView(RenderViewPurpose::Scene,RenderDebugView::DirectLighting);
    renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::DirectLighting);
    bgfx::reset(Width,Height,BGFX_RESET_NONE);
    if(profile)bgfx::setDebug(BGFX_DEBUG_PROFILER);
    TestGpuContext context;
    struct Phase {const char* name;bool directionals,points,zeroNormal,zeroDepth,unshadowed{},authored{};};
    std::vector phases{Phase{"both",true,true,false,false},Phase{"directional-only",true,false,false,false},
        Phase{"points-only",false,true,false,false},Phase{"normal-bias-zero",true,true,true,false},Phase{"both-biases-zero",true,true,true,true},
        Phase{"directional-unshadowed",true,false,false,false,true}};
    if(authoredPresentation)phases.push_back({"authored-presentation",true,true,false,false,false,true});
    if(normalCaptureOnly)phases={Phase{"receiver-normal",true,true,false,false}};
    std::uint32_t frame{};unsigned phaseIndex{};bool ready{};EditorRenderFramePreparationStats preparation;
    std::shared_ptr<const PreparedRenderFrame> last;
    const auto tick=[&] {
        resources.pump();assets.updateResources(frame);renderer.advanceStreaming();prepared.beginFrame();
        const auto& phase=phases[phaseIndex];EditorRenderFrameInput input;input.extracted=authored;if(!phase.authored)input.extracted.particles.clear();
        for(auto& light:input.extracted.directional) {
            light.enabled=light.enabled&&phase.directionals;
            if(phase.unshadowed)light.castShadows=false;
            if(phase.zeroNormal)light.shadowNormalBias=0;if(phase.zeroDepth)light.shadowDepthBias=0;
        }
        for(auto& light:input.extracted.points) {
            light.enabled=light.enabled&&phase.points;
            if(phase.unshadowed)light.castShadows=false;
            if(phase.zeroNormal)light.shadowNormalBias=0;if(phase.zeroDepth)light.shadowDepthBias=0;
        }
        input.settings.environment=authored.environment.environment;auto& environment=input.settings.environment;
        environment.map=assets.references().resolveEnvironment(environment.map);
        if(!phase.authored){environment.enabled=false;
        environment.ambientLight.source=AmbientLightSource::Disabled;environment.ambientLight.energy=0;environment.ambientLight.skyContribution=0;
        environment.background.show=false;environment.background.overrideColor=true;environment.background.color={};
        environment.fog.enabled=environment.glow.enabled=environment.ssgi.enabled=environment.ssao.enabled=environment.ssr.enabled=false;
        environment.pixelization.enabled=environment.dither.enabled=environment.posterization.enabled=environment.vignette.enabled=false;
        environment.debanding=false;environment.toneMapping=ToneMapping::Linear;}
        input.settings.exposure=phase.authored?authored.environment.exposure:1;input.settings.proceduralSun=authored.proceduralSun;
        input.settings.antiAliasing=phase.authored?savedAntiAliasing:AntiAliasing::Off;
        const auto debug=normalCaptureOnly?RenderDebugView::Normal:phase.authored?RenderDebugView::Lit:RenderDebugView::DirectLighting;
        renderer.setDebugView(RenderViewPurpose::Scene,debug);renderer.setDebugView(RenderViewPurpose::Game,debug);
        input.sceneCamera=sceneCamera;input.gameCamera=gameCamera;input.sceneWidth=input.gameWidth=Width;input.sceneHeight=input.gameHeight=Height;
        input.sceneAspect=Aspect;input.sceneVisible=input.gameVisible=true;input.worldId=phaseIndex+1;input.giWorldId=phaseIndex+1;
        input.environmentId=1;input.gameCameraId=document.scene().properties().activeCamera;input.shadowResolution=static_cast<std::uint16_t>(shadowResolution);
        input.materialContext={};preparation={};last=prepareEditorRenderFrame(std::move(input),assets,prepared,&preparation);
        const auto state=resources.stats();ready=preparation.ready&&preparation.refined&&!state.queued&&!state.running&&last->views.size()==2;
        renderer.renderFrame(last);prepared.endFrame();bgfx::touch(ToolsView);frame=renderer.advance_frame();context.flush();
    };
    double first_ready_ms{},settle_ms{};
    const auto settle=[&](unsigned minimum) {
        const auto warmupStart=Clock::now();
        first_ready_ms=-1.0;
        const unsigned limitSeconds=rawDirectional?900u:240u;
        const auto deadline=warmupStart+std::chrono::seconds{limitSeconds};unsigned warm{},stable{};
        do {tick();++warm;stable=ready?stable+1:0;
            if(ready&&first_ready_ms<0.0)first_ready_ms=milliseconds(warmupStart);
            if(warm%120==0)std::cout<<phases[phaseIndex].name<<" warm="<<warm<<" ready="<<stable<<" model="<<prepared.status()
                <<" queued="<<resources.stats().queued<<" running="<<resources.stats().running
                <<" compiler_processes="<<shaderCompilerMetrics().compilerInvocations
                <<" elapsed_s="<<std::chrono::duration_cast<std::chrono::seconds>(Clock::now()-warmupStart).count()
                <<" limit_s="<<limitSeconds<<std::endl;
            if(!ready)std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }while((warm<minimum||stable<8)&&Clock::now()<deadline);
        settle_ms=milliseconds(warmupStart);
        require(ready&&stable>=8,"Light-leak preparation did not settle within "+std::to_string(limitSeconds)+"s: "+prepared.status()+"; "+assets.status());return warm;
    };
    const auto cameraJson=[](const Camera& camera){return Json{{"position",camera.position},{"target",camera.target},{"up",camera.up},
        {"fov",camera.fov},{"near",camera.nearPlane},{"far",camera.farPlane}};};
    Json report{{"schema",1},{"scope","Disposable saved-level snapshot; production preparation; frozen authored pose/material time zero; actual saved Scene/Game cameras; DirectLighting debug, Linear/exposure1, no optional effects/ambient/environment illumination/particles."},
        {"source_project",original.string()},{"scene",sceneId},{"backend",renderer.backendName()},{"shader_sources",config.sourceDirectory.string()},
        {"directional_unshadowed_reference","Same direct-only presentation/cameras/materials as directional-only, with directional shadows disabled; use per-pixel linear ratios for normalized visibility."},
        {"authored_presentation_requested",authoredPresentation},{"authored_presentation_note","Optional final Lit capture/profile restores saved Environment/exposure/lights/particles and saved AA preference at frozen authored time zero; the both phase remains a direct-only workload."},
        {"receiver_normal_only",normalCaptureOnly},
        {"raw_directional_override",rawDirectional},{"raw_override_scope",rawDirectional?
            "Private supplied source family for fixed Model/InstancedModel and all generated material programs; private generated cache/source identity.":
            "Canonical fixed and generated source family/cache identity."},
        {"graph_shader_sources",graphConfig.sourceDirectory.string()},{"graph_cache_directory",graphConfig.cacheDirectory.string()},
        {"contact_shader_override",contactShaderSources.string()},{"contact_shader_override_scope",contactShaderSources.empty()?"Canonical embedded contact programs.":"Private trace/publication programs only; fixed and generated material source/cache identities are unchanged."},
        {"cascade_metadata_note","Camera/light texelWorld, radius and splits are exact; reconstructed caster bounds omit pose/displacement expansion, so reported upstream near/far can differ from native fits."},
        {"dimensions",{Width,Height}},{"aspect",Aspect},{"shadow_resolution",shadowResolution},
        {"saved_shadow_resolution",savedShadowResolution},{"shadow_resolution_override",resolutionOverride?Json(*resolutionOverride):Json(nullptr)},
        {"shadow_resolution_source",resolutionOverride?"cli_override":"saved_preference_or_default"},
        {"scene_camera",cameraJson(sceneCamera)},{"game_camera",cameraJson(*gameCamera)},{"models",authored.models.size()}};
    report["model_entities"]=Json::array();
    if(rawDirectional)report["raw_model_shader_sources"]=privateShaderSources.string();
    for(const auto& entity:document.scene().records())if(entity.model)report["model_entities"].push_back({{"entity",entity.id},{"name",entity.name}});
    const auto capture=[&](unsigned view,const std::string& name) {
        const auto staging=bgfx::createTexture2D(Width,Height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK);
        require(bgfx::isValid(staging),"Cannot allocate light-leak readback.");
        auto pixels=std::make_shared<std::vector<std::uint8_t>>(Width*Height*4);destinations.push_back(pixels);
        bgfx::blit(DiagnosticReadbackView,staging,0,0,view?renderer.gameViewportTexture():renderer.viewportTexture(),0,0,Width,Height);
        const auto target=bgfx::readTexture(staging,pixels->data());require(target!=UINT32_MAX,"Light-leak readback is unsupported.");
        unsigned waits{};while(static_cast<std::int32_t>(frame-target)<0&&waits++<120)tick();
        if(static_cast<std::int32_t>(frame-target)<0){renderer.drainPending(frame);bgfx::destroy(staging);throw std::runtime_error("Light-leak readback timed out.");}
        bgfx::destroy(staging);std::vector<std::uint8_t> png;require(lodepng::encode(png,*pixels,Width,Height)==0,"Light-leak PNG encoding failed.");
        std::ofstream file(output/name,std::ios::binary);file.write(reinterpret_cast<const char*>(png.data()),std::streamsize(png.size()));require(bool(file),"Cannot save light-leak PNG.");
    };
    for(phaseIndex=0;phaseIndex<phases.size();++phaseIndex) {
        const auto& phase=phases[phaseIndex];auto& result=report["phases"][phase.name];result["warmup_frames"]=settle(!phaseIndex||phase.authored?60:8);
        result["preparation_first_complete_frame_ms"]=first_ready_ms;result["settle_total_ms"]=settle_ms;
        result["preparation_timing_scope"]="From first preparation/render tick to all two-view resources ready/refined and workers idle; includes rendering cadence, excludes snapshot copy, project/library construction and application boot.";
        result["presentation"]=normalCaptureOnly?"receiver-world-normal":phase.authored?"saved-authored-lit":"isolated-direct-linear";
        result["directional_shadows_disabled"]=phase.unshadowed;
        if(profile&&(phaseIndex==0||phase.authored)) {
            for(unsigned index=0;index<60;++index)tick();
            constexpr unsigned Samples=240;const std::uint32_t first=frame+1,lastFrame=first+Samples-1;
            std::map<std::uint32_t,double> queries;
            std::map<std::uint16_t,std::map<std::uint32_t,double>> viewQueries;
            std::map<std::uint16_t,std::string> viewNames;
            const auto started=Clock::now();
            const auto collect=[&] {
                const auto* stats=bgfx::getStats();
                if(stats->gpuFrameNum<first||stats->gpuFrameNum>lastFrame||stats->gpuTimerFreq<=0||stats->gpuTimeEnd<=stats->gpuTimeBegin)return;
                queries.try_emplace(stats->gpuFrameNum,double(stats->gpuTimeEnd-stats->gpuTimeBegin)*1000./stats->gpuTimerFreq);
                for(unsigned index=0;index<stats->numViews;++index) {
                    const auto& view=stats->viewStats[index];
                    if(view.gpuFrameNum<first||view.gpuFrameNum>lastFrame||view.gpuTimeEnd<=view.gpuTimeBegin)continue;
                    viewQueries[view.view].try_emplace(view.gpuFrameNum,double(view.gpuTimeEnd-view.gpuTimeBegin)*1000./stats->gpuTimerFreq);
                    viewNames[view.view]=view.name;
                }
            };
            // No readback, explicit fence or queue drain inside the timed span.
            for(unsigned index=0;index<Samples;++index){tick();collect();}
            const double elapsed=milliseconds(started);
            for(unsigned index=0;index<16;++index){tick();collect();}
            std::vector<double> sorted;for(const auto& [id,value]:queries)sorted.push_back(value);std::sort(sorted.begin(),sorted.end());
            const auto percentile=[&](double fraction) {
                if(sorted.empty())return 0.;const double index=(sorted.size()-1)*fraction;const auto lower=std::size_t(index);
                return sorted[lower]+(sorted[std::min(lower+1,sorted.size()-1)]-sorted[lower])*(index-lower);
            };
            require(queries.size()>=Samples/2,"Saved-level profile did not publish enough unique in-window GPU queries.");
            result["profile"]={{"scope",phase.authored?
                "Both saved Lit views with authored Environment/exposure/lights/AA and particles at frozen time zero; whole-frame elapsed GPU timestamps, not active GPU time or isolated shadow duration.":
                "Both saved direct-only views, production preparation, render-only cadence; whole-frame elapsed GPU timestamps, not active GPU time or isolated shadow duration."},
                {"samples_submitted",Samples},{"unique_gpu_samples",queries.size()},{"first_submitted_frame",first},{"last_submitted_frame",lastFrame},
                {"wall_ms_mean",elapsed/Samples},{"gpu_p50_ms",percentile(.5)},{"gpu_p95_ms",percentile(.95)}};
            result["profile"]["gpu_samples_ms"]=Json::array();
            for(const auto& [id,value]:queries)result["profile"]["gpu_samples_ms"].push_back({{"frame",id},{"ms",value}});
            result["profile"]["views"]=Json::array();
            for(const auto& [id,values]:viewQueries) {
                std::vector<double> times;for(const auto& [frameId,value]:values)times.push_back(value);
                std::sort(times.begin(),times.end());
                const auto quantile=[&](double fraction) {
                    const double index=(times.size()-1)*fraction;const auto lower=std::size_t(index);
                    return times[lower]+(times[std::min(lower+1,times.size()-1)]-times[lower])*(index-lower);
                };
                result["profile"]["views"].push_back({{"id",id},{"name",viewNames[id]},{"samples",times.size()},
                    {"gpu_p50_ms",quantile(.5)},{"gpu_p95_ms",quantile(.95)}});
            }
        }
        result["directional_lights"]=Json::array();result["point_lights"]=Json::array();
        for(const auto& view:last->views) {
            const auto purpose=view.identity.purpose;const char* name=purpose==RenderViewPurpose::Game?"game":"scene";
            auto& coverage=result["shader_coverage"][name];coverage["instances"]=Json::array();
            const auto& stats=renderer.meshInstancingStats(purpose);
            coverage["actual_instancing_stats"]={{"eligible_primitives",stats.eligibleInstances},{"instanced_instances",stats.instancedInstances},
                {"instanced_draws",stats.instancedDraws},{"fallback_draws",stats.fallbackDraws}};
            for(const auto& instance:view.instances) {
                const auto entity=document.scene().find(instance.entityId);
                Json item{{"entity",instance.entityId},{"name",entity?entity->name:std::string{}},{"asset",instance.model.asset?instance.model.asset->id:std::string{}},
                    {"posed",bool(instance.pose)},{"opacity",instance.opacity},{"blend",int(instance.blend)},{"slots",Json::array()}};
                const auto count=instance.materials?instance.materials->size():instance.textures?instance.textures->size():0;
                unsigned graphSlots{},fallbackSlots{};
                for(std::size_t slot=0;slot<count;++slot) {
                    const auto* binding=instance.textures&&slot<instance.textures->size()?&(*instance.textures)[slot]:nullptr;
                    const bool graph=binding&&bool(binding->graph);graph?++graphSlots:++fallbackSlots;
                    item["slots"].push_back({{"slot",slot},{"prepared_graph",graph},{"graph_texture_sources",graph?binding->graph->sources.size():0},
                        {"raw_override_applies",rawDirectional},{"ready",!binding||(binding->sourceReady&&!binding->graphPending&&binding->drawable())},
                        {"shader_path",graph?(rawDirectional?"private-generated-override":"canonical-generated"):rawDirectional?"private-fixed-override":"canonical-fixed"}});
                }
                item["graph_slots"]=graphSlots;item["fallback_slots"]=fallbackSlots;
                item["instancing_note"]="Per-instance grouping is not exposed; actual per-view counters are above. Both fixed model shader variants are overridden in raw mode.";
                coverage["instances"].push_back(std::move(item));
            }
        }
        std::vector<Aabb> bounds;for(const auto& instance:last->world->shadowInstances)if(instance.castShadows&&instance.model.asset)
            bounds.push_back(math::transformBounds(instance.model.asset->bounds,instance.model.transform));
        for(const auto& light:last->world->directional) {
            Json item{{"entity",light.entityId},{"enabled",light.enabled},{"direction",light.direction},{"intensity",light.intensity},{"shadows",light.castShadows},
                {"depth_bias",light.shadowDepthBias},{"normal_bias",light.shadowNormalBias},{"distance",light.shadowDistance},{"angular_diameter",light.shadowAngularDiameter}};
            for(const auto& view:last->views) {
                const auto fit=directional_shadow::build(light,{view.settings.camera,Aspect},shadowResolution,bounds);
                auto& projections=item["cameras"][view.identity.purpose==RenderViewPurpose::Game?"game":"scene"];
                projections={{"valid",fit.valid},{"splits",fit.splits},{"cascades",Json::array()}};
                for(unsigned index=0;index<fit.projectionCount;++index){const auto& cascade=fit.values[index];
                    projections["cascades"].push_back({{"index",index},{"texel_world",cascade.texelWorld},{"radius",cascade.radius},
                        {"near_depth",cascade.nearDepth},{"far_depth",cascade.farDepth},{"split_near",cascade.splitNear},{"split_far",cascade.splitFar}});}
            }
            result["directional_lights"].push_back(std::move(item));
        }
        for(const auto& light:last->world->points)result["point_lights"].push_back({{"entity",light.entityId},{"enabled",light.enabled},{"position",light.position},
            {"intensity",light.intensity},{"radius",light.radius},{"shadows",light.castShadows},{"normal_bias",light.shadowNormalBias},{"depth_bias",light.shadowDepthBias}});
        for(unsigned view=0;view<2;++view){tick();capture(view,std::string(phase.name)+"-"+(view?"game":"scene")+".png");}
        const auto state=resources.stats();result["readiness"]={{"ready",preparation.ready},{"refined",preparation.refined},{"queued",state.queued},{"running",state.running},
            {"failures",state.failures},{"model_status",prepared.status()},{"shadow_status",renderer.shadowStatus()},{"compiler_processes",shaderCompilerMetrics().compilerInvocations}};
        const auto& shadowState=renderer.shadowStats();
        const bool atlasRequired=std::any_of(last->world->directional.begin(),last->world->directional.end(),[](const auto& light){return light.enabled&&light.castShadows&&light.intensity>0;})||
            std::any_of(last->world->points.begin(),last->world->points.end(),[](const auto& light){return light.enabled&&light.castShadows&&light.intensity>0;});
        result["shadow_atlas"]={{"requested_resolution",shadowResolution},{"actual_resolution",shadowState.resolution},
            {"required",atlasRequired},{"allocated_bytes",shadowState.allocatedBytes},{"status",renderer.shadowStatus()}};
        result["contact_geometry"]={{"instances",shadowState.rayInstances},{"triangles",shadowState.rayTriangles},
            {"tlas_nodes",shadowState.rayNodes},{"cpu_bytes",shadowState.rayCpuBytes},{"cache_hits",shadowState.rayCacheHits},
            {"builds",shadowState.rayBuilds},{"uploads",shadowState.rayUploads}};
        result["prepared_casters"]=last->world->shadowInstances.size();result["scene_receivers"]=last->views[0].instances.size();result["game_receivers"]=last->views[1].instances.size();
        {std::ofstream file(output/"report.json");file<<report.dump(2)<<'\n';require(bool(file),"Cannot write light-leak report.");}
        require(!atlasRequired||shadowState.resolution==shadowResolution,"Native shadow atlas resolution differs from requested diagnostic resolution; report retained.");
        std::cout<<"Captured "<<phase.name<<" casters="<<last->world->shadowInstances.size()<<" ready="<<ready<<std::endl;
    }
    resources.stop();last.reset();renderer.drainPending(frame);std::cout<<report.dump(2)<<std::endl;return 0;
}
}
