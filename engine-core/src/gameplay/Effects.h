#pragma once
#include "Attributes.h"
#include <functional>
#include <memory>

namespace crimson {
enum class EffectDurationPolicy {Instant,HasDuration,Infinite};
enum class EffectStackingType {None,AggregateBySource,AggregateByTarget};
enum class EffectDurationRefresh {RefreshOnSuccessfulApplication,NeverRefresh,ExtendDuration};
enum class EffectPeriodReset {ResetOnSuccessfulApplication,NeverReset};
enum class EffectStackExpiration {ClearEntireStack,RemoveSingleStackAndRefreshDuration,RefreshDuration};
struct EffectMagnitudeParameter {std::string name;double value{};bool operator==(const EffectMagnitudeParameter&) const=default;};
struct EffectMagnitude {
    double literal{};
    // Empty selects literal; nonempty identifies an immutable native C++ calculation.
    std::string calculation;
    std::vector<EffectMagnitudeParameter> parameters;
    bool operator==(const EffectMagnitude&) const=default;
};
struct EffectModifier {
    GameplayTag attribute;
    EffectModifierOperation operation{EffectModifierOperation::AddBase};
    EffectMagnitude magnitude;
    bool operator==(const EffectModifier&) const=default;
};
struct EffectStacking {
    EffectStackingType type{EffectStackingType::None};
    std::uint32_t limit{};
    EffectDurationRefresh durationRefresh{EffectDurationRefresh::RefreshOnSuccessfulApplication};
    double maxDuration{};
    EffectPeriodReset periodReset{EffectPeriodReset::ResetOnSuccessfulApplication};
    EffectStackExpiration expiration{EffectStackExpiration::ClearEntireStack};
    bool denyOverflowApplication{},clearStackOnOverflow{},factorInStackCount{true};
    bool operator==(const EffectStacking&) const=default;
};
struct GameplayEffectAsset {
    std::string id,name;
    EffectDurationPolicy durationPolicy{EffectDurationPolicy::Instant};
    double duration{},period{};
    bool executeOnApplication{true};
    std::vector<EffectModifier> modifiers;
    EffectStacking stacking;
    GameplayTagContainer assetTags,grantedTags,sourceRequired,sourceBlocked,targetRequired,targetBlocked;
    bool operator==(const GameplayEffectAsset&) const=default;
};
struct EffectCalculationParameter {
    std::string name;
    double defaultValue{};
    std::optional<double> minimum,maximum;
};
struct EffectCalculationContext {
    std::span<const AttributeSnapshot> source,target;
    const GameplayTagContainer* sourceTags{};
    const GameplayTagContainer* targetTags{};
};
using EffectCalculationFunction=std::function<std::optional<double>(const EffectCalculationContext&,std::span<const EffectMagnitudeParameter>)>;
struct EffectCalculationDefinition {
    std::string id,name;
    std::vector<EffectCalculationParameter> parameters;
    EffectCalculationFunction calculate;
};
class EffectCalculationRegistry {
public:
    explicit EffectCalculationRegistry(std::vector<EffectCalculationDefinition> definitions={});
    std::span<const EffectCalculationDefinition> definitions() const{return definitions_;}
    const EffectCalculationDefinition* find(std::string_view) const;
    std::optional<double> resolve(const EffectMagnitude&,const EffectCalculationContext&) const;
private:std::vector<EffectCalculationDefinition> definitions_;
};
const EffectCalculationRegistry& nativeEffectCalculations();
// Structural validation preserves unresolved native IDs in saved assets. The
// runtime additionally rejects unavailable attributes/calculations/tags atomically.
void validateGameplayEffect(const GameplayEffectAsset&,const AttributeCatalog& =nativeAttributeCatalog(),
    const EffectCalculationRegistry& =nativeEffectCalculations(),bool allowUnresolved=true);
enum class EffectReadiness {Ready,Preparing,Unavailable,Failed};
struct EffectDefinitionSnapshot {
    std::shared_ptr<const GameplayEffectAsset> definition;
    std::uint64_t generation{};
    std::string diagnostic;
    EffectReadiness readiness{EffectReadiness::Unavailable};
};
using GameplayEffectQuery=std::function<EffectDefinitionSnapshot(const std::string&)>;
struct EffectEntityRef {std::uint64_t world{},entity{},incarnation{};bool operator==(const EffectEntityRef&) const=default;};
struct EffectHandle {std::uint64_t world{},serial{};explicit operator bool() const{return world&&serial;}bool operator==(const EffectHandle&) const=default;};
enum class EffectApplyStatus {Applied,Preparing,Unavailable,Rejected,Failed};
struct EffectApplyResult {
    EffectApplyStatus status{EffectApplyStatus::Failed};EffectHandle handle;std::string diagnostic;
    explicit operator bool() const{return status==EffectApplyStatus::Applied;}
};
struct ActiveEffectSnapshot {
    EffectHandle handle;std::string definitionId,name;std::uint64_t generation{};
    EffectEntityRef source,target;std::uint32_t stacks{1};
    std::optional<double> remaining;double period{};
};
}
