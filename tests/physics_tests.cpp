#include "physics/PhysicsWorld.h"
#include "physics/CharacterMotor.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
EntityRecord floorRecord(){EntityRecord floor;floor.id=1;floor.name="Floor collider";floor.local=math::translation({0,-.5f,0});floor.collider=SceneCollider{{20,1,20}};return floor;}
EntityRecord characterRecord(){EntityRecord player;player.id=2;player.name="Character";player.actor.emplace();player.controller.emplace();player.motor.emplace();player.local=math::translation({0,2,0});return player;}
void tick(Scene& scene,PhysicsWorld& world,const Vec3& movement={},int count=1) {
    for(int i=0;i<count;++i)scene.applyRuntimePoses(world.step(scene,2,movement,1.f/60.f));
}
void floorWallAndLiveEdits() {
    Scene scene;auto player=characterRecord();scene.apply({floorRecord(),player},{});PhysicsWorld world;
    tick(scene,world,{},180);
    require(world.colliderCount()==1 && world.characterCount()==1,"Physics did not create authored objects.");
    const auto settled=*world.characterPosition(2);
    require(world.grounded(2) && settled[1]>-.05f && settled[1]<.1f,"Capsule did not settle on the explicit floor.");
    tick(scene,world,{1,0,0},60);
    require(std::abs((*world.characterPosition(2))[0]-1.f)<.08f,"Controlled velocity was not integrated at fixed rate.");
    auto records=scene.records();EntityRecord wall;wall.id=3;wall.name="Wall";wall.collider=SceneCollider{{1,4,10}};wall.local=math::translation({3,2,0});records.push_back(wall);scene.apply(records,{});
    tick(scene,world,{4,0,0},120);
    require((*world.characterPosition(2))[0]<2.25f,"Character passed through the wall.");
    const float z=(*world.characterPosition(2))[2];tick(scene,world,{3,0,2},60);
    require((*world.characterPosition(2))[0]<2.25f && (*world.characterPosition(2))[2]>z+1.5f,"Character failed to slide along a wall.");
    records=scene.records();records.erase(std::remove_if(records.begin(),records.end(),[](const auto& r){return r.id==1 || r.id==3;}),records.end());scene.apply(records,{});
    const float y=(*world.characterPosition(2))[1];tick(scene,world,{},60);
    require(world.colliderCount()==0 && (*world.characterPosition(2))[1]<y-3,"Deleting the floor left an implicit ground or stale collider.");
}
void hierarchyAndDiagnostics() {
    Scene scene;EntityRecord visual;visual.id=1;visual.name="Box parent";
    visual.local=math::translation({0,-1,0})*math::eulerRotationXYZ({0,.45f,0})*math::scaling({-8,2,8});
    EntityRecord collider;collider.id=3;collider.parent=1;collider.name="Collider";collider.collider.emplace();
    auto player=characterRecord();scene.apply({visual,collider,player},{});PhysicsWorld world;tick(scene,world,{},180);
    require(world.colliderCount()==1 && world.grounded(2),"Scaled/rotated/reflected parent was not inherited by its box collider.");
    auto records=scene.records();for(auto& r:records)if(r.id==3)r.local=math::eulerRotationXYZ({0,0,.4f});scene.apply(records,{});world.reconcile(scene);
    require(world.colliderCount()==0 && world.status().find("sheared")!=std::string::npos,"Sheared collider was silently approximated.");
    records=scene.records();for(auto& r:records)if(r.id==3){r.local={};r.parent=2;}scene.apply(records,{});world.reconcile(scene);
    require(world.colliderCount()==0,"Static collider beneath a Character Controller was enabled.");
    require(world.status().find("Character Controller")!=std::string::npos,"Rejected child collider lost its controller-ownership diagnostic.");
    records=scene.records();for(auto& r:records)if(r.id==2)r.local=r.local*math::scaling({2,1,1});scene.apply(records,{});world.reconcile(scene);
    require(world.characterCount()==0 && world.status().find("unit world scale")!=std::string::npos,"Nonuniform character scale was silently changed.");
}
void teleportLocksAndSteps() {
    Scene scene;auto player=characterRecord();player.local=math::translation({0,.1f,0});scene.apply({floorRecord(),player},{});PhysicsWorld world;tick(scene,world,{},60);
    auto records=scene.records();for(auto& r:records)if(r.id==2)r.local=math::translation({2,4,0});scene.apply(records,{});
    const std::vector<EntityId> locks{2};
    for(int i=0;i<60;++i)scene.applyRuntimePoses(world.step(scene,2,{5,0,0},1.f/60.f,locks));
    require(std::abs((*world.characterPosition(2))[0]-2)<1e-5 && std::abs((*world.characterPosition(2))[1]-4)<1e-5,"Transform gesture did not lock the character.");
    tick(scene,world,{},1);require((*world.characterPosition(2))[1]>3.99f,"Unlock retained stale falling velocity.");
    tick(scene,world,{},120);require(world.grounded(2),"Teleported actor did not refresh floor contacts.");
    records=scene.records();for(auto& r:records)if(r.id==2)r.local=math::translation({0,.05f,0});
    EntityRecord step;step.id=4;step.name="Low step";step.collider=SceneCollider{{1,.2f,4}};step.local=math::translation({1.5f,.1f,0});records.push_back(step);scene.apply(records,{});
    tick(scene,world,{1,0,0},150);require((*world.characterPosition(2))[0]>2,"Walkable small step blocked movement.");
}
void scriptedActorsAncestorLocksAndMovedFloor() {
    Scene scene;auto player=characterRecord();player.local=math::translation({0,.1f,0});
    auto scripted=characterRecord();scripted.id=4;scripted.name="Scripted actor";scripted.local=math::translation({0,.1f,3});
    EntityRecord child;child.id=5;child.parent=2;child.name="Visual gizmo target";child.local=math::translation({0,1,0});
    scene.apply({floorRecord(),player,scripted,child},{});PhysicsWorld world;tick(scene,world,{},60);
    CharacterMotor motor(world,4);require(motor.position().has_value() && motor.grounded(),"C++ motor cannot inspect its bound actor.");
    const float start=(*motor.position())[0];
    for(int i=0;i<60;++i){motor.move({2,0,0});scene.applyRuntimePoses(world.step(scene,2,{-1,0,0},1.f/60.f));}
    require((*motor.position())[0]>start+1.9f && (*world.characterPosition(2))[0]<-.9f,"Player convenience movement stomped a separately scripted actor.");
    const float stopped=(*motor.position())[0];tick(scene,world,{},1);
    require(std::abs((*motor.position())[0]-stopped)<1e-4,"Scripted movement was not consumed once per fixed tick.");
    const auto before=*world.characterPosition(2);const std::vector<EntityId> descendantLock{5};
    for(int i=0;i<30;++i)scene.applyRuntimePoses(world.step(scene,2,{5,0,0},1.f/60.f,descendantLock));
    require(math::length(*world.characterPosition(2)-before)<1e-5,"Editing a child gizmo did not freeze its Character ancestor.");
    auto records=scene.records();for(auto& record:records)if(record.id==1)record.local=math::translation({0,-10.5f,0});scene.apply(records,{});world.reconcile(scene);
    require(!world.grounded(2) && !motor.grounded(),"Moving the supporting collider left stale grounded contacts.");
    tick(scene,world,{},30);require((*world.characterPosition(2))[1]<before[1]-.8f,"Actor failed to fall after its support moved away.");
}
void slopeLimit() {
    struct Result {Vec3 position;float highestGrounded{};};
    const auto run=[](float degrees) {
        Scene scene;auto player=characterRecord();player.local=math::translation({-1,.1f,0});
        EntityRecord ramp;ramp.id=6;ramp.name="Ramp";ramp.collider=SceneCollider{{4,.2f,4}};
        const double angle=degrees*std::numbers::pi/180;
        // Its low end meets the floor. Rotating the box is the authored ramp.
        ramp.local=math::translation({1,static_cast<float>(2*std::sin(angle)),0})*math::eulerRotationXYZ({0,0,angle});
        scene.apply({floorRecord(),player,ramp},{});PhysicsWorld world;tick(scene,world,{},60);
        Result result;
        // Collision projects horizontal velocity along the slope, so uphill
        // horizontal travel is slower than on flat ground. Allow enough time
        // to traverse the complete ramp and verify ascent while supported.
        for(int i=0;i<360;++i) {
            tick(scene,world,{1,0,0});result.position=*world.characterPosition(2);
            if(world.grounded(2))result.highestGrounded=std::max(result.highestGrounded,result.position[1]);
        }
        return result;
    };
    const auto gentle=run(25),steep=run(65);
    require(gentle.position[0]>3.4f && gentle.highestGrounded>1.5f,"Character did not traverse the walkable ramp while grounded: x="+std::to_string(gentle.position[0])+", supported height="+std::to_string(gentle.highestGrounded));
    require(steep.position[0]<1.8f && steep.position[1]<.6f,"Character climbed a slope above its 45 degree limit: x="+std::to_string(steep.position[0])+", y="+std::to_string(steep.position[1]));
}
void staticWorldRaycasts() {
    Scene scene;scene.apply({floorRecord(),characterRecord()},{});PhysicsWorld world;world.reconcile(scene);
    const Ray down{{0,10,0},{0,-3,0}};
    const auto floor=world.raycast(down,20);
    require(floor && floor->entity==1 && std::abs(floor->position[1])<1e-4 && floor->normal[1]>.999f,"Raycast did not hit the static floor with its world normal, or incorrectly hit a Character.");
    require(!world.raycast(down,5),"Raycast ignored its maximum distance.");
    require(!world.raycast({{0,10,0},{}},20) && !world.raycast(down,0),"Raycast accepted a degenerate ray or range.");
    auto records=scene.records();
    EntityRecord nearWall;nearWall.id=3;nearWall.name="Near wall";nearWall.local=math::translation({0,2,2});nearWall.collider=SceneCollider{{3,4,1}};records.push_back(nearWall);
    auto farWall=nearWall;farWall.id=4;farWall.name="Far wall";farWall.local=math::translation({0,2,5});records.push_back(farWall);
    scene.apply(records,{});world.reconcile(scene);
    const auto nearest=world.raycast({{0,2,-5},{0,0,2}},20);
    require(nearest && nearest->entity==3 && std::abs(nearest->position[2]-1.5f)<1e-4 && nearest->normal[2]<-.999f,"Raycast skipped the nearest occluding wall or returned the wrong surface normal.");
    records=scene.records();for(auto& record:records)if(record.id==1)record.local=math::translation({0,-2.5f,0});scene.apply(records,{});world.reconcile(scene);
    const auto moved=world.raycast(down,20);require(moved && moved->entity==1 && std::abs(moved->position[1]+2)<1e-4,"Live collider edit left a stale raycast surface.");
    records=scene.records();std::erase_if(records,[](const auto& record){return record.id==1;});scene.apply(records,{});world.reconcile(scene);
    require(!world.raycast(down,20),"Raycast invented a ground plane after floor deletion.");
    EntityRecord parent;parent.id=8;parent.name="Reflected rotated parent";
    const auto orientation=math::eulerRotationXYZ({0,.6f,.4f});
    parent.local=math::translation({3,2,1})*orientation*math::scaling({-2,4,6});
    EntityRecord collider;collider.id=9;collider.parent=8;collider.name="Child collider";collider.collider.emplace();scene.apply({parent,collider},{});world.reconcile(scene);
    const auto up=math::transformDirection(orientation,{0,1,0});const Vec3 center{3,2,1};
    const Ray rotated{center+up*10,-up};const auto reflected=world.raycast(rotated,20);
    require(reflected && reflected->entity==9 && math::length(reflected->position-(center+up*2))<1e-4 && math::dot(reflected->normal,up)>.999,"Raycast lost reflected/nonuniform parent scale or rotated surface normals.");
    collider.local=math::eulerRotationXYZ({.3f,0,0});scene.apply({parent,collider},{});world.reconcile(scene);
    require(world.colliderCount()==0 && !world.raycast(rotated,20),"Raycast hit a sheared collider rejected by the physics world.");
}
void independentFacing() {
    Scene scene;auto player=characterRecord();player.local=math::translation({0,.05f,0});
    auto other=player;other.id=4;other.name="Other actor";other.local=math::translation({0,.05f,3});
    scene.apply({floorRecord(),player,other},{});PhysicsWorld world;tick(scene,world,{},60);
    CharacterMotor playerMotor(world,2),otherMotor(world,4);
    const auto forward=[&](EntityId id){return math::normalized(math::transformDirection(*scene.worldTransform(id),{0,0,-1}));};
    playerMotor.face({0,0,-1});otherMotor.face({-1,0,0});otherMotor.move({1,0,0});
    scene.applyRuntimePoses(world.step(scene,2,{1,0,0},1.f/60.f));
    require(forward(2)[2]<-.999f && (*playerMotor.position())[0]>.01f,"Facing override did not remain independent of player movement.");
    require(forward(4)[0]<-.999f && (*otherMotor.position())[0]>.01f,"Player command stomped another actor's independent facing or movement.");
    const float idleX=(*playerMotor.position())[0];playerMotor.face({0,0,1});tick(scene,world,{},1);
    require(forward(2)[2]>.999f && std::abs((*playerMotor.position())[0]-idleX)<1e-5,"Idle actor could not face an independent target.");
    playerMotor.face({});tick(scene,world,{1,0,0},1);
    require(forward(2)[2]>.999f,"Zero facing command did not retain yaw and suppress movement-facing.");
    tick(scene,world,{1,0,0},1);require(forward(2)[0]>.999f,"Absent facing command did not restore scripted movement-facing fallback.");
    playerMotor.face({-1,0,0});const std::vector<EntityId> locks{2};
    scene.applyRuntimePoses(world.step(scene,2,{},1.f/60.f,locks));require(forward(2)[0]>.999f,"Facing fought an active transform lock.");
    tick(scene,world,{},1);require(forward(2)[0]>.999f,"Facing queued during a transform lock replayed after unlocking.");
}
void controllerAndMotorComposition() {
    Scene scene;auto body=characterRecord();body.motor.reset();body.local=math::translation({0,2,0});
    scene.apply({floorRecord(),body},{});PhysicsWorld world;tick(scene,world,{4,0,0},60);
    require(world.characterCount()==1&&world.grounded(2)&&std::abs((*world.characterPosition(2))[0])<1e-5,
        "A controller without a Motor lost gravity/grounding or accepted horizontal movement.");
    auto records=scene.records();for(auto& record:records)if(record.id==2)record.motor.emplace();scene.apply(records,{});
    tick(scene,world,{2,0,0},30);require((*world.characterPosition(2))[0]>.95f,"Attaching Motor did not enable the existing controller.");
    records=scene.records();for(auto& record:records)if(record.id==2)record.motor->enabled=false;scene.apply(records,{});
    const auto stopped=*world.characterPosition(2);tick(scene,world,{4,0,0},30);
    require(math::length(*world.characterPosition(2)-stopped)<.001,"A disabled Motor continued moving its controller.");
    records=scene.records();for(auto& record:records)if(record.id==1)record.local=math::translation({0,-5,0});scene.apply(records,{});
    tick(scene,world,{4,0,0},30);
    require((*world.characterPosition(2))[1]<stopped[1]-.8f&&std::abs((*world.characterPosition(2))[0]-stopped[0])<.001,
        "Disabling a Motor disabled controller gravity or permitted stale horizontal commands.");
}
void geometryPublication() {
    Scene scene;auto player=characterRecord();scene.apply({floorRecord(),player},{});PhysicsWorld world;world.reconcile(scene);
    const auto revision=world.geometryRevision();const auto boxes=world.staticBoxes();
    require(boxes.size()==1 && boxes[0].entity==1 && boxes[0].size==Vec3{20,1,20},"Published navigation geometry did not match the accepted static world.");
    tick(scene,world,{1,0,0},60);require(world.geometryRevision()==revision,"Character poses invalidated static geometry.");
    auto records=scene.records();for(auto& record:records)if(record.id==2)record.motor->moveSpeed=8;
    scene.apply(records,{});world.reconcile(scene);require(world.geometryRevision()==revision,"Unrelated scene edit invalidated static geometry.");
    records=scene.records();for(auto& record:records)if(record.id==1)record.local=math::translation({0,-2,0});
    scene.apply(records,{});world.reconcile(scene);require(world.geometryRevision()==revision+1,"Live geometry edit did not publish a new revision.");
    records=scene.records();for(auto& record:records)if(record.id==1)record.local[4]=.5f;
    scene.apply(records,{});world.reconcile(scene);
    require(world.staticBoxes().empty() && world.geometryRevision()==revision+2,"Rejected shear remained in derived geometry.");
}
}
int main(){try{floorWallAndLiveEdits();hierarchyAndDiagnostics();teleportLocksAndSteps();scriptedActorsAncestorLocksAndMovedFloor();slopeLimit();staticWorldRaycasts();independentFacing();controllerAndMotorComposition();geometryPublication();std::cout<<"Physics checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
