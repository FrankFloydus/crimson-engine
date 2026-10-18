#include "ClusteredLightingGpu.h"
#include "PointLightRecord.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace crimson {
namespace {
constexpr std::uint32_t RecordTexels=CC_POINT_LIGHT_RECORD_TEXELS;
constexpr std::uint64_t AtlasFlags=BGFX_TEXTURE_COMPUTE_WRITE|BGFX_TEXTURE_BLIT_DST|
    BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIP_POINT;
// bgfx adds one vertex stride when allocating this standalone dynamic buffer.
constexpr std::size_t LightBufferBytes=(std::size_t(ClusteredLightingGpu::MaxLights)*RecordTexels+1)*sizeof(Vec4);
// The largest adapter atlas remains below 2^32 texels. 256-texel alignment
// keeps this integer exactly representable in a float uniform at every size.
constexpr std::uint64_t OccluderAlignment=256;
std::uint64_t occluderBase(std::uint64_t afterAmbient){return (afterAmbient+OccluderAlignment-1)&~(OccluderAlignment-1);}
template<class Handle> void destroyHandle(Handle& handle){if(bgfx::isValid(handle))bgfx::destroy(handle);handle=BGFX_INVALID_HANDLE;}
float linear(float value) {
    const float color=std::isfinite(value)?std::clamp(value,0.f,1.f):0.f;
    return color<=.04045f?color/12.92f:std::pow((color+.055f)/1.055f,2.4f);
}
bool sameLights(std::span<const PointLight> left,std::span<const PointLight> right) {
    return left.size()==right.size() && std::equal(left.begin(),left.end(),right.begin(),[](const auto& a,const auto& b) {
        const auto same=[](float x,float y){return std::bit_cast<std::uint32_t>(x)==std::bit_cast<std::uint32_t>(y);};
        return std::equal(a.position.begin(),a.position.end(),b.position.begin(),same) &&
            std::equal(a.color.begin(),a.color.end(),b.color.begin(),same) &&
            same(a.intensity,b.intensity) && a.enabled==b.enabled && same(a.radius,b.radius) &&
            same(a.nearFalloffRadius,b.nearFalloffRadius) && same(a.specularStrength,b.specularStrength) &&
            same(a.rangeFalloffExponent,b.rangeFalloffExponent) && same(a.edgeSoftness,b.edgeSoftness) &&
            same(a.sourceRadius,b.sourceRadius) && same(a.attenuation,b.attenuation) && same(a.indirectEnergy,b.indirectEnergy) &&
            a.roles.receiverMask==b.roles.receiverMask && a.castShadows==b.castShadows;
    });
}
}

