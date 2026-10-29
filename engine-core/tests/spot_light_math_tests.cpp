#include <crimson/renderer/forward/SpotLightMath.h>
#include <crimson/renderer/forward/ClusteredLighting.h>
#include <crimson/renderer/forward/DirectionalShadowMath.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
using namespace crimson;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Vec3 ray(double degrees){const double angle=degrees*std::numbers::pi/180;return {std::sin(angle),0,-std::cos(angle)};}
void cones() {
    PointLight light;light.type=LocalLightType::Spot;light.position={4,6,8};light.direction={0,0,-5};
    check(spot_lighting::valid(light),"Default Spot response is invalid.");
    check(spot_lighting::attenuation(light,ray(0))==1&&spot_lighting::attenuation(light,ray(29))==1,
        "Spot inner cone lost its full-intensity plateau.");
    check(spot_lighting::attenuation(light,ray(46))==0&&spot_lighting::attenuation(light,ray(180))==0,
        "Spot emitted outside its outer cone or behind its source.");
    const auto edge=spot_lighting::attenuation(light,ray(38));
    light.coneFalloff=2;
    check(edge>0&&edge<1&&std::abs(spot_lighting::attenuation(light,ray(38))-edge*edge)<1e-6,
        "Cone Falloff did not shape only the inner/outer transition.");
    light.innerConeAngle=light.outerConeAngle;
    check(spot_lighting::attenuation(light,ray(44))==1&&spot_lighting::attenuation(light,ray(46))==0,
        "Coincident cone angles did not produce a finite hard edge.");
    light.outerConeAngle=.1f;light.innerConeAngle=0;
    check(spot_lighting::valid(light)&&std::isfinite(spot_lighting::attenuation(light,ray(.05))),"Narrow valid cone became nonfinite.");
    light.outerConeAngle=90;check(!spot_lighting::valid(light),"Hemisphere projection accepted singular tangent.");
    light.outerConeAngle=45;light.direction={};check(!spot_lighting::valid(light),"Zero Spot direction passed validation.");
    light.type=LocalLightType::Point;
    check(spot_lighting::valid(light)&&spot_lighting::attenuation(light,{})==1,
        "Dormant cone properties changed an ordinary Point light.");
}
void boundsAndProjection() {
    PointLight light;light.type=LocalLightType::Spot;light.position={1000,-400,2000};light.radius=30;
    for(float angle:{.1f,1.f,30.f,45.f,60.f,89.f}) {
        light.innerConeAngle=0;light.outerConeAngle=angle;
        const auto sphere=spot_lighting::influence_sphere(light,light.radius);
        const Vec3 center{sphere[0],sphere[1],sphere[2]};
        for(unsigned radial=0;radial<=8;++radial)for(unsigned azimuth=0;azimuth<32;++azimuth) {
            const double phi=azimuth*2*std::numbers::pi/32;
            const auto edge=ray(angle);const Vec3 direction{edge[0]*std::cos(phi),edge[0]*std::sin(phi),edge[2]};
            const auto point=light.position+direction*(light.radius*radial/8);
            check(math::length(point-center)<=sphere[3]+.001f,"Cone cull sphere removed an illuminated finite point.");
        }
        const auto camera=spot_lighting::shadow_camera(light,1024);
        check(math::finite(camera.viewMatrix())&&math::finite(camera.projectionMatrix(1,ClipDepth::ZeroToOne)),
            "Valid cone produced a nonfinite shadow camera.");
        const auto expectedScale=1/(std::tan(angle*std::numbers::pi/180)*point_shadow::faceTanHalfFov(1024));
        check(std::abs(camera.projectionMatrix(1,ClipDepth::ZeroToOne).at(0,0)-expectedScale)<expectedScale*1e-5,
            "A narrow Spot projection was clamped to the scene-camera field-of-view limit.");
        const auto centerPoint=light.position+light.direction*5;
        const auto relative=centerPoint-light.position;
        const auto clip=math::transformHomogeneous(spot_lighting::shadow_relative_view_projection(light,1024),
            {relative[0],relative[1],relative[2],1});
        check(clip[3]>0&&std::abs(clip[0]/clip[3])<.001&&std::abs(clip[1]/clip[3])<.001,
            "Spot travel direction does not project to the shadow center.");
    }
    light.position={};light.innerConeAngle=20;light.outerConeAngle=30;
    check(spot_lighting::intersects({{-.1f,-.1f,-5.1f},{.1f,.1f,-4.9f},true},light,1024),"Spot removed an axial caster.");
    check(!spot_lighting::intersects({{-.1f,-.1f,4.9f},{.1f,.1f,5.1f},true},light,1024),"Spot retained a wholly behind-source caster.");
    check(!spot_lighting::intersects({{9.9f,-.1f,-5.1f},{10.1f,.1f,-4.9f},true},light,1024),"Spot retained a wholly outside-cone caster.");
    const Aabb demand{{-.1f,-.1f,-5.1f},{.1f,.1f,-4.9f},true};
    light.outerConeAngle=.1f;light.innerConeAngle=0;
    const auto narrow=spot_lighting::caster_texel_world(demand,light,1024);
    check(std::abs(narrow-2*4.9*std::tan(.1*std::numbers::pi/180)/1024)<1e-9,
        "Spot shadow demand retained the Point cube's ninety-degree footprint.");
    light.outerConeAngle=89;
    const auto wide=spot_lighting::caster_texel_world(demand,light,1024);
    check(wide>narrow*10000,"Wide/narrow Spot demand did not follow authored projection scale.");
    std::array<streaming::ModelLod,3> levels;levels[0].error=0;levels[1].error=.001f;levels[2].error=.01f;
    check(directional_shadow::chooseLod(levels,{},narrow)==0&&directional_shadow::chooseLod(levels,{},wide)==2,
        "Spot screen-error LOD did not retain fine geometry for a narrow cone.");
    light.direction=math::normalized(Vec3{.137f,-.257f,-1});
    auto origin=spot_lighting::shadow_view(light);light.position={10000,10000,10000};
    auto translated=spot_lighting::shadow_view(light);
    for(unsigned row=0;row<3;++row)for(unsigned column=0;column<3;++column)
        check(origin.at(row,column)==translated.at(row,column),"World translation quantized a Spot's projection orientation.");
    light.direction={0,0,-1};light.outerConeAngle=.1f;light.innerConeAngle=0;
    for(const Vec3 position:{Vec3{},Vec3{1000,-400,2000},Vec3{10000,10000,10000},Vec3{100000,-200000,400000}}) {
        light.position=position;
        const Aabb axial{{position[0],position[1],position[2]-.07f},{position[0],position[1],position[2]-.06f},true};
        check(spot_lighting::intersects(axial,light,1024),"World translation falsely culled a narrow on-axis Spot caster.");
    }
}
}
int main(){try{cones();boundsAndProjection();std::cout<<"Spot cone response, finite bounds, projective camera and culling checks passed.\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
