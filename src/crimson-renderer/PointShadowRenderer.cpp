#include "PointShadowRenderer.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace crimson {
namespace {
// Keep this name distinct from bgfx::destroy: ADL would prefer its unguarded
// non-template overload for bgfx handles, including invalid lazy allocations.
template<class Handle> void destroyHandle(Handle& handle) {
    if(bgfx::isValid(handle))bgfx::destroy(handle);
    handle=BGFX_INVALID_HANDLE;
}
void hash(std::uint64_t& value,std::uint64_t next){value=(value^next)*1099511628211ull;}
void hash(std::uint64_t& value,float next){hash(value,static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(next)));}
constexpr std::uint64_t Sampling=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_COMPARE_LEQUAL;
}
struct PointShadowRenderer::Atlas {
    directional_shadow::AtlasLayout layout;
    bgfx::TextureHandle depth{BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle framebuffer{BGFX_INVALID_HANDLE};
    ~Atlas(){destroyHandle(framebuffer);destroyHandle(depth);}
};
struct PointShadowRenderer::Retired {std::unique_ptr<Atlas> atlas;std::uint64_t after{};};
PointShadowRenderer::PointShadowRenderer(bool budgeted):budgeted_(budgeted) {
    sampler_=bgfx::createUniform("s_pointShadow",bgfx::UniformType::Sampler);
    parametersUniform_=bgfx::createUniform("u_pointShadow",bgfx::UniformType::Vec4,16);
    atlasUniform_=bgfx::createUniform("u_shadowAtlas",bgfx::UniformType::Vec4);
    layoutUniform_=bgfx::createUniform("u_shadowLayout",bgfx::UniformType::Vec4);
    directionalMatrices_=bgfx::createUniform("u_directionalShadowMatrix",bgfx::UniformType::Mat4,16);
    directionalRects_=bgfx::createUniform("u_directionalShadowRect",bgfx::UniformType::Vec4,16);
    directionalSplits_=bgfx::createUniform("u_directionalShadowSplits",bgfx::UniformType::Vec4,4);
    directionalParameters_=bgfx::createUniform("u_directionalShadowParameters",bgfx::UniformType::Vec4,4);
    shadowCameraForward_=bgfx::createUniform("u_shadowCameraForward",bgfx::UniformType::Vec4);
    characterMatrixUniform_=bgfx::createUniform("u_characterShadowMatrix",bgfx::UniformType::Mat4,MaximumActorShadows);
    characterRectUniform_=bgfx::createUniform("u_characterShadowRect",bgfx::UniformType::Vec4,MaximumActorShadows);
    characterDirectionUniform_=bgfx::createUniform("u_characterShadowDirection",bgfx::UniformType::Vec4,MaximumActorShadows);
    characterParametersUniform_=bgfx::createUniform("u_characterShadowParameters",bgfx::UniformType::Vec4,MaximumActorShadows);
    for(auto& value:parameters_)value={-1,0,0,0};
}
PointShadowRenderer::~PointShadowRenderer() {
    atlas_.reset();retired_.clear();destroyHandle(sampler_);destroyHandle(parametersUniform_);destroyHandle(atlasUniform_);destroyHandle(layoutUniform_);
    destroyHandle(directionalMatrices_);destroyHandle(directionalRects_);destroyHandle(directionalSplits_);destroyHandle(directionalParameters_);destroyHandle(shadowCameraForward_);
    destroyHandle(characterMatrixUniform_);destroyHandle(characterRectUniform_);destroyHandle(characterDirectionUniform_);destroyHandle(characterParametersUniform_);
}
void PointShadowRenderer::advance(std::uint64_t frame) {
    frame_=frame;
    std::erase_if(retired_,[&](const auto& retired){return retired.after<=frame;});
    stats_.renderedLights=stats_.renderedFaces=stats_.drawCalls=stats_.cacheHits=stats_.casters=0;
    stats_.directionalLights=stats_.directionalRendered=stats_.directionalCascades=stats_.directionalCacheHits=0;
    stats_.pointDeferred=stats_.pointCriticalDeferred=stats_.directionalWorldProjections=stats_.characterProjections=0;
    stats_.dedicatedCharacterUpdates=stats_.dedicatedCharacterCacheHits=0;
    stats_.allocatedBytes=residentBytes();
}
void PointShadowRenderer::invalidate(){for(auto& slot:actorSlots_)slot.valid=false;lastActorSubmission_.fill(UINT64_MAX);for(auto& slot:slots_)slot.valid=false;for(auto& context:directionalSlots_)for(auto& light:context)for(auto& slot:light)slot.valid=false;lastRenderedFrame_=UINT64_MAX;}
void PointShadowRenderer::release() {
    usable_=false;
    if(atlas_)retired_.push_back({std::move(atlas_),frame_+2});
    slots_={};directionalIds_={};directionalSlots_={};directional_={};actorSlots_={};actorParameters_={};world_=0;invalidate();
    for(auto& value:parameters_)value={-1,0,0,0};
    stats_={};stats_.allocatedBytes=residentBytes();
}
std::size_t PointShadowRenderer::residentBytes() const {
    auto bytes=atlas_?atlas_->layout.bytes():0;
    for(const auto& old:retired_)bytes+=old.atlas->layout.bytes();
    return bytes;
}
bool PointShadowRenderer::ensureAtlas(std::uint16_t resolution,std::uint16_t capacity,std::uint16_t directionalCapacity,unsigned cameraCount,unsigned actorCapacity) {
    if(atlas_ && atlas_->layout.resolution==resolution && atlas_->layout.points.capacity>=capacity && atlas_->layout.directionalCapacity>=directionalCapacity && atlas_->layout.cameraCount==cameraCount && atlas_->layout.actorCapacity>=actorCapacity)return true;
    try {
        const auto layout=directional_shadow::atlasLayout(resolution,capacity,directionalCapacity,cameraCount,cameraCount==2,actorCapacity);
        const auto* caps=bgfx::getCaps();
        if(!layout.width || !caps || caps->limits.maxTextureSamplers<9 ||
           !(caps->supported&BGFX_CAPS_TEXTURE_COMPARE_LEQUAL) ||
           layout.width>caps->limits.maxTextureSize || layout.height>caps->limits.maxTextureSize ||
           !bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::D32F,BGFX_TEXTURE_RT|Sampling))
            throw std::runtime_error("This adapter cannot allocate the requested D32F shadow atlas with comparison filtering.");
        auto next=std::make_unique<Atlas>();next->layout=layout;
        next->depth=bgfx::createTexture2D(layout.width,layout.height,false,1,bgfx::TextureFormat::D32F,BGFX_TEXTURE_RT|Sampling);
        if(!bgfx::isValid(next->depth))throw std::runtime_error("Shadow depth allocation failed.");
        next->framebuffer=bgfx::createFrameBuffer(1,&next->depth,false);
        if(!bgfx::isValid(next->framebuffer))throw std::runtime_error("Shadow framebuffer allocation failed.");
        bgfx::setName(next->depth,"World and character shadow depth atlas");
        if(atlas_)retired_.push_back({std::move(atlas_),frame_+2});
        atlas_=std::move(next);invalidate();return true;
    } catch(const std::exception& error){status_=error.what();return false;}
}
void PointShadowRenderer::render(std::span<const PointLight> lights,std::span<const DirectionalLight> directions,std::span<const std::uint64_t> casterSignatures,
    std::uint64_t world,std::uint16_t resolution,std::uint64_t frame,bgfx::ViewId firstView,unsigned cameraCount,const Vec3& focus,const DrawFace& draw,unsigned actorCapacity) {
    frame_=frame;
    usable_=false;
    for(auto& value:parameters_)value={-1,0,0,0};
    status_.clear();actorParameters_={};
    if(!world){release();return;}
    if(world_!=world){slots_={};directionalIds_={};directionalSlots_={};directional_={};actorSlots_={};actorParameters_={};invalidate();world_=world;}
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
    if(!active && !actorCapacity && std::none_of(directions.begin(),directions.end(),[](const auto& light){return light.enabled && light.castShadows;})){release();return;}
    for(auto& slot:slots_)if(slot.id && std::find(ids.begin(),ids.end(),slot.id)==ids.end())slot={};
    std::array<unsigned,16> mapping{};unsigned required=0;
    for(std::size_t index=0;index<count;++index)if(ids[index]) {
        auto found=std::find_if(slots_.begin(),slots_.end(),[&](const auto& slot){return slot.id==ids[index];});
        if(found==slots_.end())found=std::find_if(slots_.begin(),slots_.end(),[](const auto& slot){return !slot.id;});
        if(found==slots_.end()){status_="Point-shadow slot capacity exceeded.";return;}
        if(found->id!=ids[index]){*found={};found->id=ids[index];}
        const auto slot=static_cast<unsigned>(found-slots_.begin());mapping[index]=slot;required=std::max(required,slot+1);
    }
    std::uint16_t capacity=required?1:0;while(capacity<required)capacity*=2;
    unsigned directionalRequired=0;
    for(auto& id:directionalIds_)if(id && std::none_of(directions.begin(),directions.end(),[&](const auto& light){return light.enabled && light.castShadows && (light.entityId?light.entityId:std::uint64_t(&light-directions.data()+1))==id;}))id=0;
    for(std::size_t index=0;index<std::min<std::size_t>(4,directions.size());++index)if(directions[index].enabled && directions[index].castShadows) {
        const auto id=directions[index].entityId?directions[index].entityId:index+1;
        auto slot=std::find(directionalIds_.begin(),directionalIds_.end(),id);
        if(slot==directionalIds_.end())slot=std::find(directionalIds_.begin(),directionalIds_.end(),0);
        if(slot==directionalIds_.end())continue;
        if(*slot!=id){*slot=id;for(auto& context:directionalSlots_)context[slot-directionalIds_.begin()]={};}
        directionalRequired=std::max(directionalRequired,static_cast<unsigned>(slot-directionalIds_.begin()+1));
    }
    if(resolution!=256 && resolution!=512 && resolution!=1024)resolution=1024;
    if(!ensureAtlas(resolution,capacity,static_cast<std::uint16_t>(directionalRequired),cameraCount,actorCapacity)) {stats_.activeLights=0;stats_.allocatedBytes=residentBytes();return;}
    usable_=true;
    const auto* caps=bgfx::getCaps();
    if(static_cast<std::uint32_t>(firstView)+96>caps->limits.maxViews){status_="Insufficient renderer views for all point-light shadow faces.";return;}
    const bool firstSubmission=lastRenderedFrame_!=frame;
    stats_.activeLights=active;stats_.worldId=world;stats_.resolution=resolution;stats_.capacity=atlas_->layout.points.capacity;
    stats_.allocatedBytes=residentBytes();
    std::array<shadow_updates::Candidate,16> candidates;
    std::array<std::uint64_t,16> signatures{},projections{};
    std::size_t candidateCount{};
    for(std::size_t index=0;index<count;++index)if(ids[index]) {
        const auto& light=lights[index];const auto slotIndex=mapping[index];auto& slot=slots_[slotIndex];
        auto& projection=projections[index];projection=14695981039346656037ull;
        hash(projection,light.position[0]);hash(projection,light.position[1]);hash(projection,light.position[2]);hash(projection,light.radius);
        auto& signature=signatures[index];signature=index<casterSignatures.size()?casterSignatures[index]:0;hash(signature,projection);
        const bool dirty=!slot.valid || slot.signature!=signature;
        const auto displacement=light.position-focus;
        const auto distance=math::dot(displacement,displacement);
        candidates[candidateCount++]={ids[index],static_cast<std::uint8_t>(slotIndex),dirty,
            !slot.valid || slot.projectionSignature!=projection,static_cast<float>(distance),slot.framesDeferred,slot.lastUpdatedFrame};
        if(!dirty && firstSubmission){++stats_.cacheHits;slot.framesDeferred=0;}
    }
    const auto budget=budgeted_?shadow_updates::Budget{}:shadow_updates::Budget{16,96,4};
    const auto selection=firstSubmission?shadow_updates::select(std::span(candidates).first(candidateCount),budget):shadow_updates::Selection{};
    if(!selection){status_=shadow_updates::errorText(selection.error);usable_=false;return;}
    if(firstSubmission){stats_.pointDeferred=selection.deferredCount;stats_.pointCriticalDeferred=selection.criticalDeferred;}
    for(std::size_t index=0;index<count;++index)if(ids[index]) {
        const auto& light=lights[index];const auto slotIndex=mapping[index];auto& slot=slots_[slotIndex];
        const bool dirty=!slot.valid || slot.signature!=signatures[index];
        if(!dirty || !firstSubmission || !selection.selected(static_cast<std::uint8_t>(slotIndex))) {
            // Caster-only edits may retain coherent old content for a bounded
            // number of frames. A changed light projection is never sampled.
            if(slot.valid && slot.projectionSignature==projections[index])
                parameters_[index]={static_cast<float>(slotIndex),point_shadow::nearPlane(light.radius),light.shadowDepthBias,light.shadowNormalBias};
            if(firstSubmission && dirty && slot.framesDeferred<std::numeric_limits<std::uint32_t>::max())++slot.framesDeferred;
            continue;
        }
        for(unsigned face=0;face<6;++face) {
            const auto rect=point_shadow::tileRect(atlas_->layout.points,slotIndex,face);
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
        slot.signature=signatures[index];slot.projectionSignature=projections[index];slot.valid=true;
        slot.lastUpdatedFrame=frame;slot.framesDeferred=0;
        parameters_[index]={static_cast<float>(slotIndex),point_shadow::nearPlane(light.radius),light.shadowDepthBias,light.shadowNormalBias};
        ++stats_.renderedLights;
    }
    lastRenderedFrame_=frame;
}
void PointShadowRenderer::renderDirectional(std::span<const DirectionalLight> lights,std::span<const directional_shadow::Cascades> cascades,
    std::span<const std::uint64_t> casterSignatures,const ShadowCamera& camera,unsigned context,bgfx::ViewId firstView,const DrawCascade& draw) {
    if(context>=2)return;
    auto& output=directional_[context];output={};
    const auto forward=camera.camera.forward();output.cameraForward={forward[0],forward[1],forward[2],-static_cast<float>(math::dot(camera.camera.eye(),forward))};
    if(!usable_ || !atlas_ || context>=atlas_->layout.cameraCount)return;
    const auto& layout=atlas_->layout;
    for(std::size_t index=0;index<std::min<std::size_t>(4,lights.size());++index) {
        const auto& light=lights[index];if(!light.enabled||!light.castShadows||index>=cascades.size()||!cascades[index].valid)continue;
        const auto id=light.entityId?light.entityId:index+1;
        const auto found=std::find(directionalIds_.begin(),directionalIds_.end(),id);if(found==directionalIds_.end())continue;
        const auto slotIndex=static_cast<unsigned>(found-directionalIds_.begin());
        const auto& fits=cascades[index];
        const bool gameplay=fits.policy==directional_shadow::ProjectionPolicy::Gameplay;
        output.parameters[index]={gameplay?(fits.characterProjection<fits.projectionCount?3.f:2.f):1.f,
            light.shadowDepthBias,light.shadowNormalBias,fits.distance};output.splits[index]=fits.splits;
        ++stats_.directionalLights;
        const auto block=directional_shadow::blockRect(layout,slotIndex,context);
        const auto view=static_cast<bgfx::ViewId>(firstView+slotIndex);
        bool rendered{};
        for(unsigned cascade=0;cascade<fits.projectionCount;++cascade) {
            auto& slot=directionalSlots_[context][slotIndex][cascade];
            const auto signatureIndex=index*4+cascade;
            auto signature=signatureIndex<casterSignatures.size()?casterSignatures[signatureIndex]:0;
            const auto& fit=fits.values[cascade];const auto row=index*4+cascade;
            output.matrices[row]=fit.receiverViewProjection;
            const auto rect=directional_shadow::tileRect(layout,slotIndex,context,cascade);
            output.rectangles[row]={float(rect.x+2)/layout.width,float(rect.y+2)/layout.height,float(layout.resolution)/layout.width,float(layout.resolution)/layout.height};
            for(const float value:fit.receiverViewProjection)hash(signature,value);
            hash(signature,static_cast<std::uint64_t>(gameplay));
            if(slot.valid&&slot.id==id&&slot.signature==signature)continue;
            if(!rendered) {
                bgfx::setViewFrameBuffer(view,atlas_->framebuffer);bgfx::setViewMode(view,bgfx::ViewMode::Sequential);
                bgfx::setViewRect(view,block.x,block.y,block.width,block.height);bgfx::setViewClear(view,BGFX_CLEAR_NONE);
            }
            stats_.drawCalls+=draw(index,cascade,fits.values[cascade],directional_shadow::tileRect(layout,slotIndex,context,cascade),view);
            ++stats_.directionalCascades;
            if(gameplay && cascade==fits.characterProjection)++stats_.characterProjections;
            else ++stats_.directionalWorldProjections;
            slot={id,signature,true};rendered=true;
        }
        if(rendered)++stats_.directionalRendered;else ++stats_.directionalCacheHits;
    }
}
void PointShadowRenderer::renderActor(unsigned actor,const CharacterShadowSource& source,const std::optional<directional_shadow::Cascade>& fit,
    std::uint64_t signature,bgfx::ViewId view,const DrawActor& draw) {
    if(actor>=MaximumActorShadows)return;
    actorParameters_[actor]={};
    if(!usable_||!atlas_||actor>=atlas_->layout.actorCapacity||!source.enabled||!fit)return;
    const auto& layout=atlas_->layout;const auto rect=layout.actorTiles[actor];
    actorMatrices_[actor]=fit->receiverViewProjection;
    actorRects_[actor]={float(rect.x+2)/layout.width,float(rect.y+2)/layout.height,float(layout.resolution)/layout.width,float(layout.resolution)/layout.height};
    const auto rays=math::normalized(source.direction);
    actorDirections_[actor]={rays[0],rays[1],rays[2],source.strength};
    actorParameters_[actor]={source.shadowDepthBias,source.shadowNormalBias,source.distance,1};
    hash(signature,source.owner);
    for(const float value:fit->receiverViewProjection)hash(signature,value);
    const bool firstSubmission=lastActorSubmission_[actor]!=frame_;lastActorSubmission_[actor]=frame_;
    auto& cached=actorSlots_[actor];
    if(cached.valid&&cached.id==source.owner&&cached.signature==signature) {
        if(firstSubmission)++stats_.dedicatedCharacterCacheHits;
        return;
    }
    // One physical view serves all actor tiles. Tile transforms and scissor are
    // draw state; changing a view rectangle between submissions would alias it.
    bgfx::setViewFrameBuffer(view,atlas_->framebuffer);bgfx::setViewMode(view,bgfx::ViewMode::Sequential);
    bgfx::setViewRect(view,0,0,layout.width,layout.height);bgfx::setViewClear(view,BGFX_CLEAR_NONE);
    const float sx=float(rect.width)/layout.width,sy=float(rect.height)/layout.height;
    const bool bottom=bgfx::getCaps()->originBottomLeft;
    const Vec4 tile{sx,sy,2.f*rect.x/layout.width+sx-1.f,bottom?2.f*rect.y/layout.height+sy-1.f:1.f-2.f*rect.y/layout.height-sy};
    stats_.drawCalls+=draw(*fit,rect,tile,view);++stats_.dedicatedCharacterUpdates;
    cached={source.owner,signature,true,0,frame_,0};
}
void PointShadowRenderer::bindDisabled(std::uint8_t samplerSlot) const {
    std::array<Vec4,16> disabled;for(auto& value:disabled)value={-1,0,0,0};
    bgfx::setUniform(parametersUniform_,disabled.data(),16);
    const Vec4 zero{};bgfx::setUniform(atlasUniform_,zero.data());bgfx::setUniform(layoutUniform_,zero.data());
    const DirectionalParameters disabledDirections{};
    bgfx::setUniform(directionalParameters_,disabledDirections.parameters.data(),4);
    bgfx::setUniform(directionalMatrices_,disabledDirections.matrices.data(),16);bgfx::setUniform(directionalRects_,disabledDirections.rectangles.data(),16);
    bgfx::setUniform(directionalSplits_,disabledDirections.splits.data(),4);bgfx::setUniform(shadowCameraForward_,zero.data());
    const std::array<Mat4,MaximumActorShadows> identities{};const std::array<Vec4,MaximumActorShadows> zeros{};
    bgfx::setUniform(characterMatrixUniform_,identities.data(),MaximumActorShadows);
    bgfx::setUniform(characterRectUniform_,zeros.data(),MaximumActorShadows);bgfx::setUniform(characterDirectionUniform_,zeros.data(),MaximumActorShadows);bgfx::setUniform(characterParametersUniform_,zeros.data(),MaximumActorShadows);
    if(atlas_)bgfx::setTexture(samplerSlot,sampler_,atlas_->depth,static_cast<std::uint32_t>(Sampling));
}
void PointShadowRenderer::bind(bool receiving,std::uint8_t samplerSlot,unsigned cameraContext) const {
    if(!receiving || !usable_ || !atlas_){bindDisabled(samplerSlot);return;}
    const auto& layout=atlas_->layout;
    const Vec4 atlas{1.f/layout.width,1.f/layout.height,static_cast<float>(layout.resolution),static_cast<float>(layout.pitch)};
    const Vec4 dimensions{static_cast<float>(layout.points.columns),static_cast<float>(layout.pitch)/layout.resolution,0,0};
    bgfx::setUniform(parametersUniform_,parameters_.data(),16);
    bgfx::setUniform(atlasUniform_,atlas.data());bgfx::setUniform(layoutUniform_,dimensions.data());
    const auto& directions=directional_[std::min(cameraContext,1u)];
    bgfx::setUniform(directionalParameters_,directions.parameters.data(),4);bgfx::setUniform(directionalMatrices_,directions.matrices.data(),16);
    bgfx::setUniform(directionalRects_,directions.rectangles.data(),16);bgfx::setUniform(directionalSplits_,directions.splits.data(),4);
    bgfx::setUniform(shadowCameraForward_,directions.cameraForward.data());
    bgfx::setUniform(characterMatrixUniform_,actorMatrices_.data(),MaximumActorShadows);bgfx::setUniform(characterRectUniform_,actorRects_.data(),MaximumActorShadows);
    bgfx::setUniform(characterDirectionUniform_,actorDirections_.data(),MaximumActorShadows);bgfx::setUniform(characterParametersUniform_,actorParameters_.data(),MaximumActorShadows);
    bgfx::setTexture(samplerSlot,sampler_,atlas_->depth,static_cast<std::uint32_t>(Sampling));
}
}
