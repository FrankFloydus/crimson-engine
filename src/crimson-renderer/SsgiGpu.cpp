#include "SsgiGpu.h"

#include <stdexcept>

namespace crimson {
namespace {
template<class Handle> void destroyHandle(Handle& handle) {
    if(bgfx::isValid(handle))bgfx::destroy(handle);handle=BGFX_INVALID_HANDLE;
}
constexpr auto Sampling=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
constexpr std::array<const char*,8> SamplerNames{"s_ssgi0","s_ssgi1","s_ssgi2","s_ssgi3","s_ssgi4","s_ssgi5","s_ssgi6","s_ssgi7"};
}
struct SsgiGpu::Targets {
    using RadiancePair=std::array<bgfx::TextureHandle,2>;
    std::array<bgfx::TextureHandle,3> capture{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
    RadiancePair trace{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
    std::array<RadiancePair,2> history{{RadiancePair{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}},RadiancePair{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}}}};
    RadiancePair metadata{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
    RadiancePair filter{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
    std::uint16_t width{},height{},halfWidth{},halfHeight{};
    std::size_t bytes{};
    ~Targets(){for(auto& h:capture)destroyHandle(h);for(auto& h:trace)destroyHandle(h);
        for(auto& pair:history)for(auto& h:pair)destroyHandle(h);for(auto& h:metadata)destroyHandle(h);for(auto& h:filter)destroyHandle(h);}
    void create(std::uint16_t w,std::uint16_t h) {
        width=w;height=h;halfWidth=ssgi::halfExtent(w);halfHeight=ssgi::halfExtent(h);
        const auto texture=[&](std::uint16_t tw,std::uint16_t th,bgfx::TextureFormat::Enum format,std::uint64_t flags,const char* name) {
            if(!bgfx::isTextureValid(0,false,1,format,flags))throw std::runtime_error("SSGI texture format is unsupported.");
            auto handle=bgfx::createTexture2D(tw,th,false,1,format,flags);
            if(!bgfx::isValid(handle))throw std::runtime_error("SSGI texture allocation failed.");
            bgfx::setName(handle,name);bytes+=std::size_t(tw)*th*(format==bgfx::TextureFormat::RGBA32F?16:8);return handle;
        };
        for(unsigned i=0;i<3;++i)capture[i]=texture(w,h,i==2?bgfx::TextureFormat::RGBA32F:bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_RT|Sampling,"SSGI captured diffuse/geometry");
        for(auto& handle:trace)handle=texture(halfWidth,halfHeight,bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_COMPUTE_WRITE|Sampling,"SSGI traced/filtered irradiance");
        for(auto& pair:history)for(auto& handle:pair)handle=texture(halfWidth,halfHeight,bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_COMPUTE_WRITE|Sampling,"SSGI temporal irradiance");
        for(auto& handle:metadata)handle=texture(halfWidth,halfHeight,bgfx::TextureFormat::RGBA32F,BGFX_TEXTURE_COMPUTE_WRITE|Sampling,"SSGI temporal geometry");
        for(auto& handle:filter)handle=texture(halfWidth,halfHeight,bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_COMPUTE_WRITE|Sampling,"SSGI spatial irradiance");
    }
};
SsgiGpu::SsgiGpu(bgfx::ProgramHandle trace,bgfx::ProgramHandle temporal,bgfx::ProgramHandle filter,bgfx::ProgramHandle publish)
    :programs_{trace,temporal,filter,publish} {
    for(unsigned i=0;i<samplers_.size();++i)samplers_[i]=bgfx::createUniform(SamplerNames[i],bgfx::UniformType::Sampler);
    view_=bgfx::createUniform("u_ssgiView",bgfx::UniformType::Vec4);resolution_=bgfx::createUniform("u_ssgiResolution",bgfx::UniformType::Vec4);
    traceSettings_=bgfx::createUniform("u_ssgiTraceSettings",bgfx::UniformType::Vec4);state_=bgfx::createUniform("u_ssgiState",bgfx::UniformType::Vec4);
    tuning_=bgfx::createUniform("u_ssgiTuning",bgfx::UniformType::Vec4);
    filterAxis_=bgfx::createUniform("u_ssgiFilterAxis",bgfx::UniformType::Vec4);atlas_=bgfx::createUniform("u_ssgiPublishAtlas",bgfx::UniformType::Vec4);
    inverse_=bgfx::createUniform("u_ssgiInverseViewProjection",bgfx::UniformType::Mat4);projection_=bgfx::createUniform("u_ssgiViewProjection",bgfx::UniformType::Mat4);
    previousProjection_=bgfx::createUniform("u_ssgiPreviousViewProjection",bgfx::UniformType::Mat4);previousInverse_=bgfx::createUniform("u_ssgiPreviousInverseViewProjection",bgfx::UniformType::Mat4);
}
SsgiGpu::~SsgiGpu() {
    targets_.reset();retired_.clear();for(auto& handle:samplers_)destroyHandle(handle);
    for(auto* handle:{&view_,&resolution_,&traceSettings_,&tuning_,&state_,&filterAxis_,&atlas_,&inverse_,&projection_,&previousProjection_,&previousInverse_})destroyHandle(*handle);
}
void SsgiGpu::invalidate() noexcept {sourceValid_=false;historyValid_=false;capturePending_=false;sourceFrame_=UINT64_MAX;}
void SsgiGpu::setPrograms(bgfx::ProgramHandle trace,bgfx::ProgramHandle temporal,bgfx::ProgramHandle filter,bgfx::ProgramHandle publish) {
    const std::array next{trace,temporal,filter,publish};
    for(unsigned i=0;i<next.size();++i)if(next[i].idx!=programs_[i].idx){invalidate();break;}programs_=next;
}
void SsgiGpu::advance(std::uint64_t frame) {
    frame_=frame;std::erase_if(retired_,[&](const auto& old){return old.after<=frame;});
}
bool SsgiGpu::ensure(std::uint16_t width,std::uint16_t height) {
    status_.clear();
    try {
        const auto* caps=bgfx::getCaps();
        if(!caps||!(caps->supported&BGFX_CAPS_COMPUTE)||caps->limits.maxFBAttachments<5)
            throw std::runtime_error("SSGI requires compute, four color attachments and a depth attachment.");
        if(!width||!height||width>caps->limits.maxTextureSize||height>caps->limits.maxTextureSize)
            throw std::runtime_error("SSGI viewport dimensions are unsupported.");
        if(targets_&&targets_->width==width&&targets_->height==height)return true;
        auto next=std::make_unique<Targets>();next->create(width,height);
        if(targets_){retired_.reserve(retired_.size()+1);retired_.push_back({std::move(targets_),frame_+3});}targets_=std::move(next);invalidate();return true;
    }catch(const std::exception& error){invalidate();status_=error.what();return false;}
}
const std::array<bgfx::TextureHandle,3>& SsgiGpu::captureTextures() const {
    static const std::array<bgfx::TextureHandle,3> empty{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
    return targets_?targets_->capture:empty;
}
void SsgiGpu::disable() {
    invalidate();status_.clear();if(targets_){retired_.reserve(retired_.size()+1);retired_.push_back({std::move(targets_),frame_+3});}
}
std::size_t SsgiGpu::residentBytes() const {
    std::size_t bytes=targets_?targets_->bytes:0;for(const auto& old:retired_)bytes+=old.targets->bytes;return bytes;
}
bool SsgiGpu::prepare(bgfx::TextureHandle depth,bgfx::TextureHandle receiverScope,const Camera& camera,const SsgiSettings& settings,
    bgfx::ViewId view,std::uint64_t worldId,std::uint64_t cameraId,std::uint64_t environmentId,bgfx::TextureHandle atlas,std::uint16_t atlasWidth,std::uint32_t atlasBase) {
    capturePending_=false;status_.clear();
    try {
        if(!effectiveSsgi(settings)){disable();return true;}
        if(!validSsgiSettings(settings)||!targets_||!bgfx::isValid(depth)||!bgfx::isValid(receiverScope)||!bgfx::isValid(atlas)||!atlasWidth)
            throw std::runtime_error("SSGI inputs or packed atlas region are unavailable.");
        for(const auto program:programs_)if(!bgfx::isValid(program))throw std::runtime_error("SSGI compute programs are unavailable.");
        const auto* caps=bgfx::getCaps();if(!caps||view>=caps->limits.maxViews)throw std::runtime_error("SSGI compute view is unavailable.");
        const auto& t=*targets_;const auto convention=caps->homogeneousDepth?ClipDepth::NegativeOneToOne:ClipDepth::ZeroToOne;
        pendingProjection_=camera.projectionMatrix(float(t.width)/t.height,convention)*camera.viewMatrix();
        const auto inverse=math::inverse(pendingProjection_);if(!inverse||!math::finite(*inverse))throw std::runtime_error("SSGI camera projection is singular.");pendingInverse_=*inverse;
        pendingIdentity_={worldId,cameraId,environmentId};pendingSettings_=settings;pendingCamera_=camera;
        const bool sourceUsable=sourceValid_&&ssgi::reusableHistory(sourceFrame_,frame_,sourceIdentity_,pendingIdentity_,sourceCamera_,camera,sourceSettings_,settings);
        if(!sourceUsable)historyValid_=false;
        const auto budget=ssgiQualityBudget(settings);const unsigned next=historyIndex_^1;
        const Vec4 viewValues{float(t.width),float(t.height),caps->homogeneousDepth?1.f:0.f,caps->originBottomLeft?1.f:0.f};
        const Vec4 resolution{float(t.halfWidth),float(t.halfHeight),float(budget.steps),float(frame_%4096)};
        const Vec4 traceValues{settings.maxDistance,settings.thickness,settings.temporalStability,float(budget.rays)};
        const Vec4 tuning{settings.rayBias,float(settings.denoiseRadius),settings.denoiseStrength,settings.radianceClamp};
        const Vec4 state{sourceUsable?1.f:0.f,historyValid_?1.f:0.f,float(settings.bounces),0};
        const Vec4 atlasValues{float(atlasBase),float(atlasWidth),0,0};
        const auto uniforms=[&]{bgfx::setUniform(view_,viewValues.data());bgfx::setUniform(resolution_,resolution.data());bgfx::setUniform(traceSettings_,traceValues.data());bgfx::setUniform(tuning_,tuning.data());
            bgfx::setUniform(state_,state.data());bgfx::setUniform(inverse_,pendingInverse_.data());bgfx::setUniform(projection_,pendingProjection_.data());
            bgfx::setUniform(previousProjection_,sourceProjection_.data());bgfx::setUniform(previousInverse_,sourceInverse_.data());};
        const auto texture=[&](std::uint8_t stage,bgfx::TextureHandle h){bgfx::setTexture(stage,samplers_[stage],h,static_cast<std::uint32_t>(Sampling));};
        const auto output=[&](std::uint8_t stage,bgfx::TextureHandle h,bgfx::TextureFormat::Enum format=bgfx::TextureFormat::RGBA16F){bgfx::setImage(stage,h,0,bgfx::Access::Write,format);};
        const auto dispatch=[&](bgfx::ProgramHandle program){bgfx::dispatch(view,program,(std::uint32_t(t.halfWidth)+7)/8,(std::uint32_t(t.halfHeight)+7)/8);};
        bgfx::setViewMode(view,bgfx::ViewMode::Sequential);bgfx::setViewFrameBuffer(view,BGFX_INVALID_HANDLE);bgfx::setViewClear(view,BGFX_CLEAR_NONE);
        uniforms();texture(0,depth);texture(1,receiverScope);for(unsigned i=0;i<3;++i)texture(static_cast<std::uint8_t>(i+2),t.capture[i]);
        output(5,t.trace[0]);output(6,t.trace[1]);output(7,t.metadata[next],bgfx::TextureFormat::RGBA32F);dispatch(programs_[0]);
        uniforms();texture(0,t.trace[0]);texture(1,t.trace[1]);texture(2,t.metadata[next]);texture(3,t.history[historyIndex_][0]);texture(4,t.history[historyIndex_][1]);texture(5,t.metadata[historyIndex_]);
        output(6,t.history[next][0]);output(7,t.history[next][1]);dispatch(programs_[1]);
        for(unsigned axis=0;axis<2;++axis){uniforms();const Vec4 direction{axis==0?1.f:0.f,axis==1?1.f:0.f,0,0};bgfx::setUniform(filterAxis_,direction.data());
            const auto& input=axis==0?t.history[next]:t.filter;const auto& destination=axis==0?t.filter:t.trace;
            texture(0,input[0]);texture(1,input[1]);texture(2,t.metadata[next]);output(3,destination[0]);output(4,destination[1]);dispatch(programs_[2]);}
        uniforms();bgfx::setUniform(atlas_,atlasValues.data());texture(0,t.trace[0]);texture(1,t.trace[1]);texture(2,t.metadata[next]);texture(3,depth);texture(4,receiverScope);
        output(5,atlas,bgfx::TextureFormat::RGBA32F);bgfx::dispatch(view,programs_[3],(std::uint32_t(t.width)+7)/8,(std::uint32_t(t.height)+7)/8);
        historyIndex_=next;historyValid_=true;capturePending_=true;return true;
    }catch(const std::exception& error){invalidate();status_=error.what();return false;}
}
void SsgiGpu::finishCapture() {
    if(!capturePending_)return;sourceCamera_=pendingCamera_;sourceSettings_=pendingSettings_;sourceIdentity_=pendingIdentity_;
    sourceProjection_=pendingProjection_;sourceInverse_=pendingInverse_;sourceFrame_=frame_;sourceValid_=true;capturePending_=false;
}
}
