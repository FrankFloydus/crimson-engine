#pragma once
#include "SceneEditor.h"
#include "ExternalSceneObservation.h"
#include "CameraPilot.h"
#include "EditorResources.h"
#include "assets/AssetLibrary.h"
#include "tools/ParticleEditor.h"
#include "tools/MaterialEditor.h"
#include "tools/NavigationAgentProfileEditor.h"
#include "tools/CollisionLayersEditor.h"
#include "tools/GameplayEffectEditor.h"
#include "tools/AnimationEditor.h"
#include "tools/RagdollEditor.h"
#include "tools/DecalEditor.h"
#include "NavigationPreview.h"
#include "SceneParticles.h"
#include "PlayShortcuts.h"
#include "runtime/PlaySession.h"
#include "runtime/GameInput.h"
#include "runtime/SceneAnimations.h"
#include "runtime/SceneLights.h"
#include "tools/GameView.h"
#include "tools/CommandBar.h"
#include "AttributeDebug.h"
#include "CollisionResources.h"
#include "DecalResources.h"
#include "CollisionOverlays.h"
#include "quader/QuaderBridge.h"
#include "profiling/CaptureController.h"
#include <filesystem>
#include <unordered_map>

namespace crimson {
class Navigation;
class AssetPreviews;
// Application-facing authoring coordinator: document, UI, asset preparation,
// render extraction, and editor session persistence have one explicit owner.
class EditorSession {
    friend struct EditorPerformanceProbe;
public:
    EditorSession(const std::filesystem::path&, AssetLibrary&, Renderer&);
    ~EditorSession();
    void cancelInteraction(Renderer&);
    void setSimulationSuspended(bool suspended) {
        if(simulationSuspended_ && !suspended)skipSimulationTime_=true;
        simulationSuspended_=suspended;
        if(suspended) { gameInput_.cancel();playShortcuts_.cancel();playActionPending_=PlayAction::None; }
    }
    bool handleEvent(const SDL_Event& event) {
        playShortcuts_.handleEvent(event);
        if(event.type==SDL_EVENT_KEY_DOWN && event.key.scancode==SDL_SCANCODE_ESCAPE)focusGame_=false;
        const bool cancelCameraKey=event.type==SDL_EVENT_KEY_DOWN && event.key.scancode==SDL_SCANCODE_ESCAPE && !event.key.repeat &&
            (!viewportRegion_.windowId || event.key.windowID==viewportRegion_.windowId);
        const bool cancelCameraWindow=(event.type==SDL_EVENT_WINDOW_FOCUS_LOST || event.type==SDL_EVENT_WINDOW_HIDDEN ||
            event.type==SDL_EVENT_WINDOW_MINIMIZED || event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED || event.type==SDL_EVENT_WINDOW_DESTROYED) &&
            (!viewportRegion_.windowId || event.window.windowID==viewportRegion_.windowId);
        if(cameraPilot_.gestureActive() && (cancelCameraKey || cancelCameraWindow))cameraGestureCancel_=true;
        return gameInput_.handleEvent(event);
    }
    bool blocksEditorEvent(const SDL_Event& event) const { return gameInput_.blocksEditorEvent(event); }
    void frame(AssetLibrary&, Renderer&, Navigation&, SDL_Window*, uint32_t bgfxFrame, bool quitRequested,
               profiling::CaptureController* captures=nullptr);
    void prepareFrame(AssetLibrary&,Renderer&,std::uint32_t bgfxFrame);
    void filesChanged(std::span<const std::filesystem::path>);
    void setResourceStatus(std::string value) { resourceStatus_=std::move(value); }
    void setFrameTiming(FrameTiming timing) { gameView_.setFrameTiming(timing); }
    bool exitAccepted() const { return editor_.exitAccepted(); }
    bool playing() const noexcept { return play_.playing(); }
    void setProfilingActive(bool active) noexcept { profilePreparationActive_=active; }
    const profiling::CaptureFrame& profileFrame() const noexcept { return profileFrame_; }
    SceneSettings profileSettings() const { auto value=settings_;value.antiAliasing=editor_.antiAliasing();return value; }
    const SceneDocument& profileDocument() const noexcept { return activeDocument(); }
private:
    profiling::CaptureFrame profileFrame_;
    bool profilePreparationActive_{};
    std::filesystem::path root_;
    SceneDocument document_;
    PlaySession play_;
    PlayShortcuts playShortcuts_;
    GameInput gameInput_;
    GameView gameView_;
    CommandBar commandBar_;
    AttributeDebug attributeDebug_;
    SceneEditor editor_;
    CameraPilot cameraPilot_;
    ParticleEditor particleEditor_;
    MaterialEditor materialEditor_;
    NavigationAgentProfileEditor navigationProfileEditor_;
    CollisionLayersEditor collisionLayersEditor_;
    GameplayEffectEditor gameplayEffectEditor_;
    AnimationEditor animationEditor_;
    RagdollEditor ragdollEditor_;
    DecalEditor decalEditor_;
    SceneParticles particles_;
    SceneAnimations editAnimations_;
    SceneLights editLights_;
    SceneSettings settings_;
    ResourceManager& resources_;
    EditorResources prepared_;
    CollisionResources collisions_;
    DecalResources decals_;
    NavigationPreview navigationPreview_;
    CollisionOverlays collisionOverlays_;
    QuaderBridge quader_;
    double materialGameSeconds_{},materialRealSeconds_{};
    MaterialEvaluationContext displayedMaterialContext_{};
    std::chrono::steady_clock::time_point materialClockStart_{std::chrono::steady_clock::now()};
    std::unique_ptr<AssetPreviews> previews_;
    ResourceHandle<PreparedEnvironment> environment_;
    ResourceHandle<SceneSnapshot> externalScene_;
    ExternalSceneObservation externalSceneObservation_;
    std::uint64_t environmentRevision_{},savedPreviewFingerprint_{},documentEpoch_{1};
    std::vector<std::filesystem::path> changedFiles_;
    std::vector<std::string> visiblePreviewIds_;
    ViewportRegion viewportRegion_;
    struct PendingDrop { std::vector<std::string> ids;Vec3 position;std::uint64_t documentEpoch{}; };
    std::vector<PendingDrop> pendingDrops_;
    struct PendingDecalPlacement {
        DecalSurfacePlacement intent;
        std::uint64_t document{},generation{},revision{};
        Mat4 view,projection;
        Vec2 viewportSize;
    };
    std::optional<PendingDecalPlacement> pendingDecalPlacement_;
    std::unordered_map<std::string,Camera> cameras_;
    std::string cameraScene_, preparedEnvironment_, status_, unresolvedScene_, startupStatus_, resourceStatus_,materialDropStatus_,decalDropStatus_,ragdollStatus_;
    bool recoveredLegacy_{false};
    bool simulationSuspended_{},skipSimulationTime_{true};
    PlayAction playActionPending_{PlayAction::None};
    bool focusGame_{},deferredSceneReload_{};
    bool cameraGestureCancel_{};
    bool waitingForCollision_{};
    std::uint64_t waitingSceneGeneration_{},waitingSceneRevision_{};
    Camera editCameraBeforePlay_;
    std::string playStatus_;
    SceneDocument& activeDocument() { return play_.playing()?play_.document():document_; }
    const SceneDocument& activeDocument() const { return play_.playing()?play_.document():document_; }
    void transitionPlay(Renderer&,Navigation&,bool stop);
    void restoreSession(AssetLibrary&);
    void saveSession() noexcept;
};
}
