#pragma once
#include "scene/Scene.h"
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace crimson {
class CharacterMotor;
struct PhysicsRayHit { EntityId entity{}; Vec3 position{},normal{}; };
struct PhysicsBox { EntityId entity{}; Mat4 world{}; Vec3 size{}; };
// Collision and controlled actors are CPU-only. Backend handles and shapes never
// leave this owner; authored entities retain their persistent Crimson identity.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&)=delete;
    PhysicsWorld& operator=(const PhysicsWorld&)=delete;
    void reconcile(const Scene&,std::span<const EntityId> transformLocks = {});
    std::vector<ScenePoseUpdate> step(const Scene&,EntityId controlled,const Vec3& desiredVelocity,
                                    float seconds,std::span<const EntityId> transformLocks = {});
    const std::string& status() const;
    std::size_t colliderCount() const;
    std::size_t characterCount() const;
    // Only accepted static collision geometry is published. Actor poses and
    // unrelated scene edits do not invalidate derived navigation geometry.
    std::vector<PhysicsBox> staticBoxes() const;
    std::uint64_t geometryRevision() const;
    std::optional<Vec3> characterPosition(EntityId) const;
    bool grounded(EntityId) const;
    // Queries the current reconciled static collision world. Direction is
    // normalized internally; maxDistance and the returned position use meters.
    std::optional<PhysicsRayHit> raycast(const Ray&,float maxDistance) const;
private:
    friend class CharacterMotor;
    void requestMovement(EntityId,const Vec3& desiredVelocity);
    void requestFacing(EntityId,const Vec3& direction);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
