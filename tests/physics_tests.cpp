#include "scene/SceneComponents.h"
#include "physics/PhysicsWorld.h"
#include "physics/CharacterMotor.h"
#include "agent_profile_fixture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace {
using namespace crimson;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
EntityRecord floorRecord(){EntityRecord floor;floor.id=1;floor.name="Floor collider";floor.local=math::translation({0,-.5f,0});floor.collider=SceneCollider{{20,1,20}};return floor;}
EntityRecord characterRecord(){EntityRecord player;player.id=2;player.name="Character";player.actor.emplace();player.controller.emplace();player.motor.emplace();player.local=math::translation({0,2,0});return player;}
void tick(Scene& scene,PhysicsWorld& world,const Vec3& movement={},int count=1) {
    for(int i=0;i<count;++i)scene.applyRuntimePoses(world.step(scene,2,movement,1.f/60.f));
}
void firstPersonTick(Scene& scene,PhysicsWorld& world,const Vec3& movement={},int count=1,float airControl=.35f) {
    CharacterMotor motor(world,2);
    for(int i=0;i<count;++i){motor.moveFirstPerson(movement,45,8,12,airControl);scene.applyRuntimePoses(world.step(scene,NoEntity,{},1.f/60.f));}
}
void firstPersonAccelerationBrakingAndAirControl() {
    Scene scene;auto player=characterRecord();player.local=math::translation({0,.05f,0});scene.apply({floorRecord(),player},{});
    PhysicsWorld world;firstPersonTick(scene,world,{},60);CharacterMotor motor(world,2);
    firstPersonTick(scene,world,{6,0,0});const auto first=*motor.velocity();
    require(first[0]>.6f && first[0]<.9f,"FPS movement did not accelerate smoothly from rest.");
    firstPersonTick(scene,world,{6,0,0},15);
    require(std::abs((*motor.velocity())[0]-6)<.02,"FPS ground acceleration did not reach the requested movement speed.");
    const auto brakingStart=*motor.position();firstPersonTick(scene,world,{},45);
    require(std::abs((*motor.velocity())[0])<1e-4 && (*motor.position())[0]<brakingStart[0]+1,"FPS ground friction did not stop the actor promptly.");
    firstPersonTick(scene,world,{6,0,0},16);motor.jump(1.1f);firstPersonTick(scene,world,{6,0,0});
    require(!motor.grounded() && (*motor.velocity())[1]>4,"FPS jump did not leave the floor with the height-derived upward impulse.");
    firstPersonTick(scene,world,{},8);
    require(std::abs((*motor.velocity())[0]-6)<.02,"Releasing movement in the air incorrectly applied ground friction.");
    firstPersonTick(scene,world,{0,0,-6},6,0);
    require(std::abs((*motor.velocity())[0]-6)<.02 && std::abs((*motor.velocity())[2])<.01,"Zero air control still redirected airborne momentum.");
    firstPersonTick(scene,world,{0,0,-6},6,1);
    require((*motor.velocity())[0]<5.5f && (*motor.velocity())[2]<-.5f,"Authored air control did not allow airborne steering.");
}
void firstPersonJumpEdgesHeightAndBuffer() {
    Scene scene;auto player=characterRecord();player.local=math::translation({0,.05f,0});scene.apply({floorRecord(),player},{});
    PhysicsWorld world;firstPersonTick(scene,world,{},60);CharacterMotor motor(world,2);const auto floor=(*motor.position())[1];
    motor.jump(1.1f);firstPersonTick(scene,world);float highest=(*motor.position())[1];
    firstPersonTick(scene,world,{},8);const auto before=(*motor.velocity())[1];motor.jump(1.1f);firstPersonTick(scene,world);
    require((*motor.velocity())[1]<before,"A second jump press gave an unsupported actor an extra jump.");
    for(int i=0;i<100;++i){firstPersonTick(scene,world);highest=std::max(highest,(*motor.position())[1]);}
    require(highest-floor>1.f && highest-floor<1.15f,"FPS jump height did not match the authored 1.1 m ballistic arc.");
    require(motor.grounded() && std::abs((*motor.position())[1]-floor)<.02,"Jump repeated without a new press edge or failed to land.");

    Scene fallingScene;player.local=math::translation({0,.6f,0});fallingScene.apply({floorRecord(),player},{});PhysicsWorld fallingWorld;
    CharacterMotor fallingMotor(fallingWorld,2);bool queued=false,rebounded=false;
    for(int i=0;i<90;++i) {
        if(!queued && fallingMotor.position() && !fallingMotor.grounded() && (*fallingMotor.position())[1]<.12f){fallingMotor.jump(1.1f);queued=true;}
        firstPersonTick(fallingScene,fallingWorld);
        if(queued && (*fallingMotor.velocity())[1]>3){rebounded=true;break;}
    }
    require(queued && rebounded,"A jump press just before landing was lost instead of using the landing buffer.");
}
void firstPersonCoyoteWindow() {
    const auto departure=[](int ticksBeforePress) {
        Scene scene;auto player=characterRecord();player.local=math::translation({0,.05f,0});scene.apply({floorRecord(),player},{});
        PhysicsWorld world;firstPersonTick(scene,world,{},60);CharacterMotor motor(world,2);
        auto records=scene.records();std::erase_if(records,[](const auto& record){return record.id==1;});scene.apply(records,{});
        firstPersonTick(scene,world,{},ticksBeforePress);motor.jump(1.1f);firstPersonTick(scene,world);return (*motor.velocity())[1];
    };
    require(departure(3)>4,"Jump immediately after leaving support did not use the coyote window.");
    require(departure(10)<0,"Expired coyote time permitted an unsupported jump.");
}
void firstPersonCanceledJumpBuffer() {
    const auto fall=[](bool cancel) {
        Scene scene;auto player=characterRecord();player.local=math::translation({0,1,0});scene.apply({floorRecord(),player},{});
        PhysicsWorld world;CharacterMotor motor(world,2);
        for(int i=0;i<60;++i){firstPersonTick(scene,world);if((*motor.position())[1]<.35f)break;}
        motor.jump(1.1f);firstPersonTick(scene,world);
        require(!motor.grounded() && (*motor.velocity())[1]<-1,"Canceled-jump fixture did not establish a buffered airborne press.");
        const auto velocity=*motor.velocity();
        if(cancel) {
            // Focus can be lost and reacquired before another fixed tick.
            // Cancel both the consumed landing buffer and newly queued input.
            motor.jump(1.1f);motor.moveFirstPerson({6,0,0},45,8,12,.35f);motor.face({1,0,0});
            world.cancelCharacterInput(2);
            require(*motor.velocity()==velocity,"Input cancellation changed the accepted ballistic velocity.");
        }
        firstPersonTick(scene,world);
        require((*motor.velocity())[1]<velocity[1]-.1f,"Input cancellation interrupted falling momentum before reacquisition.");
        bool jumped=false;
        for(int i=0;i<30;++i){firstPersonTick(scene,world);if((*motor.velocity())[1]>1)jumped=true;}
        return jumped;
    };
    require(fall(false),"Buffered-jump cancellation fixture would not jump on its impending landing.");
    require(!fall(true),"Canceled buffered jump survived a zero-tick input loss and reacquisition.");
}
void firstPersonCeilingsWallsAndStairs() {
    Scene scene;auto player=characterRecord();player.local=math::translation({0,.05f,0});
    EntityRecord ceiling;ceiling.id=3;ceiling.name="Low ceiling";ceiling.local=math::translation({0,2.3f,0});ceiling.collider=SceneCollider{{10,.2f,10}};
    EntityRecord wall;wall.id=4;wall.name="Wall";wall.local=math::translation({1.5f,2,0});wall.collider=SceneCollider{{1,4,10}};
    scene.apply({floorRecord(),player,ceiling,wall},{});PhysicsWorld world;firstPersonTick(scene,world,{},60);CharacterMotor motor(world,2);
    motor.jump(1.1f);float highest=0;
    for(int i=0;i<90;++i){firstPersonTick(scene,world);highest=std::max(highest,(*motor.position())[1]);}
    require(highest>.2f && highest<.43f && motor.grounded(),"Low ceiling did not stop ascent and promptly restore falling/landing.");
    firstPersonTick(scene,world,{6,0,0},90);
    require((*motor.position())[0]<.72f && std::abs((*motor.velocity())[0])<.01,"FPS wall contact retained commanded momentum or passed through collision.");
    const float againstWall=(*motor.position())[0];firstPersonTick(scene,world,{-6,0,0});
    require((*motor.position())[0]<againstWall-.005f,"FPS acceleration had to unwind stale into-wall velocity before reversing.");

    Scene stairs;EntityRecord step;step.id=4;step.name="Low step";step.local=math::translation({1.5f,.1f,0});step.collider=SceneCollider{{1,.2f,4}};
    stairs.apply({floorRecord(),player,step},{});PhysicsWorld stairWorld;firstPersonTick(stairs,stairWorld,{},60);firstPersonTick(stairs,stairWorld,{3,0,0},60);
    require((*stairWorld.characterPosition(2))[0]>2.4f && stairWorld.grounded(2),"FPS momentum handling broke the shared traversal profile's stair support.");
}
void firstPersonTransientStateResets() {
    using namespace crimson::testing;
    const auto profile=agentProfile({1.8f,.3f,45,.3f});auto player=characterRecord();player.controller->profile=profile;player.local=math::translation({0,.05f,0});
    Scene scene;scene.apply({floorRecord(),player},{});PhysicsWorld world;world.setAgentProfiles(agentProfileQuery());firstPersonTick(scene,world,{},60);CharacterMotor motor(world,2);
    firstPersonTick(scene,world,{6,0,0},16);motor.jump(1.1f);firstPersonTick(scene,world,{6,0,0},3);
    auto records=scene.records();for(auto& record:records)if(record.id==2)record.local=math::translation({4,3,0});scene.apply(records,{});
    firstPersonTick(scene,world);
    require(std::abs((*motor.velocity())[0])<.01 && (*motor.velocity())[1]<0 && (*motor.position())[1]>2.99f,"Authored teleport retained FPS horizontal/jump momentum.");
    firstPersonTick(scene,world,{},120);motor.jump(1.1f);motor.moveFirstPerson({6,0,0},45,8,12,.35f);const std::vector<EntityId> locks{2};
    scene.applyRuntimePoses(world.step(scene,NoEntity,{},1.f/60.f,locks));firstPersonTick(scene,world);
    require(motor.grounded() && std::abs((*motor.velocity())[0])<.01,"Transform lock replayed FPS movement or a queued jump when released.");
    firstPersonTick(scene,world,{6,0,0},16);
    auto replacement=std::make_shared<NavigationAgentProfile>(*agentProfiles().at(profile.id));replacement->traversal.maxSlope=40;agentProfiles()[profile.id]=replacement;
    motor.jump(1.1f);firstPersonTick(scene,world);
    require(motor.grounded() && std::abs((*motor.velocity())[0])<.01,"Profile replacement retained FPS momentum or a buffered jump.");
}
void stairCorrectionPublication() {
    using namespace crimson::testing;
    const auto profile=agentProfile({1.8f,.3f,45,.3f});
    auto player=characterRecord();player.controller->profile=profile;player.local=math::translation({0,.05f,0});
    EntityRecord step;step.id=4;step.name="Camera stair";step.local=math::translation({1.5f,.1f,0});step.collider=SceneCollider{{1,.2f,4}};
    Scene scene;scene.apply({floorRecord(),player,step},{});PhysicsWorld world;world.setAgentProfiles(agentProfileQuery());firstPersonTick(scene,world,{},60);
    require(world.characterStepHeight(2)==0 && world.characterStepHeight(999)==0,"Stationary/unknown character published a stair correction.");
    float climb=0,descent=0;bool climbed=false,descended=false;
    for(int i=0;i<80;++i) {
        firstPersonTick(scene,world,{3,0,0});const auto correction=world.characterStepHeight(2);
        if(correction>0){climb+=correction;climbed=true;}
        else if(correction<0){descent+=correction;descended=true;}
    }
    require(climbed && descended && climb>.1f && climb<.3f && descent<-.1f && descent>-.3f,
        "Supported stairs did not publish distinct actual ascent/descent corrections: up="+std::to_string(climb)+", down="+std::to_string(descent));
    firstPersonTick(scene,world,{},60);require(world.characterStepHeight(2)==0,"A stair correction leaked into a later stationary tick.");
    CharacterMotor motor(world,2);motor.jump(1.1f);
    for(int i=0;i<100;++i){firstPersonTick(scene,world);require(world.characterStepHeight(2)==0,"Jump/fall/landing was misreported as a stair correction.");}

    const auto climbAgain=[&] {
        auto records=scene.records();for(auto& record:records)if(record.id==2)record.local=math::translation({0,.05f,0});scene.apply(records,{});
        world.reconcile(scene);require(world.characterStepHeight(2)==0,"Teleport retained the previous stair correction.");
        firstPersonTick(scene,world,{},60);
        for(int i=0;i<60;++i){firstPersonTick(scene,world,{3,0,0});if(world.characterStepHeight(2)>.01f)return;}
        require(false,"Reset fixture never encountered its stair.");
    };
    climbAgain();world.cancelCharacterInput(2);require(world.characterStepHeight(2)==0,"Canceling gameplay input retained the previous stair correction.");
    climbAgain();const std::vector<EntityId> locks{2};world.reconcile(scene,locks);require(world.characterStepHeight(2)==0,"Transform lock retained the previous stair correction.");
    firstPersonTick(scene,world);
    climbAgain();auto replacement=std::make_shared<NavigationAgentProfile>(*agentProfiles().at(profile.id));replacement->traversal.maxSlope=40;agentProfiles()[profile.id]=replacement;
    world.reconcile(scene);require(world.characterStepHeight(2)==0,"Profile replacement retained the previous stair correction.");
    climbAgain();auto records=scene.records();std::erase_if(records,[](const auto& record){return record.id==2;});scene.apply(records,{});world.reconcile(scene);
    require(world.characterStepHeight(2)==0,"Removed character retained the previous stair correction.");
}
void narrowStairCompleteVerticalPublication() {
    using namespace crimson::testing;
    const auto profile=agentProfile({1.8f,.3f,45,.3f});
    for(bool triangles:{false,true}) {
        auto player=characterRecord();player.controller->profile=profile;player.local=math::translation({0,.05f,0});
        std::vector<EntityRecord> records{floorRecord(),player};
        // Match the authored staircase's .25 m risers and .375 m treads.
        // Capsule rounding continues outside the explicit stair helper, on
        // both native boxes and the original imported triangle geometry.
        for(int i=0;i<8;++i) {
            const float width=i==7?8.f:.375f,left=1.f+.375f*i;
            EntityRecord step;step.id=10+i;step.name="Narrow stair tread";
            step.local=math::translation({left+width*.5f,.25f*i+.125f,0});
            step.collider=SceneCollider{{width,.25f,4}};
            if(triangles) {
                const float half=width*.5f;
                step.collider->shape=ColliderShape::Trimesh;
                step.collider->mesh=makeCollisionMesh({
                    {-half,-.125f,-2},{half,-.125f,-2},{half,-.125f,2},{-half,-.125f,2},
                    {-half,.125f,-2},{half,.125f,-2},{half,.125f,2},{-half,.125f,2}},
                    {0,1,2,0,2,3,4,6,5,4,7,6,0,3,7,0,7,4,1,5,6,1,6,2,0,4,5,0,5,1,3,2,6,3,6,7});
            }
            records.push_back(std::move(step));
        }
        Scene scene;scene.apply(records,{});PhysicsWorld world;world.setAgentProfiles(agentProfileQuery());
        firstPersonTick(scene,world,{},60);
        const auto label=triangles?"Trimesh":"Box";
        for(int direction:{1,-1}) {
            bool rounded=false,stepped=false;double displacement=0,correction=0;
            for(int i=0;i<180;++i) {
                const auto before=*world.characterPosition(2);const bool supported=world.grounded(2);
                firstPersonTick(scene,world,{direction*5.f,0,0});
                const auto after=*world.characterPosition(2);const float height=after[1]-before[1];
                const float published=world.characterStepHeight(2);
                // Jolt can remain supported on a steep rounded edge between
                // its strict OnGround samples, including during stair descent.
                displacement+=height;correction+=published;
                if(std::abs(height)>1e-3f && ((supported && world.grounded(2)) || published!=0))
                    require(std::abs(published-height)<1e-5f,
                        std::string(label)+" stair sweep/settling bypassed the complete vertical correction: height="+
                        std::to_string(height)+", published="+std::to_string(published));
                rounded=rounded || (direction*published>.001f && direction*published<.06f);
                stepped=stepped || direction*published>.1f;
                if((direction>0 && after[0]>5) || (direction<0 && after[0]<0))break;
            }
            require(direction*displacement>1.95 && std::abs(correction-displacement)<.01,
                std::string(label)+" narrow stairs failed to capture their full supported ascent/descent: direction="+
                std::to_string(direction)+", displacement="+std::to_string(displacement)+", correction="+std::to_string(correction));
            require(rounded && stepped,std::string(label)+" narrow stairs did not exercise both capsule rounding and large steps.");
            firstPersonTick(scene,world,{},60);
        }
        // An unsupported drop and its eventual landing are ballistic,
        // unlike supported downward stair snapping.
        auto falling=scene.records();
        for(auto& record:falling)if(record.id==2)record.local=math::translation({-2,3,0});
        scene.apply(falling,{});world.reconcile(scene);
        bool airborne=false,landed=false;
        for(int i=0;i<120;++i) {
            firstPersonTick(scene,world);
            airborne=airborne || !world.grounded(2);landed=landed || (airborne && world.grounded(2));
            require(world.characterStepHeight(2)==0,std::string(label)+" unsupported fall or landing entered stair smoothing.");
        }
        require(airborne && landed,std::string(label)+" stair fall fixture did not reach its landing.");
    }
}
SceneCollider stairCollider(float width,float height,float depth,bool triangles) {
    SceneCollider collider{{width,height,depth}};
    if(triangles) {
        const float x=width*.5f,y=height*.5f,z=depth*.5f;
        collider.shape=ColliderShape::Trimesh;
        collider.mesh=makeCollisionMesh({{-x,-y,-z},{x,-y,-z},{x,-y,z},{-x,-y,z},
            {-x,y,-z},{x,y,-z},{x,y,z},{-x,y,z}},
            {0,1,2,0,2,3,4,6,5,4,7,6,0,3,7,0,7,4,1,5,6,1,6,2,0,4,5,0,5,1,3,2,6,3,6,7});
    }
    return collider;
}
void obliqueStairTraversalAndIndependentFacing() {
    using namespace crimson::testing;
    // A capsule must step while most of its velocity is parallel to a riser.
    // Rotation and +/- approaches prevent an axis- or facing-dependent fix.
    struct Approach {float degrees,yaw;};
    constexpr Approach approaches[]{{0,0},{30,-.45f},{45,0},{-45,0},{60,.61f},{-60,.61f},
        {75,-.39f},{-75,-.39f},{85,.27f},{-85,.27f}};
    for(bool triangles:{false,true})for(const auto approach:approaches) {
        const auto basis=math::eulerRotationXYZ({0,approach.yaw,0});
        const auto inverse=*math::inverse(basis);
        auto player=characterRecord();player.local=math::translation({0,.05f,0});
        player.controller->profile=agentProfile({1.8f,.3f,45,.3f});
        auto floor=floorRecord();floor.collider->size={256,1,256};
        std::vector<EntityRecord> records{floor,player};
        for(int i=0;i<8;++i) {
            const float width=i==7?8.f:.375f;
            EntityRecord tread;tread.id=10+i;tread.name="Oblique stair tread";
            tread.local=basis*math::translation({1.f+.375f*i+width*.5f,.25f*i+.125f,0});
            tread.collider=stairCollider(width,.25f,128,triangles);records.push_back(std::move(tread));
        }
        Scene scene;scene.apply(records,{});PhysicsWorld world;world.setAgentProfiles(agentProfileQuery());
        firstPersonTick(scene,world,{},60);CharacterMotor motor(world,2);
        const float angle=approach.degrees*std::numbers::pi_v<float>/180;
        const Vec3 wish=math::transformDirection(basis,{5*std::cos(angle),0,5*std::sin(angle)});
        const Vec3 facing=math::normalized(Vec3{-wish[2],0,wish[0]});
        const int budget=static_cast<int>(std::ceil((4.5f/(5*std::cos(angle))+1.5f)*60));
        for(int direction:{1,-1}) {
            Vec3 position{};
            for(int i=0;i<budget;++i) {
                motor.face(facing);firstPersonTick(scene,world,wish*direction);
                position=math::transformPoint(inverse,*motor.position());
                if((direction>0 && position[0]>4.5f) || (direction<0 && position[0]<0))break;
            }
            const auto label=std::string(triangles?"Trimesh":"Box")+" at "+std::to_string(approach.degrees)+" degrees";
            require(direction>0?position[0]>4.5f:position[0]<0,
                label+" stalled on stairs while facing perpendicular to movement: x="+std::to_string(position[0]));
            firstPersonTick(scene,world,{},60);
            require(world.grounded(2) && std::abs((*motor.position())[1]-(direction>0?2.f:0.f))<.03f,
                label+" crossed the stair run without settling on its actual support.");
        }
    }
}
void obliqueStairsRespectStepAndCeilingLimits() {
    using namespace crimson::testing;
    for(bool triangles:{false,true})for(int restriction:{0,1,2}) {
        auto player=characterRecord();player.local=math::translation({0,.05f,0});
        player.controller->profile=agentProfile({1.8f,.3f,45,restriction==0?0.f:.3f});
        auto floor=floorRecord();floor.collider->size={40,1,40};
        EntityRecord step;step.id=3;step.name="Restricted diagonal step";
        const float height=restriction==2?.6f:.25f;
        step.local=math::translation({2.5f,height*.5f,0});step.collider=stairCollider(3,height,40,triangles);
        std::vector<EntityRecord> records{floor,player,step};
        if(restriction==1) {
            EntityRecord roof;roof.id=4;roof.name="Stair head clearance";
            roof.local=math::translation({2,2,0});roof.collider=stairCollider(8,.2f,40,triangles);records.push_back(std::move(roof));
        }
        Scene scene;scene.apply(records,{});PhysicsWorld world;world.setAgentProfiles(agentProfileQuery());
        firstPersonTick(scene,world,{},60);
        for(int i=0;i<180;++i) {
            firstPersonTick(scene,world,{3.535534f,0,3.535534f});
            require((*world.characterVelocity(2))[2]<3.555534f,
                "A rejected diagonal stair attempt accelerated motion parallel to the blocker.");
        }
        const auto position=*world.characterPosition(2);
        require(position[0]<.95f && position[1]<.15f,
            std::string(triangles?"Trimesh":"Box")+" diagonal stair traversal bypassed "+
            (restriction==0?"the disabled step profile.":restriction==1?"head clearance.":"a step above the profile limit."));
    }
    for(bool triangles:{false,true}) {
        auto player=characterRecord();player.local=math::translation({.69f,.05f,0});
        player.controller->profile=agentProfile({1.8f,.3f,45,.3f});
        EntityRecord wall;wall.id=3;wall.name="Parallel stair riser";
        wall.local=math::translation({2.5f,.125f,0});wall.collider=stairCollider(3,.25f,40,triangles);
        auto floor=floorRecord();floor.collider->size={40,1,40};
        Scene scene;scene.apply({floor,player,wall},{});PhysicsWorld world;world.setAgentProfiles(agentProfileQuery());
        firstPersonTick(scene,world,{},60);const auto start=*world.characterPosition(2);
        firstPersonTick(scene,world,{0,0,5},120);const auto finish=*world.characterPosition(2);
        require(finish[2]>start[2]+8 && std::abs(finish[0]-start[0])<.01f && std::abs(finish[1]-start[1])<.01f,
            std::string(triangles?"Trimesh":"Box")+" motion parallel to a riser gained height or turned into the stair.");
    }
}
std::shared_ptr<const StaticTriangleMesh> spiralStairGeometry() {
    // Reconstruct Main's authored Spiral Steps independently of mutable project
    // data: 18 risers over 270 degrees, radii 1.4/3.7 m, 32-sided arc samples.
    std::vector<Vec3> vertices;std::vector<std::uint32_t> indices;
    const auto rounded=[](double value){return static_cast<float>(std::round(value*10000)/10000);};
    const auto point=[&](double radius,double angle,float height){return Vec3{rounded(radius*std::cos(angle)),height,rounded(radius*std::sin(angle))};};
    const auto quad=[&](const Vec3& a,const Vec3& b,const Vec3& c,const Vec3& d) {
        const auto base=static_cast<std::uint32_t>(vertices.size());
        vertices.insert(vertices.end(),{a,b,c,d});indices.insert(indices.end(),{base,base+1,base+2,base,base+2,base+3});
    };
    const double pi=std::numbers::pi;
    for(int step=0;step<18;++step) {
        const double start=-pi/2-step*pi/12,end=start-pi/12;
        std::vector<double> angles{start};
        for(int sample=9;sample<=32;++sample) {
            const double angle=-sample*pi/16;
            if(angle<start-1e-8 && angle>end+1e-8)angles.push_back(angle);
        }
        angles.push_back(end);const float top=rounded(-.5+(step+1)*3.5/18);
        for(std::size_t segment=1;segment<angles.size();++segment) {
            const auto a=angles[segment-1],b=angles[segment];
            quad(point(1.4,a,top),point(3.7,a,top),point(3.7,b,top),point(1.4,b,top));
            quad(point(1.4,b,-.5f),point(3.7,b,-.5f),point(3.7,a,-.5f),point(1.4,a,-.5f));
            quad(point(3.7,a,-.5f),point(3.7,b,-.5f),point(3.7,b,top),point(3.7,a,top));
            quad(point(1.4,b,-.5f),point(1.4,a,-.5f),point(1.4,a,top),point(1.4,b,top));
        }
        quad(point(1.4,start,-.5f),point(3.7,start,-.5f),point(3.7,start,top),point(1.4,start,top));
        quad(point(3.7,end,-.5f),point(1.4,end,-.5f),point(1.4,end,top),point(3.7,end,top));
    }
    return makeCollisionMesh(std::move(vertices),std::move(indices));
}
void spiralStairObliqueEntry() {
    using namespace crimson::testing;
    const double start=-97.5*std::numbers::pi/180;
    const Vec3 spawn{3.1*std::cos(start),.25f,3.1*std::sin(start)};
    auto player=characterRecord();player.local=math::translation(spawn);
    player.controller->profile=agentProfile({1.8f,.3f,45,.3f});
    EntityRecord stairs;stairs.id=3;stairs.name="Spiral Steps regression";
    stairs.local=math::translation({0,.5f,0});stairs.collider.emplace();
    stairs.collider->shape=ColliderShape::Trimesh;stairs.collider->mesh=spiralStairGeometry();
    Scene scene;scene.apply({floorRecord(),player,stairs},{});PhysicsWorld world;world.setAgentProfiles(agentProfileQuery());
    firstPersonTick(scene,world,{},60);CharacterMotor motor(world,2);
    // Walk inward at 45 degrees to the ascending tangent. The old direction
    // test slid along the first riser and out of the stair's inner edge.
    const Vec3 wish=math::normalized(Vec3{std::sin(start)-std::cos(start),0,-std::cos(start)-std::sin(start)})*5;
    float highest=0;
    for(int i=0;i<100;++i) {
        motor.face(math::normalized(Vec3{-wish[2],0,wish[0]}));firstPersonTick(scene,world,wish);
        const auto position=*motor.position();highest=std::max(highest,position[1]);
        if(highest>.55f || std::hypot(position[0],position[2])<1.7f)break;
    }
    require(highest>.55f,"Oblique spiral entry slid along its first riser instead of ascending: height="+std::to_string(highest));
}
void spiralStairCurvedTraversal() {
    using namespace crimson::testing;
    const auto geometry=spiralStairGeometry();const double pi=std::numbers::pi;
    for(int direction:{1,-1}) {
        const double start=(-97.5-(direction<0?255:0))*pi/180;
        auto player=characterRecord();player.controller->profile=agentProfile({1.8f,.3f,45,.3f});
        player.local=math::translation({2.5*std::cos(start),direction>0?.25f:3.55f,2.5*std::sin(start)});
        EntityRecord stairs;stairs.id=3;stairs.name="Curved spiral traversal";
        stairs.local=math::translation({0,.5f,0});stairs.collider.emplace();
        stairs.collider->shape=ColliderShape::Trimesh;stairs.collider->mesh=geometry;
        Scene scene;scene.apply({floorRecord(),player,stairs},{});PhysicsWorld world;world.setAgentProfiles(agentProfileQuery());
        firstPersonTick(scene,world,{},60);CharacterMotor motor(world,2);
        double previous=start,progress=0;
        // Begin braking inside the final tread, before momentum reaches its
        // open outer edge; the route still visits all eighteen support levels.
        const double goal=250*pi/180;
        for(int i=0;i<360 && progress<goal;++i) {
            auto position=*motor.position();const double angle=std::atan2(position[2],position[0]);
            const double target=angle-direction*.1;
            const Vec3 wish=math::normalized(Vec3{2.5*std::cos(target)-position[0],0,2.5*std::sin(target)-position[2]})*5;
            motor.face(math::normalized(Vec3{-wish[2],0,wish[0]}));firstPersonTick(scene,world,wish);
            position=*motor.position();const double current=std::atan2(position[2],position[0]);
            progress-=direction*std::remainder(current-previous,2*pi);previous=current;
            require(std::abs(std::hypot(position[0],position[2])-2.5f)<.1f,
                "Curving stair traversal was redirected away from its requested path.");
        }
        require(progress>=goal,"Curving stair traversal stalled before completing the 18-tread spiral.");
        firstPersonTick(scene,world,{},60);
        require(world.grounded(2) && std::abs((*motor.position())[1]-(direction>0?3.5f:.1944f))<.04f,
            "Curving stair traversal lost the spiral's final support height: direction="+std::to_string(direction)+
            ", height="+std::to_string((*motor.position())[1])+", grounded="+std::to_string(world.grounded(2)));
    }
}
void rampMotionIsNotAStairCorrection() {
    for(bool triangles:{false,true}) {
        auto player=characterRecord();player.local=math::translation({-1,.05f,0});
        EntityRecord ramp;ramp.id=6;ramp.name="Continuous ramp";ramp.collider=SceneCollider{{4,.2f,4}};
        const double angle=25*std::numbers::pi/180;
        ramp.local=math::translation({1,static_cast<float>(2*std::sin(angle)),0})*math::eulerRotationXYZ({0,0,angle});
        if(triangles) {
            ramp.collider->shape=ColliderShape::Trimesh;
            ramp.collider->mesh=makeCollisionMesh({{-2,.1f,-2},{2,.1f,-2},{2,.1f,2},{-2,.1f,2}},{0,2,1,0,3,2});
        }
        Scene scene;scene.apply({floorRecord(),player,ramp},{});PhysicsWorld world;firstPersonTick(scene,world,{},60);
        for(int direction:{1,-1}) {
            int samples=0;
            for(int i=0;i<210;++i) {
                firstPersonTick(scene,world,{static_cast<float>(direction),0,0});const auto p=*world.characterPosition(2);
                if(p[0]>.2f && p[0]<2.3f && world.grounded(2)) {
                    ++samples;require(std::abs(world.characterStepHeight(2))<1e-5f,"Continuous ramp movement was misreported as a stair correction.");
                }
            }
            require(samples>30,"Ramp signal fixture did not traverse the interior while supported.");
        }
    }
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
    auto records=scene.records();for(auto& record:records)if(record.id==2){record.motor.emplace();}scene.apply(records,{});
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
    auto records=scene.records();for(auto& record:records)if(record.id==2)record.motor->enabled=false;
    scene.apply(records,{});world.reconcile(scene);require(world.geometryRevision()==revision,"Unrelated scene edit invalidated static geometry.");
    records=scene.records();for(auto& record:records)if(record.id==1)record.local=math::translation({0,-2,0});
    scene.apply(records,{});world.reconcile(scene);require(world.geometryRevision()==revision+1,"Live geometry edit did not publish a new revision.");
    records=scene.records();for(auto& record:records)if(record.id==1)record.local[4]=.5f;
    scene.apply(records,{});world.reconcile(scene);
    require(world.staticBoxes().empty() && world.geometryRevision()==revision+2,"Rejected shear remained in derived geometry.");
}
void sharedProfilePublicationAndStaticPreview() {
    using namespace crimson::testing;
    const auto selected=agentProfile({1.2f,.25f,20,0});auto player=characterRecord();player.controller->profile=selected;
    Scene scene;scene.apply({floorRecord(),player},{});PhysicsWorld world;world.setAgentProfiles(agentProfileQuery());tick(scene,world,{},120);
    require(world.acceptedCharacterTraversal(2)==AgentTraversalSettings{1.2f,.25f,20,0},"Controller did not resolve its shared traversal definition.");
    const auto geometry=world.geometryRevision();const auto initial=*world.characterPosition(2);
    auto replacement=std::make_shared<NavigationAgentProfile>(*agentProfiles().at(selected.id));replacement->traversal={2.4f,.6f,35,.15f};agentProfiles()[selected.id]=replacement;world.reconcile(scene);
    require(world.acceptedCharacterTraversal(2)==replacement->traversal&&world.geometryRevision()==geometry,"Live profile dimensions/slope/step were not coherent or dirtied static geometry.");
    require(math::length(*world.characterPosition(2)-initial)<1e-4,"Live capsule replacement teleported the actor's foot origin.");
    agentProfiles().erase(selected.id);world.reconcile(scene);
    require(world.characterCount()==1&&world.characterPosition(2)&&!world.acceptedCharacterTraversal(2),"Missing custom profile discarded the last accepted capsule or reported it current.");
    tick(scene,world,{4,0,0},60);require(std::abs((*world.characterPosition(2))[0]-initial[0])<.01&&world.grounded(2),"Unresolved profile allowed horizontal motion or lost retained collision.");
    agentProfiles()[selected.id]=replacement;world.reconcile(scene);require(world.acceptedCharacterTraversal(2)==replacement->traversal,"Restoring the profile did not recover its controller.");
    PhysicsWorld preview;preview.setAgentProfiles(agentProfileQuery());preview.reconcileStatics(scene);
    require(preview.colliderCount()==1&&preview.characterCount()==0&&!preview.characterPosition(2),"Static navigation preview created a character motor.");
}
void profileStepAndSlopeLimits() {
    using namespace crimson::testing;
    const auto traverseStep=[](float maximumStep) {
        auto player=characterRecord();player.local=math::translation({0,.05f,0});player.controller->profile=agentProfile({1.8f,.3f,45,maximumStep});
        EntityRecord step;step.id=4;step.name="Step";step.collider=SceneCollider{{1,.2f,4}};step.local=math::translation({1.5f,.1f,0});
        Scene scene;scene.apply({floorRecord(),player,step},{});PhysicsWorld world;world.setAgentProfiles(agentProfileQuery());tick(scene,world,{},60);tick(scene,world,{1,0,0},150);return *world.characterPosition(2);
    };
    require(traverseStep(.3f)[0]>2,"Shared profile step did not enable authored stairs.");
    require(traverseStep(0)[0]<.9f,"Zero step profile retained the old hardcoded stair height.");
    const auto traverseRamp=[](float maximumSlope) {
        auto player=characterRecord();player.local=math::translation({-1,.1f,0});player.controller->profile=agentProfile({1.8f,.3f,maximumSlope,.3f});
        EntityRecord ramp;ramp.id=6;ramp.name="Ramp";ramp.collider=SceneCollider{{4,.2f,4}};constexpr float angle=25*std::numbers::pi_v<float>/180;
        ramp.local=math::translation({1,2*std::sin(angle),0})*math::eulerRotationXYZ({0,0,angle});
        Scene scene;scene.apply({floorRecord(),player,ramp},{});PhysicsWorld world;world.setAgentProfiles(agentProfileQuery());tick(scene,world,{},60);float highest=0;
        for(unsigned tickIndex=0;tickIndex<360;++tickIndex){tick(scene,world,{1,0,0});if(world.grounded(2))highest=std::max(highest,(*world.characterPosition(2))[1]);}
        return highest;
    };
    require(traverseRamp(35)>1.5f&&traverseRamp(15)<.8f,"Shared profile slope was not applied to Jolt's walkability limit.");
}
EntityRecord idleRamp(float degrees,float yaw,bool triangles) {
    EntityRecord ramp;ramp.id=1;ramp.name=triangles?"Triangle idle ramp":"Box idle ramp";
    ramp.local=math::eulerRotationXYZ({0,yaw,0})*math::eulerRotationXYZ({0,0,degrees*std::numbers::pi_v<float>/180});
    ramp.collider=SceneCollider{{20,.2f,12}};
    if(triangles) {
        ramp.collider->shape=ColliderShape::Trimesh;
        ramp.collider->mesh=makeCollisionMesh({{-10,.1f,-6},{10,.1f,-6},{10,.1f,6},{-10,.1f,6}},{0,2,1,0,3,2});
    }
    return ramp;
}
Vec3 rampSpawn(const EntityRecord& ramp,float x=0,float z=0) {
    return math::transformPoint(ramp.local,{x,.1f,z})+Vec3{0,2,0};
}
Vec3 horizontalRampDirection(const EntityRecord& ramp,Vec3 direction) {
    auto world=math::transformDirection(ramp.local,direction);world[1]=0;return math::normalized(world);
}
void idleWalkableSlopeSupport() {
    struct Case {float slope,yaw;bool triangles;int motor;};
    const std::array cases{Case{14,0,false,1},Case{-14,.7f,false,0},Case{35,-.9f,false,-1},Case{-35,.8f,true,1}};
    for(const auto& test:cases) {
        const auto ramp=idleRamp(test.slope,test.yaw,test.triangles);auto actor=characterRecord();actor.local=math::translation(rampSpawn(ramp));
        if(test.motor==0)actor.motor.reset();else if(test.motor<0)actor.motor->enabled=false;
        const auto attempted=test.motor==1?Vec3{}:horizontalRampDirection(ramp,{1,0,0})*3;
        Scene scene;scene.apply({ramp,actor},{});PhysicsWorld world;tick(scene,world,attempted,120);
        const auto landed=*world.characterPosition(2);const auto label=std::to_string(test.slope)+" degree "+(test.triangles?"Trimesh":"Box");
        require(world.grounded(2)&&landed[1]<actor.local[13]-1,"Idle gravity did not land the controller on the "+label+" ramp.");
        tick(scene,world,attempted,240);
        const auto drift=math::length(*world.characterPosition(2)-landed);
        require(world.grounded(2)&&drift<.01,"Idle controller drifted on the walkable "+label+" ramp: "+std::to_string(drift)+" m.");
        if(test.motor<0) {
            auto records=scene.records();std::erase_if(records,[](const auto& record){return record.id==1;});scene.apply(records,{});
            tick(scene,world,attempted,30);
            require((*world.characterPosition(2))[1]<landed[1]-.8f,"Idle slope support suspended a disabled Motor after its ramp was removed.");
        }
    }
    for(const auto degrees:{14.f,35.f}) {
        const auto ramp=idleRamp(degrees,.6f,degrees>20);auto actor=characterRecord();actor.local=math::translation(rampSpawn(ramp,-2));
        Scene scene;scene.apply({ramp,actor},{});PhysicsWorld world;tick(scene,world,{},120);
        const auto start=*world.characterPosition(2);tick(scene,world,horizontalRampDirection(ramp,{1,0,0})*1.5,90);
        const auto stopped=*world.characterPosition(2);
        require(world.grounded(2)&&stopped[1]>start[1]+.2,"Commanded ascent was blocked by idle slope support.");
        tick(scene,world,{},240);
        require(world.grounded(2)&&math::length(*world.characterPosition(2)-stopped)<.01,"Stopping after ascent allowed the controller to drift down a walkable ramp.");
    }
}
void slopeIntentIsPerActorAndSteepSlopesStillSlide() {
    const auto ramp=idleRamp(14,-.7f,false);auto idle=characterRecord();idle.local=math::translation(rampSpawn(ramp,0,3));
    auto moving=characterRecord();moving.id=4;moving.name="Moving ramp actor";moving.local=math::translation(rampSpawn(ramp,3,-3));
    Scene scene;scene.apply({ramp,idle,moving},{});PhysicsWorld world;tick(scene,world,{},120);
    const auto idleStart=*world.characterPosition(2),movingStart=*world.characterPosition(4);const auto across=horizontalRampDirection(ramp,{0,0,1});
    CharacterMotor motor(world,4);
    for(int index=0;index<180;++index){motor.move(across);tick(scene,world);}
    require(world.grounded(2)&&math::length(*world.characterPosition(2)-idleStart)<.01,"Moving another actor enabled downhill drift for an idle controller.");
    require(world.grounded(4)&&math::dot(*world.characterPosition(4)-movingStart,across)>2.8,"Per-actor idle support blocked a different actor's commanded movement.");

    using namespace crimson::testing;
    const auto steep=idleRamp(35,.4f,true);auto actor=characterRecord();actor.local=math::translation(rampSpawn(steep));actor.controller->profile=agentProfile({1.8f,.3f,20,.3f});
    Scene steepScene;steepScene.apply({steep,actor},{});PhysicsWorld steepWorld;steepWorld.setAgentProfiles(agentProfileQuery());tick(steepScene,steepWorld,{},60);
    const auto start=*steepWorld.characterPosition(2);tick(steepScene,steepWorld,{},90);
    const auto downhill=horizontalRampDirection(steep,{-1,0,0}),finish=*steepWorld.characterPosition(2);
    require(math::dot(finish-start,downhill)>.25&&finish[1]<start[1]-.15,"Idle support made a slope above the profile limit walkable or suspended gravity.");
}
void firstPersonWalkableSlopes() {
    for(const auto degrees:{14.f,35.f}) {
        const auto ramp=idleRamp(degrees,.6f,degrees>20);auto actor=characterRecord();actor.local=math::translation(rampSpawn(ramp,-2));
        Scene scene;scene.apply({ramp,actor},{});PhysicsWorld world;firstPersonTick(scene,world,{},120);
        const auto start=*world.characterPosition(2);firstPersonTick(scene,world,horizontalRampDirection(ramp,{1,0,0})*1.5,90);
        require(world.grounded(2) && (*world.characterPosition(2))[1]>start[1]+.2,"FPS movement could not ascend a walkable shared-profile ramp.");
        firstPersonTick(scene,world,{},90);const auto stopped=*world.characterPosition(2);firstPersonTick(scene,world,{},90);
        require(world.grounded(2) && math::length(*world.characterPosition(2)-stopped)<.01,"FPS friction did not leave an idle actor at rest on a walkable ramp.");
    }
}
}
int main(){try{floorWallAndLiveEdits();hierarchyAndDiagnostics();teleportLocksAndSteps();scriptedActorsAncestorLocksAndMovedFloor();slopeLimit();staticWorldRaycasts();independentFacing();controllerAndMotorComposition();geometryPublication();sharedProfilePublicationAndStaticPreview();profileStepAndSlopeLimits();idleWalkableSlopeSupport();slopeIntentIsPerActorAndSteepSlopesStillSlide();firstPersonAccelerationBrakingAndAirControl();firstPersonJumpEdgesHeightAndBuffer();firstPersonCoyoteWindow();firstPersonCanceledJumpBuffer();firstPersonCeilingsWallsAndStairs();firstPersonTransientStateResets();stairCorrectionPublication();narrowStairCompleteVerticalPublication();obliqueStairTraversalAndIndependentFacing();obliqueStairsRespectStepAndCeilingLimits();spiralStairObliqueEntry();spiralStairCurvedTraversal();rampMotionIsNotAStairCorrection();firstPersonWalkableSlopes();std::cout<<"Physics checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
