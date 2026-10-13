#pragma once
#include "SceneViewport.h"
#include "tools/AssetBrowser.h"
#include <array>

namespace crimson {
// Authoring UI and document transitions. It does not own GPU resources or the world.
class SceneEditor {
public:
    EditorFrame draw(SceneDocument&, SceneSettings&, AssetLibrary&, Renderer&, SDL_Window*,
                     bool navigationActive, uint32_t bgfxFrame, bool quitRequested);
    void cancelInteraction(SceneDocument&, Renderer&);
    bool exitAccepted() const { return exitAccepted_; }
private:
    enum class Pending { None, New, Open, Exit };
    SceneViewport viewport_;
    AssetBrowserPanel browser_;
    Pending pending_{Pending::None};
    std::string pendingAsset_, deleteAsset_;
    std::array<char,256> sceneName_{};
    bool dirtyPopup_{}, savePopup_{}, deletePopup_{}, saveAs_{}, continueAfterSave_{}, exitAccepted_{}, documentReplaced_{};
    void request(Pending, const std::string&, SceneDocument&, Camera&, Renderer&);
    void perform(SceneDocument&, Camera&, Renderer&);
    void save(SceneDocument&, AssetLibrary&, bool saveAs);
    void modals(SceneDocument&, Camera&, AssetLibrary&, Renderer&);
};
}
