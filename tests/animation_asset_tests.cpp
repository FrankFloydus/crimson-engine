#include "assets/AnimationAsset.h"
#include "assets/AssetReferenceManager.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F&& action,const char* message){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}require(rejected,message);}
std::string read(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("ce-anim-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);initializeContentStorage(root);}
    ~Project(){std::error_code error;const auto target=std::filesystem::weakly_canonical(root,error),parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("ce-anim-"))std::filesystem::remove_all(target,error);}
};
ModelAsset source() {
    ModelAsset model;model.id=canonicalAssetId("animation-test-source");model.name="Animated model";
    ModelNode finger;finger.name="Finger";ModelNode root;root.name="root";root.children={0};ModelNode wrapper;wrapper.name="Armature";wrapper.children={1};
    model.nodes={finger,root,wrapper};model.roots={2};model.skins.push_back({"Skeleton",{0,1},{Mat4{},Mat4{}},2});
    ModelAnimationChannel channel;channel.node=1;channel.components=3;channel.times={1,3};channel.values={2,0,0,6,0,0};
    model.animations={{"Walk",1,3,{channel}},{"Walk",1,3,{channel}}};return model;
}
void identityAndBinding() {
    const auto model=source();const auto clip=importedAnimationAsset(model,0),second=importedAnimationAsset(model,1);
    require(clip.id==importedAnimationId(model.id,0)&&clip.id!=second.id,"Duplicate imported clip names lost their independent UUIDs.");
    require(clip.rootNode==1&&clip.startTime==0&&clip.endTime==2&&!clip.forceRootLock,"Imported root selection used the first bone or lost nonzero clip start semantics.");
    validateAnimationSource(clip,model);
    auto edited=clip;edited.name="Walk in place";edited.startTime=.25f;edited.endTime=1.75f;edited.playbackRate=1.25f;edited.forceRootLock=true;
    validateAnimationSource(edited,model);require(edited.id==clip.id,"Authored settings changed clip identity.");
    auto changed=model;changed.animations[0].channels[0].values.back()=4;
    rejects([&]{validateAnimationSource(clip,changed);},"Changed immutable source tracks silently rebound the clip.");
    changed=model;changed.nodes[0].local[12]=.1f;
    require(animationBindingSignature(changed)!=clip.bindingSignature,"Different reference poses shared an animation binding.");
    rejects([&]{validateAnimationSource(clip,changed);},"Changed reference hierarchy was accepted as the old skeleton binding.");
    changed=model;changed.nodes[0].weights={.25f};auto changedDefaults=changed;changedDefaults.nodes[0].weights={.75f};
    require(animationBindingSignature(changed)!=animationBindingSignature(changedDefaults),"Equal morph target counts hid different default weights.");
    auto meshDefaults=model;meshDefaults.meshes.push_back({"Mesh",{0},{.25f}});meshDefaults.primitives.push_back({});meshDefaults.primitives[0].morphTargets.resize(1);meshDefaults.nodes[0].mesh=0;
    changedDefaults=meshDefaults;changedDefaults.meshes[0].weights={.75f};
    require(animationBindingSignature(meshDefaults)!=animationBindingSignature(changedDefaults),"Mesh-level default morph weights were omitted from compatibility.");
    changedDefaults=meshDefaults;changedDefaults.nodes[0].mesh=-1;
    require(animationBindingSignature(meshDefaults)!=animationBindingSignature(changedDefaults),"Different mesh attachments shared an animation binding.");
    changedDefaults=meshDefaults;changedDefaults.nodes[0].skin=0;
    require(animationBindingSignature(meshDefaults)!=animationBindingSignature(changedDefaults),"Different skin attachments shared an animation binding.");
    changedDefaults=meshDefaults;changedDefaults.primitives[0].morphTargets.resize(2);
    require(animationBindingSignature(meshDefaults)!=animationBindingSignature(changedDefaults),"Different effective morph target cardinalities shared an animation binding.");
    changedDefaults=model;changedDefaults.nodes[0].matrixAuthored=true;
    require(animationBindingSignature(model)!=animationBindingSignature(changedDefaults),"Matrix-authored target nodes accepted source TRS channels.");
    auto other=model;other.id=newAssetId();require(importedAnimationAsset(other,0).id!=clip.id,"A different immutable source package reused animation identity.");
    auto invalid=clip;invalid.rootNode=100;
    rejects([&]{validateAnimationSource(invalid,model);},"Unavailable root was admitted by source validation.");
    invalid=clip;invalid.endTime=3;
    rejects([&]{validateAnimationSource(invalid,model);},"Trim range exceeded the imported clip.");
}
void codecAndStorage() {
    Project project;auto asset=importedAnimationAsset(source(),0);asset.forceRootLock=true;asset.rootLockReference=AnimationRootLockReference::ReferencePose;
    const auto bytes=serializeAnimationAsset(asset);require(deserializeAnimationAsset(bytes,asset.id)==asset,"Animation settings failed their native codec round trip.");
    rejects([&]{deserializeAnimationAsset(bytes,newAssetId());},"Animation codec ignored expected identity.");
    rejects([&]{deserializeAnimationAsset(std::string(65537,' '));},"Oversized animation JSON was admitted.");
    for(const auto& value:nlohmann::json::array({-1,1.25,"1",true})) {
        auto data=nlohmann::json::parse(bytes);data["sourceClip"]=value;
        rejects([&]{deserializeAnimationAsset(data.dump());},"Malformed animation source clip index was admitted.");
    }
    for(const auto& value:nlohmann::json::array({-2,1.5,"0",true})) {
        auto data=nlohmann::json::parse(bytes);data["rootNode"]=value;
        rejects([&]{deserializeAnimationAsset(data.dump());},"Malformed animation root node was admitted.");
    }
    for(const auto* field:{"sourceClip","rootNode","rootLockReference"}) {
        auto data=nlohmann::json::parse(bytes);data[field]=std::numeric_limits<std::uint64_t>::max();data["forceRootLock"]=false;
        rejects([&]{deserializeAnimationAsset(data.dump());},"Oversized unsigned animation index wrapped into a valid signed value.");
    }
    auto invalid=asset;invalid.playbackRate=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{validateAnimationAsset(invalid);},"Nonfinite animation playback rate was admitted.");
    invalid=asset;invalid.rootNode=-1;
    rejects([&]{validateAnimationAsset(invalid);},"Force Root Lock accepted no root node.");
    saveAnimationAsset(project.root,asset);const auto path=animationAssetPath(project.root,asset.id);
    require(loadAnimationAsset(project.root,asset.id)==asset&&path.extension()==".crimson-animation","Saved animation was not registered as a native asset.");
    const auto stored=read(path),index=read(project.root/".crimson/asset-locations.json");initializeContentStorage(project.root);
    require(read(path)==stored&&read(project.root/".crimson/asset-locations.json")==index,"Warm storage initialization rewrote an animation or its location index.");
    const auto copy=copyAnimationAsset(project.root,asset,"Copied walk");
    require(copy.id!=asset.id&&copy.sourceModel==asset.sourceModel&&copy.sourceClipIdentity==asset.sourceClipIdentity&&copy.forceRootLock,
        "Animation duplication lost authored settings or copied the source identity incorrectly.");
    AssetReferenceManager references(project.root);require(references.resolveAnimation(copy.id)==copy.id,"Native Animation reference could not resolve.");
    const std::array<AssetEntry,1> deleted{{{asset.id,asset.name,AssetKind::Animation}}};
    references.replace(deleted,{{asset.id,copy.id}});require(references.resolveAnimation(asset.id)==copy.id,"Animation replacement did not retain a typed fallback.");
    rejects([&]{references.replace(deleted,{{asset.id,asset.sourceModel.id}});},"Animation replacement accepted an unregistered model as an Animation.");
}
}
int main(){try{identityAndBinding();codecAndStorage();std::cout<<"Animation identity, source binding, root selection, native codec and storage passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<"Animation asset test failed: "<<error.what()<<'\n';return 1;}}
