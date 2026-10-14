#include "navigation/NavigationMesh.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
EntityRecord box(EntityId id,Vec3 center,Vec3 size) {
    EntityRecord record;record.id=id;record.name="Box";record.local=math::translation(center);record.collider=SceneCollider{size};return record;
}
void flatGroundAndCache() {
    Scene scene;scene.apply({box(1,{0,-.5f,0},{20,1,20})},{});PhysicsWorld physics;physics.reconcile(scene);
    NavigationMesh navigation;SceneCharacter agent;
    require(navigation.reconcile(physics,agent),"Initial navigation build was skipped.");
    const Vec3 destination{5,0,2};const auto path=navigation.findPath({0,.02f,0},destination);
    if(!path)std::cerr<<navigation.status();
    require(path && path->corners.size()==1 && path->corners.back()==destination,"Flat ground did not retain an exact final click.");
    const auto revision=navigation.revision();physics.reconcile(scene);
    require(!navigation.reconcile(physics,agent) && navigation.revision()==revision,"Unchanged world rebuilt navigation.");
    agent.moveSpeed=9;require(!navigation.reconcile(physics,agent),"Movement speed invalidated geometry clearance.");
    agent.radius=.5f;require(navigation.reconcile(physics,agent),"Capsule radius did not rebuild clearance.");
    require(!navigation.findPath({0,.02f,0},{10,0,0}),"Boundary click snapped to a distant eroded polygon.");
    require(!navigation.findPath({0,.02f,0},{50,0,0}),"Off-floor destination was approximated by a distant polygon.");
    scene.apply({},{});physics.reconcile(scene);navigation.reconcile(physics,agent);
    require(!navigation.findPath({},{1,0,0}),"Navigation invented a floor after live floor deletion.");
}
void obstacleAndLiveChanges() {
    auto floor=box(1,{0,-.5f,0},{20,1,20});auto wall=box(2,{0,1,0},{1,2,4});
    Scene scene;scene.apply({floor,wall},{});PhysicsWorld physics;physics.reconcile(scene);NavigationMesh navigation;SceneCharacter agent;
    navigation.reconcile(physics,agent);const Vec3 start{-4,.02f,0},end{4,0,0};const auto route=navigation.findPath(start,end);
    require(route && route->corners.size()>=3 && route->corners.back()==end,"Detour did not route around an intervening obstacle.");
    require(std::any_of(route->corners.begin(),route->corners.end(),[](const auto& point){return std::abs(point[2])>2.3f;}),"Route omitted capsule clearance around wall corners.");
    const auto revision=navigation.revision();scene.apply({floor},{});physics.reconcile(scene);
    require(navigation.reconcile(physics,agent) && navigation.revision()==revision+1,"Live obstacle deletion did not invalidate navigation.");
    const auto direct=navigation.findPath(start,end);require(direct && direct->corners.size()==1,"Deleted wall remained in route.");
    wall.collider->size={1,2,30};scene.apply({floor,wall},{});physics.reconcile(scene);navigation.reconcile(physics,agent);
    require(!navigation.findPath(start,end),"Unreachable side of a floor-spanning wall accepted a partial route.");
    wall.local=math::translation({0,1,6});wall.collider->size={1,2,4};scene.apply({floor,wall},{});physics.reconcile(scene);navigation.reconcile(physics,agent);
    const auto moved=navigation.findPath(start,end);require(moved && moved->corners.size()==1,"Moving a wall left a stale route obstruction.");
}
void islandsAndClearance() {
    Scene scene;auto left=box(1,{-3,-.5f,0},{4,1,6}),right=box(2,{3,-.5f,0},{4,1,6});
    scene.apply({left,right},{});PhysicsWorld physics;physics.reconcile(scene);NavigationMesh navigation;SceneCharacter agent;
    navigation.reconcile(physics,agent);
    require(!navigation.findPath({-3,.02f,0},{3,0,0}),"Disconnected floor islands returned a partial path.");
    auto floor=box(1,{0,-.5f,0},{12,1,6}),ceiling=box(2,{0,2,0},{1,1,8});
    scene.apply({floor,ceiling},{});physics.reconcile(scene);navigation.reconcile(physics,agent);
    require(!navigation.findPath({-3,.02f,0},{3,0,0}),"Tall capsule routed through a low ceiling.");
    agent.height=1;navigation.reconcile(physics,agent);
    require(navigation.findPath({-3,.02f,0},{3,0,0}).has_value(),"Short capsule failed to use a newly cleared low passage.");
    auto upper=box(2,{0,.75f,2.2f},{12,2.5f,3}),lower=box(3,{0,.75f,-2.2f},{12,2.5f,3});
    scene.apply({floor,upper,lower},{});physics.reconcile(scene);agent.height=1.8f;agent.radius=.3f;navigation.reconcile(physics,agent);
    require(navigation.findPath({-3,.02f,0},{3,0,0}).has_value(),"Narrow capsule cannot use a sufficiently wide passage.");
    agent.radius=.6f;navigation.reconcile(physics,agent);
    require(!navigation.findPath({-3,.02f,0},{3,0,0}),"Wide capsule routed through an undersized passage.");
}
void acceptedGeometryOnly() {
    EntityRecord parent;parent.id=1;parent.name="Reflected parent";parent.local=math::scaling({-2,1,2});
    auto floor=box(2,{0,-.5f,0},{10,1,10});floor.parent=1;
    Scene scene;scene.apply({parent,floor},{});PhysicsWorld physics;physics.reconcile(scene);NavigationMesh navigation;
    navigation.reconcile(physics,{});require(navigation.findPath({0,.02f,0},{3,0,0}).has_value(),"Reflected collider winding removed valid ground.");
    parent.local=math::scaling({2,1,3});floor.local=math::eulerRotationXYZ({0,.5f,0});scene.apply({parent,floor},{});physics.reconcile(scene);navigation.reconcile(physics,{});
    require(!navigation.findPath({},{3,0,0}),"Navigation used a sheared collider disabled by physics.");
}
void slopeAndStep() {
    auto ramp=box(1,{0,0,0},{8,1,4});ramp.local=math::eulerRotationXYZ({0,0,.35f});
    Scene scene;scene.apply({ramp},{});PhysicsWorld physics;physics.reconcile(scene);NavigationMesh navigation;
    navigation.reconcile(physics,{});
    require(navigation.findPath(math::transformPoint(ramp.local,{-2,.52f,0}),math::transformPoint(ramp.local,{2,.5f,0})).has_value(),"Walkable ramp was excluded from navigation.");
    ramp.local=math::eulerRotationXYZ({0,0,1.f});scene.apply({ramp},{});physics.reconcile(scene);navigation.reconcile(physics,{});
    require(!navigation.findPath(math::transformPoint(ramp.local,{-2,.52f,0}),math::transformPoint(ramp.local,{2,.5f,0})),"Steep ramp exceeded the motor's 45 degree slope limit.");
    const auto floor=box(1,{-3,-.5f,0},{6,1,6});auto step=box(2,{3,-.4f,0},{6,1.2f,6});
    scene.apply({floor,step},{});physics.reconcile(scene);navigation.reconcile(physics,{});
    require(navigation.findPath({-3,.02f,0},{3,.2f,0}).has_value(),"Walkable 20 cm step was disconnected.");
    step.local=math::translation({3,0,0});scene.apply({floor,step},{});physics.reconcile(scene);navigation.reconcile(physics,{});
    require(!navigation.findPath({-3,.02f,0},{3,.6f,0}),"Navigation crossed a step above the motor's climb height.");
}
void buildBounds() {
    Scene scene;scene.apply({box(1,{0,-.5f,0},{20,1,20})},{});PhysicsWorld physics;physics.reconcile(scene);
    NavigationMesh navigation;SceneCharacter agent;agent.height=std::numeric_limits<float>::max();
    navigation.reconcile(physics,agent);require(!navigation.findPath({},{1,0,0}) && !navigation.status().empty(),"Extreme Character dimensions escaped safe voxel validation.");
    agent={};navigation.reconcile(physics,agent);require(navigation.findPath({},{1,0,0}).has_value(),"Valid dimensions failed to recover after a rejected build.");
    scene.apply({box(1,{0,-.5f,0},{1000,1,1000})},{});physics.reconcile(scene);navigation.reconcile(physics,agent);
    require(!navigation.findPath({},{1,0,0}) && navigation.status().find("4-million-cell")!=std::string::npos,"Oversized navigation grid was not rejected before allocation.");
    scene.apply({box(1,{0,-.5f,0},{.01f,1,.01f})},{});physics.reconcile(scene);navigation.reconcile(physics,agent);
    require(!navigation.findPath({},{.001f,0,0}) && !navigation.status().empty(),"Sub-cell geometry created an invalid zero-size grid.");
}
}
int main(){try{flatGroundAndCache();obstacleAndLiveChanges();islandsAndClearance();acceptedGeometryOnly();slopeAndStep();buildBounds();std::cout<<"Navigation tests passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
