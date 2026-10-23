#pragma once

#include "math/Math.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace crimson::decals {

// Stable unit surface frame: world-up on walls, world-X on near-horizontal surfaces.
inline Mat4 surfacePlacementFrame(Vec3 point,Vec3 normal) {
    const auto z=math::normalized(normal,{0,0,1});
    const bool horizontal=std::abs(z[1])>.95f;
    const Vec3 reference=horizontal?Vec3{1,0,0}:Vec3{0,1,0};
    const auto x=horizontal?math::normalized(reference-z*math::dot(reference,z),{1,0,0}):math::normalized(math::cross(reference,z),{1,0,0});
    const auto y=math::normalized(math::cross(z,x),{0,1,0});
    Mat4 result;
    for(std::size_t axis=0;axis<3;++axis){result.at(axis,0)=x[axis];result.at(axis,1)=y[axis];result.at(axis,2)=z[axis];result.at(axis,3)=point[axis];}
    return result;
}

// Canonical editable footprint: keep the visible XY metric, shear and reflection,
// replace the unused tilted/deep Z column with the unit surface normal, and keep
// the authored world translation. Merely displaying this frame never edits it.
inline std::optional<Mat4> decalAuthoringFrame(const Mat4& authored,Vec3 normal) {
    if(!math::isAffine(authored)||!math::finite(normal)||math::length(normal)<1e-8)return {};
    const auto x=math::transformDirection(authored,{1,0,0}),y=math::transformDirection(authored,{0,1,0}),z=math::normalized(normal);
    const double lengthX=math::length(x),lengthY=math::length(y);if(lengthX<1e-6||lengthY<1e-6)return {};
    const double shear=math::dot(x,y)/lengthX,orthogonal=std::sqrt(std::max(0.,lengthY*lengthY-shear*shear));if(orthogonal<1e-6)return {};
    auto tangent=x-z*math::dot(x,z);if(math::length(tangent)<lengthX*1e-5)tangent=math::cross(y,z);
    if(math::length(tangent)<1e-8)tangent=math::transformDirection(surfacePlacementFrame({},z),{1,0,0});tangent=math::normalized(tangent);
    const auto first=tangent*lengthX,second=tangent*shear+math::cross(z,tangent)*(orthogonal*(math::handedness(authored)<0?-1:1));
    Mat4 result;
    for(std::size_t axis=0;axis<3;++axis){result.at(axis,0)=first[axis];result.at(axis,1)=second[axis];result.at(axis,2)=z[axis];result.at(axis,3)=authored.at(axis,3);}
    return result;
}

// Handles always use an orthonormal right-handed basis, including mirrored or
// sheared footprints. Their Z axis is the receiver normal rather than depth.
inline std::optional<Mat4> decalGizmoFrame(const Mat4& authoringFrame) {
    const auto frame=decalAuthoringFrame(authoringFrame,math::transformDirection(authoringFrame,{0,0,1}));
    if(!frame)return {};
    const auto x=math::normalized(math::transformDirection(*frame,{1,0,0}));
    const auto z=math::transformDirection(*frame,{0,0,1}),y=math::normalized(math::cross(z,x));
    Mat4 result=*frame;
    for(std::size_t axis=0;axis<3;++axis){result.at(axis,0)=x[axis];result.at(axis,1)=y[axis];}
    return result;
}

// Valid authoring frames expose positive width and signed height; the height
// sign carries reflection without making the image's rotation ambiguous.
inline Vec2 decalSize(const Mat4& authoringFrame) {
    return {static_cast<float>(math::length(math::transformDirection(authoringFrame,{1,0,0}))),
        static_cast<float>(math::length(math::transformDirection(authoringFrame,{0,1,0})))*(math::handedness(authoringFrame)<0?-1.f:1.f)};
}

// The optional reference retains a gesture's angle branch across +/-180 degrees.
inline float decalRotationDegrees(const Mat4& authoringFrame,float referenceDegrees=0) {
    const auto z=math::transformDirection(authoringFrame,{0,0,1}),x=math::transformDirection(authoringFrame,{1,0,0});
    const auto stable=surfacePlacementFrame({},z);
    const auto stableX=math::transformDirection(stable,{1,0,0}),stableY=math::transformDirection(stable,{0,1,0});
    double degrees=std::atan2(math::dot(x,stableY),math::dot(x,stableX))*(180./std::numbers::pi_v<double>);
    if(math::finite(referenceDegrees))degrees+=360.*std::round((referenceDegrees-degrees)/360.);
    return static_cast<float>(degrees);
}

inline std::optional<Mat4> withDecalRotation(const Mat4& authoringFrame,float degrees) {
    if(!math::finite(degrees))return {};
    auto result=decalAuthoringFrame(authoringFrame,math::transformDirection(authoringFrame,{0,0,1}));
    if(!result)return {};
    const auto normal=math::transformDirection(*result,{0,0,1});
    const double radians=std::remainder(double(degrees)-decalRotationDegrees(*result),360.)*(std::numbers::pi_v<double>/180.);
    for(std::size_t column=0;column<2;++column){
        const auto rotated=math::rotate({result->at(0,column),result->at(1,column),result->at(2,column)},normal,radians);
        for(std::size_t axis=0;axis<3;++axis)result->at(axis,column)=rotated[axis];
    }
    return result;
}

// Independent column scaling preserves the angle between the XY axes. Negative
// sizes deliberately reflect the corresponding axis; collapsed sizes are invalid.
inline std::optional<Mat4> withDecalSize(const Mat4& authoringFrame,Vec2 size) {
    if(!math::finite(size)||std::abs(size[0])<1e-6f||std::abs(size[1])<1e-6f)return {};
    auto result=decalAuthoringFrame(authoringFrame,math::transformDirection(authoringFrame,{0,0,1}));
    if(!result)return {};
    const auto previous=decalSize(*result);
    for(std::size_t column=0;column<2;++column){
        const double ratio=double(size[column])/previous[column];
        for(std::size_t axis=0;axis<3;++axis)result->at(axis,column)=static_cast<float>(result->at(axis,column)*ratio);
    }
    return math::finite(*result)?result:std::nullopt;
}

} // namespace crimson::decals
