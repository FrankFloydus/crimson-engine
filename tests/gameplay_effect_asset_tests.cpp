#include "assets/GameplayEffectAsset.h"
#include "assets/AssetReferenceManager.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "editor/GameplayEffectDocument.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& f,const char* message){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected,message);}
std::string read(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("ce-effect-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);initializeContentStorage(root);}
    ~Project(){std::error_code error;const auto target=std::filesystem::weakly_canonical(root,error),parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("ce-effect-"))std::filesystem::remove_all(target,error);}
};
void codecAndStorage() {
    Project project;ensureEngineGameplayEffects(project.root);
    require(isEngineAssetId(PlayerStartingEffectId)&&isEngineAssetId(EnemyStartingEffectId),"Starter effect identities are unprotected.");
    require(loadGameplayEffectAsset(project.root,PlayerStartingEffectId)==*playerStartingEffect(),"Protected Player definition failed to resolve.");
    require(playerStartingEffect()->modifiers.size()==7&&enemyStartingEffect()->modifiers.size()==2,"Starter effects changed resource initialization policy.");
    const auto startingValue=[](const GameplayEffectAsset& asset,const GameplayTag& attribute)->std::optional<double> {
        const auto found=std::find_if(asset.modifiers.begin(),asset.modifiers.end(),[&](const auto& modifier){return modifier.attribute==attribute;});
        if(found==asset.modifiers.end()||found->operation!=EffectModifierOperation::Override||!found->magnitude.calculation.empty())return {};
        return found->magnitude.literal;
    };
    const auto player=playerStartingEffect(),enemy=enemyStartingEffect();
    require(player->durationPolicy==EffectDurationPolicy::Instant&&enemy->durationPolicy==EffectDurationPolicy::Instant&&
        startingValue(*player,stats::Vitality)==50&&startingValue(*player,stats::Spirit)==50&&startingValue(*player,stats::MovementSpeed)==5&&
        startingValue(*player,stats::MaxHealth)==0&&startingValue(*player,stats::MaxMana)==0&&
        startingValue(*player,stats::HealthRegen)==0&&startingValue(*player,stats::ManaRegen)==0&&
        startingValue(*enemy,stats::MaxHealth)==100&&startingValue(*enemy,stats::MovementSpeed)==5,
        "Player/Enemy starter assets do not contain their authored numeric defaults.");
    for(const auto& starter:{player,enemy})require(!startingValue(*starter,stats::Health)&&!startingValue(*starter,stats::Mana),
        "Starter asset authored a resource amount instead of native full initialization.");
    auto effect=createGameplayEffectAsset(project.root,"Regeneration");effect.durationPolicy=EffectDurationPolicy::HasDuration;effect.duration=4;effect.period=.5;
    effect.executeOnApplication=false;effect.stacking={EffectStackingType::AggregateBySource,3,EffectDurationRefresh::ExtendDuration,12,
        EffectPeriodReset::NeverReset,EffectStackExpiration::RemoveSingleStackAndRefreshDuration,true,true,false};
    for(int operation=0;operation<6;++operation)effect.modifiers.push_back({stats::MaxHealth,static_cast<EffectModifierOperation>(operation),{2,{},{}}});
    effect.modifiers.back().magnitude={11,"SourceVitality",{{"coefficient",2},{"offset",3}}};
    effect.assetTags.add(GameplayTag{"Effect.Future.Asset"});effect.grantedTags.add(GameplayTag{"State.Future.Active"});
    effect.sourceRequired.add(GameplayTag{"Stats"});effect.sourceBlocked.add(GameplayTag{"State.Future.Blocked"});
    effect.targetRequired.add(GameplayTag{"Stats.Health"});effect.targetBlocked.add(GameplayTag{"State.Future.TargetBlocked"});
    require(deserializeGameplayEffectAsset(serializeGameplayEffectAsset(effect),effect.id)==effect,"Effect fields failed to round-trip.");
    saveGameplayEffectAsset(project.root,effect);require(loadGameplayEffectAsset(project.root,effect.id)==effect,"Managed effect save/load changed fields.");
    auto copy=copyGameplayEffectAsset(project.root,effect,"Independent");require(copy.id!=effect.id&&copy.modifiers==effect.modifiers,"Effect duplication reused UUID or changed modifiers.");
    effect.modifiers[0].attribute=GameplayTag{"Stats.Future.Unregistered"};effect.modifiers[0].magnitude.calculation="FutureCalculation";
    require(deserializeGameplayEffectAsset(serializeGameplayEffectAsset(effect))==effect,"Unknown native identities did not survive codec.");
    rejects([&]{saveGameplayEffectAsset(project.root,*playerStartingEffect());},"Protected starter accepted authored Save.");
    auto json=nlohmann::json::parse(serializeGameplayEffectAsset(effect));json["modifiers"][0]["operation"]="unknown";
    rejects([&]{deserializeGameplayEffectAsset(json.dump());},"Unknown operation was accepted.");
    rejects([&]{deserializeGameplayEffectAsset(serializeGameplayEffectAsset(effect),copy.id);},"Expected identity was ignored.");
    auto invalid=effect;invalid.id="not-a-uuid";rejects([&]{serializeGameplayEffectAsset(invalid);},"Invalid managed UUID was serialized.");
    invalid=effect;invalid.name=" ";rejects([&]{serializeGameplayEffectAsset(invalid);},"Whitespace effect name was serialized.");
}
void documentOwnership() {
    Project project;GameplayEffectDocument document(project.root);
    require(document.create("Buff"),document.status());const auto id=document.assetId(),originalBytes=read(gameplayEffectAssetPath(project.root,id));
    const auto revision=document.revision();require(document.begin("Gesture"),"Could not begin effect gesture.");
    require(document.update([](auto& effect){effect.durationPolicy=EffectDurationPolicy::Infinite;effect.period=1;}),document.status());
    require(document.dirty()&&document.revision()==revision&&document.committed()->period==0,"Working drag escaped its isolated definition.");
    document.cancel();require(!document.dirty()&&document.definition().period==0&&!document.canUndo(),"Canceled effect gesture changed history.");
    require(document.edit("Duration",[](auto& effect){effect.durationPolicy=EffectDurationPolicy::HasDuration;effect.duration=5;}),document.status());
    require(document.undo()&&document.definition().durationPolicy==EffectDurationPolicy::Instant&&document.redo(),"Effect history failed.");
    require(document.save("Copy",true),document.status());const auto copyId=document.assetId();
    require(copyId!=id&&read(gameplayEffectAssetPath(project.root,id))==originalBytes,"Save As modified the source asset.");
    while(document.canUndo())require(document.undo(),"Undo failed after Save As.");
    require(document.assetId()==copyId,"Undo after Save As restored source identity.");
    auto external=loadGameplayEffectAsset(project.root,copyId);external.name="External";saveGameplayEffectAsset(project.root,external);
    require(document.externalFileChanged(),"External replacement was not detected.");
    require(document.load(copyId)&&document.definition().name=="External",document.status());
    require(document.load(PlayerStartingEffectId)&&!document.editable()&&!document.begin("Protected"),"Engine effect document allowed editing.");
    require(document.save("Editable Player",true)&&document.editable(),document.status());
    auto seed=*enemyStartingEffect();seed.id=newAssetId();seed.name="Pending";require(document.loadSnapshot(seed)&&document.dirty(),document.status());
    require(!std::filesystem::exists(project.root/"Content"/"Gameplay"/(seed.id+".crimson-effect")),"Loading a staged effect wrote an asset.");
    auto edited=seed;edited.name="Independently saved";saveGameplayEffectAsset(project.root,edited);
    require(document.externalFileChanged(),"Materialized edited seed did not report conflict.");
}
void replacementPolicy() {
    Project project;auto first=createGameplayEffectAsset(project.root,"First"),second=createGameplayEffectAsset(project.root,"Second");
    AssetReferenceManager policy(project.root);const std::array<AssetEntry,1> deleted{{{first.id,first.name,AssetKind::GameplayEffect}}};
    policy.replace(deleted,{{first.id,second.id}});require(policy.resolveGameplayEffect(first.id)==second.id,"Typed effect replacement failed.");
    AssetReferenceManager cleared(project.root);cleared.replace(deleted,{});require(cleared.resolveGameplayEffect(first.id).empty(),"Cleared effect selected a hidden default.");
    AssetReferenceManager wrong(project.root);rejects([&]{wrong.replace(deleted,{{first.id,canonicalAssetId("builtin:navigation-agent/default")}});},"Effect replacement accepted another asset type.");
}
}
int main(){try{codecAndStorage();documentOwnership();replacementPolicy();std::cout<<"Gameplay Effect asset checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
