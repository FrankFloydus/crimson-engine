#pragma once
#include "CollisionOverlay.h"
#include "physics/StaticCollision.h"
#include <unordered_map>

namespace crimson {
inline const CollisionDebugMesh& meshCollisionDebug(const std::shared_ptr<const StaticTriangleMesh>& source) {
    struct Entry {std::weak_ptr<const StaticTriangleMesh> source;CollisionDebugMesh debug;};
    static std::unordered_map<const StaticTriangleMesh*,Entry> entries;
    std::erase_if(entries,[](const auto& entry){return entry.second.source.expired();});
    auto& entry=entries[source.get()];
    if(entry.source.expired()) {
        entry.source=source;entry.debug.vertices=source->positions;
        for(std::size_t index=0;index+2<source->indices.size();index+=3)
            entry.debug.triangles.push_back({source->indices[index],source->indices[index+1],source->indices[index+2]});
        entry.debug.finishEdges();
    }
    return entry.debug;
}
}
