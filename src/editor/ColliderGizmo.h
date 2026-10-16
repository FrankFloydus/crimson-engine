#pragma once
#include "scene/SceneDocument.h"
#include <optional>
#include <vector>

namespace crimson {
struct ColliderGizmoHandle { unsigned id{},axis{};Vec3 point{};const char* name{}; };
std::vector<ColliderGizmoHandle> colliderGizmoHandles(const SceneCollider&);
std::optional<float> colliderHandleCoordinate(const Ray& worldRay,const Mat4& world,unsigned axis);
struct ColliderGizmoEdit { SceneCollider collider;Mat4 local; };
// Coordinates use the original collider frame throughout a gesture. Ordinary
// face drags hold the opposite face fixed; Alt resizes around the original center.
std::optional<ColliderGizmoEdit> resizedColliderHandle(const SceneCollider&,const Mat4& local,unsigned handle,float coordinate,bool symmetric);

class ColliderGizmoDrag final {
public:
    bool begin(SceneDocument&,EntityId,unsigned handle,const Ray&);
    bool update(SceneDocument&,const Ray&,bool symmetric);
    bool finish(SceneDocument&);
    void cancel(SceneDocument&);
    bool valid(const SceneDocument&) const;
    bool active() const { return owner_!=nullptr; }
    EntityId entity() const { return entity_; }
    unsigned handle() const { return handle_; }
private:
    SceneDocument* owner_{};
    std::uint64_t generation_{};
    EntityId entity_{};
    unsigned handle_{},axis_{};
    float grabOffset_{};
    EntityRecord initial_;
    Mat4 world_,parentWorld_;
    std::vector<EntityId> selection_;
    void reset();
};
} // namespace crimson
