#include "PlaySession.h"
#include "GameInput.h"
#include "AgentSteering.h"
#include "physics/CharacterMotor.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <unordered_set>

namespace crimson {
namespace {
std::vector<ScenePoseUpdate> characterPoses(const Scene& scene) {
    std::vector<ScenePoseUpdate> result;
    for(const auto& entity:scene.records())if(entity.controller)result.push_back({entity.id,entity.local});
    return result;
}
}
PlaySession::PlaySession(std::filesystem::path projectRoot):projectRoot_(std::move(projectRoot)){}
bool PlaySession::start(const SceneDocument& authored) {
    if(playing())return true;
    try {
        auto document=std::make_unique<SceneDocument>(projectRoot_);
        if(!document->resetTransientFrom(authored)){status_=document->status();return false;}
        if(!prepareMeshColliders(document->scene(),true))return false;
        auto physics=std::make_unique<PhysicsWorld>();physics->reconcile(document->scene(),{},meshColliders_);
        presentation_.apply(document->scene().records(),document->scene().properties());
        document_=std::move(document);physics_=std::move(physics);particles_=SceneParticles{};
        navigation_.clear();agents_.clear();navigationBuilds_=0;
        remainder_=0;ticks_=droppedTicks_=0;suspended_=failed_=false;
        lastClickedGround_.reset();lastHeldTarget_.reset();heldRetargetRemaining_=0;
        controlled_=inputOwner_=NoEntity;inputCancellation_=0;lastControlledPose_.reset();
        editRevision_=document_->scene().revision();reconcileAgents();resetInterpolation();reconcileParticles(0);
        status_=physics_->status()+particles_.status();return true;
    } catch(const std::exception& error){stop();status_="Cannot enter Play: "+std::string(error.what());return false;}
}
void PlaySession::stop() {
    meshColliders_.clear();
    particles_=SceneParticles{};navigation_.clear();agents_.clear();physics_.reset();document_.reset();previous_.clear();current_.clear();
    presentation_.apply({},{});remainder_=0;ticks_=droppedTicks_=0;editRevision_=agentsRevision_=0;suspended_=failed_=false;status_.clear();
    navigationBuilds_=0;lastClickedGround_.reset();lastHeldTarget_.reset();heldRetargetRemaining_=0;
    controlled_=inputOwner_=NoEntity;inputCancellation_=0;lastControlledPose_.reset();
}
bool PlaySession::prepareMeshColliders(const Scene& scene,bool requireReady) {
    std::vector<PhysicsMeshCollider> prepared;
    for(const auto& record:scene.records())if(record.collider && record.collider->shape==ColliderShape::Trimesh && (record.collider->mesh || !record.collider->source.empty())) {
        const auto source=colliderMeshResult(*record.collider,collisionMeshes_);
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
    const auto found=navigation_.find(agent->profile);return found==navigation_.end()?0:found->second->revision();
}
std::uint64_t PlaySession::navigationRevision() const{return actorNavigationRevision(player());}
std::uint64_t PlaySession::routeQueryCount() const{return actorRouteQueryCount(player());}
const std::optional<Vec3>& PlaySession::destination() const{return actorDestination(player());}
std::span<const Vec3> PlaySession::path() const{return actorPath(player());}
std::span<const Vec3> PlaySession::remainingPath() const{return actorRemainingPath(player());}
void PlaySession::clearRoute(Agent& agent) {
    agent.steering.reset();agent.requestedDestination.reset();agent.destination.reset();agent.velocity.reset();
    agent.routeActive=false;agent.source=CommandSource::None;agent.status.clear();
}
void PlaySession::clearPlayerRoute() {
    if(const auto found=agents_.find(controlled_);found!=agents_.end() &&
       (found->second.source==CommandSource::PlayerInput||found->second.source==CommandSource::DirectionalInput))clearRoute(found->second);
    lastHeldTarget_.reset();heldRetargetRemaining_=0;
}
bool PlaySession::planRoute(Agent& agent,const Vec3& start) {
    agent.steering.reset();agent.destination.reset();agent.routeActive=false;agent.status.clear();
    if(!agent.requestedDestination)return false;
    const auto found=navigation_.find(agent.profile);
    if(found==navigation_.end()){agent.status="Actor needs an enabled Navigation Agent to follow destinations.\n";return false;}
    ++agent.routeQueries;
    if(auto route=found->second->findReachablePath(start,*agent.requestedDestination)) {
        agent.destination=route->corners.back();agent.steering.setPath(std::move(route->corners));agent.routeActive=true;return true;
    }
    agent.status="No reachable walking surface is available for this actor.\n";return false;
}
void PlaySession::reconcileAgents() {
    const auto& scene=document_->scene();std::unordered_set<EntityId> kept;std::unordered_set<std::uint64_t> profiles;
    for(const auto& record:scene.records()) {
        if(!record.controller||!record.motor||!record.motor->enabled||!physics_->characterPosition(record.id))continue;
        const auto world=scene.worldTransform(record.id);if(!world)continue;
        kept.insert(record.id);auto& agent=agents_[record.id];
        if(agent.lastPose && *agent.lastPose!=*world)clearRoute(agent);
        agent.lastPose=*world;
        if(!record.navigation||!record.navigation->enabled) {
            if(agent.requestedDestination)clearRoute(agent);
            agent.profile=0;agent.navigationRevision=0;continue;
        }
        const auto& controller=*record.controller;
        const auto key=(std::uint64_t(std::bit_cast<std::uint32_t>(controller.height))<<32)|std::bit_cast<std::uint32_t>(controller.radius);
        profiles.insert(key);auto& navigation=navigation_[key];if(!navigation)navigation=std::make_unique<NavigationMesh>();
        if(navigation->reconcile(*physics_,controller))++navigationBuilds_;
        if(agent.profile!=key||agent.navigationRevision!=navigation->revision()) {
            agent.profile=key;agent.navigationRevision=navigation->revision();
            if(agent.requestedDestination)planRoute(agent,math::transformPoint(*world,{}));
        }
    }
    std::erase_if(agents_,[&](const auto& entry){return !kept.contains(entry.first);});
    std::erase_if(navigation_,[&](const auto& entry){return !profiles.contains(entry.first);});
    agentsRevision_=scene.revision();
}
PlaySession::Agent* PlaySession::commandActor(EntityId id) {
    if(!document_||failed_){status_="Actor commands require an active Play session.";return nullptr;}
    const auto record=document_->scene().find(id);
    if(id==inputOwner_&&id==player()&&record&&record->playerInput&&record->playerInput->enabled) {
        status_="Game input currently owns this actor.";return nullptr;
    }
    if(agentsRevision_!=document_->scene().revision()) {
        prepareMeshColliders(document_->scene(),false);
        physics_->reconcile(document_->scene(),document_->transformEditRoots(),meshColliders_);reconcileAgents();
    }
    const auto found=agents_.find(id);
    if(found==agents_.end()){status_="Actor commands require an enabled Motor and a usable Character Controller.";return nullptr;}
    return &found->second;
}
bool PlaySession::setActorDestination(EntityId id,const Vec3& destination) {
    if(!math::finite(destination)){status_="Actor destination must be finite.";return false;}
    auto agent=commandActor(id);if(!agent)return false;
    if(!agent->profile){status_="Actor destinations require an enabled Navigation Agent.";return false;}
    if(agent->source==CommandSource::External&&agent->requestedDestination==destination)return agent->destination.has_value();
    const auto world=document_->scene().worldTransform(id);
    clearRoute(*agent);agent->source=CommandSource::External;agent->requestedDestination=destination;
    const auto accepted=planRoute(*agent,math::transformPoint(*world,{}));if(!accepted)status_=agent->status;return accepted;
}
bool PlaySession::setActorMovement(EntityId id,const Vec3& velocity) {
    if(!math::finite(velocity)){status_="Actor velocity must be finite.";return false;}
    auto agent=commandActor(id);if(!agent)return false;
    clearRoute(*agent);agent->source=CommandSource::External;agent->velocity=Vec3{velocity[0],0,velocity[2]};return true;
}
bool PlaySession::clearActorMovement(EntityId id) {
    auto agent=commandActor(id);if(!agent)return false;clearRoute(*agent);return true;
}
void PlaySession::retargetHeld(Agent& agent,const Vec3& start,const Vec3& target) {
    lastClickedGround_=target;
    if(lastHeldTarget_ && math::length(target-*lastHeldTarget_)<HeldRetargetDistance)return;
    lastHeldTarget_=target;const auto found=navigation_.find(agent.profile);if(found==navigation_.end())return;++agent.routeQueries;
    if(auto route=found->second->findReachablePath(start,target)) {
        agent.requestedDestination=target;agent.destination=route->corners.back();agent.velocity.reset();
        agent.steering.setPath(std::move(route->corners));agent.source=CommandSource::PlayerInput;
        agent.routeActive=true;agent.status.clear();
    } else agent.status="Held destination is unreachable; retaining the previous destination.\n";
}
SceneDocument& PlaySession::document(){if(!document_)throw std::logic_error("Play is not active.");return *document_;}
const SceneDocument& PlaySession::document() const{if(!document_)throw std::logic_error("Play is not active.");return *document_;}
void PlaySession::resetInterpolation(){if(document_)current_=characterPoses(document_->scene());else current_.clear();previous_=current_;}
void PlaySession::advance(double seconds,GameInput& input,const Camera& camera,bool active) {
    if(!active)input.cancel();
    advanceInternal(seconds,{},camera,active,&input);
}
void PlaySession::advance(double seconds,Vec2 movement,const Camera& camera,bool active) {
    advanceInternal(seconds,movement,camera,active);
}
void PlaySession::advanceInternal(double seconds,Vec2 movement,const Camera& camera,bool active,GameInput* input) {
    if(!document_ || failed_)return;
    try {
        if(document_->scene().revision()!=editRevision_) {
            presentation_.apply(document_->scene().records(),document_->scene().properties());
            editRevision_=document_->scene().revision();resetInterpolation();
        }
        prepareMeshColliders(document_->scene(),false);
        physics_->reconcile(document_->scene(),document_->transformEditRoots(),meshColliders_);
        const auto controlled=document_->scene().properties().playerCharacter;
        const auto controlledPose=document_->scene().worldTransform(controlled);
        const bool replacedPlayer=controlled_!=NoEntity && controlled_!=controlled;
        const bool editedPose=lastControlledPose_ && controlledPose && *lastControlledPose_!=*controlledPose;
        const auto controlledRecord=document_->scene().find(controlled);
        const bool acceptsInput=controlledRecord&&controlledRecord->playerInput&&controlledRecord->playerInput->enabled&&
            controlledRecord->controller&&controlledRecord->motor&&controlledRecord->motor->enabled;
        if(controlled_!=controlled || editedPose)clearPlayerRoute();
        // Edits terminate the current pointer gesture as well as its route.
        // Otherwise a still-held button would immediately undo cancellation
        // or queue a route to run as soon as an active transform is committed.
        if(input && (replacedPlayer || editedPose || (!acceptsInput && input->ownsKeyboard()) ||
           (!document_->transformEditRoots().empty() && input->ownsKeyboard())))input->cancel();
        controlled_=controlled;lastControlledPose_=controlledPose;
        if(!acceptsInput)clearPlayerRoute();
        if(input && (inputCancellation_!=input->cancellationRevision() || !input->ownsKeyboard() || !acceptsInput))clearPlayerRoute();
        if(input)inputCancellation_=input->cancellationRevision();
        const auto newInputOwner=input&&input->ownsKeyboard()&&acceptsInput?controlled:NoEntity;
        reconcileAgents();
        if(newInputOwner!=inputOwner_) {
            if(inputOwner_)clearPlayerRoute();
            if(newInputOwner)if(const auto found=agents_.find(newInputOwner);found!=agents_.end())clearRoute(found->second);
            inputOwner_=newInputOwner;
        }
        if(!active) {
            clearPlayerRoute();inputOwner_=NoEntity;remainder_=0;suspended_=true;resetInterpolation();refreshPresentation();reconcileParticles(0);status_=physics_->status()+particles_.status();return;
        }
        const auto playerAgent=agents_.find(controlled);
        if(input&&newInputOwner&&playerAgent!=agents_.end()) {
            auto& agent=playerAgent->second;
            const double inputSeconds=!suspended_ && std::isfinite(seconds) && seconds>0?std::min(seconds,1.0):0;
            heldRetargetRemaining_=std::max(0.0,heldRetargetRemaining_-inputSeconds);
            const auto ray=input->consumeMoveRay(camera);
            if(ray) {
                const auto hit=physics_->raycast(*ray,camera.farPlane);
                // Any actual collider hit expresses movement intent. Navigation
                // resolves walls and disconnected roofs to reachable ground;
                // a ray miss still cannot invent a floor.
                clearRoute(agent);
                lastClickedGround_.reset();
                if(hit) {
                    lastClickedGround_=hit->position;lastHeldTarget_=hit->position;
                    if(controlledPose && agent.profile) {
                        agent.source=CommandSource::PlayerInput;agent.requestedDestination=hit->position;
                        planRoute(agent,math::transformPoint(*controlledPose,{}));
                    }
                }
                heldRetargetRemaining_=HeldRetargetInterval;
            } else if(heldRetargetRemaining_<=1e-9) {
                // At most one held ray/path query per interval; catch-up ticks
                // never replay intermediate pointer destinations. Reproject a
                // stationary cursor too, since its displayed camera may follow.
                if(const auto heldRay=input->heldMoveRay(camera)) {
                    heldRetargetRemaining_=HeldRetargetInterval;
                    const auto hit=physics_->raycast(*heldRay,camera.farPlane);
                    if(hit && controlledPose && agent.profile)
                        retargetHeld(agent,math::transformPoint(*controlledPose,{}),hit->position);
                }
            }
        }
        if(suspended_){seconds=0;suspended_=false;remainder_=0;resetInterpolation();}
        if(!std::isfinite(seconds) || seconds<0)seconds=0;
        // Large stalls are discarded here rather than replayed as future work.
        const double bounded=std::min(seconds,1.0);remainder_+=bounded;
        const auto available=static_cast<std::uint64_t>(std::floor((remainder_+1e-12)/FixedStep));
        const auto executed=std::min(available,MaximumCatchUpTicks);
        for(std::uint64_t tick=0;tick<executed;++tick) {
            previous_=characterPoses(document_->scene());
            for(auto& [id,agent]:agents_) {
                const auto entity=document_->scene().find(id);const auto world=document_->scene().worldTransform(id);
                if(!entity||!entity->motor||!entity->motor->enabled||!world)continue;
                const float speed=entity->motor->moveSpeed;
                if(!input&&id==controlled&&acceptsInput && (movement!=Vec2{}||agent.source==CommandSource::DirectionalInput)) {
                    clearRoute(agent);agent.source=CommandSource::DirectionalInput;
                    agent.velocity=cameraRelativeMovement(movement,camera,speed);
                }
                Vec3 velocity{};CharacterMotor motor(*physics_,id);
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
            if(!document_->applyRuntimePoses(poses))throw std::runtime_error(document_->status());
            current_=characterPoses(document_->scene());lastControlledPose_=document_->scene().worldTransform(controlled);
            for(auto& [id,agent]:agents_)agent.lastPose=document_->scene().worldTransform(id);
            ++ticks_;reconcileParticles(FixedStep);
        }
        droppedTicks_+=available-executed;
        if(seconds>bounded)droppedTicks_+=static_cast<std::uint64_t>(std::min((seconds-bounded)/FixedStep,1e12));
        remainder_=std::max(0.0,remainder_-static_cast<double>(available)*FixedStep);
        if(remainder_>=FixedStep)remainder_=std::fmod(remainder_,FixedStep);
        refreshPresentation();reconcileParticles(0);
        status_=physics_->status();
        for(const auto& [profile,navigation]:navigation_)status_+=navigation->status();
        for(const auto& [id,agent]:agents_)if(!agent.status.empty())status_+="Actor "+std::to_string(id)+": "+agent.status;
        if(!particles_.status().empty())status_+=particles_.status();
        if(droppedTicks_)status_+="Play discarded "+std::to_string(droppedTicks_)+" catch-up ticks.\n";
    } catch(const std::exception& error){remainder_=0;failed_=true;status_="Play simulation stopped: "+std::string(error.what());}
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
