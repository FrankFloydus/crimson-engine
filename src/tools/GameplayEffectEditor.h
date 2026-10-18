#pragma once
#include "editor/GameplayEffectDocument.h"
#include "GameplayTagControls.h"
#include <array>
#include <span>

namespace crimson {
class AssetLibrary;
class GameplayEffectEditor {
public:
    explicit GameplayEffectEditor(const std::filesystem::path& root):document_(root){}
    bool focused() const;
    bool isOpen() const { return document_.isOpen(); }
    bool transactionActive() const { return document_.transactionActive(); }
    bool canUndo() const { return document_.canUndo(); }
    bool canRedo() const { return document_.canRedo(); }
    bool canSave() const { return document_.editable(); }
    bool canSaveAs() const { return document_.isOpen(); }
    void undo() { document_.undo(); }
    void redo() { document_.redo(); }
    void finishInteraction();
    void cancelInteraction();
    void requestOpen(std::string,AssetLibrary&);
    void requestCreate(std::string folder,AssetLibrary&);
    void requestExit();
    bool takeExitRequest() { const bool result=exitReady_;exitReady_=false;return result; }
    void save(AssetLibrary&,bool saveAs=false);
    void draw(AssetLibrary&);
    void filesChanged(std::span<const std::filesystem::path>);
    void setStagedEffects(GameplayEffectQuery query) { staged_=std::move(query); }
private:
    enum class Pending { None,Open,Create,Close,Exit };
    GameplayEffectDocument document_;
    GameplayEffectQuery staged_;
    Pending pending_{Pending::None};
    std::string pendingId_,pendingFolder_,message_,publishedId_;
    std::uint64_t publishedRevision_{};
    std::array<char,256> saveName_{};
    bool dirtyPopup_{},savePopup_{},externalPopup_{},externalChanged_{},focusNext_{},exitReady_{},continueAfterSave_{},suppressFinalUpdate_{};
    void request(Pending,std::string,AssetLibrary*);
    void perform(AssetLibrary&);
    void publish(AssetLibrary&);
    void modals(AssetLibrary&);
    void drawProperties(bool canceled);
    std::vector<GameplayTagPickerState> attributePickers_;
    std::array<GameplayTagPickerState,6> tagPickers_;
};
}
