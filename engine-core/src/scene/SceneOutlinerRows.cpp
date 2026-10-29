#include "SceneOutlinerRows.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace crimson {
bool isSceneGroup(const EntityRecord& record) {
    return !record.model&&!record.particle&&!record.decal&&!record.directional&&!record.point&&!record.spot&&!record.environment&&
        !record.actor&&!record.movement&&!record.controller&&!record.navigation&&!record.playerInput&&
        !record.collider&&!record.camera&&!record.cameraFollow;
}
namespace {
struct Projection {
    std::vector<SceneOutlinerRow> rows;
    std::vector<EntityId> order;
    std::unordered_map<EntityId,EntityId> owners;
    std::unordered_map<EntityId,EntityId> inspectorOwners;
};
Projection project(std::span<const EntityRecord> records) {
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
        const auto owner=actor&&!record.model&&!record.particle&&!record.decal&&record.camera&&record.cameraFollow&&record.cameraFollow->target==record.parent&&
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
std::string searchKey(std::string_view text) {
    const auto first=text.find_first_not_of(" \t\r\n");if(first==std::string_view::npos)return {};
    std::string key(text.substr(first,text.find_last_not_of(" \t\r\n")-first+1));
    for(auto& c:key)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    return key;
}
bool matches(const EntityRecord& record,SceneOutlinerFilter type) {
    switch(type) {
    case SceneOutlinerFilter::All:return true;
    case SceneOutlinerFilter::Models:return hasSceneComponent(record,SceneComponentKind::Model);
    case SceneOutlinerFilter::Lights:return record.point||record.spot||record.directional;
    case SceneOutlinerFilter::Cameras:return hasSceneComponent(record,SceneComponentKind::Camera);
    case SceneOutlinerFilter::Colliders:return hasSceneComponent(record,SceneComponentKind::Collider);
    case SceneOutlinerFilter::Particles:return hasSceneComponent(record,SceneComponentKind::Particle);
    case SceneOutlinerFilter::Actors:return hasSceneComponent(record,SceneComponentKind::Actor);
    case SceneOutlinerFilter::Decals:return hasSceneComponent(record,SceneComponentKind::Decal);
    case SceneOutlinerFilter::Environment:return hasSceneComponent(record,SceneComponentKind::Environment);
    case SceneOutlinerFilter::Navigation:return record.navigation||record.navigationVolume;
    }
    return false;
}
}
std::vector<SceneOutlinerRow> sceneOutlinerRows(const Scene& scene) {return project(scene.records()).rows;}
const char* sceneOutlinerFilterName(SceneOutlinerFilter value) {
    switch(value) {
    case SceneOutlinerFilter::All:return "All types";
    case SceneOutlinerFilter::Models:return "Models";
    case SceneOutlinerFilter::Lights:return "Lights";
    case SceneOutlinerFilter::Cameras:return "Cameras";
    case SceneOutlinerFilter::Colliders:return "Colliders";
    case SceneOutlinerFilter::Particles:return "Particles";
    case SceneOutlinerFilter::Actors:return "Actors";
    case SceneOutlinerFilter::Decals:return "Decals";
    case SceneOutlinerFilter::Environment:return "Environment";
    case SceneOutlinerFilter::Navigation:return "Navigation";
    }
    return "All types";
}
std::vector<EntityId> sceneOutlinerRange(std::span<const EntityId> displayed,EntityId anchor,EntityId target) {
    const auto first=std::find(displayed.begin(),displayed.end(),anchor),last=std::find(displayed.begin(),displayed.end(),target);
    if(first==displayed.end()||last==displayed.end())return {};
    return {std::min(first,last),std::max(first,last)+1};
}
std::span<const EntityId> SceneOutlinerProjection::children(EntityId parent) const {
    const auto found=children_.find(parent);return found==children_.end()?std::span<const EntityId>{}:std::span<const EntityId>{found->second};
}
void SceneOutlinerProjection::synchronize(const Scene& scene,std::string_view search,SceneOutlinerFilter filter,EntityId keepVisible) {
    const bool sourceChanged=owner_!=&scene||generation_!=scene.generation()||revision_!=scene.revision();
    if(sourceChanged) {
        records_=scene.records();auto projection=project(records_);
        baseRows_=std::move(projection.rows);owners_=std::move(projection.owners);baseParents_.clear();recordIndices_.clear();names_.clear();
        names_.reserve(records_.size());recordIndices_.reserve(records_.size());baseParents_.reserve(baseRows_.size());
        for(std::size_t index=0;index<records_.size();++index){recordIndices_.emplace(records_[index].id,index);names_.push_back(searchKey(records_[index].name));}
        for(const auto& row:baseRows_)baseParents_.emplace(row.entity,row.parent);
        owner_=&scene;generation_=scene.generation();revision_=scene.revision();++sourceBuilds_;
    }
    const auto key=searchKey(search);
    if(!sourceChanged&&search_==key&&filter_==filter&&keepVisible_==keepVisible)return;
    search_=key;filter_=filter;keepVisible_=keepVisible;rows_.clear();parents_.clear();children_.clear();
    std::unordered_set<EntityId> retained;
    const auto include=[&](EntityId id) {
        const auto owner=owners_.find(id);if(owner==owners_.end())return;
        for(id=owner->second;id&&retained.insert(id).second;) {
            const auto parent=baseParents_.find(id);if(parent==baseParents_.end())break;id=parent->second;
        }
    };
    if(filtered()) {
        std::unordered_set<EntityId> named;
        if(!search_.empty())for(std::size_t index=0;index<records_.size();++index)
            if(names_[index].find(search_)!=std::string::npos)named.insert(owners_.at(records_[index].id));
        for(std::size_t index=0;index<records_.size();++index)
            if(matches(records_[index],filter_)&&(search_.empty()||named.contains(owners_.at(records_[index].id))))include(records_[index].id);
        include(keepVisible_);
    }
    for(const auto& row:baseRows_)if(!filtered()||retained.contains(row.entity)) {
        rows_.push_back(row);parents_.emplace(row.entity,row.parent);children_[row.parent].push_back(row.entity);
    }
    ++filterBuilds_;
}
EntityId sceneEntityOwner(const Scene& scene,EntityId id) {
    const auto record=scene.find(id);if(!record)return NoEntity;
    if(!record->actor&&!record->model&&!record->particle&&record->camera&&record->cameraFollow&&record->cameraFollow->target==record->parent)
        if(const auto parent=scene.find(record->parent);parent&&parent->actor)return parent->id;
    return id;
}
std::vector<EntityId> sceneEntityParts(const Scene& scene,EntityId id) {
    const auto projection=project(scene.records());
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
