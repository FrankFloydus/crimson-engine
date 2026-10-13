#pragma once

#include "Scene.h"

#include <functional>
#include <span>

namespace crimson {

struct ExtractedModel {
    EntityId id{};
    std::string assetId;
    Mat4 world{};
};
struct ExtractedScene {
    std::vector<ExtractedModel> models;
    std::vector<DirectionalLight> directional;
    std::vector<PointLight> points;
};

// The scene owns ECS traversal. Rendering receives copied presentation values,
// with no EnTT handles, registry pointers, component pointers, or GPU ownership.
ExtractedScene extractScene(const Scene& scene);

// Direction in which emitted light travels. Use the authored local -Z axis,
// including affine parent transforms; scale affects its length, not intensity.
// The viewport marker and render extraction must consume this same direction.
Vec3 directionalLightDirection(const Mat4& world);

using ModelBoundsQuery = std::function<std::optional<Aabb>(const std::string&)>;
// Parent selections include descendants once. Missing models, empty entities
// and lights get a small, fixed world-space box around their world position.
Aabb selectionBounds(const Scene& scene, std::span<const EntityId> selection,
                     const ModelBoundsQuery& modelBounds);

} // namespace crimson
