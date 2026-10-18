#pragma once
#include "scene/Scene.h"
#include "StaticCollision.h"
#include "assets/NavigationAgentProfile.h"
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace crimson {
class CharacterMotor;
struct PhysicsRayHit { EntityId entity{}; Vec3 position{},normal{}; };
struct PhysicsBox { EntityId entity{}; Mat4 world{}; Vec3 size{}; };
struct PhysicsQueryFilter {
    std::uint32_t mask{AllCollisionLayers};
    std::span<const EntityId> ignored;
    bool includeWorld{true},includeCharacters{true},includeTriggers{false};
};
enum class TriggerEventKind : std::uint8_t { Enter,Exit };
enum class TriggerEventReason : std::uint8_t { Motion,Initial,Configuration,Removed };
struct TriggerEvent {
    std::uint64_t world{},tick{};
    EntityId trigger{},other{};
    TriggerEventKind kind{};
    TriggerEventReason reason{};
    bool operator==(const TriggerEvent&) const = default;
};
// Collision and controlled actors are CPU-only. Backend handles and shapes never
// leave this owner; authored entities retain their persistent Crimson identity.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&)=delete;
    PhysicsWorld& operator=(const PhysicsWorld&)=delete;
    void setAgentProfiles(NavigationAgentProfileQuery query);
    void reconcile(const Scene&,std::span<const EntityId> transformLocks = {},std::span<const PhysicsMeshCollider> meshes = {});
    // Navigation preview accepts the same static shapes without creating motors.
    void reconcileStatics(const Scene&,std::span<const PhysicsMeshCollider> meshes = {});
    std::vector<ScenePoseUpdate> step(const Scene&,EntityId controlled,const Vec3& desiredVelocity,
                                    float seconds,std::span<const EntityId> transformLocks = {},std::span<const PhysicsMeshCollider> meshes = {});
    const std::string& status() const;
    std::size_t colliderCount() const;
    std::size_t characterCount() const;
    // Only accepted static collision geometry is published. Actor poses and
    // unrelated scene edits do not invalidate derived navigation geometry.
    std::vector<PhysicsBox> staticBoxes() const;
    std::vector<PhysicsStaticMesh> staticMeshes() const;
    std::uint64_t geometryRevision() const;
    std::optional<Vec3> characterPosition(EntityId) const;
    // Accepted motion in meters per second, after collision resolution.
    std::optional<Vec3> characterVelocity(EntityId) const;
    std::optional<AgentTraversalSettings> acceptedCharacterTraversal(EntityId) const;
    bool grounded(EntityId) const;
    bool characterLocked(EntityId) const;
    // Drop queued input and jump grace without interrupting airborne motion.
    void cancelCharacterInput(EntityId);
    // Endpoint overlaps are sampled after all motors resolve each fixed step.
    // Events describe the latest completed step and carry persistent identities.
    std::span<const TriggerEvent> triggerEvents() const;
    std::vector<EntityId> triggerOverlaps(EntityId trigger) const;
    // Queries the current reconciled static collision world. Direction is
    // normalized internally; maxDistance and the returned position use meters.
    std::optional<PhysicsRayHit> raycast(const Ray&,float maxDistance) const;
    std::optional<PhysicsRayHit> raycast(const Ray&,float maxDistance,const PhysicsQueryFilter&) const;
private:
    friend class CharacterMotor;
    void requestMovement(EntityId,const Vec3& desiredVelocity);
    void requestFirstPersonMovement(EntityId,const Vec3& desiredHorizontalVelocity,float groundAcceleration,float groundFriction,float airAcceleration,float airControl);
    void requestJump(EntityId,float jumpHeight);
    void requestFacing(EntityId,const Vec3& direction);
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void reconcileInternal(const Scene&,std::span<const EntityId>,std::span<const PhysicsMeshCollider>,bool characters);
};
}
