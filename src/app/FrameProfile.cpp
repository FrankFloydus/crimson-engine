#include "FrameProfile.h"
#include "assets/AtomicFile.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <vector>

namespace crimson {
namespace {
nlohmann::json summarize(const std::deque<FrameTiming>& frames) {
    if(frames.empty())return {{"samples",0}};
    std::vector<double> frame,cpu;double total{},work{};
    for(const auto& value:frames){frame.push_back(value.frameMilliseconds);cpu.push_back(value.cpuWorkMilliseconds);total+=value.frameMilliseconds;work+=value.cpuWorkMilliseconds;}
    std::sort(frame.begin(),frame.end());std::sort(cpu.begin(),cpu.end());
    const auto p95=std::min(frame.size()-1,frame.size()*95/100);
    return {{"samples",frames.size()},{"fps",1000.0*frames.size()/total},{"frame_mean_ms",total/frames.size()},
        {"frame_p50_ms",frame[frame.size()/2]},{"frame_p95_ms",frame[p95]},{"frame_max_ms",frame.back()},
        {"cpu_mean_ms",work/frames.size()},{"cpu_p95_ms",cpu[p95]},{"frames_over_8_33ms",std::count_if(frame.begin(),frame.end(),[](double t){return t>1000.0/120.0;})}};
}
}
void FrameProfile::record(FrameTiming frame,bool navigating,std::uint32_t width,std::uint32_t height) {
    if(!std::isfinite(frame.frameMilliseconds)||frame.frameMilliseconds<=0||!std::isfinite(frame.cpuWorkMilliseconds)||frame.cpuWorkMilliseconds<0)return;
    const auto append=[&](auto& list){list.push_back(frame);if(list.size()>4096)list.pop_front();};
    append(frames_);++totalFrames_;if(navigating){append(navigation_);++totalNavigation_;}
    width_=width;height_=height;
    if(std::chrono::steady_clock::now()-written_>=std::chrono::seconds{1})flush();
}
void FrameProfile::flush() noexcept {
    if(frames_.empty())return;
    try {
        if(!output_.parent_path().empty())std::filesystem::create_directories(output_.parent_path());
        const nlohmann::json report{{"scope","Visible production application frames; navigation samples require an active user orbit, pan or fly gesture; presentation wait included"},
            {"total_frames",totalFrames_},{"total_navigation_frames",totalNavigation_},{"size",{width_,height_}},
            {"recent",summarize(frames_)},{"navigation",summarize(navigation_)}};
        writeFileAtomically(output_,report.dump(2)+"\n");
    }catch(const std::exception&) { /* Optional diagnostics cannot interrupt an authored session. */ }
    written_=std::chrono::steady_clock::now();
}
}
