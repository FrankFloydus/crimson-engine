#pragma once
#include "math/Math.h"

namespace crimson {

enum class ModelTransformStatus { Ready, Collapsed, Invalid };

// Shared by visible and picking draws. Finite collapsed transforms (including
// a zero scale on any ancestor) are legal scene data, but have no normal inverse.
inline ModelTransformStatus prepareModelMatrices(const Mat4& root,const Mat4& node,
                                                 Mat4& world,Mat4& normal,float& handedness) {
    world=math::multiply(root,node);
    if (!math::isAffine(root) || !math::isAffine(node) || !math::isAffine(world)) return ModelTransformStatus::Invalid;
    // Float matrix multiplication can give a collapsed product a tiny apparent
    // determinant. A known collapsed input must remain collapsed after rotation.
    if (math::handedness(root)==0 || math::handedness(node)==0) return ModelTransformStatus::Collapsed;
    handedness=math::handedness(world);
    if (handedness==0) return ModelTransformStatus::Collapsed;
    const auto inverseTranspose=math::normalMatrix(world);
    if (!inverseTranspose) return ModelTransformStatus::Invalid;
    normal=*inverseTranspose;
    return ModelTransformStatus::Ready;
}

} // namespace crimson
