#pragma once
#include "scene/Scene.h"
#include <crimson/renderer/forward/ShadowRayGeometry.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace crimson {
class SceneDocument;
struct VertexPaintSurfaceHit {
    EntityId entity{};
    std::uint32_t draw{},primitive{};
    std::uint64_t resourceRevision{};
    Vec3 worldPosition{},worldNormal{};
    bool exact{true};
};
struct VertexPaintSurfaceSnapshot {
    std::uint64_t signature{};
    bool ready{};
    std::string diagnostic;
    std::function<std::optional<VertexPaintSurfaceHit>(const Ray&)> raycast;
    std::function<std::optional<VertexPaintSurfaceHit>(const Ray&,EntityId,std::uint32_t,std::uint32_t)> raycastDraw;
};
using VertexPaintSurfaceQuery=std::function<VertexPaintSurfaceSnapshot(const SceneDocument&)>;
// The immutable BVH and transform belong to this frozen viewport occurrence.
// A null BVH is a conservative unsupported bound, requiring a GPU hover pick.
struct VertexPaintSurfaceMesh {
    std::shared_ptr<const ShadowRayMesh> mesh;
    Mat4 world{},inverse{};
    Aabb bounds;
    EntityId entity{};
    std::uint32_t draw{},primitive{};
    std::uint64_t resourceRevision{};
    bool doubleSided{},exact{true};
};
VertexPaintSurfaceSnapshot makeVertexPaintSurfaceSnapshot(std::vector<VertexPaintSurfaceMesh>,std::uint64_t signature);
}
