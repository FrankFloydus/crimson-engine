#pragma once
#include "SceneEditor.h"
#include "assets/AssetLibrary.h"
#include <filesystem>
#include <unordered_map>

namespace crimson {
class Navigation;
// Application-facing authoring coordinator: document, UI, asset preparation,
// render extraction, and editor session persistence have one explicit owner.
class EditorSession {
public:
    EditorSession(const std::filesystem::path&, AssetLibrary&, Renderer&);
    ~EditorSession();
    void cancelInteraction(Renderer&);
    void frame(AssetLibrary&, Renderer&, Navigation&, SDL_Window*, uint32_t bgfxFrame, bool quitRequested);
    bool exitAccepted() const { return editor_.exitAccepted(); }
private:
    std::filesystem::path root_;
    SceneDocument document_;
    SceneEditor editor_;
    SceneSettings settings_;
    std::unordered_map<std::string,std::vector<ModelMaterialTextures>> bindings_;
    std::unordered_map<std::string,Camera> cameras_;
    std::string cameraScene_, preparedEnvironment_, status_, unresolvedScene_, startupStatus_;
    bool recoveredLegacy_{false};
    void restoreSession(AssetLibrary&);
    void saveSession() noexcept;
    bool prepare(const std::string&, AssetLibrary&, Renderer&);
};
}
