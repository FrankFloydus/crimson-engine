#pragma once
#include "math/Math.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace crimson {
// CPU triangle data. Scene-owned payloads are constructed once through the
// factory, then shared immutably by documents, history, physics and overlays.
struct CollisionMesh {
    std::vector<Vec3> positions;
    std::vector<std::uint32_t> indices;
    Aabb bounds;
    std::uint64_t fingerprint{};
    bool validated() const noexcept { return validated_; }
private:
    bool validated_{};
    friend std::shared_ptr<const CollisionMesh> makeCollisionMesh(std::vector<Vec3>,std::vector<std::uint32_t>);
};
std::shared_ptr<const CollisionMesh> makeCollisionMesh(std::vector<Vec3> positions,std::vector<std::uint32_t> indices);
bool sameCollisionMesh(const std::shared_ptr<const CollisionMesh>&,const std::shared_ptr<const CollisionMesh>&);
}
