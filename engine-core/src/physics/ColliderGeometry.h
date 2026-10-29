#pragma once
#include "StaticCollision.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace crimson {
inline Aabb primitiveColliderBounds(const SceneCollider& collider) {
    if(collider.shape==ColliderShape::Trimesh)return {};
    Vec3 half=collider.size*.5f;
    if(collider.shape==ColliderShape::Sphere)half={collider.radius,collider.radius,collider.radius};
    else if(collider.shape!=ColliderShape::Box)half={collider.radius,collider.height*.5f,collider.radius};
    return {half*-1.f,half,true};
}
// Closed, outward-facing local geometry. Created only when an accepted static
// collider changes; navigation and nonuniform Jolt convex shapes share it.
inline std::shared_ptr<const StaticTriangleMesh> primitiveColliderMesh(const SceneCollider& collider,
    unsigned radial=32,unsigned hemisphereBands=8) {
    auto mesh=std::make_shared<StaticTriangleMesh>();mesh->bounds=primitiveColliderBounds(collider);
    if(collider.shape==ColliderShape::Box || collider.shape==ColliderShape::Trimesh)return mesh;
    const bool cylinder=collider.shape==ColliderShape::Cylinder;
    const float radius=collider.radius;
    const float height=collider.shape==ColliderShape::Sphere?2*radius:collider.height;
    const float stem=cylinder?height*.5f:std::max(0.f,height*.5f-radius);
    mesh->positions.emplace_back(0,-height*.5f,0);
    const auto ring=[&](float ringRadius,float y) {
        for(unsigned segment=0;segment<radial;++segment) {
            const auto angle=segment*2*std::numbers::pi_v<float>/radial;
            mesh->positions.emplace_back(ringRadius*std::cos(angle),y,ringRadius*std::sin(angle));
        }
    };
    if(cylinder) {ring(radius,-stem);ring(radius,stem);}
    else {
        for(unsigned band=1;band<=hemisphereBands;++band) {
            const auto angle=band*std::numbers::pi_v<float>/(2*hemisphereBands);
            ring(radius*std::sin(angle),-stem-radius*std::cos(angle));
        }
        if(stem>0)ring(radius,stem);
        for(unsigned band=1;band<hemisphereBands;++band) {
            const auto angle=band*std::numbers::pi_v<float>/(2*hemisphereBands);
            ring(radius*std::cos(angle),stem+radius*std::sin(angle));
        }
    }
    const auto top=static_cast<std::uint32_t>(mesh->positions.size());mesh->positions.emplace_back(0,height*.5f,0);
    const auto rings=(top-1)/radial;
    const auto vertex=[&](unsigned row,unsigned segment){return 1+row*radial+segment%radial;};
    const auto triangle=[&](std::uint32_t a,std::uint32_t b,std::uint32_t c){mesh->indices.insert(mesh->indices.end(),{a,b,c});};
    for(unsigned segment=0;segment<radial;++segment) {
        triangle(0,vertex(0,segment),vertex(0,segment+1));
        for(unsigned row=0;row+1<rings;++row) {
            const auto a=vertex(row,segment),b=vertex(row,segment+1),c=vertex(row+1,segment),d=vertex(row+1,segment+1);
            triangle(a,c,b);triangle(b,c,d);
        }
        triangle(vertex(rings-1,segment),top,vertex(rings-1,segment+1));
    }
    return mesh;
}
} // namespace crimson
