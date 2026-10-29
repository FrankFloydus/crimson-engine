#pragma once
#include "physics/StaticCollision.h"
#include "resources/ResourceManager.h"
#include <unordered_map>

namespace crimson {
class AssetLibrary;
class CollisionResources {
public:
    CollisionResources(AssetLibrary& assets,ResourceManager& resources):assets_(assets),resources_(resources){}
    CollisionMeshResult query(const std::string& id);
    void collect();
    void clear(){entries_.clear();}
private:
    struct Entry {ResourceHandle<StaticTriangleMesh> resource;CollisionMeshResult accepted;std::uint64_t sourceRevision{},repairKey{};
        std::chrono::steady_clock::time_point used;};
    AssetLibrary& assets_;ResourceManager& resources_;
    std::unordered_map<std::string,Entry> entries_;
};
}
