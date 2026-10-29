#include "scene/SceneComponents.h"
#include "assets/AtomicFile.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
using Json=nlohmann::json;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::string read(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-enemy-scene-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root/"assets/scenes");}
    ~Project(){std::error_code error;const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(error)return;
        const auto target=std::filesystem::weakly_canonical(root,error);if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("crimson-enemy-scene-"))std::filesystem::remove_all(target,error);}
};
void recipeDependenciesAndHistory() {
    Project project;SceneDocument document(project.root);const auto enemy=document.createEnemy();
    const auto recipe=*document.scene().find(enemy);
    require(recipe.enemy==SceneEnemy{}&&recipe.actor&&recipe.controller&&recipe.movement&&recipe.navigation,"Enemy recipe omitted its definition or required capabilities.");
    require(!recipe.playerInput&&!recipe.camera&&!recipe.cameraFollow&&document.scene().children(enemy).empty()&&!document.scene().properties().playerCharacter,"Enemy recipe acquired player input or a camera.");
    require(hasSceneComponent(recipe,SceneComponentKind::Enemy)&&editableSceneComponent(SceneComponentKind::Enemy)&&static_cast<unsigned>(SceneComponentKind::Enemy)==18,"Enemy component identity was missing or changed an existing persistent identity.");
    const auto kinds=sceneComponents(recipe);require(std::count(kinds.begin(),kinds.end(),SceneComponentKind::Enemy)==1,"Enemy component enumeration lost or duplicated the definition.");
    const auto bare=document.createEmpty("Composed creature");const auto before=document.history().size();
    require(addSceneComponent(document,bare,SceneComponentKind::Enemy)&&document.history().size()==before+1,"Enemy dependency closure was not one command.");
    auto composed=*document.scene().find(bare);require(composed.enemy&&composed.actor&&composed.controller&&composed.movement&&composed.navigation&&!composed.playerInput,"Adding Enemy omitted dependencies or introduced player input.");
    require(document.undo()&&!document.scene().find(bare)->enemy&&!document.scene().find(bare)->actor&&!document.scene().find(bare)->controller,"Undo did not remove Enemy's implicit dependencies.");
    require(document.redo()&&!addSceneComponent(document,bare,SceneComponentKind::Enemy),"Enemy redo failed or duplicate addition was accepted.");
    composed=*document.scene().find(bare);composed.enemy=SceneEnemy{EnemyBehaviour::RandomMove,7.5f,.25f,2.5f};
    composed.movement->enabled=false;composed.navigation->enabled=false;require(document.updateEntity(bare,composed),document.status());
    require(document.scene().find(bare)->enemy==composed.enemy,"Disabling Motor or Navigation discarded Enemy settings.");
    for(const auto kind:{SceneComponentKind::Actor,SceneComponentKind::Controller,SceneComponentKind::Movement,SceneComponentKind::Navigation}) {
        const auto expectedHistory=document.history().size()+(document.history().canRedo()?0:1);
        require(removeSceneComponent(document,bare,kind)&&!document.scene().find(bare)->enemy&&document.history().size()==expectedHistory,"Removing an Enemy prerequisite retained an invalid Enemy or split history.");
        require(document.undo()&&sameEntity(*document.scene().find(bare),composed),"Undo did not restore the complete tuned Enemy dependency closure.");
    }
    require(removeSceneComponent(document,bare,SceneComponentKind::Enemy),document.status());
    const auto removed=*document.scene().find(bare);require(!removed.enemy&&removed.actor&&removed.controller&&removed.movement&&removed.navigation,"Removing Enemy removed its independently useful capabilities.");
    require(document.undo()&&document.scene().find(bare)->enemy==composed.enemy,"Enemy removal did not undo with its authored settings.");
    require(document.begin("Tune Enemy"),document.status());
    require(document.update([&](auto& records,auto&){for(auto& value:records)if(value.id==bare)value.enemy->radius=12;}),document.status());
    document.cancel();require(document.scene().find(bare)->enemy==composed.enemy,"Canceling an Enemy gesture left a changed definition.");
    auto tuned=composed;tuned.enemy->behaviour=EnemyBehaviour::ChasePlayer;tuned.enemy->radius=3;
    require(document.updateEntity(bare,tuned)&&document.undo()&&document.scene().find(bare)->enemy==composed.enemy&&document.redo()&&document.scene().find(bare)->enemy==tuned.enemy,"Enemy field changes did not participate in history.");
    document.select({bare},bare);require(document.duplicateSelection(),document.status());const auto duplicate=document.selection().primary;
    require(duplicate!=bare&&document.scene().find(duplicate)->enemy==tuned.enemy&&document.scene().find(duplicate)->controller==tuned.controller,"Duplicating an Enemy lost its definition or controller profile.");
    require(document.undo()&&!document.scene().contains(duplicate)&&document.redo()&&document.scene().find(duplicate)->enemy==tuned.enemy,"Enemy duplication did not undo and redo.");
    SceneDocument play(project.root);require(play.resetTransientFrom(document)&&play.scene().find(bare)->enemy==tuned.enemy,"Transient Play copy lost the Enemy definition.");
    auto runtime=*play.scene().find(bare);runtime.enemy->radius=99;require(play.updateEntity(bare,runtime)&&document.scene().find(bare)->enemy==tuned.enemy,"Transient Enemy edits changed authored data.");
}
void persistenceAndLegacyCompatibility() {
    Project project;SceneDocument document(project.root);
    const std::array behaviours{EnemyBehaviour::Idle,EnemyBehaviour::ChasePlayer,EnemyBehaviour::RandomMove};std::array<EntityId,3> ids{};
    for(std::size_t i=0;i<behaviours.size();++i){ids[i]=document.createEnemy("Enemy "+std::to_string(i));auto entity=*document.scene().find(ids[i]);entity.enemy=SceneEnemy{behaviours[i],float(i)+.5f,float(i),float(i)+2};require(document.updateEntity(ids[i],entity),document.status());}
    require(document.save("Enemy definitions"),document.status());auto file=sceneAssetPath(project.root,document.assetId());const auto bytes=read(file);const auto json=Json::parse(bytes);
    require(json.at("version")==25,"Enemy scene did not write the current schema.");
    const auto loaded=readSceneSnapshot(project.root,document.assetId());Scene copy;copy.apply(loaded.records,loaded.properties);
    for(const auto id:ids)require(copy.find(id)->enemy==document.scene().find(id)->enemy,"Enemy settings did not round-trip through the scene registry.");
    SceneDocument reopened(project.root);require(reopened.load(document.assetId())&&!reopened.dirty()&&read(file)==bytes,"Opening Enemy definitions rewrote the scene or marked it dirty.");
    auto old=json;old["version"]=18;for(auto& entity:old["entities"])entity.erase("enemy");const auto oldBytes=old.dump(2);writeFileAtomically(file,oldBytes);
    SceneDocument legacy(project.root);require(legacy.load(document.assetId()),legacy.status());
    for(const auto id:ids)require(!legacy.scene().find(id)->enemy&&legacy.scene().find(id)->actor&&legacy.scene().find(id)->navigation,"An old actor was implicitly converted to Enemy based on its name or capabilities.");
    require(!legacy.dirty()&&read(file)==oldBytes,"Opening a version-18 scene persisted an Enemy conversion.");
    require(legacy.save(),legacy.status());file=sceneAssetPath(project.root,legacy.assetId());require(Json::parse(read(file)).at("version")==25,"Explicit legacy save retained the old schema.");
    auto malformed=json;malformed["entities"][0]["enemy"]["behaviour"]="unknown";writeFileAtomically(file,malformed.dump());
    require(!reopened.load(document.assetId())&&reopened.scene().find(ids[0])->enemy==document.scene().find(ids[0])->enemy,"Unknown Enemy behaviour loaded or partially replaced the active scene.");
}
void validationAndQuaderRefresh() {
    Project project;SceneDocument document(project.root);const auto id=document.createEnemy();const auto before=*document.scene().find(id);const auto history=document.history().size();
    const auto rejects=[&](SceneEnemy invalid){auto entity=before;entity.enemy=invalid;require(!document.updateEntity(id,entity)&&sameEntity(*document.scene().find(id),before)&&document.history().size()==history,"Invalid Enemy settings partially applied or changed history.");};
    auto invalid=*before.enemy;invalid.radius=-1;rejects(invalid);invalid=*before.enemy;invalid.radius=std::numeric_limits<float>::quiet_NaN();rejects(invalid);
    invalid=*before.enemy;invalid.minWait=-1;rejects(invalid);invalid=*before.enemy;invalid.maxWait=.5f;rejects(invalid);
    invalid=*before.enemy;invalid.maxWait=std::numeric_limits<float>::infinity();rejects(invalid);invalid=*before.enemy;invalid.behaviour=static_cast<EnemyBehaviour>(255);rejects(invalid);
    for(const auto kind:{SceneComponentKind::Actor,SceneComponentKind::Controller,SceneComponentKind::Movement,SceneComponentKind::Navigation}) {
        auto entity=before;switch(kind){case SceneComponentKind::Actor:entity.actor.reset();break;case SceneComponentKind::Controller:entity.controller.reset();break;case SceneComponentKind::Movement:entity.movement.reset();break;case SceneComponentKind::Navigation:entity.navigation.reset();break;default:break;}
        require(!document.updateEntity(id,entity)&&sameEntity(*document.scene().find(id),before),"Enemy accepted missing required capabilities.");
    }
    auto boundary=before;boundary.enemy=SceneEnemy{EnemyBehaviour::RandomMove,0,0,0};require(document.updateEntity(id,boundary),"Zero radius and zero wait Enemy boundaries were rejected.");
    const auto owner=document.createQuader();EntityRecord generated;generated.id=1;generated.name="Imported creature";generated.quaderGenerated=QuaderGenerated{owner,"mesh/creature"};
    const std::vector imported{generated};require(document.replaceQuaderImport(owner,QuaderLink{},imported),document.status());
    EntityId creature{};for(const auto& entity:document.scene().records())if(entity.quaderGenerated&&entity.quaderGenerated->sourceKey=="mesh/creature")creature=entity.id;
    require(creature&&addSceneComponent(document,creature,SceneComponentKind::Enemy),document.status());auto tuned=*document.scene().find(creature);tuned.enemy=SceneEnemy{EnemyBehaviour::RandomMove,4,.2f,.8f};require(document.updateEntity(creature,tuned),document.status());
    generated.local=math::translation({2,0,1});require(document.replaceQuaderImport(owner,QuaderLink{},{generated}),document.status());
    require(document.scene().find(creature)->enemy==tuned.enemy&&document.scene().find(creature)->local==generated.local,"Quader refresh discarded authored Enemy settings or stopped source transform synchronization.");

    SceneDocument reserved(project.root);const auto reservedOwner=reserved.createQuader();generated.quaderGenerated->owner=reservedOwner;
    require(reserved.replaceQuaderImport(reservedOwner,QuaderLink{},{generated}),reserved.status());EntityId importedRoot{};
    for(const auto& entity:reserved.scene().records())if(entity.quaderGenerated&&entity.quaderGenerated->sourceKey=="Imported")importedRoot=entity.id;
    require(importedRoot&&addSceneComponent(reserved,importedRoot,SceneComponentKind::Enemy),reserved.status());
    auto rootEnemy=*reserved.scene().find(importedRoot);rootEnemy.enemy=SceneEnemy{EnemyBehaviour::RandomMove,9,.4f,1.7f};
    rootEnemy.controller->profile=AssetRef<NavigationAgentProfile>{newAssetId()};rootEnemy.effects->starting.emplace_back(newAssetId());rootEnemy.movement->enabled=false;
    rootEnemy.navigation->enabled=false;rootEnemy.navigation->showRoute=false;require(reserved.updateEntity(importedRoot,rootEnemy),reserved.status());
    require(reserved.replaceQuaderImport(reservedOwner,QuaderLink{},{generated}),reserved.status());const auto refreshed=*reserved.scene().find(importedRoot);
    require(refreshed.enemy==rootEnemy.enemy&&refreshed.actor==rootEnemy.actor&&refreshed.controller==rootEnemy.controller&&refreshed.movement==rootEnemy.movement&&refreshed.navigation==rootEnemy.navigation&&refreshed.effects==rootEnemy.effects,
        "Quader refresh discarded Enemy or its authored prerequisites on the reserved Imported container.");
}
}
int main(){try{recipeDependenciesAndHistory();persistenceAndLegacyCompatibility();validationAndQuaderRefresh();std::cout<<"Enemy scene checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
