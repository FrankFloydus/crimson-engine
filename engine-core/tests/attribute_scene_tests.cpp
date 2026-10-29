#include "scene/SceneComponents.h"
#include "assets/GameplayEffectAsset.h"
#include "assets/AtomicFile.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
using Json=nlohmann::json;
namespace fs=std::filesystem;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-attribute-scene-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){fs::create_directories(root/"assets/scenes");}
    ~Project(){std::error_code error;const auto temp=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-attribute-scene-"))fs::remove_all(target,error);}
};
std::string read(const fs::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
template<class T>concept HasAuthoredAttributes=requires(T value){value.attributes;};
static_assert(!HasAuthoredAttributes<EntityRecord>);
std::optional<double> startingOverride(const SceneDocument& document,EntityId id,const GameplayTag& tag){
    const auto record=document.scene().find(id);if(!record||!record->effects)return {};
    std::optional<double> result;
    for(const auto& reference:record->effects->starting){
        auto effect=document.gameplayEffect(reference.id);
        if(!effect)effect=std::make_shared<const GameplayEffectAsset>(loadGameplayEffectAsset(document.projectRoot(),reference.id));
        for(const auto& mod:effect->modifiers)if(mod.attribute==tag&&mod.operation==EffectModifierOperation::Override&&mod.magnitude.calculation.empty())result=mod.magnitude.literal;
    }
    return result;
}
void retiredCapabilityAndRoleEffects(){
    static_assert(static_cast<unsigned>(SceneComponentKind::Attributes)==19&&static_cast<unsigned>(SceneComponentKind::Tags)==20&&static_cast<unsigned>(SceneComponentKind::Effects)==21);
    Project project;SceneDocument document(project.root);const auto player=document.createPlayer(),enemy=document.createEnemy();
    require(document.scene().find(player)->effects->starting==std::vector{AssetRef<GameplayEffectAsset>{PlayerStartingEffectId}}&&
        document.scene().find(enemy)->effects->starting==std::vector{AssetRef<GameplayEffectAsset>{EnemyStartingEffectId}},"Role recipes did not author their starting effects.");
    const auto record=*document.scene().find(player);const auto history=document.history().size();
    require(!hasSceneComponent(record,SceneComponentKind::Attributes)&&!editableSceneComponent(SceneComponentKind::Attributes)&&
        !addSceneComponent(document,player,SceneComponentKind::Attributes)&&!removeSceneComponent(document,player,SceneComponentKind::Attributes)&&document.history().size()==history,
        "Retired attribute membership remained an authored component or changed history.");
    const auto components=sceneComponents(record);require(std::find(components.begin(),components.end(),SceneComponentKind::Attributes)==components.end(),"Retired membership appeared in component enumeration.");
    const auto generic=document.createEmpty("Motor owner");const auto before=*document.scene().find(generic);
    require(addSceneComponent(document,generic,SceneComponentKind::Movement),document.status());const auto composed=*document.scene().find(generic);
    require(composed.controller&&composed.movement&&!composed.effects,"Motor authoring created attribute membership or an unrelated initializer.");
    require(document.undo()&&sameEntity(*document.scene().find(generic),before)&&document.redo(),"Motor dependencies did not undo and redo atomically.");
    require(addSceneComponent(document,generic,SceneComponentKind::Enemy),document.status());const auto creature=*document.scene().find(generic);
    require(creature.effects->starting.front().id==EnemyStartingEffectId&&removeSceneComponent(document,generic,SceneComponentKind::Movement)&&
        document.scene().find(generic)->effects==creature.effects&&document.undo()&&sameEntity(*document.scene().find(generic),creature),"Motor removal discarded independently authored initialization effects.");
    EntityRecord independent;initializePlayerEffects(independent);const auto once=independent.effects;initializePlayerEffects(independent);
    require(independent.effects==once,"Role initialization helper duplicated its protected effect.");
}
void effectsAndTagsStorageHistory(){
    Project project;SceneDocument document(project.root);const auto id=document.createEmpty("Authored values");
    require(addSceneComponent(document,id,SceneComponentKind::Tags)&&addSceneComponent(document,id,SceneComponentKind::Effects),document.status());
    const auto empty=*document.scene().find(id);auto authored=empty;authored.tags->add(GameplayTag{"Project.Faction.Bandit"});authored.tags->add(stats::Health);
    authored.effects->starting={AssetRef<GameplayEffectAsset>{EnemyStartingEffectId},AssetRef<GameplayEffectAsset>{EnemyStartingEffectId}};
    require(document.updateEntity(id,authored)&&document.undo()&&sameEntity(*document.scene().find(id),empty)&&document.redo()&&sameEntity(*document.scene().find(id),authored),"Effects/Tags history lost identity or order.");
    require(document.begin("Change references")&&document.update([&](auto& records,auto&){records.front().effects->starting.clear();records.front().tags->remove(stats::Health);}),document.status());
    document.cancel();require(sameEntity(*document.scene().find(id),authored),"Canceled effect/tag gesture changed the entity.");
    require(document.save("Effect and tag authoring"),document.status());const auto file=sceneAssetPath(project.root,document.assetId());const auto bytes=read(file);const auto json=Json::parse(bytes);
    require(json.at("version")==25&&!json["entities"][0].contains("attributes")&&json["entities"][0]["effects"]["starting"].size()==2,"Current scene retained authored membership or omitted repeated effects.");
    SceneDocument opened(project.root);require(opened.load(document.assetId())&&!opened.dirty()&&read(file)==bytes&&sameEntity(*opened.scene().find(id),authored),"Current scene changed values or wrote during load.");
    document.select({id},id);require(document.duplicateSelection(),document.status());const auto copy=document.selection().primary;
    require(document.scene().find(copy)->effects==authored.effects&&document.scene().find(copy)->tags==authored.tags,"Duplication lost Effects/Tags.");
    SceneDocument play(project.root);require(play.resetTransientFrom(document)&&play.scene().find(copy)->effects==authored.effects,"Transient Play copy lost initializer references.");
    require(document.save("Effect copy",true),document.status());SceneDocument copied(project.root);require(copied.load(document.assetId())&&copied.scene().find(copy)->effects==authored.effects,"Save As changed effect references.");
}
void privateLegacySpeedConversion(){
    EntityRecord actor;actor.id=17;actor.name="Player";actor.actor.emplace();actor.controller.emplace();actor.movement.emplace();
    const auto current=Json::parse(serializeSceneSnapshot({{},"Legacy values",{actor},{}}));
    for(const auto version:{4,10,15,16,17,18,19,20,21}){
        Project project;auto json=current;json["version"]=version;auto& entity=json["entities"][0];
        entity.erase("movement");
        if(version>=16)entity["motor"]={{"enabled",true}};
        if(version<=15){entity["character"]={{"height",1.8},{"radius",.3},{"moveSpeed",8.25}};entity.erase("controller");entity.erase("motor");}
        else if(version<=20){entity["motor"]["moveSpeed"]=8.25;entity["motor"]["enabled"]=false;if(version<=17){entity["controller"]["height"]=1.8;entity["controller"]["radius"]=.3;}}
        else entity["attributes"]=Json::array({{{"tag","Stats.MovementSpeed"},{"startingBase",8.25}},{{"tag","Project.Zero"},{"startingBase",0}}});
        const auto id="assets/scenes/legacy-"+std::to_string(version)+".crimson-scene",text=json.dump(2);writeFileAtomically(project.root/id,text);
        SceneDocument document(project.root);require(document.load(id),document.status());
        require(startingOverride(document,actor.id,stats::MovementSpeed)==8.25&&!document.dirty()&&document.history().size()==0&&read(project.root/id)==text,"Legacy conversion changed its value, authored file or history.");
        if(version<=15)require(startingOverride(document,actor.id,stats::Vitality)==50&&startingOverride(document,actor.id,stats::Spirit)==50,"Old Character lost its role initialization values.");
        else require(!startingOverride(document,actor.id,stats::Vitality),"A legacy Actor acquired Player values from its name.");
        if(version==21)require(startingOverride(document,actor.id,GameplayTag{"Project.Zero"})==0,"Legacy zero-valued unresolved data was dropped.");
        require(document.save(),document.status());const auto saved=Json::parse(read(sceneAssetPath(project.root,document.assetId())));
        const auto& savedActor=saved["entities"][0];
        require(saved.at("version")==25&&!savedActor.contains("attributes")&&!savedActor.contains("motor")&&
            savedActor.at("movement").contains("jumpHeight")&&!savedActor.at("movement").contains("moveSpeed")&&
            startingOverride(document,actor.id,stats::MovementSpeed)==8.25,"Explicit Save retained membership/legacy speed or lost current Movement and the converted value.");
    }
}
void tagsAndEffectsQuaderPreservation(){
    Project project;SceneDocument document(project.root);const auto owner=document.createQuader();EntityRecord input;input.id=1;input.name="Source member";input.quaderGenerated=QuaderGenerated{owner,"member"};
    require(document.replaceQuaderImport(owner,QuaderLink{},{input}),document.status());std::vector<EntityId> generated;
    for(const auto& record:document.scene().records())if(record.quaderGenerated)generated.push_back(record.id);
    for(const auto id:generated){auto record=*document.scene().find(id);record.tags=GameplayTagContainer{{GameplayTag{"Project.Creature"}}};record.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{EnemyStartingEffectId}}};require(document.updateEntity(id,record),document.status());}
    input.local=math::translation({4,0,2});require(document.replaceQuaderImport(owner,QuaderLink{},{input}),document.status());
    for(const auto id:generated){const auto record=*document.scene().find(id);require(record.tags->has(GameplayTag{"Project.Creature"},true)&&record.effects->starting.front().id==EnemyStartingEffectId,"Quader refresh dropped tags or starting effects.");}
}
}
int main(){try{retiredCapabilityAndRoleEffects();effectsAndTagsStorageHistory();privateLegacySpeedConversion();tagsAndEffectsQuaderPreservation();
    std::cout<<"Retired authored membership, effect initialization, legacy conversion and scene history checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
