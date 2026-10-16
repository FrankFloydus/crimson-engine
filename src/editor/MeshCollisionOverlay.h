#pragma once
#include "CollisionWire.h"
#include "physics/StaticCollision.h"

namespace crimson {
// Detached CPU preparation; workers use this directly without touching caches.
inline CollisionDebugMesh buildMeshCollisionDebug(const StaticTriangleMesh& source,std::stop_token stop={}) {
    if(stop.stop_requested())throw std::runtime_error("Collision overlay preparation canceled.");
    CollisionDebugMesh result;result.vertices=source.positions;result.triangles.reserve(source.indices.size()/3);
    for(std::size_t index=0;index+2<source.indices.size();index+=3) {
        if(index%3072==0&&stop.stop_requested())throw std::runtime_error("Collision overlay preparation canceled.");
        result.triangles.push_back({source.indices[index],source.indices[index+1],source.indices[index+2]});
    }
    result.finishEdges(stop);return result;
}
}
