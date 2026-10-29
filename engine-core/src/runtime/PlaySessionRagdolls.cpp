#include "PlaySession.h"
#include "AnimationPlayback.h"
#include <algorithm>
#include <stdexcept>

namespace crimson {
void PlaySession::setRagdollDefinitions(RagdollQuery definitions,ModelQuery models,AnimationQuery animations) {
    ragdollDefinitions_=std::move(definitions);ragdollModels_=std::move(models);ragdollAnimations_=std::move(animations);
}
bool PlaySession::ragdollActive(EntityId id) const {return physics_&&physics_->hasRagdoll(id);}
bool PlaySession::ragdollOwnsActor(EntityId id) const {
    return std::any_of(ragdollBindings_.begin(),ragdollBindings_.end(),[&](const auto& binding){return binding.second.owner==id;});
}
EntityId PlaySession::ragdollActorOwner(EntityId id) const {
    if(!document_)return id;
    auto ancestor=document_->scene().find(id);
    while(ancestor){if(ancestor->actor||ancestor->controller)return ancestor->id;if(!ancestor->parent)break;ancestor=document_->scene().find(ancestor->parent);}return id;
}
CollisionFilter PlaySession::ragdollCollisionFilter(EntityId actor) const {
    const auto record=document_?document_->scene().find(actor):std::optional<EntityRecord>{};
    return record&&record->controller?record->controller->collision:CollisionFilter{WorldCollisionLayer,AllCollisionLayers};
}
bool PlaySession::ragdollTransformLocked(EntityId id) const {
    if(!document_)return true;
    for(const auto locked:document_->transformEditRoots()) {
        auto ancestor=document_->scene().find(id);
        while(ancestor){if(ancestor->id==locked)return true;if(!ancestor->parent)break;ancestor=document_->scene().find(ancestor->parent);}
    }return false;
}
void PlaySession::stampRagdollAnimation(RagdollAttempt& attempt) const {
    attempt.collision=ragdollCollisionFilter(attempt.owner);
    if(ragdollAnimations_&&!attempt.animation.asset.empty())attempt.animationDefinition=ragdollAnimations_(attempt.animation.asset.id);
    if(attempt.animationDefinition&&ragdollModels_)attempt.animationSource=ragdollModels_(attempt.animationDefinition->sourceModel.id);
}
std::shared_ptr<const ModelPose> PlaySession::initialRagdollPose(EntityId id,const ModelAsset& source) {
    auto extracted=extractScene(document_->scene());
    runtimeAnimations_.advance(extracted,document_->documentGeneration(),0,ragdollAnimations_);
    const auto model=std::find_if(extracted.models.begin(),extracted.models.end(),[&](const auto& value){return value.id==id;});
    if(model==extracted.models.end())throw std::runtime_error("Ragdoll model is unavailable in the runtime scene.");
    if(!model->animation.asset.empty()) {
        if(!ragdollAnimations_||!ragdollModels_)throw std::runtime_error("The current animation is still preparing.");
        const auto animation=ragdollAnimations_(model->animation.asset.id);
        if(!animation)throw std::runtime_error("The current animation is still preparing.");
        const auto tracks=ragdollModels_(animation->sourceModel.id);
        if(!tracks)throw std::runtime_error("The current animation source is still preparing.");
        return std::make_shared<const ModelPose>(evaluateAnimationPose(*animation,*tracks,source,model->animationSeconds,model->animation.loopOverride));
    }
    return std::make_shared<const ModelPose>(evaluateModelPose(source));
}
bool PlaySession::activateRagdoll(EntityId id,Vec3 impulse,std::shared_ptr<const ModelPose> initialPose) {
    if(!playing()||failed_||!physics_||!ragdollDefinitions_||!ragdollModels_){status_="Ragdolls require an active Play session and prepared assets.";return false;}
    if(ragdollActive(id))return impulse==Vec3{}||applyRagdollImpulse(id,impulse);
    try {
        const auto record=document_->scene().find(id);const auto world=document_->scene().worldTransform(id);
        if(!record||!record->model||record->modelRagdoll.asset.empty()||!world)throw std::runtime_error("Select a model with a Ragdoll setup.");
        if(ragdollTransformLocked(id))throw std::runtime_error("Finish the model transform edit before activating its ragdoll.");
        const auto definition=ragdollDefinitions_(record->modelRagdoll.asset.id);const auto model=ragdollModels_(record->model->id);
        if(!definition||!model)throw std::runtime_error("The Ragdoll setup or source model is still preparing.");
        validateRagdollSource(*definition,*model);
        if(!initialPose)initialPose=initialRagdollPose(id,*model);
        const EntityId owner=ragdollActorOwner(id);
        if(ragdollOwnsActor(owner))throw std::runtime_error("This actor already has an active ragdoll model.");
        const auto filter=ragdollCollisionFilter(owner);
        const auto velocity=physics_->characterVelocity(owner).value_or(Vec3{});
        if(!physics_->createRagdoll(id,definition,model,*initialPose,*world,velocity,impulse,owner,filter))throw std::runtime_error(physics_->status());
        ragdollBindings_[id]={definition,model,*world,owner,record->modelRagdoll,record->model->id,filter};
        ragdollAutoStarted_[id]={definition,model,*world,record->modelAnimation,owner,{},record->modelRagdoll,record->model->id};
        stampRagdollAnimation(ragdollAutoStarted_[id]);
        if(const auto agent=agents_.find(owner);agent!=agents_.end())clearRoute(agent->second);
        status_.clear();return true;
    }catch(const std::exception& error){status_="Cannot activate ragdoll: "+std::string(error.what());return false;}
}
bool PlaySession::resetRagdoll(EntityId id) {
    if(!physics_||!physics_->hasRagdoll(id))return false;
    if(const auto binding=ragdollBindings_.find(id);binding!=ragdollBindings_.end()) {
        const auto record=document_->scene().find(id);const auto world=document_->scene().worldTransform(id);
        if(record&&record->model&&world){ragdollAutoStarted_[id]={binding->second.definition,binding->second.model,*world,record->modelAnimation,ragdollActorOwner(id),{},record->modelRagdoll,record->model->id};stampRagdollAnimation(ragdollAutoStarted_[id]);}
    }
    physics_->removeRagdoll(id);ragdollBindings_.erase(id);status_.clear();return true;
}
bool PlaySession::applyRagdollImpulse(EntityId id,Vec3 impulse,std::optional<Vec3> point,int bodyIndex) {
    if(!physics_||!physics_->hasRagdoll(id))return false;
    return physics_->applyRagdollImpulse(id,impulse,point,bodyIndex);
}
PhysicsTraceResult PlaySession::tracePlayerTargets(const PhysicsTrace& request,const PhysicsQueryFilter& filter,
    std::shared_ptr<const ModelPose>& initialPose) {
    initialPose.reset();
    // World blockers and active native ragdolls retain their ordinary query
    // semantics. A usable animated rig replaces its owner's coarse capsule.
    auto result=physics_->trace(request,PhysicsQueryFilter{filter.mask,filter.ignored,filter.includeWorld,false,filter.includeTriggers},PhysicsTraceMode::Closest);
    if(!result.valid||!filter.mask)return result;
    const auto earlier=[](const PhysicsTraceHit& a,const PhysicsTraceHit& b) {
        if(a.fraction!=b.fraction)return a.fraction<b.fraction;
        if(a.entity!=b.entity)return a.entity<b.entity;
        if(a.subshape!=b.subshape)return a.subshape<b.subshape;
        return a.ragdollBodyIndex<b.ragdollBodyIndex;
    };
    const auto remember=[&](const PhysicsTraceHit& hit,std::shared_ptr<const ModelPose> pose={}) {
        if(result.hits.empty())result.hits.push_back(hit);
        else if(earlier(hit,result.hits.front()))result.hits.front()=hit;
        else return;
        initialPose=std::move(pose);
    };
    std::vector<EntityId> ignoredCharacters(filter.ignored.begin(),filter.ignored.end());
    if(filter.includeWorld&&ragdollDefinitions_&&ragdollModels_) {
        const auto scene=extractScene(document_->scene());
        for(const auto& instance:scene.models) {
            if(instance.ragdoll.asset.empty()||ragdollTransformLocked(instance.id))continue;
            const auto owner=ragdollActorOwner(instance.id);
            if(std::find(filter.ignored.begin(),filter.ignored.end(),instance.id)!=filter.ignored.end()||
               std::find(filter.ignored.begin(),filter.ignored.end(),owner)!=filter.ignored.end()||
               !(filter.mask&ragdollCollisionFilter(owner).layers))continue;
            if(ragdollActive(instance.id)){ignoredCharacters.push_back(owner);continue;}
            if(ragdollOwnsActor(owner))continue;
            const auto definition=ragdollDefinitions_(instance.ragdoll.asset.id);
            const auto model=ragdollModels_(instance.assetId);
            if(!definition||!model)continue;
            try {
                // Evaluate the same fixed animation clock used by Play
                // presentation and retain this exact pose for the handoff.
                const auto pose=initialRagdollPose(instance.id,*model);
                bool valid=false;
                const auto hit=physics_->pickRagdollPose(instance.id,*definition,*model,*pose,instance.world,request.ray,request.maxDistance,&valid);
                if(!valid)continue;
                ignoredCharacters.push_back(owner);
                if(!hit)continue;
                PhysicsTraceHit contact;contact.entity=instance.id;contact.owner=owner;
                contact.position=contact.center=hit->position;contact.normal=hit->normal;
                contact.distance=hit->distance;contact.fraction=hit->distance/request.maxDistance;
                contact.startedInside=hit->distance==0;contact.ragdollBodyIndex=static_cast<int>(hit->bodyIndex);
                remember(contact,pose);
            }catch(const std::exception&) {
                // An unrelated pending/incompatible asset must not interrupt a
                // valid local trace. Its existing collision remains eligible.
            }
        }
    }
    if(filter.includeCharacters) {
        const auto characters=physics_->trace(request,PhysicsQueryFilter{filter.mask,ignoredCharacters,false,true,false},PhysicsTraceMode::Closest);
        if(!characters.hits.empty())remember(characters.hits.front());
    }
    return result;
}
void PlaySession::reconcileRagdolls() {
    if(!physics_||!document_)return;
    for(auto it=ragdollBindings_.begin();it!=ragdollBindings_.end();) {
        const auto record=document_->scene().find(it->first);const auto world=document_->scene().worldTransform(it->first);
        bool current=record&&record->model&&world&&*world==it->second.world&&record->modelRagdoll==it->second.settings&&record->model->id==it->second.modelId&&ragdollActorOwner(it->first)==it->second.owner&&ragdollCollisionFilter(it->second.owner)==it->second.collision;
        if(current&&ragdollDefinitions_)current=ragdollDefinitions_(record->modelRagdoll.asset.id)==it->second.definition;
        if(current&&ragdollModels_)current=ragdollModels_(record->model->id)==it->second.model;
        if(current&&ragdollTransformLocked(it->first))current=false;
        if(!current){physics_->removeRagdoll(it->first);it=ragdollBindings_.erase(it);}else ++it;
    }
    std::erase_if(ragdollAutoStarted_,[&](const auto& entry){
        const auto record=document_->scene().find(entry.first);const auto world=document_->scene().worldTransform(entry.first);
        if(!record||!record->model||!world||!record->modelRagdoll.startSimulated||record->modelRagdoll!=entry.second.settings||record->model->id!=entry.second.modelId||*world!=entry.second.world||record->modelAnimation!=entry.second.animation||ragdollActorOwner(entry.first)!=entry.second.owner||ragdollTransformLocked(entry.first))return true;
        RagdollAttempt current;current.animation=record->modelAnimation;current.owner=entry.second.owner;stampRagdollAnimation(current);
        if(current.animationDefinition!=entry.second.animationDefinition||current.animationSource!=entry.second.animationSource||current.collision!=entry.second.collision)return true;
        return (ragdollDefinitions_&&ragdollDefinitions_(record->modelRagdoll.asset.id)!=entry.second.definition)||(ragdollModels_&&ragdollModels_(record->model->id)!=entry.second.model);
    });
}
void PlaySession::advanceRagdollRuntime(double seconds,bool autoStart) {
    if(!document_||!physics_)return;
    auto scene=extractScene(document_->scene());runtimeAnimations_.advance(scene,document_->documentGeneration(),seconds,ragdollAnimations_);
    reconcileRagdolls();if(!autoStart||!ragdollDefinitions_||!ragdollModels_)return;
    for(const auto& model:scene.models)if(model.ragdoll.startSimulated&&!model.ragdoll.asset.empty()&&!ragdollActive(model.id)&&!ragdollAutoStarted_.contains(model.id)&&!ragdollTransformLocked(model.id)) {
        const auto definition=ragdollDefinitions_(model.ragdoll.asset.id);const auto source=ragdollModels_(model.assetId);if(!definition||!source)continue;
        // Pending animation publication is retried; a validated but incompatible
        // setup is attempted once per meaningful asset/placement revision.
        if(!model.animation.asset.empty()) {
            if(!ragdollAnimations_)continue;
            const auto animation=ragdollAnimations_(model.animation.asset.id);if(!animation||!ragdollModels_(animation->sourceModel.id))continue;
        }
        RagdollAttempt attempt{definition,source,model.world,model.animation,ragdollActorOwner(model.id),{},model.ragdoll,model.assetId};
        stampRagdollAnimation(attempt);
        if(!activateRagdoll(model.id)) {attempt.diagnostic=status_;ragdollAutoStarted_[model.id]=std::move(attempt);}
    }
}
std::string PlaySession::ragdollStatus() const {
    std::string result;for(const auto& [id,attempt]:ragdollAutoStarted_)if(!attempt.diagnostic.empty())result+="Model "+std::to_string(id)+": "+attempt.diagnostic+"\n";return result;
}
void PlaySession::prepareRagdollPresentation(ExtractedScene& scene) {
    if(!document_||!physics_)return;
    runtimeAnimations_.advance(scene,document_->documentGeneration(),0,ragdollAnimations_);
    for(auto& model:scene.models) {
        model.physicsPose.reset();
        if(ragdollActive(model.id))model.physicsPose=physics_->sampleRagdollPose(model.id,static_cast<float>(std::clamp(interpolation(),0.,1.)));
    }
}
}
