#include "runtime/EntityAttributes.h"
#include "runtime/PlaySession.h"
#include "runtime/EntityEffects.h"
#include "assets/GameplayEffectAsset.h"
#include "scene/SceneComponents.h"
#include "navigation_fixture.h"
#include <cmath>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double value,double expected,const char* message){require(std::abs(value-expected)<1e-7,message);}
EntityRecord resource(EntityId id) {
    EntityRecord record;record.id=id;record.name="Resource";
    return record;
}
EffectModifier initialValue(const GameplayTag& tag,double value){return {tag,EffectModifierOperation::Override,EffectMagnitude{value,{},{}}};}
std::shared_ptr<const GameplayEffectAsset> initialization(std::string id,std::vector<EffectModifier> modifiers) {
    GameplayEffectAsset effect;effect.id=std::move(id);effect.name="Test initialization";effect.modifiers=std::move(modifiers);
    return std::make_shared<const GameplayEffectAsset>(std::move(effect));
}
void independentLifetimeAndSeeds() {
    const auto initial=initialization(canonicalAssetId("test:attributes/lifetime"),{initialValue(stats::MaxHealth,100),initialValue(stats::Vitality,10),initialValue(stats::MovementSpeed,5)});
    Scene scene;auto record=resource(1);record.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{initial->id}}};scene.apply({record},{});EntityAttributes attributes;EntityEffects effects(attributes);
    effects.setDefinitions([initial](const auto& id){return EffectDefinitionSnapshot{id==initial->id?initial:nullptr,1,{}};});
    require(effects.reconcile(1,scene),"Initial attributes failed.");
    near(attributes.value(1,stats::MaxHealth)->current,120,"Optional stat formula failed.");
    near(attributes.value(1,stats::Health)->current,120,"Spawn did not fill Health to its resolved maximum.");
    require(effects.addBase(1,stats::Health,-40),"Damage failed.");
    require(effects.step(2),"Regeneration failed.");
    near(attributes.value(1,stats::Health)->base,81,"Regeneration did not update durable Base.");
    record.name="Renamed";record.local=math::translation({2,3,4});scene.apply({record},{});
    require(effects.reconcile(1,scene),"Unrelated reconcile failed.");
    near(attributes.value(1,stats::Health)->current,81,"Unrelated edit restored the starting pool.");
    effects.setBase(1,stats::Vitality,20);effects.reconcile(1,scene);
    near(attributes.value(1,stats::MaxHealth)->current,140,"Changed stat was not applied.");
    near(attributes.value(1,stats::Health)->current,81,"Capacity growth refilled Health.");
    effects.setBase(1,stats::Health,50);effects.setBase(1,stats::MovementSpeed,0);effects.reconcile(1,scene);
    near(attributes.value(1,stats::MovementSpeed)->current,0,"Scene reconciliation reset runtime zero speed.");
    effects.addBase(1,stats::Health,-10);effects.reconcile(1,scene);
    near(attributes.value(1,stats::Health)->current,40,"Scene reconciliation restored the resource pool.");
    effects.setBase(1,stats::MovementSpeed,3);effects.reconcile(1,scene);
    near(attributes.value(1,stats::MovementSpeed)->current,3,"Unrelated reconciliation reset runtime Base.");
    effects.setBase(1,stats::MovementSpeed,5);effects.reconcile(1,scene);
    near(attributes.value(1,stats::MovementSpeed)->current,5,"Runtime speed update was lost.");
    near(attributes.value(1,stats::Health)->current,40,"Changing another starting value refilled Health.");
    effects.setBase(1,stats::Vitality,0);effects.reconcile(1,scene);
    near(attributes.value(1,stats::MaxHealth)->current,100,"Native dependency did not respond to zeroed Vitality.");
    effects.setBase(1,stats::Health,-50);effects.step(10);
    near(attributes.value(1,stats::Health)->current,-50,"Health regeneration revived an overkilled entity.");
    auto duplicate=record;duplicate.id=2;scene.apply({record,duplicate},{});effects.reconcile(1,scene);
    near(attributes.value(2,stats::Health)->current,120,"A duplicate inherited depleted runtime values.");
    scene.apply({duplicate},{});effects.reconcile(1,scene);require(!attributes.value(1,stats::Health),"Despawn retained attributes.");
    scene.apply({record,duplicate},{});effects.reconcile(1,scene);
    near(attributes.value(1,stats::Health)->current,120,"Respawn did not execute its starting effect.");
    effects.addBase(1,stats::Health,-20);effects.reconcile(2,scene);
    near(attributes.value(1,stats::Health)->current,120,"World replacement retained runtime depletion.");
}
void playMovementAndPools() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-attributes-runtime";
    SceneDocument authored(root);const auto floor=authored.createCollider("Floor"),player=authored.createPlayer();
    const auto volume=authored.createNavigationVolume();
    const auto initial=initialization(canonicalAssetId("test:attributes/play-initial"),{initialValue(stats::Vitality,10),initialValue(stats::MaxHealth,100),initialValue(stats::MovementSpeed,5)});
    require(floor&&player&&volume,"Cannot author attribute Play fixture.");
    require(authored.edit("Fixture",[&](auto& records,auto&){for(auto& record:records) {
        if(record.id==floor){record.collider->size={30,1,30};record.local=math::translation({0,-.5f,0});}
        if(record.id==volume)record.navigationVolume->size={30,8,30};
        if(record.id==player){record.local=math::translation({0,.05f,0});record.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{initial->id}}};}
    }}),"Cannot tune Play fixture.");
    require(addSceneComponent(authored,player,SceneComponentKind::Navigation),"Cannot add Navigation to the external-destination fixture.");
    PlaySession play(root);play.setAgentProfiles(testing::agentProfileQuery());
    play.setGameplayEffects([initial](const std::string& id){return id==initial->id?EffectDefinitionSnapshot{initial,1,{}}:EffectDefinitionSnapshot{};});
    require(play.start(authored),"Cannot enter Play.");require(testing::waitForNavigation(play),"Navigation failed to prepare.");
    const auto navigationBuilds=play.navigationBuildCount();
    const auto value=[&](const GameplayTag& tag){return play.attributeValue(player,tag).value();};
    near(value(stats::Health).base,120,"Play did not spawn the player at full Health.");
    require(play.addAttributeBase(player,stats::Health,-50),"Runtime damage API failed.");
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,Vec2{},Camera{});
    near(value(stats::Health).base,70.5,"Play did not run fixed-tick regeneration.");
    require(play.document().edit("Rename",[&](auto& records,auto&){for(auto& record:records)if(record.id==player)record.name="Renamed";}),"Rename failed.");
    play.advance(0,Vec2{},Camera{});near(value(stats::Health).base,70.5,"A Play edit reset the resource pool.");
    require(play.document().edit("Disable Motor",[&](auto& records,auto&){for(auto& record:records)if(record.id==player)record.motor->enabled=false;}),"Disable failed.");
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,Vec2{},Camera{});
    near(value(stats::Health).base,71,"Disabling Motor stopped or reset regeneration.");
    require(play.document().edit("Enable Motor",[&](auto& records,auto&){for(auto& record:records)if(record.id==player)record.motor->enabled=true;}),"Enable failed.");
    play.advance(0,Vec2{},Camera{});
    require(play.setActorDestination(player,{6,0,0}),"Destination was rejected.");
    require(play.setAttributeBase(player,stats::MovementSpeed,0),"Zero speed was rejected.");
    const auto before=math::transformPoint(*play.document().scene().worldTransform(player),{});
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,Vec2{},Camera{});
    const auto after=math::transformPoint(*play.document().scene().worldTransform(player),{});
    require(std::hypot(after[0]-before[0],after[2]-before[2])<.01,"Zero speed permitted commanded movement.");
    require(play.actorDestination(player).has_value(),"Zero speed discarded route intent.");
    require(play.setAttributeBase(player,stats::MovementSpeed,2),"Speed change failed.");
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,Vec2{},Camera{});
    const auto resumed=math::transformPoint(*play.document().scene().worldTransform(player),{});
    require(resumed[0]>after[0]+1.8f,"Restoring Movement Speed did not resume the existing route.");
    require(play.navigationBuildCount()==navigationBuilds,"Attribute changes rebuilt unrelated navigation tiles.");
    require(authored.scene().find(player)->effects->starting==std::vector<AssetRef<GameplayEffectAsset>>{AssetRef<GameplayEffectAsset>{initial->id}},"Play changed authored starting effects.");
    play.stop();require(!play.attributeValue(player,stats::Health),"Stop retained a runtime pool.");
    require(play.start(authored),"Cannot restart Play.");near(play.attributeValue(player,stats::Health)->current,120,"Restart retained depleted Health.");
}
void persistedRoleRecipesHaveIndependentRuntimeSets() {
    const auto root=std::filesystem::temp_directory_path()/("crimson-role-runtime-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup(){std::error_code error;const auto path=std::filesystem::absolute(root).lexically_normal();
            if(std::filesystem::equivalent(path.parent_path(),std::filesystem::temp_directory_path(),error)&&!error)std::filesystem::remove_all(path,error);}
    } cleanup{root};
    SceneDocument authored(root);const auto player=authored.createPlayer(),first=authored.createEnemy(),second=authored.createEnemy();
    const auto firstInitial=initialization(canonicalAssetId("test:attributes/first-enemy"),{initialValue(stats::MaxHealth,37),initialValue(stats::MovementSpeed,5)});
    const auto secondInitial=initialization(canonicalAssetId("test:attributes/second-enemy"),{initialValue(stats::MaxHealth,80),initialValue(stats::MovementSpeed,5)});
    require(player&&first&&second,"Cannot create role runtime recipes.");
    require(authored.edit("Enemy capacities",[&](auto& records,auto&){for(auto& record:records) {
        if(record.id==first)record.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{firstInitial->id}}};
        if(record.id==second)record.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{secondInitial->id}}};
    }}),"Cannot edit per-instance Enemy capacities.");
    require(authored.save("Role runtime"),"Cannot save role runtime fixture.");SceneDocument reopened(root);
    require(reopened.load(authored.assetId()),"Cannot reopen role runtime fixture.");EntityAttributes runtime;EntityEffects effects(runtime);
    effects.setDefinitions([&](const std::string& id){auto value=id==firstInitial->id?firstInitial:id==secondInitial->id?secondInitial:engineGameplayEffect(id);return EffectDefinitionSnapshot{std::move(value),1,{}};});
    require(effects.reconcile(7,reopened.scene()),"Cannot initialize reopened role runtime attributes.");
    const auto value=[&](EntityId id,const GameplayTag& tag){const auto result=runtime.value(id,tag);require(result.has_value(),"A role recipe omitted a runtime attribute.");return *result;};
    near(value(player,stats::Vitality).current,50,"Player Vitality did not survive save/reopen.");
    near(value(player,stats::Spirit).current,50,"Player Spirit did not survive save/reopen.");
    near(value(player,stats::MaxHealth).base,0,"Player capacity gained an unintended flat Health bonus.");
    near(value(player,stats::MaxMana).base,0,"Player capacity gained an unintended flat Mana bonus.");
    near(value(player,stats::Health).current,100,"Player did not initialize full derived Health.");
    near(value(player,stats::Mana).current,100,"Player did not initialize full derived Mana.");
    near(value(first,stats::Health).current,37,"First Enemy did not initialize from its own capacity.");
    near(value(second,stats::Health).current,80,"Second Enemy shared another Enemy's capacity.");
    for(const auto id:{first,second})for(const auto& entry:runtime.snapshot(id))require(entry.tag!=stats::Vitality&&entry.tag!=stats::Spirit&&entry.tag!=stats::Mana&&entry.tag!=stats::MaxMana,
        "An Enemy recipe materialized optional Player primaries or Mana.");
    require(effects.addBase(player,stats::Health,-20)&&effects.addBase(player,stats::Mana,-30)&&effects.addBase(first,stats::Health,-7),"Cannot deplete role runtime pools.");
    require(effects.setBase(player,stats::Vitality,60)&&effects.setBase(player,stats::Spirit,70)&&effects.setBase(first,stats::MaxHealth,45),"Cannot tune runtime primaries/capacity.");
    require(reopened.edit("Unrelated rename",[&](auto& records,auto&){for(auto& record:records) {
        if(record.id==second)record.name="Unrelated renamed Enemy";
    }}),"Cannot edit live role fixture.");require(effects.reconcile(7,reopened.scene()),"Cannot reconcile role fixture edits.");
    near(value(player,stats::MaxHealth).current,120,"Player Health capacity did not follow edited Vitality.");
    near(value(player,stats::MaxMana).current,140,"Player Mana capacity did not follow edited Spirit.");
    near(value(player,stats::Health).current,80,"Editing Player primaries refilled Health.");
    near(value(player,stats::Mana).current,70,"Editing Player primaries refilled Mana.");
    near(value(first,stats::MaxHealth).current,45,"Enemy MaxHealth was not independently editable.");
    near(value(first,stats::Health).current,30,"Editing Enemy capacity refilled its depleted Health.");
    near(value(second,stats::Health).current,80,"Editing one Enemy changed another Enemy's Health.");
    require(effects.reconcile(7,reopened.scene()),"Unchanged role reconciliation failed.");
    near(value(player,stats::Health).current,80,"Repeated reconciliation refilled Player Health.");
    near(value(first,stats::Health).current,30,"Repeated reconciliation refilled Enemy Health.");
}
}
int main(){try{independentLifetimeAndSeeds();playMovementAndPools();persistedRoleRecipesHaveIndependentRuntimeSets();std::cout<<"Attribute runtime checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
