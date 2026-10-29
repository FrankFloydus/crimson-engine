#pragma once
#include "editor/FrameMetrics.h"
#include <chrono>
#include <deque>
#include <filesystem>
#include <cstdint>

namespace crimson::profiling {
// Opt-in user-driven frame recording; no desktop input, captures or UI changes.
// The report is updated once per second and contains bounded recent samples.
class FrameProfile final {
public:
    explicit FrameProfile(std::filesystem::path output):output_(std::move(output)){}
    ~FrameProfile(){flush();}
    void record(FrameTiming,bool navigating,std::uint32_t width,std::uint32_t height);
private:
    std::filesystem::path output_;
    std::deque<FrameTiming> frames_,navigation_;
    std::uint64_t totalFrames_{},totalNavigation_{};
    std::uint32_t width_{},height_{};
    std::chrono::steady_clock::time_point written_{std::chrono::steady_clock::now()};
    void flush() noexcept;
};
}
