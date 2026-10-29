#pragma once
#include <cmath>
#include <cstdint>
#include <optional>

namespace crimson {
// Completed application-frame timing supplied by the composition boundary.
// CPU work excludes the application's explicit bgfx frame/presentation wait.
struct FrameTiming {
    double frameMilliseconds{},cpuWorkMilliseconds{};
};
struct DeviceMemoryMetrics {std::uint64_t used{},available{};};
inline std::optional<DeviceMemoryMetrics> validDeviceMemory(std::int64_t used,std::int64_t available) {
    if(used<0||available<=0||used>available)return {};
    return DeviceMemoryMetrics{static_cast<std::uint64_t>(used),static_cast<std::uint64_t>(available)};
}
inline std::optional<double> gpuFrameMilliseconds(std::int64_t begin,std::int64_t end,std::int64_t frequency) {
    if(begin<0||end<begin||frequency<=0)return {};
    const auto value=(static_cast<double>(end)-static_cast<double>(begin))*1000.0/static_cast<double>(frequency);
    return std::isfinite(value)?std::optional{value}:std::nullopt;
}
struct FrameMetricsSample {
    FrameTiming timing;
    std::optional<double> gpuMilliseconds;
    std::optional<DeviceMemoryMetrics> deviceMemory;
    std::uint64_t textureAndProgramBytes{},modelBytes{},shadowBytes{},draws{};
};
struct FrameMetricsReadout : FrameMetricsSample {double fps{};};
class FrameMetricsSampler {
public:
    bool sample(const FrameMetricsSample& value) {
        if(!std::isfinite(value.timing.frameMilliseconds)||value.timing.frameMilliseconds<=0||
           !std::isfinite(value.timing.cpuWorkMilliseconds)||value.timing.cpuWorkMilliseconds<0)return false;
        frameMilliseconds_+=value.timing.frameMilliseconds;cpuMilliseconds_+=value.timing.cpuWorkMilliseconds;++count_;
        if(value.gpuMilliseconds&&std::isfinite(*value.gpuMilliseconds)&&*value.gpuMilliseconds>=0){gpuMilliseconds_+=*value.gpuMilliseconds;++gpuCount_;}
        if(ready_&&frameMilliseconds_<250.0)return false;
        static_cast<FrameMetricsSample&>(readout_)=value;
        readout_.timing={frameMilliseconds_/count_,cpuMilliseconds_/count_};readout_.fps=1000.0/readout_.timing.frameMilliseconds;
        readout_.gpuMilliseconds=gpuCount_?std::optional{gpuMilliseconds_/gpuCount_}:std::nullopt;
        ready_=true;frameMilliseconds_=cpuMilliseconds_=gpuMilliseconds_=0;count_=gpuCount_=0;return true;
    }
    void reset(){*this={};}
    bool ready() const noexcept{return ready_;}
    const FrameMetricsReadout& readout() const noexcept{return readout_;}
private:
    FrameMetricsReadout readout_;
    double frameMilliseconds_{},cpuMilliseconds_{},gpuMilliseconds_{};
    std::uint32_t count_{},gpuCount_{};
    bool ready_{};
};
}
