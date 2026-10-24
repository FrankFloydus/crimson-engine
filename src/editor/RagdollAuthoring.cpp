#include "RagdollAuthoring.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace crimson {
namespace {
constexpr float MinimumSize=.0001f,MaximumSize=10000;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void validBody(const RagdollBody& body){
    check(body.shape>=RagdollShape::Capsule&&body.shape<=RagdollShape::Sphere,"Unsupported collider shape.");
    check(math::finite(body.position)&&math::finite(body.rotation),"Collider transform must be finite.");
    double norm{};for(float q:body.rotation)norm+=double(q)*q;check(std::abs(norm-1)<=1e-4,"Collider rotation must be a unit quaternion.");
    for(float p:body.position)check(std::abs(p)<=1e6f,"Collider offset exceeds one million metres.");
    for(float s:body.size)check(std::isfinite(s)&&s>=MinimumSize&&s<=MaximumSize,"Box dimensions are outside their valid range.");
    check(std::isfinite(body.radius)&&body.radius>=MinimumSize&&body.radius<=MaximumSize&&std::isfinite(body.height)&&body.height>=2*MinimumSize&&body.height<=MaximumSize,"Collider dimensions are outside their valid range.");
    check(body.shape!=RagdollShape::Capsule||body.height>=2*body.radius,"Capsule full height must include both caps.");
}
Vec3 matrixDimensions(const Mat4& matrix,bool rigid){
    check(math::isAffine(matrix)&&math::handedness(matrix)>0,"Collider gizmo requires a finite, positive affine transform.");
    std::array<Vec3,3> axis;Vec3 dimensions;
    for(std::size_t i=0;i<3;++i){axis[i]={matrix[i*4],matrix[i*4+1],matrix[i*4+2]};const double length=math::length(axis[i]);check(std::isfinite(length)&&length>1e-9&&length<=1e8,"Collider gizmo axes are singular or outside the valid range.");dimensions[i]=float(length);axis[i]=axis[i]/length;if(rigid)check(std::abs(length-1)<=1e-4,"Bone and placement frames must have unit rigid axes.");}
    check(std::abs(math::dot(axis[0],axis[1]))<=1e-5&&std::abs(math::dot(axis[0],axis[2]))<=1e-5&&std::abs(math::dot(axis[1],axis[2]))<=1e-5,"Collider gizmo cannot contain shear.");return dimensions;
}
Vec3 shapeDimensions(const RagdollBody& body){if(body.shape==RagdollShape::Box)return body.size;if(body.shape==RagdollShape::Sphere)return {2*body.radius,2*body.radius,2*body.radius};return {2*body.radius,body.height,2*body.radius};}
Quat matrixQuaternion(const Mat4& matrix){
    double x{},y{},z{},w{};const double trace=matrix[0]+matrix[5]+matrix[10];
    if(trace>0){const double s=std::sqrt(trace+1)*2;w=.25*s;x=(matrix[6]-matrix[9])/s;y=(matrix[8]-matrix[2])/s;z=(matrix[1]-matrix[4])/s;}
    else if(matrix[0]>matrix[5]&&matrix[0]>matrix[10]){const double s=std::sqrt(1+matrix[0]-matrix[5]-matrix[10])*2;w=(matrix[6]-matrix[9])/s;x=.25*s;y=(matrix[4]+matrix[1])/s;z=(matrix[8]+matrix[2])/s;}
    else if(matrix[5]>matrix[10]){const double s=std::sqrt(1+matrix[5]-matrix[0]-matrix[10])*2;w=(matrix[8]-matrix[2])/s;x=(matrix[4]+matrix[1])/s;y=.25*s;z=(matrix[9]+matrix[6])/s;}
    else{const double s=std::sqrt(1+matrix[10]-matrix[0]-matrix[5])*2;w=(matrix[1]-matrix[4])/s;x=(matrix[8]+matrix[2])/s;y=(matrix[9]+matrix[6])/s;z=.25*s;}
    double inverse=1/std::sqrt(x*x+y*y+z*z+w*w);if(w<0)inverse=-inverse;return {float(x*inverse),float(y*inverse),float(z*inverse),float(w*inverse)};
}
double strongestRatio(std::span<const double> ratios){double result=1,strength=-1;for(double ratio:ratios){const double change=std::abs(std::log(ratio));if(change>strength){strength=change;result=ratio;}}return result;}
struct Hit {double distance{};Vec3 normal{};};
std::optional<Hit> sphereHit(const Vec3& origin,const Vec3& direction,const Vec3& center,double radius,double limit,int cap=0){
    const auto offset=origin-center;const double a=math::dot(direction,direction),b=math::dot(offset,direction),c=math::dot(offset,offset)-radius*radius,discriminant=b*b-a*c;if(discriminant<0||a<=1e-20)return {};
    const double root=std::sqrt(std::max(0.,discriminant));for(double t:{(-b-root)/a,(-b+root)/a})if(t>=0&&t<=limit){const auto hit=origin+direction*t;if((cap>0&&hit[1]<center[1]-1e-7)||(cap<0&&hit[1]>center[1]+1e-7))continue;return Hit{t,math::normalized(hit-center)};}return {};
}
std::optional<Hit> shapeHit(const RagdollBody& body,const Vec3& origin,const Vec3& direction,double limit){
    if(body.shape==RagdollShape::Sphere)return sphereHit(origin,direction,{},body.radius,limit);
    if(body.shape==RagdollShape::Box){double near=-std::numeric_limits<double>::infinity(),far=std::numeric_limits<double>::infinity();Vec3 nearNormal{},farNormal{};
        for(std::size_t axis=0;axis<3;++axis){const double half=body.size[axis]*.5;if(std::abs(direction[axis])<1e-12){if(origin[axis]<-half||origin[axis]>half)return {};continue;}
            double first=(-half-origin[axis])/direction[axis],last=(half-origin[axis])/direction[axis];Vec3 firstNormal{},lastNormal{};firstNormal[axis]=-1;lastNormal[axis]=1;if(first>last){std::swap(first,last);std::swap(firstNormal,lastNormal);}if(first>near){near=first;nearNormal=firstNormal;}if(last<far){far=last;farNormal=lastNormal;}if(near>far)return {};}
        const double distance=near>=0?near:far;return distance>=0&&distance<=limit?std::optional<Hit>{Hit{distance,near>=0?nearNormal:farNormal}}:std::nullopt;
    }
    const double segment=std::max(0.,double(body.height)*.5-body.radius);if(segment<1e-9)return sphereHit(origin,direction,{},body.radius,limit);
    std::optional<Hit> best;const auto consider=[&](std::optional<Hit> hit){if(hit&&(!best||hit->distance<best->distance))best=std::move(hit);};
    const double a=double(direction[0])*direction[0]+double(direction[2])*direction[2],b=double(origin[0])*direction[0]+double(origin[2])*direction[2],c=double(origin[0])*origin[0]+double(origin[2])*origin[2]-double(body.radius)*body.radius;
    const double discriminant=b*b-a*c;if(a>1e-20&&discriminant>=0){const double root=std::sqrt(std::max(0.,discriminant));for(double t:{(-b-root)/a,(-b+root)/a})if(t>=0&&t<=limit){const auto hit=origin+direction*t;if(hit[1]>=-segment-1e-7&&hit[1]<=segment+1e-7)consider(Hit{t,math::normalized({hit[0],0,hit[2]})});}}
    consider(sphereHit(origin,direction,{0,float(segment),0},body.radius,limit,1));consider(sphereHit(origin,direction,{0,float(-segment),0},body.radius,limit,-1));return best;
}
void validHierarchy(std::span<const RagdollBody> bodies){check(bodies.size()<=256,"Ragdoll scope exceeds 256 bodies.");for(std::size_t i=0;i<bodies.size();++i)check(bodies[i].parent>=-1&&(bodies[i].parent<0||std::size_t(bodies[i].parent)<i),"Ragdoll scope requires parents before children.");}
}
std::optional<Mat4> ragdollBodyGizmoMatrix(const RagdollBody& body,const Mat4& bone,RagdollAuthoringOperation operation,std::string* diagnostic){
    try{validBody(body);(void)matrixDimensions(bone,true);check(operation>=RagdollAuthoringOperation::Translate&&operation<=RagdollAuthoringOperation::Resize,"Unsupported collider authoring operation.");auto matrix=bone*math::translation(body.position)*math::rotation(body.rotation);if(operation==RagdollAuthoringOperation::Resize)matrix=matrix*math::scaling(shapeDimensions(body));if(diagnostic)diagnostic->clear();return matrix;}
    catch(const std::exception& error){if(diagnostic)*diagnostic=error.what();return {};}
}
std::optional<RagdollBody> applyRagdollBodyGizmoTransform(const RagdollBody& baseline,const Mat4& bone,const Mat4& manipulated,RagdollAuthoringOperation operation,bool lockProportions,std::string* diagnostic){
    try{validBody(baseline);(void)matrixDimensions(bone,true);const auto dimensions=matrixDimensions(manipulated,operation!=RagdollAuthoringOperation::Resize);check(operation>=RagdollAuthoringOperation::Translate&&operation<=RagdollAuthoringOperation::Resize,"Unsupported collider authoring operation.");const auto inverse=math::inverse(bone);check(inverse.has_value(),"Bone frame is not invertible.");auto result=baseline;
        if(operation==RagdollAuthoringOperation::Translate)result.position=math::transformPoint(*inverse,{manipulated[12],manipulated[13],manipulated[14]});
        else if(operation==RagdollAuthoringOperation::Rotate)result.rotation=matrixQuaternion(*inverse*manipulated);
        else {const auto original=shapeDimensions(baseline);const std::array<double,3> ratio{double(dimensions[0])/original[0],double(dimensions[1])/original[1],double(dimensions[2])/original[2]};
            if(baseline.shape==RagdollShape::Box){if(lockProportions){double minimum=0,maximum=std::numeric_limits<double>::infinity();for(float size:baseline.size){minimum=std::max(minimum,MinimumSize/double(size));maximum=std::min(maximum,MaximumSize/double(size));}const auto uniform=std::clamp(strongestRatio(ratio),minimum,maximum);for(std::size_t i=0;i<3;++i)result.size[i]=float(baseline.size[i]*uniform);}else for(std::size_t i=0;i<3;++i)result.size[i]=std::clamp(dimensions[i],MinimumSize,MaximumSize);}
            else if(baseline.shape==RagdollShape::Sphere)result.radius=float(std::clamp(baseline.radius*strongestRatio(ratio),double(MinimumSize),double(MaximumSize)));
            else {const std::array<double,2> radial{ratio[0],ratio[2]};if(lockProportions){const double minimum=std::max(MinimumSize/double(baseline.radius),2*MinimumSize/double(baseline.height)),maximum=std::min(MaximumSize*.5/double(baseline.radius),MaximumSize/double(baseline.height)),uniform=std::clamp(strongestRatio(ratio),minimum,maximum);result.radius=float(baseline.radius*uniform);result.height=std::max(2*result.radius,float(baseline.height*uniform));}else{const double radiusScale=strongestRatio(radial);result.radius=float(std::clamp(baseline.radius*radiusScale,double(MinimumSize),double(MaximumSize)*.5));result.height=float(std::clamp(baseline.height*ratio[1],2*double(result.radius),double(MaximumSize)));}}
        }validBody(result);if(diagnostic)diagnostic->clear();return result;
    }catch(const std::exception& error){if(diagnostic)*diagnostic=error.what();return {};}
}
std::optional<RagdollBodyPick> pickRagdollBodies(std::span<const RagdollBody> bodies,std::span<const Mat4> frames,const Ray& ray,float maximumDistance){
    if(!math::finite(ray.origin)||!math::finite(ray.direction)||math::length(ray.direction)<1e-9||!std::isfinite(maximumDistance)||maximumDistance<=0)return {};const auto direction=math::normalized(ray.direction);std::optional<RagdollBodyPick> closest;
    for(std::size_t i=0;i<bodies.size();++i){const auto& body=bodies[i];if(body.node>=frames.size())continue;const auto frame=ragdollBodyGizmoMatrix(body,frames[body.node],RagdollAuthoringOperation::Translate);if(!frame)continue;const auto inverse=math::inverse(*frame);if(!inverse)continue;const auto hit=shapeHit(body,math::transformPoint(*inverse,ray.origin),math::transformDirection(*inverse,direction),closest?closest->distance:maximumDistance);if(!hit||closest&&hit->distance>=closest->distance)continue;closest=RagdollBodyPick{i,float(hit->distance),ray.origin+direction*hit->distance,math::normalized(math::transformDirection(*frame,hit->normal))};}
    return closest;
}
std::vector<std::uint8_t> ragdollSimulationMask(std::span<const RagdollBody> bodies,std::size_t selected,RagdollSimulationScope scope){
    validHierarchy(bodies);check(scope>=RagdollSimulationScope::All&&scope<=RagdollSimulationScope::SelectedBranch,"Unsupported ragdoll simulation scope.");std::vector<std::uint8_t> mask(bodies.size());if(scope==RagdollSimulationScope::All){std::fill(mask.begin(),mask.end(),1);return mask;}if(selected>=bodies.size())return mask;mask[selected]=1;if(scope==RagdollSimulationScope::SelectedBranch)for(std::size_t i=selected+1;i<bodies.size();++i)if(bodies[i].parent>=0&&mask[std::size_t(bodies[i].parent)])mask[i]=1;return mask;
}
std::vector<std::uint8_t> ragdollScopePinMask(std::span<const RagdollBody> bodies,std::span<const std::uint8_t> simulated,bool pinScopeRoot){
    validHierarchy(bodies);check(simulated.size()==bodies.size(),"Ragdoll simulation mask must parallel its bodies.");std::vector<std::uint8_t> pinned(bodies.size());for(std::size_t i=0;i<bodies.size();++i)if(!simulated[i]||(pinScopeRoot&&(bodies[i].parent<0||!simulated[std::size_t(bodies[i].parent)])))pinned[i]=1;return pinned;
}
} // namespace crimson
