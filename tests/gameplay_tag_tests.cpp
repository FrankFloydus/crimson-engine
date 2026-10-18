#include "gameplay/GameplayTags.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Action> void rejects(Action action,const char* message){try{action();}catch(const std::invalid_argument&){return;}throw std::runtime_error(message);}
void nativeRegistryAndIdentity() {
    const GameplayTagRegistry registry({GameplayTag{"State.Combat.Stunned"},GameplayTag{"State.Combat.Dead"},GameplayTag{"state.combat.stunned"},GameplayTag{"Stats.Health"}});
    const auto stunned=registry.request("STATE.COMBAT.STUNNED");require(stunned&&stunned->name()=="State.Combat.Stunned","Native registry did not retain canonical spelling with case-insensitive lookup.");
    require(registry.contains(GameplayTag{"state"})&&registry.request("State.Combat").has_value(),"Native registration did not establish its implicit parents.");
    require(!registry.request("State.Unknown")&&!registry.request("State..Combat"),"Native registry fabricated an unregistered or malformed tag.");
    require(GameplayTag{"Stats.Health"}==GameplayTag{"stats.health"},"Gameplay tag identity depends on case.");
    rejects([]{GameplayTag invalid{"State..Dead"};},"Empty tag segment was accepted.");rejects([]{GameplayTag invalid{" State.Dead"};},"Tag whitespace was silently normalized.");
    require(!GameplayTag{}.matches(GameplayTag{})&&!GameplayTag{}.matchesExact(GameplayTag{})&&!GameplayTag{}.valid(),"Invalid tags matched one another.");
    require(nativeGameplayTags().contains(stats::MovementSpeed)&&nativeGameplayTags().contains(GameplayTag{"Stats"}),"Built-in native tags were not deterministically registered.");
}
void directionExactParentsAndDepth() {
    const GameplayTag child{"State.Combat.Stunned"},parent{"State.Combat"},sibling{"State.Combat.Dead"};
    require(child.matches(parent)&&!parent.matches(child)&&!child.matches(sibling),"Hierarchical tag matching lost its direction.");
    require(child.matchesAny(GameplayTagContainer({parent}))&&!parent.matchesAny(GameplayTagContainer({child}))&&!child.matchesAny(GameplayTagContainer({parent}),true),"Tag-to-container matching lost its direction or exactness.");
    require(!GameplayTag{"State.Combatant"}.matches(parent)&&!GameplayTag{"States.Combat"}.matches(parent),"Tag matching ignored segment boundaries.");
    require(child.parent()==parent&&GameplayTag{"State"}.parent()==GameplayTag{},"Direct parent traversal is incorrect.");
    require(child.commonDepth(child)==3&&child.commonDepth(parent)==2&&child.commonDepth(sibling)==2&&child.commonDepth(stats::Health)==0,"Shared tag depth is not the common segment prefix.");
    GameplayTagContainer tags({child,stats::Health});require(tags.has(parent)&&!tags.has(parent,true)&&tags.has(child,true),"Container explicit versus implicit matching is incorrect.");
    const auto parents=tags.parents();require(parents.has(child,true)&&parents.has(parent,true)&&parents.has(GameplayTag{"State"},true),"Parent expansion did not retain explicit values and all ancestors.");
    require(tags.tags().size()==2,"Reading parents mutated the explicit authored container.");
    require(tags.filter(GameplayTagContainer({parent}))==GameplayTagContainer({child}),"Directional filter did not retain the matching child.");
    require(GameplayTagContainer({parent}).filter(tags).empty()&&tags.filter(GameplayTagContainer({parent}),true).empty(),"Directional or exact filter promoted an implicit parent.");
}
void explicitContainerOperations() {
    GameplayTagContainer first({stats::Health,stats::Mana}),second({stats::Mana,GameplayTag{"stats.health"}});require(first==second,"Explicit container equality depends on insertion order or case.");
    require(!first.add(stats::Health)&&first.remove(stats::Mana)&&!first.remove(stats::Mana),"Container mutation did not deduplicate explicit tags.");
    first.append(second);first.append(first);require(first==second,"Append or self-append duplicated tags.");
    const GameplayTagContainer empty;require(!first.hasAny(empty)&&first.hasAll(empty)&&empty.hasAll(empty)&&!empty.hasAny(first),"Empty any/all container semantics differ from UE.");
    GameplayTagContainer leaves({GameplayTag{"State"},GameplayTag{"Stats"}});const GameplayTag child{"State.Combat.Stunned"};
    require(leaves.addLeaf(child)&&leaves.has(child,true)&&!leaves.has(GameplayTag{"State"},true),"Adding a leaf did not remove its explicit ancestor.");
    require(leaves.addLeaf(child)&&!leaves.addLeaf(GameplayTag{"State.Combat"}),"Leaf insertion did not preserve UE duplicate/ancestor results.");
    require(leaves.has(GameplayTag{"Stats"},true),"Leaf insertion removed an unrelated explicit tag.");
}
void nestedQueriesAndEmptyOperators() {
    const GameplayTag parent{"State.Combat"},child{"State.Combat.Stunned"};const GameplayTagContainer tags({child,stats::Health});
    const auto leaf=[&](GameplayTagQueryKind kind,GameplayTag tag){return GameplayTagQuery{kind,GameplayTagContainer({tag}),{}};};
    require(!GameplayTagQuery{}.matches(tags),"Default unbuilt query matched a container.");
    require(leaf(GameplayTagQueryKind::AnyTags,parent).matches(tags)&&leaf(GameplayTagQueryKind::AllTags,parent).matches(tags),"Hierarchical any/all tag query failed.");
    require(!leaf(GameplayTagQueryKind::NoTags,parent).matches(tags)&&leaf(GameplayTagQueryKind::NoTags,stats::Mana).matches(tags),"No-tags query failed.");
    require(!leaf(GameplayTagQueryKind::AnyTagsExact,parent).matches(tags)&&leaf(GameplayTagQueryKind::AllTagsExact,child).matches(tags),"Exact query evaluated implicit parents as explicit tags.");
    const GameplayTagQuery nested{GameplayTagQueryKind::AllExpressions,{}, {leaf(GameplayTagQueryKind::AnyTags,parent),GameplayTagQuery{GameplayTagQueryKind::NoExpressions,{}, {leaf(GameplayTagQueryKind::AnyTags,stats::Mana)}}}};
    require(nested.matches(tags)&&!nested.matches(GameplayTagContainer({stats::Mana})),"Nested all/no expressions did not compose.");
    const GameplayTagQuery any{GameplayTagQueryKind::AnyExpressions,{}, {leaf(GameplayTagQueryKind::AnyTags,stats::Mana),nested}};require(any.matches(tags)&&any==any,"Nested any expression or structural equality failed.");
    require(!(nested==GameplayTagQuery{}),"Structural query equality ignored its expression tree.");
    for(const auto kind:{GameplayTagQueryKind::AnyTags,GameplayTagQueryKind::AnyTagsExact,GameplayTagQueryKind::AnyExpressions})require(!GameplayTagQuery{kind,{},{}}.matches(tags),"Empty Any query returned true.");
    for(const auto kind:{GameplayTagQueryKind::AllTags,GameplayTagQueryKind::AllTagsExact,GameplayTagQueryKind::NoTags,GameplayTagQueryKind::AllExpressions,GameplayTagQueryKind::NoExpressions})require(GameplayTagQuery{kind,{},{}}.matches(tags),"Empty All/No query returned false.");
}
}
int main(){try{nativeRegistryAndIdentity();directionExactParentsAndDepth();explicitContainerOperations();nestedQueriesAndEmptyOperators();std::cout<<"Native gameplay tag identity, hierarchy, containers and eight query forms passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
