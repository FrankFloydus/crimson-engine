#pragma once

#include "editor/NavigationPreview.h"

namespace crimson::integration {
// Retains the existing Edit-only static bake owner and immutable debug geometry.
// A Play world is borrowed for visualization; it is never mutated or stepped.
class LevelNavigationPreview final {
public:
    LevelNavigationPreview(ResourceManager&,const std::filesystem::path& projectRoot);
    void update(const SceneDocument&,const CollisionMeshQuery&,const NavigationAgentProfileQuery&,
        const NavigationWorld* runtimeWorld=nullptr);
    std::string& profile(){return profile_;}
    const std::string& profile()const{return profile_;}
    std::string status(const std::string&)const;
    void setVisible(bool visible){visible_=visible;}
    bool visible()const{return visible_;}
    std::vector<RenderCollisionOverlay> overlays();
    const NavigationWorld& authoringWorld()const{return preview_.world();}
private:
    NavigationPreview preview_;
    const NavigationWorld* source_{};
    std::string profile_;
    std::uint64_t generation_{};
    EntityId volume_{};
    bool visible_{};
};
}
