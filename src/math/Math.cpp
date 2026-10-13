#include "Math.h"

// GLM is confined to this implementation; no third-party type escapes it.
#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace crimson::math {
namespace {
glm::dvec3 native(const Vec3& value) { return {value[0],value[1],value[2]}; }
glm::vec3 nativeFloat(const Vec3& value) { return {value[0],value[1],value[2]}; }
Vec3 own(const glm::dvec3& value) { return {value.x,value.y,value.z}; }
Vec3 own(const glm::vec3& value) { return {value.x,value.y,value.z}; }
glm::dmat4 native(const Mat4& value) {
    glm::dmat4 result(0);
    for (int c=0;c<4;++c) for (int r=0;r<4;++r) result[c][r]=value[static_cast<std::size_t>(c*4+r)];
    return result;
}
Mat4 own(const glm::dmat4& value) {
    auto result=Mat4::zero();
    for (int c=0;c<4;++c) for (int r=0;r<4;++r) result[static_cast<std::size_t>(c*4+r)]=static_cast<float>(value[c][r]);
    return result;
}
template<class T> bool finiteRange(const T& value) { return std::all_of(value.begin(),value.end(),[](float v){return std::isfinite(v);}); }
bool validViewport(const Vec4& viewport) { return finite(viewport) && viewport[2]>0 && viewport[3]>0; }
bool invertibleLinear(const Mat4& value, double& determinant) {
    if (!finite(value)) return false;
    const glm::dmat3 linear(native(value));
    determinant=glm::determinant(linear);
    const double scale=glm::length(linear[0])*glm::length(linear[1])*glm::length(linear[2]);
    return scale>0 && std::isfinite(scale) && std::isfinite(determinant) && std::abs(determinant)>scale*1e-10;
}
}
bool finite(float value) { return std::isfinite(value); }
bool finite(const Vec2& value) { return finiteRange(value); }
bool finite(const Vec3& value) { return finiteRange(value); }
bool finite(const Vec4& value) { return finiteRange(value); }
bool finite(const Quat& value) { return finiteRange(value); }
bool finite(const Mat3& value) { return finiteRange(value); }
bool finite(const Mat4& value) { return finiteRange(value); }
Vec3 add(const Vec3& a, const Vec3& b) { return own(native(a)+native(b)); }
Vec3 subtract(const Vec3& a, const Vec3& b) { return own(native(a)-native(b)); }
Vec3 scale(const Vec3& value, double scalar) { return own(native(value)*scalar); }
double dot(const Vec3& a, const Vec3& b) { return glm::dot(native(a),native(b)); }
float dotFloat(const Vec3& a, const Vec3& b) { return glm::dot(nativeFloat(a),nativeFloat(b)); }
Vec3 cross(const Vec3& a, const Vec3& b) { return own(glm::cross(native(a),native(b))); }
double length(const Vec3& value) { return glm::length(native(value)); }
Vec3 normalized(const Vec3& value, const Vec3& fallback, double epsilon) {
    const double magnitude=length(value);
    return finite(value) && std::isfinite(magnitude) && magnitude>std::max(0.0,epsilon) ? own(native(value)/magnitude) : fallback;
}
Vec3 normalizedFloat(const Vec3& value, const Vec3& fallback, float epsilon) {
    const auto v=nativeFloat(value);
    const float squared=glm::dot(v,v);
    if (!std::isfinite(squared) || squared<epsilon*epsilon || squared<=0) return fallback;
    return own(v*(1/std::sqrt(squared)));
}
Vec3 rotate(const Vec3& value, const Vec3& axis, double radians) {
    if (!finite(value) || !finite(axis) || !std::isfinite(radians)) return value;
    const auto unit=normalized(axis,{0,0,0});
    if (dot(unit,unit)==0) return value;
    return own(glm::angleAxis(radians,native(unit))*native(value));
}
Quat axisAngle(const Vec3& axis, double radians) {
    if (!finite(axis) || !std::isfinite(radians)) return {};
    const auto unit=normalized(axis,{0,0,0});
    if (dot(unit,unit)==0) return {};
    const auto value=glm::angleAxis(radians,native(unit));
    return {static_cast<float>(value.x),static_cast<float>(value.y),static_cast<float>(value.z),static_cast<float>(value.w)};
}
Mat4 multiply(const Mat4& a, const Mat4& b) { return own(native(a)*native(b)); }
Mat4 translation(const Vec3& value) { return own(glm::translate(glm::dmat4(1),native(value))); }
Mat4 rotation(const Quat& value) {
    if (!finite(value)) return {};
    const glm::dquat q(value[3],value[0],value[1],value[2]);
    const double magnitude=glm::length(q);
    return magnitude>1e-20 ? own(glm::mat4_cast(q/magnitude)) : Mat4{};
}
Mat4 scaling(const Vec3& value) { return own(glm::scale(glm::dmat4(1),native(value))); }
Vec3 transformPoint(const Mat4& matrix, const Vec3& value) { return own(glm::dvec3(native(matrix)*glm::dvec4(native(value),1))); }
Vec4 transformHomogeneous(const Mat4& matrix,const Vec4& value) {
    const auto result=native(matrix)*glm::dvec4(value[0],value[1],value[2],value[3]);return {result.x,result.y,result.z,result.w};
}
Vec3 transformDirection(const Mat4& matrix, const Vec3& value) { return own(glm::dvec3(native(matrix)*glm::dvec4(native(value),0))); }
bool isAffine(const Mat4& matrix, double tolerance) {
    return finite(matrix) && std::abs(matrix[3])<=tolerance && std::abs(matrix[7])<=tolerance &&
        std::abs(matrix[11])<=tolerance && std::abs(double(matrix[15])-1)<=tolerance;
}
float handedness(const Mat4& matrix) {
    double determinant{};
    return invertibleLinear(matrix,determinant) ? (determinant<0 ? -1.0f : 1.0f) : 0.0f;
}
std::optional<Mat4> inverse(const Mat4& matrix) {
    if (!finite(matrix) || (isAffine(matrix) && handedness(matrix)==0)) return {};
    const auto m=native(matrix);
    const auto determinant=glm::determinant(m);
    if (!std::isfinite(determinant) || determinant==0) return {};
    const auto result=own(glm::inverse(m));
    return finite(result) ? std::optional<Mat4>{result} : std::nullopt;
}
std::optional<Mat4> normalMatrix(const Mat4& matrix) {
    double determinant{};
    if (!invertibleLinear(matrix,determinant)) return {};
    const auto result=own(glm::dmat4(glm::transpose(glm::inverse(glm::dmat3(native(matrix))))));
    return finite(result) ? std::optional<Mat4>{result} : std::nullopt;
}
Mat4 orthonormalFrame(const Mat4& matrix) {
    if (!finite(matrix)) return {};
    const auto x=normalized({matrix[0],matrix[1],matrix[2]},{1,0,0});
    const Vec3 originalY{matrix[4],matrix[5],matrix[6]};
    const auto fallback=normalized(cross(std::abs(x[1])<.9f ? Vec3{0,1,0} : Vec3{0,0,1},x),{0,1,0});
    const auto y=normalized(originalY-x*dot(x,originalY),fallback);
    const auto z=normalized(cross(x,y),{0,0,1});
    return {x[0],x[1],x[2],0, y[0],y[1],y[2],0, z[0],z[1],z[2],0, matrix[12],matrix[13],matrix[14],1};
}
namespace {
glm::dmat3 eulerBasis(const Vec3& angles) {return glm::mat3_cast(glm::dquat(native(angles)));}
glm::dvec3 perpendicular(const glm::dvec3& unit,const glm::dvec3& preferred) {
    auto result=preferred-unit*glm::dot(preferred,unit);double magnitude=glm::length(result);
    if(!(magnitude>1e-10)) {
        glm::dvec3 axis(0);const auto smallest=std::abs(unit.x)<=std::abs(unit.y) && std::abs(unit.x)<=std::abs(unit.z) ? 0:std::abs(unit.y)<=std::abs(unit.z) ? 1:2;
        axis[smallest]=1;result=axis-unit*glm::dot(axis,unit);magnitude=glm::length(result);
    }
    return result/magnitude;
}
glm::dmat3 editableBasis(const glm::dmat3& linear,const glm::dmat3& hint) {
    const double xLength=glm::length(linear[0]),yLength=glm::length(linear[1]),zLength=glm::length(linear[2]);
    glm::dvec3 x,y,z;
    if(xLength>0) {
        x=linear[0]/xLength;
        const auto remainder=linear[1]-x*glm::dot(x,linear[1]);
        if(glm::length(remainder)>yLength*1e-10 && yLength>0)y=glm::normalize(remainder);
        else {
            const auto remainingZ=linear[2]-x*glm::dot(x,linear[2]);
            y=zLength>0 && glm::length(remainingZ)>zLength*1e-10 ? glm::normalize(glm::cross(remainingZ,x)):perpendicular(x,hint[1]);
        }
        z=glm::normalize(glm::cross(x,y));y=glm::normalize(glm::cross(z,x));
    } else if(yLength>0) {
        y=linear[1]/yLength;const auto remainder=linear[2]-y*glm::dot(y,linear[2]);
        if(zLength>0 && glm::length(remainder)>zLength*1e-10){z=glm::normalize(remainder);x=glm::normalize(glm::cross(y,z));}
        else {x=perpendicular(y,hint[0]);z=glm::normalize(glm::cross(x,y));}
    } else if(zLength>0) {
        z=linear[2]/zLength;x=perpendicular(z,hint[0]);y=glm::normalize(glm::cross(z,x));
    } else return hint;
    glm::dmat3 result(x,y,z);
    // Matrix storage cannot identify the originally mirrored axis. With no
    // history the canonical reflection is placed on X; a reference may retain
    // any equivalent pair of sign flips without changing the affine matrix.
    if(glm::dot(result[2],linear[2])<0) {result[0]=-result[0];result[2]=-result[2];}
    return result;
}
Vec3 closestEuler(const glm::dmat3& basis,const Vec3& reference) {
    constexpr double pi=std::numbers::pi,tau=2*pi;
    const auto wrap=[](double angle,double near){return angle+tau*std::round((near-angle)/tau);};
    const double cosine=std::hypot(basis[0][0],basis[0][1]);
    if(cosine<=1e-6) {
        const double sign=basis[0][2]<0 ? 1.0:-1.0;
        const double wanted=reference[0]-sign*reference[2];
        const double coupled=wrap(std::atan2(-basis[2][1],basis[1][1]),wanted);
        const double adjustment=.5*(coupled-wanted);
        return {reference[0]+adjustment,wrap(sign*pi*.5,reference[1]),reference[2]-sign*adjustment};
    }
    const glm::dvec3 base{std::atan2(basis[1][2],basis[2][2]),std::atan2(-basis[0][2],cosine),std::atan2(basis[0][1],basis[0][0])};
    const auto near=[&](glm::dvec3 value){for(int i=0;i<3;++i)value[i]=wrap(value[i],reference[static_cast<std::size_t>(i)]);return value;};
    const auto a=near(base),b=near({base.x+pi,pi-base.y,base.z+pi});
    const auto referenceNative=native(reference);
    return own(glm::dot(b-referenceNative,b-referenceNative)<glm::dot(a-referenceNative,a-referenceNative) ? b:a);
}
std::optional<glm::dmat3> capturedBasis(const AffineTransformFields& fields) {
    if(!isAffine(fields.rotation) || !finite(fields.rotationRadians) || !finite(fields.scale))return {};
    const glm::dmat3 raw(native(fields.rotation));
    for(int i=0;i<3;++i) {
        if(std::abs(glm::length(raw[i])-1)>1e-4)return {};
        for(int j=i+1;j<3;++j)if(std::abs(glm::dot(raw[i],raw[j]))>1e-4)return {};
    }
    if(glm::determinant(raw)<=0)return {};
    const auto x=glm::normalize(raw[0]),y=glm::normalize(raw[1]-x*glm::dot(x,raw[1]));
    return glm::dmat3(x,y,glm::normalize(glm::cross(x,y)));
}
}
Mat4 eulerRotationXYZ(const Vec3& angles) {return finite(angles) ? own(glm::dmat4(eulerBasis(angles))):Mat4{};}
std::optional<AffineTransformFields> affineTransformFields(const Mat4& matrix,const AffineTransformFields* reference) {
    if(!isAffine(matrix))return {};
    const bool hinted=reference && finite(reference->rotationRadians);
    const glm::dmat3 linear(native(matrix)),hint=hinted ? eulerBasis(reference->rotationRadians):glm::dmat3(1);
    auto basis=editableBasis(linear,hint);
    if(hinted) {
        glm::dmat3 chosen=basis;double best=-std::numeric_limits<double>::infinity();
        for(const auto& signs:std::array<glm::dvec3,4>{{{1,1,1},{1,-1,-1},{-1,1,-1},{-1,-1,1}}}) {
            auto candidate=basis;double score=0;for(int axis=0;axis<3;++axis){candidate[axis]*=signs[axis];score+=glm::dot(candidate[axis],hint[axis]);}
            if(score>best){best=score;chosen=candidate;}
        }
        basis=chosen;
    }
    AffineTransformFields fields;fields.position={matrix[12],matrix[13],matrix[14]};fields.rotation=own(glm::dmat4(basis));
    fields.rotationRadians=closestEuler(basis,hinted ? reference->rotationRadians:Vec3{});fields.singular=handedness(matrix)==0;
    for(int axis=0;axis<3;++axis) {
        const double size=glm::length(linear[axis]);const double orientation=glm::dot(basis[axis],linear[axis]);
        fields.scale[static_cast<std::size_t>(axis)]=static_cast<float>(orientation<0 ? -size:size);
        for(int other=axis+1;other<3;++other) {
            const double relative=size*glm::length(linear[other]);
            if(relative>0 && std::abs(glm::dot(linear[axis],linear[other]))>relative*2e-5)fields.sheared=true;
        }
    }
    return finite(fields.scale) && finite(fields.rotationRadians) ? std::optional{fields}:std::nullopt;
}
std::optional<Mat4> withAffinePosition(const Mat4& matrix,const Vec3& position) {
    if(!isAffine(matrix) || !finite(position))return {};
    if(position==Vec3{matrix[12],matrix[13],matrix[14]})return matrix;
    auto result=matrix;for(std::size_t axis=0;axis<3;++axis)result[12+axis]=position[axis];return result;
}
std::optional<Mat4> withAffineRotation(const Mat4& matrix,const AffineTransformFields& captured,const Vec3& angles) {
    if(!isAffine(matrix) || !finite(angles))return {};
    if(angles==captured.rotationRadians)return matrix;
    const auto old=capturedBasis(captured);if(!old)return {};
    const glm::dmat3 linear(native(matrix));const auto changed=eulerBasis(angles)*glm::transpose(*old)*linear;
    auto result=matrix;for(int column=0;column<3;++column)for(int row=0;row<3;++row)result.at(row,column)=static_cast<float>(changed[column][row]);
    return finite(result) ? std::optional{result}:std::nullopt;
}
std::optional<Mat4> withAffineScale(const Mat4& matrix,const AffineTransformFields& captured,const Vec3& requested) {
    if(!isAffine(matrix) || !finite(requested) || !finite(captured.scale))return {};
    if(requested==captured.scale)return matrix;
    const auto basis=capturedBasis(captured);if(!basis)return {};
    const glm::dmat3 linear(native(matrix));auto result=matrix;
    for(int axis=0;axis<3;++axis) {
        const auto index=static_cast<std::size_t>(axis);if(requested[index]==captured.scale[index])continue;
        glm::dvec3 column;
        if(captured.scale[index]!=0)column=linear[axis]*(static_cast<double>(requested[index])/captured.scale[index]);
        else {
            if(glm::length(linear[axis])!=0)return {}; // Invalid captured fields must not erase a nonzero column.
            column=(*basis)[axis]*static_cast<double>(requested[index]);
        }
        for(int row=0;row<3;++row)result.at(row,axis)=static_cast<float>(column[row]);
    }
    return finite(result) ? std::optional{result}:std::nullopt;
}
void include(Aabb& bounds, const Vec3& point) {
    if (!finite(point)) return;
    if (!bounds.valid) { bounds.minimum=bounds.maximum=point; bounds.valid=true; return; }
    for (std::size_t i=0;i<3;++i) { bounds.minimum[i]=std::min(bounds.minimum[i],point[i]); bounds.maximum[i]=std::max(bounds.maximum[i],point[i]); }
}
void include(Aabb& bounds, const Aabb& other) {
    if (!other.valid || !finite(other.minimum) || !finite(other.maximum)) return;
    for (std::size_t i=0;i<3;++i) if (other.minimum[i]>other.maximum[i]) return;
    include(bounds,other.minimum); include(bounds,other.maximum);
}
Aabb transformBounds(const Aabb& bounds, const Mat4& matrix) {
    Aabb result;
    if (!bounds.valid || !isAffine(matrix) || !finite(bounds.minimum) || !finite(bounds.maximum)) return result;
    for (std::size_t i=0;i<3;++i) if (bounds.minimum[i]>bounds.maximum[i]) return result;
    for (unsigned corner=0;corner<8;++corner) {
        const auto point=transformPoint(matrix,{corner&1 ? bounds.maximum[0] : bounds.minimum[0],
            corner&2 ? bounds.maximum[1] : bounds.minimum[1],corner&4 ? bounds.maximum[2] : bounds.minimum[2]});
        if (!finite(point)) return {};
        include(result,point);
    }
    return result;
}
Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    if (!finite(eye)) return {};
    const auto forward=normalized(target-eye,{0,0,-1});
    const auto right=normalized(cross(forward,up),normalized(cross(forward,{0,0,1}),{1,0,0}));
    return own(glm::lookAtRH(native(eye),native(eye)+native(forward),native(cross(right,forward))));
}
Mat4 perspective(double verticalRadians, double aspect, double nearPlane, double farPlane, ClipDepth depth) {
    if (!std::isfinite(verticalRadians) || !std::isfinite(aspect) || !std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
        verticalRadians<=0 || verticalRadians>=3.14159265358979323846 || aspect<=0 || nearPlane<=0 || farPlane<=nearPlane)
        throw std::invalid_argument("Invalid perspective projection.");
    return own(depth==ClipDepth::ZeroToOne ? glm::perspectiveRH_ZO(verticalRadians,aspect,nearPlane,farPlane) :
        glm::perspectiveRH_NO(verticalRadians,aspect,nearPlane,farPlane));
}
Mat4 orthographic(double left, double right, double bottom, double top, double nearPlane, double farPlane, ClipDepth depth) {
    if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(bottom) || !std::isfinite(top) ||
        !std::isfinite(nearPlane) || !std::isfinite(farPlane) || left==right || bottom==top || nearPlane==farPlane)
        throw std::invalid_argument("Invalid orthographic projection.");
    return own(depth==ClipDepth::ZeroToOne ? glm::orthoRH_ZO(left,right,bottom,top,nearPlane,farPlane) :
        glm::orthoRH_NO(left,right,bottom,top,nearPlane,farPlane));
}
std::optional<Vec3> project(const Vec3& world, const Mat4& viewProjection, const Vec4& viewport, ClipDepth depth) {
    if (!finite(world) || !finite(viewProjection) || !validViewport(viewport)) return {};
    const auto clip=native(viewProjection)*glm::dvec4(native(world),1);
    if (!std::isfinite(clip.w) || clip.w<=1e-12) return {};
    const auto ndc=glm::dvec3(clip)/clip.w;
    const Vec3 result{viewport[0]+(ndc.x+1)*.5*viewport[2],viewport[1]+(1-ndc.y)*.5*viewport[3],
        depth==ClipDepth::ZeroToOne ? ndc.z : (ndc.z+1)*.5};
    return finite(result) ? std::optional<Vec3>{result} : std::nullopt;
}
std::optional<Vec3> unproject(const Vec3& screen, const Mat4& viewProjection, const Vec4& viewport, ClipDepth depth) {
    if (!finite(screen) || !validViewport(viewport) || !finite(viewProjection)) return {};
    const auto m=native(viewProjection);
    const auto determinant=glm::determinant(m);
    if (!std::isfinite(determinant) || determinant==0) return {};
    const glm::dvec4 clip((double(screen[0])-viewport[0])/viewport[2]*2-1,
        1-(double(screen[1])-viewport[1])/viewport[3]*2,depth==ClipDepth::ZeroToOne ? screen[2] : double(screen[2])*2-1,1);
    const auto world=glm::inverse(m)*clip;
    if (!std::isfinite(world.w) || std::abs(world.w)<=1e-20) return {};
    const auto result=own(glm::dvec3(world)/world.w);
    return finite(result) ? std::optional<Vec3>{result} : std::nullopt;
}
std::optional<Ray> rayFromScreen(float x, float y, const Mat4& viewProjection, const Vec4& viewport, ClipDepth depth) {
    if (!finite(x) || !finite(y) || !validViewport(viewport) || !finite(viewProjection)) return {};
    const auto matrix=native(viewProjection);
    const double determinant=glm::determinant(matrix);
    if (!std::isfinite(determinant) || determinant==0) return {};
    const auto inverseMatrix=glm::inverse(matrix);
    const double ndcX=(double(x)-viewport[0])/viewport[2]*2-1, ndcY=1-(double(y)-viewport[1])/viewport[3]*2;
    const auto nearClip=inverseMatrix*glm::dvec4(ndcX,ndcY,depth==ClipDepth::ZeroToOne ? 0 : -1,1);
    // An interior depth avoids an infinite far-plane point when a large
    // far/near ratio rounds the float projection to an infinite projection.
    const auto midClip=inverseMatrix*glm::dvec4(ndcX,ndcY,depth==ClipDepth::ZeroToOne ? .5 : 0,1);
    if (std::abs(nearClip.w)<=1e-20 || std::abs(midClip.w)<=1e-20) return {};
    const auto origin=glm::dvec3(nearClip)/nearClip.w;
    const auto delta=glm::dvec3(midClip)/midClip.w-origin;
    const double magnitude=glm::length(delta);
    if (!std::isfinite(magnitude) || magnitude<=1e-20) return {};
    const Ray result{own(origin),own(delta/magnitude)};
    return finite(result.origin) && finite(result.direction) ? std::optional<Ray>{result} : std::nullopt;
}
std::optional<Vec3> intersectPlane(const Ray& ray, const Vec3& point, const Vec3& normal) {
    if (!finite(ray.origin) || !finite(ray.direction) || !finite(point) || !finite(normal)) return {};
    const double denominator=dot(ray.direction,normal);
    if (std::abs(denominator)<=1e-10) return {};
    const double distance=dot(point-ray.origin,normal)/denominator;
    if (!std::isfinite(distance) || distance<0) return {};
    const auto result=ray.origin+ray.direction*distance;
    return finite(result) ? std::optional<Vec3>{result} : std::nullopt;
}
} // namespace crimson::math
