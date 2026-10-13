#pragma once

namespace crimson {
class SceneDocument;
class AssetLibrary;
class Renderer;

void drawSceneOutliner(SceneDocument& document, bool allowEdits = true);
void drawSceneInspector(SceneDocument& document, AssetLibrary& assets, Renderer& renderer, bool allowEdits = true);
void drawSceneSettings(SceneDocument& document, AssetLibrary& assets, Renderer& renderer, bool allowEdits = true);
} // namespace crimson
