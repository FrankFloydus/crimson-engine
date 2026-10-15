#include "AmbientVisibilityGpu.h"

#include <algorithm>
#include <stdexcept>

namespace crimson {
namespace {
template<class Handle> void destroyHandle(Handle& handle) {
    if(bgfx::isValid(handle))bgfx::destroy(handle);handle=BGFX_INVALID_HANDLE;
}
constexpr auto Sampling=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
}
AmbientVisibilityGpu::AmbientVisibilityGpu(bgfx::ProgramHandle evaluateProgram,bgfx::ProgramHandle publishProgram)
    :evaluateProgram_(evaluateProgram),publishProgram_(publishProgram) {
    depthSampler_=bgfx::createUniform("s_ambientDepth",bgfx::UniformType::Sampler);
    scopeSampler_=bgfx::createUniform("s_ambientScope",bgfx::UniformType::Sampler);
    scratchSampler_=bgfx::createUniform("s_ambientScratch",bgfx::UniformType::Sampler);
    settingsUniform_=bgfx::createUniform("u_ambientSettings",bgfx::UniformType::Vec4);
    transitionUniform_=bgfx::createUniform("u_ambientTransition",bgfx::UniformType::Vec4);
    inverseUniform_=bgfx::createUniform("u_ambientInverseViewProjection",bgfx::UniformType::Mat4);
    viewUniform_=bgfx::createUniform("u_ambientView",bgfx::UniformType::Vec4);
}
AmbientVisibilityGpu::~AmbientVisibilityGpu() {
    destroyHandle(scratch_);for(auto& old:retired_)destroyHandle(old.texture);
    destroyHandle(depthSampler_);destroyHandle(scopeSampler_);destroyHandle(scratchSampler_);
    destroyHandle(settingsUniform_);destroyHandle(transitionUniform_);destroyHandle(inverseUniform_);destroyHandle(viewUniform_);
}
void AmbientVisibilityGpu::setPrograms(bgfx::ProgramHandle evaluateProgram,bgfx::ProgramHandle publishProgram) {
    evaluateProgram_=evaluateProgram;publishProgram_=publishProgram;
}
void AmbientVisibilityGpu::advance(std::uint64_t frame) {
    frame_=frame;
    std::erase_if(retired_,[&](auto& old){if(old.after>frame)return false;destroyHandle(old.texture);return true;});
}
std::size_t AmbientVisibilityGpu::residentBytes() const {
    std::size_t bytes=bgfx::isValid(scratch_)?std::size_t(width_)*height_*sizeof(float):0;
    for(const auto& old:retired_)bytes+=old.bytes;return bytes;
}
void AmbientVisibilityGpu::ensureScratch(std::uint16_t width,std::uint16_t height) {
    if(bgfx::isValid(scratch_) && width_==width && height_==height)return;
    constexpr auto flags=BGFX_TEXTURE_COMPUTE_WRITE|Sampling;
    if(!bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::R32F,flags))
        throw std::runtime_error("Ambient visibility requires an R32F compute-write texture.");
    auto next=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::R32F,flags);
    if(!bgfx::isValid(next))throw std::runtime_error("Ambient visibility scratch allocation failed.");
    if(bgfx::isValid(scratch_)) {
        try{retired_.push_back({scratch_,std::size_t(width_)*height_*sizeof(float),frame_+2});}
        catch(...){destroyHandle(next);throw;}
    }
    scratch_=next;width_=width;height_=height;bgfx::setName(scratch_,"Ambient visibility evaluation");
}
bool AmbientVisibilityGpu::prepare(bgfx::TextureHandle depth,bgfx::TextureHandle receiverScope,const Camera& camera,
    std::uint16_t width,std::uint16_t height,ClusteredLightingGpu& clustered,const AmbientVisibilitySettings& settings,
    bgfx::ViewId view,const std::function<void()>& bindShadowAndDirectional) {
    usable_=false;status_.clear();clustered.setAmbientVisibilityReady(false);
    settingsValues_={};transitionValues_={};
    if(!settings.enabled)return true;
    try {
        if(!validAmbientVisibilitySettings(settings))throw std::runtime_error("Invalid ambient visibility settings.");
        if(!width || !height || clustered.ambientWidth()!=width || clustered.ambientHeight()!=height ||
            !bgfx::isValid(depth) || !bgfx::isValid(receiverScope) || !bgfx::isValid(clustered.texture()))
            throw std::runtime_error("Ambient visibility depth, receiver scopes or packed atlas region is unavailable.");
        if(!bgfx::isValid(evaluateProgram_) || !bgfx::isValid(publishProgram_))
            throw std::runtime_error("Ambient visibility compute programs are unavailable.");
        const auto* caps=bgfx::getCaps();
        if(!caps || view>=caps->limits.maxViews)throw std::runtime_error("Ambient visibility view is unavailable.");
        bgfx::setViewMode(view,bgfx::ViewMode::Sequential);
        // Detach the readable prepass DSV before exposing its SRV to compute.
        bgfx::setViewFrameBuffer(view,BGFX_INVALID_HANDLE);
        bgfx::setViewClear(view,BGFX_CLEAR_NONE);
        const auto inverse=math::inverse(camera.projectionMatrix(float(width)/height,caps->homogeneousDepth?ClipDepth::NegativeOneToOne:ClipDepth::ZeroToOne)*camera.viewMatrix());
        if(!inverse || !math::finite(*inverse))throw std::runtime_error("Ambient visibility camera projection is singular.");
        ensureScratch(width,height);
        settingsValues_={1,settings.strength,settings.darknessFloor,settings.shadowAware?1.f:0.f};
        transitionValues_={settings.threshold,settings.transition,0,0};
        const Vec4 viewValues{float(width),float(height),caps->homogeneousDepth?1.f:0.f,caps->originBottomLeft?1.f:0.f};
        bindShadowAndDirectional();clustered.bind(2);bind();
        bgfx::setUniform(inverseUniform_,inverse->data());bgfx::setUniform(viewUniform_,viewValues.data());
        bgfx::setTexture(0,depthSampler_,depth,static_cast<std::uint32_t>(Sampling));
        bgfx::setTexture(1,scopeSampler_,receiverScope,static_cast<std::uint32_t>(Sampling));
        bgfx::setImage(4,scratch_,0,bgfx::Access::Write,bgfx::TextureFormat::R32F);
        bgfx::dispatch(view,evaluateProgram_,(std::uint32_t(width)+7)/8,(std::uint32_t(height)+7)/8,1);
        // End all atlas reads before publishing into its disjoint tail region.
        clustered.setAmbientVisibilityReady(true);clustered.bindUniforms();
        bgfx::setTexture(0,scratchSampler_,scratch_,static_cast<std::uint32_t>(Sampling));
        bgfx::setImage(1,clustered.texture(),0,bgfx::Access::Write,bgfx::TextureFormat::RGBA32F);
        bgfx::dispatch(view,publishProgram_,(std::uint32_t(width)+31)/32,(std::uint32_t(height)+7)/8,1);
        usable_=true;return true;
    } catch(const std::exception& error) {
        clustered.setAmbientVisibilityReady(false);settingsValues_={};status_=error.what();return false;
    }
}
void AmbientVisibilityGpu::bind() const {
    bgfx::setUniform(settingsUniform_,settingsValues_.data());bgfx::setUniform(transitionUniform_,transitionValues_.data());
}
void AmbientVisibilityGpu::bindDisabled() const {
    const Vec4 zero{};bgfx::setUniform(settingsUniform_,zero.data());bgfx::setUniform(transitionUniform_,zero.data());
}
} // namespace crimson
