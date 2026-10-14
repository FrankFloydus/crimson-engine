#pragma once

#include "scene/SceneDocument.h"
#include <algorithm>
#include <utility>

namespace crimson {

// Viewport geometry and markers select the assembled object. Explicit
// Outliner selection still addresses any individual scene entity.
inline EntityId viewportSelectionRoot(const Scene& scene,EntityId hit) {
    while(hit) {
        const auto entity=scene.find(hit);
        if(!entity)return NoEntity;
        if(!entity->parent)return hit;
        hit=entity->parent;
    }
    return NoEntity;
}

inline bool selectViewportHit(SceneDocument& document,EntityId hit,bool toggle,bool append) {
    if(document.transactionActive())return false;
    const auto id=viewportSelectionRoot(document.scene(),hit);
    auto ids=document.selection().ids;
    if(!toggle && !append)ids.clear();
    const auto found=std::find(ids.begin(),ids.end(),id);
    if(id) {
        if(toggle && found!=ids.end())ids.erase(found);
        else if(found==ids.end())ids.push_back(id);
    }
    document.select(std::move(ids),id);
    return true;
}

} // namespace crimson
