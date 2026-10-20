#pragma once

#include "editor/ParticleDocument.h"
#include "particles/CpuParticleSystem.h"
#include "crimson-renderer/Renderer.h"
#include "ParticleAppearanceEditor.h"
#include "HdrColorEditor.h"
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace crimson {
class AssetLibrary;
class MaterialEditor;
// Owns one asset document and its isolated preview. Scene playback has a separate owner.
class ParticleEditor {
public:
    explicit ParticleEditor(const std::filesystem::path& root):document_(root) {}
    bool focused() const;
    bool isOpen() const;
    bool exitReady() const { return exitReady_; }
    bool takeExitRequest() { const bool ready=exitReady_;exitReady_=false;return ready; }
    void requestExit();
    void requestOpen(std::string id,AssetLibrary&);
    void requestCreate(std::string folder,AssetLibrary&);
    void save(AssetLibrary&,bool saveAs=false);
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    void draw(AssetLibrary&,Renderer&,double dt,MaterialEditor&);
    void setAntiAliasing(AntiAliasing value) { preview_.antiAliasing=value; }
    bool transactionActive() const { return document_.transactionActive(); }
    void finishInteraction();
    void filesChanged(std::span<const std::filesystem::path>);
    void cancelInteraction();
private:
    enum class Pending { None,Open,Create,Close,Exit };
    ParticleEffectDocument document_;
    CpuParticleSystem simulation_;
    SceneSettings preview_;
    std::vector<ParticlePresentation> presentation_;
    std::vector<RenderParticle> packets_;
    std::string pendingId_,pendingFolder_,message_,publishedId_,materialDiagnostic_;
    Pending pending_{Pending::None};
    std::uint64_t previewRevision_{},publishedRevision_{};
    std::array<char,256> saveName_{};
    bool dirtyPopup_{},savePopup_{},continueAfterSave_{},exitReady_{},externalChanged_{},externalPopup_{},focusNext_{};
    bool suppressCanceledUpdate_{};
    std::uint32_t appearanceEditOwner_{};
    std::uint32_t overallColorEditOwner_{};
    std::array<ParticleCurveEditorState,3> curveEditors_{};
    ParticleGradientEditorState gradientEditor_{};
    HdrColorEditorState colorEditor_{};
    int previewGesture_{};
    void request(Pending,std::string,AssetLibrary*);
    void perform(AssetLibrary&);
    void publish(AssetLibrary&);
    void modals(AssetLibrary&);
    void framePreview();
    void materialControls(AssetLibrary&,MaterialEditor&);
    void authoringControls(AssetLibrary&,MaterialEditor&,bool canceled);
};
}
