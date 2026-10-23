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
void setLiteral(GameplayEffectAsset& effect,const GameplayTag& attribute,double value) {
    const auto modifier=std::find_if(effect.modifiers.begin(),effect.modifiers.end(),[&](const auto& candidate){return candidate.attribute==attribute;});
    require(modifier!=effect.modifiers.end(),"Starter effect is missing the tested attribute.");modifier->magnitude.literal=value;
}
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
    auto protectedEffect=*playerStartingEffect();protectedEffect.id=canonicalAssetId("builtin:material/default");
    rejects([&]{saveGameplayEffectAsset(project.root,protectedEffect);},"Effect Save accepted another protected engine identity.");
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
    require(document.load(PlayerStartingEffectId)&&document.editable(),"Player Defaults did not open as an editable effect.");
    const auto starterFile=gameplayEffectAssetPath(project.root,PlayerStartingEffectId);
    require(!std::filesystem::exists(starterFile)&&!document.externalFileChanged(),"An absent starter was materialized or falsely reported as an external conflict.");
    require(document.edit("Materialize starter",[](auto& effect){setLiteral(effect,stats::MovementSpeed,6);})&&document.save(),document.status());
    require(document.assetId()==PlayerStartingEffectId&&std::filesystem::exists(starterFile)&&
        loadGameplayEffectAsset(project.root,PlayerStartingEffectId)==document.definition(),"Saving fallback starter values lost their reserved identity.");
    require(document.save("Editable Player",true)&&document.editable(),document.status());
    auto seed=*enemyStartingEffect();seed.id=newAssetId();seed.name="Pending";require(document.loadSnapshot(seed)&&document.dirty(),document.status());
    require(!std::filesystem::exists(project.root/"Content"/"Gameplay"/(seed.id+".crimson-effect")),"Loading a staged effect wrote an asset.");
    auto edited=seed;edited.name="Independently saved";saveGameplayEffectAsset(project.root,edited);
    require(document.externalFileChanged(),"Materialized edited seed did not report conflict.");
}
void editableStarterDocuments() {
    Project project;ensureEngineGameplayEffects(project.root);
    for(const auto& id:{PlayerStartingEffectId,EnemyStartingEffectId}) {
        GameplayEffectDocument document(project.root);require(document.load(id)&&document.editable(),document.status());
        require(isEngineAssetId(id)&&editableGameplayEffect(id),"Editable starter lost its reserved engine identity.");
        const auto original=document.definition();const auto file=gameplayEffectAssetPath(project.root,id);
        const auto originalBytes=read(file),indexBefore=read(project.root/".crimson"/"asset-locations.json");
        const auto revision=document.revision();require(document.begin("Cancel starter change"),"Starter property gesture could not start.");
        require(document.update([](auto& effect){setLiteral(effect,stats::MovementSpeed,8);}),document.status());
        require(document.dirty()&&document.revision()==revision&&*document.committed()==original,
            "An uncommitted starter edit escaped its document.");
        document.cancel();require(document.definition()==original&&!document.dirty()&&!document.canUndo(),"Cancel changed starter defaults or history.");
        require(document.edit("Tune starter speed",[](auto& effect){setLiteral(effect,stats::MovementSpeed,7);}),document.status());
        const auto changed=document.definition();require(document.dirty()&&changed.id==id,"Starter editing changed identity or failed to mark the document dirty.");
        require(document.undo()&&document.definition()==original&&document.redo()&&document.definition()==changed,"Starter undo/redo lost its authored settings.");
        require(document.save()&&!document.dirty()&&document.assetId()==id,document.status());
        require(gameplayEffectAssetPath(project.root,id)==file&&loadGameplayEffectAsset(project.root,id)==changed&&read(file)!=originalBytes,
            "Starter Save did not replace its own persisted definition.");
        require(read(project.root/".crimson"/"asset-locations.json")==indexBefore,"Saving a starter rewrote the managed location index.");
        const auto savedBytes=read(file);ensureEngineGameplayEffects(project.root);initializeContentStorage(project.root);ensureEngineGameplayEffects(project.root);
        require(read(file)==savedBytes&&loadGameplayEffectAsset(project.root,id)==changed,"Initialization overwrote customized starter defaults.");
        GameplayEffectDocument reopened(project.root);require(reopened.load(id)&&reopened.definition()==changed&&!reopened.externalFileChanged(),reopened.status());
        require(reopened.edit("Discard starter change",[](auto& effect){setLiteral(effect,stats::MovementSpeed,9);})&&reopened.discard()&&
            reopened.definition()==changed&&!reopened.dirty(),"Discard did not restore the starter's saved definition.");
        auto external=changed;setLiteral(external,stats::MovementSpeed,6);saveGameplayEffectAsset(project.root,external);
        require(reopened.externalFileChanged(),"An externally edited starter did not report a conflict.");
        require(reopened.load(id)&&reopened.definition()==external,reopened.status());
        const auto sourceBytes=read(file);require(reopened.save("Independent starter",true)&&reopened.assetId()!=id&&reopened.editable(),reopened.status());
        require(read(file)==sourceBytes&&loadGameplayEffectAsset(project.root,id)==external,"Save As overwrote the source starter.");
        writeFileAtomically(file,"{malformed starter");
        rejects([&]{loadGameplayEffectAsset(project.root,id);},"A corrupt authored starter silently used compiled defaults.");
        writeFileAtomically(file,sourceBytes);
    }
    // Copies add Content assets; saving each reserved starter itself does not
    // replace its identity or move its registered Engine file.
    for(const auto& id:{PlayerStartingEffectId,EnemyStartingEffectId})
        require(gameplayEffectAssetPath(project.root,id).parent_path()==project.root/"Engine"/"gameplay","Starter Save moved its fixed managed file.");
    require(!editableGameplayEffect(canonicalAssetId("builtin:material/default")),"Gameplay Effect editing unlocked another engine role.");
}
void replacementPolicy() {
    Project project;auto first=createGameplayEffectAsset(project.root,"First"),second=createGameplayEffectAsset(project.root,"Second");
    AssetReferenceManager policy(project.root);const std::array<AssetEntry,1> deleted{{{first.id,first.name,AssetKind::GameplayEffect}}};
    policy.replace(deleted,{{first.id,second.id}});require(policy.resolveGameplayEffect(first.id)==second.id,"Typed effect replacement failed.");
    AssetReferenceManager cleared(project.root);cleared.replace(deleted,{});require(cleared.resolveGameplayEffect(first.id).empty(),"Cleared effect selected a hidden default.");
    AssetReferenceManager wrong(project.root);rejects([&]{wrong.replace(deleted,{{first.id,canonicalAssetId("builtin:navigation-agent/default")}});},"Effect replacement accepted another asset type.");
    AssetReferenceManager protectedRole(project.root);const std::array<AssetEntry,1> starter{{{EnemyStartingEffectId,"Enemy Defaults",AssetKind::GameplayEffect}}};
    rejects([&]{protectedRole.replace(starter,{});},"Editable starter defaults became deletable engine roles.");
}
}
int main(){try{codecAndStorage();documentOwnership();editableStarterDocuments();replacementPolicy();std::cout<<"Gameplay Effect asset checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
