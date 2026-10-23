// Explicit integration diagnostic. It runs production PlaySession and Scene/Game
// rendering in the existing disposable-project harness, with no desktop input.
#include "profiling/CaptureController.h"
#include "runtime/PlaySession.h"
#include "scene/EnvironmentSerialization.h"
#include <crimson/renderer/BuildIdentity.hpp>
#include <crimson/renderer/core/Profiler.hpp>
#include "EngineProfileIdentity.h"
#include <dxgi.h>
#include <wrl/client.h>

namespace {
std::string tracyDiagnosticGpuName() {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return {};
    const auto* caps=bgfx::getCaps();
    for(UINT index=0;;++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if(factory->EnumAdapters1(index,&adapter)==DXGI_ERROR_NOT_FOUND)break;
        DXGI_ADAPTER_DESC1 description{};
        if(!adapter || FAILED(adapter->GetDesc1(&description)) || description.VendorId!=caps->vendorId || description.DeviceId!=caps->deviceId)continue;
        const auto count=WideCharToMultiByte(CP_UTF8,0,description.Description,-1,nullptr,0,nullptr,nullptr);
        if(count<=1)return {};
        std::string name(static_cast<std::size_t>(count),'\0');
        WideCharToMultiByte(CP_UTF8,0,description.Description,-1,name.data(),count,nullptr,nullptr);name.pop_back();return name;
    }
    return {};
}
int tracyCaptureDiagnostic(int argc,char** argv) {
    require(argc==8,"Usage: forward_plus_room_benchmark --tracy-project PROJECT SHADERC SOURCES INCLUDES OUTPUT TOOLS");
    const auto original=fs::weakly_canonical(argv[2]);Temporary temporary;
    copyGiProject(original,temporary.root);const auto project=temporary.root;
    initializeContentStorage(project);
    Json session;{std::ifstream file(project/".crimson/editor-session.json");require(bool(file),"Saved editor session is required.");file>>session;}
    profiling::CaptureOptions options;options.toolsDirectory=fs::absolute(argv[7]);options.outputDirectory=fs::absolute(argv[6]);
    profiling::CaptureContext metadata;metadata.projectRoot=project.string();metadata.rendererCommit=shared::kRendererSourceCommit;
    metadata.engineCommit=profiling::kEngineProfileCommit;metadata.engineDirty=profiling::kEngineProfileDirty;
    metadata.rendererDirty=shared::kRendererWorkingTreeDirty;metadata.backend=graphicsBackendKey(selectedTestGraphicsBackend());
    metadata.sourcesJson=Json{{"diagnostic","saved-scene production PlaySession and Scene/Game; editor UI and physical input excluded"},
        {"sourceProject",original.string()},{"rendererManifestSha256",shared::kRendererManifestSha256}}.dump();
    profiling::CaptureController captures(options,metadata);
    shared::profiling::set_enabled(true);shared::profiling::thread_name("Play capture diagnostic");
    std::uint64_t ticks{};
    {
        Sdl sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Crimson Tracy integration diagnostic",Width,Height,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        require(bool(window),SDL_GetError());
        ResourceManager resources;Renderer renderer(native_renderer_context(window.get(),nullptr,selectedTestGraphicsBackend()));
        metadata.gpuName=tracyDiagnosticGpuName();
        ShaderCompileConfig config{argv[3],argv[4],argv[5],resolveAssetPath(project,"assets/cache/shaders")};
        MaterialGraphPrograms programs(resources,config);AssetLibrary assets(project,&resources);assets.setMaterialGraphPrograms(&programs);
        EditorResources prepared(assets,renderer,resources,project);SceneDocument document(project);
        require(document.load(session.at("scene").get<std::string>()),document.status());
        const auto authored=extractScene(document.scene());const auto initialCamera=resolveGameCamera(document.scene());
        require(initialCamera.has_value(),"Saved scene has no active Game camera.");
        const auto map=authored.environment.environment.source==EnvironmentSource::Hdri?
            assets.references().resolveEnvironment(authored.environment.environment.map):std::string{};
        require(renderer.publishEnvironment(*prepareEnvironmentCpu(project,map)),renderer.environmentStatus());
        renderer.resizeViewport(Width,Height);renderer.resizeGameViewport(Width,Height);renderer.setGridVisible(false);
        PlaySession play(project);play.setResourceManager(resources);
        play.setAgentProfiles([&](const std::string& id){auto profile=assets.findNavigationAgentProfile(id);return profile?profile:document.navigationAgentProfile(id);});
        play.setGameplayEffects([&](const std::string& id){
            if(auto effect=assets.findGameplayEffect(id))return EffectDefinitionSnapshot{std::move(effect),assets.gameplayEffectRevision(id),{}};
            if(auto effect=document.gameplayEffect(id))return EffectDefinitionSnapshot{std::move(effect),1,{}};
            return EffectDefinitionSnapshot{{},0,"Effect unavailable in diagnostic: "+id};
        });
        std::uint32_t frame{};bool started{},recording{};unsigned readyFrames{};
        const auto deadline=Clock::now()+std::chrono::seconds{90};
        while(Clock::now()<deadline) {
            CRIMSON_PROFILE_ZONE("Diagnostic gameplay frame");
            resources.pump();assets.updateResources(frame);renderer.advanceStreaming();prepared.beginFrame();
            if(started)play.advance(PlaySession::FixedStep,Vec2{},*initialCamera);
            const auto& scene=started?play.presentationScene():document.scene();
            EditorRenderFrameInput input;input.extracted=extractScene(scene);
            input.settings.environment=input.extracted.environment.environment;input.settings.exposure=input.extracted.environment.exposure;
            input.settings.proceduralSun=input.extracted.proceduralSun;input.settings.antiAliasing=AntiAliasing::Smaa;
            input.sceneCamera=resolveGameCamera(scene).value_or(*initialCamera);input.gameCamera=input.sceneCamera;
            input.sceneWidth=input.gameWidth=Width;input.sceneHeight=input.gameHeight=Height;input.sceneAspect=Aspect;
            input.sceneVisible=input.gameVisible=true;input.worldId=1;input.giWorldId=1;input.gameCameraId=scene.properties().activeCamera;
            input.environmentId=scene.properties().activeEnvironment;input.shadowResolution=1024;
            EditorRenderFramePreparationStats preparation;
            renderer.renderFrame(prepareEditorRenderFrame(std::move(input),assets,prepared,&preparation));prepared.endFrame();
            frame=renderer.advance_frame();
            shared::profiling::frame_mark();
            const auto stats=resources.stats();
            if(!started) {
                readyFrames=preparation.ready&&!stats.queued&&!stats.running?readyFrames+1:0;
                if(readyFrames>=8) {
                    require(play.start(document),play.status());started=true;
                    metadata.settingsJson=Json{{"environment",environmentSettingsJson(authored.environment.environment)},
                        {"exposure",authored.environment.exposure}}.dump();
                    metadata.cacheJson=Json{{"state","warm after readiness"},{"loads",stats.loads},{"cacheHits",stats.cacheHits}}.dump();
                    captures.updateContext(metadata);require(captures.captureNow(profiling::CaptureKind::Gameplay,3),"Cannot request diagnostic capture.");recording=true;
                }
            }
            profiling::CaptureFrame sample;sample.appFrame=frame;sample.bgfxFrame=frame;
            sample.sceneWidth=sample.gameWidth=Width;sample.sceneHeight=sample.gameHeight=Height;
            sample.materialsReady=preparation.ready;sample.playing=started;
            if(const auto* gpu=bgfx::getStats();gpu&&gpu->gpuTimerFreq>0&&gpu->gpuTimeEnd>gpu->gpuTimeBegin)sample.sourceFrame=gpu->gpuFrameNum;
            captures.frame(sample);
            if(recording&&!captures.status().active())break;
        }
        require(recording,"Saved project did not become ready for its gameplay capture.");
        ticks=play.tickCount();play.stop();resources.stop();renderer.drainPending(frame);
    }
    captures.finish();const auto status=captures.status();
    require(status.state==profiling::CaptureState::Saved,status.detail);
    require(ticks>0,"Gameplay capture performed no fixed simulation ticks.");
    std::cout<<Json{{"trace",status.trace.string()},{"manifest",status.manifest.string()},{"fixedTicks",ticks},
        {"backend",metadata.backend}}.dump(2)<<std::endl;
    return 0;
}
}
