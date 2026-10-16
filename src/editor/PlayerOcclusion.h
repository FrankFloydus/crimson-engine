#pragma once
#include "PlayerOcclusionSettings.h"
#include "scene/SceneExtraction.h"
#include "resources/ResourceManager.h"
#include <unordered_map>

namespace crimson {
struct PlayerOcclusionGeometry;
using PlayerOcclusionOpacities=std::unordered_map<EntityId,float>;
struct PlayerOcclusionStats {
    std::size_t boundsCandidates{},nodeTests{},triangleTests{},cachedMeshes{},pendingMeshes{};
};
// Transient Game-camera visibility. Original triangles and their immutable
// worker-built BVH are shared by all instances of an asset; no scene is edited.
class PlayerOcclusion {
public:
    explicit PlayerOcclusion(ResourceManager&);
    const PlayerOcclusionOpacities& update(const Scene&,std::span<const ExtractedModel>,const Camera&,
        std::uint64_t documentGeneration,float seconds,const ModelBoundsQuery&,const CollisionMeshQuery&,
        const PlayerOcclusionSettings& settings={});
    void clear();
    const PlayerOcclusionStats& stats()const{return stats_;}
private:
    struct MeshEntry {
        std::weak_ptr<const StaticTriangleMesh> source;
        ResourceHandle<PlayerOcclusionGeometry> pending;
        std::shared_ptr<const PlayerOcclusionGeometry> accepted;
        std::uint64_t revision{},used{},queried{};
    };
    struct Fade {double opacity{1},target{1},clearSeconds{};bool obstructed{};};
    ResourceManager& resources_;
    std::uint64_t owner_{},job_{},frame_{},generation_{};
    EntityId player_{},camera_{};
    std::unordered_map<std::string,MeshEntry> meshes_;
    std::unordered_map<EntityId,Fade> fades_;
    PlayerOcclusionOpacities opacities_;
    PlayerOcclusionStats stats_;
    const PlayerOcclusionGeometry* geometry(const std::string&,const CollisionMeshQuery&);
};
}
