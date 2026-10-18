#include "editor/FrameMetrics.h"
#include "app/FrameProfile.h"
#include "tools/GameViewInteraction.h"
#include <imgui_internal.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {
using namespace crimson;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void recordedNavigation() {
    const auto path=std::filesystem::temp_directory_path()/("crimson-frame-profile-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".json");
    {
        FrameProfile profile(path);
        profile.record({std::numeric_limits<double>::quiet_NaN(),1},true,1920,1080);
        for(unsigned i=0;i<5000;++i)profile.record({5,2},i%2==0,1920,1080);
    }
    std::ifstream input(path);const auto report=nlohmann::json::parse(input);input.close();std::filesystem::remove(path);
    check(report.at("total_frames")==5000&&report.at("total_navigation_frames")==2500,"Frame recorder mixed idle and user navigation frames.");
    check(report.at("recent").at("samples")==4096&&report.at("navigation").at("samples")==2500,"Frame recorder did not bound recent samples.");
    check(report.at("recent").at("fps")==200.0&&report.at("navigation").at("frame_p95_ms")==5.0&&report.at("navigation").at("cpu_p95_ms")==2.0,
        "Recorded FPS/percentiles do not match completed frame timings.");
    check(report.at("navigation").at("frames_over_8_33ms")==0,"Fast navigation frames were classified below 120 FPS.");
}
void near(double actual,double expected){check(std::abs(actual-expected)<1e-9,"Unexpected metrics conversion or average.");}
void sampling(){
    check(!validDeviceMemory(-1,1024)&&!validDeviceMemory(0,0)&&!validDeviceMemory(1025,1024),"Unavailable or invalid device memory was presented as real VRAM.");
    const auto empty=validDeviceMemory(0,1024);check(empty&&empty->used==0&&empty->available==1024,"Zero reported VRAM usage was mistaken for missing data.");
    check(!gpuFrameMilliseconds(1,0,1000)&&!gpuFrameMilliseconds(0,1,0)&&!gpuFrameMilliseconds(-1,1,1000),"Invalid GPU timer data produced a GPU time.");
    near(*gpuFrameMilliseconds(100,300,1000),200);
    FrameMetricsSampler sampler;FrameMetricsSample sample;sample.timing={10,3};sample.gpuMilliseconds=2.5;
    sample.deviceMemory=validDeviceMemory(1024,4096);sample.textureAndProgramBytes=500;sample.modelBytes=100;sample.shadowBytes=200;sample.draws=8;
    check(sampler.sample(sample)&&sampler.ready(),"First valid frame did not supply an immediate readout.");
    near(sampler.readout().fps,100);near(sampler.readout().timing.cpuWorkMilliseconds,3);near(*sampler.readout().gpuMilliseconds,2.5);
    unsigned publications{};for(unsigned frame=0;frame<100;++frame)publications+=sampler.sample(sample);
    check(publications==4,"Metrics text did not update at four samples per second.");
    auto invalid=sample;invalid.timing.frameMilliseconds=std::numeric_limits<double>::quiet_NaN();
    check(!sampler.sample(invalid)&&std::isfinite(sampler.readout().fps),"A nonfinite frame poisoned the readout.");
    sampler.reset();check(!sampler.ready(),"Starting a new Play run retained its previous sample.");
    sampler.sample(sample);sample.timing={100,5};sample.gpuMilliseconds={};sample.deviceMemory={};
    check(!sampler.sample(sample),"Sampler published before its refresh interval.");
    sample.timing={150,15};sample.draws=12;check(sampler.sample(sample),"Sampler failed to publish a complete refresh interval.");
    near(sampler.readout().fps,8);near(sampler.readout().timing.frameMilliseconds,125);near(sampler.readout().timing.cpuWorkMilliseconds,10);
    check(!sampler.readout().gpuMilliseconds&&!sampler.readout().deviceMemory&&sampler.readout().draws==12,"Unsupported backend data retained a stale available value.");
}
class Ui {
public:
    Ui(){context_=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={900,600};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));tick(false);tick(false);}
    ~Ui(){ImGui::DestroyContext(context_);}
    void mouse(bool down){auto& io=ImGui::GetIO();io.AddMousePosEvent(120,150);io.AddMouseButtonEvent(ImGuiMouseButton_Left,down);tick(false);}
    bool imageHeld() const{return imageHeld_;}
    bool tick(bool down,bool playing=true,bool focus=true,bool text=false,bool ime=false,bool popup=false){
        auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiKey_F1,down);ImGui::NewFrame();
        ImGui::SetNextWindowPos({500,20});ImGui::SetNextWindowSize({300,200});if(!focus)ImGui::SetNextWindowFocus();
        ImGui::Begin("Inspector");ImGui::TextUnformatted("Other editor");ImGui::End();
        ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({440,450});if(focus)ImGui::SetNextWindowFocus();
        ImGui::Begin("Game");drawGameViewImage(ImTextureRef(static_cast<ImTextureID>(1)),{380,340});
        imageHeld_=ImGui::IsItemActive()&&ImGui::IsMouseDown(ImGuiMouseButton_Left);
        io.WantTextInput=text;context_->PlatformImeData.WantTextInput=ime;
        if(popup)ImGui::OpenPopup("Metrics test modal");
        const auto active=context_->ActiveId;const auto toggled=gameMetricsToggleRequested(playing);
        check(context_->ActiveId==active,"Metrics shortcut changed gameplay widget ownership.");
        if(ImGui::BeginPopupModal("Metrics test modal",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextUnformatted("Modal");ImGui::CloseCurrentPopup();ImGui::EndPopup();}
        ImGui::End();ImGui::Render();return toggled;
    }
private:ImGuiContext* context_{};bool imageHeld_{};
};
void shortcut(){
    Ui ui;check(ui.tick(true),"F1 did not toggle metrics in focused Play Game.");
    check(!ui.tick(true),"Holding F1 repeated the toggle.");ui.tick(false);
    check(!ui.tick(true,false),"Metrics shortcut activated outside Play.");ui.tick(false);
    check(!ui.tick(true,true,false),"An unfocused Game consumed F1 from another editor.");ui.tick(false);
    check(!ui.tick(true,true,true,true),"Metrics shortcut consumed text input.");ui.tick(false);
    check(!ui.tick(true,true,true,false,true),"Metrics shortcut consumed current-frame IME input.");ui.tick(false);
    check(!ui.tick(true,true,true,false,false,true),"Metrics shortcut consumed a modal's keyboard input.");ui.tick(false);
    check(ui.tick(true),"Metrics shortcut did not recover after capture ended.");
    ui.tick(false);ui.mouse(true);check(ui.imageHeld(),"Held gameplay image fixture did not acquire its click.");
    check(ui.tick(true)&&ui.imageHeld(),"F1 interrupted an existing gameplay click or failed while it was held.");ui.mouse(false);
}
}
int main(){try{sampling();shortcut();recordedNavigation();std::cout<<"Game metrics cadence, recorded navigation timing and F1 ownership checks passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
