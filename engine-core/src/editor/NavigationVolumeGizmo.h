#pragma once
#include "ColliderGizmo.h"

namespace crimson {
// Navigation bounds use the same box face geometry and coordinate solver as
// Collider. Their gesture writes a navigation capability, never a collider.
class NavigationVolumeGizmoDrag final {
public:
    bool begin(SceneDocument&,EntityId,unsigned handle,const Ray&);
    bool update(SceneDocument&,const Ray&,bool symmetric);
    bool finish(SceneDocument&);
    void cancel(SceneDocument&);
    bool valid(const SceneDocument&) const;
    bool active() const { return owner_!=nullptr; }
    unsigned handle() const { return handle_; }
private:
    SceneDocument* owner_{};
    std::uint64_t generation_{},transaction_{};
    EntityId entity_{};
    unsigned handle_{},axis_{};
    float grabOffset_{};
    EntityRecord initial_;
    Mat4 world_,parentWorld_;
    std::vector<EntityId> selection_;
    void reset();
};
} // namespace crimson
