#pragma once
#include <cstdint>
#include <string>
#include <functional>

namespace crimson {
class SceneDocument;
class AssetLibrary;
class Renderer;
class QuaderBridge;
struct NavigationInspectorContext {
    std::string* previewProfile{};
    std::string* openProfile{};
    std::function<std::string(const std::string&)> buildStatus;
};

// Explicit menu actions may finish an owned panel edit before querying history.
// Viewport gestures have different ownership and are never committed here.
bool finishScenePanelEdit(SceneDocument& document);
void drawSceneOutliner(SceneDocument& document, AssetLibrary& assets, bool allowEdits = true);
void drawSceneInspector(SceneDocument& document, AssetLibrary& assets, Renderer& renderer, bool allowEdits = true,
                        std::uint64_t* playParticle = nullptr,std::string* openParticle = nullptr,QuaderBridge* quader=nullptr,
                        std::string* openMaterial=nullptr,const NavigationInspectorContext& navigation={});
} // namespace crimson
