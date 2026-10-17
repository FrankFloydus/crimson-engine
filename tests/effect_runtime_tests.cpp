#include "runtime/EntityEffects.h"
#include "resources/AssetId.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,const char* message){require(std::abs(actual-expected)<1e-7,message);}
EffectModifier modifier(GameplayTag tag,double value,EffectModifierOperation operation=EffectModifierOperation::AddBase){return {std::move(tag),operation,EffectMagnitude{value,{},{}}};}
GameplayEffectAsset definition(std::string id,EffectDurationPolicy policy,std::vector<EffectModifier> modifiers){GameplayEffectAsset result;result.id=std::move(id);result.name=result.id;result.durationPolicy=policy;result.duration=1;result.modifiers=std::move(modifiers);return result;}
const std::string FixtureInitialization=canonicalAssetId("test:effects/fixture-init");
const std::string StartupInitialization=canonicalAssetId("test:effects/initialize");
const std::string StartupDamage=canonicalAssetId("test:effects/startup-damage");
const std::string StartupBuff=canonicalAssetId("test:effects/startup-buff");
EntityRecord actor(EntityId id){EntityRecord result;result.id=id;result.name="Actor";result.actor.emplace();result.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{FixtureInitialization}}};return result;}
struct World {
    Scene scene;EntityAttributes attributes;EntityEffects effects{attributes};std::map<std::string,EffectDefinitionSnapshot> definitions;
    World(){effects.setDefinitions([this](const std::string& id){const auto found=definitions.find(id);return found==definitions.end()?EffectDefinitionSnapshot{}:found->second;});put(definition(FixtureInitialization,EffectDurationPolicy::Instant,{modifier(stats::MaxHealth,100,EffectModifierOperation::Override),modifier(stats::MovementSpeed,10,EffectModifierOperation::Override)}));scene.apply({actor(1),actor(2),actor(3)},{});require(effects.reconcile(1,scene),"Initial effect fixture failed.");}
    void put(GameplayEffectAsset value,std::uint64_t generation=1){const auto id=value.id;definitions[id]={std::make_shared<const GameplayEffectAsset>(std::move(value)),generation,{}};}
    double current(EntityId id,const GameplayTag& tag){return attributes.value(id,tag).value().current;}
    double base(EntityId id,const GameplayTag& tag){return attributes.value(id,tag).value().base;}
    void ticks(unsigned count){for(unsigned index=0;index<count;++index)require(effects.step(EntityEffects::FixedStep),effects.status().c_str());}
};
void instantContinuousExpiryAndCapturedSources(){
    World world;world.put(definition("damage",EffectDurationPolicy::Instant,{modifier(stats::Health,-10)}));
    const auto damage=world.effects.apply(1,2,"damage");require(bool(damage)&&!damage.handle&&world.base(2,stats::Health)==90,"Instant effect did not execute Base exactly once without a handle.");
    auto speed=definition("speed",EffectDurationPolicy::HasDuration,{modifier(stats::MovementSpeed,2)});speed.duration=.1;world.put(speed);
    const auto applied=world.effects.apply(1,2,"speed");require(bool(applied)&&applied.handle&&world.current(2,stats::MovementSpeed)==12&&world.base(2,stats::MovementSpeed)==10,"Timed effect changed Base rather than Current.");
    world.ticks(5);require(world.current(2,stats::MovementSpeed)==12,"Duration expired before ceil-quantized fixed ticks.");world.ticks(1);require(world.current(2,stats::MovementSpeed)==10&&!world.effects.remove(applied.handle),"Expiry retained a modifier or a valid removed handle.");
    world.effects.setBase(1,stats::Vitality,10);auto poison=definition("captured",EffectDurationPolicy::Infinite,{{stats::Health,EffectModifierOperation::AddBase,{0,"SourceVitality",{{"coefficient",-1}}}}});poison.period=.05;poison.executeOnApplication=false;world.put(poison);
    require(bool(world.effects.apply(1,2,"captured")),"Cannot apply captured periodic effect.");world.effects.setBase(1,stats::Vitality,20);
    auto records=world.scene.records();std::erase_if(records,[](const auto& record){return record.id==1;});world.scene.apply(records,{});require(world.effects.reconcile(1,world.scene),"Source despawn reconcile failed.");world.ticks(3);
    require(world.base(2,stats::Health)==80,"Source changes/despawn altered a pinned periodic magnitude.");
}
void periodBoundaryAndNeverReset(){
    World world;auto poison=definition("boundary",EffectDurationPolicy::HasDuration,{modifier(stats::Health,-5)});poison.duration=.1;poison.period=.05;poison.executeOnApplication=false;world.put(poison);
    require(bool(world.effects.apply(1,2,"boundary")),"Cannot apply boundary poison.");world.ticks(6);require(world.base(2,stats::Health)==90&&world.effects.active(2).empty(),"A pulse due at expiry was skipped or continued after expiry.");
    auto repeated=definition("never-reset",EffectDurationPolicy::Infinite,{modifier(stats::Health,-5)});repeated.period=.05;repeated.stacking.type=EffectStackingType::AggregateByTarget;repeated.stacking.periodReset=EffectPeriodReset::NeverReset;world.put(repeated);
    const auto first=world.effects.apply(1,2,"never-reset");require(bool(first)&&world.base(2,stats::Health)==85,"Initial execute-on-application pulse did not execute.");world.ticks(1);
    const auto second=world.effects.apply(1,2,"never-reset");require(bool(second)&&second.handle==first.handle&&world.base(2,stats::Health)==85,"NeverReset reapplication executed an extra initial pulse.");world.ticks(2);require(world.base(2,stats::Health)==75,"NeverReset changed the next due tick or omitted stacked pulse scaling.");
    require(world.effects.remove(first.handle)&&world.base(2,stats::Health)==75,"Removing poison reversed past periodic damage.");
}
void stackingAndDefinitionGenerations(){
    World world;auto buff=definition("stack",EffectDurationPolicy::Infinite,{{stats::MovementSpeed,EffectModifierOperation::AddBase,{0,"SourceVitality",{}}}});buff.stacking.type=EffectStackingType::AggregateByTarget;buff.stacking.limit=1;world.put(buff);
    world.effects.setBase(1,stats::Vitality,2);auto first=world.effects.apply(1,2,"stack");require(bool(first)&&world.current(2,stats::MovementSpeed)==12,"Initial captured stack failed.");world.effects.setBase(1,stats::Vitality,7);auto capped=world.effects.apply(1,2,"stack");require(bool(capped)&&capped.handle==first.handle&&world.current(2,stats::MovementSpeed)==17&&world.effects.active(2)[0].stacks==1,"Accepted 1-to-1 reapply failed to replace its resolved specification.");
    buff.modifiers={modifier(stats::MovementSpeed,20)};world.put(buff,2);require(world.current(2,stats::MovementSpeed)==17,"Asset replacement changed an active pinned generation.");auto newer=world.effects.apply(1,2,"stack");require(bool(newer)&&newer.handle!=first.handle&&world.current(2,stats::MovementSpeed)==37&&world.effects.active(2).size()==2,"Different definition generations merged into one aggregate stack.");
    require(world.effects.remove(first.handle)&&world.current(2,stats::MovementSpeed)==30,"Handle removal changed the wrong generation.");
    auto deny=definition("deny",EffectDurationPolicy::Infinite,{modifier(stats::MovementSpeed,3)});deny.stacking.type=EffectStackingType::AggregateByTarget;deny.stacking.limit=1;deny.stacking.denyOverflowApplication=true;deny.stacking.clearStackOnOverflow=true;deny.grantedTags.add(stats::Spirit);world.put(deny);
    require(bool(world.effects.apply(1,2,"deny"))&&world.effects.effectiveTags(2).has(stats::Spirit,true),"Active effect did not grant a runtime tag.");const auto denied=world.effects.apply(1,2,"deny");require(denied.status==EffectApplyStatus::Rejected&&!world.effects.effectiveTags(2).has(stats::Spirit,true)&&world.current(2,stats::MovementSpeed)==30,"Deny+clear overflow refreshed/accepted the effect or retained its modifier/tag.");
}
void refreshExpiryStacksAndTags(){
    World world;auto buff=definition("extend",EffectDurationPolicy::HasDuration,{modifier(stats::MovementSpeed,2)});buff.duration=4*EntityEffects::FixedStep;buff.stacking.type=EffectStackingType::AggregateByTarget;buff.stacking.durationRefresh=EffectDurationRefresh::ExtendDuration;buff.stacking.maxDuration=10*EntityEffects::FixedStep;buff.stacking.expiration=EffectStackExpiration::RemoveSingleStackAndRefreshDuration;buff.grantedTags.add(stats::Spirit);world.put(buff);
    const auto applied=world.effects.apply(1,2,"extend");world.ticks(2);require(bool(world.effects.apply(1,2,"extend")),"Cannot extend active stack.");near(world.effects.active(2)[0].remaining.value(),6*EntityEffects::FixedStep,"Extend duration did not add incoming duration to remaining time.");world.ticks(6);require(world.effects.active(2)[0].stacks==1&&world.current(2,stats::MovementSpeed)==12,"Single-stack expiry did not decrement/recompute.");near(world.effects.active(2)[0].remaining.value(),6*EntityEffects::FixedStep,"Expiry refresh did not use the stored extended duration.");world.ticks(6);require(world.effects.active(2).empty()&&world.current(2,stats::MovementSpeed)==10,"Final stack did not expire.");
    auto first=definition("tag1",EffectDurationPolicy::Infinite,{}),second=definition("tag2",EffectDurationPolicy::Infinite,{});first.grantedTags.add(stats::Vitality);second.grantedTags.add(stats::Vitality);world.put(first);world.put(second);const auto a=world.effects.apply(1,2,"tag1"),b=world.effects.apply(1,2,"tag2");require(bool(a)&&bool(b)&&world.effects.remove(a.handle)&&world.effects.effectiveTags(2).has(stats::Vitality,true),"Removing one effect lost a shared granted-tag reference.");require(world.effects.remove(b.handle)&&!world.effects.effectiveTags(2).has(stats::Vitality,true),"Final granted-tag removal retained a runtime tag.");
    (void)applied;
}
void bootstrapReadinessDamageAndMembership(){
    World world;auto entity=actor(9);entity.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{StartupInitialization},AssetRef<GameplayEffectAsset>{StartupDamage},AssetRef<GameplayEffectAsset>{StartupBuff}}};
    world.put(definition(StartupInitialization,EffectDurationPolicy::Instant,{modifier(stats::Vitality,50,EffectModifierOperation::Override),modifier(stats::MovementSpeed,5,EffectModifierOperation::Override)}));
    world.put(definition(StartupDamage,EffectDurationPolicy::Instant,{modifier(stats::Health,-15),modifier(stats::Vitality,60,EffectModifierOperation::Override)}));
    world.definitions[StartupBuff]={{},0,"Preparing test definition",EffectReadiness::Preparing};auto records=world.scene.records();records.push_back(entity);world.scene.apply(records,{});
    require(!world.effects.reconcile(1,world.scene)&&world.effects.readiness(9)==EffectReadiness::Preparing&&!world.attributes.value(9,stats::Health)&&world.effects.active(9).empty(),"Incomplete starting definitions published a partial attribute/effect world.");
    require(world.effects.step(EntityEffects::FixedStep),"A preparing actor stopped other runtime effect ticking.");
    world.put(definition(StartupBuff,EffectDurationPolicy::Infinite,{modifier(stats::Vitality,10)}));require(world.effects.reconcile(1,world.scene),world.effects.status().c_str());require(world.current(9,stats::MaxHealth)==140&&world.current(9,stats::Health)==105,"Bootstrap refilled startup damage or omitted continuous capacity modifiers.");
    require(world.attributes.changes(9).size()==5,"Bootstrap published incomplete native closure notifications.");world.effects.addBase(9,stats::Health,-5);require(world.effects.reconcile(1,world.scene)&&world.current(9,stats::Health)==100,"Reconciliation replayed starting effects or healed depletion.");
    records=world.scene.records();for(auto& record:records)if(record.id==9){record.effects.reset();record.name="Changed authored setup";}world.scene.apply(records,{});
    require(world.effects.reconcile(1,world.scene)&&world.current(9,stats::Health)==100&&world.current(9,stats::Vitality)==70,"Changing authored setup erased activated stats or replayed startup.");
}
void atomicFailuresReferencesAndRegen(){
    World world;auto invalid=definition("invalid",EffectDurationPolicy::Instant,{modifier(stats::Health,-10),modifier(GameplayTag{"Stats.Unknown"},5)});world.put(invalid);require(!bool(world.effects.apply(1,2,"invalid"))&&world.base(2,stats::Health)==100,"Unknown native attribute partially executed a batch.");
    auto gated=definition("gated",EffectDurationPolicy::Infinite,{modifier(stats::MovementSpeed,2)});gated.targetRequired.add(stats::Spirit);world.put(gated);require(!bool(world.effects.apply(1,2,"gated"))&&world.effects.active(2).empty(),"Application tag gates were ignored.");
    auto records=world.scene.records();for(auto& record:records)if(record.id==2)record.tags=GameplayTagContainer({stats::Spirit});world.scene.apply(records,{});require(world.effects.reconcile(1,world.scene),"Cannot update authored tags.");require(bool(world.effects.apply(1,2,"gated")),"Authored tags were not included in effective application tags.");
    world.effects.setBase(2,stats::Health,50);world.effects.setBase(2,stats::HealthRegen,2);auto regen=definition("regen",EffectDurationPolicy::Infinite,{modifier(stats::HealthRegen,2,EffectModifierOperation::MultiplyCompound)});world.put(regen);const auto applied=world.effects.apply(1,2,"regen");require(bool(applied),"Cannot modify intrinsic regeneration rate.");world.ticks(60);near(world.base(2,stats::Health),54,"Intrinsic regeneration ran twice or ignored buffed Current.");
    const auto stale=world.effects.reference(2);const auto oldHandle=applied.handle;records=world.scene.records();std::erase_if(records,[](const auto& record){return record.id==2;});world.scene.apply(records,{});world.effects.reconcile(1,world.scene);records.push_back(actor(2));world.scene.apply(records,{});world.effects.reconcile(1,world.scene);
    require(world.effects.reference(2)!=stale&&!bool(world.effects.apply(world.effects.reference(1),stale,"gated"))&&!world.effects.remove(oldHandle),"Despawn/reused IDs accepted stale references or handles.");
    const auto before=world.effects.reference(1);world.effects.clear();require(world.effects.reconcile(1,world.scene)&&world.effects.reference(1).world!=before.world,"Stop/restart reused effect world identity.");
}
void failedTickRecoveryAndDefinitionStatuses(){
    World world;world.definitions["failed"]={{},0,"Invalid test definition",EffectReadiness::Failed};
    world.definitions["pending"]={{},0,"Preparing test definition",EffectReadiness::Preparing};
    require(world.effects.apply(1,2,"failed").status==EffectApplyStatus::Failed&&world.effects.apply(1,2,"pending").status==EffectApplyStatus::Preparing&&world.effects.apply(1,2,"missing").status==EffectApplyStatus::Unavailable,"Definition failure/readiness statuses were conflated.");
    require(world.effects.setBase(2,stats::Health,50)&&world.effects.setBase(2,stats::HealthRegen,60),"Cannot initialize recovery-test values.");
    auto growth=definition("overflow",EffectDurationPolicy::Infinite,{modifier(stats::MovementSpeed,1e200,EffectModifierOperation::MultiplyCompound)});growth.period=EntityEffects::FixedStep;growth.executeOnApplication=false;world.put(growth);const auto applied=world.effects.apply(1,2,"overflow");require(bool(applied),"Cannot apply overflow recovery fixture.");
    require(!world.effects.step(3*EntityEffects::FixedStep)&&world.base(2,stats::Health)==51,"A failing tick partially published or discarded an earlier successful tick.");
    require(world.effects.remove(applied.handle)&&world.effects.step(0),"Effect failure could not recover after removal.");
    near(world.base(2,stats::Health),53,"Recovery replayed time already committed before the failing tick.");
}
void nativeCatalogTargetsActivateWithoutAuthoring(){
    World world;const auto has=[&](EntityId id,const GameplayTag& tag){const auto values=world.attributes.snapshot(id);return std::any_of(values.begin(),values.end(),[&](const auto& value){return value.tag==tag;});};
    require(!has(2,stats::Spirit)&&!has(2,stats::Mana)&&world.current(2,stats::MovementSpeed)==10,"Enemy-like fixture unexpectedly started with Player-only attributes.");
    world.effects.addBase(2,stats::Health,-20);auto spirit=definition("spirit",EffectDurationPolicy::Infinite,{modifier(stats::Spirit,25)});world.put(spirit);
    const auto applied=world.effects.apply(1,2,"spirit");require(bool(applied)&&has(2,stats::Spirit)&&has(2,stats::MaxMana)&&has(2,stats::Mana)&&has(2,stats::ManaRegen),"A known native effect target did not materialize its formula/bound dependents.");
    near(world.current(2,stats::Spirit),25,"Enemy Spirit did not receive the modifier.");near(world.current(2,stats::MaxMana),50,"Native MaxMana did not derive from introduced Spirit.");near(world.current(2,stats::ManaRegen),2.5,"Native ManaRegen did not derive from introduced Spirit.");
    near(world.current(2,stats::Mana),0,"A resource introduced after startup received an implicit full refill.");near(world.current(2,stats::Health),80,"Introducing Spirit refilled an existing unrelated resource.");
    require(world.effects.reconcile(1,world.scene)&&world.current(2,stats::Spirit)==25&&has(2,stats::Mana),"Scene reconciliation erased effect-introduced attributes.");
    require(world.effects.remove(applied.handle)&&has(2,stats::Spirit)&&world.current(2,stats::Spirit)==0&&world.current(2,stats::MaxMana)==0,"Removing a continuous effect failed to reverse its contribution or erased its materialized native stat.");
    EntityRecord empty;empty.id=9;empty.name="No authored attributes or effects";auto records=world.scene.records();records.push_back(empty);world.scene.apply(records,{});require(world.effects.reconcile(1,world.scene)&&world.attributes.snapshot(9).empty(),"An effect-free entity did not bootstrap an empty sparse runtime set.");
    near(world.current(9,stats::MovementSpeed),5,"A known unmaterialized value did not resolve its native default.");require(world.attributes.snapshot(9).empty(),"Reading a native default mutated the sparse runtime set.");
    auto captured=definition("default-capture",EffectDurationPolicy::Instant,{{stats::Spirit,EffectModifierOperation::AddBase,{0,"TargetMaxHealth",{{"offset",25}}}}});world.put(captured);
    require(bool(world.effects.apply(9,9,"default-capture"))&&world.current(9,stats::Spirit)==25&&world.current(9,stats::MaxMana)==50&&world.current(9,stats::Mana)==0,"An entity without an authored Attributes component could not receive a known native effect/default capture.");
    auto initialize=definition(canonicalAssetId("test:effects/native-spirit-bootstrap"),EffectDurationPolicy::Instant,{modifier(stats::Spirit,25)});world.put(initialize);empty.id=10;empty.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{initialize.id}}};records=world.scene.records();records.push_back(empty);world.scene.apply(records,{});require(world.effects.reconcile(1,world.scene)&&world.current(10,stats::Mana)==50,"A startup-introduced native resource did not spawn full at its resolved capacity.");
}
}
int main(){try{instantContinuousExpiryAndCapturedSources();periodBoundaryAndNeverReset();stackingAndDefinitionGenerations();refreshExpiryStacksAndTags();bootstrapReadinessDamageAndMembership();atomicFailuresReferencesAndRegen();failedTickRecoveryAndDefinitionStatuses();nativeCatalogTargetsActivateWithoutAuthoring();std::cout<<"Runtime effects, native catalog activation, startup readiness, stacks, tags, captured generations, timers and lifetime checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
