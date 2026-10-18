#include "PlaySession.h"
#include "GameInput.h"
#include "AgentSteering.h"
#include "physics/CharacterMotor.h"
#include "scene/CameraAuthoring.h"
#include "assets/GameplayEffectAsset.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace crimson {
namespace {
std::vector<ScenePoseUpdate> characterPoses(const Scene& scene) {
    std::vector<ScenePoseUpdate> result;
    for(const auto& entity:scene.records())if(entity.controller)result.push_back({entity.id,entity.local});
    return result;
}
struct StepReturn {double offset{},velocity{};};
StepReturn returnStepOffset(double offset,double velocity,double response,double seconds) {
    const double impulse=velocity+response*offset;
    const double decay=std::exp(-response*seconds);
    StepReturn result{(offset+impulse*seconds)*decay,(velocity-response*impulse*seconds)*decay};
    if((offset<=0 && result.offset>0) || (offset>=0 && result.offset<0))return {};
    return result;
}
}
PlaySession::PlaySession(std::filesystem::path projectRoot):projectRoot_(std::move(projectRoot)){
    effects_.setDefinitions([this](const std::string& id) {
        if(effectDefinitions_) {
            auto result=effectDefinitions_(id);
            if(result.definition||!result.diagnostic.empty()||result.readiness==EffectReadiness::Preparing)return result;
        }
        if(document_)if(auto staged=document_->gameplayEffect(id))return EffectDefinitionSnapshot{std::move(staged),1,{}};
        if(auto engine=engineGameplayEffect(id))return EffectDefinitionSnapshot{std::move(engine),1,{}};
        if(!effectDefinitions_)try {
            auto definition=std::make_shared<const GameplayEffectAsset>(loadGameplayEffectAsset(projectRoot_,id));
            std::uint64_t generation=14695981039346656037ull;
            for(const auto byte:serializeGameplayEffectAsset(*definition)){generation^=static_cast<unsigned char>(byte);generation*=1099511628211ull;}
            return EffectDefinitionSnapshot{std::move(definition),generation?generation:1,{}};
        }catch(const std::exception& error){return EffectDefinitionSnapshot{{},0,error.what(),EffectReadiness::Failed};}
        return EffectDefinitionSnapshot{{},0,"Effect unavailable: "+id};
    });
}
void PlaySession::setResourceManager(ResourceManager& resources) {
    if(playing())throw std::logic_error("Cannot replace resource workers during Play.");
    navigation_.reset();ownedResources_.reset();resources_=&resources;
}
void PlaySession::prepareNavigationProfile(const std::string& profile){if(navigation_)navigation_->prepare(profile);}
NavigationStatus PlaySession::navigationReadiness(const std::string& profile) const{return navigation_?navigation_->readiness(profile):NavigationStatus::Unavailable;}
std::shared_ptr<const NavigationAgentProfile> PlaySession::acceptedAgentProfile(const std::string& id) const {
    const auto found=acceptedProfiles_.find(id);return found==acceptedProfiles_.end()?nullptr:found->second;
}
NavigationProjection PlaySession::projectToNavigation(const std::string& profile,const Vec3& point) const {
    return navigation_?navigation_->project(profile,point):NavigationProjection{NavigationStatus::Unavailable,{},"Play is not active."};
}
bool PlaySession::start(const SceneDocument& authored) {
    if(playing())return true;
    try {
        auto document=std::make_unique<SceneDocument>(projectRoot_);
        if(!document->resetTransientFrom(authored)){status_=document->status();return false;}
        if(!prepareMeshColliders(document->scene(),true))return false;
        presentation_.apply(document->scene().records(),document->scene().properties());
        document_=std::move(document);physics_=std::make_unique<PhysicsWorld>();particles_=SceneParticles{};
        if(!resources_){ownedResources_=std::make_unique<ResourceManager>();resources_=ownedResources_.get();}
        navigation_=std::make_unique<NavigationWorld>(*resources_,projectRoot_/".crimson"/"cache"/"navigation");
        physics_->setAgentProfiles([this](const std::string& id){const auto found=frameProfiles_.find(id);return found==frameProfiles_.end()?nullptr:found->second;});
        reconcileProfiles();physics_->reconcile(document_->scene(),{},meshColliders_);reconcileNavigation();agents_.clear();
        remainder_=0;ticks_=droppedTicks_=0;suspended_=failed_=false;
        firstPerson_.reset();pendingJump_=false;
        controlled_=inputOwner_=NoEntity;inputCancellation_=0;lastControlledPose_.reset();
        editRevision_=document_->scene().revision();reconcileAttributes();reconcileAgents();resetInterpolation();reconcileParticles(0);
        status_=physics_->status()+particles_.status();return true;
    } catch(const std::exception& error){stop();status_="Cannot enter Play: "+std::string(error.what());return false;}
}
void PlaySession::stop() {
    effects_.clear();
    attributes_.clear();
    triggerEvents_.clear();
    meshColliders_.clear();
    particles_=SceneParticles{};navigation_.reset();agents_.clear();enemies_.clear();physics_.reset();document_.reset();previous_.clear();current_.clear();frameProfiles_.clear();acceptedProfiles_.clear();supportedProfiles_.clear();
    presentation_.apply({},{});remainder_=0;ticks_=droppedTicks_=0;editRevision_=agentsRevision_=0;suspended_=failed_=false;status_.clear();
    navigationUpdate_={};navigationDomain_=navigationSceneRevision_=navigationGeometryRevision_=0;navigationVolume_.reset();navigationTransaction_=navigationCoalescing_=false;
    firstPerson_.reset();pendingJump_=false;
    controlled_=inputOwner_=NoEntity;inputCancellation_=0;lastControlledPose_.reset();
}
void PlaySession::reconcileAttributes() {
    if(document_)effects_.reconcile(document_->documentGeneration(),document_->scene());
}
bool PlaySession::setAttributeBase(EntityId id,const GameplayTag& tag,double value) {
    const AttributeBaseChange change{tag,value};return applyAttributeBaseChanges(id,std::span{&change,1});
}
bool PlaySession::addAttributeBase(EntityId id,const GameplayTag& tag,double value) {
    const AttributeBaseChange change{tag,value,AttributeBaseOperation::Add};return applyAttributeBaseChanges(id,std::span{&change,1});
}
bool PlaySession::applyAttributeBaseChanges(EntityId id,std::span<const AttributeBaseChange> changes) {
    if(!document_||failed_){status_="Attribute changes require an active Play session.";return false;}
    reconcileAttributes();
    const bool accepted=effects_.applyBaseChanges(id,changes);
    if(!accepted)status_=effects_.status();return accepted;
}
EffectApplyResult PlaySession::applyEffect(EntityId source,EntityId target,const std::string& asset) {
    if(!document_||failed_)return {EffectApplyStatus::Unavailable,{},"Effects require an active Play session."};
    reconcileAttributes();return effects_.apply(source,target,asset);
}
bool PlaySession::removeEffect(EffectHandle handle,std::uint32_t stacks) {
    return document_&&!failed_&&effects_.remove(handle,stacks);
}
std::size_t PlaySession::removeEffects(EntityId target,const std::string& asset) {
    if(!document_||failed_)return 0;
    std::size_t removed{};
    for(const auto& effect:effects_.active(target))if(effect.definitionId==asset&&effects_.remove(effect.handle))++removed;
    return removed;
}
std::vector<EntityId> PlaySession::triggerOverlaps(EntityId trigger) const {
    return physics_?physics_->triggerOverlaps(trigger):std::vector<EntityId>{};
}
std::optional<PhysicsRayHit> PlaySession::raycast(const Ray& ray,float maxDistance,const PhysicsQueryFilter& filter) const {
    return physics_?physics_->raycast(ray,maxDistance,filter):std::nullopt;
}
bool PlaySession::prepareMeshColliders(const Scene& scene,bool requireReady) {
    std::vector<PhysicsMeshCollider> prepared;
    collisionSourcesCurrent_=true;collisionSourceDiagnostic_.clear();
    for(const auto& record:scene.records())if(record.collider && record.collider->mode==ColliderMode::Solid && record.collider->shape==ColliderShape::Trimesh && (record.collider->mesh || !record.collider->source.empty())) {
        const auto source=colliderMeshResult(*record.collider,collisionMeshes_);
        if(!source.mesh||!source.current) {
            collisionSourcesCurrent_=false;
            collisionSourceDiagnostic_=source.diagnostic.empty()?"Waiting for current mesh collision geometry.":source.diagnostic;
        }
        if(!source.mesh || (requireReady && !source.current)) {
            if(requireReady){status_=source.diagnostic.empty()?"Mesh collision is unavailable.":source.diagnostic;return false;}
            continue;
        }
        prepared.push_back({record.id,source.mesh,source.revision});
    }
    meshColliders_=std::move(prepared);return true;
}
EntityId PlaySession::player() const{return document_?document_->scene().properties().playerCharacter:NoEntity;}
const PlaySession::Agent* PlaySession::findAgent(EntityId id) const {
    const auto found=agents_.find(id);return found==agents_.end()?nullptr:&found->second;
}
const std::optional<Vec3>& PlaySession::actorDestination(EntityId id) const {
    static const std::optional<Vec3> empty;const auto agent=findAgent(id);return agent?agent->destination:empty;
}
std::span<const Vec3> PlaySession::actorPath(EntityId id) const {const auto agent=findAgent(id);return agent?agent->steering.path():std::span<const Vec3>{};}
std::span<const Vec3> PlaySession::actorRemainingPath(EntityId id) const {
    const auto agent=findAgent(id);return agent?agent->steering.path().subspan(agent->steering.cornerIndex()):std::span<const Vec3>{};
}
std::uint64_t PlaySession::actorRouteQueryCount(EntityId id) const {const auto agent=findAgent(id);return agent?agent->routeQueries:0;}
std::uint64_t PlaySession::actorNavigationRevision(EntityId id) const {
    const auto agent=findAgent(id);if(!agent)return 0;
    return navigation_?navigation_->revision(agent->profile):0;
}
std::uint64_t PlaySession::navigationRevision() const{return actorNavigationRevision(player());}
std::uint64_t PlaySession::routeQueryCount() const{return actorRouteQueryCount(player());}
const std::optional<Vec3>& PlaySession::destination() const{return actorDestination(player());}
std::span<const Vec3> PlaySession::path() const{return actorPath(player());}
std::span<const Vec3> PlaySession::remainingPath() const{return actorRemainingPath(player());}
void PlaySession::clearRoute(Agent& agent) {
    agent.steering.reset();agent.requestedDestination.reset();agent.destination.reset();agent.velocity.reset();
    agent.navigationPath={};agent.queryStatus=NavigationStatus::Unavailable;
    agent.routeActive=false;agent.source=CommandSource::None;agent.status.clear();
}
void PlaySession::clearPlayerRoute() {
    if(physics_ && controlled_)physics_->cancelCharacterInput(controlled_);
    if(const auto found=agents_.find(controlled_);found!=agents_.end() &&
       found->second.source==CommandSource::DirectionalInput)clearRoute(found->second);
    pendingJump_=false;
}
bool PlaySession::planRoute(Agent& agent,const Vec3& start) {
    if(!agent.requestedDestination)return false;
    if(!agent.profileAccepted){agent.queryStatus=NavigationStatus::Unavailable;agent.status="Character profile is unavailable or has not been accepted by physics.\n";return false;}
    if(!navigation_||agent.profile.empty()){agent.status="Actor needs an enabled Navigation Agent to follow destinations.\n";return false;}
    ++agent.routeQueries;
    auto result=navigation_->query(agent.profile,start,*agent.requestedDestination);agent.queryStatus=result.status;
    if(result.status==NavigationStatus::Ready&&result.path&&!result.path->corners.empty()) {
        agent.navigationPath=std::move(*result.path);agent.destination=agent.navigationPath.corners.back();
        agent.steering.setPath(agent.navigationPath.corners);agent.routeActive=true;agent.status.clear();return true;
    }
    // Pending replacement is not an obstacle. Preserve the original command,
    // and keep an earlier route only while every tile it uses remains current.
    if(!agent.routeActive||!navigation_->valid(agent.profile,agent.navigationPath)) {
        agent.steering.reset();agent.destination.reset();agent.routeActive=false;agent.navigationPath={};
    }
    agent.status=result.diagnostic.empty()?"Navigation is not ready for this destination.\n":result.diagnostic+"\n";return false;
}
void PlaySession::reconcileProfiles() {
    std::unordered_map<std::string,std::shared_ptr<const NavigationAgentProfile>> profiles;
    const auto add=[&](const std::string& id){
        if(profiles.contains(id))return;
        auto profile=resolveNavigationAgentProfile(id,profileDefinitions_);
        if(!profile)profile=document_->navigationAgentProfile(id);
        if(profile)profiles.emplace(id,std::move(profile));
    };
    for(const auto& record:document_->scene().records()) {
        if(record.controller)add(record.controller->profile.id);
        if(record.navigationVolume)for(const auto& reference:record.navigationVolume->profiles)add(reference.id);
    }
    frameProfiles_=std::move(profiles);
}
void PlaySession::reconcileNavigation() {
    navigationCoalescing_=false;
    std::optional<NavigationVolumeDescriptor> volume;supportedProfiles_.clear();
    std::unordered_set<std::string> used;
    const auto records=document_->scene().records();
    for(const auto& record:records)if(record.controller)used.insert(record.controller->profile.id);
    for(const auto& [id,profile]:frameProfiles_) {
        bool hasActor=false,accepted=false;
        for(const auto& record:records)if(record.controller&&record.controller->profile.id==id) {
            hasActor=true;
            const auto definition=physics_->acceptedCharacterTraversal(record.id);
            accepted|=definition&&*definition==profile->traversal;
        }
        if(!hasActor||accepted)acceptedProfiles_[id]=profile;
    }
    for(const auto& record:records)if(record.navigationVolume&&record.navigationVolume->enabled) {
        if(const auto world=document_->scene().worldTransform(record.id)) {
            const auto& source=*record.navigationVolume;
            volume=NavigationVolumeDescriptor{record.id,*world,source.size,source.bake,source.enabled};
            for(const auto& reference:source.profiles) {
                used.insert(reference.id);
                if(const auto found=frameProfiles_.find(reference.id);found!=frameProfiles_.end())supportedProfiles_.push_back(*found->second);
            }
        }
        break;
    }
    std::erase_if(acceptedProfiles_,[&](const auto& entry){return !used.contains(entry.first);});
    const auto now=std::chrono::steady_clock::now();
    const auto generation=document_->documentGeneration(),revision=document_->scene().revision();
    const bool domainChanged=navigationDomain_!=generation;
    const bool released=navigationTransaction_&&!document_->transactionActive();
    navigationTransaction_=document_->transactionActive();
    const bool changed=navigationSceneRevision_!=revision;
    const bool geometryChanged=navigationGeometryRevision_!=physics_->geometryRevision()||navigationVolume_!=volume;
    if(!domainChanged&&!released&&navigationTransaction_&&changed&&geometryChanged&&now-navigationUpdate_<std::chrono::milliseconds(100)) {
        navigationCoalescing_=true;
        navigation_->setSourceReadiness(false,"Navigation is preparing the current edit.");
        navigation_->pumpPublication();return;
    }
    if(domainChanged||released||changed)navigationUpdate_=now;
    navigationDomain_=generation;navigationSceneRevision_=revision;
    navigationGeometryRevision_=physics_->geometryRevision();navigationVolume_=volume;
    const auto boxes=physics_->staticBoxes();const auto meshes=physics_->staticMeshes();
    navigation_->setSourceReadiness(collisionSourcesCurrent_,collisionSourceDiagnostic_);
    navigation_->reconcile(document_->documentGeneration(),volume,supportedProfiles_,boxes,meshes);
    navigation_->pumpPublication();
}
void PlaySession::reconcileAgents() {
    const auto& scene=document_->scene();std::unordered_set<EntityId> kept;
    for(const auto& record:scene.records()) {
        if(!record.controller||!record.movement||!record.movement->enabled||!physics_->characterPosition(record.id))continue;
        const auto world=scene.worldTransform(record.id);if(!world)continue;
        kept.insert(record.id);auto& agent=agents_[record.id];
        agent.profileAccepted=false;
        if(agent.lastPose && *agent.lastPose!=*world)clearRoute(agent);
        agent.lastPose=*world;
        if(!record.navigation||!record.navigation->enabled) {
            if(agent.requestedDestination)clearRoute(agent);
            agent.profile.clear();agent.navigationRevision=0;continue;
        }
        const auto key=record.controller->profile.id;
        const auto resolved=frameProfiles_.find(key);const auto accepted=physics_->acceptedCharacterTraversal(record.id);
        if(resolved==frameProfiles_.end()||!accepted||*accepted!=resolved->second->traversal) {
            agent.steering.reset();agent.destination.reset();agent.navigationPath={};agent.routeActive=false;
            agent.profile=key;agent.queryStatus=NavigationStatus::Unavailable;agent.status="Character profile is unavailable or has not been accepted by physics.\n";continue;
        }
        const auto revision=navigation_->revision(key);
        agent.profileAccepted=true;
        if(navigationCoalescing_&&agent.requestedDestination) {
            agent.profile=key;agent.navigationRevision=revision;agent.queryStatus=NavigationStatus::Preparing;
            agent.steering.reset();agent.destination.reset();agent.navigationPath={};agent.routeActive=false;
            agent.status="Navigation is preparing the current edit.\n";continue;
        }
        const bool changed=agent.profile!=key||agent.navigationRevision!=revision||
            (agent.routeActive&&!navigation_->valid(key,agent.navigationPath))||
            (agent.requestedDestination&&agent.queryStatus==NavigationStatus::Preparing&&navigation_->readiness(key)!=NavigationStatus::Preparing);
        if(agent.profile!=key){agent.steering.reset();agent.destination.reset();agent.navigationPath={};agent.routeActive=false;}
        agent.profile=key;
        if(changed) {
            agent.navigationRevision=revision;
            if(agent.requestedDestination)planRoute(agent,math::transformPoint(*world,{}));
        }
    }
    std::erase_if(agents_,[&](const auto& entry){return !kept.contains(entry.first);});
    agentsRevision_=scene.revision();
}
PlaySession::Agent* PlaySession::commandActor(EntityId id) {
    if(!document_||failed_){status_="Actor commands require an active Play session.";return nullptr;}
    const auto record=document_->scene().find(id);
    if(id==inputOwner_&&id==player()&&record&&record->playerInput&&record->playerInput->enabled) {
        status_="Game input currently owns this actor.";return nullptr;
    }
    if(agentsRevision_!=document_->scene().revision()) {
        reconcileAttributes();
        prepareMeshColliders(document_->scene(),false);
        reconcileProfiles();physics_->reconcile(document_->scene(),document_->transformEditRoots(),meshColliders_);reconcileNavigation();reconcileAgents();
    }
    const auto found=agents_.find(id);
    if(found==agents_.end()){status_="Actor commands require an enabled Motor and a usable Character Controller.";return nullptr;}
    return &found->second;
}
bool PlaySession::setActorDestination(EntityId id,const Vec3& destination) {
    if(!math::finite(destination)){status_="Actor destination must be finite.";return false;}
    auto agent=commandActor(id);if(!agent)return false;
    if(agent->profile.empty()){status_="Actor destinations require an enabled Navigation Agent.";return false;}
    if(agent->source==CommandSource::External&&agent->requestedDestination==destination)return agent->destination.has_value()||agent->queryStatus==NavigationStatus::Preparing;
    const auto world=document_->scene().worldTransform(id);
    clearRoute(*agent);agent->source=CommandSource::External;agent->requestedDestination=destination;
    const auto accepted=planRoute(*agent,math::transformPoint(*world,{}));if(!accepted)status_=agent->status;return accepted||agent->queryStatus==NavigationStatus::Preparing;
}
bool PlaySession::setActorMovement(EntityId id,const Vec3& velocity) {
    if(!math::finite(velocity)){status_="Actor velocity must be finite.";return false;}
    auto agent=commandActor(id);if(!agent)return false;
    clearRoute(*agent);agent->source=CommandSource::External;agent->velocity=Vec3{velocity[0],0,velocity[2]};return true;
}
bool PlaySession::clearActorMovement(EntityId id) {
    auto agent=commandActor(id);if(!agent)return false;clearRoute(*agent);return true;
}
void PlaySession::reconcileFirstPerson(EntityId actor,bool reset) {
    const auto& scene=document_->scene();
    const auto playerRecord=scene.find(actor);
    auto cameraId=resolveOwnedActorCamera(scene,actor);
    if(const auto active=scene.find(scene.properties().activeCamera);active && active->camera &&
       ((!active->cameraFollow && active->parent==actor) || (active->cameraFollow && active->cameraFollow->target==actor)))cameraId=active->id;
    const auto record=scene.find(cameraId);
    const auto actorWorld=scene.worldTransform(actor),cameraWorld=scene.worldTransform(cameraId);
    if(!playerRecord || !record || !record->camera || !actorWorld || !cameraWorld ||
       (record->cameraFollow && record->cameraFollow->target!=actor)) { firstPerson_.reset();return; }
    // Follow definitions are stable authored values; their resolved local pose
    // changes as the upright capsule turns, without representing a camera edit.
    const bool changed=!firstPerson_ || firstPerson_->actor!=actor || firstPerson_->camera!=cameraId ||
        firstPerson_->follow!=record->cameraFollow || (!record->cameraFollow && firstPerson_->cameraLocal!=record->local) ||
        firstPerson_->traversal!=physics_->acceptedCharacterTraversal(actor);
    if(!reset && !changed)return;
    FirstPersonView view;view.actor=actor;view.camera=cameraId;view.follow=record->cameraFollow;view.cameraLocal=record->local;
    view.traversal=physics_->acceptedCharacterTraversal(actor);
    view.offset=math::transformPoint(*cameraWorld,{})-math::transformPoint(*actorWorld,{});
    const auto forward=math::normalized(math::transformDirection(*cameraWorld,{0,0,-1}),{0,0,-1});
    view.yaw=view.initialYaw=std::atan2(-forward[0],-forward[2]);
    view.pitch=std::clamp(std::asin(std::clamp(static_cast<double>(forward[1]),-1.0,1.0)),-89.0*std::numbers::pi/180,89.0*std::numbers::pi/180);
    const auto rotation=math::rotation(math::axisAngle({0,1,0},view.yaw))*math::rotation(math::axisAngle({1,0,0},view.pitch));
    const auto basis=math::withAffinePosition(*cameraWorld,{});
    if(const auto inverse=math::inverse(rotation);inverse && basis)view.residualBasis=*inverse * *basis;
    firstPerson_=view;
}
void PlaySession::updateFirstPerson(GameInput& input,const ScenePlayerInput& settings,const MovementComponent& movement,double seconds) {
    const auto delta=input.consumeLookDelta();
    if(!firstPerson_)return;
    constexpr double radians=std::numbers::pi/180;
    auto& view=*firstPerson_;
    // SDL relative motion is an accumulated distance, never a frame velocity.
    view.yaw=std::remainder(view.yaw-static_cast<double>(delta[0])*settings.mouseSensitivity*radians,2*std::numbers::pi);
    view.pitch=std::clamp(view.pitch+(settings.invertY?1:-1)*static_cast<double>(delta[1])*settings.mouseSensitivity*radians,-89*radians,89*radians);
    const Vec3 right{std::cos(view.yaw),0,-std::sin(view.yaw)};
    const auto velocity=physics_->characterVelocity(view.actor).value_or(Vec3{});
    const auto speed=attributes_.value(view.actor,stats::MovementSpeed);
    const double fraction=speed && speed->current>0?std::clamp(math::dot(velocity,right)/speed->current,-1.0,1.0):0;
    const double target=-movement.strafeTilt*radians*fraction;
    const double dt=std::isfinite(seconds)?std::clamp(seconds,0.0,.25):0;
    view.tilt+=(target-view.tilt)*(movement.tiltResponse>0?-std::expm1(-movement.tiltResponse*dt):1);
    if(std::abs(view.tilt-target)<1e-6)view.tilt=target;
}
void PlaySession::advanceFirstPersonStep(double seconds,float height,float smoothingTime) {
    if(!firstPerson_)return;
    if(smoothingTime<=0) {
        clearFirstPersonStep();
        return;
    }
    auto& view=*firstPerson_;view.previousStepOffset=view.stepOffset;view.previousStepVelocity=view.stepVelocity;
    view.stepResponse=2.0/static_cast<double>(smoothingTime);view.stepInterval=true;
    const auto settled=returnStepOffset(view.stepOffset,view.stepVelocity,view.stepResponse,seconds);
    view.stepOffset=settled.offset;view.stepVelocity=settled.velocity;
    // Preserve the entire terrain correction. A fixed offset cap releases
    // part of every closely spaced step directly into the eye; world geometry
    // constrains the displayed displacement separately below.
    view.stepOffset-=static_cast<double>(height);
    // Consecutive opposite steps must not carry a spring velocity away from
    // the new target and produce an artificial bounce.
    if(view.stepOffset*view.stepVelocity>0 || std::abs(view.stepOffset)<1e-6)view.stepVelocity=0;
    if(std::abs(view.stepOffset)<1e-6 && std::abs(view.stepVelocity)<1e-5) {
        view.stepOffset=0;view.stepVelocity=0;
    }
    view.stepCorrection=settled.offset-view.stepOffset;
}
void PlaySession::clearFirstPersonStep() {
    if(!firstPerson_)return;
    auto& view=*firstPerson_;
    view.stepOffset=view.previousStepOffset=view.stepVelocity=view.previousStepVelocity=0;
    view.stepCorrection=view.stepResponse=0;view.stepInterval=false;
}
SceneDocument& PlaySession::document(){if(!document_)throw std::logic_error("Play is not active.");return *document_;}
const SceneDocument& PlaySession::document() const{if(!document_)throw std::logic_error("Play is not active.");return *document_;}
void PlaySession::resetInterpolation(){
    if(document_)current_=characterPoses(document_->scene());else current_.clear();previous_=current_;
    if(firstPerson_) {
        firstPerson_->previousStepOffset=firstPerson_->stepOffset;
        firstPerson_->previousStepVelocity=firstPerson_->stepVelocity;
        firstPerson_->stepInterval=false;
    }
}
void PlaySession::advance(double seconds,GameInput& input,const Camera& camera,bool active) {
    if(!active)input.cancel();
    advanceInternal(seconds,{},camera,active,&input);
}
void PlaySession::advance(double seconds,Vec2 movement,const Camera& camera,bool active) {
    advanceInternal(seconds,movement,camera,active);
}
void PlaySession::advanceInternal(double seconds,Vec2 movement,const Camera& camera,bool active,GameInput* input) {
    triggerEvents_.clear();
    if(!document_ || failed_)return;
    try {
        if(ownedResources_)resources_->pump();
        if(document_->scene().revision()!=editRevision_) {
            presentation_.apply(document_->scene().records(),document_->scene().properties());
            editRevision_=document_->scene().revision();resetInterpolation();
        }
        reconcileAttributes();
        prepareMeshColliders(document_->scene(),false);
        reconcileProfiles();
        physics_->reconcile(document_->scene(),document_->transformEditRoots(),meshColliders_);
        reconcileNavigation();
        const auto controlled=document_->scene().properties().playerCharacter;
        const auto controlledPose=document_->scene().worldTransform(controlled);
        const bool replacedPlayer=controlled_!=NoEntity && controlled_!=controlled;
        const bool editedPose=lastControlledPose_ && controlledPose && *lastControlledPose_!=*controlledPose;
        const auto controlledRecord=document_->scene().find(controlled);
        const bool acceptsInput=controlledRecord&&controlledRecord->playerInput&&controlledRecord->playerInput->enabled&&
            controlledRecord->controller&&controlledRecord->movement&&controlledRecord->movement->enabled;
        if(controlled_!=controlled || editedPose)clearPlayerRoute();
        // Authoring transforms releases native input before physics reconciles
        // the edit, so held keys cannot fight an Inspector or gizmo gesture.
        if(input && (replacedPlayer || editedPose || (!acceptsInput && input->ownsKeyboard()) ||
           (!document_->transformEditRoots().empty() && input->ownsKeyboard())))input->cancel();
        controlled_=controlled;lastControlledPose_=controlledPose;
        if(!acceptsInput)clearPlayerRoute();
        if(input && (inputCancellation_!=input->cancellationRevision() || !input->ownsKeyboard() || !acceptsInput))clearPlayerRoute();
        if(input)inputCancellation_=input->cancellationRevision();
        const auto newInputOwner=input&&input->ownsKeyboard()&&acceptsInput?controlled:NoEntity;
        reconcileAgents();
        advanceEnemies(0);
        if(newInputOwner!=inputOwner_) {
            if(inputOwner_)clearPlayerRoute();
            if(newInputOwner)if(const auto found=agents_.find(newInputOwner);found!=agents_.end())clearRoute(found->second);
            inputOwner_=newInputOwner;
        }
        if(acceptsInput)reconcileFirstPerson(controlled,replacedPlayer || editedPose);
        else firstPerson_.reset();
        if(firstPerson_ && (physics_->characterLocked(controlled) || controlledRecord->movement->stairSmoothingTime<=0))
            clearFirstPersonStep();
        if(!active) {
            clearFirstPersonStep();
            clearPlayerRoute();inputOwner_=NoEntity;remainder_=0;suspended_=true;resetInterpolation();refreshPresentation();reconcileParticles(0);status_=physics_->status()+particles_.status();return;
        }
        if(input && newInputOwner) {
            movement=input->movement();
            pendingJump_=input->consumeJump() || pendingJump_;
            updateFirstPerson(*input,*controlledRecord->playerInput,*controlledRecord->movement,0);
        }
        if(suspended_){seconds=0;suspended_=false;remainder_=0;resetInterpolation();}
        if(!std::isfinite(seconds) || seconds<0)seconds=0;
        // Large stalls are discarded here rather than replayed as future work.
        const double bounded=std::min(seconds,1.0);remainder_+=bounded;
        const auto available=static_cast<std::uint64_t>(std::floor((remainder_+1e-12)/FixedStep));
        const auto executed=std::min(available,MaximumCatchUpTicks);
        for(std::uint64_t tick=0;tick<executed;++tick) {
            previous_=characterPoses(document_->scene());
            if(!effects_.step(FixedStep))throw std::runtime_error(effects_.status());
            advanceEnemies(FixedStep);
            for(auto& [id,agent]:agents_) {
                const auto entity=document_->scene().find(id);const auto world=document_->scene().worldTransform(id);
                if(!entity||!entity->movement||!entity->movement->enabled||!world)continue;
                if(effects_.readiness(id)!=EffectReadiness::Ready) {
                    CharacterMotor(*physics_,id).move({});
                    if(id==controlled)pendingJump_=false;
                    continue;
                }
                const auto movementSpeed=attributes_.value(id,stats::MovementSpeed);
                if(!movementSpeed)throw std::runtime_error("Motor is missing Stats.MovementSpeed.");
                if(movementSpeed->current>std::numeric_limits<float>::max())throw std::runtime_error("Movement Speed exceeds the physics numeric range.");
                const float speed=static_cast<float>(movementSpeed->current);
                if(!input&&id==controlled&&acceptsInput && (movement!=Vec2{}||agent.source==CommandSource::DirectionalInput)) {
                    clearRoute(agent);agent.source=CommandSource::DirectionalInput;
                    agent.velocity=cameraRelativeMovement(movement,camera,speed);
                }
                Vec3 velocity{};CharacterMotor motor(*physics_,id);
                if(input && id==controlled && acceptsInput &&
                   (newInputOwner || agent.source==CommandSource::DirectionalInput)) {
                    const auto& settings=*entity->movement;
                    clearRoute(agent);agent.source=CommandSource::DirectionalInput;
                    const Vec2 axes=newInputOwner?movement:Vec2{};
                    if(firstPerson_) {
                        const Vec3 forward{-std::sin(firstPerson_->yaw),0,-std::cos(firstPerson_->yaw)};
                        const Vec3 right{std::cos(firstPerson_->yaw),0,-std::sin(firstPerson_->yaw)};
                        velocity=right*axes[0]+forward*axes[1];
                        const auto length=math::length(velocity);
                        if(length>1)velocity=velocity/length;
                        velocity=velocity*speed;
                        motor.face(forward);
                    } else velocity=cameraRelativeMovement(axes,camera,speed);
                    motor.moveFirstPerson(velocity,settings.groundAcceleration,settings.groundFriction,settings.airAcceleration,settings.airControl);
                    if(newInputOwner && pendingJump_) {motor.jump(settings.jumpHeight);pendingJump_=false;}
                    continue;
                }
                if(agent.velocity) {
                    velocity=*agent.velocity;const auto magnitude=math::length(velocity);
                    if(magnitude>speed&&magnitude>0)velocity=velocity*(speed/magnitude);
                } else if(agent.requestedDestination) {
                    const auto position=math::transformPoint(*world,{});
                    const auto command=agent.steering.update(position,math::transformDirection(*world,{0,0,-1}),speed,FixedStep);
                    velocity=command.velocity;motor.face(command.facing);
                    if(agent.routeActive&&!agent.steering.destination()) {
                        // A reachable approach point can extend after geometry
                        // changes. Exact arrival finishes the original request.
                        if(agent.requestedDestination&&agent.destination&&math::length(*agent.requestedDestination-*agent.destination)<1e-4)
                            agent.requestedDestination.reset();
                        agent.destination.reset();agent.routeActive=false;
                    }
                }
                motor.move(velocity);
            }
            const auto poses=physics_->step(document_->scene(),NoEntity,{},static_cast<float>(FixedStep),document_->transformEditRoots(),meshColliders_);
            if(acceptsInput)advanceFirstPersonStep(FixedStep,physics_->characterStepHeight(controlled),controlledRecord->movement->stairSmoothingTime);
            if(!document_->applyRuntimePoses(poses))throw std::runtime_error(document_->status());
            const auto events=physics_->triggerEvents();
            triggerEvents_.insert(triggerEvents_.end(),events.begin(),events.end());
            current_=characterPoses(document_->scene());lastControlledPose_=document_->scene().worldTransform(controlled);
            for(auto& [id,agent]:agents_)agent.lastPose=document_->scene().worldTransform(id);
            for(auto& [id,enemy]:enemies_)enemy.pose=document_->scene().worldTransform(id);
            ++ticks_;reconcileParticles(FixedStep);
        }
        droppedTicks_+=available-executed;
        if(seconds>bounded)droppedTicks_+=static_cast<std::uint64_t>(std::min((seconds-bounded)/FixedStep,1e12));
        remainder_=std::max(0.0,remainder_-static_cast<double>(available)*FixedStep);
        if(remainder_>=FixedStep)remainder_=std::fmod(remainder_,FixedStep);
        if(input && acceptsInput)updateFirstPerson(*input,*controlledRecord->playerInput,*controlledRecord->movement,seconds);
        refreshPresentation();reconcileParticles(0);
        status_=physics_->status();
        if(!attributes_.status().empty())status_+=attributes_.status();
        if(!effects_.status().empty())status_+=effects_.status();
        for(const auto& profile:supportedProfiles_) {
            const auto debug=navigation_->summary(profile.id);
            if(!debug.diagnostic.empty())status_+=debug.diagnostic+"\n";
        }
        for(const auto& [id,agent]:agents_)if(!agent.status.empty())status_+="Actor "+std::to_string(id)+": "+agent.status;
        if(!particles_.status().empty())status_+=particles_.status();
        if(droppedTicks_)status_+="Play discarded "+std::to_string(droppedTicks_)+" catch-up ticks.\n";
    } catch(const std::exception& error){triggerEvents_.clear();remainder_=0;failed_=true;status_="Play simulation stopped: "+std::string(error.what());}
}
void PlaySession::refreshPresentation() {
    std::vector<ScenePoseUpdate> poses;poses.reserve(current_.size());const double alpha=std::clamp(interpolation(),0.0,1.0);
    for(const auto& current:current_) {
        auto pose=current;
        const auto previous=std::find_if(previous_.begin(),previous_.end(),[&](const auto& value){return value.id==current.id;});
        if(previous!=previous_.end()) {
            const auto record=document_->scene().find(current.id);
            const auto parent=record && record->parent?document_->scene().worldTransform(record->parent):std::optional{Mat4{}};
            const auto inverse=parent?math::inverse(*parent):std::nullopt;
            if(parent && inverse) {
                const auto before=*parent*previous->local,after=*parent*current.local;
                const auto beforeFields=math::affineTransformFields(before),afterFields=math::affineTransformFields(after);
                auto world=after;
                const auto upright=[](const Mat4& m,const std::optional<math::AffineTransformFields>& fields) {
                    return fields && !fields->singular && !fields->sheared &&
                        std::abs(fields->scale[0]-1)<2e-5f && std::abs(fields->scale[1]-1)<2e-5f && std::abs(fields->scale[2]-1)<2e-5f &&
                        std::abs(m[4])<2e-5f && std::abs(m[5]-1)<2e-5f && std::abs(m[6])<2e-5f;
                };
                if(upright(before,beforeFields) && upright(after,afterFields)) {
                    const auto a=std::atan2(before[8],before[10]),b=std::atan2(after[8],after[10]);
                    const auto delta=std::remainder(static_cast<double>(b-a),2*std::numbers::pi);
                    world=math::rotation(math::axisAngle({0,1,0},a+delta*alpha));
                }
                for(int axis=0;axis<3;++axis)world[12+axis]=static_cast<float>(before[12+axis]+(after[12+axis]-before[12+axis])*alpha);
                pose.local=*inverse*world;
            }
        }
        poses.push_back(pose);
    }
    presentation_.applyRuntimePoses(poses);
    if(firstPerson_) {
        const auto& view=*firstPerson_;
        if(const auto actor=presentation_.worldTransform(view.actor)) {
            const auto yaw=math::rotation(math::axisAngle({0,1,0},view.yaw));
            const auto offsetRotation=math::rotation(math::axisAngle({0,1,0},view.yaw-view.initialYaw));
            const auto orientation=yaw*math::rotation(math::axisAngle({1,0,0},view.pitch))*
                math::rotation(math::axisAngle({0,0,1},view.tilt))*view.residualBasis;
            auto position=math::transformPoint(*actor,{})+math::transformDirection(offsetRotation,view.offset);
            double stepOffset=view.stepOffset;
            if(view.stepInterval) {
                // Evaluate the spring between ticks rather than joining its
                // samples with straight lines. Cancel the interpolated terrain
                // correction at the same alpha so camera velocity stays smooth.
                stepOffset=returnStepOffset(view.previousStepOffset,view.previousStepVelocity,view.stepResponse,alpha*FixedStep).offset-
                    alpha*view.stepCorrection;
            }
            if(std::abs(stepOffset)>1e-6 && physics_) {
                // The capsule steps immediately. Its delayed eye can therefore
                // approach a stair soffit or ceiling; constrain only the extra
                // camera displacement, keeping the authored view authoritative.
                const auto camera=presentation_.find(view.camera),actorRecord=presentation_.find(view.actor);
                const float clearance=camera && camera->camera?std::max(.05f,camera->camera->nearPlane):.05f;
                const auto sign=stepOffset>0?1.f:-1.f;
                const auto mask=actorRecord && actorRecord->controller?
                    actorRecord->controller->collision.mask|WorldCollisionLayer:AllCollisionLayers;
                if(const auto hit=physics_->raycast({position,{0,sign,0}},static_cast<float>(std::abs(stepOffset))+clearance,
                    PhysicsQueryFilter{mask,{},true,false,false})) {
                    stepOffset=sign*std::min(std::abs(stepOffset),std::max(0.0,math::length(hit->position-position)-clearance));
                }
            }
            position[1]+=static_cast<float>(stepOffset);
            if(const auto world=math::withAffinePosition(orientation,position))presentation_.applyRuntimeCameraPose(view.camera,*world);
        }
    }
}
void PlaySession::reconcileParticles(double seconds) {
    if(!document_ || !particleDefinitions_)return;
    const auto scene=extractScene(document_->scene());
    // Local effects follow the same interpolated hierarchy as their rendered
    // parent. Births and simulation continue to consume the current tick world.
    if(seconds==0) {
        const auto presented=extractScene(presentation_);
        particles_.reconcile(document_->documentGeneration(),scene.particles,particleDefinitions_,seconds,NoEntity,interpolation(),presented.particles);
    } else particles_.reconcile(document_->documentGeneration(),scene.particles,particleDefinitions_,seconds,NoEntity,interpolation());
}
}
