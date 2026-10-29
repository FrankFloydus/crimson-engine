#pragma once
#include "gameplay/Attributes.h"
#include "scene/Scene.h"
#include <unordered_map>

namespace crimson {
// Sparse materialized values belong to an entity's Play lifetime. Native
// catalog values remain available without an authored component or allowlist.
class EntityAttributes {
public:
    explicit EntityAttributes(const AttributeCatalog& catalog=nativeAttributeCatalog()):catalog_(&catalog){}
    bool reconcile(std::uint64_t world,const Scene&);
    void beginWorld(std::uint64_t);
    const AttributeCatalog& catalog() const{return *catalog_;}
    std::optional<AttributeSet> copySet(EntityId) const;
    void publish(EntityId,AttributeSet);
    void erase(EntityId);
    std::vector<AttributeSnapshot> snapshot(EntityId) const;
    std::vector<AttributeSnapshot> resolvedSnapshot(EntityId) const;
    bool regenerate(double seconds);
    void clear();
    std::optional<AttributeValue> value(EntityId,const GameplayTag&) const;
    bool setBase(EntityId,const GameplayTag&,double);
    bool addBase(EntityId,const GameplayTag&,double);
    bool applyBaseChanges(EntityId,std::span<const AttributeBaseChange>);
    std::span<const AttributeChange> changes(EntityId) const;
    const std::string& status() const{return status_;}
private:
    const AttributeCatalog* catalog_;
    std::uint64_t world_{};
    std::unordered_map<EntityId,AttributeSet> sets_;
    std::string status_;
};
}
