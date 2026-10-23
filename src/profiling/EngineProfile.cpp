#include "EngineProfile.h"
#include "App.h"
#include "EngineProfileIdentity.h"
#include "resources/ResourceManager.h"
#include "resources/ShaderCompiler.h"
#include "scene/EnvironmentSerialization.h"
#include "scene/SceneDocument.h"
#include <crimson/renderer/BuildIdentity.hpp>
#include <crimson/renderer/core/Profiler.hpp>
#include <bgfx/bgfx.h>
#include <Windows.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <stdexcept>

namespace crimson::profiling {
namespace {
std::filesystem::path utf8Path(const std::string& value) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size()));
}
std::string utf8(const std::filesystem::path& path) {
    const auto text=path.u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};
}
std::filesystem::path executableDirectory() {
    std::wstring path(32768,L'\0');
    const auto count=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!count || count>=path.size())throw std::runtime_error("Cannot locate the profiling tools beside the renderer.");
    path.resize(count);return std::filesystem::path(path).parent_path();
}
CaptureOptions captureOptions(const AppOptions& options,const std::filesystem::path& root) {
    CaptureOptions result;
    result.toolsDirectory=executableDirectory()/L"profiling";
    result.outputDirectory=options.profileOutput.empty()?root/"captures"/"profiling":std::filesystem::absolute(utf8Path(options.profileOutput));
    return result;
}
CaptureContext initialContext(const std::filesystem::path& root) {
    CaptureContext result;
    result.projectRoot=utf8(root);
    result.engineCommit=kEngineProfileCommit;result.engineDirty=kEngineProfileDirty;
    result.rendererCommit=shared::kRendererSourceCommit;result.rendererDirty=shared::kRendererWorkingTreeDirty;
    result.sourcesJson=nlohmann::json{{"engineConfiguredSourceSha256",kEngineProfileSourceSha256},
        {"rendererManifestSha256",shared::kRendererManifestSha256},{"buildConfiguration",kEngineProfileConfiguration},
        {"tracyVersion","0.14.1"},{"materialAbi",kEngineProfileMaterialAbi},
        {"materialGenerator",kEngineProfileMaterialGenerator}}.dump();
    return result;
}
std::string adapterName() {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return {};
    const auto* caps=bgfx::getCaps();
    for(UINT index=0;;++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if(factory->EnumAdapters1(index,&adapter)==DXGI_ERROR_NOT_FOUND)break;
        if(!adapter)continue;
        DXGI_ADAPTER_DESC1 desc{};
        if(FAILED(adapter->GetDesc1(&desc)) || desc.VendorId!=caps->vendorId || desc.DeviceId!=caps->deviceId)continue;
        const auto count=WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,nullptr,0,nullptr,nullptr);
        if(count<=1)return {};
        std::string result(static_cast<std::size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,result.data(),count,nullptr,nullptr);result.pop_back();
        return result;
    }
    return {};
}
}
EngineProfile::EngineProfile(const AppOptions& options)
    : root_(options.projectRoot.empty()?std::filesystem::path(CC_PROJECT_ROOT):std::filesystem::absolute(utf8Path(options.projectRoot))),
      context_(initialContext(root_)),captures_(captureOptions(options,root_),context_),
      gameplayArmed_(options.profileMode=="gameplay"),editorArmed_(options.profileMode=="editor"),captureDuration_(options.profileDuration) {
    shared::profiling::set_enabled(CRIMSON_ENABLE_PROFILING!=0);
    shared::profiling::thread_name("Engine main");
    if(options.profileMode=="startup") {
        if(captures_.captureNow(CaptureKind::Startup,options.profileDuration))captures_.waitForConnection();
        const auto status=captures_.status();
        if(status.state==CaptureState::Failed)std::fprintf(stderr,"Tracy startup capture: %s\n",status.detail.c_str());
    }
}
void EngineProfile::backendInitialized(GraphicsBackend backend) {
    context_.backend=graphicsBackendKey(backend);context_.gpuName=adapterName();
    const auto* caps=bgfx::getCaps();
    auto sources=nlohmann::json::parse(context_.sourcesJson);
    sources["gpuVendorId"]=caps->vendorId;sources["gpuDeviceId"]=caps->deviceId;
    context_.sourcesJson=sources.dump();captures_.updateContext(context_);
    shared::profiling::app_info(std::string("Crimson Engine ")+context_.engineCommit+"; renderer "+context_.rendererCommit+"; "+context_.backend);
}
bool EngineProfile::observing(bool playing) const {
    return editorArmed_ || (gameplayArmed_ && playing) || shared::profiling::connected() || captures_.active();
}
void EngineProfile::frame(CaptureFrame frame,const SceneSettings* settings,const ResourceStats* resources,const SceneDocument* document) {
    if(editorArmed_) {
        editorArmed_=false;captures_.captureNow(CaptureKind::Editor,captureDuration_);
    }
    if(gameplayArmed_ && frame.playing) {
        gameplayArmed_=false;captures_.captureNow(CaptureKind::Gameplay,captureDuration_);
    }
    const auto* stats=bgfx::getStats();
    if(stats && stats->gpuTimerFreq>0 && stats->gpuTimeEnd>stats->gpuTimeBegin)frame.sourceFrame=stats->gpuFrameNum;
    if(captures_.active()) {
        const auto now=std::chrono::steady_clock::now();
        if(now-lastContext_>=std::chrono::milliseconds{250}) {
            if(settings) {
                nlohmann::json snapshot{{"environment",environmentSettingsJson(settings->environment)},
                    {"exposure",settings->exposure},{"antiAliasing",static_cast<int>(settings->antiAliasing)}};
                if(document)snapshot["scene"]={{"assetId",document->assetId()},{"revision",document->scene().revision()},
                    {"documentGeneration",document->documentGeneration()},{"dirty",document->dirty()},{"entities",document->scene().records().size()}};
                context_.settingsJson=snapshot.dump();
            }
            const auto shader=shaderCompilerMetrics();
            nlohmann::json cache{{"shaderCompilerInvocations",shader.compilerInvocations},{"shaderCompilerNanoseconds",shader.compilerNanoseconds},
                {"shaderCacheOnlyRequests",shader.cacheOnlyRequests},{"shaderCacheOnlyHits",shader.cacheOnlyHits},
                {"shaderWorkerRequests",shader.workerRequests},{"shaderWorkerCacheHits",shader.workerCacheHits}};
            if(resources)cache["resources"]={{"queued",resources->queued},{"running",resources->running},
                {"loads",resources->loads},{"cacheHits",resources->cacheHits},{"failures",resources->failures},
                {"cpuBytes",resources->cpuBytes}};
            context_.cacheJson=cache.dump();captures_.updateContext(context_);lastContext_=now;
        }
    }
    captures_.frame(frame);
    if(!shared::profiling::connected())return;
    shared::profiling::frame_mark();
    shared::profiling::counter("Scene pixels",static_cast<double>(frame.sceneWidth)*frame.sceneHeight);
    shared::profiling::counter("Game pixels",static_cast<double>(frame.gameWidth)*frame.gameHeight);
    if(settings) {
        shared::profiling::counter("SSR active",effectiveSsr(settings->environment.ssr)?1:0);
        shared::profiling::counter("SSR quality",static_cast<int>(settings->environment.ssr.quality));
        shared::profiling::counter("SSGI active",effectiveSsgi(settings->environment.ssgi)?1:0);
        shared::profiling::counter("SSAO active",effectiveSsao(settings->environment.ssao)?1:0);
        shared::profiling::counter("Antialiasing",static_cast<int>(settings->antiAliasing));
    }
    if(resources) {
        shared::profiling::counter("Resources queued",static_cast<double>(resources->queued));
        shared::profiling::counter("Resources running",static_cast<double>(resources->running));
        shared::profiling::counter("Resource CPU bytes",static_cast<double>(resources->cpuBytes));
    }
    if(stats) {
        shared::profiling::counter("Draw calls",stats->numDraw);
        shared::profiling::counter("Compute dispatches",stats->numCompute);
        if(frame.sourceFrame && stats->gpuFrameNum!=lastGpuFrame_) {
            lastGpuFrame_=stats->gpuFrameNum;
            // This is elapsed timestamp duration. Native Tracy GPU zones provide
            // the calibrated timeline; an async bgfx sample is never a CPU zone.
            shared::profiling::counter("bgfx GPU elapsed ms (async)",1000.0*static_cast<double>(stats->gpuTimeEnd-stats->gpuTimeBegin)/static_cast<double>(stats->gpuTimerFreq));
            shared::profiling::counter("bgfx GPU source frame",stats->gpuFrameNum);
        }
    }
}
} // namespace crimson::profiling
