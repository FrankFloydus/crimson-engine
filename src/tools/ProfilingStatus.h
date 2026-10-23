#pragma once
#include "profiling/CaptureController.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

namespace crimson {
inline std::string profilingCaptureTime(double elapsedSeconds) {
    const auto seconds=static_cast<std::uint64_t>(std::max(0.0,elapsedSeconds));
    char text[40]{};
    std::snprintf(text,sizeof(text),"%02llu:%02llu",static_cast<unsigned long long>(seconds/60),
        static_cast<unsigned long long>(seconds%60));
    return text;
}

inline std::string profilingStatusLabel(const profiling::CaptureStatus& status) {
    switch(status.state) {
    case profiling::CaptureState::Idle:return {};
    case profiling::CaptureState::Connecting:return "Connecting...";
    case profiling::CaptureState::Capturing: {
        auto text=std::string(status.kind==profiling::CaptureKind::Editor?"Recording editor ":"Recording ")+profilingCaptureTime(status.elapsedSeconds);
        if(status.durationSeconds)text+=" / "+profilingCaptureTime(status.durationSeconds);
        return text;
    }
    case profiling::CaptureState::Saving:return "Saving...";
    case profiling::CaptureState::Saved:return "Profile saved";
    case profiling::CaptureState::Failed:return "Capture failed";
    }
    return "Capture failed";
}
}
