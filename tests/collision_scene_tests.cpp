#include "scene/SceneComponents.h"
#include "assets/AtomicFile.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace crimson;
using Json=nlohmann::json;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::string read(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-collision-scene-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root/"assets/scenes");}
    ~Project(){std::error_code error;const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(error)return;
        const auto target=std::filesystem::weakly_canonical(root,error);if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("crimson-collision-scene-"))std::filesystem::remove_all(target,error);}
};
void roundTripHistoryAndMigration() {
    Project project;SceneDocument document(project.root);const auto player=document.createPlayer(),enemy=document.createEnemy(),generic=document.createEmpty("Generic actor");
    require(addSceneComponent(document,generic,SceneComponentKind::Motor),document.status());const auto solid=document.createCollider(),trigger=document.createCollider("Actor trigger",player);
    require(document.scene().find(player)->controller->collision==CollisionFilter{PlayerCollisionLayer,AllCollisionLayers}&&document.scene().find(enemy)->controller->collision.layers==EnemyCollisionLayer&&document.scene().find(generic)->controller->collision.layers==CharacterCollisionLayer,"Actor recipes did not assign their collision layers.");
    auto sensor=*document.scene().find(trigger);sensor.collider->mode=ColliderMode::Trigger;sensor.collider->shape=ColliderShape::Sphere;sensor.collider->collision={TriggerCollisionLayer|(1u<<31),EnemyCollisionLayer};
    const auto history=document.history().size();require(document.updateEntity(trigger,sensor)&&document.history().size()==history+1&&document.undo()&&document.scene().find(trigger)->collider->mode==ColliderMode::Solid&&document.redo()&&document.scene().find(trigger)->collider==sensor.collider,"Collider mode and bit-31 filters did not participate in scene history.");
    auto actor=*document.scene().find(enemy);actor.controller->collision={EnemyCollisionLayer|(1u<<31),WorldCollisionLayer|TriggerCollisionLayer};require(document.updateEntity(enemy,actor),document.status());
    const auto profile=actor.controller->profile;require(removeSceneComponent(document,enemy,SceneComponentKind::Navigation)&&document.scene().find(enemy)->controller->collision==actor.controller->collision&&document.scene().find(enemy)->controller->profile==profile,"Removing Navigation changed collision policy or traversal profile.");require(document.undo(),document.status());
    document.select({player},player);require(document.duplicateSelection(),document.status());const auto duplicate=document.selection().primary;
    require(document.scene().find(duplicate)->controller->collision==document.scene().find(player)->controller->collision&&document.scene().children(duplicate).size()==2,"Duplicating a controller lost its filter or trigger child.");
    bool copiedTrigger=false;for(const auto child:document.scene().children(duplicate))if(const auto record=document.scene().find(child);record->collider){copiedTrigger=record->collider==sensor.collider;}
    require(copiedTrigger&&document.undo(),"Duplicating a trigger child lost its mode or collision bits.");
    SceneDocument play(project.root);require(play.resetTransientFrom(document)&&play.scene().find(trigger)->collider==sensor.collider&&play.scene().find(enemy)->controller->collision==actor.controller->collision,"Play clone lost collision definitions.");
    require(document.save("Collision definitions"),document.status());const auto file=sceneAssetPath(project.root,document.assetId());const auto bytes=read(file);auto json=Json::parse(bytes);
    require(json.at("version")==23,"Collision scene did not write schema 23.");SceneDocument reopened(project.root);require(reopened.load(document.assetId())&&reopened.scene().find(trigger)->collider==sensor.collider&&reopened.scene().find(enemy)->controller->collision==actor.controller->collision&&!reopened.dirty()&&read(file)==bytes,"Collision mode/filter round trip changed bit 31, wrote during load or dirtied the document.");
    json["version"]=19;for(auto& entity:json["entities"]){if(entity.contains("controller"))entity["controller"].erase("collision");if(entity.contains("collider")){entity["collider"].erase("collision");entity["collider"].erase("mode");}}
    const auto old=json.dump();writeFileAtomically(file,old);SceneDocument legacy(project.root);require(legacy.load(document.assetId()),legacy.status());
    require(legacy.scene().find(player)->controller->collision.layers==PlayerCollisionLayer&&legacy.scene().find(enemy)->controller->collision.layers==EnemyCollisionLayer&&legacy.scene().find(generic)->controller->collision.layers==CharacterCollisionLayer,"Legacy controller migration inferred the wrong layer from its capabilities.");
    require(legacy.scene().find(trigger)->collider->mode==ColliderMode::Solid&&legacy.scene().find(trigger)->collider->collision==CollisionFilter{WorldCollisionLayer,AllCollisionLayers}&&legacy.scene().find(solid)->collider->collision==CollisionFilter{WorldCollisionLayer,AllCollisionLayers},"Legacy Colliders changed to sensors or lost shared World blocking.");
    require(!legacy.dirty()&&read(file)==old,"Legacy collision conversion wrote on load or marked the scene dirty.");
    require(legacy.save(),legacy.status());require(Json::parse(read(sceneAssetPath(project.root,legacy.assetId()))).at("version")==23,"Explicit save retained the old collision schema.");
}
void validationAndQuaderRetention() {
    Project project;SceneDocument document(project.root);const auto enemy=document.createEnemy(),id=document.createCollider();const auto body=*document.scene().find(enemy),before=*document.scene().find(id);
    auto invalid=body;invalid.controller->collision.layers|=WorldCollisionLayer;require(!document.updateEntity(enemy,invalid),"Controller was allowed to belong to World.");invalid=body;invalid.controller->collision.mask&=~WorldCollisionLayer;require(!document.updateEntity(enemy,invalid),"Controller could disable shared World blocking.");
    invalid=before;invalid.collider->collision.layers=EnemyCollisionLayer;require(!document.updateEntity(id,invalid),"Solid Collider could leave World.");invalid=before;invalid.collider->collision.mask=EnemyCollisionLayer;require(!document.updateEntity(id,invalid),"Solid Collider could select actor-specific blocking.");
    invalid=before;invalid.collider->mode=ColliderMode::Trigger;require(!document.updateEntity(id,invalid),"Trigger could belong to World.");invalid.collider->collision.layers=TriggerCollisionLayer;invalid.collider->shape=ColliderShape::Trimesh;require(!document.updateEntity(id,invalid),"Trigger accepted Trimesh geometry.");
    invalid=before;invalid.collider->mode=static_cast<ColliderMode>(255);require(!document.updateEntity(id,invalid),"Invalid Collider mode was accepted.");
    require(document.save("Collision validation"),document.status());const auto file=sceneAssetPath(project.root,document.assetId());const auto valid=Json::parse(read(file));
    for(const auto bits:std::vector<Json>{-1,std::uint64_t(1)<<32,1.0,false,"1"}) {
        auto malformed=valid;for(auto& entity:malformed["entities"])if(entity.contains("controller"))entity["controller"]["collision"]["layers"]=bits;
        writeFileAtomically(file,malformed.dump());SceneDocument rejected(project.root);require(!rejected.load(document.assetId()),"Collision reader accepted a non-uint32 layers value.");
    }
    const auto owner=document.createQuader();EntityRecord generated;generated.id=1;generated.name="Generated collision";generated.collider.emplace();generated.quaderGenerated=QuaderGenerated{owner,"collision/fixture"};
    require(document.replaceQuaderImport(owner,QuaderLink{},{generated}),document.status());EntityId collider{};for(const auto& record:document.scene().records())if(record.quaderGenerated&&record.quaderGenerated->sourceKey=="collision/fixture")collider=record.id;
    auto authored=*document.scene().find(collider);authored.collider->mode=ColliderMode::Trigger;authored.collider->collision={TriggerCollisionLayer|(1u<<31),PlayerCollisionLayer};require(document.updateEntity(collider,authored),document.status());
    generated.collider->size={2,3,4};require(document.replaceQuaderImport(owner,QuaderLink{},{generated})&&document.scene().find(collider)->collider->mode==ColliderMode::Trigger&&document.scene().find(collider)->collider->collision==authored.collider->collision,"Quader refresh replaced authored Collider mode or filtering.");
}
}
int main(){try{roundTripHistoryAndMigration();validationAndQuaderRetention();std::cout<<"Collision scene checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
