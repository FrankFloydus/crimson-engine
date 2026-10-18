#include "SceneOutlinerRows.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace crimson {
bool isSceneGroup(const EntityRecord& record) {
    return !record.model&&!record.particle&&!record.directional&&!record.point&&!record.environment&&
        !record.actor&&!record.movement&&!record.controller&&!record.navigation&&!record.playerInput&&
        !record.collider&&!record.camera&&!record.cameraFollow&&
        !record.quader&&!record.quaderGenerated;
}
namespace {
struct Projection {
    std::vector<SceneOutlinerRow> rows;
    std::vector<EntityId> order;
    std::unordered_map<EntityId,EntityId> owners;
    std::unordered_map<EntityId,EntityId> inspectorOwners;
};
Projection project(const Scene& scene) {
    const auto records=scene.records();
    std::unordered_map<EntityId,std::vector<const EntityRecord*>> children;
    children.reserve(records.size());
    for(const auto& record:records)children[record.parent].push_back(&record);
    for(auto& [parent,siblings]:children)std::sort(siblings.begin(),siblings.end(),[](const auto* a,const auto* b) {
        return a->order!=b->order?a->order<b->order:a->id<b->id;
    });
    struct Work {const EntityRecord* record;EntityId parent{},actor{};};
    std::vector<Work> pending;
    if(const auto roots=children.find(NoEntity);roots!=children.end())
        for(auto it=roots->second.rbegin();it!=roots->second.rend();++it)pending.push_back({*it});
    Projection result;result.rows.reserve(records.size());result.order.reserve(records.size());result.owners.reserve(records.size());
    result.inspectorOwners.reserve(records.size());
    while(!pending.empty()) {
        const auto work=pending.back();pending.pop_back();const auto& record=*work.record;
        const bool group=isSceneGroup(record);
        const auto actor=record.actor?record.id:group?NoEntity:work.actor;
        // Visual models and emitters are authored entities, even when they are
        // attached to a character. Only the recipe's following camera is an
        // inline authoring part rather than an Outliner child.
        const auto owner=actor&&!record.model&&!record.particle&&record.camera&&record.cameraFollow&&record.cameraFollow->target==record.parent&&
            record.parent==actor?actor:record.id;
        const auto inspectorOwner=!record.actor&&!group&&actor&&(record.model||record.particle||record.camera)?actor:record.id;
        result.order.push_back(record.id);result.owners.emplace(record.id,owner);
        result.inspectorOwners.emplace(record.id,inspectorOwner);
        if(owner==record.id)result.rows.push_back({record.id,work.parent});
        if(const auto descendants=children.find(record.id);descendants!=children.end())
            for(auto it=descendants->second.rbegin();it!=descendants->second.rend();++it)
                pending.push_back({*it,owner==record.id?record.id:work.parent,actor});
    }
    return result;
}
}
std::vector<SceneOutlinerRow> sceneOutlinerRows(const Scene& scene) {return project(scene).rows;}
EntityId sceneEntityOwner(const Scene& scene,EntityId id) {
    const auto record=scene.find(id);if(!record)return NoEntity;
    if(!record->actor&&!record->model&&!record->particle&&record->camera&&record->cameraFollow&&record->cameraFollow->target==record->parent)
        if(const auto parent=scene.find(record->parent);parent&&parent->actor)return parent->id;
    return id;
}
std::vector<EntityId> sceneEntityParts(const Scene& scene,EntityId id) {
    const auto projection=project(scene);
    const auto found=projection.owners.find(id);if(found==projection.owners.end())return {};
    const auto owner=found->second;std::vector<EntityId> result{owner};
    for(const auto part:projection.order)if(part!=owner&&projection.inspectorOwners.at(part)==owner)result.push_back(part);
    return result;
}
std::optional<Vec4> sceneOutlinerColor(const EntityRecord& record) {
    return record.outlinerStyle?record.outlinerStyle->color:std::nullopt;
}
bool setSceneOutlinerColors(SceneDocument& document,std::span<const SceneOutlinerColorTarget> requested,std::optional<Vec4> color) {
    if(requested.empty()||(color&&(!math::finite(*color)||std::any_of(color->begin(),color->end(),[](float value){return value<0||value>1;}))))return false;
    std::unordered_set<EntityId> targets;bool changed=false;
    for(const auto& target:requested) {
        const auto record=document.scene().find(target.entity);if(!record)return false;
        targets.insert(target.entity);changed=changed||sceneOutlinerColor(*record)!=color;
    }
    if(!changed)return false;
    return document.edit(color?"Set Outliner color":"Clear Outliner color",[&](auto& records,auto&) {
        for(auto& record:records)if(targets.contains(record.id)) {
            if(color)record.outlinerStyle=SceneOutlinerStyle{color};else record.outlinerStyle.reset();
        }
    });
}
}
