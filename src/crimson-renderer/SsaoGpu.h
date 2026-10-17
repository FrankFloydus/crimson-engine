#pragma once

#include "ClusteredLightingGpu.h"
#include "Ssao.h"

namespace crimson {
// A view owns its half-resolution current-frame AO scratch. The final scalar
// image shares Forward+'s existing material sampler and packed lighting atlas.
class SsaoGpu final {
public:
    SsaoGpu(bgfx::ProgramHandle evaluate,bgfx::ProgramHandle publish);
    ~SsaoGpu();
    SsaoGpu(const SsaoGpu&)=delete;
    SsaoGpu& operator=(const SsaoGpu&)=delete;
    void setPrograms(bgfx::ProgramHandle evaluate,bgfx::ProgramHandle publish);
    void advance(std::uint64_t frame);
    void disable();
    bool prepare(bgfx::TextureHandle depth,bgfx::TextureHandle receiver,const Camera&,
        std::uint16_t width,std::uint16_t height,ClusteredLightingGpu&,const SsaoSettings&,bgfx::ViewId);
    std::size_t residentBytes() const;
    const std::string& status() const {return status_;}
private:
    struct Retired {bgfx::TextureHandle texture;std::size_t bytes;std::uint64_t after;};
    bgfx::ProgramHandle evaluate_,publish_;
    bgfx::TextureHandle scratch_{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle depth_{BGFX_INVALID_HANDLE},receiver_{BGFX_INVALID_HANDLE},scratchSampler_{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle view_{BGFX_INVALID_HANDLE},size_{BGFX_INVALID_HANDLE},parameters_{BGFX_INVALID_HANDLE},projection_{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle inverse_{BGFX_INVALID_HANDLE},viewMatrix_{BGFX_INVALID_HANDLE},atlas_{BGFX_INVALID_HANDLE};
    std::vector<Retired> retired_;
    std::uint16_t width_{},height_{};
    std::uint64_t frame_{};
    std::string status_;
    void ensureScratch(std::uint16_t,std::uint16_t);
    void destroyUniforms();
};
}
