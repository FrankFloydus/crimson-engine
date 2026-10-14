#pragma once
#include "physics/PhysicsWorld.h"
#include <memory>

namespace crimson {
struct NavigationPath { std::vector<Vec3> corners; };

// Recast/Detour data is derived from accepted static collision geometry. One
// cached mesh serves the controlled capsule; it never supplies collision itself.
class NavigationMesh {
public:
    static constexpr float CellSize=.1f;
    static constexpr float CellHeight=.05f;
    static constexpr float MaximumSlope=45.f;
    static constexpr float MaximumStep=.3f;
    NavigationMesh();
    ~NavigationMesh();
    NavigationMesh(const NavigationMesh&)=delete;
    NavigationMesh& operator=(const NavigationMesh&)=delete;
    // Returns true only when geometry or capsule clearance changed.
    bool reconcile(const PhysicsWorld&,const SceneCharacter&);
    std::optional<NavigationPath> findPath(const Vec3& start,const Vec3& destination) const;
    std::uint64_t revision() const;
    const std::string& status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
