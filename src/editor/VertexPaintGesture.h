#pragma once
#include "scene/SceneDocument.h"
#include "VertexPaintSurface.h"
#include <crimson/renderer/forward/MeshPaint.h>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace crimson {
struct VertexPaintTarget {
    EntityId entity{};
    std::uint32_t draw{},primitive{},sourcePrimitive{};
    std::uint64_t resourceRevision{};
    std::uint32_t materialSlot{};
    std::string materialId;
    std::int32_t sourceNode{-1};
    Mat4 world;
    std::shared_ptr<const MeshPaintGeometry> geometry;
    std::shared_ptr<const MeshPaintPayload> weights;
};
struct VertexPaintTargets {std::vector<VertexPaintTarget> draws;std::string diagnostic;};
using VertexPaintTargetQuery=std::function<VertexPaintTargets(const SceneDocument&,EntityId)>;
struct VertexPaintAlias {std::uint32_t target{},vertex{};};
using VertexPaintSeams=std::vector<std::unordered_map<std::uint32_t,std::vector<VertexPaintAlias>>>;
// Only a shared, oppositely oriented boundary edge within one source mesh
// occurrence can prove a material seam. Coincident points alone cannot.
VertexPaintSeams buildVertexPaintSeams(std::span<const VertexPaintTarget>);
struct VertexPaintBrush {float radius{.5f},strength{.5f},hardness{.5f};std::uint32_t layer{1};};
inline constexpr std::size_t VertexPaintCircleSegments=96;
// The displayed brush boundary is a world-radius circle on the hit tangent
// plane. Perspective and affine mesh scale are applied only by projection.
std::array<Vec3,VertexPaintCircleSegments+1> vertexPaintBrushCircle(Vec3 center,Vec3 normal,float radius);
struct VertexPaintDab {Vec3 center;Vec2 pixel;float screenRadius{};};
struct VertexPaintCandidate {
    std::uint32_t target{},vertex{};
    Vec3 world;
    Vec2 pixel;
};
struct VertexPaintDabChanges {std::uint32_t target{};std::vector<MeshPaintChange> vertices;};
struct VertexPaintBatch {
    std::vector<VertexPaintDabChanges> changes;
    std::optional<VertexPaintCandidate> missing;
};
// Pure immutable-input preparation. No ECS, renderer or UI calls: preparation
// can run on a ResourceManager worker. Visibility is supplied by the exact
// material-aware viewport snapshot, never inferred from selected geometry.
class VertexPaintBrushCache {
public:
    bool prepare(std::span<const VertexPaintTarget>,const Mat4& viewProjection,std::uint32_t width,std::uint32_t height);
    using Visibility=std::function<std::optional<bool>(const VertexPaintCandidate&)>;
    std::optional<VertexPaintCandidate> prepareVisibility(const VertexPaintDab&,const VertexPaintBrush&,const Visibility&);
    VertexPaintBatch apply(std::span<const VertexPaintDab>,const VertexPaintBrush&,
        std::span<const std::shared_ptr<const MeshPaintPayload>>,const Visibility&);
    std::span<const VertexPaintCandidate> candidates() const {return candidates_;}
    void resetVisibility();
    void clear();
private:
    static constexpr std::uint32_t BinSize=32;
    std::uint32_t width_{},height_{},columns_{};
    std::vector<VertexPaintCandidate> candidates_;
    std::vector<std::vector<std::uint32_t>> bins_;
    std::vector<std::int8_t> visibility_;
};
// Owns exactly one SceneDocument transaction. Published immutable weight pages
// are shared with history; cancellation restores the exact original payloads.
class VertexPaintGesture {
public:
    bool begin(SceneDocument&,std::vector<VertexPaintTarget>);
    bool publish(SceneDocument&,std::span<const VertexPaintDabChanges>);
    bool finish(SceneDocument&);
    void cancel(SceneDocument&);
    bool valid(const SceneDocument&) const;
    bool active() const {return owner_!=nullptr;}
    std::span<const VertexPaintTarget> targets() const {return targets_;}
    std::vector<std::shared_ptr<const MeshPaintPayload>> weights() const;
private:
    SceneDocument* owner_{};
    std::uint64_t generation_{},revision_{},transaction_{};
    SceneSelection selection_;
    std::vector<VertexPaintTarget> targets_;
    std::vector<Mat4> ownerWorlds_;
    void reset();
};
}
