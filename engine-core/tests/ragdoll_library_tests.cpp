#include "ragdoll_fixture.h"
#include "assets/AssetLibrary.h"
#include "assets/AtomicFile.h"
#include "scene/SceneDocument.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
using namespace crimson;using namespace crimson::ragdoll_test;using Json=nlohmann::json;
std::string read(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
std::filesystem::path writeSource(const std::filesystem::path& folder,const std::string& jointName="End"){
    std::filesystem::create_directories(folder);std::vector<std::uint8_t> bytes;
    const auto append=[&](const auto& values){const auto* first=reinterpret_cast<const std::uint8_t*>(values.data());bytes.insert(bytes.end(),first,first+sizeof(values));};
    append(std::array<float,9>{0,0,0,.2f,0,0,0,.5f,0});append(std::array<std::uint8_t,12>{0,0,0,0,0,0,0,0,1,0,0,0});append(std::array<float,12>{1,0,0,0,1,0,0,0,1,0,0,0});
    std::ofstream buffer(folder/"rig.bin",std::ios::binary);buffer.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));buffer.close();
    const Json gltf{{"asset",{{"version","2.0"}}},{"scene",0},{"scenes",Json::array({{{"nodes",{0,2}}}})},
        {"nodes",Json::array({{{"name","Base"},{"children",{1}}},{{"name",jointName},{"translation",{0,.5,0}}},{{"name","Surface"},{"mesh",0},{"skin",0}}})},
        {"skins",Json::array({{{"name","Rig"},{"joints",{0,1}},{"skeleton",0}}})},
        {"meshes",Json::array({{{"primitives",Json::array({{{"attributes",{{"POSITION",0},{"JOINTS_0",1},{"WEIGHTS_0",2}}}}})}}})},
        {"buffers",Json::array({{{"uri","rig.bin"},{"byteLength",bytes.size()}}})},
        {"bufferViews",Json::array({{{"buffer",0},{"byteOffset",0},{"byteLength",36}},{{"buffer",0},{"byteOffset",36},{"byteLength",12}},{{"buffer",0},{"byteOffset",48},{"byteLength",48}}})},
        {"accessors",Json::array({{{"bufferView",0},{"componentType",5126},{"count",3},{"type","VEC3"},{"min",{0,0,0}},{"max",{.2,.5,0}}},
            {{"bufferView",1},{"componentType",5121},{"count",3},{"type","VEC4"}},{{"bufferView",2},{"componentType",5126},{"count",3},{"type","VEC4"}}})}};
    writeFileAtomically(folder/"rig.gltf",gltf.dump());return folder/"rig.gltf";
}
bool has(const AssetLibrary& library,const std::string& id){return std::any_of(library.catalog().begin(),library.catalog().end(),[&](const auto& value){return value.id==id&&value.kind==AssetKind::Ragdoll;});}
std::shared_ptr<const ModelAsset> waitModel(AssetLibrary& library,ResourceManager& resources,const std::string& id){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
    while(std::chrono::steady_clock::now()<deadline){resources.pump();library.updateResources(0);if(const auto model=library.findModel(id))return model;std::this_thread::sleep_for(std::chrono::milliseconds{1});}
    throw std::runtime_error("Ragdoll source metadata did not finish preparing: "+library.modelDiagnostic(id));
}
void finish(AssetLibrary& library,ResourceManager& resources,const AssetDeletionPlan& plan,const std::unordered_map<std::string,std::string>& replacements,SceneDocument& active){
    require(plan.error.empty(),plan.error.c_str());if(library.deleteAssets(plan,replacements,&active))return;require(library.deletionPending(),library.status().c_str());
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{20};while(std::chrono::steady_clock::now()<deadline){resources.pump();if(library.pumpDeletion(&active))return;require(library.deletionPending(),library.status().c_str());std::this_thread::sleep_for(std::chrono::milliseconds{1});}throw std::runtime_error("Ragdoll deletion did not finish.");
}
void publicationAndReferences(){
    Project project;ResourceManager resources;AssetLibrary library(project.root,&resources);const auto imported=library.importModel(writeSource(project.root/"source"));require(bool(imported),library.status().c_str());const auto model=waitModel(library,resources,imported->id);
    const auto folder=library.browserCatalog().createFolder(BrowserCatalog::RootFolder,"Enemy physics");const auto created=library.createRagdoll(model->id,"Enemy ragdoll",folder);
    require(created&&created->bodies.empty()&&has(library,created->id)&&library.findRagdoll(created->id)&&library.browserCatalog().parent(created->id)==folder,"Native Ragdoll creation lost its empty setup, catalog kind or destination folder.");
    const auto items=library.browserCatalog().items(folder,{},BrowserAssetFilter::Ragdolls);require(items.size()==1&&items.front().id==created->id,"Ragdoll type filter omitted the native asset.");
    auto& browser=library.browserCatalog();const auto blocked=browser.createFolder(BrowserCatalog::RootFolder,"Blocked destination");const auto blockedPath=browser.folderPath(blocked);
    require(blockedPath.parent_path()==project.root/"Content"&&std::filesystem::remove(blockedPath),"Could not prepare a disposable blocked folder.");writeFileAtomically(blockedPath,"Occupied by a file");
    const auto catalogSize=library.catalog().size();const auto locationBytes=read(project.root/".crimson/asset-locations.json");
    rejects([&]{library.createRagdoll(model->id,"Unpublished setup",blocked);},"Ragdoll creation accepted a folder that became a file.");
    require(library.catalog().size()==catalogSize&&read(project.root/".crimson/asset-locations.json")==locationBytes,"Failed folder publication left an orphan Ragdoll asset or identity registration.");
    auto authored=*created;generateRagdollBodies(authored,*model);saveRagdollAsset(project.root,authored);library.publishRagdoll(std::make_shared<const RagdollAsset>(authored));const auto rev=library.ragdollRevision(authored.id),catalog=library.catalogRevision();
    library.publishRagdoll(std::make_shared<const RagdollAsset>(authored));require(library.ragdollRevision(authored.id)==rev&&library.catalogRevision()==catalog,"Publishing an equal Ragdoll changed dependent identities.");
    auto preview=authored;preview.bodies[0].mass=8;library.protectRagdollDocument(authored.id);library.publishRagdoll(std::make_shared<const RagdollAsset>(preview));library.onFileChanged(ragdollAssetPath(project.root,authored.id));
    require(*library.findRagdoll(authored.id)==preview,"Disk notification replaced the open Ragdoll document's committed authority.");library.protectRagdollDocument({});library.refreshRagdolls();require(*library.findRagdoll(authored.id)==authored,"Closing authoring authority did not resume disk publication.");
    const auto file=ragdollAssetPath(project.root,authored.id);const auto bytes=read(file);writeFileAtomically(file,"{broken rig");library.onFileChanged(file);require(!library.findRagdoll(authored.id)&&!library.ragdollDiagnostic(authored.id).empty(),"Malformed external Ragdoll remained usable without a diagnostic.");writeFileAtomically(file,bytes);library.onFileChanged(file);require(bool(library.findRagdoll(authored.id)),"Corrected external Ragdoll failed to republish.");
    const auto copy=library.copyRagdoll(authored.id,"Alternative setup");const auto otherImported=library.importModel(writeSource(project.root/"other-source","Different end"));require(bool(otherImported),library.status().c_str());const auto otherModel=waitModel(library,resources,otherImported->id);const auto incompatible=library.createRagdoll(otherModel->id,"Other skeleton");
    const auto candidates=library.fallbackCandidates(authored.id);require(std::any_of(candidates.begin(),candidates.end(),[&](const auto& v){return v.id==copy->id;})&&std::none_of(candidates.begin(),candidates.end(),[&](const auto& v){return v.id==incompatible->id;}),"Ragdoll fallbacks did not enforce exact source/skeleton compatibility.");
    rejects([&]{library.references().resolveRagdoll(model->id);},"Typed Ragdoll lookup accepted a model.");AssetReferenceManager policy(project.root);const std::array removed{AssetEntry{authored.id,authored.name,AssetKind::Ragdoll}};
    rejects([&]{policy.replace(removed,{{authored.id,incompatible->id}});},"Persistent replacement policy accepted another skeleton's Ragdoll.");
    SceneDocument active(project.root);const auto entity=active.createModel(model->id,"Enemy");auto record=*active.scene().find(entity);record.modelRagdoll.asset.id=authored.id;record.modelRagdoll.startSimulated=true;require(active.updateEntity(entity,record)&&active.save("Ragdoll scene"),active.status().c_str());library.refreshScenes();
    require(!library.planDeletion({model->id},&active).error.empty(),"Deleting a source model orphaned Ragdoll assets.");
    auto deletion=library.planDeletion({authored.id},&active);require(deletion.error.empty()&&deletion.needsReferenceWarning()&&deletion.clearsActiveHistory,"Ragdoll deletion omitted saved usage or active history impact.");
    finish(library,resources,deletion,{{authored.id,copy->id}},active);require(active.scene().find(entity)->modelRagdoll.asset.id==copy->id&&active.scene().find(entity)->modelRagdoll.startSimulated&&active.history().size()==0,"Compatible Ragdoll replacement lost start mode or retained stale history.");
    require(!has(library,authored.id)&&library.references().resolveRagdoll(authored.id)==copy->id,"Deleted Ragdoll remained in the catalog or lost typed fallback.");
    const auto saved=readSceneSnapshot(project.root,active.assetId());const auto savedRecord=std::find_if(saved.records.begin(),saved.records.end(),[&](const auto& value){return value.id==entity;});require(savedRecord!=saved.records.end()&&savedRecord->modelRagdoll.asset.id==copy->id,"Saved scene retained a deleted Ragdoll.");
    library.protectRagdollDocument(copy->id);deletion=library.planDeletion({copy->id},&active);require(!library.deleteAssets(deletion,{},&active)&&!library.deletionPending(),"An open Ragdoll document did not protect its source asset.");library.protectRagdollDocument({});
    library.setRagdollDocumentReferences({model->id},{});require(!library.planDeletion({model->id,copy->id},&active).error.empty(),"Open Ragdoll source dependency was not protected.");library.setRagdollDocumentReferences({},{model->id});require(!library.planDeletion({model->id,copy->id},&active).error.empty(),"Ragdoll undo-history source dependency was not protected.");library.setRagdollDocumentReferences({},{});
    deletion=library.planDeletion({copy->id},&active);finish(library,resources,deletion,{},active);require(active.scene().find(entity)->modelRagdoll.asset.empty()&&!active.scene().find(entity)->modelRagdoll.startSimulated,"Deleting the last Ragdoll left an invalid simulated scene record.");
    deletion=library.planDeletion({model->id},&active);require(deletion.error.empty(),"Deleted Ragdoll still retained its former source model.");resources.stop();
}
}
int main(){try{publicationAndReferences();std::cout<<"Ragdoll library creation, folders, publication, typed references, source protection and transactional scene deletion passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
