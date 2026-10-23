#pragma once
#include "DecalSurface.h"
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace crimson {
struct ModelAsset;
struct ModelPrimitive;
namespace decals {
enum class SourceNormals { UseMeshNormals, Flat, Smooth };
struct SourceBvhNode {
    Aabb bounds;
    uint32_t first{},count{},left{},right{};
};
struct ModelDrawSurface {
    SurfaceReceiver surface; // Entity is assigned by the scene occurrence; transform is the model draw transform.
    uint32_t material{};
    Aabb bounds;
    std::vector<uint32_t> order;
    std::vector<SourceBvhNode> nodes;
};
struct ModelSurface {
    uint64_t fingerprint{};
    std::vector<ModelDrawSurface> draws;
    std::string diagnostic;
    size_t bytes() const noexcept;
};
// The loader is invoked only for stripped original geometry, on the calling resource worker.
using OriginalPrimitiveLoader=std::function<std::shared_ptr<const ModelPrimitive>(uint32_t,std::stop_token)>;
ModelSurface buildModelSurface(const ModelAsset&,uint64_t fingerprint,SourceNormals,
    const OriginalPrimitiveLoader& original={},std::stop_token stop={});
// Conservative BVH query. Returned indices address surface.triangles, whose triangle IDs retain source provenance.
std::vector<uint32_t> querySurfaceTriangles(const ModelDrawSurface&,const Mat4& entityWorld,const Aabb& worldBounds);
struct ModelSurfaceHit {
    DecalSeed seed;
    Vec3 localPoint{},localNormal{}; // Model-root coordinates, including the source draw transform once.
};
enum class SurfaceHitStatus { Found,Missing,Ambiguous };
struct SurfaceHitResult {
    SurfaceHitStatus status{SurfaceHitStatus::Missing};
    std::optional<ModelSurfaceHit> hit;
};
// Match an actual GPU-visible position, not the nearest geometric intersection before alpha coverage.
SurfaceHitResult resolveModelSurfaceHit(const ModelSurface&,uint64_t entity,const Mat4& entityWorld,
    uint32_t draw,uint32_t primitive,Vec3 visibleWorldPoint,float tolerance=0);
}
}
