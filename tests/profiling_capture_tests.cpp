#include "profiling/CaptureController.h"
#include "profiling/CaptureReadiness.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
namespace fs=std::filesystem;
using namespace crimson::profiling;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Temporary {
    fs::path root=fs::temp_directory_path()/("crimson-profile-capture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Temporary(){std::error_code error;const auto path=fs::weakly_canonical(root,error),parent=fs::weakly_canonical(fs::temp_directory_path(),error);
        if(!error&&path.parent_path()==parent&&path.filename().string().starts_with("crimson-profile-capture-"))fs::remove_all(path,error);}
};
std::string read(const fs::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
void startupSubmissionTail() {
    detail::StartupReadiness readiness;CaptureFrame frame;frame.materialsReady=true;
    require(!readiness.observe(frame)&&!readiness.reached,"An unrendered frame proved startup readiness.");
    frame.bgfxFrame=10;frame.sceneWidth=909;frame.sceneHeight=684;
    require(!readiness.observe(frame)&&readiness.reached&&readiness.tailFrames==0,"The first ready submission omitted its GPU tail.");
    for(unsigned i=0;i<12;++i)require(!readiness.observe(frame),"Duplicate submissions advanced the GPU tail.");
    for(unsigned i=0;i<7;++i){++frame.bgfxFrame;require(!readiness.observe(frame),"Startup stopped before eight further submissions.");}
    frame.materialsReady=false;require(!readiness.observe(frame)&&!readiness.reached,"New pending materials retained a stale ready tail.");
    frame.materialsReady=true;++frame.bgfxFrame;require(!readiness.observe(frame),"A new readiness interval inherited the old tail.");
    for(unsigned i=0;i<8;++i){++frame.bgfxFrame;require(readiness.observe(frame)==(i==7),"Startup tail did not finish at its eighth distinct submission.");}
    require(readiness.complete()&&readiness.tailFrames==8,"The complete startup interval was lost.");
}
void failedCollectorManifest() {
    Temporary temp;
    CaptureOptions options;options.toolsDirectory=temp.root/"missing tools";options.outputDirectory=temp.root/"captures";
    CaptureContext context;context.engineCommit="engine-fixture";context.rendererCommit="renderer-fixture";
    context.backend="not initialized";context.settingsJson=R"({"ssr":false,"width":909})";
    context.cacheJson=R"({"state":"cold"})";context.sourcesJson=R"({"scene":"fixture"})";
    CaptureController controller(options,context);
    require(controller.status().state==CaptureState::Idle&&!controller.active(),"Unrequested profiling was active.");
    CaptureFrame frame;frame.appFrame=1;frame.bgfxFrame=3;frame.sceneWidth=909;frame.sceneHeight=684;frame.materialsReady=true;
    controller.frame(frame);controller.requestStop();
    require(!fs::exists(options.outputDirectory),"Idle profiling created capture files.");
    require(!controller.captureNow(CaptureKind::Startup,0)&&!controller.captureNow(CaptureKind::Gameplay,3601),"Invalid capture duration was admitted.");
    require(controller.captureNow(CaptureKind::Startup,15),"A valid capture request was rejected.");
    require(!controller.waitForConnection(std::chrono::milliseconds{100}),"A missing collector falsely completed its handshake.");
    controller.frame(frame);controller.finish();
    const auto first=controller.status();
    require(first.state==CaptureState::Failed&&!first.active()&&!controller.active(),"Missing paired tools were not a final failed capture.");
    require(!fs::exists(first.trace)&&fs::is_regular_file(first.manifest),"Failed capture fabricated a trace or lost its manifest.");
    const auto firstBytes=read(first.manifest);const auto manifest=nlohmann::json::parse(firstBytes);
    require(manifest["schema"]==1&&manifest["capture_type"]=="startup"&&manifest["state"]=="failed", "Failure manifest lost its version, type or final state.");
    require(!manifest["complete"].get<bool>()&&!manifest["trace_complete"].get<bool>()&&!manifest["connected"].get<bool>(),"Unconnected capture claimed complete trace data.");
    require(!manifest["startup_readiness_reached"].get<bool>()&&manifest["frames_observed"]==0&&manifest["first_frame"].is_null(),
        "Preconnection frame state falsely proved startup capture coverage.");
    require(manifest["context"]["engine_commit"]==context.engineCommit&&manifest["context"]["renderer_commit"]==context.rendererCommit&&
        manifest["context"]["settings"]["ssr"]==false&&manifest["context"]["settings"]["width"]==909&&
        manifest["context"]["cache"]["state"]=="cold"&&!manifest["errors"].empty(),"Failure manifest lost reproducible context or the collector error.");
    context.cacheJson=R"({"state":"warm"})";controller.updateContext(context);
    require(controller.captureNow(CaptureKind::Gameplay,1),"A failed capture prevented a later explicit retry.");
    controller.finish();const auto second=controller.status();
    require(second.state==CaptureState::Failed&&second.manifest!=first.manifest&&read(first.manifest)==firstBytes,
        "Retry overwrote a prior capture or lost its independent failure state.");
    const auto retried=nlohmann::json::parse(read(second.manifest));
    require(retried["capture_type"]=="gameplay"&&retried["context"]["cache"]["state"]=="warm"&&!retried["complete"].get<bool>(),
        "Retry reused stale context or reported a nonexistent complete capture.");
    fs::create_directories(options.toolsDirectory);
    // Deliberately nonexecutable placeholders must be rejected by the version
    // guard before any process can be launched.
    std::ofstream(options.toolsDirectory/"tracy-capture.exe")<<"fixture";
    std::ofstream(options.toolsDirectory/"crimson-tracy-capture.exe")<<"fixture";
    std::ofstream(options.toolsDirectory/"tracy-tools.json")<<R"({"tracy_version":"0.13.1","files":{"tracy-capture.exe":"old"}})";
    require(controller.captureNow(CaptureKind::Gameplay,1),"Version validation request was rejected before its diagnostic could be written.");
    controller.finish();const auto incompatible=controller.status();
    require(incompatible.state==CaptureState::Failed&&incompatible.detail.find("0.14.1")!=std::string::npos&&!fs::exists(incompatible.trace),
        "An incompatible paired collector manifest was not rejected before launch.");
}
}
int main(){try{startupSubmissionTail();failedCollectorManifest();std::cout<<"Capture idle, duration, missing-tool, submission-tail, manifest and retry checks passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
