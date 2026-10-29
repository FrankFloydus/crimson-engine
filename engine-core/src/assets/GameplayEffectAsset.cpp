#include "GameplayEffectAsset.h"
#include "AtomicFile.h"
#include "ContentStorage.h"
#include "AssetTransaction.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace crimson {
namespace {
using Json=nlohmann::json;
constexpr std::size_t MaximumEffectBytes=1024*1024;
std::filesystem::path fromUtf8(const std::string& value){return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()),value.size()));}
std::string utf8(const std::filesystem::path& value){const auto text=value.generic_u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
template<class E,std::size_t N> const char* encode(E value,const std::array<const char*,N>& names) {
    const auto i=static_cast<std::size_t>(value);if(i>=N)throw std::runtime_error("Invalid Gameplay Effect enum.");return names[i];
}
template<class E,std::size_t N> E decode(const Json& value,const std::array<const char*,N>& names) {
    const auto name=value.get<std::string>();const auto found=std::find(names.begin(),names.end(),name);
    if(found==names.end())throw std::runtime_error("Unknown Gameplay Effect option: "+name);return static_cast<E>(found-names.begin());
}
constexpr std::array durationNames{"instant","hasDuration","infinite"};
constexpr std::array operationNames{"addBase","multiplyAdditive","divideAdditive","multiplyCompound","addFinal","override"};
constexpr std::array stackingNames{"none","aggregateBySource","aggregateByTarget"};
constexpr std::array refreshNames{"refreshOnSuccessfulApplication","neverRefresh","extendDuration"};
constexpr std::array resetNames{"resetOnSuccessfulApplication","neverReset"};
constexpr std::array expirationNames{"clearEntireStack","removeSingleStackAndRefreshDuration","refreshDuration"};
Json tags(const GameplayTagContainer& values) {auto out=Json::array();for(const auto& tag:values.tags())out.push_back(tag.name());return out;}
GameplayTagContainer readTags(const Json& values) {
    if(!values.is_array()||values.size()>4096)throw std::runtime_error("Invalid Gameplay Effect tags.");
    GameplayTagContainer out;for(const auto& item:values) {GameplayTag tag(item.get<std::string>());if(!tag.valid()||!out.add(tag))throw std::runtime_error("Invalid or duplicate Gameplay Effect tag.");}return out;
}
GameplayEffectAsset starter(bool player) {
    GameplayEffectAsset asset;asset.id=player?PlayerStartingEffectId:EnemyStartingEffectId;asset.name=player?"Player Defaults":"Enemy Defaults";
    const auto add=[&](const GameplayTag& tag,double value){asset.modifiers.push_back({tag,EffectModifierOperation::Override,{value,{},{}}});};
    if(player) {add(stats::Vitality,50);add(stats::Spirit,50);add(stats::MaxHealth,0);add(stats::MaxMana,0);add(stats::HealthRegen,0);add(stats::ManaRegen,0);}
    else add(stats::MaxHealth,100);
    add(stats::MovementSpeed,5);return asset;
}
}
std::shared_ptr<const GameplayEffectAsset> playerStartingEffect(){static const auto asset=std::make_shared<const GameplayEffectAsset>(starter(true));return asset;}
std::shared_ptr<const GameplayEffectAsset> enemyStartingEffect(){static const auto asset=std::make_shared<const GameplayEffectAsset>(starter(false));return asset;}
std::shared_ptr<const GameplayEffectAsset> engineGameplayEffect(const std::string& id) {
    if(id==PlayerStartingEffectId)return playerStartingEffect();if(id==EnemyStartingEffectId)return enemyStartingEffect();return {};
}
bool editableGameplayEffect(const std::string& id) {
    return id==PlayerStartingEffectId || id==EnemyStartingEffectId || !isEngineAssetId(id);
}
void validateGameplayEffectAsset(const GameplayEffectAsset& asset) {
    if(!isAssetUuid(asset.id)||asset.name.empty()||asset.name.size()>1024||asset.name.find_first_not_of(" \t\r\n")==std::string::npos)
        throw std::runtime_error("Gameplay Effect requires a UUID and a nonempty name of at most 1024 bytes.");
    validateGameplayEffect(asset);
}
std::string serializeGameplayEffectAsset(const GameplayEffectAsset& asset) {
    validateGameplayEffectAsset(asset);auto modifiers=Json::array();
    for(const auto& modifier:asset.modifiers) {
        auto parameters=Json::array();for(const auto& parameter:modifier.magnitude.parameters)parameters.push_back({{"name",parameter.name},{"value",parameter.value}});
        modifiers.push_back({{"attribute",modifier.attribute.name()},{"operation",encode(modifier.operation,operationNames)},
            {"magnitude",{{"literal",modifier.magnitude.literal},{"calculation",modifier.magnitude.calculation},{"parameters",std::move(parameters)}}}});
    }
    const auto& stack=asset.stacking;
    auto result=Json{{"type","crimson-gameplay-effect"},{"version",1},{"id",asset.id},{"name",asset.name},
        {"durationPolicy",encode(asset.durationPolicy,durationNames)},{"duration",asset.duration},{"period",asset.period},{"executeOnApplication",asset.executeOnApplication},
        {"modifiers",std::move(modifiers)},{"stacking",{{"type",encode(stack.type,stackingNames)},{"limit",stack.limit},
        {"durationRefresh",encode(stack.durationRefresh,refreshNames)},{"maxDuration",stack.maxDuration},{"periodReset",encode(stack.periodReset,resetNames)},
        {"expiration",encode(stack.expiration,expirationNames)},{"denyOverflowApplication",stack.denyOverflowApplication},
        {"clearStackOnOverflow",stack.clearStackOnOverflow},{"factorInStackCount",stack.factorInStackCount}}},
        {"assetTags",tags(asset.assetTags)},{"grantedTags",tags(asset.grantedTags)},{"sourceRequired",tags(asset.sourceRequired)},
        {"sourceBlocked",tags(asset.sourceBlocked)},{"targetRequired",tags(asset.targetRequired)},{"targetBlocked",tags(asset.targetBlocked)}}.dump(2)+'\n';
    if(result.size()>MaximumEffectBytes)throw std::runtime_error("Gameplay Effect exceeds 1 MiB.");return result;
}
GameplayEffectAsset deserializeGameplayEffectAsset(std::string_view text,const std::string& expectedId) {
    if(text.size()>MaximumEffectBytes)throw std::runtime_error("Gameplay Effect exceeds 1 MiB.");const auto data=Json::parse(text);
    if(data.at("type")!="crimson-gameplay-effect"||data.at("version")!=1)throw std::runtime_error("Unsupported Gameplay Effect type or version.");
    GameplayEffectAsset asset;asset.id=data.at("id").get<std::string>();asset.name=data.at("name").get<std::string>();
    asset.durationPolicy=decode<EffectDurationPolicy>(data.at("durationPolicy"),durationNames);asset.duration=data.at("duration").get<double>();
    asset.period=data.at("period").get<double>();asset.executeOnApplication=data.at("executeOnApplication").get<bool>();
    const auto& modifiers=data.at("modifiers");if(!modifiers.is_array()||modifiers.size()>4096)throw std::runtime_error("Invalid Gameplay Effect modifiers.");
    for(const auto& item:modifiers) {
        EffectModifier modifier;modifier.attribute=GameplayTag(item.at("attribute").get<std::string>());
        modifier.operation=decode<EffectModifierOperation>(item.at("operation"),operationNames);const auto& magnitude=item.at("magnitude");
        modifier.magnitude.literal=magnitude.at("literal").get<double>();modifier.magnitude.calculation=magnitude.at("calculation").get<std::string>();
        const auto& parameters=magnitude.at("parameters");if(!parameters.is_array()||parameters.size()>256)throw std::runtime_error("Invalid Gameplay Effect magnitude parameters.");
        for(const auto& parameter:parameters)modifier.magnitude.parameters.push_back({parameter.at("name").get<std::string>(),parameter.at("value").get<double>()});
        asset.modifiers.push_back(std::move(modifier));
    }
    const auto& stack=data.at("stacking");asset.stacking={decode<EffectStackingType>(stack.at("type"),stackingNames),stack.at("limit").get<std::uint32_t>(),
        decode<EffectDurationRefresh>(stack.at("durationRefresh"),refreshNames),stack.at("maxDuration").get<double>(),
        decode<EffectPeriodReset>(stack.at("periodReset"),resetNames),decode<EffectStackExpiration>(stack.at("expiration"),expirationNames),
        stack.at("denyOverflowApplication").get<bool>(),stack.at("clearStackOnOverflow").get<bool>(),stack.at("factorInStackCount").get<bool>()};
    asset.assetTags=readTags(data.at("assetTags"));asset.grantedTags=readTags(data.at("grantedTags"));asset.sourceRequired=readTags(data.at("sourceRequired"));
    asset.sourceBlocked=readTags(data.at("sourceBlocked"));asset.targetRequired=readTags(data.at("targetRequired"));asset.targetBlocked=readTags(data.at("targetBlocked"));
    validateGameplayEffectAsset(asset);if(!expectedId.empty()&&asset.id!=expectedId)throw std::runtime_error("Gameplay Effect has a different asset identity.");return asset;
}
std::filesystem::path gameplayEffectAssetPath(const std::filesystem::path& root,const std::string& id) {
    if(!isAssetUuid(id))throw std::runtime_error("Gameplay Effect reference must be a UUID.");auto file=resolveAssetPath(root,id);
    if(assetPathExtension(file)!=".crimson-effect")throw std::runtime_error("Gameplay Effect reference has an incompatible asset kind.");return file;
}
GameplayEffectAsset loadGameplayEffectAsset(const std::filesystem::path& root,const std::string& id) {
    const auto starter=engineGameplayEffect(id);
    if(starter && !contentStorageReady(root))return *starter;
    const auto file=gameplayEffectAssetPath(root,id);
    if(starter && !std::filesystem::exists(file))return *starter;
    if(std::filesystem::file_size(file)>MaximumEffectBytes)throw std::runtime_error("Gameplay Effect exceeds 1 MiB.");
    std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Cannot read Gameplay Effect.");
    return deserializeGameplayEffectAsset(std::string{std::istreambuf_iterator<char>(input),{}},id);
}
void saveGameplayEffectAsset(const std::filesystem::path& root,const GameplayEffectAsset& asset) {
    if(!editableGameplayEffect(asset.id))throw std::runtime_error("This engine asset cannot be saved as a Gameplay Effect.");
    if(!contentStorageReady(root))initializeContentStorage(root);const auto contents=serializeGameplayEffectAsset(asset);
    if(engineGameplayEffect(asset.id)) {
        // These two registered role assets are editable game defaults. Their
        // fixed identities/locations do not require an index mutation.
        writeFileAtomically(gameplayEffectAssetPath(root,asset.id),contents);return;
    }
    const auto entries=storedAssetPaths(root);const auto existing=std::find_if(entries.begin(),entries.end(),[&](const auto& entry){return entry.id==asset.id;});
    const auto file=existing==entries.end()?checkedContentPath(root,root/fromUtf8(asset.id+".crimson-effect")):gameplayEffectAssetPath(root,asset.id);
    (void)checkedContentPath(root,file);if(existing==entries.end()&&std::filesystem::exists(file))throw std::runtime_error("An unregistered Gameplay Effect destination already exists.");
    const std::array<StoredAssetPath,1> registration{{{asset.id,file}}};
    const std::array<AssetFileChange,2> changes{{{utf8(file.lexically_relative(root)),contents},{".crimson/asset-locations.json",prepareAssetPathRegistration(root,registration)}}};
    commitAssetTransaction(root,changes);refreshContentStorage(root);
}
GameplayEffectAsset createGameplayEffectAsset(const std::filesystem::path& root,const std::string& name) {
    GameplayEffectAsset asset;asset.name=name;return copyGameplayEffectAsset(root,asset,name);
}
GameplayEffectAsset copyGameplayEffectAsset(const std::filesystem::path& root,const GameplayEffectAsset& source,const std::string& name) {
    auto asset=source;asset.id=newAssetId();if(!name.empty())asset.name=name;saveGameplayEffectAsset(root,asset);return asset;
}
void ensureEngineGameplayEffects(const std::filesystem::path& root) {
    if(!contentStorageReady(root))return;for(const auto& asset:{playerStartingEffect(),enemyStartingEffect()}) {
        const auto file=gameplayEffectAssetPath(root,asset->id);if(!std::filesystem::exists(file))writeFileAtomically(file,serializeGameplayEffectAsset(*asset));
    }
}
}
