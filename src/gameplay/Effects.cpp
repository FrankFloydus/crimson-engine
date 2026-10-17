#include "Effects.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace crimson {
namespace {
void finite(double value,const char* context){if(!std::isfinite(value))throw std::invalid_argument(std::string(context)+" must be finite.");}
std::optional<double> captured(std::span<const AttributeSnapshot> values,const GameplayTag& tag) {
    const auto found=std::find_if(values.begin(),values.end(),[&](const auto& value){return value.tag==tag;});
    return found==values.end()?std::nullopt:std::optional{found->value.current};
}
}
EffectCalculationRegistry::EffectCalculationRegistry(std::vector<EffectCalculationDefinition> definitions):definitions_(std::move(definitions)) {
    if(definitions_.size()>256)throw std::invalid_argument("Native effect calculation catalog exceeds 256 definitions.");
    std::sort(definitions_.begin(),definitions_.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    for(std::size_t index=0;index<definitions_.size();++index) {
        const auto& definition=definitions_[index];
        if(definition.id.empty()||definition.id.size()>256||!definition.calculate||(index&&definitions_[index-1].id==definition.id))throw std::invalid_argument("Invalid or duplicate native effect calculation.");
        if(definition.parameters.size()>32)throw std::invalid_argument("Effect calculation exceeds 32 scalar parameters.");
        std::set<std::string> names;
        for(const auto& parameter:definition.parameters) {
            if(parameter.name.empty()||parameter.name.size()>128||!names.insert(parameter.name).second)throw std::invalid_argument("Invalid effect calculation parameter name.");
            finite(parameter.defaultValue,"Calculation parameter default");
            if(parameter.minimum)finite(*parameter.minimum,"Calculation parameter minimum");if(parameter.maximum)finite(*parameter.maximum,"Calculation parameter maximum");
            if((parameter.minimum&&parameter.defaultValue<*parameter.minimum)||(parameter.maximum&&parameter.defaultValue>*parameter.maximum))throw std::invalid_argument("Calculation parameter default is outside its bounds.");
        }
    }
}
const EffectCalculationDefinition* EffectCalculationRegistry::find(std::string_view id) const {
    const auto found=std::lower_bound(definitions_.begin(),definitions_.end(),id,[](const auto& definition,const auto& key){return definition.id<key;});
    return found==definitions_.end()||found->id!=id?nullptr:&*found;
}
std::optional<double> EffectCalculationRegistry::resolve(const EffectMagnitude& magnitude,const EffectCalculationContext& context) const {
    if(magnitude.calculation.empty())return std::isfinite(magnitude.literal)?std::optional{magnitude.literal}:std::nullopt;
    const auto* definition=find(magnitude.calculation);if(!definition)return {};
    std::vector<EffectMagnitudeParameter> parameters;parameters.reserve(definition->parameters.size());
    std::set<std::string> supplied;
    for(const auto& input:magnitude.parameters)if(!std::isfinite(input.value)||!supplied.insert(input.name).second||
        std::none_of(definition->parameters.begin(),definition->parameters.end(),[&](const auto& parameter){return parameter.name==input.name;}))return {};
    for(const auto& parameter:definition->parameters) {
        const auto input=std::find_if(magnitude.parameters.begin(),magnitude.parameters.end(),[&](const auto& value){return value.name==parameter.name;});
        const double value=input==magnitude.parameters.end()?parameter.defaultValue:input->value;
        if((parameter.minimum&&value<*parameter.minimum)||(parameter.maximum&&value>*parameter.maximum))return {};
        parameters.push_back({parameter.name,value});
    }
    try{const auto result=definition->calculate(context,parameters);return result&&std::isfinite(*result)?result:std::nullopt;}catch(const std::exception&){return {};}
}
const EffectCalculationRegistry& nativeEffectCalculations() {
    static const EffectCalculationRegistry registry([] {
        std::vector<EffectCalculationDefinition> result;
        for(const bool source:{true,false}) {
            EffectCalculationDefinition definition;definition.id=source?"SourceVitality":"TargetMaxHealth";definition.name=source?"Source Vitality":"Target Max Health";
            definition.parameters={{"coefficient",1,{},{}},{"offset",0,{},{}}};
            definition.calculate=[source](const EffectCalculationContext& context,std::span<const EffectMagnitudeParameter> parameters)->std::optional<double> {
                const auto input=captured(source?context.source:context.target,source?stats::Vitality:stats::MaxHealth);if(!input)return {};
                return *input*parameters[0].value+parameters[1].value;
            };result.push_back(std::move(definition));
        }return result;
    }());return registry;
}
void validateGameplayEffect(const GameplayEffectAsset& effect,const AttributeCatalog& catalog,const EffectCalculationRegistry& calculations,bool allowUnresolved) {
    if(effect.id.empty()||effect.id.size()>256||effect.name.size()>1024||effect.modifiers.size()>1024)throw std::invalid_argument("Invalid Gameplay Effect identity, name or modifier count.");
    finite(effect.duration,"Effect duration");finite(effect.period,"Effect period");finite(effect.stacking.maxDuration,"Maximum stacked duration");
    if(effect.duration<0||effect.period<0||effect.stacking.maxDuration<0)throw std::invalid_argument("Effect times must be nonnegative.");
    switch(effect.durationPolicy){case EffectDurationPolicy::Instant:break;case EffectDurationPolicy::HasDuration:if(effect.duration<=0)throw std::invalid_argument("Duration effects need a positive duration.");break;case EffectDurationPolicy::Infinite:break;default:throw std::invalid_argument("Invalid effect duration policy.");}
    switch(effect.stacking.type){case EffectStackingType::None:case EffectStackingType::AggregateBySource:case EffectStackingType::AggregateByTarget:break;default:throw std::invalid_argument("Invalid effect stacking type.");}
    switch(effect.stacking.durationRefresh){case EffectDurationRefresh::RefreshOnSuccessfulApplication:case EffectDurationRefresh::NeverRefresh:case EffectDurationRefresh::ExtendDuration:break;default:throw std::invalid_argument("Invalid effect duration refresh policy.");}
    switch(effect.stacking.periodReset){case EffectPeriodReset::ResetOnSuccessfulApplication:case EffectPeriodReset::NeverReset:break;default:throw std::invalid_argument("Invalid effect period reset policy.");}
    switch(effect.stacking.expiration){case EffectStackExpiration::ClearEntireStack:case EffectStackExpiration::RemoveSingleStackAndRefreshDuration:case EffectStackExpiration::RefreshDuration:break;default:throw std::invalid_argument("Invalid effect expiration policy.");}
    for(const auto& modifier:effect.modifiers) {
        if(!modifier.attribute.valid()||(!allowUnresolved&&!catalog.find(modifier.attribute)))throw std::invalid_argument("Effect modifier has an unavailable attribute.");
        switch(modifier.operation){case EffectModifierOperation::AddBase:case EffectModifierOperation::MultiplyAdditive:case EffectModifierOperation::DivideAdditive:case EffectModifierOperation::MultiplyCompound:case EffectModifierOperation::AddFinal:case EffectModifierOperation::Override:break;default:throw std::invalid_argument("Invalid effect modifier operation.");}
        finite(modifier.magnitude.literal,"Effect literal magnitude");if(modifier.magnitude.parameters.size()>32)throw std::invalid_argument("Effect magnitude exceeds 32 parameters.");
        if(modifier.magnitude.calculation.size()>256)throw std::invalid_argument("Native calculation identifier is too long.");
        if(!allowUnresolved&&!modifier.magnitude.calculation.empty()&&!calculations.find(modifier.magnitude.calculation))throw std::invalid_argument("Native effect calculation is unavailable.");
        std::set<std::string> parameters;for(const auto& parameter:modifier.magnitude.parameters){if(parameter.name.empty()||parameter.name.size()>128||!parameters.insert(parameter.name).second)throw std::invalid_argument("Invalid or duplicate effect magnitude parameter.");finite(parameter.value,"Effect magnitude parameter");}
    }
    for(const auto* tags:{&effect.assetTags,&effect.grantedTags,&effect.sourceRequired,&effect.sourceBlocked,&effect.targetRequired,&effect.targetBlocked}) {
        if(tags->tags().size()>1024)throw std::invalid_argument("Effect tag container exceeds 1024 entries.");
        if(!allowUnresolved)validateGameplayTags(*tags,catalog.registry());
    }
}
}
