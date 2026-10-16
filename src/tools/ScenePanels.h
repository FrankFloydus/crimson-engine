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
// Ctrl-selected component rows own atomic component deletion; entity rows
// retain ordinary selection/deletion. The Inspector shows the primary row.
bool scenePanelComponentFocused(SceneDocument& document);
void clearScenePanelComponentSelection(SceneDocument& document);
bool deleteScenePanelSelection(SceneDocument& document);
void drawSceneOutliner(SceneDocument& document, AssetLibrary& assets, bool allowEdits = true);
void drawSceneInspector(SceneDocument& document, AssetLibrary& assets, Renderer& renderer, bool allowEdits = true,
                        std::uint64_t* playParticle = nullptr,std::string* openParticle = nullptr,QuaderBridge* quader=nullptr,
                        std::string* openMaterial=nullptr);
} // namespace crimson
