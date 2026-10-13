#include "math/Math.h"
#include "math/Json.h"
#include "assets/ModelAsset.h"
#include "crimson-renderer/RenderTypes.h"

// Consumers of engine math require neither a GLM header nor its configuration.
#ifdef GLM_VERSION
#error Public engine headers exposed the GLM backend.
#endif

// The old backend is an independent projection oracle, only in tests.
#include <bx/math.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {
using namespace crimson;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void near(double a, double b, double tolerance=1e-5) {
    if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a-b)>tolerance)
        throw std::runtime_error(std::to_string(a)+" differs from "+std::to_string(b));
}
void same(const Vec3& a, const Vec3& b, double tolerance=1e-5) { for (std::size_t i=0;i<3;++i) near(a[i],b[i],tolerance); }
void same(const Mat4& a, const Mat4& b, double tolerance=1e-5) { for (std::size_t i=0;i<16;++i) near(a[i],b[i],tolerance); }
void layoutAndSerialization() {
    static_assert(sizeof(Vec2)==8 && sizeof(Vec3)==12 && sizeof(Vec4)==16 && sizeof(Quat)==16);
    static_assert(sizeof(Mat3)==36 && sizeof(Mat4)==64 && sizeof(ModelVertex)==72);
    static_assert(offsetof(ModelVertex,normal)==12 && offsetof(ModelVertex,uv0)==24 && offsetof(ModelVertex,uv1)==32);
    static_assert(offsetof(ModelVertex,tangent)==40 && offsetof(ModelVertex,color)==56);
    static_assert(std::is_standard_layout_v<ModelVertex> && std::is_trivially_copyable_v<ModelVertex>);
    constexpr Vec3 color{.25f,.5f,1};
    const nlohmann::json json=color;
    require(json==nlohmann::json::array({.25,.5,1}),"Existing vector JSON shape changed");
    require(json.get<Vec3>()==color,"Vector JSON round trip failed");
    auto transform=math::translation({3,-2,9})*math::scaling({-2,3,4}); transform[4]=.5f;
    const nlohmann::json matrix=transform;
    require(matrix.size()==16 && matrix[12]==3 && matrix.get<Mat4>()==transform,"Matrix column-major JSON changed");
    require(nlohmann::json(Quat{})==nlohmann::json::array({0,0,0,1}),"Quaternion storage order changed");
    bool rejected=false;
    try { static_cast<void>(nlohmann::json::array({1,2}).get<Vec3>()); } catch (const std::exception&) { rejected=true; }
    require(rejected,"Malformed vector JSON accepted");
    require(Mat4{}==Mat4::identity() && Mat4::zero()[0]==0 && Mat4{}[0]==1,"Matrix identity/zero defaults changed");
}
void vectorsAndRobustFallbacks() {
    same(math::cross({1,0,0},{0,1,0}),{0,0,1});
    same(math::normalized({0,0,0},{0,0,-1}),{0,0,-1});
    same(math::normalized({std::numeric_limits<float>::infinity(),0,0},{1,0,0}),{1,0,0});
    same(math::normalized({1e-30f,0,0},{0,0,-1},1e-20),{0,0,-1});
    same(math::normalized({1e30f,0,0}),{1,0,0});
    near(math::length({3,4,0}),5);
    same(math::rotate({1,0,0},{0,1,0},std::numbers::pi/2),{0,0,-1});
    same(math::transformDirection(math::rotation(math::axisAngle({0,1,0},std::numbers::pi/2)),{1,0,0}),{0,0,-1});
    const Vec3 ibl{.25f,.5f,.75f};
    const float squared=.25f*.25f+.5f*.5f+.75f*.75f;
    near(math::dotFloat(ibl,ibl),squared,0);
    for (std::size_t i=0;i<3;++i) near(math::normalizedFloat(ibl)[i],ibl[i]*(1/std::sqrt(squared)),0);
}
void affineTransformsAndNormals() {
    auto parent=math::translation({3,4,5})*math::rotation(math::axisAngle({0,1,0},.6))*math::scaling({-2,3,.5f});
    parent[4]+=.7f; // A sheared, mirrored parent must remain affine and invertible.
    const auto local=math::translation({2,-1,6})*math::rotation(math::axisAngle({1,0,0},.4));
    const auto world=parent*local;
    require(math::isAffine(world) && math::handedness(world)==-1,"Mirrored affine transform rejected");
    const auto inverse=math::inverse(parent);
    require(inverse.has_value(),"Invertible affine transform rejected");
    same(*inverse*world,local,3e-5);
    same(math::transformPoint(*inverse,math::transformPoint(parent,{1,2,3})),{1,2,3},3e-5);
    const auto normal=math::normalMatrix(world);
    require(normal.has_value(),"Normal matrix failed");
    const auto transformedNormal=math::transformDirection(*normal,{0,0,1});
    near(math::dot(transformedNormal,math::transformDirection(world,{1,0,0})),0,1e-5);
    near(math::dot(transformedNormal,math::transformDirection(world,{0,1,0})),0,1e-5);
    const auto singular=math::scaling({1,0,1});
    require(!math::inverse(singular) && !math::normalMatrix(singular) && math::handedness(singular)==0,"Singular transform accepted");
    require(math::inverse(math::scaling({1e-12f,2e-12f,3e-12f})).has_value(),"Small well-conditioned transform rejected");
    Mat4 nearlyParallel; nearlyParallel[4]=1; nearlyParallel[5]=1e-12f;
    require(!math::inverse(nearlyParallel),"Relative singularity guard failed");
    const auto frame=math::orthonormalFrame(world);
    same({frame[12],frame[13],frame[14]},{world[12],world[13],world[14]});
    near(math::length({frame[0],frame[1],frame[2]}),1);
    near(math::length({frame[4],frame[5],frame[6]}),1);
    near(math::dot({frame[0],frame[1],frame[2]},{frame[4],frame[5],frame[6]}),0);
    require(math::handedness(frame)==1,"Gizmo proxy did not produce a right-handed basis");
}
void transformedBounds() {
    const Aabb original{{-1,-2,-3},{2,4,5},true};
    auto transform=math::translation({5,-7,2})*math::rotation(math::axisAngle({0,1,0},.8))*math::scaling({-2,.5f,3});
    transform[4]=.9f;
    const auto bounds=math::transformBounds(original,transform);
    require(bounds.valid,"Transformed bounds lost");
    for (unsigned c=0;c<8;++c) {
        const auto point=math::transformPoint(transform,{c&1 ? 2 : -1,c&2 ? 4 : -2,c&4 ? 5 : -3});
        for (std::size_t axis=0;axis<3;++axis)
            require(point[axis]>=bounds.minimum[axis]-1e-5f && point[axis]<=bounds.maximum[axis]+1e-5f,"Bounds failed to enclose a transformed corner");
    }
    Aabb combined; math::include(combined,bounds); math::include(combined,{100,100,100});
    same(combined.maximum,{100,100,100});
    require(!math::transformBounds({{1,0,0},{-1,0,0},true},transform).valid,"Invalid source bounds accepted");
}
void projectionParityAndRays() {
    Camera camera; camera.position={3,4,8}; camera.target={-1,2,-3};
    const auto eye=camera.eye(), forward=camera.forward(), up=math::cross(camera.right(),forward);
    float legacyView[16];
    bx::mtxLookAt(legacyView,{eye[0],eye[1],eye[2]},{eye[0]+forward[0],eye[1]+forward[1],eye[2]+forward[2]},
        {up[0],up[1],up[2]},bx::Handedness::Right);
    const auto view=camera.viewMatrix();
    for (std::size_t i=0;i<16;++i) near(view[i],legacyView[i],2e-5);
    for (auto depth:{ClipDepth::ZeroToOne,ClipDepth::NegativeOneToOne}) {
        for (float aspect:{.4f,1.0f,2.2f}) {
            float legacy[16];
            bx::mtxProj(legacy,camera.verticalFov(aspect),aspect,camera.nearPlane,camera.farPlane,depth==ClipDepth::NegativeOneToOne,bx::Handedness::Right);
            const auto projection=camera.projectionMatrix(aspect,depth);
            for (std::size_t i=0;i<16;++i) near(projection[i],legacy[i],2e-5);
            const Vec4 viewport{70,130,700*aspect,700};
            const auto vp=projection*view;
            const auto screen=math::project(camera.target,vp,viewport,depth);
            require(screen.has_value(),"Visible point could not be projected");
            near((*screen)[0],viewport[0]+viewport[2]*.5,1e-3); near((*screen)[1],480,1e-3);
            const auto restored=math::unproject(*screen,vp,viewport,depth);
            require(restored.has_value(),"Screen point could not be unprojected");
            same(*restored,camera.target,2e-3);
            const auto ray=math::rayFromScreen((*screen)[0],(*screen)[1],vp,viewport,depth);
            require(ray.has_value(),"Center ray missing"); same(ray->direction,forward,2e-4);
        }
        float legacyUi[16];
        bx::mtxOrtho(legacyUi,10,810,630,30,0,100,0,depth==ClipDepth::NegativeOneToOne);
        const auto ui=math::orthographic(10,810,630,30,0,100,depth)*math::scaling({1,1,-1});
        for (std::size_t i=0;i<16;++i) near(ui[i],legacyUi[i],1e-6);
    }
    require(!math::rayFromScreen(0,0,Mat4{},Vec4{},ClipDepth::ZeroToOne),"Zero-size viewport ray accepted");
    require(!math::unproject({0,0,0},Mat4::zero(),{0,0,100,100},ClipDepth::ZeroToOne),"Singular projection accepted");
    const auto hit=math::intersectPlane({{1,2,3},{0,-1,0}},{0,0,0},{0,1,0});
    require(hit.has_value(),"Forward ray missed plane"); same(*hit,{1,0,3});
    require(!math::intersectPlane({{1,2,3},{0,1,0}},{0,0,0},{0,1,0}),"Plane behind ray accepted");
    camera.position={0,1,5}; camera.target={0,1,0};
    const auto ray=camera.screenRay(400,300,{0,0,800,600,true,false});
    require(ray.has_value(),"Camera ray failed"); same(ray->origin,camera.eye()); same(ray->direction,{0,0,-1});
    camera.nearPlane=.00001f; camera.farPlane=2048;
    require(camera.screenRay(400,300,{0,0,800,600,true,false}).has_value(),"Large depth range broke picking rays");
}
}
int main() {
    int failures=0;
    const auto run=[&](const char* name,auto function) {
        try { function(); std::cout<<"PASS "<<name<<'\n'; }
        catch (const std::exception& error) { ++failures; std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n'; }
    };
    run("packed public types and compatible serialization",layoutAndSerialization);
    run("vector math and robust fallbacks",vectorsAndRobustFallbacks);
    run("affine inverse, shear, reflection and normal matrices",affineTransformsAndNormals);
    run("transformed bounds",transformedBounds);
    run("projection parity, unprojection and screen rays",projectionParityAndRays);
    return failures ? 1 : 0;
}
