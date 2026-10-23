#include "CaptureController.h"
#include "CaptureProcess.h"
#include "CaptureReadiness.h"
#include "assets/AtomicFile.h"
#include <crimson/renderer/core/Profiler.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace crimson::profiling {
namespace {
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
std::string pathText(const std::filesystem::path& path) {const auto value=path.generic_u8string();return {value.begin(),value.end()};}
std::string utc(bool filename=false) {
    SYSTEMTIME time{};GetSystemTime(&time);char value[48]{};
    if(filename)std::snprintf(value,sizeof(value),"%04u%02u%02u-%02u%02u%02u-%03u",unsigned(time.wYear),unsigned(time.wMonth),unsigned(time.wDay),unsigned(time.wHour),unsigned(time.wMinute),unsigned(time.wSecond),unsigned(time.wMilliseconds));
    else std::snprintf(value,sizeof(value),"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",unsigned(time.wYear),unsigned(time.wMonth),unsigned(time.wDay),unsigned(time.wHour),unsigned(time.wMinute),unsigned(time.wSecond),unsigned(time.wMilliseconds));
    return value;
}
Json frameJson(const CaptureFrame& value) {
    return {{"app_frame",value.appFrame},{"bgfx_frame",value.bgfxFrame},
        {"source_frame",value.sourceFrame?Json(*value.sourceFrame):Json(nullptr)},
        {"scene_width",value.sceneWidth},{"scene_height",value.sceneHeight},{"game_width",value.gameWidth},{"game_height",value.gameHeight},
        {"materials_ready",value.materialsReady},{"playing",value.playing}};
}
Json contextJson(const CaptureContext& value,std::vector<std::string>& errors) {
    Json result{{"engine_commit",value.engineCommit},{"renderer_commit",value.rendererCommit},
        {"engine_dirty",value.engineDirty},{"renderer_dirty",value.rendererDirty},{"backend",value.backend},
        {"gpu_name",value.gpuName},{"project_root",value.projectRoot}};
    for(const auto& [name,text]:std::array<std::pair<const char*,const std::string*>,3>{{
        {"settings",&value.settingsJson},{"cache",&value.cacheJson},{"sources",&value.sourcesJson}}}) {
        result[name]=text->empty()?Json(nullptr):Json::parse(*text,nullptr,false);
        if(result[name].is_discarded()){result[name]=nullptr;errors.push_back(std::string("Invalid ")+name+" metadata JSON.");}
    }
    return result;
}
std::string logTail(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return {};
    const auto end=file.tellg();if(end<=0)return {};
    const auto count=std::min<std::streamoff>(static_cast<std::streamoff>(end),4*1024*1024);
    std::string result(static_cast<std::size_t>(count),'\0');file.seekg(-count,std::ios::end);file.read(result.data(),count);return result;
}
void validateTools(const std::filesystem::path& directory) {
    std::ifstream file(directory/"tracy-tools.json",std::ios::binary);
    if(!file)throw std::runtime_error("Paired Tracy tool manifest is unavailable: "+pathText(directory/"tracy-tools.json"));
    const auto manifest=Json::parse(file);
    if(manifest.value("tracy_version",std::string{})!="0.14.1" || !manifest.contains("files") ||
        !manifest["files"].is_object() || !manifest["files"].contains("tracy-capture.exe") ||
        !manifest["files"]["tracy-capture.exe"].is_string())
        throw std::runtime_error("Capture requires the paired Tracy 0.14.1 tool manifest and its tracy-capture.exe entry.");
    const auto hash=manifest["files"]["tracy-capture.exe"].get<std::string>();
    if(hash.size()!=64 || !std::all_of(hash.begin(),hash.end(),[](unsigned char value){return std::isxdigit(value)!=0;}))
        throw std::runtime_error("Paired Tracy capture tool manifest has an invalid SHA256 entry.");
}
enum StopReason { NoStop,UserStop,StartupReady,DurationLimit,Shutdown,ConnectionTimeout };
const char* stopName(int reason) {
    switch(reason){case UserStop:return "requested";case StartupReady:return "startup-materials-ready";case DurationLimit:return "duration-limit";
    case Shutdown:return "application-shutdown";case ConnectionTimeout:return "connection-timeout";default:return "collector-finished";}
}
}

