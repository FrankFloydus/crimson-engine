#include "ragdoll_fixture.h"
#include "assets/AnimationAsset.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>

namespace {
using namespace crimson;using namespace crimson::ragdoll_test;
std::string read(const std::filesystem::path& file){std::ifstream in(file,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void sourceAndGeneration(){
    const auto model=source();auto rig=makeRagdollAsset(model);require(rig.bodies.empty(),"New ragdoll should be a valid empty authoring document.");
    generateRagdollBodies(rig,model,{1,3});require(rig.bodies.size()==2&&rig.bodies[0].node==3&&rig.bodies[1].node==1&&rig.bodies[1].parent==0,"Selected joints were not ordered by physical ancestry through skipped bones.");
    validateRagdollSource(rig,model);const auto frames=ragdollReferenceTransforms(model);require(std::abs(frames[1][13]-2)<1e-6,"Reference joints lost hierarchy translation.");
    auto scaled=model;scaled.nodes[0].local=math::scaling({2,3,4});const auto scaleFrames=ragdollReferenceTransforms(scaled);require(std::abs(scaleFrames[1][13]-6)<1e-6&&std::abs(math::length(math::transformDirection(scaleFrames[1],{0,1,0}))-1)<1e-6,"Model-space metres or rigid frame normalization was lost.");
    auto changed=model;changed.nodes[1].local[12]+=.1f;rejects([&]{validateRagdollSource(rig,changed);},"Changed bind pose silently reused the old ragdoll.");
    changed=model;changed.id=newAssetId();rejects([&]{validateRagdollSource(rig,changed);},"Unrelated source package was admitted by signature alone.");
    const auto before=rig;rejects([&]{rebindRagdollSource(rig,changed,0,{3,3});},"Duplicate explicit remapping was admitted.");require(rig==before,"Rejected remapping partially changed authored data.");
    rebindRagdollSource(rig,changed,0,{3,4});require(rig.sourceModel.id==changed.id&&rig.bodies[1].node==4,"Explicit arbitrary-skeleton remapping failed.");validateRagdollSource(rig,changed);
    auto bad=rig;bad.bodies[1].parent=-1;rejects([&]{validateRagdollSource(bad,changed);},"Disconnected physics hierarchy ignored a selected ancestor.");
    changed=model;changed.nodes[1].local=math::scaling({-1,1,1});rejects([&]{ragdollReferenceTransforms(changed);},"Reflected bind frame silently changed handedness.");
    changed=model;changed.nodes[1].children={0};rejects([&]{ragdollReferenceTransforms(changed);},"Cyclic source hierarchy was admitted.");
    rejects([&]{generateRagdollBodies(rig,model,{0});},"Generation accepted a non-skin joint.");
}
void strictCodecAndStorage(){
    Project project;auto rig=asset();rig.bodies[0].shape=RagdollShape::Box;rig.bodies[1].joint.rotation=math::axisAngle({0,0,1},.3);rig.bodies[1].joint.position={.01f,.02f,0};rig.bodies[1].joint.friction=2;
    const auto bytes=serializeRagdollAsset(rig);require(deserializeRagdollAsset(bytes,rig.id)==rig,"Ragdoll native codec lost authored bodies or joint frames.");
    rejects([&]{deserializeRagdollAsset(bytes,newAssetId());},"Expected ragdoll identity was ignored.");
    rejects([&]{deserializeRagdollAsset(std::string(1024*1024+1,' '));},"Oversized ragdoll JSON was admitted.");
    for(const auto* field:{"node","parent","shape"})for(const auto& value:nlohmann::json::array({true,1.25,"0",std::numeric_limits<std::uint64_t>::max()})){
        auto data=nlohmann::json::parse(bytes);data["bodies"][0][field]=value;rejects([&]{deserializeRagdollAsset(data.dump());},"Malformed or overflowing ragdoll body index was admitted.");}
    for(const auto* field:{"skin","version"}){auto data=nlohmann::json::parse(bytes);data[field]=true;rejects([&]{deserializeRagdollAsset(data.dump());},"Boolean ragdoll index/version was admitted.");}
    for(const auto& value:nlohmann::json::array({true,"1",nullptr})){auto data=nlohmann::json::parse(bytes);data["gravityFactor"]=value;rejects([&]{deserializeRagdollAsset(data.dump());},"Nonnumeric physics setting was admitted.");}
    auto bad=rig;bad.bodies[1].parent=1;rejects([&]{validateRagdollAsset(bad);},"A self-parent body was admitted.");bad=rig;bad.bodies[0].rotation={0,0,0,2};rejects([&]{validateRagdollAsset(bad);},"Nonunit collider rotation was admitted.");
    bad=rig;bad.bodies[0].mass=std::numeric_limits<float>::quiet_NaN();rejects([&]{validateRagdollAsset(bad);},"Nonfinite mass was admitted.");bad=rig;bad.bodies[1].height=bad.bodies[1].radius;rejects([&]{validateRagdollAsset(bad);},"Capsule caps exceeded total height.");
    bad=rig;bad.bodies[1].joint.twistMin=45;bad.bodies[1].joint.twistMax=-45;rejects([&]{validateRagdollAsset(bad);},"Reversed joint limits were admitted.");
    saveRagdollAsset(project.root,rig);const auto path=ragdollAssetPath(project.root,rig.id);require(path.extension()==".crimson-ragdoll"&&loadRagdollAsset(project.root,rig.id)==rig,"Saved Ragdoll did not register/resolve natively.");
    const auto stored=read(path),locations=read(project.root/".crimson/asset-locations.json");initializeContentStorage(project.root);require(read(path)==stored&&read(project.root/".crimson/asset-locations.json")==locations,"Warm storage initialization rewrote Ragdoll authored data.");
    const auto copy=copyRagdollAsset(project.root,rig,"Creature variation");require(copy.id!=rig.id&&copy.sourceModel==rig.sourceModel&&copy.bodies==rig.bodies,"Ragdoll Save As lost source binding or physical setup.");
}
}
int main(){try{sourceAndGeneration();strictCodecAndStorage();std::cout<<"Ragdoll source binding, arbitrary-joint generation, rigid frames, strict native codec and storage passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
