#pragma once
#include "ModelAsset.h"

namespace crimson {
struct ModelPose {
    std::vector<Mat4> localTransforms,worldTransforms;
    std::vector<std::vector<float>> nodeWeights;
    // Per mesh node: inverse(mesh world) * joint world * inverse bind.
    // A singular mesh world uses model-space palettes and identity draw
    // transforms, preserving glTF's cancellation of the skin mesh transform.
    std::vector<std::vector<Mat4>> skinPalettes;
    std::vector<Mat4> drawTransforms;
    Aabb bounds;
    std::uint64_t signature{};
};
// Shared import/cache validation; does not require resident vertex arrays.
// Evaluation consumes this immutable validated metadata without rechecking all
// clip key values on every frame.
void validateModelAnimationData(const ModelAsset&);
// No clip evaluates authored transforms, weights and bind pose. Finite times
// outside a clip clamp, or wrap relative to its first timestamp when looping.
ModelPose evaluateModelPose(const ModelAsset&,std::optional<std::size_t> clip=std::nullopt,
    double seconds=0,bool loop=true);
// CPU reference/dependent consumers. Rendering uses GPU deformation; this
// helper evaluates morph deltas then skinning into the mesh node's local space.
ModelPrimitive deformModelPrimitive(const ModelAsset&,std::size_t primitive,std::size_t node,const ModelPose&);
}