const char* captureStateName(CaptureState state) noexcept {
    switch(state){case CaptureState::Idle:return "idle";case CaptureState::Connecting:return "connecting";case CaptureState::Capturing:return "capturing";
    case CaptureState::Saving:return "saving";case CaptureState::Saved:return "saved";case CaptureState::Failed:return "failed";}return "failed";
}
const char* captureKindName(CaptureKind kind) noexcept{return kind==CaptureKind::Startup?"startup":"gameplay";}

struct CaptureController::Impl {
    CaptureOptions options;CaptureContext context,requestedContext;
    mutable std::mutex mutex;std::condition_variable changed;std::jthread worker;
    CaptureStatus status;std::atomic<bool> active{};std::atomic<int> stopReason{NoStop};
    CaptureFrame first,last;std::uint64_t frames{};detail::StartupReadiness readiness;
    bool connected{},traceComplete{},forced{},ownsDirectory{};
    std::uint64_t traceBytes{};std::vector<std::string> errors;
    std::string requestedUtc,connectedUtc,endedUtc;
    explicit Impl(CaptureOptions settings,CaptureContext metadata):options(std::move(settings)),context(std::move(metadata)) {
        options.connectionTimeout=std::clamp(options.connectionTimeout,std::chrono::milliseconds{1},std::chrono::milliseconds{5000});
        options.stopTimeout=std::clamp(options.stopTimeout,std::chrono::milliseconds{100},std::chrono::milliseconds{15000});
        if(options.outputDirectory.empty())options.outputDirectory=std::filesystem::current_path()/"captures/profiling";
    }
    void stop(int reason) noexcept {int previous=NoStop;stopReason.compare_exchange_strong(previous,reason,std::memory_order_relaxed);}
    void transition(CaptureState state,const char* detail) {
        {std::lock_guard lock(mutex);status.state=state;status.detail=detail;}
        changed.notify_all();
    }
    void manifest() {
        Json value;std::filesystem::path path;
        {
            std::lock_guard lock(mutex);if(!ownsDirectory)return;auto allErrors=errors;
            value={{"schema",1},{"tracy_version","0.14.1"},{"capture_type",captureKindName(status.kind)},
                {"state",captureStateName(status.state)},{"duration_limit_seconds",status.durationSeconds},
                {"process_id",GetCurrentProcessId()},{"collector_address","127.0.0.1"},{"collector_port",options.port},
                {"trace",pathText(status.trace)},{"log",pathText(status.log)},{"trace_bytes",traceBytes},
                {"requested_utc",requestedUtc},{"connected_utc",connectedUtc.empty()?Json(nullptr):Json(connectedUtc)},
                {"ended_utc",endedUtc.empty()?Json(nullptr):Json(endedUtc)},
                {"connected",connected},{"forced_stop",forced},{"trace_complete",traceComplete},
                {"startup_readiness_reached",readiness.reached},{"startup_tail_frames",readiness.tailFrames},
                {"startup_tail_required",detail::StartupReadiness::requiredTailFrames},{"frames_observed",frames},
                {"first_frame",frames?frameJson(first):Json(nullptr)},{"last_frame",frames?frameJson(last):Json(nullptr)},
                {"startup_ready_frame",readiness.reached?frameJson(readiness.readyFrame):Json(nullptr)},
                {"stop_reason",stopName(stopReason.load(std::memory_order_relaxed))},
                {"requested_context",contextJson(requestedContext,allErrors)},{"context",contextJson(context,allErrors)},
                {"gpu_scope","Native Tracy GPU timestamps only where supported by the actual backend; no CPU timer is labeled as a GPU zone."}};
            value["errors"]=allErrors;
            value["complete"]=traceComplete&&!forced&&allErrors.empty()&&(status.kind!=CaptureKind::Startup||readiness.complete());
            path=status.manifest;
        }
        writeFileAtomically(path,value.dump(2)+"\n");
    }
    void collect() noexcept {
        shared::profiling::thread_name("Tracy capture controller");
        detail::Handle process,job;
        try {
            CaptureStatus initial;{std::lock_guard lock(mutex);initial=status;}
            std::filesystem::create_directories(initial.trace.parent_path().parent_path());
            if(!std::filesystem::create_directory(initial.trace.parent_path()))throw std::runtime_error("Capture output already exists; refusing to overwrite it.");
            {std::lock_guard lock(mutex);ownsDirectory=true;}
            const auto capture=options.toolsDirectory/"tracy-capture.exe",broker=options.toolsDirectory/"crimson-tracy-capture.exe";
            if(!std::filesystem::is_regular_file(capture)||!std::filesystem::is_regular_file(broker))
                throw std::runtime_error("Paired Tracy 0.14.1 capture tool or native capture broker is unavailable.");
            validateTools(options.toolsDirectory);
            if(!options.port)throw std::runtime_error("Capture port is invalid.");
            SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
            detail::Handle stopEvent(CreateEventW(&security,TRUE,FALSE,nullptr)),readyEvent(CreateEventW(&security,TRUE,FALSE,nullptr)),
                abortEvent(CreateEventW(&security,TRUE,FALSE,nullptr));
            if(!stopEvent.valid()||!readyEvent.valid()||!abortEvent.valid())throw detail::windowsError("Cannot create capture control events");
            job=detail::Handle(CreateJobObjectW(nullptr,nullptr));
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if(!job.valid()||!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))
                throw detail::windowsError("Cannot contain capture collector lifetime");
            const std::array inherited{stopEvent.value,readyEvent.value,abortEvent.value};
            process=detail::launch(broker,{capture.wstring(),initial.trace.wstring(),initial.log.wstring(),
                std::to_wstring(initial.durationSeconds),std::to_wstring(options.port),detail::handleArgument(stopEvent.value),
                detail::handleArgument(readyEvent.value),detail::handleArgument(abortEvent.value)},inherited,CREATE_NEW_CONSOLE,nullptr,nullptr,job.value);
            manifest();
            const auto connectionDeadline=Clock::now()+options.connectionTimeout;
            auto connectedAt=Clock::time_point{},savingDeadline=Clock::time_point{};
            bool handshake{},stopSent{},abortSent{};
            DWORD exitCode=STILL_ACTIVE;
            for(;;) {
                const auto wait=WaitForSingleObject(process.value,20);
                if(wait==WAIT_FAILED)throw detail::windowsError("Cannot poll capture broker");
                if(wait==WAIT_OBJECT_0) {
                    if(!GetExitCodeProcess(process.value,&exitCode))throw detail::windowsError("Cannot read capture broker status");
                    break;
                }
                const auto now=Clock::now();
                if(!handshake&&WaitForSingleObject(readyEvent.value,0)==WAIT_OBJECT_0&&shared::profiling::connected()) {
                    handshake=true;connectedAt=now;
                    {std::lock_guard lock(mutex);connected=true;connectedUtc=utc();}
                    transition(CaptureState::Capturing,"Recording Tracy CPU and supported native GPU zones.");manifest();
                }
                if(!handshake&&now>=connectionDeadline&&!abortSent) {
                    stop(ConnectionTimeout);SetEvent(abortEvent.value);abortSent=true;savingDeadline=now+options.stopTimeout;
                    {std::lock_guard lock(mutex);forced=true;errors.push_back("This engine and its official collector did not complete the capture handshake before the timeout.");}
                    transition(CaptureState::Saving,"Capture connection failed; stopping its private collector.");
                }
                if(handshake&&now-connectedAt>=std::chrono::seconds(initial.durationSeconds))stop(DurationLimit);
                if(handshake&&!stopSent&&stopReason.load(std::memory_order_relaxed)!=NoStop) {
                    if(!SetEvent(stopEvent.value))throw detail::windowsError("Cannot request graceful capture stop");
                    stopSent=true;savingDeadline=now+options.stopTimeout;
                    transition(CaptureState::Saving,"Saving the official Tracy capture.");manifest();
                }
                if(handshake&&savingDeadline==Clock::time_point{}&&!shared::profiling::connected()) {
                    savingDeadline=now+options.stopTimeout;transition(CaptureState::Saving,"Collector disconnected; saving trace.");
                }
                if(savingDeadline!=Clock::time_point{}&&now>=savingDeadline) {
                    {std::lock_guard lock(mutex);forced=true;errors.push_back("Official collector did not finish saving before the bounded stop timeout.");}
                    TerminateJobObject(job.value,ERROR_TIMEOUT);WaitForSingleObject(process.value,1000);exitCode=ERROR_TIMEOUT;break;
                }
            }
            std::error_code error;const auto bytes=std::filesystem::file_size(initial.trace,error);
            const auto log=logTail(initial.log);
            {
                std::lock_guard lock(mutex);endedUtc=utc();traceBytes=error?0:bytes;
                traceComplete=connected&&!forced&&exitCode==0&&traceBytes>0&&log.find("Trace size")!=std::string::npos&&
                    log.find("Instrumentation failure:")==std::string::npos;
                if(!traceComplete)errors.push_back("Official collector did not confirm a complete trace save (exit "+std::to_string(exitCode)+").");
                status.state=traceComplete?CaptureState::Saved:CaptureState::Failed;
                status.detail=traceComplete?(initial.kind==CaptureKind::Startup&&!readiness.complete()?
                    "Trace saved; startup readiness and GPU submission tail were not completed before the capture ended.":"Tracy capture saved."):"Capture incomplete; see its manifest and collector log.";
            }
            manifest();
        } catch(const std::exception& error) {
            if(job.valid())TerminateJobObject(job.value,ERROR_CANCELLED);
            {std::lock_guard lock(mutex);endedUtc=utc();errors.push_back(error.what());status.state=CaptureState::Failed;status.detail=error.what();}
            try{manifest();}catch(const std::exception& failure){std::lock_guard lock(mutex);status.detail+=" Manifest: ";status.detail+=failure.what();}
        }
        active.store(false,std::memory_order_release);changed.notify_all();
    }
};

