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
struct RagdollAsset;
struct ModelAsset;
struct ModelPose;
struct RagdollBodyHit {
    EntityId entity{};
    std::size_t bodyIndex{};
    Vec3 position{},normal{};
    float distance{};
};
struct PhysicsRayHit { EntityId entity{}; Vec3 position{},normal{}; };
struct PhysicsBox { EntityId entity{}; Mat4 world{}; Vec3 size{}; };
struct PhysicsQueryFilter {
    std::uint32_t mask{AllCollisionLayers};
    std::span<const EntityId> ignored;
    bool includeWorld{true},includeCharacters{true},includeTriggers{false};
};
enum class PhysicsTraceShape : std::uint8_t { Line,Sphere,Box,Capsule };
enum class PhysicsTraceMode : std::uint8_t { Any,Closest,All };
struct PhysicsTrace {
    PhysicsTraceShape shape{PhysicsTraceShape::Line};
    Ray ray{};
    float maxDistance{10.f};
    float radius{.25f};
    Vec3 halfExtents{.5f,.5f,.5f};
    // Cylinder half-height; the complete capsule height is 2*(halfHeight+radius).
    float capsuleHalfHeight{.5f};
    Quat orientation{}; // Fixed world orientation; casts translate without rotating.
};
struct PhysicsTraceHit {
    EntityId entity{},owner{}; // Exact collider and nearest actor (or entity itself).
    Vec3 position{},normal{},center{}; // Target contact and query center at impact.
    float distance{},fraction{}; // Center travel in meters and normalized [0,1].
    bool startedInside{}; // Initial overlap/contact; normal may be a fallback.
    std::uint32_t subshape{}; // Opaque, transient primitive identifier, never saved.
};
struct PhysicsTraceResult {
    bool valid{}; // False for malformed/unsupported requests; a valid miss has no hits.
    std::vector<PhysicsTraceHit> hits;
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
    // Runtime bodies and constraints stay in this world's collision/query owner.
    // The model transform must have a positive uniform scale without shear.
    bool createRagdoll(EntityId,std::shared_ptr<const RagdollAsset>,std::shared_ptr<const ModelAsset>,
        const ModelPose&,const Mat4& modelWorld,const Vec3& velocity={},const Vec3& impulse={},
        EntityId actorOwner=NoEntity,CollisionFilter collision={EnemyCollisionLayer,AllCollisionLayers});
    void removeRagdoll(EntityId);
    bool hasRagdoll(EntityId) const;
    std::size_t ragdollCount() const;
    bool ragdollActive(EntityId) const;
    std::shared_ptr<const ModelPose> sampleRagdollPose(EntityId,float alpha=1) const;
    // World rigid source-bone frames, parallel to the setup's body array.
    std::vector<Mat4> ragdollBodyTransforms(EntityId,float alpha=1) const;
    bool applyRagdollImpulse(EntityId,const Vec3&,std::optional<Vec3> worldPoint={},int bodyIndex=-1);
    std::optional<RagdollBodyHit> pickRagdoll(const Ray&,float maxDistance) const;
    bool beginRagdollDrag(EntityId,std::size_t bodyIndex,const Vec3& worldPoint);
    bool updateRagdollDrag(const Vec3& worldTarget);
    void endRagdollDrag();
    // Only accepted static collision geometry is published. Actor poses and
    // unrelated scene edits do not invalidate derived navigation geometry.
    std::vector<PhysicsBox> staticBoxes() const;
    std::vector<PhysicsStaticMesh> staticMeshes() const;
    std::uint64_t geometryRevision() const;
    std::optional<Vec3> characterPosition(EntityId) const;
    // Accepted motion in meters per second, after collision resolution.
    std::optional<Vec3> characterVelocity(EntityId) const;
    // Complete vertical motion over discontinuous support in the latest tick,
    // including capsule edge sweep/settling and stair/floor helpers. Continuous
    // planar slope travel, jumps, unsupported falling and landing do not contribute.
    float characterStepHeight(EntityId) const;
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
    // Queries the accepted collision world, independent of movement response.
    // Mask selects target authored layers; ignored IDs also exclude their actor
    // descendants. All returns the nearest entry/contact per collider, ordered
    // by distance, entity and subshape; it does not stop at the first collider.
    // Triangle backfaces collide. Convex initial overlaps return distance zero.
    // Any may return any accepted hit and avoids unnecessary query work.
    PhysicsTraceResult trace(const PhysicsTrace&,const PhysicsQueryFilter& = {},PhysicsTraceMode = PhysicsTraceMode::Closest) const;
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
