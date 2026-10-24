#include "scene/SceneDocument.h"
#include "scene/SceneComponents.h"
#include "scene/BuiltinModels.h"
#include "assets/AtomicFile.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>

using namespace crimson;
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
std::string read(const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);return {std::istreambuf_iterator<char>(stream),{}};}
struct Project {std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-paint-scene-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));Project(){std::filesystem::create_directories(root);}~Project(){std::error_code ignored;std::filesystem::remove_all(root,ignored);}};
void check(){Project project;SceneDocument doc(project.root);const auto id=doc.createModel(BuiltinBoxModelId,"Painted Box");require(doc.save("Paint scene"),doc.status().c_str());const auto file=sceneAssetPath(project.root,doc.assetId());const auto initial=nlohmann::json::parse(read(file));require(initial.at("version")==25&&!initial.at("entities").front().contains("meshPaint"),"Default scenes gained paint data or wrong schema.");
    require(addSceneComponent(doc,id,SceneComponentKind::Subdivision),"Could not add Subdivision.");const auto base=makeMeshPaintPayload(4);const MeshPaintChange change{1,{80,50,20}};const auto edited=updateMeshPaintPayload(base,std::span{&change,1});
    const auto assign=[&](auto& records,auto&){for(auto& record:records)if(record.id==id)record.meshPaint->draws={{0,0,0,123,edited}};};
    require(doc.begin("Paint stroke")&&doc.update(assign),"Could not start paint stroke.");doc.cancel();require(doc.scene().find(id)->meshPaint->draws.empty(),"Cancel retained painted weights.");
    require(doc.edit("Paint stroke",assign)&&doc.undo()&&doc.scene().find(id)->meshPaint->draws.empty()&&doc.redo(),"Paint undo/redo failed.");
    require(doc.save(),doc.status().c_str());const auto saved=read(file);SceneDocument loaded(project.root);require(loaded.load(doc.assetId())&&!loaded.dirty()&&read(file)==saved,"Loading paint changed the file or dirty state.");require(loaded.scene().find(id)->meshPaint==doc.scene().find(id)->meshPaint,"RGB8 persistence changed paint.");
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(loaded),"Play copy failed.");require(runtime.edit("Runtime paint",[&](auto& records,auto&){for(auto& record:records)if(record.id==id)record.meshPaint->draws.clear();})&&!loaded.scene().find(id)->meshPaint->draws.empty(),"Play paint escaped into authored scene.");
    auto invalid=nlohmann::json::parse(saved);invalid["entities"][0]["meshPaint"]["draws"][0]["rgb8"]="AAAAAAAAAAAAAAAA"; // correct12bytes, allBase is valid
    invalid["entities"][0]["meshPaint"]["draws"][0]["vertices"]=5;writeFileAtomically(file,invalid.dump());SceneDocument bad(project.root);require(!bad.load(doc.assetId()),"Wrong RGB8 byte count was admitted.");
    invalid=nlohmann::json::parse(saved);invalid["entities"][0]["meshPaint"]["draws"][0]["rgb8"]="////////////////";writeFileAtomically(file,invalid.dump());require(!bad.load(doc.assetId()),"Weights summing above one were admitted.");
}
}
int main(){try{check();std::cout<<"Mesh paint scene checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