ClusteredLightingGpu::ClusteredLightingGpu(bgfx::ProgramHandle computeProgram):computeProgram_(computeProgram) {
    sampler_=bgfx::createUniform("s_brdfLut",bgfx::UniformType::Sampler);
    gridUniform_=bgfx::createUniform("u_clusterGrid",bgfx::UniformType::Vec4);
    depthUniform_=bgfx::createUniform("u_clusterDepth",bgfx::UniformType::Vec4);
    viewportUniform_=bgfx::createUniform("u_clusterViewport",bgfx::UniformType::Vec4);
    atlasUniform_=bgfx::createUniform("u_clusterAtlas",bgfx::UniformType::Vec4);
    optionsUniform_=bgfx::createUniform("u_clusterOptions",bgfx::UniformType::Vec4);
    recordLayoutUniform_=bgfx::createUniform("u_clusterRecordLayoutV6",bgfx::UniformType::Vec4);
    viewUniform_=bgfx::createUniform("u_clusterView",bgfx::UniformType::Mat4);
    occluderUniform_=bgfx::createUniform("u_occluderDepthAtlas",bgfx::UniformType::Vec4);
    occluderOptionsUniform_=bgfx::createUniform("u_occluderDepthOptions",bgfx::UniformType::Vec4);
    occluderDepthSampler_=bgfx::createUniform("s_occluderPublishDepth",bgfx::UniformType::Sampler);
}
ClusteredLightingGpu::~ClusteredLightingGpu() {
    destroyHandle(atlas_);destroyHandle(lightInput_);
    for(auto& old:retired_)destroyHandle(old.texture);
    destroyHandle(sampler_);destroyHandle(gridUniform_);destroyHandle(depthUniform_);destroyHandle(viewportUniform_);
    destroyHandle(atlasUniform_);destroyHandle(optionsUniform_);destroyHandle(viewUniform_);
    destroyHandle(recordLayoutUniform_);

    destroyHandle(occluderUniform_);destroyHandle(occluderOptionsUniform_);destroyHandle(occluderDepthSampler_);
}
void ClusteredLightingGpu::advance(std::uint64_t frame) {
    frame_=frame;
    std::erase_if(retired_,[&](auto& old){if(old.after>frame)return false;destroyHandle(old.texture);return true;});
    stats_.allocatedBytes=residentBytes();
}
std::size_t ClusteredLightingGpu::residentBytes() const {
    std::size_t bytes=bgfx::isValid(atlas_)?std::size_t(atlasWidth_)*atlasHeight_*sizeof(Vec4):0;
    if(bgfx::isValid(lightInput_))bytes+=LightBufferBytes;
    for(const auto& old:retired_)bytes+=old.bytes;
    return bytes;
}
bool ClusteredLightingGpu::ensureAtlas(std::uint64_t maskTexels,std::uint16_t brdfSize,std::uint64_t occluderTexels,std::uint64_t ssgiTexels,std::uint64_t ssaoTexels) {
    const auto* caps=bgfx::getCaps();
    if(!caps || !(caps->supported&BGFX_CAPS_COMPUTE) || !bgfx::isValid(computeProgram_))
        throw std::runtime_error("Clustered Forward+ requires a valid compute program and compute-capable adapter.");
    if(!bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RGBA32F,AtlasFlags))
        throw std::runtime_error("Clustered Forward+ requires an RGBA32F compute-write texture.");
    if(!bgfx::isValid(lightInput_)) {
        bgfx::VertexLayout layout;layout.begin().add(bgfx::Attrib::Position,4,bgfx::AttribType::Float).end();
        lightInput_=bgfx::createDynamicVertexBuffer(MaxLights*RecordTexels,layout,
            BGFX_BUFFER_COMPUTE_READ|BGFX_BUFFER_COMPUTE_FORMAT_32X4|BGFX_BUFFER_COMPUTE_TYPE_FLOAT);
        if(!bgfx::isValid(lightInput_))throw std::runtime_error("Clustered Forward+ light-input allocation failed.");
    }
    const std::uint32_t limit=std::min<std::uint32_t>({caps->limits.maxTextureSize,UINT16_MAX,MaxLights*RecordTexels});
    // Both the BRDF and the complete light reservation end on row boundaries,
    // including odd BRDF extents and widths that do not divide the record count.
    // Padded record uploads must never overwrite the first cluster masks.
    const bool releaseSsgi=(ssgiAllocation_ && !ssgiTexels)||(ssaoAllocation_ && !ssaoTexels);
    std::uint32_t width=std::bit_ceil(std::max<std::uint32_t>(brdfSize,releaseSsgi?0:atlasWidth_));
    auto requiredHeight=[&](std::uint32_t extent) {
        const auto lightRows=point_lighting::pointLightRecordRows(MaxLights,extent);
        const auto afterAmbient=(std::uint64_t(brdfSize)+lightRows)*extent+maskTexels;
        const auto end=occluderTexels?occluderBase(afterAmbient)+occluderTexels:afterAmbient;
        return (end+ssgiTexels+ssaoTexels+extent-1)/extent;
    };
    while(width<=limit && requiredHeight(width)>limit)width*=2;
    if(width>limit || width==0 || requiredHeight(width)>limit)
        throw std::runtime_error("Clustered Forward+ atlas exceeds the adapter texture size.");
    const auto required=static_cast<std::uint32_t>(requiredHeight(width));
    if(bgfx::isValid(atlas_) && width==atlasWidth_ && brdfSize==brdfSize_ && required<=atlasHeight_ && !releaseSsgi){ssgiAllocation_=ssgiTexels!=0;ssaoAllocation_=ssaoTexels!=0;return true;}
    // Grow by rows to avoid reallocating for each extra light word or resize.
    const std::uint32_t height=std::min(limit,(required+63u)&~63u);
    auto next=bgfx::createTexture2D(static_cast<std::uint16_t>(width),static_cast<std::uint16_t>(height),false,1,
        bgfx::TextureFormat::RGBA32F,AtlasFlags);
    if(!bgfx::isValid(next))throw std::runtime_error("Clustered Forward+ atlas allocation failed.");
    bgfx::setName(next,"Forward+ BRDF, lights, masks, ambient, occluder depth and GI");
    if(bgfx::isValid(atlas_)) {
        try {retired_.push_back({atlas_,std::size_t(atlasWidth_)*atlasHeight_*sizeof(Vec4),frame_+2});}
        catch(...) {destroyHandle(next);throw;}
    }
    atlas_=next;atlasWidth_=static_cast<std::uint16_t>(width);atlasHeight_=static_cast<std::uint16_t>(height);brdfSize_=brdfSize;
    ssgiAllocation_=ssgiTexels!=0;ssaoAllocation_=ssaoTexels!=0;
    brdfSnapshot_.clear();recordsUploaded_=false;masksValid_=false;
    return true;
}
bool ClusteredLightingGpu::prepare(const Camera& camera,std::uint16_t width,std::uint16_t height,
    std::span<const PointLight> lights,std::span<const std::int32_t> shadowIndices,
    std::span<const float> brdfRG,bgfx::ViewId computeView,bool referenceAllLights,
    std::uint16_t occluderWidth,std::uint16_t occluderHeight,
    std::uint16_t ssgiWidth,std::uint16_t ssgiHeight,std::uint16_t ssaoWidth,std::uint16_t ssaoHeight) {
    const auto begin=std::chrono::steady_clock::now();
    usable_=false;status_.clear();stats_={};occluderValues_={};ssgiBase_=0;ssaoBase_=0;ssaoWidth_=ssaoHeight_=0;
    occluderOptionsValues_={bgfx::getCaps()->originBottomLeft?1.f:0.f,0,0,0};
    stats_.totalLights=static_cast<std::uint32_t>(std::min<std::size_t>(lights.size(),UINT32_MAX));
    try {
        if(!width || !height)throw std::runtime_error("Clustered Forward+ requires a nonempty viewport.");
        if(bool(occluderWidth)!=bool(occluderHeight))throw std::runtime_error("Occluder depth requires both target dimensions.");
        if(bool(ssgiWidth)!=bool(ssgiHeight))throw std::runtime_error("SSGI requires both target dimensions.");
        if(bool(ssaoWidth)!=bool(ssaoHeight))throw std::runtime_error("SSAO requires both target dimensions.");
        if(lights.size()>MaxLights){stats_.overflow=true;throw std::runtime_error("Clustered Forward+ source capacity of 4096 point lights exceeded.");}
        const auto brdfSize=static_cast<std::uint32_t>(std::llround(std::sqrt(double(brdfRG.size()/2))));
        if(brdfSize==0 || brdfSize>UINT16_MAX || std::size_t(brdfSize)*brdfSize*2!=brdfRG.size())
            throw std::runtime_error("Clustered Forward+ BRDF lookup data is incomplete.");
        std::uint32_t tileSize=TileSize;
        auto tilesX=(std::uint32_t(width)+tileSize-1)/tileSize,tilesY=(std::uint32_t(height)+tileSize-1)/tileSize;
        // Preserve coverage at very large viewport sizes by using larger tiles.
        // Larger tiles can retain extra lights but never omit affecting lights.
        while(std::uint64_t(tilesX)*tilesY*DepthSlices>MaxClusters) {
            tileSize*=2;
            tilesX=(std::uint32_t(width)+tileSize-1)/tileSize;tilesY=(std::uint32_t(height)+tileSize-1)/tileSize;
        }
        const auto clusters=tilesX*tilesY*DepthSlices;
        const float nearPlane=std::isfinite(camera.nearPlane)?std::clamp(camera.nearPlane,1e-7f,1e8f):.01f;
        const float farPlane=std::max(nearPlane*2,std::isfinite(camera.farPlane)?std::clamp(camera.farPlane,1e-6f,1e12f):2048.f);
        const float scale=static_cast<float>(DepthSlices/std::log2(double(farPlane)/nearPlane));
        const float bias=-std::log2(nearPlane)*scale;
        const float aspect=float(width)/height;
        const float tanY=static_cast<float>(std::tan(double(camera.verticalFov(aspect))*std::numbers::pi/360));
        const auto nextView=camera.viewMatrix();
        const bool sameShadowIndices=shadowIndices.size()==shadowSnapshot_.size() &&
            std::equal(shadowIndices.begin(),shadowIndices.end(),shadowSnapshot_.begin());
        if(!sourceSnapshotValid_ || nextView!=recordViewSnapshot_ || !sameLights(lights,lightSnapshot_) || !sameShadowIndices) {
            records_.clear();currentSpheres_.clear();
            records_.reserve(lights.size()*RecordTexels);currentSpheres_.reserve(lights.size());
            for(std::size_t source=0;source<lights.size();++source) {
                const auto& light=lights[source];
                if(!light.enabled || !std::isfinite(light.intensity) || light.intensity<=0 || !math::finite(light.position))continue;
                const auto view=math::transformPoint(nextView,light.position);
                if(!math::finite(view))continue;
                const float radius=std::isfinite(light.radius)?std::clamp(light.radius,.01f,100000.f):20.f;
                // The camera and material evaluation use float transforms. Inflate
                // the cull sphere conservatively across tile and depth boundaries.
                const float padding=std::max({1e-5f,std::abs(view[0]),std::abs(view[1]),std::abs(view[2]),radius})*
                    (16*std::numeric_limits<float>::epsilon())+1e-5f;
                const auto shadow=source<shadowIndices.size()?shadowIndices[source]:-1;
                const auto first=records_.size();
                records_.push_back({light.position[0],light.position[1],light.position[2],std::clamp(light.intensity,0.f,1000000.f)});
                records_.push_back({linear(light.color[0]),linear(light.color[1]),linear(light.color[2]),light.inverseRadiusSquared()});
                records_.push_back({view[0],view[1],-view[2],radius+padding});
                const auto roles=light.roles.receiverMask;
                const float sourceRadius=std::isfinite(light.sourceRadius)?std::clamp(light.sourceRadius,0.f,100000.f):0.f;
                records_.push_back({float(shadow>=0 && shadow<16?shadow:-1),sourceRadius,float(roles),0.f});
                const float nearRadius=std::isfinite(light.nearFalloffRadius)?std::clamp(light.nearFalloffRadius,0.f,100000.f):0.f;
                const float specular=std::isfinite(light.specularStrength)?std::clamp(light.specularStrength,0.f,1.f):1.f;
                const float exponent=std::isfinite(light.rangeFalloffExponent)?std::clamp(light.rangeFalloffExponent,.25f,8.f):4.f;
                const float softness=std::isfinite(light.edgeSoftness)?std::clamp(light.edgeSoftness,.05f,1.f):1.f;
                records_.push_back({nearRadius*nearRadius,specular,exponent,softness});
                const float attenuation=std::isfinite(light.attenuation)?std::clamp(light.attenuation,-10.f,10.f):2.f;
                const float indirectEnergy=std::isfinite(light.indirectEnergy)?std::clamp(light.indirectEnergy,0.f,16.f):1.f;
                // Delta from one keeps the default record bytes unchanged.
                // This artistic multiplier is read only by SSGI donor capture.
                records_.push_back({attenuation,indirectEnergy-1.f,0,0});
                currentSpheres_.push_back(records_[first+2]);
            }
            lightSnapshot_.assign(lights.begin(),lights.end());shadowSnapshot_.assign(shadowIndices.begin(),shadowIndices.end());
            recordViewSnapshot_=nextView;sourceSnapshotValid_=true;
        }
        const auto count=static_cast<std::uint32_t>(records_.size()/RecordTexels);
        // Four 32-bit masks share a texel, with the final unused bits zeroed.
        const auto words=((count+127)/128)*4;
        const auto maskTexels=std::uint64_t(clusters)*(words/4);
        const auto occluderTexels=(std::uint64_t(occluderWidth)*occluderHeight+3)/4;
        const auto ssgiTexels=std::uint64_t(ssgiWidth)*ssgiHeight;
        const auto ssaoTexels=(std::uint64_t(ssaoWidth)*ssaoHeight+3)/4;
        ensureAtlas(maskTexels,static_cast<std::uint16_t>(brdfSize),occluderTexels,ssgiTexels,ssaoTexels);
        const auto lightBase=std::uint32_t(brdfSize_)*atlasWidth_;
        const auto lightRows=point_lighting::pointLightRecordRows(MaxLights,atlasWidth_);
        const auto maskBase=lightBase+lightRows*atlasWidth_;
        const auto afterAmbient=std::uint64_t(maskBase)+maskTexels;
        const auto screenBase=occluderTexels?occluderBase(afterAmbient)+occluderTexels:afterAmbient;
        if(ssgiTexels)ssgiBase_=static_cast<std::uint32_t>(screenBase);
        if(ssaoTexels){ssaoBase_=static_cast<std::uint32_t>(screenBase+ssgiTexels);ssaoWidth_=ssaoWidth;ssaoHeight_=ssaoHeight;}
        if(occluderTexels)occluderValues_={float(occluderBase(maskBase+maskTexels)),float(occluderWidth),float(occluderHeight),0};
        const Vec4 nextGrid{float(tilesX),float(tilesY),float(DepthSlices),float(words)};
        const Vec4 nextDepth{scale,bias,nearPlane,farPlane};
        const Vec4 nextViewport{float(width),float(height),tanY*aspect,tanY};
        const Vec4 nextAtlas{float(atlasWidth_),float(atlasHeight_),float(lightBase),float(maskBase)};
        const bool inputChanged=!inputUploaded_ || currentSpheres_!=inputSpheres_;
        if(inputChanged || nextGrid!=gridValues_ || nextDepth!=depthValues_ || nextViewport!=viewportValues_ ||
            nextAtlas!=atlasValues_ || float(tileSize)!=optionsValues_[3])masksValid_=false;
        gridValues_=nextGrid;depthValues_=nextDepth;viewportValues_=nextViewport;atlasValues_=nextAtlas;viewValues_=nextView;
        optionsValues_={float(count),referenceAllLights?1.f:0.f,float(brdfSize_),float(tileSize)};
        if(brdfSnapshot_.size()!=brdfRG.size() || !std::equal(brdfRG.begin(),brdfRG.end(),brdfSnapshot_.begin())) {
            brdfPixels_.resize(std::size_t(brdfSize_)*brdfSize_);
            for(std::size_t i=0;i<brdfPixels_.size();++i)brdfPixels_[i]={brdfRG[i*2],brdfRG[i*2+1],0,0};
            bgfx::updateTexture2D(atlas_,0,0,0,0,brdfSize_,brdfSize_,bgfx::copy(brdfPixels_.data(),
                static_cast<std::uint32_t>(brdfPixels_.size()*sizeof(Vec4))));
            brdfSnapshot_.assign(brdfRG.begin(),brdfRG.end());
        }
        if(count && (!recordsUploaded_ || records_!=publishedRecords_)) {
            const auto rows=static_cast<std::uint16_t>((records_.size()+atlasWidth_-1)/atlasWidth_);
            uploadRows_.assign(std::size_t(rows)*atlasWidth_,Vec4{});
            std::copy(records_.begin(),records_.end(),uploadRows_.begin());
            bgfx::updateTexture2D(atlas_,0,0,0,brdfSize_,atlasWidth_,rows,bgfx::copy(uploadRows_.data(),
                static_cast<std::uint32_t>(uploadRows_.size()*sizeof(Vec4))));
            publishedRecords_=records_;recordsUploaded_=true;
        }
        if(count && inputChanged) {
            bgfx::update(lightInput_,0,bgfx::copy(records_.data(),static_cast<std::uint32_t>(records_.size()*sizeof(Vec4))));
            inputSpheres_=currentSpheres_;inputUploaded_=true;
        }
        if(count && !referenceAllLights && !masksValid_) {
            setUniforms();bgfx::setBuffer(0,lightInput_,bgfx::Access::Read);
            bgfx::setImage(1,atlas_,0,bgfx::Access::Write,bgfx::TextureFormat::RGBA32F);
            bgfx::dispatch(computeView,computeProgram_,(clusters+63)/64,words/4,1);masksValid_=true;
        }
        stats_.activeLights=count;stats_.clusterCount=clusters;stats_.wordCount=words;
        stats_.allocatedBytes=residentBytes();stats_.buildCpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        usable_=true;return true;
    } catch(const std::exception& error) {
        // A partially submitted update must never make a later prepare reuse
        // snapshots that were not published coherently with its masks.
        sourceSnapshotValid_=recordsUploaded_=inputUploaded_=masksValid_=false;
        occluderValues_={};
        brdfSnapshot_.clear();
        status_=error.what();stats_.allocatedBytes=residentBytes();
        stats_.buildCpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();return false;
    }
}
void ClusteredLightingGpu::setUniforms() const {
    bgfx::setUniform(gridUniform_,gridValues_.data());bgfx::setUniform(depthUniform_,depthValues_.data());
    bgfx::setUniform(viewportUniform_,viewportValues_.data());bgfx::setUniform(atlasUniform_,atlasValues_.data());
    bgfx::setUniform(optionsUniform_,optionsValues_.data());bgfx::setUniform(viewUniform_,viewValues_.data());
    const Vec4 recordLayout{float(RecordTexels),0,0,0};bgfx::setUniform(recordLayoutUniform_,recordLayout.data());
    bgfx::setUniform(occluderUniform_,occluderValues_.data());bgfx::setUniform(occluderOptionsUniform_,occluderOptionsValues_.data());
}
bool ClusteredLightingGpu::publishOccluderDepth(bgfx::TextureHandle depth,bgfx::ProgramHandle program,bgfx::ViewId view) {
    if(!occluderWidth()||!occluderHeight())return true;
    occluderValues_[3]=0;
    if(!usable_||!bgfx::isValid(depth)||!bgfx::isValid(program)||view==UINT16_MAX) {
        status_="Nearest occluder depth publication requires coherent targets and a compute program.";return false;
    }
    constexpr auto sampling=BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|
        BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
    setUniforms();bgfx::setTexture(0,occluderDepthSampler_,depth,static_cast<std::uint32_t>(sampling));
    bgfx::setImage(1,atlas_,0,bgfx::Access::Write,bgfx::TextureFormat::RGBA32F);
    bgfx::dispatch(view,program,(std::uint32_t(occluderWidth())+31)/32,(std::uint32_t(occluderHeight())+7)/8,1);
    occluderValues_[3]=1;return true;
}
void ClusteredLightingGpu::bind(std::uint8_t samplerStage) const {
    if(!usable_)return;
    setUniforms();bgfx::setTexture(samplerStage,sampler_,atlas_);
}
}
