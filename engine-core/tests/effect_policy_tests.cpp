#include "runtime/EntityEffects.h"
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected,const char* message){require(std::abs(actual-expected)<1e-7,message);}
struct Fixture {
    Scene scene;EntityAttributes attributes;EntityEffects runtime{attributes};
    std::map<std::string,EffectDefinitionSnapshot> definitions;
    Fixture(){std::vector<EntityRecord> records;for(EntityId id=1;id<=3;++id){EntityRecord record;record.id=id;record.name="Actor";records.push_back(record);}
        scene.apply(records,{});runtime.setDefinitions([&](const auto& id){return definitions.at(id);});
        require(runtime.reconcile(1,scene),"Cannot initialize entity attribute runtime.");
        near(attributes.value(2,stats::MovementSpeed)->current,5,"Known native attribute did not resolve its default.");
        require(runtime.setBase(2,stats::MovementSpeed,10),"Cannot initialize test speed.");}
    void put(GameplayEffectAsset effect){const auto id=effect.id;definitions[id]={std::make_shared<const GameplayEffectAsset>(std::move(effect)),1,{}};}
    void ticks(unsigned count){while(count--)require(runtime.step(EntityEffects::FixedStep),"Policy tick failed.");}
    double speed(){return attributes.value(2,stats::MovementSpeed)->current;}
};
GameplayEffectAsset effect(EffectModifierOperation op,double value){GameplayEffectAsset result;result.id="policy";result.name="Policy";result.durationPolicy=EffectDurationPolicy::Infinite;
    result.modifiers={{stats::MovementSpeed,op,{value}}};result.stacking.type=EffectStackingType::AggregateByTarget;return result;}
void magnitudeScaling(){
    struct Case {EffectModifierOperation op;double magnitude,expected;};
    for(const auto& item:{Case{EffectModifierOperation::AddBase,2,14},Case{EffectModifierOperation::AddFinal,2,14},
        Case{EffectModifierOperation::MultiplyAdditive,1.5,20},Case{EffectModifierOperation::DivideAdditive,1.5,5},
        Case{EffectModifierOperation::MultiplyCompound,1.5,22.5},Case{EffectModifierOperation::Override,7,7}}){
        Fixture fixture;fixture.put(effect(item.op,item.magnitude));
        const auto first=fixture.runtime.apply(1,2,"policy"),second=fixture.runtime.apply(1,2,"policy");
        require(bool(first)&&bool(second)&&first.handle==second.handle,"Repeated application did not aggregate.");
        near(fixture.speed(),item.expected,"Stack count used the wrong operation scaling.");
        require(fixture.runtime.remove(first.handle,1)&&fixture.runtime.active(2)[0].stacks==1,"Partial stack removal failed.");
    }
    Fixture fixture;auto buff=effect(EffectModifierOperation::AddBase,2);buff.stacking.factorInStackCount=false;fixture.put(buff);
    fixture.runtime.apply(1,2,"policy");fixture.runtime.apply(1,2,"policy");near(fixture.speed(),12,"FactorInStackCount=false still scaled modifiers.");
}
void grouping(){
    Fixture fixture;auto buff=effect(EffectModifierOperation::AddBase,2);buff.stacking.type=EffectStackingType::AggregateBySource;fixture.put(buff);
    const auto first=fixture.runtime.apply(1,2,"policy"),same=fixture.runtime.apply(1,2,"policy"),other=fixture.runtime.apply(3,2,"policy");
    require(bool(first)&&bool(same)&&bool(other)&&first.handle==same.handle&&first.handle!=other.handle&&fixture.runtime.active(2).size()==2,"Source aggregation merged different sources or split one source.");
    near(fixture.speed(),16,"Source groups lost stack contributions.");
    Fixture independent;buff.stacking.type=EffectStackingType::None;independent.put(buff);
    require(independent.runtime.apply(1,2,"policy").handle!=independent.runtime.apply(1,2,"policy").handle&&independent.runtime.active(2).size()==2,"No stacking did not retain independent instances.");
}
void durationPolicies(){
    for(const bool refresh:{false,true}){
        Fixture fixture;auto buff=effect(EffectModifierOperation::AddBase,2);buff.durationPolicy=EffectDurationPolicy::HasDuration;buff.duration=4*EntityEffects::FixedStep;
        buff.stacking.limit=1;buff.stacking.durationRefresh=refresh?EffectDurationRefresh::RefreshOnSuccessfulApplication:EffectDurationRefresh::NeverRefresh;fixture.put(buff);
        require(bool(fixture.runtime.apply(1,2,"policy")),"Cannot install timed policy.");fixture.ticks(2);require(bool(fixture.runtime.apply(1,2,"policy")),"Capped reapplication was denied.");fixture.ticks(2);
        near(fixture.speed(),refresh?12:10,"Duration refresh policy ignored capped reapplication.");fixture.ticks(2);near(fixture.speed(),10,"Refreshed duration never expired.");
    }
    Fixture deny;auto buff=effect(EffectModifierOperation::AddBase,2);buff.durationPolicy=EffectDurationPolicy::HasDuration;buff.duration=4*EntityEffects::FixedStep;
    buff.stacking.limit=1;buff.stacking.denyOverflowApplication=true;deny.put(buff);deny.runtime.apply(1,2,"policy");deny.ticks(2);
    require(!bool(deny.runtime.apply(1,2,"policy")),"Denied overflow was accepted.");deny.ticks(2);near(deny.speed(),10,"Denied overflow refreshed duration.");
    Fixture persistent;buff.stacking.denyOverflowApplication=false;buff.stacking.expiration=EffectStackExpiration::RefreshDuration;persistent.put(buff);const auto handle=persistent.runtime.apply(1,2,"policy").handle;
    persistent.ticks(12);near(persistent.speed(),12,"RefreshDuration removed the effect.");require(persistent.runtime.remove(handle),"Refreshed duration could not be explicitly removed.");
}
void resetPeriodAndQuantization(){
    Fixture fixture;auto pulse=effect(EffectModifierOperation::AddBase,1);pulse.period=4*EntityEffects::FixedStep;pulse.executeOnApplication=false;fixture.put(pulse);
    fixture.runtime.apply(1,2,"policy");fixture.ticks(2);fixture.runtime.apply(1,2,"policy");fixture.ticks(2);near(fixture.speed(),10,"Default period reset did not restart its timer.");
    fixture.ticks(2);near(fixture.speed(),12,"Reset period did not execute the stacked Base pulse.");
    Fixture quantized;auto timed=effect(EffectModifierOperation::AddBase,2);timed.durationPolicy=EffectDurationPolicy::HasDuration;timed.duration=1.1*EntityEffects::FixedStep;quantized.put(timed);quantized.runtime.apply(1,2,"policy");
    quantized.ticks(1);near(quantized.speed(),12,"Positive fractional duration rounded down.");quantized.ticks(1);near(quantized.speed(),10,"Fractional duration did not expire at its ceiling tick.");
}
}
int main(){try{magnitudeScaling();grouping();durationPolicies();resetPeriodAndQuantization();std::cout<<"Effect operation, grouping, duration, overflow and period policies passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
