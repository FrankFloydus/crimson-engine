#pragma once
#include "navigation/NavigationMesh.h"
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace crimson::test {
inline NavigationVolumeDescriptor navigationTestVolume(){NavigationVolumeDescriptor volume;volume.entity=999;volume.size={24,16,24};return volume;}
// Synchronous test driver for the same detached tile builder used by jobs.
// Coverage is always explicit; production has no whole-world fallback.
class NavigationMeshFixture:public NavigationMesh {
public:
    explicit NavigationMeshFixture(NavigationVolumeDescriptor volume):volume_(std::move(volume)){}
    bool reconcile(const PhysicsWorld& physics,const AgentTraversalSettings& agent) {
        if(source_==physics.geometryRevision()&&agent_==agent)return false;
        source_=physics.geometryRevision();agent_=agent;++revision_;diagnostic_.clear();
        for(const auto key:keys_)remove(key);keys_.clear();configure(volume_.bake,agent);
        try {
            if(!validAgentTraversal(agent))throw std::runtime_error("Invalid agent dimensions.");
            const auto bounds=navigationVolumeBounds(volume_);const float side=volume_.bake.tileSize*volume_.bake.cellSize;
            for(int z=static_cast<int>(std::floor(bounds.minimum[2]/side));z<std::ceil(bounds.maximum[2]/side);++z)
                for(int x=static_cast<int>(std::floor(bounds.minimum[0]/side));x<std::ceil(bounds.maximum[0]/side);++x) {
                    NavigationTileInput input;input.tile={x,z};input.volume=volume_;input.agent=agent;input.boxes=physics.staticBoxes();input.meshes=physics.staticMeshes();
                    input.minimumY=std::floor((bounds.minimum[1]-.05f)/volume_.bake.cellHeight)*volume_.bake.cellHeight;
                    input.maximumY=std::ceil((bounds.maximum[1]+agent.height+.1f)/volume_.bake.cellHeight)*volume_.bake.cellHeight;
                    auto tile=buildNavigationTile(input);if(!publish(tile,revision_))throw std::runtime_error(NavigationMesh::status());keys_.insert(input.tile);
                }
        }catch(const std::exception& error){diagnostic_=error.what();for(const auto key:keys_)remove(key);keys_.clear();}
        finishPublication();return true;
    }
    std::uint64_t revision() const{return revision_;}
    const std::string& status() const{return diagnostic_.empty()?NavigationMesh::status():diagnostic_;}
private:
    NavigationVolumeDescriptor volume_;AgentTraversalSettings agent_;
    std::uint64_t source_{std::numeric_limits<std::uint64_t>::max()},revision_{};
    std::set<NavigationTileKey> keys_;std::string diagnostic_;
};
}
