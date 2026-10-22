#include "AssetReferenceManager.h"
#include "scene/BuiltinModels.h"
#include "MaterialAsset.h"
#include "AtomicFile.h"
#include "ContentStorage.h"
#include "NavigationAgentProfile.h"
#include "GameplayEffectIds.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace crimson {
namespace {
using Json=nlohmann::json;
std::filesystem::path path(const std::string& id){return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(id.data()),id.size()));}
std::string text(const std::filesystem::path& file){std::ifstream in(file,std::ios::binary);if(!in)throw std::runtime_error("Cannot read asset transaction file.");return {std::istreambuf_iterator<char>(in),{}};}
bool builtin(const std::string& id){return isEngineAssetId(id);}
bool knownBuiltin(const std::string& id){return id==PlayerStartingEffectId||id==EnemyStartingEffectId||id==GrayTextureId||id==NeutralNormalTextureId||id==DefaultMaterialId||id==DefaultParticleMaterialId||id==StudioEnvironmentId||id==DefaultNavigationAgentProfileId||isBuiltinModel(id);}
std::optional<std::filesystem::path> managedIdentityPath(const std::filesystem::path& root,const std::string& id) {
    std::optional<std::filesystem::path> resolved;
    try{resolved=resolveAssetPath(root,id);}catch(const std::exception&){/* Deleted bundle members no longer have a live location. */}
    if(resolved)return checkedManagedAssetPath(root,id);
    // Aliases retain source provenance after a deleted member's cache location
    // is removed. An opaque UUID without provenance has no inferred asset kind.
    const auto legacy=legacyAssetId(root,id);
    if(legacy==id)return {};
    return checkedManagedAssetPath(root,legacy);
}
void validateDeletedIdentity(const std::filesystem::path& root,const std::string& id) {
    if(builtin(id))throw std::runtime_error("Built-in asset cannot be deleted.");
    if(isAssetUuid(id)){
        if(canonicalAssetId(id)!=id)throw std::runtime_error("Deleted asset identity must be a canonical UUID.");
        // Tombstones reserve immutable identities, independently of whether a
        // deleted asset still owns a registered physical file.
        (void)managedIdentityPath(root,id);return;
    }
    checkedManagedAssetPath(root,id);
}
bool gameplayEffectIdentity(const std::filesystem::path& root,const std::string& id) {
    if(id==PlayerStartingEffectId||id==EnemyStartingEffectId)return true;
    if(!isAssetUuid(id)||builtin(id))return false;
    try{return resolveAssetPath(root,id).extension()==".crimson-effect";}catch(...){return true;}
}
bool navigationProfileIdentity(const std::filesystem::path& root,const std::string& id) {
    if(id==DefaultNavigationAgentProfileId)return true;
    if(!isAssetUuid(id)||builtin(id))return false;
    try{return resolveAssetPath(root,id).extension()==".crimson-agent-profile";}catch(...){return true;}
}
bool animationIdentity(const std::filesystem::path& root,const std::string& id) {
    if(!isAssetUuid(id)||builtin(id))return false;
    try{return resolveAssetPath(root,id).extension()==".crimson-animation";}catch(...){return true;}
}
bool particleIdentity(const std::filesystem::path& root,const std::string& id){if(isAssetUuid(id)){if(builtin(id))return false;try{return resolveAssetPath(root,id).extension()==".crimson-particle";}catch(...){return true;}}const auto relative=path(id);return relative.parent_path()==std::filesystem::path("assets/particles")&&relative.extension()==".crimson-particle";}
bool materialIdentity(const std::filesystem::path& root,const std::string& id){if(isAssetUuid(id)){if(builtin(id))return false;try{return resolveAssetPath(root,id).extension()==".crimson-material";}catch(...){return true;}}const auto relative=path(id);return relative.parent_path()==std::filesystem::path("assets/materials")&&relative.extension()==".crimson-material";}
void validateMaterialTarget(const std::filesystem::path& root,const std::string& target){if(!target.empty()&&target!=DefaultMaterialId&&target!=DefaultParticleMaterialId&&!materialIdentity(root,target))throw std::runtime_error("Material replacement must reference a managed material or protected material default.");}
}
AssetReferenceManager::AssetReferenceManager(std::filesystem::path root):root_(std::move(root)){
    recoverAssetTransactions(root_);const auto file=resolveAssetPath(root_,"assets/references.json");
    try{if(!std::filesystem::exists(file))return;const auto j=Json::parse(text(file));if(j.at("version")!=1)throw std::runtime_error("Unsupported asset reference state version.");
        for(const auto& id:j.at("deleted")){const auto value=id.get<std::string>();validateDeletedIdentity(root_,value);deleted_.insert(value);}
        for(auto it=j.at("replacements").begin();it!=j.at("replacements").end();++it){const auto replacement=it.value().get<std::string>();
            if(!deleted_.contains(it.key()))throw std::runtime_error("Replacement source is not a deleted managed asset.");
            if(builtin(replacement)&&!knownBuiltin(replacement))throw std::runtime_error("Replacement references an unknown built-in resource.");
            if(!replacement.empty()&&!builtin(replacement)&&!deleted_.contains(replacement))checkedManagedAssetPath(root_,replacement);replacements_.emplace(it.key(),replacement);}
        for(const auto& [id,target]:replacements_){(void)target;const auto source=managedIdentityPath(root_,id);
            if(source&&source->extension()==".crimson-animation")resolveAnimation(id);
            else if(source&&source->extension()==".crimson-effect")resolveGameplayEffect(id);
            else if(source&&source->extension()==".crimson-particle")resolveParticle(id);
            else if(source&&source->extension()==".crimson-material")validateMaterialTarget(root_,resolve(id));
            else resolve(id);}
    }catch(const std::exception& e){deleted_.clear();replacements_.clear();writable_=false;status_="Asset reference state could not be loaded: "+std::string(e.what())+" The existing file was preserved; destructive asset operations are disabled.";}
}
std::string AssetReferenceManager::resolve(const std::string& id)const{
    std::unordered_set<std::string> visited;auto current=contentStorageReady(root_)&&!id.empty()?canonicalAssetId(id):id;
    while(!current.empty()){if(!visited.insert(current).second)throw std::runtime_error("Asset fallback references contain a cycle.");
        const auto found=replacements_.find(current);if(found==replacements_.end())return deleted_.contains(current)?std::string{}:current;current=found->second;}
    return {};
}
std::vector<std::string> AssetReferenceManager::replacementTargets()const{
    if(!writable_)throw std::runtime_error(status_);std::set<std::string> targets;
    for(const auto& [source,target]:replacements_){(void)source;const auto resolved=resolve(target);if(!resolved.empty())targets.insert(resolved);}
    return {targets.begin(),targets.end()};
}
std::string AssetReferenceManager::resolveTexture(const std::string& id,bool normal)const{auto result=resolve(id);if(!id.empty()&&(result.empty()||result==GrayTextureId))return normal?NeutralNormalTextureId:GrayTextureId;return result;}
std::string AssetReferenceManager::resolveMaterial(const std::string& id)const{const auto result=resolve(id);validateMaterialTarget(root_,result);return result;}
std::string AssetReferenceManager::resolveModelMaterial(const std::string& id)const{
    const auto result=resolveMaterial(id);
    if(result==DefaultParticleMaterialId)throw std::runtime_error("Box material replacement requires a Standard PBR material.");
    return result.empty()?std::string(DefaultMaterialId):result;
}
std::string AssetReferenceManager::resolveEnvironment(const std::string& id)const{const auto result=resolve(id);return result==StudioEnvironmentId?std::string{}:result;}
std::string AssetReferenceManager::resolveParticle(const std::string& id)const{
    auto result=resolve(id);if(result.empty())return result;
    if(!particleIdentity(root_,result))
        throw std::runtime_error("Particle replacement must reference a managed particle effect.");
    return result;
}
std::string AssetReferenceManager::resolveNavigationAgentProfile(const std::string& id)const {
    const auto result=resolve(id);
    if(result.empty())return DefaultNavigationAgentProfileId;
    if(!navigationProfileIdentity(root_,result))throw std::runtime_error("Navigation Agent Profile replacement has an incompatible asset kind.");
    return result;
}
std::string AssetReferenceManager::resolveGameplayEffect(const std::string& id)const {
    const auto result=resolve(id);if(!result.empty()&&!gameplayEffectIdentity(root_,result))throw std::runtime_error("Gameplay Effect replacement has an incompatible asset kind.");return result;
}
std::string AssetReferenceManager::resolveAnimation(const std::string& id)const {
    const auto result=resolve(id);if(!result.empty()&&!animationIdentity(root_,result))throw std::runtime_error("Animation replacement has an incompatible asset kind.");return result;
}
std::string AssetReferenceManager::resolveParticleMaterial(const std::string& id)const{
    const auto result=resolve(id);if(result.empty()||(result==DefaultMaterialId&&deleted_.contains(id)))return DefaultParticleMaterialId;
    validateMaterialTarget(root_,result);if(!builtin(result))checkedManagedAssetPath(root_,result);return result;
}
std::string AssetReferenceManager::resolveParticleTexture(const std::string& id)const{
    const auto result=resolve(id);return result==GrayTextureId?std::string{}:result;
}
void AssetReferenceManager::setUsages(std::vector<AssetUsage> usages){usages_=std::move(usages);}
std::vector<AssetUsage> AssetReferenceManager::users(const std::string& id)const{
    std::vector<AssetUsage> result;std::vector<std::string> pending{id};std::unordered_set<std::string> visited;std::set<std::tuple<std::string,std::string,std::string,bool,bool,bool>> emitted;
    while(!pending.empty()){const auto target=pending.back();pending.pop_back();if(!visited.insert(target).second)continue;
        for(const auto& u:usages_)if(u.targetId==target||resolve(u.targetId)==target){
            if(emitted.emplace(u.targetId,u.ownerId,u.slot,u.active,u.historical,u.editorState).second)result.push_back(u);
            if(!u.ownerId.empty()&&u.ownerId!=target)pending.push_back(u.ownerId);
        }
    }return result;
}
std::vector<AssetUsage> AssetReferenceManager::authoredUsers(const std::string& id,std::span<const std::string> removedOwners)const{
    // Match Godot's dependency removal dialog: only surviving owner files with
    // a direct dependency are counted. Runtime residency and undo records are
    // separate impact information, even when they refer to the same resource.
    struct Owner { AssetUsage usage;std::set<std::string> slots; };
    std::map<std::string,Owner> owners;
    const std::unordered_set<std::string> removed(removedOwners.begin(),removedOwners.end());
    for(const auto& usage:usages_) {
        if(usage.active||usage.historical||usage.editorState||usage.ownerId.empty()||usage.ownerId==id||removed.contains(usage.ownerId))continue;
        if(usage.targetId!=id&&resolve(usage.targetId)!=id)continue;
        // Imported package metadata keeps the original material identity so
        // historical references remain resolvable. Warnings name its surviving
        // native replacement rather than a deleted sidecar or built-in default.
        const auto ownerId=resolve(usage.ownerId);
        if(ownerId.empty()||builtin(ownerId)||ownerId==id||removed.contains(ownerId))continue;
        auto [owner,inserted]=owners.try_emplace(ownerId);
        if(inserted){owner->second.usage=usage;owner->second.usage.targetId=id;owner->second.usage.ownerId=ownerId;}
        if(ownerId==usage.ownerId)owner->second.usage.ownerName=usage.ownerName;
        if(!usage.slot.empty())owner->second.slots.insert(usage.slot);
    }
    std::vector<AssetUsage> result;result.reserve(owners.size());
    for(auto& [ownerId,owner]:owners) {
        (void)ownerId;owner.usage.slot.clear();
        for(const auto& slot:owner.slots){if(!owner.usage.slot.empty())owner.usage.slot+=", ";owner.usage.slot+=slot;}
        result.push_back(std::move(owner.usage));
    }
    return result;
}
void AssetReferenceManager::replace(std::span<const AssetEntry> assets,const std::unordered_map<std::string,std::string>& replacements){
    if(!writable_)throw std::runtime_error(status_);auto deleted=deleted_;auto redirects=replacements_;std::unordered_set<std::string> selected;
    for(const auto& a:assets){if(builtin(a.id))throw std::runtime_error("Built-in assets cannot be deleted.");selected.insert(a.id);}
    for(const auto& a:assets){auto found=replacements.find(a.id);std::string target=found==replacements.end()?std::string{}:found->second;
        checkedManagedAssetPath(root_,a.id);
        if(selected.contains(target))throw std::runtime_error("A fallback cannot be another asset in the deletion batch.");
        if(!target.empty()&&!builtin(target))checkedManagedAssetPath(root_,target);
        if(a.kind==AssetKind::Particle && !target.empty()) {
            if(!particleIdentity(root_,target))
                throw std::runtime_error("Particle replacement must reference another managed particle effect.");
        }
        if(a.kind==AssetKind::Material){validateMaterialTarget(root_,target);if(!target.empty()&&!builtin(target)&&checkedManagedAssetPath(root_,target).extension()!=".crimson-material")throw std::runtime_error("Material replacement has an incompatible asset kind.");}
        if(a.kind==AssetKind::Particle&&!target.empty()&&checkedManagedAssetPath(root_,target).extension()!=".crimson-particle")throw std::runtime_error("Particle replacement has an incompatible asset kind.");
        if(a.kind==AssetKind::NavigationAgentProfile&&!target.empty()&&!navigationProfileIdentity(root_,target))throw std::runtime_error("Navigation Agent Profile replacement has an incompatible asset kind.");
        if(a.kind==AssetKind::GameplayEffect&&!target.empty()&&!gameplayEffectIdentity(root_,target))throw std::runtime_error("Gameplay Effect replacement has an incompatible asset kind.");
        if(a.kind==AssetKind::Animation&&!target.empty()&&!animationIdentity(root_,target))throw std::runtime_error("Animation replacement has an incompatible asset kind.");
        if(builtin(target)&&(!knownBuiltin(target)||(a.kind==AssetKind::Texture&&target!=GrayTextureId&&target!=NeutralNormalTextureId)||
            (a.kind==AssetKind::Environment&&target!=StudioEnvironmentId)||(a.kind==AssetKind::Material&&target!=DefaultMaterialId&&target!=DefaultParticleMaterialId)||
            (a.kind==AssetKind::NavigationAgentProfile&&target!=DefaultNavigationAgentProfileId)||(a.kind==AssetKind::GameplayEffect&&target!=PlayerStartingEffectId&&target!=EnemyStartingEffectId)||a.kind==AssetKind::Model||a.kind==AssetKind::Scene||a.kind==AssetKind::Particle||a.kind==AssetKind::Animation))
            throw std::runtime_error("Built-in replacement has an incompatible asset kind.");
        if(target.empty()){switch(a.kind){case AssetKind::Texture:target=GrayTextureId;break;case AssetKind::Environment:target=StudioEnvironmentId;break;case AssetKind::Material:target=DefaultMaterialId;break;case AssetKind::NavigationAgentProfile:target=DefaultNavigationAgentProfileId;break;default:break;}}
        deleted.insert(a.id);redirects[a.id]=std::move(target);
    }
    auto previousDeleted=std::move(deleted_);auto previousRedirects=std::move(replacements_);deleted_=std::move(deleted);replacements_=std::move(redirects);
    try{for(const auto& [id,target]:replacements_){(void)target;const auto source=managedIdentityPath(root_,id);
        if(source&&source->extension()==".crimson-animation")resolveAnimation(id);
        else if(source&&source->extension()==".crimson-effect")resolveGameplayEffect(id);
        else if(source&&source->extension()==".crimson-particle")resolveParticle(id);
        else if(source&&source->extension()==".crimson-material")validateMaterialTarget(root_,resolve(id));
        else resolve(id);}}
    catch(...){deleted_=std::move(previousDeleted);replacements_=std::move(previousRedirects);throw;}++revision_;
}
std::string AssetReferenceManager::serialize()const{std::vector<std::string> deleted(deleted_.begin(),deleted_.end());std::sort(deleted.begin(),deleted.end());return Json{{"version",1},{"deleted",deleted},{"replacements",replacements_}}.dump(2)+"\n";}
void AssetReferenceManager::save()const{if(!writable_)throw std::runtime_error(status_);writeFileAtomically(resolveAssetPath(root_,"assets/references.json"),serialize());}
}
