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
void affineInspectorFields() {
    const Vec3 angles{.3f,-.4f,.7f},position{3,-2,9};
    const auto rotation=math::eulerRotationXYZ(angles);
    const auto explicitOrder=math::rotation(math::axisAngle({0,0,1},angles[2]))*
        math::rotation(math::axisAngle({0,1,0},angles[1]))*math::rotation(math::axisAngle({1,0,0},angles[0]));
    same(rotation,explicitOrder,2e-6);
    for(const Vec3 scale:{Vec3{2,3,4},Vec3{-2,3,4}}) {
        const auto original=math::translation(position)*rotation*math::scaling(scale);
        const auto fields=math::affineTransformFields(original);require(fields.has_value(),"Ordinary affine inspector presentation failed");
        same(fields->position,position,0);same(fields->scale,scale);same(fields->rotationRadians,angles);
        require(!fields->sheared&&!fields->singular,"Ordinary mirrored TRS was classified as sheared or singular");
        same(math::translation(fields->position)*fields->rotation*math::scaling(fields->scale),original,3e-6);
        require(*math::withAffinePosition(original,fields->position)==original && *math::withAffineRotation(original,*fields,fields->rotationRadians)==original &&
            *math::withAffineScale(original,*fields,fields->scale)==original,"Unchanged inspector edits must retain the exact matrix");
        const auto moved=math::withAffinePosition(original,{8,6,-3});require(moved.has_value(),"Inspector position edit failed");
        for(std::size_t i=0;i<16;++i)if(i<12||i>14)near((*moved)[i],original[i],0);
        const Vec3 requested{-.2f,.6f,-.4f};const auto rotated=math::withAffineRotation(original,*fields,requested);
        require(rotated.has_value(),"Inspector rotation edit failed");same(*rotated,math::translation(position)*math::eulerRotationXYZ(requested)*math::scaling(scale),4e-6);
        auto wanted=fields->scale;wanted[1]=-5;const auto scaled=math::withAffineScale(original,*fields,wanted);
        require(scaled.has_value(),"Signed inspector scale edit failed");same(*scaled,math::translation(position)*rotation*math::scaling({scale[0],-5,scale[2]}),4e-6);
        for(std::size_t i=0;i<16;++i)if(i<4||i>=8)near((*scaled)[i],original[i],0);
    }
    math::AffineTransformFields hint;hint.rotationRadians=angles;
    const auto mirroredY=math::translation(position)*rotation*math::scaling({2,-3,4});
    const auto retained=math::affineTransformFields(mirroredY,&hint);require(retained.has_value(),"Reflected inspector hint failed");
    same(retained->rotationRadians,angles);same(retained->scale,{2,-3,4});
    Mat4 shear;shear[4]=.65f;shear[8]=-.3f;shear[9]=.4f;
    const auto affine=math::translation(position)*rotation*shear*math::scaling({-2,3,4});const auto fields=math::affineTransformFields(affine);
    require(fields&&fields->sheared&&!fields->singular,"Inspector did not identify full affine shear/reflection");
    const auto turned=math::withAffineRotation(affine,*fields,{.8f,.1f,-.2f});require(turned.has_value(),"Sheared inspector rotation failed");
    const auto column=[](const Mat4& matrix,std::size_t axis){return Vec3{matrix[axis*4],matrix[axis*4+1],matrix[axis*4+2]};};
    for(std::size_t a=0;a<3;++a)for(std::size_t b=0;b<3;++b)near(math::dot(column(*turned,a),column(*turned,b)),math::dot(column(affine,a),column(affine,b)),6e-5);
    require(math::handedness(*turned)==math::handedness(affine),"Inspector rotation changed reflection handedness");
    same({(*turned)[12],(*turned)[13],(*turned)[14]},position,0);
    auto wanted=fields->scale;wanted[2]*=-.5f;const auto resized=math::withAffineScale(affine,*fields,wanted);
    require(resized.has_value(),"Sheared inspector scale failed");
    same(column(*resized,0),column(affine,0),0);same(column(*resized,1),column(affine,1),0);same(column(*resized,2),column(affine,2)*-.5f,2e-6);
    require(math::handedness(*resized)==-math::handedness(affine),"Negative inspector scale did not change reflection");
}
void inspectorEulerAndCollapsedAxes() {
    constexpr float pi=std::numbers::pi_v<float>;
    for(float sign:{-1.0f,1.0f})for(float offset:{-.00001f,-.0000001f,0.0f,.0000001f,.00001f}) {
        const Vec3 angles{.73f,sign*pi*.5f+offset,-.42f};const auto original=math::eulerRotationXYZ(angles);
        math::AffineTransformFields reference;reference.rotationRadians=angles;
        const auto fields=math::affineTransformFields(original,&reference);require(fields.has_value(),"Gimbal-adjacent inspector presentation failed");
        same(math::eulerRotationXYZ(fields->rotationRadians),original,3e-6);
        same(fields->rotationRadians,angles,3e-5);
        reference.rotationRadians={angles[0]+4*pi,angles[1]-2*pi,angles[2]+6*pi};
        const auto unwrapped=math::affineTransformFields(original,&reference);require(unwrapped.has_value(),"Unwrapped inspector Euler hint failed");
        same(unwrapped->rotationRadians,reference.rotationRadians,4e-5);same(math::eulerRotationXYZ(unwrapped->rotationRadians),original,4e-6);
    }
    const Vec3 angles{.4f,-.6f,.8f};math::AffineTransformFields reference;reference.rotationRadians=angles;
    const auto basis=math::eulerRotationXYZ(angles);
    for(const Vec3 scale:{Vec3{0,2,3},Vec3{2,0,3},Vec3{2,3,0},Vec3{0,0,0}}) {
        const auto original=math::translation({1,2,3})*basis*math::scaling(scale);
        const auto fields=math::affineTransformFields(original,&reference);require(fields&&fields->singular,"Collapsed inspector matrix must remain explicitly editable");
        const auto moved=math::withAffinePosition(original,{4,5,6});require(moved.has_value(),"Singular matrices must permit exact position edits");
        for(std::size_t i=0;i<12;++i)near((*moved)[i],original[i],0);
        auto wanted=fields->scale;for(std::size_t i=0;i<3;++i)if(wanted[i]==0)wanted[i]=-1;
        const auto recovered=math::withAffineScale(original,*fields,wanted);require(recovered&&math::handedness(*recovered)!=0,"Inspector scale must recover collapsed columns along its displayed frame");
        same(*recovered,math::translation({1,2,3})*basis*math::scaling(wanted),4e-6);
    }
    auto tiny=math::eulerRotationXYZ(angles)*math::scaling({1e-20f,2e-20f,3e-20f});
    const auto small=math::affineTransformFields(tiny);require(small&&!small->singular,"Scale-relative inspector presentation rejected tiny invertible transforms");
    auto invalid=Mat4{};invalid[3]=1;
    require(!math::affineTransformFields(invalid)&&!math::withAffinePosition(invalid,{1,2,3}),"Projective matrices must fail inspector edits explicitly");
    invalid=Mat4{};invalid[0]=std::numeric_limits<float>::infinity();
    require(!math::affineTransformFields(invalid),"Nonfinite inspector matrix accepted");
    const auto identity=math::affineTransformFields(Mat4{});require(identity.has_value(),"Identity inspector presentation missing");
    require(!math::withAffinePosition(Mat4{},{std::numeric_limits<float>::quiet_NaN(),0,0}) &&
        !math::withAffineRotation(Mat4{},*identity,{0,std::numeric_limits<float>::infinity(),0}) &&
        !math::withAffineScale(Mat4{},*identity,{0,0,std::numeric_limits<float>::infinity()}),"Nonfinite inspector input must fail atomically");
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
    run("affine inspector presentation and preserving edits",affineInspectorFields);
    run("inspector Euler continuity, singular matrices and scale recovery",inspectorEulerAndCollapsedAxes);
    run("projection parity, unprojection and screen rays",projectionParityAndRays);
    return failures ? 1 : 0;
}
