#include "editor/NavigationAgentProfileDocument.h"
#include "assets/ContentStorage.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-profile-doc-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);initializeContentStorage(root);}
    ~Project(){std::error_code error;const auto path=std::filesystem::weakly_canonical(root,error),temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error && path.parent_path()==temp && path.filename().string().starts_with("crimson-profile-doc-"))std::filesystem::remove_all(path,error);}
};
void historyAndPublication(const std::filesystem::path& root) {
    NavigationAgentProfileDocument document(root);require(document.create("Shared Enemy"),"Profile creation failed.");
    const auto original=document.committed();require(!document.dirty() && document.editable(),"New profile was not saved and editable.");
    require(document.begin("Change clearance") && document.update([](auto& profile){profile.traversal.radius=.45f;}),"Property gesture failed.");
    require(document.committed()==original && document.definition().traversal.radius==.45f,"Uncommitted profile was published to asset users.");
    document.cancel();require(document.committed()==original && !document.dirty(),"Cancel changed the shared snapshot or dirty state.");
    require(document.edit("Change clearance",[](auto& profile){profile.traversal.radius=.45f;}),"Profile commit failed.");
    require(document.committed()!=original && document.dirty() && document.canUndo(),"Committed generation or history was not published.");
    require(document.undo() && document.definition().traversal==original->traversal && !document.dirty(),"Undo did not restore the saved shared settings.");
    require(document.redo() && document.save(),"Redo/Save failed.");
    const auto source=document.assetId();const auto disk=loadNavigationAgentProfile(root,source);
    require(disk.traversal.radius==.45f && !document.dirty(),"Save did not persist the edited radius.");
    require(document.save("Enemy Copy",true) && document.assetId()!=source,"Save As reused its source identity.");
    const auto copy=document.assetId();require(document.undo() && document.assetId()==copy,"Undo after Save As restored the source identity.");
    require(loadNavigationAgentProfile(root,source)==disk,"Save As modified its source profile.");
    require(document.begin("Invalid height") && !document.update([](auto& profile){profile.traversal.height=.01f;}),"Invalid capsule height was accepted.");
    require(!document.commit(),"An invalid property gesture was committed.");document.cancel();
    auto external=loadNavigationAgentProfile(root,copy);external.traversal.maxStep=.6f;saveNavigationAgentProfile(root,external);
    require(document.externalFileChanged(),"An external profile modification was not detected.");
    require(document.load(copy) && document.definition().traversal.maxStep==.6f,"External reload lost authored traversal settings.");
}
void defaultAndStaging(const std::filesystem::path& root) {
    NavigationAgentProfileDocument document(root);require(document.load(DefaultNavigationAgentProfileId),"Default Agent did not open.");
    require(!document.editable() && !document.begin("Modify default") && !document.save(),"Protected default became editable.");
    require(document.save("My Default",true) && document.editable(),"Default Agent Save As failed.");
    NavigationAgentProfile seed{newAssetId(),"Migrated Custom",{2.4f,.55f,45.f,.3f}};
    require(document.loadSnapshot(seed) && document.dirty() && !document.externalFileChanged(),"An unmaterialized migration seed did not open without a file.");
    require(document.edit("Edit migration",[](auto& profile){profile.traversal.maxSlope=32.f;}) && document.save(),"Saving an edited migration seed failed.");
    const auto saved=loadNavigationAgentProfile(root,seed.id);require(saved.traversal.maxSlope==32.f && saved.id==seed.id,"Seed Save changed its UUID or lost the edit.");
    seed.id=newAssetId();require(document.loadSnapshot(seed),"Second staged profile did not open.");
    auto materialized=seed;materialized.traversal.maxStep=.7f;saveNavigationAgentProfile(root,materialized);
    require(document.externalFileChanged(),"An independently edited materialization was masked by its open migration seed.");
}
void damagedSourceSaveAs(const std::filesystem::path& root) {
    NavigationAgentProfileDocument document(root);require(document.create("Damaged Source"),"Could not create the damaged-source fixture.");
    const auto original=document.savedDefinition();
    require(document.edit("Change slope",[](auto& profile){profile.traversal.maxSlope=20.f;}),"Could not edit the damaged-source fixture.");
    require(document.savedDefinition()==original,"Unsaved edits replaced the source's saved generation.");
    {std::ofstream output(navigationAgentProfilePath(root,original.id),std::ios::binary|std::ios::trunc);output<<"damaged profile";}
    require(document.externalFileChanged(),"A damaged source file was not detected.");
    // Save As needs this captured generation to restore shared source users
    // when the old file cannot be reloaded after switching document identity.
    const auto restore=document.savedDefinition();
    require(document.save("Recovered Copy",true),"Save As could not recover edits from a damaged source.");
    require(restore.id==original.id && restore.traversal.maxSlope==45.f &&
        document.savedDefinition().id!=restore.id && document.savedDefinition().traversal.maxSlope==20.f,
        "Save As lost the old saved generation or used it for the edited copy.");
}
}
int main(){try{Project project;historyAndPublication(project.root);defaultAndStaging(project.root);damagedSourceSaveAs(project.root);std::cout<<"Navigation profile document history, publication, conflicts, defaults and staged Save passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
