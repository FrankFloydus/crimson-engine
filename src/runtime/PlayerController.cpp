#include "PlayerController.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace crimson {
void PlayerController::setPath(std::vector<Vec3> corners) {
    reset();
    if(corners.empty() || std::any_of(corners.begin(),corners.end(),[](const Vec3& point){return !math::finite(point);}))return;
    destination_=corners.back();path_=std::move(corners);
}
PlayerMotion PlayerController::update(const Vec3& position,const Vec3& forward,float speed,double seconds) {
    PlayerMotion motion;
    const Vec3 horizontalForward{forward[0],0,forward[2]};
    motion.facing=math::finite(horizontalForward)?math::normalized(horizontalForward,{0,0,-1}):Vec3{0,0,-1};
    if(!math::finite(position) || !math::finite(forward) || !math::finite(speed) || speed<0 ||
       !std::isfinite(seconds) || seconds<=0)return motion;
    while(corner_<path_.size()) {
        const auto& target=path_[corner_];
        const double dx=static_cast<double>(target[0])-position[0],dz=static_cast<double>(target[2])-position[2];
        const double distance=std::hypot(dx,dz);
        if(distance<=ArrivalDistance){++corner_;continue;}
        if(speed>0) {
            const double magnitude=std::min(static_cast<double>(speed),distance/seconds);
            motion.velocity={dx/distance*magnitude,0,dz/distance*magnitude};
        }
        const double current=std::atan2(-motion.facing[0],-motion.facing[2]);
        const double desired=std::atan2(-dx,-dz);
        const double turn=std::remainder(desired-current,2*std::numbers::pi);
        const double yaw=current+turn*(-std::expm1(-FacingResponse*seconds));
        motion.facing={-std::sin(yaw),0,-std::cos(yaw)};
        return motion;
    }
    reset();
    return motion;
}
Vec3 cameraRelativeMovement(const Vec2& axes,const Camera& camera,float speed) {
    if(!math::finite(axes) || !math::finite(speed) || speed<=0)return {};
    const Vec3 up{0,1,0};
    auto forward=camera.target-camera.position;forward[1]=0;
    if(math::length(forward)<1e-6) {
        auto right=math::cross(camera.target-camera.position,camera.up);right[1]=0;
        right=math::normalized(right,{1,0,0});forward=math::cross(up,right);
    } else forward=math::normalized(forward,{0,0,-1});
    const auto right=math::normalized(math::cross(forward,up),{1,0,0});
    auto direction=right*std::clamp(axes[0],-1.f,1.f)+forward*std::clamp(axes[1],-1.f,1.f);
    const auto length=math::length(direction);
    return length>1e-8 ? direction*(static_cast<double>(speed)/std::max(1.0,length)):Vec3{};
}
}
