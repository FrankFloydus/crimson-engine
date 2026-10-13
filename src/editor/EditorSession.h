#pragma once
#include "SceneEditor.h"
#include "EditorResources.h"
#include "assets/AssetLibrary.h"
#include <filesystem>
#include <unordered_map>

namespace crimson {
class Navigation;
class AssetPreviews;
// Application-facing authoring coordinator: document, UI, asset preparation,
// render extraction, and editor session persistence have one explicit owner.
class EditorSession {
public:
    EditorSession(const std::filesystem::path&, AssetLibrary&, Renderer&);
    ~EditorSession();
    void cancelInteraction(Renderer&);
    void frame(AssetLibrary&, Renderer&, Navigation&, SDL_Window*, uint32_t bgfxFrame, bool quitRequested);
    void prepareFrame(AssetLibrary&,Renderer&,std::uint32_t bgfxFrame);
    void filesChanged(std::span<const std::filesystem::path>);
    void setResourceStatus(std::string value) { resourceStatus_=std::move(value); }
    bool exitAccepted() const { return editor_.exitAccepted(); }
private:
    std::filesystem::path root_;
    SceneDocument document_;
    SceneEditor editor_;
    SceneSettings settings_;
    ResourceManager& resources_;
    EditorResources prepared_;
    std::unique_ptr<AssetPreviews> previews_;
    ResourceHandle<PreparedEnvironment> environment_;
    ResourceHandle<SceneSnapshot> externalScene_;
    std::uint64_t environmentRevision_{},notifiedSceneFingerprint_{},savedPreviewFingerprint_{},documentEpoch_{1};
    std::vector<std::filesystem::path> changedFiles_;
    std::vector<std::string> visiblePreviewIds_;
    ViewportRegion viewportRegion_;
    struct PendingDrop { std::vector<std::string> ids;Vec3 position;std::uint64_t documentEpoch{}; };
    std::vector<PendingDrop> pendingDrops_;
    std::unordered_map<std::string,Camera> cameras_;
    std::string cameraScene_, preparedEnvironment_, status_, unresolvedScene_, startupStatus_, resourceStatus_;
    bool recoveredLegacy_{false};
    void restoreSession(AssetLibrary&);
    void saveSession() noexcept;
};
}
