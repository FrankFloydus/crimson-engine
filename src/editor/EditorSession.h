#pragma once
#include "SceneEditor.h"
#include "EditorResources.h"
#include "assets/AssetLibrary.h"
#include "tools/ParticleEditor.h"
#include "tools/MaterialEditor.h"
#include "SceneParticles.h"
#include "runtime/PlaySession.h"
#include "runtime/GameInput.h"
#include "tools/GameView.h"
#include <filesystem>
#include <unordered_map>

namespace crimson {
class Navigation;
class AssetPreviews;
// Application-facing authoring coordinator: document, UI, asset preparation,
// render extraction, and editor session persistence have one explicit owner.
class EditorSession {
public:
    EditorSession(const std::filesystem::path&, AssetLibrary&, Renderer&);
    ~EditorSession();
    void cancelInteraction(Renderer&);
    void setSimulationSuspended(bool suspended) { if(simulationSuspended_ && !suspended)skipSimulationTime_=true;simulationSuspended_=suspended;if(suspended)gameInput_.cancel(); }
    bool handleEvent(const SDL_Event& event) {
        if(event.type==SDL_EVENT_KEY_DOWN && event.key.scancode==SDL_SCANCODE_ESCAPE)focusGame_=false;
        return gameInput_.handleEvent(event);
    }
    void frame(AssetLibrary&, Renderer&, Navigation&, SDL_Window*, uint32_t bgfxFrame, bool quitRequested);
    void prepareFrame(AssetLibrary&,Renderer&,std::uint32_t bgfxFrame);
    void filesChanged(std::span<const std::filesystem::path>);
    void setResourceStatus(std::string value) { resourceStatus_=std::move(value); }
    bool exitAccepted() const { return editor_.exitAccepted(); }
private:
    std::filesystem::path root_;
    SceneDocument document_;
    PlaySession play_;
    GameInput gameInput_;
    GameView gameView_;
    SceneEditor editor_;
    ParticleEditor particleEditor_;
    MaterialEditor materialEditor_;
    SceneParticles particles_;
    std::vector<RenderParticle> particlePackets_;
    std::vector<RenderParticleMaterial> particleMaterials_;
    SceneSettings settings_;
    ResourceManager& resources_;
    EditorResources prepared_;
    std::unique_ptr<AssetPreviews> previews_;
    ResourceHandle<PreparedEnvironment> environment_;
    ResourceHandle<SceneSnapshot> externalScene_;
    std::uint64_t environmentRevision_{},notifiedSceneFingerprint_{},savedPreviewFingerprint_{},documentEpoch_{1};
    std::vector<std::filesystem::path> changedFiles_;
    std::vector<std::string> visiblePreviewIds_;
    ViewportRegion viewportRegion_;
    struct PendingDrop { std::vector<std::string> ids;Vec3 position;std::uint64_t documentEpoch{}; };
    std::vector<PendingDrop> pendingDrops_;
    std::unordered_map<std::string,Camera> cameras_;
    std::string cameraScene_, preparedEnvironment_, status_, unresolvedScene_, startupStatus_, resourceStatus_;
    bool recoveredLegacy_{false};
    bool simulationSuspended_{},skipSimulationTime_{true};
    bool togglePlayPending_{},focusGame_{},deferredSceneReload_{};
    Camera editCameraBeforePlay_;
    std::string playStatus_;
    SceneDocument& activeDocument() { return play_.playing()?play_.document():document_; }
    const SceneDocument& activeDocument() const { return play_.playing()?play_.document():document_; }
    void transitionPlay(Renderer&,Navigation&,bool stop);
    void restoreSession(AssetLibrary&);
    void saveSession() noexcept;
};
}
