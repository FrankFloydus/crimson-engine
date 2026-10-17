#include "navigation_mesh_fixture.h"
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
    EntityRecord actor;actor.id=2;actor.name="Actor";actor.controller.emplace();actor.motor.emplace();
    Scene scene;scene.apply({box(1,{0,-.5f,0},{20,1,20}),actor},{});PhysicsWorld physics;physics.reconcile(scene);
    crimson::test::NavigationMeshFixture navigation(crimson::test::navigationTestVolume());AgentTraversalSettings agent;
    require(navigation.reconcile(physics,agent),"Initial navigation build was skipped.");
    const Vec3 destination{5,0,2};const auto path=navigation.findPath({0,.02f,0},destination);
    if(!path)std::cerr<<navigation.status();
    require(path && path->corners.size()==1 && path->corners.back()==destination,"Flat ground did not retain an exact final click.");
    const auto revision=navigation.revision();physics.reconcile(scene);
    require(!navigation.reconcile(physics,agent) && navigation.revision()==revision,"Unchanged world rebuilt navigation.");
    actor.motor->moveSpeed=9;scene.apply({box(1,{0,-.5f,0},{20,1,20}),actor},{});physics.reconcile(scene);
    require(!navigation.reconcile(physics,agent),"Motor speed invalidated geometry clearance.");
    agent.radius=.5f;require(navigation.reconcile(physics,agent),"Capsule radius did not rebuild clearance.");
    require(!navigation.findPath({0,.02f,0},{10,0,0}),"Boundary click snapped to a distant eroded polygon.");
    require(!navigation.findPath({0,.02f,0},{50,0,0}),"Off-floor destination was approximated by a distant polygon.");
    scene.apply({},{});physics.reconcile(scene);navigation.reconcile(physics,agent);
    require(!navigation.findPath({},{1,0,0}),"Navigation invented a floor after live floor deletion.");
}
void obstacleAndLiveChanges() {
    auto floor=box(1,{0,-.5f,0},{20,1,20});auto wall=box(2,{0,1,0},{1,2,4});
    Scene scene;scene.apply({floor,wall},{});PhysicsWorld physics;physics.reconcile(scene);crimson::test::NavigationMeshFixture navigation(crimson::test::navigationTestVolume());AgentTraversalSettings agent;
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
    scene.apply({left,right},{});PhysicsWorld physics;physics.reconcile(scene);crimson::test::NavigationMeshFixture navigation(crimson::test::navigationTestVolume());AgentTraversalSettings agent;
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
    Scene scene;scene.apply({parent,floor},{});PhysicsWorld physics;physics.reconcile(scene);crimson::test::NavigationMeshFixture navigation(crimson::test::navigationTestVolume());
    navigation.reconcile(physics,{});require(navigation.findPath({0,.02f,0},{3,0,0}).has_value(),"Reflected collider winding removed valid ground.");
    parent.local=math::scaling({2,1,3});floor.local=math::eulerRotationXYZ({0,.5f,0});scene.apply({parent,floor},{});physics.reconcile(scene);navigation.reconcile(physics,{});
    require(!navigation.findPath({},{3,0,0}),"Navigation used a sheared collider disabled by physics.");
}
void slopeAndStep() {
    auto ramp=box(1,{0,0,0},{8,1,4});ramp.local=math::eulerRotationXYZ({0,0,.35f});
    Scene scene;scene.apply({ramp},{});PhysicsWorld physics;physics.reconcile(scene);crimson::test::NavigationMeshFixture navigation(crimson::test::navigationTestVolume());
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
    crimson::test::NavigationMeshFixture navigation(crimson::test::navigationTestVolume());AgentTraversalSettings agent;agent.height=std::numeric_limits<float>::max();
    navigation.reconcile(physics,agent);require(!navigation.findPath({},{1,0,0}) && !navigation.status().empty(),"Extreme Character dimensions escaped safe voxel validation.");
    agent={};navigation.reconcile(physics,agent);require(navigation.findPath({},{1,0,0}).has_value(),"Valid dimensions failed to recover after a rejected build.");
    scene.apply({box(1,{0,-.5f,0},{1000,1,1000})},{});physics.reconcile(scene);navigation.reconcile(physics,agent);
    require(navigation.findPath({},{1,0,0}).has_value(),"A large source collider escaped explicit volume/tile clipping.");
    scene.apply({box(1,{0,-.5f,0},{.01f,1,.01f})},{});physics.reconcile(scene);navigation.reconcile(physics,agent);
    require(!navigation.findPath({},{.001f,0,0}),"Sub-cell geometry created an invalid walking surface.");
}
void closestReachableDestinations() {
    auto floor=box(1,{0,-.5f,0},{20,1,20}),wall=box(2,{0,1,0},{1,2,4});
    Scene scene;scene.apply({floor,wall},{});PhysicsWorld physics;physics.reconcile(scene);
    crimson::test::NavigationMeshFixture navigation(crimson::test::navigationTestVolume());AgentTraversalSettings agent;navigation.reconcile(physics,agent);
    const Vec3 start{-4,.02f,0},exact{4,0,0};
    const auto strict=navigation.findPath(start,exact),preserved=navigation.findReachablePath(start,exact);
    require(strict && preserved && strict->corners==preserved->corners && preserved->corners.back()==exact,
        "Reachable fallback changed an exact valid click or its complete route.");
    const auto side=navigation.findReachablePath(start,{-.5f,1,0});
    require(side && side->corners.back()[0]<-.8f && side->corners.back()[0]>-1.3f &&
        std::abs(side->corners.back()[2])<.1f && side->corners.back()[1]<.2f,
        "Wall-side click did not approach its closest capsule-clear ground point.");
    require(!navigation.findPath(start,{-.5f,1,0}),"Strict wall-side query lost its rejection semantics.");
    const auto roof=navigation.findReachablePath(start,{0,2,0});
    require(roof && roof->corners.back()[1]<.2f && roof->corners.back()[0]<-.8f && std::abs(roof->corners.back()[2])<.1f,
        "Rooftop click selected a disconnected roof or ignored the nearer starting side tie-break.");
    const auto farSide=navigation.findReachablePath(start,{.5f,1,0});
    require(farSide && farSide->corners.back()[0]>.8f && farSide->corners.size()>=3 &&
        std::any_of(farSide->corners.begin(),farSide->corners.end(),[](const auto& point){return std::abs(point[2])>2.3f;}),
        "Closest reachable wall side did not retain its complete around-wall corridor.");
    const auto revision=navigation.revision();
    for(unsigned repeat=0;repeat<32;++repeat) {
        const auto again=navigation.findReachablePath(start,{0,2,0});
        require(again && again->corners==roof->corners && navigation.revision()==revision,
            "Repeated fallback queries changed a deterministic endpoint or rebuilt connectivity.");
    }
    const auto edge=navigation.findReachablePath(start,{50,0,0});
    require(edge && edge->corners.back()[0]>9 && edge->corners.back()[0]<10,
        "Off-floor click did not resolve to the existing eroded ground boundary.");
    require(!navigation.findReachablePath({100,.02f,100},exact) && !navigation.findReachablePath({-4,20,0},exact),
        "Fallback invented a connected starting floor for an off-mesh Character.");
    require(!navigation.findReachablePath(start,{std::numeric_limits<float>::infinity(),0,0}) &&
        !navigation.findReachablePath({std::numeric_limits<float>::max(),0,0},exact),
        "Invalid or out-of-bounds start escaped safe query validation.");
    const auto largeBox=box(3,{0,1,0},{4,2,4});scene.apply({floor,largeBox},{});physics.reconcile(scene);navigation.reconcile(physics,agent);
    require(navigation.findPath({0,2.02f,0},{.5f,2,0}).has_value(),"Roof fixture must contain a real disconnected walkable roof.");
    const auto realRoof=navigation.findReachablePath(start,{0,2,0});
    require(realRoof && realRoof->corners.back()[1]<.2f && realRoof->corners.back()[0]<-2.3f &&
        std::abs(realRoof->corners.back()[2])<.1f,"Fallback chose the disconnected walkable roof over reachable ground.");
    wall.collider->size={1,2,30};scene.apply({floor,wall},{});physics.reconcile(scene);navigation.reconcile(physics,agent);
    const auto partition=navigation.findReachablePath(start,exact);
    require(partition && partition->corners.back()[0]<-.8f && partition->corners.back()[0]>-1.3f &&
        std::abs(partition->corners.back()[2])<.1f && partition->corners.back()[1]<.2f,
        "Partition fallback selected a disconnected destination instead of the reachable boundary.");
    require(!navigation.findPath(start,exact),"Strict query accepted a disconnected partition.");
    scene.apply({floor},{});physics.reconcile(scene);navigation.reconcile(physics,agent);
    const auto reopened=navigation.findReachablePath(start,exact);
    require(reopened && reopened->corners.size()==1 && reopened->corners.back()==exact,
        "Removing a partition did not recover the original exact request.");
    scene.apply({},{});physics.reconcile(scene);navigation.reconcile(physics,agent);
    require(!navigation.findReachablePath(start,exact),"Deleted floor left stale component fallback data.");
}
void reachableIslandsAndClearance() {
    Scene scene;auto left=box(1,{-3,-.5f,0},{4,1,6}),right=box(2,{3,-.5f,0},{4,1,6});
    scene.apply({left,right},{});PhysicsWorld physics;physics.reconcile(scene);crimson::test::NavigationMeshFixture navigation(crimson::test::navigationTestVolume());AgentTraversalSettings agent;
    navigation.reconcile(physics,agent);const Vec3 start{-3,.02f,0};
    const auto island=navigation.findReachablePath(start,{3,0,0});
    require(island && island->corners.back()[0]<-1.3f && island->corners.back()[0]>-1.8f && std::abs(island->corners.back()[2])<.1f,
        "Island fallback crossed a gap or selected the disconnected clicked island.");
    const auto before=island->corners.back();agent.radius=.6f;navigation.reconcile(physics,agent);
    const auto wider=navigation.findReachablePath(start,{3,0,0});
    require(wider && wider->corners.back()[0]<before[0]-.2f,"Fallback ignored rebuilt capsule clearance at an island boundary.");
    auto floor=box(1,{0,-.5f,0},{12,1,6}),ceiling=box(2,{0,2,0},{1,1,8});
    scene.apply({floor,ceiling},{});physics.reconcile(scene);agent={};navigation.reconcile(physics,agent);
    const auto low=navigation.findReachablePath(start,{3,0,0});
    require(low && low->corners.back()[0]<-.8f && low->corners.back()[1]<.2f,
        "Fallback routed a tall capsule through a low ceiling or onto its roof.");
    agent.height=1;navigation.reconcile(physics,agent);const Vec3 target{3,0,0};
    const auto shortRoute=navigation.findReachablePath(start,target);
    require(shortRoute && shortRoute->corners.back()==target,"Reduced capsule height did not restore an exact reachable destination.");
}
}
int main(){try{flatGroundAndCache();obstacleAndLiveChanges();islandsAndClearance();acceptedGeometryOnly();slopeAndStep();buildBounds();closestReachableDestinations();reachableIslandsAndClearance();std::cout<<"Navigation tests passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
