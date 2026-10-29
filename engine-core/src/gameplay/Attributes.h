#pragma once
#include "GameplayTags.h"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace crimson {
// Low-level bootstrap and legacy conversion input only. Scene entities do not
// author an attribute membership list or numeric starting values.
struct AttributeSeed {
    GameplayTag tag;
    double startingBase{};
    bool operator==(const AttributeSeed&) const=default;
};
using AttributeSeeds=std::vector<AttributeSeed>;
struct AttributeValue {
    double base{},current{};
    bool operator==(const AttributeValue&) const=default;
};
struct AttributeBound {
    std::optional<double> constant;
    std::optional<GameplayTag> attribute;
    static AttributeBound number(double value){return {value,{}};}
    static AttributeBound reference(GameplayTag tag){return {{},std::move(tag)};}
};
struct AttributeContribution {GameplayTag attribute;double coefficient{};bool optional{};};
struct AttributeRegeneration {GameplayTag rate;bool stopAtNonPositive{};};
struct AttributeDefinition {
    GameplayTag tag;
    std::string displayName;
    double defaultBase{};
    AttributeBound minimum,maximum;
    std::vector<AttributeContribution> contributions;
    std::optional<AttributeRegeneration> regeneration;
    // Native bootstrap policy: fill a new resource at its resolved maximum.
    // Later materialization uses defaultBase and never refills existing pools.
    bool initializeToMaximum{};
};
class AttributeCatalog {
public:
    AttributeCatalog(GameplayTagRegistry registry,std::vector<AttributeDefinition> definitions);
    const GameplayTagRegistry& registry() const{return registry_;}
    std::span<const AttributeDefinition> definitions() const{return definitions_;}
    const AttributeDefinition* find(const GameplayTag&) const;
    std::span<const GameplayTag> evaluationOrder() const{return order_;}
    // Known targets activate required inputs and all native dependent outputs.
    std::vector<GameplayTag> activationClosure(std::span<const GameplayTag>) const;
private:
    GameplayTagRegistry registry_;
    std::vector<AttributeDefinition> definitions_;
    std::vector<GameplayTag> order_;
};
const AttributeCatalog& nativeAttributeCatalog();
void validateAttributeSeeds(const AttributeSeeds&,const AttributeCatalog& =nativeAttributeCatalog());
// Helpers for low-level/legacy seed lists. Required bounds remain complete;
// optional formula inputs do not expand these conversion inputs.
bool addSeed(AttributeSeeds&,const GameplayTag&,const AttributeCatalog& =nativeAttributeCatalog());
bool removeSeed(AttributeSeeds&,const GameplayTag&,const AttributeCatalog& =nativeAttributeCatalog());
bool setSeedBase(AttributeSeeds&,const GameplayTag&,double,const AttributeCatalog& =nativeAttributeCatalog());
std::optional<double> seedBase(const AttributeSeeds&,const GameplayTag&);

struct AttributeChange {GameplayTag tag;std::optional<AttributeValue> before,after;bool operator==(const AttributeChange&) const=default;};
enum class EffectModifierOperation {AddBase,MultiplyAdditive,DivideAdditive,MultiplyCompound,AddFinal,Override};
struct AttributeCurrentModifier {GameplayTag attribute;EffectModifierOperation operation{EffectModifierOperation::AddBase};double magnitude{};};
enum class AttributeBaseOperation { Set,Add,Multiply,Divide };
struct AttributeBaseChange {GameplayTag tag;double value{};AttributeBaseOperation operation{AttributeBaseOperation::Set};};
struct AttributeSnapshot {GameplayTag tag;AttributeValue value;bool operator==(const AttributeSnapshot&) const=default;};
class AttributeSet {
public:
    explicit AttributeSet(const AttributeCatalog& catalog=nativeAttributeCatalog()):catalog_(&catalog){}
    // Low-level seed reconciliation; runtime Effects use materialize instead.
    bool reconcile(const AttributeSeeds&);
    bool setBase(const GameplayTag&,double);
    bool addBase(const GameplayTag&,double);
    bool applyBaseChanges(std::span<const AttributeBaseChange>);
    bool regenerate(double seconds);
    // Bootstrap candidates are kept private by EntityEffects until their whole
    // starting list is ready. Pending full pools follow capacities until their
    // first deliberate Base operation, then preserve damage/spending normally.
    bool beginBootstrap(const AttributeSeeds&);
    bool finishBootstrap();
    bool setCurrentModifiers(std::span<const AttributeCurrentModifier>);
    // Missing known values use native defaults; existing values are preserved.
    // Newly introduced pools remain at their native Base outside bootstrap.
    bool materialize(std::span<const GameplayTag>);
    std::optional<AttributeValue> resolvedValue(const GameplayTag&) const;
    std::vector<AttributeSnapshot> resolvedSnapshot() const;
    void settleChangesFrom(const AttributeSet& previous);
    std::optional<AttributeValue> value(const GameplayTag&) const;
    std::vector<AttributeSnapshot> snapshot() const;
    std::span<const AttributeChange> changes() const{return changes_;}
    const std::string& status() const{return status_;}
private:
    struct Entry {GameplayTag tag;double seed{};AttributeValue value;};
    const AttributeCatalog* catalog_;
    std::vector<Entry> entries_;
    std::vector<AttributeChange> changes_;
    std::vector<AttributeCurrentModifier> modifiers_;
    std::vector<GameplayTag> pendingFull_;
    std::string status_,unresolved_;
    void evaluate(std::vector<Entry>&,std::span<const GameplayTag> initialize={}) const;
    void publish(std::vector<Entry>);
};
}
