#pragma once
#include "editor/DecalDocument.h"
#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/Environment.h"
#include "resources/ResourceManager.h"
#include <array>
#include <span>
namespace crimson {
class AssetLibrary;
class EditorResources;
class DecalEditor {
public:
    explicit DecalEditor(const std::filesystem::path& root):document_(root){}
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
    void requestOpen(std::string,AssetLibrary&);
    void requestCreate(std::string folder,AssetLibrary&);
    void requestExit();
    bool takeExitRequest(){const auto result=exitReady_;exitReady_=false;return result;}
    void save(AssetLibrary&,bool saveAs=false);
    void draw(AssetLibrary&,Renderer&,ResourceManager&,EditorResources&);
    void filesChanged(std::span<const std::filesystem::path>);
    void setAntiAliasing(AntiAliasing value){settings_.antiAliasing=value;}
private:
    enum class Pending {None,Open,Create,Close,Exit};
    DecalDocument document_;
    Pending pending_{Pending::None};
    std::string pendingId_,pendingFolder_,message_,publishedId_,previewStatus_;
    std::uint64_t publishedRevision_{},environmentRevision_{};
    std::array<char,256> saveName_{};
    bool dirtyPopup_{},savePopup_{},externalPopup_{},externalChanged_{},focusNext_{},exitReady_{},continueAfterSave_{},suppressFinalUpdate_{};
    bool cameraInitialized_{};
    int previewGesture_{};
    std::uint16_t displayedTarget_{UINT16_MAX};
    SceneSettings settings_;
    ResourceHandle<PreparedEnvironment> environment_;
    void request(Pending,std::string,AssetLibrary*);
    void perform(AssetLibrary&);
    void publish(AssetLibrary&);
    void modals(AssetLibrary&);
    void properties(AssetLibrary&,bool canceled);
    void preview(AssetLibrary&,Renderer&,ResourceManager&,EditorResources&);
};
}
