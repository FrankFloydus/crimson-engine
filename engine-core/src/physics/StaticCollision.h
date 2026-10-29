#pragma once
#include "scene/Scene.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace crimson {
// Original CPU geometry, independent of render residency and camera LOD.
using StaticTriangleMesh = CollisionMesh;
struct CollisionMeshResult {
    std::shared_ptr<const StaticTriangleMesh> mesh;
    std::uint64_t revision{};
    bool current{};
    std::string diagnostic;
};
using CollisionMeshQuery=std::function<CollisionMeshResult(const std::string&)>;
inline CollisionMeshResult colliderMeshResult(const SceneCollider& collider,const CollisionMeshQuery& query={}) {
    if(collider.mesh)return {collider.mesh,collider.mesh->fingerprint,true,{}};
    return !collider.source.empty() && query ? query(collider.source.id) : CollisionMeshResult{};
}
struct PhysicsMeshCollider {
    EntityId entity{};
    std::shared_ptr<const StaticTriangleMesh> mesh;
    std::uint64_t revision{};
};
struct PhysicsStaticMesh {
    EntityId entity{};
    Mat4 world{};
    std::shared_ptr<const StaticTriangleMesh> mesh;
    bool solid{}; // Closed convex primitives fill their entire rasterized volume.
};
}
