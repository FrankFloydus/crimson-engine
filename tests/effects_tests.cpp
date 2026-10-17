#include "gameplay/Effects.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,const char* message){require(std::abs(actual-expected)<1e-8,message);}
template<class Action>void rejects(Action action,const char* message){try{action();}catch(const std::invalid_argument&){return;}throw std::runtime_error(message);}
void currentAggregatorAndBaseAtomicity(){
    AttributeSet values;require(values.reconcile({{stats::MovementSpeed,10}}),"Cannot prepare modifier set.");
    const AttributeCurrentModifier modifiers[]{{stats::MovementSpeed,EffectModifierOperation::AddBase,5},{stats::MovementSpeed,EffectModifierOperation::MultiplyAdditive,1.2},{stats::MovementSpeed,EffectModifierOperation::MultiplyAdditive,1.3},{stats::MovementSpeed,EffectModifierOperation::DivideAdditive,2},{stats::MovementSpeed,EffectModifierOperation::MultiplyCompound,2},{stats::MovementSpeed,EffectModifierOperation::MultiplyCompound,.5},{stats::MovementSpeed,EffectModifierOperation::AddFinal,2}};
    require(values.setCurrentModifiers(modifiers),values.status().c_str());near(values.value(stats::MovementSpeed)->base,10,"Continuous modifiers changed Base.");near(values.value(stats::MovementSpeed)->current,13.25,"Six-operation aggregation did not use biased sums/compound/final order.");
    const AttributeCurrentModifier overrides[]{{stats::MovementSpeed,EffectModifierOperation::Override,7},{stats::MovementSpeed,EffectModifierOperation::Override,9},{stats::MovementSpeed,EffectModifierOperation::AddFinal,100}};
    require(values.setCurrentModifiers(overrides)&&values.value(stats::MovementSpeed)->current==7,"Override did not select first active application/modifier order.");
    const AttributeCurrentModifier invalid[]{{stats::MovementSpeed,EffectModifierOperation::DivideAdditive,0}};
    require(!values.setCurrentModifiers(invalid)&&values.value(stats::MovementSpeed)->current==7,"Invalid continuous division partially published.");
    require(values.setCurrentModifiers({})&&values.value(stats::MovementSpeed)==AttributeValue{10,10},"Removing Current contributions did not recompute from Base.");
    const AttributeBaseChange operations[]{{stats::MovementSpeed,2,AttributeBaseOperation::Multiply},{stats::MovementSpeed,4,AttributeBaseOperation::Add},{stats::MovementSpeed,3,AttributeBaseOperation::Divide}};
    require(values.applyBaseChanges(operations)&&values.value(stats::MovementSpeed)==AttributeValue{8,8},"Instant operations did not execute in authored order on Base.");
    const auto previous=values.snapshot();const AttributeBaseChange bad[]{{stats::MovementSpeed,5},{stats::MovementSpeed,0,AttributeBaseOperation::Divide}};
    require(!values.applyBaseChanges(bad)&&values.snapshot()==previous,"Invalid Base batch did not roll back atomically.");
}
void nativeDependencyAndPendingPools(){
    AttributeSet values;require(values.beginBootstrap({{stats::Health,0},{stats::MaxHealth,0},{stats::Vitality,0}}),"Cannot begin pool bootstrap.");
    const AttributeBaseChange start[]{{stats::Health,-15,AttributeBaseOperation::Add},{stats::Vitality,60}};
    require(values.applyBaseChanges(start)&&values.value(stats::Health)==AttributeValue{105,105},"Startup damage did not use the application's final resolved capacity.");
    const AttributeCurrentModifier vitality[]{{stats::Vitality,EffectModifierOperation::AddBase,10}};
    require(values.setCurrentModifiers(vitality)&&values.finishBootstrap()&&values.value(stats::MaxHealth)==AttributeValue{0,140}&&values.value(stats::Health)==AttributeValue{105,105},"Later startup capacity growth refilled deliberate startup damage.");
    require(values.setCurrentModifiers({})&&values.value(stats::Health)==AttributeValue{105,105},"Removing a buff rewound resource damage.");
    require(values.setBase(stats::Vitality,30)&&values.value(stats::Health)==AttributeValue{60,60},"Capacity reduction failed the existing durable clamp rule.");
    require(values.setBase(stats::Vitality,60)&&values.value(stats::Health)==AttributeValue{60,60},"Restoring capacity resurrected clamped Health.");
    AttributeSet full;require(full.beginBootstrap({{stats::Health,0},{stats::MaxHealth,0},{stats::Vitality,0}})&&full.setBase(stats::Vitality,80)&&full.finishBootstrap()&&full.value(stats::Health)==AttributeValue{160,160},"Untouched startup pool did not fill at the final derived capacity.");
}
void registeredSnapshotCalculations(){
    const auto& registry=nativeEffectCalculations();require(registry.find("SourceVitality")&&registry.find("TargetMaxHealth"),"Native effect calculations were not registered.");
    const std::vector<AttributeSnapshot> source{{stats::Vitality,{10,12}}},target{{stats::MaxHealth,{100,140}}};const EffectCalculationContext context{source,target};
    const EffectMagnitude magnitude{0,"SourceVitality",{{"coefficient",2},{"offset",3}}};near(registry.resolve(magnitude,context).value(),27,"Native magnitude did not read captured Current plus scalar parameters.");
    require(!registry.resolve(EffectMagnitude{0,"Missing",{}},context)&&!registry.resolve(EffectMagnitude{0,"SourceVitality",{{"typo",2}}},context)&&!registry.resolve(magnitude,EffectCalculationContext{}),"Unresolved native calculations/parameters/captures were silently accepted.");
    GameplayEffectAsset effect;effect.id="test";effect.name="Test";effect.modifiers.push_back({stats::Health,EffectModifierOperation::AddBase,magnitude});validateGameplayEffect(effect);
    effect.durationPolicy=EffectDurationPolicy::HasDuration;effect.duration=0;rejects([&]{validateGameplayEffect(effect);},"A zero-duration timed effect passed validation.");
    effect.duration=1;effect.period=std::numeric_limits<double>::infinity();rejects([&]{validateGameplayEffect(effect);},"An infinite period passed validation.");
    effect.period=0;effect.modifiers[0].attribute=GameplayTag{"Stats.Future"};validateGameplayEffect(effect);
    rejects([&]{validateGameplayEffect(effect,nativeAttributeCatalog(),registry,false);},"Runtime validation accepted an unknown native attribute.");
}
void metadataDrivenActivation(){
    const GameplayTag power{"Stats.TestPower"},limit{"Stats.TestLimit"},pool{"Stats.TestPool"};
    AttributeDefinition powerDefinition;powerDefinition.tag=power;powerDefinition.displayName="Power";powerDefinition.defaultBase=2;
    AttributeDefinition limitDefinition;limitDefinition.tag=limit;limitDefinition.displayName="Limit";limitDefinition.contributions={{power,3,true}};
    AttributeDefinition poolDefinition;poolDefinition.tag=pool;poolDefinition.displayName="Pool";poolDefinition.maximum=AttributeBound::reference(limit);poolDefinition.initializeToMaximum=true;
    const AttributeCatalog catalog(GameplayTagRegistry({power,limit,pool}),{powerDefinition,limitDefinition,poolDefinition});AttributeSet values(catalog);
    AttributeSet defaults(catalog);require(defaults.materialize(std::span{&limit,1})&&defaults.value(limit)->current==6&&!defaults.value(power),"An unmaterialized native input lost its C++ default contribution or was implicitly stored.");
    require(values.materialize(std::span{&power,1})&&values.snapshot().size()==3,"Activation did not follow custom native formula/bound metadata.");
    require(values.setBase(power,7)&&values.value(limit)->current==21&&values.value(pool)->current==0,"New runtime resource did not retain its native default while its capacity followed the formula.");
    const auto before=values.snapshot();const GameplayTag unknown{"Stats.NotRegistered"};require(!values.materialize(std::span{&unknown,1})&&values.snapshot()==before,"Unknown activation partially published.");
    AttributeSet empty;require(empty.resolvedValue(stats::MovementSpeed)==AttributeValue{5,5}&&empty.snapshot().empty(),"Default resolution mutated sparse storage or ignored native default.");
    require(empty.resolvedSnapshot().size()==nativeAttributeCatalog().definitions().size()&&empty.snapshot().empty(),"Read-only captures did not expand native defaults independently from sparse storage.");
}
}
int main(){try{currentAggregatorAndBaseAtomicity();nativeDependencyAndPendingPools();registeredSnapshotCalculations();metadataDrivenActivation();std::cout<<"Effect aggregator, native activation, bootstrap pools, atomic Base operations and native calculation checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
