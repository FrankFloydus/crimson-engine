#include "PhysicsWorld.h"
#include "ColliderGeometry.h"
#include "assets/RagdollAsset.h"
#include "assets/ModelAnimation.h"
#include <crimson/renderer/core/Profiler.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
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
#include <unordered_map>
#include <unordered_set>

namespace crimson {
namespace {
constexpr JPH::ObjectLayer StaticLayer=0,CharacterLayer=1,DynamicLayer=2;
class Layers final:public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override{return 3;}
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override{return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer));}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override{return layer==JPH::BroadPhaseLayer(0)?"Static":layer==JPH::BroadPhaseLayer(1)?"Character":"Dynamic";}
#endif
};
class Pairs final:public JPH::ObjectLayerPairFilter {
public:bool ShouldCollide(JPH::ObjectLayer a,JPH::ObjectLayer b) const override{
    // Virtual characters resolve their peers through the explicit no-pushing
    // provider. Inner kinematic proxies exist only for dynamic-body response.
    return (a!=StaticLayer || b!=StaticLayer) && (a!=CharacterLayer || b!=CharacterLayer);
}
};
class BroadPairs final:public JPH::ObjectVsBroadPhaseLayerFilter {
public:bool ShouldCollide(JPH::ObjectLayer a,JPH::BroadPhaseLayer b) const override{return a!=StaticLayer || b!=JPH::BroadPhaseLayer(0);}
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
Mat4 ownMatrix(JPH::RVec3Arg point,JPH::QuatArg q) {
    const auto m=JPH::Mat44::sRotation(q);Mat4 result;
    for(int c=0;c<3;++c)for(int r=0;r<3;++r)result.at(r,c)=m.GetColumn4(c)[r];
    result[12]=point.GetX();result[13]=point.GetY();result[14]=point.GetZ();return result;
}
// Bone frame scale is independent from collider dimensions. Keep the residual
// affine basis for rendering, while Jolt receives a right-handed rigid frame.
Mat4 rigidFrame(const Mat4& matrix) {
    if(!math::finite(matrix))throw std::runtime_error("Ragdoll pose contains a nonfinite transform.");
    const Vec3 x{matrix[0],matrix[1],matrix[2]},y{matrix[4],matrix[5],matrix[6]},z{matrix[8],matrix[9],matrix[10]};
    if(math::length(x)<1e-7 || math::dot(math::cross(x,y),z)<=1e-12)throw std::runtime_error("Ragdoll bones require nonsingular right-handed transforms.");
    const auto axisX=math::normalized(x);const auto projected=y-axisX*math::dot(y,axisX);
    if(math::length(projected)<1e-7)throw std::runtime_error("Ragdoll bone basis is collapsed.");
    const auto axisY=math::normalized(projected),axisZ=math::cross(axisX,axisY);Mat4 result;
    for(int i=0;i<3;++i){result[i]=axisX[i];result[4+i]=axisY[i];result[8+i]=axisZ[i];result[12+i]=matrix[12+i];}return result;
}
// Retain at most one hit per body while allowing Jolt to prune farther triangles.
// Direct virtual-character/trigger queries use the same collector with an
// explicit entity because they do not have rigid-body handles.
template<class Base> class TraceCollector final:public Base {
public:
    using Result=typename Base::ResultType;
    std::function<bool(EntityId,const JPH::TransformedShape*,const Result&)> accept;
    EntityId entity{};
    PhysicsTraceMode mode{PhysicsTraceMode::Closest};
    float bodyLimit{};
    void OnBody(const JPH::Body& body) override {entity=body.GetUserData();bodyLimit=Base::GetEarlyOutFraction();}
    void OnBodyEnd() override {if(mode==PhysicsTraceMode::All)Base::ResetEarlyOutFraction(bodyLimit);}
    void AddHit(const Result& hit) override {
        if(!accept(entity,Base::GetContext(),hit))return;
        if(mode==PhysicsTraceMode::Any)Base::ForceEarlyOut();
        else {
            // Jolt ranks initial shape overlaps by negative penetration depth.
            // Our distance-zero ties use entity/subshape identity, so a deep
            // overlap must not prune a shallower overlap on another collider.
            const float limit=std::nextafter(std::max(0.f,hit.GetEarlyOutFraction()),std::numeric_limits<float>::infinity());
            if(limit<Base::GetEarlyOutFraction())Base::UpdateEarlyOutFraction(limit);
        }
    }
};
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
struct PhysicsWorld::Impl final: JPH::CharacterContactListener,JPH::CharacterVsCharacterCollision,JPH::ContactListener {
    // Lifetime is declared first so every Jolt resource is released before it.
    std::shared_ptr<JoltLifetime> lifetime{acquireJolt()};
    Layers layers;Pairs pairs;BroadPairs broadPairs;
    JPH::PhysicsSystem world;
    JPH::TempAllocatorImpl allocator{16*1024*1024};
    JPH::JobSystemSingleThreaded jobs{2048};
    struct RagdollState {
        JPH::Ref<JPH::Ragdoll> native;
        std::shared_ptr<const RagdollAsset> asset;
        std::shared_ptr<const ModelAsset> model;
        LocalModelPose basePose;
        Mat4 modelWorld,inverseWorld;
        std::vector<Mat4> residual,previous,current;
        EntityId owner{};
        CollisionFilter collision;
    };
    std::map<EntityId,RagdollState> ragdolls;
    JPH::Ref<JPH::SixDOFConstraint> drag;
    EntityId dragEntity{};std::size_t dragBody{};Vec3 dragOrigin{};
    std::uint32_t nextRagdollGroup{1};
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
    std::map<EntityId,EntityId> queryOwners;
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
    Impl(){world.Init(4096,0,4096,4096,layers,broadPairs,pairs);world.SetGravity({0,-9.81f,0});world.SetContactListener(this);}
    ~Impl(){endDrag();for(auto& [id,ragdoll]:ragdolls){(void)id;ragdoll.native->RemoveFromPhysicsSystem();}ragdolls.clear();characters.clear();for(const auto& [id,collider]:colliders){(void)id;world.GetBodyInterface().RemoveBody(collider.body);world.GetBodyInterface().DestroyBody(collider.body);}
        for(const auto& [id,collider]:meshColliders){(void)id;world.GetBodyInterface().RemoveBody(collider.body);world.GetBodyInterface().DestroyBody(collider.body);}}
    void report(EntityId id,const std::string& text){diagnostic+="Entity "+std::to_string(id)+": "+text+"\n";}
    void endDrag(){
        if(drag){
            world.RemoveConstraint(drag);drag=nullptr;
            // A body can sleep while the mouse constraint supports its weight.
            // Removing that support must wake the island so gravity resumes.
            if(const auto found=ragdolls.find(dragEntity);found!=ragdolls.end())found->second.native->Activate();
        }
        dragEntity=NoEntity;
    }
    bool suppressed(EntityId id) const {
        const auto known=queryOwners.find(id);const auto owner=known==queryOwners.end()?id:known->second;
        for(const auto& [model,ragdoll]:ragdolls)if(id==model || owner==ragdoll.owner)return true;
        return false;
    }
    CollisionFilter bodyFilter(EntityId id) const {
        if(const auto found=ragdolls.find(id);found!=ragdolls.end())return found->second.collision;
        if(const auto found=colliders.find(id);found!=colliders.end())return found->second.definition.collision;
        if(const auto found=meshColliders.find(id);found!=meshColliders.end())return found->second.collision;
        if(const auto found=characters.find(id);found!=characters.end())return found->second.collision;
        return {WorldCollisionLayer,AllCollisionLayers};
    }
    JPH::ValidateResult OnContactValidate(const JPH::Body& a,const JPH::Body& b,JPH::RVec3Arg,const JPH::CollideShapeResult&) override {
        const auto first=bodyFilter(a.GetUserData()),second=bodyFilter(b.GetUserData());
        return (first.mask&second.layers)&&(second.mask&first.layers)?JPH::ValidateResult::AcceptAllContactsForThisBodyPair:JPH::ValidateResult::RejectAllContactsForThisBodyPair;
    }
    bool OnContactValidate(const JPH::CharacterVirtual* character,const JPH::BodyID& id,const JPH::SubShapeID&) override {
        const auto mover=characters.find(character->GetUserData());if(mover==characters.end())return false;
        const JPH::BodyLockRead body(world.GetBodyLockInterface(),id);if(!body.Succeeded())return false;
        return (mover->second.collision.mask&bodyFilter(body.GetBody().GetUserData()).layers)!=0;
    }
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
        const auto known=queryOwners.find(id);const auto owner=known==queryOwners.end()?NoEntity:known->second;
        if(lock.Succeeded())targets.push_back({id,owner,collision,lock.GetBody().GetTransformedShape()});
    };
    for(const auto& [id,collider]:colliders)addBody(id,collider.body,collider.definition.collision);
    for(const auto& [id,collider]:meshColliders)addBody(id,collider.body,collider.collision);
    for(const auto& [id,ragdoll]:ragdolls)for(const auto& body:ragdoll.native->GetBodyIDs())addBody(id,body,ragdoll.collision);
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
bool PhysicsWorld::createRagdoll(EntityId id,std::shared_ptr<const RagdollAsset> asset,std::shared_ptr<const ModelAsset> model,
    const ModelPose& pose,const Mat4& modelWorld,const Vec3& velocity,const Vec3& impulse,EntityId actorOwner,CollisionFilter collision) {
    auto& state=*impl_;
    try {
        if(!id || !asset || !model)throw std::runtime_error("Ragdoll requires an entity, setup and source model.");
        validateRagdollSource(*asset,*model);
        validateModelAnimationData(*model);
        if(asset->bodies.empty())throw std::runtime_error("Ragdoll setup has no physical bodies.");
        if(!math::finite(velocity)||!math::finite(impulse))throw std::runtime_error("Ragdoll velocity and impulse must be finite.");
        const auto fields=math::affineTransformFields(modelWorld);
        if(!fields || fields->singular || fields->sheared || fields->scale[0]<=0 || fields->scale[1]<=0 || fields->scale[2]<=0 ||
            std::abs(fields->scale[0]-fields->scale[1])>2e-5f*fields->scale[0] || std::abs(fields->scale[0]-fields->scale[2])>2e-5f*fields->scale[0])
            throw std::runtime_error("Ragdoll requires a positive uniform model scale without shear or reflection.");
        if(pose.worldTransforms.size()!=model->nodes.size() || pose.localTransforms.size()!=model->nodes.size() || pose.nodeWeights.size()!=model->nodes.size())throw std::runtime_error("Ragdoll initial pose does not match its source skeleton.");
        for(const auto& local:pose.localTransforms)if(!math::isAffine(local))throw std::runtime_error("Ragdoll initial pose must contain finite affine transforms.");
        const float scale=fields->scale[0];const auto reference=ragdollReferenceTransforms(*model);
        Impl::RagdollState pending;pending.asset=std::move(asset);pending.model=std::move(model);pending.modelWorld=modelWorld;pending.inverseWorld=*math::inverse(modelWorld);
        pending.owner=actorOwner?actorOwner:id;pending.collision=characterFilter(collision);
        pending.basePose.nodes.resize(pending.model->nodes.size());pending.basePose.node_weights=pose.nodeWeights;
        for(std::size_t node=0;node<pending.basePose.nodes.size();++node)pending.basePose.nodes[node].matrix=pose.localTransforms[node];
        JPH::Ref<JPH::RagdollSettings> settings=new JPH::RagdollSettings;settings->mSkeleton=new JPH::Skeleton;
        JPH::Array<JPH::Mat44> referenceMatrices,currentMatrices;
        for(const auto& body:pending.asset->bodies) {
            const auto frame=rigidFrame(modelWorld*reference[body.node]);
            const auto initialFull=modelWorld*pose.worldTransforms[body.node],initial=rigidFrame(initialFull);
            const auto q=rotation(frame),initialQ=rotation(initial);
            settings->mSkeleton->AddJoint(std::to_string(body.node).c_str(),body.parent);
            JPH::ShapeSettings::ShapeResult shape;
            if(body.shape==RagdollShape::Box)shape=JPH::BoxShapeSettings(native(body.size*(scale*.5f)),std::min(.05f,scale*std::min({body.size[0],body.size[1],body.size[2]})*.05f)).Create();
            else if(body.shape==RagdollShape::Sphere || body.height<=2*body.radius+1e-6f)shape=JPH::SphereShapeSettings(body.radius*scale).Create();
            else shape=JPH::CapsuleShapeSettings((body.height*.5f-body.radius)*scale,body.radius*scale).Create();
            if(shape.HasError())throw std::runtime_error(shape.GetError().c_str());
            const JPH::Quat offsetRotation(body.rotation[0],body.rotation[1],body.rotation[2],body.rotation[3]);
            shape=JPH::RotatedTranslatedShapeSettings(native(body.position*scale),offsetRotation,shape.Get()).Create();
            if(shape.HasError())throw std::runtime_error(shape.GetError().c_str());
            JPH::RagdollSettings::Part part;part.SetShape(shape.Get());part.mPosition=position(frame);part.mRotation=q;
            part.mMotionType=JPH::EMotionType::Dynamic;part.mObjectLayer=DynamicLayer;part.mMotionQuality=JPH::EMotionQuality::LinearCast;
            part.mFriction=body.friction;part.mRestitution=body.restitution;part.mGravityFactor=pending.asset->gravityFactor;
            part.mOverrideMassProperties=JPH::EOverrideMassProperties::CalculateInertia;part.mMassPropertiesOverride.mMass=body.mass;
            if(body.parent>=0) {
                const auto jointFrame=frame*math::translation(body.joint.position*scale)*math::rotation(body.joint.rotation);
                auto* constraint=new JPH::SwingTwistConstraintSettings;
                constraint->mPosition1=constraint->mPosition2=position(jointFrame);
                constraint->mTwistAxis1=constraint->mTwistAxis2=native({jointFrame[0],jointFrame[1],jointFrame[2]});
                // Jolt constructs (twist, plane x twist, plane), so its plane
                // axis is our authored Z, preserving the complete XYZ frame.
                constraint->mPlaneAxis1=constraint->mPlaneAxis2=native({jointFrame[8],jointFrame[9],jointFrame[10]});
                constexpr auto radians=std::numbers::pi_v<float>/180;
                // Jolt's plane half-cone is rotation about constraint Y;
                // its normal half-cone is rotation about constraint Z.
                constraint->mPlaneHalfConeAngle=body.joint.swingY*radians;constraint->mNormalHalfConeAngle=body.joint.swingZ*radians;
                constraint->mTwistMinAngle=body.joint.twistMin*radians;constraint->mTwistMaxAngle=body.joint.twistMax*radians;constraint->mMaxFrictionTorque=body.joint.friction;
                part.mToParent=constraint;
            }
            settings->mParts.push_back(part);
            referenceMatrices.push_back(JPH::Mat44::sRotationTranslation(q,JPH::Vec3(position(frame))));
            currentMatrices.push_back(JPH::Mat44::sRotationTranslation(initialQ,JPH::Vec3(position(initial))));
            pending.current.push_back(initial);pending.residual.push_back(*math::inverse(initial)*initialFull);
        }
        settings->CalculateBodyIndexToConstraintIndex();settings->CalculateConstraintIndexToBodyIdxPair();settings->CalculateConstraintPriorities();
        settings->DisableParentChildCollisions(referenceMatrices.data(),.002f*scale);
        pending.native=settings->CreateRagdoll(state.nextRagdollGroup++,id,&state.world);
        if(!pending.native)throw std::runtime_error("Physics body capacity exceeded; ragdoll was not created.");
        pending.native->SetPose(JPH::RVec3::sZero(),currentMatrices.data());pending.native->SetLinearVelocity(native(velocity));pending.previous=pending.current;
        removeRagdoll(id);
        state.ragdolls.emplace(id,std::move(pending));auto& accepted=state.ragdolls.at(id);state.queryOwners[id]=accepted.owner;
        accepted.native->AddToPhysicsSystem(JPH::EActivation::Activate);
        // Retire the animated actor's collision immediately. The next reconcile
        // keeps it suppressed while the physical skeleton owns the pose.
        std::erase_if(state.characters,[&](const auto& entry){return state.suppressed(entry.first);});
        for(auto it=state.colliders.begin();it!=state.colliders.end();) {if(state.suppressed(it->first)){auto remove=it++;state.eraseCollider(remove);}else ++it;}
        for(auto it=state.meshColliders.begin();it!=state.meshColliders.end();) {
            if(!state.suppressed(it->first)){++it;continue;}state.world.GetBodyInterface().RemoveBody(it->second.body);state.world.GetBodyInterface().DestroyBody(it->second.body);it=state.meshColliders.erase(it);
        }
        state.initial.insert(id);cancelCharacterInput(accepted.owner);
        if(math::length(impulse)>0)applyRagdollImpulse(id,impulse);
        return true;
    } catch(const std::exception& error){state.report(id,error.what());return false;}
}
void PhysicsWorld::removeRagdoll(EntityId id) {
    auto& state=*impl_;const auto found=state.ragdolls.find(id);if(found==state.ragdolls.end())return;
    if(state.dragEntity==id)state.endDrag();found->second.native->RemoveFromPhysicsSystem();state.ragdolls.erase(found);state.removed.insert(id);
}
bool PhysicsWorld::hasRagdoll(EntityId id) const{return impl_->ragdolls.contains(id);}
std::size_t PhysicsWorld::ragdollCount() const{return impl_->ragdolls.size();}
bool PhysicsWorld::ragdollActive(EntityId id) const{const auto found=impl_->ragdolls.find(id);return found!=impl_->ragdolls.end()&&found->second.native->IsActive();}
std::vector<Mat4> PhysicsWorld::ragdollBodyTransforms(EntityId id,float alpha) const {
    const auto found=impl_->ragdolls.find(id);if(found==impl_->ragdolls.end())return {};
    alpha=std::isfinite(alpha)?std::clamp(alpha,0.f,1.f):1;
    const auto& ragdoll=found->second;std::vector<Mat4> result;result.reserve(ragdoll.current.size());
    for(std::size_t body=0;body<ragdoll.current.size();++body) {
        const auto& previous=ragdoll.previous[body];const auto& current=ragdoll.current[body];
        result.push_back(ownMatrix(position(previous)+(position(current)-position(previous))*alpha,rotation(previous).SLERP(rotation(current),alpha)));
    }return result;
}
std::shared_ptr<const ModelPose> PhysicsWorld::sampleRagdollPose(EntityId id,float alpha) const {
    const auto found=impl_->ragdolls.find(id);if(found==impl_->ragdolls.end())return {};
    const auto& ragdoll=found->second;const auto frames=ragdollBodyTransforms(id,alpha);auto local=ragdoll.basePose;
    std::vector<Mat4> world(local.nodes.size());std::vector<int> physical(local.nodes.size(),-1);std::vector<bool> visited(local.nodes.size());
    for(std::size_t body=0;body<ragdoll.asset->bodies.size();++body)physical[ragdoll.asset->bodies[body].node]=static_cast<int>(body);
    std::function<void(std::size_t)> resolve=[&](std::size_t node) {
        if(visited[node])return;const auto parent=ragdoll.model->nodes[node].parent;
        if(parent>=0)resolve(static_cast<std::size_t>(parent));
        if(physical[node]>=0) {
            const auto body=static_cast<std::size_t>(physical[node]);world[node]=ragdoll.inverseWorld*frames[body]*ragdoll.residual[body];
            local.nodes[node].matrix=parent>=0?*math::inverse(world[parent])*world[node]:world[node];
        } else world[node]=parent>=0?world[parent]*local.nodes[node].matrix:local.nodes[node].matrix;
        visited[node]=true;
    };
    for(std::size_t node=0;node<world.size();++node)resolve(node);
    return std::make_shared<const ModelPose>(finalize_model_pose(*ragdoll.model,local));
}
bool PhysicsWorld::applyRagdollImpulse(EntityId id,const Vec3& impulse,std::optional<Vec3> point,int bodyIndex) {
    const auto found=impl_->ragdolls.find(id);if(found==impl_->ragdolls.end()||!math::finite(impulse)||(point&&!math::finite(*point)))return false;
    auto& ragdoll=found->second;const auto count=ragdoll.native->GetBodyCount();
    if(bodyIndex<-1 || bodyIndex>=static_cast<int>(count))return false;
    if(bodyIndex<0 && point) {
        double nearest=std::numeric_limits<double>::infinity();
        for(std::size_t body=0;body<count;++body){const auto distance=math::length(own(impl_->world.GetBodyInterface().GetCenterOfMassPosition(ragdoll.native->GetBodyID(static_cast<int>(body))))-*point);if(distance<nearest){nearest=distance;bodyIndex=static_cast<int>(body);}}
    }
    if(bodyIndex<0) {
        // One authored impulse is a total impulse for the character, distributed
        // by mass. Applying the full impulse to every bone multiplies its energy.
        float mass=0;for(const auto& body:ragdoll.asset->bodies)mass+=body.mass;
        for(std::size_t body=0;body<count;++body)impl_->world.GetBodyInterface().AddImpulse(ragdoll.native->GetBodyID(static_cast<int>(body)),native(impulse*(ragdoll.asset->bodies[body].mass/mass)));
    } else if(point)impl_->world.GetBodyInterface().AddImpulse(ragdoll.native->GetBodyID(bodyIndex),native(impulse),JPH::RVec3((*point)[0],(*point)[1],(*point)[2]));
    else impl_->world.GetBodyInterface().AddImpulse(ragdoll.native->GetBodyID(bodyIndex),native(impulse));
    return true;
}
std::optional<RagdollBodyHit> PhysicsWorld::pickRagdoll(const Ray& ray,float maxDistance) const {
    if(!math::finite(ray.origin)||!math::finite(ray.direction)||!std::isfinite(maxDistance)||maxDistance<=0||math::length(ray.direction)<=1e-8)return {};
    const auto direction=math::normalized(ray.direction),displacement=direction*maxDistance;
    if(!math::finite(displacement)||!math::finite(ray.origin+displacement))return {};
    const JPH::RRayCast query(JPH::RVec3(ray.origin[0],ray.origin[1],ray.origin[2]),native(displacement));
    JPH::RayCastSettings settings;JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
    impl_->world.GetNarrowPhaseQuery().CastRay(query,settings,collector,JPH::SpecifiedBroadPhaseLayerFilter(JPH::BroadPhaseLayer(DynamicLayer)),JPH::SpecifiedObjectLayerFilter(DynamicLayer));
    if(!collector.HadHit())return {};const auto& hit=collector.mHit;
    const JPH::BodyLockRead lock(impl_->world.GetBodyLockInterface(),hit.mBodyID);if(!lock.Succeeded())return {};
    const auto id=lock.GetBody().GetUserData();const auto found=impl_->ragdolls.find(id);if(found==impl_->ragdolls.end())return {};
    const auto& bodies=found->second.native->GetBodyIDs();const auto body=std::find(bodies.begin(),bodies.end(),hit.mBodyID);if(body==bodies.end())return {};
    const auto point=query.GetPointOnRay(hit.mFraction);const auto normal=hit.mFraction==0?native(direction*-1):lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2,point);
    return RagdollBodyHit{id,static_cast<std::size_t>(body-bodies.begin()),own(point),own(normal),hit.mFraction*maxDistance};
}
bool PhysicsWorld::beginRagdollDrag(EntityId id,std::size_t body,const Vec3& point) {
    endRagdollDrag();const auto found=impl_->ragdolls.find(id);if(found==impl_->ragdolls.end()||body>=found->second.native->GetBodyCount()||!math::finite(point))return false;
    const auto bodyId=found->second.native->GetBodyID(static_cast<int>(body));
    {JPH::BodyLockWrite lock(impl_->world.GetBodyLockInterface(),bodyId);if(!lock.Succeeded())return false;
        JPH::SixDOFConstraintSettings settings;settings.mPosition1=settings.mPosition2=JPH::RVec3(point[0],point[1],point[2]);
        float totalMass=0;for(const auto& part:found->second.asset->bodies)totalMass+=part.mass;
        for(int axis=0;axis<3;++axis)settings.mMotorSettings[axis]=JPH::MotorSettings(6.f,1.f,std::max(200.f,totalMass*100.f),0);
        impl_->drag=static_cast<JPH::SixDOFConstraint*>(settings.Create(JPH::Body::sFixedToWorld,lock.GetBody()));
    }
    for(int axis=0;axis<3;++axis)impl_->drag->SetMotorState(static_cast<JPH::SixDOFConstraint::EAxis>(axis),JPH::EMotorState::Position);
    impl_->world.AddConstraint(impl_->drag);impl_->dragEntity=id;impl_->dragBody=body;impl_->dragOrigin=point;found->second.native->Activate();return true;
}
bool PhysicsWorld::updateRagdollDrag(const Vec3& target) {
    if(!impl_->drag||!math::finite(target))return false;
    // Bound a single mouse target to avoid nonfinite solver impulses after a
    // lost capture or invalid unprojection; this changes a target, never a pose.
    auto offset=target-impl_->dragOrigin;if(!math::finite(offset))return false;const auto distance=math::length(offset);if(distance>100)offset=offset*(100/distance);
    impl_->drag->SetTargetPositionCS(native(offset));const auto found=impl_->ragdolls.find(impl_->dragEntity);if(found!=impl_->ragdolls.end())found->second.native->Activate();return true;
}
void PhysicsWorld::endRagdollDrag(){impl_->endDrag();}
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
    state.queryOwners.clear();
    std::unordered_map<EntityId,const EntityRecord*> recordById;recordById.reserve(records.size());
    for(const auto& record:records)recordById.emplace(record.id,&record);
    for(const auto& record:records) {
        auto* current=&record;EntityId owner=record.id;
        while(current) {
            if(current->actor || current->controller){owner=current->id;break;}
            const auto parent=recordById.find(current->parent);current=parent==recordById.end()?nullptr:parent->second;
        }
        state.queryOwners[record.id]=owner;
    }
    for(const auto& [id,ragdoll]:state.ragdolls)state.queryOwners[id]=ragdoll.owner;
    for(const auto& record:records) {
        const auto world=scene.worldTransform(record.id);if(!world)continue;
        if(record.collider && record.collider->shape!=ColliderShape::Trimesh) {
            if(state.suppressed(record.id))continue;
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
        if(state.suppressed(record.id))continue;
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
        if(!record.controller || state.suppressed(record.id))continue;const auto world=scene.worldTransform(record.id);if(!world)continue;
        std::string error;
        if(underCharacter(scene,record.id)){state.report(record.id,"A Character cannot be nested beneath another Character.");continue;}
        auto found=state.characters.find(record.id);
        if(found!=state.characters.end()&&found->second.collision!=characterFilter(record.controller->collision)) {
            found->second.collision=characterFilter(record.controller->collision);state.configured.insert(record.id);charactersChanged=true;
            const auto body=found->second.motor->GetInnerBodyID();if(!body.IsInvalid())state.world.GetBodyInterface().InvalidateContactCache(body);
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
            settings.mMaxStrength=0;settings.mInnerBodyShape=shifted.Get();settings.mInnerBodyLayer=CharacterLayer;
            Impl::Character character;character.definition=definition;character.profile=record.controller->profile.id;character.world=*world;character.collision=characterFilter(record.controller->collision);
            character.motor=new JPH::CharacterVirtual(&settings,position(*world),rotation(*world),record.id,&state.world);
            if(character.motor->GetInnerBodyID().IsInvalid())state.report(record.id,"Physics body capacity exceeded; Character dynamic collision is unavailable.");
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
    CRIMSON_PROFILE_ZONE("Physics and character movement");
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
    if(!state.ragdolls.empty()) {
        for(auto& [id,ragdoll]:state.ragdolls){(void)id;ragdoll.previous=ragdoll.current;}
        const auto error=state.world.Update(seconds,1,&state.allocator,&state.jobs);
        if(error!=JPH::EPhysicsUpdateError::None)state.diagnostic+="Ragdoll physics exhausted a bounded collision capacity.\n";
        for(auto& [id,ragdoll]:state.ragdolls)for(std::size_t body=0;body<ragdoll.current.size();++body) {
            JPH::RVec3 point;JPH::Quat q;state.world.GetBodyInterface().GetPositionAndRotation(ragdoll.native->GetBodyID(static_cast<int>(body)),point,q);
            const auto matrix=ownMatrix(point,q);
            if(math::finite(matrix))ragdoll.current[body]=matrix;
            else state.report(id,"Ragdoll produced a nonfinite pose; retaining its last finite presentation.");
        }
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
PhysicsTraceResult PhysicsWorld::trace(const PhysicsTrace& request,const PhysicsQueryFilter& filter,PhysicsTraceMode mode) const {
    CRIMSON_PROFILE_ZONE("PhysicsWorld::trace");
    PhysicsTraceResult result;
    if(mode!=PhysicsTraceMode::Any && mode!=PhysicsTraceMode::Closest && mode!=PhysicsTraceMode::All)return result;
    if(!math::finite(request.ray.origin) || !math::finite(request.ray.direction) ||
       !std::isfinite(request.maxDistance) || request.maxDistance<=0)return result;
    const auto directionLength=math::length(request.ray.direction);
    if(!std::isfinite(directionLength) || directionLength<=1e-8f)return result;
    const auto direction=request.ray.direction/directionLength;
    const auto displacement=direction*request.maxDistance;
    if(!math::finite(displacement) || !math::finite(request.ray.origin+displacement))return result;
    const auto origin=JPH::RVec3(request.ray.origin[0],request.ray.origin[1],request.ray.origin[2]);
    JPH::ShapeSettings::ShapeResult shapeResult;
    JPH::Quat orientation=JPH::Quat::sIdentity();
    if(request.shape!=PhysicsTraceShape::Line) {
        if(!math::finite(request.orientation))return result;
        double lengthSquared=0;for(const auto value:request.orientation)lengthSquared+=static_cast<double>(value)*value;
        if(lengthSquared<=1e-16 || !std::isfinite(lengthSquared))return result;
        const auto reciprocal=1/std::sqrt(lengthSquared);
        orientation=JPH::Quat(static_cast<float>(request.orientation[0]*reciprocal),static_cast<float>(request.orientation[1]*reciprocal),
            static_cast<float>(request.orientation[2]*reciprocal),static_cast<float>(request.orientation[3]*reciprocal));
        switch(request.shape) {
        case PhysicsTraceShape::Sphere:
            if(!std::isfinite(request.radius) || request.radius<=1e-6f)return result;
            shapeResult=JPH::SphereShapeSettings(request.radius).Create();break;
        case PhysicsTraceShape::Box:
            if(!math::finite(request.halfExtents) || request.halfExtents[0]<=1e-6f || request.halfExtents[1]<=1e-6f || request.halfExtents[2]<=1e-6f)return result;
            // Zero convex radius keeps authored box dimensions exact.
            shapeResult=JPH::BoxShapeSettings(native(request.halfExtents),0).Create();break;
        case PhysicsTraceShape::Capsule:
            if(!std::isfinite(request.radius) || request.radius<=1e-6f || !std::isfinite(request.capsuleHalfHeight) || request.capsuleHalfHeight<0)return result;
            shapeResult=request.capsuleHalfHeight<=1e-6f?JPH::SphereShapeSettings(request.radius).Create():
                JPH::CapsuleShapeSettings(request.capsuleHalfHeight,request.radius).Create();break;
        default:return result;
        }
        if(shapeResult.HasError())return result;
    }
    result.valid=true;
    if(!filter.mask)return result;
    const auto owner=[&](EntityId id){const auto found=impl_->queryOwners.find(id);return found==impl_->queryOwners.end()?id:found->second;};
    const auto allowed=[&](EntityId id,std::uint32_t layers) {
        if(!(filter.mask&layers))return false;
        const auto actor=owner(id);
        return std::find(filter.ignored.begin(),filter.ignored.end(),id)==filter.ignored.end() &&
            std::find(filter.ignored.begin(),filter.ignored.end(),actor)==filter.ignored.end();
    };
    const auto earlier=[](const PhysicsTraceHit& a,const PhysicsTraceHit& b) {
        if(a.fraction!=b.fraction)return a.fraction<b.fraction;
        if(a.entity!=b.entity)return a.entity<b.entity;
        return a.subshape<b.subshape;
    };
    std::map<EntityId,PhysicsTraceHit> entries;
    const auto remember=[&](PhysicsTraceHit hit) {
        if(!std::isfinite(hit.fraction) || hit.fraction<0 || hit.fraction>1 || !math::finite(hit.position))return false;
        if(!math::finite(hit.normal) || math::length(hit.normal)<=1e-8f)hit.normal=direction*-1;
        else hit.normal=math::normalized(hit.normal);
        hit.owner=owner(hit.entity);hit.distance=hit.fraction*request.maxDistance;
        hit.center=request.ray.origin+displacement*hit.fraction;
        if(mode==PhysicsTraceMode::All) {
            const auto found=entries.find(hit.entity);
            if(found==entries.end())entries.emplace(hit.entity,hit);
            else if(earlier(hit,found->second))found->second=hit;
        } else if(result.hits.empty())result.hits.push_back(hit);
        else if(earlier(hit,result.hits.front()))result.hits.front()=hit;
        return true;
    };
    class QueryBodies final:public JPH::BodyFilter {
    public:
        std::function<bool(EntityId)> accepts;
        bool ShouldCollideLocked(const JPH::Body& body) const override{return accepts(body.GetUserData());}
    } bodies;
    bodies.accepts=[&](EntityId id) {
        if(const auto found=impl_->ragdolls.find(id);found!=impl_->ragdolls.end())return allowed(id,found->second.collision.layers);
        if(const auto found=impl_->colliders.find(id);found!=impl_->colliders.end())return allowed(id,found->second.definition.collision.layers);
        if(const auto found=impl_->meshColliders.find(id);found!=impl_->meshColliders.end())return allowed(id,found->second.collision.layers);
        return false;
    };
    const JPH::BroadPhaseLayerFilter broad;
    const JPH::ObjectLayerFilter layer;
    const auto queryVirtuals=[&](auto&& cast) {
        if(filter.includeCharacters)for(const auto& [id,character]:impl_->characters) {
            if(!allowed(id,character.collision.layers))continue;
            cast(id,character.motor->GetTransformedShape());
            if(mode==PhysicsTraceMode::Any && !result.hits.empty())return;
        }
        if(filter.includeTriggers)for(const auto& [id,trigger]:impl_->triggers) {
            if(!allowed(id,trigger.definition.collision.layers))continue;
            const auto fields=math::affineTransformFields(trigger.world);if(!fields)continue;
            JPH::TransformedShape shape;shape.mShape=trigger.shape;
            shape.SetWorldTransform(position(trigger.world),rotation(fields->rotation),JPH::Vec3::sOne());
            cast(id,shape);
            if(mode==PhysicsTraceMode::Any && !result.hits.empty())return;
        }
    };
    if(request.shape==PhysicsTraceShape::Line) {
        const JPH::RRayCast ray(origin,native(displacement));
        JPH::RayCastSettings settings;settings.mBackFaceModeTriangles=JPH::EBackFaceMode::CollideWithBackFaces;
        TraceCollector<JPH::CastRayCollector> collector;collector.mode=mode;
        collector.accept=[&](EntityId id,const JPH::TransformedShape* shape,const JPH::RayCastResult& hit) {
            if(!shape)return false;
            PhysicsTraceHit contact;contact.entity=id;contact.fraction=hit.mFraction;
            contact.startedInside=hit.mFraction==0;contact.subshape=hit.mSubShapeID2.GetValue();
            contact.position=own(ray.GetPointOnRay(hit.mFraction));
            // An interior point has no unique surface normal (a sphere's center
            // would otherwise normalize zero inside Jolt). Publish a stable one.
            if(contact.startedInside)contact.normal=direction*-1;
            else {const auto normal=shape->GetWorldSpaceSurfaceNormal(hit.mSubShapeID2,ray.GetPointOnRay(hit.mFraction));contact.normal={normal.GetX(),normal.GetY(),normal.GetZ()};}
            return remember(contact);
        };
        if(filter.includeWorld)impl_->world.GetNarrowPhaseQuery().CastRay(ray,settings,collector,broad,layer,bodies);
        if(mode==PhysicsTraceMode::Any && !result.hits.empty())return result;
        queryVirtuals([&](EntityId id,const JPH::TransformedShape& shape) {
            auto direct=collector;direct.entity=id;direct.mode=mode==PhysicsTraceMode::Any?mode:PhysicsTraceMode::Closest;
            direct.Reset();shape.CastRay(ray,settings,direct);
        });
    } else {
        const JPH::RShapeCast cast(shapeResult.Get(),JPH::Vec3::sOne(),JPH::RMat44::sRotationTranslation(orientation,origin),native(displacement));
        JPH::ShapeCastSettings settings;settings.SetBackFaceMode(JPH::EBackFaceMode::CollideWithBackFaces);settings.mReturnDeepestPoint=true;
        TraceCollector<JPH::CastShapeCollector> collector;collector.mode=mode;
        collector.accept=[&](EntityId id,const JPH::TransformedShape*,const JPH::ShapeCastResult& hit) {
            PhysicsTraceHit contact;contact.entity=id;contact.fraction=hit.mFraction;
            contact.startedInside=hit.mFraction==0;contact.subshape=hit.mSubShapeID2.GetValue();
            contact.position=request.ray.origin+Vec3{hit.mContactPointOn2.GetX(),hit.mContactPointOn2.GetY(),hit.mContactPointOn2.GetZ()};
            contact.normal={-hit.mPenetrationAxis.GetX(),-hit.mPenetrationAxis.GetY(),-hit.mPenetrationAxis.GetZ()};
            return remember(contact);
        };
        if(filter.includeWorld)impl_->world.GetNarrowPhaseQuery().CastShape(cast,settings,origin,collector,broad,layer,bodies);
        if(mode==PhysicsTraceMode::Any && !result.hits.empty())return result;
        queryVirtuals([&](EntityId id,const JPH::TransformedShape& shape) {
            auto direct=collector;direct.entity=id;direct.mode=mode==PhysicsTraceMode::Any?mode:PhysicsTraceMode::Closest;
            direct.Reset();shape.CastShape(cast,settings,origin,direct);
        });
    }
    if(mode==PhysicsTraceMode::All) {
        result.hits.reserve(entries.size());for(const auto& [id,hit]:entries){(void)id;result.hits.push_back(hit);}
        std::sort(result.hits.begin(),result.hits.end(),earlier);
    }
    return result;
}
std::optional<PhysicsRayHit> PhysicsWorld::raycast(const Ray& ray,float maxDistance) const {
    PhysicsQueryFilter filter;filter.includeCharacters=false;return raycast(ray,maxDistance,filter);
}
std::optional<PhysicsRayHit> PhysicsWorld::raycast(const Ray& ray,float maxDistance,const PhysicsQueryFilter& filter) const {
    PhysicsTrace request;request.ray=ray;request.maxDistance=maxDistance;
    const auto result=trace(request,filter);
    if(result.hits.empty())return {};
    const auto& hit=result.hits.front();return PhysicsRayHit{hit.entity,hit.position,hit.normal};
}
}
