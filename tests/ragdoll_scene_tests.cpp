#include "ragdoll_fixture.h"
#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"
#include "scene/SceneOutlinerRows.h"
#include "assets/AtomicFile.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>

namespace {
using namespace crimson;using namespace crimson::ragdoll_test;using Json=nlohmann::json;
std::string read(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
void persistenceAndHistory(){
    Project project;const auto model=source();const auto rig=asset();SceneDocument document(project.root);const auto entity=document.createModel(model.id,"Enemy",math::translation({2,3,4}));
    require(document.save("Enemy setup"),document.status().c_str());const auto original=read(sceneAssetPath(project.root,document.assetId()));const auto absent=Json::parse(original);require(std::none_of(absent.at("entities").begin(),absent.at("entities").end(),[](const auto& value){return value.contains("modelRagdoll");}),"Default ragdoll settings were written unnecessarily.");
    const auto before=*document.scene().find(entity);require(document.begin("Assign ragdoll")&&document.update([&](auto& records,auto&){for(auto& record:records)if(record.id==entity)record.modelRagdoll={AssetRef<RagdollAsset>{rig.id},true};}),"Ragdoll gesture failed.");
    require(document.scene().find(entity)->modelRagdoll.startSimulated,"Uncommitted ragdoll property did not preview.");document.cancel();require(sameEntity(before,*document.scene().find(entity))&&!document.dirty(),"Canceled ragdoll edit changed authored state.");
    auto changed=before;changed.modelRagdoll={AssetRef<RagdollAsset>{rig.id},true};require(document.updateEntity(entity,changed)&&document.save(),document.status().c_str());
    const auto file=sceneAssetPath(project.root,document.assetId());const auto fingerprint=sceneFileFingerprint(project.root,document.assetId());const auto saved=read(file);
    SceneDocument reopened(project.root);require(reopened.load(document.assetId())&&!reopened.dirty()&&reopened.scene().find(entity)->modelRagdoll==changed.modelRagdoll,"Optional Ragdoll settings failed to load cleanly.");require(read(file)==saved&&sceneFileFingerprint(project.root,document.assetId())==fingerprint,"Loading Ragdoll settings rewrote a scene.");
    const auto extracted=extractScene(reopened.scene());const auto visual=std::find_if(extracted.models.begin(),extracted.models.end(),[&](const auto& value){return value.id==entity;});require(visual!=extracted.models.end()&&visual->ragdoll==changed.modelRagdoll&&!visual->physicsPose,"Extraction lost authored setup or invented a physics pose.");
    const auto rows=sceneOutlinerRows(reopened.scene());require(std::count_if(rows.begin(),rows.end(),[&](const auto& row){return row.entity==entity;})==1,"A Ragdoll added duplicate Outliner entity rows.");
    auto cleared=*reopened.scene().find(entity);cleared.modelRagdoll={};require(reopened.updateEntity(entity,cleared),"Could not clear Ragdoll assignment.");const auto refs=reopened.historicalRagdollReferences();require(std::find(refs.begin(),refs.end(),rig.id)!=refs.end(),"Scene history did not retain the removed Ragdoll asset.");
    require(reopened.undo()&&reopened.scene().find(entity)->modelRagdoll==changed.modelRagdoll&&reopened.redo()&&reopened.scene().find(entity)->modelRagdoll==SceneModelRagdoll{},"Ragdoll property undo/redo lost asset or start mode.");
    require(reopened.undo(),"Could not restore assignment for Play isolation.");SceneDocument runtime(project.root);require(runtime.resetTransientFrom(reopened)&&runtime.transient()&&!runtime.dirty(),"Play copy did not retain a clean isolated Ragdoll setup.");
    auto runtimeEdit=*runtime.scene().find(entity);runtimeEdit.modelRagdoll={};require(runtime.updateEntity(entity,runtimeEdit)&&reopened.scene().find(entity)->modelRagdoll==changed.modelRagdoll&&!runtime.save(),"Play Ragdoll edit escaped into authored data or saved transient state.");
}
void strictOptionalSettings(){
    Project project;SceneDocument document(project.root);const auto entity=document.createModel(newAssetId(),"Enemy");auto record=*document.scene().find(entity);record.modelRagdoll.asset.id=newAssetId();require(document.updateEntity(entity,record)&&document.save("Strict ragdoll"),"Could not prepare optional settings fixture.");
    const auto file=sceneAssetPath(project.root,document.assetId());const auto valid=Json::parse(read(file));
    const auto malformed=[&](const auto& change){auto data=valid;for(auto& value:data.at("entities"))if(value.at("name")=="Enemy")change(value);writeFileAtomically(file,data.dump());SceneDocument invalid(project.root);return !invalid.load(document.assetId());};
    require(malformed([](auto& value){value["modelRagdoll"]=false;}),"Nonobject Ragdoll settings were admitted.");
    for(const auto& value:Json::array({1,"true",nullptr}))require(malformed([&](auto& entity){entity["modelRagdoll"]["startSimulated"]=value;}),"Nonboolean Start Simulated was admitted.");
    require(malformed([](auto& value){value["modelRagdoll"]["asset"]="arbitrary path";}),"New Ragdoll field silently converted a malformed asset UUID.");
    require(malformed([](auto& value){value["modelRagdoll"]["asset"]="";value["modelRagdoll"]["startSimulated"]=true;}),"Start Simulated accepted no setup.");
    require(malformed([](auto& value){value.erase("model");}),"Ragdoll settings were admitted without a model.");writeFileAtomically(file,valid.dump());SceneDocument reopened(project.root);require(reopened.load(document.assetId()),"Corrected optional settings did not reopen.");
    auto defaults=valid;for(auto& value:defaults.at("entities"))value.erase("modelRagdoll");writeFileAtomically(file,defaults.dump());require(reopened.load(document.assetId())&&reopened.scene().find(entity)->modelRagdoll==SceneModelRagdoll{}&&!reopened.dirty(),"Older scenes acquired a Ragdoll or dirty state when optional settings were absent.");
}
}
int main(){try{persistenceAndHistory();strictOptionalSettings();std::cout<<"Optional Ragdoll scene persistence, strict settings, undo/history, extraction and Play-copy isolation passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
