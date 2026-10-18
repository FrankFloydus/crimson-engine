#include "PhysicsWorld.h"
#include "ColliderGeometry.h"
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <map>
#include <limits>
#include <mutex>
#include <numbers>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace crimson {
namespace {
constexpr JPH::ObjectLayer StaticLayer=0,CharacterLayer=1;
class Layers final:public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override{return 2;}
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override{return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer));}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override{return layer==JPH::BroadPhaseLayer(0)?"Static":"Character";}
#endif
};
class Pairs final:public JPH::ObjectLayerPairFilter {
public:bool ShouldCollide(JPH::ObjectLayer a,JPH::ObjectLayer b) const override{return a==CharacterLayer || b==CharacterLayer;}
};
class BroadPairs final:public JPH::ObjectVsBroadPhaseLayerFilter {
public:bool ShouldCollide(JPH::ObjectLayer a,JPH::BroadPhaseLayer b) const override{return a==CharacterLayer || b==JPH::BroadPhaseLayer(1);}
};
struct JoltLifetime {
    JoltLifetime(){JPH::RegisterDefaultAllocator();JPH::Factory::sInstance=new JPH::Factory;JPH::RegisterTypes();}
    ~JoltLifetime(){JPH::UnregisterTypes();delete JPH::Factory::sInstance;JPH::Factory::sInstance=nullptr;}
};
std::shared_ptr<JoltLifetime> acquireJolt() {
    static std::mutex mutex;static std::weak_ptr<JoltLifetime> shared;
    std::lock_guard lock(mutex);auto result=shared.lock();
    if(!result){result=std::make_shared<JoltLifetime>();shared=result;}return result;
}
JPH::Vec3 native(const Vec3& v){return {v[0],v[1],v[2]};}
JPH::RVec3 position(const Mat4& v){return {v[12],v[13],v[14]};}
Vec3 own(JPH::RVec3Arg v){return {v.GetX(),v.GetY(),v.GetZ()};}
JPH::Quat rotation(const Mat4& m) {
    return JPH::Mat44(JPH::Vec4(m[0],m[1],m[2],0),JPH::Vec4(m[4],m[5],m[6],0),
        JPH::Vec4(m[8],m[9],m[10],0),JPH::Vec4(0,0,0,1)).GetQuaternion().Normalized();
}
bool close(const Mat4& a,const Mat4& b) {
    for(std::size_t i=0;i<16;++i)if(std::abs(a[i]-b[i])>std::max(1e-6f,std::numeric_limits<float>::epsilon()*2*std::max(std::abs(a[i]),std::abs(b[i]))))return false;
    return true;
}
bool underCharacter(const Scene& scene,EntityId id) {
    auto entity=scene.find(id);
    while(entity && entity->parent){entity=scene.find(entity->parent);if(entity && entity->controller)return true;}
    return false;
}
EntityId characterOwner(const Scene& scene,EntityId id) {
    auto entity=scene.find(id);
    while(entity){if(entity->controller)return entity->id;entity=entity->parent?scene.find(entity->parent):std::nullopt;}
    return NoEntity;
}
CollisionFilter characterFilter(CollisionFilter value){value.mask|=WorldCollisionLayer;return value;}
CollisionFilter solidFilter(CollisionFilter value){value.layers|=WorldCollisionLayer;value.mask=AllCollisionLayers;return value;}
bool locked(const Scene& scene,EntityId id,std::span<const EntityId> locks) {
    const auto character=id;
    while(id) {
        if(std::find(locks.begin(),locks.end(),id)!=locks.end())return true;
        const auto entity=scene.find(id);if(!entity)break;id=entity->parent;
    }
    // Captured world-space gizmo and Inspector baselines depend on both parent
    // transforms and camera-follow targets. Hold every moving actor on that
    // dependency path, including ancestors of a followed visual or camera.
    std::vector<EntityId> pending(locks.begin(),locks.end());
    std::unordered_set<EntityId> visited;
    while(!pending.empty()) {
        const auto dependency=pending.back();pending.pop_back();
        if(!dependency || !visited.insert(dependency).second)continue;
        if(dependency==character)return true;
        const auto entity=scene.find(dependency);if(!entity)continue;
        if(entity->parent)pending.push_back(entity->parent);
        if(entity->cameraFollow && entity->cameraFollow->target)pending.push_back(entity->cameraFollow->target);
    }
    return false;
}
bool sameCollider(const SceneCollider& a,const SceneCollider& b) {
    if(a.shape!=b.shape)return false;
    if(a.shape==ColliderShape::Box)return a.size==b.size;
    return a.radius==b.radius && (a.shape==ColliderShape::Sphere || a.height==b.height);
}
bool validCharacter(const Mat4& world,const AgentTraversalSettings& character,std::string& error) {
    const auto fields=math::affineTransformFields(world);
    if(!fields || fields->singular || fields->sheared){error="Character requires a nonsingular transform without shear.";return false;}
    for(int i=0;i<3;++i)if(std::abs(fields->scale[i]-1.f)>2e-5f){error="Character requires unit world scale; resize its capsule or scale a visual child.";return false;}
    if(std::abs(world[4])>2e-5f || std::abs(world[5]-1.f)>2e-5f || std::abs(world[6])>2e-5f){error="Character must remain upright; rotate visual children for alignment.";return false;}
    if(!math::finite(character.radius) || !math::finite(character.height) || character.radius<=0 || character.height<2*character.radius){error="Character height must be at least twice its positive radius.";return false;}
    return true;
}
float nonnegative(float value){return std::isfinite(value)?std::max(0.f,value):0.f;}
Vec3 approachVelocity(const Vec3& current,const Vec3& target,float maximumChange) {
    const auto delta=target-current;const auto length=math::length(delta);
    return length<=maximumChange?target:current+delta*(maximumChange/length);
}
float updateCharacter(JPH::CharacterVirtual& motor,float seconds,JPH::PhysicsSystem& world,
    const JPH::CharacterVirtual::ExtendedUpdateSettings& settings,JPH::TempAllocator& allocator) {
    const auto desired=motor.GetLinearVelocity();
    const auto previous=motor.GetPosition();const auto up=motor.GetUp();
    const bool supported=motor.IsSupported();
    const auto groundPoint=motor.GetGroundPosition();auto groundNormal=motor.GetGroundNormal();
    if(supported && desired.Dot(up)<=0) {
        // Jolt may replace triangle surface normals with upward contact normals
        // and average them. Use the original collider plane for continuity so
        // those changing solver normals do not turn a smooth ramp into stairs.
        JPH::BodyLockRead ground(world.GetBodyLockInterface(),motor.GetGroundBodyID());
        if(ground.Succeeded()) {
            auto normal=ground.GetBody().GetWorldSpaceSurfaceNormal(motor.GetGroundSubShapeID(),groundPoint);
            if(normal.Dot(groundNormal)<0)normal=-normal;
            groundNormal=normal;
        }
    }
    motor.ExtendedUpdate(seconds,world.GetGravity(),settings,
        world.GetDefaultBroadPhaseLayerFilter(CharacterLayer),world.GetDefaultLayerFilter(CharacterLayer),{},{},allocator);
    // The rounded capsule can rise before WalkStairs and settle afterwards in
    // the ordinary sweep. Publish the complete supported vertical motion so
    // that those smaller changes do not bypass camera smoothing. Unsupported
    // falling, the landing tick and an upward jump remain immediate.
    if(!supported || !motor.IsSupported() || desired.Dot(up)>0)return 0;
    const auto displacement=JPH::Vec3(motor.GetPosition()-previous);
    // A smooth ramp can need StickToFloor every tick. Its foot motion remains
    // tangent to the same support plane. Testing contacts alone is insufficient:
    // a capsule rounding a tread edge can keep the same flat contact plane while
    // its distance above that plane changes. A vertical riser is never a ramp.
    const bool continuous=groundNormal.Dot(up)>1e-3f &&
        std::abs(JPH::Vec3(motor.GetGroundPosition()-groundPoint).Dot(groundNormal))<1e-3f &&
        std::abs(displacement.Dot(groundNormal))<1e-3f;
    const float correction=continuous?0:displacement.GetY();
    return std::isfinite(correction)?correction:0;
}
}
struct PhysicsWorld::Impl final: JPH::CharacterContactListener,JPH::CharacterVsCharacterCollision {
    // Lifetime is declared first so every Jolt resource is released before it.
    std::shared_ptr<JoltLifetime> lifetime{acquireJolt()};
    Layers layers;Pairs pairs;BroadPairs broadPairs;
    JPH::PhysicsSystem world;
    JPH::TempAllocatorImpl allocator{4*1024*1024};
    struct Collider {JPH::BodyID body;Mat4 world;SceneCollider definition;std::shared_ptr<const StaticTriangleMesh> mesh;};
    struct FirstPersonMotion {Vec3 velocity{};float coyote{},jumpBuffer{},jumpHeight{};bool active{};};
    struct FirstPersonRequest {Vec3 velocity{};float groundAcceleration{},groundFriction{},airAcceleration{},airControl{};};
    struct Character {
        JPH::Ref<JPH::CharacterVirtual> motor;AgentTraversalSettings definition;std::string profile;Mat4 world;CollisionFilter collision;
        bool locked{},movementEnabled{},profileCurrent{true},allowSliding{true};
        FirstPersonMotion firstPerson;Vec3 resolvedVelocity{};float stepHeight{};
    };
    std::map<EntityId,Collider> colliders;
    struct MeshCollider {JPH::BodyID body;Mat4 world;std::shared_ptr<const StaticTriangleMesh> mesh;std::uint64_t revision{};CollisionFilter collision{WorldCollisionLayer,AllCollisionLayers};};
    std::map<EntityId,MeshCollider> meshColliders;
    std::map<EntityId,Character> characters;
    struct Trigger {JPH::RefConst<JPH::Shape> shape;Mat4 world;SceneCollider definition;EntityId owner{};};
    std::map<EntityId,Trigger> triggers;
    std::map<EntityId,std::set<EntityId>> overlaps;
    std::vector<TriggerEvent> events;
    std::set<EntityId> initial,configured,removed;
    std::map<EntityId,Vec3> desiredVelocities;
    std::map<EntityId,FirstPersonRequest> firstPersonRequests;
    std::map<EntityId,float> jumpRequests;
    std::map<EntityId,Vec3> desiredFacing;
    std::string diagnostic;
    std::uint64_t geometryRevision{};
    inline static std::atomic_uint64_t nextWorld{1};
    const std::uint64_t identity=nextWorld.fetch_add(1);
    std::uint64_t ticks{};
    std::uint32_t nextCharacter{1};
    NavigationAgentProfileQuery profiles;
    Impl(){world.Init(4096,0,1024,1024,layers,broadPairs,pairs);world.SetGravity({0,-9.81f,0});}
    ~Impl(){characters.clear();for(const auto& [id,collider]:colliders){(void)id;world.GetBodyInterface().RemoveBody(collider.body);world.GetBodyInterface().DestroyBody(collider.body);}
        for(const auto& [id,collider]:meshColliders){(void)id;world.GetBodyInterface().RemoveBody(collider.body);world.GetBodyInterface().DestroyBody(collider.body);}}
    void report(EntityId id,const std::string& text){diagnostic+="Entity "+std::to_string(id)+": "+text+"\n";}
    bool peerAllowed(const JPH::CharacterVirtual* mover,const Character& target) const {
        const auto found=characters.find(mover->GetUserData());
        return found!=characters.end()&&found->second.motor.GetPtr()==mover&&target.motor.GetPtr()!=mover&&
            (found->second.collision.mask&target.collision.layers)!=0;
    }
    bool OnCharacterContactValidate(const JPH::CharacterVirtual* mover,const JPH::CharacterVirtual* target,const JPH::SubShapeID&) override {
        const auto found=characters.find(target->GetUserData());return found!=characters.end()&&found->second.motor.GetPtr()==target&&peerAllowed(mover,found->second);
    }
    bool peerContactNeedsRecovery(const JPH::CharacterVirtual* mover,const JPH::CharacterVirtual* other) const {
        const auto first=characters.find(mover->GetUserData()),second=characters.find(other->GetUserData());
        if(first==characters.end()||second==characters.end())return false;
        // Jolt's "can push" flag also controls penetration recovery. Permit
        // that recovery after a resize/teleport overlap, while normal contacts
        // keep peers' requested velocities from bulldozing stationary actors.
        const auto a=mover->GetPosition(),b=other->GetPosition();
        const auto& one=first->second.definition;const auto& two=second->second.definition;
        const double lowA=a.GetY()+mover->GetCharacterPadding()+one.radius,highA=a.GetY()+mover->GetCharacterPadding()+one.height-one.radius;
        const double lowB=b.GetY()+other->GetCharacterPadding()+two.radius,highB=b.GetY()+other->GetCharacterPadding()+two.height-two.radius;
        const double vertical=std::max({0.0,lowA-highB,lowB-highA});
        const double x=a.GetX()-b.GetX(),z=a.GetZ()-b.GetZ();
        const double radius=one.radius+two.radius+mover->GetCharacterPadding()+other->GetCharacterPadding();
        return std::sqrt(x*x+z*z+vertical*vertical)<radius-.003;
    }
    void peerContactSettings(const JPH::CharacterVirtual* mover,const JPH::CharacterVirtual* other,JPH::CharacterContactSettings& settings) const {
        settings.mCanPushCharacter=peerContactNeedsRecovery(mover,other);
    }
    void OnCharacterContactAdded(const JPH::CharacterVirtual* mover,const JPH::CharacterVirtual* other,const JPH::SubShapeID&,JPH::RVec3Arg,JPH::Vec3Arg,JPH::CharacterContactSettings& settings) override {peerContactSettings(mover,other,settings);}
    void OnCharacterContactPersisted(const JPH::CharacterVirtual* mover,const JPH::CharacterVirtual* other,const JPH::SubShapeID&,JPH::RVec3Arg,JPH::Vec3Arg,JPH::CharacterContactSettings& settings) override {peerContactSettings(mover,other,settings);}
    void OnCharacterContactSolve(const JPH::CharacterVirtual* mover,const JPH::CharacterVirtual* other,const JPH::SubShapeID&,
        JPH::RVec3Arg,JPH::Vec3Arg normal,JPH::Vec3Arg,const JPH::PhysicsMaterial*,
        JPH::Vec3Arg incomingVelocity,JPH::Vec3& resolvedVelocity) override {
        // Jolt selects the constraint using peer/recovery velocity before our
        // no-pushing policy zeroes it. A shallow rear contact can then clip an
        // actor moving away. Preserve that outgoing motion; deeper overlaps
        // still use Jolt's recovery and approaching contacts retain blocking.
        if(incomingVelocity.Dot(normal)>0 && !peerContactNeedsRecovery(mover,other))
            resolvedVelocity=incomingVelocity;
    }
    void CollideCharacter(const JPH::CharacterVirtual* mover,JPH::RMat44Arg transform,const JPH::CollideShapeSettings& settings,JPH::RVec3Arg base,JPH::CollideShapeCollector& collector) const override {
        const auto first=transform.PostTranslated(-base).ToMat44();const auto* shape=mover->GetShape();
        const auto bounds=shape->GetWorldSpaceBounds(first,JPH::Vec3::sOne());
        for(const auto& [id,peer]:characters) {
            (void)id;if(!peerAllowed(mover,peer)||collector.ShouldEarlyOut())continue;
            const auto other=peer.motor->GetCenterOfMassTransform().PostTranslated(-base).ToMat44();
            auto configuration=settings;configuration.mMaxSeparationDistance+=peer.motor->GetCharacterPadding();
            auto otherBounds=peer.motor->GetShape()->GetWorldSpaceBounds(other,JPH::Vec3::sOne());otherBounds.ExpandBy(JPH::Vec3::sReplicate(configuration.mMaxSeparationDistance));
            if(!bounds.Overlaps(otherBounds))continue;
            collector.SetUserData(reinterpret_cast<JPH::uint64>(peer.motor.GetPtr()));
            JPH::CollisionDispatch::sCollideShapeVsShape(shape,peer.motor->GetShape(),JPH::Vec3::sOne(),JPH::Vec3::sOne(),first,other,{},{},configuration,collector);
        }
        collector.SetUserData(0);
    }
    void CastCharacter(const JPH::CharacterVirtual* mover,JPH::RMat44Arg transform,JPH::Vec3Arg direction,const JPH::ShapeCastSettings& settings,JPH::RVec3Arg base,JPH::CastShapeCollector& collector) const override {
        const auto start=transform.PostTranslated(-base).ToMat44();const auto* shape=mover->GetShape();
        const JPH::ShapeCast cast(shape,JPH::Vec3::sOne(),start,direction);
        auto bounds=cast.mShapeWorldBounds;bounds.Encapsulate(cast.mShapeWorldBounds.mMin+direction);bounds.Encapsulate(cast.mShapeWorldBounds.mMax+direction);
        for(const auto& [id,peer]:characters) {
            (void)id;if(!peerAllowed(mover,peer)||collector.ShouldEarlyOut())continue;
            const auto other=peer.motor->GetCenterOfMassTransform().PostTranslated(-base).ToMat44();
            auto otherBounds=peer.motor->GetShape()->GetWorldSpaceBounds(other,JPH::Vec3::sOne());otherBounds.ExpandBy(JPH::Vec3::sReplicate(peer.motor->GetCharacterPadding()));
            if(!bounds.Overlaps(otherBounds))continue;
            collector.SetUserData(reinterpret_cast<JPH::uint64>(peer.motor.GetPtr()));
            JPH::CollisionDispatch::sCastShapeVsShapeWorldSpace(cast,settings,peer.motor->GetShape(),JPH::Vec3::sOne(),{},other,{},{},collector);
        }
        collector.SetUserData(0);
    }
    void OnContactSolve(const JPH::CharacterVirtual* character,const JPH::BodyID&,const JPH::SubShapeID&,
        JPH::RVec3Arg,JPH::Vec3Arg normal,JPH::Vec3Arg contactVelocity,const JPH::PhysicsMaterial*,
        JPH::Vec3Arg,JPH::Vec3& resolvedVelocity) override {
        const auto found=characters.find(character->GetUserData());
        // Gravity must still establish support and handle falling. Cancel its
        // downhill projection only at an idle, stationary, walkable contact.
        if(found!=characters.end() && found->second.motor.GetPtr()==character && !found->second.allowSliding &&
            contactVelocity.IsNearZero() && normal.Dot(character->GetUp())>=character->GetCosMaxSlopeAngle())
            resolvedVelocity=JPH::Vec3::sZero();
    }
    void refresh(JPH::CharacterVirtual& motor) {
        motor.RefreshContacts(world.GetDefaultBroadPhaseLayerFilter(CharacterLayer),world.GetDefaultLayerFilter(CharacterLayer),{},{},allocator);
    }
    void eraseCollider(std::map<EntityId,Collider>::iterator iterator) {
        world.GetBodyInterface().RemoveBody(iterator->second.body);world.GetBodyInterface().DestroyBody(iterator->second.body);colliders.erase(iterator);
    }
    void sampleTriggers(const Scene& scene);
};
void PhysicsWorld::Impl::sampleTriggers(const Scene& scene) {
    ++ticks;events.clear();
    if(triggers.empty()&&overlaps.empty()){initial.clear();configured.clear();removed.clear();return;}
    // Resolve descendants from the newly accepted motor poses, before callers
    // publish those poses to their scene. Attachments follow this same tick.
    std::map<EntityId,Mat4> transforms;
    std::function<std::optional<Mat4>(EntityId)> resolvedWorld=[&](EntityId id)->std::optional<Mat4> {
        if(const auto known=transforms.find(id);known!=transforms.end())return known->second;
        if(const auto character=characters.find(id);character!=characters.end())return transforms[id]=character->second.world;
        const auto record=scene.find(id);if(!record)return {};
        auto value=record->local;
        if(record->parent){const auto parent=resolvedWorld(record->parent);if(!parent)return {};value=*parent*value;}
        return transforms[id]=value;
    };
    struct Target {EntityId id{},owner{};CollisionFilter collision;JPH::TransformedShape shape;bool sensor{};};
    std::vector<Target> targets;targets.reserve(colliders.size()+meshColliders.size()+characters.size()+triggers.size());
    const auto addBody=[&](EntityId id,const JPH::BodyID& body,CollisionFilter collision) {
        const JPH::BodyLockRead lock(world.GetBodyLockInterface(),body);
        if(lock.Succeeded())targets.push_back({id,NoEntity,collision,lock.GetBody().GetTransformedShape()});
    };
    for(const auto& [id,collider]:colliders)addBody(id,collider.body,collider.definition.collision);
    for(const auto& [id,collider]:meshColliders)addBody(id,collider.body,collider.collision);
    for(const auto& [id,character]:characters)targets.push_back({id,id,character.collision,character.motor->GetTransformedShape()});
    for(auto& [id,trigger]:triggers) {
        if(const auto transform=resolvedWorld(id))trigger.world=*transform;
        const auto fields=math::affineTransformFields(trigger.world);if(!fields)continue;
        JPH::TransformedShape shape;shape.mShape=trigger.shape;shape.SetWorldTransform(position(trigger.world),rotation(fields->rotation),JPH::Vec3::sOne());
        targets.push_back({id,trigger.owner,trigger.definition.collision,std::move(shape),true});
    }
    std::map<EntityId,std::set<EntityId>> current;
    for(const auto& [id,trigger]:triggers) {
        const auto self=std::find_if(targets.begin(),targets.end(),[id](const auto& target){return target.id==id&&target.sensor;});if(self==targets.end())continue;
        auto& detected=current[id];const auto bounds=self->shape.GetWorldSpaceBounds();
        JPH::CollideShapeSettings settings;settings.mBackFaceMode=JPH::EBackFaceMode::CollideWithBackFaces;
        for(const auto& target:targets) {
            if(target.id==id||(trigger.owner&&trigger.owner==target.owner)||!(trigger.definition.collision.mask&target.collision.layers)||!bounds.Overlaps(target.shape.GetWorldSpaceBounds()))continue;
            JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> collector;
            target.shape.CollideShape(trigger.shape,JPH::Vec3::sOne(),self->shape.GetCenterOfMassTransform(),settings,self->shape.mShapePositionCOM,collector);
            if(collector.HadHit())detected.insert(target.id);
        }
    }
    const auto reason=[&](EntityId trigger,EntityId other,bool enter) {
        if(!enter&&(removed.contains(trigger)||removed.contains(other)))return TriggerEventReason::Removed;
        if(configured.contains(trigger)||configured.contains(other))return TriggerEventReason::Configuration;
        if(enter&&(initial.contains(trigger)||initial.contains(other)))return TriggerEventReason::Initial;
        return TriggerEventReason::Motion;
    };
    for(const auto& [id,previous]:overlaps)for(const auto other:previous) {
        const auto next=current.find(id);if(next==current.end()||!next->second.contains(other))events.push_back({identity,ticks,id,other,TriggerEventKind::Exit,reason(id,other,false)});
    }
    for(const auto& [id,next]:current)for(const auto other:next) {
        const auto previous=overlaps.find(id);if(previous==overlaps.end()||!previous->second.contains(other))events.push_back({identity,ticks,id,other,TriggerEventKind::Enter,reason(id,other,true)});
    }
    std::sort(events.begin(),events.end(),[](const auto& a,const auto& b){if(a.trigger!=b.trigger)return a.trigger<b.trigger;if(a.other!=b.other)return a.other<b.other;return a.kind<b.kind;});
    overlaps=std::move(current);initial.clear();configured.clear();removed.clear();
}
PhysicsWorld::PhysicsWorld():impl_(std::make_unique<Impl>()){}
PhysicsWorld::~PhysicsWorld()=default;
void PhysicsWorld::setAgentProfiles(NavigationAgentProfileQuery query){impl_->profiles=std::move(query);}
void PhysicsWorld::reconcile(const Scene& scene,std::span<const EntityId> locks,std::span<const PhysicsMeshCollider> meshes) {
    reconcileInternal(scene,locks,meshes,true);
}
void PhysicsWorld::reconcileStatics(const Scene& scene,std::span<const PhysicsMeshCollider> meshes) {
    reconcileInternal(scene,{},meshes,false);
}
void PhysicsWorld::reconcileInternal(const Scene& scene,std::span<const EntityId> locks,std::span<const PhysicsMeshCollider> meshes,bool includeCharacters) {
    auto& state=*impl_;state.diagnostic.clear();
    bool collidersChanged=false,charactersChanged=false;
    std::unordered_set<EntityId> keptColliders,keptCharacters,keptTriggers;
    const auto records=scene.records();
    for(const auto& record:records) {
        const auto world=scene.worldTransform(record.id);if(!world)continue;
        if(record.collider && record.collider->shape!=ColliderShape::Trimesh) {
            const auto fields=math::affineTransformFields(*world);auto definition=*record.collider;
            const bool trigger=definition.mode==ColliderMode::Trigger;
            if(!trigger)definition.collision=solidFilter(definition.collision);
            if(!trigger&&(underCharacter(scene,record.id) || record.controller)){state.report(record.id,"Static Collider cannot be attached beneath a Character Controller; its capsule supplies collision.");continue;}
            if(!fields || fields->singular || fields->sheared){state.report(record.id,"Collider is disabled: its world transform is collapsed or sheared.");continue;}
            const auto bounds=primitiveColliderBounds(definition);const auto localHalf=bounds.maximum;
            Vec3 half{},scale{};bool valid=bounds.valid && math::finite(localHalf);
            for(int i=0;i<3;++i){scale[i]=std::abs(fields->scale[i]);half[i]=localHalf[i]*scale[i];valid=valid && localHalf[i]>0 && math::finite(half[i]) && half[i]>1e-6f;}
            valid=valid && (definition.shape!=ColliderShape::Capsule || definition.height>=2*definition.radius);
            if(!valid){state.report(record.id,"Collider requires positive finite dimensions and nonzero world scale; capsule height includes both caps.");continue;}
            auto found=state.colliders.find(record.id);
            auto sensor=state.triggers.find(record.id);
            if(trigger&&sensor!=state.triggers.end()&&sameCollider(sensor->second.definition,definition)&&close(sensor->second.world,*world)) {
                if(sensor->second.definition.collision!=definition.collision||sensor->second.owner!=characterOwner(scene,record.id))state.configured.insert(record.id);
                sensor->second.definition=definition;sensor->second.owner=characterOwner(scene,record.id);keptTriggers.insert(record.id);continue;
            }
            if(!trigger&&found!=state.colliders.end() && sameCollider(found->second.definition,definition) && close(found->second.world,*world)) {
                if(found->second.definition.collision!=definition.collision)state.configured.insert(record.id);
                found->second.definition.collision=definition.collision;keptColliders.insert(record.id);continue;
            }
            JPH::ShapeSettings::ShapeResult shape;
            std::shared_ptr<const StaticTriangleMesh> geometry;
            const auto same=[](float a,float b){return std::abs(a-b)<=1e-6f*std::max(a,b);};
            const bool radial=same(scale[0],scale[2]),uniform=radial && same(scale[0],scale[1]);
            if(definition.shape==ColliderShape::Box) {
                const float convexRadius=std::min(.05f,std::min({half[0],half[1],half[2]})*.1f);
                shape=JPH::BoxShapeSettings(native(half),convexRadius).Create();
            } else {
                const bool analytic=definition.shape==ColliderShape::Cylinder?radial:uniform;
                // Jolt hulls support at most 256 vertices. This closed sampling
                // stays below that limit, including both capsule equators.
                geometry=primitiveColliderMesh(definition,analytic?32:24,analytic?8:5);
                if(!analytic) {
                    JPH::Array<JPH::Vec3> points;points.reserve(geometry->positions.size());
                    for(const auto& point:geometry->positions)points.push_back({point[0]*scale[0],point[1]*scale[1],point[2]*scale[2]});
                    JPH::ConvexHullShapeSettings hull(points);hull.mMaxConvexRadius=0;
                    shape=hull.Create();
                } else if(definition.shape==ColliderShape::Sphere)shape=JPH::SphereShapeSettings(half[0]).Create();
                else if(definition.shape==ColliderShape::Cylinder)shape=JPH::CylinderShapeSettings(half[1],half[0],0).Create();
                else {
                    const auto cylinderHalf=(definition.height*.5f-definition.radius)*scale[1];
                    shape=cylinderHalf<=1e-6f?JPH::SphereShapeSettings(half[0]).Create():JPH::CapsuleShapeSettings(cylinderHalf,half[0]).Create();
                }
            }
            if(shape.HasError()){state.report(record.id,shape.GetError().c_str());continue;}
            if(trigger) {
                if(sensor==state.triggers.end())state.initial.insert(record.id);else state.configured.insert(record.id);
                state.triggers[record.id]=Impl::Trigger{shape.Get(),*world,definition,characterOwner(scene,record.id)};keptTriggers.insert(record.id);continue;
            }
            if(found==state.colliders.end())state.initial.insert(record.id);else state.configured.insert(record.id);
            if(found!=state.colliders.end())state.eraseCollider(found);
            collidersChanged=true;
            JPH::BodyCreationSettings settings(shape.Get(),position(*world),rotation(fields->rotation),JPH::EMotionType::Static,StaticLayer);
            settings.mUserData=record.id;
            const auto body=state.world.GetBodyInterface().CreateAndAddBody(settings,JPH::EActivation::DontActivate);
            if(body.IsInvalid()){state.report(record.id,"Physics body capacity exceeded; Collider is disabled.");continue;}
            state.colliders.emplace(record.id,Impl::Collider{body,*world,definition,std::move(geometry)});keptColliders.insert(record.id);
        }
    }
    for(auto iterator=state.colliders.begin();iterator!=state.colliders.end();) {
        if(keptColliders.contains(iterator->first)){++iterator;continue;}
        (scene.find(iterator->first)?state.configured:state.removed).insert(iterator->first);auto remove=iterator++;state.eraseCollider(remove);collidersChanged=true;
    }
    for(auto iterator=state.triggers.begin();iterator!=state.triggers.end();) {
        if(keptTriggers.contains(iterator->first)){++iterator;continue;}
        (scene.find(iterator->first)?state.configured:state.removed).insert(iterator->first);iterator=state.triggers.erase(iterator);
    }
    std::unordered_set<EntityId> keptMeshes;
    for(const auto& record:records) {
        if(!record.collider || record.collider->shape!=ColliderShape::Trimesh || (!record.collider->mesh && record.collider->source.empty()))continue;
        if(record.collider->mode==ColliderMode::Trigger){state.report(record.id,"Trimesh triggers are unsupported; use a primitive Trigger.");continue;}
        if(underCharacter(scene,record.id) || record.controller){state.report(record.id,"Static mesh collision cannot be attached beneath a Character Controller.");continue;}
        const auto world=scene.worldTransform(record.id);if(!world)continue;
        const auto found=state.meshColliders.find(record.id);
        const auto filter=solidFilter(record.collider->collision);
        if(found!=state.meshColliders.end()&&found->second.collision!=filter){found->second.collision=filter;state.configured.insert(record.id);}
        const auto prepared=std::find_if(meshes.begin(),meshes.end(),[&](const auto& mesh){return mesh.entity==record.id;});
        const PhysicsMeshCollider native{record.id,record.collider->mesh,record.collider->mesh?record.collider->mesh->fingerprint:0};
        const auto* pending=record.collider->mesh?&native:prepared!=meshes.end()?&*prepared:nullptr;
        if(!pending || !pending->mesh) {
            if(found!=state.meshColliders.end())keptMeshes.insert(record.id);
            state.report(record.id,"Mesh collision source is not ready; retaining its last accepted geometry.");continue;
        }
        if(found!=state.meshColliders.end() && found->second.revision==pending->revision &&
           found->second.mesh==pending->mesh && close(found->second.world,*world)){keptMeshes.insert(record.id);continue;}
        try {
            if(math::handedness(*world)==0)throw std::runtime_error("Mesh Collider has a collapsed transform.");
            const auto& mesh=*pending->mesh;
            if(mesh.indices.empty() || mesh.indices.size()%3)throw std::runtime_error("Mesh Collider has no valid triangles.");
            JPH::VertexList vertices;vertices.reserve(mesh.positions.size());
            for(const auto& point:mesh.positions) {
                const auto transformed=math::transformDirection(*world,point);
                if(!math::finite(transformed))throw std::runtime_error("Mesh Collider transform exceeds finite range.");
                vertices.emplace_back(transformed[0],transformed[1],transformed[2]);
            }
            JPH::IndexedTriangleList triangles;triangles.reserve(mesh.indices.size()/3);
            const bool reflected=math::handedness(*world)<0;
            for(std::size_t index=0;index<mesh.indices.size();index+=3) {
                const auto a=mesh.indices[index],b=mesh.indices[index+(reflected?2:1)],c=mesh.indices[index+(reflected?1:2)];
                if(a>=vertices.size() || b>=vertices.size() || c>=vertices.size())throw std::runtime_error("Mesh Collider index exceeds its positions.");
                triangles.emplace_back(a,b,c,0);
            }
            const auto shape=JPH::MeshShapeSettings(std::move(vertices),std::move(triangles)).Create();
            if(shape.HasError())throw std::runtime_error(shape.GetError().c_str());
            JPH::BodyCreationSettings settings(shape.Get(),position(*world),JPH::Quat::sIdentity(),JPH::EMotionType::Static,StaticLayer);
            settings.mUserData=record.id;
            const auto body=state.world.GetBodyInterface().CreateAndAddBody(settings,JPH::EActivation::DontActivate);
            if(body.IsInvalid())throw std::runtime_error("Physics body capacity exceeded.");
            if(found!=state.meshColliders.end()) {
                state.world.GetBodyInterface().RemoveBody(found->second.body);state.world.GetBodyInterface().DestroyBody(found->second.body);
                found->second={body,*world,pending->mesh,pending->revision,filter};
            } else state.meshColliders.emplace(record.id,Impl::MeshCollider{body,*world,pending->mesh,pending->revision,filter});
            (found==state.meshColliders.end()?state.initial:state.configured).insert(record.id);
            keptMeshes.insert(record.id);collidersChanged=true;
        } catch(const std::exception& error) {
            if(found!=state.meshColliders.end())keptMeshes.insert(record.id);
            state.report(record.id,error.what());
        }
    }
    for(auto iterator=state.meshColliders.begin();iterator!=state.meshColliders.end();) {
        if(keptMeshes.contains(iterator->first)){++iterator;continue;}
        state.world.GetBodyInterface().RemoveBody(iterator->second.body);state.world.GetBodyInterface().DestroyBody(iterator->second.body);
        (scene.find(iterator->first)?state.configured:state.removed).insert(iterator->first);
        iterator=state.meshColliders.erase(iterator);collidersChanged=true;
    }
    if(collidersChanged)++state.geometryRevision;
    if(!includeCharacters){state.characters.clear();state.desiredVelocities.clear();state.firstPersonRequests.clear();state.jumpRequests.clear();state.desiredFacing.clear();return;}
    for(const auto& record:records) {
        if(!record.controller)continue;const auto world=scene.worldTransform(record.id);if(!world)continue;
        std::string error;
        if(underCharacter(scene,record.id)){state.report(record.id,"A Character cannot be nested beneath another Character.");continue;}
        auto found=state.characters.find(record.id);
        if(found!=state.characters.end()&&found->second.collision!=characterFilter(record.controller->collision)) {
            found->second.collision=characterFilter(record.controller->collision);state.configured.insert(record.id);charactersChanged=true;
        }
        const auto resolved=resolveAgentTraversal(*record.controller,state.profiles);
        if(!resolved) {
            state.report(record.id,"Navigation Agent Profile is unavailable; retaining the last accepted capsule.");
            if(found!=state.characters.end()){keptCharacters.insert(record.id);found->second.profileCurrent=false;found->second.movementEnabled=false;found->second.firstPerson={};found->second.stepHeight=0;}
            state.jumpRequests.erase(record.id);
            continue;
        }
        if(!validCharacter(*world,*resolved,error)){state.report(record.id,error);continue;}
        const auto& definition=*resolved;
        const bool profileChanged=found!=state.characters.end() && (found->second.definition!=definition || found->second.profile!=record.controller->profile.id);
        const auto retainPrevious=[&]{
            if(found!=state.characters.end()){keptCharacters.insert(record.id);found->second.profileCurrent=false;found->second.movementEnabled=false;found->second.firstPerson={};found->second.stepHeight=0;}
            state.jumpRequests.erase(record.id);
        };
        const bool newCapsule=found==state.characters.end()||found->second.definition.height!=definition.height||found->second.definition.radius!=definition.radius;
        if(newCapsule) {
            JPH::ShapeSettings::ShapeResult capsule;
            const float cylinderHalfHeight=(definition.height-2*definition.radius)*.5f;
            if(cylinderHalfHeight<=1e-6f)capsule=JPH::SphereShapeSettings(definition.radius).Create();
            else capsule=JPH::CapsuleShapeSettings(cylinderHalfHeight,definition.radius).Create();
            if(capsule.HasError()){state.report(record.id,capsule.GetError().c_str());retainPrevious();continue;}
            const auto shifted=JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0,definition.height*.5f,0),JPH::Quat::sIdentity(),capsule.Get()).Create();
            if(shifted.HasError()){state.report(record.id,shifted.GetError().c_str());retainPrevious();continue;}
            JPH::CharacterVirtualSettings settings;settings.mShape=shifted.Get();settings.mMaxSlopeAngle=definition.maxSlope*std::numbers::pi_v<float>/180.f;
            settings.mID=JPH::CharacterID(state.nextCharacter++);
            settings.mSupportingVolume=JPH::Plane(JPH::Vec3::sAxisY(),-definition.radius);
            settings.mMaxStrength=0;settings.mInnerBodyShape=nullptr;
            Impl::Character character;character.definition=definition;character.profile=record.controller->profile.id;character.world=*world;character.collision=characterFilter(record.controller->collision);
            character.motor=new JPH::CharacterVirtual(&settings,position(*world),rotation(*world),record.id,&state.world);
            character.motor->SetListener(&state);
            character.motor->SetCharacterVsCharacterCollision(&state);
            charactersChanged=true;(found==state.characters.end()?state.initial:state.configured).insert(record.id);
            if(found==state.characters.end())found=state.characters.emplace(record.id,std::move(character)).first;
            else found->second=std::move(character);
        }
        auto& character=found->second;const bool editing=locked(scene,record.id,locks);
        character.motor->SetMaxSlopeAngle(definition.maxSlope*std::numbers::pi_v<float>/180.f);
        character.profileCurrent=true;character.profile=record.controller->profile.id;
        if(!close(character.world,*world) || character.locked!=editing || profileChanged) {
            character.motor->SetPosition(position(*world));character.motor->SetRotation(rotation(*world));
            character.motor->SetLinearVelocity(JPH::Vec3::sZero());character.world=*world;charactersChanged=true;state.configured.insert(record.id);
            character.firstPerson={};character.resolvedVelocity={};character.stepHeight=0;state.jumpRequests.erase(record.id);
        }
        character.locked=editing;character.definition=definition;
        character.movementEnabled=record.movement && record.movement->enabled;keptCharacters.insert(record.id);
        if(editing || !character.movementEnabled){character.firstPerson={};character.stepHeight=0;state.jumpRequests.erase(record.id);}
    }
    std::erase_if(state.characters,[&](const auto& entry){
        if(keptCharacters.contains(entry.first))return false;
        (scene.find(entry.first)?state.configured:state.removed).insert(entry.first);charactersChanged=true;return true;
    });
    // Publish every replacement/removal before querying peers. The provider
    // reads this owning map and never retains dangling registration pointers.
    if(collidersChanged||charactersChanged)for(auto& [id,character]:state.characters){(void)id;state.refresh(*character.motor);}
}
std::vector<ScenePoseUpdate> PhysicsWorld::step(const Scene& scene,EntityId controlled,const Vec3& desired,float seconds,std::span<const EntityId> locks,std::span<const PhysicsMeshCollider> meshes) {
    reconcile(scene,locks,meshes);std::vector<ScenePoseUpdate> poses;
    for(auto& [id,character]:impl_->characters){(void)id;character.stepHeight=0;}
    if(!std::isfinite(seconds) || seconds<=0)return poses;auto& state=*impl_;
    auto movement=std::move(state.desiredVelocities);state.desiredVelocities.clear();
    auto firstPerson=std::move(state.firstPersonRequests);state.firstPersonRequests.clear();
    auto jumps=std::move(state.jumpRequests);state.jumpRequests.clear();
    auto facing=std::move(state.desiredFacing);state.desiredFacing.clear();
    if(controlled!=NoEntity){movement[controlled]=math::finite(desired)?desired:Vec3{};firstPerson.erase(controlled);}
    // Every peer observes this tick's requested velocity, independent of its
    // stable EntityId update position. Character collision never applies forces.
    for(auto& [id,character]:state.characters) {
        if(character.locked){character.motor->SetLinearVelocity(JPH::Vec3::sZero());character.firstPerson={};character.resolvedVelocity={};continue;}
        auto& motor=*character.motor;motor.UpdateGroundVelocity();
        const auto current=motor.GetLinearVelocity();
        float vertical=current.GetY();
        const bool onGround=motor.GetGroundState()==JPH::CharacterBase::EGroundState::OnGround && vertical<=.1f;
        if(onGround)vertical=0;
        const auto requested=movement.find(id);
        Vec3 horizontal=character.movementEnabled && requested!=movement.end()?Vec3{requested->second[0],0,requested->second[2]}:Vec3{};
        const auto fps=firstPerson.find(id);
        if(character.movementEnabled && fps!=firstPerson.end()) {
            auto& motion=character.firstPerson;
            if(!motion.active){motion={};motion.active=true;motion.velocity=character.resolvedVelocity;}
            motion.coyote=onGround?.10f:std::max(0.f,motion.coyote-seconds);
            motion.jumpBuffer=std::max(0.f,motion.jumpBuffer-seconds);
            if(const auto jump=jumps.find(id);jump!=jumps.end()){motion.jumpBuffer=.12f;motion.jumpHeight=jump->second;}
            const auto& input=fps->second;const auto wish=input.velocity;
            horizontal={motion.velocity[0],0,motion.velocity[2]};
            if(onGround) {
                if(math::length(wish)>1e-6)horizontal=approachVelocity(horizontal,wish,input.groundAcceleration*seconds);
                else {
                    const auto speed=math::length(horizontal);
                    if(speed>1e-6)horizontal=horizontal*(std::max(0.,speed-input.groundFriction*std::max(speed,1.5)*seconds)/speed);
                    else horizontal={};
                }
            } else if(math::length(wish)>1e-6) {
                horizontal=approachVelocity(horizontal,wish,input.airAcceleration*input.airControl*seconds);
            }
            if(motion.jumpBuffer>0 && motion.coyote>0 && motion.jumpHeight>0) {
                vertical=static_cast<float>(std::sqrt(2.0*9.81*motion.jumpHeight));
                motion.coyote=0;motion.jumpBuffer=0;
            }
        } else character.firstPerson={};
        vertical-=9.81f*seconds;
        character.allowSliding=!native(horizontal).IsNearZero();
        const auto requestedFacing=facing.find(id);
        const auto heading=character.movementEnabled && requestedFacing!=facing.end()?Vec3{requestedFacing->second[0],0,requestedFacing->second[2]}:horizontal;
        if(math::length(heading)>1e-6) {
            const auto yaw=std::atan2(-heading[0],-heading[2]);
            const auto rotationMatrix=math::rotation(math::axisAngle({0,1,0},yaw));
            for(int column=0;column<3;++column)for(int row=0;row<3;++row)character.world.at(row,column)=rotationMatrix.at(row,column);
            motor.SetRotation(rotation(character.world));
        }
        motor.SetLinearVelocity(native(horizontal)+JPH::Vec3(0,vertical,0));
    }
    for(auto& [id,character]:state.characters) {
        if(character.locked)continue;
        JPH::CharacterVirtual::ExtendedUpdateSettings update;
        update.mWalkStairsStepUp={0,character.definition.maxStep,0};update.mStickToFloorStepDown={0,-.5f,0};
        // Follow an opposing riser even when most movement is along its face.
        // The native stair sweep still requires progress into an actual steep
        // contact, a reachable support surface and clearance for the capsule.
        update.mWalkStairsCosAngleForwardContact=0;
        auto& motor=*character.motor;
        // A jump must leave support cleanly. Floor snapping and stair probes
        // resume on the next supported walking tick, including downhill steps.
        if(character.firstPerson.active && motor.GetLinearVelocity().GetY()>0){update.mWalkStairsStepUp=JPH::Vec3::sZero();update.mStickToFloorStepDown=JPH::Vec3::sZero();}
        const auto previous=motor.GetPosition();
        character.stepHeight=updateCharacter(motor,seconds,state.world,update,state.allocator);
        const auto resolved=own(motor.GetPosition());
        if(!math::finite(resolved)){state.report(id,"Character produced a nonfinite pose; movement was discarded.");motor.SetPosition(position(character.world));motor.SetLinearVelocity(JPH::Vec3::sZero());character.firstPerson={};character.resolvedVelocity={};character.stepHeight=0;continue;}
        character.resolvedVelocity=own((motor.GetPosition()-previous)/seconds);
        if(character.firstPerson.active) {
            auto& motion=character.firstPerson;
            // CharacterVirtual retains its commanded velocity after solving.
            // Actual displacement supplies wall/slope-resolved momentum; keep
            // ballistic vertical speed separate from stair and floor snapping.
            motion.velocity=character.resolvedVelocity;
            float vertical=motor.GetLinearVelocity().GetY();
            for(const auto& contact:motor.GetActiveContacts())
                if(contact.mHadCollision && !contact.mWasDiscarded && !contact.mIsSensorB && contact.mContactNormal.GetY()<-.01f && vertical>0)vertical=0;
            if(motor.GetGroundState()==JPH::CharacterBase::EGroundState::OnGround && vertical<0)vertical=0;
            motion.velocity[1]=vertical;
            motor.SetLinearVelocity(native(motion.velocity));
        }
        auto world=character.world;world[12]=resolved[0];world[13]=resolved[1];world[14]=resolved[2];
        const auto record=scene.find(id);if(!record)continue;
        Mat4 local=world;
        if(record->parent) {
            const auto parent=scene.worldTransform(record->parent);const auto inverse=parent?math::inverse(*parent):std::nullopt;
            if(!inverse){state.report(id,"Character parent transform is not invertible.");character.stepHeight=0;continue;}local=*inverse*world;
        }
        poses.push_back({id,local});character.world=world;
    }
    state.sampleTriggers(scene);
    return poses;
}
const std::string& PhysicsWorld::status() const{return impl_->diagnostic;}
void PhysicsWorld::requestMovement(EntityId id,const Vec3& desired){if(id){impl_->desiredVelocities[id]=math::finite(desired)?desired:Vec3{};impl_->firstPersonRequests.erase(id);}}
void PhysicsWorld::requestFirstPersonMovement(EntityId id,const Vec3& desired,float groundAcceleration,float groundFriction,float airAcceleration,float airControl) {
    if(!id)return;
    const auto velocity=math::finite(desired)?Vec3{desired[0],0,desired[2]}:Vec3{};
    impl_->firstPersonRequests[id]={velocity,nonnegative(groundAcceleration),nonnegative(groundFriction),nonnegative(airAcceleration),std::clamp(nonnegative(airControl),0.f,1.f)};
    impl_->desiredVelocities.erase(id);
}
void PhysicsWorld::requestJump(EntityId id,float height){if(id && std::isfinite(height) && height>0)impl_->jumpRequests[id]=height;}
void PhysicsWorld::requestFacing(EntityId id,const Vec3& direction){if(id)impl_->desiredFacing[id]=math::finite(direction)?direction:Vec3{};}
std::size_t PhysicsWorld::colliderCount() const{return impl_->colliders.size()+impl_->meshColliders.size()+impl_->triggers.size();}
std::uint64_t PhysicsWorld::geometryRevision() const{return impl_->geometryRevision;}
std::vector<PhysicsBox> PhysicsWorld::staticBoxes() const {
    std::vector<PhysicsBox> boxes;boxes.reserve(impl_->colliders.size());
    for(const auto& [id,collider]:impl_->colliders)if(collider.definition.shape==ColliderShape::Box)boxes.push_back({id,collider.world,collider.definition.size});
    return boxes;
}
std::vector<PhysicsStaticMesh> PhysicsWorld::staticMeshes() const {
    std::vector<PhysicsStaticMesh> result;result.reserve(impl_->meshColliders.size());
    for(const auto& [id,collider]:impl_->meshColliders)result.push_back({id,collider.world,collider.mesh});
    for(const auto& [id,collider]:impl_->colliders)if(collider.mesh)result.push_back({id,collider.world,collider.mesh,true});
    return result;
}
std::size_t PhysicsWorld::characterCount() const{return impl_->characters.size();}
std::optional<Vec3> PhysicsWorld::characterPosition(EntityId id) const{const auto found=impl_->characters.find(id);return found==impl_->characters.end()?std::nullopt:std::optional{own(found->second.motor->GetPosition())};}
std::optional<Vec3> PhysicsWorld::characterVelocity(EntityId id) const{const auto found=impl_->characters.find(id);return found==impl_->characters.end()?std::nullopt:std::optional{found->second.resolvedVelocity};}
float PhysicsWorld::characterStepHeight(EntityId id) const{const auto found=impl_->characters.find(id);return found==impl_->characters.end()?0.f:found->second.stepHeight;}
std::optional<AgentTraversalSettings> PhysicsWorld::acceptedCharacterTraversal(EntityId id) const {
    const auto found=impl_->characters.find(id);
    if(found==impl_->characters.end()||!found->second.profileCurrent)return {};
    return found->second.definition;
}
bool PhysicsWorld::grounded(EntityId id) const{const auto found=impl_->characters.find(id);return found!=impl_->characters.end() && found->second.motor->GetGroundState()==JPH::CharacterBase::EGroundState::OnGround;}
bool PhysicsWorld::characterLocked(EntityId id) const{const auto found=impl_->characters.find(id);return found!=impl_->characters.end() && found->second.locked;}
void PhysicsWorld::cancelCharacterInput(EntityId id) {
    impl_->desiredVelocities.erase(id);impl_->firstPersonRequests.erase(id);impl_->jumpRequests.erase(id);impl_->desiredFacing.erase(id);
    if(const auto found=impl_->characters.find(id);found!=impl_->characters.end()){found->second.firstPerson={};found->second.stepHeight=0;}
}
std::span<const TriggerEvent> PhysicsWorld::triggerEvents() const{return impl_->events;}
std::vector<EntityId> PhysicsWorld::triggerOverlaps(EntityId id) const {
    const auto found=impl_->overlaps.find(id);return found==impl_->overlaps.end()?std::vector<EntityId>{}:std::vector<EntityId>(found->second.begin(),found->second.end());
}
std::optional<PhysicsRayHit> PhysicsWorld::raycast(const Ray& ray,float maxDistance) const {
    PhysicsQueryFilter filter;filter.includeCharacters=false;return raycast(ray,maxDistance,filter);
}
std::optional<PhysicsRayHit> PhysicsWorld::raycast(const Ray& ray,float maxDistance,const PhysicsQueryFilter& filter) const {
    if(!math::finite(ray.origin) || !math::finite(ray.direction) || !std::isfinite(maxDistance) || maxDistance<=0)return {};
    const auto length=math::length(ray.direction);if(length<=1e-8)return {};
    const auto displacement=ray.direction*(static_cast<double>(maxDistance)/length);
    if(!math::finite(displacement) || !math::finite(ray.origin+displacement))return {};
    const JPH::RRayCast query(JPH::RVec3(ray.origin[0],ray.origin[1],ray.origin[2]),native(displacement));
    auto& world=impl_->world;std::optional<PhysicsRayHit> nearest;float fraction=1.f;
    const auto ignored=[&](EntityId id){return std::find(filter.ignored.begin(),filter.ignored.end(),id)!=filter.ignored.end();};
    const auto acceptHit=[&](EntityId id,const JPH::TransformedShape& shape,const JPH::RayCastResult& result) {
        if(result.mFraction>fraction)return;
        const auto point=query.GetPointOnRay(result.mFraction);const auto normal=shape.GetWorldSpaceSurfaceNormal(result.mSubShapeID2,point);
        PhysicsRayHit hit{id,own(point),{normal.GetX(),normal.GetY(),normal.GetZ()}};
        if(!math::finite(hit.position)||!math::finite(hit.normal)||math::length(hit.normal)<=1e-8)return;
        if(nearest&&result.mFraction==fraction&&nearest->entity<id)return;
        hit.normal=math::normalized(hit.normal);nearest=hit;fraction=result.mFraction;
    };
    const auto accept=[&](EntityId id,const JPH::TransformedShape& shape) {
        JPH::RayCastSettings settings;settings.mBackFaceModeTriangles=JPH::EBackFaceMode::CollideWithBackFaces;
        JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;shape.CastRay(query,settings,collector);
        if(collector.HadHit())acceptHit(id,shape,collector.mHit);
    };
    if(filter.includeWorld&&filter.mask) {
        class QueryBodies final:public JPH::BodyFilter {
        public:
            const Impl* state{};const PhysicsQueryFilter* query{};
            bool ShouldCollideLocked(const JPH::Body& body) const override {
                const auto id=body.GetUserData();if(std::find(query->ignored.begin(),query->ignored.end(),id)!=query->ignored.end())return false;
                if(const auto found=state->colliders.find(id);found!=state->colliders.end())return (query->mask&found->second.definition.collision.layers)!=0;
                if(const auto found=state->meshColliders.find(id);found!=state->meshColliders.end())return (query->mask&found->second.collision.layers)!=0;
                return false;
            }
        } bodies;
        bodies.state=impl_.get();bodies.query=&filter;
        JPH::RayCastSettings settings;settings.mBackFaceModeTriangles=JPH::EBackFaceMode::CollideWithBackFaces;
        JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
        world.GetNarrowPhaseQuery().CastRay(query,settings,collector,JPH::SpecifiedBroadPhaseLayerFilter(JPH::BroadPhaseLayer(0)),JPH::SpecifiedObjectLayerFilter(StaticLayer),bodies);
        if(collector.HadHit()) {
            const auto& result=collector.mHit;const JPH::BodyLockRead body(world.GetBodyLockInterface(),result.mBodyID);
            if(body.Succeeded())acceptHit(body.GetBody().GetUserData(),body.GetBody().GetTransformedShape(),result);
        }
    }
    if(filter.includeCharacters)for(const auto& [id,character]:impl_->characters)
        if(!ignored(id)&&(filter.mask&character.collision.layers))accept(id,character.motor->GetTransformedShape());
    if(filter.includeTriggers)for(const auto& [id,trigger]:impl_->triggers) {
        if(ignored(id)||!(filter.mask&trigger.definition.collision.layers))continue;
        const auto fields=math::affineTransformFields(trigger.world);if(!fields)continue;
        JPH::TransformedShape shape;shape.mShape=trigger.shape;shape.SetWorldTransform(position(trigger.world),rotation(fields->rotation),JPH::Vec3::sOne());accept(id,shape);
    }
    return nearest;
}
}
