// Explicit diagnostic, not a routine CTest. Copies managed sources into a
// disposable project, imports a real native Quader room, and exercises the
// production resource preparation and D3D11 Scene/Game submission paths. The
// default 30-frame phases preserve compatibility with the retained pre-Forward+
// probe. For durable profiling use --warmup 300 --samples 2000 and --output.
#include "assets/AssetLibrary.h"
#include "assets/AtomicFile.h"
#include "crimson-renderer/Environment.h"
#include "editor/EditorResources.h"
#include "editor/EditorRenderFrame.h"
#include "editor/QuaderSceneIntegration.h"
#include "math/Json.h"
#include "quader/QuaderImport.h"
#include "scene/SceneExtraction.h"
#include <SDL3/SDL.h>
#include <bgfx/platform.h>
#include <d3d11.h>
#include <nlohmann/json.hpp>
#include <lodepng.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <numeric>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
using Json=nlohmann::json;
constexpr unsigned Width=909,Height=684;
constexpr float Aspect=float(Width)/Height;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
double milliseconds(Clock::time_point begin){return std::chrono::duration<double,std::milli>(Clock::now()-begin).count();}
struct Temporary {
    fs::path root=fs::temp_directory_path()/("crimson-shadow-probe-"+std::to_string(Clock::now().time_since_epoch().count()));
    Temporary(){fs::create_directories(root/"assets");}
    ~Temporary(){if(std::uncaught_exceptions()){std::cerr<<"Retained failed diagnostic project: "<<root<<std::endl;return;}std::error_code error;const auto target=fs::weakly_canonical(root,error),parent=fs::weakly_canonical(fs::temp_directory_path(),error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("crimson-shadow-probe-"))fs::remove_all(target,error);}
};
struct Sdl {Sdl(){require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~Sdl(){SDL_Quit();}};
struct DeviceContext {ID3D11DeviceContext* value{};~DeviceContext(){if(value)value->Release();}};
struct Sample {
    double update{},prepareShadows{},prepareReceivers{},submit{},frame{},gpu{},backend{},gpuPoints{},gpuDirectionals{},gpuScene{},gpuGame{},gpuPost{},gpuDepth{},gpuAmbient{};
    std::uint64_t draws{},shadowDraws{},faces{},cascades{},pointHits{},directionalHits{},preparedCasters{},sceneModels{},gameModels{};
    std::uint64_t pointDeferred{},pointCriticalDeferred{},directionalWorldProjections{},characterProjections{},focusCasters{};
    std::uint32_t submittedFrame{},gpuFrame{};
};
struct Options {
    bool legacy{},savedWorld{},cold{},capture{},ambient{},presentedPlayer{},rigOff{},keyLight{};
    unsigned warmup{30},samples{30};fs::path output;
};
template<class Record> void disablePlayerLighting(Record& record) {
    if(record.lightRig)record.lightRig->enabled=false;
    if(record.darknessReveal)record.darknessReveal->enabled=false;
}
Json playerLightingMetadata(const ExtractedScene& world) {
    return {{"point_lights",world.points.size()},{"actor_rigs",world.actorLighting.size()},
        {"reveal_sources",world.reveals.size()}};
}
Json summarize(const std::vector<Sample>& values) {
    const auto mean=[&](auto member){double sum{};for(const auto& value:values)sum+=static_cast<double>(value.*member);return sum/values.size();};
    std::vector<double> frames;for(const auto& value:values)frames.push_back(value.frame);std::sort(frames.begin(),frames.end());
    return {{"samples",values.size()},{"update_ms",mean(&Sample::update)},{"shadow_preparation_ms",mean(&Sample::prepareShadows)},
        {"receiver_preparation_ms",mean(&Sample::prepareReceivers)},{"render_submission_ms",mean(&Sample::submit)},
        {"wall_frame_ms",mean(&Sample::frame)},{"wall_p95_ms",frames[frames.size()*95/100]},
        {"gpu_ms",mean(&Sample::gpu)},{"backend_submit_ms",mean(&Sample::backend)},
        {"gpu_point_views_ms",mean(&Sample::gpuPoints)},{"gpu_directional_views_ms",mean(&Sample::gpuDirectionals)},
        {"gpu_depth_views_ms",mean(&Sample::gpuDepth)},{"gpu_ambient_views_ms",mean(&Sample::gpuAmbient)},
        {"gpu_scene_view_ms",mean(&Sample::gpuScene)},{"gpu_game_view_ms",mean(&Sample::gpuGame)},{"gpu_post_views_ms",mean(&Sample::gpuPost)},
        {"draws",mean(&Sample::draws)},{"shadow_draws",mean(&Sample::shadowDraws)},
        {"rendered_point_faces",mean(&Sample::faces)},{"rendered_cascades",mean(&Sample::cascades)},
        {"point_cache_hits",mean(&Sample::pointHits)},{"directional_cache_hits",mean(&Sample::directionalHits)},
        {"point_deferred",mean(&Sample::pointDeferred)},{"point_critical_deferred",mean(&Sample::pointCriticalDeferred)},
        {"directional_world_projections",mean(&Sample::directionalWorldProjections)},
        {"character_projections",mean(&Sample::characterProjections)},{"focus_casters",mean(&Sample::focusCasters)},
        {"prepared_casters",mean(&Sample::preparedCasters)},{"scene_models",mean(&Sample::sceneModels)},{"game_models",mean(&Sample::gameModels)}};
}
Json exercise(SDL_Window* window,const fs::path& project,const fs::path& nativeProject,const std::string& nativeScene,const ShaderCompileConfig& config,const Options& parameters) {
    const auto [legacy,savedWorld,cold,captureImages,ambientEnabled,presentedPlayer,rigOff,keyLight,warmupCount,sampleCount,output]=parameters;
    std::ofstream raw;if(!output.empty()){fs::create_directories(output);raw.open(output/"room-frames.csv");require(bool(raw),"Cannot open room benchmark raw output.");
        raw<<"phase,index,submitted_frame,gpu_frame,update_ms,shadow_preparation_ms,receiver_preparation_ms,render_submission_ms,wall_frame_ms,gpu_ms,backend_ms,draws,shadow_draws,faces,cascades,point_hits,directional_hits,scene_models,game_models,point_deferred,point_critical_deferred,directional_world_projections,character_projections,focus_casters,gpu_depth_views_ms,gpu_ambient_views_ms,environment_bytes\n";}
    std::cout<<"reading native source"<<std::endl;
    const auto source=readQuaderScene(nativeProject,nativeScene);
    QuaderPrepareOptions options;options.projectRoot=project;const auto imported=savedWorld?QuaderImportResult{}:prepareQuaderImport(*source,options);
    if(!savedWorld)commitAssetTransaction(project,imported.files);
    std::cout<<"native source staged"<<std::endl;
    SceneDocument document(project);Json session;{std::ifstream input(project/"assets/editor-session.json");input>>session;}
    const auto sceneId=session.at("scene").get<std::string>();require(document.load(sceneId),document.status());
    if(rigOff||keyLight)require(document.edit("Diagnostic lighting contribution",[&](auto& records,auto&) {
        if(rigOff)for(auto& record:records)disablePlayerLighting(record);
        if(keyLight) {
            EntityRecord key;key.id=UINT64_MAX;
            while(std::any_of(records.begin(),records.end(),[&](const auto& record){return record.id==key.id;}))--key.id;
            key.name="Diagnostic World Key";key.order=static_cast<std::uint32_t>(records.size());
            key.local=math::inverse(math::lookAt({},{-.4f,-1,.25f},{0,1,0})).value();
            key.directional=SceneDirectionalLight{};key.directional->intensity=2;
            key.directional->castShadows=true;key.directional->roles.ambientReveal=false;
            records.push_back(std::move(key));
        }
    }),document.status());
    // Opt-in gameplay workload uses the production presented scene/extraction
    // path. The default retains the exact historical benchmark workload.
    std::array<std::unique_ptr<Scene>,2> presentations;
    const auto snapshot=[&](unsigned index) {
        if(presentedPlayer){presentations[index]=std::make_unique<Scene>();
            presentations[index]->apply(document.scene().records(),document.scene().properties());}
    };
    if(savedWorld)snapshot(1);
    const auto savedCombined=extractScene(document.scene());
    // The user may have saved an earlier import in Main. Build the baseline
    // from its authored non-Quader records, then import this room exactly once.
    require(document.edit("Diagnostic baseline",[](auto& records,auto&) {
        std::erase_if(records,[](const auto& record){return record.quader.has_value()||record.quaderGenerated.has_value();});
    }),document.status());
    Camera sceneCamera;
    if(session.contains("cameras")&&session["cameras"].contains(sceneId)) {
        const auto& value=session["cameras"][sceneId];sceneCamera.position=value.at("position").get<Vec3>();sceneCamera.target=value.at("target").get<Vec3>();
        sceneCamera.up=value.at("up").get<Vec3>();sceneCamera.fov=value.value("fov",45.f);sceneCamera.nearPlane=value.value("near",.01f);sceneCamera.farPlane=value.value("far",2048.f);
    }
    const auto baseline=extractScene(document.scene());
    snapshot(0);
    if(!savedWorld){const auto owner=document.createQuader();QuaderLink link;link.projectPath=nativeProject.generic_string();link.sceneId=nativeScene;
        require(applyQuaderSceneImport(document,owner,link,imported),document.status());}
    if(!savedWorld)snapshot(1);
    const auto combined=savedWorld?savedCombined:extractScene(document.scene());const auto gameCamera=resolveGameCamera(document.scene()).value_or(sceneCamera);
    const auto movingEntities=document.scene().subtree(document.scene().properties().playerCharacter);
    std::array<Mat4,2> originalPlayerWorld,parentInverse;
    if(presentedPlayer)for(unsigned index=0;index<presentations.size();++index) {
        const auto& scene=*presentations[index];const auto player=scene.find(scene.properties().playerCharacter);
        require(player&&player->controller.has_value(),"Presented-player workload requires a designated Character.");
        originalPlayerWorld[index]=scene.worldTransform(player->id).value();
        const auto parent=player->parent?scene.worldTransform(player->parent):std::optional<Mat4>{Mat4{}};
        require(parent.has_value(),"Presented player has a missing parent transform.");
        const auto inverse=math::inverse(*parent);require(inverse.has_value(),"Presented player parent transform is singular.");
        parentInverse[index]=*inverse;
    }
    // Readback destinations outlive Renderer even if a capture throws.
    std::vector<std::shared_ptr<std::vector<std::uint8_t>>> captureDestinations;
    ResourceManager resources;Renderer renderer(window);MaterialGraphPrograms programs(resources,config);AssetLibrary assets(project,&resources);
    std::cout<<"GPU/resource owners initialized"<<std::endl;
    assets.setMaterialGraphPrograms(&programs);assets.publishManagedModels(imported.preparedModels);
    EditorResources prepared(assets,renderer,resources,project);
    renderer.resizeViewport(Width,Height);renderer.resizeGameViewport(Width,Height);renderer.setGridVisible(false);
    // Remove presentation throttling from this hidden, offscreen diagnostic.
    bgfx::reset(Width,Height,BGFX_RESET_NONE);
    bgfx::setDebug(BGFX_DEBUG_PROFILER);
    DeviceContext deviceContext;static_cast<ID3D11Device*>(bgfx::getInternalData()->context)->GetImmediateContext(&deviceContext.value);
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x50524f4245;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.1f,.1f,.1f})},environment.settings);
    require(renderer.publishEnvironment(environment),renderer.environmentStatus());
    std::uint32_t frame{};bool ready{};std::map<std::string,double> viewTimes;
    const auto runFrame=[&](const ExtractedScene& world,bool shadows,bool moving,unsigned number) {
        const auto begin=Clock::now();Sample sample;
        resources.pump();assets.updateResources(frame);renderer.advanceStreaming();
        sample.update=milliseconds(begin);auto extracted=world;
        const Vec3 movement{moving?float(number%60)*.01f:0,0,0};
        if(presentedPlayer) {
            const unsigned index=&world==&baseline?0:1;auto& scene=*presentations[index];
            const ScenePoseUpdate pose{scene.properties().playerCharacter,parentInverse[index]*math::translation(movement)*originalPlayerWorld[index]};
            scene.applyRuntimePoses(std::span(&pose,1));extracted=extractScene(scene);
            sample.update=milliseconds(begin);
        } else if(moving)for(auto& model:extracted.models)if(std::find(movingEntities.begin(),movingEntities.end(),model.id)!=movingEntities.end())model.world=math::translation(movement)*model.world;
        if(ambientEnabled)extracted.environment.environment.ambientVisibility.enabled=true;
        if(!shadows){for(auto& light:extracted.points)light.castShadows=false;for(auto& light:extracted.directional)light.castShadows=false;for(auto& shadow:extracted.actorShadows)shadow.enabled=false;}
        prepared.beginFrame();prepared.setMaterialContext({double(number)/60,double(number)/60});
        auto followCamera=gameCamera;if(moving){followCamera.position=followCamera.position+movement;followCamera.target=followCamera.target+movement;}
        EditorRenderFrameInput input;
        input.settings.camera=sceneCamera;input.settings.environment=extracted.environment.environment;
        input.settings.exposure=extracted.environment.exposure;input.settings.proceduralSun=extracted.proceduralSun;
        input.sceneCamera=sceneCamera;input.gameCamera=followCamera;
        input.sceneWidth=Width;input.sceneHeight=Height;input.gameWidth=Width;input.gameHeight=Height;
        input.sceneAspect=Aspect;input.sceneVisible=true;input.gameVisible=true;input.gridVisible=false;
        input.worldId=1;input.shadowResolution=1024;
        input.shadowFocusEntities=movingEntities;
        // Preserve the retained room probe's fixed submitted material time.
        // The production producer also uses this captured context to prepare
        // resources. The frozen fixture has no Time/Panner material nodes.
        input.materialContext={};input.extracted=std::move(extracted);
        EditorRenderFramePreparationStats preparation;
        const auto preparedFrame=prepareEditorRenderFrame(std::move(input),assets,prepared,&preparation);
        sample.prepareShadows=preparation.shadowPreparationMs;sample.prepareReceivers=preparation.receiverPreparationMs;
        sample.preparedCasters=preparation.preparedCasters;ready=preparation.ready&&preparation.refined;
        const auto casterPins=std::static_pointer_cast<const EditorRenderWorldPins>(preparedFrame->world->pinned);
        require(legacy||casterPins->shadows->models.size()==casterPins->shadows->instances.size(),
            "A caster was prepared separately for each overlapping light.");
        for(const auto& caster:casterPins->shadows->instances)
            if(std::find(movingEntities.begin(),movingEntities.end(),caster.entityId)!=movingEntities.end())++sample.focusCasters;
        require(preparedFrame->views.size()==2,"Room frame did not prepare both camera requests.");
        sample.sceneModels=preparedFrame->views[0].instances.size();sample.gameModels=preparedFrame->views[1].instances.size();
        const auto start=Clock::now();
        renderer.renderFrame(preparedFrame);
        sample.submit=milliseconds(start);require(renderer.shadowStatus().empty(),renderer.shadowStatus());
        const auto shadow=renderer.shadowStats();sample.shadowDraws=shadow.drawCalls;sample.faces=shadow.renderedFaces;sample.cascades=shadow.directionalCascades;
        sample.pointHits=shadow.cacheHits;sample.directionalHits=shadow.directionalCacheHits;
        sample.pointDeferred=shadow.pointDeferred;sample.pointCriticalDeferred=shadow.pointCriticalDeferred;
        sample.directionalWorldProjections=shadow.directionalWorldProjections;sample.characterProjections=shadow.characterProjections;
        require(sample.faces<=24,"Scene/Game exceeded the shared twenty-four-face point-shadow update budget.");
        prepared.endFrame();const auto retained=renderer.modelResidentBytes()+renderer.thumbnailResidentBytes();
        assets.trimTextureCache(retained>=resources.config().gpuRetentionBytes?0:resources.config().gpuRetentionBytes-retained);
        frame=bgfx::frame();sample.submittedFrame=frame;
        // Offscreen views never call Present. Flush complete query pairs now,
        // so GPU timestamps cannot include idle time awaiting the next frame's
        // driver command-buffer submission. This queues work without waiting.
        deviceContext.value->Flush();sample.frame=milliseconds(begin);
        const auto* stats=bgfx::getStats();sample.draws=stats->numDraw;sample.gpuFrame=stats->gpuFrameNum;
        if(stats->gpuFrameNum&&stats->gpuTimerFreq>0&&stats->gpuTimeEnd>=stats->gpuTimeBegin)sample.gpu=double(stats->gpuTimeEnd-stats->gpuTimeBegin)*1000/stats->gpuTimerFreq;
        viewTimes.clear();
        for(unsigned i=0;i<stats->numViews&&stats->gpuTimerFreq>0;++i) {
            const auto& view=stats->viewStats[i];const auto elapsed=double(view.gpuTimeEnd-view.gpuTimeBegin)*1000/stats->gpuTimerFreq;
            if(elapsed<0)continue;viewTimes[view.name]=elapsed;
            if(view.view>=PointShadowView&&view.view<DirectionalShadowView)sample.gpuPoints+=elapsed;
            else if(view.view>=DirectionalShadowView&&view.view<SceneDepthPrepassView)sample.gpuDirectionals+=elapsed;
            else if(view.view==SceneDepthPrepassView||view.view==GameDepthPrepassView)sample.gpuDepth+=elapsed;
            else if(view.view==SceneAmbientView||view.view==GameAmbientView)sample.gpuAmbient+=elapsed;
            else if(view.view==SceneView)sample.gpuScene+=elapsed;
            else if(view.view==GameSceneView)sample.gpuGame+=elapsed;
            else sample.gpuPost+=elapsed;
        }
        if(stats->cpuTimerFreq>0)sample.backend=double(stats->cpuTimeEnd-stats->cpuTimeBegin)*1000/stats->cpuTimerFreq;
        return sample;
    };
    Json result;result["world_origin"]=savedWorld?"saved_main":"native_import";result["legacy_shadow_preparation"]=legacy;result["adapter"]=bgfx::getCaps()->vendorId;result["device"]=bgfx::getCaps()->deviceId;result["viewport"]={Width,Height};
    result["native_meshes"]=source->meshes.size();result["native_points"]=std::count_if(source->lights.begin(),source->lights.end(),[](const auto& light){return light.type==1;});
    result["combined_models"]=combined.models.size();result["combined_points"]=combined.points.size();result["combined_directionals"]=combined.directional.size();
    result["shadow_focus_entities"]=movingEntities;result["point_shadow_face_budget"]=24;
    result["ambient_visibility_requested"]=ambientEnabled;
    result["presented_player_workload"]=presentedPlayer;result["rig_disabled_override"]=rigOff;result["diagnostic_world_key"]=keyLight;
    result["ambient_visibility_saved"]=combined.environment.environment.ambientVisibility.enabled;
    result["player_lighting"]=playerLightingMetadata(combined);
    for(unsigned phase=0;phase<4;++phase) {
        const auto& world=phase?combined:baseline;const bool shadows=phase!=1,moving=phase==3;
        const auto deadline=Clock::now()+std::chrono::seconds{120};unsigned warm{};
        std::uint64_t warmFaces{},maximumWarmFaces{},maximumWarmDeferred{},maximumWarmCriticalDeferred{};
        std::cout<<"phase "<<phase<<" warm start"<<std::endl;
        do {const auto sample=runFrame(world,shadows,moving&&!cold,warm++);
            warmFaces+=sample.faces;maximumWarmFaces=std::max(maximumWarmFaces,sample.faces);
            maximumWarmDeferred=std::max(maximumWarmDeferred,sample.pointDeferred);
            maximumWarmCriticalDeferred=std::max(maximumWarmCriticalDeferred,sample.pointCriticalDeferred);
            // A continuously moving caster legitimately leaves coherent
            // same-projection work pending. Static/cold preparation must drain.
            if(!moving||cold)ready&=sample.pointDeferred==0&&sample.pointCriticalDeferred==0;
            if(warm%10==0)std::cout<<"warm "<<warm<<" ready="<<ready<<" frame="<<sample.frame<<" update="<<sample.update<<" shadowPrep="<<sample.prepareShadows<<" receiverPrep="<<sample.prepareReceivers<<" submit="<<sample.submit<<" gpu="<<sample.gpu<<" pointFaces="<<sample.faces<<" deferred="<<sample.pointDeferred<<" critical="<<sample.pointCriticalDeferred<<std::endl;
            if(!ready)std::this_thread::sleep_for(std::chrono::milliseconds{1});}
        while((!ready||warm<warmupCount)&&Clock::now()<deadline);
        require(ready,"Timed out preparing complete room materials: "+prepared.status()+assets.status());
        if(cold){std::cout<<"Cold cache phase "<<phase<<" fully prepared after "<<warm<<" frames; "<<prepared.status()<<assets.status()<<std::endl;continue;}
        std::vector<Sample> samples;for(unsigned i=0;i<sampleCount;++i){samples.push_back(runFrame(world,shadows,moving,i));const auto& value=samples.back();
            if(raw)raw<<phase<<','<<i<<','<<value.submittedFrame<<','<<value.gpuFrame<<','<<value.update<<','<<value.prepareShadows<<','<<value.prepareReceivers<<','<<value.submit<<','<<value.frame<<','<<value.gpu<<','<<value.backend<<','<<value.draws<<','<<value.shadowDraws<<','<<value.faces<<','<<value.cascades<<','<<value.pointHits<<','<<value.directionalHits<<','<<value.sceneModels<<','<<value.gameModels<<','<<value.pointDeferred<<','<<value.pointCriticalDeferred<<','<<value.directionalWorldProjections<<','<<value.characterProjections<<','<<value.focusCasters<<','<<value.gpuDepth<<','<<value.gpuAmbient<<','<<renderer.environmentResidentBytes()<<'\n';}
        if(phase==2)require(std::all_of(samples.begin(),samples.end(),[](const auto& sample){return sample.faces==0&&sample.cascades==0;}),
            "A static, fully prepared room rewrote cached shadow maps.");
        const char* name=phase==0?"main":phase==1?"room_no_shadows":phase==2?"room_static_shadows":"room_moving_player";
        result[name]=summarize(samples);result[name]["shadow_bytes"]=renderer.shadowResidentBytes();result[name]["texture_bytes"]=assets.textureResidentBytes();
        result[name]["warmup_frames"]=warm;result[name]["warmup_point_faces"]=warmFaces;
        result[name]["warmup_max_point_faces"]=maximumWarmFaces;result[name]["warmup_max_point_deferred"]=maximumWarmDeferred;
        result[name]["warmup_max_point_critical_deferred"]=maximumWarmCriticalDeferred;
        result[name]["model_bytes"]=renderer.modelResidentBytes();
        result[name]["environment_bytes"]=renderer.environmentResidentBytes();
        result[name]["driver_gpu_memory_used"]=bgfx::getStats()->gpuMemoryUsed;result[name]["driver_gpu_memory_max"]=bgfx::getStats()->gpuMemoryMax;
        result[name]["last_frame_gpu_views_ms"]=viewTimes;
        std::cout<<name<<" "<<result[name].dump()<<std::endl;
        if(captureImages) {
            // Replay the final measured pose and material time. Readback-only
            // frames occur after timing and before the next phase's warmup.
            runFrame(world,shadows,moving,sampleCount-1);
            for(unsigned view=0;view<2;++view) {
                const auto target=view?renderer.gameViewportTexture():renderer.viewportTexture();
                const auto staging=bgfx::createTexture2D(Width,Height,false,1,bgfx::TextureFormat::RGBA8,
                    BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK);
                require(bgfx::isValid(staging),"Cannot allocate room image readback.");
                auto destination=std::make_shared<std::vector<std::uint8_t>>(Width*Height*4);
                captureDestinations.push_back(destination);auto& pixels=*destination;
                bgfx::blit(254,staging,0,0,target,0,0,Width,Height);
                const auto readyFrame=bgfx::readTexture(staging,pixels.data());
                require(readyFrame!=UINT32_MAX,"Room image readback is unsupported.");
                // On timeout, drain outstanding writes before pixels is freed.
                unsigned waited{};
                while(static_cast<std::int32_t>(frame-readyFrame)<0&&waited++<120){frame=bgfx::frame();deviceContext.value->Flush();}
                if(static_cast<std::int32_t>(frame-readyFrame)<0){renderer.drainPending(frame);bgfx::destroy(staging);throw std::runtime_error("Room image readback timed out.");}
                bgfx::destroy(staging);
                std::vector<std::uint8_t> png;
                require(lodepng::encode(png,pixels,Width,Height)==0,"Cannot encode room image.");
                const auto path=output/(std::string(name)+(view?"-game.png":"-scene.png"));
                std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(png.data()),static_cast<std::streamsize>(png.size()));
                require(bool(file),"Cannot write room image.");
            }
        }
    }
    result["warmup_minimum"]=warmupCount;result["sample_count"]=sampleCount;
    if(!output.empty()){std::ofstream file(output/"room-summary.json");file<<result.dump(2)<<'\n';}
    resources.stop();renderer.drainPending(frame);return result;
}
}
int main(int argc,char** argv) {
    try {
        require(argc>=7,"Pass engine project, native project, native scene ID, shaderc, shader sources and shader include directory (optional --legacy, --saved, --cold, --capture, --ambient, --presented-player, --rig-off, --key-light, --warmup N, --samples N, --output directory).");
        Options parameters;for(int i=7;i<argc;++i){const std::string option=argv[i];if(option=="--legacy")parameters.legacy=true;else if(option=="--saved")parameters.savedWorld=true;else if(option=="--cold")parameters.cold=true;else if(option=="--capture")parameters.capture=true;
            else if(option=="--ambient")parameters.ambient=true;
            else if(option=="--presented-player")parameters.presentedPlayer=true;
            else if(option=="--rig-off")parameters.rigOff=true;
            else if(option=="--key-light")parameters.keyLight=true;
            else if(option=="--output"){require(i+1<argc,"Missing output directory.");parameters.output=argv[++i];}
            else if(option=="--samples"||option=="--warmup"){require(i+1<argc,"Missing frame count.");const auto count=std::stoul(argv[++i]);require(count>0&&count<=1000000,"Frame count is out of range.");if(option=="--samples")parameters.samples=static_cast<unsigned>(count);else parameters.warmup=static_cast<unsigned>(count);}
            else throw std::runtime_error("Unknown diagnostic option: "+option);}
        require(!parameters.capture||(!parameters.output.empty()&&!parameters.cold),"--capture requires --output and measured phases.");
        Temporary temporary;const fs::path original=argv[1];
        std::cout<<"temporary project "<<temporary.root<<std::endl;
        for(const auto& entry:fs::directory_iterator(original/"assets"))if(entry.path().filename()!="cache"&&entry.path().filename()!="branding")
            fs::copy(entry.path(),temporary.root/"assets"/entry.path().filename(),fs::copy_options::recursive);
        fs::create_directories(temporary.root/"assets/cache");
        if(!parameters.cold)for(const char* cache:{"mesh-lods","texture-mips","shaders"})if(fs::is_directory(original/"assets/cache"/cache))
            fs::copy(original/"assets/cache"/cache,temporary.root/"assets/cache"/cache,fs::copy_options::recursive);
        ShaderCompileConfig config{fs::path(argv[4]),fs::path(argv[5]),fs::path(argv[6]),temporary.root/"assets/cache/shaders"};
        Sdl sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson room performance probe",Width,Height,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        require(bool(window),SDL_GetError());std::cout<<exercise(window.get(),temporary.root,argv[2],argv[3],config,parameters).dump(2)<<std::endl;return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
