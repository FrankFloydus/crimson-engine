#include "PointShadowMath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace crimson::point_shadow {
namespace {
constexpr std::array<Vec3,6> directions{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
constexpr std::array<Vec3,6> ups{{{0,1,0},{0,1,0},{0,0,-1},{0,0,1},{0,1,0},{0,1,0}}};
bool validBounds(const Aabb& box) {
    return box.valid && math::finite(box.minimum) && math::finite(box.maximum) &&
        box.minimum[0]<=box.maximum[0] && box.minimum[1]<=box.maximum[1] && box.minimum[2]<=box.maximum[2];
}
}
AtlasLayout atlasLayout(unsigned resolution,unsigned capacity) {
    if (!resolution || !capacity || capacity>MaxLights || resolution>65535-2*GuardTexels) return {};
    unsigned rounded=1;while (rounded<capacity) rounded*=2;
    const unsigned blockColumns=rounded==1 ? 1:rounded<=4 ? 2:4;
    const unsigned columns=blockColumns*3,rows=(rounded/blockColumns)*2,pitch=resolution+2*GuardTexels;
    if (pitch>65535/columns || pitch>65535/rows) return {};
    return {static_cast<std::uint16_t>(resolution),static_cast<std::uint16_t>(pitch),
        static_cast<std::uint16_t>(columns),static_cast<std::uint16_t>(rows),
        static_cast<std::uint16_t>(pitch*columns),static_cast<std::uint16_t>(pitch*rows),static_cast<std::uint16_t>(rounded)};
}
TileRect tileRect(const AtlasLayout& atlas,unsigned lightSlot,unsigned face) {
    if (!atlas.columns || !atlas.pitch || lightSlot>=atlas.capacity || face>=FaceCount) return {};
    const auto blockColumns=atlas.columns/3;
    if (!blockColumns) return {};
    return {static_cast<std::uint16_t>(((lightSlot%blockColumns)*3+face%3)*atlas.pitch),
        static_cast<std::uint16_t>(((lightSlot/blockColumns)*2+face/3)*atlas.pitch),atlas.pitch,atlas.pitch};
}
float nearPlane(float radius) {
    if (!std::isfinite(radius) || radius<=0) return 0;
    return std::min(.05f,radius*.01f);
}
float faceTanHalfFov(unsigned resolution) {
    return resolution ? 1.f+float(2*GuardTexels)/float(resolution) : 1.f;
}
unsigned faceIndex(const Vec3& direction) {
    if (!math::finite(direction)) return 0;
    const auto x=std::abs(direction[0]),y=std::abs(direction[1]),z=std::abs(direction[2]);
    if (x>=y && x>=z) return direction[0]>=0 ? 0:1;
    if (y>=z) return direction[1]>=0 ? 2:3;
    return direction[2]>=0 ? 4:5;
}
Vec2 faceCoordinates(const Vec3& direction) {
    if (!math::finite(direction)) return {.5f,.5f};
    const auto face=faceIndex(direction);const auto distance=math::dot(direction,directions[face]);
    if (!(distance>0)) return {.5f,.5f};
    const auto right=math::cross(directions[face],ups[face]);
    return {.5+.5*math::dot(direction,right)/distance,.5-.5*math::dot(direction,ups[face])/distance};
}
Camera faceCamera(Vec3 position,float radius,unsigned face,std::uint16_t resolution) {
    face=std::min(face,FaceCount-1);Camera camera;
    camera.position=position;camera.target=position+directions[face];camera.up=ups[face];
    camera.fov=static_cast<float>(2*std::atan(faceTanHalfFov(resolution))*180/std::numbers::pi);
    camera.fovAxis=CameraFovAxis::Vertical;camera.nearPlane=nearPlane(radius);camera.farPlane=radius;
    return camera;
}
float projectedDepth(float distance,float nearValue,float farValue) {
    if (!std::isfinite(distance) || !std::isfinite(nearValue) || !std::isfinite(farValue) ||
        distance<=0 || nearValue<=0 || farValue<=nearValue) return std::numeric_limits<float>::quiet_NaN();
    return static_cast<float>((double(farValue)/(farValue-nearValue))*(1-double(nearValue)/distance));
}
Vec2 receiverPlaneGradient(Vec3 point,Vec3 normal,unsigned face,unsigned resolution,float nearValue,float farValue) {
    if (face>=FaceCount || !resolution || !math::finite(point) || !math::finite(normal) ||
        !std::isfinite(nearValue) || !std::isfinite(farValue) || nearValue<=0 || farValue<=nearValue) return {};
    normal=math::normalized(normal,{});
    const double distance=math::dot(normal,point),radial=std::sqrt(math::dot(point,point));
    if (std::abs(distance)<=std::max(radial*1e-6,1e-6)) return {};
    const auto right=math::cross(directions[face],ups[face]);
    const double factor=2.0*farValue*nearValue/((farValue-nearValue)*double(resolution)*distance);
    return {-math::dot(normal,right)*factor,math::dot(normal,ups[face])*factor};
}
bool intersectsLight(const Aabb& box,const Vec3& position,float radius) {
    if (!validBounds(box) || !math::finite(position) || !std::isfinite(radius) || radius<=0) return true;
    double squared=0;
    for (unsigned axis=0;axis<3;++axis) {
        const double distance=double(position[axis])-std::clamp(position[axis],box.minimum[axis],box.maximum[axis]);
        squared+=distance*distance;
    }
    return squared<=double(radius)*radius;
}
bool intersectsFace(const Aabb& box,const Camera& camera) {
    if (!validBounds(box)) return true;
    const auto matrix=camera.projectionMatrix(1,ClipDepth::ZeroToOne)*camera.viewMatrix();
    if (!math::finite(matrix)) return true;
    std::array<Vec4,8> corners;
    for (unsigned i=0;i<8;++i) corners[i]=math::transformHomogeneous(matrix,{(i&1)?box.maximum[0]:box.minimum[0],
        (i&2)?box.maximum[1]:box.minimum[1],(i&4)?box.maximum[2]:box.minimum[2],1});
    for (unsigned plane=0;plane<6;++plane) {
        const bool outside=std::all_of(corners.begin(),corners.end(),[&](const Vec4& p) {
            switch (plane) {case 0:return p[0]<-p[3];case 1:return p[0]>p[3];case 2:return p[1]<-p[3];
                case 3:return p[1]>p[3];case 4:return p[2]<0;default:return p[2]>p[3];}
        });
        if (outside) return false;
    }
    return true;
}
} // namespace crimson::point_shadow
