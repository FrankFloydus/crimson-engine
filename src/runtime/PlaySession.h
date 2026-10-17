#pragma once
#include "editor/SceneParticles.h"
#include "physics/PhysicsWorld.h"
#include "scene/SceneDocument.h"
#include "AgentSteering.h"
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
    static constexpr double HeldRetargetInterval=.05;
    static constexpr float HeldRetargetDistance=.05f;
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
    std::uint64_t navigationRevision() const;
    std::uint64_t routeQueryCount() const;
    const std::optional<Vec3>& destination() const;
    const std::optional<Vec3>& lastClickedGround() const{return lastClickedGround_;}
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
private:
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
    enum class CommandSource { None,External,PlayerInput,DirectionalInput };
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
    std::optional<Vec3> lastClickedGround_;
    std::optional<Vec3> lastHeldTarget_;
    double heldRetargetRemaining_{};
    EntityId controlled_{},inputOwner_{};
    std::uint64_t inputCancellation_{};
    std::optional<Mat4> lastControlledPose_;
    std::string status_;
    std::uint64_t editRevision_{},agentsRevision_{},ticks_{},droppedTicks_{};
    double remainder_{};
    bool suspended_{},failed_{};
    void advanceInternal(double,Vec2,const Camera&,bool,GameInput* = nullptr);
    void refreshPresentation();
    void reconcileParticles(double);
    void resetInterpolation();
    void reconcileAgents();
    void reconcileProfiles();
    void reconcileNavigation();
    static void clearRoute(Agent&);
    void clearPlayerRoute();
    bool planRoute(Agent&,const Vec3& start);
    void retargetHeld(Agent&,const Vec3& start,const Vec3& target);
    Agent* commandActor(EntityId);
    const Agent* findAgent(EntityId) const;
    EntityId player() const;
};
}
