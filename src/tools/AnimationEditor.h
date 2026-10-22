#pragma once
#include "editor/AnimationDocument.h"
#include "editor/EditorResources.h"
#include "runtime/AnimationPlayback.h"
#include "crimson-renderer/Environment.h"
#include <array>
#include <span>

namespace crimson {
class AssetLibrary;

class AnimationEditor {
public:
    explicit AnimationEditor(const std::filesystem::path& root):document_(root){}
    bool focused() const;
    bool isOpen() const {return document_.isOpen();}
    bool transactionActive() const {return document_.transactionActive();}
    bool canUndo() const {return document_.canUndo();}
    bool canRedo() const {return document_.canRedo();}
    bool canSave() const {return document_.editable();}
    bool canSaveAs() const {return isOpen();}
    void undo(){document_.undo();}
    void redo(){document_.redo();}
    void finishInteraction();
    void cancelInteraction();
    void requestOpen(std::string id,AssetLibrary&);
    void requestExit();
    bool takeExitRequest(){const auto value=exitReady_;exitReady_=false;return value;}
    void save(AssetLibrary&,bool saveAs=false);
    void draw(AssetLibrary&,Renderer&,ResourceManager&,EditorResources&,double dt);
    void filesChanged(std::span<const std::filesystem::path>);
    void setAntiAliasing(AntiAliasing value){settings_.antiAliasing=value;}
private:
    enum class Pending {None,Open,Close,Exit};
    AnimationDocument document_;
    Pending pending_{Pending::None};
    std::string pendingId_,message_,publishedId_,previewStatus_;
    std::array<char,256> saveName_{};
    std::uint64_t publishedRevision_{},bindingRevision_{},environmentRevision_{};
    bool dirtyPopup_{},savePopup_{},externalPopup_{},externalChanged_{},focusNext_{},exitReady_{},continueAfterSave_{},suppressFinalUpdate_{};
    bool playing_{true},framePending_{true};
    double seconds_{};
    int previewGesture_{};
    int propertySection_{};
    std::uint16_t displayedTarget_{UINT16_MAX};
    SceneSettings settings_;
    ResourceHandle<PreparedEnvironment> environment_;
    std::optional<PreparedAnimationBinding> binding_;
    std::shared_ptr<const ModelAsset> boundSource_;
    void request(Pending,std::string,AssetLibrary*);
    void perform(AssetLibrary&);
    void publish(AssetLibrary&);
    void modals(AssetLibrary&);
    void properties(AssetLibrary&,const std::shared_ptr<const ModelAsset>&,bool canceled);
    void timeline(double duration);
    void preview(AssetLibrary&,Renderer&,ResourceManager&,EditorResources&,double dt);
};
}
