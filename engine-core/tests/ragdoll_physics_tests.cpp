#include "physics/PhysicsWorld.h"
#include "assets/RagdollAsset.h"
#include "assets/ModelAnimation.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool near(float a,float b,float tolerance=.003f){return std::abs(a-b)<tolerance;}
void same(const Mat4& a,const Mat4& b,const char* message,float tolerance=.003f){for(std::size_t i=0;i<16;++i)require(near(a[i],b[i],tolerance),message);}
std::shared_ptr<ModelAsset> model(bool wrapper=false) {
    auto source=std::make_shared<ModelAsset>();source->id="124e84e1-4d42-4f80-979e-72aece60fef2";source->name=wrapper?"Offset creature":"Test creature";
    source->nodes.resize(7);source->roots={0};
    const std::array<int,7> parents{-1,0,1,2,3,4,0};
    const std::array<Vec3,7> offsets{{{0,0,0},{0,1,0},{0,.1f,0},{0,.4f,0},{0,.4f,0},{.12f,0,0},{0,0,0}}};
    for(std::size_t i=0;i<source->nodes.size();++i) {
        auto& node=source->nodes[i];node.name="Creature bone "+std::to_string(i);node.parent=parents[i];node.matrixAuthored=true;node.local=math::translation(offsets[i]);
        if(parents[i]>=0)source->nodes[parents[i]].children.push_back(static_cast<std::uint32_t>(i));
    }
    if(wrapper)source->nodes[0].local=math::translation({.3f,.2f,-.1f})*math::scaling({.5f,.5f,.5f});
    const auto reference=evaluateModelPose(*source);ModelSkin skin;skin.name="Creature Skin";skin.joints={1,3,4};skin.skeleton=1;
    for(const auto node:skin.joints)skin.inverseBindMatrices.push_back(*math::inverse(reference.worldTransforms[node]));source->skins.push_back(std::move(skin));
    ModelPrimitive primitive;primitive.vertices.resize(3);primitive.vertices[0].position={0,1,0};primitive.vertices[1].position={.1f,1,0};primitive.vertices[2].position={0,1.1f,0};primitive.indices={0,1,2};
    primitive.hasSkinning=true;primitive.skinWeights.resize(3);primitive.skinJointBounds.resize(3);
    for(std::size_t i=0;i<3;++i){primitive.skinWeights[i].weights[0]=1;math::include(primitive.bounds,primitive.vertices[i].position);math::include(primitive.skinJointBounds[0],primitive.vertices[i].position);}
    source->primitives.push_back(std::move(primitive));ModelMesh mesh;mesh.name="Skinned body";mesh.primitives={0};source->meshes.push_back(std::move(mesh));
    source->nodes[6].mesh=0;source->nodes[6].skin=0;source->draws.push_back({0,Mat4{},6});
    return source;
}
std::shared_ptr<RagdollAsset> setup(const ModelAsset& source,bool single=false) {
    auto asset=std::make_shared<RagdollAsset>(makeRagdollAsset(source));generateRagdollBodies(*asset,source,single?std::vector<std::uint32_t>{1}:std::vector<std::uint32_t>{});
    for(auto& body:asset->bodies){body.shape=RagdollShape::Sphere;body.radius=.1f;body.position={0,.1f,0};body.mass=2;body.joint.swingY=70;body.joint.swingZ=70;body.joint.twistMin=-60;body.joint.twistMax=60;}
    return asset;
}
void floorScene(Scene& scene) {
    EntityRecord floor;floor.id=100;floor.name="Floor";floor.local=math::translation({0,-.5f,0});floor.collider.emplace();floor.collider->size={100,1,100};
    scene.apply({floor},{});
}
void tick(PhysicsWorld& world,const Scene& scene,int ticks=1){for(int i=0;i<ticks;++i)world.step(scene,NoEntity,{},1.f/60);}
void poseBindingAndHandoff() {
    const auto source=model(true);const auto asset=setup(*source);auto initial=sample_model_local_pose(*source);initial.nodes[1].matrix=initial.nodes[1].matrix*math::rotation(math::axisAngle({0,0,1},.3f));
    initial.nodes[3].matrix=initial.nodes[3].matrix*math::rotation(math::axisAngle({0,1,0},.2f));const auto pose=finalize_model_pose(*source,initial);
    const auto transform=math::translation({4,3,-2})*math::rotation(math::axisAngle({0,1,0},.6f))*math::scaling({1.5f,1.5f,1.5f});
    PhysicsWorld world;require(world.createRagdoll(1,asset,source,pose,transform,{2,0,0}),"Could not create offset rig.");
    const auto restored=world.sampleRagdollPose(1);require(restored && restored->worldTransforms.size()==source->nodes.size(),"Ragdoll did not publish the full model pose.");
    for(std::size_t i=0;i<source->nodes.size();++i)same(restored->worldTransforms[i],pose.worldTransforms[i],"Physics handoff changed bone/wrapper/helper transforms.");
    require(restored->skinPalettes[6].size()==3 && restored->bounds.valid,"Ragdoll finalization lost skin palettes or deformed bounds.");
    for(std::size_t joint=0;joint<3;++joint)same(restored->skinPalettes[6][joint],pose.skinPalettes[6][joint],"Ragdoll handoff changed a skin palette.");
    require(world.ragdollBodyTransforms(1).size()==asset->bodies.size(),"Physical frames do not parallel body definitions.");
    Scene empty;tick(world,empty);const auto moved=world.sampleRagdollPose(1);require(moved->worldTransforms[1][12]>restored->worldTransforms[1][12],"Initial actor velocity was lost.");
    const auto previous=world.sampleRagdollPose(1,0),current=world.sampleRagdollPose(1,1),middle=world.sampleRagdollPose(1,.5f);
    require(near(middle->worldTransforms[1][13],(previous->worldTransforms[1][13]+current->worldTransforms[1][13])*.5f,.01f),"Ragdoll interpolation did not remain between fixed poses.");
    const auto bad=math::scaling({1,2,1});require(!world.createRagdoll(2,asset,source,pose,bad),"Nonuniform instance scale was accepted.");
    require(world.hasRagdoll(1)&&world.ragdollCount()==1,"A failed creation damaged an independent ragdoll.");
}
void dropSleepQueriesAndIndependence() {
    Scene scene;floorScene(scene);const auto source=model();const auto asset=setup(*source,true);const auto pose=evaluateModelPose(*source);PhysicsWorld world;world.reconcileStatics(scene);
    require(world.createRagdoll(1,asset,source,pose,math::translation({-2,2,0})),"Could not create first rigid rig.");
    require(world.createRagdoll(2,asset,source,pose,math::translation({2,4,0})),"Could not create second independent rig.");
    tick(world,scene,30);const auto first=world.ragdollBodyTransforms(1).front(),second=world.ragdollBodyTransforms(2).front();
    require(first[13]<2.5f && second[13]>first[13]+1.5f,"Ragdolls did not fall independently.");
    tick(world,scene,450);const auto settled=world.ragdollBodyTransforms(1).front();require(settled[13]>-.06f && settled[13]<.06f,"Ragdoll penetrated the floor or failed to settle.");
    require(!world.ragdollActive(1),"Settled ragdoll failed to sleep.");
    const auto pick=world.pickRagdoll({{-2,2,0},{0,-1,0}},4);require(pick && pick->entity==1 && pick->bodyIndex==0,"Native ray pick did not return the exact ragdoll body.");
    PhysicsTrace trace;trace.ray={{-2,2,0},{0,-1,0}};trace.maxDistance=4;PhysicsQueryFilter filter;filter.mask=EnemyCollisionLayer;
    const auto hit=world.trace(trace,filter);require(hit.hits.size()==1 && hit.hits[0].entity==1,"Normal physics trace omitted ragdoll bodies or target layers.");
    const std::array<EntityId,1> ignored{1};filter.ignored=ignored;require(world.trace(trace,filter).hits.empty(),"Ignoring a ragdoll entity did not omit its bones.");
    require(world.applyRagdollImpulse(1,{0,4,0}),"Ragdoll impulse was rejected.");tick(world,scene,15);require(world.ragdollActive(1)&&world.ragdollBodyTransforms(1).front()[13]>settled[13]+.1f,"Impulse did not wake or move a sleeping ragdoll.");
    require(!world.ragdollActive(2),"Waking one ragdoll woke an unrelated sleeping rig.");world.removeRagdoll(1);require(!world.hasRagdoll(1)&&world.hasRagdoll(2),"Removing one rig destroyed another.");
}
void actorCollisionOwnershipAndTriggers() {
    Scene scene;floorScene(scene);auto records=scene.records();EntityRecord actor;actor.id=10;actor.name="Enemy";actor.actor.emplace();actor.controller.emplace();actor.local=math::translation({0,2,0});
    EntityRecord visual;visual.id=11;visual.name="Enemy visual";visual.parent=10;records.push_back(actor);records.push_back(visual);
    EntityRecord sensor;sensor.id=200;sensor.name="Trigger";sensor.local=math::translation({0,2.7f,0});sensor.collider.emplace();sensor.collider->size={3,3,3};sensor.collider->mode=ColliderMode::Trigger;sensor.collider->collision={TriggerCollisionLayer,EnemyCollisionLayer};records.push_back(sensor);scene.apply(records,{});
    PhysicsWorld world;world.reconcile(scene);require(world.characterCount()==1,"Controller fixture did not create a motor.");const auto source=model();const auto asset=setup(*source);
    require(world.createRagdoll(11,asset,source,evaluateModelPose(*source),*scene.worldTransform(11),{},{},10),"Could not activate an owned ragdoll.");
    tick(world,scene);require(world.characterCount()==0,"Reconcile recreated the ragdolled actor's capsule.");
    const auto overlaps=world.triggerOverlaps(200);require(overlaps.size()==1&&overlaps.front()==11,"Trigger sampling did not deduplicate the physical bones.");
    PhysicsTrace trace;trace.ray={{0,3,2},{0,0,-1}};trace.maxDistance=4;PhysicsQueryFilter filter;filter.mask=EnemyCollisionLayer;
    auto hits=world.trace(trace,filter,PhysicsTraceMode::All);require(hits.hits.size()==1&&hits.hits[0].owner==10,"Ragdoll trace lost actor ownership.");
    const std::array<EntityId,1> ignored{10};filter.ignored=ignored;require(world.trace(trace,filter).hits.empty(),"Ignoring an actor did not exclude its ragdoll.");
    world.removeRagdoll(11);world.reconcile(scene);require(world.characterCount()==1,"Removing the ragdoll did not restore the authored motor on reconcile.");
}
void physicsDragReleaseAndReset() {
    Scene scene;floorScene(scene);const auto source=model();const auto asset=setup(*source,true);PhysicsWorld world;world.reconcile(scene);
    require(world.createRagdoll(1,asset,source,evaluateModelPose(*source),math::translation({0,1,0})),"Could not create draggable rig.");
    const auto start=world.ragdollBodyTransforms(1).front();const Vec3 grabbed{start[12],start[13]+.1f,start[14]};
    require(world.beginRagdollDrag(1,0,grabbed)&&world.updateRagdollDrag(grabbed+Vec3{2,1,0}),"Could not create/update native drag constraint.");
    tick(world,scene,90);const auto pulled=world.ragdollBodyTransforms(1).front();require(pulled[12]>1.5f&&pulled[13]>start[13]+.5f,"Physics drag did not pull the body to the requested anchor.");
    world.endRagdollDrag();require(!world.updateRagdollDrag(grabbed),"Released drag retained an active constraint.");tick(world,scene,90);const float releasedY=world.ragdollBodyTransforms(1).front()[13];if(!(releasedY<pulled[13]-.5f))throw std::runtime_error("Released body did not resume gravity: pulledY="+std::to_string(pulled[13])+", releasedY="+std::to_string(releasedY));
    require(world.beginRagdollDrag(1,0,{pulled[12],.1f,0}),"Could not restart drag.");world.removeRagdoll(1);require(!world.updateRagdollDrag(grabbed),"Removing the rig left a stale drag constraint.");
    require(!world.beginRagdollDrag(1,0,grabbed),"Missing body accepted a drag.");
}
void stationaryControllerBlocksDynamicBones() {
    Scene scene;floorScene(scene);auto records=scene.records();EntityRecord character;character.id=20;character.name="Standing character";character.controller.emplace();records.push_back(character);
    character.id=21;character.local=math::translation({4,0,0});character.controller->collision.mask=WorldCollisionLayer;records.push_back(character);scene.apply(records,{});
    PhysicsWorld world;world.reconcile(scene);const auto source=model();const auto asset=setup(*source,true);
    require(world.createRagdoll(1,asset,source,evaluateModelPose(*source),math::translation({0,3,0})),"Could not create rig over stationary character.");
    require(world.createRagdoll(2,asset,source,evaluateModelPose(*source),math::translation({4,3,0}),{},{},NoEntity,{EnemyCollisionLayer,WorldCollisionLayer}),"Could not create filtered rig over character.");
    tick(world,scene,45);require(world.ragdollBodyTransforms(1).front()[13]>1.65f,"Dynamic ragdoll fell through a stationary Character Controller.");
    require(world.ragdollBodyTransforms(2).front()[13]<1.65f,"Dynamic ragdoll ignored character collision masks.");
    // The inner proxy must never appear as a duplicate world collider in traces.
    PhysicsTrace trace;trace.ray={{0,.9f,2},{0,0,-1}};trace.maxDistance=4;PhysicsQueryFilter filter;filter.includeCharacters=false;filter.mask=CharacterCollisionLayer;
    require(world.trace(trace,filter).hits.empty(),"Character inner proxy escaped the virtual-character query ownership.");
}
void dynamicPairMasks() {
    Scene scene;const auto source=model();const auto asset=setup(*source,true);asset->gravityFactor=0;PhysicsWorld world;
    const auto pose=evaluateModelPose(*source);
    require(world.createRagdoll(1,asset,source,pose,math::translation({-1,0,0}),{1,0,0},{},NoEntity,{EnemyCollisionLayer,WorldCollisionLayer}),"Could not create filtered moving body.");
    require(world.createRagdoll(2,asset,source,pose,math::translation({1,0,0}),{-1,0,0}),"Could not create opposing moving body.");
    tick(world,scene,90);require(world.ragdollBodyTransforms(1).front()[12]>.3f&&world.ragdollBodyTransforms(2).front()[12]<-.3f,"Dynamic-body collision did not respect both target membership masks.");
}
void asymmetricJointAxes() {
    const auto source=model();auto asset=std::make_shared<RagdollAsset>(makeRagdollAsset(*source));generateRagdollBodies(*asset,*source,{1,3});asset->gravityFactor=0;
    for(auto& body:asset->bodies){body.shape=RagdollShape::Sphere;body.radius=.08f;body.position={};body.joint.rotation={};body.joint.swingY=0;body.joint.swingZ=90;body.joint.twistMin=body.joint.twistMax=0;}
    auto local=sample_model_local_pose(*source);local.nodes[3].matrix=local.nodes[3].matrix*math::rotation(math::axisAngle({0,1,0},.5f));
    PhysicsWorld world;require(world.createRagdoll(1,asset,source,finalize_model_pose(*source,local),Mat4{}),"Could not create asymmetric joint fixture.");Scene scene;tick(world,scene,120);
    const auto frames=world.ragdollBodyTransforms(1);const auto relative=*math::inverse(frames[0])*frames[1];
    require(relative[0]>.995f&&std::abs(relative[2])<.08f,"Authored Swing Y limit was applied to the wrong joint axis.");
}
void transientPinsFreezeAndResume() {
    Scene scene;const auto source=model();const auto asset=setup(*source,true);const auto pose=evaluateModelPose(*source);PhysicsWorld world;
    require(world.createRagdoll(1,asset,source,pose,math::translation({0,2,0}),{1,0,0}),"Could not create moving pin fixture.");tick(world,scene,15);
    const auto moving=world.ragdollBodyTransforms(1).front();require(world.setRagdollBodyPinned(1,0,true)&&world.ragdollBodyPinned(1,0),"Body pin did not become active.");
    same(world.ragdollBodyTransforms(1,0).front(),moving,"Pin retained an interpolated pose instead of freezing the actual body.");
    require(world.setRagdollBodyPinned(1,0,true),"Repeated pin changed its result.");tick(world,scene,120);
    same(world.ragdollBodyTransforms(1).front(),moving,"Pinned body moved under gravity.",.0005f);
    const auto pick=world.pickRagdoll({{moving[12],moving[13]+2,moving[14]},{0,-1,0}},4);require(pick&&pick->entity==1&&pick->bodyIndex==0,"Kinematic pinned body disappeared from picking.");
    require(!world.beginRagdollDrag(1,0,{moving[12],moving[13],moving[14]}),"Pinned body accepted a mouse drag.");
    require(!world.applyRagdollImpulse(1,{0,5,0},{},0)&&!world.applyRagdollImpulse(1,{0,5,0}),"Pinned body accepted an impulse.");
    require(world.setRagdollBodyPinned(1,0,false)&&!world.ragdollBodyPinned(1,0),"Body unpin failed.");tick(world,scene,30);
    const auto released=world.ragdollBodyTransforms(1).front();require(released[13]<moving[13]-.8f&&near(released[12],moving[12]),"Unpin did not resume gravity with cleared prior velocity.");
    require(!world.setRagdollBodyPinned(1,9,true)&&!world.ragdollBodyPinned(1,9),"Invalid body index accepted a pin.");
    world.removeRagdoll(1);require(!world.ragdollBodyPinned(1,0)&&!world.setRagdollBodyPinned(1,0,false),"Removed rig retained transient pins.");
    require(world.createRagdoll(1,asset,source,pose,Mat4{})&&!world.ragdollBodyPinned(1,0),"Recreated rig inherited old pins.");
}
void isolatedBranchesRetainNativeConstraints() {
    Scene scene;const auto source=model();const auto asset=setup(*source);for(auto& body:asset->bodies)body.position={.1f,0,0};
    PhysicsWorld world;require(world.createRagdoll(1,asset,source,evaluateModelPose(*source),Mat4{}),"Could not create isolated branch fixture.");
    const auto initial=world.ragdollBodyTransforms(1);require(world.setRagdollBodyPinned(1,0,true),"Could not pin the branch parent.");
    require(world.applyRagdollImpulse(1,{2,0,0},{},1),"Free selected body rejected an impulse.");tick(world,scene,90);
    const auto branch=world.ragdollBodyTransforms(1);same(branch[0],initial[0],"Selected branch moved its pinned ancestor.",.0005f);
    require(std::abs(branch[1][0]-initial[1][0])>.02f&&math::length(Vec3{branch[2][12]-initial[2][12],branch[2][13]-initial[2][13],branch[2][14]-initial[2][14]})>.05f,"Selected body or its free descendants did not respond below the pin.");
    require(math::length(Vec3{branch[1][12]-initial[1][12],branch[1][13]-initial[1][13],branch[1][14]-initial[1][14]})<.02f,"Pinning discarded the native parent joint anchor.");
    require(world.setRagdollBodyPinned(1,1,true),"Could not isolate the leaf joint.");const auto fixedParent=world.ragdollBodyTransforms(1)[1];
    require(world.applyRagdollImpulse(1,{-2,1,0},{},2),"Selected leaf rejected an impulse.");tick(world,scene,60);const auto leaf=world.ragdollBodyTransforms(1);
    same(leaf[1],fixedParent,"Testing a leaf joint moved its pinned parent.",.0005f);require(std::abs(leaf[2][0]-branch[2][0])>.02f,"The isolated selected joint did not respond.");
    const Vec3 grab{leaf[2][12]+.1f,leaf[2][13],leaf[2][14]};require(world.beginRagdollDrag(1,2,grab),"Free isolated joint could not be grabbed.");
    require(world.setRagdollBodyPinned(1,2,true)&&!world.updateRagdollDrag(grab+Vec3{1,0,0}),"Pinning a grabbed body retained its mouse constraint.");
    const auto allPinned=world.ragdollBodyTransforms(1);tick(world,scene,60);for(std::size_t i=0;i<allPinned.size();++i)same(world.ragdollBodyTransforms(1)[i],allPinned[i],"All-pinned constraints changed a frozen pose.",.0005f);
    require(world.setRagdollBodyPinned(1,2,false),"Could not release the isolated joint.");tick(world,scene,15);require(world.ragdollActive(1),"Unpinning did not wake the connected ragdoll.");world.removeRagdoll(1);
}
void activeTracesRetainExactPhysicalBody() {
    const auto source=model();const auto asset=setup(*source);const auto pose=evaluateModelPose(*source);PhysicsWorld world;
    require(world.createRagdoll(11,asset,source,pose,Mat4{},{},{},10),"Could not create body trace fixture.");
    for(const auto shape:{PhysicsTraceShape::Line,PhysicsTraceShape::Sphere,PhysicsTraceShape::Box,PhysicsTraceShape::Capsule}) {
        PhysicsTrace query;query.shape=shape;query.ray={{0,2,3},{0,0,-1}};query.maxDistance=6;query.radius=.025f;query.halfExtents={.025f,.025f,.025f};query.capsuleHalfHeight=.025f;
        const auto result=world.trace(query);require(result.valid&&result.hits.size()==1&&result.hits[0].entity==11&&result.hits[0].owner==10&&result.hits[0].ragdollBodyIndex==2,"Active line/shape trace lost its exact physical bone or actor owner.");
        require(result.hits[0].subshape!=static_cast<std::uint32_t>(result.hits[0].ragdollBodyIndex),"Trace confused the shape's opaque subshape identifier with its ragdoll body index.");
        const auto all=world.trace(query,{},PhysicsTraceMode::All);require(all.hits.size()==1&&all.hits[0].ragdollBodyIndex==2,"All trace lost the selected physical body.");
    }
    EntityRecord wall;wall.id=100;wall.name="Occluding wall";wall.local=math::translation({0,2,1});wall.collider.emplace();wall.collider->size={1,1,.1f};
    EntityRecord character;character.id=20;character.name="Ordinary character";character.local=math::translation({2,0,0});character.controller.emplace();Scene scene;scene.apply({wall,character},{});world.reconcile(scene);
    PhysicsTrace query;query.ray={{0,2,3},{0,0,-1}};query.maxDistance=6;const auto hit=world.trace(query);
    require(hit.hits.size()==1&&hit.hits[0].entity==100&&hit.hits[0].ragdollBodyIndex==-1,"Physical bone metadata replaced ordinary closest-world occlusion.");
    query.ray={{2,.9f,3},{0,0,-1}};const auto capsule=world.trace(query);require(capsule.hits.size()==1&&capsule.hits[0].entity==20&&capsule.hits[0].ragdollBodyIndex==-1,"A virtual character inherited a stale native ragdoll body identifier.");
}
void posedQueriesMatchSimulationShapesWithoutWorldChanges() {
    const auto source=model(true);auto local=sample_model_local_pose(*source);local.nodes[1].matrix=math::translation({2,1.3f,-.4f})*math::rotation(math::axisAngle({0,0,1},.4f));const auto pose=finalize_model_pose(*source,local);
    const float scale=1.75f;const auto worldTransform=math::translation({4,2,-3})*math::rotation(math::axisAngle({0,1,0},.65f))*math::scaling({scale,scale,scale});
    const auto bone=math::orthonormalFrame(worldTransform*pose.worldTransforms[1]);PhysicsWorld world;
    const auto revision=world.geometryRevision();const auto status=world.status();const auto poseBefore=pose.worldTransforms;
    for(const auto shape:{RagdollShape::Sphere,RagdollShape::Box,RagdollShape::Capsule}) {
        const auto asset=setup(*source,true);auto& body=asset->bodies[0];body.shape=shape;body.position={.15f,.05f,-.1f};body.rotation=math::axisAngle({1,1,0},.45f);body.radius=.08f;body.height=.4f;body.size={.3f,.2f,.4f};
        const auto original=*asset;const auto shapeFrame=bone*math::translation(body.position*scale)*math::rotation(body.rotation);const Vec3 center{shapeFrame[12],shapeFrame[13],shapeFrame[14]},normal{shapeFrame[8],shapeFrame[9],shapeFrame[10]};
        const Ray ray{center+normal*2,normal*-3};const float extent=(shape==RagdollShape::Box?body.size[2]*.5f:body.radius)*scale;bool valid{};
        const auto posed=world.pickRagdollPose(11,*asset,*source,pose,worldTransform,ray,5,&valid);
        require(valid&&posed&&posed->entity==11&&posed->bodyIndex==0&&near(posed->distance,2-extent,.004f)&&math::length(posed->position-(center+normal*extent))<.004&&math::length(posed->normal-normal)<.004,"Posed shape query lost current animation, authored offsets, orientation, dimensions or uniform instance scale.");
        require(world.ragdollCount()==0&&world.colliderCount()==0&&world.characterCount()==0&&world.geometryRevision()==revision&&world.status()==status&&*asset==original&&pose.worldTransforms==poseBefore,"Posed query created bodies, changed world metadata or modified authored/pose data.");
        valid=false;const auto miss=world.pickRagdollPose(11,*asset,*source,pose,worldTransform,{center+normal*2,{0,1,0}},.01f,&valid);require(valid&&!miss,"Valid posed miss was reported as an unsupported rig.");
        require(world.createRagdoll(11,asset,source,pose,worldTransform),"Could not instantiate comparison shapes.");const auto active=world.pickRagdoll(ray,5);
        require(active&&active->bodyIndex==posed->bodyIndex&&near(active->distance,posed->distance,.0005f)&&math::length(active->position-posed->position)<.0005,"Posed query geometry differs from native simulation body geometry.");world.removeRagdoll(11);
    }
    const auto asset=setup(*source,true);const Ray ray{{0,2,3},{0,0,-1}};bool valid=true;
    require(!world.pickRagdollPose(11,*asset,*source,pose,math::scaling({1,2,1}),ray,5,&valid)&&!valid,"Nonuniform posed query was accepted.");
    valid=true;require(!world.pickRagdollPose(11,*asset,*source,pose,math::scaling({-1,1,1}),ray,5,&valid)&&!valid,"Reflected posed query was accepted.");
    auto malformed=pose;malformed.worldTransforms[1][12]=std::numeric_limits<float>::quiet_NaN();valid=true;require(!world.pickRagdollPose(11,*asset,*source,malformed,Mat4{},ray,5,&valid)&&!valid,"Malformed posed bone was accepted.");
    auto stale=*asset;stale.sourceModel.id="58070ae2-87db-4923-9654-c0b335e877e2";valid=true;require(!world.pickRagdollPose(11,stale,*source,pose,Mat4{},ray,5,&valid)&&!valid,"Mismatched source identity was accepted.");
    stale=*asset;stale.bodies.clear();valid=true;require(!world.pickRagdollPose(11,stale,*source,pose,Mat4{},ray,5,&valid)&&!valid,"Empty unfinished rig displaced ordinary collision fallback.");
}
}
int main() {
    try{poseBindingAndHandoff();dropSleepQueriesAndIndependence();actorCollisionOwnershipAndTriggers();physicsDragReleaseAndReset();stationaryControllerBlocksDynamicBones();dynamicPairMasks();asymmetricJointAxes();transientPinsFreezeAndResume();isolatedBranchesRetainNativeConstraints();activeTracesRetainExactPhysicalBody();posedQueriesMatchSimulationShapesWithoutWorldChanges();std::cout<<"Ragdoll physics checks passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
