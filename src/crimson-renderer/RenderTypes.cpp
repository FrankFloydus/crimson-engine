#include "RenderTypes.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace crimson {
namespace {
constexpr Vec3 DefaultPosition{0.0f, 1.6276402f, 4.1528387f};
constexpr Vec3 DefaultTarget{0.0f, 1.0f, 0.0f};
constexpr Vec3 WorldUp{0.0f, 1.0f, 0.0f};
using math::finite;
using math::add;
using math::scale;
using math::dot;
using math::cross;
using math::normalized;
using math::rotate;
Vec3 sub(const Vec3& a, const Vec3& b) { return math::subtract(a,b); }
void prepare(Camera& camera) {
    if (!finite(camera.position)) camera.position = DefaultPosition;
    if (!finite(camera.target)) camera.target = DefaultTarget;
    if (dot(sub(camera.target,camera.position),sub(camera.target,camera.position)) < 1e-12) camera.target = add(camera.position,{0,0,-4.2f});
    const Vec3 f = camera.forward(), r = camera.right();
    camera.up = normalized(cross(r,f),WorldUp);
}
struct LookRotation {
    double yaw{}, pitch{}, correction{};
    Vec3 right{1,0,0};
    Vec3 apply(const Vec3& v) const { return rotate(rotate(rotate(v,WorldUp,yaw),right,pitch),right,correction); }
};
LookRotation lookRotation(const Camera& camera, float dx, float dy, double sensitivity,bool preserveRoll) {
    LookRotation rotation{-double(dx)*sensitivity,-double(dy)*sensitivity,0,{}};
    rotation.right = normalized(rotate(camera.right(),WorldUp,rotation.yaw),{1,0,0});
    const Vec3 nextUp = rotation.apply(camera.up);
    if (!preserveRoll && dot(nextUp,WorldUp) < 0) {
        const Vec3 horizontalUp = normalized(sub(nextUp,scale(WorldUp,dot(nextUp,WorldUp))),{1,0,0});
        const Vec3 nextForward = rotation.apply(camera.forward());
        rotation.correction = std::acos(std::clamp(dot(horizontalUp,nextUp),-1.0,1.0)) * (nextForward[1] > 0 ? -1 : 1);
    }
    return rotation;
}
}
float PointLight::inverseRadiusSquared() const {
    const auto safeRadius=std::isfinite(radius)?std::clamp(radius,.01f,100000.f):20.f;
    return 1.f/(safeRadius*safeRadius);
}
Vec3 Camera::eye() const { return finite(position) ? position : DefaultPosition; }
Vec3 Camera::forward() const { return normalized(sub(finite(target) ? target : DefaultTarget,eye()),{0,0,-1}); }
Vec3 Camera::right() const {
    const Vec3 f = forward();
    return normalized(cross(f,finite(up) ? up : WorldUp),normalized(cross(f,{0,0,1}),{1,0,0}));
}
float Camera::distance() const {
    const Vec3 offset = sub(finite(target) ? target : DefaultTarget,eye());
    const double d = std::sqrt(dot(offset,offset));
    return std::isfinite(d) && d > 1e-6 ? static_cast<float>(d) : 4.2f;
}
float Camera::verticalFov(float aspect) const {
    constexpr double radians = std::numbers::pi / 180;
    const bool vertical = fovAxis == CameraFovAxis::Vertical;
    const double degrees = std::isfinite(fov) ? std::clamp(double(fov),vertical ? 1.0 : 15.0,vertical ? 179.0 : 100.0) : 45.0;
    if (vertical) return static_cast<float>(degrees);
    const double ratio = std::isfinite(aspect) && aspect > 0 ? aspect : 1;
    return static_cast<float>(2*std::atan(std::tan(degrees*radians*.5)/std::min(1.0,ratio))/radians);
}
Mat4 Camera::viewMatrix() const { return math::lookAt(eye(),eye()+forward(),math::cross(right(),forward())); }
Mat4 Camera::projectionMatrix(float aspect, ClipDepth depth) const {
    const float nearValue=std::isfinite(nearPlane) ? std::clamp(nearPlane,1e-7f,1e8f) : .01f;
    const float farValue=std::max(nearValue*2,std::isfinite(farPlane) ? std::clamp(farPlane,1e-6f,1e12f) : 2048.0f);
    const float ratio=std::isfinite(aspect) && aspect>0 ? aspect : 1;
    return math::perspective(verticalFov(ratio)*std::numbers::pi/180,ratio,nearValue,farValue,depth);
}
std::optional<Ray> Camera::screenRay(float x, float y, const ViewportRegion& viewport) const {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(viewport.width) || !std::isfinite(viewport.height) || viewport.width<=0 || viewport.height<=0) return {};
    const auto ray=math::rayFromScreen(x,y,projectionMatrix(viewport.width/viewport.height,ClipDepth::ZeroToOne)*viewMatrix(),
        {viewport.x,viewport.y,viewport.width,viewport.height},ClipDepth::ZeroToOne);
    if (!ray) return {};
    return Ray{eye(),ray->direction};
}
bool Camera::frameBounds(const Vec3& minimum, const Vec3& maximum, float aspect) {
    if (!finite(minimum) || !finite(maximum) || !std::isfinite(aspect) || aspect <= 0) return false;
    Vec3 center{};
    double radiusSquared = 0;
    for (size_t i = 0; i < 3; ++i) {
        if (maximum[i] < minimum[i]) return false;
        center[i] = static_cast<float>((double(minimum[i]) + maximum[i]) * .5);
        const double half = (double(maximum[i]) - minimum[i]) * .5;
        radiusSquared += half * half;
    }
    const double radius = std::max(.001, std::sqrt(radiusSquared));
    const double vertical = verticalFov(aspect) * std::numbers::pi / 360;
    const double horizontal = std::atan(std::tan(vertical) * aspect);
    const double viewDistance = radius * 1.15 / std::sin(std::min(vertical, horizontal));
    const auto newPosition = sub(center, scale(forward(), viewDistance));
    const double farDistance = std::max(2048.0, viewDistance + radius * 4);
    if (!finite(newPosition) || farDistance > std::numeric_limits<float>::max()) return false;
    position = newPosition;
    target = center;
    nearPlane = static_cast<float>(std::max(.00001, radius * .001));
    farPlane = static_cast<float>(farDistance);
    prepare(*this);
    return true;
}
void Camera::orbit(float dx, float dy, const Vec3& pivot,bool preserveRoll) {
    if (!std::isfinite(dx) || !std::isfinite(dy) || !finite(pivot)) return;
    if(std::abs(dx)<1e-6f&&std::abs(dy)<1e-6f)return;
    prepare(*this);
    const auto rotation = lookRotation(*this,dx,dy,.006,preserveRoll);
    position = add(pivot,rotation.apply(sub(position,pivot)));
    target = add(pivot,rotation.apply(sub(target,pivot)));
    up = normalized(rotation.apply(up),WorldUp);
    prepare(*this);
}
void Camera::pan(float dx, float dy, float width, float height) {
    if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) return;
    prepare(*this);
    const double worldPerPixel = std::max(.0001,2*distance()*std::tan(verticalFov(width/height)*std::numbers::pi/360))/height;
    const Vec3 motion = add(scale(right(),-dx*worldPerPixel),scale(up,dy*worldPerPixel));
    position = add(position,motion); target = add(target,motion);
}
void Camera::flyLook(float dx, float dy,bool preserveRoll) {
    if (!std::isfinite(dx) || !std::isfinite(dy)) return;
    if(std::abs(dx)<1e-6f&&std::abs(dy)<1e-6f)return;
    prepare(*this);
    const float preservedDistance = distance();
    const auto rotation = lookRotation(*this,dx,dy,.004,preserveRoll);
    target = add(position,scale(normalized(rotation.apply(forward()),{0,0,-1}),preservedDistance));
    up = normalized(rotation.apply(up),WorldUp);
    prepare(*this);
}
void Camera::flyMove(float forwardInput, float horizontalInput, float dt, float speed) {
    if (!std::isfinite(forwardInput) || !std::isfinite(horizontalInput) || !std::isfinite(dt) || !std::isfinite(speed) || dt <= 0 || speed <= 0) return;
    prepare(*this);
    const Vec3 direction = add(scale(forward(),forwardInput),scale(right(),horizontalInput));
    if (dot(direction,direction) < 1e-12) return;
    const Vec3 motion = scale(normalized(direction,{0,0,0}),std::clamp(double(dt),0.0,.25)*std::clamp(double(speed),1.0,1000.0));
    position = add(position,motion); target = add(target,motion);
}
void Camera::zoom(float wheel) {
    if (!std::isfinite(wheel) || std::abs(wheel) < 1e-6) return;
    prepare(*this);
    double result = distance(), remaining = wheel;
    const double minimum = .05, maximum = 500;
    while (std::abs(remaining) > 1e-6) {
        const double step = std::copysign(std::min(1.0,std::abs(remaining)),remaining);
        const double boundary = step > 0 ? minimum : maximum;
        const double tolerance = std::max(1e-6,std::abs(boundary)*std::numeric_limits<float>::epsilon()*8);
        if (step > 0 ? result <= boundary+tolerance : result >= boundary-tolerance) break;
        const double next = result/std::pow(1.18,step);
        if (!std::isfinite(next) || next <= 0) break;
        result = next; remaining -= step;
    }
    position = sub(target,scale(forward(),result));
}
} // namespace crimson
