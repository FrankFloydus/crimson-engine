#include "PhysicsWorld.h"
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <mutex>
#include <numbers>
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
    while(entity && entity->parent){entity=scene.find(entity->parent);if(entity && entity->character)return true;}
    return false;
}
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
        if(entity->camera && entity->camera->followTarget)pending.push_back(entity->camera->followTarget);
    }
    return false;
}
bool sameCharacter(const SceneCharacter& a,const SceneCharacter& b){return a.height==b.height && a.radius==b.radius;}
bool validCharacter(const Mat4& world,const SceneCharacter& character,std::string& error) {
    const auto fields=math::affineTransformFields(world);
    if(!fields || fields->singular || fields->sheared){error="Character requires a nonsingular transform without shear.";return false;}
    for(int i=0;i<3;++i)if(std::abs(fields->scale[i]-1.f)>2e-5f){error="Character requires unit world scale; resize its capsule or scale a visual child.";return false;}
    if(std::abs(world[4])>2e-5f || std::abs(world[5]-1.f)>2e-5f || std::abs(world[6])>2e-5f){error="Character must remain upright; rotate visual children for alignment.";return false;}
    if(!math::finite(character.radius) || !math::finite(character.height) || character.radius<=0 || character.height<2*character.radius){error="Character height must be at least twice its positive radius.";return false;}
    return true;
}
}
struct PhysicsWorld::Impl {
    // Lifetime is declared first so every Jolt resource is released before it.
    std::shared_ptr<JoltLifetime> lifetime{acquireJolt()};
    Layers layers;Pairs pairs;BroadPairs broadPairs;
    JPH::PhysicsSystem world;
    JPH::TempAllocatorImpl allocator{4*1024*1024};
    struct Collider {JPH::BodyID body;Mat4 world;Vec3 size;};
    struct Character {JPH::Ref<JPH::CharacterVirtual> motor;SceneCharacter definition;Mat4 world;bool locked{};};
    std::map<EntityId,Collider> colliders;
    std::map<EntityId,Character> characters;
    std::map<EntityId,Vec3> desiredVelocities;
    std::map<EntityId,Vec3> desiredFacing;
    std::string diagnostic;
    std::uint64_t geometryRevision{};
    Impl(){world.Init(4096,0,1024,1024,layers,broadPairs,pairs);world.SetGravity({0,-9.81f,0});}
    ~Impl(){characters.clear();for(const auto& [id,collider]:colliders){(void)id;world.GetBodyInterface().RemoveBody(collider.body);world.GetBodyInterface().DestroyBody(collider.body);}}
    void report(EntityId id,const std::string& text){diagnostic+="Entity "+std::to_string(id)+": "+text+"\n";}
    void refresh(JPH::CharacterVirtual& motor) {
        motor.RefreshContacts(world.GetDefaultBroadPhaseLayerFilter(CharacterLayer),world.GetDefaultLayerFilter(CharacterLayer),{},{},allocator);
    }
    void eraseCollider(std::map<EntityId,Collider>::iterator iterator) {
        world.GetBodyInterface().RemoveBody(iterator->second.body);world.GetBodyInterface().DestroyBody(iterator->second.body);colliders.erase(iterator);
    }
};
PhysicsWorld::PhysicsWorld():impl_(std::make_unique<Impl>()){}
PhysicsWorld::~PhysicsWorld()=default;
void PhysicsWorld::reconcile(const Scene& scene,std::span<const EntityId> locks) {
    auto& state=*impl_;state.diagnostic.clear();
    bool collidersChanged=false;
    std::unordered_set<EntityId> keptColliders,keptCharacters;
    const auto records=scene.records();
    for(const auto& record:records) {
        const auto world=scene.worldTransform(record.id);if(!world)continue;
        if(record.collider) {
            const auto fields=math::affineTransformFields(*world);const auto& size=record.collider->size;
            if(underCharacter(scene,record.id) || record.character){state.report(record.id,"Static Collider cannot be attached beneath a Character; the Character capsule supplies its collision.");continue;}
            if(!fields || fields->singular || fields->sheared){state.report(record.id,"Box Collider is disabled: its world transform is collapsed or sheared.");continue;}
            Vec3 half{};bool valid=math::finite(size);
            for(int i=0;i<3;++i){half[i]=.5f*size[i]*std::abs(fields->scale[i]);valid=valid && size[i]>0 && math::finite(half[i]) && half[i]>1e-6f;}
            if(!valid){state.report(record.id,"Box Collider requires positive finite dimensions and nonzero world scale.");continue;}
            auto found=state.colliders.find(record.id);
            if(found!=state.colliders.end() && found->second.size==size && close(found->second.world,*world)){keptColliders.insert(record.id);continue;}
            const float convexRadius=std::min(.05f,std::min({half[0],half[1],half[2]})*.1f);
            const auto shape=JPH::BoxShapeSettings(native(half),convexRadius).Create();
            if(shape.HasError()){state.report(record.id,shape.GetError().c_str());continue;}
            if(found!=state.colliders.end())state.eraseCollider(found);
            collidersChanged=true;
            JPH::BodyCreationSettings settings(shape.Get(),position(*world),rotation(fields->rotation),JPH::EMotionType::Static,StaticLayer);
            settings.mUserData=record.id;
            const auto body=state.world.GetBodyInterface().CreateAndAddBody(settings,JPH::EActivation::DontActivate);
            if(body.IsInvalid()){state.report(record.id,"Physics body capacity exceeded; Box Collider is disabled.");continue;}
            state.colliders.emplace(record.id,Impl::Collider{body,*world,size});keptColliders.insert(record.id);
        }
    }
    for(auto iterator=state.colliders.begin();iterator!=state.colliders.end();) {
        if(keptColliders.contains(iterator->first)){++iterator;continue;}auto remove=iterator++;state.eraseCollider(remove);collidersChanged=true;
    }
    if(collidersChanged)++state.geometryRevision;
    for(const auto& record:records) {
        if(!record.character)continue;const auto world=scene.worldTransform(record.id);if(!world)continue;
        std::string error;
        if(underCharacter(scene,record.id)){state.report(record.id,"A Character cannot be nested beneath another Character.");continue;}
        if(!validCharacter(*world,*record.character,error)){state.report(record.id,error);continue;}
        auto found=state.characters.find(record.id);
        if(found!=state.characters.end() && !sameCharacter(found->second.definition,*record.character)){state.characters.erase(found);found=state.characters.end();}
        if(found==state.characters.end()) {
            const auto& definition=*record.character;
            JPH::ShapeSettings::ShapeResult capsule;
            const float cylinderHalfHeight=(definition.height-2*definition.radius)*.5f;
            if(cylinderHalfHeight<=1e-6f)capsule=JPH::SphereShapeSettings(definition.radius).Create();
            else capsule=JPH::CapsuleShapeSettings(cylinderHalfHeight,definition.radius).Create();
            if(capsule.HasError()){state.report(record.id,capsule.GetError().c_str());continue;}
            const auto shifted=JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0,definition.height*.5f,0),JPH::Quat::sIdentity(),capsule.Get()).Create();
            if(shifted.HasError()){state.report(record.id,shifted.GetError().c_str());continue;}
            JPH::CharacterVirtualSettings settings;settings.mShape=shifted.Get();settings.mMaxSlopeAngle=std::numbers::pi_v<float>*.25f;
            settings.mSupportingVolume=JPH::Plane(JPH::Vec3::sAxisY(),-definition.radius);
            settings.mMaxStrength=0;settings.mInnerBodyShape=nullptr;
            Impl::Character character;character.definition=definition;character.world=*world;
            character.motor=new JPH::CharacterVirtual(&settings,position(*world),rotation(*world),record.id,&state.world);
            state.refresh(*character.motor);
            found=state.characters.emplace(record.id,std::move(character)).first;
        }
        auto& character=found->second;const bool editing=locked(scene,record.id,locks);
        if(!close(character.world,*world) || character.locked!=editing) {
            character.motor->SetPosition(position(*world));character.motor->SetRotation(rotation(*world));
            character.motor->SetLinearVelocity(JPH::Vec3::sZero());state.refresh(*character.motor);character.world=*world;
        } else if(collidersChanged)state.refresh(*character.motor);
        character.locked=editing;character.definition=*record.character;keptCharacters.insert(record.id);
    }
    std::erase_if(state.characters,[&](const auto& entry){return !keptCharacters.contains(entry.first);});
}
std::vector<ScenePoseUpdate> PhysicsWorld::step(const Scene& scene,EntityId controlled,const Vec3& desired,float seconds,std::span<const EntityId> locks) {
    reconcile(scene,locks);std::vector<ScenePoseUpdate> poses;
    if(!std::isfinite(seconds) || seconds<=0)return poses;auto& state=*impl_;
    auto movement=std::move(state.desiredVelocities);state.desiredVelocities.clear();
    auto facing=std::move(state.desiredFacing);state.desiredFacing.clear();
    if(controlled!=NoEntity)movement[controlled]=math::finite(desired)?desired:Vec3{};
    JPH::CharacterVirtual::ExtendedUpdateSettings update;update.mWalkStairsStepUp={0,.3f,0};update.mStickToFloorStepDown={0,-.5f,0};
    for(auto& [id,character]:state.characters) {
        if(character.locked)continue;
        auto& motor=*character.motor;motor.UpdateGroundVelocity();
        const auto current=motor.GetLinearVelocity();
        float vertical=current.GetY();
        if(motor.GetGroundState()==JPH::CharacterBase::EGroundState::OnGround && vertical<=.1f)vertical=0;
        vertical-=9.81f*seconds;
        const auto requested=movement.find(id);
        const Vec3 horizontal=requested!=movement.end()?Vec3{requested->second[0],0,requested->second[2]}:Vec3{};
        const auto requestedFacing=facing.find(id);
        const auto heading=requestedFacing!=facing.end()?Vec3{requestedFacing->second[0],0,requestedFacing->second[2]}:horizontal;
        if(math::length(heading)>1e-6) {
            const auto yaw=std::atan2(-heading[0],-heading[2]);
            const auto rotationMatrix=math::rotation(math::axisAngle({0,1,0},yaw));
            for(int column=0;column<3;++column)for(int row=0;row<3;++row)character.world.at(row,column)=rotationMatrix.at(row,column);
            motor.SetRotation(rotation(character.world));
        }
        motor.SetLinearVelocity(native(horizontal)+JPH::Vec3(0,vertical,0));
        motor.ExtendedUpdate(seconds,state.world.GetGravity(),update,state.world.GetDefaultBroadPhaseLayerFilter(CharacterLayer),
            state.world.GetDefaultLayerFilter(CharacterLayer),{},{},state.allocator);
        const auto resolved=own(motor.GetPosition());
        if(!math::finite(resolved)){state.report(id,"Character produced a nonfinite pose; movement was discarded.");motor.SetPosition(position(character.world));motor.SetLinearVelocity(JPH::Vec3::sZero());continue;}
        auto world=character.world;world[12]=resolved[0];world[13]=resolved[1];world[14]=resolved[2];
        const auto record=scene.find(id);if(!record)continue;
        Mat4 local=world;
        if(record->parent) {
            const auto parent=scene.worldTransform(record->parent);const auto inverse=parent?math::inverse(*parent):std::nullopt;
            if(!inverse){state.report(id,"Character parent transform is not invertible.");continue;}local=*inverse*world;
        }
        poses.push_back({id,local});character.world=world;
    }
    return poses;
}
const std::string& PhysicsWorld::status() const{return impl_->diagnostic;}
void PhysicsWorld::requestMovement(EntityId id,const Vec3& desired){if(id)impl_->desiredVelocities[id]=math::finite(desired)?desired:Vec3{};}
void PhysicsWorld::requestFacing(EntityId id,const Vec3& direction){if(id)impl_->desiredFacing[id]=math::finite(direction)?direction:Vec3{};}
std::size_t PhysicsWorld::colliderCount() const{return impl_->colliders.size();}
std::uint64_t PhysicsWorld::geometryRevision() const{return impl_->geometryRevision;}
std::vector<PhysicsBox> PhysicsWorld::staticBoxes() const {
    std::vector<PhysicsBox> boxes;boxes.reserve(impl_->colliders.size());
    for(const auto& [id,collider]:impl_->colliders)boxes.push_back({id,collider.world,collider.size});
    return boxes;
}
std::size_t PhysicsWorld::characterCount() const{return impl_->characters.size();}
std::optional<Vec3> PhysicsWorld::characterPosition(EntityId id) const{const auto found=impl_->characters.find(id);return found==impl_->characters.end()?std::nullopt:std::optional{own(found->second.motor->GetPosition())};}
bool PhysicsWorld::grounded(EntityId id) const{const auto found=impl_->characters.find(id);return found!=impl_->characters.end() && found->second.motor->GetGroundState()==JPH::CharacterBase::EGroundState::OnGround;}
std::optional<PhysicsRayHit> PhysicsWorld::raycast(const Ray& ray,float maxDistance) const {
    if(!math::finite(ray.origin) || !math::finite(ray.direction) || !std::isfinite(maxDistance) || maxDistance<=0)return {};
    const auto length=math::length(ray.direction);if(length<=1e-8)return {};
    const auto displacement=ray.direction*(static_cast<double>(maxDistance)/length);
    if(!math::finite(displacement) || !math::finite(ray.origin+displacement))return {};
    const JPH::RRayCast query(JPH::RVec3(ray.origin[0],ray.origin[1],ray.origin[2]),native(displacement));
    JPH::RayCastResult result;
    auto& world=impl_->world;
    if(!world.GetNarrowPhaseQuery().CastRay(query,result,JPH::SpecifiedBroadPhaseLayerFilter(JPH::BroadPhaseLayer(0)),JPH::SpecifiedObjectLayerFilter(StaticLayer)))return {};
    const JPH::BodyLockRead body(world.GetBodyLockInterface(),result.mBodyID);
    if(!body.Succeeded())return {};
    const auto point=query.GetPointOnRay(result.mFraction);
    const auto normal=body.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2,point);
    PhysicsRayHit hit{body.GetBody().GetUserData(),own(point),{normal.GetX(),normal.GetY(),normal.GetZ()}};
    if(!math::finite(hit.position) || !math::finite(hit.normal) || math::length(hit.normal)<=1e-8)return {};
    hit.normal=math::normalized(hit.normal);return hit;
}
}