CaptureController::CaptureController(CaptureOptions options,CaptureContext context):impl_(std::make_unique<Impl>(std::move(options),std::move(context))){}
CaptureController::~CaptureController(){finish();}
bool CaptureController::captureNow(CaptureKind kind,std::uint32_t duration) {
    auto& owner=*impl_;
    if(owner.active.load(std::memory_order_acquire))return false;
    if(duration<1||duration>3600) {std::lock_guard lock(owner.mutex);owner.status.detail="Capture duration must be from 1 to 3600 seconds.";return false;}
    if(owner.worker.joinable())owner.worker.join();
    static std::atomic_uint64_t sequence{};
    {
        std::lock_guard lock(owner.mutex);
        const auto directory=owner.options.outputDirectory/(std::string(captureKindName(kind))+"-"+utc(true)+"-"+
            std::to_string(GetCurrentProcessId())+"-"+std::to_string(sequence.fetch_add(1)));
        owner.status={kind,CaptureState::Connecting,duration,directory/"capture.tracy",directory/"manifest.json",directory/"collector.log","Connecting the official local Tracy collector."};
        owner.requestedContext=owner.context;owner.requestedUtc=utc();owner.connectedUtc.clear();owner.endedUtc.clear();
        owner.frames=0;owner.readiness={};owner.connected=owner.traceComplete=owner.forced=owner.ownsDirectory=false;owner.traceBytes=0;owner.errors.clear();
        owner.stopReason.store(NoStop,std::memory_order_relaxed);owner.active.store(true,std::memory_order_release);
    }
    try{owner.worker=std::jthread([&owner]{owner.collect();});return true;}
    catch(const std::exception& error) {std::lock_guard lock(owner.mutex);owner.status.state=CaptureState::Failed;owner.status.detail=error.what();owner.active.store(false,std::memory_order_release);return false;}
}
bool CaptureController::waitForConnection(std::chrono::milliseconds timeout) {
    auto& owner=*impl_;std::unique_lock lock(owner.mutex);
    timeout=std::clamp(timeout,std::chrono::milliseconds{0},std::chrono::milliseconds{5000});
    owner.changed.wait_for(lock,timeout,[&]{return owner.connected||!owner.active.load(std::memory_order_acquire);});
    return owner.connected;
}
void CaptureController::updateContext(CaptureContext context){std::lock_guard lock(impl_->mutex);impl_->context=std::move(context);}
void CaptureController::frame(const CaptureFrame& value) noexcept {
    auto& owner=*impl_;if(!owner.active.load(std::memory_order_relaxed))return;
    try {
        std::lock_guard lock(owner.mutex);
        if(owner.status.state!=CaptureState::Capturing)return;
        if(!owner.frames)owner.first=value;owner.last=value;++owner.frames;
        if(owner.status.kind==CaptureKind::Startup&&owner.readiness.observe(value))owner.stop(StartupReady);
    }catch(...){}
}
void CaptureController::requestStop() noexcept {if(impl_->active.load(std::memory_order_relaxed))impl_->stop(UserStop);}
bool CaptureController::active() const noexcept{return impl_->active.load(std::memory_order_relaxed);}
CaptureStatus CaptureController::status() const {std::lock_guard lock(impl_->mutex);return impl_->status;}
void CaptureController::finish() noexcept {
    auto& owner=*impl_;if(owner.active.load(std::memory_order_relaxed))owner.stop(Shutdown);
    if(owner.worker.joinable())owner.worker.join();
}
} // namespace crimson::profiling
