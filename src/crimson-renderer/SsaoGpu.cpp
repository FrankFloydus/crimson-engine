#include "SsaoGpu.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace crimson {
namespace {
template<class T> void destroy(T& value) {if(bgfx::isValid(value))bgfx::destroy(value);value=BGFX_INVALID_HANDLE;}
constexpr auto Sampling=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
}
SsaoGpu::SsaoGpu(bgfx::ProgramHandle evaluate,bgfx::ProgramHandle publish):evaluate_(evaluate),publish_(publish) {
    try {
        depth_=bgfx::createUniform("s_ssaoDepth",bgfx::UniformType::Sampler);
        receiver_=bgfx::createUniform("s_ssaoReceiver",bgfx::UniformType::Sampler);
        scratchSampler_=bgfx::createUniform("s_ssaoScratch",bgfx::UniformType::Sampler);
        view_=bgfx::createUniform("u_ssaoComputeView",bgfx::UniformType::Vec4);
        size_=bgfx::createUniform("u_ssaoComputeSize",bgfx::UniformType::Vec4);
        parameters_=bgfx::createUniform("u_ssaoComputeParameters",bgfx::UniformType::Vec4);
        projection_=bgfx::createUniform("u_ssaoComputeProjection",bgfx::UniformType::Vec4);
        inverse_=bgfx::createUniform("u_ssaoComputeInverseViewProjection",bgfx::UniformType::Mat4);
        viewMatrix_=bgfx::createUniform("u_ssaoComputeViewMatrix",bgfx::UniformType::Mat4);
        atlas_=bgfx::createUniform("u_ssaoPublish",bgfx::UniformType::Vec4);
        for(auto handle:{depth_,receiver_,scratchSampler_,view_,size_,parameters_,projection_,inverse_,viewMatrix_,atlas_})
            if(!bgfx::isValid(handle))throw std::runtime_error("SSAO uniform allocation failed.");
    }catch(...){destroyUniforms();throw;}
}
SsaoGpu::~SsaoGpu(){destroy(scratch_);for(auto& old:retired_)destroy(old.texture);destroyUniforms();}
void SsaoGpu::destroyUniforms(){destroy(depth_);destroy(receiver_);destroy(scratchSampler_);destroy(view_);destroy(size_);destroy(parameters_);destroy(projection_);destroy(inverse_);destroy(viewMatrix_);destroy(atlas_);}
void SsaoGpu::setPrograms(bgfx::ProgramHandle evaluate,bgfx::ProgramHandle publish){evaluate_=evaluate;publish_=publish;}
void SsaoGpu::advance(std::uint64_t frame){frame_=frame;std::erase_if(retired_,[&](auto& old){if(old.after>frame)return false;destroy(old.texture);return true;});}
void SsaoGpu::disable(){
    if(bgfx::isValid(scratch_)){retired_.push_back({scratch_,std::size_t(width_)*height_*16,frame_+3});scratch_=BGFX_INVALID_HANDLE;}
    width_=height_=0;status_.clear();
}
std::size_t SsaoGpu::residentBytes()const{std::size_t bytes=bgfx::isValid(scratch_)?std::size_t(width_)*height_*16:0;for(const auto& old:retired_)bytes+=old.bytes;return bytes;}
void SsaoGpu::ensureScratch(std::uint16_t width,std::uint16_t height){
    if(bgfx::isValid(scratch_)&&width_==width&&height_==height)return;
    constexpr auto flags=BGFX_TEXTURE_COMPUTE_WRITE|Sampling;
    if(!bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RGBA32F,flags))throw std::runtime_error("SSAO requires an RGBA32F compute-write texture.");
    auto next=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA32F,flags);
    if(!bgfx::isValid(next))throw std::runtime_error("SSAO scratch allocation failed.");
    if(bgfx::isValid(scratch_))try{retired_.push_back({scratch_,std::size_t(width_)*height_*16,frame_+3});}catch(...){destroy(next);throw;}
    scratch_=next;width_=width;height_=height;bgfx::setName(scratch_,"SSAO half-resolution geometry and visibility");
}
bool SsaoGpu::prepare(bgfx::TextureHandle depth,bgfx::TextureHandle receiver,const Camera& camera,
    std::uint16_t width,std::uint16_t height,ClusteredLightingGpu& clustered,const SsaoSettings& settings,bgfx::ViewId view){
    status_.clear();
    if(!settings.enabled||settings.strength<=0){disable();return false;}
    try {
        if(!validSsaoSettings(settings))throw std::runtime_error("Invalid SSAO settings.");
        if(!width||!height||clustered.ssaoWidth()!=width||clustered.ssaoHeight()!=height||
            !bgfx::isValid(depth)||!bgfx::isValid(receiver)||!bgfx::isValid(clustered.texture()))throw std::runtime_error("SSAO depth, normals or atlas region is unavailable.");
        const auto* caps=bgfx::getCaps();
        if(!caps||!(caps->supported&BGFX_CAPS_COMPUTE)||view>=caps->limits.maxViews||!bgfx::isValid(evaluate_)||!bgfx::isValid(publish_))throw std::runtime_error("SSAO compute programs or view are unavailable.");
        const auto projection=camera.projectionMatrix(float(width)/height,caps->homogeneousDepth?ClipDepth::NegativeOneToOne:ClipDepth::ZeroToOne),viewMatrix=camera.viewMatrix();
        const auto inverse=math::inverse(projection*viewMatrix);if(!inverse||!math::finite(*inverse))throw std::runtime_error("SSAO camera projection is singular.");
        const auto halfWidth=std::uint16_t((std::uint32_t(width)+1)/2),halfHeight=std::uint16_t((std::uint32_t(height)+1)/2);
        ensureScratch(halfWidth,halfHeight);
        const unsigned directions=settings.quality==SsaoQuality::Low?4:settings.quality==SsaoQuality::High?12:8;
        const unsigned steps=settings.quality==SsaoQuality::Low?3:settings.quality==SsaoQuality::High?6:4;
        const Vec4 screen{float(width),float(height),caps->homogeneousDepth?1.f:0.f,caps->originBottomLeft?1.f:0.f};
        const Vec4 size{float(halfWidth),float(halfHeight),float(directions),float(steps)};
        const Vec4 parameters{settings.radius,.025f,std::max(.002f,settings.radius*.04f),0};
        const Vec4 projectionValues{projection[0],projection[5],std::max(camera.nearPlane,.0001f),0};
        const Vec4 atlas{float(clustered.ssaoBaseTexel()),float(clustered.atlasWidth()),float(width),float(height)};
        const auto uniforms=[&]{bgfx::setUniform(view_,screen.data());bgfx::setUniform(size_,size.data());bgfx::setUniform(parameters_,parameters.data());bgfx::setUniform(projection_,projectionValues.data());bgfx::setUniform(inverse_,inverse->data());bgfx::setUniform(viewMatrix_,viewMatrix.data());};
        bgfx::setViewMode(view,bgfx::ViewMode::Sequential);bgfx::setViewFrameBuffer(view,BGFX_INVALID_HANDLE);bgfx::setViewClear(view,BGFX_CLEAR_NONE);
        uniforms();bgfx::setTexture(0,depth_,depth,std::uint32_t(Sampling));bgfx::setTexture(1,receiver_,receiver,std::uint32_t(Sampling));
        bgfx::setImage(3,scratch_,0,bgfx::Access::Write,bgfx::TextureFormat::RGBA32F);
        bgfx::dispatch(view,evaluate_,(std::uint32_t(halfWidth)+7)/8,(std::uint32_t(halfHeight)+7)/8,1);
        uniforms();bgfx::setUniform(atlas_,atlas.data());bgfx::setTexture(0,scratchSampler_,scratch_,std::uint32_t(Sampling));
        bgfx::setTexture(1,depth_,depth,std::uint32_t(Sampling));bgfx::setTexture(2,receiver_,receiver,std::uint32_t(Sampling));
        bgfx::setImage(3,clustered.texture(),0,bgfx::Access::Write,bgfx::TextureFormat::RGBA32F);
        bgfx::dispatch(view,publish_,(std::uint32_t(width)+31)/32,(std::uint32_t(height)+7)/8,1);return true;
    }catch(const std::exception& error){status_=error.what();return false;}
}
}
