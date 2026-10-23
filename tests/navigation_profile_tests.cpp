#include "assets/NavigationAgentProfile.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "scene/SceneComponents.h"
#include "scene/SceneExtraction.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using namespace crimson;
using Json=nlohmann::json;
void require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
template<class Action> void rejects(Action action,const char* message){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}require(rejected,message);}
std::string read(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-agent-profile-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root/"assets/scenes");}
    ~Project(){std::error_code error;const auto target=std::filesystem::weakly_canonical(root,error);const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("crimson-agent-profile-"))std::filesystem::remove_all(target,error);}
};
void profileStorage() {
    Project project;auto profile=createNavigationAgentProfile(project.root,"Large creature");
    require(isAssetUuid(profile.id)&&!isEngineAssetId(profile.id),"New profile did not receive a managed identity.");
    profile.traversal={2.4f,.5f,35,.2f};saveNavigationAgentProfile(project.root,profile);
    require(loadNavigationAgentProfile(project.root,profile.id)==profile,"Profile fields did not round-trip.");
    auto copy=copyNavigationAgentProfile(project.root,profile,"Large creature copy");
    require(copy.id!=profile.id&&copy.traversal==profile.traversal,"Save As changed traversal or reused the original identity.");
    rejects([&]{saveNavigationAgentProfile(project.root,*defaultNavigationAgentProfile());},"Default profile was editable.");
    auto invalid=profile;invalid.traversal.radius=2;rejects([&]{validateNavigationAgentProfile(invalid);},"Invalid capsule dimensions were accepted.");
    invalid=profile;invalid.traversal.maxSlope=90;rejects([&]{validateNavigationAgentProfile(invalid);},"A vertical maximum slope was accepted.");
    invalid=profile;invalid.traversal.maxStep=-1;rejects([&]{validateNavigationAgentProfile(invalid);},"Negative step height was accepted.");
    auto wrong=Json::parse(serializeNavigationAgentProfile(profile));wrong["id"]=copy.id;
    rejects([&]{deserializeNavigationAgentProfile(wrong.dump(),profile.id);},"A mismatching expected UUID was ignored.");
    SceneCharacterController controller;require(resolveAgentTraversal(controller)==AgentTraversalSettings{},"Default capsule required a callback.");
    controller.profile=AssetRef<NavigationAgentProfile>{profile.id};
    require(!resolveAgentTraversal(controller),"An unresolved custom profile silently used default dimensions.");
    const NavigationAgentProfileQuery query=[&](const auto& id){return id==profile.id?std::make_shared<const NavigationAgentProfile>(profile):nullptr;};
    require(resolveAgentTraversal(controller,query)==profile.traversal,"Controller did not resolve its one authored profile.");
}
void legacyProfilesAndExplicitVolume() {
    Project project;SceneDocument source(project.root);const auto player=source.createPlayer(),enemy=source.createEnemy();
    auto legacy=Json::parse(serializeSceneSnapshot({{},"Legacy agents",source.scene().records(),source.scene().properties()}));legacy["version"]=17;
    for(auto& entity:legacy["entities"])if(entity.contains("controller")) {
        auto& controller=entity["controller"];controller.erase("profile");controller["height"]=2.4f;controller["radius"]=.4f;
        controller["showCollisionInGame"]=true;controller["collisionColor"]={.2,.4,.6,.8};
    }
    const std::string id="assets/scenes/legacy-agents.crimson-scene",bytes=legacy.dump(2);writeFileAtomically(project.root/id,bytes);
    SceneDocument document(project.root);require(document.load(id),document.status());
    require(!document.dirty()&&read(project.root/id)==bytes&&!std::filesystem::exists(project.root/"Content"),"Opening a legacy scene wrote files or marked it dirty.");
    const auto first=*document.scene().find(player),second=*document.scene().find(enemy);
    require(first.controller->profile!=second.controller->profile,"Independent legacy actors acquired implicit profile sharing.");
    const auto definition=document.navigationAgentProfile(first.controller->profile.id);
    require(definition&&definition->traversal==AgentTraversalSettings{2.4f,.4f,45,.3f},"Legacy custom dimensions did not survive conversion.");
    require(first.controller->showCollisionInGame&&first.controller->collisionColor==Vec4{.2f,.4f,.6f,.8f},"Migration lost collision display settings.");
    const auto migratedRecords=document.scene().records();
    require(std::none_of(migratedRecords.begin(),migratedRecords.end(),[](const auto& record){return record.navigationVolume.has_value();}),"Opening an old scene inserted a Navigation Volume.");
    const auto history=document.history().size(),volume=document.createNavigationVolume();
    require(volume&&document.history().size()==history+1&&document.scene().find(volume)->navigationVolume->profiles.size()==2,"Volume creation did not collect unique existing actor profiles.");
    require(document.createNavigationVolume()==volume&&document.history().size()==history+1,"A second active volume was created.");
    require(document.undo()&&!document.scene().find(volume)&&document.redo(),"Navigation Volume creation did not undo/redo atomically.");
    SceneDocument play(project.root);require(play.resetTransientFrom(document)&&play.navigationAgentProfile(first.controller->profile.id),"Play lost unsaved converted profile definitions.");
    require(!document.scene().find(player)->navigation,"First-person Player unexpectedly requires a Navigation Agent.");
    require(removeSceneComponent(document,enemy,SceneComponentKind::Navigation),"Cannot remove the Enemy Navigation Agent.");
    require(document.scene().find(enemy)->controller->profile==second.controller->profile&&
        document.scene().find(player)->controller->profile==first.controller->profile,"Removing Navigation changed a physical capsule profile.");
    require(document.save(),document.status());
    const auto materialized=loadNavigationAgentProfile(project.root,first.controller->profile.id);
    require(materialized.traversal==definition->traversal&&!document.navigationAgentProfile(first.controller->profile.id),"Scene Save did not materialize/retire its staged definition.");
    const auto saved=Json::parse(read(sceneAssetPath(project.root,document.assetId())));require(saved.at("version")==24,"Profile scene did not write schema 24.");
    for(const auto& entity:saved["entities"])if(entity.contains("controller"))require(entity["controller"].contains("profile")&&!entity["controller"].contains("height")&&!entity["controller"].contains("radius"),"Current scene wrote obsolete inline capsule dimensions.");
    SceneDocument reopened(project.root);require(reopened.load(document.assetId())&&reopened.stagedNavigationAgentProfiles().empty(),"Current scene recreated legacy conversion state.");
    auto edited=materialized;edited.traversal.height=3;saveNavigationAgentProfile(project.root,edited);
    std::filesystem::remove(navigationAgentProfilePath(project.root,edited.id));
    require(!play.navigationAgentProfile(edited.id),"Missing indexed profile resurrected an old staged definition.");
}
void profileSavedBeforeScene() {
    Project project;initializeContentStorage(project.root);SceneDocument source(project.root);source.createEnemy();
    auto legacy=Json::parse(serializeSceneSnapshot({{},"Pending custom agent",source.scene().records(),source.scene().properties()}));legacy["version"]=17;
    auto& controller=legacy["entities"][0]["controller"];controller.erase("profile");controller["height"]=2.2f;controller["radius"]=.4f;
    const auto id=newAssetId();legacy["id"]=id;const auto file=project.root/"Content"/"Pending Agent.crimson-scene";
    writeFileAtomically(file,legacy.dump());registerAssetPath(project.root,id,file);
    SceneDocument document(project.root);require(document.load(id),document.status());
    const auto reference=document.scene().records().front().controller->profile.id;auto profile=*document.navigationAgentProfile(reference);
    saveNavigationAgentProfile(project.root,profile);profile.traversal.height=3.2f;saveNavigationAgentProfile(project.root,profile);
    require(!document.navigationAgentProfile(reference),"Materialized profile remained shadowed by a conversion seed.");
    require(document.save(),document.status());require(loadNavigationAgentProfile(project.root,reference).traversal.height==3.2f,"Scene Save overwrote independent profile edits with migration data.");
}
}
int main(){try{profileStorage();legacyProfilesAndExplicitVolume();profileSavedBeforeScene();std::cout<<"Navigation profile tests passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
