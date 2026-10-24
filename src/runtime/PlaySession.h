#pragma once
#include "editor/SceneParticles.h"
#include "physics/PhysicsWorld.h"
#include "scene/SceneDocument.h"
#include "AgentSteering.h"
#include "EntityAttributes.h"
#include "EntityEffects.h"
#include "TraceVisualization.h"
#include "assets/RagdollAsset.h"
#include "assets/ModelAnimation.h"
#include "SceneAnimations.h"
#include "navigation/NavigationWorld.h"
#include <functional>
#include <chrono>
#include <memory>
#include <unordered_map>

namespace crimson {
class GameInput;
class PlaySession {
public:
    static constexpr double FixedStep=1.0/60.0;
    static constexpr std::uint64_t MaximumCatchUpTicks=8;
    explicit PlaySession(std::filesystem::path projectRoot);
    bool start(const SceneDocument& authored);
    void stop();
    bool playing() const {return document_!=nullptr;}
    SceneDocument& document();
    const SceneDocument& document() const;
    const Scene& presentationScene() const {return presentation_;}
    void setParticleDefinitions(SceneParticles::DefinitionQuery query){particleDefinitions_=std::move(query);}
    void setCollisionMeshDefinitions(CollisionMeshQuery query){collisionMeshes_=std::move(query);}
    void setAgentProfiles(NavigationAgentProfileQuery query){profileDefinitions_=std::move(query);}
    void setGameplayEffects(GameplayEffectQuery query){effectDefinitions_=std::move(query);}
    void setResourceManager(ResourceManager&);
    const NavigationWorld* navigationWorld() const{return navigation_.get();}
    std::shared_ptr<const NavigationAgentProfile> acceptedAgentProfile(const std::string& id) const;
    void prepareNavigationProfile(const std::string& profile);
    NavigationStatus navigationReadiness(const std::string& profile) const;
    NavigationProjection projectToNavigation(const std::string& profile,const Vec3&) const;
    void advance(double seconds,GameInput&,const Camera&,bool active=true);
    void advance(double seconds,Vec2 movement,const Camera&,bool active=true);
    SceneParticles& particles(){return particles_;}
    const SceneParticles& particles() const{return particles_;}
    const std::string& status() const{return status_;}
    double interpolation() const{return remainder_/FixedStep;}
    std::uint64_t tickCount() const{return ticks_;}
    std::uint64_t droppedTicks() const{return droppedTicks_;}
    const EntityAttributes& attributes() const{return attributes_;}
    std::vector<AttributeSnapshot> attributeSnapshots(EntityId id) const{return attributes_.snapshot(id);}
    std::optional<AttributeValue> attributeValue(EntityId id,const GameplayTag& tag) const{return attributes_.value(id,tag);}
    bool setAttributeBase(EntityId,const GameplayTag&,double);
    bool addAttributeBase(EntityId,const GameplayTag&,double);
    bool applyAttributeBaseChanges(EntityId,std::span<const AttributeBaseChange>);
    EffectApplyResult applyEffect(EntityId source,EntityId target,const std::string& asset);
    bool removeEffect(EffectHandle,std::uint32_t stacks=0);
    std::size_t removeEffects(EntityId target,const std::string& asset);
    const EntityEffects& effects() const{return effects_;}
    std::uint64_t navigationRevision() const;
    std::uint64_t routeQueryCount() const;
    const std::optional<Vec3>& destination() const;
    std::span<const Vec3> path() const;
    std::span<const Vec3> remainingPath() const;
    // Explicit transient commands share the same steering/motor path as local
    // input. Velocity remains active until replaced or cleared. Native input
    // owns its designated actor while the Game viewport owns that input.
    bool setActorDestination(EntityId,const Vec3& destination);
    bool setActorMovement(EntityId,const Vec3& worldVelocity);
    bool clearActorMovement(EntityId);
    const std::optional<Vec3>& actorDestination(EntityId) const;
    std::span<const Vec3> actorPath(EntityId) const;
    std::span<const Vec3> actorRemainingPath(EntityId) const;
    std::uint64_t actorRouteQueryCount(EntityId) const;
    std::uint64_t actorNavigationRevision(EntityId) const;
    std::size_t navigationProfileCount() const{return supportedProfiles_.size();}
    std::uint64_t navigationBuildCount() const{return navigation_?navigation_->buildCount():0;}
    // One batch contains all fixed ticks executed by the latest advance. Values
    // remain valid until the next advance or Stop; observing them never drains it.
    std::span<const TriggerEvent> triggerEvents() const{return triggerEvents_;}
    std::vector<EntityId> triggerOverlaps(EntityId trigger) const;
    std::optional<PhysicsRayHit> raycast(const Ray&,float maxDistance,const PhysicsQueryFilter& = {}) const;
    // Queries the current CPU collision world. Visualization is optional and
    // belongs only to this Play session; it never edits scene/history state.
    PhysicsTraceResult trace(const PhysicsTrace&,const PhysicsQueryFilter& = {},
                            PhysicsTraceMode = PhysicsTraceMode::Closest,
                            std::optional<TraceVisualizationSettings> visualization = {});
    const TraceVisualization& traceVisualization() const {return traceVisualization_;}
    using RagdollQuery=std::function<std::shared_ptr<const RagdollAsset>(const std::string&)>;
    using ModelQuery=std::function<std::shared_ptr<const ModelAsset>(const std::string&)>;
    using AnimationQuery=std::function<std::shared_ptr<const AnimationAsset>(const std::string&)>;
    void setRagdollDefinitions(RagdollQuery definitions,ModelQuery models,AnimationQuery animations={});
    bool activateRagdoll(EntityId model,Vec3 impulse={},std::shared_ptr<const ModelPose> initialPose={});
    bool resetRagdoll(EntityId model);
    bool ragdollActive(EntityId model) const;
    bool applyRagdollImpulse(EntityId model,Vec3 impulse,std::optional<Vec3> point={},int bodyIndex=-1);
    // Projects the fixed-tick Play animation clock and interpolated physics pose.
    // Scene/Game/shadow/picking consumers share the same immutable pose.
    void prepareRagdollPresentation(ExtractedScene&);
private:
    RagdollQuery ragdollDefinitions_;
    ModelQuery ragdollModels_;
    AnimationQuery ragdollAnimations_;
    SceneAnimations runtimeAnimations_;
    struct RagdollBinding {std::shared_ptr<const RagdollAsset> definition;std::shared_ptr<const ModelAsset> model;Mat4 world{};EntityId owner{};SceneModelRagdoll settings;std::string modelId;CollisionFilter collision;};
    std::unordered_map<EntityId,RagdollBinding> ragdollBindings_;
    struct RagdollAttempt {std::shared_ptr<const RagdollAsset> definition;std::shared_ptr<const ModelAsset> model;Mat4 world{};SceneModelAnimation animation;EntityId owner{};std::string diagnostic;SceneModelRagdoll settings;std::string modelId;std::shared_ptr<const AnimationAsset> animationDefinition;std::shared_ptr<const ModelAsset> animationSource;CollisionFilter collision;};
    std::unordered_map<EntityId,RagdollAttempt> ragdollAutoStarted_;
    void reconcileRagdolls();
    void stampRagdollAnimation(RagdollAttempt&) const;
    void advanceRagdollRuntime(double seconds,bool autoStart);
    std::shared_ptr<const ModelPose> initialRagdollPose(EntityId,const ModelAsset&);
    EntityId ragdollActorOwner(EntityId) const;
    CollisionFilter ragdollCollisionFilter(EntityId actor) const;
    bool ragdollTransformLocked(EntityId) const;
    std::string ragdollStatus() const;
    bool ragdollOwnsActor(EntityId) const;
    PhysicsTraceResult tracePlayerTargets(const PhysicsTrace&,const PhysicsQueryFilter&,
        std::shared_ptr<const ModelPose>& initialPose);
    std::filesystem::path projectRoot_;
    std::unique_ptr<SceneDocument> document_;
    std::unique_ptr<PhysicsWorld> physics_;
    std::unique_ptr<ResourceManager> ownedResources_;
    ResourceManager* resources_{};
    std::unique_ptr<NavigationWorld> navigation_;
    NavigationAgentProfileQuery profileDefinitions_;
    std::unordered_map<std::string,std::shared_ptr<const NavigationAgentProfile>> frameProfiles_;
    std::unordered_map<std::string,std::shared_ptr<const NavigationAgentProfile>> acceptedProfiles_;
    std::vector<NavigationAgentProfile> supportedProfiles_;
    Scene presentation_;
    TraceVisualization traceVisualization_;
    EntityId traceCamera_{};
    SceneParticles particles_;
    SceneParticles::DefinitionQuery particleDefinitions_;
    CollisionMeshQuery collisionMeshes_;
    std::vector<PhysicsMeshCollider> meshColliders_;
    bool collisionSourcesCurrent_{true};
    std::string collisionSourceDiagnostic_;
    std::chrono::steady_clock::time_point navigationUpdate_{};
    std::uint64_t navigationDomain_{},navigationSceneRevision_{};
    std::uint64_t navigationGeometryRevision_{};
    std::optional<NavigationVolumeDescriptor> navigationVolume_;
    bool navigationTransaction_{},navigationCoalescing_{};
    bool prepareMeshColliders(const Scene&,bool requireReady);
    std::vector<ScenePoseUpdate> previous_,current_;
    enum class CommandSource { None,External,DirectionalInput,Enemy };
    struct Agent {
        AgentSteering steering;
        // Original intent survives an approach point so geometry changes can
        // replan toward it; destination is the currently accepted endpoint.
        std::optional<Vec3> requestedDestination,destination,velocity;
        std::optional<Mat4> lastPose;
        std::string profile;
        std::uint64_t navigationRevision{},routeQueries{};
        NavigationPath navigationPath;
        NavigationStatus queryStatus{NavigationStatus::Unavailable};
        bool routeActive{},profileAccepted{};
        CommandSource source{CommandSource::None};
        std::string status;
    };
    std::unordered_map<EntityId,Agent> agents_;
    EntityAttributes attributes_;
    EntityEffects effects_{attributes_};
    GameplayEffectQuery effectDefinitions_;
    struct EnemyState {
        SceneEnemy definition;
        Vec3 origin{};
        std::optional<Vec3> target;
        std::optional<Vec3> chaseProgress;
        std::optional<Mat4> pose;
        std::optional<AgentTraversalSettings> traversal;
        std::string profile;
        std::uint64_t random{};
        EntityId player{};
        double wait{},retry{},retarget{};
        double chaseStalled{},chaseRetry{};
    };
    std::unordered_map<EntityId,EnemyState> enemies_;
    std::vector<TriggerEvent> triggerEvents_;
    struct FirstPersonView {
        EntityId actor{},camera{};
        Vec3 offset{};
        Mat4 residualBasis{},cameraLocal{};
        std::optional<SceneCameraFollow> follow;
        std::optional<AgentTraversalSettings> traversal;
        double yaw{},pitch{},initialYaw{},tilt{};
        double stepOffset{},previousStepOffset{},stepVelocity{},previousStepVelocity{};
        double stepCorrection{},stepResponse{};
        bool stepInterval{};
    };
    std::optional<FirstPersonView> firstPerson_;
    bool pendingJump_{};
    EntityId controlled_{},inputOwner_{};
    std::uint64_t inputCancellation_{};
    std::optional<Mat4> lastControlledPose_;
    std::string status_;
    std::uint64_t editRevision_{},agentsRevision_{},ticks_{},droppedTicks_{};
    double remainder_{};
    bool suspended_{},failed_{};
    void advanceInternal(double,Vec2,const Camera&,bool,GameInput* = nullptr);
    void refreshPresentation();
    std::string firePlayerTrace(EntityId);
    void reconcileParticles(double);
    void resetInterpolation();
    void reconcileAgents();
    void reconcileAttributes();
    void reconcileProfiles();
    void reconcileNavigation();
    void advanceEnemies(double seconds);
    static void clearRoute(Agent&);
    void clearPlayerRoute();
    bool planRoute(Agent&,const Vec3& start);
    void reconcileFirstPerson(EntityId,bool reset);
    void updateFirstPerson(GameInput&,const ScenePlayerInput&,const MovementComponent&,double seconds);
    void advanceFirstPersonStep(double seconds,float height,float smoothingTime);
    void clearFirstPersonStep();
    Agent* commandActor(EntityId);
    const Agent* findAgent(EntityId) const;
    EntityId player() const;
};
}
