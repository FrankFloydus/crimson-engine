#include "editor/AttributeDebug.h"
#include "runtime/PlaySession.h"
#include "scene/SceneComponents.h"
#include "assets/GameplayEffectAsset.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Scratch {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-attribute-debug-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch(){std::filesystem::create_directories(root);}
    ~Scratch(){std::error_code error;const auto absolute=std::filesystem::absolute(root).lexically_normal();if(std::filesystem::equivalent(absolute.parent_path(),std::filesystem::temp_directory_path(),error)&&!error)std::filesystem::remove_all(absolute,error);}
};
void configureEffects(PlaySession& play,const std::filesystem::path& root) {
    play.setGameplayEffects([root](const std::string& id){return EffectDefinitionSnapshot{std::make_shared<const GameplayEffectAsset>(loadGameplayEffectAsset(root,id)),1,{}};});
}
EntityRecord actor(SceneDocument& document,EntityId id,const AttributeSeeds& initial={}) {
    EntityRecord result;result.id=id;result.name="Actor "+std::to_string(id);result.actor.emplace();
    if(!initial.empty()) {
        GameplayEffectAsset effect;effect.id=newAssetId();effect.name=result.name+" Initial Values";
        for(const auto& seed:initial)if(seed.tag!=stats::Health&&seed.tag!=stats::Mana)
            effect.modifiers.push_back({seed.tag,EffectModifierOperation::Override,{seed.startingBase,{},{}}});
        if(!effect.modifiers.empty()){saveGameplayEffectAsset(document.projectRoot(),effect);result.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{effect.id}}};}
    }
    return result;
}
void fixture(SceneDocument& document) {
    auto first=actor(document,10,{{stats::MaxMana,25}}),second=actor(document,20,{{stats::MovementSpeed,4}});
    auto player=actor(document,40,{{stats::MaxHealth,100},{stats::Vitality,10},{stats::MovementSpeed,5}});
    player.controller.emplace();player.movement.emplace();player.playerInput.emplace();
    auto empty=actor(document,30),unresolved=actor(document,35,{{GameplayTag{"Stats.Future"},0}});
    auto arbitrary=actor(document,5,{{stats::MaxHealth,100}});arbitrary.actor.reset();
    auto visual=actor(document,6,{{stats::MovementSpeed,8}});visual.actor.reset();visual.parent=10;
    require(document.edit("Debug fixture",[&](auto& records,auto& properties){records={player,empty,arbitrary,second,unresolved,visual,first};properties={};properties.playerCharacter=40;}),"Cannot author attribute debug fixture.");
    document.select({6},6);
}
double rowValue(const std::vector<AttributeDebugRow>& rows,const char* name) {
    const auto found=std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.name==name;});
    require(found!=rows.end(),"The debug overlay omitted an expected attribute row.");return found->current;
}
void reconcile(PlaySession& play){play.advance(0,Vec2{},Camera{});}
void eligibilityCyclingAndReadOnlyValues() {
    Scratch scratch;SceneDocument authored(scratch.root);fixture(authored);PlaySession play(scratch.root);AttributeDebug debug;
    debug.toggle(play);require(!debug.enabled()&&debug.target()==NoEntity&&debug.rows(play).empty(),"Debug enabled without Play.");
    configureEffects(play,scratch.root);require(play.start(authored),"Cannot start debug fixture Play.");debug.toggle(play);
    require(debug.enabled()&&debug.target()==40,"Debug did not choose the eligible designated player initially.");
    const auto rows=debug.rows(play);require(rows.size()==5&&rowValue(rows,"Health")==120&&rowValue(rows,"MaxHealth")==120&&rowValue(rows,"MovementSpeed")==5&&rowValue(rows,"Vitality")==10&&rowValue(rows,"HealthRegen")==.5,"Debug did not show effect values and C++ native formula outputs.");
    require(play.addAttributeBase(40,stats::Health,-17),"Cannot change the live debug target's Health.");
    require(debug.rows(play)[0].current==103,"Debug cached authored or stale values instead of current runtime values.");
    debug.cycle(play,false);require(debug.target()==10&&debug.rows(play).size()==2&&debug.rows(play)[0].name=="Mana"&&debug.rows(play)[0].current==25,"Forward wrap did not select the first eligible Actor subset.");
    debug.cycle(play,false);require(debug.target()==20&&debug.rows(play).size()==1&&debug.rows(play)[0].name=="MovementSpeed","Cycling included a non-Actor, empty Actor, unresolved-only Actor, or attached visual.");
    debug.cycle(play,false);require(debug.target()==30&&debug.rows(play).empty(),"Effect-free Actors could not be selected for effect application.");
    debug.cycle(play,false);require(debug.target()==35&&debug.rows(play).empty(),"An Actor with failed starting data disappeared from debug targeting.");
    debug.cycle(play,false);require(debug.target()==40,"Forward cycling did not use persistent EntityId order.");
    debug.cycle(play,true);require(debug.target()==35,"Backward cycling skipped the preceding Actor.");
    debug.cycle(play,true);require(debug.target()==30,"Backward cycling omitted an effect-free Actor.");
    debug.cycle(play,true);require(debug.target()==20,"Backward cycling omitted the preceding initialized Actor.");
    debug.cycle(play,true);require(debug.target()==10,"Backward cycling did not reach the first Actor.");
    debug.cycle(play,true);require(debug.target()==40,"Backward cycling did not wrap.");
    require(play.document().selection().primary==6&&authored.selection().primary==6,"Debug targeting changed editor selection.");
    require(authored.scene().find(40)->effects->starting.size()==1,"Debug inspection changed initialization effects.");
    debug.toggle(play);require(!debug.enabled()&&debug.target()==NoEntity&&debug.rows(play).empty(),"Disabling debug retained its target or rows.");
    debug.cycle(play,false);require(!debug.enabled()&&debug.target()==NoEntity,"Cycling enabled a disabled debug controller.");
}
void targetRemovalAndEmptySets() {
    Scratch scratch;SceneDocument authored(scratch.root);fixture(authored);
    require(authored.edit("Remove helper Actors",[](auto& records,auto&){std::erase_if(records,[](const auto& record){return record.id==30||record.id==35;});}),"Cannot trim removal fixture.");
    PlaySession play(scratch.root);configureEffects(play,scratch.root);require(play.start(authored),"Cannot start removal fixture.");AttributeDebug debug;debug.toggle(play);debug.cycle(play,false);debug.cycle(play,false);require(debug.target()==20,"Removal fixture did not select the middle Actor.");
    require(play.document().edit("Remove middle Actor",[](auto& records,auto&){std::erase_if(records,[](const auto& record){return record.id==20;});}),"Cannot remove debug target.");reconcile(play);debug.update(play);require(debug.target()==40,"Removed target did not select its deterministic successor.");
    require(play.document().edit("Remove last Actor",[](auto& records,auto& properties){std::erase_if(records,[](const auto& record){return record.id==40;});properties.playerCharacter=NoEntity;}),"Cannot remove last debug target.");reconcile(play);debug.update(play);require(debug.target()==10,"Removed final target did not wrap to the first eligible Actor.");
    require(play.document().edit("Remove final initialized Actor",[](auto& records,auto&){std::erase_if(records,[](const auto& record){return record.id==10||record.parent==10;});}),"Cannot remove final initialized actor.");reconcile(play);debug.update(play);require(debug.enabled()&&debug.target()==NoEntity&&debug.rows(play).empty(),"Empty eligible set retained a stale target or disabled the requested overlay.");
    debug.cycle(play,false);debug.cycle(play,true);require(debug.target()==NoEntity,"Cycling an empty set selected an invalid entity.");
    const auto spawned=actor(authored,15,{{stats::MaxMana,0}});
    require(play.document().edit("Spawn effect Actor",[&](auto& records,auto&){records.push_back(spawned);}),"Cannot spawn eligible Actor.");reconcile(play);debug.update(play);require(debug.target()==15,"An enabled empty controller did not acquire a newly eligible Actor.");
    debug.cycle(play,false);debug.cycle(play,true);require(debug.target()==15,"Single Actor cycling did not wrap to itself.");
    require(play.document().edit("Remove Actor capability",[](auto& records,auto&){for(auto& record:records)if(record.id==15)record.actor.reset();}),"Cannot remove Actor capability.");reconcile(play);debug.update(play);require(debug.target()==NoEntity,"An entity without Actor capability remained eligible through its attributes.");
}
void lifecycleAndRetention() {
    Scratch scratch;SceneDocument authored(scratch.root);fixture(authored);PlaySession play(scratch.root);configureEffects(play,scratch.root);require(play.start(authored),"Cannot start lifecycle fixture.");AttributeDebug debug;debug.toggle(play);debug.cycle(play,false);require(debug.target()==10,"Lifecycle fixture target is incorrect.");
    require(play.document().edit("Rename and clear player designation",[](auto& records,auto& properties){for(auto& record:records)if(record.id==10)record.name="Renamed";properties.playerCharacter=NoEntity;}),"Cannot edit lifecycle fixture.");reconcile(play);debug.update(play);require(debug.target()==10,"Unrelated edits replaced a retained eligible target.");
    play.stop();debug.update(play);require(!debug.enabled()&&debug.target()==NoEntity&&debug.rows(play).empty(),"Observed Stop did not reset debug state.");
    require(play.start(authored),"Cannot restart lifecycle fixture.");debug.update(play);require(!debug.enabled()&&debug.target()==NoEntity,"Restart unexpectedly enabled the debug controller.");debug.toggle(play);require(debug.target()==40,"Restart did not select the new world's designated player.");
    debug.reset();require(!debug.enabled()&&debug.target()==NoEntity,"Explicit Play transition reset did not clear debug state.");
    debug.toggle(play);PlaySession other(scratch.root);configureEffects(other,scratch.root);require(other.start(authored),"Cannot create second Play owner.");debug.update(other);require(!debug.enabled()&&debug.target()==NoEntity,"Switching Play owners retained the old debug target.");
    require(other.document().edit("No designated player",[](auto&,auto& properties){properties.playerCharacter=NoEntity;}),"Cannot clear second player designation.");reconcile(other);debug.toggle(other);require(debug.target()==10,"No-player initialization did not choose the first eligible Actor.");
}
void selectTarget(AttributeDebug& debug,const PlaySession& play,EntityId target) {
    for(unsigned attempt=0;attempt<8 && debug.target()!=target;++attempt)debug.cycle(play,false);
    require(debug.target()==target,"The debug overlay could not select an authored role Actor.");
}
void roleRecipesSurviveSaveAndShowCompleteSubsets() {
    Scratch scratch;SceneDocument authored(scratch.root);
    const auto player=authored.createPlayer("Player"),first=authored.createEnemy("Enemy 37"),second=authored.createEnemy("Enemy 80");
    require(player&&first&&second,"Cannot create role recipe debug actors.");
    auto firstEffect=*enemyStartingEffect();firstEffect.id=newAssetId();firstEffect.name="Enemy 37";
    auto secondEffect=firstEffect;secondEffect.id=newAssetId();secondEffect.name="Enemy 80";
    for(auto& modifier:firstEffect.modifiers)if(modifier.attribute==stats::MaxHealth)modifier.magnitude.literal=37;
    for(auto& modifier:secondEffect.modifiers)if(modifier.attribute==stats::MaxHealth)modifier.magnitude.literal=80;
    saveGameplayEffectAsset(scratch.root,firstEffect);saveGameplayEffectAsset(scratch.root,secondEffect);
    require(authored.edit("Tune independent enemies",[&](auto& records,auto&){for(auto& record:records) {
        if(record.id==first){record.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{firstEffect.id}}};record.local=math::translation({-3,0,0});}
        if(record.id==second){record.effects=SceneEffects{{AssetRef<GameplayEffectAsset>{secondEffect.id}}};record.local=math::translation({3,0,0});}
    }}),"Cannot tune enemy debug capacities.");
    require(authored.save("Role debug rows"),"Cannot save role recipe debug actors.");
    SceneDocument reopened(scratch.root);require(reopened.load(authored.assetId()),"Cannot reopen saved role recipe debug actors.");
    PlaySession play(scratch.root);configureEffects(play,scratch.root);require(play.start(reopened),"Cannot start reopened role recipe debug actors.");
    AttributeDebug debug;debug.toggle(play);require(debug.target()==player,"Debug did not default to the reopened Player.");
    auto rows=debug.rows(play);require(rows.size()==9 && rowValue(rows,"Vitality")==50 && rowValue(rows,"Spirit")==50 &&
        rowValue(rows,"Health")==100 && rowValue(rows,"MaxHealth")==100 && rowValue(rows,"Mana")==100 && rowValue(rows,"MaxMana")==100,
        "The saved Player did not expose its primary stats and full derived pools to ShowDebug.");
    require(play.addAttributeBase(player,stats::Health,-9)&&play.addAttributeBase(player,stats::Mana,-20),"Cannot deplete Player debug pools.");
    require(play.setAttributeBase(player,stats::Vitality,60)&&play.setAttributeBase(player,stats::Spirit,70),"Cannot change live Player primary stats through the runtime API.");reconcile(play);debug.update(play);rows=debug.rows(play);
    require(rowValue(rows,"MaxHealth")==120 && rowValue(rows,"MaxMana")==140 && rowValue(rows,"Health")==91 && rowValue(rows,"Mana")==80,
        "Primary stat edits failed to update displayed capacities or refilled live Player resources.");
    selectTarget(debug,play,first);rows=debug.rows(play);
    require(rows.size()==3 && rowValue(rows,"Health")==37 && rowValue(rows,"MaxHealth")==37 && rowValue(rows,"MovementSpeed")==5,
        "An Enemy debug subset inherited Player primaries/Mana or ignored its per-instance capacity.");
    selectTarget(debug,play,second);rows=debug.rows(play);
    require(rows.size()==3 && rowValue(rows,"Health")==80 && rowValue(rows,"MaxHealth")==80,
        "Independent saved Enemy capacities shared a runtime value.");
}
void fixedPoolEffectsIntroduceStats() {
    Scratch scratch;SceneDocument authored(scratch.root);
    const auto zero=actor(authored,10,{{stats::Vitality,0}});
    require(authored.edit("Zero initializer",[&](auto& records,auto&){records={zero};}),"Cannot author zero effect.");
    PlaySession play(scratch.root);configureEffects(play,scratch.root);require(play.start(authored),"Cannot start zero initializer.");AttributeDebug debug;debug.toggle(play);
    auto rows=debug.rows(play);require(debug.target()==10&&rows.size()==4&&rowValue(rows,"Vitality")==0&&rowValue(rows,"Health")==0,"Explicit zero effect/native formula outputs were hidden.");
    require(play.attributeValue(10,stats::Spirit)==AttributeValue{0,0}&&std::none_of(rows.begin(),rows.end(),[](const auto& row){return row.name=="Spirit";}),"Fixed-pool defaults must be available without filling the overlay with untouched stats.");
    auto spirit=GameplayEffectAsset{};spirit.id=newAssetId();spirit.name="Spirit 25";spirit.modifiers={{stats::Spirit,EffectModifierOperation::AddBase,{25}}};saveGameplayEffectAsset(scratch.root,spirit);
    const auto enemy=authored.createEnemy("Enemy");
    play.stop();require(play.start(authored),"Cannot start enemy initializer.");debug.reset();debug.toggle(play);selectTarget(debug,play,enemy);
    rows=debug.rows(play);require(std::none_of(rows.begin(),rows.end(),[](const auto& row){return row.name=="Spirit";}),"Enemy started with an unrelated visible Spirit stat.");
    require(bool(play.applyEffect(enemy,enemy,spirit.id)),"An enemy could not gain Spirit from an effect without membership authoring.");
    rows=debug.rows(play);require(rowValue(rows,"Spirit")==25&&rowValue(rows,"MaxMana")==50&&rowValue(rows,"ManaRegen")==2.5,"The enemy's introduced Spirit/native C++ outputs did not appear in the debug overlay.");
    require(play.document().edit("Unrelated rename",[&](auto& records,auto&){for(auto& record:records)if(record.id==enemy)record.name="Renamed";}),"Cannot rename enemy.");reconcile(play);
    require(play.attributeValue(enemy,stats::Spirit)->current==25,"Reconciliation removed an effect-introduced attribute.");
}

}
int main(){try{eligibilityCyclingAndReadOnlyValues();targetRemovalAndEmptySets();lifecycleAndRetention();roleRecipesSurviveSaveAndShowCompleteSubsets();fixedPoolEffectsIntroduceStats();std::cout<<"Transient attribute debug targeting, cycling, live values and lifecycle checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
