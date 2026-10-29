#include "MaterialPreviewNavigation.h"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace crimson {
namespace {
float wrap(float angle) { const auto turn=2*std::numbers::pi_v<float>;angle=std::fmod(angle+std::numbers::pi_v<float>,turn);if(angle<0)angle+=turn;return angle-std::numbers::pi_v<float>; }
}
void MaterialPreviewNavigation::tumble(Vec2 pixels) {
    if(!math::finite(pixels))return;
    yaw=wrap(yaw+pixels[0]*.008f);pitch=std::clamp(pitch+pixels[1]*.008f,-1.56079632679f,1.56079632679f);
}
void MaterialPreviewNavigation::translate(Vec2 pixels) {
    if(!math::finite(pixels))return;
    const auto scale=distance*.0015f;pan[0]-=pixels[0]*scale;pan[1]+=pixels[1]*scale;
}
void MaterialPreviewNavigation::zoom(float steps) {
    if(std::isfinite(steps))distance=std::clamp(distance*std::pow(1.12f,-std::clamp(steps,-100.f,100.f)),1.5f,8.f);
}
Mat4 MaterialPreviewNavigation::objectTransform() const {
    return math::rotation(math::axisAngle({1,0,0},pitch))*math::rotation(math::axisAngle({0,1,0},yaw));
}
float MaterialPreviewNavigation::rotateEnvironment(float degrees,float pixels) {
    if(!std::isfinite(pixels)||!std::isfinite(degrees))return degrees;
    return wrap(degrees*std::numbers::pi_v<float>/180+pixels*.008f)*180/std::numbers::pi_v<float>;
}
}
