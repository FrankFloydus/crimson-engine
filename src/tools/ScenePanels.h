#pragma once
#include <cstdint>
#include <string>

namespace crimson {
class SceneDocument;
class AssetLibrary;
class Renderer;
class QuaderBridge;

// Explicit menu actions may finish an owned panel edit before querying history.
// Viewport gestures have different ownership and are never committed here.
bool finishScenePanelEdit(SceneDocument& document);
void drawSceneOutliner(SceneDocument& document, bool allowEdits = true);
void drawSceneInspector(SceneDocument& document, AssetLibrary& assets, Renderer& renderer, bool allowEdits = true,
                        std::uint64_t* playParticle = nullptr,std::string* openParticle = nullptr,QuaderBridge* quader=nullptr,
                        std::string* openMaterial=nullptr);
} // namespace crimson
