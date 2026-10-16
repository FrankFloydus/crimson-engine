#pragma once
#include "crimson-renderer/CollisionOverlayTypes.h"
#include "physics/StaticCollision.h"
#include "resources/ResourceManager.h"
#include <array>
#include <map>
#include <span>
#include <unordered_map>

namespace crimson {
struct PreparedCollisionOverlays {
    std::vector<RenderCollisionOverlay> scene,game;
};
// Prepares immutable CPU geometry once; the renderer owns its GPU residency.
// Trimesh adjacency extraction runs on resource workers, never in editor draw.
class CollisionOverlays {
public:
    explicit CollisionOverlays(ResourceManager& resources);
    PreparedCollisionOverlays prepare(const Scene&,std::span<const EntityId> selection,
        bool enabled,const CollisionMeshQuery&);
private:
    struct PrimitiveEntry {std::shared_ptr<const CollisionOverlayGeometry> geometry;std::uint64_t used{};};
    struct MeshEntry {
        std::weak_ptr<const StaticTriangleMesh> source;
        ResourceHandle<CollisionOverlayGeometry> pending;
        std::shared_ptr<const CollisionOverlayGeometry> accepted;
        std::uint64_t revision{},used{};
    };
    ResourceManager& resources_;
    std::uint64_t owner_{},frame_{},job_{};
    std::map<std::array<std::uint32_t,4>,PrimitiveEntry> primitives_;
    std::unordered_map<std::string,MeshEntry> meshes_;
    std::shared_ptr<const CollisionOverlayGeometry> primitive(const SceneCollider&);
    std::shared_ptr<const CollisionOverlayGeometry> mesh(const std::string&,const CollisionMeshResult&);
};
}
