#pragma once
#include "RenderTypes.h"
#include "PointShadowMath.h"
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
    PointShadowRenderer();
    ~PointShadowRenderer();
    PointShadowRenderer(const PointShadowRenderer&)=delete;
    PointShadowRenderer& operator=(const PointShadowRenderer&)=delete;
    void advance(std::uint64_t frame);
    void render(std::span<const PointLight>,std::span<const std::uint64_t> casterSignatures,
        std::uint64_t world,std::uint16_t resolution,std::uint64_t frame,bgfx::ViewId firstView,const DrawFace&);
    void bind(bool receiving=true) const;
    void bindDisabled() const;
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
    struct Slot {std::uint64_t id{},signature{};bool valid{};};
    std::array<Slot,16> slots_{};
    std::array<Vec4,16> parameters_{};
    bgfx::UniformHandle sampler_{BGFX_INVALID_HANDLE},parametersUniform_{BGFX_INVALID_HANDLE},atlasUniform_{BGFX_INVALID_HANDLE},layoutUniform_{BGFX_INVALID_HANDLE};
    PointShadowStats stats_;
    std::string status_;
    std::uint64_t frame_{},world_{},lastRenderedFrame_{UINT64_MAX};
    bool ensureAtlas(std::uint16_t resolution,std::uint16_t capacity);
};
}
