#pragma once
#include "SceneViewport.h"
#include "tools/AssetBrowser.h"
#include "tools/GameplayInspectorContext.h"
#include "EditorPreferences.h"
#include "assets/AssetReferenceManager.h"
#include "resources/ResourceManager.h"
#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace crimson {
namespace profiling { class CaptureController; }
class QuaderBridge;
class ParticleEditor;
class MaterialEditor;
class NavigationAgentProfileEditor;
class CollisionLayersEditor;
class GameplayEffectEditor;
class AnimationEditor;
class DecalEditor;
struct PanelVisibility { bool outliner{true}, inspector{true}, assetBrowser{true}, game{false}; };
struct OverlayVisibility { bool collisions{true}, grid{true}, transformGizmo{true},navigationMesh{true}; };
// Authoring UI and document transitions. It does not own GPU resources or the world.
class SceneEditor {
    friend struct EditorPerformanceProbe;
public:
    EditorFrame draw(SceneDocument&, SceneSettings&, AssetLibrary&, Renderer&, SDL_Window*,
                     bool navigationActive, uint32_t bgfxFrame, bool quitRequested, ParticleEditor&, MaterialEditor&,NavigationAgentProfileEditor&,CollisionLayersEditor&,GameplayEffectEditor&,AnimationEditor&,DecalEditor&,
                     const GameplayInspectorContext& gameplay={}, profiling::CaptureController* captures=nullptr);
    void cancelInteraction(SceneDocument&, Renderer&);
    bool exitAccepted() const { return exitAccepted_; }
    const ViewportAppearance& appearance() const { return preferences_.values().appearance; }
    AntiAliasing antiAliasing() const { return preferences_.values().antiAliasing; }
    std::uint16_t pointShadowResolution() const { return static_cast<std::uint16_t>(preferences_.values().pointShadowResolution); }
    std::uint32_t thumbnailDemand() const { return browser_.thumbnailDemand(); }
    const PanelVisibility& panels() const { return panels_; }
    void setPanels(const PanelVisibility& panels) { panels_=panels; }
    const OverlayVisibility& overlays() const { return overlays_; }
    void setOverlays(const OverlayVisibility& overlays) { overlays_=overlays; }
    void setCameraViewState(EntityId camera, bool piloting, bool gestureActive=false) {
        viewedCamera_=camera; pilotingCamera_=piloting; cameraGestureActive_=gestureActive;
    }
    bool needsAuthoringDocument() const { return deferred_!=Pending::None || !deferredDeletion_.empty(); }
    bool playTransitionBlocked() const {
        return opening_ || deletion_ || pending_!=Pending::None || needsAuthoringDocument() ||
            dirtyPopup_ || savePopup_ || deletePopup_ || saveConflictPopup_ || externalPopup_ || openingPopup_;
    }
    void showGame() { panels_.game=true; }
    void hideGame() { panels_.game=false; }
    void notifyExternalSceneChange(SceneSnapshot snapshot) { externalScene_=std::move(snapshot); }
    void setParticleBoundsQuery(ParticleBoundsQuery query) { viewport_.setParticleBoundsQuery(std::move(query)); }
    void setCollisionMeshQuery(CollisionMeshQuery query) { viewport_.setCollisionMeshQuery(std::move(query)); }
    void setQuaderBridge(QuaderBridge* bridge){quader_=bridge;}
    void setAgentProfileQuery(NavigationAgentProfileQuery query) { viewport_.setAgentProfileQuery(std::move(query)); }
    void setNavigationBuildStatus(std::function<std::string(const std::string&)> query) { navigationBuildStatus_=std::move(query); }
    void setDecalStatusQuery(std::function<std::string(EntityId)> query) { decalStatus_=std::move(query); }
    void setDecalPatchQuery(std::function<std::shared_ptr<const DecalPatchData>(EntityId)> query) { viewport_.setDecalPatchQuery(std::move(query)); }
    void setDecalFrameQuery(std::function<std::optional<Mat4>(EntityId)> query) { decalFrames_=query;viewport_.setDecalFrameQuery(std::move(query)); }
    void drawDecalPlacementPlane(const Mat4& frame,const Camera& camera,bgfx::TextureHandle texture,const Vec4& color) { viewport_.drawDecalPlacementPlane(frame,camera,texture,color); }
    const std::string& navigationPreviewProfile() const { return navigationPreviewProfile_; }
private:
    enum class Pending { None, New, Open, Exit };
    SceneViewport viewport_;
    QuaderBridge* quader_{};
    AssetBrowserPanel browser_;
    EditorPreferences preferences_;
    PanelVisibility panels_;
    OverlayVisibility overlays_;
    EntityId viewedCamera_{};
    bool pilotingCamera_{},cameraGestureActive_{};
    Pending pending_{Pending::None};
    Pending deferred_{Pending::None};
    std::string deferredAsset_;
    std::vector<std::string> deferredDeletion_;
    std::string pendingAsset_;
    std::optional<AssetDeletionPlan> deletion_;
    std::unordered_map<std::string,std::string> replacements_;
    AssetLibrary* editingAssets_{};
    std::string deletionDocumentId_;
    std::string deletionStatus_;
    std::optional<SceneSnapshot> externalScene_;
    ResourceHandle<SceneSnapshot> opening_;
    std::string openingAsset_,openingDocumentId_,openingError_;
    uint64_t openingGeneration_{},openingRevision_{};
    std::array<char,256> sceneName_{};
    bool dirtyPopup_{}, savePopup_{}, deletePopup_{}, saveConflictPopup_{}, externalPopup_{}, openingPopup_{}, saveAs_{}, continueAfterSave_{}, exitAccepted_{}, documentReplaced_{};
    std::string navigationPreviewProfile_;
    std::uint64_t navigationPreviewGeneration_{};
    EntityId navigationPreviewVolume_{};
    std::function<std::string(const std::string&)> navigationBuildStatus_;
    std::function<std::string(EntityId)> decalStatus_;
    std::function<std::optional<Mat4>(EntityId)> decalFrames_;
    enum class CommandTarget { Scene,Particle,Material,Profile,CollisionLayers,Effect,Animation,Decal,Game };
    CommandTarget commandTarget_{CommandTarget::Scene};
    bool viewportBusy() const { return viewport_.interacting() || cameraGestureActive_; }
    void cancelOpen();
    void pumpOpen(SceneDocument&,Renderer&);
    void request(Pending, const std::string&, SceneDocument&, Camera&, Renderer&);
    void perform(SceneDocument&, Camera&, Renderer&);
    void save(SceneDocument&, AssetLibrary&, bool saveAs);
    void modals(SceneDocument&, Camera&, AssetLibrary&, Renderer&);
};
}
