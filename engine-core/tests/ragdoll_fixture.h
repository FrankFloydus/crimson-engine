#pragma once
#include "assets/RagdollAsset.h"
#include "assets/ContentStorage.h"
#include "resources/AssetId.h"
#include <chrono>
#include <filesystem>
#include <stdexcept>

namespace crimson::ragdoll_test {
inline void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& action,const char* message){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}require(rejected,message);}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("ce-rag-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);initializeContentStorage(root);}
    ~Project(){std::error_code error;const auto target=std::filesystem::weakly_canonical(root,error);if(error)return;const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("ce-rag-"))std::filesystem::remove_all(target,error);}
};
inline ModelAsset source(){
    ModelAsset model;model.id=canonicalAssetId("ragdoll-test-source");model.name="Arbitrary creature";
    model.nodes.resize(5);model.nodes[0].name="Wrapper";model.nodes[0].children={3};
    model.nodes[1].name="Tip";model.nodes[1].parent=2;model.nodes[1].local=math::translation({.3f,.5f,0});
    model.nodes[2].name="Segment";model.nodes[2].parent=3;model.nodes[2].children={1,4};model.nodes[2].local=math::translation({0,.5f,0});
    model.nodes[3].name="Center";model.nodes[3].parent=0;model.nodes[3].children={2};model.nodes[3].local=math::translation({0,1,0});
    model.nodes[4].name="Tip";model.nodes[4].parent=2;model.nodes[4].local=math::translation({-.3f,.5f,0});
    model.roots={0};model.skins.push_back({"Creature",{1,2,3,4},{Mat4{},Mat4{},Mat4{},Mat4{}},3});return model;
}
inline RagdollAsset asset(){const auto model=source();auto result=makeRagdollAsset(model);generateRagdollBodies(result,model,{3,1});return result;}
}
