#include "runtime/PlayerController.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
using namespace crimson;
constexpr Vec3 Forward{0,0,-1};
constexpr double Tick=1.0/60.0;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(const Vec3& actual,const Vec3& expected,double tolerance=2e-5) {
    require(math::finite(actual) && math::length(actual-expected)<=tolerance,"Unexpected controller vector.");
}
Vec3 direction(double yaw){return {-std::sin(yaw),0,-std::cos(yaw)};}
double yaw(const Vec3& direction){return std::atan2(-direction[0],-direction[2]);}
void pathAndIdleFacing() {
    PlayerController controller;const auto idle=controller.update({},Forward,5,Tick);
    near(idle.velocity,{});near(idle.facing,Forward);
    controller.setPath({{3,0,0},{3,0,-4}});
    const auto first=controller.update({},Forward,5,Tick);
    near(first.velocity,{5,0,0});require(first.facing[0]>0 && first.facing[0]<.9f,"Path facing did not turn smoothly toward its first waypoint.");
    require(controller.destination()==std::optional{Vec3{3,0,-4}},"Path lost its final clicked destination.");
    const auto corner=controller.update({3,0,0},{1,0,0},5,Tick);
    near(corner.velocity,{0,0,-5});require(corner.facing[2]<0 && corner.facing[0]>0,"Corner did not turn toward the next path segment.");
    require(controller.cornerIndex()==1,"Path did not advance past its reached corner.");
    const auto arrived=controller.update({3,0,-4},corner.facing,5,Tick);
    require(!controller.destination(),"Arrival retained its destination.");near(arrived.velocity,{});near(arrived.facing,corner.facing);
    near(controller.update({3,0,-4},arrived.facing,5,1).facing,arrived.facing);
    controller.setPath({{10,0,0}});controller.reset();require(controller.path().empty(),"Reset retained old corners.");
}
void arrivalAndRetarget() {
    PlayerController controller;Vec3 position{},forward=Forward;const Vec3 destination{1,0,0};
    controller.setPath({destination});
    for(unsigned step=0;step<100 && controller.destination();++step) {
        const auto motion=controller.update(position,forward,5,Tick);
        const auto displacement=motion.velocity*Tick;
        require(math::length(displacement)<=math::length(destination-position)+1e-6,"Final tick overshot its destination.");
        position=position+displacement;forward=motion.facing;
    }
    require(!controller.destination(),"Controller never arrived.");near(position,destination,PlayerController::ArrivalDistance+1e-5);
    controller.setPath({{10,0,0}});controller.setPath({{0,0,-10}});
    near(controller.update({},Forward,5,Tick).velocity,{0,0,-5});
    controller.setPath({{.02f,0,0}});
    require(math::length(controller.update({},Forward,100,1).velocity)<=.020001,"Large timestep overshot a close click.");
    controller.setPath({{0,0,0},{0,5,0},{1,0,0}});
    near(controller.update({},Forward,5,Tick).velocity,{5,0,0});
}
void shortestTurn() {
    PlayerController controller;const double start=179*std::numbers::pi/180,target=-179*std::numbers::pi/180;
    controller.setPath({direction(target)*10});
    const auto motion=controller.update({},direction(start),5,Tick);
    const auto turn=std::remainder(yaw(motion.facing)-start,2*std::numbers::pi);
    require(turn>0 && turn<2*std::numbers::pi/180,"Path facing took the long turn across the yaw seam.");
    const auto expected=std::remainder(target-start,2*std::numbers::pi)*(1-std::exp(-PlayerController::FacingResponse*Tick));
    require(std::abs(turn-expected)<2e-6,"Facing did not use exponential response.");
    controller.setPath({{10,0,3}});const auto whole=controller.update({},Forward,0,.1).facing;
    auto divided=Forward;for(int step=0;step<6;++step)divided=controller.update({},divided,0,.1/6).facing;
    near(whole,divided,5e-5);
}
void invalidInputs() {
    const auto nan=std::numeric_limits<float>::quiet_NaN();PlayerController controller;
    controller.setPath({{10,0,0},{nan,0,0}});require(!controller.destination(),"Nonfinite path accepted.");
    controller.setPath({{10,0,0}});
    near(controller.update({},Forward,5,0).velocity,{});near(controller.update({},Forward,-1,Tick).velocity,{});
    near(controller.update({nan,0,0},Forward,5,Tick).velocity,{});
    const auto invalidForward=controller.update({},Vec3{nan,0,0},5,Tick);
    near(invalidForward.facing,Forward);near(invalidForward.velocity,{});
    controller.setPath({});require(!controller.destination(),"Empty path retained old destination.");
}
}
int main(){try{pathAndIdleFacing();arrivalAndRetarget();shortestTurn();invalidInputs();std::cout<<"Player controller tests passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
