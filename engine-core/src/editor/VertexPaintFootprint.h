#pragma once
#include <crimson/renderer/forward/MeshPaint.h>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace crimson {
struct VertexPaintFootprintMesh {
    std::shared_ptr<const MeshPaintGeometry> geometry;
    Mat4 world;
};
struct VertexPaintFootprintSample {
    std::uint32_t target{},cell{},vertex{};
    Vec3 world;
    Vec2 pixel;
};
struct VertexPaintFootprintInfluence {
    std::uint32_t target{},cell{};
    std::array<std::uint32_t,4> vertices{};
    std::array<float,4> basis{};
    std::uint8_t cornerCount{};
    float amount{};
};
struct VertexPaintFootprintResult {
    std::vector<VertexPaintFootprintInfluence> influences;
    std::optional<VertexPaintFootprintSample> missing;
};
// The brush may cover a visible cell interior without reaching any of its
// control vertices. This immutable spatial cache finds those controls while
// retaining the existing geometry and serialized paint representation.
class VertexPaintFootprintCache {
public:
    using Visibility=std::function<std::optional<bool>(const VertexPaintFootprintSample&)>;
    bool prepare(std::span<const VertexPaintFootprintMesh>,const Mat4& viewProjection,std::uint32_t width,std::uint32_t height);
    VertexPaintFootprintResult collect(Vec3 center,float radius,float strength,float hardness,const Visibility&) const;
    void clear();
private:
    struct Cell {std::uint32_t target{},cell{};Aabb bounds;};
    struct Node {Aabb bounds;std::uint32_t first{},count{},escape{};};
    std::vector<VertexPaintFootprintMesh> meshes_;
    std::vector<Cell> cells_;
    std::vector<Node> nodes_;
    Mat4 projection_;
    std::uint32_t width_{},height_{};
    void build(std::uint32_t first,std::uint32_t count);
};
}
