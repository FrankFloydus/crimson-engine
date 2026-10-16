#pragma once
#include "CollisionOverlayTypes.h"
#include <bgfx/bgfx.h>
#include <memory>
#include <span>
#include <string>
namespace crimson {
struct Camera;
class CollisionOverlayGpu final {
public:
    CollisionOverlayGpu();
    ~CollisionOverlayGpu();
    void render(std::span<const RenderCollisionOverlay>,const Camera&,bgfx::ViewId,bgfx::ProgramHandle,std::uint64_t frame);
    void trim(std::uint64_t frame);
    std::size_t residentBytes() const;
    std::uint64_t uploads() const;
    const std::string& status() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace crimson
