#pragma once

#include "editor/ParticleDocument.h"
#include "particles/CpuParticleSystem.h"
#include "crimson-renderer/Renderer.h"
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace crimson {
class AssetLibrary;
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
    void draw(AssetLibrary&,Renderer&,float dt);
    void filesChanged(std::span<const std::filesystem::path>);
    void cancelInteraction() { document_.cancel();previewGesture_=0; }
private:
    enum class Pending { None,Open,Create,Close,Exit };
    ParticleEffectDocument document_;
    CpuParticleSystem simulation_;
    SceneSettings preview_;
    std::vector<ParticlePresentation> presentation_;
    std::vector<RenderParticle> packets_;
    std::string pendingId_,pendingFolder_,message_,publishedId_;
    Pending pending_{Pending::None};
    std::uint64_t previewRevision_{},publishedRevision_{};
    std::array<char,256> saveName_{};
    bool dirtyPopup_{},savePopup_{},continueAfterSave_{},exitReady_{},externalChanged_{},externalPopup_{},focusNext_{};
    int previewGesture_{};
    void request(Pending,std::string,AssetLibrary*);
    void perform(AssetLibrary&);
    void publish(AssetLibrary&);
    void modals(AssetLibrary&);
    void framePreview();
};
}
