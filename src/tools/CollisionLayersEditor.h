#pragma once
#include "editor/CollisionLayersDocument.h"
#include <array>
#include <span>

namespace crimson {
class CollisionLayersEditor {
public:
    explicit CollisionLayersEditor(const std::filesystem::path& root):document_(root){}
    const CollisionLayerNames& names() const { return document_.names(); }
    bool focused() const;
    bool isOpen() const { return open_; }
    bool transactionActive() const { return document_.transactionActive(); }
    bool canUndo() const { return document_.canUndo(); }
    bool canRedo() const { return document_.canRedo(); }
    bool canSave() const { return true; }
    void undo();
    void redo();
    void requestOpen();
    void requestExit();
    bool takeExitRequest() { const bool result=exitReady_;exitReady_=false;return result; }
    void finishInteraction();
    void cancelInteraction();
    void save();
    void draw();
    void filesChanged(std::span<const std::filesystem::path>);
private:
    enum class Pending { None,Close,Exit };
    CollisionLayersDocument document_;
    std::array<std::array<char,129>,32> text_{};
    Pending pending_{Pending::None};
    unsigned activeSlot_{32},activeItem_{};
    bool open_{},focusNext_{},exitReady_{},dirtyPopup_{},externalPopup_{},externalChanged_{},continueAfterSave_{},suppressFinalUpdate_{};
    void request(Pending);
    void perform();
    void modals();
};
}
