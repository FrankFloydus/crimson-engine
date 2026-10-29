#include "AttributeDebug.h"
#include "runtime/PlaySession.h"
#include <algorithm>
#include <iterator>

namespace crimson {
namespace {
std::vector<EntityId> eligibleActors(const PlaySession& play) {
    std::vector<EntityId> result;
    if(!play.playing())return result;
    for(const auto& record:play.document().scene().records())if(record.actor)result.push_back(record.id);
    // Scene::records already uses persistent ID order. Keep that contract
    // explicit here so cycling never depends on Outliner or hash-map order.
    std::sort(result.begin(),result.end());return result;
}
EntityId initialTarget(const PlaySession& play,const std::vector<EntityId>& eligible) {
    const auto player=play.document().scene().properties().playerCharacter;
    if(std::binary_search(eligible.begin(),eligible.end(),player))return player;
    return eligible.empty()?NoEntity:eligible.front();
}
}
void AttributeDebug::reset() {play_=nullptr;generation_=0;target_=NoEntity;enabled_=false;}
void AttributeDebug::toggle(const PlaySession& play) {
    if(enabled_||!play.playing()){reset();return;}
    play_=&play;generation_=play.document().documentGeneration();enabled_=true;
    target_=initialTarget(play,eligibleActors(play));
}
void AttributeDebug::update(const PlaySession& play) {
    if(!play.playing()){reset();return;}
    if(!enabled_)return;
    if(play_!=&play||generation_!=play.document().documentGeneration()){reset();return;}
    const auto eligible=eligibleActors(play);
    if(std::binary_search(eligible.begin(),eligible.end(),target_))return;
    if(target_==NoEntity){target_=initialTarget(play,eligible);return;}
    const auto next=std::upper_bound(eligible.begin(),eligible.end(),target_);
    target_=eligible.empty()?NoEntity:next==eligible.end()?eligible.front():*next;
}
void AttributeDebug::cycle(const PlaySession& play,bool backwards) {
    update(play);if(!enabled_||target_==NoEntity)return;
    const auto eligible=eligibleActors(play);
    const auto current=std::lower_bound(eligible.begin(),eligible.end(),target_);
    if(current==eligible.end())return;
    if(backwards)target_=current==eligible.begin()?eligible.back():*std::prev(current);
    else target_=std::next(current)==eligible.end()?eligible.front():*std::next(current);
}
std::vector<AttributeDebugRow> AttributeDebug::rows(const PlaySession& play) const {
    std::vector<AttributeDebugRow> result;
    if(!enabled_||target_==NoEntity||play_!=&play||!play.playing()||generation_!=play.document().documentGeneration())return result;
    const auto record=play.document().scene().find(target_);if(!record||!record->actor)return result;
    const auto snapshots=play.attributeSnapshots(target_);
    for(const auto& definition:nativeAttributeCatalog().definitions())if(const auto value=std::find_if(snapshots.begin(),snapshots.end(),[&](const auto& entry){return entry.tag==definition.tag;});value!=snapshots.end()) {
        const auto& name=definition.tag.name();const auto separator=name.find_last_of('.');
        result.push_back({name.substr(separator==std::string::npos?0:separator+1),value->value.current});
    }
    return result;
}
}
