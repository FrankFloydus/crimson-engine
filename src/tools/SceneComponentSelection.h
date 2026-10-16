#pragma once

#include "scene/SceneComponents.h"
#include "scene/SceneOutlinerRows.h"
#include <algorithm>

namespace crimson {
// Inspector target is a component on an existing entity. It never becomes a
// synthetic scene entity and does not depend on which panel owns keyboard focus.
class SceneComponentSelection {
public:
    void bind(const SceneDocument& document) { sync(document); }
    void sync(const SceneDocument& document) {
        if (document_!=&document || generation_!=document.documentGeneration()) {
            document_=&document;generation_=document.documentGeneration();clear();
        }
        if (targets_.empty()) return;
        const auto& selected=document.selection();
        std::vector<EntityId> owners;
        for(const auto& target:targets_) {
            const auto record=document.scene().find(target.entity);
            if(!record || !target.component || !hasSceneComponent(*record,*target.component)){clear();return;}
            if(std::find(owners.begin(),owners.end(),target.entity)==owners.end())owners.push_back(target.entity);
        }
        if(selected.primary!=entity_ || selected.ids.size()!=owners.size() ||
            std::any_of(owners.begin(),owners.end(),[&](const auto owner){return std::find(selected.ids.begin(),selected.ids.end(),owner)==selected.ids.end();}))clear();
    }
    bool select(SceneDocument& document,EntityId entity,SceneComponentKind component,bool toggle=false) {
        sync(document);
        const auto record=document.scene().find(entity);
        if (document.transactionActive() || !record || !hasSceneComponent(*record,component)) return false;
        const SceneOutlinerColorTarget target{entity,component};
        if(!toggle)targets_.clear();
        const auto found=std::find(targets_.begin(),targets_.end(),target);
        if(toggle && found!=targets_.end()) {
            targets_.erase(found);
            if(targets_.empty()){clear();document.select({});return true;}
            if(entity_==entity && component_==component){entity_=targets_.back().entity;component_=targets_.back().component;}
        } else {targets_.push_back(target);entity_=entity;component_=component;}
        publish(document);return true;
    }
    bool focus(SceneDocument& document,EntityId entity,SceneComponentKind component) {
        sync(document);
        if(!selected(entity,component))return select(document,entity,component);
        if(document.transactionActive())return false;
        entity_=entity;component_=component;publish(document);return true;
    }
    bool focusEntity(SceneDocument& document,EntityId entity) {
        sync(document);
        if(document.transactionActive() || !document.scene().contains(entity))return false;
        const bool componentMode=!targets_.empty();clear();
        const auto& selected=document.selection();
        if(componentMode || std::find(selected.ids.begin(),selected.ids.end(),entity)==selected.ids.end())document.select({entity},entity);
        else document.select(selected.ids,entity);
        return true;
    }
    bool selected(EntityId entity,SceneComponentKind component)const {
        return std::find(targets_.begin(),targets_.end(),SceneOutlinerColorTarget{entity,component})!=targets_.end();
    }
    bool erase(SceneDocument& document) {
        sync(document);
        if (targets_.empty() || std::any_of(targets_.begin(),targets_.end(),[](const auto& target){return !target.component || !editableSceneComponent(*target.component);})) return false;
        const bool removed=removeSceneComponents(document,targets_);
        if (removed) clear();
        return removed;
    }
    void clear() { entity_=NoEntity;component_.reset();targets_.clear(); }
    EntityId entity() const { return entity_; }
    std::optional<SceneComponentKind> component() const { return component_; }
    std::span<const SceneOutlinerColorTarget> targets()const{return targets_;}
    bool allows(SceneComponentKind component) const {
        return component_ ? *component_==component : component==SceneComponentKind::Transform;
    }
private:
    void publish(SceneDocument& document) {
        std::vector<EntityId> owners;
        for(const auto& target:targets_)if(std::find(owners.begin(),owners.end(),target.entity)==owners.end())owners.push_back(target.entity);
        document.select(std::move(owners),entity_);
    }
    const SceneDocument* document_{};
    std::uint64_t generation_{};
    EntityId entity_{};
    std::optional<SceneComponentKind> component_;
    std::vector<SceneOutlinerColorTarget> targets_;
};
}
