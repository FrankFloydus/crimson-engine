#include "editor/MaterialPreviewNavigation.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
using namespace crimson;
namespace {
void require(bool result,const char* message){if(!result)throw std::runtime_error(message);}
bool near(float a,float b){return std::abs(a-b)<.0001f;}
}
int main(){try {
    MaterialPreviewNavigation view;
    require(view.distance==3.2f && view.pan==Vec2{} && view.objectTransform()==Mat4{},"Preview initial view changed.");
    view.tumble({100,50});require(near(view.yaw,.8f) && near(view.pitch,.4f),"Quader object tumble sensitivity changed.");
    const auto matrix=view.objectTransform();
    for(std::size_t i=0;i<3;++i)for(std::size_t j=0;j<3;++j) {
        float dot{};for(std::size_t row=0;row<3;++row)dot+=matrix.at(row,i)*matrix.at(row,j);
        require(near(dot,i==j?1.f:0.f),"Object tumble introduced scale or shear.");
    }
    const auto expected=math::rotation(math::axisAngle({1,0,0},.4f))*math::rotation(math::axisAngle({0,1,0},.8f));
    for(std::size_t i=0;i<16;++i)require(near(matrix[i],expected[i]),"Object rotation order differs from Quader mini viewport.");
    view.translate({100,-50});require(near(view.pan[0],-.48f) && near(view.pan[1],-.24f),"Pan direction or distance sensitivity differs.");
    view.zoom(1);require(near(view.distance,3.2f/1.12f),"Wheel zoom differs from reference.");
    view.zoom(1000);require(view.distance==1.5f,"Near zoom limit missing.");view.zoom(-1000);require(view.distance==8.f,"Far zoom limit missing.");
    view.tumble({1000000,1000000});require(view.yaw>=-std::numbers::pi_v<float> && view.yaw<std::numbers::pi_v<float> && near(view.pitch,1.56079632679f),"Tumble angle bounds missing.");
    const auto yaw=view.yaw;view.tumble({std::numeric_limits<float>::quiet_NaN(),0});view.zoom(std::numeric_limits<float>::infinity());
    require(view.yaw==yaw && view.distance==8.f,"Nonfinite input changed navigation.");
    require(near(MaterialPreviewNavigation::rotateEnvironment(0,100),.8f*180/std::numbers::pi_v<float>),"Environment rotation sensitivity differs.");
    const auto rotation=MaterialPreviewNavigation::rotateEnvironment(179,100);require(rotation>=-180 && rotation<180,"Environment angle did not wrap.");
    view.reset();require(view.distance==3.2f && view.pan==Vec2{} && view.objectTransform()==Mat4{},"View reset did not restore all navigation state.");
    std::cout<<"Material preview navigation checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
