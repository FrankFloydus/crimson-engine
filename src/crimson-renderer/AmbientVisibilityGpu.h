#pragma once

#include "ClusteredLightingGpu.h"
#include "LightingSettings.h"

#include <functional>

namespace crimson {
// Per-view, optional ambient visibility. Programs are renderer-owned. Scratch
// and packed atlas storage never alias during evaluation/publication.
class AmbientVisibilityGpu final {
public:
    AmbientVisibilityGpu(bgfx::ProgramHandle evaluateProgram,bgfx::ProgramHandle publishProgram);
    ~AmbientVisibilityGpu();
    AmbientVisibilityGpu(const AmbientVisibilityGpu&)=delete;
    AmbientVisibilityGpu& operator=(const AmbientVisibilityGpu&)=delete;
    void setPrograms(bgfx::ProgramHandle evaluateProgram,bgfx::ProgramHandle publishProgram);
    void advance(std::uint64_t frame);
    bool prepare(bgfx::TextureHandle depth,bgfx::TextureHandle receiverScope,const Camera& camera,
        std::uint16_t width,std::uint16_t height,ClusteredLightingGpu& clustered,
        const AmbientVisibilitySettings& settings,bgfx::ViewId view,const std::function<void()>& bindShadowAndDirectional);
    void bind() const;
    void bindDisabled() const;
    std::size_t residentBytes() const;
    const std::string& status() const{return status_;}
private:
    struct Retired {bgfx::TextureHandle texture;std::size_t bytes;std::uint64_t after;};
    bgfx::ProgramHandle evaluateProgram_,publishProgram_;
    bgfx::TextureHandle scratch_{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle depthSampler_{BGFX_INVALID_HANDLE},scopeSampler_{BGFX_INVALID_HANDLE},scratchSampler_{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle settingsUniform_{BGFX_INVALID_HANDLE},transitionUniform_{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle inverseUniform_{BGFX_INVALID_HANDLE},viewUniform_{BGFX_INVALID_HANDLE};
    Vec4 settingsValues_{},transitionValues_{};
    std::vector<Retired> retired_;
    std::string status_;
    std::uint16_t width_{},height_{};
    std::uint64_t frame_{};
    bool usable_{};
    void ensureScratch(std::uint16_t width,std::uint16_t height);
};
} // namespace crimson
