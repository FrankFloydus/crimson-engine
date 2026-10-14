#include "PointShadowRenderer.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace crimson {
namespace {
template<class Handle> void destroy(Handle& handle) {
    if(bgfx::isValid(handle))bgfx::destroy(handle);
    handle=BGFX_INVALID_HANDLE;
}
void hash(std::uint64_t& value,std::uint64_t next){value=(value^next)*1099511628211ull;}
void hash(std::uint64_t& value,float next){hash(value,static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(next)));}
constexpr std::uint64_t Sampling=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_COMPARE_LEQUAL;
}
struct PointShadowRenderer::Atlas {
    point_shadow::AtlasLayout layout;
    bgfx::TextureHandle depth{BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle framebuffer{BGFX_INVALID_HANDLE};
    ~Atlas(){destroy(framebuffer);destroy(depth);}
};
struct PointShadowRenderer::Retired {std::unique_ptr<Atlas> atlas;std::uint64_t after{};};
PointShadowRenderer::PointShadowRenderer() {
    sampler_=bgfx::createUniform("s_pointShadow",bgfx::UniformType::Sampler);
    parametersUniform_=bgfx::createUniform("u_pointShadow",bgfx::UniformType::Vec4,16);
    atlasUniform_=bgfx::createUniform("u_shadowAtlas",bgfx::UniformType::Vec4);
    layoutUniform_=bgfx::createUniform("u_shadowLayout",bgfx::UniformType::Vec4);
    for(auto& value:parameters_)value={-1,0,0,0};
}
PointShadowRenderer::~PointShadowRenderer() {
    atlas_.reset();retired_.clear();destroy(sampler_);destroy(parametersUniform_);destroy(atlasUniform_);destroy(layoutUniform_);
}
void PointShadowRenderer::advance(std::uint64_t frame) {
    frame_=frame;
    std::erase_if(retired_,[&](const auto& retired){return retired.after<=frame;});
    stats_.renderedLights=stats_.renderedFaces=stats_.drawCalls=stats_.cacheHits=stats_.casters=0;
    stats_.allocatedBytes=residentBytes();
}
void PointShadowRenderer::invalidate(){for(auto& slot:slots_)slot.valid=false;lastRenderedFrame_=UINT64_MAX;}
void PointShadowRenderer::release() {
    if(atlas_)retired_.push_back({std::move(atlas_),frame_+2});
    slots_={};world_=0;invalidate();
    for(auto& value:parameters_)value={-1,0,0,0};
    stats_={};stats_.allocatedBytes=residentBytes();
}
std::size_t PointShadowRenderer::residentBytes() const {
    auto bytes=atlas_?atlas_->layout.bytes():0;
    for(const auto& old:retired_)bytes+=old.atlas->layout.bytes();
    return bytes;
}
bool PointShadowRenderer::ensureAtlas(std::uint16_t resolution,std::uint16_t capacity) {
    if(atlas_ && atlas_->layout.resolution==resolution && atlas_->layout.capacity>=capacity)return true;
    try {
        const auto layout=point_shadow::atlasLayout(resolution,capacity);
        const auto* caps=bgfx::getCaps();
        if(!layout.width || !caps || caps->limits.maxTextureSamplers<9 ||
           !(caps->supported&BGFX_CAPS_TEXTURE_COMPARE_LEQUAL) ||
           layout.width>caps->limits.maxTextureSize || layout.height>caps->limits.maxTextureSize ||
           !bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::D32F,BGFX_TEXTURE_RT|Sampling))
            throw std::runtime_error("This adapter cannot allocate the requested D32F point-shadow atlas with comparison filtering.");
        auto next=std::make_unique<Atlas>();next->layout=layout;
        next->depth=bgfx::createTexture2D(layout.width,layout.height,false,1,bgfx::TextureFormat::D32F,BGFX_TEXTURE_RT|Sampling);
        if(!bgfx::isValid(next->depth))throw std::runtime_error("Point-shadow depth allocation failed.");
        next->framebuffer=bgfx::createFrameBuffer(1,&next->depth,false);
        if(!bgfx::isValid(next->framebuffer))throw std::runtime_error("Point-shadow framebuffer allocation failed.");
        bgfx::setName(next->depth,"Point shadow depth atlas");
        if(atlas_)retired_.push_back({std::move(atlas_),frame_+2});
        atlas_=std::move(next);invalidate();return true;
    } catch(const std::exception& error){status_=error.what();return false;}
}
void PointShadowRenderer::render(std::span<const PointLight> lights,std::span<const std::uint64_t> casterSignatures,
    std::uint64_t world,std::uint16_t resolution,std::uint64_t frame,bgfx::ViewId firstView,const DrawFace& draw) {
    frame_=frame;
    for(auto& value:parameters_)value={-1,0,0,0};
    status_.clear();
    if(!world){release();return;}
    if(world_!=world){slots_={};invalidate();world_=world;}
    const auto count=std::min<std::size_t>(16,lights.size());
    std::array<std::uint64_t,16> ids{};
    unsigned active=0;
    for(std::size_t index=0;index<count;++index) {
        const auto& light=lights[index];
        if(light.enabled && light.castShadows && math::finite(light.position) &&
           std::isfinite(light.radius) && light.radius>0) {
            ids[index]=light.entityId?light.entityId:index+1;++active;
        }
    }
    if(!active){release();return;}
    for(auto& slot:slots_)if(slot.id && std::find(ids.begin(),ids.end(),slot.id)==ids.end())slot={};
    std::array<unsigned,16> mapping{};unsigned required=0;
    for(std::size_t index=0;index<count;++index)if(ids[index]) {
        auto found=std::find_if(slots_.begin(),slots_.end(),[&](const auto& slot){return slot.id==ids[index];});
        if(found==slots_.end())found=std::find_if(slots_.begin(),slots_.end(),[](const auto& slot){return !slot.id;});
        if(found==slots_.end()){status_="Point-shadow slot capacity exceeded.";return;}
        if(found->id!=ids[index]){*found={};found->id=ids[index];}
        const auto slot=static_cast<unsigned>(found-slots_.begin());mapping[index]=slot;required=std::max(required,slot+1);
    }
    std::uint16_t capacity=1;while(capacity<required)capacity*=2;
    if(resolution!=256 && resolution!=512 && resolution!=1024)resolution=1024;
    if(!ensureAtlas(resolution,capacity)) {stats_.activeLights=0;stats_.allocatedBytes=residentBytes();return;}
    const auto* caps=bgfx::getCaps();
    if(static_cast<std::uint32_t>(firstView)+96>caps->limits.maxViews){status_="Insufficient renderer views for all point-light shadow faces.";return;}
    const bool firstSubmission=lastRenderedFrame_!=frame;
    stats_.activeLights=active;stats_.worldId=world;stats_.resolution=resolution;stats_.capacity=atlas_->layout.capacity;
    stats_.allocatedBytes=residentBytes();
    for(std::size_t index=0;index<count;++index)if(ids[index]) {
        const auto& light=lights[index];const auto slotIndex=mapping[index];auto& slot=slots_[slotIndex];
        std::uint64_t signature=index<casterSignatures.size()?casterSignatures[index]:0;
        hash(signature,light.position[0]);hash(signature,light.position[1]);hash(signature,light.position[2]);hash(signature,light.radius);
        parameters_[index]={static_cast<float>(slotIndex),point_shadow::nearPlane(light.radius),light.shadowDepthBias,light.shadowNormalBias};
        if(slot.valid && slot.signature==signature){if(firstSubmission)++stats_.cacheHits;continue;}
        for(unsigned face=0;face<6;++face) {
            const auto rect=point_shadow::tileRect(atlas_->layout,slotIndex,face);
            const auto view=static_cast<bgfx::ViewId>(firstView+slotIndex*6+face);
            const auto camera=point_shadow::faceCamera(light.position,light.radius,face,resolution);
            const auto viewMatrix=camera.viewMatrix();
            const auto projection=camera.projectionMatrix(1,caps->homogeneousDepth?ClipDepth::NegativeOneToOne:ClipDepth::ZeroToOne);
            bgfx::setViewFrameBuffer(view,atlas_->framebuffer);
            bgfx::setViewMode(view,bgfx::ViewMode::Sequential);
            bgfx::setViewRect(view,rect.x,rect.y,rect.width,rect.height);
            bgfx::setViewClear(view,BGFX_CLEAR_DEPTH,0u,1.0f,0);
            bgfx::setViewTransform(view,viewMatrix.data(),projection.data());
            bgfx::touch(view);
            stats_.drawCalls+=draw(index,camera,view);++stats_.renderedFaces;
        }
        slot.signature=signature;slot.valid=true;++stats_.renderedLights;
    }
    lastRenderedFrame_=frame;
}
void PointShadowRenderer::bindDisabled() const {
    std::array<Vec4,16> disabled;for(auto& value:disabled)value={-1,0,0,0};
    bgfx::setUniform(parametersUniform_,disabled.data(),16);
    const Vec4 zero{};bgfx::setUniform(atlasUniform_,zero.data());bgfx::setUniform(layoutUniform_,zero.data());
    if(atlas_)bgfx::setTexture(8,sampler_,atlas_->depth,static_cast<std::uint32_t>(Sampling));
}
void PointShadowRenderer::bind(bool receiving) const {
    if(!receiving || !atlas_){bindDisabled();return;}
    const auto& layout=atlas_->layout;
    const Vec4 atlas{1.f/layout.width,1.f/layout.height,static_cast<float>(layout.resolution),static_cast<float>(layout.pitch)};
    const Vec4 dimensions{static_cast<float>(layout.columns),static_cast<float>(layout.pitch)/layout.resolution,0,0};
    bgfx::setUniform(parametersUniform_,parameters_.data(),16);
    bgfx::setUniform(atlasUniform_,atlas.data());bgfx::setUniform(layoutUniform_,dimensions.data());
    bgfx::setTexture(8,sampler_,atlas_->depth,static_cast<std::uint32_t>(Sampling));
}
}
