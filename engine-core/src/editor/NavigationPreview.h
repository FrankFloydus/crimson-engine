#pragma once
#include "navigation/NavigationWorld.h"
#include "scene/SceneDocument.h"
#include "crimson-renderer/CollisionOverlayTypes.h"
#include <chrono>

namespace crimson {
// Edit's static world never creates character motors or advances simulation.
// Publication and queries remain on the main thread; tile workers are shared.
class NavigationPreview {
public:
    explicit NavigationPreview(ResourceManager&,const std::filesystem::path& root);
    void update(const SceneDocument&,const CollisionMeshQuery&,const NavigationAgentProfileQuery&);
    std::string status(const NavigationWorld*,const std::string& profile) const;
    std::vector<RenderCollisionOverlay> overlays(const NavigationWorld*,const std::string& profile);
    const NavigationWorld& world() const { return world_; }
private:
    PhysicsWorld physics_;
    NavigationWorld world_;
    std::chrono::steady_clock::time_point lastUpdate_{};
    std::uint64_t domain_{},revision_{};
    bool transaction_{},coalescing_{},collisionPending_{};
    std::string diagnostic_,overlayProfile_;
    std::uint64_t overlayRevision_{};
    const NavigationWorld* overlayWorld_{};
    std::shared_ptr<const CollisionOverlayGeometry> geometry_,pendingGeometry_;
};
}
