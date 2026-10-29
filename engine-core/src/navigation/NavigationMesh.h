#pragma once
#include "NavigationTypes.h"
#include <memory>

namespace crimson {

// Recast/Detour data is derived from accepted static collision geometry. Agents
// with compatible capsule clearance share it; it never supplies collision itself.
class NavigationMesh {
public:
    NavigationMesh();
    ~NavigationMesh();
    NavigationMesh(const NavigationMesh&)=delete;
    NavigationMesh& operator=(const NavigationMesh&)=delete;
    bool configure(const NavigationBakeSettings&,const AgentTraversalSettings&);
    bool publish(std::shared_ptr<const NavigationTileData>,std::uint64_t version);
    bool remove(NavigationTileKey);
    void finishPublication();
    bool valid(const NavigationPath&) const;
    std::optional<Vec3> project(const Vec3&,const Vec3& extents) const;
    NavigationDebugSnapshot debugSnapshot() const;
    std::optional<NavigationPath> findPath(const Vec3& start,const Vec3& destination) const;
    // Preserve an exact reachable destination; otherwise resolve to the closest
    // surface in the start polygon's connected component. The final corner is
    // the accepted walkable point, which may differ from the requested hit.
    // Invalid starts, missing ground, or exhausted bounded queries yield no path.
    std::optional<NavigationPath> findReachablePath(const Vec3& start,const Vec3& destination) const;
    std::uint64_t revision() const;
    const std::string& status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
