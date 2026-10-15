#pragma once
#include "scene/Scene.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace crimson {
// Original CPU geometry, independent of render residency and camera LOD.
struct StaticTriangleMesh {
    std::vector<Vec3> positions;
    std::vector<std::uint32_t> indices;
    Aabb bounds;
    std::uint64_t fingerprint{};
};
struct CollisionMeshResult {
    std::shared_ptr<const StaticTriangleMesh> mesh;
    std::uint64_t revision{};
    bool current{};
    std::string diagnostic;
};
using CollisionMeshQuery=std::function<CollisionMeshResult(const std::string&)>;
struct PhysicsMeshCollider {
    EntityId entity{};
    std::shared_ptr<const StaticTriangleMesh> mesh;
    std::uint64_t revision{};
};
struct PhysicsStaticMesh {
    EntityId entity{};
    Mat4 world{};
    std::shared_ptr<const StaticTriangleMesh> mesh;
};
}
