#pragma once
#include "EntityAttributes.h"
#include "gameplay/Effects.h"
#include <memory>

namespace crimson {
// Main-thread CPU owner. Definitions and calculated magnitudes are pinned at
// application; native formulas continue evaluating from the active Current set.
class EntityEffects {
public:
    static constexpr double FixedStep=1.0/60.0;
    explicit EntityEffects(EntityAttributes&,const EffectCalculationRegistry& =nativeEffectCalculations());
    ~EntityEffects();
    EntityEffects(const EntityEffects&)=delete;
    EntityEffects& operator=(const EntityEffects&)=delete;
    void setDefinitions(GameplayEffectQuery);
    bool reconcile(std::uint64_t domain,const Scene&);
    bool step(double seconds);
    void clear();
    EffectApplyResult apply(EntityId source,EntityId target,const std::string& definitionId);
    EffectApplyResult apply(EffectEntityRef source,EffectEntityRef target,const std::string& definitionId);
    // count==0 removes the complete instance; otherwise removes that many stacks.
    bool remove(EffectHandle,std::uint32_t count=0);
    EffectEntityRef reference(EntityId) const;
    GameplayTagContainer effectiveTags(EntityId) const;
    std::vector<ActiveEffectSnapshot> active(EntityId target=NoEntity) const;
    EffectReadiness readiness(EntityId) const;
    void prepare(EntityId);
    const std::string& status() const;
    bool setBase(EntityId,const GameplayTag&,double);
    bool addBase(EntityId,const GameplayTag&,double);
    bool applyBaseChanges(EntityId,std::span<const AttributeBaseChange>);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
