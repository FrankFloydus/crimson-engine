#pragma once
#include "editor/MaterialDocument.h"
#include "editor/MaterialPreview.h"
#include "MaterialGraphEditor.h"
#include "HdrColorEditor.h"
#include "ColorGradientEditor.h"
#include <array>
#include <span>
namespace crimson {
class AssetLibrary;
namespace shared { class Renderer; } using Renderer=shared::Renderer;
class ResourceManager;
// One independent material document and its replaceable property-editor view.
class MaterialEditor {
public:
    explicit MaterialEditor(const std::filesystem::path& root):document_(root) {}
    bool focused() const;
    bool isOpen() const { return document_.isOpen(); }
    bool transactionActive() const { return document_.transactionActive(); }
    bool blocksExternalRefresh(const std::string& id) const {
        return document_.isOpen()&&document_.assetId()==id&&(document_.dirty()||document_.transactionActive());
    }
    bool takeExitRequest() { const bool ready=exitReady_;exitReady_=false;return ready; }
    void requestExit();
    void requestOpen(std::string id,AssetLibrary&);
    void requestCreate(std::string folder,AssetLibrary&);
    void requestCreateBlend(std::string folder,AssetLibrary&);
    void save(AssetLibrary&,bool saveAs=false);
    void undo();
    void redo();
    void finishInteraction(AssetLibrary* assets=nullptr);
    bool canUndo() const;
    bool canRedo() const;
    bool canSave() const { return document_.editable(); }
    bool canSaveAs() const { return document_.isOpen() && (particleMaterialCompatible(document_.definition()) || modelMaterialCompatible(document_.definition())); }
    void draw(AssetLibrary&,Renderer&,ResourceManager&,double dt);
    void setAntiAliasing(AntiAliasing value) { preview_.setAntiAliasing(value); }
    void filesChanged(std::span<const std::filesystem::path>);
    void cancelInteraction() { document_.cancel();preview_.cancelInteraction();graph_.cancelInteraction();propertyEditing_=false;colorEditor_.cancel();emissionEditor_.cancel();alphaEditor_.cancel();gradientEditor_.cancel();for(auto& state:blendColorEditors_)state.cancel(); }
private:
    enum class Pending { None,Open,Create,CreateBlend,Close,Exit };
    MaterialDocument document_;
    MaterialPreview preview_;
    MaterialGraphEditor graph_;
    std::shared_ptr<const MaterialGraph> imageGraph_;
    std::string imageDocument_;
    std::uint64_t imageRevision_{};
    std::shared_ptr<const MaterialGraph> blendImageGraph_;
    std::array<std::shared_ptr<const MaterialGraph>,28> blendChannelGraphs_;
    std::optional<MaterialGraphPreviewTarget> blendPreview_;
    std::size_t blendLayer_{};
    std::size_t blendPreviewChannel_{};
    std::uint32_t blendControlId_{};
    std::uint64_t blendImageRevision_{},blendSourceRevision_{};
    HdrColorEditorState colorEditor_,emissionEditor_,alphaEditor_;
    std::array<HdrColorEditorState,84> blendColorEditors_;
    ColorGradientEditorState gradientEditor_;
    MaterialNodeId propertyNode_{};
    MaterialNodeId texturePickerNode_{};
    float previewHeight_{260};
    std::string pendingId_,pendingFolder_,message_,publishedId_,referenceDocumentId_;
    Pending pending_{Pending::None};
    std::uint64_t publishedRevision_{},referenceRevision_{},referenceMaterialRevision_{};
    std::array<char,256> saveName_{};
    Vec3 uvScaleBaseline_{1,1,1};
    bool dirtyPopup_{},savePopup_{},continueAfterSave_{},exitReady_{},externalChanged_{},externalPopup_{},focusNext_{};
    bool suppressFinalizedUpdate_{},propertyEditing_{};
    void request(Pending,std::string,AssetLibrary*);
    void perform(AssetLibrary&);
    void publish(AssetLibrary&);
    bool reconcileReferences(AssetLibrary&,bool force=false);
    void modals(AssetLibrary&);
    void properties(AssetLibrary&,Renderer&);
    void blendWorkspace(AssetLibrary&,Renderer&,ResourceManager&,double dt);
};
}
