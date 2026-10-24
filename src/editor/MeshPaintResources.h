#pragma once
#include "VertexPaintGesture.h"
#include "resources/ResourceManager.h"
#include "crimson-renderer/CollisionOverlayTypes.h"
#include <crimson/renderer/forward/ForwardTypes.hpp>
#include <chrono>
#include <unordered_map>

namespace crimson {
class AssetLibrary;
struct ExtractedModel;
struct MeshPaintPreparedDraw {
    std::uint32_t draw{},sourcePrimitive{},primitive{};
    std::shared_ptr<const MeshPaintGeometry> geometry;
    std::shared_ptr<const MeshPaintPayload> weights;
    bool compatible{true};
};
struct PreparedMeshPaintModel {
    std::shared_ptr<const ModelAsset> asset;
    std::vector<MeshPaintPreparedDraw> draws;
    std::vector<RenderMeshPaintBinding> bindings;
    std::uint64_t revision{},signature{};
    bool ready{},failed{};
    std::string diagnostic;
};
class MeshPaintResources {
public:
    MeshPaintResources(AssetLibrary&,ResourceManager&);
    PreparedMeshPaintModel prepare(const EntityRecord&,ResourcePriority=ResourcePriority::Visible);
    PreparedMeshPaintModel prepare(const ExtractedModel&,ResourcePriority=ResourcePriority::Visible);
    VertexPaintTargets targets(const SceneDocument&,EntityId);
    std::vector<RenderCollisionOverlay> subdivisionOverlays(const SceneDocument&,std::span<const EntityId>);
    struct SurfaceBounds {Aabb bounds;std::uint64_t signature{};};
    using SurfaceBoundsQuery=std::function<SurfaceBounds(const ExtractedModel&,const std::shared_ptr<const ModelAsset>&)>;
    VertexPaintSurfaceSnapshot surfaces(const SceneDocument&,const SurfaceBoundsQuery& bounds={});
    bool subdivision(const EntityRecord&,std::uint32_t level,SceneMeshPaint&,std::string& diagnostic);
    std::string status(EntityId) const;
    void collect();
    void clear();
private:
    struct Entry {ResourceHandle<ModelAsset> resource;std::chrono::steady_clock::time_point used;EntityId entity{};bool repairRequested{};};
    struct SubdivisionOverlayEntry {
        std::weak_ptr<const MeshPaintGeometry> source;
        ResourceHandle<CollisionOverlayGeometry> resource;
        std::chrono::steady_clock::time_point used;
        std::size_t reservedBytes{};
    };
    AssetLibrary& assets_;ResourceManager& resources_;
    std::unordered_map<std::string,Entry> geometry_,resampling_;
    std::unordered_map<std::string,Entry> surfaceModels_;
    std::unordered_map<const MeshPaintGeometry*,SubdivisionOverlayEntry> subdivisionOverlays_;
    std::size_t subdivisionOverlayBytes_{};
    std::uint64_t subdivisionOverlayOwner_{},subdivisionOverlayJob_{};
    VertexPaintSurfaceSnapshot surfaceSnapshot_;
    std::uint64_t surfaceSnapshotKey_{};
    std::unordered_map<EntityId,std::string> statuses_;
    ResourceHandle<ModelAsset> request(const EntityRecord&,std::uint32_t,ResourcePriority,bool resample,std::string&);
};
}
