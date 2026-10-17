#include "Attributes.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace crimson {
namespace {
std::vector<GameplayTag> dependencies(const AttributeDefinition& definition,bool requiredOnly) {
    std::vector<GameplayTag> result;
    if(definition.minimum.attribute)result.push_back(*definition.minimum.attribute);
    if(definition.maximum.attribute)result.push_back(*definition.maximum.attribute);
    for(const auto& term:definition.contributions)if(!requiredOnly||!term.optional)result.push_back(term.attribute);
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());return result;
}
void finite(double value,const std::string& context){if(!std::isfinite(value))throw std::invalid_argument(context+" must be finite.");}
}
AttributeCatalog::AttributeCatalog(GameplayTagRegistry registry,std::vector<AttributeDefinition> definitions):registry_(std::move(registry)),definitions_(std::move(definitions)) {
    if(definitions_.size()>4096)throw std::invalid_argument("Attribute catalog exceeds 4096 definitions.");
    std::sort(definitions_.begin(),definitions_.end(),[](const auto& a,const auto& b){return a.tag<b.tag;});
    for(std::size_t index=0;index<definitions_.size();++index) {
        auto& definition=definitions_[index];
        const GameplayTag root{"Stats"};
        if(!definition.tag.valid()||!registry_.contains(definition.tag))throw std::invalid_argument("Attribute definition needs a registered native tag.");
        if(!definition.tag.matches(root)||definition.tag==root)throw std::invalid_argument("Attribute definition must be beneath Stats.");
        if(index&&definitions_[index-1].tag==definition.tag)throw std::invalid_argument("Duplicate native attribute definition: "+definition.tag.name());
        definition.tag=*registry_.request(definition.tag.name());finite(definition.defaultBase,definition.tag.name()+" default");
        for(const auto* bound:{&definition.minimum,&definition.maximum}) {
            if(bound->constant&&bound->attribute)throw std::invalid_argument("An attribute bound must be constant or attribute-valued.");
            if(bound->constant)finite(*bound->constant,definition.tag.name()+" bound");
            if(bound->attribute&&!find(*bound->attribute))throw std::invalid_argument("Attribute bound references a missing definition.");
        }
        if(definition.initializeToMaximum&&!definition.maximum.constant&&!definition.maximum.attribute)throw std::invalid_argument("Initialize-to-maximum needs a finite maximum bound.");
        if(definition.minimum.constant&&definition.maximum.constant&&*definition.minimum.constant>*definition.maximum.constant)throw std::invalid_argument("Attribute minimum exceeds maximum.");
        for(const auto& term:definition.contributions){finite(term.coefficient,"Attribute contribution");if(!find(term.attribute))throw std::invalid_argument("Attribute formula references a missing definition.");}
        if(definition.regeneration&&!find(definition.regeneration->rate))throw std::invalid_argument("Regeneration references a missing rate definition.");
    }
    // Iterative topology also validates long native dependency chains without
    // consuming the host's call stack. All ready-node ties use stable tag order.
    std::map<GameplayTag,std::size_t> indegree;
    std::map<GameplayTag,std::vector<GameplayTag>> dependents;
    std::set<GameplayTag> ready;
    for(const auto& definition:definitions_) {
        const auto inputs=dependencies(definition,false);indegree[definition.tag]=inputs.size();
        for(const auto& input:inputs)dependents[input].push_back(definition.tag);
        if(inputs.empty())ready.insert(definition.tag);
    }
    while(!ready.empty()) {
        const auto tag=*ready.begin();ready.erase(ready.begin());order_.push_back(tag);
        for(const auto& dependent:dependents[tag])if(--indegree[dependent]==0)ready.insert(dependent);
    }
    if(order_.size()!=definitions_.size())throw std::invalid_argument("Cyclic attribute dependency.");
}
const AttributeDefinition* AttributeCatalog::find(const GameplayTag& tag) const {
    const auto where=std::lower_bound(definitions_.begin(),definitions_.end(),tag,[](const auto& definition,const auto& key){return definition.tag<key;});
    return where==definitions_.end()||where->tag!=tag?nullptr:&*where;
}
std::vector<GameplayTag> AttributeCatalog::activationClosure(std::span<const GameplayTag> targets) const {
    std::set<GameplayTag> selected;std::vector<GameplayTag> pending(targets.begin(),targets.end());
    while(!pending.empty()) {
        const auto tag=pending.back();pending.pop_back();const auto* definition=find(tag);
        if(!definition)throw std::invalid_argument("Unknown native attribute: "+tag.name());
        if(!selected.insert(tag).second)continue;
        for(const auto& required:dependencies(*definition,true))pending.push_back(required);
        for(const auto& dependent:definitions_) {
            const auto inputs=dependencies(dependent,false);
            if(std::find(inputs.begin(),inputs.end(),tag)!=inputs.end())pending.push_back(dependent.tag);
        }
    }
    std::vector<GameplayTag> result;for(const auto& tag:order_)if(selected.contains(tag))result.push_back(tag);return result;
}
const AttributeCatalog& nativeAttributeCatalog() {
    static const AttributeCatalog catalog=[] {
        std::vector<AttributeDefinition> definitions;
        const auto define=[&](GameplayTag tag,std::string name,double base,std::optional<double> minimum) {
            AttributeDefinition result;result.tag=std::move(tag);result.displayName=std::move(name);result.defaultBase=base;result.minimum.constant=minimum;definitions.push_back(std::move(result));
        };
        define(stats::Health,"Health",0,{});define(stats::MaxHealth,"Max Health",0,0);
        define(stats::Mana,"Mana",0,0);define(stats::MaxMana,"Max Mana",0,0);
        define(stats::MovementSpeed,"Movement Speed",5,0);define(stats::Vitality,"Vitality",0,0);define(stats::Spirit,"Spirit",0,0);
        define(stats::HealthRegen,"Health Regen",0,0);define(stats::ManaRegen,"Mana Regen",0,0);
        definitions[0].maximum=AttributeBound::reference(stats::MaxHealth);definitions[0].regeneration=AttributeRegeneration{stats::HealthRegen,true};
        definitions[0].initializeToMaximum=true;
        definitions[1].contributions.push_back({stats::Vitality,2,true});
        definitions[2].maximum=AttributeBound::reference(stats::MaxMana);definitions[2].regeneration=AttributeRegeneration{stats::ManaRegen,false};
        definitions[2].initializeToMaximum=true;
        definitions[3].contributions.push_back({stats::Spirit,2,true});
        definitions[7].contributions.push_back({stats::Vitality,.05,true});definitions[8].contributions.push_back({stats::Spirit,.10,true});
        return AttributeCatalog(nativeGameplayTags(),std::move(definitions));
    }();return catalog;
}
std::optional<double> seedBase(const AttributeSeeds& seeds,const GameplayTag& tag) {
    const auto found=std::find_if(seeds.begin(),seeds.end(),[&](const auto& entry){return entry.tag==tag;});return found==seeds.end()?std::nullopt:std::optional{found->startingBase};
}
void validateAttributeSeeds(const AttributeSeeds& seeds,const AttributeCatalog& catalog) {
    if(seeds.size()>4096)throw std::invalid_argument("Attribute seed input exceeds 4096 entries.");
    std::set<GameplayTag> selected;
    for(const auto& entry:seeds){if(!entry.tag.valid())throw std::invalid_argument("Attribute seed needs a valid tag.");finite(entry.startingBase,entry.tag.name()+" starting Base");if(!selected.insert(entry.tag).second)throw std::invalid_argument("Duplicate attribute seed: "+entry.tag.name());}
    for(const auto& entry:seeds)if(const auto* definition=catalog.find(entry.tag))for(const auto& required:dependencies(*definition,true))if(!selected.contains(required))throw std::invalid_argument(entry.tag.name()+" requires "+required.name());
}
bool addSeed(AttributeSeeds& seeds,const GameplayTag& tag,const AttributeCatalog& catalog) {
    if(seedBase(seeds,tag)||!catalog.find(tag))return false;
    auto candidate=seeds;std::set<GameplayTag> closure;std::vector<GameplayTag> pending{tag};
    while(!pending.empty()) {
        const auto key=pending.back();pending.pop_back();if(!closure.insert(key).second)continue;
        for(const auto& required:dependencies(*catalog.find(key),true))pending.push_back(required);
    }
    for(const auto& key:catalog.evaluationOrder())if(closure.contains(key)&&!seedBase(candidate,key)) {
        const auto& definition=*catalog.find(key);candidate.push_back({definition.tag,definition.defaultBase});
    }
    validateAttributeSeeds(candidate,catalog);seeds=std::move(candidate);return true;
}
bool removeSeed(AttributeSeeds& seeds,const GameplayTag& tag,const AttributeCatalog& catalog) {
    if(!seedBase(seeds,tag))return false;
    for(const auto& entry:seeds)if(entry.tag!=tag)if(const auto* definition=catalog.find(entry.tag))for(const auto& required:dependencies(*definition,true))if(required==tag)return false;
    std::erase_if(seeds,[&](const auto& entry){return entry.tag==tag;});return true;
}
bool setSeedBase(AttributeSeeds& seeds,const GameplayTag& tag,double value,const AttributeCatalog& catalog) {
    finite(value,"Starting Base");if(const auto* definition=catalog.find(tag);definition&&definition->initializeToMaximum)return false;auto candidate=seeds;
    auto found=std::find_if(candidate.begin(),candidate.end(),[&](const auto& entry){return entry.tag==tag;});
    if(found==candidate.end()){if(!addSeed(candidate,tag,catalog))return false;found=std::find_if(candidate.begin(),candidate.end(),[&](const auto& entry){return entry.tag==tag;});}
    else if(found->startingBase==value)return false;
    found->startingBase=value;validateAttributeSeeds(candidate,catalog);seeds=std::move(candidate);return true;
}
std::optional<AttributeValue> AttributeSet::value(const GameplayTag& tag) const {
    const auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& entry){return entry.tag==tag;});return found==entries_.end()?std::nullopt:std::optional{found->value};
}
std::vector<AttributeSnapshot> AttributeSet::snapshot() const {
    std::vector<AttributeSnapshot> result;result.reserve(entries_.size());for(const auto& entry:entries_)result.push_back({entry.tag,entry.value});return result;
}
bool AttributeSet::materialize(std::span<const GameplayTag> targets) {
    changes_.clear();try {
        auto candidate=entries_;
        for(const auto& tag:catalog_->activationClosure(targets))if(std::none_of(candidate.begin(),candidate.end(),[&](const auto& entry){return entry.tag==tag;})) {
            const auto& definition=*catalog_->find(tag);candidate.push_back({tag,definition.defaultBase,{definition.defaultBase,definition.defaultBase}});
        }
        evaluate(candidate,pendingFull_);publish(std::move(candidate));status_=unresolved_;return true;
    }catch(const std::exception& error){status_=error.what();return false;}
}
std::optional<AttributeValue> AttributeSet::resolvedValue(const GameplayTag& tag) const {
    if(const auto existing=value(tag))return existing;if(!catalog_->find(tag))return {};
    auto candidate=*this;if(!candidate.materialize(std::span{&tag,1}))return {};return candidate.value(tag);
}
std::vector<AttributeSnapshot> AttributeSet::resolvedSnapshot() const {
    std::vector<GameplayTag> tags;tags.reserve(catalog_->definitions().size());for(const auto& definition:catalog_->definitions())tags.push_back(definition.tag);
    auto candidate=*this;if(!candidate.materialize(tags))throw std::invalid_argument(candidate.status());return candidate.snapshot();
}
void AttributeSet::evaluate(std::vector<Entry>& entries,std::span<const GameplayTag> initialize) const {
    auto resolved=entries;
    for(const auto& definition:catalog_->definitions())if(std::none_of(resolved.begin(),resolved.end(),[&](const auto& entry){return entry.tag==definition.tag;}))
        resolved.push_back({definition.tag,definition.defaultBase,{definition.defaultBase,definition.defaultBase}});
    const auto find=[&](const auto& tag)->Entry*{const auto found=std::find_if(resolved.begin(),resolved.end(),[&](const auto& entry){return entry.tag==tag;});return found==resolved.end()?nullptr:&*found;};
    for(const auto& tag:catalog_->evaluationOrder())if(auto* entry=find(tag)) {
        const auto& definition=*catalog_->find(tag);
        const auto bound=[&](const AttributeBound& value,double fallback) {
            if(value.constant)return *value.constant;if(value.attribute){const auto* other=find(*value.attribute);if(!other)throw std::invalid_argument(tag.name()+" has a missing bound attribute.");return other->value.current;}return fallback;
        };
        const double lower=bound(definition.minimum,-std::numeric_limits<double>::infinity()),upper=bound(definition.maximum,std::numeric_limits<double>::infinity());
        if(lower>upper)throw std::invalid_argument(tag.name()+" minimum exceeds its maximum.");
        // Initialization occurs at this node, after its bounds and before any
        // dependent formula, so only one coherent graph is ever published.
        if(std::find(initialize.begin(),initialize.end(),tag)!=initialize.end()) {
            finite(upper,tag.name()+" initialization maximum");entry->value.base=upper;
        }
        entry->value.base=std::clamp(entry->value.base,lower,upper);double current=entry->value.base;
        for(const auto& term:definition.contributions) {
            const auto* input=find(term.attribute);if(!input){if(term.optional)continue;throw std::invalid_argument(tag.name()+" has a missing formula input.");}
            current+=term.coefficient*input->value.current;finite(current,tag.name()+" derived value");
        }
        double additive=0,multiply=1,divide=1,compound=1,final=0;std::optional<double> overrideValue;
        for(const auto& modifier:modifiers_)if(modifier.attribute==tag) {
            finite(modifier.magnitude,"Current modifier");
            switch(modifier.operation) {
            case EffectModifierOperation::AddBase:additive+=modifier.magnitude;break;
            case EffectModifierOperation::MultiplyAdditive:multiply+=modifier.magnitude-1;break;
            case EffectModifierOperation::DivideAdditive:divide+=modifier.magnitude-1;break;
            case EffectModifierOperation::MultiplyCompound:compound*=modifier.magnitude;break;
            case EffectModifierOperation::AddFinal:final+=modifier.magnitude;break;
            case EffectModifierOperation::Override:if(!overrideValue)overrideValue=modifier.magnitude;break;
            default:throw std::invalid_argument("Invalid Current modifier operation.");
            }
        }
        if(overrideValue)current=*overrideValue;
        else {if(divide==0)throw std::invalid_argument("Current modifier division is zero.");current=((current+additive)*multiply/divide*compound)+final;}
        finite(current,tag.name()+" modified Current");
        entry->value.current=std::clamp(current,lower,upper);
        finite(entry->value.base,tag.name()+" Base");finite(entry->value.current,tag.name()+" Current");
    }
    for(auto& entry:entries)entry.value=find(entry.tag)->value;
}
void AttributeSet::publish(std::vector<Entry> candidate) {
    std::sort(candidate.begin(),candidate.end(),[](const auto& a,const auto& b){return a.tag<b.tag;});std::vector<AttributeChange> changes;
    for(const auto& previous:entries_) {
        const auto next=std::find_if(candidate.begin(),candidate.end(),[&](const auto& entry){return entry.tag==previous.tag;});
        if(next==candidate.end())changes.push_back({previous.tag,previous.value,{}});else if(next->value!=previous.value)changes.push_back({next->tag,previous.value,next->value});
    }
    for(const auto& next:candidate)if(std::none_of(entries_.begin(),entries_.end(),[&](const auto& entry){return entry.tag==next.tag;}))changes.push_back({next.tag,{},next.value});
    std::sort(changes.begin(),changes.end(),[](const auto& a,const auto& b){return a.tag<b.tag;});entries_=std::move(candidate);changes_=std::move(changes);
}
void AttributeSet::settleChangesFrom(const AttributeSet& previous){auto candidate=entries_;entries_=previous.entries_;publish(std::move(candidate));}
bool AttributeSet::reconcile(const AttributeSeeds& seeds) {
    changes_.clear();try {
        validateAttributeSeeds(seeds,*catalog_);std::vector<Entry> candidate;std::vector<GameplayTag> initialize;std::string unresolved;
        for(const auto& seed:seeds) {
            const auto* definition=catalog_->find(seed.tag);if(!definition){unresolved+="Unknown attribute definition: "+seed.tag.name()+".\n";continue;}
            const auto previous=std::find_if(entries_.begin(),entries_.end(),[&](const auto& entry){return entry.tag==seed.tag;});
            if(previous!=entries_.end()&&(definition->initializeToMaximum||previous->seed==seed.startingBase)) {
                candidate.push_back(*previous);candidate.back().seed=seed.startingBase;
            } else {
                candidate.push_back({definition->tag,seed.startingBase,{seed.startingBase,seed.startingBase}});
                if(definition->initializeToMaximum)initialize.push_back(definition->tag);
            }
        }
        for(const auto& modifier:modifiers_)if(std::none_of(candidate.begin(),candidate.end(),[&](const auto& entry){return entry.tag==modifier.attribute;}))throw std::invalid_argument("Active effect needs an attribute removed from this set.");
        initialize.insert(initialize.end(),pendingFull_.begin(),pendingFull_.end());
        evaluate(candidate,initialize);publish(std::move(candidate));unresolved_=std::move(unresolved);status_=unresolved_;return true;
    }catch(const std::exception& error){status_=error.what();return false;}
}
bool AttributeSet::setBase(const GameplayTag& tag,double value){const AttributeBaseChange change{tag,value};return applyBaseChanges(std::span{&change,1});}
bool AttributeSet::addBase(const GameplayTag& tag,double value){const AttributeBaseChange change{tag,value,AttributeBaseOperation::Add};return applyBaseChanges(std::span{&change,1});}
bool AttributeSet::applyBaseChanges(std::span<const AttributeBaseChange> changes) {
    changes_.clear();try {
        auto candidate=entries_;auto pending=pendingFull_;
        const auto apply=[&](const AttributeBaseChange& change) {
            finite(change.value,"Attribute change");const auto entry=std::find_if(candidate.begin(),candidate.end(),[&](const auto& value){return value.tag==change.tag;});
            if(entry==candidate.end())throw std::invalid_argument("Attribute is not available: "+change.tag.name());
            switch(change.operation){case AttributeBaseOperation::Set:entry->value.base=change.value;break;case AttributeBaseOperation::Add:entry->value.base+=change.value;break;case AttributeBaseOperation::Multiply:entry->value.base*=change.value;break;case AttributeBaseOperation::Divide:if(change.value==0)throw std::invalid_argument("Base division is zero.");entry->value.base/=change.value;break;default:throw std::invalid_argument("Invalid attribute Base operation.");}
            finite(entry->value.base,change.tag.name()+" Base");
        };
        const auto pendingChange=[&](const auto& change){return std::find(pendingFull_.begin(),pendingFull_.end(),change.tag)!=pendingFull_.end();};
        for(const auto& change:changes)if(!pendingChange(change))apply(change);
        if(!pending.empty()) {
            evaluate(candidate,pending);
            for(const auto& change:changes)if(pendingChange(change)){apply(change);std::erase(pending,change.tag);}
        }
        evaluate(candidate,pending);publish(std::move(candidate));pendingFull_=std::move(pending);status_=unresolved_;return true;
    }catch(const std::exception& error){status_=error.what();return false;}
}
bool AttributeSet::beginBootstrap(const AttributeSeeds& seeds) {
    AttributeSet candidate(*catalog_);if(!candidate.reconcile(seeds)){status_=candidate.status();return false;}
    for(const auto& seed:seeds)if(const auto* definition=catalog_->find(seed.tag);definition&&definition->initializeToMaximum)candidate.pendingFull_.push_back(definition->tag);
    *this=std::move(candidate);return true;
}
bool AttributeSet::finishBootstrap() {
    changes_.clear();try{auto candidate=entries_;evaluate(candidate,pendingFull_);publish(std::move(candidate));pendingFull_.clear();status_=unresolved_;return true;}catch(const std::exception& error){status_=error.what();return false;}
}
bool AttributeSet::setCurrentModifiers(std::span<const AttributeCurrentModifier> modifiers) {
    const auto previous=modifiers_;try {
        for(const auto& modifier:modifiers)if(!value(modifier.attribute))throw std::invalid_argument("Current modifier targets a missing attribute.");
        modifiers_.assign(modifiers.begin(),modifiers.end());auto candidate=entries_;evaluate(candidate,pendingFull_);publish(std::move(candidate));status_=unresolved_;return true;
    }catch(const std::exception& error){modifiers_=previous;changes_.clear();status_=error.what();return false;}
}
bool AttributeSet::regenerate(double seconds) {
    changes_.clear();if(!std::isfinite(seconds)||seconds<0){status_="Regeneration interval must be finite and nonnegative.";return false;}
    std::vector<AttributeBaseChange> changes;
    for(const auto& entry:entries_) {
        const auto& definition=*catalog_->find(entry.tag);if(!definition.regeneration)continue;
        const auto& rule=*definition.regeneration;if(rule.stopAtNonPositive&&entry.value.current<=0)continue;
        if(const auto rate=resolvedValue(rule.rate)) {
            const double delta=rate->current*seconds;if(!std::isfinite(delta)){status_="Regeneration delta exceeds finite range.";return false;}
            if(delta!=0)changes.push_back({entry.tag,delta,AttributeBaseOperation::Add});
        }
    }
    return applyBaseChanges(changes);
}
}
