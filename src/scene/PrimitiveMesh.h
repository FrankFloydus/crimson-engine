#pragma once
#include "BuiltinModels.h"
#include "math/Math.h"
#include <cmath>
#include <cstdint>

namespace crimson {
struct PrimitiveMeshSettings {
    Vec3 size{1,1,1};
    float radius{.5f},height{1.f};
    std::uint32_t sides{32},rings{32};
    bool operator==(const PrimitiveMeshSettings&) const = default;
};
inline PrimitiveMeshSettings normalizedPrimitiveMeshSettings(std::string_view id,const PrimitiveMeshSettings& value) {
    PrimitiveMeshSettings result;
    if(id==BuiltinBoxModelId || id=="builtin:mesh-box-v1")result.size=value.size;
    else if(id==BuiltinSphereModelId || id=="builtin:mesh-sphere-v1") {
        result.radius=value.radius;result.sides=value.sides;result.rings=value.rings;
    } else if(id==BuiltinCylinderModelId || id=="builtin:mesh-cylinder-v1") {
        result.radius=value.radius;result.height=value.height;result.sides=value.sides;
    }
    return result;
}
inline bool validPrimitiveMeshSettings(std::string_view id,const PrimitiveMeshSettings& value) {
    if(!isBuiltinModel(id))return false;
    const auto dimension=[](float number){return std::isfinite(number)&&number>=.001f&&number<=100000.f;};
    if(id==BuiltinBoxModelId || id=="builtin:mesh-box-v1")
        return dimension(value.size[0])&&dimension(value.size[1])&&dimension(value.size[2]);
    if(!dimension(value.radius)||value.sides<3||value.sides>256)return false;
    if(id==BuiltinSphereModelId || id=="builtin:mesh-sphere-v1")return value.rings>=2&&value.rings<=256;
    return dimension(value.height);
}
inline Aabb primitiveMeshBounds(std::string_view id,const PrimitiveMeshSettings& value={}) {
    if(!validPrimitiveMeshSettings(id,value))return {};
    const auto settings=normalizedPrimitiveMeshSettings(id,value);
    const auto half=id==BuiltinBoxModelId || id=="builtin:mesh-box-v1"?settings.size*.5f:
        id==BuiltinSphereModelId || id=="builtin:mesh-sphere-v1"?Vec3{settings.radius,settings.radius,settings.radius}:
        Vec3{settings.radius,settings.height*.5f,settings.radius};
    return {half*-1.f,half,true};
}
}
