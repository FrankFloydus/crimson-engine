#pragma once
#include "SceneViewport.h"
#include "tools/AssetBrowser.h"
#include "EditorPreferences.h"
#include "assets/AssetReferenceManager.h"
#include "resources/ResourceManager.h"
#include <array>
#include <optional>
#include <unordered_map>

namespace crimson {
class ParticleEditor;
class MaterialEditor;
struct PanelVisibility { bool outliner{true}, inspector{true}, assetBrowser{true}, game{false}; };
// Authoring UI and document transitions. It does not own GPU resources or the world.
class SceneEditor {
public:
    EditorFrame draw(SceneDocument&, SceneSettings&, AssetLibrary&, Renderer&, SDL_Window*,
                     bool navigationActive, uint32_t bgfxFrame, bool quitRequested, ParticleEditor&, MaterialEditor&);
    void cancelInteraction(SceneDocument&, Renderer&);
    bool exitAccepted() const { return exitAccepted_; }
    const ViewportAppearance& appearance() const { return preferences_.values().appearance; }
    const PanelVisibility& panels() const { return panels_; }
    void setPanels(const PanelVisibility& panels) { panels_=panels; }
    bool needsAuthoringDocument() const { return deferred_!=Pending::None || !deferredDeletion_.empty(); }
    void showGame() { panels_.game=true; }
    void hideGame() { panels_.game=false; }
    void notifyExternalSceneChange(SceneSnapshot snapshot) { externalScene_=std::move(snapshot); }
    void setParticleBoundsQuery(ParticleBoundsQuery query) { viewport_.setParticleBoundsQuery(std::move(query)); }
private:
    enum class Pending { None, New, Open, Exit };
    SceneViewport viewport_;
    AssetBrowserPanel browser_;
    EditorPreferences preferences_;
    PanelVisibility panels_;
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
    enum class CommandTarget { Scene,Particle,Material,Game };
    CommandTarget commandTarget_{CommandTarget::Scene};
    void cancelOpen();
    void pumpOpen(SceneDocument&,Renderer&);
    void request(Pending, const std::string&, SceneDocument&, Camera&, Renderer&);
    void perform(SceneDocument&, Camera&, Renderer&);
    void save(SceneDocument&, AssetLibrary&, bool saveAs);
    void modals(SceneDocument&, Camera&, AssetLibrary&, Renderer&);
};
}
