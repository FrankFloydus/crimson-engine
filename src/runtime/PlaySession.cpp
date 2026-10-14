#include "PlaySession.h"
#include "GameInput.h"
#include "PlayerController.h"
#include "physics/CharacterMotor.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace crimson {
namespace {
std::vector<ScenePoseUpdate> characterPoses(const Scene& scene) {
    std::vector<ScenePoseUpdate> result;
    for(const auto& entity:scene.records())if(entity.character)result.push_back({entity.id,entity.local});
    return result;
}
}
PlaySession::PlaySession(std::filesystem::path projectRoot):projectRoot_(std::move(projectRoot)){}
bool PlaySession::start(const SceneDocument& authored) {
    if(playing())return true;
    try {
        auto document=std::make_unique<SceneDocument>(projectRoot_);
        if(!document->resetTransientFrom(authored)){status_=document->status();return false;}
        auto physics=std::make_unique<PhysicsWorld>();physics->reconcile(document->scene());
        presentation_.apply(document->scene().records(),document->scene().properties());
        document_=std::move(document);physics_=std::move(physics);particles_=SceneParticles{};
        navigation_=std::make_unique<NavigationMesh>();
        remainder_=0;ticks_=droppedTicks_=routeQueries_=0;suspended_=failed_=false;
        clearRoute();lastClickedGround_.reset();controlled_=NoEntity;inputCancellation_=0;lastControlledPose_.reset();
        editRevision_=document_->scene().revision();resetInterpolation();reconcileParticles(0);
        status_=physics_->status();return true;
    } catch(const std::exception& error){stop();status_="Cannot enter Play: "+std::string(error.what());return false;}
}
void PlaySession::stop() {
    particles_=SceneParticles{};navigation_.reset();physics_.reset();document_.reset();previous_.clear();current_.clear();
    presentation_.apply({},{});remainder_=0;ticks_=droppedTicks_=routeQueries_=0;editRevision_=0;suspended_=failed_=false;status_.clear();
    clearRoute();lastClickedGround_.reset();controlled_=NoEntity;inputCancellation_=0;lastControlledPose_.reset();
}
void PlaySession::clearRoute(){controller_.reset();destination_.reset();routeActive_=false;routeStatus_.clear();lastHeldTarget_.reset();heldRetargetRemaining_=0;}
void PlaySession::planRoute(const Vec3& start) {
    controller_.reset();routeActive_=false;routeStatus_.clear();
    if(!destination_)return;
    ++routeQueries_;
    if(auto route=navigation_->findPath(start,*destination_)) {
        controller_.setPath(std::move(route->corners));routeActive_=true;
    } else routeStatus_="The clicked destination is not reachable by this Character.\n";
}
void PlaySession::retargetHeld(const Vec3& start,const Vec3& target) {
    lastClickedGround_=target;
    if(lastHeldTarget_ && math::length(target-*lastHeldTarget_)<HeldRetargetDistance)return;
    lastHeldTarget_=target;++routeQueries_;
    if(auto route=navigation_->findPath(start,target)) {
        controller_.setPath(std::move(route->corners));destination_=target;
        routeActive_=true;routeStatus_.clear();
    } else routeStatus_="Held destination is unreachable; retaining the previous destination.\n";
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
        physics_->reconcile(document_->scene(),document_->transformEditRoots());
        const auto controlled=document_->scene().properties().playerCharacter;
        const auto controlledPose=document_->scene().worldTransform(controlled);
        const bool replacedPlayer=controlled_!=NoEntity && controlled_!=controlled;
        const bool editedPose=lastControlledPose_ && controlledPose && *lastControlledPose_!=*controlledPose;
        if(controlled_!=controlled || editedPose)clearRoute();
        // Edits terminate the current pointer gesture as well as its route.
        // Otherwise a still-held button would immediately undo cancellation
        // or queue a route to run as soon as an active transform is committed.
        if(input && (replacedPlayer || editedPose ||
           (!document_->transformEditRoots().empty() && input->ownsKeyboard())))input->cancel();
        controlled_=controlled;lastControlledPose_=controlledPose;
        if(input && (inputCancellation_!=input->cancellationRevision() || !input->ownsKeyboard()))clearRoute();
        if(input)inputCancellation_=input->cancellationRevision();
        if(!active) {
            clearRoute();remainder_=0;suspended_=true;resetInterpolation();reconcileParticles(0);refreshPresentation();status_=physics_->status();return;
        }
        if(input) {
            const auto character=document_->scene().find(controlled);
            const double inputSeconds=!suspended_ && std::isfinite(seconds) && seconds>0?std::min(seconds,1.0):0;
            heldRetargetRemaining_=std::max(0.0,heldRetargetRemaining_-inputSeconds);
            const auto ray=input->consumeMoveRay(camera);
            if(character && character->character && navigation_->reconcile(*physics_,*character->character)) {
                lastHeldTarget_.reset();
                if(controlledPose && destination_ && !ray)planRoute(math::transformPoint(*controlledPose,{}));
                heldRetargetRemaining_=HeldRetargetInterval;
            }
            if(ray) {
                const auto hit=physics_->raycast(*ray,camera.farPlane);
                // Click destinations need actual walkable collision geometry.
                // A wall face or empty space cannot create an implicit floor.
                clearRoute();
                lastClickedGround_.reset();
                if(hit && hit->normal[1]>=std::cos(std::numbers::pi/4)-1e-5) {
                    lastClickedGround_=hit->position;lastHeldTarget_=hit->position;
                    if(controlledPose && character && character->character) {
                        destination_=hit->position;planRoute(math::transformPoint(*controlledPose,{}));
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
                    if(hit && hit->normal[1]>=std::cos(std::numbers::pi/4)-1e-5 && controlledPose && character && character->character)
                        retargetHeld(math::transformPoint(*controlledPose,{}),hit->position);
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
            const auto entity=document_->scene().find(controlled);
            const float speed=entity && entity->character?entity->character->moveSpeed:0.f;
            auto velocity=cameraRelativeMovement(movement,camera,speed);
            if(input) {
                const auto world=document_->scene().worldTransform(controlled);
                if(world) {
                    const auto position=math::transformPoint(*world,{});
                    const auto command=controller_.update(position,math::transformDirection(*world,{0,0,-1}),speed,FixedStep);
                    velocity=command.velocity;
                    CharacterMotor(*physics_,controlled).face(command.facing);
                    if(routeActive_ && !controller_.destination()){destination_.reset();routeActive_=false;}
                } else velocity={};
            }
            const auto poses=physics_->step(document_->scene(),controlled,velocity,static_cast<float>(FixedStep),document_->transformEditRoots());
            if(!document_->applyRuntimePoses(poses))throw std::runtime_error(document_->status());
            current_=characterPoses(document_->scene());lastControlledPose_=document_->scene().worldTransform(controlled);
            ++ticks_;reconcileParticles(FixedStep);
        }
        droppedTicks_+=available-executed;
        if(seconds>bounded)droppedTicks_+=static_cast<std::uint64_t>(std::min((seconds-bounded)/FixedStep,1e12));
        remainder_=std::max(0.0,remainder_-static_cast<double>(available)*FixedStep);
        if(remainder_>=FixedStep)remainder_=std::fmod(remainder_,FixedStep);
        refreshPresentation();reconcileParticles(0);
        status_=physics_->status();
        if(input)status_+=navigation_->status()+routeStatus_;
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
    particles_.reconcile(document_->documentGeneration(),scene.particles,particleDefinitions_,seconds,NoEntity,interpolation());
}
}
