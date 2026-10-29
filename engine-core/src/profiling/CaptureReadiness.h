#pragma once

#include "CaptureController.h"

namespace crimson::profiling::detail {

// Observe ordinary submissions only. The tail lets delayed GPU timestamps
// arrive while the startup collector remains connected; it schedules no work.
struct StartupReadiness {
    static constexpr std::uint32_t requiredTailFrames=8;
    bool reached{};
    std::uint32_t tailFrames{};
    std::uint64_t lastSubmittedFrame{};
    CaptureFrame readyFrame;

    bool observe(const CaptureFrame& frame) noexcept {
        if(!frame.materialsReady) {reached=false;tailFrames=0;return false;}
        const bool submitted=frame.bgfxFrame>0&&((frame.sceneWidth&&frame.sceneHeight)||(frame.gameWidth&&frame.gameHeight));
        if(!submitted)return false;
        if(!reached) {reached=true;readyFrame=frame;lastSubmittedFrame=frame.bgfxFrame;return false;}
        if(frame.bgfxFrame!=lastSubmittedFrame) {
            lastSubmittedFrame=frame.bgfxFrame;
            if(tailFrames<requiredTailFrames)++tailFrames;
        }
        return complete();
    }
    bool complete() const noexcept{return reached&&tailFrames==requiredTailFrames;}
};

} // namespace crimson::profiling::detail
