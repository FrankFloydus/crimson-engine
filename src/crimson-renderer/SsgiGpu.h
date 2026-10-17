#pragma once

#include "SsgiHistory.h"
#include <bgfx/bgfx.h>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace crimson {
// One owner per Scene/Game target. Programs are borrowed. Captured lighting is
// previous-frame direct diffuse/emission, so traced GI never feeds itself.
class SsgiGpu final {
public:
    SsgiGpu(bgfx::ProgramHandle trace,bgfx::ProgramHandle temporal,bgfx::ProgramHandle filter,bgfx::ProgramHandle publish);
    ~SsgiGpu();
    SsgiGpu(const SsgiGpu&)=delete;
    SsgiGpu& operator=(const SsgiGpu&)=delete;
    void setPrograms(bgfx::ProgramHandle trace,bgfx::ProgramHandle temporal,bgfx::ProgramHandle filter,bgfx::ProgramHandle publish);
    void advance(std::uint64_t frame);
    bool ensure(std::uint16_t width,std::uint16_t height);
    const std::array<bgfx::TextureHandle,3>& captureTextures() const;
    bool prepare(bgfx::TextureHandle depth,bgfx::TextureHandle receiverScope,const Camera&,const SsgiSettings&,
        bgfx::ViewId,std::uint64_t worldId,std::uint64_t cameraId,std::uint64_t environmentId,
        bgfx::TextureHandle atlas,std::uint16_t atlasWidth,std::uint32_t atlasBase);
    // Call only after the matching scene-color draws were actually submitted.
    void finishCapture();
    void invalidate() noexcept;
    void disable();
    std::size_t residentBytes() const;
    const std::string& status() const {return status_;}
private:
    struct Targets;
    struct Retired {std::unique_ptr<Targets> targets;std::uint64_t after;};
    std::unique_ptr<Targets> targets_;
    std::vector<Retired> retired_;
    std::array<bgfx::ProgramHandle,4> programs_;
    std::array<bgfx::UniformHandle,8> samplers_;
    bgfx::UniformHandle view_,resolution_,traceSettings_,tuning_,state_,filterAxis_,atlas_,inverse_,projection_,previousProjection_,previousInverse_;
    Camera sourceCamera_,pendingCamera_;
    SsgiSettings sourceSettings_,pendingSettings_;
    ssgi::HistoryIdentity sourceIdentity_,pendingIdentity_;
    Mat4 sourceProjection_,sourceInverse_,pendingProjection_,pendingInverse_;
    std::uint64_t frame_{},sourceFrame_{UINT64_MAX};
    unsigned historyIndex_{};
    bool sourceValid_{},historyValid_{},capturePending_{};
    std::string status_;
};
}
