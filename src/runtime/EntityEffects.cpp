#include "EntityEffects.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace crimson {
namespace {
std::atomic_uint64_t nextEffectWorld{1};
std::uint64_t ticksFor(double seconds) {
    if(!std::isfinite(seconds)||seconds<0||seconds/EntityEffects::FixedStep>double(std::numeric_limits<std::uint64_t>::max()/4))throw std::invalid_argument("Effect time exceeds the simulation tick range.");
    return seconds>0?std::max<std::uint64_t>(1,static_cast<std::uint64_t>(std::ceil(seconds/EntityEffects::FixedStep-1e-10))):0;
}
std::uint64_t later(std::uint64_t now,std::uint64_t duration) {
    if(duration>std::numeric_limits<std::uint64_t>::max()-now)throw std::invalid_argument("Effect timer exceeds the simulation tick range.");return now+duration;
}
EffectApplyStatus applicationReadiness(EffectReadiness readiness) {
    return readiness==EffectReadiness::Preparing?EffectApplyStatus::Preparing:readiness==EffectReadiness::Failed?EffectApplyStatus::Failed:EffectApplyStatus::Unavailable;
}
struct ResolvedModifier {GameplayTag attribute;EffectModifierOperation operation;double magnitude;};
double stackedMagnitude(const ResolvedModifier& modifier,std::uint32_t stacks,bool factor) {
    const double count=factor?stacks:1;
    switch(modifier.operation) {
    case EffectModifierOperation::AddBase:case EffectModifierOperation::AddFinal:return modifier.magnitude*count;
    case EffectModifierOperation::MultiplyAdditive:case EffectModifierOperation::DivideAdditive:return 1+(modifier.magnitude-1)*count;
    case EffectModifierOperation::MultiplyCompound:return std::pow(modifier.magnitude,count);
    case EffectModifierOperation::Override:return modifier.magnitude;
    default:throw std::invalid_argument("Invalid resolved modifier operation.");
    }
}
std::vector<AttributeBaseChange> baseChanges(std::span<const ResolvedModifier> modifiers,std::uint32_t stacks,bool factor) {
    std::vector<AttributeBaseChange> result;result.reserve(modifiers.size());
    for(const auto& modifier:modifiers) {
        const double magnitude=stackedMagnitude(modifier,stacks,factor);if(!std::isfinite(magnitude))throw std::invalid_argument("Stacked effect magnitude exceeds finite range.");
        AttributeBaseOperation operation;
        switch(modifier.operation) {
        case EffectModifierOperation::AddBase:case EffectModifierOperation::AddFinal:operation=AttributeBaseOperation::Add;break;
        case EffectModifierOperation::MultiplyAdditive:case EffectModifierOperation::MultiplyCompound:operation=AttributeBaseOperation::Multiply;break;
        case EffectModifierOperation::DivideAdditive:operation=AttributeBaseOperation::Divide;break;
        case EffectModifierOperation::Override:operation=AttributeBaseOperation::Set;break;
        default:throw std::invalid_argument("Invalid resolved modifier operation.");
        }result.push_back({modifier.attribute,magnitude,operation});
    }return result;
}
}
struct EntityEffects::Impl {
    struct State {
        EffectEntityRef reference;GameplayTagContainer authoredTags;
        std::vector<std::string> starting;
        bool bootstrapped{};
        EffectReadiness readiness{EffectReadiness::Preparing};std::string diagnostic;
    };
    struct Active {
        EffectHandle handle;EffectDefinitionSnapshot definition;EffectEntityRef source,target;
        std::vector<ResolvedModifier> modifiers;std::uint32_t stacks{1};
        std::uint64_t duration{},period{},nextPulse{},lastPulse{std::numeric_limits<std::uint64_t>::max()};
        std::optional<std::uint64_t> expires;
    };
    using ActiveMap=std::map<std::uint64_t,Active>;
    EntityAttributes& attributes;const EffectCalculationRegistry& calculations;GameplayEffectQuery definitions;
    std::map<EntityId,State> states;ActiveMap effects;
    std::uint64_t domain{},identity{},incarnation{},serial{},tick{};double remainder{};std::string diagnostic;
    Impl(EntityAttributes& values,const EffectCalculationRegistry& registry):attributes(values),calculations(registry){}
    bool current(EffectEntityRef reference) const {
        if(reference.world!=identity)return false;
        if(!reference.entity)return reference.incarnation==0;
        const auto found=states.find(reference.entity);return found!=states.end()&&found->second.reference==reference;
    }
    GameplayTagContainer tags(EffectEntityRef target,const ActiveMap& active) const {
        GameplayTagContainer result;const auto found=states.find(target.entity);
        if(found!=states.end()&&found->second.reference==target)result=found->second.authoredTags;
        for(const auto& [id,effect]:active)if(effect.target==target){(void)id;result.append(effect.definition.definition->grantedTags);}return result;
    }
    bool sync(AttributeSet& values,EffectEntityRef target,const ActiveMap& active) const {
        std::vector<AttributeCurrentModifier> modifiers;
        for(const auto& [id,effect]:active) {
            (void)id;if(effect.target!=target||effect.period)continue;
            const auto& definition=*effect.definition.definition;
            for(const auto& modifier:effect.modifiers)modifiers.push_back({modifier.attribute,modifier.operation,stackedMagnitude(modifier,effect.stacks,definition.stacking.factorInStackCount)});
        }return values.setCurrentModifiers(modifiers);
    }
    struct Outcome {EffectApplyResult result;bool commit{},initialPulse{};};
    Outcome apply(EffectEntityRef source,EffectEntityRef target,const EffectDefinitionSnapshot& snapshot,AttributeSet& values,ActiveMap& active,bool deferPulse) {
        const auto reject=[](std::string text,EffectApplyStatus status=EffectApplyStatus::Rejected){return Outcome{{status,{},std::move(text)},false};};
        if(!current(source)||!current(target)||!target.entity)return reject("Effect context refers to an unavailable actor incarnation.");
        if(!snapshot.definition)return reject(snapshot.diagnostic.empty()?"Effect definition is unavailable.":snapshot.diagnostic,applicationReadiness(snapshot.readiness));
        try {
            const auto& definition=*snapshot.definition;validateGameplayEffect(definition,attributes.catalog(),calculations,false);
            const auto sourceTags=tags(source,active),targetTags=tags(target,active);
            if(!sourceTags.hasAll(definition.sourceRequired)||sourceTags.hasAny(definition.sourceBlocked)||!targetTags.hasAll(definition.targetRequired)||targetTags.hasAny(definition.targetBlocked))return reject("Effect application tag requirements were not met.");
            const auto targetValues=values.resolvedSnapshot();const auto sourceValues=source==target?targetValues:attributes.resolvedSnapshot(source.entity);
            const EffectCalculationContext context{sourceValues,targetValues,&sourceTags,&targetTags};std::vector<ResolvedModifier> resolved;resolved.reserve(definition.modifiers.size());
            for(const auto& modifier:definition.modifiers) {
                const auto magnitude=calculations.resolve(modifier.magnitude,context);
                if(!magnitude)return reject("Effect magnitude could not resolve: "+modifier.magnitude.calculation,EffectApplyStatus::Failed);
                resolved.push_back({modifier.attribute,modifier.operation,*magnitude});
            }
            std::vector<GameplayTag> targets;targets.reserve(resolved.size());for(const auto& modifier:resolved)targets.push_back(modifier.attribute);
            if(!values.materialize(targets))return reject(values.status(),EffectApplyStatus::Failed);
            if(definition.durationPolicy==EffectDurationPolicy::Instant) {
                const auto changes=baseChanges(resolved,1,false);if(!values.applyBaseChanges(changes))return reject(values.status(),EffectApplyStatus::Failed);
                return {{EffectApplyStatus::Applied,{},{}},true};
            }
            auto existing=active.end();
            if(definition.stacking.type!=EffectStackingType::None)existing=std::find_if(active.begin(),active.end(),[&](const auto& entry) {
                const auto& effect=entry.second;
                return effect.target==target&&effect.definition.definition->id==definition.id&&effect.definition.generation==snapshot.generation&&
                    (definition.stacking.type!=EffectStackingType::AggregateBySource||effect.source==source);
            });
            const auto duration=definition.durationPolicy==EffectDurationPolicy::HasDuration?ticksFor(definition.duration):0;
            const auto period=ticksFor(definition.period),maximum=ticksFor(definition.stacking.maxDuration);
            EffectHandle handle;bool initialPulse=true;
            if(existing!=active.end()) {
                auto& effect=existing->second;handle=effect.handle;
                initialPulse=definition.stacking.periodReset==EffectPeriodReset::ResetOnSuccessfulApplication;
                const bool overflow=definition.stacking.limit&&effect.stacks>=definition.stacking.limit;
                if(overflow&&definition.stacking.denyOverflowApplication) {
                    if(!definition.stacking.clearStackOnOverflow)return reject("Effect stack limit denied this application.");
                    active.erase(existing);if(!sync(values,target,active))return reject(values.status(),EffectApplyStatus::Failed);
                    return {{EffectApplyStatus::Rejected,{},"Effect stack limit cleared the old stack and denied this application."},true};
                }
                if(!overflow){if(effect.stacks==std::numeric_limits<std::uint32_t>::max())return reject("Effect stack count exceeds its numeric range.");++effect.stacks;}
                effect.source=source;effect.definition=snapshot;effect.modifiers=std::move(resolved);
                if(effect.expires) {
                    if(definition.stacking.durationRefresh==EffectDurationRefresh::RefreshOnSuccessfulApplication)effect.duration=maximum?std::min(duration,maximum):duration;
                    else if(definition.stacking.durationRefresh==EffectDurationRefresh::ExtendDuration) {
                        const auto remaining=*effect.expires>tick?*effect.expires-tick:0;effect.duration=later(remaining,duration);if(maximum)effect.duration=std::min(effect.duration,maximum);
                    }
                    if(definition.stacking.durationRefresh!=EffectDurationRefresh::NeverRefresh)effect.expires=later(tick,effect.duration);
                }
                if(period&&definition.stacking.periodReset==EffectPeriodReset::ResetOnSuccessfulApplication)effect.nextPulse=later(tick,period);
            } else {
                if(serial==std::numeric_limits<std::uint64_t>::max())return reject("Effect handle range exhausted.");
                handle={identity,++serial};Active effect;effect.handle=handle;effect.definition=snapshot;effect.source=source;effect.target=target;effect.modifiers=std::move(resolved);
                effect.duration=maximum?std::min(duration,maximum):duration;effect.period=period;effect.nextPulse=period?later(tick,period):0;
                if(definition.durationPolicy==EffectDurationPolicy::HasDuration)effect.expires=later(tick,effect.duration);
                active.emplace(handle.serial,std::move(effect));
            }
            if(!sync(values,target,active))return reject(values.status(),EffectApplyStatus::Failed);
            auto& effect=active.at(handle.serial);
            initialPulse=initialPulse&&effect.period&&definition.executeOnApplication;
            if(initialPulse&&!deferPulse&&effect.lastPulse!=tick) {
                if(!values.applyBaseChanges(baseChanges(effect.modifiers,effect.stacks,definition.stacking.factorInStackCount)))return reject(values.status(),EffectApplyStatus::Failed);effect.lastPulse=tick;
            }
            return {{EffectApplyStatus::Applied,handle,{}},true,initialPulse};
        }catch(const std::exception& error){return reject(error.what(),EffectApplyStatus::Failed);}
    }
    EffectDefinitionSnapshot query(const std::string& id) const {
        if(!definitions)return {{},0,"Gameplay Effect definitions are unavailable.",EffectReadiness::Unavailable};
        try{return definitions(id);}catch(const std::exception& error){return {{},0,error.what(),EffectReadiness::Failed};}
    }
    bool bootstrap(State& state) {
        std::vector<EffectDefinitionSnapshot> prepared;prepared.reserve(state.starting.size());
        for(const auto& id:state.starting) {
            auto snapshot=query(id);if(!snapshot.definition) {
                state.readiness=snapshot.readiness;state.diagnostic=snapshot.diagnostic.empty()?"Starting Gameplay Effect is unavailable: "+id:snapshot.diagnostic;return false;
            }prepared.push_back(std::move(snapshot));
        }
        AttributeSet values(attributes.catalog());std::vector<GameplayTag> targets;for(const auto& snapshot:prepared)for(const auto& modifier:snapshot.definition->modifiers)targets.push_back(modifier.attribute);
        if(!values.materialize(targets)){state.readiness=EffectReadiness::Failed;state.diagnostic=values.status();return false;}
        AttributeSeeds seeds;for(const auto& value:values.snapshot())seeds.push_back({value.tag,attributes.catalog().find(value.tag)->defaultBase});
        if(!values.beginBootstrap(seeds)){state.readiness=EffectReadiness::Failed;state.diagnostic=values.status();return false;}
        auto active=effects;std::vector<EffectHandle> initialPulses;
        for(const auto& snapshot:prepared) {
            const auto outcome=apply(state.reference,state.reference,snapshot,values,active,true);
            if(!outcome.result){state.readiness=outcome.result.status==EffectApplyStatus::Preparing?EffectReadiness::Preparing:outcome.result.status==EffectApplyStatus::Unavailable?EffectReadiness::Unavailable:EffectReadiness::Failed;state.diagnostic=outcome.result.diagnostic;return false;}
            if(outcome.initialPulse)initialPulses.push_back(outcome.result.handle);
        }
        if(!values.finishBootstrap()){state.readiness=EffectReadiness::Failed;state.diagnostic=values.status();return false;}
        for(const auto handle:initialPulses)if(const auto found=active.find(handle.serial);found!=active.end()&&found->second.lastPulse!=tick) {
            auto& effect=found->second;
            if(!values.applyBaseChanges(baseChanges(effect.modifiers,effect.stacks,effect.definition.definition->stacking.factorInStackCount))){state.readiness=EffectReadiness::Failed;state.diagnostic=values.status();return false;}effect.lastPulse=tick;
        }
        effects=std::move(active);attributes.publish(state.reference.entity,std::move(values));
        state.bootstrapped=true;state.readiness=EffectReadiness::Ready;state.diagnostic.clear();return true;
    }
};
EntityEffects::EntityEffects(EntityAttributes& attributes,const EffectCalculationRegistry& calculations):impl_(std::make_unique<Impl>(attributes,calculations)){}
EntityEffects::~EntityEffects()=default;
void EntityEffects::setDefinitions(GameplayEffectQuery query){impl_->definitions=std::move(query);}
void EntityEffects::clear(){auto& s=*impl_;s.effects.clear();s.states.clear();s.domain=s.identity=s.incarnation=s.serial=s.tick=0;s.remainder=0;s.diagnostic.clear();s.attributes.clear();}
bool EntityEffects::reconcile(std::uint64_t domain,const Scene& scene) {
    auto& s=*impl_;if(s.domain!=domain||!s.identity){clear();s.domain=domain;s.identity=nextEffectWorld.fetch_add(1);s.attributes.beginWorld(s.identity);}
    s.diagnostic.clear();std::set<EntityId> present;bool success=true;
    for(const auto& record:scene.records()) {
        present.insert(record.id);auto [found,created]=s.states.try_emplace(record.id);auto& state=found->second;
        if(created)state.reference={s.identity,record.id,++s.incarnation};
        state.authoredTags=record.tags.value_or(GameplayTagContainer{});
        if(!state.bootstrapped) {
            state.starting.clear();if(record.effects)for(const auto& reference:record.effects->starting)state.starting.push_back(reference.id);
            if(!s.bootstrap(state)) {success=false;s.attributes.erase(record.id);}
        }
        if(!state.diagnostic.empty())s.diagnostic+="Entity "+std::to_string(record.id)+": "+state.diagnostic+"\n";
    }
    std::erase_if(s.effects,[&](const auto& entry){return !present.contains(entry.second.target.entity);});
    std::erase_if(s.states,[&](const auto& entry){if(present.contains(entry.first))return false;s.attributes.erase(entry.first);return true;});
    return success;
}
EffectEntityRef EntityEffects::reference(EntityId id) const {const auto& s=*impl_;if(!id)return {s.identity,0,0};const auto found=s.states.find(id);return found==s.states.end()?EffectEntityRef{}:found->second.reference;}
EffectReadiness EntityEffects::readiness(EntityId id) const {const auto found=impl_->states.find(id);return found==impl_->states.end()?EffectReadiness::Unavailable:found->second.readiness;}
void EntityEffects::prepare(EntityId id){auto& s=*impl_;const auto found=s.states.find(id);if(found!=s.states.end()&&!found->second.bootstrapped){s.bootstrap(found->second);s.diagnostic=found->second.diagnostic;}}
const std::string& EntityEffects::status() const{return impl_->diagnostic;}
GameplayTagContainer EntityEffects::effectiveTags(EntityId id) const{return impl_->tags(reference(id),impl_->effects);}
std::vector<ActiveEffectSnapshot> EntityEffects::active(EntityId target) const {
    const auto& s=*impl_;std::vector<ActiveEffectSnapshot> result;
    for(const auto& [id,effect]:s.effects)if(!target||effect.target.entity==target) {
        (void)id;const auto& definition=*effect.definition.definition;
        result.push_back({effect.handle,definition.id,definition.name,effect.definition.generation,effect.source,effect.target,effect.stacks,
            effect.expires?std::optional{double(*effect.expires>s.tick?*effect.expires-s.tick:0)*FixedStep}:std::nullopt,double(effect.period)*FixedStep});
    }return result;
}
EffectApplyResult EntityEffects::apply(EntityId source,EntityId target,const std::string& id){return apply(reference(source),reference(target),id);}
EffectApplyResult EntityEffects::apply(EffectEntityRef source,EffectEntityRef target,const std::string& id) {
    auto& s=*impl_;if(!s.current(source)||!s.current(target)||!target.entity)return {EffectApplyStatus::Unavailable,{},"Effect target/source incarnation is unavailable."};
    const auto found=s.states.find(target.entity);if(found->second.readiness!=EffectReadiness::Ready)return {applicationReadiness(found->second.readiness),{},found->second.diagnostic};
    auto values=s.attributes.copySet(target.entity).value_or(AttributeSet(s.attributes.catalog()));auto active=s.effects;
    const auto outcome=s.apply(source,target,s.query(id),values,active,false);s.diagnostic=outcome.result.diagnostic;
    if(outcome.commit){s.effects=std::move(active);s.attributes.publish(target.entity,std::move(values));}return outcome.result;
}
bool EntityEffects::remove(EffectHandle handle,std::uint32_t count) {
    auto& s=*impl_;if(handle.world!=s.identity||!handle.serial){s.diagnostic="Effect handle belongs to an unavailable Play world.";return false;}
    auto active=s.effects;const auto found=active.find(handle.serial);if(found==active.end()){s.diagnostic="Active effect handle is unavailable.";return false;}
    const auto target=found->second.target;auto values=s.attributes.copySet(target.entity).value_or(AttributeSet(s.attributes.catalog()));
    if(!count||count>=found->second.stacks)active.erase(found);else found->second.stacks-=count;
    try{if(!s.sync(values,target,active)){s.diagnostic=values.status();return false;}s.effects=std::move(active);s.attributes.publish(target.entity,std::move(values));s.diagnostic.clear();return true;}
    catch(const std::exception& error){s.diagnostic=error.what();return false;}
}
bool EntityEffects::applyBaseChanges(EntityId id,std::span<const AttributeBaseChange> changes) {
    auto& s=*impl_;if(readiness(id)!=EffectReadiness::Ready){s.diagnostic="Actor attributes are not ready for Base changes.";return false;}
    auto values=s.attributes.copySet(id);if(!values){s.diagnostic="Entity runtime values are unavailable.";return false;}
    std::vector<GameplayTag> targets;targets.reserve(changes.size());for(const auto& change:changes)targets.push_back(change.tag);
    if(!values->materialize(targets)||!values->applyBaseChanges(changes)){s.diagnostic=values->status();return false;}s.attributes.publish(id,std::move(*values));s.diagnostic.clear();return true;
}
bool EntityEffects::setBase(EntityId id,const GameplayTag& tag,double value){const AttributeBaseChange change{tag,value};return applyBaseChanges(id,std::span{&change,1});}
bool EntityEffects::addBase(EntityId id,const GameplayTag& tag,double value){const AttributeBaseChange change{tag,value,AttributeBaseOperation::Add};return applyBaseChanges(id,std::span{&change,1});}
bool EntityEffects::step(double seconds) {
    auto& s=*impl_;if(!std::isfinite(seconds)||seconds<0){s.diagnostic="Effect step must be finite and nonnegative.";return false;}
    if(!s.identity)return true;
    s.remainder+=seconds;if(!std::isfinite(s.remainder)||s.remainder/FixedStep>double(std::numeric_limits<std::uint64_t>::max()/4)){s.diagnostic="Effect step exceeds its numeric range.";return false;}
    const auto count=static_cast<std::uint64_t>(std::floor((s.remainder+1e-12)/FixedStep));
    for(std::uint64_t index=0;index<count;++index)try {
        const auto now=later(s.tick,1);auto active=s.effects;std::map<EntityId,AttributeSet> candidates;
        const auto values=[&](EntityId id)->AttributeSet& {
            const auto found=candidates.find(id);if(found!=candidates.end())return found->second;
            return candidates.emplace(id,s.attributes.copySet(id).value_or(AttributeSet(s.attributes.catalog()))).first->second;
        };
        for(auto& [id,effect]:active)if(effect.period&&effect.nextPulse<=now) {
            (void)id;if(effect.lastPulse!=now) {
                auto& target=values(effect.target.entity);
                if(!target.applyBaseChanges(baseChanges(effect.modifiers,effect.stacks,effect.definition.definition->stacking.factorInStackCount)))throw std::runtime_error(target.status());effect.lastPulse=now;
            }effect.nextPulse=later(now,effect.period);
        }
        std::set<EntityId> changed;
        for(auto entry=active.begin();entry!=active.end();) {
            auto& effect=entry->second;if(!effect.expires||*effect.expires>now){++entry;continue;}
            changed.insert(effect.target.entity);const auto policy=effect.definition.definition->stacking.expiration;
            if(policy==EffectStackExpiration::ClearEntireStack||(policy==EffectStackExpiration::RemoveSingleStackAndRefreshDuration&&--effect.stacks==0))entry=active.erase(entry);
            else {effect.expires=later(now,effect.duration);++entry;}
        }
        for(const auto id:changed)if(!s.sync(values(id),s.states.at(id).reference,active))throw std::runtime_error(values(id).status());
        for(const auto& [id,state]:s.states)if(state.readiness==EffectReadiness::Ready) {
            auto& target=values(id);if(!target.regenerate(FixedStep))throw std::runtime_error(target.status());
        }
        s.effects=std::move(active);for(auto& [id,value]:candidates)s.attributes.publish(id,std::move(value));s.tick=now;s.remainder=std::max(0.0,s.remainder-FixedStep);
    }catch(const std::exception& error){s.diagnostic=error.what();return false;}
    return true;
}
}
