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
bool finite(const Vec3& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
Vec3 add(const Vec3& a, const Vec3& b) { return {a[0]+b[0],a[1]+b[1],a[2]+b[2]}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
Vec3 scale(const Vec3& v, double s) { return {static_cast<float>(v[0]*s),static_cast<float>(v[1]*s),static_cast<float>(v[2]*s)}; }
double dot(const Vec3& a, const Vec3& b) { return double(a[0])*b[0]+double(a[1])*b[1]+double(a[2])*b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
Vec3 normalized(const Vec3& v, const Vec3& fallback) { const double n = std::sqrt(dot(v,v)); return finite(v) && n > 1e-8 ? scale(v,1/n) : fallback; }
Vec3 rotate(const Vec3& v, const Vec3& axis, double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    return add(add(scale(v,c),scale(cross(axis,v),s)),scale(axis,dot(axis,v)*(1-c)));
}
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
LookRotation lookRotation(const Camera& camera, float dx, float dy, double sensitivity) {
    LookRotation rotation{-double(dx)*sensitivity,-double(dy)*sensitivity,0,{}};
    rotation.right = normalized(rotate(camera.right(),WorldUp,rotation.yaw),{1,0,0});
    const Vec3 nextUp = rotation.apply(camera.up);
    if (dot(nextUp,WorldUp) < 0) {
        const Vec3 horizontalUp = normalized(sub(nextUp,scale(WorldUp,dot(nextUp,WorldUp))),{1,0,0});
        const Vec3 nextForward = rotation.apply(camera.forward());
        rotation.correction = std::acos(std::clamp(dot(horizontalUp,nextUp),-1.0,1.0)) * (nextForward[1] > 0 ? -1 : 1);
    }
    return rotation;
}
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
    const double degrees = std::isfinite(fov) ? std::clamp(double(fov),15.0,100.0) : 45.0;
    const double ratio = std::isfinite(aspect) && aspect > 0 ? aspect : 1;
    return static_cast<float>(2*std::atan(std::tan(degrees*radians*.5)/std::min(1.0,ratio))/radians);
}
void Camera::orbit(float dx, float dy, const Vec3& pivot) {
    if (!std::isfinite(dx) || !std::isfinite(dy) || !finite(pivot)) return;
    prepare(*this);
    const auto rotation = lookRotation(*this,dx,dy,.006);
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
void Camera::flyLook(float dx, float dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy)) return;
    prepare(*this);
    const float preservedDistance = distance();
    const auto rotation = lookRotation(*this,dx,dy,.004);
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
