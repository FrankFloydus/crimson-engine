#include "scene/SceneComponents.h"
#include "assets/GameplayEffectAsset.h"
#include "assets/AtomicFile.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
using Json=nlohmann::json;
namespace fs=std::filesystem;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-effects-scene-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){fs::create_directories(root/"assets/scenes");}
    ~Project(){std::error_code error;const auto temp=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-effects-scene-"))fs::remove_all(target,error);}
};
std::string read(const fs::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
Json legacy(const std::string& name="Legacy effects") {
    EntityRecord record;record.id=17;record.name="Customized actor";
    auto json=Json::parse(serializeSceneSnapshot({{},name,{record},{}}));json["version"]=21;
    json["entities"][0]["attributes"]=Json::array({
        {{"tag","Stats.Health"},{"startingBase",-99}},{{"tag","Stats.MaxHealth"},{"startingBase",0}},
        {{"tag","Stats.Mana"},{"startingBase",3}},{{"tag","Stats.MaxMana"},{"startingBase",0}},
        {{"tag","Stats.Vitality"},{"startingBase",18}},{{"tag","Stats.Spirit"},{"startingBase",24}},
        {{"tag","Stats.MovementSpeed"},{"startingBase",3}},{{"tag","Project.Carry"},{"startingBase",12.5}}});
    return json;
}
double magnitude(const GameplayEffectAsset& effect,const GameplayTag& tag) {
    const auto found=std::find_if(effect.modifiers.begin(),effect.modifiers.end(),[&](const auto& mod){return mod.attribute==tag;});
    require(found!=effect.modifiers.end(),"Converted effect omitted "+tag.name());
    require(found->operation==EffectModifierOperation::Override&&found->magnitude.calculation.empty(),"Conversion did not author an independent literal Override.");
    return found->magnitude.literal;
}
void recipesAndMembership() {
    Project project;SceneDocument document(project.root);const auto player=document.createPlayer(),enemy=document.createEnemy();
    require(static_cast<unsigned>(SceneComponentKind::Effects)==21,"Effects changed a persistent component identity.");
    const auto p=*document.scene().find(player),e=*document.scene().find(enemy);
    require(p.effects->starting==std::vector{AssetRef<GameplayEffectAsset>{PlayerStartingEffectId}}&&
        e.effects->starting==std::vector{AssetRef<GameplayEffectAsset>{EnemyStartingEffectId}},"Role recipes did not reference their protected starting effects.");
    require(document.gameplayEffect(PlayerStartingEffectId)&&document.gameplayEffect(EnemyStartingEffectId),"New scenes cannot resolve protected role effects without an asset callback.");
    const auto json=Json::parse(serializeSceneSnapshot({{},"Membership",{p,e},{}}));
    require(json["version"]==24&&!json["entities"][0].contains("attributes")&&json["entities"][0]["effects"]["starting"].size()==1,"Schema24 retained authored membership or omitted ordered references.");
}
void deterministicReadAndCurrentSubsets() {
    Project project;const std::string id="assets/scenes/legacy.crimson-scene";const auto bytes=legacy().dump(2);writeFileAtomically(project.root/id,bytes);
    const auto first=readSceneSnapshot(project.root,id),again=readSceneSnapshot(project.root,id);
    require(first.stagedGameplayEffects.size()==1&&first.records.front().effects==again.records.front().effects,"Repeated conversion created new effect identities.");
    const auto effect=first.stagedGameplayEffects.front();
    require(effect->durationPolicy==EffectDurationPolicy::Instant&&effect->modifiers.size()==6&&magnitude(*effect,stats::Vitality)==18&&
        magnitude(*effect,stats::Spirit)==24&&magnitude(*effect,stats::MovementSpeed)==3&&magnitude(*effect,GameplayTag{"Project.Carry"})==12.5,
        "Legacy conversion lost custom or unresolved values, or retained ignored resource amounts.");
    require(std::none_of(effect->modifiers.begin(),effect->modifiers.end(),[](const auto& mod){return mod.attribute==stats::Health||mod.attribute==stats::Mana;}),"Ignored old resource starting amounts became initialization overrides.");
    SceneDocument document(project.root);require(document.load(id)&&!document.dirty()&&document.history().size()==0&&document.gameplayEffect(effect->id)&&read(project.root/id)==bytes&&!fs::exists(project.root/"Content"),"Opening legacy values wrote files or dirtied the scene.");
    auto changed=legacy();changed["entities"][0]["attributes"][4]["startingBase"]=19;writeFileAtomically(project.root/id,changed.dump());
    require(readSceneSnapshot(project.root,id).stagedGameplayEffects.front()->id!=effect->id,"Changed legacy values reused an existing editable effect identity.");
    auto zero=legacy();zero["entities"][0]["attributes"][6]["startingBase"]=0;writeFileAtomically(project.root/id,zero.dump());
    require(magnitude(*readSceneSnapshot(project.root,id).stagedGameplayEffects.front(),stats::MovementSpeed)==0,"Legacy explicit zero speed was replaced by its native nonzero default.");
    auto current=Json::parse(serializeSceneSnapshot({{},"Exact subset",first.records,{}}));current["version"]=22;current["entities"][0].erase("effects");
    current["entities"][0]["attributes"]=Json::array({"Stats.Vitality","Project.Zero"});writeFileAtomically(project.root/id,current.dump());
    const auto subset=readSceneSnapshot(project.root,id);
    require(subset.records.front().effects&&subset.stagedGameplayEffects.size()==1&&magnitude(*subset.stagedGameplayEffects.front(),stats::Vitality)==0&&magnitude(*subset.stagedGameplayEffects.front(),GameplayTag{"Project.Zero"})==0,"Schema22 conversion lost explicit zero or unresolved selected values.");
}
void membershipConversionDefaultsAndExistingEffects() {
    Project project;SceneDocument source(project.root);const auto player=source.createPlayer(),enemy=source.createEnemy();
    auto json=Json::parse(serializeSceneSnapshot({{},"Schema22 roles",source.scene().records(),source.scene().properties()}));json["version"]=22;
    for(auto& entity:json["entities"])if(entity.contains("playerInput")) {
        auto names=Json::array();for(const auto& definition:nativeAttributeCatalog().definitions())names.push_back(definition.tag.name());entity["attributes"]=std::move(names);
    }else if(entity.contains("enemy"))entity["attributes"]={"Stats.Health","Stats.MaxHealth","Stats.MovementSpeed"};
    const std::string id="assets/scenes/schema22-roles.crimson-scene";const auto bytes=json.dump(2);writeFileAtomically(project.root/id,bytes);
    SceneDocument converted(project.root);require(converted.load(id),converted.status());
    require(converted.stagedGameplayEffects().empty()&&converted.scene().find(player)->effects==source.scene().find(player)->effects&&
        converted.scene().find(enemy)->effects==source.scene().find(enemy)->effects&&!converted.dirty()&&read(project.root/id)==bytes,
        "Schema22 role effects gained redundant default initializers or loading changed the saved document.");
    EntityRecord generic;generic.id=1;generic.name="Selected native defaults";
    auto defaults=Json::parse(serializeSceneSnapshot({{},"Schema22 defaults",{generic},{}}));defaults["version"]=22;
    defaults["entities"][0]["attributes"]={"Stats.MovementSpeed","Stats.Spirit","Stats.MaxHealth"};writeFileAtomically(project.root/id,defaults.dump());
    const auto snapshot=readSceneSnapshot(project.root,id);require(snapshot.stagedGameplayEffects.size()==1,"Schema22 selected values lost activation when membership was removed.");
    const auto effect=snapshot.stagedGameplayEffects.front();
    require(magnitude(*effect,stats::MovementSpeed)==nativeAttributeCatalog().find(stats::MovementSpeed)->defaultBase&&
        magnitude(*effect,stats::Spirit)==0&&magnitude(*effect,stats::MaxHealth)==0,
        "Schema22 conversion confused selected zero values with native nonzero defaults.");
    require(!Json::parse(serializeSceneSnapshot(snapshot))["entities"][0].contains("attributes"),"Converted schema22 membership leaked into the current writer.");
}
void membershipConversionPreservesUnknownEffectModifiers() {
    Project project;GameplayEffectAsset unavailable;unavailable.id=newAssetId();unavailable.name="Future native attribute";
    unavailable.modifiers={{GameplayTag{"Stats.FutureNative"},EffectModifierOperation::Override,{12,{},{}}}};
    saveGameplayEffectAsset(project.root,unavailable);const auto effectFile=gameplayEffectAssetPath(project.root,unavailable.id);const auto effectBytes=read(effectFile);
    EntityRecord actor;actor.id=1;actor.name="Forward-compatible effect";actor.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{unavailable.id}}};
    const auto id=newAssetId();auto json=Json::parse(serializeSceneSnapshot({id,"Unknown native effect",{actor},{}}));json["version"]=22;
    json["entities"][0]["attributes"]={"Stats.MovementSpeed"};const auto bytes=json.dump(2);
    const auto file=project.root/"Content"/"Unknown native effect.crimson-scene";writeFileAtomically(file,bytes);registerAssetPath(project.root,id,file);
    SceneDocument converted(project.root);require(converted.load(id),converted.status());const auto record=*converted.scene().find(actor.id);
    require(record.effects&&record.effects->starting.size()==2&&record.effects->starting.back().id==unavailable.id&&
        converted.stagedGameplayEffects().size()==1&&magnitude(*converted.stagedGameplayEffects().front(),stats::MovementSpeed)==5,
        "An unknown effect modifier prevented membership conversion or lost the original effect reference.");
    require(!converted.dirty()&&converted.history().size()==0&&read(file)==bytes&&read(effectFile)==effectBytes,
        "Opening a schema22 scene with an unknown modifier changed authored files or history.");
}
void historyPlayAndPersistence() {
    Project project;const std::string id="assets/scenes/legacy.crimson-scene";writeFileAtomically(project.root/id,legacy().dump());
    SceneDocument document(project.root);require(document.load(id),document.status());const auto actor=document.scene().find(17);const auto effect=document.legacyGameplayEffectSeedForEntity(17);
    require(actor&&effect,"Cannot prepare converted effect fixture.");
    auto repeated=*actor;repeated.effects->starting.emplace_back(effect->id);require(document.updateEntity(17,repeated),document.status());
    require(document.undo()&&sameEntity(*document.scene().find(17),*actor)&&document.redo()&&sameEntity(*document.scene().find(17),repeated),"Effects field did not participate in atomic history.");
    require(document.begin("Change Effects")&&document.update([&](auto& records,auto&){records.front().effects->starting.clear();}),document.status());document.cancel();
    require(document.scene().find(17)->effects==repeated.effects,"Cancel did not restore ordered repeated effects.");
    document.select({17},17);require(document.duplicateSelection(),document.status());const auto copy=document.selection().primary;
    require(document.scene().find(copy)->effects==repeated.effects&&document.gameplayEffect(effect->id),"Duplication lost staged definitions or repeated applications.");
    SceneDocument play(project.root);require(play.resetTransientFrom(document)&&play.gameplayEffect(effect->id)&&play.scene().find(copy)->effects==repeated.effects,"Transient Play copy lost pending definitions.");
    require(removeSceneComponent(document,17,SceneComponentKind::Effects),document.status());const auto refs=document.historicalGameplayEffectReferences();
    require(std::find(refs.begin(),refs.end(),effect->id)!=refs.end()&&document.undo(),"Historical references omitted a removed effect.");
    require(document.save("Materialized conversion"),document.status());const auto file=sceneAssetPath(project.root,document.assetId());
    require(Json::parse(read(file))["version"]==24&&loadGameplayEffectAsset(project.root,effect->id)==*effect&&!document.gameplayEffect(effect->id),"Scene save did not materialize its pending definition and retire staged resolution.");
    SceneDocument reopened(project.root);require(reopened.load(document.assetId())&&!reopened.dirty()&&reopened.scene().find(copy)->effects==repeated.effects&&reopened.stagedGameplayEffects().empty(),"Current scene reopened with a second conversion or lost application order.");
    require(reopened.save("Materialized copy",true),reopened.status());SceneDocument copied(project.root);
    require(copied.load(reopened.assetId())&&copied.scene().find(17)->effects==repeated.effects,"Save As changed references or duplicated migrated definitions.");
}
void independentlyMaterializedWins() {
    Project project;const std::string id="assets/scenes/legacy.crimson-scene";writeFileAtomically(project.root/id,legacy().dump());initializeContentStorage(project.root);
    SceneDocument document(project.root);require(document.load(canonicalAssetId(id)),document.status());const auto seed=document.legacyGameplayEffectSeedForEntity(17);require(bool(seed),"Missing seed fixture.");
    auto edited=*seed;edited.name="Independently edited";edited.modifiers.front().magnitude.literal=71;saveGameplayEffectAsset(project.root,edited);
    require(!document.gameplayEffect(seed->id),"Materialized identity still resolved to a stale seed.");
    require(document.save(),document.status());require(loadGameplayEffectAsset(project.root,seed->id)==edited,"Scene Save overwrote an independently edited migrated effect.");
    const auto file=gameplayEffectAssetPath(project.root,seed->id);writeFileAtomically(file,"corrupt");
    SceneSnapshot snapshot{document.assetId(),document.displayName(),document.scene().records(),document.scene().properties()};snapshot.stagedGameplayEffects={seed};
    bool failed=false;try{saveSceneSnapshot(project.root,document.assetId(),snapshot);}catch(const std::exception&){failed=true;}
    require(failed&&read(file)=="corrupt","A corrupt materialized identity was silently overwritten with migration data.");
}
void componentAndQuaderOwnership() {
    Project project;SceneDocument document(project.root);const auto group=document.createEmpty("Effect owner");require(addSceneComponent(document,group,SceneComponentKind::Effects),document.status());
    const auto child=document.createEmpty("Child");document.select({child},child);require(document.reparentSelection(group),document.status());document.select({group},group);
    require(!document.ungroupSelection(),"An Effects-only owner was treated as an empty group.");
    const auto owner=document.createQuader();EntityRecord input;input.id=1;input.name="Imported entity";input.quaderGenerated=QuaderGenerated{owner,"entity"};
    require(document.replaceQuaderImport(owner,QuaderLink{},{input}),document.status());
    std::vector<EntityId> ids;for(const auto& record:document.scene().records())if(record.quaderGenerated)ids.push_back(record.id);
    for(const auto entity:ids){auto record=*document.scene().find(entity);record.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{EnemyStartingEffectId}}};require(document.updateEntity(entity,record),document.status());}
    input.local=math::translation({2,0,1});require(document.replaceQuaderImport(owner,QuaderLink{},{input}),document.status());
    for(const auto entity:ids)require(document.scene().find(entity)->effects==SceneEffects{{AssetRef<GameplayEffectAsset>{EnemyStartingEffectId}}},"Quader refresh discarded effects from a generated member or Imported container.");
}
}
int main(){try{recipesAndMembership();deterministicReadAndCurrentSubsets();membershipConversionDefaultsAndExistingEffects();membershipConversionPreservesUnknownEffectModifiers();historyPlayAndPersistence();independentlyMaterializedWins();componentAndQuaderOwnership();
    std::cout<<"Effect scene checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
