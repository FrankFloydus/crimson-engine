#include "ragdoll_fixture.h"
#include "editor/RagdollAuthoring.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>

namespace {
using namespace crimson;using namespace crimson::ragdoll_test;
bool near(double a,double b,double tolerance=1e-5){return std::abs(a-b)<tolerance;}
void same(const Vec3& a,const Vec3& b,const char* message){require(math::length(a-b)<2e-5,message);}
void sameRotation(const Quat& a,const Quat& b,const char* message){const auto first=math::rotation(a),second=math::rotation(b);for(std::size_t i=0;i<12;++i)require(near(first[i],second[i]),message);}
RagdollBody body(){RagdollBody result;result.node=3;result.position={.2f,.3f,-.1f};result.rotation=math::axisAngle({0,0,1},.4);result.size={.4f,.8f,.2f};result.radius=.1f;result.height=.8f;result.mass=3;result.joint.twistMin=-50;return result;}
Mat4 bone(){auto model=source();model.nodes[0].local=math::translation({2,3,-1})*math::rotation(math::axisAngle({0,1,0},.6))*math::scaling({.5f,.5f,.5f});return ragdollReferenceTransforms(model)[3];}
void moveAndRotate(){
    const auto original=body();const auto reference=bone();const auto proxy=ragdollBodyGizmoMatrix(original,reference,RagdollAuthoringOperation::Translate);require(bool(proxy),"Collider proxy could not use a normalized scaled-source joint.");
    same({(*proxy)[12],(*proxy)[13],(*proxy)[14]},math::transformPoint(reference,original.position),"Inherited skeleton scale resized an authored body offset.");
    const Vec3 expected{-.3f,.7f,.2f};auto moved=*proxy;const auto center=math::transformPoint(reference,expected);for(std::size_t i=0;i<3;++i)moved[12+i]=center[i];const auto translated=applyRagdollBodyGizmoTransform(original,reference,moved,RagdollAuthoringOperation::Translate);
    require(bool(translated),"Valid Move gizmo was rejected.");same(translated->position,expected,"Move did not convert the world offset into the bone's rigid frame.");require(translated->size==original.size&&translated->radius==original.radius&&translated->height==original.height&&translated->joint==original.joint,"Move changed authored shape dimensions or joint settings.");sameRotation(translated->rotation,original.rotation,"Move changed collider rotation.");
    const auto desired=math::axisAngle(math::normalized(Vec3{1,2,-1}),1.3);const auto rotated=reference*math::translation(original.position)*math::rotation(desired);const auto changed=applyRagdollBodyGizmoTransform(original,reference,rotated,RagdollAuthoringOperation::Rotate);require(bool(changed),"Valid Rotate gizmo was rejected.");sameRotation(changed->rotation,desired,"Rotate did not preserve bone-local orientation.");require(changed->position==original.position&&changed->size==original.size&&changed->radius==original.radius&&changed->height==original.height,"Rotate drifted the collider centre or resized it.");
    for(auto malformed:{*proxy*math::scaling({-1,1,1}),*proxy*math::scaling({0,1,1}),*proxy*math::scaling({2,1,1})})require(!applyRagdollBodyGizmoTransform(original,reference,malformed,RagdollAuthoringOperation::Translate),"Move accepted reflection, singularity or scale.");
    auto shear=*proxy;shear[4]+=.2f;require(!applyRagdollBodyGizmoTransform(original,reference,shear,RagdollAuthoringOperation::Rotate),"Rotate accepted a sheared proxy.");auto invalid=*proxy;invalid[12]=std::numeric_limits<float>::quiet_NaN();require(!applyRagdollBodyGizmoTransform(original,reference,invalid,RagdollAuthoringOperation::Translate),"Move accepted nonfinite position.");
    require(!ragdollBodyGizmoMatrix(original,reference*math::scaling({.5f,.5f,.5f}),RagdollAuthoringOperation::Resize),"A scaled bone frame silently scaled model-metre dimensions.");
}
void resizePolicies(){
    const auto reference=bone();auto original=body();original.shape=RagdollShape::Box;auto proxy=*ragdollBodyGizmoMatrix(original,reference,RagdollAuthoringOperation::Resize);auto manipulated=proxy*math::scaling({2,.5f,3});manipulated[12]+=5;const auto box=applyRagdollBodyGizmoTransform(original,reference,manipulated,RagdollAuthoringOperation::Resize);
    require(box&&near(box->size[0],.8)&&near(box->size[1],.4)&&near(box->size[2],.6)&&box->position==original.position,"Box resizing did not use exact independent full dimensions or drifted its captured centre.");sameRotation(box->rotation,original.rotation,"Box resize rotated the collider.");
    const auto repeat=applyRagdollBodyGizmoTransform(original,reference,manipulated,RagdollAuthoringOperation::Resize);require(repeat==box,"A repeated gesture sample compounded the prior resize.");
    const auto locked=applyRagdollBodyGizmoTransform(original,reference,proxy*math::scaling({2,1,1}),RagdollAuthoringOperation::Resize,true);require(locked&&near(locked->size[0],.8)&&near(locked->size[1],1.6)&&near(locked->size[2],.4),"Box proportions lock did not scale every captured dimension.");
    original.shape=RagdollShape::Sphere;proxy=*ragdollBodyGizmoMatrix(original,reference,RagdollAuthoringOperation::Resize);const auto sphere=applyRagdollBodyGizmoTransform(original,reference,proxy*math::scaling({1,1,3}),RagdollAuthoringOperation::Resize);require(sphere&&near(sphere->radius,.3)&&sphere->position==original.position,"Sphere resize did not couple all axes to its radius.");
    original.shape=RagdollShape::Capsule;proxy=*ragdollBodyGizmoMatrix(original,reference,RagdollAuthoringOperation::Resize);const auto radial=applyRagdollBodyGizmoTransform(original,reference,proxy*math::scaling({1,1,2}),RagdollAuthoringOperation::Resize);require(radial&&near(radial->radius,.2)&&near(radial->height,.8),"Capsule Z resize did not couple X/Z while preserving independent Y height.");
    const auto shortCapsule=applyRagdollBodyGizmoTransform(original,reference,proxy*math::scaling({1,.1f,1}),RagdollAuthoringOperation::Resize);require(shortCapsule&&near(shortCapsule->radius,.1)&&near(shortCapsule->height,.2),"Capsule full height was allowed below its two cap radii.");
    const auto uniform=applyRagdollBodyGizmoTransform(original,reference,proxy*math::scaling({2,2,2}),RagdollAuthoringOperation::Resize);require(uniform&&near(uniform->radius,.2)&&near(uniform->height,1.6),"Uniform capsule resize did not scale both radius and full height.");
    const auto tiny=applyRagdollBodyGizmoTransform(original,reference,proxy*math::scaling({.00001f,.00001f,.00001f}),RagdollAuthoringOperation::Resize);require(tiny&&tiny->radius>=.0001f&&tiny->height>=2*tiny->radius,"Collider resize violated minimum dimensions.");
    original.shape=RagdollShape::Box;original.size={5000,10000,2500};proxy=*ragdollBodyGizmoMatrix(original,reference,RagdollAuthoringOperation::Resize);const auto bounded=applyRagdollBodyGizmoTransform(original,reference,proxy*math::scaling({2,1,1}),RagdollAuthoringOperation::Resize,true);require(bounded&&bounded->size==original.size,"Proportions lock distorted a box after reaching the maximum dimension.");
}
void analyticPicking(){
    std::vector<Mat4> frames(4);frames[3]=bone();auto collider=body();collider.shape=RagdollShape::Capsule;collider.radius=.2f;collider.height=1.4f;collider.rotation=math::axisAngle({1,0,0},.7);const auto world=*ragdollBodyGizmoMatrix(collider,frames[3],RagdollAuthoringOperation::Translate);
    const auto cast=[&](Vec3 origin,Vec3 direction){return pickRagdollBodies(std::span{&collider,1},frames,{math::transformPoint(world,origin),math::transformDirection(world,direction)},10);};
    const auto top=cast({0,3,0},{0,-2,0});require(top&&near(top->distance,2.3,2e-5),"Rotated capsule top picking used the body origin or wrong full-height convention.");same(top->normal,math::transformDirection(world,{0,1,0}),"Capsule cap hit normal was not transformed to world space.");
    const auto side=cast({2,0,0},{-1,0,0});require(side&&near(side->distance,1.8,2e-5),"Capsule shaft ray hit missed its exact cylinder.");
    const auto cap=cast({2,.6f,0},{-1,0,0});require(cap&&near(cap->distance,2-std::sqrt(.03),3e-5),"Capsule rounded cap was picked as a box/cylinder proxy.");
    const auto inside=cast({0,0,0},{1,0,0});require(inside&&near(inside->distance,.2,2e-5),"Inside capsule ray did not return its forward exit surface.");require(!cast({2,.71f,0},{-1,0,0}),"Ray beyond the capsule end cap reported a proxy hit.");
    collider.shape=RagdollShape::Sphere;const auto sphere=cast({0,0,0},{0,0,1});require(sphere&&near(sphere->distance,.2,2e-5),"Inside sphere ray did not find the exact exit.");
    collider.shape=RagdollShape::Box;collider.size={.4f,.8f,.2f};const auto box=cast({0,0,2},{0,0,-1});require(box&&near(box->distance,1.9,2e-5),"Rotated box ray did not use full box dimensions.");
    std::vector<RagdollBody> pair{collider,collider};pair[0].position[2]-=1;const auto closest=pickRagdollBodies(pair,frames,{math::transformPoint(world,{0,0,2}),math::transformDirection(world,{0,0,-1})},10);require(closest&&closest->body==1,"Collider picking did not select the nearest exact surface.");
}
void simulationScope(){
    std::vector<RagdollBody> bodies(4);bodies[0].parent=-1;bodies[1].parent=0;bodies[2].parent=1;bodies[3].parent=1;
    const auto branch=ragdollSimulationMask(bodies,1,RagdollSimulationScope::SelectedBranch);require(branch==std::vector<std::uint8_t>({0,1,1,1}),"Selected branch simulation omitted physical descendants or crossed its parent boundary.");
    require(ragdollSimulationMask(bodies,2,RagdollSimulationScope::SelectedBody)==std::vector<std::uint8_t>({0,0,1,0}),"Isolated body scope included other bodies.");require(ragdollScopePinMask(bodies,branch,true)==std::vector<std::uint8_t>({1,1,0,0}),"Scope root pinning failed to preserve the surrounding pose or pinned descendants.");
    require(ragdollScopePinMask(bodies,branch)==std::vector<std::uint8_t>({1,0,0,0}),"Bodies outside the selected simulation scope did not remain fixed.");require(ragdollSimulationMask(bodies,99,RagdollSimulationScope::SelectedBranch)==std::vector<std::uint8_t>(4),"An absent selected body escaped the bounded scope.");
    bodies[2].parent=2;rejects([&]{ragdollSimulationMask(bodies,1,RagdollSimulationScope::All);},"A cyclic physical hierarchy was admitted by scope traversal.");
}
}
int main(){try{moveAndRotate();resizePolicies();analyticPicking();simulationScope();std::cout<<"Ragdoll collider gizmos, immutable resize policies, exact analytic picking and bounded simulation scopes passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
