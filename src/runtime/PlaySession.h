#pragma once
#include "editor/SceneParticles.h"
#include "physics/PhysicsWorld.h"
#include "scene/SceneDocument.h"
#include "PlayerController.h"
#include "navigation/NavigationMesh.h"
#include <functional>
#include <memory>

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
    void advance(double seconds,GameInput&,const Camera&,bool active=true);
    void advance(double seconds,Vec2 movement,const Camera&,bool active=true);
    SceneParticles& particles(){return particles_;}
    const SceneParticles& particles() const{return particles_;}
    const std::string& status() const{return status_;}
    double interpolation() const{return remainder_/FixedStep;}
    std::uint64_t tickCount() const{return ticks_;}
    std::uint64_t droppedTicks() const{return droppedTicks_;}
    std::uint64_t navigationRevision() const{return navigation_?navigation_->revision():0;}
    std::uint64_t routeQueryCount() const{return routeQueries_;}
    const std::optional<Vec3>& destination() const{return destination_;}
    const std::optional<Vec3>& lastClickedGround() const{return lastClickedGround_;}
    std::span<const Vec3> path() const{return controller_.path();}
private:
    std::filesystem::path projectRoot_;
    std::unique_ptr<SceneDocument> document_;
    std::unique_ptr<PhysicsWorld> physics_;
    std::unique_ptr<NavigationMesh> navigation_;
    Scene presentation_;
    SceneParticles particles_;
    SceneParticles::DefinitionQuery particleDefinitions_;
    std::vector<ScenePoseUpdate> previous_,current_;
    PlayerController controller_;
    // Keep the original hit for live-geometry replanning; destination_ is the
    // reachable endpoint returned by navigation, which can be beside a box.
    std::optional<Vec3> requestedDestination_;
    std::optional<Vec3> destination_;
    std::optional<Vec3> lastClickedGround_;
    std::optional<Vec3> lastHeldTarget_;
    double heldRetargetRemaining_{};
    std::uint64_t routeQueries_{};
    bool routeActive_{};
    std::string routeStatus_;
    EntityId controlled_{};
    std::uint64_t inputCancellation_{};
    std::optional<Mat4> lastControlledPose_;
    std::string status_;
    std::uint64_t editRevision_{},ticks_{},droppedTicks_{};
    double remainder_{};
    bool suspended_{},failed_{};
    void advanceInternal(double,Vec2,const Camera&,bool,GameInput* = nullptr);
    void refreshPresentation();
    void reconcileParticles(double);
    void resetInterpolation();
    void clearRoute();
    void planRoute(const Vec3& start);
    void retargetHeld(const Vec3& start,const Vec3& target);
};
}
