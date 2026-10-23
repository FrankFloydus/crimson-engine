#include "runtime/PlaySession.h"
#include "assets/GameplayEffectAsset.h"
#include "assets/ContentStorage.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,const char* message){require(std::abs(actual-expected)<1e-6,message);}
void setLiteral(GameplayEffectAsset& effect,const GameplayTag& attribute,double value) {
    const auto modifier=std::find_if(effect.modifiers.begin(),effect.modifiers.end(),[&](const auto& candidate){return candidate.attribute==attribute;});
    require(modifier!=effect.modifiers.end(),"Starter effect is missing the tested attribute.");modifier->magnitude.literal=value;
}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("cc-fx-play-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project(){std::error_code error;const auto resolved=std::filesystem::weakly_canonical(root,error);
        const auto temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error&&resolved.parent_path()==temp&&resolved.filename().string().starts_with("cc-fx-play-"))std::filesystem::remove_all(resolved,error);}
};
void playFacade(){
    Project project;SceneDocument authored(project.root);const auto actor=authored.createEmpty("Actor");
    const auto initId=newAssetId(),hurtId=newAssetId(),buffId=newAssetId();
    GameplayEffectAsset init;init.id=initId;init.name="Init";
    init.modifiers={{stats::MaxHealth,EffectModifierOperation::Override,{100}},{stats::Vitality,EffectModifierOperation::Override,{10}}};
    GameplayEffectAsset hurt;hurt.id=hurtId;hurt.name="Damage";hurt.modifiers={{stats::Health,EffectModifierOperation::AddBase,{-10}}};
    GameplayEffectAsset buff;buff.id=buffId;buff.name="Vitality";buff.durationPolicy=EffectDurationPolicy::Infinite;
    buff.modifiers={{stats::Vitality,EffectModifierOperation::AddBase,{5}}};
    std::map<std::string,GameplayEffectAsset> definitions{{initId,init},{hurtId,hurt},{buffId,buff}};
    require(authored.edit("Actor stats",[&](auto& records,auto&){for(auto& value:records)if(value.id==actor){
        value.actor.emplace();
        value.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{initId},AssetRef<GameplayEffectAsset>{hurtId}}};
    }}),"Cannot compose effects actor.");
    PlaySession play(project.root);play.setGameplayEffects([&](const auto& id){const auto found=definitions.find(id);
        return found==definitions.end()?EffectDefinitionSnapshot{}:EffectDefinitionSnapshot{std::make_shared<const GameplayEffectAsset>(found->second),1,{}};});
    require(play.start(authored),play.status());
    near(play.attributeValue(actor,stats::Health)->current,110,"Startup damage was refilled or capacities were not settled.");
    const auto applied=play.applyEffect(actor,actor,buffId);require(static_cast<bool>(applied),applied.diagnostic);
    near(play.attributeValue(actor,stats::MaxHealth)->current,130,"Play effect facade did not evaluate native dependent capacity.");
    require(play.addAttributeBase(actor,stats::Health,-15),"Play Base helper rejected damage.");
    near(play.attributeValue(actor,stats::Health)->current,95,"Damage helper did not share effect backend.");
    require(play.removeEffects(actor,buffId)==1,"Remove by definition did not remove its active instance.");
    near(play.attributeValue(actor,stats::MaxHealth)->current,120,"Removing buff failed to restore native capacity.");
    require(play.document().edit("Rename",[&](auto& records,auto&){for(auto& value:records)if(value.id==actor)value.name="Renamed";}),"Rename failed.");
    play.advance(0,Vec2{},Camera{});
    near(play.attributeValue(actor,stats::Health)->current,95,"Reconciliation replayed starting effects.");
    const auto stale=play.applyEffect(actor,actor,buffId).handle;play.stop();require(play.start(authored),play.status());
    require(!play.removeEffect(stale),"A previous Play handle removed the new world's effect.");
    near(play.attributeValue(actor,stats::Health)->current,110,"Restart did not perform a fresh ordered bootstrap.");
}
void legacyActorMigration(){
    Project project;SceneDocument source(project.root);
    const auto player=source.createPlayer("Legacy Player");
    for(unsigned index=0;index<4;++index)source.createEnemy("Legacy Enemy "+std::to_string(index+1));
    // Build an isolated, deliberately old scene instead of depending on the
    // user's current Main, its tuning, or separately materialized effect files.
    auto json=nlohmann::json::parse(serializeSceneSnapshot({{},"Legacy actor values",source.scene().records(),source.scene().properties()}));
    json["version"]=21;
    for(auto& entity:json["entities"]) {
        entity.erase("effects");
        if(entity.contains("playerInput"))entity["attributes"]=nlohmann::json::array({
            {{"tag","Stats.Health"},{"startingBase",-99}},{{"tag","Stats.MaxHealth"},{"startingBase",0}},
            {{"tag","Stats.Mana"},{"startingBase",3}},{{"tag","Stats.MaxMana"},{"startingBase",0}},
            {{"tag","Stats.Vitality"},{"startingBase",18}},{{"tag","Stats.Spirit"},{"startingBase",24}},
            {{"tag","Stats.MovementSpeed"},{"startingBase",3}}});
        else if(entity.contains("enemy"))entity["attributes"]=nlohmann::json::array({
            {{"tag","Stats.Health"},{"startingBase",1}},{{"tag","Stats.MaxHealth"},{"startingBase",100}},
            {{"tag","Stats.MovementSpeed"},{"startingBase",2}}});
    }
    const auto original=json.dump(2);
    std::filesystem::create_directories(project.root/"assets"/"scenes");
    const auto file=project.root/"assets"/"scenes"/"legacy.crimson-scene";
    {std::ofstream fixture(file,std::ios::binary);fixture<<original;require(static_cast<bool>(fixture),"Cannot prepare disposable legacy fixture.");}
    SceneDocument document(project.root);
    require(document.openPreparedSnapshot(readSceneSnapshot(project.root,"assets/scenes/legacy.crimson-scene")),document.status());
    require(!document.dirty()&&document.history().size()==0&&document.stagedGameplayEffects().size()==5,
        "Legacy conversion dirtied the scene or lost independent staged initializer effects.");
    PlaySession play(project.root);require(play.start(document),play.status());
    require(document.scene().properties().playerCharacter==player,"Legacy conversion changed the designated Player identity.");
    near(play.attributeValue(player,stats::Vitality)->current,18,"Authored Vitality changed during conversion.");
    near(play.attributeValue(player,stats::Spirit)->current,24,"Authored Spirit changed during conversion.");
    near(play.attributeValue(player,stats::Health)->current,36,"Legacy Health did not start at derived capacity.");
    near(play.attributeValue(player,stats::Mana)->current,48,"Legacy Mana did not start at derived capacity.");
    near(play.attributeValue(player,stats::MovementSpeed)->current,3,"Legacy movement speed changed during conversion.");
    unsigned enemies{};
    for(const auto& record:document.scene().records())if(record.enemy){++enemies;
        near(play.attributeValue(record.id,stats::Health)->current,100,"Legacy Enemy Health did not spawn maximized.");
        near(play.attributeValue(record.id,stats::MovementSpeed)->current,2,"Legacy Enemy speed changed.");
        const auto shown=play.attributeSnapshots(record.id);
        require(std::none_of(shown.begin(),shown.end(),[](const auto& value){return value.tag==stats::Vitality||value.tag==stats::Mana;}),"Migration activated unrelated Enemy stats.");
    }
    require(enemies==4,"Legacy conversion lost authored Enemy entities.");
    std::ifstream after(file,std::ios::binary);require(std::string{std::istreambuf_iterator<char>(after),{}}==original,
        "Legacy migration wrote authored data while loading or starting Play.");
    require(!std::filesystem::exists(project.root/"Content"),"Loading or Play materialized pending effects before an explicit save.");
}
void authoredDefaultsApplyOnNextSpawn() {
    Project project;initializeContentStorage(project.root);ensureEngineGameplayEffects(project.root);
    SceneDocument source(project.root);const auto existingPlayer=source.createPlayer("Existing Player"),existingEnemy=source.createEnemy("Existing Enemy");
    require(source.save("Starter defaults references"),source.status());
    const auto savedScene=sceneAssetPath(project.root,source.assetId());std::ifstream beforeFile(savedScene,std::ios::binary);
    const std::string savedBytes{std::istreambuf_iterator<char>(beforeFile),{}};beforeFile.close();
    auto player=loadGameplayEffectAsset(project.root,PlayerStartingEffectId),enemy=loadGameplayEffectAsset(project.root,EnemyStartingEffectId);
    setLiteral(player,stats::Vitality,30);setLiteral(player,stats::MovementSpeed,7);
    setLiteral(enemy,stats::MaxHealth,245);setLiteral(enemy,stats::MovementSpeed,3.5);
    saveGameplayEffectAsset(project.root,player);saveGameplayEffectAsset(project.root,enemy);
    SceneDocument authored(project.root);require(authored.load(source.assetId()),authored.status());
    const auto newPlayer=authored.createPlayer("New Player"),newEnemy=authored.createEnemy("New Enemy");
    const auto references=[&](const Scene& scene,EntityId entity,const std::string& expected) {
        const auto record=scene.find(entity);require(record&&record->effects&&record->effects->starting.size()==1&&record->effects->starting.front().id==expected,
            "Editing starter defaults replaced an actor's shared effect reference.");
    };
    for(const auto entity:{existingPlayer,newPlayer})references(authored.scene(),entity,PlayerStartingEffectId);
    for(const auto entity:{existingEnemy,newEnemy})references(authored.scene(),entity,EnemyStartingEffectId);
    require(authored.gameplayEffect(PlayerStartingEffectId)&&*authored.gameplayEffect(PlayerStartingEffectId)==player&&
        authored.gameplayEffect(EnemyStartingEffectId)&&*authored.gameplayEffect(EnemyStartingEffectId)==enemy,
        "Scene effect lookup returned compiled starters instead of saved authored defaults.");
    PlaySession play(project.root);require(play.start(authored),play.status());
    const auto playerValues=[&](EntityId entity,double vitality,double speed) {
        near(play.attributeValue(entity,stats::Vitality)->current,vitality,"Player spawn ignored authored starter Vitality.");
        near(play.attributeValue(entity,stats::MaxHealth)->current,vitality*2,"Player starter capacity did not use native Vitality calculation.");
        near(play.attributeValue(entity,stats::Health)->current,vitality*2,"Player spawn did not fill authored starter capacity.");
        near(play.attributeValue(entity,stats::MovementSpeed)->current,speed,"Player spawn ignored authored starter movement speed.");
    };
    const auto enemyValues=[&](EntityId entity,double health,double speed) {
        near(play.attributeValue(entity,stats::MaxHealth)->current,health,"Enemy spawn ignored authored starter capacity.");
        near(play.attributeValue(entity,stats::Health)->current,health,"Enemy spawn did not fill authored starter capacity.");
        near(play.attributeValue(entity,stats::MovementSpeed)->current,speed,"Enemy spawn ignored authored starter movement speed.");
    };
    for(const auto entity:{existingPlayer,newPlayer})playerValues(entity,30,7);
    for(const auto entity:{existingEnemy,newEnemy})enemyValues(entity,245,3.5);
    setLiteral(player,stats::Vitality,40);setLiteral(player,stats::MovementSpeed,9);
    setLiteral(enemy,stats::MaxHealth,375);setLiteral(enemy,stats::MovementSpeed,2);
    saveGameplayEffectAsset(project.root,player);saveGameplayEffectAsset(project.root,enemy);play.advance(0,Vec2{},Camera{});
    for(const auto entity:{existingPlayer,newPlayer})playerValues(entity,30,7);
    for(const auto entity:{existingEnemy,newEnemy})enemyValues(entity,245,3.5);
    const auto spawnedPlayer=play.document().createPlayer("Spawned Player"),spawnedEnemy=play.document().createEnemy("Spawned Enemy");
    require(spawnedPlayer&&spawnedEnemy,play.document().status());play.advance(0,Vec2{},Camera{});
    playerValues(spawnedPlayer,40,9);enemyValues(spawnedEnemy,375,2);
    references(play.document().scene(),spawnedPlayer,PlayerStartingEffectId);references(play.document().scene(),spawnedEnemy,EnemyStartingEffectId);
    play.stop();require(play.start(authored),play.status());
    for(const auto entity:{existingPlayer,newPlayer})playerValues(entity,40,9);
    for(const auto entity:{existingEnemy,newEnemy})enemyValues(entity,375,2);
    for(const auto entity:{existingPlayer,newPlayer})references(authored.scene(),entity,PlayerStartingEffectId);
    for(const auto entity:{existingEnemy,newEnemy})references(authored.scene(),entity,EnemyStartingEffectId);
    std::ifstream afterFile(savedScene,std::ios::binary);
    require(std::string{std::istreambuf_iterator<char>(afterFile),{}}==savedBytes,"Editing default effects rewrote the saved scene or its actor references.");
}
}
int main(){try{playFacade();legacyActorMigration();authoredDefaultsApplyOnNextSpawn();std::cout<<"Effects Play integration checks passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
