#include "EntityAttributes.h"
#include <unordered_set>

namespace crimson {
void EntityAttributes::clear(){sets_.clear();world_=0;status_.clear();}
void EntityAttributes::beginWorld(std::uint64_t world){if(world_!=world){clear();world_=world;}}
std::optional<AttributeSet> EntityAttributes::copySet(EntityId id) const {const auto found=sets_.find(id);return found==sets_.end()?std::nullopt:std::optional{found->second};}
void EntityAttributes::publish(EntityId id,AttributeSet value){const auto found=sets_.find(id);value.settleChangesFrom(found==sets_.end()?AttributeSet(*catalog_):found->second);sets_.insert_or_assign(id,std::move(value));}
void EntityAttributes::erase(EntityId id){sets_.erase(id);}
std::vector<AttributeSnapshot> EntityAttributes::snapshot(EntityId id) const{const auto found=sets_.find(id);return found==sets_.end()?std::vector<AttributeSnapshot>{}:found->second.snapshot();}
std::vector<AttributeSnapshot> EntityAttributes::resolvedSnapshot(EntityId id) const{const auto found=sets_.find(id);return found==sets_.end()?AttributeSet(*catalog_).resolvedSnapshot():found->second.resolvedSnapshot();}
bool EntityAttributes::reconcile(std::uint64_t world,const Scene& scene) {
    beginWorld(world);
    std::unordered_set<EntityId> present;
    status_.clear();
    for(const auto& record:scene.records()) {
        present.insert(record.id);
        auto [found,inserted]=sets_.try_emplace(record.id,*catalog_);
        (void)inserted;
        if(!found->second.status().empty())status_+="Entity "+std::to_string(record.id)+": "+found->second.status()+"\n";
    }
    std::erase_if(sets_,[&](const auto& entry){return !present.contains(entry.first);});
    return true;
}
bool EntityAttributes::regenerate(double seconds) {
    bool accepted=true;
    for(auto& [id,set]:sets_)if(!set.regenerate(seconds)) {
        accepted=false;status_+="Entity "+std::to_string(id)+": "+set.status()+"\n";
    }
    return accepted;
}
std::optional<AttributeValue> EntityAttributes::value(EntityId entity,const GameplayTag& tag) const {
    const auto found=sets_.find(entity);return found==sets_.end()?std::nullopt:found->second.resolvedValue(tag);
}
bool EntityAttributes::setBase(EntityId entity,const GameplayTag& tag,double value) {
    const AttributeBaseChange change{tag,value};return applyBaseChanges(entity,std::span{&change,1});
}
bool EntityAttributes::addBase(EntityId entity,const GameplayTag& tag,double value) {
    const AttributeBaseChange change{tag,value,AttributeBaseOperation::Add};return applyBaseChanges(entity,std::span{&change,1});
}
bool EntityAttributes::applyBaseChanges(EntityId entity,std::span<const AttributeBaseChange> changes) {
    const auto found=sets_.find(entity);
    if(found==sets_.end()){status_="Entity runtime values are unavailable.";return false;}
    auto candidate=found->second;std::vector<GameplayTag> targets;targets.reserve(changes.size());for(const auto& change:changes)targets.push_back(change.tag);
    const bool accepted=candidate.materialize(targets)&&candidate.applyBaseChanges(changes);
    status_=candidate.status();if(accepted)publish(entity,std::move(candidate));return accepted;
}
std::span<const AttributeChange> EntityAttributes::changes(EntityId entity) const {
    const auto found=sets_.find(entity);return found==sets_.end()?std::span<const AttributeChange>{}:found->second.changes();
}
}
