#include "SceneOutlinerRows.h"
#include <algorithm>
#include <unordered_map>

namespace crimson {
std::vector<SceneOutlinerRow> sceneOutlinerRows(const Scene& scene,EntityId owner) {
    const auto parent=scene.find(owner);if(!parent)return {};
    const auto childIds=scene.children(owner);
    std::vector<EntityRecord> children;children.reserve(childIds.size());
    for(const auto id:childIds)children.push_back(*scene.find(id));
    std::vector<const EntityRecord*> ordered;ordered.reserve(children.size());
    for(const auto& child:children)ordered.push_back(&child);
    return sceneOutlinerRows(*parent,ordered);
}
std::vector<SceneOutlinerRow> sceneOutlinerRows(const EntityRecord& owner,
                                             std::span<const EntityRecord* const> orderedChildren) {
    const auto components=sceneComponents(owner);
    std::unordered_map<SceneComponentKind,std::vector<EntityId>> anchored;
    std::vector<EntityId> trailing;
    for(const auto* child:orderedChildren) {
        if(!child||child->parent!=owner.id)continue;
        if(child->outlinerBefore&&*child->outlinerBefore!=SceneComponentKind::Transform&&
            std::find(components.begin(),components.end(),*child->outlinerBefore)!=components.end())
            anchored[*child->outlinerBefore].push_back(child->id);
        else trailing.push_back(child->id);
    }
    std::vector<SceneOutlinerRow> rows;rows.reserve(components.size()+orderedChildren.size());
    for(const auto component:components) {
        if(component==SceneComponentKind::Transform)continue;
        if(const auto found=anchored.find(component);found!=anchored.end())
            for(const auto id:found->second)rows.push_back(SceneOutlinerRow::childRow(id));
        rows.push_back(SceneOutlinerRow::componentRow(component));
    }
    for(const auto id:trailing)rows.push_back(SceneOutlinerRow::childRow(id));
    return rows;
}
bool reorderSceneOutlinerRow(SceneDocument& document,EntityId owner,SceneOutlinerRow moving,
                             std::optional<SceneOutlinerRow> before) {
    auto rows=sceneOutlinerRows(document.scene(),owner);const auto original=rows;
    const auto found=std::find(rows.begin(),rows.end(),moving);
    if(found==rows.end()||before==moving||(before&&std::find(rows.begin(),rows.end(),*before)==rows.end()))return false;
    rows.erase(found);
    rows.insert(before?std::find(rows.begin(),rows.end(),*before):rows.end(),moving);
    if(rows==original)return false;
    std::vector<SceneComponentKind> components{SceneComponentKind::Transform};
    struct ChildOrder {uint32_t order{};std::optional<SceneComponentKind> before;};
    std::unordered_map<EntityId,ChildOrder> children;std::optional<SceneComponentKind> next;
    for(auto row=rows.rbegin();row!=rows.rend();++row) {
        if(row->kind==SceneOutlinerRowKind::Component)next=row->component;
        else children.emplace(row->child,ChildOrder{0,next});
    }
    uint32_t order{};
    for(const auto& row:rows) {
        if(row.kind==SceneOutlinerRowKind::Component) {components.push_back(row.component);continue;}
        children.at(row.child).order=order++;
    }
    return document.edit("Reorder Outliner row",[&](auto& records,auto&) {
        for(auto& record:records) {
            if(record.id==owner)record.componentOrder=components;
            if(const auto child=children.find(record.id);child!=children.end()) {
                record.order=child->second.order;record.outlinerBefore=child->second.before;
            }
        }
    });
}
std::optional<Vec4> sceneOutlinerColor(const EntityRecord& record,std::optional<SceneComponentKind> component) {
    if(!record.outlinerStyle)return std::nullopt;
    if(!component)return record.outlinerStyle->color;
    const auto index=static_cast<size_t>(*component);
    return index<record.outlinerStyle->componentColors.size()?record.outlinerStyle->componentColors[index]:std::nullopt;
}
bool setSceneOutlinerColors(SceneDocument& document,std::span<const SceneOutlinerColorTarget> requested,std::optional<Vec4> color) {
    if(requested.empty()||(color&&(!math::finite(*color)||std::any_of(color->begin(),color->end(),[](float value){return value<0||value>1;}))))return false;
    std::unordered_map<EntityId,std::vector<std::optional<SceneComponentKind>>> targets;
    bool changed=false;
    for(const auto& target:requested) {
        const auto record=document.scene().find(target.entity);
        if(!record||(target.component&&!hasSceneComponent(*record,*target.component)))return false;
        auto& components=targets[target.entity];
        if(std::find(components.begin(),components.end(),target.component)==components.end())components.push_back(target.component);
        changed=changed||sceneOutlinerColor(*record,target.component)!=color;
    }
    if(!changed)return false;
    return document.edit(color?"Set Outliner color":"Clear Outliner color",[&](auto& records,auto&) {
        for(auto& record:records)if(const auto found=targets.find(record.id);found!=targets.end()) {
            if(color&&!record.outlinerStyle)record.outlinerStyle.emplace();
            if(record.outlinerStyle)for(const auto component:found->second) {
                if(component)record.outlinerStyle->componentColors[static_cast<size_t>(*component)]=color;
                else record.outlinerStyle->color=color;
            }
            normalizeSceneOutlinerStyle(record);
        }
    });
}
}
