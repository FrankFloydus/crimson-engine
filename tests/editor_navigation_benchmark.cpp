// Explicit, hidden production-editor benchmark. Never uses desktop input or
// the authored root: the caller supplies a disposable frozen project copy.
#include "editor/EditorSession.h"
#include "editor/AssetPreviews.h"
#include "editor/ResourceRuntime.h"
#include "crimson-renderer/Diagnostics.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "crimson-renderer/Renderer.h"
#include "input/Navigation.h"
#include "tools/Tools.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <implot.h>
#include <imgui_impl_sdl3.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace crimson {
struct EditorPerformanceProbe {
    template<class T> static nlohmann::json stats(const T& previews) {
        if constexpr(requires {previews.stats();}) {
            const auto s=previews.stats();
            return {{"requests",s.requests},{"validations",s.validations},{"resident_hits",s.residentHits},
                {"disk_requests",s.diskRequests},{"render_requests",s.renderRequests},{"invalidations",s.invalidations},
                {"source_plan_builds",s.sourcePlanBuilds},{"source_plan_hits",s.sourcePlanHits}};
        } else return nlohmann::json::object();
    }
    static auto previews(const EditorSession& e) {return stats(*e.previews_);}
    static Camera camera(const EditorSession& e) {return e.settings_.camera;}
    static void camera(EditorSession& e,const Camera& c) {e.settings_.camera=c;}
    static void folder(EditorSession& e,const std::string& id) {e.editor_.browser_.folder_=id;}
    static const auto& visible(const EditorSession& e) {return e.visiblePreviewIds_;}
    static auto entityCount(const EditorSession& e) {return e.document_.scene().records().size();}
    static nlohmann::json modelStatus(const EditorSession& e,const AssetLibrary& assets) {
        auto result=nlohmann::json::array();
        for(const auto& record:e.document_.scene().records())if(record.model)
            result.push_back({{"id",record.model->id},{"revision",assets.modelRevision(record.model->id)},{"diagnostic",assets.modelDiagnostic(record.model->id)}});
        return result;
    }
    static const auto& preparationStatus(const EditorSession& e) {return e.prepared_.status();}
};
}
namespace {
using namespace crimson;
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point a,Clock::time_point b) {return std::chrono::duration<double,std::milli>(b-a).count();}
struct Sample {double runtime{},editor{},ui{},submit{},frame{},gpu{};std::size_t visible{},ready{};};
Json summary(const std::vector<Sample>& samples) {
    Json result;result["samples"]=samples.size();if(samples.empty())return result;
    const auto summarize=[&](auto field) {std::vector<double> v;double sum{};for(const auto& s:samples){v.push_back(s.*field);sum+=s.*field;}
        std::sort(v.begin(),v.end());return Json{{"mean",sum/v.size()},{"p50",v[v.size()/2]},{"p95",v[std::min(v.size()-1,v.size()*95/100)]},{"maximum",v.back()}};};
    result["frame_ms"]=summarize(&Sample::frame);result["resource_runtime_ms"]=summarize(&Sample::runtime);
    result["editor_ms"]=summarize(&Sample::editor);result["ui_submit_ms"]=summarize(&Sample::ui);
    result["present_ms"]=summarize(&Sample::submit);result["gpu_ms"]=summarize(&Sample::gpu);
    result["mean_fps"]=1000.0/result["frame_ms"]["mean"].get<double>();
    result["visible_last"]=samples.back().visible;result["ready_last"]=samples.back().ready;
    result["meets_120fps_p95"]=result["frame_ms"]["p95"].get<double>()<=1000.0/120.0;return result;
}
Json resourceStats(ResourceStats s) {return {{"loads",s.loads},{"cache_hits",s.cacheHits},{"evictions",s.evictions},{"failures",s.failures},{"queued",s.queued},{"running",s.running},{"cpu_bytes",s.cpuBytes}};}
}
int main(int argc,char** argv) {
    using namespace crimson;
    try {
        if(argc!=3)throw std::runtime_error("Usage: editor_navigation_benchmark DISPOSABLE_ROOT REPORT.json");
        const auto root=std::filesystem::weakly_canonical(argv[1]);
        const auto temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
        if(root.parent_path()!=temp || !root.filename().string().starts_with("crimson-editor-perf-"))
            throw std::runtime_error("Benchmark requires a disposable crimson-editor-perf-* directory directly under system temp.");
        const std::filesystem::path output=std::filesystem::absolute(argv[2]);
        if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS))throw std::runtime_error(SDL_GetError());
        Json report;report["scope"]="Hidden Direct3D11 production EditorSession/AssetBrowser with saved scene, scripted internal camera movement and folder revisits; no desktop input, no capture";
        report["root"]=root.string();report["size"]={1920,1080};
        {
            std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson editor performance fixture",1920,1080,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
            if(!window)throw std::runtime_error(SDL_GetError());
            ResourceManager resources;
            Diagnostics diagnostics;
            Renderer renderer(native_renderer_context(window.get(),&diagnostics));
            report["adapter_vendor"]=bgfx::getCaps()->vendorId;report["adapter_device"]=bgfx::getCaps()->deviceId;
            IMGUI_CHECKVERSION();ImGui::CreateContext();ImPlot::CreateContext();
            ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_DockingEnable;
            ImGui::GetIO().IniFilename=nullptr;
            ImGui::LoadIniSettingsFromDisk((root/"crimson_ui.ini").string().c_str());
            if(!ImGui_ImplSDL3_InitForD3D(window.get()))throw std::runtime_error("ImGui SDL initialization failed.");
            {
                ImGuiBgfx tools(renderer,[&](const std::string& s){diagnostics.log(s);});
                AssetLibrary assets(root,&resources);
                Navigation navigation(window.get());
                applyToolsStyle();
                ResourceRuntime runtime(root,resources,assets,renderer,tools);
                {
                    EditorSession editor(root,assets,renderer);
                    editor.setSimulationSuspended(true);
                    report["entities"]=EditorPerformanceProbe::entityCount(editor);report["catalog_entries"]=assets.catalog().size();
                    const Camera initial=EditorPerformanceProbe::camera(editor);
                    std::uint32_t frame{};double motion{};
                    std::ofstream csv(output.string()+".csv");csv<<"phase,index,frame_ms,runtime_ms,editor_ms,ui_ms,present_ms,gpu_ms,visible,ready,loads\n";
                    const auto tick=[&](const std::string& phase,bool moving) {
                        const auto a=Clock::now();SDL_Event event;while(SDL_PollEvent(&event))ImGui_ImplSDL3_ProcessEvent(&event);
                        if(moving) {motion+=.006;Camera camera=initial;camera.position[0]+=static_cast<float>(std::sin(motion)*.5);camera.position[2]+=static_cast<float>((std::cos(motion)-1)*.5);EditorPerformanceProbe::camera(editor,camera);}
                        renderer.resize(1920,1080);runtime.preFrame(&editor,frame);const auto b=Clock::now();
                        tools.newFrame(frame);ImGui_ImplSDL3_NewFrame();ImGui::NewFrame();
                        editor.frame(assets,renderer,navigation,window.get(),frame,false);const auto c=Clock::now();
                        ImGui::Render();bgfx::setViewClear(ToolsView,BGFX_CLEAR_COLOR,0x202020ff);tools.render(ImGui::GetDrawData());const auto d=Clock::now();
                        frame=renderer.advance_frame();tools.postFrame(frame);const auto e=Clock::now();
                        Sample s;s.runtime=ms(a,b);s.editor=ms(b,c);s.ui=ms(c,d);s.submit=ms(d,e);s.frame=ms(a,e);
                        if(const auto* stats=bgfx::getStats();stats&&stats->gpuTimerFreq>0)s.gpu=1000.0*static_cast<double>(stats->gpuTimeEnd-stats->gpuTimeBegin)/stats->gpuTimerFreq;
                        s.visible=EditorPerformanceProbe::visible(editor).size();
                        for(const auto& id:EditorPerformanceProbe::visible(editor))if(bgfx::isValid(renderer.thumbnailTexture(id)))++s.ready;
                        csv<<phase<<','<<frame<<','<<s.frame<<','<<s.runtime<<','<<s.editor<<','<<s.ui<<','<<s.submit<<','<<s.gpu<<','<<s.visible<<','<<s.ready<<','<<resources.stats().loads<<'\n';
                        return s;
                    };
                    std::vector<Sample> warmup;const auto started=Clock::now();double initialReadyLatency=-1;
                    for(unsigned i=0;i<600;++i) {const auto s=tick("warmup",false);warmup.push_back(s);const auto stats=resources.stats();
                        if(initialReadyLatency<0&&s.visible&&s.ready==s.visible)initialReadyLatency=ms(started,Clock::now());
                        if(i>=120 && !stats.queued&&!stats.running && (!s.visible||s.ready==s.visible))break;
                        if(ms(started,Clock::now())>30000)break;}
                    report["warmup"]=summary(warmup);report["initial_all_ready_ms"]=initialReadyLatency;
                    report["warmup_complete"]=!warmup.empty()&&warmup.back().visible&&warmup.back().ready==warmup.back().visible;
                    report["before_navigation"]=resourceStats(resources.stats());report["previews_before_navigation"]=EditorPerformanceProbe::previews(editor);
                    std::cout<<"warmup complete"<<std::endl;
                    std::vector<Sample> navigationSamples;const auto navigationStart=Clock::now();
                    for(unsigned i=0;i<10000;++i) {
                        navigationSamples.push_back(tick("navigation",true));
                        if(i>=299 && ms(navigationStart,Clock::now())>=6100)break;
                    }
                    report["navigation"]=summary(navigationSamples);report["after_navigation"]=resourceStats(resources.stats());
                    report["scene_models"]=EditorPerformanceProbe::modelStatus(editor,assets);
                    report["preparation_status"]=EditorPerformanceProbe::preparationStatus(editor);
                    report["model_gpu_bytes"]=renderer.modelResidentBytes();
                    report["navigation"]["wall_total_ms"]=ms(navigationStart,Clock::now());
                    report["navigation"]["wall_mean_fps"]=1000.0*navigationSamples.size()/ms(navigationStart,Clock::now());
                    report["previews_after_navigation"]=EditorPerformanceProbe::previews(editor);
                    EditorPerformanceProbe::folder(editor,"folder:engine");
                    const auto away=Clock::now();unsigned awayFrames{};
                    do {tick("away",true);++awayFrames;} while(ms(away,Clock::now())<6100 && awayFrames<5000);
                    report["away_frames"]=awayFrames;report["before_revisit"]=resourceStats(resources.stats());report["previews_before_revisit"]=EditorPerformanceProbe::previews(editor);
                    EditorPerformanceProbe::folder(editor,"folder:content");
                    std::vector<Sample> revisit;const auto revisitStart=Clock::now();double readyLatency=-1;
                    for(unsigned i=0;i<180;++i) {auto s=tick("revisit",true);revisit.push_back(s);if(readyLatency<0&&s.visible&&s.ready==s.visible)readyLatency=ms(revisitStart,Clock::now());}
                    report["revisit"]=summary(revisit);report["revisit"]["wall_total_ms"]=ms(revisitStart,Clock::now());
                    report["revisit"]["wall_mean_fps"]=1000.0*revisit.size()/ms(revisitStart,Clock::now());
                    report["revisit_first_frame_ms"]=revisit.front().frame;report["revisit_first_frame_visible"]=revisit.front().visible;
                    report["revisit_first_frame_ready"]=revisit.front().ready;report["revisit_all_ready_ms"]=readyLatency;report["after_revisit"]=resourceStats(resources.stats());
                    report["previews_after_revisit"]=EditorPerformanceProbe::previews(editor);
                    report["thumbnail_resident_bytes"]=renderer.thumbnailResidentBytes();report["resource_status"]=runtime.status();
                    runtime.stop();renderer.drainPending(frame);
                }
                tools.shutdown();
                for(unsigned i=0;i<8&&tools.hasPendingRetirements();++i){auto f=renderer.advance_frame();tools.postFrame(f);}
            }
            ImGui_ImplSDL3_Shutdown();ImPlot::DestroyContext();ImGui::DestroyContext();
            report["render_error"]=diagnostics.hadRenderError();
        }
        SDL_Quit();std::filesystem::create_directories(output.parent_path());std::ofstream(output)<<report.dump(2);
        std::cout<<report.dump(2)<<std::endl;return report.value("render_error",false)?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
