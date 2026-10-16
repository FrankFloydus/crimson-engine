#pragma once
#include "RenderTypes.h"
#include "PointShadowMath.h"
#include "DirectionalShadowMath.h"
#include "ShadowUpdateScheduler.h"
#include <bgfx/bgfx.h>
#include <array>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace crimson {
// Renderer-private GPU owner. A context belongs either to the active world
// (shared by Scene/Game) or to the isolated saved-scene thumbnail world.
class PointShadowRenderer {
public:
    using DrawFace=std::function<std::uint32_t(std::size_t,const Camera&,bgfx::ViewId)>;
    // Each callback first clears only its tile by drawing depth 1, then casters.
    // Per-tile clears retain cached sibling projections in the same view block.
    using DrawCascade=std::function<std::uint32_t(std::size_t,unsigned,const directional_shadow::Cascade&,const point_shadow::TileRect&,bgfx::ViewId)>;
    explicit PointShadowRenderer(bool budgeted=true);
    ~PointShadowRenderer();
    PointShadowRenderer(const PointShadowRenderer&)=delete;
    PointShadowRenderer& operator=(const PointShadowRenderer&)=delete;
    void advance(std::uint64_t frame);
    void render(std::span<const PointLight>,std::span<const DirectionalLight>,std::span<const std::uint64_t> casterSignatures,
        std::uint64_t world,std::uint16_t resolution,std::uint64_t frame,bgfx::ViewId firstView,unsigned cameraCount,const Vec3& focus,const DrawFace&,unsigned actorCapacity=0);
    void renderDirectional(std::span<const DirectionalLight>,std::span<const directional_shadow::Cascades>,std::span<const std::uint64_t>,
        const ShadowCamera&,unsigned cameraContext,bgfx::ViewId firstView,const DrawCascade&);
    using DrawActor=std::function<std::uint32_t(const directional_shadow::Cascade&,const point_shadow::TileRect&,const Vec4& tile,bgfx::ViewId)>;
    void renderActor(unsigned slot,const CharacterShadowSource&,const std::optional<directional_shadow::Cascade>&,
        std::uint64_t casterSignature,bgfx::ViewId,const DrawActor&);
    void bind(bool receiving=true,std::uint8_t samplerSlot=8,unsigned cameraContext=0) const;
    void bindDisabled(std::uint8_t samplerSlot=8) const;
    void setActorAdmission(std::uint32_t requested,std::uint32_t admitted){stats_.actorShadowRequested=requested;stats_.actorShadowAdmitted=admitted;stats_.actorShadowUnadmitted=requested-admitted;}
    void setCasterCount(std::size_t count){stats_.casters=static_cast<std::uint32_t>(count);}
    void invalidate();
    void release();
    std::size_t residentBytes() const;
    const PointShadowStats& stats() const{return stats_;}
    const std::string& status() const{return status_;}
private:
    struct Atlas;
    struct Retired;
    std::unique_ptr<Atlas> atlas_;
    std::vector<Retired> retired_;
    struct Slot {
        std::uint64_t id{},signature{};bool valid{};
        std::uint64_t projectionSignature{},lastUpdatedFrame{};
        std::uint32_t framesDeferred{};
    };
    std::array<Slot,16> slots_{};
    std::array<std::uint64_t,4> directionalIds_{};
    std::array<std::array<std::array<Slot,4>,4>,2> directionalSlots_{};
    std::array<Vec4,16> parameters_{};
    struct DirectionalParameters {
        std::array<Mat4,16> matrices;
        std::array<Vec4,16> rectangles;
        std::array<Vec4,4> splits,parameters;
        Vec4 cameraForward;
    };
    std::array<DirectionalParameters,2> directional_{};
    std::array<Slot,MaximumActorShadows> actorSlots_{};
    std::array<Mat4,MaximumActorShadows> actorMatrices_{};
    std::array<Vec4,MaximumActorShadows> actorRects_{},actorDirections_{},actorParameters_{};
    std::array<std::uint64_t,MaximumActorShadows> lastActorSubmission_{};
    bgfx::UniformHandle sampler_{BGFX_INVALID_HANDLE},parametersUniform_{BGFX_INVALID_HANDLE},atlasUniform_{BGFX_INVALID_HANDLE},layoutUniform_{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle directionalMatrices_{BGFX_INVALID_HANDLE},directionalRects_{BGFX_INVALID_HANDLE},directionalSplits_{BGFX_INVALID_HANDLE},directionalParameters_{BGFX_INVALID_HANDLE},shadowCameraForward_{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle characterMatrixUniform_{BGFX_INVALID_HANDLE},characterRectUniform_{BGFX_INVALID_HANDLE},characterDirectionUniform_{BGFX_INVALID_HANDLE},characterParametersUniform_{BGFX_INVALID_HANDLE};
    PointShadowStats stats_;
    std::string status_;
    std::uint64_t frame_{},world_{},lastRenderedFrame_{UINT64_MAX};
    bool usable_{};
    bool budgeted_{};
    bool ensureAtlas(std::uint16_t resolution,std::uint16_t pointCapacity,std::uint16_t directionalCapacity,unsigned cameraCount,unsigned actorCapacity);
};
}
