#pragma once

#include "RenderTypes.h"
#include <bgfx/bgfx.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace crimson {

// One owner per camera/render target. All resources are private to the renderer
// and must be destroyed before bgfx shutdown. The compute program is borrowed.
// The BRDF and light data share one texture binding, preserving all twelve
// graph material textures and the existing environment/shadow bindings.
class ClusteredLightingGpu {
public:
    static constexpr std::uint32_t MaxLights=4096,TileSize=32,DepthSlices=24,MaxClusters=262144;
    explicit ClusteredLightingGpu(bgfx::ProgramHandle computeProgram);
    ~ClusteredLightingGpu();
    ClusteredLightingGpu(const ClusteredLightingGpu&)=delete;
    ClusteredLightingGpu& operator=(const ClusteredLightingGpu&)=delete;
    void setComputeProgram(bgfx::ProgramHandle program) {
        if(program.idx!=computeProgram_.idx){computeProgram_=program;masksValid_=false;}
    }
    void advance(std::uint64_t frame);
    bool prepare(const Camera&,std::uint16_t width,std::uint16_t height,
        std::span<const PointLight>,std::span<const std::int32_t> shadowIndices,
        std::span<const float> brdfRG,bgfx::ViewId computeView,bool referenceAllLights=false,
        std::uint16_t occluderWidth=0,std::uint16_t occluderHeight=0,
        std::uint16_t ssgiWidth=0,std::uint16_t ssgiHeight=0,
        std::uint16_t ssaoWidth=0,std::uint16_t ssaoHeight=0);
    // Publish four normalized nearest-layer depths per atlas texel, after the
    // private layer has detached and before transparent SceneColor draws.
    bool publishOccluderDepth(bgfx::TextureHandle depth,bgfx::ProgramHandle,bgfx::ViewId);
    void bind(std::uint8_t samplerStage) const;
    void bindUniforms() const {setUniforms();}
    std::size_t residentBytes() const;
    const ClusteredLightingStats& stats() const{return stats_;}
    const std::string& status() const{return status_;}
    bgfx::TextureHandle texture() const{return atlas_;}
    std::uint16_t atlasWidth() const{return atlasWidth_;}
    std::uint16_t atlasHeight() const{return atlasHeight_;}
    std::uint16_t viewportWidth() const{return static_cast<std::uint16_t>(viewportValues_[0]);}
    std::uint16_t viewportHeight() const{return static_cast<std::uint16_t>(viewportValues_[1]);}
    std::uint32_t maskBaseTexel() const{return static_cast<std::uint32_t>(atlasValues_[3]);}
    std::uint32_t ssgiBaseTexel() const{return ssgiBase_;}
    std::uint32_t ssaoBaseTexel() const{return ssaoBase_;}
    std::uint16_t ssaoWidth() const{return ssaoWidth_;}
    std::uint16_t ssaoHeight() const{return ssaoHeight_;}
    std::uint32_t occluderBaseTexel() const{return static_cast<std::uint32_t>(occluderValues_[0]);}
    std::uint16_t occluderWidth() const{return static_cast<std::uint16_t>(occluderValues_[1]);}
    std::uint16_t occluderHeight() const{return static_cast<std::uint16_t>(occluderValues_[2]);}
private:
    struct Retired {bgfx::TextureHandle texture;std::size_t bytes;std::uint64_t after;};
    bgfx::ProgramHandle computeProgram_{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle atlas_{BGFX_INVALID_HANDLE};
    bgfx::DynamicVertexBufferHandle lightInput_{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle sampler_{BGFX_INVALID_HANDLE},gridUniform_{BGFX_INVALID_HANDLE},depthUniform_{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle viewportUniform_{BGFX_INVALID_HANDLE},atlasUniform_{BGFX_INVALID_HANDLE},optionsUniform_{BGFX_INVALID_HANDLE},recordLayoutUniform_{BGFX_INVALID_HANDLE},viewUniform_{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle occluderUniform_{BGFX_INVALID_HANDLE},occluderOptionsUniform_{BGFX_INVALID_HANDLE},occluderDepthSampler_{BGFX_INVALID_HANDLE};
    Vec4 gridValues_{},depthValues_{},viewportValues_{},atlasValues_{},optionsValues_{};
    Vec4 occluderValues_{},occluderOptionsValues_{};
    Mat4 viewValues_{};
    std::vector<Vec4> records_,publishedRecords_,inputSpheres_,currentSpheres_,uploadRows_,brdfPixels_;
    std::vector<PointLight> lightSnapshot_;
    std::vector<std::int32_t> shadowSnapshot_;
    Mat4 recordViewSnapshot_{};
    std::vector<float> brdfSnapshot_;
    std::vector<Retired> retired_;
    std::uint16_t atlasWidth_{},atlasHeight_{},brdfSize_{};
    std::uint64_t frame_{};
    ClusteredLightingStats stats_{};
    std::string status_;
    bool usable_{},sourceSnapshotValid_{},recordsUploaded_{},inputUploaded_{},masksValid_{};
    bool ssgiAllocation_{},ssaoAllocation_{};
    std::uint32_t ssgiBase_{},ssaoBase_{};
    std::uint16_t ssaoWidth_{},ssaoHeight_{};
    bool ensureAtlas(std::uint64_t maskTexels,std::uint16_t brdfSize,std::uint64_t occluderTexels,std::uint64_t ssgiTexels,std::uint64_t ssaoTexels);
    void setUniforms() const;
};
}
