#include "editor/CollisionLayersDocument.h"
#include "assets/AtomicFile.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
using Json=nlohmann::json;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::string read(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-collision-names-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);}
    ~Project(){std::error_code error;const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(error)return;
        const auto target=std::filesystem::weakly_canonical(root,error);if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("crimson-collision-names-"))std::filesystem::remove_all(target,error);}
};
void defaultsHistoryAndSave() {
    Project project;CollisionLayersDocument document(project.root);
    require(document.names().size()==32&&document.name(0)=="World"&&document.name(1)=="Player"&&document.name(2)=="Enemy"&&document.name(3)=="Trigger"&&document.name(4)=="Character"&&document.name(31)=="Layer 32","Collision layer defaults or fixed-slot fallback were wrong.");
    require(!document.dirty()&&!document.canUndo()&&!std::filesystem::exists(document.filePath())&&!std::filesystem::exists(project.root/".crimson"),"Opening default collision layer names wrote project data or marked it dirty.");
    const auto committed=document.committed();const auto revision=document.revision();
    require(document.begin()&&document.updateName(0,"Terrain")&&document.name(0)=="Terrain"&&document.committed()==committed,"Layer gesture did not expose working names independently of publication.");
    document.cancel();require(document.name(0)=="World"&&!document.dirty()&&document.revision()==revision,"Canceling a name gesture changed committed names or dirty state.");
    require(document.begin("Rename collision layers")&&document.updateName(0,"Terrain")&&document.updateName(31,"Last layer")&&document.commit(),document.status());
    require(document.dirty()&&document.undoLabel()=="Rename collision layers"&&document.committed()->at(0)=="Terrain","Collision name transaction did not publish one complete history state.");
    require(document.undo()&&document.name(0)=="World"&&document.name(31)=="Layer 32"&&!document.dirty()&&document.redo()&&document.name(31)=="Last layer","Collision name undo/redo lost fixed slots or gesture grouping.");
    require(document.save(),document.status());const auto contents=read(document.filePath());const auto json=Json::parse(contents);
    require(json.at("version")==1&&json.at("names").size()==32&&!document.dirty(),"Collision names did not save the complete current format.");
    require(document.undo()&&document.dirty()&&document.redo()&&!document.dirty(),"Saved name state did not participate in history dirty tracking.");
    CollisionLayersDocument reopened(project.root);require(reopened.names()==document.names()&&!reopened.dirty()&&read(reopened.filePath())==contents,"Reopening layer names lost data or rewrote the file.");
    require(reopened.setName(31,"")&&reopened.name(31)=="Layer 32"&&reopened.undo()&&reopened.name(31)=="Last layer","Clearing a label lost its numbered identity or undo state.");
    require(!reopened.setName(32,"Outside")&&!reopened.setName(5,std::string(129,'x'))&&!reopened.setName(5,"Line\nBreak")&&!reopened.dirty(),"Invalid collision layer label changed the document.");
    require(reopened.begin()&&reopened.updateName(5,"Working")&&!reopened.updateName(6,"Invalid\nName")&&!reopened.commit(),"An invalid name gesture partially committed.");
    reopened.cancel();require(reopened.names()==document.names()&&!reopened.dirty(),"Canceling an invalid gesture discarded the last saved names.");
}
void externalConflictsAndFailures() {
    Project project;CollisionLayersDocument document(project.root);require(document.setName(2,"Monsters")&&document.save(),document.status());
    require(document.setName(4,"Unsaved actors"),document.status());const auto working=document.names();
    auto external=defaultCollisionLayerNames();external[0]="External World";const auto externalText=Json{{"version",1},{"names",external}}.dump();writeFileAtomically(document.filePath(),externalText);
    require(document.externalFileChanged()&&document.checkExternalConflict()&&document.names()==working&&!document.reload()&&!document.save()&&read(document.filePath())==externalText,"External name changes silently replaced or overwrote unsaved edits.");
    require(document.reload(true)&&document.names()==external&&!document.dirty()&&!document.canUndo(),document.status());
    require(document.setName(0,"Local World"),document.status());const auto local=document.names();
    writeFileAtomically(document.filePath(),"{broken");require(!document.reload(true)&&document.names()==local&&document.dirty()&&!document.save(),"Malformed external names discarded the working document.");
    require(document.save(true)&&!document.dirty()&&Json::parse(read(document.filePath())).at("names").get<CollisionLayerNames>()==local,"Explicit conflict overwrite failed to preserve working names.");
    auto malformed=Json{{"version",1},{"names",local}};malformed["names"].erase(malformed["names"].end()-1);writeFileAtomically(document.filePath(),malformed.dump());
    require(!document.reload(true)&&document.names()==local,"A non-32-slot registry was accepted or partially loaded.");
    std::filesystem::remove(document.filePath());require(document.externalFileChanged()&&document.reload(true)&&document.names()==defaultCollisionLayerNames()&&!document.dirty()&&!std::filesystem::exists(document.filePath()),"Reloading a removed names file failed to restore unsaved-free defaults without writes.");

    Project blocked;writeFileAtomically(blocked.root/".crimson","not a directory");CollisionLayersDocument cannotSave(blocked.root);
    require(cannotSave.setName(5,"Keep this edit")&&!cannotSave.save()&&cannotSave.dirty()&&cannotSave.name(5)=="Keep this edit","Failed name-file publication discarded its unsaved definition.");
}
}
int main(){try{defaultsHistoryAndSave();externalConflictsAndFailures();std::cout<<"Collision layer document checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
