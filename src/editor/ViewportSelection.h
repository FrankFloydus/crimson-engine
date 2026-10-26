#pragma once

#include "scene/SceneDocument.h"
#include <algorithm>
#include <utility>

namespace crimson {

// Geometry and markers address the actual entity. Parenting affects transforms,
// never picking; an organizational group is selected explicitly in the Outliner.
inline EntityId viewportSelectionEntity(const Scene& scene,EntityId hit) {
    return scene.contains(hit)?hit:NoEntity;
}

inline bool viewportEntityHasCenterMarker(const EntityRecord& entity) {
    return entity.directional || entity.point || entity.spot || entity.particle || entity.decal ||
        entity.camera;
}
inline bool viewportEntityHasMarker(const EntityRecord& entity) {
    return viewportEntityHasCenterMarker(entity) || entity.collider || entity.controller || entity.navigationVolume;
}

inline bool selectViewportHit(SceneDocument& document,EntityId hit,bool toggle,bool append) {
    if(document.transactionActive())return false;
    const auto id=viewportSelectionEntity(document.scene(),hit);
    auto ids=document.selection().ids;
    if(!id || (!toggle && !append))ids.clear();
    const auto found=std::find(ids.begin(),ids.end(),id);
    if(id) {
        if(toggle && found!=ids.end())ids.erase(found);
        else if(found==ids.end())ids.push_back(id);
    }
    document.select(std::move(ids),id);
    return true;
}

} // namespace crimson
